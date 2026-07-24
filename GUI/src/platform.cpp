#include "platform.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>

#ifdef Q_OS_WIN
#include <qt_windows.h> // CREATE_NO_WINDOW
#include <shlobj.h>     // SHGetKnownFolderPath
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

static bool copySeedDir(const QString &source, const QString &destination)
{
    if (QFileInfo::exists(destination))
        return true;
    const QDir sourceDir(source);
    if (!sourceDir.exists() || !QDir().mkpath(destination))
        return false;
    for (const QFileInfo &entry : sourceDir.entryInfoList(
             QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot)) {
        const QString target = destination + "/" + entry.fileName();
        if (entry.isDir()) {
            if (!copySeedDir(entry.filePath(), target))
                return false;
        } else if (!QFile::copy(entry.filePath(), target)) {
            return false;
        }
    }
    return true;
}

void prepareDataDir()
{
    const QString root = dataDir();
    const QString shipped = QCoreApplication::applicationDirPath();
    QDir().mkpath(root + "/GUI");
    const QString seedConfig = shipped + "/GUI/refind.conf";
    const QString userConfig = root + "/GUI/refind.conf";
    if (!QFileInfo::exists(userConfig) && QFileInfo::exists(seedConfig))
        QFile::copy(seedConfig, userConfig);
    copySeedDir(shipped + "/icons", root + "/icons");
    copySeedDir(shipped + "/backgrounds", root + "/backgrounds");
}

static bool isBelow(const QString &path, const QString &root)
{
    const QString cleanPath = QDir::cleanPath(path);
    QString cleanRoot = QDir::cleanPath(root);
    if (!cleanRoot.endsWith('/'))
        cleanRoot += '/';
    return cleanPath.startsWith(cleanRoot, Qt::CaseInsensitive);
}

static QString knownFolderPath(REFKNOWNFOLDERID folderId)
{
    PWSTR nativePath = nullptr;
    if (FAILED(SHGetKnownFolderPath(folderId, KF_FLAG_DEFAULT, nullptr, &nativePath)))
        return {};
    const QString path = QDir::fromNativeSeparators(QString::fromWCharArray(nativePath));
    CoTaskMemFree(nativePath);
    return path;
}

static QString trustedScriptPath(const QString &name)
{
    const QString appDir = QFileInfo(QCoreApplication::applicationDirPath()).canonicalFilePath();
    const QString script = QFileInfo(appDir + "/windows/" + name).canonicalFilePath();
    if (appDir.isEmpty() || script.isEmpty() || !isBelow(script, appDir))
        return {};

    const QStringList protectedRoots = {
        knownFolderPath(FOLDERID_ProgramFiles),
        knownFolderPath(FOLDERID_ProgramFilesX64),
        knownFolderPath(FOLDERID_ProgramFilesX86)
    };
    for (const QString &root : protectedRoots) {
        if (!root.isEmpty() && isBelow(appDir, QDir::fromNativeSeparators(root)))
            return script;
    }
    return {};
}

static QString systemPowerShell()
{
    QString systemDirectory(MAX_PATH, Qt::Uninitialized);
    UINT length = GetSystemDirectoryW(
        reinterpret_cast<LPWSTR>(systemDirectory.data()),
        static_cast<UINT>(systemDirectory.size()));
    if (length == 0)
        return {};
    if (length >= static_cast<UINT>(systemDirectory.size())) {
        systemDirectory.resize(static_cast<int>(length) + 1);
        length = GetSystemDirectoryW(
            reinterpret_cast<LPWSTR>(systemDirectory.data()),
            static_cast<UINT>(systemDirectory.size()));
        if (length == 0 || length >= static_cast<UINT>(systemDirectory.size()))
            return {};
    }
    systemDirectory.resize(static_cast<int>(length));
    return QDir::toNativeSeparators(QDir::fromNativeSeparators(systemDirectory)
                                    + "/WindowsPowerShell/v1.0/powershell.exe");
}

static bool runScriptInWindow(const QString &scriptName, const QStringList &scriptArgs = {})
{
    const QString scriptPath = trustedScriptPath(scriptName);
    const QString powershell = systemPowerShell();
    if (scriptPath.isEmpty() || powershell.isEmpty())
        return false;
    QStringList args = {QStringLiteral("-NoProfile"), QStringLiteral("-ExecutionPolicy"),
                        QStringLiteral("Bypass"), QStringLiteral("-NoExit"),
                        QStringLiteral("-File"), QDir::toNativeSeparators(scriptPath)};
    args += scriptArgs;
    return QProcess::startDetached(powershell, args);
}

bool runInstallerScript(const QString &installSource)
{
    Q_UNUSED(installSource); // only the SourceForge download exists on Windows
    return runScriptInWindow(QStringLiteral("install_rEFInd.ps1"));
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
    const QString script = trustedScriptPath(QStringLiteral("install_config_from_GUI.ps1"));
    const QString powershell = systemPowerShell();
    if (script.isEmpty() || powershell.isEmpty()) {
        if (output)
            *output = QCoreApplication::translate(
                "Platform", "The privileged helper is not installed under Program Files.");
        return -1;
    }
    proc.start(powershell,
               {QStringLiteral("-NoProfile"), QStringLiteral("-ExecutionPolicy"),
                QStringLiteral("Bypass"), QStringLiteral("-File"),
                QDir::toNativeSeparators(script)});
    if (!proc.waitForStarted()) {
        if (output)
            *output = QCoreApplication::translate("Platform",
                                                  "powershell.exe could not be started.");
        return -1;
    }
    proc.waitForFinished(-1);
    if (output)
        *output = QString::fromLocal8Bit(proc.readAll());
    return proc.exitStatus() == QProcess::NormalExit ? proc.exitCode() : -1;
}

bool installConfigScriptTrusted(QString *detail)
{
    const QString script = trustedScriptPath(QStringLiteral("install_config_from_GUI.ps1"));
    if (detail)
        *detail = script.isEmpty()
            ? QCoreApplication::applicationDirPath() + "/windows/install_config_from_GUI.ps1"
            : script;
    return !script.isEmpty();
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
    return runScriptInWindow(QStringLiteral("rEFInd_bg_randomizer_task.ps1"),
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

void prepareDataDir()
{
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
            *output = QCoreApplication::translate("Platform",
                                                  "sudo could not be started.");
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
