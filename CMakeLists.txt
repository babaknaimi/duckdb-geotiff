cmake_minimum_required(VERSION 3.5)

project(geotiff)

set(TARGET_NAME geotiff)
set(EXTENSION_NAME ${TARGET_NAME}_extension)
set(LOADABLE_EXTENSION_NAME ${TARGET_NAME}_loadable_extension)

set(EXTENSION_SOURCES
  src/geotiff.cpp
  src/geotiff_extension.cpp)

include_directories(src/include)

build_static_extension(${TARGET_NAME} ${EXTENSION_SOURCES})
build_loadable_extension(${TARGET_NAME} " " ${EXTENSION_SOURCES})

target_include_directories(${EXTENSION_NAME}          PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/include)
target_include_directories(${LOADABLE_EXTENSION_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/include)

find_package(GDAL CONFIG REQUIRED)
target_link_libraries(${EXTENSION_NAME}          GDAL::GDAL)
target_link_libraries(${LOADABLE_EXTENSION_NAME} GDAL::GDAL)


install(
  TARGETS ${EXTENSION_NAME}
  EXPORT "${DUCKDB_EXPORT_SET}"
  LIBRARY DESTINATION "${INSTALL_LIB_DIR}"
  ARCHIVE DESTINATION "${INSTALL_LIB_DIR}")
