cmake_minimum_required(VERSION 3.16)
project(geotiff)

# Ensure DuckDB's macros are available (otherwise bail early)
if(NOT COMMAND build_loadable_extension)
  message(FATAL_ERROR "DuckDB build macros not loaded. Did you configure via DuckDB with EXTENSION_CONFIGS or DUCKDB_EXTENSION_CONFIGS?")
endif()

set(EXT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
set(EXTENSION_SOURCES
  "${EXT_ROOT}/src/geotiff.cpp"           # your table function + geotiff_init/version
  # If you ALSO want the static-in-CLI variant, uncomment the next line:
  # "${EXT_ROOT}/src/geotiff_extension.cpp"
)

# Directory-scope include path so headers resolve even before targets exist
include_directories("${EXT_ROOT}/src/include")

# Create targets FIRST
# Static only when DuckDB asks for it (keeps export errors away)
if(EXTENSION_STATIC_BUILD)
  build_static_extension(geotiff ${EXTENSION_SOURCES})
endif()
build_loadable_extension(geotiff "" ${EXTENSION_SOURCES})

# Link GDAL (vcpkg or system)
find_package(GDAL CONFIG REQUIRED)
if(TARGET geotiff_loadable_extension)
  target_link_libraries(geotiff_loadable_extension GDAL::GDAL)
endif()
if(TARGET geotiff_extension) # only when static is enabled
  target_link_libraries(geotiff_extension GDAL::GDAL)
endif()

# RPATH so the loadable finds GDAL beside itself
if(APPLE AND TARGET geotiff_loadable_extension)
  set_target_properties(geotiff_loadable_extension PROPERTIES
    BUILD_RPATH "@loader_path" INSTALL_RPATH "@loader_path")
elseif(UNIX AND TARGET geotiff_loadable_extension)
  set_target_properties(geotiff_loadable_extension PROPERTIES
    BUILD_RPATH "\$ORIGIN" INSTALL_RPATH "\$ORIGIN")
endif()
