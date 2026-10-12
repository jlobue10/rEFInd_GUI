#ifndef EFIUTIL_SECUREBOOT_H
#define EFIUTIL_SECUREBOOT_H

#include <QByteArray>
#include <QString>

namespace EfiUtil {

enum class SecureBoot { Unknown, Disabled, Enabled };

// `rawFile` is the efivarfs file, including the 4-byte Linux attribute
// prefix. The Secure Boot flag is the fifth byte (1 = enabled), which is
// how the rEFInd install scripts read it. A short buffer is Unknown.
SecureBoot secureBootFromEfivar(const QByteArray &rawFile);

// Standing limitation plus a stronger warning when Secure Boot is on.
// Always non-empty: the installed EFI is not upstream's signed disk image.
QString secureBootNotice(SecureBoot state);

} // namespace EfiUtil

#endif
