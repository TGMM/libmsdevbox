//! C ABI for GUI clients (KRDC, Remmina, anything). See `include/msdevbox.h`.
//!
//! Conventions:
//!  * Fallible calls return `NULL` / non-zero and record a thread-local
//!    message readable via [`msdevbox_last_error`].
//!  * Returned `char*` are owned by the caller and freed with
//!    [`msdevbox_free_string`].
//!  * Handles are opaque; free them with the matching `_free` function.
//!
//! Edition 2024: unsafe attributes are spelled `#[unsafe(no_mangle)]` and
//! unsafe operations inside `unsafe extern "C" fn` bodies are wrapped in
//! explicit `unsafe { }` blocks.

use crate::{auth, discovery, error, rdpfile};
use std::ffi::{c_char, CStr, CString};
use std::ptr;

/// Opaque login session (in flight between authorize URL and redirect).
pub struct MsDevBoxSession {
    session: auth::AuthSession,
    authorize_url: CString,
}

fn to_str<'a>(ptr: *const c_char) -> Result<&'a str, String> {
    if ptr.is_null() {
        return Err("null string argument".to_string());
    }
    unsafe { CStr::from_ptr(ptr) }
        .to_str()
        .map_err(|e| format!("invalid UTF-8 argument: {e}"))
}

fn set_err(msg: &str) {
    error::set_last_error(msg);
}

/// Begin an interactive login. The caller navigates to the authorize URL
/// (web view or browser) and completes with the landing URL.
///
/// # Safety
/// `upn` and `scope` must be valid C strings (or NULL for `upn`).
#[unsafe(no_mangle)]
pub unsafe extern "C" fn msdevbox_auth_begin(upn: *const c_char, scope: *const c_char) -> *mut MsDevBoxSession {
    let upn = match to_str(upn) {
        Ok(s) => s,
        Err(e) => {
            set_err(&e);
            return ptr::null_mut();
        }
    };
    let scope = match to_str(scope) {
        Ok(s) => s,
        Err(e) => {
            set_err(&e);
            return ptr::null_mut();
        }
    };
    let session = auth::begin(upn, scope);
    let authorize_url = match CString::new(session.authorize_url.clone()) {
        Ok(u) => u,
        Err(_) => {
            set_err("authorize URL contains NUL");
            return ptr::null_mut();
        }
    };
    Box::into_raw(Box::new(MsDevBoxSession { session, authorize_url }))
}

/// The authorize URL to navigate to. Valid until the session is freed.
///
/// # Safety
/// `session` must come from [`msdevbox_auth_begin`] and not be freed.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn msdevbox_auth_authorize_url(session: *const MsDevBoxSession) -> *const c_char {
    if session.is_null() {
        return ptr::null();
    }
    unsafe { (*session).authorize_url.as_ptr() }
}

/// Complete the flow from the redirect URL; validates the state, exchanges
/// the code and caches the tokens. Returns 0 on success.
///
/// # Safety
/// `session` must come from [`msdevbox_auth_begin`]; `redirect_url` must be a
/// valid C string.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn msdevbox_auth_complete(session: *mut MsDevBoxSession, redirect_url: *const c_char) -> i32 {
    if session.is_null() {
        set_err("null session");
        return -1;
    }
    let redirect = match to_str(redirect_url) {
        Ok(s) => s,
        Err(e) => {
            set_err(&e);
            return -1;
        }
    };
    let session = unsafe { &(*session).session };
    match auth::complete(session, redirect) {
        Ok(_) => 0,
        Err(e) => {
            set_err(&e);
            -1
        }
    }
}

/// Free a login session.
///
/// # Safety
/// `session` must come from [`msdevbox_auth_begin`] and be freed exactly once.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn msdevbox_session_free(session: *mut MsDevBoxSession) {
    if !session.is_null() {
        drop(unsafe { Box::from_raw(session) });
    }
}

