#ifndef EFIUTILITIESDIALOG_H
#define EFIUTILITIESDIALOG_H

#include "efiutil/catalog.h"
#include "efiutil/netbootxyz.h"
#include "efiutil/request.h"
#include "efiutil/secureboot.h"

#include <QDialog>
#include <QHash>

class QCheckBox;
class QComboBox;
class QLabel;
class QNetworkAccessManager;
class QNetworkReply;
class QPushButton;

class EfiUtilitiesDialog : public QDialog
{
    Q_OBJECT

public:
    EfiUtilitiesDialog(const QString &showtoolsText, const QString &stagedConfPath,
                       const QString &settingsPath, QWidget *parent = nullptr);

signals:
    void showtoolsEdited(const QString &text);
    void installConfigRequested();

private:
    void populateUtilities();
    void populateEsps();
    void populateVariants();
    void applyUtilityChrome();
    void refreshRelease();
    void refreshNetbootRelease();
    void refreshShellRelease();
    void refreshMemtestRelease();
    void refreshPinned();
    void refreshLocal();
    void refreshStatus();
    void applyStatus(const QString &output, int code);
    void updateButtons();
    void beginInstall();
    void beginRemove();
    void downloadAsset();
    void finishWithPayload(const QByteArray &payload, const QString &asset,
                           const QString &version, const QString &sha256);
    void runRequest(const EfiUtil::Request &request, const QByteArray &payload);
    void applyShowtoolsAfterInstall();
    void applyShowtoolsAfterRemove();
    QString secureBootText() const;
    QString helperRepairText() const;
    QString variantLabel(const QString &id) const;
    void rememberShowtoolsFlag(bool weAdded);
    bool showtoolsFlag() const;
    QString selectedGuid() const;
    QString selectedAsset() const;
    QString currentUtility() const;
    QString currentInstalledName() const;
    QStringList showtoolsAddedBy() const;
    bool weAddedToken(const QString &token) const;
    void rememberUtility(const QString &utilityId, bool added);
    void setBusy(bool busy);
    void noteRelease(int generation, const QString &version, const QString &asset,
                     const QString &sha256, const QString &url);
    void failRelease(int generation, const QString &text);

    QString showtoolsText;
    QString stagedConfPath;
    QString settingsPath;
    QString cpuArch;
    EfiUtil::GithubRelease release;
    EfiUtil::ChecksumFile checksums;
    QString statusPresence = QStringLiteral("absent");
    QString statusVersion;
    QString statusAsset;
    QString statusSha;
    QString liveShowtools = QStringLiteral("unreadable");
    QString liveExcluded = QStringLiteral("unreadable");
    EfiUtil::SecureBoot secureBoot = EfiUtil::SecureBoot::Unknown;
    bool releaseReady = false;
    int releaseGeneration = 0;
    int statusGeneration = 0;
    QString pendingVersion;
    QString pendingAsset;
    QString pendingSha;
    QString pendingUrl;

    QLabel *notice = nullptr;
    QLabel *builtInNote = nullptr;
    QComboBox *utilityCombo = nullptr;
    QLabel *secureLabel = nullptr;
    QLabel *statusLabel = nullptr;
    QLabel *installedLabel = nullptr;
    QLabel *latestLabel = nullptr;
    QLabel *menuLabel = nullptr;
    QComboBox *espCombo = nullptr;
    QComboBox *variantCombo = nullptr;
    QCheckBox *menuCheck = nullptr;
    QPushButton *refreshButton = nullptr;
    QPushButton *installButton = nullptr;
    QPushButton *updateButton = nullptr;
    QPushButton *removeButton = nullptr;
    QNetworkAccessManager *network = nullptr;
};

#endif
