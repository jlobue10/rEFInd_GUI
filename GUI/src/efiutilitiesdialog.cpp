#include "efiutilitiesdialog.h"

#include "efiutil/install.h"
#include "efiutil/showtools.h"
#include "efiutil/trust.h"
#include "efiutil/zipmember.h"
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

    notice = new QLabel;
    notice->setWordWrap(true);

    QFile efivar(QStringLiteral(
        "/sys/firmware/efi/efivars/SecureBoot-8be4df61-93ca-11d2-aa0d-00e098032b8c"));
    if (efivar.open(QIODevice::ReadOnly))
        secureBoot = EfiUtil::secureBootFromEfivar(efivar.readAll());
    secureLabel = new QLabel(secureBootText());
    secureLabel->setWordWrap(true);

    utilityCombo = new QComboBox;
    espCombo = new QComboBox;
    variantCombo = new QComboBox;
    statusLabel = new QLabel(tr("Checking the EFI System Partition..."));
    statusLabel->setWordWrap(true);
    installedLabel = new QLabel(QStringLiteral("—"));
    latestLabel = new QLabel(tr("Checking..."));
    menuLabel = new QLabel(QStringLiteral("—"));
    menuLabel->setWordWrap(true);
    menuCheck = new QCheckBox;
    menuCheck->setChecked(true);
    builtInNote = new QLabel(tr(
        "Shutdown, reboot, about, exit, firmware setup, hidden tags, install rEFInd, "
        "boot order, Apple recovery, and SIP rotate are built into rEFInd. Turn them "
        "on in the Showtools field. rEFInd does not launch a separate power-off "
        "EFI file. gptsync ships inside the rEFInd package and is not downloaded here."));
    builtInNote->setWordWrap(true);

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
    // QOverload: Qt 5 also declares currentIndexChanged(const QString &), so
    // the bare member pointer is ambiguous there.
    connect(espCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int) { refreshStatus(); });
    connect(utilityCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        applyUtilityChrome();
        populateVariants();
        refreshRelease();
        refreshStatus();
    });
    connect(variantCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        if (currentUtility() != QLatin1String(EfiUtil::kUtilityId) || !checksums.ok)
            return;
        pendingAsset = selectedAsset();
        pendingSha = EfiUtil::checksumFor(checksums, pendingAsset);
        updateButtons();
    });

    auto *form = new QFormLayout;
    form->addRow(tr("Utility"), utilityCombo);
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
    layout->addWidget(builtInNote);
    layout->addLayout(buttons);
    resize(680, 560);

    populateUtilities();
    populateVariants();
    populateEsps();
    applyUtilityChrome();
    refreshRelease();
    refreshStatus();
    updateButtons();
}

void EfiUtilitiesDialog::populateUtilities()
{
    utilityCombo->blockSignals(true);
    utilityCombo->clear();
    const auto add = [this](const char *id, const QString &label) {
        utilityCombo->addItem(label, QLatin1String(id));
    };
    add(EfiUtil::kUtilityId, tr("netboot.xyz"));
    add(EfiUtil::kShellId, tr("UEFI shell"));
    add(EfiUtil::kMemtestId, tr("Memtest86+"));
    add(EfiUtil::kHashToolId, tr("HashTool"));
    add(EfiUtil::kMokManagerId, tr("MokManager"));
    add(EfiUtil::kKeyToolId, tr("KeyTool"));
    add(EfiUtil::kGdiskId, tr("gdisk"));
    add(EfiUtil::kFwupdId, tr("fwupd"));
    utilityCombo->blockSignals(false);
}

QString EfiUtilitiesDialog::currentUtility() const
{
    return utilityCombo->currentData().toString();
}

QString EfiUtilitiesDialog::currentInstalledName() const
{
    const QString id = currentUtility();
    if (!pendingAsset.isEmpty()) {
        const EfiUtil::ManagedFile file = EfiUtil::managedFileFor(id, pendingAsset);
        if (file.ok)
            return file.fileName;
    }
    const QString asset = selectedAsset();
    if (!asset.isEmpty()) {
        const EfiUtil::ManagedFile file = EfiUtil::managedFileFor(id, asset);
        if (file.ok)
            return file.fileName;
    }
    return EfiUtil::installedNameForArch(id, cpuArch);
}

