# libmsdevbox

Microsoft Dev Box client library for Linux.

Everything needed to give a Linux RDP client the Dev Box experience the Windows
App has on Windows:

| Component | What it does |
|---|---|
| `MsDevBox::Auth` | Entra ID (Azure AD) interactive login — auth-code + PKCE against the **RD Web client identity** (`a85cf173-…`), the one tenant Conditional Access allows from unmanaged Linux endpoints. Tokens cached only in `0600` files. |
| `MsDevBox::Discovery` | Lists your Dev Boxes via the Windows 365 **EndUser API** (`/u/api/v2/cloudPCs` — Dev Boxes are served as CloudPC-shaped entities), then fetches the signed `.rdp` through **RDWeb ARM feeddiscovery**. |
| `MsDevBox::RdpFile` | Persists the `.rdp`; `isDevBoxRdp()` detects ARM-gatewayed connections (for clients that want to enable advanced routing + optimization). |
| `channels/teamscap` | FreeRDP 3 DVC channel implementing the **Teams VDI 1.0** optimization handshake (`sessioninfo` event) — Teams reports **"AVD Media Optimized"**. |
| `patches/krdc` | KRDC integration patches (see the `devbox-support` branch of your KRDC fork). |
| `tools/msdevbox` | CLI harness: `login`, `list`, `rdp`. |

## CLI quick start

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release .
cmake --build build
./build/msdevbox login --upn you@tenant.com          # RDWeb token (paste the landing URL)
./build/msdevbox rdp  --upn you@tenant.com --tenant <guid> --resource <guid>
xfreerdp3 ~/devbox.rdp /gateway:type:arm /sec:aad /cert:tofu /u:you@tenant.com
```

The login flow prints an authorize URL. Embedded clients (see the KRDC patch)
load that URL in a web view and intercept the navigation to
`https://login.microsoftonline.com/common/oauth2/nativeclient?code=…` —
no copy-paste needed.

## Client integration

The library is Qt 6 (Core + Network) with a plain C++ API:

```cpp
#include <msdevbox/auth.h>
#include <msdevbox/discovery.h>

using namespace MsDevBox;

// 1. login (web view intercepts the redirect; terminal paste also works)
auto session = Auth::begin(upn, Constants::ScopeRdWeb);
webEngineView->load(session.authorizeUrl);
// on urlChanged → if url starts with the nativeclient redirect:
auth->completeFromRedirect(session, url);

// 2. list Dev Boxes
auto endUser = auth->accessToken(Constants::ScopeEndUser, upn);
auto boxes = Discovery{}.listDevBoxes(endUser.accessToken);

// 3. fetch the .rdp for the chosen box
auto rdweb = auth->accessToken(Constants::ScopeRdWeb, upn);
auto rdp = Discovery{}.fetchRdpFile(rdweb.accessToken, box.directLaunchUrl);
RdpFile::save(rdp, path);   // → hand to the RDP client
```

## CMake

```cmake
find_package(MsDevBox 0.1 REQUIRED CONFIG)
target_link_libraries(myclient PRIVATE MsDevBox::MsDevBox)
```

## Research provenance

Every protocol behavior in this library was validated live against a real
Dev Box on 2026-11-24; the full analysis lives in the WindowsApp research
project (`PROTOCOL-CONNECTION.md`, `PROTOCOL-TEAMS-VDI1.md`). Nothing here is
guessed — that is the point.

## License

Apache-2.0 (matches FreeRDP, which the channel code builds against).
