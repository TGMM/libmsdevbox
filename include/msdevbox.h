/*
 * msdevbox.h — Microsoft Dev Box client library, C API.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Pipeline (live-validated): Entra auth-code + PKCE against the RD Web
 * client identity, W365 EndUser API Dev Box listing, RDWeb ARM
 * feeddiscovery for the signed .rdp.
 *
 * Conventions:
 *   - Fallible functions return NULL / non-zero and record a thread-local
 *     message readable via msdevbox_last_error().
 *   - Returned char* values are owned by the caller; free them with
 *     msdevbox_free_string().
 *   - Handles are opaque; free them with the matching _free function.
 */
#ifndef MSDEVBOX_H
#define MSDEVBOX_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Token scopes (pass to the auth/token functions below). */
#define MSDEVBOX_SCOPE_RDWEB \
    "https://www.wvd.microsoft.com/.default openid profile offline_access"
#define MSDEVBOX_SCOPE_ENDUSER \
    "7c0a6aea-533c-458c-9f81-15568f10f6e4/EndUser.Access openid profile offline_access"

/* Opaque login session (between authorize URL and redirect). */
typedef struct MsDevBoxSession msdevbox_session;

/* Begin an interactive login; navigate to the returned authorize URL
 * (embedded web view or system browser). NULL on failure. */
msdevbox_session *msdevbox_auth_begin(const char *upn, const char *scope);

/* The authorize URL for this session. Valid until the session is freed. */
const char *msdevbox_auth_authorize_url(const msdevbox_session *session);

/* Complete from the redirect URL the web view / user landed on.
 * Validates the state, exchanges the code, caches the tokens.
 * Returns 0 on success, non-zero on failure. */
int msdevbox_auth_complete(msdevbox_session *session, const char *redirect_url);

void msdevbox_session_free(msdevbox_session *session);

/* Cached access token for a scope (refreshed silently when possible).
 * Returns a malloc'd string the caller frees with
 * msdevbox_free_string(), or NULL when a sign-in is needed. */
char *msdevbox_access_token(const char *scope);

/* Remove cached credentials for a scope (sign out). */
void msdevbox_forget_token(const char *scope);

void msdevbox_free_string(char *s);

/* One Dev Box as returned by msdevbox_list_dev_boxes(). All char* members
 * are owned by the array; free with msdevbox_dev_boxes_free(). */
typedef struct msdevbox_info {
    char *id;
    char *display_name;
    char *workspace_name;
    char *connection_state;
    char *direct_launch_url;
    int is_dev_box; /* 1 when the partner marks a Dev Box */
    char *avd_tenant;  /* GUIDs parsed from direct_launch_url, or NULL */
    char *avd_resource;
} msdevbox_info;

/* List Dev Boxes via the Windows 365 EndUser API (EndUser scope token).
 * Stores the array length in *count. The array is owned by the caller;
 * free with msdevbox_dev_boxes_free(). NULL on failure. */
msdevbox_info *msdevbox_list_dev_boxes(const char *access_token,
                                       int include_all_cloud_pcs,
                                       size_t *count);

void msdevbox_dev_boxes_free(msdevbox_info *boxes, size_t count);

/* Fetch the signed .rdp content for tenant/resource GUIDs (RDWeb scope
 * token). Returns a malloc'd NUL-terminated string the caller frees with
 * msdevbox_free_string(), or NULL on failure. */
char *msdevbox_fetch_rdp(const char *access_token,
                         const char *avd_tenant,
                         const char *avd_resource);

/* Write .rdp content to a path (NULL = ~/devbox.rdp). Returns the malloc'd
 * path, or NULL on failure. */
char *msdevbox_rdp_save(const char *rdp_content, const char *out_path);

/* 1 when the .rdp content describes an ARM-gatewayed (Dev Box / W365)
 * connection, 0 otherwise. */
int msdevbox_rdp_is_devbox(const char *rdp_content);

/* The last error recorded on the calling thread (empty string when the
 * last call succeeded). Valid until the next library call. */
const char *msdevbox_last_error(void);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* MSDEVBOX_H */
