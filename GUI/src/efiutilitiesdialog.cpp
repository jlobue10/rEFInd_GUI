#include "efiutilitiesdialog.h"

#include "efiutil/install.h"
#include "efiutil/showtools.h"
#include "espops/espresolve.h"
#include "platform.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPushButton>
#include <QSaveFile>
#include <QSettings>
#include <QSysInfo>
#include <QUrl>
#include <QVBoxLayout>

namespace {

const char kReleaseUrl[] =
    "https://api.github.com/repos/netbootxyz/netboot.xyz/releases/latest";

QString presenceText(const QString &presence)
{
    if (presence == QLatin1String("managed"))
        return EfiUtilitiesDialog::tr("Installed (managed by rEFInd_GUI)");
    if (presence == QLatin1String("unmanaged"))
        return EfiUtilitiesDialog::tr("Present, not managed by rEFInd_GUI");
    if (presence == QLatin1String("changed"))
        return EfiUtilitiesDialog::tr("Present, changed outside rEFInd_GUI");
    return EfiUtilitiesDialog::tr("Not installed");
}

QNetworkRequest githubRequest(const QUrl &url)
{
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("rEFInd_GUI efi-utility"));
    request.setRawHeader("Accept", "application/vnd.github+json");
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
    request.setTransferTimeout(20000);
#endif
    return request;
}

} // namespace

EfiUtilitiesDialog::EfiUtilitiesDialog(const QString &showtoolsText,
                                       const QString &stagedConfPath,
                                       const QString &settingsPath, QWidget *parent)
    : QDialog(parent)
    , showtoolsText(showtoolsText)
    , stagedConfPath(stagedConfPath)
    , settingsPath(settingsPath)
    , cpuArch(QSysInfo::currentCpuArchitecture())
{
    setWindowTitle(tr("EFI Utilities"));
    network = new QNetworkAccessManager(this);

    notice = new QLabel(tr(
        "netboot.xyz is installed as EFI/tools/ipxe.efi so rEFInd's existing "
        "Netboot tool can launch it. rEFInd will label the icon \"Netboot\". "
        "This is not the legacy ipxe_discover.efi scanner, and scanfor is not changed."));
    notice->setWordWrap(true);

    QFile efivar(QStringLiteral(
        "/sys/firmware/efi/efivars/SecureBoot-8be4df61-93ca-11d2-aa0d-00e098032b8c"));
    if (efivar.open(QIODevice::ReadOnly))
        secureBoot = EfiUtil::secureBootFromEfivar(efivar.readAll());
    secureLabel = new QLabel(secureBootText());
    secureLabel->setWordWrap(true);

    espCombo = new QComboBox;
    variantCombo = new QComboBox;
    statusLabel = new QLabel(tr("Checking the EFI System Partition..."));
    statusLabel->setWordWrap(true);
    installedLabel = new QLabel(QStringLiteral("—"));
    latestLabel = new QLabel(tr("Checking..."));
    menuLabel = new QLabel(QStringLiteral("—"));
    menuLabel->setWordWrap(true);
    menuCheck = new QCheckBox(tr("Show netboot in the rEFInd tools menu"));
    menuCheck->setChecked(true);

    refreshButton = new QPushButton(tr("Refresh"));
    installButton = new QPushButton(tr("Install"));
    updateButton = new QPushButton(tr("Update"));
    removeButton = new QPushButton(tr("Remove"));
    auto *closeButton = new QPushButton(tr("Close"));
    connect(refreshButton, &QPushButton::clicked, this, [this] {
        refreshRelease();
        refreshStatus();
    });
    connect(installButton, &QPushButton::clicked, this, &EfiUtilitiesDialog::beginInstall);
    connect(updateButton, &QPushButton::clicked, this, &EfiUtilitiesDialog::beginInstall);
    connect(removeButton, &QPushButton::clicked, this, &EfiUtilitiesDialog::beginRemove);
    connect(closeButton, &QPushButton::clicked, this, &QDialog::reject);
    connect(espCombo, &QComboBox::currentIndexChanged, this, [this](int) { refreshStatus(); });

    auto *form = new QFormLayout;
    form->addRow(tr("EFI System Partition"), espCombo);
    form->addRow(tr("Variant"), variantCombo);
    form->addRow(tr("Status"), statusLabel);
    form->addRow(tr("Installed version"), installedLabel);
    form->addRow(tr("Latest version"), latestLabel);
    form->addRow(tr("Tools menu"), menuLabel);
    form->addRow(QString(), menuCheck);

    auto *buttons = new QHBoxLayout;
    buttons->addWidget(refreshButton);
    buttons->addWidget(installButton);
    buttons->addWidget(updateButton);
    buttons->addWidget(removeButton);
    buttons->addStretch();
    buttons->addWidget(closeButton);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(notice);
    layout->addWidget(secureLabel);
    layout->addLayout(form);
    layout->addLayout(buttons);
    resize(640, 420);

    populateVariants();
    populateEsps();
    refreshRelease();
    refreshStatus();
    updateButtons();
}

