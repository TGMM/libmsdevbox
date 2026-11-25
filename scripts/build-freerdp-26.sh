#!/bin/bash
# Rebuild FreeRDP 3.33 + teamscap on Ubuntu 26.04, fresh build dir.
set -e
cd /root/freerdp3

# restore the channel toggle if the template lost it
if ! grep -q "CHANNEL_TEAMSCAP_CLIENT" include/config/config.h.in; then
    echo '#cmakedefine CHANNEL_TEAMSCAP_CLIENT' >> include/config/config.h.in
    echo "config.h.in: re-added CHANNEL_TEAMSCAP_CLIENT"
else
    echo "config.h.in: patch present"
fi
grep -q "teamscap" client/common/cmdline.c || { echo "FATAL: cmdline.c patch missing"; exit 1; }

rm -rf build26
cmake -B build26 -DCMAKE_BUILD_TYPE=Release -DWITH_AAD=ON -DWITH_WINPR_JSON=ON . > /tmp/freerdp26-cmake.log 2>&1
cmake --build build26 --parallel "$(nproc)" > /tmp/freerdp26-build.log 2>&1
cmake --install build26 >> /tmp/freerdp26-build.log 2>&1
ldconfig
cp /usr/local/bin/xfreerdp /usr/local/bin/xfreerdp3
echo "=== xfreerdp3 smoke test ==="
/usr/local/bin/xfreerdp3 --version 2>&1 | head -2
/usr/local/bin/xfreerdp3 --buildconfig 2>&1 | grep -iE "aad|json" | head -4
echo "=== FREERDP26 OK ==="
