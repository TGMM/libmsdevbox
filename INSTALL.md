# Installing the Dev Box client stack on Linux

Everything below was validated end-to-end on Ubuntu 26.04 LTS (including
under WSL2/WSLg). Package names are Debian-family; adjust for other
distros. Sections build on each other in order.

The end state: `krdc devbox-krdc.rdp` → one consent → one password dialog →
connected Dev Box with **"Teams is AVD Media Optimized"**, no URL/token
copy-paste anywhere.

---

## 0. Build dependencies (system)

```bash
sudo apt update
sudo apt install -y build-essential cmake ninja-build git curl \
    libssl-dev libcjson-dev \
    libavutil-dev libavcodec-dev libswscale-dev libswresample-dev \
    libkrb5-dev uuid-dev \
    libpulse-dev libasound2-dev libusb-1.0-0-dev libudev-dev \
    libxcursor-dev libxext-dev libxdamage-dev libxfixes-dev libxi-dev \
    libxrandr-dev libxrender-dev libxv-dev libxtst-dev libxkbfile-dev \
    libxinerama-dev libxkbcommon-dev libxkbcommon-x11-dev \
    libcairo2-dev libjpeg-dev libpng-dev libwebp-dev \
    libegl-dev libgles-dev libsystemd-dev libpam0g-dev libxml2-dev \
    libcups2-dev libpcsclite-dev libfaudio-dev libwayland-dev
```

## 1. libmsdevbox (the Rust library + CLI)

```bash
# Rust toolchain (any recent stable; edition 2024 needs >= 1.85)
curl -sSf https://sh.rustup.rs | sh -s -- -y --profile minimal
source "$HOME/.cargo/env"

git clone https://github.com/TGMM/libmsdevbox.git
cd libmsdevbox
cargo build --release
cargo test --release          # 8 unit tests should pass

sudo bash scripts/install.sh /usr/local   # library + header + CMake config
sudo cp target/release/msdevbox /usr/local/bin/   # the CLI
sudo ldconfig
```

`scripts/install.sh` installs `libmsdevbox.so`, `msdevbox.h` and the
CMake package config so `find_package(MsDevBox 0.1 REQUIRED CONFIG)`
resolves.

## 2. Sign in (once per machine)

```bash
msdevbox login --upn you@tenant.com
```

Open the printed URL in a browser (on WSL: your Windows browser — Windows
Hello works there), sign in, and paste the full landing URL back into the
terminal. This is the **only paste in the whole setup**: the token and its
refresh token are cached in `~/.local/share/msdevbox/tokens` (0600) and
refreshed silently from then on.

## 3. FreeRDP 3.33 with the teamscap channel

```bash
cd ~
git clone https://github.com/FreeRDP/FreeRDP.git freerdp3
cd freerdp3
git checkout 3.33.1

# 1. copy the channel into the tree
cp -r <path-to>/libmsdevbox/channels/teamscap channels/

# 2. register it
#    edit channels/CMakeLists.txt and add:
#        add_subdirectory(teamscap)

# 3. add the channel toggle to the generated config header
#    (this is include/config/config.h.in — not any of the other config.h.in files)
echo '#cmakedefine CHANNEL_TEAMSCAP_CLIENT' >> include/config/config.h.in

# 4. make the client always load it:
#    edit client/common/cmdline.c, find the dynChannels[] table, add a row:
#        { "teamscap", FreeRDP_BOOL_UNUSED },
#    (same style as the existing rows)

cmake -B build -DCMAKE_BUILD_TYPE=Release \
    -DWITH_AAD=ON -DWITH_WINPR_JSON=ON -DWITH_CLIENT_SDL=OFF .
cmake --build build --parallel "$(nproc)"
sudo cmake --install build
sudo ldconfig
xfreerdp3 --version    # sanity check (some distros name it xfreerdp)
```

## 4. KRDC (devbox-support branch)