void EfiUtilitiesDialog::applyUtilityChrome()
{
    const QString id = currentUtility();
    const QString token = EfiUtil::showtoolTokenFor(id);
    const QString installed = EfiUtil::installedNameForArch(id, cpuArch);
    if (id == QLatin1String(EfiUtil::kUtilityId)) {
        notice->setText(tr(
            "netboot.xyz is installed as EFI/tools/ipxe.efi so rEFInd's existing "
            "Netboot tool can launch it. rEFInd will label the icon \"Netboot\". "
            "This is not the legacy ipxe_discover.efi scanner, and scanfor is not changed. "
            "The privileged helper downloads the publisher's checksum itself. A checksum "
            "published next to the file only shows the download was not corrupted."));
    } else if (id == QLatin1String(EfiUtil::kShellId)) {
        notice->setText(tr(
            "The UEFI shell is the EDK2 Shell v2 binary published by the UEFI-Shell "
            "project from EDK2 stable. The privileged helper reads GitHub's SHA-256 "
            "digest itself; a digest stored beside the download only shows the file "
            "was not corrupted. It is installed as EFI/tools/%1, which is a name "
            "rEFInd's shell tool launches. It is not copied to the root of the EFI "
            "System Partition.")
                            .arg(installed.isEmpty() ? tr("shell.efi") : installed));
    } else if (id == QLatin1String(EfiUtil::kMemtestId)) {
        notice->setText(tr(
            "Memtest86+ is taken from the binaries zip on memtest.org. The privileged "
            "helper downloads that site's sha256sum.txt and the zip itself; a checksum "
            "next to the download only shows the file was not corrupted. The file is "
            "installed as EFI/tools/%1 so rEFInd's memtest tool finds it. Upstream's "
            "own filename is not in rEFInd's search list. This is Memtest86+, not "
            "PassMark Memtest86.")
                            .arg(installed.isEmpty() ? tr("(unsupported architecture)") : installed));
    } else if (id == QLatin1String(EfiUtil::kHashToolId)) {
        notice->setText(tr(
            "HashTool is the Linux Foundation HashTool.efi from James Bottomley's "
            "February 2013 kit, which Microsoft signed. It is installed as "
            "EFI/tools/HashTool.efi. rEFInd labels it \"MOK utility\" when showtools "
            "includes mok_tool. This app does not enroll keys or hashes, and it does "
            "not check whether firmware has since revoked that signature."));
    } else if (id == QLatin1String(EfiUtil::kMokManagerId)) {
        notice->setText(tr(
            "MokManager is copied from the installed shim package (mmx64.efi, or the "
            "matching architecture). rEFInd labels it \"MOK utility\". Shim publishes "
            "source tarballs, not a standalone EFI file, so there is no separate download. "
            "This app does not enroll keys."));
    } else if (id == QLatin1String(EfiUtil::kKeyToolId)) {
        notice->setText(tr(
            "KeyTool is copied from the installed efitools package. The usual file is "
            "unsigned: Microsoft declined to sign it. It is installed under its own name "
            "in EFI/tools and is not signed here. rEFInd labels it \"MOK utility\"."));
    } else if (id == QLatin1String(EfiUtil::kGdiskId)) {
        notice->setText(tr(
            "gdisk is the EFI build from GPT fdisk 1.0.4, still the last version Rod "
            "Smith published as an EFI binary. It is installed as EFI/tools/gdisk_x64.efi "
            "and is signed with his rEFInd key, not a key firmware trusts by default. "
            "The zip also contains a certificate. This app does not enroll it. Later "
            "GPT fdisk versions do not ship an EFI binary."));
    } else if (id == QLatin1String(EfiUtil::kFwupdId)) {
        notice->setText(tr(
            "fwupd is copied from the installed fwupd package and renamed to %1, which "
            "is the filename rEFInd's fwupdate tool launches. Copying the launcher does "
            "not stage a firmware capsule; the fwupd service on the OS does that.")
                            .arg(installed.isEmpty() ? tr("fwupx64.efi") : installed));
    } else {
        notice->setText(QString());
    }
    menuCheck->setText(tr("Show %1 in the rEFInd tools menu").arg(token));
    if (secureLabel)
        secureLabel->setText(secureBootText());
}

