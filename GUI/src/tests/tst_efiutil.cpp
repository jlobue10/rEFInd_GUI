// EFI utility manager: release parsing, checksums, temp-dir install/remove,
// showtools edits, and Secure Boot efivar bytes. No network and no real ESP.

#include "efiutil/catalog.h"
#include "efiutil/helpercmd.h"
#include "efiutil/install.h"
#include "efiutil/netbootxyz.h"
#include "efiutil/request.h"
#include "efiutil/secureboot.h"
#include "efiutil/showtools.h"
#include "efiutil/trust.h"
#include "efiutil/zipmember.h"
#include "espops/espconstants.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QTemporaryDir>

#include <QtTest>

#include <zlib.h>

using namespace EfiUtil;

namespace {

const char kHelloSha[] =
    "2cf24dba5fb0a30e26e83b2ac5b9e29e1b161e5c1fa7425e73043362938b9824";

QByteArray readFixture(const char *name)
{
    QFile file(QString::fromLatin1(EFIUTIL_FIXTURE_DIR) + QLatin1Char('/')
               + QLatin1String(name));
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}

QString makeEsp(QTemporaryDir &dir)
{
    QDir(dir.path()).mkpath(QStringLiteral("EFI/tools"));
    return dir.path();
}

UtilityRecord recordFor(const QByteArray &payload, const QString &version = QStringLiteral("3.0.3"),
                        const QString &asset = QStringLiteral("netboot.xyz.efi"))
{
    UtilityRecord rec;
    rec.product = QLatin1String(EspOps::kProductName);
    rec.id = QLatin1String(kUtilityId);
    rec.version = version;
    rec.asset = asset;
    rec.sha256 = sha256Hex(payload);
    rec.valid = true;
    return rec;
}

QStringList toolNames(const QString &root)
{
    return QDir(root + QStringLiteral("/EFI/tools"))
        .entryList(QDir::Files | QDir::Hidden | QDir::System, QDir::Name);
}

void append16(QByteArray *out, quint16 value)
{
    out->append(char(value & 0xff));
    out->append(char((value >> 8) & 0xff));
}

void append32(QByteArray *out, quint32 value)
{
    append16(out, quint16(value & 0xffff));
    append16(out, quint16(value >> 16));
}

QByteArray storedZip(const QString &name, const QByteArray &data)
{
    const QByteArray encoded = name.toUtf8();
    const quint32 crc = quint32(::crc32(
        0L, reinterpret_cast<const Bytef *>(data.constData()), uInt(data.size())));
    QByteArray local;
    append32(&local, 0x04034b50u);
    append16(&local, 20);
    append16(&local, 0);
    append16(&local, 0);
    append16(&local, 0);
    append16(&local, 0);
    append32(&local, crc);
    append32(&local, quint32(data.size()));
    append32(&local, quint32(data.size()));
    append16(&local, quint16(encoded.size()));
    append16(&local, 0);
    local += encoded;
    local += data;

    QByteArray central;
    append32(&central, 0x02014b50u);
    append16(&central, 20);
    append16(&central, 20);
    append16(&central, 0);
    append16(&central, 0);
    append16(&central, 0);
    append16(&central, 0);
    append32(&central, crc);
    append32(&central, quint32(data.size()));
    append32(&central, quint32(data.size()));
    append16(&central, quint16(encoded.size()));
    append16(&central, 0);
    append16(&central, 0);
    append16(&central, 0);
    append16(&central, 0);
    append32(&central, 0);
    append32(&central, 0);
    central += encoded;

    QByteArray end;
    append32(&end, 0x06054b50u);
    append16(&end, 0);
    append16(&end, 0);
    append16(&end, 1);
    append16(&end, 1);
    append32(&end, quint32(central.size()));
    append32(&end, quint32(local.size()));
    append16(&end, 0);
    return local + central + end;
}

class MapFetch : public PublisherFetch
{
public:
    QHash<QString, QByteArray> files;
    QStringList asked;
    QByteArray get(const QString &url, QString *error) override
    {
        asked.append(url);
        if (!files.contains(url)) {
            if (error)
                *error = QStringLiteral("not in the test map");
            return {};
        }
        return files.value(url);
    }
};

class MapDistro : public DistroBytes
{
public:
    LocalToolFile file;
    int calls = 0;
    LocalToolFile read(const QString &, const QString &) override
    {
        ++calls;
        return file;
    }
};

Request installRequest(const QString &utility, const QString &asset, const QString &version,
                       const QByteArray &payload)
{
    Request req;
    req.action = QStringLiteral("install");
    req.utility = utility;
    req.asset = asset;
    req.version = version;
    req.espGuid = QStringLiteral("01234567-89ab-cdef-0123-456789abcdef");
    req.sha256 = sha256Hex(payload);
    req.ok = true;
    return req;
}

} // namespace

class TestEfiUtil : public QObject
{
    Q_OBJECT

private slots:
    void releaseJsonPicksAssetUrl()
    {
        const GithubRelease release = parseGithubRelease(readFixture("release-ok.json"));
        QVERIFY2(release.ok, qPrintable(release.error));
        QCOMPARE(release.tag, QStringLiteral("3.0.3"));
        const GithubAsset *efi = findAsset(release, QStringLiteral("netboot.xyz.efi"));
        QVERIFY(efi);
        QCOMPARE(efi->url, QStringLiteral(
            "https://github.com/netbootxyz/netboot.xyz/releases/download/3.0.3/netboot.xyz.efi"));
        QVERIFY(findAsset(release, QLatin1String(kChecksumAsset)));
        const GithubAsset *iso = findAsset(release, QStringLiteral("netboot.xyz-sb.iso"));
        QVERIFY(iso);
        QVERIFY(!isNetbootXyzAsset(iso->name));
        QVERIFY(!isNetbootXyzAsset(QStringLiteral("ipxe.efi")));
        QVERIFY(!isNetbootXyzAsset(QStringLiteral("ipxe_discover.efi")));
        QVERIFY(isNetbootXyzAsset(QStringLiteral("netboot.xyz.efi")));
        QVERIFY(isNetbootXyzAsset(QStringLiteral("netboot.xyz-arm64-snponly.efi")));
    }

    void releaseJsonRejectsMissingChecksumAsset()
    {
        const GithubRelease release = parseGithubRelease(readFixture("release-no-checksum.json"));
        QVERIFY(!release.ok);
        QVERIFY(release.assets.isEmpty());
        QVERIFY(release.error.contains(QLatin1String("checksum")));
    }

