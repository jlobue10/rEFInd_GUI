#include "uki/ukirefresh.h"

#include "espops/espconstants.h"
#include "espops/espresolve.h"
#include "espops/userio.h"
#include "uki/uki.h"

#include <QCryptographicHash>
#include <QRegularExpression>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QHash>
#include <QTemporaryFile>

#include <cstdio>

#include <unistd.h>

namespace Uki {

namespace {

const qint64 kMaxConfBytes = 4 * 1024 * 1024;

void say(const QString &line)
{
    std::printf("%s: %s\n", EspOps::kProductName, line.toLocal8Bit().constData());
}

QByteArray sha256Hex(const QByteArray &data)
{
    return QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex();
}

bool looksLikeGuid(const QString &v)
{
    static const QRegularExpression re(
        QStringLiteral("^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}$"));
    return re.match(v).hasMatch();
}

// Same-directory staged write + rename (FAT has no atomic rename; this is
// the publish-last pattern configinstall uses).
bool publishBytes(const QString &dir, const QString &name, const QByteArray &bytes)
{
    QString staged;
    {
        QTemporaryFile stage(dir + QStringLiteral("/.%1.new.XXXXXX").arg(name));
        stage.setAutoRemove(false);
        if (!stage.open())
            return false;
        staged = stage.fileName();
        if (stage.write(bytes) != bytes.size() || !stage.flush()) {
            stage.close();
            QFile::remove(staged);
            return false;
        }
    }
    if (!EspOps::publishRename(staged, dir + QLatin1Char('/') + name)) {
        QFile::remove(staged);
        return false;
    }
    return true;
}

// Keeps refind.conf.origin truthful: when it attributed the outgoing config
// to a GUI install (its sha256 matched), carry the attribution over to the
// refreshed bytes -- otherwise the next Install Config would report the
// refresh as a hand edit. A sidecar that already did not match is left
// alone; that config was hand-edited before this ran.
void updateOrigin(const QString &refindDir, const QByteArray &oldSha, const QByteArray &newSha)
{
    const QString name = QStringLiteral("refind.conf.origin");
    QFile f(refindDir + QLatin1Char('/') + name);
    if (!f.open(QIODevice::ReadOnly))
        return;
    const QList<QByteArray> lines = f.read(64 * 1024).split('\n');
    f.close();
    QByteArray out;
    bool matched = false;
    for (const QByteArray &raw : lines) {
        const QByteArray line = raw.trimmed();
        if (line.startsWith("sha256=")) {
            if (line.mid(7) != oldSha)
                return;
            matched = true;
            out += "sha256=" + newSha + '\n';
        } else if (line.startsWith("uki_refreshed=") || (line.isEmpty() && &raw == &lines.last())) {
            continue;
        } else {
            out += raw + '\n';
        }
    }
    if (!matched)
        return;
    out += "uki_refreshed=" + QDateTime::currentDateTime().toString(Qt::ISODate).toUtf8() + '\n';
    publishBytes(refindDir, name, out);
}

} // namespace

int runHelperRefresh(bool quiet)
{
    if (geteuid() != 0) {
        say(QStringLiteral("uki-refresh must run as root (it is run by kernel-install)."));
        return 2;
    }

    EspOps::EspResolver resolver;
    EspOps::RefindTarget target;
    if (!resolver.resolve(&target)) {
        if (!quiet)
            say(QStringLiteral("no ESP with rEFInd on it; nothing to refresh."));
        return 0;
    }
    const QString confPath = target.refindDir + QLatin1String("/refind.conf");
    QFile conf(confPath);
    if (!conf.open(QIODevice::ReadOnly)) {
        if (!quiet)
            say(QStringLiteral("no refind.conf at %1; nothing to refresh.").arg(target.refindDir));
        return 0;
    }
    const QByteArray oldBytes = conf.read(kMaxConfBytes + 1);
    conf.close();
    if (oldBytes.size() > kMaxConfBytes) {
        say(QStringLiteral("%1 is implausibly large; left unchanged.").arg(confPath));
        return 0;
    }
    const QString oldText = QString::fromUtf8(oldBytes);
    if (markedStanzas(oldText).isEmpty()) {
        if (!quiet)
            say(QStringLiteral("refind.conf has no UKI stanzas; nothing to refresh."));
        return 0;
    }

    // rEFInd's own ESP is where an unqualified loader resolves; any other
    // volume is a partition GUID the resolver can mount read-only.
    const QString refindEspRoot = QDir::cleanPath(target.refindDir + QLatin1String("/../.."));
    QHash<QString, QList<Family>> familiesByVolume;
    QStringList notes;
    const FamilyLookup lookup = [&](const QString &volume) {
        const QString key = volume.toLower();
        if (!familiesByVolume.contains(key)) {
            QString root;
            if (volume.isEmpty()) {
                root = refindEspRoot;
            } else if (looksLikeGuid(volume)) {
                QString how, error;
                if (!resolver.mountByPartUuid(volume.toLower(), &root, &how, &error)) {
                    notes << QStringLiteral("Could not open volume %1: %2").arg(volume, error);
                    root.clear();
                }
            } else {
                notes << QStringLiteral("Volume '%1' is not a partition GUID; skipped.").arg(volume);
            }
            familiesByVolume.insert(key, root.isEmpty() ? QList<Family>() : scanFamilies(root));
        }
        return familiesByVolume.value(key);
    };

    bool changed = false;
    const QString newText = refreshConf(oldText, lookup, &changed, &notes);
    for (const QString &n : notes)
        say(n);
    if (!changed) {
        if (!quiet)
            say(QStringLiteral("UKI stanzas are already current."));
        return 0;
    }

    resolver.makeWritable(target.refindDir);
    const QByteArray newBytes = newText.toUtf8();
    if (!publishBytes(target.refindDir, QStringLiteral("refind.conf"), newBytes)) {
        say(QStringLiteral("could not write %1; the previous config is still active.").arg(confPath));
        return 5;
    }
    updateOrigin(target.refindDir, sha256Hex(oldBytes), sha256Hex(newBytes));
    ::sync();
    say(QStringLiteral("re-pointed the UKI boot stanzas in %1 (%2).").arg(confPath, target.how));
    return 0;
}

} // namespace Uki
