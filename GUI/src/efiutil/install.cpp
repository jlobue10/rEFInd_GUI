#include "install.h"

#include "netbootxyz.h"
#include "espops/espconstants.h"
#include "espops/userio.h"

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

QString efiPath(const QString &espRoot)
{
    return QDir(toolsDir(espRoot)).filePath(QLatin1String(kInstalledEfiName));
}

QString sidecarPath(const QString &espRoot)
{
    return QDir(toolsDir(espRoot)).filePath(QLatin1String(kSidecarName));
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
    return QStringLiteral("%1/%2").arg(QLatin1String(kToolsDir),
                                       QLatin1String(kInstalledEfiName));
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
        && rec.id == QLatin1String(kUtilityId)
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
    DetectedInstall found;
    const QString efi = efiPath(espRoot);
    QFile bin(efi);
    if (bin.exists() && QFileInfo(efi).isFile() && !QFileInfo(efi).isSymLink()
        && bin.open(QIODevice::ReadOnly)) {
        found.onDiskSha256 = sha256Hex(bin.readAll());
    }
    QFile side(sidecarPath(espRoot));
    if (side.exists() && side.open(QIODevice::ReadOnly))
        found.record = parseSidecar(QString::fromUtf8(side.readAll()));

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
    if (!isNetbootXyzAsset(record.asset))
        return fail(kExitBadRequest,
                    QStringLiteral("That asset is not a netboot.xyz UEFI executable this feature installs."));
    const QString got = sha256Hex(payload);
    if (!digestsEqual(got, record.sha256))
        return fail(kExitBadRequest,
                    QStringLiteral("The EFI bytes do not match the recorded checksum."));
    if (isSymlink(efiPath(espRoot)))
        return fail(kExitRefused,
                    QStringLiteral("ipxe.efi is a symlink; it was left untouched."));

    UtilityRecord owned = record;
    owned.product = QLatin1String(EspOps::kProductName);
    owned.id = QLatin1String(kUtilityId);
    owned.sha256 = got;

    const DetectedInstall current = detectInstall(espRoot);
    if (current.presence == Presence::Managed
        && digestsEqual(current.onDiskSha256, got)) {
        if (current.record.version == owned.version
            && current.record.asset == owned.asset) {
            return fail(kExitOk, QStringLiteral(
                            "netboot.xyz is already installed and matches this release."));
        }
        QString error;
        if (!publishBytes(toolsDir(espRoot), QLatin1String(kSidecarName),
                          formatSidecar(owned).toUtf8(), &error))
            return fail(kExitStaging, error);
        return fail(kExitOk, QStringLiteral(
                        "The installed binary already matched; the ownership record was updated."));
    }

    if (!current.onDiskSha256.isEmpty() && !replace) {
        return fail(kExitRefused,
                    QStringLiteral(
                        "EFI/tools/ipxe.efi already exists and was not replaced. "
                        "Confirm the replacement to overwrite it."));
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
    if (!publishBytes(tools, QLatin1String(kInstalledEfiName), payload, &error))
        return fail(kExitStaging, error);
    if (!publishBytes(tools, QLatin1String(kSidecarName), formatSidecar(owned).toUtf8(),
                      &error)) {
        return fail(kExitStaging,
                    QStringLiteral(
                        "ipxe.efi was copied, but the ownership record could not be written. "
                        "The file is not marked as managed. %1")
                        .arg(error));
    }

    const DetectedInstall after = detectInstall(espRoot);
    if (after.presence != Presence::Managed)
        return fail(kExitStaging,
                    QStringLiteral("The install did not verify after publishing."));
    FileOutcome ok;
    ok.lines << QStringLiteral(
                    "Installed netboot.xyz %1 as EFI/tools/ipxe.efi (%2).")
                    .arg(owned.version, owned.asset);
    ok.lines << QStringLiteral(
        "rEFInd lists this file as \"Netboot\" only when showtools includes netboot. "
        "This is not ipxe_discover.efi, and scanfor was not changed.");
    return ok;
}

FileOutcome removeInstall(const QString &espRoot)
{
    QString why;
    if (!looksLikeEspRoot(espRoot, &why))
        return fail(kExitNoEsp, why);
    const DetectedInstall current = detectInstall(espRoot);
    if (current.presence == Presence::Absent) {
        if (current.record.valid)
            QFile::remove(sidecarPath(espRoot));
        return fail(kExitOk, QStringLiteral("netboot.xyz is not installed on this ESP."));
    }
    if (current.presence != Presence::Managed) {
        return fail(kExitRefused,
                    QStringLiteral(
                        "EFI/tools/ipxe.efi is not an installation managed by this app, "
                        "so it was not deleted."));
    }
    if (!QFile::remove(efiPath(espRoot)))
        return fail(kExitStaging, QStringLiteral("Could not delete EFI/tools/ipxe.efi."));
    QFile::remove(sidecarPath(espRoot));
    if (QFile::exists(efiPath(espRoot)))
        return fail(kExitStaging, QStringLiteral("ipxe.efi is still present after removal."));
    return fail(kExitOk, QStringLiteral(
                    "Removed the managed EFI/tools/ipxe.efi. Other files in EFI/tools were left in place."));
}

} // namespace EfiUtil
