#include "duckdb.hpp"
#include "duckdb/main/extension.hpp"
#include "geotiff_register.hpp"

namespace duckdb {

class GeotiffExtension final : public Extension {
public:
  void Load(DuckDB &db) override {
    RegisterGeoTiff(*db.instance);
  }
  std::string Name() override { return "geotiff"; }
  std::string Version() const override { return DuckDB::LibraryVersion(); }
};

} // namespace duckdb



// #define DUCKDB_EXTENSION_MAIN
// 
// #include "geotiff_extension.hpp"
// #include "duckdb.hpp"
// #include "duckdb/common/exception.hpp"
// #include "duckdb/common/string_util.hpp"
// #include "duckdb/function/scalar_function.hpp"
// #include "duckdb/main/extension_util.hpp"
// #include <duckdb/parser/parsed_data/create_scalar_function_info.hpp>
// 
// // OpenSSL linked through vcpkg
// #include <openssl/opensslv.h>
// 
// namespace duckdb {
// 
// inline void GeotiffScalarFun(DataChunk &args, ExpressionState &state, Vector &result) {
// 	auto &name_vector = args.data[0];
// 	UnaryExecutor::Execute<string_t, string_t>(name_vector, result, args.size(), [&](string_t name) {
// 		return StringVector::AddString(result, "Geotiff " + name.GetString() + " 🐥");
// 	});
// }
// 
// inline void GeotiffOpenSSLVersionScalarFun(DataChunk &args, ExpressionState &state, Vector &result) {
// 	auto &name_vector = args.data[0];
// 	UnaryExecutor::Execute<string_t, string_t>(name_vector, result, args.size(), [&](string_t name) {
// 		return StringVector::AddString(result, "Geotiff " + name.GetString() + ", my linked OpenSSL version is " +
// 		                                           OPENSSL_VERSION_TEXT);
// 	});
// }
// 
// static void LoadInternal(DatabaseInstance &instance) {
// 	// Register a scalar function
// 	auto geotiff_scalar_function = ScalarFunction("geotiff", {LogicalType::VARCHAR}, LogicalType::VARCHAR, GeotiffScalarFun);
// 	ExtensionUtil::RegisterFunction(instance, geotiff_scalar_function);
// 
// 	// Register another scalar function
// 	auto geotiff_openssl_version_scalar_function = ScalarFunction("geotiff_openssl_version", {LogicalType::VARCHAR},
// 	                                                            LogicalType::VARCHAR, GeotiffOpenSSLVersionScalarFun);
// 	ExtensionUtil::RegisterFunction(instance, geotiff_openssl_version_scalar_function);
// }
// 
// void GeotiffExtension::Load(DuckDB &db) {
// 	LoadInternal(*db.instance);
// }
// std::string GeotiffExtension::Name() {
// 	return "geotiff";
// }
// 
// std::string GeotiffExtension::Version() const {
// #ifdef EXT_VERSION_GEOTIFF
// 	return EXT_VERSION_GEOTIFF;
// #else
// 	return "";
// #endif
// }
// 
// } // namespace duckdb
// 
// extern "C" {
// 
// DUCKDB_EXTENSION_API void geotiff_init(duckdb::DatabaseInstance &db) {
// 	duckdb::DuckDB db_wrapper(db);
// 	db_wrapper.LoadExtension<duckdb::GeotiffExtension>();
// }
// 
// DUCKDB_EXTENSION_API const char *geotiff_version() {
// 	return duckdb::DuckDB::LibraryVersion();
// }
// }
// 
// #ifndef DUCKDB_EXTENSION_MAIN
// #error DUCKDB_EXTENSION_MAIN not defined
// #endif
