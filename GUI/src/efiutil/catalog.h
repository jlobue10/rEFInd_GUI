// Allowlist of EFI tools this feature can install. The request names a
// utility and an upstream asset. The installed filename is chosen here and
// is always one rEFInd's FindTool() already searches for under EFI/tools.
// Nothing in a request can select another path.

#ifndef EFIUTIL_CATALOG_H
#define EFIUTIL_CATALOG_H

#include <QByteArray>
#include <QRegularExpression>
#include <QString>
#include <QStringList>

namespace EfiUtil {

inline constexpr char kShellId[] = "shell";
inline constexpr char kMemtestId[] = "memtest86+";
inline constexpr char kGdiskId[] = "gdisk";
inline constexpr char kFwupdId[] = "fwupd";
inline constexpr char kHashToolId[] = "hashtool";
inline constexpr char kMokManagerId[] = "mokmanager";
inline constexpr char kKeyToolId[] = "keytool";

// Dialog order. netboot.xyz stays first.
QStringList utilityIds();

bool isKnownUtility(const QString &utilityId);
QString sidecarNameFor(const QString &utilityId);
QString showtoolTokenFor(const QString &utilityId);

struct ManagedFile
{
    bool ok = false;
    QString error;
    QString utilityId;
    QString asset;
    QString fileName;
    QString sidecarName;
    QString showtoolToken;
    QString relativePath;
};

// Fails closed for unknown ids, ipxe_discover.efi, slashes, and "..".
ManagedFile managedFileFor(const QString &utilityId, const QString &asset);

// Filename rEFInd will look for on this CPU, before a release is downloaded.
// Empty when that tool has no build for the architecture.
QString installedNameForArch(const QString &utilityId, const QString &cpuArch);

// Upstream asset the dialog asks for. Empty for memtest86+ (the name is
// inside the zip) and for architectures the tool does not support.
QString defaultAssetFor(const QString &utilityId, const QString &cpuArch);

bool utilitySupportsArch(const QString &utilityId, const QString &cpuArch);
bool assetFitsArch(const QString &utilityId, const QString &asset, const QString &cpuArch);

// Exact paths of distro-packaged EFI binaries the GUI may read. The request
// cannot name a path.
QStringList localToolPaths(const QString &utilityId, const QString &cpuArch);
bool isAllowedLocalToolPath(const QString &path);

struct LocalToolFile
{
    bool ok = false;
    QString error;
    QString path;
    QString asset;
    QByteArray bytes;
    QString sha256;
};

// Reads the first regular, non-symlink candidate. Does not follow a symlink
// at the file itself.
LocalToolFile readLocalTool(const QString &utilityId, const QString &cpuArch);

// GPT fdisk 1.0.4 (SourceForge, 2019) is still the last release Rod Smith
// published an EFI binary for. gdisk_x64.efi in that zip is signed with his
// rEFInd key, not the Microsoft UEFI CA. The helper accepts only the
// compiled-in member hash below.
QString gdiskZipUrl();
QString gdiskZipSha256();
QString gdiskMemberName();
QString gdiskMemberSha256();
QString gdiskVersion();

// Linux Foundation HashTool.efi from the February 2013 signed kit. Microsoft
// signed this file and declined to sign KeyTool. The SHA-256 is of the file
// whose MD5 (45639d23aa5f2a394b03a65fc732acf2) is published on
// https://blog.hansenpartnership.com/linux-foundation-secure-boot-system-released/
// This helper does not consult the firmware dbx; a machine that has revoked
// that signature will refuse to run the file. The helper accepts only this hash.
QString hashToolUrl();
QString hashToolSha256();
QString hashToolVersion();

struct MemtestLocation
{
    bool ok = false;
    QString error;
    QString tag;
    QString zipName;
    QString checksumUrl;
    QString zipUrl;
    QRegularExpression memberPattern;
};

// tag is a GitHub tag such as "v8.10". The binaries live on memtest.org.
MemtestLocation memtestLocation(const QString &tag, const QString &cpuArch);

// EDK2 Shell binaries built from EDK2 stable and published with a GitHub digest.
QString shellReleaseApiUrl();
QString memtestReleaseApiUrl();

} // namespace EfiUtil

#endif
