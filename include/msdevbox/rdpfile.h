/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * .rdp file handling for Dev Box connections.
 */
#pragma once

#include <QObject>
#include <QString>

namespace MsDevBox {

struct RdpFileResult
{
    QString path;
    QByteArray content;
    bool valid = false;
    QString error;
};

class RdpFile : public QObject
{
    Q_OBJECT
public:
    explicit RdpFile(QObject *parent = nullptr);
    ~RdpFile() override;

    /* Persist .rdp content to a file. Defaults to the user's home directory
     * with Constants::defaultRdpFileName() when outPath is empty. */
    static RdpFileResult save(const QByteArray &rdpContent, const QString &outPath = QString());

    /* Heuristic: does this .rdp describe an ARM-gatewayed (W365/DevBox) connection
     * that needs RDSAAD auth + the ARM transport? Used by client integrations
     * to decide whether to load advanced routing settings and the teamscap
     * channel. */
    static bool isDevBoxRdp(const QByteArray &rdpContent);
};

} // namespace MsDevBox