    void releaseJsonRejectsBadUrlAndPrerelease()
    {
        const GithubRelease bad = parseGithubRelease(readFixture("release-bad-url.json"));
        QVERIFY(!bad.ok);
        QVERIFY(bad.error.contains(QLatin1String("unexpected download URL")));

        const GithubRelease pre = parseGithubRelease(readFixture("release-prerelease.json"));
        QVERIFY(!pre.ok);
        QVERIFY(pre.error.contains(QLatin1String("prerelease")));
    }

    void variantsFollowCpuArch()
    {
        const QList<Variant> pc = netbootVariants(QStringLiteral("x86_64"));
        QCOMPARE(pc.size(), 3);
        QCOMPARE(assetForVariant(QStringLiteral("standard"), QStringLiteral("x86_64")),
                 QStringLiteral("netboot.xyz.efi"));
        QCOMPARE(assetForVariant(QStringLiteral("snp"), QStringLiteral("x86_64")),
                 QStringLiteral("netboot.xyz-snp.efi"));
        QCOMPARE(assetForVariant(QStringLiteral("snponly"), QStringLiteral("x86_64")),
                 QStringLiteral("netboot.xyz-snponly.efi"));

        QCOMPARE(assetForVariant(QStringLiteral("standard"), QStringLiteral("aarch64")),
                 QStringLiteral("netboot.xyz-arm64.efi"));
        QCOMPARE(assetForVariant(QStringLiteral("snponly"), QStringLiteral("arm64")),
                 QStringLiteral("netboot.xyz-arm64-snponly.efi"));
        QVERIFY(netbootVariants(QStringLiteral("power")).isEmpty());
        QVERIFY(assetForVariant(QStringLiteral("standard"), QStringLiteral("power")).isEmpty());
    }

    void checksumFileAcceptsStarAndTwoSpaces()
    {
        const QByteArray raw = readFixture("sha256sums-ok.txt");
        const QString text = QString::fromUtf8(raw);
        QVERIFY(text.contains(QStringLiteral(" *netboot.xyz.efi")));
        QVERIFY(text.contains(QStringLiteral("  netboot.xyz-snp.efi")));

        const ChecksumFile sums = parseSha256Sums(text);
        QVERIFY2(sums.ok, qPrintable(sums.error));
        QCOMPARE(checksumFor(sums, QStringLiteral("netboot.xyz.efi")),
                 QString::fromLatin1(kHelloSha));
        QCOMPARE(checksumFor(sums, QStringLiteral("netboot.xyz-snp.efi")),
                 QString(64, QLatin1Char('b')));
        QVERIFY(checksumFor(sums, QStringLiteral("missing.efi")).isEmpty());

        const QString inlineSums =
            QStringLiteral("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
                           "  netboot.xyz-snponly.efi\r\n");
        const ChecksumFile crlf = parseSha256Sums(inlineSums);
        QVERIFY(crlf.ok);
        QCOMPARE(checksumFor(crlf, QStringLiteral("netboot.xyz-snponly.efi")).size(), 64);
    }

    void checksumFileRejectsTruncatedAndMalformed()
    {
        const ChecksumFile bad = parseSha256Sums(QString::fromUtf8(readFixture("sha256sums-bad.txt")));
        QVERIFY(!bad.ok);
        QVERIFY(bad.sums.isEmpty());

        const ChecksumFile oneSpace = parseSha256Sums(
            QString::fromLatin1(kHelloSha) + QStringLiteral(" netboot.xyz.efi\n"));
        QVERIFY(!oneSpace.ok);

        const ChecksumFile empty = parseSha256Sums(QStringLiteral("# only a comment\n\n"));
        QVERIFY(!empty.ok);
    }

    void sha256OfHelloAndDownloadSkip()
    {
        QCOMPARE(sha256Hex(QByteArray("hello")), QString::fromLatin1(kHelloSha));
        QVERIFY(digestsEqual(QString::fromLatin1(kHelloSha),
                             QString::fromLatin1(kHelloSha).toUpper()));
        QVERIFY(!digestsEqual(QStringLiteral("abc"), QString::fromLatin1(kHelloSha)));
        QVERIFY(!efiPayloadDownloadNeeded(QString::fromLatin1(kHelloSha),
                                          QString::fromLatin1(kHelloSha).toUpper()));
        QVERIFY(efiPayloadDownloadNeeded(QString(), QString::fromLatin1(kHelloSha)));
        QVERIFY(efiPayloadDownloadNeeded(QString::fromLatin1(kHelloSha), QString(64, QLatin1Char('a'))));
    }

    void skipEfiDownloadWhenManagedShaMatches()
    {
        const QByteArray payload("hello");
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString root = makeEsp(dir);
        UtilityRecord rec = recordFor(payload);
        QCOMPARE(installPayload(root, payload, rec, false).exitCode, kExitOk);

        rec.version = QStringLiteral("3.0.4");
        QVERIFY(!efiPayloadDownloadNeeded(sha256Hex(payload), rec.sha256));
        QFile existing(root + QStringLiteral("/EFI/tools/ipxe.efi"));
        QVERIFY(existing.open(QIODevice::ReadOnly));
        const QByteArray onDisk = existing.readAll();
        existing.close();
        const FileOutcome again = installPayload(root, onDisk, rec, false);
        QCOMPARE(again.exitCode, kExitOk);

        QFile after(root + QStringLiteral("/EFI/tools/ipxe.efi"));
        QVERIFY(after.open(QIODevice::ReadOnly));
        QCOMPARE(after.readAll(), payload);
        const DetectedInstall found = detectInstall(root);
        QCOMPARE(found.presence, Presence::Managed);
        QCOMPARE(found.record.version, QStringLiteral("3.0.4"));
        QCOMPARE(found.record.asset, QStringLiteral("netboot.xyz.efi"));
    }

    void installWritesOnlyManagedFiles()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString root = makeEsp(dir);
        const QByteArray shell("shell-bytes");
        const QByteArray discover("discover-bytes");
        QVERIFY(QFile(root + QStringLiteral("/EFI/tools/shell.efi")).open(QIODevice::WriteOnly));
        {
            QFile f(root + QStringLiteral("/EFI/tools/shell.efi"));
            QVERIFY(f.open(QIODevice::WriteOnly));
            QCOMPARE(f.write(shell), shell.size());
        }
        {
            QFile f(root + QStringLiteral("/EFI/tools/ipxe_discover.efi"));
            QVERIFY(f.open(QIODevice::WriteOnly));
            QCOMPARE(f.write(discover), discover.size());
        }

