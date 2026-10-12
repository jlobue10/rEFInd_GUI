// showtools token edits. These functions never rewrite scanfor or menuentry
// stanzas; they only add or remove the single "netboot" token.

#ifndef EFIUTIL_SHOWTOOLS_H
#define EFIUTIL_SHOWTOOLS_H

#include <QString>
#include <QStringList>

namespace EfiUtil {

QStringList showtoolsTokens(const QString &text);
bool showtoolsContains(const QString &text, const QString &token);

// Appends `token` if it is absent. Does not reorder existing tokens.
QString appendShowtoolToken(const QString &text, const QString &token, bool *added);

// Removes `token` if present. Other tokens stay in order.
QString removeShowtoolToken(const QString &text, const QString &token, bool *removed);

struct ShowtoolsPolicy
{
    QString text;
    bool weAdded = false;
    bool changed = false;
};

// Remembers whether this feature inserted the token, so a later removal
// does not strip a token the user already had.
ShowtoolsPolicy enableShowtool(const QString &text, const QString &token, bool weAdded);
ShowtoolsPolicy disableShowtool(const QString &text, const QString &token, bool weAdded);

// True when the box is empty, so publishing it would list only the token
// we are about to add and hide every other tool.
bool showtoolsListIsEmpty(const QString &text);

struct ConfPatch
{
    QString text;
    bool changed = false;
    bool hadDirective = false;
};

// Edits uncommented `showtools` lines only. A config with no such line is
// returned unchanged (hadDirective false) so a hand-written file that relies
// on rEFInd's built-in default is not given a new directive here.
ConfPatch patchShowtools(const QString &conf, const QString &token, bool enable);

// True when an uncommented dont_scan_tools line names this filename, either
// bare or as the last path component.
bool dontScanToolsExcludes(const QString &conf, const QString &fileName);

// The token list on the last uncommented showtools line, or empty when the
// directive is absent. *present is set when a directive line exists.
QStringList liveShowtoolsTokens(const QString &conf, bool *present);

} // namespace EfiUtil

#endif
