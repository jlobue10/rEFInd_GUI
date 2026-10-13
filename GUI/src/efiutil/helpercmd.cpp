#include "helpercmd.h"

#include "catalog.h"
#include "install.h"
#include "netbootxyz.h"
#include "request.h"
#include "showtools.h"
#include "trust.h"

#include "espops/espconstants.h"
#include "espops/espresolve.h"
#include "espops/userio.h"

#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSysInfo>

#include <cstdio>

#ifdef Q_OS_UNIX
#include <unistd.h>
#endif

namespace EfiUtil {

namespace {

QString readUserFile(EspOps::UserFiles &files, const QString &path, qint64 maxBytes,
                     bool *ok)
{
    QBuffer buf;
    buf.open(QIODevice::WriteOnly);
    qint64 written = 0;
    if (!files.read(path, &buf, maxBytes, &written)) {
        *ok = false;
        return {};
    }
    *ok = true;
    return QString::fromUtf8(buf.data());
}

QByteArray readUserBytes(EspOps::UserFiles &files, const QString &path, qint64 maxBytes,
                         bool *ok)
{
    QBuffer buf;
    buf.open(QIODevice::WriteOnly);
    qint64 written = 0;
    if (!files.read(path, &buf, maxBytes, &written)) {
        *ok = false;
        return {};
    }
    *ok = true;
    return buf.data();
}

void appendStatus(const QString &espRoot, const QString &utilityId,
                  EspOps::EspResolver &resolver, QStringList *lines)
{
    const DetectedInstall found = detectInstall(espRoot, utilityId);
    const QString showtool = showtoolTokenFor(utilityId);
    QString installedName = found.record.valid
        ? managedFileFor(utilityId, found.record.asset).fileName
        : installedNameForArch(utilityId, QSysInfo::currentCpuArchitecture());
    QString presence = QStringLiteral("absent");
    if (found.presence == Presence::Managed)
        presence = QStringLiteral("managed");
    else if (found.presence == Presence::Unmanaged)
        presence = QStringLiteral("unmanaged");
    else if (found.presence == Presence::Changed)
        presence = QStringLiteral("changed");
    lines->append(QStringLiteral("result=ok"));
    lines->append(QStringLiteral("presence=%1").arg(presence));
    lines->append(QStringLiteral("version=%1").arg(found.record.version));
    lines->append(QStringLiteral("asset=%1").arg(found.record.asset));
    lines->append(QStringLiteral("sha256=%1").arg(found.onDiskSha256));

    QString live = QStringLiteral("unreadable");
    QString excluded = QStringLiteral("unreadable");
    EspOps::RefindTarget target;
    if (resolver.resolve(&target)) {
        QFile conf(target.refindDir + QStringLiteral("/refind.conf"));
        if (!conf.exists()) {
            live = QStringLiteral("missing");
            excluded = QStringLiteral("no");
        } else if (!conf.open(QIODevice::ReadOnly)) {
            live = QStringLiteral("unreadable");
        } else {
            const QString text = QString::fromUtf8(conf.readAll());
            bool present = false;
            const QStringList tokens = liveShowtoolsTokens(text, &present);
            if (!present)
                live = QStringLiteral("missing");
            else if (tokens.contains(showtool))
                live = QStringLiteral("yes");
            else
                live = QStringLiteral("no");
            excluded = (!installedName.isEmpty()
                        && dontScanToolsExcludes(text, installedName))
                ? QStringLiteral("yes")
                : QStringLiteral("no");
        }
    }
    lines->append(QStringLiteral("live-showtools=%1").arg(live));
    lines->append(QStringLiteral("live-excluded=%1").arg(excluded));
}

int perform(EspOps::UserFiles &files, const QString &dataDir, QStringList *lines)
{
    bool ok = false;
    const QString requestText = readUserFile(
        files, dataDir + QLatin1Char('/') + QLatin1String(kRequestRelative), 16384, &ok);
    if (!ok) {
        lines->append(QStringLiteral("No EFI utility request was found in the data directory."));
        return kExitBadRequest;
    }
    const Request req = parseRequest(requestText);
    if (!req.ok) {
        lines->append(req.error);
        return kExitBadRequest;
    }

    EspOps::EspResolver resolver;
    QString root;
    QString how;
    QString error;
    if (!resolver.mountByPartUuid(req.espGuid, &root, &how, &error)) {
        lines->append(error);
        return kExitNoEsp;
    }

    if (req.action == QLatin1String("status")) {
        appendStatus(root, req.utility, resolver, lines);
        lines->append(QStringLiteral("esp=%1").arg(how));
        return kExitOk;
    }

    if (req.action == QLatin1String("remove")) {
        resolver.makeWritable(root);
        const FileOutcome o = removeInstall(root, req.utility);
        *lines += o.lines;
#ifdef Q_OS_UNIX
        ::sync();
#endif
        return o.exitCode;
    }

    if (!assetFitsArch(req.utility, req.asset, QSysInfo::currentCpuArchitecture())) {
        lines->append(QStringLiteral(
            "That EFI file does not match this computer's architecture."));
        return kExitBadRequest;
    }
    const ManagedFile dest = managedFileFor(req.utility, req.asset);
    if (!dest.ok) {
        lines->append(dest.error);
        return kExitBadRequest;
    }

    // The request checksum is not a credential. Bytes come from a compiled-in
    // pin, a root-owned distro file, or a checksum this process fetches.
    bool got = false;
    QByteArray staged = readUserBytes(
        files, dataDir + QLatin1Char('/') + QLatin1String(kPayloadRelative),
        kMaxEfiBytes, &got);
    if (!got)
        staged.clear();

    const QString destPath = QDir(root).filePath(dest.relativePath);
    if (QFileInfo(destPath).isSymLink()) {
        lines->append(QStringLiteral("%1 is a symlink; it was left untouched.")
                          .arg(dest.fileName));
        return kExitRefused;
    }
    QByteArray onDisk;
    QFile existing(destPath);
    if (existing.open(QIODevice::ReadOnly))
        onDisk = existing.readAll();

    CurlPublisherFetch fetch;
    FilesystemDistroBytes distroFiles;
    const TrustedPayload trusted = trustedPayload(
        req, staged, onDisk, QSysInfo::currentCpuArchitecture(), &fetch, &distroFiles);
    if (!trusted.ok) {
        lines->append(trusted.error);
        return kExitBadRequest;
    }
    if (trusted.asset != req.asset) {
        lines->append(QStringLiteral(
            "The request names a different file than the one the helper verified."));
        return kExitBadRequest;
    }

    UtilityRecord record;
    record.product = QLatin1String(EspOps::kProductName);
    record.id = req.utility;
    record.version = trusted.version;
    record.asset = trusted.asset;
    record.sha256 = trusted.sha256;
    record.valid = true;

    resolver.makeWritable(root);
    const FileOutcome o = installPayload(root, trusted.bytes, record, req.replace);
    *lines += o.lines;
    if (o.exitCode == kExitOk)
        lines->append(QStringLiteral("(chosen as %1)").arg(how));
#ifdef Q_OS_UNIX
    if (o.exitCode == kExitOk)
        ::sync();
#endif
    return o.exitCode;
}

} // namespace

int runEfiUtilityInProcess(const QString &dataDir, QString *output)
{
    EspOps::DirectUserFiles files;
    QStringList lines;
    const int code = perform(files, dataDir, &lines);
    if (output)
        *output = lines.join(QLatin1Char('\n'));
    return code;
}

#ifdef Q_OS_UNIX

int runEfiUtilityHelper()
{
    if (geteuid() != 0) {
        std::printf("This helper must run as root (the %s GUI launches it via sudo).\n",
                    EspOps::kProductName);
        return 2;
    }
    const EspOps::SudoUser user = EspOps::sudoUserFromEnvironment();
    if (!user.valid) {
        std::printf("This helper is meant to be launched by the %s GUI via sudo.\n",
                    EspOps::kProductName);
        return 2;
    }
    const QString dataDir = user.home + QLatin1String("/.local/")
        + QLatin1String(EspOps::kDataDirName);
    EspOps::ForkUserFiles files(user);
    QStringList lines;
    const int code = perform(files, dataDir, &lines);
    for (const QString &line : lines)
        std::printf("%s\n", line.toLocal8Bit().constData());
    return code;
}

#endif

} // namespace EfiUtil
