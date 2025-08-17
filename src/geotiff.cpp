#include "duckdb.hpp"
#include "duckdb/main/extension_util.hpp"
#include "duckdb/common/types/validity_mask.hpp"
#include "geotiff_register.hpp"

#include <algorithm>
#include <limits>
#include <cstring>
#include <vector>

#include "gdal_priv.h"
#include "cpl_conv.h"

using namespace duckdb;

namespace {

struct BindData : public FunctionData {
	std::string path;
	// one or many bands
	std::vector<int> bands; // 1-based GDAL band indices
	idx_t target_mb = 64;   // per-refill read budget (MiB)
	idx_t cache_mb = 0;     // optional: set GDAL global cache (MiB); 0 = don't touch

	unique_ptr<FunctionData> Copy() const override {
		return make_uniq<BindData>(*this);
	}
	bool Equals(const FunctionData &o_p) const override {
		auto &o = o_p.Cast<BindData>();
		return path == o.path && bands == o.bands && target_mb == o.target_mb && cache_mb == o.cache_mb;
	}
};

struct GlobalState : public GlobalTableFunctionState {
	// GDAL handles
	std::unique_ptr<GDALDataset> ds;
	// parsed in Init
	int64_t width = 0, height = 0;
	std::vector<int> bands;       // band numbers (1-based)
	std::vector<int> band_map;    // same as bands, cached as int*
	std::vector<double> nodata;   // per-band nodata
	std::vector<bool> has_nodata; // per-band nodata flag

	// block & buffering
	int bx = 0, by = 0;
	idx_t buf_rows = 0;   // rows per refill
	idx_t buf_pos_px = 0; // position within current buffer in pixels (not counting bands)
	idx_t buf_len_px = 0; // valid pixels currently buffered
	int64_t next_row = 0; // next dataset row to read
	int64_t buf_row0 = 0; // first row contained in current buffer

	std::vector<double> buf; // layout = band-sequential planes:
	// [b0_plane | b1_plane | ...], each plane size = width * rows_read

