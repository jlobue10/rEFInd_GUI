// Locks the documented classification behavior of
// OSDetector::classifyLoaderPath (the deep-scan/efibootmgr recovery path):
// real OS loaders in, rEFInd itself / removable-media fallback / driver and
// tool dirs / non-bootmgfw Microsoft plumbing out. The osdetect_* sources are
// linked completely untouched (they are parity-locked with the sibling repo);
// the private-access define is confined to this test translation unit.
#define private public
#include "osdetect.h"
#undef private

#include <QtTest>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

class TestOsDetect : public QObject
{
    Q_OBJECT

private slots:
    void windowsLoader()
    {
        BootEntry e;
        QVERIFY(OSDetector::classifyLoaderPath(
            QStringLiteral("/EFI/Microsoft/Boot/bootmgfw.efi"), e));
        QCOMPARE(e.displayName, QStringLiteral("Windows"));
        QCOMPARE(e.loaderPath, QStringLiteral("/EFI/Microsoft/Boot/bootmgfw.efi"));
        QVERIFY(!e.supportsFirmwareBootnum);
    }

    void windowsLoaderCaseInsensitive()
    {
        BootEntry e;
        QVERIFY(OSDetector::classifyLoaderPath(
            QStringLiteral("/efi/microsoft/boot/BOOTMGFW.EFI"), e));
        QCOMPARE(e.displayName, QStringLiteral("Windows"));
        // The canonical path is emitted regardless of the input's case.
        QCOMPARE(e.loaderPath, QStringLiteral("/EFI/Microsoft/Boot/bootmgfw.efi"));
    }

    void steamosLoader()
    {
        BootEntry e;
        QVERIFY(OSDetector::classifyLoaderPath(
            QStringLiteral("/EFI/steamos/steamcl.efi"), e));
        QCOMPARE(e.displayName, QStringLiteral("SteamOS"));
        QVERIFY(e.supportsFirmwareBootnum);
    }

    void microsoftPlumbingIsSkipped()
    {
        // Everything under /EFI/Microsoft except bootmgfw is Windows
        // plumbing, not a bootable OS (bogus "Microsoft" entry regression).
        BootEntry e;
        QVERIFY(!OSDetector::classifyLoaderPath(
            QStringLiteral("/EFI/Microsoft/Boot/bootmgr.efi"), e));
        QVERIFY(!OSDetector::classifyLoaderPath(
            QStringLiteral("/EFI/Microsoft/Boot/memtest.efi"), e));
        QVERIFY(!OSDetector::classifyLoaderPath(
            QStringLiteral("/EFI/Microsoft/Recovery/WinRE.efi"), e));
    }

    void skipListIsHonored()
    {
        BootEntry e;
        // rEFInd itself and the removable-media fallback loader.
        QVERIFY(!OSDetector::classifyLoaderPath(
            QStringLiteral("/EFI/refind/refind_x64.efi"), e));
        QVERIFY(!OSDetector::classifyLoaderPath(
            QStringLiteral("/EFI/BOOT/bootx64.efi"), e));
        // Utility/driver dirs.
        QVERIFY(!OSDetector::classifyLoaderPath(
            QStringLiteral("/EFI/memtest86/memtest.efi"), e));
        QVERIFY(!OSDetector::classifyLoaderPath(
            QStringLiteral("/EFI/drivers_x64/UsbXbox360Dxe.efi"), e));
        QVERIFY(!OSDetector::classifyLoaderPath(
            QStringLiteral("/EFI/tools/shell.efi"), e));
    }

    void loaderWithoutVendorDirIsSkipped()
    {
        BootEntry e;
        QVERIFY(!OSDetector::classifyLoaderPath(QStringLiteral("/bootx64.efi"), e));
        QVERIFY(!OSDetector::classifyLoaderPath(QStringLiteral("/shellx64.efi"), e));
    }

    void linuxVendorLoaderIsAccepted()
    {
        // Exact display name depends on the vendor-name table (and, for the
        // running distro's own ESP, on /etc/os-release), so only the stable
        // invariants are asserted here.
        BootEntry e;
        QVERIFY(OSDetector::classifyLoaderPath(
            QStringLiteral("/EFI/fedora/shimx64.efi"), e));
        QVERIFY(!e.displayName.isEmpty());
        QCOMPARE(e.loaderPath, QStringLiteral("/EFI/fedora/shimx64.efi"));
        QCOMPARE(e.menuName, e.displayName);
    }

    void ukiLoaderIsClassified()
    {
        // Unified Kernel Images in /EFI/Linux carry no vendor dir and their
        // filename ends in .efi — the vendor-dir logic alone would reject
        // them. kernel-install names them <entry-token>-<version>.efi, where
        // the entry token is usually a 32-hex machine-id that must be
        // stripped for display.
        BootEntry e;
        const QString path = QStringLiteral(
            "/EFI/Linux/ec4de823fa4f4251832ddc066f2a77c5-7.2.10-200.fc44.x86_64.efi");
        QVERIFY(OSDetector::classifyLoaderPath(path, e));
        QCOMPARE(e.loaderPath, path);
        // The distro prefix depends on the /etc/os-release of the machine
        // running the test; the version part is deterministic.
        QVERIFY(e.displayName.endsWith(QStringLiteral("(7.2.10-200.fc44.x86_64)")));
        QVERIFY(!e.supportsFirmwareBootnum);
    }

    void ukiWithoutVersionIsSkipped()
    {
        BootEntry e;
        QVERIFY(!OSDetector::classifyLoaderPath(QStringLiteral("/EFI/Linux/.efi"), e));
    }

    void scanEspRootFindsUkis()
    {
        // scanEspRoot() populates the Boot Option boxes; every UKI in
        // /EFI/Linux must surface as a slottable entry, newest-named sort
        // order matching QDir::Name (ascending), named after the distro
        // handed in with the machine-id token stripped.
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QDir(dir.path()).mkpath(QStringLiteral("EFI/Linux"));
        const QStringList names = {
            QStringLiteral("ec4de823fa4f4251832ddc066f2a77c5-7.2.10-200.fc44.x86_64.efi"),
            QStringLiteral("arch-linux.efi"),
        };
        for (const QString &name : names) {
            QFile f(dir.filePath(QStringLiteral("EFI/Linux/") + name));
            QVERIFY(f.open(QIODevice::WriteOnly));
        }
        OSDetector detector;
        const QList<BootEntry> entries
            = detector.scanEspRoot(dir.path(), QStringLiteral("Fedora"));
        QCOMPARE(entries.size(), 2);
        QCOMPARE(entries.at(0).displayName, QStringLiteral("Fedora (arch-linux)"));
        QCOMPARE(entries.at(0).loaderPath, QStringLiteral("/EFI/Linux/arch-linux.efi"));
        QCOMPARE(entries.at(1).displayName, QStringLiteral("Fedora (7.2.10-200.fc44.x86_64)"));
        QCOMPARE(entries.at(1).loaderPath,
                 QStringLiteral("/EFI/Linux/ec4de823fa4f4251832ddc066f2a77c5-7.2.10-200.fc44.x86_64.efi"));
    }
};

QTEST_APPLESS_MAIN(TestOsDetect)
#include "tst_osdetect.moc"
