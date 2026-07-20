#include "platform.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>

#ifdef Q_OS_WIN
#include <qt_windows.h> // CREATE_NO_WINDOW
#else
#include <sys/stat.h> // ::stat, to trigger systemd ESP automounts
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

bool installConfigScriptTrusted(QString *detail)
{
    // The .ps1 scripts are Authenticode-signed at release time, and signing
    // rewrites the file, so a hash embedded at build time could never match.
    if (detail)
        detail->clear();
    return true;
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
    // Allowed without a password via /etc/sudoers.d/zz_install_config_from_GUI
    // (zz_ so it sorts after any passworded catch-all drop-in -- sudo takes
    // the last lexical match); -n keeps the GUI from hanging on a prompt if
    // that rule is missing.
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

bool installConfigScriptTrusted(QString *detail)
{
    // /etc/rEFInd/install_config_from_GUI.sh runs as root via the passwordless
    // sudoers rule, so refuse to invoke it unless it hashes identically to the
    // copy this build shipped (embedded as a Qt resource at build time). The
    // installer seds the literal HOME placeholder to the user's home before
    // copying the script to /etc; replicate that on the embedded reference so
    // the hashes are comparable.
    const QString installedPath = QStringLiteral("/etc/rEFInd/install_config_from_GUI.sh");
    if (detail)
        *detail = installedPath;
    QFile ref(QStringLiteral(":/install_config_from_GUI.sh"));
    if (!ref.open(QIODevice::ReadOnly))
        return false;
    QByteArray expected = ref.readAll();
    expected.replace(QByteArrayLiteral("HOME"), QDir::homePath().toUtf8());
    QFile installed(installedPath);
    if (!installed.open(QIODevice::ReadOnly))
        return false;
    return QCryptographicHash::hash(installed.readAll(), QCryptographicHash::Sha256)
        == QCryptographicHash::hash(expected, QCryptographicHash::Sha256);
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
    // Establish any systemd ESP automounts first (SteamOS mounts /esp and
    // /efi that way): stat of "<m>/." resolves through the automount point
    // and triggers the mount, where a plain stat of m does not
    // (AT_NO_AUTOMOUNT). Without this, right after boot the ESP isn't
    // mounted yet and reads as absent, wrongly disabling the button.
    struct stat sb;
    for (const QString &m : mounts)
        (void)::stat(QString(m + QStringLiteral("/.")).toLocal8Bit().constData(), &sb);
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
