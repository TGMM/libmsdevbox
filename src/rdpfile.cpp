/*
 * SPDX-License-Identifier: Apache-2.0
 */
#include "msdevbox/rdpfile.h"

#include <QDir>
#include <QFile>
#include <QStandardPaths>

using namespace MsDevBox;

RdpFile::RdpFile(QObject *parent)
    : QObject(parent)
{
}

RdpFile::~RdpFile() = default;

RdpFileResult RdpFile::save(const QByteArray &rdpContent, const QString &outPath)
{
    RdpFileResult result;
    QString path = outPath;
    if (path.isEmpty()) {
        path = QDir::homePath() + QStringLiteral("/") + QStringLiteral("devbox.rdp");
    }
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        result.error = QStringLiteral("cannot write %1").arg(path);
        return result;
    }
    if (file.write(rdpContent) != rdpContent.size()) {
        result.error = QStringLiteral("short write to %1").arg(path);
        return result;
    }
    file.close();
    result.path = path;
    result.content = rdpContent;
    result.valid = true;
    return result;
}

bool RdpFile::isDevBoxRdp(const QByteArray &rdpContent)
{
    /* ARM-gatewayed W365/DevBox connections carry these signed routing keys;
     * plain LAN .rdp files never do. */
    const QByteArray normalized = rdpContent.toLower();
    return normalized.contains("armpath:s:") && normalized.contains("resourceprovider:s:arm")
        && normalized.contains("loadbalanceinfo:s:");
}
