#include "showtools.h"

#include <QRegularExpression>

namespace EfiUtil {

namespace {

QStringList splitTokens(const QString &text)
{
    static const QRegularExpression sep(QStringLiteral("[,\\s]+"));
    QStringList out;
    const QStringList raw = text.split(sep, Qt::SkipEmptyParts);
    for (const QString &t : raw) {
        const QString token = t.toLower();
        if (!out.contains(token))
            out << token;
    }
    return out;
}

QString joinTokens(const QStringList &tokens)
{
    return tokens.join(QLatin1Char(','));
}

bool isComment(const QString &line)
{
    return line.trimmed().startsWith(QLatin1Char('#'));
}

// The argument text of a `showtools` line, or a null QString when the line
// is not that directive.
QString showtoolsArgs(const QString &line)
{
    const QString trimmed = line.trimmed();
    if (isComment(line))
        return QString();
    static const QRegularExpression re(
        QStringLiteral("^showtools\\b(.*)$"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch m = re.match(trimmed);
    if (!m.hasMatch())
        return QString();
    return m.captured(1);
}

bool lineNamesFile(const QString &token, const QString &fileName)
{
    const QString folded = token.trimmed();
    const QString base = fileName.toLower();
    if (folded.compare(fileName, Qt::CaseInsensitive) == 0)
        return true;
    const QString slash = QLatin1Char('/') + fileName;
    const QString back = QLatin1Char('\\') + fileName;
    return folded.endsWith(slash, Qt::CaseInsensitive)
        || folded.endsWith(back, Qt::CaseInsensitive);
}

} // namespace

QStringList showtoolsTokens(const QString &text)
{
    return splitTokens(text);
}

bool showtoolsContains(const QString &text, const QString &token)
{
    return splitTokens(text).contains(token.toLower());
}

QString appendShowtoolToken(const QString &text, const QString &token, bool *added)
{
    QStringList tokens = splitTokens(text);
    const QString want = token.toLower();
    if (tokens.contains(want)) {
        if (added)
            *added = false;
        return text;
    }
    tokens << want;
    if (added)
        *added = true;
    return joinTokens(tokens);
}

QString removeShowtoolToken(const QString &text, const QString &token, bool *removed)
{
    QStringList tokens = splitTokens(text);
    const bool did = tokens.removeAll(token.toLower()) > 0;
    if (removed)
        *removed = did;
    return joinTokens(tokens);
}

ShowtoolsPolicy enableShowtool(const QString &text, const QString &token, bool weAdded)
{
    ShowtoolsPolicy out;
    out.weAdded = weAdded;
    bool added = false;
    out.text = appendShowtoolToken(text, token, &added);
    if (added)
        out.weAdded = true;
    out.changed = added;
    return out;
}

ShowtoolsPolicy disableShowtool(const QString &text, const QString &token, bool weAdded)
{
    ShowtoolsPolicy out;
    out.weAdded = weAdded;
    out.text = text;
    if (!weAdded)
        return out;
    bool removed = false;
    out.text = removeShowtoolToken(showtoolsTokens(text).isEmpty() && text.trimmed().isEmpty()
                                       ? text
                                       : joinTokens(showtoolsTokens(text)),
                                   token, &removed);
    // removeShowtoolToken canonicalizes separators. When the token was not
    // present, keep the caller's text so an unrelated edit is not implied.
    if (!removed) {
        out.text = text;
        return out;
    }
    out.weAdded = false;
    out.changed = true;
    return out;
}

bool showtoolsListIsEmpty(const QString &text)
{
    return showtoolsTokens(text).isEmpty();
}

ConfPatch patchShowtools(const QString &conf, const QString &token, bool enable)
{
    ConfPatch out;
    out.text = conf;
    bool any = false;
    QStringList lines = conf.split(QLatin1Char('\n'));
    const bool trailingNl = conf.endsWith(QLatin1Char('\n'));
    for (QString &line : lines) {
        const QString args = showtoolsArgs(line);
        if (args.isNull())
            continue;
        any = true;
        bool changed = false;
        const QString next = enable ? appendShowtoolToken(args, token, &changed)
                                    : removeShowtoolToken(args, token, &changed);
        if (!changed)
            continue;
        const int lead = line.indexOf(QRegularExpression(QStringLiteral("\\S")));
        const QString indent = lead > 0 ? line.left(lead) : QString();
        line = indent + QStringLiteral("showtools");
        if (!next.isEmpty())
            line += QLatin1Char(' ') + next;
        out.changed = true;
    }
    out.hadDirective = any;
    if (out.changed) {
        out.text = lines.join(QLatin1Char('\n'));
        if (trailingNl && !out.text.endsWith(QLatin1Char('\n')))
            out.text += QLatin1Char('\n');
    }
    return out;
}

bool dontScanToolsExcludes(const QString &conf, const QString &fileName)
{
    const QStringList lines = conf.split(QLatin1Char('\n'));
    static const QRegularExpression re(
        QStringLiteral("^dont_scan_tools\\b(.*)$"),
        QRegularExpression::CaseInsensitiveOption);
    for (const QString &line : lines) {
        if (isComment(line))
            continue;
        const QRegularExpressionMatch m = re.match(line.trimmed());
        if (!m.hasMatch())
            continue;
        const QStringList tokens = m.captured(1).split(
            QRegularExpression(QStringLiteral("[,\\s]+")), Qt::SkipEmptyParts);
        for (const QString &token : tokens) {
            if (lineNamesFile(token, fileName))
                return true;
        }
    }
    return false;
}

QStringList liveShowtoolsTokens(const QString &conf, bool *present)
{
    if (present)
        *present = false;
    QStringList last;
    bool saw = false;
    for (const QString &line : conf.split(QLatin1Char('\n'))) {
        const QString args = showtoolsArgs(line);
        if (args.isNull())
            continue;
        saw = true;
        last = showtoolsTokens(args);
    }
    if (present)
        *present = saw;
    return last;
}

} // namespace EfiUtil
