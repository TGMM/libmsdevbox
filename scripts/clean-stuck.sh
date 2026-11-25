#!/bin/bash
# Clean any stuck helper/xfreerdp processes from the failed attempts.
pkill -f freerdp-qt-aad-helper || true
pkill -f xfreerdp || true
sleep 1
pgrep -a -f "freerdp|aad-helper" || echo "all clean"