        const QByteArray payload("hello");
        const FileOutcome o = installPayload(root, payload, recordFor(payload), false);
        QCOMPARE(o.exitCode, kExitOk);
        QCOMPARE(toolNames(root),
                 QStringList({QStringLiteral("ipxe.efi"),
                              QStringLiteral("ipxe_discover.efi"),
                              QStringLiteral("rEFInd_GUI-netboot.xyz"),
                              QStringLiteral("shell.efi")}));
        QCOMPARE(managedRelativePath(), QStringLiteral("EFI/tools/ipxe.efi"));
        QVERIFY(!QFile::exists(root + QStringLiteral("/EFI/netboot.xyz")));

        QFile bin(root + QStringLiteral("/EFI/tools/ipxe.efi"));
        QVERIFY(bin.open(QIODevice::ReadOnly));
        QCOMPARE(bin.readAll(), payload);
        QFile side(root + QStringLiteral("/EFI/tools/rEFInd_GUI-netboot.xyz"));
        QVERIFY(side.open(QIODevice::ReadOnly));
        const UtilityRecord rec = parseSidecar(QString::fromUtf8(side.readAll()));
        QVERIFY(rec.valid);
        QCOMPARE(rec.sha256, QString::fromLatin1(kHelloSha));
        QCOMPARE(detectInstall(root).presence, Presence::Managed);

