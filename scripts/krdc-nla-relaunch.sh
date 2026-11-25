#!/bin/bash
# Relaunch KRDC with a Dev Box .rdp (the NLA flow: cached gateway token +
# password dialog). Detached via setsid so it survives the launcher exiting.
#   RDP_FILE=~/devbox-krdc.rdp scripts/krdc-nla-relaunch.sh
set -e
: "${RDP_FILE:?set RDP_FILE to the .rdp path}"
pkill krdc 2>/dev/null || true
sleep 1

setsid nohup env \
    XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/mnt/wslg}" \
    DISPLAY="${DISPLAY:-:0}" \
    QT_QPA_PLATFORM=xcb \
    krdc "$RDP_FILE" > /tmp/krdc-run.log 2>&1 < /dev/null &

sleep 10
if pgrep -x krdc > /dev/null; then
    echo "=== KRDC RUNNING (detached) ==="
else
    echo "DIED AT LAUNCH"
    tail -8 /tmp/krdc-run.log
fi
