//! Dev Box discovery: Windows 365 EndUser API listing and RDWeb ARM
//! feeddiscovery.

use crate::{
    error, CLOUDPCS_URL, ENDUSER_CLIENT_VERSION, ENDUSER_SOURCE_CLIENT, PARTNER_DEVBOX,
    PARTNER_DEVBOX_V2, RDWEB_USER_AGENT,
};
use serde_json::Value;
use std::time::Duration;

const FEED_DISCOVERY_TEMPLATE: &str =
    "https://rdweb.wvd.microsoft.com/api/arm/feeddiscovery/tenants/{tenant}/app/{resource}";

#[derive(Debug, Clone)]
pub struct DevBoxInfo {
    pub id: String,
    pub display_name: String,
    pub workspace_name: String,
    pub partner_id: String,
    pub connection_state: String,
    pub direct_launch_url: String,
    pub is_dev_box: bool,
    /// GUIDs parsed from `direct_launch_url` (None when absent).
    pub avd_tenant: Option<String>,
    pub avd_resource: Option<String>,
}

fn http_client() -> reqwest::blocking::Client {
    reqwest::blocking::Client::builder()
        .user_agent(crate::APP_USER_AGENT)
        .timeout(Duration::from_secs(30))
        .build()
        .expect("reqwest client")
}

fn bearer(mut request: reqwest::blocking::RequestBuilder, token: &str) -> reqwest::blocking::RequestBuilder {
    request = request.bearer_auth(token);
    // RDWeb endpoints reject requests without the approved user agent header.
    request = request.header("x-ms-user-agent", RDWEB_USER_AGENT);
    request
}

/// Extract the `tenants/<guid>` and `resources/<guid>` pair from a
/// directLaunchURL.
pub fn parse_direct_launch_url(url: &str) -> Option<(String, String)> {
    extract_guid_after(url, "/tenants/").zip(extract_guid_after(url, "/resources/"))
}

fn extract_guid_after(haystack: &str, marker: &str) -> Option<String> {
    let start = haystack.find(marker)? + marker.len();
    let rest = &haystack[start..];
    let end = rest
        .find(|c: char| !(c.is_ascii_hexdigit() || c == '-'))
        .unwrap_or(rest.len());
    let candidate = &rest[..end];
    if candidate.len() == 36 && candidate.as_bytes()[8] == b'-' && candidate.as_bytes()[13] == b'-' {
        Some(candidate.to_string())
    } else {
        None
    }
}

fn parse_cloud_pc(obj: &Value) -> DevBoxInfo {
    let get_str = |key: &str| obj.get(key).and_then(Value::as_str).unwrap_or_default().to_string();
    let partner_id = get_str("partnerId");
    let direct_launch_url = {
        let primary = get_str("directLaunchURL");
        if primary.is_empty() {
            get_str("directLaunchUrl")
        } else {
            primary
        }
    };
    let is_dev_box = (!partner_id.is_empty()
        && partner_id.eq_ignore_ascii_case(PARTNER_DEVBOX))
        || (!partner_id.is_empty() && partner_id.eq_ignore_ascii_case(PARTNER_DEVBOX_V2));
    let (avd_tenant, avd_resource) = match parse_direct_launch_url(&direct_launch_url) {
        Some((tenant, resource)) => (Some(tenant), Some(resource)),
        None => (None, None),
    };
    DevBoxInfo {
        id: get_str("id"),
        display_name: get_str("displayName"),
        workspace_name: get_str("workspaceName"),
        connection_state: get_str("connectionState"),
        direct_launch_url,
        partner_id,
        is_dev_box,
        avd_tenant,
        avd_resource,
    }
}

/// List Dev Boxes via the Windows 365 EndUser API. `token` needs the
/// `SCOPE_ENDUSER` scope. Set `include_all_cloud_pcs` to also return
/// non-DevBox Cloud PCs.
///
/// Live-validated response shape: an object keyed by license category
/// ({"Dedicated":[…],"Flex":[],"FrontlineShared":[]}); a flat array is
/// accepted as well.
pub fn list_dev_boxes(token: &str, include_all_cloud_pcs: bool) -> Result<Vec<DevBoxInfo>, String> {
    let request_id = uuid::Uuid::new_v4().to_string();
    let response = bearer(
        http_client().get(CLOUDPCS_URL),
        token,
    )
    .header("source-client", ENDUSER_SOURCE_CLIENT)
    .header("client-version", ENDUSER_CLIENT_VERSION)
    .header("client-request-id", request_id)
    .header("accept", "application/json")
    .send()
    .map_err(|e| format!("EndUser API request failed: {e}"))?;
    let status = response.status();
    let body = response
        .text()
        .map_err(|e| format!("EndUser API request failed: {e}"))?;
    if !status.is_success() {
        return error::fail(format!("EndUser API returned HTTP {status}: {}", truncate(&body, 300)));
    }

    let json: Value = serde_json::from_str(&body)
        .map_err(|e| format!("EndUser API returned invalid JSON: {e}"))?;
    let mut boxes = Vec::new();
    let mut collect = |array: &Value| {
        if let Some(items) = array.as_array() {
            for item in items {
                if let Some(obj) = item.as_object() {
                    let info = parse_cloud_pc(&Value::Object(obj.clone()));
                    if info.is_dev_box || include_all_cloud_pcs {
                        boxes.push(info);
                    }
                }
            }
        }
    };
    match &json {
        Value::Array(array) => collect(&Value::Array(array.clone())),
        Value::Object(map) => {
            for value in map.values() {
                collect(value);
            }
        }
        _ => return error::fail("EndUser API returned unexpected JSON"),
    }
    Ok(boxes)
}

