#include "catalog.h"

#include "netbootxyz.h"

#include <QFile>
#include <QFileInfo>
#include <QSysInfo>

namespace EfiUtil {

namespace {

bool isArm(const QString &cpuArch)
{
    return cpuArch == QLatin1String("arm64") || cpuArch == QLatin1String("aarch64");
}

bool isX64(const QString &cpuArch)
{
    return cpuArch == QLatin1String("x86_64");
}

bool isIa32(const QString &cpuArch)
{
    return cpuArch == QLatin1String("i386");
}

bool safeAsset(const QString &asset)
{
    if (asset.isEmpty() || asset.contains(QLatin1Char('/')) || asset.contains(QLatin1Char('\\'))
        || asset.contains(QLatin1String("..")) || asset.startsWith(QLatin1Char('.')))
        return false;
    return true;
}

ManagedFile refuse(const QString &error)
{
    ManagedFile file;
    file.error = error;
    return file;
}

ManagedFile accept(const QString &utilityId, const QString &asset, const QString &fileName,
                   const QString &sidecar, const QString &token)
{
    ManagedFile file;
    file.ok = true;
    file.utilityId = utilityId;
    file.asset = asset;
    file.fileName = fileName;
    file.sidecarName = sidecar;
    file.showtoolToken = token;
    file.relativePath = QStringLiteral("%1/%2").arg(QLatin1String(kToolsDir), fileName);
    return file;
}

bool memtestAsset(const QString &asset, QString *archToken)
{
    static const QRegularExpression re(QStringLiteral("^mt86p_[0-9]+_(x86_64|i586)$"));
    const QRegularExpressionMatch m = re.match(asset);
    if (!m.hasMatch())
        return false;
    *archToken = m.captured(1);
    return true;
}

} // namespace

QStringList utilityIds()
{
    return {QLatin1String(kUtilityId),     QLatin1String(kShellId),
            QLatin1String(kMemtestId),     QLatin1String(kHashToolId),
            QLatin1String(kMokManagerId),  QLatin1String(kKeyToolId),
            QLatin1String(kGdiskId),       QLatin1String(kFwupdId)};
}

bool isKnownUtility(const QString &utilityId)
{
    return utilityIds().contains(utilityId);
}

QString sidecarNameFor(const QString &utilityId)
{
    if (!isKnownUtility(utilityId))
        return {};
    if (utilityId == QLatin1String(kUtilityId))
        return QLatin1String(kSidecarName);
    return QStringLiteral("rEFInd_GUI-%1").arg(utilityId);
}

QString showtoolTokenFor(const QString &utilityId)
{
    if (utilityId == QLatin1String(kUtilityId))
        return QLatin1String(kShowtoolToken);
    if (utilityId == QLatin1String(kShellId))
        return QStringLiteral("shell");
    if (utilityId == QLatin1String(kMemtestId))
        return QStringLiteral("memtest");
    if (utilityId == QLatin1String(kGdiskId))
        return QStringLiteral("gdisk");
    if (utilityId == QLatin1String(kFwupdId))
        return QStringLiteral("fwupdate");
    if (utilityId == QLatin1String(kHashToolId) || utilityId == QLatin1String(kMokManagerId)
        || utilityId == QLatin1String(kKeyToolId))
        return QStringLiteral("mok_tool");
    return {};
}

ManagedFile managedFileFor(const QString &utilityId, const QString &asset)
{
    if (!safeAsset(asset) || asset == QLatin1String("ipxe_discover.efi")
        || asset == QLatin1String("ipxe.efi"))
        return refuse(QStringLiteral("That asset is not an EFI tool this feature installs."));
    const QString sidecar = sidecarNameFor(utilityId);
    const QString token = showtoolTokenFor(utilityId);
    if (sidecar.isEmpty() || token.isEmpty())
        return refuse(QStringLiteral("That utility is not one this feature installs."));

    if (utilityId == QLatin1String(kUtilityId)) {
        if (!isNetbootXyzAsset(asset))
            return refuse(QStringLiteral("That asset is not a netboot.xyz UEFI executable."));
        return accept(utilityId, asset, QLatin1String(kInstalledEfiName), sidecar, token);
    }
    if (utilityId == QLatin1String(kShellId)) {
        if (asset != QLatin1String("shellx64.efi") && asset != QLatin1String("shellaa64.efi")
            && asset != QLatin1String("shellia32.efi"))
            return refuse(QStringLiteral("That asset is not an EDK2 shell rEFInd launches."));
        return accept(utilityId, asset, asset, sidecar, token);
    }
    if (utilityId == QLatin1String(kMemtestId)) {
        QString archToken;
        if (!memtestAsset(asset, &archToken))
            return refuse(QStringLiteral("That asset is not a memtest86+ binary."));
        const QString installed = archToken == QLatin1String("i586")
            ? QStringLiteral("memtest86+ia32.efi")
            : QStringLiteral("memtest86+x64.efi");
        return accept(utilityId, asset, installed, sidecar, token);
    }
    if (utilityId == QLatin1String(kGdiskId)) {
        if (asset != QLatin1String("gdisk_x64.efi"))
            return refuse(QStringLiteral("That asset is not the GPT fdisk EFI build."));
        return accept(utilityId, asset, asset, sidecar, token);
    }
    if (utilityId == QLatin1String(kFwupdId)) {
        QString installed;
        if (asset == QLatin1String("fwupdx64.efi"))
            installed = QStringLiteral("fwupx64.efi");
        else if (asset == QLatin1String("fwupdaa64.efi"))
            installed = QStringLiteral("fwupaa64.efi");
        else if (asset == QLatin1String("fwupdia32.efi") || asset == QLatin1String("fwupia32.efi"))
            installed = QStringLiteral("fwupia32.efi");
        else
            return refuse(QStringLiteral("That asset is not an fwupd EFI binary."));
        return accept(utilityId, asset, installed, sidecar, token);
    }
    if (utilityId == QLatin1String(kHashToolId)) {
        if (asset != QLatin1String("HashTool.efi"))
            return refuse(QStringLiteral("That asset is not HashTool.efi."));
        return accept(utilityId, asset, asset, sidecar, token);
    }
    if (utilityId == QLatin1String(kMokManagerId)) {
        if (asset != QLatin1String("mmx64.efi") && asset != QLatin1String("mmaa64.efi")
            && asset != QLatin1String("mmia32.efi") && asset != QLatin1String("MokManager.efi"))
            return refuse(QStringLiteral("That asset is not a MokManager binary rEFInd launches."));
        return accept(utilityId, asset, asset, sidecar, token);
    }
    if (utilityId == QLatin1String(kKeyToolId)) {
        if (asset != QLatin1String("KeyTool.efi") && asset != QLatin1String("KeyTool-signed.efi"))
            return refuse(QStringLiteral("That asset is not KeyTool."));
        return accept(utilityId, asset, asset, sidecar, token);
    }
    return refuse(QStringLiteral("That utility is not one this feature installs."));
}

QString defaultAssetFor(const QString &utilityId, const QString &cpuArch)
{
    if (utilityId == QLatin1String(kUtilityId))
        return assetForVariant(QStringLiteral("standard"), cpuArch);
    if (utilityId == QLatin1String(kShellId)) {
        if (isX64(cpuArch))
            return QStringLiteral("shellx64.efi");
        if (isArm(cpuArch))
            return QStringLiteral("shellaa64.efi");
        if (isIa32(cpuArch))
            return QStringLiteral("shellia32.efi");
        return {};
    }
    if (utilityId == QLatin1String(kGdiskId))
        return isX64(cpuArch) ? QStringLiteral("gdisk_x64.efi") : QString();
    if (utilityId == QLatin1String(kFwupdId)) {
        if (isX64(cpuArch))
            return QStringLiteral("fwupdx64.efi");
        if (isArm(cpuArch))
            return QStringLiteral("fwupdaa64.efi");
        if (isIa32(cpuArch))
            return QStringLiteral("fwupdia32.efi");
        return {};
    }
    if (utilityId == QLatin1String(kHashToolId))
        return isX64(cpuArch) ? QStringLiteral("HashTool.efi") : QString();
    if (utilityId == QLatin1String(kMokManagerId)) {
        if (isX64(cpuArch))
            return QStringLiteral("mmx64.efi");
        if (isArm(cpuArch))
            return QStringLiteral("mmaa64.efi");
        if (isIa32(cpuArch))
            return QStringLiteral("mmia32.efi");
        return {};
    }
    if (utilityId == QLatin1String(kKeyToolId)) {
        if (isX64(cpuArch) || isArm(cpuArch) || isIa32(cpuArch))
            return QStringLiteral("KeyTool.efi");
        return {};
    }
    return {};
}

QString installedNameForArch(const QString &utilityId, const QString &cpuArch)
{
    if (utilityId == QLatin1String(kUtilityId))
        return utilitySupportsArch(utilityId, cpuArch) ? QLatin1String(kInstalledEfiName) : QString();
    if (utilityId == QLatin1String(kMemtestId)) {
        if (isX64(cpuArch))
            return QStringLiteral("memtest86+x64.efi");
        if (isIa32(cpuArch))
            return QStringLiteral("memtest86+ia32.efi");
        return {};
    }
    const QString asset = defaultAssetFor(utilityId, cpuArch);
    if (asset.isEmpty())
        return {};
    return managedFileFor(utilityId, asset).fileName;
}

bool utilitySupportsArch(const QString &utilityId, const QString &cpuArch)
{
    if (utilityId == QLatin1String(kMemtestId))
        return isX64(cpuArch) || isIa32(cpuArch);
    if (utilityId == QLatin1String(kUtilityId))
        return !netbootVariants(cpuArch).isEmpty();
    return !defaultAssetFor(utilityId, cpuArch).isEmpty();
}

bool assetFitsArch(const QString &utilityId, const QString &asset, const QString &cpuArch)
{
    const ManagedFile file = managedFileFor(utilityId, asset);
    if (!file.ok || !utilitySupportsArch(utilityId, cpuArch))
        return false;
    if (utilityId == QLatin1String(kUtilityId)) {
        const bool armAsset = asset.contains(QLatin1String("arm64"));
        return isArm(cpuArch) ? armAsset : !armAsset;
    }
    if (utilityId == QLatin1String(kMemtestId)) {
        QString archToken;
        if (!memtestAsset(asset, &archToken))
            return false;
        if (isX64(cpuArch))
            return archToken == QLatin1String("x86_64");
        if (isIa32(cpuArch))
            return archToken == QLatin1String("i586");
        return false;
    }
    if (utilityId == QLatin1String(kMokManagerId) && asset == QLatin1String("MokManager.efi"))
        return isX64(cpuArch);
    if (utilityId == QLatin1String(kKeyToolId))
        return true;
    return asset == defaultAssetFor(utilityId, cpuArch)
        || file.fileName == installedNameForArch(utilityId, cpuArch);
}

QStringList localToolPaths(const QString &utilityId, const QString &cpuArch)
{
    QStringList paths;
    const auto add = [&paths](const char *path) { paths << QLatin1String(path); };
    if (utilityId == QLatin1String(kFwupdId)) {
        if (isX64(cpuArch)) {
            add("/usr/lib/fwupd/efi/fwupdx64.efi");
            add("/usr/lib64/fwupd/efi/fwupdx64.efi");
            add("/usr/libexec/fwupd/efi/fwupdx64.efi");
        } else if (isArm(cpuArch)) {
            add("/usr/lib/fwupd/efi/fwupdaa64.efi");
            add("/usr/lib64/fwupd/efi/fwupdaa64.efi");
            add("/usr/libexec/fwupd/efi/fwupdaa64.efi");
        } else if (isIa32(cpuArch)) {
            add("/usr/lib/fwupd/efi/fwupdia32.efi");
            add("/usr/lib64/fwupd/efi/fwupdia32.efi");
            add("/usr/libexec/fwupd/efi/fwupdia32.efi");
        }
    } else if (utilityId == QLatin1String(kMokManagerId)) {
        const char *name = nullptr;
        if (isX64(cpuArch))
            name = "mmx64.efi";
        else if (isArm(cpuArch))
            name = "mmaa64.efi";
        else if (isIa32(cpuArch))
            name = "mmia32.efi";
        if (name) {
            const QString file = QLatin1String(name);
            paths << QStringLiteral("/usr/lib/shim/") + file
                  << QStringLiteral("/usr/share/shim/") + file
                  << QStringLiteral("/usr/share/shim-signed/") + file;
        }
        if (isX64(cpuArch)) {
            add("/usr/lib/shim/MokManager.efi");
            add("/usr/share/shim/MokManager.efi");
            add("/usr/share/shim-signed/MokManager.efi");
        }
    } else if (utilityId == QLatin1String(kKeyToolId)) {
        if (isX64(cpuArch) || isArm(cpuArch) || isIa32(cpuArch)) {
            add("/usr/share/efitools/efi/KeyTool-signed.efi");
            add("/usr/share/efitools/efi/KeyTool.efi");
            add("/usr/lib/efitools/KeyTool.efi");
        }
    }
    return paths;
}

bool isAllowedLocalToolPath(const QString &path)
{
    if (path.contains(QLatin1String("..")) || path.contains(QLatin1Char('\\')))
        return false;
    const QStringList ids = {QLatin1String(kFwupdId), QLatin1String(kMokManagerId),
                             QLatin1String(kKeyToolId)};
    const QStringList archs = {QStringLiteral("x86_64"), QStringLiteral("aarch64"),
                               QStringLiteral("i386")};
    for (const QString &id : ids) {
        for (const QString &arch : archs) {
            if (localToolPaths(id, arch).contains(path))
                return true;
        }
    }
    return false;
}

LocalToolFile readLocalTool(const QString &utilityId, const QString &cpuArch)
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
        if (!info.exists() || info.isSymLink() || !info.isFile())
            continue;
        if (info.size() <= 0 || info.size() > kMaxEfiBytes)
            continue;
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
        "The distro package that ships this EFI binary is not installed.");
    return out;
}

