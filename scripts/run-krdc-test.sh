#!/bin/bash
# End-to-end: regenerate the .rdp for a Dev Box and open it in KRDC.
#
#   UPN=you@tenant.com AVD_TENANT=<guid> AVD_RESOURCE=<guid> scripts/run-krdc-test.sh
#
# Runs as the invoking user: the msdevbox token cache (~/.local/share/msdevbox)
# serves and refreshes the RD Web token silently, then KRDC opens the file.
set -e

: "${UPN:?set UPN=your user principal name}"
: "${AVD_TENANT:?set AVD_TENANT (tenants/<guid> from directLaunchURL)}"
: "${AVD_RESOURCE:?set AVD_RESOURCE (resources/<guid> from directLaunchURL)}"

RDP_OUT="${RDP_OUT:-$HOME/devbox-krdc.rdp}"

msdevbox rdp \
    --upn "$UPN" \
    --tenant "$AVD_TENANT" \
    --resource "$AVD_RESOURCE" \
    -o "$RDP_OUT"

echo "=== rdp regenerated (paste-free: silent token refresh) ==="

echo "=== launching KRDC under WSLg ==="
XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/mnt/wslg}" \
    DISPLAY="${DISPLAY:-:0}" \
    QT_QPA_PLATFORM=xcb \
    krdc "$RDP_OUT" &
sleep 5
pgrep -x krdc > /dev/null && echo "=== KRDC LAUNCHED ===" || echo "krdc did not start"
