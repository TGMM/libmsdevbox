# libmsdevbox

Microsoft Dev Box client library for Linux, written in Rust with a plain
**C ABI** (`include/msdevbox.h`) so any client can consume it — KRDC (Qt),
Remmina (GTK), or a standalone frontend.

Everything needed to give a Linux RDP client the Dev Box experience the
Windows App has on Windows:

| Component | What it does |
|---|---|
| `auth` | Entra ID (Azure AD) interactive login — auth-code + PKCE against the **RD Web client identity** (`a85cf173-…`), the one tenant Conditional Access allows from unmanaged Linux endpoints. Tokens cached only in `0600` files, never displayed. |
| `discovery` | Lists your Dev Boxes via the Windows 365 **EndUser API** (`/u/api/v2/cloudPCs` — Dev Boxes are served as CloudPC-shaped entities), then fetches the signed `.rdp` through **RDWeb ARM feeddiscovery** (with the required `x-ms-user-agent` header). |
| `rdpfile` | Persists the `.rdp`; `is_devbox_rdp()` detects ARM-gatewayed connections. |
| `ffi` | The C API in `include/msdevbox.h`. |
| `channels/teamscap` | FreeRDP 3 DVC channel implementing the **Teams VDI 1.0** optimization handshake (`sessioninfo` event) — Teams reports **"AVD Media Optimized"**. |
| `msdevbox` (bin) | CLI harness: `login`, `list`, `rdp`. |

A previous Qt implementation is preserved on the `qt-implementation` branch;
main is Rust by design — no GUI framework in the core, TLS via rustls
(no OpenSSL dependency).

## CLI quick start

```bash
cargo build --release
./target/release/msdevbox login --upn you@tenant.com          # paste the landing URL
./target/release/msdevbox rdp  --upn you@tenant.com --tenant <guid> --resource <guid>
xfreerdp3 ~/devbox.rdp /gateway:type:arm /sec:aad /cert:tofu /u:you@tenant.com
```

The login flow prints an authorize URL. Embedded clients load that URL in a
web view and intercept the navigation to
`https://login.microsoftonline.com/common/oauth2/nativeclient?code=…` —
no copy-paste needed.

## C API sketch

```c
#include <msdevbox.h>

/* 1. sign in (web view intercepts the redirect; terminal paste also works) */
msdevbox_session *login = msdevbox_auth_begin(upn, MSDEVBOX_SCOPE_RDWEB);
web_view_load(msdevbox_auth_authorize_url(login));
/* on redirect: */ msdevbox_auth_complete(login, landing_url);
msdevbox_session_free(login);

/* 2. later connections: silent refresh, no prompt */
char *token = msdevbox_access_token(MSDEVBOX_SCOPE_RDWEB);

/* 3. Dev Box list + .rdp generation */
size_t count = 0;
msdevbox_info *boxes = msdevbox_list_dev_boxes(enduser_token, 0, &count);
char *rdp = msdevbox_fetch_rdp_file(token, boxes[i].avd_tenant, boxes[i].avd_resource);
msdevbox_rdp_save(rdp, NULL);
```

## Building and installing for CMake clients

```bash
scripts/install.sh           # or: scripts/install.sh /custom/prefix
# then, in the client:
#   find_package(MsDevBox 0.1 REQUIRED CONFIG)
#   target_link_libraries(myclient PRIVATE MsDevBox::MsDevBox)
```

## License

Apache-2.0.

## Research provenance

Every protocol behavior was validated live against a real Dev Box on
2026-11-24; the full analysis lives in the WindowsApp research project
(`PROTOCOL-CONNECTION.md`, `PROTOCOL-TEAMS-VDI1.md`). Nothing here is
guessed — that is the point.
