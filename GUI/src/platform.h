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

// Installs the generated config + PNGs onto the ESP (blocking).
// Returns the process exit code (0 = success).
int installConfig();

// Enables/disables the boot-background randomizer (systemd unit / scheduled
// task) in a visible terminal window. Returns false if the launch failed.
bool setBackgroundRandomizer(bool enable);

// SteamOS firmware_bootnum lookup needs efibootmgr (Linux only).
bool firmwareBootnumSupported();

// KDE Plasma (and other Linux desktops) only show PNG thumbnails and view-mode
// options in the file dialog when the matching Qt platform integration is
// installed; without it Qt's built-in dialog previews nothing. On Linux we drive
// a non-native QFileDialog with our own PNG preview pane instead. Windows keeps
// its native Explorer dialog, which already previews images.
bool useImagePreviewDialog();

// Which OS should lead the auto-selection (slot 1 + default boot):
// Windows on the Windows build, the running Linux distro on Linux.
bool preferWindowsAsDefault();

// Entries for the Install Source combo box.
QStringList installSourceOptions();

} // namespace Platform

#endif // PLATFORM_H
