// Token edits for the main-window showtools list. The line edit stays the
// source of truth; a checkbox only appends or removes one token and leaves
// the rest in order. This does not rewrite refind.conf.

#ifndef EFIUTIL_SHOWTOOLS_H
#define EFIUTIL_SHOWTOOLS_H

#include <QString>

namespace EfiUtil {

// Appends `token` if it is absent. Does not reorder existing tokens.
// *added is false when the token was already present, and the returned
// text is then the caller's original string.
QString appendShowtoolToken(const QString &text, const QString &token, bool *added);

// Removes `token` if present. Other tokens stay in order. When the token
// was absent, the caller's text is returned unchanged so separators are
// not rewritten.
QString removeShowtoolToken(const QString &text, const QString &token, bool *removed);

} // namespace EfiUtil

#endif
