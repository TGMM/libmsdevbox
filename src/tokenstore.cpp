/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Per-scope token cache. Tokens only ever live here — never in logs,
 * never in memory longer than needed, never in chat. Files are 0600.
 */
#include "tokenstore.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QStandardPaths>

namespace MsDevBox {

QString TokenStore::dir()
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
                         + QStringLiteral("/msdevbox/tokens");
    QDir().mkpath(base);
    return base;
}

QString TokenStore::filePath(const QString &scope)
{
    const QByteArray key = QCryptographicHash::hash(scope.toUtf8(), QCryptographicHash::Sha256).toHex();
    return dir() + QStringLiteral("/token-") + QString::fromLatin1(key.left(16)) + QStringLiteral(".json");
}

bool TokenStore::save(const QString &scope, const QJsonObject &doc)
{
    const QString path = filePath(scope);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    const QByteArray data = QJsonDocument(doc).toJson(QJsonDocument::Indented);
    if (file.write(data) != data.size()) {
        file.close();
        QFile::remove(path);
        return false;
    }
    file.close();
    /* owner read/write only */
    QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    return true;
}

QJsonObject TokenStore::load(const QString &scope)
{
    QFile file(filePath(scope));
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    const auto doc = QJsonDocument::fromJson(file.readAll());
    return doc.isObject() ? doc.object() : QJsonObject();
}

void TokenStore::remove(const QString &scope)
{
    QFile::remove(filePath(scope));
}

} // namespace MsDevBox
