# teamscap — Teams VDI 1.0 WebRTC redirector channel for FreeRDP 3

`teamscap` is a FreeRDP dynamic virtual channel (DVC) client plugin that speaks the
Microsoft Teams VDI 1.0 optimization protocol on the
`com.microsoft.rdc.dvc.webrtc.1` channel.

What it does today:

- Registers as the client-side listener for `com.microsoft.rdc.dvc.webrtc.1`
- Receives the VM-side `setVersionInfo` handshake
- Replies with the `sessioninfo` event (byte-exact envelope — the VM-side
  `MsRdcWebRTCSvc` gates it with a raw `strstr`), which flips Teams into
  **"AVD Media Optimized"** mode
- Logs all incoming protocol messages to `/tmp/teamscap-*.log` and `/tmp/teamscap-*.bin`
  for protocol analysis

The media engine (the `RTCPeerConnection` verb set that follows once Teams trusts
the endpoint) is under development — see the `PROTOCOL-TEAMS-VDI1.md` spec in the
research project for the captured verb sequence and the implementation plan.

## Integrating with a FreeRDP 3 build

The channel is built inside the FreeRDP source tree (this matches how FreeRDP
builds its own channels):

```bash
git clone https://github.com/FreeRDP/FreeRDP.git freerdp3
cd freerdp3

# 1. copy the channel into the tree
cp -r <this-repo>/channels/teamscap channels/

# 2. register it in the channel list
#    edit channels/CMakeLists.txt and add:
#        add_subdirectory(teamscap)

# 3. add the channel option to the generated config header
#    edit config.h.in and add the line:
#        #cmakedefine CHANNEL_TEAMSCAP_CLIENT

# 4. add a table row so the client always loads it
#    edit client/common/cmdline.c, find dynChannels[] (FreeRDP 3.33) and add:
#        { "teamscap", FreeRDP_BOOL_UNUSED },
#    (pattern: same style as the existing channel rows)

# 5. build + install
cmake -B build -DWITH_AAD=ON -DWITH_WINPR_JSON=ON .
cmake --build build --parallel
cmake --install build
ldconfig
```

After installation the addin (`libteamscap-client.so`) is found by any FreeRDP
client (xfreerdp3, sdl-freerdp, KRDC with `freerdp_client_add_dynamic_channel`)
that requests the `teamscap` dynamic channel.

## Connecting with optimization enabled

```bash
msdevbox rdp --upn you@tenant.com          # generates the signed .rdp
xfreerdp3 ~/devbox.rdp /gateway:type:arm /sec:aad /cert:tofu /u:you@tenant.com
```

Open Teams inside the Dev Box — the banner should read
**"Teams is AVD Media Optimized"**.

## Wire protocol notes

- VM → endpoint messages are JSON calls: `{"rpcObjectType":..., "rpcName":..., "rpcCallId":N}`
- endpoint → VM messages are events: `{"rpcEventTarget":{"rpcObjectType":...},"rpcEventName":...,"rpcEventArgs":{...}}`
- The `sessioninfo` reply to `setVersionInfo` must contain the byte-exact substring
  `"rpcEventTarget":{"rpcObjectType":"RDWebRTCRedirector"},"rpcEventName":"sessioninfo"`
  — the VM-side service matches it with `strstr` and drops anything else.
- Trailing bytes after incoming JSON are uninitialized VM-side buffer garbage;
  parse to the last `}` and ignore the rest.