```bash
sudo apt install -y extra-cmake-modules qt6-base-dev qt6-wayland-dev \
    qtkeychain-qt6-dev libkscreen-dev libssh-dev shared-mime-info \
    libkf6config-dev libkf6kcmutils-dev libkf6dnssd-dev \
    libkf6notifications-dev libkf6notifyconfig-dev libkf6bookmarks-dev \
    libkf6iconthemes-dev libkf6xmlgui-dev libkf6completion-dev \
    libkf6widgetsaddons-dev libkf6windowsystem-dev libkf6i18n-dev \
    libkf6crash-dev libkf6guiaddons-dev libkf6kio-dev libkf6solid-dev \
    libkf6statusnotifieritem-dev

cd ~
git clone -b devbox-support https://github.com/TGMM/krdc.git
cd krdc
cmake -B build -DCMAKE_PREFIX_PATH=/usr/local \
    -DWITH_DEVBOX=ON -DWITH_VNC=OFF -DWITH_SPICE=OFF -DKRDC_BUILD_DOCS=OFF .
cmake --build build --parallel "$(nproc)"
sudo cmake --install build
```

`WITH_DEVBOX=ON` resolves `find_package(MsDevBox 0.1 REQUIRED CONFIG)`
against the library installed in step 1.

## 5. Generate the .rdp

You need your Dev Box's AVD tenant and resource GUIDs. Either:

```bash
# from the listing (if your tenant grants the EndUser scope to the RD Web identity)
msdevbox login --upn you@tenant.com --scope enduser
msdevbox list --upn you@tenant.com        # columns include tenant/resource
```

or extract them from any known-good `.rdp` file for the box:

```bash
grep -E "^workspace id|^remoteapplicationprogram" existing.rdp
# workspace id:s:<TENANT_GUID>
# remoteapplicationprogram:s:||<RESOURCE_GUID>
```

Then:

```bash
msdevbox rdp --upn you@tenant.com \
    --tenant <TENANT_GUID> --resource <RESOURCE_GUID> \
    -o ~/devbox-krdc.rdp
```

The token is served silently from the cache — no prompts. The file only
needs regeneration if the infrastructure moves.

## 6. Connect

```bash
krdc ~/devbox-krdc.rdp
```

What you should see, in order:

1. **"Use Cloud Connection File?"** — KRDC asks once whether to trust the
   file's ARM routing properties (remembered per host)
2. **The login dialog** — your password (NLA host logon; KRDC can remember it)
3. The Dev Box desktop — no browser, no webview, no token prompts

Open Teams and confirm the banner: **"Teams is AVD Media Optimized"**.

### WSL2 specifics

Launching from a `wsl.exe` session tears the GUI down when the session
exits — detach KRDC so it survives:

```bash
setsid nohup env XDG_RUNTIME_DIR=/mnt/wslg DISPLAY=:0 \
    QT_QPA_PLATFORM=xcb krdc ~/devbox-krdc.rdp > /tmp/krdc.log 2>&1 < /dev/null &
```

(On a native Linux desktop, just run `krdc ~/devbox-krdc.rdp`.)

## Troubleshooting

| Symptom | Cause / fix |
|---|---|
| `Could not find a configuration file for package "MsDevBox"` | run `scripts/install.sh /usr/local` again and point `CMAKE_PREFIX_PATH` at the same prefix |
| KRDC build fails on `po-ar-docs…` | you skipped `-DKRDC_BUILD_DOCS=OFF`; some distros ship incomplete kdoctools language customizations |
| FreeRDP build fails at `sdl2-freerdp` | keep `-DWITH_CLIENT_SDL=OFF` |
| "Unable to obtain access token" during connect | the token cache is per-user: sign in (`msdevbox login`) as the same user that runs KRDC |
| Login dialog rejects the password | NLA must be enabled in KRDC's host settings (it is by default) |
| Teams shows "Unable to optimize" | the `teamscap` addin is missing from the libfreerdp3 KRDC links against — recheck steps 3.3/3.4 and rebuild |
