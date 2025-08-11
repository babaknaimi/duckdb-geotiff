#!/usr/bin/env bash
set -euo pipefail

EXT="build/release/extension/geotiff/geotiff.duckdb_extension"
OUTDIR="dist/linux"
mkdir -p "$OUTDIR"
cp "$EXT" "$OUTDIR/"

VCPKG_LIB_DIR="$(pwd)/build/release/vcpkg_installed/x64-linux-dynamic/lib"

# Ensure patchelf is available
if ! command -v patchelf >/dev/null 2>&1; then
  echo "patchelf not found; please install it (apt-get install patchelf or similar)."
  exit 1
fi

# Copy likely deps (GDAL and friends) from vcpkg lib dir
copy_if_present() {
  for n in "$@"; do
    for f in "$VCPKG_LIB_DIR"/$n; do
      [ -f "$f" ] && cp -n "$f" "$OUTDIR/" || true
    done
  done
}
copy_if_present "libgdal*.so*" "libproj*.so*" "libgeos*.so*" "libcurl*.so*" "libtiff*.so*" "libjpeg*.so*" "libpng*.so*" "libwebp*.so*" "libexpat*.so*" "libxml2*.so*" "libz*.so*"

# Set RPATH on extension and copied libs to $ORIGIN so they find each other
patchelf --set-rpath '$ORIGIN' "$OUTDIR/geotiff.duckdb_extension"
for so in "$OUTDIR"/*.so*; do
  patchelf --set-rpath '$ORIGIN' "$so" || true
done

# Quick check
echo "ldd check:"
ldd "$OUTDIR/geotiff.duckdb_extension" || true
echo "Linux package at: $OUTDIR"