void EfiUtilitiesDialog::populateVariants()
{
    variantCombo->clear();
    const QList<EfiUtil::Variant> variants = EfiUtil::netbootVariants(cpuArch);
    if (variants.isEmpty()) {
        variantCombo->addItem(tr("This CPU architecture is not supported"));
        variantCombo->setEnabled(false);
        return;
    }
    for (const EfiUtil::Variant &v : variants)
        variantCombo->addItem(variantLabel(v.id), v.id);
}

QString EfiUtilitiesDialog::variantLabel(const QString &id) const
{
    if (id == QLatin1String("standard"))
        return tr("Standard UEFI");
    if (id == QLatin1String("snp"))
        return tr("SNP");
    if (id == QLatin1String("snponly"))
        return tr("SNP-only");
    return id;
}

QString EfiUtilitiesDialog::secureBootText() const
{
    const QString limitation = tr(
        "The file installed here is netboot.xyz's unsigned UEFI executable. "
        "Upstream's Secure Boot images are netboot.xyz-sb.iso and "
        "netboot.xyz-sb.img, which are disk images, not an EFI tool rEFInd "
        "can launch. This app will not enroll keys, disable Secure Boot, or "
        "sign the file.");
    if (secureBoot == EfiUtil::SecureBoot::Enabled) {
        return tr("Secure Boot is enabled. %1 It will not boot under Secure "
                  "Boot unless the firmware already trusts it.")
            .arg(limitation);
    }
    if (secureBoot == EfiUtil::SecureBoot::Disabled)
        return tr("Secure Boot is off. %1").arg(limitation);
    return tr("Secure Boot state could not be read. %1").arg(limitation);
}

QString EfiUtilitiesDialog::helperRepairText() const
{
    return tr("The privileged install helper was NOT run.\n\n"
              "It is missing, is an older build that does not provide "
              "efi-utility, or the passwordless sudo rule is not installed.\n\n"
              "Reinstall the GUI to restore the matching helper, then try again.");
}

void EfiUtilitiesDialog::populateEsps()
{
    espCombo->blockSignals(true);
    espCombo->clear();
    const QList<EspOps::EspPartition> parts = EspOps::listEspPartitions();
    const QString preferred = EspOps::refindEspGuidFromNvram().toLower();
    int preferredIndex = -1;
    for (const EspOps::EspPartition &part : parts) {
        QString text = part.partUuid;
        if (!part.label.isEmpty())
            text = part.label + QStringLiteral(" (") + part.partUuid + QLatin1Char(')');
        if (part.mountPoint.isEmpty())
            text += tr(" — not mounted");
        else
            text += QStringLiteral(" — ") + part.mountPoint;
        espCombo->addItem(text, part.partUuid);
        if (!preferred.isEmpty() && part.partUuid == preferred)
            preferredIndex = espCombo->count() - 1;
        else if (preferredIndex < 0
                 && (part.mountPoint == QLatin1String("/boot/efi")
                     || part.mountPoint == QLatin1String("/efi")
                     || part.mountPoint == QLatin1String("/boot")))
            preferredIndex = espCombo->count() - 1;
    }
    if (espCombo->count() == 0)
        espCombo->addItem(tr("No EFI System Partition found"), QString());
    if (preferredIndex >= 0)
        espCombo->setCurrentIndex(preferredIndex);
    espCombo->blockSignals(false);
}

