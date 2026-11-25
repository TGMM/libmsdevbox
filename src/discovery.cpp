/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Dev Box discovery via the Windows 365 EndUser API and RDWeb feeddiscovery.
 */
#include "msdevbox/discovery.h"
#include "msdevbox/constants.h"

#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QTimer>
#include <QUuid>

using namespace MsDevBox;

namespace {

QNetworkReply *getJson(QNetworkAccessManager &nam, const QUrl &url, const QString &bearer,
                       const QList<QPair<QString, QString>> &extraHeaders)
{
    QNetworkRequest request(url);
    if (!bearer.isEmpty()) {
        request.setRawHeader(QByteArrayLiteral("Authorization"),
                             (QStringLiteral("Bearer ") + bearer).toUtf8());
    }
    for (const auto &header : extraHeaders) {
        request.setRawHeader(header.first.toUtf8(), header.second.toUtf8());
    }
    request.setRawHeader(QByteArrayLiteral("Accept"), QByteArrayLiteral("application/json"));
    /* RDWeb endpoints reject requests without the approved user agent header. */
    request.setRawHeader(QByteArrayLiteral(Constants::RdWebUserAgentHeader),
                         QByteArrayLiteral(Constants::RdWebUserAgent));
    return nam.get(request);
}

bool waitForReply(QNetworkReply *reply, int timeoutMs = 30000)
{
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QTimer::singleShot(timeoutMs, &loop, &QEventLoop::quit);
    loop.exec();
    return reply->isFinished();
}

QByteArray httpGet(QNetworkAccessManager &nam, const QUrl &url, const QString &bearer,
                   const QList<QPair<QString, QString>> &extraHeaders, QString *error)
{
    QNetworkReply *reply = getJson(nam, url, bearer, extraHeaders);
    if (!waitForReply(reply)) {
        reply->deleteLater();
        if (error) {
            *error = QStringLiteral("request timed out");
        }
        return {};
    }
    const QByteArray body = reply->readAll();
    if (reply->error() != QNetworkReply::NoError) {
        reply->deleteLater();
        if (error) {
            *error = QStringLiteral("HTTP %1: %2").arg(reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt())
                          .arg(QString::fromUtf8(body.left(200)));
        }
        return {};
    }
    reply->deleteLater();
    return body;
}

DevBoxInfo parseCloudPc(const QJsonObject &obj)
{
    DevBoxInfo info;
    info.id = obj.value(QStringLiteral("id")).toString();
    info.displayName = obj.value(QStringLiteral("displayName")).toString();
    info.workspaceName = obj.value(QStringLiteral("workspaceName")).toString();
    info.partnerId = obj.value(QStringLiteral("partnerId")).toString();
    info.connectionState = obj.value(QStringLiteral("connectionState")).toString();
    info.directLaunchUrl = obj.value(QStringLiteral("directLaunchURL")).toString();
    if (info.directLaunchUrl.isEmpty()) {
        info.directLaunchUrl = obj.value(QStringLiteral("directLaunchUrl")).toString();
    }
    const QJsonObject rdpInfo = obj.value(QStringLiteral("rdpContentInfo")).toObject();
    info.rdpContent = rdpInfo.value(QStringLiteral("rdpContent")).toString();

    info.isDevBox = !info.partnerId.isEmpty()
        && (info.partnerId.compare(QLatin1String(Constants::PartnerDevBox), Qt::CaseInsensitive) == 0
            || info.partnerId.compare(QLatin1String(Constants::PartnerDevBoxV2), Qt::CaseInsensitive) == 0);

    QString tenant;
    QString resource;
    if (!info.directLaunchUrl.isEmpty() && Discovery::parseDirectLaunchUrl(QUrl(info.directLaunchUrl), &tenant, &resource)) {
        info.avdTenant = tenant;
        info.avdResource = resource;
    }
    return info;
}

} // namespace

Discovery::Discovery(QObject *parent)
    : QObject(parent)
{
}

Discovery::~Discovery() = default;

