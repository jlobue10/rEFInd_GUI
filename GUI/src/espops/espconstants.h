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
