//! Entra ID (Azure AD) interactive auth: auth-code + PKCE.
//!
//! The authorize URL is meant to be loaded in an embedded web view. The view
//! intercepts the navigation to REDIRECT_URI?code=... and hands the full URL
//! to [`complete`]. A terminal fallback (open the URL in a browser, paste the
//! landing URL) works identically — the CLI does that.

use crate::tokenstore;
use crate::{
    error, AUTHORITY, CLIENT_ID_RDWEB, REDIRECT_URI,
};
use base64::engine::general_purpose::URL_SAFE_NO_PAD;
use base64::Engine;
use rand::RngCore;
use serde_json::{json, Value};
use sha2::{Digest, Sha256};
use std::time::{SystemTime, UNIX_EPOCH};

const TOKEN_PATH: &str = "/oauth2/v2.0/token";
/// Refresh the token this many milliseconds before it actually expires.
const EXPIRY_SAFETY_MS: u64 = 30_000;

#[derive(Debug, Clone)]
pub struct AuthSession {
    pub scope: String,
    pub state: String,
    pub code_verifier: String,
    pub upn: String,
    pub authorize_url: String,
}

#[derive(Debug, Clone)]
pub struct TokenResult {
    pub access_token: String,
    pub refresh_token: Option<String>,
    /// Unix epoch milliseconds, already shortened by the safety margin.
    pub expires_at: u64,
    pub upn: String,
    pub scope: String,
}

fn random_urlsafe(bytes: usize) -> String {
    let mut raw = vec![0u8; bytes];
    rand::thread_rng().fill_bytes(&mut raw);
    URL_SAFE_NO_PAD.encode(raw)
}

fn pkce_challenge(verifier: &str) -> String {
    URL_SAFE_NO_PAD.encode(Sha256::digest(verifier.as_bytes()))
}

fn now_ms() -> u64 {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map(|d| d.as_millis() as u64)
        .unwrap_or(0)
}

fn http_client() -> reqwest::blocking::Client {
    // A per-call client is fine: these are single, short round trips.
    reqwest::blocking::Client::builder()
        .user_agent(concat!("msdevbox/", env!("CARGO_PKG_VERSION")))
        .timeout(std::time::Duration::from_secs(60))
        .build()
        .expect("reqwest client")
}

/// Begin an interactive login for `scope`. The returned session carries the
/// authorize URL the caller must navigate to.
pub fn begin(upn: &str, scope: &str) -> AuthSession {
    let state = random_urlsafe(24);
    let code_verifier = random_urlsafe(32);

    let mut url = url::Url::parse(&format!("{AUTHORITY}/oauth2/v2.0/authorize")).expect("authorize url");
    {
        let mut query = url.query_pairs_mut();
        query.append_pair("client_id", CLIENT_ID_RDWEB);
        query.append_pair("response_type", "code");
        query.append_pair("scope", scope);
        query.append_pair("redirect_uri", REDIRECT_URI);
        if !upn.is_empty() {
            query.append_pair("login_hint", upn);
        }
        query.append_pair("state", &state);
        query.append_pair("code_challenge", &pkce_challenge(&code_verifier));
        query.append_pair("code_challenge_method", "S256");
    }

    AuthSession {
        scope: scope.to_string(),
        state,
        code_verifier,
        upn: upn.to_string(),
        authorize_url: url.to_string(),
    }
}

/// Complete the flow from the redirect URL the web view (or the user) landed
/// on. Validates the state, extracts the code, exchanges it, and caches the
/// tokens.
pub fn complete(session: &AuthSession, redirect_url: &str) -> Result<TokenResult, String> {
    let parsed = url::Url::parse(redirect_url).map_err(|e| format!("invalid redirect URL: {e}"))?;
    let query: Vec<(String, String)> = parsed.query_pairs().map(|(k, v)| (k.to_string(), v.to_string())).collect();
    let get = |key: &str| -> String {
        query
            .iter()
            .find(|(k, _)| k == key)
            .map(|(_, v)| v.to_string())
            .unwrap_or_default()
    };

    let returned_state = get("state");
    if returned_state != session.state {
        return error::fail("state mismatch: the redirect URL belongs to a different login attempt");
    }
    let auth_error = get("error");
    if !auth_error.is_empty() {
        return error::fail(format!("{auth_error}: {}", get("error_description")));
    }
    let code = get("code");
    if code.is_empty() {
        return error::fail("no authorization code in the redirect URL");
    }

    let result = exchange_code(session, &code)?;
    persist(session.scope.as_str(), &result)?;
    Ok(result)
}

fn token_from_response(body: &str, scope: &str, upn: &str) -> Result<TokenResult, String> {
    let obj: Value = serde_json::from_str(body)
        .map_err(|e| format!("token endpoint returned invalid JSON: {e}"))?;
    let access_token = obj
        .get("access_token")
        .and_then(Value::as_str)
        .unwrap_or_default()
        .to_string();
    if access_token.is_empty() {
        let description = obj
            .get("error_description")
            .and_then(Value::as_str)
            .or_else(|| obj.get("error").and_then(Value::as_str))
            .unwrap_or("no access_token in response");
        return error::fail(description);
    }
    let lifetime = obj.get("expires_in").and_then(Value::as_i64).unwrap_or(0);
    Ok(TokenResult {
        access_token,
        refresh_token: obj
            .get("refresh_token")
            .and_then(Value::as_str)
            .map(str::to_string),
        expires_at: now_ms() + lifetime.max(0) as u64 * 1000 - EXPIRY_SAFETY_MS,
        upn: upn.to_string(),
        scope: obj
            .get("scope")
            .and_then(Value::as_str)
            .unwrap_or(scope)
            .to_string(),
    })
}