void EfiUtilitiesDialog::populateVariants()
{
    variantCombo->clear();
    if (currentUtility() != QLatin1String(EfiUtil::kUtilityId)) {
        const QString installed = EfiUtil::installedNameForArch(currentUtility(), cpuArch);
        if (installed.isEmpty())
            variantCombo->addItem(tr("This CPU architecture is not supported"));
        else if (currentUtility() == QLatin1String(EfiUtil::kFwupdId)
                 || currentUtility() == QLatin1String(EfiUtil::kMokManagerId)
                 || currentUtility() == QLatin1String(EfiUtil::kKeyToolId))
            variantCombo->addItem(tr("Distro package (%1)").arg(installed));
        else
            variantCombo->addItem(installed);
        variantCombo->setEnabled(false);
        return;
    }
    const QList<EfiUtil::Variant> variants = EfiUtil::netbootVariants(cpuArch);
    if (variants.isEmpty()) {
        variantCombo->addItem(tr("This CPU architecture is not supported"));
        variantCombo->setEnabled(false);
        return;
    }
    variantCombo->setEnabled(true);
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
    const QString id = utilityCombo ? utilityCombo->currentData().toString() : QString();
    QString limitation;
    if (id == QLatin1String(EfiUtil::kShellId)) {
        limitation = tr(
            "This EDK2 shell is not signed with a key firmware trusts by default. "
            "This app will not enroll keys, disable Secure Boot, or sign the file.");
    } else if (id == QLatin1String(EfiUtil::kMemtestId)) {
        limitation = tr(
            "This Memtest86+ binary is not a signed Secure Boot chain. "
            "This app will not enroll keys, disable Secure Boot, or sign the file.");
    } else if (id == QLatin1String(EfiUtil::kHashToolId)) {
        limitation = tr(
            "This is the 2013 Linux Foundation HashTool. This app will not enroll "
            "keys or hashes, disable Secure Boot, or sign anything, and it does not "
            "check the firmware revocation list.");
    } else if (id == QLatin1String(EfiUtil::kMokManagerId)) {
        limitation = tr(
            "The shim package's MokManager may already be signed. This app copies "
            "those bytes unchanged and will not enroll keys or disable Secure Boot.");
    } else if (id == QLatin1String(EfiUtil::kKeyToolId)) {
        limitation = tr(
            "KeyTool.efi is unsigned. This app will not sign it, enroll its hash, "
            "or disable Secure Boot. A file already named KeyTool-signed.efi is "
            "copied under that name and is not re-signed.");
    } else if (id == QLatin1String(EfiUtil::kGdiskId)) {
        limitation = tr(
            "The GPT fdisk 1.0.4 binary is signed with Rod Smith's rEFInd key. "
            "This app does not enroll that certificate, disable Secure Boot, or "
            "sign the binary.");
    } else if (id == QLatin1String(EfiUtil::kFwupdId)) {
        limitation = tr(
            "The fwupd EFI binary is copied unchanged from the distro package. "
            "This app will not sign it, enroll keys, or disable Secure Boot.");
    } else {
        limitation = tr(
            "The file installed here is netboot.xyz's unsigned UEFI executable. "
            "Upstream's Secure Boot images are netboot.xyz-sb.iso and "
            "netboot.xyz-sb.img, which are disk images, not an EFI tool rEFInd "
            "can launch. This app will not enroll keys, disable Secure Boot, or "
            "sign the file.");
    }
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
    if (currentUtility() == QLatin1String(EfiUtil::kUtilityId))
        return EfiUtil::assetForVariant(variantCombo->currentData().toString(), cpuArch);
    if (currentUtility() == QLatin1String(EfiUtil::kMemtestId))
        return pendingAsset;
    return EfiUtil::defaultAssetFor(currentUtility(), cpuArch);
}

QStringList EfiUtilitiesDialog::showtoolsAddedBy() const
{
    QSettings settings(settingsPath, QSettings::IniFormat);
    settings.beginGroup(QStringLiteral("EfiUtilities"));
    QStringList list = settings.value(QStringLiteral("ShowtoolsAddedBy")).toString().split(
        QLatin1Char(','), Qt::SkipEmptyParts);
    if (settings.value(QStringLiteral("ShowtoolsAdded")).toBool()
        && !list.contains(QLatin1String(EfiUtil::kUtilityId)))
        list << QLatin1String(EfiUtil::kUtilityId);
    return list;
}

bool EfiUtilitiesDialog::showtoolsFlag() const
{
    return weAddedToken(EfiUtil::showtoolTokenFor(currentUtility()));
}

bool EfiUtilitiesDialog::weAddedToken(const QString &token) const
{
    for (const QString &id : showtoolsAddedBy()) {
        if (EfiUtil::showtoolTokenFor(id) == token)
            return true;
    }
    return false;
}

void EfiUtilitiesDialog::rememberUtility(const QString &utilityId, bool added)
{
    QStringList list = showtoolsAddedBy();
    list.removeAll(utilityId);
    if (added)
        list << utilityId;
    QSettings settings(settingsPath, QSettings::IniFormat);
    settings.beginGroup(QStringLiteral("EfiUtilities"));
    settings.setValue(QStringLiteral("ShowtoolsAddedBy"), list.join(QLatin1Char(',')));
    settings.setValue(QStringLiteral("ShowtoolsAdded"),
                      list.contains(QLatin1String(EfiUtil::kUtilityId)));
}

void EfiUtilitiesDialog::rememberShowtoolsFlag(bool weAdded)
{
    rememberUtility(currentUtility(), weAdded);
}

