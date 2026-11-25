/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Internal: per-scope token cache on disk (owner-only permissions).
 */
#pragma once

#include <QString>
#include <QJsonObject>

namespace MsDevBox {

class TokenStore
{
public:
    static QString filePath(const QString &scope);
    static bool save(const QString &scope, const class QJsonObject &doc);
    static QJsonObject load(const QString &scope);
    static void remove(const QString &scope);
    static QString dir();
};

} // namespace MsDevBox
