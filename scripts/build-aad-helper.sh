#!/bin/bash
# Build freerdp-qt-aad-helper into the existing FreeRDP tree, install it,
# then relaunch KRDC.
set -e
export DEBIAN_FRONTEND=noninteractive
apt install -y -qq qt6-webengine-dev > /tmp/webengine-install.log 2>&1
dpkg -l qt6-webengine-dev 2>/dev/null | grep -c ^ii

cd /root/freerdp3
cmake -B build26 -DCMAKE_BUILD_TYPE=Release -DWITH_AAD=ON -DWITH_WINPR_JSON=ON \
    -DWITH_CLIENT_SDL=OFF -DWITH_QT_AAD_AUTH_HELPER=ON . >> /tmp/freerdp26-cmake.log 2>&1
cmake --build build26 --parallel "$(nproc)" --target freerdp-qt-aad-helper > /tmp/aad-helper-build.log 2>&1 || { tail -15 /tmp/aad-helper-build.log; exit 1; }
cmake --install build26 >> /tmp/aad-helper-build.log 2>&1
echo "=== helper installed ==="
ls -la /usr/local/bin/freerdp-qt-aad-helper
