#!/bin/bash
# Build and install libmsdevbox (Rust) so CMake clients can find it:
#   find_package(MsDevBox 0.1 REQUIRED CONFIG)
#
# Installs:
#   <prefix>/lib/libmsdevbox.so          (cdylib, C ABI)
#   <prefix>/lib/libmsdevbox.a           (staticlib, C ABI)
#   <prefix>/include/msdevbox.h
#   <prefix>/lib/cmake/MsDevBox/MsDevBoxConfig.cmake
set -e

cd "$(dirname "$0")/.."
cargo build --release

PREFIX="${1:-/usr/local}"
LIBDIR="${PREFIX}/lib"
INSTALL=install

$INSTALL -d "$LIBDIR" "$PREFIX/include" "$LIBDIR/cmake/MsDevBox"
$INSTALL -m 644 target/release/libmsdevbox.so "$LIBDIR/libmsdevbox.so.$(cargo pkg-config --version 2>/dev/null || echo 0.1.0)" 2>/dev/null \
  || $INSTALL -m 644 target/release/libmsdevbox.so "$LIBDIR/"
# keep an unversioned name for CMake's find_library
[ -f "$LIBDIR/libmsdevbox.so" ] || cp target/release/libmsdevbox.so "$LIBDIR/libmsdevbox.so"
[ -f target/release/libmsdevbox.a ] && $INSTALL -m 644 target/release/libmsdevbox.a "$LIBDIR/"
$INSTALL -m 644 include/msdevbox.h "$PREFIX/include/"
$INSTALL -m 644 cmake/MsDevBoxConfig.cmake "$LIBDIR/cmake/MsDevBox/"

echo "installed to $PREFIX"
echo "build the CLI tool too? it is at target/release/msdevbox"
