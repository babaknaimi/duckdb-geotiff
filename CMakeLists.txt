cmake_minimum_required(VERSION 3.16)
project(geotiff)

# Ensure we are being built by DuckDB (so the macros exist)
if(NOT COMMAND build_loadable_extension)
  message(FATAL_ERROR "DuckDB build macros not loaded. Configure DuckDB with EXTENSION_CONFIGS or DUCKDB_EXTENSION_CONFIGS.")
endif()

set(EXT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
set(EXTENSION_SOURCES
  "${EXT_ROOT}/src/geotiff.cpp"
  # Uncomment ONLY if you also want the static-in-CLI flavor:
  # "${EXT_ROOT}/src/geotiff_extension.cpp"
)

# Make headers visible without touching targets (avoids 'target not built' errors)
include_directories("${EXT_ROOT}/src/include")

# Create targets FIRST (DuckDB macros define geotiff_loadable_extension, etc.)
build_loadable_extension(geotiff "" ${EXTENSION_SOURCES})
if(EXTENSION_STATIC_BUILD)
  build_static_extension(geotiff ${EXTENSION_SOURCES})
endif()

# Link GDAL via vcpkg or system package
find_package(GDAL CONFIG REQUIRED)
if(TARGET geotiff_loadable_extension)
  target_link_libraries(geotiff_loadable_extension GDAL::GDAL)
endif()
if(TARGET geotiff_extension) # only present when EXTENSION_STATIC_BUILD=ON
  target_link_libraries(geotiff_extension GDAL::GDAL)
endif()

# RPATH so the loadable finds GDAL next to itself
if(APPLE AND TARGET geotiff_loadable_extension)
  set_target_properties(geotiff_loadable_extension PROPERTIES
    BUILD_RPATH "@loader_path" INSTALL_RPATH "@loader_path")
elseif(UNIX AND TARGET geotiff_loadable_extension)
  set_target_properties(geotiff_loadable_extension PROPERTIES
    BUILD_RPATH "\$ORIGIN" INSTALL_RPATH "\$ORIGIN")
endif()
