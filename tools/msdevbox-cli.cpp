/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * msdevbox — command line interface for the Dev Box pipeline.
 *
 *   msdevbox login --upn you@tenant.com [--scope rdweb|enduser]
 *   msdevbox list --upn you@tenant.com [--all]
 *   msdevbox rdp  --upn you@tenant.com [--tenant GUID --resource GUID | --launch URL] [-o FILE]
 *
 * Tokens are never printed. The login flow prints the authorize URL; open it
 * in a browser (or point an embedded web view at it), then paste the FULL
 * landing URL back. GUI integrations intercept the navigation instead.
 */
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QTextStream>

#include "msdevbox/auth.h"
#include "msdevbox/constants.h"
#include "msdevbox/discovery.h"
#include "msdevbox/rdpfile.h"

using namespace MsDevBox;

static QTextStream out(stdout);
static QTextStream err(stderr);

static QString scopeFromName(const QString &name, bool *ok)
{
    *ok = true;
    if (name.compare(QStringLiteral("rdweb"), Qt::CaseInsensitive) == 0) {
        return QString::fromLatin1(Constants::ScopeRdWeb);
    }
    if (name.compare(QStringLiteral("enduser"), Qt::CaseInsensitive) == 0) {
        return QString::fromLatin1(Constants::ScopeEndUser);
    }
    *ok = false;
    return {};
}

static int cmdLogin(const QString &upn, const QString &scopeName)
{
    bool ok = false;
    const QString scope = scopeFromName(scopeName, &ok);
    if (!ok) {
        err << "unknown scope: " << scopeName << Qt::endl;
        return 2;
    }
    const AuthSession session = Auth::begin(upn, scope);
    out << "Open this URL in a browser and sign in:\n\n" << session.authorizeUrl.toString() << "\n\n"
        << "After sign-in you land on a nativeclient page.\n"
        << "Paste the FULL redirect URL here: " << Qt::flush;

    QTextStream stdinStream(stdin);
    const QString pasted = stdinStream.readLine().trimmed();
    if (pasted.isEmpty()) {
        err << "no URL given\n";
        return 1;
    }
    TokenResult result;
    Auth auth;
    if (!auth.completeFromRedirect(session, QUrl(pasted), &result)) {
        err << "login failed: " << result.error << Qt::endl;
        return 1;
    }
    out << "login complete; token cached (never displayed)\n";
    return 0;
}

static int cmdList(const QString &upn, bool all)
{
    const QString scope = QString::fromLatin1(Constants::ScopeEndUser);
    Auth auth;
    TokenResult token = auth.accessToken(scope, upn);
    if (!token.valid) {
        err << "no cached EndUser token; run: msdevbox login --upn " << upn << " --scope enduser\n";
        return 1;
    }
    Discovery discovery;
    QString error;
    const QList<DevBoxInfo> boxes = discovery.listDevBoxes(token.accessToken, all, &error);
    if (!error.isEmpty()) {
        err << "listing failed: " << error << "\n";
        if (error.contains(QLatin1String("401"), Qt::CaseInsensitive)) {
            err << "the EndUser.Access scope may not be grantable to the RD Web client identity\n"
                << "from this tenant; the rdp command (rdweb scope) works regardless\n";
        }
        return 1;
    }
    out << "name\tworkspace\tstate\ttenant/resource\n";
    for (const DevBoxInfo &box : boxes) {
        out << box.displayName << '\t' << box.workspaceName << '\t' << box.connectionState << '\t'
            << box.avdTenant << '/' << box.avdResource << '\n';
    }
    if (boxes.isEmpty()) {
        out << "(no Dev Boxes visible)\n";
    }
    return 0;
}

