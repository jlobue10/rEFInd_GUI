#include "install.h"

#include "catalog.h"
#include "netbootxyz.h"
#include "espops/espconstants.h"
#include "espops/userio.h"

#include <QSysInfo>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStorageInfo>
#include <QTemporaryFile>

namespace EfiUtil {

namespace {

FileOutcome fail(int code, const QString &line)
{
    FileOutcome o;
    o.exitCode = code;
    o.lines << line;
    return o;
}

QString toolsDir(const QString &espRoot)
{
    return QDir(espRoot).filePath(QLatin1String(kToolsDir));
}

QString fileInTools(const QString &espRoot, const QString &fileName)
{
    return QDir(toolsDir(espRoot)).filePath(fileName);
}

bool componentIsDotDot(const QString &path)
{
    const QString native = QDir::fromNativeSeparators(path);
    for (const QString &part : native.split(QLatin1Char('/'), Qt::SkipEmptyParts)) {
        if (part == QLatin1String(".."))
            return true;
    }
    return false;
}

bool isSymlink(const QString &path)
{
    return QFileInfo(path).isSymLink();
}

bool publishBytes(const QString &destDir, const QString &finalName,
                  const QByteArray &bytes, QString *error)
{
    QDir dir(destDir);
    if (!dir.exists() && !QDir().mkpath(destDir)) {
        *error = QStringLiteral("Could not create %1.").arg(destDir);
        return false;
    }
    const QStringList stale = dir.entryList(
        {QStringLiteral(".%1.new.*").arg(finalName)},
        QDir::Files | QDir::Hidden);
    for (const QString &s : stale)
        QFile::remove(dir.filePath(s));

    QString stagedName;
    {
        QTemporaryFile stage(dir.filePath(
            QStringLiteral(".%1.new.XXXXXX").arg(finalName)));
        stage.setAutoRemove(false);
        if (!stage.open()) {
            *error = QStringLiteral(
                         "Could not create a staging file in %1. The EFI "
                         "System Partition may be full or read-only.")
                         .arg(destDir);
            return false;
        }
        if (stage.write(bytes) != bytes.size() || !stage.flush()) {
            const QString name = stage.fileName();
            stage.close();
            QFile::remove(name);
            *error = QStringLiteral(
                         "The staging write in %1 was incomplete. The EFI "
                         "System Partition may be full.")
                         .arg(destDir);
            return false;
        }
        stage.close();
        stagedName = stage.fileName();
    }
    const QString dest = dir.filePath(finalName);
    if (!EspOps::publishRename(stagedName, dest)) {
        QFile::remove(stagedName);
        *error = QStringLiteral("Could not publish %1.").arg(dest);
        return false;
    }
    return true;
}

} // namespace

QString managedRelativePath()
{
    return managedRelativePath(QLatin1String(kUtilityId), QLatin1String("netboot.xyz.efi"));
}

QString managedRelativePath(const QString &utilityId, const QString &asset)
{
    const ManagedFile file = managedFileFor(utilityId, asset);
    return file.ok ? file.relativePath : QString();
}

bool looksLikeEspRoot(const QString &root, QString *error)
{
    const auto set = [error](const QString &m) {
        if (error)
            *error = m;
    };
    if (root.isEmpty() || componentIsDotDot(root)) {
        set(QStringLiteral("The ESP path is empty or contains \"..\"."));
        return false;
    }
    const QFileInfo info(root);
    if (!info.isAbsolute()) {
        set(QStringLiteral("The ESP path is not absolute."));
        return false;
    }
    const QString efi = QDir(root).filePath(QStringLiteral("EFI"));
    const QFileInfo efiInfo(efi);
    if (!efiInfo.exists() || !efiInfo.isDir()) {
        set(QStringLiteral(
            "That directory has no EFI folder, so it is not an EFI System Partition."));
        return false;
    }
    if (efiInfo.isSymLink() || isSymlink(QDir(root).filePath(QLatin1String(kToolsDir)))) {
        set(QStringLiteral(
            "Refusing to follow a symlink under the EFI System Partition."));
        return false;
    }
    return true;
}

UtilityRecord parseSidecar(const QString &text)
{
    UtilityRecord rec;
    const QStringList lines = text.split(QRegularExpression(QStringLiteral("\\r?\\n")));
    for (const QString &raw : lines) {
        const QString line = raw.trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
            continue;
        const int eq = line.indexOf(QLatin1Char('='));
        if (eq <= 0)
            continue;
        const QString key = line.left(eq).trimmed();
        const QString value = line.mid(eq + 1).trimmed();
        if (key == QLatin1String("product"))
            rec.product = value;
        else if (key == QLatin1String("id"))
            rec.id = value;
        else if (key == QLatin1String("version"))
            rec.version = value;
        else if (key == QLatin1String("asset"))
            rec.asset = value;
        else if (key == QLatin1String("sha256"))
            rec.sha256 = value.toLower();
    }
    rec.valid = rec.product == QLatin1String(EspOps::kProductName)
        && isKnownUtility(rec.id)
        && digestsEqual(rec.sha256, rec.sha256);
    return rec;
}

QString formatSidecar(const UtilityRecord &record)
{
    return QStringLiteral("product=%1\nid=%2\nversion=%3\nasset=%4\nsha256=%5\n")
        .arg(record.product, record.id, record.version, record.asset,
             record.sha256.toLower());
}

DetectedInstall detectInstall(const QString &espRoot)
{
    return detectInstall(espRoot, QLatin1String(kUtilityId));
}

DetectedInstall detectInstall(const QString &espRoot, const QString &utilityId)
{
    DetectedInstall found;
    const QString sidecarName = sidecarNameFor(utilityId);
    if (sidecarName.isEmpty())
        return found;
    const QString sidePath = fileInTools(espRoot, sidecarName);
    QFile side(sidePath);
    if (side.exists() && side.open(QIODevice::ReadOnly))
        found.record = parseSidecar(QString::fromUtf8(side.readAll()));
    if (found.record.valid && found.record.id != utilityId)
        found.record.valid = false;

    QString fileName;
    if (found.record.valid)
        fileName = managedFileFor(utilityId, found.record.asset).fileName;
    if (fileName.isEmpty())
        fileName = installedNameForArch(utilityId, QSysInfo::currentCpuArchitecture());
    if (fileName.isEmpty())
        return found;

    const QString efi = fileInTools(espRoot, fileName);
    QFile bin(efi);
    if (bin.exists() && QFileInfo(efi).isFile() && !QFileInfo(efi).isSymLink()
        && bin.open(QIODevice::ReadOnly)) {
        found.onDiskSha256 = sha256Hex(bin.readAll());
    }

    if (found.onDiskSha256.isEmpty()) {
        found.presence = Presence::Absent;
        return found;
    }
    if (!found.record.valid) {
        found.presence = Presence::Unmanaged;
        return found;
    }
    if (digestsEqual(found.onDiskSha256, found.record.sha256)) {
        found.presence = Presence::Managed;
        return found;
    }
    found.presence = Presence::Changed;
    return found;
}

FileOutcome installPayload(const QString &espRoot, const QByteArray &payload,
                           const UtilityRecord &record, bool replace)
{
    QString why;
    if (!looksLikeEspRoot(espRoot, &why))
        return fail(kExitNoEsp, why);
    if (payload.isEmpty() || payload.size() > kMaxEfiBytes)
        return fail(kExitBadRequest,
                    QStringLiteral("The EFI binary is empty or larger than the size cap."));
    const ManagedFile dest = managedFileFor(record.id, record.asset);
    if (!dest.ok)
        return fail(kExitBadRequest, dest.error);
    const QString got = sha256Hex(payload);
    if (!digestsEqual(got, record.sha256))
        return fail(kExitBadRequest,
                    QStringLiteral("The EFI bytes do not match the recorded checksum."));
    if (isSymlink(fileInTools(espRoot, dest.fileName)))
        return fail(kExitRefused,
                    QStringLiteral("%1 is a symlink; it was left untouched.").arg(dest.fileName));

    UtilityRecord owned = record;
    owned.product = QLatin1String(EspOps::kProductName);
    owned.id = dest.utilityId;
    owned.sha256 = got;

    const DetectedInstall current = detectInstall(espRoot, dest.utilityId);
    if (current.presence == Presence::Managed
        && digestsEqual(current.onDiskSha256, got)) {
        if (current.record.version == owned.version
            && current.record.asset == owned.asset) {
            return fail(kExitOk, QStringLiteral("%1 is already installed and matches this release.")
                                     .arg(dest.utilityId));
        }
        QString error;
        if (!publishBytes(toolsDir(espRoot), dest.sidecarName,
                          formatSidecar(owned).toUtf8(), &error))
            return fail(kExitStaging, error);
        return fail(kExitOk, QStringLiteral(
                        "The installed binary already matched; the ownership record was updated."));
    }

    if (!current.onDiskSha256.isEmpty() && !replace) {
        return fail(kExitRefused,
                    QStringLiteral(
                        "%1 already exists and was not replaced. "
                        "Confirm the replacement to overwrite it.")
                        .arg(dest.relativePath));
    }

    const QStorageInfo space(espRoot);
    if (space.isReadOnly()) {
        return fail(kExitReadOnly,
                    QStringLiteral("The EFI System Partition is mounted read-only."));
    }
    if (space.isValid() && space.bytesAvailable() >= 0
        && space.bytesAvailable() < payload.size() + 65536) {
        return fail(kExitStaging,
                    QStringLiteral("The EFI System Partition does not have enough free space."));
    }

    const QString tools = toolsDir(espRoot);
    if (!QDir(tools).exists() && !QDir().mkpath(tools)) {
        return fail(kExitReadOnly,
                    QStringLiteral("Could not create EFI/tools. The partition may be read-only."));
    }

    QString error;
    if (!publishBytes(tools, dest.fileName, payload, &error))
        return fail(kExitStaging, error);
    if (!publishBytes(tools, dest.sidecarName, formatSidecar(owned).toUtf8(), &error)) {
        return fail(kExitStaging,
                    QStringLiteral(
                        "%1 was copied, but the ownership record could not be written. "
                        "The file is not marked as managed. %2")
                        .arg(dest.fileName, error));
    }

    const DetectedInstall after = detectInstall(espRoot, dest.utilityId);
    if (after.presence != Presence::Managed)
        return fail(kExitStaging,
                    QStringLiteral("The install did not verify after publishing."));
    FileOutcome ok;
    ok.lines << QStringLiteral("Installed %1 %2 as %3 (%4).")
                    .arg(dest.utilityId, owned.version, dest.relativePath, owned.asset);
    if (dest.utilityId == QLatin1String(kUtilityId)) {
        ok.lines << QStringLiteral(
            "rEFInd lists this file as \"Netboot\" only when showtools includes netboot. "
            "This is not ipxe_discover.efi, and scanfor was not changed.");
    } else {
        ok.lines << QStringLiteral(
                        "rEFInd shows this file when showtools includes %1. "
                        "scanfor was not changed, and no other tool file was written.")
                        .arg(dest.showtoolToken);
    }
    return ok;
}

FileOutcome removeInstall(const QString &espRoot)
{
    return removeInstall(espRoot, QLatin1String(kUtilityId));
}

FileOutcome removeInstall(const QString &espRoot, const QString &utilityId)
{
    QString why;
    if (!looksLikeEspRoot(espRoot, &why))
        return fail(kExitNoEsp, why);
    const QString sidecarName = sidecarNameFor(utilityId);
    if (sidecarName.isEmpty())
        return fail(kExitBadRequest, QStringLiteral("That utility is not one this feature installs."));
    const DetectedInstall current = detectInstall(espRoot, utilityId);
    const QString fileName = current.record.valid
        ? managedFileFor(utilityId, current.record.asset).fileName
        : installedNameForArch(utilityId, QSysInfo::currentCpuArchitecture());
    const QString relative = fileName.isEmpty()
        ? sidecarName
        : QStringLiteral("%1/%2").arg(QLatin1String(kToolsDir), fileName);
    if (current.presence == Presence::Absent) {
        if (current.record.valid)
            QFile::remove(fileInTools(espRoot, sidecarName));
        return fail(kExitOk, QStringLiteral("%1 is not installed on this ESP.").arg(utilityId));
    }
    if (current.presence != Presence::Managed || fileName.isEmpty()) {
        return fail(kExitRefused,
                    QStringLiteral(
                        "%1 is not an installation managed by this app, so it was not deleted.")
                        .arg(relative));
    }
    const QString efi = fileInTools(espRoot, fileName);
    if (!QFile::remove(efi))
        return fail(kExitStaging, QStringLiteral("Could not delete %1.").arg(relative));
    QFile::remove(fileInTools(espRoot, sidecarName));
    if (QFile::exists(efi))
        return fail(kExitStaging, QStringLiteral("%1 is still present after removal.").arg(fileName));
    return fail(kExitOk, QStringLiteral(
                    "Removed the managed %1. Other files in EFI/tools were left in place.")
                    .arg(relative));
}

} // namespace EfiUtil
