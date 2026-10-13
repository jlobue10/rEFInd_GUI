// Main-window showtools checkboxes: they edit the same list Create Config
// writes, keep the existing order, and persist with the Showtools box.

#include "mainwindow.h"

#include <QCheckBox>
#include <QDir>
#include <QFile>
#include <QLineEdit>
#include <QSettings>

#include <QtTest>

static const bool kIsolateHome = [] {
    qputenv("HOME", "/tmp/refind-showtools-home");
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "offscreen");
    QDir().mkpath(QStringLiteral("/tmp/refind-showtools-home/.local/rEFInd_GUI/GUI"));
    return true;
}();

class TestShowtoolsUi : public QObject
{
    Q_OBJECT

private slots:
    void checkboxesEditTheShowtoolsList()
    {
        Q_UNUSED(kIsolateHome);
        QFile::remove(QStringLiteral(
            "/tmp/refind-showtools-home/.local/rEFInd_GUI/GUI/rEFInd_GUI.ini"));
        MainWindow window;
        auto *edit = window.findChild<QLineEdit *>(QStringLiteral("Showtools_lineEdit"));
        auto *shutdown = window.findChild<QCheckBox *>(QStringLiteral("Showtool_shutdown_checkBox"));
        auto *netboot = window.findChild<QCheckBox *>(QStringLiteral("Showtool_netboot_checkBox"));
        auto *firmware = window.findChild<QCheckBox *>(QStringLiteral("Showtool_firmware_checkBox"));
        auto *shell = window.findChild<QCheckBox *>(QStringLiteral("Showtool_shell_checkBox"));
        QVERIFY(edit);
        QVERIFY(shutdown);
        QVERIFY(netboot);
        QVERIFY(firmware);
        QVERIFY(shell);
        QCOMPARE(edit->text(), QString());
        QVERIFY(!shutdown->isChecked());

        shutdown->setChecked(true);
        QCOMPARE(edit->text(), QStringLiteral("shutdown"));
        netboot->setChecked(true);
        QCOMPARE(edit->text(), QStringLiteral("shutdown,netboot"));
        shutdown->setChecked(false);
        QCOMPARE(edit->text(), QStringLiteral("netboot"));
        QVERIFY(!shutdown->isChecked());
        QVERIFY(netboot->isChecked());

        edit->setText(QStringLiteral("reboot,shutdown"));
        QVERIFY(shutdown->isChecked());
        QVERIFY(!netboot->isChecked());
        firmware->setChecked(true);
        QCOMPARE(edit->text(), QStringLiteral("reboot,shutdown,firmware"));

        edit->setText(QStringLiteral("notatool,shell"));
        QVERIFY(shell->isChecked());
        QVERIFY(!firmware->isChecked());
        QVERIFY(!edit->styleSheet().isEmpty());
        netboot->setChecked(true);
        QCOMPARE(edit->text(), QStringLiteral("notatool,shell,netboot"));

        edit->setText(QString());
        QVERIFY(!shell->isChecked());
        QVERIFY(!netboot->isChecked());

        shutdown->setChecked(true);
        QSettings settings(QStringLiteral("/tmp/refind-showtools-home/.local/rEFInd_GUI/GUI/rEFInd_GUI.ini"),
                           QSettings::IniFormat);
        settings.beginGroup(QStringLiteral("ShowTools"));
        QCOMPARE(settings.value(QStringLiteral("ShowTools")).toString(), QStringLiteral("shutdown"));
    }
};

QTEST_MAIN(TestShowtoolsUi)
#include "tst_showtools_ui.moc"
