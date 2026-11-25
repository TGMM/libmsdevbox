#!/bin/bash
# Install the KF6/Qt6 dev stack KRDC needs (26.04 package names).
set -e
export DEBIAN_FRONTEND=noninteractive

BASE="extra-cmake-modules qtkeychain-qt6-dev \
libkf6config-dev libkf6kcmutils-dev libkf6dnssd-dev \
libkf6notifications-dev libkf6notifyconfig-dev libkf6bookmarks-dev \
libkf6iconthemes-dev libkf6xmlgui-dev libkf6completion-dev \
libkf6widgetsaddons-dev libkf6windowsystem-dev libkf6i18n-dev \
libkf6crash-dev libkf6guiaddons-dev libkf6kio-dev libkf6solid-dev \
libkscreen-dev libssh-dev shared-mime-info qt6-wayland-dev"

# names that vary; add whichever have an installation candidate
for extra in libkf6bookmarkswidgets-dev libkf6kiowidgets-dev \
    libkf6statusnotifieritem-dev; do
    if apt-cache policy "$extra" 2>/dev/null | grep -q "Candidate: [0-9]"; then
        BASE="$BASE $extra"
    fi
done
echo "installing: $BASE"
apt install -y $BASE > /tmp/kf6-install.log 2>&1
echo "=== KF6 STACK OK ==="
