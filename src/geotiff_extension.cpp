#include "duckdb/main/extension.hpp"
#include "geotiff_register.hpp"

namespace duckdb {
class GeotiffExtension final : public Extension {
public:
	void Load(DuckDB &db) override {
		RegisterGeoTiff(*db.instance);
	}
	std::string Name() override {
		return "geotiff";
	}
	std::string Version() const override {
		return DuckDB::LibraryVersion();
	}
};
} // namespace duckdb
