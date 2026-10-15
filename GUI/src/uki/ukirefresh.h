// `rEFInd_GUI_helper uki-refresh` (Linux): re-points the marked UKI stanzas
// of the live refind.conf at the UKIs on the ESP now. Run as root by the
// kernel-install plugin (99-zz-rEFInd_GUI-uki.install) after every kernel
// add/remove, and by the helper itself right after a successful
// install-config, so a config staged before a kernel update is never
// published with a deleted loader.
//
// rEFInd_GUI only (SteamDeck_rEFInd has no UKI flow): dispatched from
// helper/main.cpp next to efi-utility, not from the parity-locked espops.

#ifndef UKI_UKIREFRESH_H
#define UKI_UKIREFRESH_H

#include <QString>

namespace Uki {

// Exit codes: 0 refreshed / nothing to do / no rEFInd ESP (the kernel
// install must never fail because of the boot menu), 2 not root, 5 the
// rewritten config could not be published (the previous one stays live).
// `quiet` suppresses the "nothing to do" lines (used after install-config).
// When not quiet (the kernel-install path) and refind.conf has UKI
// stanzas, also hands the boot priority back to rEFInd after a distro tool
// (Fedora uki-direct) pointed BootNext/BootOrder at the new kernel's own
// firmware entry; see Uki::planRefindPriority().
// `removingVersion` (kernel-install remove's KERNEL_VERSION) is treated as
// uninstalled even though its modules are still on disk while the remove
// runs.
int runHelperRefresh(bool quiet, const QString &removingVersion = QString());

// The stanza refresh alone; *hasUkiStanzas reports whether the live config
// has any marked UKI stanza.
int refreshStanzas(bool quiet, const QString &removingVersion, bool *hasUkiStanzas);

} // namespace Uki

#endif // UKI_UKIREFRESH_H
