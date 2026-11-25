//! Per-scope token cache on disk (owner-only permissions).

use serde_json::Value;
use sha2::{Digest, Sha256};
use std::fs;
use std::io::Write;
use std::os::unix::fs::PermissionsExt;
use std::path::PathBuf;

fn base_dir() -> PathBuf {
    let data = std::env::var_os("XDG_DATA_HOME")
        .map(PathBuf::from)
        .unwrap_or_else(|| {
            let home = std::env::var_os("HOME").map(PathBuf::from).unwrap_or_default();
            home.join(".local/share")
        });
    let dir = data.join("msdevbox/tokens");
    let _ = fs::create_dir_all(&dir);
    dir
}

fn scope_file(scope: &str) -> PathBuf {
    let digest = Sha256::digest(scope.as_bytes());
    let hex: String = digest.iter().take(8).map(|b| format!("{b:02x}")).collect();
    base_dir().join(format!("token-{hex}.json"))
}

/// The directory tokens are cached in (for documentation/diagnostics).
pub fn dir() -> PathBuf {
    base_dir()
}

pub fn save(scope: &str, doc: &Value) -> std::io::Result<()> {
    let path = scope_file(scope);
    let mut file = fs::OpenOptions::new()
        .write(true)
        .create(true)
        .truncate(true)
        .open(&path)?;
    let bytes = serde_json::to_vec_pretty(doc).map_err(std::io::Error::other)?;
    file.write_all(&bytes)?;
    file.set_permissions(fs::Permissions::from_mode(0o600))?;
    Ok(())
}

pub fn load(scope: &str) -> Option<Value> {
    let bytes = fs::read(scope_file(scope)).ok()?;
    serde_json::from_slice(&bytes).ok()
}

pub fn remove(scope: &str) {
    let _ = fs::remove_file(scope_file(scope));
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn save_load_roundtrip() {
        // The store uses XDG_DATA_HOME; point it at a temp dir for the test.
        let tmp = std::env::temp_dir().join(format!("msdevbox-test-{}", std::process::id()));
        // Edition 2024: mutating the process environment is unsafe.
        unsafe { std::env::set_var("XDG_DATA_HOME", &tmp) };
        let doc: Value = serde_json::json!({"access_token": "abc", "expires_at": 12345u64});
        save("test-scope", &doc).unwrap();
        let loaded = load("test-scope").unwrap();
        assert_eq!(loaded["access_token"], "abc");
        remove("test-scope");
        assert!(load("test-scope").is_none());
        let _ = fs::remove_dir_all(&tmp);
    }
}
