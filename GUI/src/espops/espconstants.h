// Repo-specific constants for the parity-locked espops/ code and the helper
// binary. This is the ONE espops file that differs between rEFInd_GUI and
// SteamDeck_rEFInd — everything else in espops/ and helper/ must stay
// byte-identical between the repos (same workflow as the osdetect_* files).

#ifndef ESPOPS_ESPCONSTANTS_H
#define ESPOPS_ESPCONSTANTS_H

namespace EspOps {

// Product name as used in messages and the helper's usage text.
constexpr const char kProductName[] = "rEFInd_GUI";

// Data dir name under ~/.local (Linux) / %LOCALAPPDATA% (Windows).
constexpr const char kDataDirName[] = "rEFInd_GUI";

// Root-owned install location of the helper binary on Linux — the path the
// sudoers NOPASSWD lines and the systemd randomizer units point at.
constexpr const char kHelperEtcPath[] = "/etc/rEFInd/rEFInd_GUI_helper";

// The user-facing installer script, named in "re-run the installer"
// diagnostics.
constexpr const char kInstallerName[] = "install-rEFInd-GUI.sh";

// Root-owned pointer file naming the user's backgrounds directory, written
// by the installer and read (as data, never sourced) by the background
// randomizer.
constexpr const char kBackgroundDirPointer[] = "/etc/rEFInd/background-dir";

// Windows only. The at-logon Scheduled Tasks this product manages: the
// suffix appended to kProductName for the task name, and the helper
// subcommand the task runs. Null-terminated. `migrate-tasks` re-points
// every one of these that already exists at the installed helper exe.
struct LogonTaskSpec
{
    const char *nameSuffix;
    const char *subcommand;
};
inline constexpr LogonTaskSpec kLogonTasks[] = {
    {"_bg_randomizer", "randomize-background"},
    {"_theme_randomizer", "randomize-theme"},
    {nullptr, nullptr},
};

// Windows only. Task names registered by earlier versions, whose action ran
// a .ps1 wrapper that no longer ships. On upgrade each one that still exists
// is re-registered under its current name and then unregistered, so an
// enabled feature survives instead of silently failing at every logon.
// Null-terminated.
struct LegacyLogonTask
{
    const char *oldName;
    const char *newNameSuffix;
    const char *subcommand;
};
inline constexpr LegacyLogonTask kLegacyLogonTasks[] = {
    {"rEFInd_bg_randomizer", "_bg_randomizer", "randomize-background"},
    {nullptr, nullptr, nullptr},
};

// "Running system's ESP" fallback mountpoints, in preference order, for
// when NVRAM resolution finds nothing (first install, not yet booted
// through rEFInd). Null-terminated.
inline constexpr const char *kSystemEspMountpoints[] = {
    "/boot/efi",
    "/efi",
    "/boot",
    nullptr,
};

} // namespace EspOps

#endif // ESPOPS_ESPCONSTANTS_H
