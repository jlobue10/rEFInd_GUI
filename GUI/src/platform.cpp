#include "platform.h"

#include <QDir>
#include <QProcess>
#include <QProcessEnvironment>

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

int installConfig()
{
    return QProcess::execute(QStringLiteral("powershell.exe"),
                             {QStringLiteral("-NoProfile"), QStringLiteral("-ExecutionPolicy"),
                              QStringLiteral("Bypass"), QStringLiteral("-File"),
                              QDir::toNativeSeparators(dataDir() + "/windows/install_config_from_GUI.ps1")});
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

bool useImagePreviewDialog()
{
    return false; // the native Explorer dialog already previews images
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

int installConfig()
{
    // Allowed without a password via /etc/sudoers.d/install_config_from_GUI;
    // -n keeps the GUI from hanging on a prompt if that rule is missing.
    return QProcess::execute(QStringLiteral("sudo"),
                             {QStringLiteral("-n"),
                              QStringLiteral("/etc/rEFInd/install_config_from_GUI.sh")});
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

bool useImagePreviewDialog()
{
    return true; // drive Qt's own dialog with a PNG preview pane (desktop parity)
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
