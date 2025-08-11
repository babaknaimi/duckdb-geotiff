# extension_config.cmake — read by DuckDB's top-level build
duckdb_extension_load(geotiff
  SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR}
  DONT_LINK          # only build the loadable .duckdb_extension
  LOAD_TESTS         # optional
)
