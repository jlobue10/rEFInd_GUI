#include "platform.h"
#include "efiutil/helpercmd.h"
#include "efiutil/trust.h"
#include "espops/configinstall.h"
#include "espops/espconstants.h"
#include "espops/espresolve.h"
#include "espops/themesinstall.h"
#include "espops/userio.h"
#include "espops/wintasks.h"
#include "scripttrust.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSysInfo>

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
    const QDir sourceDir(source);
    const QFileInfo destinationInfo(destination);
    if (!sourceDir.exists()
        || (destinationInfo.exists() && !destinationInfo.isDir())
        || !QDir().mkpath(destination)) {
        return false;
    }

    for (const QFileInfo &entry : sourceDir.entryInfoList(
             QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot)) {
        const QString target = destination + "/" + entry.fileName();
        const QFileInfo targetInfo(target);
        if (entry.isDir()) {
            if (targetInfo.exists() && !targetInfo.isDir())
                return false;
            if (!copySeedDir(entry.filePath(), target))
                return false;
        } else {
            // Seed assets are defaults. Add files introduced by an update,
            // but preserve every file the user already has.
            if (targetInfo.exists()) {
                if (!targetInfo.isFile())
                    return false;
                continue;
            }
            if (!QFile::copy(entry.filePath(), target))
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
    copySeedDir(shipped + "/themes", root + "/themes");
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

// The elevated GUI runs the shipped PowerShell scripts, so they are trusted
// only from beneath Program Files (where standard users cannot replace them).
// ScriptTrust holds the decision, expands 8.3 short launch paths before the
// comparison, and explains a refusal in *detail.
static QString trustedScriptPath(const QString &name, QString *detail = nullptr)
{
    const QStringList protectedRoots = {
        knownFolderPath(FOLDERID_ProgramFiles),
        knownFolderPath(FOLDERID_ProgramFilesX64),
        knownFolderPath(FOLDERID_ProgramFilesX86)
    };
    const ScriptTrust::Verdict verdict = ScriptTrust::evaluate(
        QCoreApplication::applicationDirPath(), name, protectedRoots);
    if (detail)
        *detail = verdict.detail;
    return verdict.scriptPath;
}

QString windowsSystemExecutable(const QString &relativePath)
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
    return QDir::toNativeSeparators(
        QDir(QDir::fromNativeSeparators(systemDirectory)).filePath(relativePath));
}

static bool runScriptInWindow(const QString &scriptName, QString *detail = nullptr,
                              const QStringList &scriptArgs = {})
{
    const QString scriptPath = trustedScriptPath(scriptName, detail);
    if (scriptPath.isEmpty())
        return false;
    const QString powershell = windowsSystemExecutable(
        QStringLiteral("WindowsPowerShell/v1.0/powershell.exe"));
    if (powershell.isEmpty() || !QFileInfo::exists(powershell)) {
        if (detail)
            *detail = QCoreApplication::translate(
                          "Platform", "Windows PowerShell was not found at %1.")
                          .arg(powershell.isEmpty() ? QStringLiteral("System32\\WindowsPowerShell\\v1.0")
                                                    : powershell);
        return false;
    }
    QStringList args = {QStringLiteral("-NoProfile"), QStringLiteral("-ExecutionPolicy"),
                        QStringLiteral("Bypass"), QStringLiteral("-NoExit"),
                        QStringLiteral("-File"), QDir::toNativeSeparators(scriptPath)};
    args += scriptArgs;
    if (!QProcess::startDetached(powershell, args)) {
        if (detail)
            *detail = QCoreApplication::translate(
                          "Platform", "Windows refused to start %1 for %2.")
                          .arg(powershell, QDir::toNativeSeparators(scriptPath));
        return false;
    }
    return true;
}

bool runInstallerScript(const QString &installSource, QString *detail)
{
    Q_UNUSED(installSource); // only the SourceForge download exists on Windows
    return runScriptInWindow(QStringLiteral("install_rEFInd.ps1"), detail);
}

// The GUI itself runs elevated (requireAdministrator manifest), so the
// install actions call the shared espops logic IN-PROCESS: no PowerShell,
// no location-trust check, no OEM-codepage capture pitfalls
// (NATIVE_HELPER_DESIGN.md §2.2). The data dir belongs to the same user
// the elevated process runs as, so DirectUserFiles reads it directly.
static int runEspInstallInProcess(QString *output, const char *noun,
                                  EspOps::InstallOutcome (*action)(
                                      EspOps::UserFiles &, const QString &,
                                      const QString &))
{
    EspOps::EspResolver resolver;
    EspOps::RefindTarget target;
    if (!resolver.resolve(&target)) {
        if (output)
            *output = QCoreApplication::translate(
                          "Platform",
                          "No EFI System Partition with rEFInd on it could be "
                          "found.\nInstall rEFInd first, then install the %1.")
                          .arg(QLatin1String(noun));
        return 3;
    }
    EspOps::DirectUserFiles files;
    const QString srcDir = dataDir()
        + (qstrcmp(noun, "config") == 0 ? QLatin1String("/GUI")
                                        : QLatin1String("/themes"));
    const EspOps::InstallOutcome o = action(files, srcDir, target.refindDir);
    if (output) {
        *output = o.lines.join(QLatin1Char('\n'));
        // A letterless ESP is mounted on a private temp directory for the
        // duration of the write, so espops' messages name a path that no
        // longer exists by the time the user reads them (and reads as "it
        // installed into Temp"). Present the ESP itself instead. The
        // themes subdirectory is substituted first — replacing the parent
        // first would leave a mixed "...target ESP/themes" tail.
        if (!target.refindDir.isEmpty()) {
            output->replace(target.refindDir + QLatin1String("/themes"),
                            QCoreApplication::translate(
                                "Platform",
                                "EFI\\refind\\themes on the target ESP"));
            output->replace(target.refindDir,
                            QCoreApplication::translate(
                                "Platform", "EFI\\refind on the target ESP"));
        }
        if (o.exitCode == 0)
            *output += QStringLiteral("\n(chosen as %1)").arg(target.how);
    }
    return o.exitCode;
}

int installConfig(QString *output)
{
    return runEspInstallInProcess(output, "config", EspOps::installConfigSet);
}

int installThemes(QString *output)
{
    return runEspInstallInProcess(output, "themes", EspOps::installThemeSet);
}

bool installConfigScriptTrusted(QString *detail)
{
    // Nothing external is launched anymore — the logic is compiled into
    // this (elevated, Program Files-protected) executable.
    if (detail)
        detail->clear();
    return true;
}

bool installThemesScriptTrusted(QString *detail)
{
    if (detail)
        detail->clear();
    return true;
}

int runEfiUtility(QString *output)
{
    return EfiUtil::runEfiUtilityInProcess(dataDir(), output);
}

bool efiUtilityReady(QString *detail)
{
    // The Windows GUI is already the elevated process.
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

// The at-logon Scheduled Tasks now run the helper exe directly, registered
// natively via the Task Scheduler COM API (NATIVE_HELPER_DESIGN.md §2.7);
// task names are unchanged so existing uninstall flows keep matching.
static bool setHelperTask(const char *taskSuffix, const char *subcommand,
                          bool enable)
{
    const QString exe = QCoreApplication::applicationDirPath()
        + QLatin1Char('/') + QLatin1String(EspOps::kProductName)
        + QLatin1String("_helper.exe");
    if (enable && !QFileInfo::exists(exe))
        return false;
    return EspOps::setLogonTask(QLatin1String(EspOps::kProductName)
                                    + QLatin1String(taskSuffix),
                                exe, QLatin1String(subcommand), enable);
}

bool setBackgroundRandomizer(bool enable)
{
    return setHelperTask("_bg_randomizer", "randomize-background", enable);
}

bool setThemeRandomizer(bool enable)
{
    return setHelperTask("_theme_randomizer", "randomize-theme", enable);
}

bool firmwareBootnumSupported()
{
    return false;
}

bool ukiRefreshHookInstalled()
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

bool runInstallerScript(const QString &installSource, QString *detail)
{
    const QString script = dataDir()
        + (installSource == QLatin1String("Sourceforge")
               ? QStringLiteral("/refind_install_Sourceforge.sh")
               : QStringLiteral("/refind_install_package_mgr.sh"));
    if (!QProcess::startDetached(QStringLiteral("xterm"), {QStringLiteral("-e"), script})) {
        if (detail)
            *detail = QCoreApplication::translate(
                          "Platform", "xterm could not be started to run %1. Is xterm installed?")
                          .arg(script);
        return false;
    }
    return true;
}

// Runs one subcommand of the root-owned /etc/rEFInd helper binary
// (NATIVE_HELPER_DESIGN.md). Allowed without a password via
// /etc/sudoers.d/zz_install_config_from_GUI (zz_ so it sorts after any
// passworded catch-all drop-in -- sudo takes the last lexical match; the
// rule whitelists the exact helper+subcommand argument vectors); -n keeps
// the GUI from hanging on a prompt if that rule is missing.
static int runRootHelperCaptured(const QString &subcommand, QString *output)
{
    QProcess proc;
    proc.setProcessChannelMode(QProcess::MergedChannels);
    proc.start(QStringLiteral("sudo"),
               {QStringLiteral("-n"),
                QLatin1String(EspOps::kHelperEtcPath), subcommand});
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

int installConfig(QString *output)
{
    return runRootHelperCaptured(QStringLiteral("install-config"), output);
}

int installThemes(QString *output)
{
    return runRootHelperCaptured(QStringLiteral("install-themes"), output);
}

// The /etc helper runs as root via the passwordless sudoers rule. The
// security boundary is its root ownership (nothing user-writable is ever
// elevated); this handshake is skew detection, not tamper detection — it
// runs `helper --version` unprivileged (the file is world-executable) and
// requires the version this GUI was built with, so a helper left behind by
// an older or newer install produces a precise "reinstall" dialog instead
// of a subtle misbehavior. It replaces the pre-4.x SHA-256 script
// hash-check, which died with the on-disk scripts.
static bool helperVersionMatches(QString *detail)
{
    const QString helper = QLatin1String(EspOps::kHelperEtcPath);
    QString found = QCoreApplication::translate("Platform", "missing");
    QProcess proc;
    proc.setProcessChannelMode(QProcess::SeparateChannels);
    proc.start(helper, {QStringLiteral("--version")});
    bool ok = false;
    if (proc.waitForStarted() && proc.waitForFinished(10000)
        && proc.exitStatus() == QProcess::NormalExit && proc.exitCode() == 0) {
        const QString version =
            QString::fromLocal8Bit(proc.readAllStandardOutput()).trimmed();
        if (!version.isEmpty())
            found = version;
        ok = version == QLatin1String(ESPOPS_APP_VERSION);
    }
    if (detail)
        *detail = QCoreApplication::translate(
                      "Platform", "%1 (expected version %2, found: %3)")
                      .arg(helper, QLatin1String(ESPOPS_APP_VERSION), found);
    return ok;
}

bool installConfigScriptTrusted(QString *detail)
{
    return helperVersionMatches(detail);
}

bool installThemesScriptTrusted(QString *detail)
{
    return helperVersionMatches(detail);
}

static bool helperListsEfiUtility(QString *detail)
{
    const QString helper = QLatin1String(EspOps::kHelperEtcPath);
    QProcess proc;
    proc.setProcessChannelMode(QProcess::SeparateChannels);
    proc.start(helper, {});
    QString text;
    if (proc.waitForStarted() && proc.waitForFinished(10000)) {
        text = QString::fromLocal8Bit(proc.readAllStandardOutput());
        text += QString::fromLocal8Bit(proc.readAllStandardError());
    }
    if (EfiUtil::usageListsSubcommand(text, QStringLiteral("efi-utility")))
        return true;
    if (detail) {
        *detail = QCoreApplication::translate(
            "Platform",
            "%1 does not provide the efi-utility operation (expected version %2). "
            "Reinstall rEFInd_GUI to repair.")
                      .arg(helper, QLatin1String(ESPOPS_APP_VERSION));
    }
    return false;
}

int runEfiUtility(QString *output)
{
    return runRootHelperCaptured(QStringLiteral("efi-utility"), output);
}

bool efiUtilityReady(QString *detail)
{
    if (!helperVersionMatches(detail))
        return false;
    return helperListsEfiUtility(detail);
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

static bool setRandomizerUnit(const QString &unit, bool enable)
{
    const QString action = enable ? QStringLiteral("enable") : QStringLiteral("disable");
    const QString command = QStringLiteral(
        "sudo systemctl %1 --now %2 && "
        "sudo systemctl status %2; exec bash").arg(action, unit);
    return QProcess::startDetached(QStringLiteral("xterm"),
                                   {QStringLiteral("-e"), QStringLiteral("bash"),
                                    QStringLiteral("-c"), command});
}

bool setBackgroundRandomizer(bool enable)
{
    return setRandomizerUnit(QStringLiteral("rEFInd_bg_randomizer.service"), enable);
}

bool setThemeRandomizer(bool enable)
{
    return setRandomizerUnit(QStringLiteral("rEFInd_theme_randomizer.service"), enable);
}

bool firmwareBootnumSupported()
{
    return true;
}

bool ukiRefreshHookInstalled()
{
    // Packages ship it under /usr/lib; /etc is the admin override location.
    return QFile::exists(QStringLiteral("/usr/lib/kernel/install.d/99-zz-rEFInd_GUI-uki.install"))
           || QFile::exists(QStringLiteral("/etc/kernel/install.d/99-zz-rEFInd_GUI-uki.install"));
}

bool preferWindowsAsDefault()
{
    return false; // on Linux, default to the running distro
}

QStringList installSourceOptions()
{
    // Arch Linux ARM carries no refind package, so on an ARM64 pacman system
    // the package-manager installer can only fail; the SourceForge zip ships
    // refind_aa64.efi and is the one working source there.
    const QString cpu = QSysInfo::currentCpuArchitecture();
    if ((cpu == QLatin1String("arm64") || cpu == QLatin1String("aarch64"))
        && QFile::exists(QStringLiteral("/usr/bin/pacman")))
        return {QStringLiteral("Sourceforge")};
    return {QStringLiteral("Package Mgr"), QStringLiteral("Sourceforge")};
}

#endif

} // namespace Platform