/// Fetch the signed .rdp content for a Dev Box via RDWeb feeddiscovery.
/// `token` needs the `SCOPE_RDWEB` scope.
pub fn fetch_rdp_file(token: &str, avd_tenant: &str, avd_resource: &str) -> Result<Vec<u8>, String> {
    let discovery_url = FEED_DISCOVERY_TEMPLATE
        .replace("{tenant}", avd_tenant)
        .replace("{resource}", avd_resource);

    let response = bearer(http_client().get(&discovery_url), token)
        .header("accept", "application/json")
        .send()
        .map_err(|e| format!("feeddiscovery request failed: {e}"))?;
    let status = response.status();
    let body = response
        .text()
        .map_err(|e| format!("feeddiscovery request failed: {e}"))?;
    if !status.is_success() {
        return error::fail(format!("feeddiscovery returned HTTP {status}: {}", truncate(&body, 300)));
    }

    let json: Value = serde_json::from_str(&body)
        .map_err(|e| format!("feeddiscovery returned invalid JSON: {e}"))?;
    let rdp_link = json
        .get("RdpLink")
        .or_else(|| json.get("rdpLink"))
        .and_then(Value::as_str)
        .unwrap_or_default();
    if rdp_link.is_empty() {
        return error::fail("feeddiscovery response contains no RdpLink");
    }

    // RdpLink: the signed .rdp behind the ARM gateway, also needs the token.
    let response = bearer(http_client().get(rdp_link), token)
        .send()
        .map_err(|e| format!("RdpLink request failed: {e}"))?;
    let status = response.status();
    let bytes = response
        .bytes()
        .map_err(|e| format!("RdpLink request failed: {e}"))?;
    if !status.is_success() {
        return error::fail(format!("RdpLink request returned HTTP {status}"));
    }
    Ok(bytes.to_vec())
}

/// Fetch the .rdp content starting from a Dev Box `directLaunchURL`.
pub fn fetch_rdp_file_from_launch_url(token: &str, direct_launch_url: &str) -> Result<Vec<u8>, String> {
    let (tenant, resource) = parse_direct_launch_url(direct_launch_url).ok_or_else(|| {
        error::set_last_error("directLaunchURL does not contain tenants/resources GUIDs");
        "directLaunchURL does not contain tenants/resources GUIDs".to_string()
    })?;
    fetch_rdp_file(token, &tenant, &resource)
}

fn truncate(text: &str, max: usize) -> String {
    if text.len() <= max {
        text.to_string()
    } else {
        format!("{}…", &text[..max])
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parse_direct_launch_url_shape() {
        let url = "https://rdweb.wvd.microsoft.com/api/arm/weblaunch/tenants/11111111-1111-1111-1111-111111111111/resources/22222222-2222-2222-2222-222222222222/fid?upn=x";
        let (tenant, resource) = parse_direct_launch_url(url).unwrap();
        assert_eq!(tenant, "11111111-1111-1111-1111-111111111111");
        assert_eq!(resource, "22222222-2222-2222-2222-222222222222");
    }

    #[test]
    fn parse_direct_launch_url_rejects_nonsense() {
        assert!(parse_direct_launch_url("https://example.com/tenants/foo/resources/bar").is_none());
        assert!(parse_direct_launch_url("https://example.com/nothing").is_none());
    }

    #[test]
    fn parse_cloud_pc_marks_devbox_partner() {
        let obj: Value = serde_json::json!({
            "id": "b1",
            "displayName": "QED",
            "partnerId": "e3171dd9-9a5f-e5be-b36c-cc7c4f3f3bcf",
            "directLaunchURL": "https://x/tenants/11111111-1111-1111-1111-111111111111/resources/22222222-2222-2222-2222-222222222222/y"
        });
        let info = parse_cloud_pc(&obj);
        assert!(info.is_dev_box);
        assert_eq!(info.avd_tenant.as_deref(), Some("11111111-1111-1111-1111-111111111111"));
        assert_eq!(info.avd_resource.as_deref(), Some("22222222-2222-2222-2222-222222222222"));
    }
}