void EfiUtilitiesDialog::setBusy(bool busy)
{
    refreshButton->setEnabled(!busy);
    installButton->setEnabled(!busy);
    updateButton->setEnabled(!busy);
    removeButton->setEnabled(!busy);
    utilityCombo->setEnabled(!busy);
    espCombo->setEnabled(!busy);
    variantCombo->setEnabled(!busy && currentUtility() == QLatin1String(EfiUtil::kUtilityId));
    if (!busy)
        updateButtons();
}

void EfiUtilitiesDialog::updateButtons()
{
    const bool haveEsp = !selectedGuid().isEmpty();
    const bool supported = EfiUtil::utilitySupportsArch(currentUtility(), cpuArch);
    const bool haveAsset = currentUtility() == QLatin1String(EfiUtil::kMemtestId)
        ? supported
        : !selectedAsset().isEmpty();
    const bool installed = statusPresence == QLatin1String("managed")
        || statusPresence == QLatin1String("unmanaged")
        || statusPresence == QLatin1String("changed");
    installButton->setEnabled(haveEsp && haveAsset && supported && releaseReady && !installed);
    updateButton->setEnabled(haveEsp && haveAsset && supported && installed && releaseReady);
    removeButton->setEnabled(haveEsp && statusPresence == QLatin1String("managed"));
}

void EfiUtilitiesDialog::noteRelease(int generation, const QString &version, const QString &asset,
                                     const QString &sha256, const QString &url)
{
    if (generation != releaseGeneration)
        return;
    pendingVersion = version;
    pendingAsset = asset;
    pendingSha = sha256;
    pendingUrl = url;
    releaseReady = true;
    latestLabel->setText(version);
    updateButtons();
}

void EfiUtilitiesDialog::failRelease(int generation, const QString &text)
{
    if (generation != releaseGeneration)
        return;
    releaseReady = false;
    pendingVersion.clear();
    pendingAsset.clear();
    pendingSha.clear();
    pendingUrl.clear();
    release = {};
    latestLabel->setText(text);
    updateButtons();
}

void EfiUtilitiesDialog::refreshRelease()
{
    ++releaseGeneration;
    releaseReady = false;
    pendingVersion.clear();
    pendingAsset.clear();
    pendingSha.clear();
    pendingUrl.clear();
    checksums = {};
    release = {};
    latestLabel->setText(tr("Checking..."));
    const QString id = currentUtility();
    if (!EfiUtil::utilitySupportsArch(id, cpuArch)) {
        failRelease(releaseGeneration, tr("This CPU architecture is not supported"));
        return;
    }
    if (id == QLatin1String(EfiUtil::kUtilityId))
        refreshNetbootRelease();
    else if (id == QLatin1String(EfiUtil::kShellId))
        refreshShellRelease();
    else if (id == QLatin1String(EfiUtil::kMemtestId))
        refreshMemtestRelease();
    else if (id == QLatin1String(EfiUtil::kGdiskId) || id == QLatin1String(EfiUtil::kHashToolId))
        refreshPinned();
    else
        refreshLocal();
}

void EfiUtilitiesDialog::refreshNetbootRelease()
{
    const int generation = releaseGeneration;
    QNetworkReply *reply = network->get(githubRequest(
        QUrl(EfiUtil::netbootReleaseApiUrlFor(QStringLiteral("latest")))));
    connect(reply, &QNetworkReply::finished, this, [this, reply, generation] {
        reply->deleteLater();
        if (generation != releaseGeneration)
            return;
        if (reply->error() != QNetworkReply::NoError) {
            failRelease(generation, tr("Could not reach GitHub: %1").arg(reply->errorString()));
            return;
        }
        release = EfiUtil::parseGithubRelease(reply->readAll());
        if (!release.ok) {
            failRelease(generation, release.error);
            return;
        }
        const EfiUtil::GithubAsset *sums =
            EfiUtil::findAsset(release, QLatin1String(EfiUtil::kChecksumAsset));
        if (!sums) {
            failRelease(generation, tr("The release has no checksum file."));
            return;
        }
        QNetworkReply *sumReply = network->get(githubRequest(QUrl(sums->url)));
        connect(sumReply, &QNetworkReply::finished, this, [this, sumReply, generation] {
            sumReply->deleteLater();
            if (generation != releaseGeneration)
                return;
            if (sumReply->error() != QNetworkReply::NoError) {
                failRelease(generation, tr("Could not download the checksum file: %1")
                                            .arg(sumReply->errorString()));
                return;
            }
            checksums = EfiUtil::parseSha256Sums(QString::fromUtf8(sumReply->readAll()));
            if (!checksums.ok) {
                failRelease(generation, checksums.error);
                return;
            }
            const QString asset = selectedAsset();
            const QString sum = EfiUtil::checksumFor(checksums, asset);
            if (sum.isEmpty()) {
                failRelease(generation, tr("The selected variant is not in the published checksum file."));
                return;
            }
            noteRelease(generation, release.tag, asset, sum, QString());
        });
    });
}