QList<DevBoxInfo> Discovery::listDevBoxes(const QString &endUserAccessToken, bool includeAllCloudPcs, QString *error)
{
    QList<DevBoxInfo> result;
    QNetworkAccessManager nam;
    const QUrl url(QString::fromLatin1(Constants::CloudPcsUrl));
    const QList<QPair<QString, QString>> headers = {
        { QStringLiteral("source-client"), QString::fromLatin1(Constants::EndUserSourceClient) },
        { QStringLiteral("client-version"), QString::fromLatin1(Constants::EndUserClientVersion) },
        { QStringLiteral("client-request-id"), QUuid::createUuid().toString(QUuid::WithoutBraces) },
    };
    const QByteArray body = httpGet(nam, url, endUserAccessToken, headers, error);
    if (body.isEmpty()) {
        return result;
    }
    const auto doc = QJsonDocument::fromJson(body);
    if (!doc.isObject() && !doc.isArray()) {
        if (error) {
            *error = QStringLiteral("EndUser API returned invalid JSON");
        }
        return result;
    }

    /* Live-validated shape: {"Dedicated":[...],"Flex":[],"FrontlineShared":[]}.
     * Some responses may be a flat array; accept both. */
    const auto collect = [&](const QJsonArray &array) {
        for (const auto &value : array) {
            if (!value.isObject()) {
                continue;
            }
            const DevBoxInfo info = parseCloudPc(value.toObject());
            if (info.isDevBox || includeAllCloudPcs) {
                result.append(info);
            }
        }
    };
    if (doc.isArray()) {
        collect(doc.array());
    } else {
        const auto obj = doc.object();
        for (auto it = obj.begin(); it != obj.end(); ++it) {
            if (it.value().isArray()) {
                collect(it.value().toArray());
            }
        }
    }
    return result;
}

bool Discovery::parseDirectLaunchUrl(const QUrl &url, QString *avdTenant, QString *avdResource)
{
    const QString text = url.toString();
    static const QRegularExpression tenantRe(QStringLiteral("/tenants/([0-9a-fA-F-]{36})"));
    static const QRegularExpression resourceRe(QStringLiteral("/resources/([0-9a-fA-F-]{36})"));
    const auto tenantMatch = tenantRe.match(text);
    const auto resourceMatch = resourceRe.match(text);
    if (!tenantMatch.hasMatch() || !resourceMatch.hasMatch()) {
        return false;
    }
    if (avdTenant) {
        *avdTenant = tenantMatch.captured(1);
    }
    if (avdResource) {
        *avdResource = resourceMatch.captured(1);
    }
    return true;
}

QByteArray Discovery::fetchRdpFile(const QString &rdWebAccessToken, const QUrl &directLaunchUrl, QString *error)
{
    QString tenant;
    QString resource;
    if (!parseDirectLaunchUrl(directLaunchUrl, &tenant, &resource)) {
        if (error) {
            *error = QStringLiteral("directLaunchURL does not contain tenants/resources GUIDs: %1").arg(directLaunchUrl.toString());
        }
        return {};
    }
    return fetchRdpFile(rdWebAccessToken, tenant, resource, error);
}

QByteArray Discovery::fetchRdpFile(const QString &rdWebAccessToken, const QString &avdTenant, const QString &avdResource, QString *error)
{
    QNetworkAccessManager nam;

    /* feeddiscovery: tenants/<t>/app/<r> — the .rdp properties live here */
    const QUrl discUrl(QString::fromLatin1(Constants::FeedDiscoveryUrlTemplate).arg(avdTenant, avdResource));
    const QByteArray discBody = httpGet(nam, discUrl, rdWebAccessToken, {}, error);
    if (discBody.isEmpty()) {
        return {};
    }
    const auto doc = QJsonDocument::fromJson(discBody);
    if (!doc.isObject()) {
        if (error) {
            *error = QStringLiteral("feeddiscovery returned invalid JSON");
        }
        return {};
    }
    const auto obj = doc.object();
    QString rdpLink = obj.value(QStringLiteral("RdpLink")).toString();
    if (rdpLink.isEmpty()) {
        rdpLink = obj.value(QStringLiteral("rdpLink")).toString();
    }
    if (rdpLink.isEmpty()) {
        if (error) {
            *error = QStringLiteral("feeddiscovery response contains no RdpLink");
        }
        return {};
    }

    /* RdpLink: signed .rdp behind the ARM gateway, also needs the token */
    const QByteArray rdp = httpGet(nam, QUrl(rdpLink), rdWebAccessToken, {}, error);
    if (rdp.isEmpty()) {
        return {};
    }
    return rdp;
}
