#!/bin/bash
# Build KRDC (devbox-support branch) against the patched FreeRDP + MsDevBox.
set -e
export PATH="/root/.cargo/bin:$PATH"

cd /root
rm -rf krdc
git clone -b devbox-support https://github.com/TGMM/krdc.git krdc > /tmp/krdc-clone.log 2>&1
cd krdc

cmake -B build \
    -DCMAKE_PREFIX_PATH=/usr/local \
    -DWITH_DEVBOX=ON \
    -DWITH_VNC=OFF \
    -DWITH_SPICE=OFF \
    > /tmp/krdc-cmake.log 2>&1 || { cat /tmp/krdc-cmake.log; exit 1; }
cmake --build build --parallel "$(nproc)" > /tmp/krdc-build.log 2>&1 || { tail -30 /tmp/krdc-build.log; exit 1; }
cmake --install build >> /tmp/krdc-build.log 2>&1

echo "=== installed ==="
which krdc
krdc --version 2>&1 | head -2 || true
echo "=== KRDC BUILD OK ==="