void EfiUtilitiesDialog::refreshShellRelease()
{
    const int generation = releaseGeneration;
    const QString asset = EfiUtil::defaultAssetFor(currentUtility(), cpuArch);
    QNetworkReply *reply = network->get(githubRequest(QUrl(EfiUtil::shellReleaseApiUrl())));
    connect(reply, &QNetworkReply::finished, this, [this, reply, generation, asset] {
        reply->deleteLater();
        if (generation != releaseGeneration)
            return;
        if (reply->error() != QNetworkReply::NoError) {
            failRelease(generation, tr("Could not reach GitHub: %1").arg(reply->errorString()));
            return;
        }
        release = EfiUtil::parseGithubAssetRelease(reply->readAll());
        if (!release.ok) {
            failRelease(generation, release.error);
            return;
        }
        const EfiUtil::GithubAsset *file = EfiUtil::findAsset(release, asset);
        if (!file || file->sha256.isEmpty()) {
            failRelease(generation, tr("The shell release has no SHA-256 digest for %1.").arg(asset));
            return;
        }
        noteRelease(generation, release.tag, asset, file->sha256, file->url);
    });
}

void EfiUtilitiesDialog::refreshMemtestRelease()
{
    const int generation = releaseGeneration;
    QNetworkReply *reply = network->get(githubRequest(QUrl(EfiUtil::memtestReleaseApiUrl())));
    connect(reply, &QNetworkReply::finished, this, [this, reply, generation] {
        reply->deleteLater();
        if (generation != releaseGeneration)
            return;
        if (reply->error() != QNetworkReply::NoError) {
            failRelease(generation, tr("Could not reach GitHub: %1").arg(reply->errorString()));
            return;
        }
        release = EfiUtil::parseGithubTag(reply->readAll());
        if (!release.ok) {
            failRelease(generation, release.error);
            return;
        }
        const EfiUtil::MemtestLocation loc = EfiUtil::memtestLocation(release.tag, cpuArch);
        if (!loc.ok) {
            failRelease(generation, loc.error);
            return;
        }
        QNetworkReply *sumReply = network->get(githubRequest(QUrl(loc.checksumUrl)));
        connect(sumReply, &QNetworkReply::finished, this, [this, sumReply, generation, loc] {
            sumReply->deleteLater();
            if (generation != releaseGeneration)
                return;
            if (sumReply->error() != QNetworkReply::NoError) {
                failRelease(generation, tr("Could not download the checksum file: %1")
                                            .arg(sumReply->errorString()));
                return;
            }
            checksums = EfiUtil::parseSha256Sums(QString::fromUtf8(sumReply->readAll()));
            if (!checksums.ok) {
                failRelease(generation, checksums.error);
                return;
            }
            const QString zipSha = EfiUtil::checksumByBasename(checksums, loc.zipName);
            if (zipSha.isEmpty()) {
                failRelease(generation, tr("The checksum file has no entry for %1.").arg(loc.zipName));
                return;
            }
            noteRelease(generation, loc.tag, QString(), zipSha, loc.zipUrl);
        });
    });
}

void EfiUtilitiesDialog::refreshPinned()
{
    const int generation = releaseGeneration;
    if (currentUtility() == QLatin1String(EfiUtil::kGdiskId)) {
        noteRelease(generation, EfiUtil::gdiskVersion(), EfiUtil::defaultAssetFor(currentUtility(), cpuArch),
                    EfiUtil::gdiskMemberSha256(), EfiUtil::gdiskZipUrl());
        return;
    }
    noteRelease(generation, EfiUtil::hashToolVersion(), QStringLiteral("HashTool.efi"),
                EfiUtil::hashToolSha256(), EfiUtil::hashToolUrl());
}

