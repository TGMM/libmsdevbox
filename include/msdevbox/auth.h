/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Entra ID (Azure AD) interactive auth for the Dev Box pipeline.
 *
 * Implements the auth-code + PKCE flow against the RD Web client identity
 * (Constants::ClientIdRdWeb), the identity tenant Conditional Access allows
 * from unmanaged Linux endpoints.
 *
 * The library never displays tokens; they are exchanged over HTTPS and cached
 * only in files under the user's data directory with owner-only permissions.
 *
 * The authorize URL is meant to be loaded in an embedded web view (see the
 * KRDC integration): the view intercepts the navigation to
 * https://login.microsoftonline.com/common/oauth2/nativeclient?code=...
 * and hands the full URL to completeFromRedirect(). A terminal fallback
 * (open URL in a browser, paste the landing URL) works identically.
 */
#pragma once

#include <QObject>
#include <QString>
#include <QUrl>

namespace MsDevBox {

struct AuthSession
{
    QString scope;
    QString state;
    QString codeVerifier;
    QString upn;
    QUrl authorizeUrl;
};

struct TokenResult
{
    QString accessToken;
    QString refreshToken;
    qint64 expiresAt = 0; // epoch ms
    QString upn;
    QString scope;
    bool valid = false;
    QString error;
};

class Auth : public QObject
{
    Q_OBJECT
public:
    explicit Auth(QObject *parent = nullptr);
    ~Auth() override;

    /* Begin an interactive login. Returns the session (with authorizeUrl)
     * the caller must navigate to. */
    static AuthSession begin(const QString &upn, const QString &scope);

    /* Complete the flow from the redirect URL the web view / user landed on.
     * Validates the state, extracts the code and exchanges it for tokens.
     * Blocks until the token endpoint replies (short HTTPS round trip). */
    bool completeFromRedirect(const AuthSession &session, const QUrl &redirectUrl, TokenResult *out = nullptr);

    /* Cached token for a scope, refreshing it with the stored refresh token
     * when possible. Returns an invalid TokenResult when no login exists. */
    TokenResult accessToken(const QString &scope, const QString &upn = QString());

    /* Remove cached credentials for a scope (e.g. "sign out"). */
    static void forget(const QString &scope);

    /* Where the token cache lives (per-scope files, 0600). */
    static QString tokenDir();

Q_SIGNALS:
    void progress(const QString &message);

private:
    static bool exchangeCode(const AuthSession &session, const QUrl &redirectUrl, TokenResult *out);
    static bool refreshTokens(const QString &scope, const QString &refreshToken, TokenResult *out);
};

} // namespace MsDevBox