QString EfiUtilitiesDialog::selectedGuid() const
{
    return espCombo->currentData().toString();
}

QString EfiUtilitiesDialog::selectedAsset() const
{
    return EfiUtil::assetForVariant(variantCombo->currentData().toString(), cpuArch);
}

bool EfiUtilitiesDialog::showtoolsFlag() const
{
    QSettings settings(settingsPath, QSettings::IniFormat);
    settings.beginGroup(QStringLiteral("EfiUtilities"));
    return settings.value(QStringLiteral("ShowtoolsAdded")).toBool();
}

void EfiUtilitiesDialog::rememberShowtoolsFlag(bool weAdded)
{
    QSettings settings(settingsPath, QSettings::IniFormat);
    settings.beginGroup(QStringLiteral("EfiUtilities"));
    settings.setValue(QStringLiteral("ShowtoolsAdded"), weAdded);
}

void EfiUtilitiesDialog::setBusy(bool busy)
{
    refreshButton->setEnabled(!busy);
    installButton->setEnabled(!busy);
    updateButton->setEnabled(!busy);
    removeButton->setEnabled(!busy);
    if (!busy)
        updateButtons();
}

void EfiUtilitiesDialog::updateButtons()
{
    const bool haveEsp = !selectedGuid().isEmpty();
    const bool haveAsset = !selectedAsset().isEmpty();
    const bool installed = statusPresence == QLatin1String("managed")
        || statusPresence == QLatin1String("unmanaged")
        || statusPresence == QLatin1String("changed");
    installButton->setEnabled(haveEsp && haveAsset && !installed);
    updateButton->setEnabled(haveEsp && haveAsset && installed && releaseReady);
    removeButton->setEnabled(haveEsp && statusPresence == QLatin1String("managed"));
}

void EfiUtilitiesDialog::refreshRelease()
{
    releaseReady = false;
    latestLabel->setText(tr("Checking..."));
    QNetworkReply *reply = network->get(githubRequest(QUrl(QLatin1String(kReleaseUrl))));
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            latestLabel->setText(tr("Could not reach GitHub: %1").arg(reply->errorString()));
            release = {};
            updateButtons();
            return;
        }
        release = EfiUtil::parseGithubRelease(reply->readAll());
        if (!release.ok) {
            latestLabel->setText(release.error);
            updateButtons();
            return;
        }
        const EfiUtil::GithubAsset *sums =
            EfiUtil::findAsset(release, QLatin1String(EfiUtil::kChecksumAsset));
        if (!sums) {
            latestLabel->setText(tr("The release has no checksum file."));
            release.ok = false;
            updateButtons();
            return;
        }
        QNetworkReply *sumReply = network->get(githubRequest(QUrl(sums->url)));
        connect(sumReply, &QNetworkReply::finished, this, [this, sumReply] {
            sumReply->deleteLater();
            if (sumReply->error() != QNetworkReply::NoError) {
                latestLabel->setText(tr("Could not download the checksum file: %1")
                                         .arg(sumReply->errorString()));
                release.ok = false;
                updateButtons();
                return;
            }
            checksums = EfiUtil::parseSha256Sums(QString::fromUtf8(sumReply->readAll()));
            if (!checksums.ok) {
                latestLabel->setText(checksums.error);
                release.ok = false;
                updateButtons();
                return;
            }
            releaseReady = true;
            latestLabel->setText(release.tag);
            updateButtons();
        });
    });
}

