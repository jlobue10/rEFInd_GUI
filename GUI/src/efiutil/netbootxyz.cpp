#include "netbootxyz.h"

#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QUrl>

namespace EfiUtil {

namespace {

const char *kAssets[] = {
    "netboot.xyz.efi",
    "netboot.xyz-snp.efi",
    "netboot.xyz-snponly.efi",
    "netboot.xyz-arm64.efi",
    "netboot.xyz-arm64-snp.efi",
    "netboot.xyz-arm64-snponly.efi",
};

bool isArm(const QString &cpuArch)
{
    return cpuArch == QLatin1String("arm64")
        || cpuArch == QLatin1String("aarch64");
}

bool knownArch(const QString &cpuArch)
{
    return cpuArch == QLatin1String("x86_64") || isArm(cpuArch);
}

bool hex64(const QString &s)
{
    static const QRegularExpression re(QStringLiteral("^[0-9a-fA-F]{64}$"));
    return re.match(s).hasMatch();
}

} // namespace

bool isNetbootXyzAsset(const QString &name)
{
    for (const char *a : kAssets) {
        if (name == QLatin1String(a))
            return true;
    }
    return false;
}

QList<Variant> netbootVariants(const QString &cpuArch)
{
    if (!knownArch(cpuArch))
        return {};
    const bool arm = isArm(cpuArch);
    return {
        {QStringLiteral("standard"), QStringLiteral("Standard UEFI"),
         arm ? QStringLiteral("netboot.xyz-arm64.efi")
             : QStringLiteral("netboot.xyz.efi")},
        {QStringLiteral("snp"), QStringLiteral("SNP"),
         arm ? QStringLiteral("netboot.xyz-arm64-snp.efi")
             : QStringLiteral("netboot.xyz-snp.efi")},
        {QStringLiteral("snponly"), QStringLiteral("SNP-only"),
         arm ? QStringLiteral("netboot.xyz-arm64-snponly.efi")
             : QStringLiteral("netboot.xyz-snponly.efi")},
    };
}

QString assetForVariant(const QString &variantId, const QString &cpuArch)
{
    for (const Variant &v : netbootVariants(cpuArch)) {
        if (v.id == variantId)
            return v.asset;
    }
    return {};
}

GithubRelease parseGithubRelease(const QByteArray &json)
{
    GithubRelease out;
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        out.error = QStringLiteral("The release response was not a JSON object.");
        return out;
    }
    const QJsonObject root = doc.object();
    if (root.value(QStringLiteral("prerelease")).toBool(false)) {
        out.error = QStringLiteral("The release is marked as a prerelease.");
        return out;
    }
    out.tag = root.value(QStringLiteral("tag_name")).toString().trimmed();
    if (out.tag.isEmpty()) {
        out.error = QStringLiteral("The release has no tag_name.");
        return out;
    }
    const QJsonArray assets = root.value(QStringLiteral("assets")).toArray();
    if (assets.isEmpty()) {
        out.error = QStringLiteral("The release lists no assets.");
        return out;
    }
    for (const QJsonValue &v : assets) {
        if (!v.isObject())
            continue;
        const QJsonObject o = v.toObject();
        GithubAsset a;
        a.name = o.value(QStringLiteral("name")).toString();
        a.url = o.value(QStringLiteral("browser_download_url")).toString();
        a.size = qint64(o.value(QStringLiteral("size")).toDouble(-1));
        if (a.name.isEmpty() || a.url.isEmpty())
            continue;
        const QUrl url(a.url);
        if (url.scheme() != QLatin1String("https")
            || url.host() != QLatin1String("github.com")
            || !url.path().endsWith(QLatin1Char('/') + a.name)) {
            out.error = QStringLiteral("Asset \"%1\" has an unexpected download URL.")
                            .arg(a.name);
            return out;
        }
        out.assets.append(a);
    }
    if (!findAsset(out, QLatin1String(kChecksumAsset))) {
        out.error = QStringLiteral(
            "The release does not publish netboot.xyz-sha256-checksums.txt.");
        out.assets.clear();
        return out;
    }
    out.ok = true;
    return out;
}

const GithubAsset *findAsset(const GithubRelease &release, const QString &name)
{
    for (const GithubAsset &a : release.assets) {
        if (a.name == name)
            return &a;
    }
    return nullptr;
}

ChecksumFile parseSha256Sums(const QString &text)
{
    ChecksumFile out;
    const QStringList lines = text.split(QRegularExpression(QStringLiteral("\\r?\\n")));
    // "HEX *name" (binary mode) or "HEX  name" (text mode).
    static const QRegularExpression row(
        QStringLiteral("^([0-9a-fA-F]{64}) [ *](\\S+)\\s*$"));
    for (QString line : lines) {
        if (line.endsWith(QLatin1Char('\r')))
            line.chop(1);
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
            continue;
        const QRegularExpressionMatch m = row.match(line);
        if (!m.hasMatch()) {
            out.error = QStringLiteral("The checksum file has an unreadable line.");
            out.sums.clear();
            return out;
        }
        out.sums.append({m.captured(2), m.captured(1).toLower()});
    }
    if (out.sums.isEmpty()) {
        out.error = QStringLiteral("The checksum file listed no files.");
        return out;
    }
    out.ok = true;
    return out;
}

QString checksumFor(const ChecksumFile &file, const QString &assetName)
{
    for (const auto &row : file.sums) {
        if (row.first == assetName)
            return row.second;
    }
    return {};
}

QByteArray sha256(const QByteArray &data)
{
    return QCryptographicHash::hash(data, QCryptographicHash::Sha256);
}

QString sha256Hex(const QByteArray &data)
{
    return QString::fromLatin1(sha256(data).toHex());
}

bool digestsEqual(const QString &installed, const QString &release)
{
    return hex64(installed) && hex64(release)
        && installed.compare(release, Qt::CaseInsensitive) == 0;
}

} // namespace EfiUtil
