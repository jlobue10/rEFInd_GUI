#include "showtools.h"

#include <QRegularExpression>
#include <QStringList>

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

} // namespace

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
    if (!did)
        return text;
    return joinTokens(tokens);
}

} // namespace EfiUtil