QString gdiskZipUrl()
{
    return QStringLiteral(
        "https://sourceforge.net/projects/gptfdisk/files/gptfdisk/1.0.4/"
        "gdisk-binaries/gdisk-efi-1.0.4.zip/download");
}

QString gdiskZipSha256()
{
    return QStringLiteral("c97d2aa71f3c24624a92fbbb093a7695bc1f39218e9d290c2108072e6f3e00ae");
}

QString gdiskMemberName()
{
    return QStringLiteral("gdisk-efi/gdisk_x64.efi");
}

QString gdiskMemberSha256()
{
    return QStringLiteral("28c5deb0b7f6675d3b32f9339002028aa7cfc8ae2719241da32c7bd201e7f274");
}

QString gdiskVersion()
{
    return QStringLiteral("1.0.4");
}

QString hashToolUrl()
{
    return QStringLiteral("https://blog.hansenpartnership.com/wp-uploads/2013/HashTool.efi");
}

QString hashToolSha256()
{
    return QStringLiteral("919f3561eea5cb20681d90986b1ced7b627aead09496194325f681c7ccc14468");
}

QString hashToolVersion()
{
    return QStringLiteral("2013");
}

MemtestLocation memtestLocation(const QString &tag, const QString &cpuArch)
{
    MemtestLocation loc;
    static const QRegularExpression tagRe(QStringLiteral("^v([0-9]+\\.[0-9]+)$"));
    const QRegularExpressionMatch m = tagRe.match(tag);
    if (!m.hasMatch()) {
        loc.error = QStringLiteral("The memtest86+ tag is not a stable version.");
        return loc;
    }
    if (!utilitySupportsArch(QLatin1String(kMemtestId), cpuArch)) {
        loc.error = QStringLiteral("memtest86+ publishes no EFI binary for this architecture.");
        return loc;
    }
    const QString ver = m.captured(1);
    loc.tag = tag;
    loc.zipName = QStringLiteral("mt86plus_%1.binaries.zip").arg(ver);
    loc.checksumUrl = QStringLiteral("https://memtest.org/download/%1/sha256sum.txt").arg(tag);
    loc.zipUrl = QStringLiteral("https://memtest.org/download/%1/%2").arg(tag, loc.zipName);
    const QString suffix = isIa32(cpuArch) ? QStringLiteral("i586") : QStringLiteral("x86_64");
    loc.memberPattern = QRegularExpression(QStringLiteral("^mt86p_[0-9]+_%1$").arg(suffix));
    loc.ok = loc.memberPattern.isValid();
    if (!loc.ok)
        loc.error = QStringLiteral("The memtest86+ member pattern is invalid.");
    return loc;
}

QString shellReleaseApiUrl()
{
    return QStringLiteral("https://api.github.com/repos/pbatard/UEFI-Shell/releases/latest");
}

QString memtestReleaseApiUrl()
{
    return QStringLiteral(
        "https://api.github.com/repos/memtest86plus/memtest86plus/releases/latest");
}

} // namespace EfiUtil
