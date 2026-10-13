// What the privileged helper is willing to install. A request names a catalog
// tool and an operation. The checksum, filename, and payload in that request
// are not credentials: this module ignores the request checksum and decides
// the bytes from a source the helper owns.

#ifndef EFIUTIL_TRUST_H
#define EFIUTIL_TRUST_H

#include "catalog.h"
#include "request.h"

#include <QByteArray>
#include <QString>

namespace EfiUtil {

struct TrustedPayload
{
    bool ok = false;
    QString error;
    QByteArray bytes;
    QString sha256;
    QString version;
    QString asset;
};

// HTTPS GET of a URL this helper already decided to fetch. Production uses
// curl. Tests return canned bodies. The URL is never taken from the request.
class PublisherFetch
{
public:
    virtual ~PublisherFetch() = default;
    virtual QByteArray get(const QString &url, QString *error) = 0;
};

// The distro-owned EFI file for a tool. Production reads the allowlisted
// path and refuses anything that is not a root-owned regular file.
class DistroBytes
{
public:
    virtual ~DistroBytes() = default;
    virtual LocalToolFile read(const QString &utilityId, const QString &cpuArch) = 0;
};

// `request.sha256` is not read. `staged` is installed only when its hash
// equals a compiled-in pin or a checksum fetched here. `onDisk` is the
// same kind of candidate for a file already on the ESP, so a repeat install
// does not need the user to stage the bytes again. Memtest86+ ignores both
// and returns the member extracted from the publisher zip.
TrustedPayload trustedPayload(const Request &request, const QByteArray &staged,
                              const QByteArray &onDisk, const QString &cpuArch,
                              PublisherFetch *fetch, DistroBytes *distro);

// uid 0, regular file, not a symlink, not group- or world-writable, and
// within the EFI size cap. `mode` is the POSIX mode bits.
bool distroStatAcceptable(bool isSymlink, bool isRegular, quint32 uid, quint32 mode,
                          qint64 size, QString *error);

// True when `usage` lists `subcommand` as its own word (the helper's usage text).
bool usageListsSubcommand(const QString &usage, const QString &subcommand);

// https only, no userinfo, no ".." , and only the publisher hosts this
// feature downloads from. A URL from a release JSON still has to pass.
bool publisherUrlAllowed(const QString &url);

// Empty or "latest" selects the compiled-in latest-release API. Any other
// version must be a single path token. An empty result means "do not fetch".
QString netbootReleaseApiUrlFor(const QString &version);
QString shellReleaseApiUrlFor(const QString &version);

class CurlPublisherFetch : public PublisherFetch
{
public:
    QByteArray get(const QString &url, QString *error) override;
};

class FilesystemDistroBytes : public DistroBytes
{
public:
    LocalToolFile read(const QString &utilityId, const QString &cpuArch) override;
};

} // namespace EfiUtil

#endif
