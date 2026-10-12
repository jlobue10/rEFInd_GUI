// Install, detect, and remove the managed netboot.xyz file on an ESP root.
// The destination filename is always EFI/tools/ipxe.efi. Callers cannot
// choose another path. Operates on a directory the caller already mounted;
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

// "EFI/tools/ipxe.efi". There is no other managed destination.
QString managedRelativePath();

// True when `root` is an absolute path with no ".." component and contains
// a real EFI directory (not a symlink).
bool looksLikeEspRoot(const QString &root, QString *error);

UtilityRecord parseSidecar(const QString &text);
QString formatSidecar(const UtilityRecord &record);

DetectedInstall detectInstall(const QString &espRoot);

// `replace` is required when ipxe.efi already exists and its bytes differ
// from `payload`, or when the file is present but not managed. A managed
// file whose hash already matches `payload` is left in place.
FileOutcome installPayload(const QString &espRoot, const QByteArray &payload,
                           const UtilityRecord &record, bool replace);

// Deletes ipxe.efi and the sidecar only when the sidecar names this product
// and the on-disk hash still matches. Every other file in EFI/tools stays.
FileOutcome removeInstall(const QString &espRoot);

} // namespace EfiUtil

#endif