	idx_t MaxThreads() const override {
		return 1;
	} // single-threaded
};

static idx_t RoundUp(idx_t v, idx_t mul) {
	if (!mul)
		return v;
	auto r = v % mul;
	return r ? (v + (mul - r)) : v;
}

static void ParseBands(const Value &v, std::vector<int> &out) {
	// Accept either a single INTEGER or a LIST of INTEGERs
	if (v.type().id() == LogicalTypeId::INTEGER || v.type().id() == LogicalTypeId::BIGINT ||
	    v.type().id() == LogicalTypeId::SMALLINT || v.type().id() == LogicalTypeId::TINYINT) {
		out.push_back(v.GetValue<int32_t>());
		return;
	}
	if (v.type().id() == LogicalTypeId::LIST) {
		const auto &children = ListValue::GetChildren(v);
		out.reserve(children.size());
		for (auto &c : children)
			out.push_back(c.GetValue<int32_t>());
		return;
	}
	throw BinderException("Parameter 'band' must be an INTEGER or a LIST of INTEGERs");
}

// ---- Bind: parse args & declare schema based on number of bands
static unique_ptr<FunctionData> Bind(ClientContext &, TableFunctionBindInput &input, vector<LogicalType> &types,
                                     vector<string> &names) {

	if (input.inputs.empty())
		throw BinderException("read_geotiff(path, ...) requires a file path");

	auto bd = make_uniq<BindData>();
	bd->path = StringValue::Get(input.inputs[0]);

	// bands: default [1]
	bd->bands.clear();
	auto it = input.named_parameters.find("band");
	if (it != input.named_parameters.end()) {
		ParseBands(it->second, bd->bands);
	} else {
		bd->bands.push_back(1);
	}
	// normalize & sanity
	for (auto &b : bd->bands)
		if (b < 1)
			throw BinderException("band indices must be >= 1");

	// target_mb (optional)
	it = input.named_parameters.find("target_mb");
	if (it != input.named_parameters.end())
		bd->target_mb = (idx_t)it->second.GetValue<int32_t>();

	// cache_mb (optional)
	it = input.named_parameters.find("cache_mb");
	if (it != input.named_parameters.end())
		bd->cache_mb = (idx_t)it->second.GetValue<int32_t>();

	// output schema
	types.clear();
	names.clear();
	types.push_back(LogicalType::BIGINT);
	names.push_back("cell_id");

	if (bd->bands.size() == 1) {
		types.push_back(LogicalType::DOUBLE);
		names.push_back("value");
	} else {
		for (auto b : bd->bands) {
			types.push_back(LogicalType::DOUBLE);
			names.push_back("b" + std::to_string(b)); // e.g. b1, b2, b5
		}
	}
	return std::move(bd);
}

// ---- Init: open dataset, set cache, choose buffer size
static unique_ptr<GlobalTableFunctionState> Init(ClientContext &, TableFunctionInitInput &in) {
	auto &bd = in.bind_data->Cast<BindData>();
	auto st = make_uniq<GlobalState>();

	if (bd.cache_mb > 0) {
		CPLSetConfigOption("GDAL_CACHEMAX", std::to_string(bd.cache_mb).c_str());
	}

	GDALAllRegister();
	GDALDataset *raw = static_cast<GDALDataset *>(GDALOpen(bd.path.c_str(), GA_ReadOnly));
	if (!raw)
		throw IOException("GDALOpen failed for '%s'", bd.path.c_str());
	st->ds.reset(raw);

	st->width = raw->GetRasterXSize();
	st->height = raw->GetRasterYSize();

	// Validate bands against dataset
	const int band_count = raw->GetRasterCount();
	st->bands = bd.bands;
	for (auto b : st->bands) {
		if (b > band_count)
			throw IOException("Requested band %d but file has only %d bands", b, band_count);
	}

	// per-band nodata & natural block size
	st->nodata.resize(st->bands.size(), std::numeric_limits<double>::quiet_NaN());
	st->has_nodata.resize(st->bands.size(), false);

	int bxs = 0, bys = 0;
	for (size_t i = 0; i < st->bands.size(); ++i) {
		auto *rb = raw->GetRasterBand(st->bands[i]);
		int has_nd = 0;
		double nd = rb->GetNoDataValue(&has_nd);
		st->nodata[i] = nd;
		st->has_nodata[i] = (has_nd != 0);
		// use the first band's block size as a hint
		if (i == 0) {
			rb->GetBlockSize(&bxs, &bys);
		}
	}
	st->bx = bxs > 0 ? bxs : (int)st->width;
	st->by = bys > 0 ? bys : 1;

	// choose rows per refill so that (width * rows * bands * 8) ~= target_mb MiB
	const idx_t bytes_budget = (idx_t)bd.target_mb * 1024ull * 1024ull;
	const idx_t denom = (idx_t)st->width * (idx_t)st->bands.size() * (idx_t)sizeof(double);
	idx_t rows = std::max<idx_t>(1, bytes_budget / std::max<idx_t>(denom, 1));
	// align to block height & cap to image height
	rows = RoundUp(rows, (idx_t)std::max(1, st->by));
	rows = std::min<idx_t>(rows, (idx_t)st->height);

	// allocate buffer (one plane per band)
	st->buf_rows = rows;
	const idx_t plane_px = (idx_t)st->width * st->buf_rows;
	const idx_t total_px = plane_px * (idx_t)st->bands.size();
	st->buf.assign((size_t)total_px, 0.0);

	// pre-build band map (GDAL wants int*)
	st->band_map.resize(st->bands.size());
	for (size_t i = 0; i < st->bands.size(); ++i)
		st->band_map[i] = st->bands[i];

	st->buf_pos_px = 0;
	st->buf_len_px = 0;
	st->next_row = 0;
	st->buf_row0 = 0;
	return std::move(st);
}

// ---- Refill: one multi-band RasterIO into band-sequential planes
static void Refill(GlobalState &st) {
	if (st.next_row >= st.height) {
		st.buf_len_px = 0;
		return;
	}
	const int64_t max_rows = (int64_t)st.buf_rows;
	const int64_t rows_to_read = std::min<int64_t>(max_rows, st.height - st.next_row);

	const int nBands = (int)st.bands.size();
	// layout is band-sequential (BSQ): each band plane is width*rows double
	const int nXSize = (int)st.width;
	const int nYSize = (int)rows_to_read;

	// spacing for BSQ
	const GSpacing pixel_space = sizeof(double);
	const GSpacing line_space = (GSpacing)(sizeof(double) * (size_t)st.width);
	const GSpacing band_space = (GSpacing)(sizeof(double) * (size_t)st.width * (size_t)rows_to_read);

	CPLErr err = st.ds->RasterIO(GF_Read,
	                             /*xoff,yoff*/ 0, (int)st.next_row,
	                             /*xsize,ysize*/ nXSize, nYSize,
	                             /*data*/ (void *)st.buf.data(),
	                             /*buf_x,buf_y*/ nXSize, nYSize, GDT_Float64,
	                             /*bands*/ nBands, st.band_map.data(), pixel_space, line_space, band_space, nullptr);

	if (err != CE_None) {
		throw IOException("RasterIO failed at row %lld", (long long)st.next_row);
	}

	st.buf_row0 = st.next_row;
	st.next_row += rows_to_read;
	st.buf_pos_px = 0;
	st.buf_len_px = (idx_t)((int64_t)st.width * rows_to_read); // in pixels (per band)
}

// ---- Scan: emit up to STANDARD_VECTOR_SIZE rows
static void Scan(ClientContext &, TableFunctionInput &in, DataChunk &out) {
	auto &st = in.global_state->Cast<GlobalState>();

	if (st.buf_pos_px >= st.buf_len_px) {
		Refill(st);
		if (st.buf_len_px == 0) {
			out.SetCardinality(0);
			return;
		}
	}

	const idx_t remaining = st.buf_len_px - st.buf_pos_px;
	const idx_t to_emit = std::min<idx_t>(remaining, STANDARD_VECTOR_SIZE);

	auto *id = FlatVector::GetData<int64_t>(out.data[0]);
	const int64_t cell0 = st.buf_row0 * st.width;

	const size_t nBands = st.bands.size();
	const idx_t plane_off = (idx_t)st.width * ((idx_t)st.buf_row0 == st.next_row ? 0 : 0); // kept for clarity

	// Fill cell_id
	for (idx_t i = 0; i < to_emit; i++) {
		id[i] = cell0 + (int64_t)st.buf_pos_px + (int64_t)i;
	}

	if (nBands == 1) {
		// single band fast path
		auto *val = FlatVector::GetData<double>(out.data[1]);
		auto &valid = FlatVector::Validity(out.data[1]);

		if (!st.has_nodata[0]) {
			std::memcpy(val, st.buf.data() + st.buf_pos_px, sizeof(double) * to_emit);
			valid.SetAllValid(to_emit);
		} else {
			valid.SetAllValid(to_emit);
			const double nd = st.nodata[0];
			for (idx_t i = 0; i < to_emit; i++) {
				const double v = st.buf[st.buf_pos_px + i];
				if (v == nd)
					valid.SetInvalid(i);
				else
					val[i] = v;
			}
		}
	} else {
		// multi-band: each output column j reads from its band plane
		const idx_t plane_size = (idx_t)st.width * st.buf_rows; // in pixels
		for (size_t j = 0; j < nBands; ++j) {
			auto *col = FlatVector::GetData<double>(out.data[1 + j]);
			auto &vbit = FlatVector::Validity(out.data[1 + j]);

			const double *src = st.buf.data() + (idx_t)j * plane_size + st.buf_pos_px;

			if (!st.has_nodata[j]) {
				std::memcpy(col, src, sizeof(double) * to_emit);
				vbit.SetAllValid(to_emit);
			} else {
				vbit.SetAllValid(to_emit);
				const double nd = st.nodata[j];
				for (idx_t i = 0; i < to_emit; i++) {
					const double v = src[i];
					if (v == nd)
						vbit.SetInvalid(i);
					else
						col[i] = v;
				}
			}
		}
	}

	st.buf_pos_px += to_emit;
	out.SetCardinality(to_emit);
}

} // namespace

namespace duckdb {
void RegisterGeoTiff(DatabaseInstance &db) {
	TableFunction tf("read_geotiff", {LogicalType::VARCHAR}, Scan, Bind, Init);
	// accept integer OR list for 'band'
	tf.named_parameters["band"] = LogicalType::ANY;
	tf.named_parameters["target_mb"] = LogicalType::INTEGER;
	tf.named_parameters["cache_mb"] = LogicalType::INTEGER; // optional
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