fn exchange_code(session: &AuthSession, code: &str) -> Result<TokenResult, String> {
    let params = [
        ("client_id", CLIENT_ID_RDWEB),
        ("grant_type", "authorization_code"),
        ("code", code),
        ("scope", session.scope.as_str()),
        ("redirect_uri", REDIRECT_URI),
        ("code_verifier", session.code_verifier.as_str()),
    ];
    let response = http_client()
        .post(format!("{AUTHORITY}{TOKEN_PATH}"))
        .form(&params)
        .send()
        .map_err(|e| format!("token request failed: {e}"))?;
    let status = response.status();
    let body = response.text().map_err(|e| format!("token request failed: {e}"))?;
    if !status.is_success() {
        return error::fail(format!("token endpoint returned HTTP {status}: {}", truncate(&body, 300)));
    }
    token_from_response(&body, &session.scope, &session.upn)
}

fn persist(scope: &str, result: &TokenResult) -> Result<(), String> {
    let mut doc = json!({
        "access_token": result.access_token,
        "expires_at": result.expires_at,
        "upn": result.upn,
        "scope": result.scope,
    });
    if let Some(refresh) = &result.refresh_token {
        doc["refresh_token"] = Value::String(refresh.clone());
    }
    tokenstore::save(scope, &doc).map_err(|e| format!("cannot write token cache: {e}"))
}

/// Cached token for `scope`, refreshing it silently with the stored refresh
/// token when possible. `Err` means: no login yet, run [`begin`] + [`complete`].
pub fn access_token(scope: &str) -> Result<TokenResult, String> {
    let doc = tokenstore::load(scope).ok_or_else(|| {
        error::set_last_error("no cached token: sign in first");
        "no cached token: sign in first".to_string()
    })?;
    let expires_at = doc.get("expires_at").and_then(Value::as_u64).unwrap_or(0);
    let cached = doc
        .get("access_token")
        .and_then(Value::as_str)
        .unwrap_or_default()
        .to_string();
    let upn = doc
        .get("upn")
        .and_then(Value::as_str)
        .unwrap_or_default()
        .to_string();
    let refresh = doc
        .get("refresh_token")
        .and_then(Value::as_str)
        .map(str::to_string);

    if !cached.is_empty() && expires_at > now_ms() {
        return Ok(TokenResult {
            access_token: cached,
            refresh_token: refresh,
            expires_at,
            upn,
            scope: scope.to_string(),
        });
    }

    let refresh_token = refresh.ok_or_else(|| {
        error::set_last_error("token expired and no refresh token stored");
        "token expired and no refresh token stored".to_string()
    })?;
    let refreshed = refresh_tokens(scope, &refresh_token)?;
    // Keep the newest refresh token we have: a fresh one from the response,
    // else the one we just used.
    let kept_refresh = refreshed
        .refresh_token
        .clone()
        .unwrap_or_else(|| refresh_token.clone());
    let updated = json!({
        "access_token": refreshed.access_token,
        "expires_at": refreshed.expires_at,
        "upn": upn,
        "scope": scope,
        "refresh_token": kept_refresh.clone(),
    });
    tokenstore::save(scope, &updated).map_err(|e| format!("cannot write token cache: {e}"))?;
    Ok(TokenResult {
        access_token: refreshed.access_token,
        refresh_token: Some(kept_refresh),
        expires_at: refreshed.expires_at,
        upn,
        scope: scope.to_string(),
    })
}

fn refresh_tokens(scope: &str, refresh_token: &str) -> Result<TokenResult, String> {
    let params = [
        ("client_id", CLIENT_ID_RDWEB),
        ("grant_type", "refresh_token"),
        ("refresh_token", refresh_token),
        ("scope", scope),
    ];
    let response = http_client()
        .post(format!("{AUTHORITY}{TOKEN_PATH}"))
        .form(&params)
        .send()
        .map_err(|e| format!("token refresh failed: {e}"))?;
    let status = response.status();
    let body = response.text().map_err(|e| format!("token refresh failed: {e}"))?;
    if !status.is_success() {
        return error::fail(format!("token refresh returned HTTP {status}: {}", truncate(&body, 300)));
    }
    token_from_response(&body, scope, "")
}

/// Remove cached credentials for a scope ("sign out").
pub fn forget(scope: &str) {
    tokenstore::remove(scope);
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
    fn pkce_verifier_is_urlsafe() {
        let verifier = random_urlsafe(32);
        assert_eq!(verifier.len(), 43);
        assert!(verifier.chars().all(|c| c.is_ascii_alphanumeric() || c == '-' || c == '_'));
    }

    #[test]
    fn pkce_challenge_is_sha256_b64url() {
        // Deterministic: SHA-256 of "test" -> known base64url value.
        assert_eq!(
            pkce_challenge("test"),
            URL_SAFE_NO_PAD.encode(Sha256::digest(b"test"))
        );
    }

    #[test]
    fn authorize_url_shape() {
        let session = begin("user@tenant.com", crate::SCOPE_RDWEB);
        assert!(session.authorize_url.starts_with("https://login.microsoftonline.com/common/oauth2/v2.0/authorize"));
        assert!(session.authorize_url.contains("code_challenge_method=S256"));
        assert!(session.authorize_url.contains("login_hint=user%40tenant.com"));
        assert!(session.authorize_url.contains("response_type=code"));
    }

    #[test]
    fn complete_rejects_wrong_state() {
        let session = begin("u@t.com", crate::SCOPE_RDWEB);
        let redirect = format!("{}?code=abc&state=other", REDIRECT_URI);
        assert!(complete(&session, &redirect).is_err());
    }
}
