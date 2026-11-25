/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Entra ID auth-code + PKCE flow for the Dev Box pipeline.
 */
#include "msdevbox/auth.h"
#include "msdevbox/constants.h"
#include "tokenstore.h"

#include <QCryptographicHash>
#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRandomGenerator>
#include <QTimer>
#include <QUrlQuery>

using namespace MsDevBox;

namespace {

QByteArray randomUrlSafe(int bytes)
{
    QByteArray raw(bytes, 0);
    for (int i = 0; i < bytes; ++i) {
        raw[i] = static_cast<char>(QRandomGenerator::system()->generate() & 0xff);
    }
    return raw.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
}

QString pkceChallenge(const QString &verifier)
{
    const QByteArray digest = QCryptographicHash::hash(verifier.toUtf8(), QCryptographicHash::Sha256);
    return QString::fromLatin1(digest.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}

QNetworkReply *postForm(QNetworkAccessManager &nam, const QUrl &url, const QList<QPair<QString, QString>> &form)
{
    QUrlQuery query;
    for (const auto &pair : form) {
        query.addQueryItem(pair.first, pair.second);
    }
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/x-www-form-urlencoded"));
    return nam.post(request, query.toString(QUrl::FullyEncoded).toUtf8());
}

bool waitForReply(QNetworkReply *reply, int timeoutMs = 60000)
{
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QTimer::singleShot(timeoutMs, &loop, &QEventLoop::quit);
    loop.exec();
    return reply->isFinished();
}

bool tokenFromJson(const QByteArray &body, const QString &scope, const QString &upn, TokenResult *out)
{
    const auto doc = QJsonDocument::fromJson(body);
    if (!doc.isObject()) {
        if (out) {
            out->error = QStringLiteral("token endpoint returned invalid JSON");
        }
        return false;
    }
    const auto obj = doc.object();
    const QString accessToken = obj.value(QStringLiteral("access_token")).toString();
    if (accessToken.isEmpty()) {
        if (out) {
            out->error = obj.value(QStringLiteral("error_description")).toString();
            if (out->error.isEmpty()) {
                out->error = obj.value(QStringLiteral("error")).toString(QStringLiteral("no access_token in response"));
            }
        }
        return false;
    }
    if (out) {
        out->accessToken = accessToken;
        out->refreshToken = obj.value(QStringLiteral("refresh_token")).toString();
        const qint64 lifetime = obj.value(QStringLiteral("expires_in")).toInteger(0);
        out->expiresAt = QDateTime::currentMSecsSinceEpoch() + lifetime * 1000 - 30000; // 30s safety
        out->upn = upn;
        out->scope = obj.value(QStringLiteral("scope")).toString(scope);
        out->valid = true;
    }
    return true;
}

} // namespace

Auth::Auth(QObject *parent)
    : QObject(parent)
{
}

Auth::~Auth() = default;

QString Auth::tokenDir()
{
    return TokenStore::dir();
}

AuthSession Auth::begin(const QString &upn, const QString &scope)
{
    AuthSession session;
    session.scope = scope;
    session.state = QString::fromLatin1(randomUrlSafe(24));
    session.codeVerifier = QString::fromLatin1(randomUrlSafe(32));
    session.upn = upn;

    QUrl url(QString::fromLatin1(Constants::Authority) + QString::fromLatin1(Constants::AuthorizePath));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("client_id"), QString::fromLatin1(Constants::ClientIdRdWeb));
    query.addQueryItem(QStringLiteral("response_type"), QStringLiteral("code"));
    query.addQueryItem(QStringLiteral("scope"), scope);
    query.addQueryItem(QStringLiteral("redirect_uri"), QString::fromLatin1(Constants::RedirectUri));
    if (!upn.isEmpty()) {
        query.addQueryItem(QStringLiteral("login_hint"), upn);
    }
    query.addQueryItem(QStringLiteral("state"), session.state);
    query.addQueryItem(QStringLiteral("code_challenge"), pkceChallenge(session.codeVerifier));
    query.addQueryItem(QStringLiteral("code_challenge_method"), QStringLiteral("S256"));
    url.setQuery(query);
    session.authorizeUrl = url;
    return session;
}

bool Auth::completeFromRedirect(const AuthSession &session, const QUrl &redirectUrl, TokenResult *out)
{
    TokenResult result;
    if (!exchangeCode(session, redirectUrl, &result)) {
        if (out) {
            *out = result;
        }
        return false;
    }
    /* persist: access/refresh tokens, never displayed */
    QJsonObject doc;
    doc.insert(QStringLiteral("access_token"), result.accessToken);
    if (!result.refreshToken.isEmpty()) {
        doc.insert(QStringLiteral("refresh_token"), result.refreshToken);
    }
    doc.insert(QStringLiteral("expires_at"), result.expiresAt);
    doc.insert(QStringLiteral("upn"), result.upn);
    doc.insert(QStringLiteral("scope"), session.scope);
    TokenStore::save(session.scope, doc);
    if (out) {
        *out = result;
    }
    return true;
}

