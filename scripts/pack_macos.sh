#!/usr/bin/env bash
set -euo pipefail

EXT="build/release/extension/geotiff/geotiff.duckdb_extension"
OUTDIR="dist/macos"
mkdir -p "$OUTDIR"
cp "$EXT" "$OUTDIR/"

# vcpkg dynamic libs live here on macOS:
VCPKG_LIB_DIR="$(pwd)/build/release/vcpkg_installed/arm64-osx-dynamic/lib"

# List extension's dylib deps, copy vcpkg ones beside it
otool -L "$EXT" | awk '{print $1}' | while read -r dep; do
  case "$dep" in
    *libgdal*.dylib|*libproj*.dylib|*libgeos*.dylib|*libcurl*.dylib|*libz*.dylib|*libtiff*.dylib|*libjpeg*.dylib|*libpng*.dylib|*libwebp*.dylib|*libexpat*.dylib|*libxml2*.dylib)
      base="$(basename "$dep")"
      if [ -f "$VCPKG_LIB_DIR/$base" ]; then
        cp -n "$VCPKG_LIB_DIR/$base" "$OUTDIR/"
      fi
      ;;
  esac
done

# Also scan the libgdal itself for secondary deps and copy those too
if ls "$OUTDIR"/libgdal*.dylib >/dev/null 2>&1; then
  for g in "$OUTDIR"/libgdal*.dylib; do
    otool -L "$g" | awk '{print $1}' | while read -r dep; do
      base="$(basename "$dep")"
      [ -f "$VCPKG_LIB_DIR/$base" ] && cp -n "$VCPKG_LIB_DIR/$base" "$OUTDIR/" || true
    done
  done
fi

echo "macOS package at: $OUTDIR"
