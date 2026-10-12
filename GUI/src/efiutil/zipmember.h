// Read one member out of a ZIP archive. Used for the memtest86+ binaries
// zip and the GPT fdisk EFI zip. No other members are returned.

#ifndef EFIUTIL_ZIPMEMBER_H
#define EFIUTIL_ZIPMEMBER_H

#include <QByteArray>
#include <QRegularExpression>
#include <QString>

namespace EfiUtil {

inline constexpr qint64 kMaxZipBytes = 16 * 1024 * 1024;

struct ZipMember
{
    bool ok = false;
    QString error;
    QString name;
    QByteArray bytes;
};

// `namePattern` is matched against the whole member name. A name containing
// "..", a backslash, or a leading slash is never returned. Zero or more
// than one match fails closed. Stored and raw-deflate members are accepted.
ZipMember extractZipMember(const QByteArray &zip, const QRegularExpression &namePattern,
                           qint64 maxBytes);

} // namespace EfiUtil

#endif
