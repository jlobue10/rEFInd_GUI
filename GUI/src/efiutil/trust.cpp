#include "trust.h"

#include "netbootxyz.h"
#include "zipmember.h"

#include <QDir>
#include <QFile>
#include <QFileDevice>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QUrl>

namespace EfiUtil {

namespace {

bool releaseVersionToken(const QString &version)
{
    if (version.isEmpty() || version == QLatin1String("latest"))
        return true;
    static const QRegularExpression re(QStringLiteral("^[A-Za-z0-9._+-]{1,64}$"));
    return re.match(version).hasMatch();
}

QString releaseApiFor(const QString &latestUrl, const QString &version)
{
    if (!releaseVersionToken(version) || !latestUrl.endsWith(QLatin1String("/latest")))
        return {};
    if (version.isEmpty() || version == QLatin1String("latest"))
        return latestUrl;
    QString base = latestUrl;
    base.chop(int(QLatin1String("latest").size()));
    return base + QStringLiteral("tags/") + version;
}

TrustedPayload refuse(const QString &error)
{
    TrustedPayload out;
    out.error = error;
    return out;
}

TrustedPayload matchingBytes(const QByteArray &staged, const QByteArray &onDisk,
                             const QString &expectedSha, const QString &version,
                             const QString &asset)
{
    TrustedPayload out;
    out.version = version;
    out.asset = asset;
    out.sha256 = expectedSha.toLower();
    if (digestsEqual(sha256Hex(staged), expectedSha)) {
        out.ok = true;
        out.bytes = staged;
        return out;
    }
    if (digestsEqual(sha256Hex(onDisk), expectedSha)) {
        out.ok = true;
        out.bytes = onDisk;
        return out;
    }
    out.error = QStringLiteral(
        "The EFI file does not match the checksum this helper trusts for that tool.");
    return out;
}

bool isDistroTool(const QString &utilityId)
{
    return utilityId == QLatin1String(kFwupdId) || utilityId == QLatin1String(kMokManagerId)
        || utilityId == QLatin1String(kKeyToolId);
}

TrustedPayload fromGithubRelease(const Request &request, const QByteArray &staged,
                                 const QByteArray &onDisk, PublisherFetch *fetch,
                                 bool netboot)
{
    if (!fetch)
        return refuse(QStringLiteral("There is no way to fetch the publisher checksum."));
    const QString api = netboot ? netbootReleaseApiUrlFor(request.version)
                                : shellReleaseApiUrlFor(request.version);
    if (api.isEmpty())
        return refuse(QStringLiteral("The release version is not a name this helper fetches."));
    QString error;
    const QByteArray json = fetch->get(api, &error);
    if (json.isEmpty())
        return refuse(error.isEmpty()
                          ? QStringLiteral("The publisher release could not be downloaded.")
                          : error);
    const GithubRelease release = netboot ? parseGithubRelease(json)
                                          : parseGithubAssetRelease(json);
    if (!release.ok)
        return refuse(release.error);
    if (request.version != QLatin1String("latest") && !request.version.isEmpty()
        && release.tag != request.version) {
        return refuse(QStringLiteral(
            "The publisher release tag does not match the requested version."));
    }
    if (!releaseVersionToken(release.tag) || release.tag == QLatin1String("latest"))
        return refuse(QStringLiteral("The publisher release tag cannot be recorded."));

    if (!netboot) {
        const GithubAsset *file = findAsset(release, request.asset);
        if (!file || file->sha256.isEmpty()) {
            return refuse(QStringLiteral(
                "The publisher release has no SHA-256 digest for that EFI file."));
        }
        return matchingBytes(staged, onDisk, file->sha256, release.tag, request.asset);
    }

    const GithubAsset *sums = findAsset(release, QLatin1String(kChecksumAsset));
    if (!sums || !publisherUrlAllowed(sums->url))
        return refuse(QStringLiteral("The publisher checksum file URL is not one this helper fetches."));
    const QByteArray body = fetch->get(sums->url, &error);
    if (body.isEmpty())
        return refuse(error.isEmpty()
                          ? QStringLiteral("The publisher checksum file could not be downloaded.")
                          : error);
    const ChecksumFile parsed = parseSha256Sums(QString::fromUtf8(body));
    if (!parsed.ok)
        return refuse(parsed.error);
    const QString expected = checksumFor(parsed, request.asset);
    if (!isSha256Hex(expected))
        return refuse(QStringLiteral("The publisher checksum file has no entry for that EFI file."));
    return matchingBytes(staged, onDisk, expected, release.tag, request.asset);
}

TrustedPayload fromMemtest(const Request &request, const QString &cpuArch, PublisherFetch *fetch)
{
    if (!fetch)
        return refuse(QStringLiteral("There is no way to fetch the publisher checksum."));
    const MemtestLocation loc = memtestLocation(request.version, cpuArch);
    if (!loc.ok)
        return refuse(loc.error);
    if (!publisherUrlAllowed(loc.checksumUrl) || !publisherUrlAllowed(loc.zipUrl))
        return refuse(QStringLiteral("The memtest86+ download URL is not one this helper fetches."));
    QString error;
    const QByteArray sums = fetch->get(loc.checksumUrl, &error);
    if (sums.isEmpty())
        return refuse(error.isEmpty()
                          ? QStringLiteral("The memtest86+ checksum file could not be downloaded.")
                          : error);
    const ChecksumFile parsed = parseSha256Sums(QString::fromUtf8(sums));
    if (!parsed.ok)
        return refuse(parsed.error);
    const QString zipSha = checksumByBasename(parsed, loc.zipName);
    if (!isSha256Hex(zipSha))
        return refuse(QStringLiteral("The memtest86+ checksum file has no entry for the binaries zip."));
    const QByteArray zip = fetch->get(loc.zipUrl, &error);
    if (zip.isEmpty())
        return refuse(error.isEmpty() ? QStringLiteral("The memtest86+ zip could not be downloaded.")
                                      : error);
    if (zip.size() > kMaxZipBytes || !digestsEqual(sha256Hex(zip), zipSha)) {
        return refuse(QStringLiteral(
            "The memtest86+ zip does not match the checksum this helper downloaded."));
    }
    const ZipMember member = extractZipMember(zip, loc.memberPattern, kMaxEfiBytes);
    if (!member.ok)
        return refuse(member.error);
    if (!managedFileFor(request.utility, member.name).ok)
        return refuse(QStringLiteral("The memtest86+ archive member is not a file this helper installs."));
    TrustedPayload out;
    out.ok = true;
    out.bytes = member.bytes;
    out.sha256 = sha256Hex(member.bytes);
    out.version = loc.tag;
    out.asset = member.name;
    return out;
}

} // namespace

QString netbootReleaseApiUrlFor(const QString &version)
{
    return releaseApiFor(QStringLiteral(
                             "https://api.github.com/repos/netbootxyz/netboot.xyz/releases/latest"),
                         version);
}

QString shellReleaseApiUrlFor(const QString &version)
{
    return releaseApiFor(shellReleaseApiUrl(), version);
}

bool publisherUrlAllowed(const QString &urlText)
{
    const QUrl url(urlText);
    if (!url.isValid() || url.scheme() != QLatin1String("https") || !url.userInfo().isEmpty())
        return false;
    if (url.port() != -1 && url.port() != 443)
        return false;
    const QString path = url.path();
    if (path.contains(QLatin1String("..")) || path.contains(QLatin1Char('\\')))
        return false;
    const QString host = url.host().toLower();
    if (host == QLatin1String("memtest.org")) {
        static const QRegularExpression re(QStringLiteral(
            "^/download/v[0-9]+\\.[0-9]+/(sha256sum\\.txt|mt86plus_[0-9.]+\\.binaries\\.zip)$"));
        return re.match(path).hasMatch();
    }
    if (host == QLatin1String("api.github.com")) {
        return path.startsWith(QLatin1String("/repos/netbootxyz/netboot.xyz/releases/"))
            || path.startsWith(QLatin1String("/repos/pbatard/UEFI-Shell/releases/"));
    }
    if (host == QLatin1String("github.com")) {
        return path.startsWith(QLatin1String("/netbootxyz/netboot.xyz/releases/download/"))
            || path.startsWith(QLatin1String("/pbatard/UEFI-Shell/releases/download/"));
    }
    return false;
}

bool distroStatAcceptable(bool isSymlink, bool isRegular, quint32 uid, quint32 mode,
                          qint64 size, QString *error)
{
    const auto fail = [error](const char *text) {
        if (error)
            *error = QString::fromLatin1(text);
        return false;
    };
    if (isSymlink)
        return fail("The distro EFI file is a symlink.");
    if (!isRegular)
        return fail("The distro EFI path is not a regular file.");
    if (uid != 0)
        return fail("The distro EFI file is not owned by root.");
    // Group- or world-writable. 022 is S_IWGRP|S_IWOTH.
    if ((mode & 022u) != 0)
        return fail("The distro EFI file is writable by the group or by others.");
    if (size <= 0 || size > kMaxEfiBytes)
        return fail("The distro EFI file is empty or larger than the size cap.");
    return true;
}

bool usageListsSubcommand(const QString &usage, const QString &subcommand)
{
    if (subcommand.isEmpty())
        return false;
    const QStringList words = usage.split(QRegularExpression(QStringLiteral("\\s+")),
                                          Qt::SkipEmptyParts);
    return words.contains(subcommand);
}

TrustedPayload trustedPayload(const Request &request, const QByteArray &staged,
                              const QByteArray &onDisk, const QString &cpuArch,
                              PublisherFetch *fetch, DistroBytes *distro)
{
    if (request.action != QLatin1String("install"))
        return refuse(QStringLiteral("The request is not an install."));
    if (!isKnownUtility(request.utility))
        return refuse(QStringLiteral("The request is not for a known EFI utility."));
    const ManagedFile dest = managedFileFor(request.utility, request.asset);
    if (!dest.ok)
        return refuse(dest.error);
    if (!assetFitsArch(request.utility, request.asset, cpuArch)) {
        return refuse(QStringLiteral(
            "That EFI file does not match this computer's architecture."));
    }

    if (request.utility == QLatin1String(kGdiskId)) {
        return matchingBytes(staged, onDisk, gdiskMemberSha256(), gdiskVersion(), request.asset);
    }
    if (request.utility == QLatin1String(kHashToolId)) {
        return matchingBytes(staged, onDisk, hashToolSha256(), hashToolVersion(), request.asset);
    }
    if (isDistroTool(request.utility)) {
        if (!distro)
            return refuse(QStringLiteral("There is no distro file source for this tool."));
        const LocalToolFile local = distro->read(request.utility, cpuArch);
        if (!local.ok)
            return refuse(local.error);
        if (local.asset != request.asset || !isSha256Hex(local.sha256)
            || !digestsEqual(sha256Hex(local.bytes), local.sha256)) {
            return refuse(QStringLiteral(
                "The distro EFI file could not be verified."));
        }
        TrustedPayload out;
        out.ok = true;
        out.bytes = local.bytes;
        out.sha256 = local.sha256.toLower();
        out.version = QStringLiteral("local");
        out.asset = local.asset;
        return out;
    }
    if (request.utility == QLatin1String(kMemtestId)) {
        const TrustedPayload extracted = fromMemtest(request, cpuArch, fetch);
        if (!extracted.ok)
            return extracted;
        if (extracted.asset != request.asset) {
            return refuse(QStringLiteral(
                "The request names a different file than the one the helper verified."));
        }
        return extracted;
    }
    if (request.utility == QLatin1String(kUtilityId))
        return fromGithubRelease(request, staged, onDisk, fetch, true);
    if (request.utility == QLatin1String(kShellId))
        return fromGithubRelease(request, staged, onDisk, fetch, false);
    return refuse(QStringLiteral("That utility has no trusted source."));
}

QByteArray CurlPublisherFetch::get(const QString &url, QString *error)
{
    if (!publisherUrlAllowed(url)) {
        if (error)
            *error = QStringLiteral("That download URL is not one this helper fetches.");
        return {};
    }
    // Absolute system locations only, never a PATH lookup: this runs as root
    // (the Linux helper) or elevated (the Windows GUI).
#ifdef Q_OS_WIN
    const QStringList curlDirs = {QDir::fromNativeSeparators(
        QString::fromLocal8Bit(qgetenv("SystemRoot")) + QLatin1String("/System32"))};
#else
    const QStringList curlDirs = {QStringLiteral("/usr/bin"), QStringLiteral("/usr/sbin"),
                                  QStringLiteral("/bin"), QStringLiteral("/sbin")};
#endif
    const QString curlExe = QStandardPaths::findExecutable(QStringLiteral("curl"), curlDirs);
    if (curlExe.isEmpty()) {
        if (error)
            *error = QStringLiteral("curl was not found in the system directories.");
        return {};
    }
    QProcess curl;
    curl.start(curlExe,
               {QStringLiteral("--fail"), QStringLiteral("--silent"), QStringLiteral("--show-error"),
                QStringLiteral("--location"), QStringLiteral("--proto"), QStringLiteral("=https"),
                QStringLiteral("--max-redirs"), QStringLiteral("3"), QStringLiteral("--max-time"),
                QStringLiteral("30"), QStringLiteral("--max-filesize"), QStringLiteral("16777216"),
                url});
    if (!curl.waitForStarted(5000)) {
        if (error)
            *error = QStringLiteral(
                "curl could not be started to fetch the publisher checksum.");
        return {};
    }
    if (!curl.waitForFinished(35000) || curl.exitStatus() != QProcess::NormalExit
        || curl.exitCode() != 0) {
        if (error)
            *error = QStringLiteral("The publisher file could not be downloaded.");
        return {};
    }
    const QByteArray body = curl.readAllStandardOutput();
    if (body.isEmpty() && error)
        *error = QStringLiteral("The publisher file was empty.");
    return body;
}

LocalToolFile FilesystemDistroBytes::read(const QString &utilityId, const QString &cpuArch)
{
    LocalToolFile out;
    const QStringList paths = localToolPaths(utilityId, cpuArch);
    if (paths.isEmpty()) {
        out.error = QStringLiteral("This tool has no distro package path on this architecture.");
        return out;
    }
    for (const QString &path : paths) {
        if (!isAllowedLocalToolPath(path))
            continue;
        const QFileInfo info(path);
        if (!info.exists())
            continue;
        QString why;
        const quint32 mode = (info.permission(QFileDevice::WriteGroup) ? 020u : 0u)
            | (info.permission(QFileDevice::WriteOther) ? 002u : 0u);
        if (!distroStatAcceptable(info.isSymLink(), info.isFile() && !info.isSymLink(),
                                  info.ownerId(), mode, info.size(), &why)) {
            continue;
        }
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            continue;
        const QByteArray bytes = file.readAll();
        if (bytes.size() != info.size())
            continue;
        out.ok = true;
        out.path = path;
        out.asset = info.fileName();
        out.bytes = bytes;
        out.sha256 = sha256Hex(bytes);
        return out;
    }
    out.error = QStringLiteral(
        "The distro package that ships this EFI binary is not installed, "
        "or the file is not a root-owned regular file.");
    return out;
}

} // namespace EfiUtil
