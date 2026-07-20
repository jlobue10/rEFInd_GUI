#ifndef PLATFORM_H
#define PLATFORM_H

#include <QString>
#include <QStringList>

// Thin platform layer so mainwindow.cpp stays free of #ifdef Q_OS_WIN.
// Linux: xterm + shell scripts + systemd. Windows: PowerShell scripts +
// Scheduled Task (the exe runs elevated via its requireAdministrator manifest).
namespace Platform {

// App data root: ~/.local/rEFInd_GUI on Linux, %LOCALAPPDATA%\rEFInd_GUI on Windows.
QString dataDir();

// Launches the rEFInd installer for the chosen source in a new terminal
// window. Returns false if the launch itself failed.
bool runInstallerScript(const QString &installSource);

// Installs the generated config + PNGs onto the ESP (blocking, no visible
// window). Returns the process exit code (0 = success) and captures the
// script's combined output into *output for the caller to present.
int installConfig(QString *output = nullptr);

// True when the config-install script on disk is byte-identical (SHA-256) to
// the copy this build shipped, so it is safe to run with root privileges. On
// mismatch (tampered, missing, or from another version) returns false and
// puts the offending file's path in *detail — the caller must refuse to run
// it and suggest reinstalling. Always true on Windows: the .ps1 scripts are
// Authenticode-signed instead, and signing rewrites the file so a build-time
// hash could never match.
bool installConfigScriptTrusted(QString *detail = nullptr);

// Runs the elevated ESP scan (scan_esp.sh), caching the EFI/ tree for
// detection to read. Blocks while the script prompts for a password and shows
// its own result dialogs. Returns 0 on success. Linux only: the Windows build
// runs elevated and scans ESPs directly.
int runEspDeepScan();

// Whether an ESP the GUI wants to scan is unreadable, so the Deep Scan button
// is worth offering. False on Windows.
bool espDeepScanUseful();

// Enables/disables the boot-background randomizer (systemd unit / scheduled
// task) in a visible terminal window. Returns false if the launch failed.
bool setBackgroundRandomizer(bool enable);

// SteamOS firmware_bootnum lookup needs efibootmgr (Linux only).
bool firmwareBootnumSupported();

// Which OS should lead the auto-selection (slot 1 + default boot):
// Windows on the Windows build, the running Linux distro on Linux.
bool preferWindowsAsDefault();

// Entries for the Install Source combo box.
QStringList installSourceOptions();

} // namespace Platform

#endif // PLATFORM_H
