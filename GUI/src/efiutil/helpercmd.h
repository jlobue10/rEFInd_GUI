// Privileged entry point for `helper efi-utility` and the in-process
// Windows path. Downloads stay in the GUI; this only reads the staged
// request and payload and writes EFI/tools/ipxe.efi.

#ifndef EFIUTIL_HELPERCMD_H
#define EFIUTIL_HELPERCMD_H

#include <QString>

namespace EfiUtil {

// Linux helper. Prints short English lines and returns the exit code.
int runEfiUtilityHelper();

// Already-elevated caller (the Windows GUI). `dataDir` is Platform::dataDir().
int runEfiUtilityInProcess(const QString &dataDir, QString *output);

} // namespace EfiUtil

#endif