/// Cached access token for a scope (refreshed silently when possible).
/// Returns a malloc'd string the caller frees with
/// [`msdevbox_free_string`], or NULL when a sign-in is needed
/// (see [`msdevbox_last_error`]).
///
/// # Safety
/// `scope` must be a valid C string.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn msdevbox_access_token(scope: *const c_char) -> *mut c_char {
    let scope = match to_str(scope) {
        Ok(s) => s,
        Err(e) => {
            set_err(&e);
            return ptr::null_mut();
        }
    };
    match auth::access_token(scope) {
        Ok(result) => match CString::new(result.access_token) {
            Ok(s) => s.into_raw(),
            Err(_) => {
                set_err("token contains NUL");
                ptr::null_mut()
            }
        },
        Err(e) => {
            set_err(&e);
            ptr::null_mut()
        }
    }
}

/// Free a string returned by this library.
///
/// # Safety
/// `s` must come from this library and be freed exactly once.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn msdevbox_free_string(s: *mut c_char) {
    if !s.is_null() {
        drop(unsafe { CString::from_raw(s) });
    }
}

/// Remove cached credentials for a scope (sign out).
///
/// # Safety
/// `scope` must be a valid C string.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn msdevbox_forget_token(scope: *const c_char) {
    if let Ok(scope) = to_str(scope) {
        auth::forget(scope);
    }
}

/// One Dev Box as returned by [`msdevbox_list_dev_boxes`]. All `char*`
/// members are owned by the array; free with [`msdevbox_dev_boxes_free`].
#[repr(C)]
pub struct MsDevBoxInfo {
    pub id: *mut c_char,
    pub display_name: *mut c_char,
    pub workspace_name: *mut c_char,
    pub connection_state: *mut c_char,
    pub direct_launch_url: *mut c_char,
    pub is_dev_box: i32,
    pub avd_tenant: *mut c_char,
    pub avd_resource: *mut c_char,
}

fn into_c(s: String) -> *mut c_char {
    // A non-UTF8-encodable path would be a bug in our own parsing; default to
    // an empty string rather than dropping the entry.
    CString::new(s).map(|c| c.into_raw()).unwrap_or_else(|_| ptr::null_mut())
}

/// List Dev Boxes via the EndUser API. Returns an array the caller frees
/// with [`msdevbox_dev_boxes_free`], and stores its length in `*count`.
/// NULL on failure ([`msdevbox_last_error`]).
///
/// # Safety
/// `token` must be a valid C string; `count` must point to writable memory.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn msdevbox_list_dev_boxes(
    token: *const c_char,
    include_all_cloud_pcs: i32,
    count: *mut usize,
) -> *mut MsDevBoxInfo {
    let token = match to_str(token) {
        Ok(s) => s,
        Err(e) => {
            set_err(&e);
            return ptr::null_mut();
        }
    };
    if count.is_null() {
        set_err("count must not be null");
        return ptr::null_mut();
    }
    match discovery::list_dev_boxes(token, include_all_cloud_pcs != 0) {
        Ok(boxes) => {
            let len = boxes.len();
            let mut array: Vec<MsDevBoxInfo> = Vec::with_capacity(len);
            for info in boxes {
                array.push(MsDevBoxInfo {
                    id: into_c(info.id),
                    display_name: into_c(info.display_name),
                    workspace_name: into_c(info.workspace_name),
                    connection_state: into_c(info.connection_state),
                    direct_launch_url: into_c(info.direct_launch_url),
                    is_dev_box: info.is_dev_box as i32,
                    avd_tenant: info.avd_tenant.map(into_c).unwrap_or_else(ptr::null_mut),
                    avd_resource: info.avd_resource.map(into_c).unwrap_or_else(ptr::null_mut),
                });
            }
            unsafe { *count = len };
            if len == 0 {
                return ptr::null_mut();
            }
            let mut array = std::mem::ManuallyDrop::new(array);
            array.as_mut_ptr()
        }
        Err(e) => {
            set_err(&e);
            ptr::null_mut()
        }
    }
}

