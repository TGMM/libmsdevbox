#!/bin/bash
# Build libmsdevbox in WSL (uses the Windows checkout via /mnt/c)
set -e
rm -rf /root/libmsdevbox-build
cp -r /mnt/c/Users/TGMM/Documents/Projects/libmsdevbox /root/libmsdevbox-build
cd /root/libmsdevbox-build
rm -rf build
cmake -B build -DCMAKE_BUILD_TYPE=Release . > /tmp/msdevbox-cmake.log 2>&1 || { cat /tmp/msdevbox-cmake.log; exit 1; }
cmake --build build --parallel "$(nproc)" > /tmp/msdevbox-build.log 2>&1 || { cat /tmp/msdevbox-build.log; exit 1; }
echo "=== BUILD OK ==="
ls -la build/msdevbox build/libMsDevBox.so* 2>/dev/null

