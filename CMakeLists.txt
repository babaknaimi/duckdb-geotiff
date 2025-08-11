cmake_minimum_required(VERSION 3.16)
project(geotiff)

# 0) Ensure we are being built from DuckDB (so the macros exist)
if(NOT COMMAND build_loadable_extension)
  message(FATAL_ERROR "DuckDB build macros not loaded. Configure DuckDB with DUCKDB_EXTENSION_CONFIGS=<path>/extension_config.cmake")
endif()

# 1) Let CI's tidy job configure without GDAL (no hard failure)
option(GEOTIFF_ENABLE_GDAL "Build geotiff extension (requires GDAL)" ON)

# Try to find GDAL unless the caller turned it off explicitly
if(GEOTIFF_ENABLE_GDAL)
  find_package(GDAL CONFIG QUIET)
endif()

# If GDAL isn't available, skip building the targets (tidy/format still succeed)
if(NOT GEOTIFF_ENABLE_GDAL OR NOT GDAL_FOUND)
  message(STATUS "GDAL not found or disabled - skipping geotiff targets (e.g., tidy mode).")
  return()
endif()

# 2) Sources & headers
set(EXT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
set(TARGET_NAME geotiff)
set(EXTENSION_SOURCES
  "${EXT_ROOT}/src/geotiff.cpp"
  # Uncomment if you also want the static-in-CLI flavor:
  # "${EXT_ROOT}/src/geotiff_extension.cpp"
)
include_directories("${EXT_ROOT}/src/include")

# 3) Build targets (loadable always; static only if EXTENSION_STATIC_BUILD=ON)
build_loadable_extension(${TARGET_NAME} "" ${EXTENSION_SOURCES})
if(EXTENSION_STATIC_BUILD)
  build_static_extension(${TARGET_NAME} ${EXTENSION_SOURCES})
endif()

# 4) Link GDAL
if(TARGET ${TARGET_NAME}_loadable_extension)
  target_link_libraries(${TARGET_NAME}_loadable_extension GDAL::GDAL)
endif()
if(TARGET ${TARGET_NAME}_extension) # present only with EXTENSION_STATIC_BUILD
  target_link_libraries(${TARGET_NAME}_extension GDAL::GDAL)
endif()

# 5) RPATH so the loadable finds GDAL next to itself (packaging scripts copy the dylibs/so’s)
if(APPLE AND TARGET ${TARGET_NAME}_loadable_extension)
  set_target_properties(${TARGET_NAME}_loadable_extension PROPERTIES
    BUILD_RPATH "@loader_path" INSTALL_RPATH "@loader_path")
elseif(UNIX AND TARGET ${TARGET_NAME}_loadable_extension)
  set_target_properties(${TARGET_NAME}_loadable_extension PROPERTIES
    BUILD_RPATH "\$ORIGIN" INSTALL_RPATH "\$ORIGIN")
endif()