void EfiUtilitiesDialog::refreshLocal()
{
    const int generation = releaseGeneration;
    const EfiUtil::LocalToolFile local = EfiUtil::readLocalTool(currentUtility(), cpuArch);
    if (!local.ok) {
        failRelease(generation, local.error);
        return;
    }
    noteRelease(generation, QStringLiteral("local"), local.asset, local.sha256, local.path);
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
        statusLabel->setText(tr("The privileged helper is missing, is a different version, "
                                "or does not provide this operation.\n%1\n"
                                "Reinstall rEFInd_GUI, then try again.")
                                 .arg(detail));
        updateButtons();
        return;
    }
    ++statusGeneration;
    EfiUtil::Request request;
    request.action = QStringLiteral("status");
    request.utility = currentUtility();
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

    const QString token = EfiUtil::showtoolTokenFor(currentUtility());
    const QString installedName = currentInstalledName();
    bool stagedPresent = false;
    bool stagedHasToken = false;
    QString staged = tr("no staged refind.conf");
    QFile conf(stagedConfPath);
    if (conf.open(QIODevice::ReadOnly)) {
        const QString text = QString::fromUtf8(conf.readAll());
        const QStringList tokens = EfiUtil::liveShowtoolsTokens(text, &stagedPresent);
        stagedHasToken = tokens.contains(token);
        if (!stagedPresent)
            staged = tr("staged config has no showtools line");
        else if (stagedHasToken)
            staged = tr("staged config includes %1").arg(token);
        else
            staged = tr("staged config omits %1").arg(token);
        if (!installedName.isEmpty() && EfiUtil::dontScanToolsExcludes(text, installedName))
            staged += tr("; dont_scan_tools hides %1 (left unchanged)").arg(installedName);
    }
    QString live;
    if (liveShowtools == QLatin1String("yes"))
        live = tr("live config includes %1").arg(token);
    else if (liveShowtools == QLatin1String("no"))
        live = tr("live config omits %1").arg(token);
    else if (liveShowtools == QLatin1String("missing"))
        live = tr("live config has no showtools line");
    else
        live = tr("live config could not be read");
    if (liveExcluded == QLatin1String("yes"))
        live += tr("; dont_scan_tools hides %1").arg(installedName);

    const bool managed = statusPresence == QLatin1String("managed");
    const bool liveHasToken = liveShowtools == QLatin1String("yes");
    QString shown;
    if (managed && (stagedHasToken || liveHasToken))
        shown = tr("Shown in the tools menu: yes");
    else if (!managed)
        shown = tr("Shown in the tools menu: no");
    else if (liveShowtools == QLatin1String("unreadable") && !stagedHasToken)
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
    const int generation = statusGeneration;
    setBusy(true);
    QString output;
    const int code = Platform::runEfiUtility(&output);
    setBusy(false);
    if (request.action == QLatin1String("status")) {
        if (generation != statusGeneration)
            return;
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
    const QString token = EfiUtil::showtoolTokenFor(currentUtility());
    const QString installedName = currentInstalledName();
    if (EfiUtil::showtoolsListIsEmpty(showtoolsText)) {
        const auto answer = QMessageBox::question(
            this, tr("EFI Utilities"),
            tr("The Showtools box is empty. Create Config would then publish a tools "
               "row containing only %1, and the other tools would be hidden.\n\n"
               "Add %1 as the only tool?")
                .arg(token));
        if (answer != QMessageBox::Yes)
            return;
    }
    QFile staged(stagedConfPath);
    QString stagedText;
    if (staged.open(QIODevice::ReadOnly))
        stagedText = QString::fromUtf8(staged.readAll());
    if (!installedName.isEmpty()
        && (EfiUtil::dontScanToolsExcludes(stagedText, installedName)
            || liveExcluded == QLatin1String("yes"))) {
        QMessageBox::information(
            this, tr("EFI Utilities"),
            tr("dont_scan_tools already hides %1. That line was left unchanged, "
               "so the tools menu will not show it until you remove the exclusion.")
                .arg(installedName));
    }
    const EfiUtil::ShowtoolsPolicy policy = EfiUtil::enableShowtool(
        showtoolsText, token, showtoolsFlag());
    if (policy.changed) {
        showtoolsText = policy.text;
        emit showtoolsEdited(showtoolsText);
        rememberShowtoolsFlag(policy.weAdded);
    }
    bool wroteFile = false;
    bool hadDirective = false;
    if (!stagedText.isEmpty()) {
        const EfiUtil::ConfPatch patch = EfiUtil::patchShowtools(stagedText, token, true);
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
                tr("%1 was added to the staged refind.conf without changing other lines. "
                   "Install Config to publish it?")
                    .arg(token))
            == QMessageBox::Yes)
            emit installConfigRequested();
        return;
    }
    if (!stagedText.isEmpty() && !hadDirective) {
        QMessageBox::information(
            this, tr("EFI Utilities"),
            tr("The staged refind.conf has no showtools line, so it was left unchanged. "
               "Create Config will write the Showtools box, which now includes %1. "
               "That regenerates the rest of the file from the current settings.")
                .arg(token));
        return;
    }
    QMessageBox::information(
        this, tr("EFI Utilities"),
        tr("The Showtools box now includes %1. Create Config writes that into "
           "refind.conf, and Install Config publishes the file.")
            .arg(token));
}

