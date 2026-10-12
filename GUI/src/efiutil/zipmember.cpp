#include "zipmember.h"

#include "netbootxyz.h"

#include <QList>

#include <zlib.h>

#include <QtEndian>

namespace EfiUtil {

namespace {

constexpr quint32 kEocdSig = 0x06054b50u;
constexpr quint32 kCentralSig = 0x02014b50u;
constexpr quint32 kLocalSig = 0x04034b50u;

bool rangeOk(qsizetype size, int offset, int length)
{
    return offset >= 0 && length >= 0 && offset <= size && length <= size - offset;
}

quint16 read16(const QByteArray &b, int offset)
{
    return qFromLittleEndian<quint16>(b.constData() + offset);
}

quint32 read32(const QByteArray &b, int offset)
{
    return qFromLittleEndian<quint32>(b.constData() + offset);
}

bool unsafeName(const QString &name)
{
    if (name.isEmpty() || name.startsWith(QLatin1Char('/')) || name.startsWith(QLatin1Char('\\')))
        return true;
    if (name.contains(QLatin1Char('\\')) || name.contains(QLatin1String("..")))
        return true;
    return false;
}

QByteArray inflateMember(const QByteArray &input, qint64 expected)
{
    if (expected < 0 || expected > kMaxZipBytes)
        return {};
    z_stream stream = {};
    if (inflateInit2(&stream, -MAX_WBITS) != Z_OK)
        return {};
    QByteArray out(int(expected), Qt::Uninitialized);
    stream.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(input.constData()));
    stream.avail_in = uInt(input.size());
    stream.next_out = reinterpret_cast<Bytef *>(out.data());
    stream.avail_out = uInt(out.size());
    const int rc = inflate(&stream, Z_FINISH);
    const uLong produced = stream.total_out;
    inflateEnd(&stream);
    if (rc != Z_STREAM_END || qint64(produced) != expected)
        return {};
    return out;
}

ZipMember fail(const QString &error)
{
    ZipMember member;
    member.error = error;
    return member;
}

} // namespace

