// `rEFInd_GUI_helper uki-refresh` (Linux): re-points the marked UKI stanzas
// of the live refind.conf at the UKIs on the ESP now. Run as root by the
// kernel-install plugin (95-rEFInd_GUI-uki.install) after every kernel
// add/remove, and by the helper itself right after a successful
// install-config, so a config staged before a kernel update is never
// published with a deleted loader.
//
// rEFInd_GUI only (SteamDeck_rEFInd has no UKI flow): dispatched from
// helper/main.cpp next to efi-utility, not from the parity-locked espops.

#ifndef UKI_UKIREFRESH_H
#define UKI_UKIREFRESH_H

namespace Uki {

// Exit codes: 0 refreshed / nothing to do / no rEFInd ESP (the kernel
// install must never fail because of the boot menu), 2 not root, 5 the
// rewritten config could not be published (the previous one stays live).
// `quiet` suppresses the "nothing to do" lines (used after install-config).
int runHelperRefresh(bool quiet);

} // namespace Uki

#endif // UKI_UKIREFRESH_H