void EfiUtilitiesDialog::applyShowtoolsAfterRemove()
{
    const QString id = currentUtility();
    const QString token = EfiUtil::showtoolTokenFor(id);
    int others = 0;
    for (const QString &owner : showtoolsAddedBy()) {
        if (owner != id && EfiUtil::showtoolTokenFor(owner) == token)
            ++others;
    }
    const bool weAdded = showtoolsAddedBy().contains(id);
    if (others > 0) {
        rememberUtility(id, false);
        return;
    }
    const EfiUtil::ShowtoolsPolicy policy = EfiUtil::disableShowtool(
        showtoolsText, token, weAdded);
    rememberUtility(id, false);
    if (!policy.changed)
        return;
    showtoolsText = policy.text;
    emit showtoolsEdited(showtoolsText);

    QFile conf(stagedConfPath);
    if (!conf.open(QIODevice::ReadOnly))
        return;
    const EfiUtil::ConfPatch patch = EfiUtil::patchShowtools(
        QString::fromUtf8(conf.readAll()), token, false);
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
            tr("%1 was removed from the staged refind.conf. Other lines were left "
               "in place. Install Config to publish it?")
                .arg(token))
        == QMessageBox::Yes)
        emit installConfigRequested();
}

void EfiUtilitiesDialog::beginRemove()
{
    const QString installed = currentInstalledName();
    const QString relative = installed.isEmpty()
        ? currentUtility()
        : QStringLiteral("EFI/tools/%1").arg(installed);
    if (statusPresence != QLatin1String("managed")) {
        QMessageBox::information(
            this, tr("EFI Utilities"),
            tr("%1 is not an installation managed by rEFInd_GUI, so it will not be deleted.")
                .arg(relative));
        return;
    }
    if (QMessageBox::question(this, tr("EFI Utilities"),
                              tr("Remove the managed %1? Other tools in EFI/tools are left in place.")
                                  .arg(relative))
        != QMessageBox::Yes)
        return;
    EfiUtil::Request request;
    request.action = QStringLiteral("remove");
    request.utility = currentUtility();
    request.espGuid = selectedGuid();
    runRequest(request, {});
}

