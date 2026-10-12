// Install, detect, and remove one managed EFI tool on an ESP root.
// The destination filename comes from the utility catalog, never from a
// caller-supplied path. Operates on a directory the caller already mounted;
// it does not mount anything and it does not edit refind.conf.

#ifndef EFIUTIL_INSTALL_H
#define EFIUTIL_INSTALL_H

#include <QByteArray>
#include <QString>
#include <QStringList>

namespace EfiUtil {

inline constexpr int kExitOk = 0;
inline constexpr int kExitNoEsp = 3;
inline constexpr int kExitReadOnly = 4;
inline constexpr int kExitStaging = 5;
inline constexpr int kExitRefused = 7;
inline constexpr int kExitBadRequest = 8;

struct UtilityRecord
{
    QString product;
    QString id;
    QString version;
    QString asset;
    QString sha256;
    bool valid = false;
};

enum class Presence { Absent, Managed, Unmanaged, Changed };

struct DetectedInstall
{
    Presence presence = Presence::Absent;
    UtilityRecord record;
    QString onDiskSha256;
};

struct FileOutcome
{
    int exitCode = kExitOk;
    QStringList lines;
};

// "EFI/tools/ipxe.efi" for netboot.xyz. Other utilities pass their id and asset.
QString managedRelativePath();
QString managedRelativePath(const QString &utilityId, const QString &asset);

// True when `root` is an absolute path with no ".." component and contains
// a real EFI directory (not a symlink).
bool looksLikeEspRoot(const QString &root, QString *error);

UtilityRecord parseSidecar(const QString &text);
QString formatSidecar(const UtilityRecord &record);

// The one-argument form reports netboot.xyz.
DetectedInstall detectInstall(const QString &espRoot);
DetectedInstall detectInstall(const QString &espRoot, const QString &utilityId);

// `replace` is required when the destination already exists and its bytes
// differ from `payload`, or when the file is present but not managed. A
// managed file whose hash already matches `payload` is left in place.
// `record.id` and `record.asset` select the catalog destination.
FileOutcome installPayload(const QString &espRoot, const QByteArray &payload,
                           const UtilityRecord &record, bool replace);

// Deletes that utility's EFI file and sidecar only when the sidecar names
// it and the on-disk hash still matches. Every other file in EFI/tools stays.
FileOutcome removeInstall(const QString &espRoot);
FileOutcome removeInstall(const QString &espRoot, const QString &utilityId);

} // namespace EfiUtil

#endif
