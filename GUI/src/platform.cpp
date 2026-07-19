#include "platform.h"

#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>

#ifdef Q_OS_WIN
#include <qt_windows.h> // CREATE_NO_WINDOW
#endif

namespace Platform {

#ifdef Q_OS_WIN

QString dataDir()
{
    QString base = QProcessEnvironment::systemEnvironment().value(QStringLiteral("LOCALAPPDATA"));
    if (base.isEmpty())
        base = QDir::homePath() + "/AppData/Local";
    return QDir::fromNativeSeparators(base) + "/rEFInd_GUI";
}

static bool runScriptInWindow(const QString &scriptPath, const QStringList &scriptArgs = {})
{
    QStringList args = {QStringLiteral("-NoProfile"), QStringLiteral("-ExecutionPolicy"),
                        QStringLiteral("Bypass"), QStringLiteral("-NoExit"),
                        QStringLiteral("-File"), QDir::toNativeSeparators(scriptPath)};
    args += scriptArgs;
    return QProcess::startDetached(QStringLiteral("powershell.exe"), args);
}

bool runInstallerScript(const QString &installSource)
{
    Q_UNUSED(installSource); // only the SourceForge download exists on Windows
    return runScriptInWindow(dataDir() + "/windows/install_rEFInd.ps1");
}

int installConfig(QString *output)
{
    // Synchronous, window-less run with the output captured: the script's
    // console used to flash open and vanish, leaving no trace of whether the
    // install worked. The caller shows the result dialog from *output.
    QProcess proc;
    proc.setProcessChannelMode(QProcess::MergedChannels);
    proc.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args) {
        args->flags |= CREATE_NO_WINDOW;
    });
    proc.start(QStringLiteral("powershell.exe"),
               {QStringLiteral("-NoProfile"), QStringLiteral("-ExecutionPolicy"),
                QStringLiteral("Bypass"), QStringLiteral("-File"),
                QDir::toNativeSeparators(dataDir() + "/windows/install_config_from_GUI.ps1")});
    if (!proc.waitForStarted()) {
        if (output)
            *output = QStringLiteral("powershell.exe could not be started.");
        return -1;
    }
    proc.waitForFinished(-1);
    if (output)
        *output = QString::fromLocal8Bit(proc.readAll());
    return proc.exitStatus() == QProcess::NormalExit ? proc.exitCode() : -1;
}

int runEspDeepScan()
{
    return -1; // the elevated Windows build scans ESPs directly
}

bool espDeepScanUseful()
{
    return false;
}

bool setBackgroundRandomizer(bool enable)
{
    return runScriptInWindow(dataDir() + "/windows/rEFInd_bg_randomizer_task.ps1",
                             {enable ? QStringLiteral("-Enable") : QStringLiteral("-Disable")});
}

bool firmwareBootnumSupported()
{
    return false;
}

bool preferWindowsAsDefault()
{
    return true;
}

QStringList installSourceOptions()
{
    return {QStringLiteral("Sourceforge")};
}

#else // Linux

QString dataDir()
{
    return QDir::homePath() + "/.local/rEFInd_GUI";
}

bool runInstallerScript(const QString &installSource)
{
    const QString script = dataDir()
        + (installSource == QLatin1String("Sourceforge")
               ? QStringLiteral("/refind_install_Sourceforge.sh")
               : QStringLiteral("/refind_install_package_mgr.sh"));
    return QProcess::startDetached(QStringLiteral("xterm"), {QStringLiteral("-e"), script});
}

int installConfig(QString *output)
{
    // Allowed without a password via /etc/sudoers.d/install_config_from_GUI;
    // -n keeps the GUI from hanging on a prompt if that rule is missing.
    QProcess proc;
    proc.setProcessChannelMode(QProcess::MergedChannels);
    proc.start(QStringLiteral("sudo"),
               {QStringLiteral("-n"),
                QStringLiteral("/etc/rEFInd/install_config_from_GUI.sh")});
    if (!proc.waitForStarted()) {
        if (output)
            *output = QStringLiteral("sudo could not be started.");
        return -1;
    }
    proc.waitForFinished(-1);
    if (output)
        *output = QString::fromLocal8Bit(proc.readAll());
    return proc.exitStatus() == QProcess::NormalExit ? proc.exitCode() : -1;
}

int runEspDeepScan()
{
    // Blocking, unlike the other script launchers: the caller re-runs detection
    // as soon as this returns, so it has to wait for the cache to be written.
    // The script owns the password prompt and the result dialogs.
    return QProcess::execute(QStringLiteral("bash"),
                             {dataDir() + QStringLiteral("/scan_esp.sh")});
}

bool espDeepScanUseful()
{
    // Only worth offering when an ESP really is unreadable. Mirrors the check
    // in OSDetector::espRootUnreadable() without pulling detection in here.
    // Fedora-family installs mount /boot/efi umask=0077 root:root by default,
    // so this is common off the Steam Deck too.
    const QStringList mounts = {QStringLiteral("/esp"), QStringLiteral("/boot/efi"),
                                QStringLiteral("/efi"), QStringLiteral("/boot")};
    for (const QString &m : mounts) {
        const QFileInfo info(m);
        if (info.exists() && !info.isReadable())
            return true;
    }
    return false;
}

bool setBackgroundRandomizer(bool enable)
{
    const QString action = enable ? QStringLiteral("enable") : QStringLiteral("disable");
    const QString command = QStringLiteral(
        "sudo systemctl %1 --now rEFInd_bg_randomizer.service && "
        "sudo systemctl status rEFInd_bg_randomizer.service; exec bash").arg(action);
    return QProcess::startDetached(QStringLiteral("xterm"),
                                   {QStringLiteral("-e"), QStringLiteral("bash"),
                                    QStringLiteral("-c"), command});
}

bool firmwareBootnumSupported()
{
    return true;
}

bool preferWindowsAsDefault()
{
    return false; // on Linux, default to the running distro
}

QStringList installSourceOptions()
{
    return {QStringLiteral("Package Mgr"), QStringLiteral("Sourceforge")};
}

#endif

} // namespace Platform
