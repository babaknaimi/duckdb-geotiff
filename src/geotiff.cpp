#include "duckdb.hpp"
#include "duckdb/main/extension_util.hpp"
#include "duckdb/common/types/validity_mask.hpp"
#include "geotiff_register.hpp"

#include <limits>
#include <cstring>
#include "gdal_priv.h"
#include "cpl_conv.h"

using namespace duckdb;

namespace {

struct BindData : public FunctionData {
  std::string path;
  int band = 1;
  idx_t target_mb = 64;
  idx_t cache_mb  = 0;
  unique_ptr<FunctionData> Copy() const override { return make_uniq<BindData>(*this); }
  bool Equals(const FunctionData &o_p) const override {
    auto &o = o_p.Cast<BindData>();
    return path == o.path && band == o.band && target_mb == o.target_mb && cache_mb == o.cache_mb;
  }
};

struct GlobalState : public GlobalTableFunctionState {
  std::unique_ptr<GDALDataset> ds;
  GDALRasterBand *rb = nullptr;
  
  int64_t width = 0, height = 0;
  bool has_nodata = false;
  double nodata = std::numeric_limits<double>::quiet_NaN();
  
  int bx = 0, by = 0;
  idx_t buf_rows = 0;
  idx_t buf_pos_px = 0;
  idx_t buf_len_px = 0;
  int64_t next_row = 0;
  int64_t buf_row0 = 0;
  std::vector<double> buf;
  
