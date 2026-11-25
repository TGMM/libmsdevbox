//! .rdp file handling for Dev Box connections.

use std::fs;
use std::io::Write;
use std::path::PathBuf;

/// Persist .rdp content. `out_path` defaults to `~/devbox.rdp`.
pub fn save(rdp_content: &[u8], out_path: Option<&str>) -> Result<String, String> {
    let path = match out_path {
        Some(p) => PathBuf::from(p),
        None => std::env::var_os("HOME")
            .map(|home| PathBuf::from(home).join("devbox.rdp"))
            .unwrap_or_else(|| PathBuf::from("devbox.rdp")),
    };
    let mut file =
        fs::File::create(&path).map_err(|e| format!("cannot write {}: {e}", path.display()))?;
    file.write_all(rdp_content)
        .map_err(|e| format!("short write to {}: {e}", path.display()))?;
    Ok(path.display().to_string())
}

/// Does this .rdp describe an ARM-gatewayed (W365/DevBox) connection that
/// needs RDSAAD auth + the ARM transport? Client integrations use this to
/// decide whether to apply advanced routing settings.
pub fn is_devbox_rdp(rdp_content: &[u8]) -> bool {
    let normalized = String::from_utf8_lossy(rdp_content).to_lowercase();
    normalized.contains("armpath:s:")
        && normalized.contains("resourceprovider:s:arm")
        && normalized.contains("loadbalanceinfo:s:")
}
