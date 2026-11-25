//! Microsoft Dev Box client library for Linux.
//!
//! Pipeline (live-validated 2026-11-24):
//!  1. Entra ID auth-code + PKCE against the RD Web client identity — the one
//!     tenant Conditional Access allows from unmanaged Linux endpoints.
//!  2. Dev Box listing via the Windows 365 EndUser API (Dev Boxes are served
//!     as CloudPC-shaped entities).
//!  3. Signed .rdp generation via RDWeb ARM feeddiscovery.
//!
//! Tokens are exchanged over HTTPS and cached only in 0600 files; they are
//! never exposed through the API beyond handing them to the caller.
//!
//! GUI clients consume this through the C ABI in [`ffi`] / `include/msdevbox.h`.

pub mod auth;
pub mod discovery;
pub mod error;
pub mod ffi;
pub mod rdpfile;
pub mod tokenstore;

/// OAuth client identity the tenant allows from unmanaged Linux endpoints.
pub const CLIENT_ID_RDWEB: &str = "a85cf173-4192-42f8-81fa-777a763e6e2c";
/// The Windows 365 Client identity (used with the Windows broker; may be
/// blocked for browser flows on unmanaged devices).
pub const CLIENT_ID_WINDOWS365: &str = "4fb5cc57-dbbc-4cdc-9595-748adff5f414";

pub const AUTHORITY: &str = "https://login.microsoftonline.com/common";
pub const REDIRECT_URI: &str = "https://login.microsoftonline.com/common/oauth2/nativeclient";

/// Token scope for RDWeb feeddiscovery / RdpLink.
pub const SCOPE_RDWEB: &str = "https://www.wvd.microsoft.com/.default openid profile offline_access";
/// Token scope for the Windows 365 EndUser API listing.
pub const SCOPE_ENDUSER: &str = "7c0a6aea-533c-458c-9f81-15568f10f6e4/EndUser.Access openid profile offline_access";

/// EndUser API. Dev Boxes appear as CloudPC entities.
pub const CLOUDPCS_URL: &str = "https://windows365.microsoft.com/u/api/v2/cloudPCs?extend=IsDefault&licenseCategories=Dedicated,Flex,FrontlineShared";

/// REQUIRED on all RDWeb endpoints; without it the service answers
/// 400 INCOMPATIBLE_CLIENT_VERSION.
pub const RDWEB_USER_AGENT: &str = "win365.nxt/3.33.1";
pub const ENDUSER_SOURCE_CLIENT: &str = "NxtClient";
pub const ENDUSER_CLIENT_VERSION: &str = "2.0.1601.0";

/// CloudPC partnerId values that mark a Dev Box.
pub const PARTNER_DEVBOX: &str = "e3171dd9-9a5f-e5be-b36c-cc7c4f3f3bcf";
pub const PARTNER_DEVBOX_V2: &str = "10099236-61c3-44e0-b9a8-c1036643ae26";

const USER_AGENT: &str = concat!("msdevbox/", env!("CARGO_PKG_VERSION"));