bool Auth::exchangeCode(const AuthSession &session, const QUrl &redirectUrl, TokenResult *out)
{
    const QUrlQuery query(redirectUrl);
    const QString returnedState = query.queryItemValue(QStringLiteral("state"), QUrl::FullyDecoded);
    if (returnedState != session.state) {
        if (out) {
            out->error = QStringLiteral("state mismatch: the redirect URL belongs to a different login attempt");
        }
        return false;
    }
    const QString error = query.queryItemValue(QStringLiteral("error"), QUrl::FullyDecoded);
    if (!error.isEmpty()) {
        if (out) {
            out->error = error + QStringLiteral(": ")
                + query.queryItemValue(QStringLiteral("error_description"), QUrl::FullyDecoded);
        }
        return false;
    }
    const QString code = query.queryItemValue(QStringLiteral("code"), QUrl::FullyDecoded);
    if (code.isEmpty()) {
        if (out) {
            out->error = QStringLiteral("no authorization code in the redirect URL");
        }
        return false;
    }

    QNetworkAccessManager nam;
    QUrl tokenUrl(QString::fromLatin1(Constants::Authority) + QString::fromLatin1(Constants::TokenPath));
    QNetworkReply *reply = postForm(nam, tokenUrl, {
        { QStringLiteral("client_id"), QString::fromLatin1(Constants::ClientIdRdWeb) },
        { QStringLiteral("grant_type"), QStringLiteral("authorization_code") },
        { QStringLiteral("code"), code },
        { QStringLiteral("scope"), session.scope },
        { QStringLiteral("redirect_uri"), QString::fromLatin1(Constants::RedirectUri) },
        { QStringLiteral("code_verifier"), session.codeVerifier },
    });
    if (!waitForReply(reply)) {
        reply->deleteLater();
        if (out) {
            out->error = QStringLiteral("token endpoint timed out");
        }
        return false;
    }
    const bool ok = tokenFromJson(reply->readAll(), session.scope, session.upn, out);
    reply->deleteLater();
    return ok;
}

TokenResult Auth::accessToken(const QString &scope, const QString &upn)
{
    TokenResult result;
    const QJsonObject doc = TokenStore::load(scope);
    if (doc.isEmpty()) {
        return result;
    }
    const qint64 expiresAt = doc.value(QStringLiteral("expires_at")).toInteger();
    const QString cached = doc.value(QStringLiteral("access_token")).toString();
    const QString refresh = doc.value(QStringLiteral("refresh_token")).toString();
    const QString storedUpn = doc.value(QStringLiteral("upn")).toString();

    if (!cached.isEmpty() && expiresAt > QDateTime::currentMSecsSinceEpoch()) {
        result.accessToken = cached;
        result.expiresAt = expiresAt;
        result.upn = storedUpn;
        result.scope = doc.value(QStringLiteral("scope")).toString(scope);
        result.valid = true;
        return result;
    }
    if (!refresh.isEmpty() && refreshTokens(scope, refresh, &result)) {
        result.upn = storedUpn;
        QJsonObject updated;
        updated.insert(QStringLiteral("access_token"), result.accessToken);
        if (!result.refreshToken.isEmpty()) {
            updated.insert(QStringLiteral("refresh_token"), result.refreshToken);
        } else {
            updated.insert(QStringLiteral("refresh_token"), refresh); // keep the old one
        }
        updated.insert(QStringLiteral("expires_at"), result.expiresAt);
        updated.insert(QStringLiteral("upn"), storedUpn);
        updated.insert(QStringLiteral("scope"), scope);
        TokenStore::save(scope, updated);
        return result;
    }
    return result; // invalid: caller must run an interactive login
}

bool Auth::refreshTokens(const QString &scope, const QString &refreshToken, TokenResult *out)
{
    QNetworkAccessManager nam;
    QUrl tokenUrl(QString::fromLatin1(Constants::Authority) + QString::fromLatin1(Constants::TokenPath));
    QNetworkReply *reply = postForm(nam, tokenUrl, {
        { QStringLiteral("client_id"), QString::fromLatin1(Constants::ClientIdRdWeb) },
        { QStringLiteral("grant_type"), QStringLiteral("refresh_token") },
        { QStringLiteral("refresh_token"), refreshToken },
        { QStringLiteral("scope"), scope },
    });
    if (!waitForReply(reply)) {
        reply->deleteLater();
        return false;
    }
    const bool ok = tokenFromJson(reply->readAll(), scope, QString(), out);
    reply->deleteLater();
    return ok;
}

void Auth::forget(const QString &scope)
{
    TokenStore::remove(scope);
}
