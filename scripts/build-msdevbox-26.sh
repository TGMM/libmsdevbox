#!/bin/bash
# Rebuild libmsdevbox on 26.04 and install it (library + header + cmake config).
set -e
export PATH="/root/.cargo/bin:$PATH"
cd /root/libmsdevbox-e2e
rm -rf target
cargo build --release > /tmp/msdevbox26-build.log 2>&1
cargo test --release >> /tmp/msdevbox26-build.log 2>&1 || true
bash scripts/install.sh /usr/local >> /tmp/msdevbox26-build.log 2>&1
ldconfig
echo "=== installed ==="
ls -la /usr/local/lib/libmsdevbox.so /usr/local/include/msdevbox.h /usr/local/lib/cmake/MsDevBox/ 2>&1
echo "=== MSDEVBOX26 OK ==="