  idx_t MaxThreads() const override { return 1; }
};

static idx_t RoundUp(idx_t v, idx_t mul) {
  if (!mul) return v;
  auto r = v % mul;
  return r ? (v + (mul - r)) : v;
}

static unique_ptr<FunctionData>
  Bind(ClientContext &, TableFunctionBindInput &input,
       vector<LogicalType> &types, vector<string> &names) {
    if (input.inputs.empty()) throw BinderException("read_geotiff(path, ...) requires a file path");
    auto bd = make_uniq<BindData>();
    bd->path = StringValue::Get(input.inputs[0]);
    
    // C++11: split the “if with initializer” into two lines
    auto it = input.named_parameters.find("band");
    if (it != input.named_parameters.end()) {
      bd->band = it->second.GetValue<int32_t>();
    }
    it = input.named_parameters.find("target_mb");
    if (it != input.named_parameters.end()) {
      bd->target_mb = (idx_t)it->second.GetValue<int32_t>();
    }
    it = input.named_parameters.find("cache_mb");
    if (it != input.named_parameters.end()) {
      bd->cache_mb = (idx_t)it->second.GetValue<int32_t>();
    }
    
    if (bd->band < 1) throw BinderException("band must be >= 1");
    
    types = {LogicalType::BIGINT, LogicalType::DOUBLE};
    names = {"cell_id", "value"};
    return std::move(bd);
  }

// ---- Init: open dataset, set cache, size buffer
static unique_ptr<GlobalTableFunctionState>
  Init(ClientContext &, TableFunctionInitInput &in) {
    auto &bd = in.bind_data->Cast<BindData>();   // <-- bd is a reference
    
    auto st = make_uniq<GlobalState>();
    if (bd.cache_mb > 0) {
      CPLSetConfigOption("GDAL_CACHEMAX", std::to_string(bd.cache_mb).c_str());
    }
    
    GDALAllRegister();
    GDALDataset *raw = static_cast<GDALDataset*>(GDALOpen(bd.path.c_str(), GA_ReadOnly));
    if (!raw) throw IOException("GDALOpen failed for '%s'", bd.path.c_str());
    st->ds.reset(raw);
    
    if (bd.band > raw->GetRasterCount()) {       // <-- use '.' not '->'
      throw IOException("Requested band %d but file has only %d",
                        bd.band, raw->GetRasterCount());
    }
    
    st->rb = st->ds->GetRasterBand(bd.band);
    st->width  = raw->GetRasterXSize();
    st->height = raw->GetRasterYSize();
    
    int has_nd = 0;
    st->nodata = st->rb->GetNoDataValue(&has_nd);
    st->has_nodata = has_nd != 0;
    
    st->rb->GetBlockSize(&st->bx, &st->by);
    if (st->bx <= 0) st->bx = (int)st->width;
    
    const idx_t bytes     = (idx_t)bd.target_mb * 1024ull * 1024ull;
    const idx_t px_budget = MaxValue<idx_t>((idx_t)1, bytes / sizeof(double));
    idx_t rows            = MaxValue<idx_t>(st->by ? (idx_t)st->by : 1, px_budget / (idx_t)st->width);
    rows                  = RoundUp(rows, (idx_t)MaxValue(1, st->by));
    rows                  = MinValue<idx_t>(rows, (idx_t)st->height);
    
    st->buf_rows = rows;
    st->buf.assign((size_t)((idx_t)st->width * st->buf_rows), 0.0);
    st->buf_pos_px = 0;
    st->buf_len_px = 0;
    st->next_row   = 0;
    st->buf_row0   = 0;
    return std::move(st);
  }

// ---- refill buffer (fix st.width usage & safer error message)
static void Refill(GlobalState &st) {
  if (st.next_row >= st.height) { st.buf_len_px = 0; return; }
  const int64_t rows_to_read =
    (int64_t)MinValue<idx_t>(st.buf_rows, (idx_t)(st.height - st.next_row));
  
  CPLErr err = st.rb->RasterIO(GF_Read,
                               0, (int)st.next_row,
                               (int)st.width, (int)rows_to_read,
                               st.buf.data(),
                               (int)st.width, (int)rows_to_read,
                               GDT_Float64, 0, 0, nullptr);
  if (err != CE_None) {
    // avoid PRId64 portability hiccups under C++11 toolchain
    throw IOException("RasterIO failed at row %lld", (long long)st.next_row);
  }
  
  st.buf_row0  = st.next_row;
  st.next_row += rows_to_read;
  st.buf_pos_px = 0;
  st.buf_len_px = (idx_t)((int64_t)st.width * rows_to_read);  // <-- use '.' not '->'
}

static void Scan(ClientContext &, TableFunctionInput &in, DataChunk &out) {
  auto &st = in.global_state->Cast<GlobalState>();
  if (st.buf_pos_px >= st.buf_len_px) {
    Refill(st);
    if (st.buf_len_px == 0) { out.SetCardinality(0); return; }
  }
  const idx_t remaining = st.buf_len_px - st.buf_pos_px;
  const idx_t to_emit   = MinValue<idx_t>(remaining, STANDARD_VECTOR_SIZE);
  
  auto *id   = FlatVector::GetData<int64_t>(out.data[0]);
  auto *val  = FlatVector::GetData<double>(out.data[1]);
  auto &valid = FlatVector::Validity(out.data[1]);
  
  const int64_t cell0 = st.buf_row0 * st.width;
  
  if (!st.has_nodata) {
    std::memcpy(val, st.buf.data() + st.buf_pos_px, sizeof(double) * to_emit);
    valid.SetAllValid(to_emit);
    for (idx_t i = 0; i < to_emit; i++) id[i] = cell0 + (int64_t)st.buf_pos_px + (int64_t)i;
  } else {
    valid.SetAllValid(to_emit);
    for (idx_t i = 0; i < to_emit; i++) {
      const idx_t p = st.buf_pos_px + i;
      id[i] = cell0 + (int64_t)p;
      const double v = st.buf[p];
      if (v == st.nodata) valid.SetInvalid(i); else val[i] = v;
    }
  }
  
  st.buf_pos_px += to_emit;
  out.SetCardinality(to_emit);
}

} // namespace

namespace duckdb {
void RegisterGeoTiff(DatabaseInstance &db) {
  TableFunction tf("read_geotiff", {LogicalType::VARCHAR}, Scan, Bind, Init);
  tf.named_parameters["band"]      = LogicalType::INTEGER;
  tf.named_parameters["target_mb"] = LogicalType::INTEGER;
  tf.named_parameters["cache_mb"]  = LogicalType::INTEGER;
  ExtensionUtil::RegisterFunction(db, tf);
}
} // namespace duckdb

extern "C" {
  DUCKDB_EXTENSION_API void geotiff_init(duckdb::DatabaseInstance &db) {
    duckdb::RegisterGeoTiff(db);
  }
  DUCKDB_EXTENSION_API const char *geotiff_version() {
    return duckdb::DuckDB::LibraryVersion();
  }
} // extern "C"