void EfiUtilitiesDialog::beginInstall()
{
    if (!releaseReady) {
        QMessageBox::warning(this, tr("EFI Utilities"),
                             tr("The selected utility is not available yet."));
        return;
    }
    const QString id = currentUtility();
    const bool zipPayload = id == QLatin1String(EfiUtil::kMemtestId)
        || id == QLatin1String(EfiUtil::kGdiskId);
    const QString asset = zipPayload ? QString() : selectedAsset();
    const QString sum = pendingSha;
    if (!zipPayload && (asset.isEmpty() || sum.isEmpty())) {
        QMessageBox::warning(this, tr("EFI Utilities"),
                             tr("The selected file has no published checksum."));
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
    const bool sameBytes = !zipPayload && !EfiUtil::efiPayloadDownloadNeeded(statusSha, sum);
    if (statusPresence == QLatin1String("managed") && sameBytes && statusAsset == asset
        && statusVersion == pendingVersion) {
        QMessageBox::information(this, tr("EFI Utilities"),
                                 tr("The installed file already matches %1.").arg(pendingVersion));
        if (menuCheck->isChecked())
            applyShowtoolsAfterInstall();
        return;
    }
    if (statusPresence == QLatin1String("unmanaged") || statusPresence == QLatin1String("changed")) {
        const QString warning = statusPresence == QLatin1String("unmanaged")
            ? (id == QLatin1String(EfiUtil::kUtilityId)
                   ? tr("EFI/tools/ipxe.efi is already on this ESP and was not installed by "
                        "rEFInd_GUI. It may be rEFInd's own iPXE tool, which is not netboot.xyz.\n\n"
                        "Replace it?")
                   : tr("%1 is already on this ESP and was not installed by rEFInd_GUI.\n\n"
                        "Replace it?")
                         .arg(QStringLiteral("EFI/tools/%1").arg(currentInstalledName())))
            : tr("The installed %1 no longer matches the ownership record.\n\nReplace it?")
                  .arg(currentInstalledName());
        if (QMessageBox::question(this, tr("EFI Utilities"), warning) != QMessageBox::Yes)
            return;
    }
    if (id == QLatin1String(EfiUtil::kFwupdId) || id == QLatin1String(EfiUtil::kMokManagerId)
        || id == QLatin1String(EfiUtil::kKeyToolId)) {
        const EfiUtil::LocalToolFile local = EfiUtil::readLocalTool(id, cpuArch);
        if (!local.ok) {
            QMessageBox::warning(this, tr("EFI Utilities"), local.error);
            return;
        }
        finishWithPayload(local.bytes, local.asset, pendingVersion, local.sha256);
        return;
    }
    if (sameBytes) {
        EfiUtil::Request request;
        request.action = QStringLiteral("install");
        request.utility = id;
        request.espGuid = selectedGuid();
        request.asset = asset;
        request.version = pendingVersion;
        request.sha256 = sum;
        request.replace = statusPresence != QLatin1String("absent");
        runRequest(request, {});
        return;
    }
    downloadAsset();
}

void EfiUtilitiesDialog::downloadAsset()
{
    const QString id = currentUtility();
    QString url = pendingUrl;
    QString label = pendingAsset.isEmpty() ? id : pendingAsset;
    if (id == QLatin1String(EfiUtil::kUtilityId)) {
        label = selectedAsset();
        const EfiUtil::GithubAsset *file = EfiUtil::findAsset(release, label);
        if (!file) {
            QMessageBox::warning(this, tr("EFI Utilities"),
                                 tr("The release does not publish %1.").arg(label));
            return;
        }
        url = file->url;
    }
    if (url.isEmpty()) {
        QMessageBox::warning(this, tr("EFI Utilities"),
                             tr("There is no download URL for this utility."));
        return;
    }
    const int generation = releaseGeneration;
    const QString version = pendingVersion;
    const QString expectSha = pendingSha;
    setBusy(true);
    latestLabel->setText(tr("Downloading %1...").arg(label));
    QNetworkRequest request = githubRequest(QUrl(url));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply *reply = network->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, generation, id, version, expectSha, label] {
        reply->deleteLater();
        setBusy(false);
        if (generation != releaseGeneration)
            return;
        latestLabel->setText(version);
        if (reply->error() != QNetworkReply::NoError) {
            QMessageBox::warning(this, tr("EFI Utilities"),
                                 tr("The download failed: %1").arg(reply->errorString()));
            return;
        }
        const QByteArray bytes = reply->readAll();
        const auto mismatch = [this] {
            QMessageBox::warning(this, tr("EFI Utilities"),
                                 tr("The download does not match the published checksum. "
                                    "Nothing was written to the EFI System Partition."));
        };
        if (id == QLatin1String(EfiUtil::kMemtestId) || id == QLatin1String(EfiUtil::kGdiskId)) {
            const QString zipSha = id == QLatin1String(EfiUtil::kGdiskId)
                ? EfiUtil::gdiskZipSha256()
                : expectSha;
            if (bytes.size() > EfiUtil::kMaxZipBytes
                || !EfiUtil::digestsEqual(EfiUtil::sha256Hex(bytes), zipSha)) {
                mismatch();
                return;
            }
            QRegularExpression pattern;
            if (id == QLatin1String(EfiUtil::kGdiskId))
                pattern = QRegularExpression(QRegularExpression::anchoredPattern(
                    QRegularExpression::escape(EfiUtil::gdiskMemberName())));
            else
                pattern = EfiUtil::memtestLocation(version, cpuArch).memberPattern;
            const EfiUtil::ZipMember member = EfiUtil::extractZipMember(
                bytes, pattern, EfiUtil::kMaxEfiBytes);
            if (!member.ok) {
                QMessageBox::warning(this, tr("EFI Utilities"), member.error);
                return;
            }
            if (id == QLatin1String(EfiUtil::kGdiskId)
                && !EfiUtil::digestsEqual(EfiUtil::sha256Hex(member.bytes),
                                          EfiUtil::gdiskMemberSha256())) {
                mismatch();
                return;
            }
            const QString asset = id == QLatin1String(EfiUtil::kGdiskId)
                ? QStringLiteral("gdisk_x64.efi")
                : member.name;
            finishWithPayload(member.bytes, asset, version, EfiUtil::sha256Hex(member.bytes));
            return;
        }
        if (bytes.size() > EfiUtil::kMaxEfiBytes
            || !EfiUtil::digestsEqual(EfiUtil::sha256Hex(bytes), expectSha)) {
            mismatch();
            return;
        }
        finishWithPayload(bytes, label, version, expectSha);
    });
}

void EfiUtilitiesDialog::finishWithPayload(const QByteArray &payload, const QString &asset,
                                           const QString &version, const QString &sha256)
{
    if (!EfiUtil::managedFileFor(currentUtility(), asset).ok
        || !EfiUtil::digestsEqual(EfiUtil::sha256Hex(payload), sha256)) {
        QMessageBox::warning(this, tr("EFI Utilities"),
                             tr("The EFI file failed the final check. Nothing was written."));
        return;
    }
    EfiUtil::Request request;
    request.action = QStringLiteral("install");
    request.utility = currentUtility();
    request.espGuid = selectedGuid();
    request.asset = asset;
    request.version = version;
    request.sha256 = sha256;
    request.replace = statusPresence != QLatin1String("absent");
    runRequest(request, payload);
}