static int cmdRdp(const QString &upn, const QString &tenant, const QString &resource,
                  const QString &launchUrl, const QString &outPath)
{
    const QString scope = QString::fromLatin1(Constants::ScopeRdWeb);
    Auth auth;
    TokenResult token = auth.accessToken(scope, upn);
    if (!token.valid) {
        err << "no cached RDWeb token; run: msdevbox login --upn " << upn << "\n";
        return 1;
    }
    Discovery discovery;
    QString error;
    QByteArray rdp;
    if (!launchUrl.isEmpty()) {
        rdp = discovery.fetchRdpFile(token.accessToken, QUrl(launchUrl), &error);
    } else if (!tenant.isEmpty() && !resource.isEmpty()) {
        rdp = discovery.fetchRdpFile(token.accessToken, tenant, resource, &error);
    } else {
        err << "need --tenant and --resource, or --launch URL\n";
        return 2;
    }
    if (rdp.isEmpty()) {
        err << "rdp fetch failed: " << error << "\n";
        return 1;
    }
    const RdpFileResult saved = RdpFile::save(rdp, outPath);
    if (!saved.valid) {
        err << "save failed: " << saved.error << "\n";
        return 1;
    }
    out << "saved: " << saved.path << " (" << rdp.size() << " bytes, DevBox routing: "
        << (RdpFile::isDevBoxRdp(rdp) ? "yes" : "no") << ")\n";
    out << "connect with:\n  xfreerdp3 " << saved.path
        << " /gateway:type:arm /sec:aad /cert:tofu /u:" << upn << "\n";
    return 0;
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("msdevbox"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Microsoft Dev Box client (list, login, generate .rdp)"));
    parser.addHelpOption();
    parser.addVersionOption();

    parser.addPositionalArgument(QStringLiteral("command"), QStringLiteral("login | list | rdp"));

    QCommandLineOption upnOpt(QStringList{ QStringLiteral("u"), QStringLiteral("upn") },
                              QStringLiteral("User principal name, e.g. user@tenant.com"),
                              QStringLiteral("upn"));
    QCommandLineOption scopeOpt(QStringLiteral("scope"),
                                 QStringLiteral("rdweb (default) or enduser"),
                                 QStringLiteral("scope"), QStringLiteral("rdweb"));
    QCommandLineOption allOpt(QStringList{ QStringLiteral("a"), QStringLiteral("all") },
                               QStringLiteral("list all Cloud PCs, not just Dev Boxes"));
    QCommandLineOption tenantOpt(QStringLiteral("tenant"),
                                 QStringLiteral("AVD tenant GUID from directLaunchURL"),
                                 QStringLiteral("guid"));
    QCommandLineOption resourceOpt(QStringLiteral("resource"),
                                   QStringLiteral("AVD resource GUID from directLaunchURL"),
                                   QStringLiteral("guid"));
    QCommandLineOption launchOpt(QStringLiteral("launch"),
                                 QStringLiteral("directLaunchURL from the feed"),
                                 QStringLiteral("url"));
    QCommandLineOption outOpt(QStringList{ QStringLiteral("o"), QStringLiteral("out") },
                              QStringLiteral("output .rdp path"), QStringLiteral("path"));

    parser.addOptions({ upnOpt, scopeOpt, allOpt, tenantOpt, resourceOpt, launchOpt, outOpt });
    parser.process(app.arguments());

    const QStringList args = parser.positionalArguments();
    if (args.isEmpty()) {
        parser.showHelp(1);
    }
    const QString command = args.first();
    if (command != QStringLiteral("login") && command != QStringLiteral("list")
        && command != QStringLiteral("rdp")) {
        err << "unknown command: " << command << "\n";
        parser.showHelp(1);
    }

    const QString upn = parser.value(upnOpt);
    if (upn.isEmpty()) {
        err << "--upn is required\n";
        return 2;
    }

    if (command == QStringLiteral("login")) {
        return cmdLogin(upn, parser.value(scopeOpt));
    }
    if (command == QStringLiteral("list")) {
        return cmdList(upn, parser.isSet(allOpt));
    }
    return cmdRdp(upn, parser.value(tenantOpt), parser.value(resourceOpt),
                  parser.value(launchOpt), parser.value(outOpt));
}