ZipMember extractZipMember(const QByteArray &zip, const QRegularExpression &namePattern,
                           qint64 maxBytes)
{
    if (zip.size() < 22 || zip.size() > kMaxZipBytes)
        return fail(QStringLiteral("The archive is empty or larger than the size cap."));
    if (!namePattern.isValid() || maxBytes <= 0 || maxBytes > kMaxEfiBytes)
        return fail(QStringLiteral("The archive member request is not valid."));

    int eocd = -1;
    const int scanFrom = qMax(0, int(zip.size()) - (65535 + 22));
    for (int i = int(zip.size()) - 22; i >= scanFrom; --i) {
        if (read32(zip, i) == kEocdSig) {
            eocd = i;
            break;
        }
    }
    if (eocd < 0)
        return fail(QStringLiteral("The archive has no end-of-central-directory record."));
    if (!rangeOk(zip.size(), eocd, 22))
        return fail(QStringLiteral("The archive directory is truncated."));

    const quint16 diskEntries = read16(zip, eocd + 8);
    const quint16 totalEntries = read16(zip, eocd + 10);
    const quint32 dirSize = read32(zip, eocd + 12);
    const quint32 dirOffset = read32(zip, eocd + 16);
    const quint16 commentLen = read16(zip, eocd + 20);
    if (diskEntries != totalEntries || totalEntries == 0 || totalEntries > 256)
        return fail(QStringLiteral("The archive directory is not a single complete zip."));
    if (commentLen != 0 && !rangeOk(zip.size(), eocd + 22, commentLen))
        return fail(QStringLiteral("The archive comment is truncated."));
    if (dirOffset == 0xffffffffu || dirSize == 0xffffffffu)
        return fail(QStringLiteral("Zip64 archives are not accepted."));
    if (!rangeOk(zip.size(), int(dirOffset), int(dirSize)))
        return fail(QStringLiteral("The archive directory is outside the file."));

    struct Entry
    {
        QString name;
        quint16 method = 0;
        quint32 compSize = 0;
        quint32 uncompSize = 0;
        quint32 localOffset = 0;
        quint16 flags = 0;
    };
    QList<Entry> matches;
    int cursor = int(dirOffset);
    const int dirEnd = int(dirOffset) + int(dirSize);
    for (quint16 n = 0; n < totalEntries; ++n) {
        if (!rangeOk(zip.size(), cursor, 46) || cursor + 46 > dirEnd)
            return fail(QStringLiteral("A central-directory entry is truncated."));
        if (read32(zip, cursor) != kCentralSig)
            return fail(QStringLiteral("The archive directory is damaged."));
        Entry entry;
        entry.flags = read16(zip, cursor + 8);
        entry.method = read16(zip, cursor + 10);
        entry.compSize = read32(zip, cursor + 20);
        entry.uncompSize = read32(zip, cursor + 24);
        const quint16 nameLen = read16(zip, cursor + 28);
        const quint16 extraLen = read16(zip, cursor + 30);
        const quint16 commentLenEntry = read16(zip, cursor + 32);
        entry.localOffset = read32(zip, cursor + 42);
        if (entry.compSize == 0xffffffffu || entry.uncompSize == 0xffffffffu
            || entry.localOffset == 0xffffffffu)
            return fail(QStringLiteral("Zip64 members are not accepted."));
        if (!rangeOk(zip.size(), cursor + 46, nameLen))
            return fail(QStringLiteral("A member name is truncated."));
        entry.name = QString::fromUtf8(zip.constData() + cursor + 46, nameLen);
        const int next = cursor + 46 + nameLen + extraLen + commentLenEntry;
        if (!rangeOk(zip.size(), cursor, next - cursor) || next > dirEnd)
            return fail(QStringLiteral("A central-directory entry overruns the directory."));
        cursor = next;
        if (unsafeName(entry.name) || !namePattern.match(entry.name).hasMatch())
            continue;
        if (entry.flags & 0x1)
            return fail(QStringLiteral("Encrypted archive members are not accepted."));
        matches.append(entry);
    }
    if (matches.size() != 1)
        return fail(QStringLiteral("The archive does not contain exactly one matching member."));

    const Entry entry = matches.first();
    if (qint64(entry.uncompSize) > maxBytes || qint64(entry.compSize) > kMaxZipBytes)
        return fail(QStringLiteral("The archive member is larger than the size cap."));
    const int local = int(entry.localOffset);
    if (!rangeOk(zip.size(), local, 30) || read32(zip, local) != kLocalSig)
        return fail(QStringLiteral("The archive member header is missing."));
    const quint16 localNameLen = read16(zip, local + 26);
    const quint16 localExtraLen = read16(zip, local + 28);
    if (!rangeOk(zip.size(), local + 30, localNameLen))
        return fail(QStringLiteral("The archive member name is truncated."));
    const QString localName = QString::fromUtf8(zip.constData() + local + 30, localNameLen);
    if (localName != entry.name)
        return fail(QStringLiteral("The archive member name does not match the directory."));
    const int dataOffset = local + 30 + localNameLen + localExtraLen;
    if (!rangeOk(zip.size(), dataOffset, int(entry.compSize)))
        return fail(QStringLiteral("The archive member data is truncated."));
    const QByteArray compressed = zip.mid(dataOffset, int(entry.compSize));

    QByteArray bytes;
    if (entry.method == 0) {
        if (entry.compSize != entry.uncompSize)
            return fail(QStringLiteral("The stored archive member has inconsistent sizes."));
        bytes = compressed;
    } else if (entry.method == 8) {
        bytes = inflateMember(compressed, entry.uncompSize);
        if (bytes.size() != int(entry.uncompSize))
            return fail(QStringLiteral("The archive member did not decompress."));
    } else {
        return fail(QStringLiteral("The archive member uses an unsupported compression method."));
    }

    ZipMember member;
    member.ok = true;
    member.name = entry.name;
    member.bytes = bytes;
    return member;
}

} // namespace EfiUtil
