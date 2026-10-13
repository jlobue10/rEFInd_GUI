#include "secureboot.h"

namespace EfiUtil {

SecureBoot secureBootFromEfivar(const QByteArray &rawFile)
{
    // efivarfs stores a 4-byte attribute header in front of the variable
    // value. The SecureBoot variable is a single byte.
    if (rawFile.size() < 5)
        return SecureBoot::Unknown;
    if (static_cast<unsigned char>(rawFile.at(4)) == 1)
        return SecureBoot::Enabled;
    if (static_cast<unsigned char>(rawFile.at(4)) == 0)
        return SecureBoot::Disabled;
    return SecureBoot::Unknown;
}

QString secureBootNotice(SecureBoot state)
{
    const QString limitation = QStringLiteral(
        "The file installed here is netboot.xyz's unsigned UEFI executable. "
        "Upstream's Secure Boot images are netboot.xyz-sb.iso and "
        "netboot.xyz-sb.img, which are disk images, not an EFI tool rEFInd "
        "can launch. This app will not enroll keys, disable Secure Boot, or "
        "sign the file.");
    if (state == SecureBoot::Enabled) {
        return QStringLiteral(
                   "Secure Boot is enabled. %1 It will not boot under Secure "
                   "Boot unless the firmware already trusts it.")
            .arg(limitation);
    }
    if (state == SecureBoot::Disabled)
        return QStringLiteral("Secure Boot is off. %1").arg(limitation);
    return QStringLiteral("Secure Boot state could not be read. %1").arg(limitation);
}

} // namespace EfiUtil
