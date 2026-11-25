/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Dev Box discovery: Windows 365 EndUser API listing and RDWeb feeddiscovery.
 */
#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QUrl>

namespace MsDevBox {

struct DevBoxInfo
{
    QString id;               // CloudPC id
    QString displayName;
    QString workspaceName;
    QString partnerId;        // Constants::PartnerDevBox / PartnerDevBoxV2
    QString connectionState;
    QString directLaunchUrl;  // contains tenants/<guid>/resources/<guid>
    QString rdpContent;       // inline fast path (rdpContentInfo.rdpContent), may be empty
    bool isDevBox = false;

    /* GUIDs parsed from directLaunchUrl (empty when absent) */
    QString avdTenant;
    QString avdResource;
};

class Discovery : public QObject
{
    Q_OBJECT
public:
    explicit Discovery(QObject *parent = nullptr);
    ~Discovery() override;

    /* List Dev Boxes via the Windows 365 EndUser API. Requires an EndUser.Access
     * token (Auth::accessToken(Constants::ScopeEndUser)).
     * When includeAllCloudPcs is set, non-DevBox Cloud PCs are returned too. */
    QList<DevBoxInfo> listDevBoxes(const QString &endUserAccessToken, bool includeAllCloudPcs = false, QString *error = nullptr);

    /* The proven path: fetch the .rdp content for a Dev Box via RDWeb
     * feeddiscovery (needs the RdWeb token, Constants::ScopeRdWeb).
     * Works from just the directLaunchUrl or the explicit tenant/resource GUIDs. */
    QByteArray fetchRdpFile(const QString &rdWebAccessToken, const QString &avdTenant, const QString &avdResource, QString *error = nullptr);
    QByteArray fetchRdpFile(const QString &rdWebAccessToken, const QUrl &directLaunchUrl, QString *error = nullptr);

    /* Parse the directLaunchUrl shape: .../tenants/<guid>/weblaunch/... .../resources/<guid>... */
    static bool parseDirectLaunchUrl(const QUrl &url, QString *avdTenant, QString *avdResource);

Q_SIGNALS:
    void progress(const QString &message);
};

} // namespace MsDevBox
