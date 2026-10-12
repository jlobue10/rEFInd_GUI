// EFI utility manager: release parsing, checksums, temp-dir install/remove,
// showtools edits, and Secure Boot efivar bytes. No network and no real ESP.

#include "efiutil/helpercmd.h"
#include "efiutil/install.h"
#include "efiutil/netbootxyz.h"
#include "efiutil/request.h"
#include "efiutil/secureboot.h"
#include "efiutil/showtools.h"
#include "espops/espconstants.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <QtTest>

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
};

QTEST_APPLESS_MAIN(TestEfiUtil)
#include "tst_efiutil.moc"