void EfiUtilitiesDialog::refreshStatus()
{
    const QString guid = selectedGuid();
    if (guid.isEmpty()) {
        statusPresence = QStringLiteral("absent");
        statusLabel->setText(tr("No EFI System Partition is selected."));
        installedLabel->setText(QStringLiteral("—"));
        menuLabel->setText(QStringLiteral("—"));
        updateButtons();
        return;
    }
    if (!Platform::efiUtilityReady()) {
        QString detail;
        Platform::efiUtilityReady(&detail);
        statusLabel->setText(tr("The privileged helper is missing or is a different version.\n%1\n"
                                "Reinstall rEFInd_GUI, then try again.")
                                 .arg(detail));
        updateButtons();
        return;
    }
    EfiUtil::Request request;
    request.action = QStringLiteral("status");
    request.utility = QLatin1String(EfiUtil::kUtilityId);
    request.espGuid = guid;
    runRequest(request, {});
}

void EfiUtilitiesDialog::applyStatus(const QString &output, int code)
{
    if (code != 0) {
        statusLabel->setText(output.trimmed().isEmpty()
                                 ? tr("Could not read the EFI System Partition (code %1).").arg(code)
                                 : output.trimmed());
        updateButtons();
        return;
    }
    QHash<QString, QString> fields;
    for (const QString &line : output.split(QLatin1Char('\n'))) {
        const int eq = line.indexOf(QLatin1Char('='));
        if (eq > 0)
            fields.insert(line.left(eq).trimmed(), line.mid(eq + 1).trimmed());
    }
    statusPresence = fields.value(QStringLiteral("presence"), QStringLiteral("absent"));
    statusVersion = fields.value(QStringLiteral("version"));
    statusAsset = fields.value(QStringLiteral("asset"));
    statusSha = fields.value(QStringLiteral("sha256"));
    liveShowtools = fields.value(QStringLiteral("live-showtools"), QStringLiteral("unreadable"));
    liveExcluded = fields.value(QStringLiteral("live-excluded"), QStringLiteral("unreadable"));
    statusLabel->setText(presenceText(statusPresence));
    if (statusPresence == QLatin1String("managed") && !statusVersion.isEmpty())
        installedLabel->setText(tr("%1 (%2)").arg(statusVersion, statusAsset));
    else if (statusPresence == QLatin1String("absent"))
        installedLabel->setText(tr("Not installed"));
    else
        installedLabel->setText(tr("Unknown (not managed)"));

    bool stagedPresent = false;
    bool stagedHasNetboot = false;
    QString staged = tr("no staged refind.conf");
    QFile conf(stagedConfPath);
    if (conf.open(QIODevice::ReadOnly)) {
        const QString text = QString::fromUtf8(conf.readAll());
        const QStringList tokens = EfiUtil::liveShowtoolsTokens(text, &stagedPresent);
        stagedHasNetboot = tokens.contains(QLatin1String(EfiUtil::kShowtoolToken));
        if (!stagedPresent)
            staged = tr("staged config has no showtools line");
        else if (stagedHasNetboot)
            staged = tr("staged config includes netboot");
        else
            staged = tr("staged config omits netboot");
        if (EfiUtil::dontScanToolsExcludes(text, QLatin1String(EfiUtil::kInstalledEfiName)))
            staged += tr("; dont_scan_tools hides ipxe.efi (left unchanged)");
    }
    QString live;
    if (liveShowtools == QLatin1String("yes"))
        live = tr("live config includes netboot");
    else if (liveShowtools == QLatin1String("no"))
        live = tr("live config omits netboot");
    else if (liveShowtools == QLatin1String("missing"))
        live = tr("live config has no showtools line");
    else
        live = tr("live config could not be read");
    if (liveExcluded == QLatin1String("yes"))
        live += tr("; dont_scan_tools hides ipxe.efi");

    const bool managed = statusPresence == QLatin1String("managed");
    const bool liveHasNetboot = liveShowtools == QLatin1String("yes");
    QString shown;
    if (managed && (stagedHasNetboot || liveHasNetboot))
        shown = tr("Shown in the tools menu: yes");
    else if (!managed)
        shown = tr("Shown in the tools menu: no");
    else if (liveShowtools == QLatin1String("unreadable") && !stagedHasNetboot)
        shown = tr("Shown in the tools menu: unknown (live config could not be read)");
    else
        shown = tr("Shown in the tools menu: no");
    menuLabel->setText(shown + QStringLiteral("\n") + staged + QStringLiteral("\n") + live);
    updateButtons();
}