        QFile shellFile(root + QStringLiteral("/EFI/tools/shell.efi"));
        QVERIFY(shellFile.open(QIODevice::ReadOnly));
        QCOMPARE(shellFile.readAll(), shell);
        QFile discoverFile(root + QStringLiteral("/EFI/tools/ipxe_discover.efi"));
        QVERIFY(discoverFile.open(QIODevice::ReadOnly));
        QCOMPARE(discoverFile.readAll(), discover);
    }

    void installRefusesUnconfirmedOverwrite()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString root = makeEsp(dir);
        const QByteArray existing("legacy-ipxe");
        {
            QFile f(root + QStringLiteral("/EFI/tools/ipxe.efi"));
            QVERIFY(f.open(QIODevice::WriteOnly));
            QCOMPARE(f.write(existing), existing.size());
        }
        QCOMPARE(detectInstall(root).presence, Presence::Unmanaged);

        const QByteArray payload("hello");
        const FileOutcome refused = installPayload(root, payload, recordFor(payload), false);
        QCOMPARE(refused.exitCode, kExitRefused);
        QFile still(root + QStringLiteral("/EFI/tools/ipxe.efi"));
        QVERIFY(still.open(QIODevice::ReadOnly));
        QCOMPARE(still.readAll(), existing);
        QVERIFY(!QFile::exists(root + QStringLiteral("/EFI/tools/rEFInd_GUI-netboot.xyz")));

        const FileOutcome replaced = installPayload(root, payload, recordFor(payload), true);
        QCOMPARE(replaced.exitCode, kExitOk);
        QCOMPARE(detectInstall(root).presence, Presence::Managed);
        QFile now(root + QStringLiteral("/EFI/tools/ipxe.efi"));
        QVERIFY(now.open(QIODevice::ReadOnly));
        QCOMPARE(now.readAll(), payload);
    }

    void changedSidecarIsNotManaged()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString root = makeEsp(dir);
        const QByteArray payload("hello");
        QCOMPARE(installPayload(root, payload, recordFor(payload), false).exitCode, kExitOk);
        QFile side(root + QStringLiteral("/EFI/tools/rEFInd_GUI-netboot.xyz"));
        QVERIFY(side.open(QIODevice::ReadOnly));
        QString text = QString::fromUtf8(side.readAll());
        side.close();
        text.replace(QString::fromLatin1(kHelloSha), QString(64, QLatin1Char('a')));
        QVERIFY(side.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QCOMPARE(side.write(text.toUtf8()), text.toUtf8().size());
        side.close();
        QCOMPARE(detectInstall(root).presence, Presence::Changed);
    }

    void removeDeletesOnlyOwnedFiles()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString root = makeEsp(dir);
        const QByteArray shell("shell-bytes");
        const QByteArray discover("discover-bytes");
        {
            QFile f(root + QStringLiteral("/EFI/tools/shell.efi"));
            QVERIFY(f.open(QIODevice::WriteOnly));
            QCOMPARE(f.write(shell), shell.size());
        }
        {
            QFile f(root + QStringLiteral("/EFI/tools/ipxe_discover.efi"));
            QVERIFY(f.open(QIODevice::WriteOnly));
            QCOMPARE(f.write(discover), discover.size());
        }
        const QByteArray payload("hello");
        QCOMPARE(installPayload(root, payload, recordFor(payload), false).exitCode, kExitOk);

        const FileOutcome removed = removeInstall(root);
        QCOMPARE(removed.exitCode, kExitOk);
        QVERIFY(!QFile::exists(root + QStringLiteral("/EFI/tools/ipxe.efi")));
        QVERIFY(!QFile::exists(root + QStringLiteral("/EFI/tools/rEFInd_GUI-netboot.xyz")));
        QCOMPARE(toolNames(root),
                 QStringList({QStringLiteral("ipxe_discover.efi"), QStringLiteral("shell.efi")}));
        QFile shellFile(root + QStringLiteral("/EFI/tools/shell.efi"));
        QVERIFY(shellFile.open(QIODevice::ReadOnly));
        QCOMPARE(shellFile.readAll(), shell);
        QFile discoverFile(root + QStringLiteral("/EFI/tools/ipxe_discover.efi"));
        QVERIFY(discoverFile.open(QIODevice::ReadOnly));
        QCOMPARE(discoverFile.readAll(), discover);
        QCOMPARE(detectInstall(root).presence, Presence::Absent);
    }

    void removeRefusesUnmanaged()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString root = makeEsp(dir);
        const QByteArray existing("legacy-ipxe");
        {
            QFile f(root + QStringLiteral("/EFI/tools/ipxe.efi"));
            QVERIFY(f.open(QIODevice::WriteOnly));
            QCOMPARE(f.write(existing), existing.size());
        }
        {
            QFile f(root + QStringLiteral("/EFI/tools/shell.efi"));
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("shell");
        }
        QCOMPARE(removeInstall(root).exitCode, kExitRefused);
        QVERIFY(QFile::exists(root + QStringLiteral("/EFI/tools/ipxe.efi")));
        QVERIFY(QFile::exists(root + QStringLiteral("/EFI/tools/shell.efi")));
    }

    void pathChecksRejectEscapeAndNonEsp()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QString error;
        QVERIFY(!looksLikeEspRoot(dir.path() + QStringLiteral("/../")
                                      + QFileInfo(dir.path()).fileName(),
                                  &error));
        QVERIFY(error.contains(QLatin1String("..")));
        QVERIFY(!looksLikeEspRoot(QStringLiteral("EFI"), &error));

        const QByteArray payload("hello");
        const FileOutcome notEsp = installPayload(dir.path(), payload, recordFor(payload), true);
        QCOMPARE(notEsp.exitCode, kExitNoEsp);
        QVERIFY(!QFileInfo(dir.path() + QStringLiteral("/EFI")).exists());
        QVERIFY(!QFile::exists(dir.path() + QStringLiteral("/EFI/tools/ipxe_discover.efi")));

        const QString root = makeEsp(dir);
        {
            QFile shell(root + QStringLiteral("/EFI/tools/shell.efi"));
            QVERIFY(shell.open(QIODevice::WriteOnly));
            QCOMPARE(shell.write("shell"), 5);
        }
        QVERIFY(QFile::link(root + QStringLiteral("/EFI/tools/shell.efi"),
                            root + QStringLiteral("/EFI/tools/ipxe.efi")));
        QVERIFY(QFileInfo(root + QStringLiteral("/EFI/tools/ipxe.efi")).isSymLink());
        const FileOutcome linked = installPayload(root, payload, recordFor(payload), true);
        QCOMPARE(linked.exitCode, kExitRefused);
        QVERIFY(QFileInfo(root + QStringLiteral("/EFI/tools/ipxe.efi")).isSymLink());
        QVERIFY(!QFile::exists(root + QStringLiteral("/EFI/tools/ipxe_discover.efi")));
    }

    void requestRejectsDiscoverDestination()
    {
        QVERIFY(isPartitionGuid(QStringLiteral("01234567-89ab-cdef-0123-456789abcdef")));
        QVERIFY(!isPartitionGuid(QStringLiteral("../01234567-89ab-cdef-0123-456789abcdef")));

        Request req;
        req.action = QStringLiteral("install");
        req.utility = QLatin1String(kUtilityId);
        req.espGuid = QStringLiteral("01234567-89ab-cdef-0123-456789abcdef");
        req.asset = QStringLiteral("netboot.xyz.efi");
        req.version = QStringLiteral("3.0.3");
        req.sha256 = QString::fromLatin1(kHelloSha);
        req.replace = false;
        const QString body = formatRequest(req);
        QVERIFY(!body.contains(QLatin1String("ipxe_discover")));
        QVERIFY(!body.contains(QLatin1String("dest=")));
        const Request round = parseRequest(body);
        QVERIFY2(round.ok, qPrintable(round.error));
        QCOMPARE(round.asset, QStringLiteral("netboot.xyz.efi"));
        QVERIFY(!round.replace);

        const Request smuggled = parseRequest(
            body + QStringLiteral("file=EFI/tools/ipxe_discover.efi\n"));
        QVERIFY(!smuggled.ok);
        QVERIFY(smuggled.error.contains(QLatin1String("unknown field")));

        QString discoverAsset = body;
        discoverAsset.replace(QStringLiteral("asset=netboot.xyz.efi"),
                              QStringLiteral("asset=ipxe_discover.efi"));
        const Request named = parseRequest(discoverAsset);
        QVERIFY(!named.ok);

        QTemporaryDir data;
        QVERIFY(data.isValid());
        QVERIFY(QDir().mkpath(data.path() + QStringLiteral("/GUI/efi-utilities")));
        QFile requestFile(data.path() + QStringLiteral("/GUI/efi-utilities/request"));
        QVERIFY(requestFile.open(QIODevice::WriteOnly));
        const QByteArray smuggledBody = (body + QStringLiteral("dest=ipxe_discover.efi\n")).toUtf8();
        QCOMPARE(requestFile.write(smuggledBody), smuggledBody.size());
        requestFile.close();
        QString output;
        QCOMPARE(runEfiUtilityInProcess(data.path(), &output), kExitBadRequest);
        QVERIFY(output.contains(QLatin1String("unknown field")));
        QVERIFY(!output.contains(QLatin1String("ipxe.efi was")));
    }

    void showtoolsAppendsOnceAndPreservesOrder()
    {
        bool added = false;
        QCOMPARE(appendShowtoolToken(QStringLiteral("firmware,shutdown,reboot"),
                                     QStringLiteral("netboot"), &added),
                 QStringLiteral("firmware,shutdown,reboot,netboot"));
        QVERIFY(added);
        added = true;
        QCOMPARE(appendShowtoolToken(QStringLiteral("firmware,shutdown,reboot,netboot"),
                                     QStringLiteral("netboot"), &added),
                 QStringLiteral("firmware,shutdown,reboot,netboot"));
        QVERIFY(!added);

        const ShowtoolsPolicy enabled = enableShowtool(
            QStringLiteral("shell,memtest"), QStringLiteral("netboot"), false);
        QVERIFY(enabled.changed);
        QVERIFY(enabled.weAdded);
        QCOMPARE(enabled.text, QStringLiteral("shell,memtest,netboot"));

        const ShowtoolsPolicy already = enableShowtool(
            QStringLiteral("shell,netboot"), QStringLiteral("netboot"), false);
        QVERIFY(!already.changed);
        QVERIFY(!already.weAdded);
        QCOMPARE(already.text, QStringLiteral("shell,netboot"));

        const ShowtoolsPolicy kept = disableShowtool(
            QStringLiteral("shell,netboot"), QStringLiteral("netboot"), false);
        QVERIFY(!kept.changed);
        QCOMPARE(kept.text, QStringLiteral("shell,netboot"));

        const ShowtoolsPolicy removed = disableShowtool(
            QStringLiteral("shell,netboot"), QStringLiteral("netboot"), true);
        QVERIFY(removed.changed);
        QVERIFY(!removed.weAdded);
        QCOMPARE(removed.text, QStringLiteral("shell"));

        QVERIFY(showtoolsListIsEmpty(QString()));
        QVERIFY(!showtoolsListIsEmpty(QStringLiteral("shell")));
    }

    void showtoolsPatchLeavesScanforAndMenuentry()
    {
        const QString conf =
            "timeout 20\n"
            "scanfor manual\n"
            "showtools shell,memtest,gdisk\n"
            "dont_scan_tools ipxe.efi,shell.efi\n"
            "menuentry \"Linux\" {\n"
            "    loader /vmlinuz\n"
            "    options \"root=UUID=abc\"\n"
            "}\n"
            "# showtools netboot\n"
            "menuentry \"Windows\" {\n"
            "    loader /EFI/Microsoft/Boot/bootmgfw.efi\n"
            "}\n";
        QVERIFY(dontScanToolsExcludes(conf, QStringLiteral("ipxe.efi")));
        QVERIFY(!dontScanToolsExcludes(conf, QStringLiteral("memtest86.efi")));

        const ConfPatch added = patchShowtools(conf, QStringLiteral("netboot"), true);
        QVERIFY(added.changed);
        QVERIFY(added.hadDirective);
        QVERIFY(added.text.contains(QStringLiteral("showtools shell,memtest,gdisk,netboot")));
        QVERIFY(added.text.contains(QStringLiteral("scanfor manual")));
        QVERIFY(added.text.contains(QStringLiteral("dont_scan_tools ipxe.efi,shell.efi")));
        QVERIFY(added.text.contains(QStringLiteral("menuentry \"Linux\" {")));
        QVERIFY(added.text.contains(QStringLiteral("menuentry \"Windows\" {")));
        QVERIFY(added.text.contains(QStringLiteral("# showtools netboot")));
        QCOMPARE(added.text.count(QStringLiteral("netboot")), 2);

        const ConfPatch twice = patchShowtools(added.text, QStringLiteral("netboot"), true);
        QVERIFY(!twice.changed);

        bool present = false;
        QCOMPARE(liveShowtoolsTokens(added.text, &present),
                 QStringList({QStringLiteral("shell"), QStringLiteral("memtest"),
                              QStringLiteral("gdisk"), QStringLiteral("netboot")}));
        QVERIFY(present);

        const ConfPatch removed = patchShowtools(added.text, QStringLiteral("netboot"), false);
        QVERIFY(removed.changed);
        QVERIFY(removed.text.contains(QStringLiteral("showtools shell,memtest,gdisk\n")));
        QVERIFY(!liveShowtoolsTokens(removed.text, &present).contains(QStringLiteral("netboot")));
        QVERIFY(removed.text.contains(QStringLiteral("scanfor manual")));
        QVERIFY(removed.text.contains(QStringLiteral("menuentry \"Linux\" {")));

        const QString noDirective = "scanfor manual\nmenuentry \"Linux\" {\n}\n";
        const ConfPatch untouched = patchShowtools(noDirective, QStringLiteral("netboot"), true);
        QVERIFY(!untouched.changed);
        QVERIFY(!untouched.hadDirective);
        QCOMPARE(untouched.text, noDirective);

        const QString indented = "    showtools shell\n";
        const ConfPatch pad = patchShowtools(indented, QStringLiteral("netboot"), true);
        QCOMPARE(pad.text, QStringLiteral("    showtools shell,netboot\n"));
    }

    void secureBootFromEfivarBytes()
    {
        QByteArray raw(5, '\0');
        raw[4] = 1;
        QCOMPARE(secureBootFromEfivar(raw), SecureBoot::Enabled);
        raw[4] = 0;
        QCOMPARE(secureBootFromEfivar(raw), SecureBoot::Disabled);
        raw[4] = 2;
        QCOMPARE(secureBootFromEfivar(raw), SecureBoot::Unknown);
        QCOMPARE(secureBootFromEfivar(QByteArray(4, '\0')), SecureBoot::Unknown);
        QCOMPARE(secureBootFromEfivar(QByteArray()), SecureBoot::Unknown);

        const QString enabled = secureBootNotice(SecureBoot::Enabled);
        const QString disabled = secureBootNotice(SecureBoot::Disabled);
        const QString unknown = secureBootNotice(SecureBoot::Unknown);
        QVERIFY(enabled.contains(QStringLiteral("Secure Boot is enabled")));
        QVERIFY(disabled.contains(QStringLiteral("Secure Boot is off")));
        QVERIFY(unknown.contains(QStringLiteral("could not be read")));
        for (const QString &notice : {enabled, disabled, unknown}) {
            QVERIFY(notice.contains(QStringLiteral("unsigned")));
            QVERIFY(notice.contains(QStringLiteral("will not enroll keys")));
            QVERIFY(notice.contains(QStringLiteral("netboot.xyz-sb.iso")));
        }
        QVERIFY(enabled != disabled);
        QVERIFY(disabled != unknown);
    }

    void catalogMapsOfficialNamesAndRejectsDiscover()
    {
        const ManagedFile netboot = managedFileFor(QLatin1String(kUtilityId),
                                                   QStringLiteral("netboot.xyz.efi"));
        QVERIFY(netboot.ok);
        QCOMPARE(netboot.fileName, QStringLiteral("ipxe.efi"));
        QCOMPARE(netboot.relativePath, QStringLiteral("EFI/tools/ipxe.efi"));

        const ManagedFile shell = managedFileFor(QLatin1String(kShellId),
                                                 QStringLiteral("shellx64.efi"));
        QVERIFY(shell.ok);
        QCOMPARE(shell.fileName, QStringLiteral("shellx64.efi"));
        QCOMPARE(shell.showtoolToken, QStringLiteral("shell"));

        const ManagedFile fwupd = managedFileFor(QLatin1String(kFwupdId),
                                                 QStringLiteral("fwupdx64.efi"));
        QVERIFY(fwupd.ok);
        QCOMPARE(fwupd.fileName, QStringLiteral("fwupx64.efi"));
        QCOMPARE(installedNameForArch(QLatin1String(kMemtestId), QStringLiteral("x86_64")),
                 QStringLiteral("memtest86+x64.efi"));
        QCOMPARE(installedNameForArch(QLatin1String(kGdiskId), QStringLiteral("aarch64")),
                 QString());

        QVERIFY(!managedFileFor(QLatin1String(kUtilityId), QStringLiteral("ipxe_discover.efi")).ok);
        QVERIFY(!managedFileFor(QLatin1String(kShellId), QStringLiteral("../shellx64.efi")).ok);
        QVERIFY(!managedFileFor(QLatin1String(kShellId), QStringLiteral("EFI/tools/shellx64.efi")).ok);
        QVERIFY(!managedFileFor(QStringLiteral("not-a-tool"), QStringLiteral("shellx64.efi")).ok);

        QVERIFY(assetFitsArch(QLatin1String(kUtilityId), QStringLiteral("netboot.xyz.efi"),
                              QStringLiteral("x86_64")));
        QVERIFY(!assetFitsArch(QLatin1String(kUtilityId), QStringLiteral("netboot.xyz-arm64.efi"),
                               QStringLiteral("x86_64")));
        QVERIFY(assetFitsArch(QLatin1String(kMemtestId), QStringLiteral("mt86p_810_x86_64"),
                              QStringLiteral("x86_64")));
        QVERIFY(!assetFitsArch(QLatin1String(kMemtestId), QStringLiteral("mt86p_810_i586"),
                               QStringLiteral("x86_64")));
        QVERIFY(!isAllowedLocalToolPath(QStringLiteral("/tmp/fwupdx64.efi")));
        QVERIFY(!isAllowedLocalToolPath(QStringLiteral("/usr/lib/fwupd/efi/../../../etc/passwd")));
        QVERIFY(isAllowedLocalToolPath(QStringLiteral("/usr/lib/fwupd/efi/fwupdx64.efi")));
        const LocalToolFile missing = readLocalTool(QLatin1String(kFwupdId), QStringLiteral("x86_64"));
        QVERIFY(!missing.ok);
    }

    void otherToolsInstallBesideNetboot()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString root = makeEsp(dir);
        const QByteArray netboot("hello");
        QCOMPARE(installPayload(root, netboot, recordFor(netboot), false).exitCode, kExitOk);
        {
            QFile discover(root + QStringLiteral("/EFI/tools/ipxe_discover.efi"));
            QVERIFY(discover.open(QIODevice::WriteOnly));
            QCOMPARE(discover.write("discover"), 8);
        }

        UtilityRecord shell = recordFor(QByteArray("shell-bytes"), QStringLiteral("26H2"),
                                        QStringLiteral("shellx64.efi"));
        shell.id = QLatin1String(kShellId);
        QCOMPARE(installPayload(root, QByteArray("shell-bytes"), shell, false).exitCode, kExitOk);

        UtilityRecord fwupd = recordFor(QByteArray("fwupd-bytes"), QStringLiteral("local"),
                                        QStringLiteral("fwupdx64.efi"));
        fwupd.id = QLatin1String(kFwupdId);
        QCOMPARE(installPayload(root, QByteArray("fwupd-bytes"), fwupd, false).exitCode, kExitOk);

        QCOMPARE(toolNames(root),
                 QStringList({QStringLiteral("fwupx64.efi"),
                              QStringLiteral("ipxe.efi"),
                              QStringLiteral("ipxe_discover.efi"),
                              QStringLiteral("rEFInd_GUI-fwupd"),
                              QStringLiteral("rEFInd_GUI-netboot.xyz"),
                              QStringLiteral("rEFInd_GUI-shell"),
                              QStringLiteral("shellx64.efi")}));
        QVERIFY(!QFile::exists(root + QStringLiteral("/EFI/tools/fwupdx64.efi")));
        QCOMPARE(detectInstall(root, QLatin1String(kShellId)).presence, Presence::Managed);
        QCOMPARE(detectInstall(root).presence, Presence::Managed);

        QCOMPARE(removeInstall(root, QLatin1String(kShellId)).exitCode, kExitOk);
        QVERIFY(!QFile::exists(root + QStringLiteral("/EFI/tools/shellx64.efi")));
        QVERIFY(QFile::exists(root + QStringLiteral("/EFI/tools/ipxe.efi")));
        QVERIFY(QFile::exists(root + QStringLiteral("/EFI/tools/fwupx64.efi")));
        QVERIFY(QFile::exists(root + QStringLiteral("/EFI/tools/ipxe_discover.efi")));
        QCOMPARE(detectInstall(root).presence, Presence::Managed);
    }

    void requestAcceptsKnownToolsOnly()
    {
        Request shell;
        shell.action = QStringLiteral("install");
        shell.utility = QLatin1String(kShellId);
        shell.espGuid = QStringLiteral("01234567-89ab-cdef-0123-456789abcdef");
        shell.asset = QStringLiteral("shellx64.efi");
        shell.version = QStringLiteral("26H2");
        shell.sha256 = QString::fromLatin1(kHelloSha);
        const Request parsed = parseRequest(formatRequest(shell));
        QVERIFY2(parsed.ok, qPrintable(parsed.error));

        Request memtest = shell;
        memtest.utility = QLatin1String(kMemtestId);
        memtest.asset = QStringLiteral("mt86p_810_x86_64");
        QVERIFY(parseRequest(formatRequest(memtest)).ok);

        Request bad = shell;
        bad.utility = QStringLiteral("poweroff");
        QVERIFY(!parseRequest(formatRequest(bad)).ok);
        bad = shell;
        bad.asset = QStringLiteral("ipxe_discover.efi");
        QVERIFY(!parseRequest(formatRequest(bad)).ok);
    }

    void memtestChecksumAndZipMember()
    {
        const QString sums =
            QString::fromLatin1(kHelloSha) + QStringLiteral("  v8.10/mt86plus_8.10.binaries.zip\n");
        const ChecksumFile file = parseSha256Sums(sums);
        QVERIFY(file.ok);
        QCOMPARE(checksumByBasename(file, QStringLiteral("mt86plus_8.10.binaries.zip")),
                 QString::fromLatin1(kHelloSha));
        const MemtestLocation loc = memtestLocation(QStringLiteral("v8.10"), QStringLiteral("x86_64"));
        QVERIFY(loc.ok);
        QCOMPARE(loc.zipUrl, QStringLiteral(
            "https://memtest.org/download/v8.10/mt86plus_8.10.binaries.zip"));
        QVERIFY(!memtestLocation(QStringLiteral("../v8.10"), QStringLiteral("x86_64")).ok);
        QVERIFY(!memtestLocation(QStringLiteral("v8.10"), QStringLiteral("aarch64")).ok);

        const ZipMember member = extractZipMember(
            readFixture("hello-deflate.zip"),
            QRegularExpression(QStringLiteral("^hello\\.efi$")),
            kMaxEfiBytes);
        QVERIFY2(member.ok, qPrintable(member.error));
        QCOMPARE(member.bytes, QByteArray("hello"));
        const ZipMember evil = extractZipMember(
            readFixture("hello-deflate.zip"),
            QRegularExpression(QStringLiteral("evil")),
            kMaxEfiBytes);
        QVERIFY(!evil.ok);
    }

    void sidecarRequiresARealSha256()
    {
        const QString good = QStringLiteral(
            "product=rEFInd_GUI\nid=netboot.xyz\nversion=3.0.3\n"
            "asset=netboot.xyz.efi\nsha256=%1\n").arg(QString(64, QLatin1Char('a')));
        QVERIFY(parseSidecar(good).valid);

        QString bad = good;
        bad.replace(QString(64, QLatin1Char('a')), QStringLiteral("not-a-digest"));
        QVERIFY(!parseSidecar(bad).valid);
        bad = good;
        bad.replace(QStringLiteral("sha256=") + QString(64, QLatin1Char('a')),
                    QStringLiteral("sha256="));
        QVERIFY(!parseSidecar(bad).valid);
        bad = good;
        bad.replace(QStringLiteral("asset=netboot.xyz.efi"),
                    QStringLiteral("asset=../../EFI/BOOT/bootx64.efi"));
        QVERIFY(!parseSidecar(bad).valid);
    }

    void userChecksumDoesNotAuthorizeAPayload()
    {
        const QByteArray evil("evil-efi");
        const QByteArray official("official-shell");
        MapFetch fetch;
        MapDistro distro;

        const Request gdisk = installRequest(QLatin1String(kGdiskId),
                                              QStringLiteral("gdisk_x64.efi"),
                                              QStringLiteral("9.9.9"), evil);
        const TrustedPayload pinned = trustedPayload(
            gdisk, evil, QByteArray(), QStringLiteral("x86_64"), &fetch, &distro);
        QVERIFY(!pinned.ok);
        QVERIFY(pinned.bytes.isEmpty());
        QVERIFY(pinned.error.contains(QStringLiteral("does not match")));
        QVERIFY(fetch.asked.isEmpty());
        QCOMPARE(distro.calls, 0);
        QVERIFY(sha256Hex(evil) != gdiskMemberSha256());

        const QString shellUrl = shellReleaseApiUrlFor(QStringLiteral("26H2"));
        QVERIFY(publisherUrlAllowed(shellUrl));
        const QString shellJson = QStringLiteral(
            "{\"tag_name\":\"26H2\",\"prerelease\":false,\"assets\":[{"
            "\"name\":\"shellx64.efi\","
            "\"browser_download_url\":\"https://github.com/pbatard/UEFI-Shell/releases/download/26H2/shellx64.efi\","
            "\"size\":15,\"digest\":\"sha256:%1\"}]}")
                                      .arg(sha256Hex(official));
        fetch.files.insert(shellUrl, shellJson.toUtf8());
        const Request shell = installRequest(QLatin1String(kShellId),
                                              QStringLiteral("shellx64.efi"),
                                              QStringLiteral("26H2"), evil);
        const TrustedPayload rejected = trustedPayload(
            shell, evil, evil, QStringLiteral("x86_64"), &fetch, &distro);
        QVERIFY(!rejected.ok);
        QVERIFY(rejected.bytes.isEmpty());
        QCOMPARE(rejected.sha256, sha256Hex(official));

        const TrustedPayload accepted = trustedPayload(
            shell, official, QByteArray(), QStringLiteral("x86_64"), &fetch, &distro);
        QVERIFY2(accepted.ok, qPrintable(accepted.error));
        QCOMPARE(accepted.bytes, official);
        QCOMPARE(accepted.sha256, sha256Hex(official));
        QCOMPARE(accepted.version, QStringLiteral("26H2"));
        QVERIFY(accepted.sha256 != shell.sha256);

        const TrustedPayload reused = trustedPayload(
            shell, QByteArray(), official, QStringLiteral("x86_64"), &fetch, &distro);
        QVERIFY(reused.ok);
        QCOMPARE(reused.bytes, official);

        distro.file.ok = true;
        distro.file.asset = QStringLiteral("mmx64.efi");
        distro.file.bytes = QByteArray("mok-from-distro");
        distro.file.sha256 = sha256Hex(distro.file.bytes);
        const Request mok = installRequest(QLatin1String(kMokManagerId),
                                            QStringLiteral("mmx64.efi"),
                                            QStringLiteral("local"), evil);
        const TrustedPayload copied = trustedPayload(
            mok, evil, evil, QStringLiteral("x86_64"), &fetch, &distro);
        QVERIFY2(copied.ok, qPrintable(copied.error));
        QCOMPARE(copied.bytes, distro.file.bytes);
        QCOMPARE(copied.sha256, distro.file.sha256);
        QCOMPARE(copied.version, QStringLiteral("local"));
        QVERIFY(copied.sha256 != mok.sha256);
        QVERIFY(distro.calls >= 1);

        const QByteArray netboot("netboot-official");
        const QString releaseUrl = netbootReleaseApiUrlFor(QStringLiteral("3.0.3"));
        const QString sumUrl = QStringLiteral(
            "https://github.com/netbootxyz/netboot.xyz/releases/download/3.0.3/"
            "netboot.xyz-sha256-checksums.txt");
        const QString releaseJson = QStringLiteral(
            "{\"tag_name\":\"3.0.3\",\"prerelease\":false,\"assets\":["
            "{\"name\":\"netboot.xyz-sha256-checksums.txt\",\"browser_download_url\":\"%1\",\"size\":80},"
            "{\"name\":\"netboot.xyz.efi\",\"browser_download_url\":"
            "\"https://github.com/netbootxyz/netboot.xyz/releases/download/3.0.3/netboot.xyz.efi\","
            "\"size\":16}]}")
                                        .arg(sumUrl);
        fetch.files.insert(releaseUrl, releaseJson.toUtf8());
        fetch.files.insert(sumUrl, (sha256Hex(netboot) + QStringLiteral("  netboot.xyz.efi\n")).toUtf8());
        const Request net = installRequest(QLatin1String(kUtilityId),
                                            QStringLiteral("netboot.xyz.efi"),
                                            QStringLiteral("3.0.3"), evil);
        const TrustedPayload forged = trustedPayload(
            net, evil, evil, QStringLiteral("x86_64"), &fetch, &distro);
        QVERIFY(!forged.ok);
        QVERIFY(forged.bytes.isEmpty());
        QCOMPARE(forged.sha256, sha256Hex(netboot));
        QVERIFY(forged.sha256 != net.sha256);

        const TrustedPayload published = trustedPayload(
            net, netboot, QByteArray(), QStringLiteral("x86_64"), &fetch, &distro);
        QVERIFY2(published.ok, qPrintable(published.error));
        QCOMPARE(published.bytes, netboot);
        QCOMPARE(published.sha256, sha256Hex(netboot));
        QVERIFY(published.sha256 != net.sha256);
    }

    void memtestIgnoresTheStagedBinary()
    {
        const QByteArray member("MEMTEST");
        const QByteArray evil("EVIL");
        const QByteArray zip = storedZip(QStringLiteral("mt86p_810_x86_64"), member);
        const MemtestLocation loc = memtestLocation(QStringLiteral("v8.10"),
                                                     QStringLiteral("x86_64"));
        QVERIFY(loc.ok);
        const QString sums = sha256Hex(zip) + QStringLiteral("  mt86plus_8.10.binaries.zip\n");
        MapFetch fetch;
        fetch.files.insert(loc.checksumUrl, sums.toUtf8());
        fetch.files.insert(loc.zipUrl, zip);
        MapDistro distro;
        const Request req = installRequest(QLatin1String(kMemtestId),
                                            QStringLiteral("mt86p_810_x86_64"),
                                            QStringLiteral("v8.10"), evil);
        const TrustedPayload got = trustedPayload(
            req, evil, evil, QStringLiteral("x86_64"), &fetch, &distro);
        QVERIFY2(got.ok, qPrintable(got.error));
        QCOMPARE(got.bytes, member);
        QCOMPARE(got.asset, QStringLiteral("mt86p_810_x86_64"));
        QVERIFY(got.sha256 != req.sha256);
        QCOMPARE(distro.calls, 0);

        Request wrong = req;
        wrong.asset = QStringLiteral("mt86p_1_x86_64");
        wrong.sha256 = sha256Hex(evil);
        const TrustedPayload mismatched = trustedPayload(
            wrong, evil, QByteArray(), QStringLiteral("x86_64"), &fetch, &distro);
        QVERIFY(!mismatched.ok);
        QVERIFY(mismatched.bytes.isEmpty());

        Request escape = req;
        escape.version = QStringLiteral("../v8.10");
        QVERIFY(!trustedPayload(escape, evil, QByteArray(), QStringLiteral("x86_64"),
                                &fetch, &distro).ok);
        Request slash = req;
        slash.asset = QStringLiteral("../../EFI/BOOT/bootx64.efi");
        QVERIFY(!trustedPayload(slash, evil, QByteArray(), QStringLiteral("x86_64"),
                                &fetch, &distro).ok);
    }

    void publisherUrlsAndDistroStatAreClosed()
    {
        QVERIFY(publisherUrlAllowed(shellReleaseApiUrlFor(QString())));
        QVERIFY(publisherUrlAllowed(QStringLiteral(
            "https://memtest.org/download/v8.10/sha256sum.txt")));
        QVERIFY(!publisherUrlAllowed(QStringLiteral("file:///etc/passwd")));
        QVERIFY(!publisherUrlAllowed(QStringLiteral(
            "https://evil.example/netboot.xyz.efi")));
        QVERIFY(!publisherUrlAllowed(QStringLiteral(
            "https://github.com/someone/UEFI-Shell/releases/download/1/shellx64.efi")));
        QVERIFY(!publisherUrlAllowed(QStringLiteral(
            "https://memtest.org/download/v8.10/../sha256sum.txt")));
        QVERIFY(!publisherUrlAllowed(QStringLiteral("http://memtest.org/download/v8.10/sha256sum.txt")));
        QVERIFY(!publisherUrlAllowed(QStringLiteral(
            "https://user:pass@memtest.org/download/v8.10/sha256sum.txt")));
        QVERIFY(!publisherUrlAllowed(QStringLiteral(
            "https://api.github.com:8443/repos/pbatard/UEFI-Shell/releases/latest")));
        QVERIFY(shellReleaseApiUrlFor(QStringLiteral("v1/../../evil")).isEmpty());
        QVERIFY(netbootReleaseApiUrlFor(QStringLiteral("3.0.3/../../other")).isEmpty());

        const QString conflict = QString::fromLatin1(kHelloSha)
            + QStringLiteral("  netboot.xyz.efi\n")
            + QString(64, QLatin1Char('b'))
            + QStringLiteral("  netboot.xyz.efi\n");
        const ChecksumFile conflicted = parseSha256Sums(conflict);
        QVERIFY(conflicted.ok);
        QVERIFY(checksumFor(conflicted, QStringLiteral("netboot.xyz.efi")).isEmpty());

        QString error;
        QVERIFY(distroStatAcceptable(false, true, 0, 0644, 16, &error));
        QVERIFY(!distroStatAcceptable(true, true, 0, 0644, 16, &error));
        QVERIFY(error.contains(QStringLiteral("symlink")));
        QVERIFY(!distroStatAcceptable(false, true, 1000, 0644, 16, &error));
        QVERIFY(error.contains(QStringLiteral("root")));
        QVERIFY(!distroStatAcceptable(false, true, 0, 0666, 16, &error));
        QVERIFY(error.contains(QStringLiteral("writable")));
        QVERIFY(!distroStatAcceptable(false, true, 0, 0644, 0, &error));

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString root = makeEsp(dir);
        const QString kept = root + QStringLiteral("/EFI/tools/ipxe.efi");
        const QByteArray original("leave-me");
        {
            QFile file(kept);
            QVERIFY(file.open(QIODevice::WriteOnly));
            QCOMPARE(file.write(original), original.size());
        }
        UtilityRecord rec = recordFor(QByteArray("replacement"));
        const FileOutcome refused = installPayload(root, QByteArray("replacement"), rec, false);
        QCOMPARE(refused.exitCode, kExitRefused);
        QFile after(kept);
        QVERIFY(after.open(QIODevice::ReadOnly));
        QCOMPARE(after.readAll(), original);
        const FileOutcome removed = removeInstall(root, QLatin1String(kUtilityId));
        QCOMPARE(removed.exitCode, kExitRefused);
        QVERIFY(QFile::exists(kept));
        QFile still(kept);
        QVERIFY(still.open(QIODevice::ReadOnly));
        QCOMPARE(still.readAll(), original);

        QVERIFY(usageListsSubcommand(
            QStringLiteral("usage: helper\n  efi-utility            install a managed EFI tool\n"),
            QStringLiteral("efi-utility")));
        QVERIFY(!usageListsSubcommand(
            QStringLiteral("usage: helper\n  install-config\n"), QStringLiteral("efi-utility")));
        QVERIFY(!usageListsSubcommand(QStringLiteral("efi-utility-extra"),
                                      QStringLiteral("efi-utility")));
    }
};

QTEST_APPLESS_MAIN(TestEfiUtil)
#include "tst_efiutil.moc"
