#!/bin/bash
# Build libmsdevbox from a clean export of the committed tree.
#   TESTDIR=/tmp/libmsdevbox-build scripts/e2e-build.sh
set -e
export PATH="/root/.cargo/bin:$PATH"

SCRIPT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
TESTDIR="${TESTDIR:-/tmp/libmsdevbox-build}"

rm -rf "$TESTDIR"
mkdir -p "$TESTDIR"
git -C "$SCRIPT_DIR" archive HEAD | tar -x -C "$TESTDIR"

cd "$TESTDIR"
echo "=== tree from git HEAD ==="
git -C "$SCRIPT_DIR" log --oneline -1

echo "=== cargo build + test ==="
cargo build --release 2>&1 | tail -2
cargo test --release 2>&1 | grep -E "test result: ok" | head -1

echo "=== C symbols ==="
nm -D target/release/libmsdevbox.so | grep -cE " T msdevbox_"

echo "=== E2E SETUP OK ==="
