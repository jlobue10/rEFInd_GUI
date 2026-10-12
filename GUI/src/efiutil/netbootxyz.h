// netboot.xyz release metadata. Parsing only — no network, no ESP writes.
// Asset URLs come from the GitHub releases JSON; nothing here invents one.

#ifndef EFIUTIL_NETBOOTXYZ_H
#define EFIUTIL_NETBOOTXYZ_H

#include <QByteArray>
#include <QList>
#include <QPair>
#include <QString>

namespace EfiUtil {

inline constexpr char kUtilityId[] = "netboot.xyz";
inline constexpr char kChecksumAsset[] = "netboot.xyz-sha256-checksums.txt";
// The only filename rEFInd's showtools "netboot" token looks for.
inline constexpr char kInstalledEfiName[] = "ipxe.efi";
inline constexpr char kShowtoolToken[] = "netboot";
// Not a tool filename, so FindTool() will not offer it on the tools row.
inline constexpr char kSidecarName[] = "rEFInd_GUI-netboot.xyz";
inline constexpr char kToolsDir[] = "EFI/tools";
inline constexpr qint64 kMaxEfiBytes = 8 * 1024 * 1024;

struct GithubAsset
{
    QString name;
    QString url;
    qint64 size = -1;
    // Lowercase hex from the GitHub asset "digest" field (sha256:...), or
    // empty when the release JSON does not publish one.
    QString sha256;
};

struct GithubRelease
{
    bool ok = false;
    QString error;
    QString tag;
    QList<GithubAsset> assets;
};

struct ChecksumFile
{
    bool ok = false;
    QString error;
    // Lowercase hex digest keyed by the exact asset filename.
    QList<QPair<QString, QString>> sums;
};

struct Variant
{
    QString id;    // "standard", "snp", "snponly"
    QString label; // short UI label; the dialog translates its own copy
    QString asset; // upstream filename for this CPU architecture
};

// True for the six UEFI executables this feature knows how to install
// (x86_64 and arm64, standard / SNP / SNP-only). ipxe.efi is not one of them.
bool isNetbootXyzAsset(const QString &name);

// Three variants for the running architecture. cpuArch is a Qt
// QSysInfo::currentCpuArchitecture() value ("x86_64", "arm64", "aarch64").
// Unknown architectures yield an empty list.
QList<Variant> netbootVariants(const QString &cpuArch);

QString assetForVariant(const QString &variantId, const QString &cpuArch);

// Requires the netboot.xyz checksum asset. Other projects use the two
// parsers below so a shell or memtest response is not rejected for lacking
// that filename.
GithubRelease parseGithubRelease(const QByteArray &json);

// Same URL rules, but the checksum asset is not required. Assets may carry
// a GitHub digest. An empty asset list fails.
GithubRelease parseGithubAssetRelease(const QByteArray &json);

// tag_name only. Used when the release publishes no EFI assets (memtest86+
// hosts its binaries on memtest.org).
GithubRelease parseGithubTag(const QByteArray &json);

const GithubAsset *findAsset(const GithubRelease &release, const QString &name);

// coreutils sha256sum text: "HEX *name" or "HEX  name". Comments (#) and
// blank lines are ignored. A truncated or malformed line fails the file.
ChecksumFile parseSha256Sums(const QString &text);
QString checksumFor(const ChecksumFile &file, const QString &assetName);

// Exact name, or the last path component of a sum such as
// "v8.10/mt86plus_8.10.binaries.zip". Two matches fail closed.
QString checksumByBasename(const ChecksumFile &file, const QString &baseName);

QByteArray sha256(const QByteArray &data);
QString sha256Hex(const QByteArray &data);

// True when both digests are 64 hex chars and equal, ignoring case.
bool digestsEqual(const QString &installed, const QString &release);

// The GUI downloads the EFI only when the managed file's hash differs from
// the selected release. Equal digests skip that download.
inline bool efiPayloadDownloadNeeded(const QString &managedSha256,
                                     const QString &releaseSha256)
{
    return !digestsEqual(managedSha256, releaseSha256);
}

} // namespace EfiUtil

#endif