void EfiUtilitiesDialog::runRequest(const EfiUtil::Request &request, const QByteArray &payload)
{
    const QString dir = Platform::dataDir() + QStringLiteral("/GUI/efi-utilities");
    if (!QDir().mkpath(dir)) {
        QMessageBox::warning(this, tr("EFI Utilities"),
                             tr("Could not create the staging directory in the data folder."));
        return;
    }
    if (!payload.isEmpty()) {
        QSaveFile file(dir + QStringLiteral("/payload.bin"));
        file.setDirectWriteFallback(false);
        if (!file.open(QIODevice::WriteOnly) || file.write(payload) != payload.size()
            || !file.commit()) {
            QMessageBox::warning(this, tr("EFI Utilities"),
                                 tr("Could not stage the EFI binary."));
            return;
        }
    }
    {
        QSaveFile file(dir + QStringLiteral("/request"));
        file.setDirectWriteFallback(false);
        const QByteArray body = EfiUtil::formatRequest(request).toUtf8();
        if (!file.open(QIODevice::WriteOnly) || file.write(body) != body.size() || !file.commit()) {
            QMessageBox::warning(this, tr("EFI Utilities"),
                                 tr("Could not stage the install request."));
            return;
        }
    }
    const auto needsRepair = [](int code, const QString &output) {
        return code == 64 || output.contains(QLatin1String("password is required"));
    };
    setBusy(true);
    QString output;
    const int code = Platform::runEfiUtility(&output);
    setBusy(false);
    if (request.action == QLatin1String("status")) {
        if (needsRepair(code, output)) {
            statusLabel->setText(helperRepairText());
            updateButtons();
            return;
        }
        applyStatus(output, code);
        return;
    }
    if (needsRepair(code, output)) {
        QMessageBox::warning(this, tr("EFI Utilities"), helperRepairText());
        refreshStatus();
        return;
    }
    QMessageBox box(this);
    box.setTextFormat(Qt::PlainText);
    box.setWindowTitle(tr("EFI Utilities"));
    box.setIcon(code == 0 ? QMessageBox::Information : QMessageBox::Warning);
    box.setText(output.trimmed().isEmpty()
                    ? tr("The helper returned code %1.").arg(code)
                    : output.trimmed());
    box.exec();
    if (code == 0 && request.action == QLatin1String("install") && menuCheck->isChecked())
        applyShowtoolsAfterInstall();
    if (code == 0 && request.action == QLatin1String("remove"))
        applyShowtoolsAfterRemove();
    refreshStatus();
}

