#ifndef EFIUTIL_REQUEST_H
#define EFIUTIL_REQUEST_H

#include <QString>

namespace EfiUtil {

// Fixed paths under the per-user data directory. The helper reads these
// through the user-context file handoff. The request cannot name a
// destination path; the installed filename is fixed in the helper.
inline constexpr char kRequestRelative[] = "GUI/efi-utilities/request";
inline constexpr char kPayloadRelative[] = "GUI/efi-utilities/payload.bin";

struct Request
{
    bool ok = false;
    QString error;
    QString action; // status, install, remove
    QString utility;
    QString espGuid; // lowercase
    QString asset;
    QString version;
    QString sha256;
    bool replace = false;
};

bool isPartitionGuid(const QString &guid);

Request parseRequest(const QString &text);
QString formatRequest(const Request &request);

} // namespace EfiUtil

#endif
