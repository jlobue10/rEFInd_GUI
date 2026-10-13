#include "request.h"

#include "catalog.h"
#include "netbootxyz.h"

#include <QRegularExpression>

namespace EfiUtil {

namespace {

bool hex64(const QString &s)
{
    static const QRegularExpression re(QStringLiteral("^[0-9a-fA-F]{64}$"));
    return re.match(s).hasMatch();
}

} // namespace

bool isPartitionGuid(const QString &guid)
{
    static const QRegularExpression re(
        QStringLiteral("^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-"
                       "[0-9a-fA-F]{4}-[0-9a-fA-F]{12}$"));
    return re.match(guid).hasMatch();
}

Request parseRequest(const QString &text)
{
    Request req;
    const QStringList lines = text.split(QRegularExpression(QStringLiteral("\\r?\\n")));
    for (const QString &raw : lines) {
        const QString line = raw.trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
            continue;
        const int eq = line.indexOf(QLatin1Char('='));
        if (eq <= 0) {
            req.error = QStringLiteral("The request has an unreadable line.");
            return req;
        }
        const QString key = line.left(eq).trimmed();
        const QString value = line.mid(eq + 1).trimmed();
        if (key == QLatin1String("action"))
            req.action = value;
        else if (key == QLatin1String("utility"))
            req.utility = value;
        else if (key == QLatin1String("esp-guid"))
            req.espGuid = value.toLower();
        else if (key == QLatin1String("asset"))
            req.asset = value;
        else if (key == QLatin1String("version"))
            req.version = value;
        else if (key == QLatin1String("sha256"))
            req.sha256 = value.toLower();
        else if (key == QLatin1String("replace")) {
            if (value != QLatin1String("0") && value != QLatin1String("1")) {
                req.error = QStringLiteral("The request replace flag must be 0 or 1.");
                return req;
            }
            req.replace = value == QLatin1String("1");
        } else {
            req.error = QStringLiteral("The request contains an unknown field.");
            return req;
        }
    }
    if (req.action != QLatin1String("status") && req.action != QLatin1String("install")
        && req.action != QLatin1String("remove")) {
        req.error = QStringLiteral("The request action is not status, install, or remove.");
        return req;
    }
    if (!isKnownUtility(req.utility)) {
        req.error = QStringLiteral("The request is not for a known EFI utility.");
        return req;
    }
    if (!isPartitionGuid(req.espGuid)) {
        req.error = QStringLiteral("The request ESP id is not a partition GUID.");
        return req;
    }
    if (req.action == QLatin1String("install")) {
        if (!managedFileFor(req.utility, req.asset).ok) {
            req.error = QStringLiteral(
                "The request asset is not an EFI file this feature installs.");
            return req;
        }
        if (!hex64(req.sha256)) {
            req.error = QStringLiteral("The request checksum is not a SHA-256 digest.");
            return req;
        }
    }
    req.ok = true;
    return req;
}

QString formatRequest(const Request &request)
{
    return QStringLiteral(
               "action=%1\nutility=%2\nesp-guid=%3\nasset=%4\nversion=%5\nsha256=%6\nreplace=%7\n")
        .arg(request.action, request.utility, request.espGuid.toLower(), request.asset,
             request.version, request.sha256.toLower(),
             request.replace ? QStringLiteral("1") : QStringLiteral("0"));
}

} // namespace EfiUtil