void EfiUtilitiesDialog::applyShowtoolsAfterInstall()
{
    if (EfiUtil::showtoolsListIsEmpty(showtoolsText)) {
        const auto answer = QMessageBox::question(
            this, tr("EFI Utilities"),
            tr("The Showtools box is empty. Create Config would then publish a tools "
               "row containing only netboot, and the other tools would be hidden.\n\n"
               "Add netboot as the only tool?"));
        if (answer != QMessageBox::Yes)
            return;
    }
    QFile staged(stagedConfPath);
    QString stagedText;
    if (staged.open(QIODevice::ReadOnly))
        stagedText = QString::fromUtf8(staged.readAll());
    if (EfiUtil::dontScanToolsExcludes(stagedText, QLatin1String(EfiUtil::kInstalledEfiName))
        || liveExcluded == QLatin1String("yes")) {
        QMessageBox::information(
            this, tr("EFI Utilities"),
            tr("dont_scan_tools already hides ipxe.efi. That line was left unchanged, "
               "so the tools menu will not show Netboot until you remove the exclusion."));
    }
    const EfiUtil::ShowtoolsPolicy policy = EfiUtil::enableShowtool(
        showtoolsText, QLatin1String(EfiUtil::kShowtoolToken), showtoolsFlag());
    if (policy.changed) {
        showtoolsText = policy.text;
        emit showtoolsEdited(showtoolsText);
        rememberShowtoolsFlag(policy.weAdded);
    }
    bool wroteFile = false;
    bool hadDirective = false;
    if (!stagedText.isEmpty()) {
        const EfiUtil::ConfPatch patch = EfiUtil::patchShowtools(
            stagedText, QLatin1String(EfiUtil::kShowtoolToken), true);
        hadDirective = patch.hadDirective;
        if (patch.changed) {
            QSaveFile out(stagedConfPath);
            out.setDirectWriteFallback(false);
            const QByteArray bytes = patch.text.toUtf8();
            if (out.open(QIODevice::WriteOnly) && out.write(bytes) == bytes.size() && out.commit())
                wroteFile = true;
        }
    }
    if (wroteFile) {
        if (QMessageBox::question(
                this, tr("EFI Utilities"),
                tr("netboot was added to the staged refind.conf without changing other lines. "
                   "Install Config to publish it?"))
            == QMessageBox::Yes)
            emit installConfigRequested();
        return;
    }
    if (!stagedText.isEmpty() && !hadDirective) {
        QMessageBox::information(
            this, tr("EFI Utilities"),
            tr("The staged refind.conf has no showtools line, so it was left unchanged. "
               "Create Config will write the Showtools box, which now includes netboot. "
               "That regenerates the rest of the file from the current settings."));
        return;
    }
    QMessageBox::information(
        this, tr("EFI Utilities"),
        tr("The Showtools box now includes netboot. Create Config writes that into "
           "refind.conf, and Install Config publishes the file."));
}

void EfiUtilitiesDialog::applyShowtoolsAfterRemove()
{
    const EfiUtil::ShowtoolsPolicy policy = EfiUtil::disableShowtool(
        showtoolsText, QLatin1String(EfiUtil::kShowtoolToken), showtoolsFlag());
    if (!policy.changed)
        return;
    showtoolsText = policy.text;
    emit showtoolsEdited(showtoolsText);
    rememberShowtoolsFlag(false);

    QFile conf(stagedConfPath);
    if (!conf.open(QIODevice::ReadOnly))
        return;
    const EfiUtil::ConfPatch patch = EfiUtil::patchShowtools(
        QString::fromUtf8(conf.readAll()), QLatin1String(EfiUtil::kShowtoolToken), false);
    conf.close();
    if (!patch.changed)
        return;
    QSaveFile out(stagedConfPath);
    out.setDirectWriteFallback(false);
    const QByteArray bytes = patch.text.toUtf8();
    if (!out.open(QIODevice::WriteOnly) || out.write(bytes) != bytes.size() || !out.commit())
        return;
    if (QMessageBox::question(
            this, tr("EFI Utilities"),
            tr("netboot was removed from the staged refind.conf. Other lines were left "
               "in place. Install Config to publish it?"))
        == QMessageBox::Yes)
        emit installConfigRequested();
}

void EfiUtilitiesDialog::beginRemove()
{
    if (statusPresence != QLatin1String("managed")) {
        QMessageBox::information(
            this, tr("EFI Utilities"),
            tr("EFI/tools/ipxe.efi is not an installation managed by rEFInd_GUI, so it will not be deleted."));
        return;
    }
    if (QMessageBox::question(this, tr("EFI Utilities"),
                              tr("Remove the managed ipxe.efi from EFI/tools? "
                                 "Other tools in that directory are left in place."))
        != QMessageBox::Yes)
        return;
    EfiUtil::Request request;
    request.action = QStringLiteral("remove");
    request.utility = QLatin1String(EfiUtil::kUtilityId);
    request.espGuid = selectedGuid();
    runRequest(request, {});
}