/// Free a Dev Box array from [`msdevbox_list_dev_boxes`].
///
/// # Safety
/// `boxes`/`count` must be the pointer and length from a successful
/// [`msdevbox_list_dev_boxes`] call.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn msdevbox_dev_boxes_free(boxes: *mut MsDevBoxInfo, count: usize) {
    if boxes.is_null() {
        return;
    }
    unsafe {
        let array = std::slice::from_raw_parts_mut(boxes, count);
        for info in array {
            for field in [
                &mut info.id,
                &mut info.display_name,
                &mut info.workspace_name,
                &mut info.connection_state,
                &mut info.direct_launch_url,
                &mut info.avd_tenant,
                &mut info.avd_resource,
            ] {
                if !field.is_null() {
                    drop(CString::from_raw(*field));
                    *field = ptr::null_mut();
                }
            }
        }
        drop(Box::from_raw(boxes));
    }
}

/// Fetch the signed .rdp content for tenant/resource GUIDs (RDWeb scope
/// token). Returns a malloc'd, NUL-terminated buffer the caller frees with
/// [`msdevbox_free_string`], or NULL on failure.
///
/// # Safety
/// All string arguments must be valid C strings.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn msdevbox_fetch_rdp(
    token: *const c_char,
    avd_tenant: *const c_char,
    avd_resource: *const c_char,
) -> *mut c_char {
    let token = match to_str(token) {
        Ok(s) => s,
        Err(e) => {
            set_err(&e);
            return ptr::null_mut();
        }
    };
    let tenant = match to_str(avd_tenant) {
        Ok(s) => s,
        Err(e) => {
            set_err(&e);
            return ptr::null_mut();
        }
    };
    let resource = match to_str(avd_resource) {
        Ok(s) => s,
        Err(e) => {
            set_err(&e);
            return ptr::null_mut();
        }
    };
    match discovery::fetch_rdp_file(token, tenant, resource) {
        Ok(bytes) => {
            // .rdp is text; NUL-terminate for C without a length out-param.
            let mut with_nul = bytes;
            with_nul.push(0);
            match CString::from_vec_with_nul(with_nul) {
                Ok(s) => s.into_raw(),
                Err(_) => {
                    set_err("rdp content contains NUL");
                    ptr::null_mut()
                }
            }
        }
        Err(e) => {
            set_err(&e);
            ptr::null_mut()
        }
    }
}

/// Write .rdp content to a path (NULL = ~/devbox.rdp). Returns the malloc'd
/// path on success, NULL on failure.
///
/// # Safety
/// `content` must be a valid NUL-terminated string.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn msdevbox_rdp_save(content: *const c_char, out_path: *const c_char) -> *mut c_char {
    let content = match to_str(content) {
        Ok(s) => s,
        Err(e) => {
            set_err(&e);
            return ptr::null_mut();
        }
    };
    let out = if out_path.is_null() {
        None
    } else {
        match to_str(out_path) {
            Ok(s) => Some(s),
            Err(e) => {
                set_err(&e);
                return ptr::null_mut();
            }
        }
    };
    match rdpfile::save(content.as_bytes(), out) {
        Ok(path) => match CString::new(path) {
            Ok(s) => s.into_raw(),
            Err(_) => {
                set_err("path contains NUL");
                ptr::null_mut()
            }
        },
        Err(e) => {
            set_err(&e);
            ptr::null_mut()
        }
    }
}

/// 1 when the .rdp content describes an ARM-gatewayed (Dev Box / W365)
/// connection, 0 otherwise.
///
/// # Safety
/// `content` must be a valid NUL-terminated string.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn msdevbox_rdp_is_devbox(content: *const c_char) -> i32 {
    match to_str(content) {
        Ok(s) => rdpfile::is_devbox_rdp(s.as_bytes()) as i32,
        Err(_) => 0,
    }
}

/// The last error recorded on this thread (empty when the last call
/// succeeded). Valid until the next library call on the same thread.
#[unsafe(no_mangle)]
pub extern "C" fn msdevbox_last_error() -> *const c_char {
    thread_local! {
        static LAST_ERROR_C: std::cell::RefCell<CString> =
            std::cell::RefCell::new(CString::new("").unwrap());
    }
    let message = error::last_error();
    LAST_ERROR_C.with(|slot| {
        *slot.borrow_mut() = CString::new(message).unwrap_or_default();
        slot.borrow().as_ptr()
    })
}