void EfiUtilitiesDialog::beginInstall()
{
    if (!release.ok || !releaseReady) {
        QMessageBox::warning(this, tr("EFI Utilities"),
                             tr("The latest netboot.xyz release is not available yet."));
        return;
    }
    const QString asset = selectedAsset();
    const QString sum = EfiUtil::checksumFor(checksums, asset);
    if (asset.isEmpty() || sum.isEmpty()) {
        QMessageBox::warning(this, tr("EFI Utilities"),
                             tr("The selected variant is not in the published checksum file."));
        return;
    }
    if (selectedGuid().isEmpty()) {
        QMessageBox::warning(this, tr("EFI Utilities"),
                             tr("Select an EFI System Partition first."));
        return;
    }
    if (secureBoot == EfiUtil::SecureBoot::Enabled) {
        if (QMessageBox::warning(this, tr("EFI Utilities"),
                                 secureBootText() + QLatin1Char('\n')
                                     + tr("Install it anyway?"),
                                 QMessageBox::Yes | QMessageBox::No)
            != QMessageBox::Yes)
            return;
    }
    const bool sameBytes = !EfiUtil::efiPayloadDownloadNeeded(statusSha, sum);
    if (statusPresence == QLatin1String("managed") && sameBytes && statusAsset == asset
        && statusVersion == release.tag) {
        QMessageBox::information(this, tr("EFI Utilities"),
                                 tr("The installed file already matches %1.").arg(release.tag));
        if (menuCheck->isChecked())
            applyShowtoolsAfterInstall();
        return;
    }
    if (statusPresence == QLatin1String("unmanaged") || statusPresence == QLatin1String("changed")) {
        const QString warning = statusPresence == QLatin1String("unmanaged")
            ? tr("EFI/tools/ipxe.efi is already on this ESP and was not installed by "
                 "rEFInd_GUI. It may be rEFInd's own iPXE tool, which is not netboot.xyz.\n\n"
                 "Replace it?")
            : tr("The installed ipxe.efi no longer matches the ownership record.\n\nReplace it?");
        if (QMessageBox::question(this, tr("EFI Utilities"), warning) != QMessageBox::Yes)
            return;
    }
    if (sameBytes) {
        EfiUtil::Request request;
        request.action = QStringLiteral("install");
        request.utility = QLatin1String(EfiUtil::kUtilityId);
        request.espGuid = selectedGuid();
        request.asset = asset;
        request.version = release.tag;
        request.sha256 = sum;
        request.replace = statusPresence != QLatin1String("absent");
        runRequest(request, {});
        return;
    }
    downloadAsset();
}

void EfiUtilitiesDialog::downloadAsset()
{
    const QString asset = selectedAsset();
    const EfiUtil::GithubAsset *file = EfiUtil::findAsset(release, asset);
    if (!file) {
        QMessageBox::warning(this, tr("EFI Utilities"),
                             tr("The release does not publish %1.").arg(asset));
        return;
    }
    setBusy(true);
    latestLabel->setText(tr("Downloading %1...").arg(asset));
    QNetworkReply *reply = network->get(githubRequest(QUrl(file->url)));
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        setBusy(false);
        latestLabel->setText(release.tag);
        if (reply->error() != QNetworkReply::NoError) {
            QMessageBox::warning(this, tr("EFI Utilities"),
                                 tr("The download failed: %1").arg(reply->errorString()));
            return;
        }
        const QByteArray bytes = reply->readAll();
        const QString sum = EfiUtil::checksumFor(checksums, selectedAsset());
        if (bytes.size() > EfiUtil::kMaxEfiBytes || !EfiUtil::digestsEqual(EfiUtil::sha256Hex(bytes), sum)) {
            QMessageBox::warning(this, tr("EFI Utilities"),
                                 tr("The download does not match the published checksum. "
                                    "Nothing was written to the EFI System Partition."));
            return;
        }
        finishWithPayload(bytes);
    });
}

void EfiUtilitiesDialog::finishWithPayload(const QByteArray &payload)
{
    const QString asset = selectedAsset();
    const QString sum = EfiUtil::checksumFor(checksums, asset);
    EfiUtil::Request request;
    request.action = QStringLiteral("install");
    request.utility = QLatin1String(EfiUtil::kUtilityId);
    request.espGuid = selectedGuid();
    request.asset = asset;
    request.version = release.tag;
    request.sha256 = sum;
    request.replace = statusPresence != QLatin1String("absent");
    runRequest(request, payload);
}
