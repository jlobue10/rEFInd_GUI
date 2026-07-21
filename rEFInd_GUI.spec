%global _name   rEFInd_GUI
# No debuginfo/debugsource split packages; install scripts and the release
# workflow glob the built package by name.
%global debug_package %{nil}

Name:           rEFInd_GUI
Version:        2.7.0
Release:        1%{?dist}
Summary:        Small GUI for customizing and installing rEFInd bootloader

License:        GPL3
URL:            https://github.com/jlobue10/rEFInd_GUI
Source0:        rEFInd_bg_randomizer.service

BuildRequires:  cmake gcc-c++ git-core make qt6-qtbase-devel qt6-qttools-devel
Requires:       mokutil sbsigntools xterm zenity
Provides:       rEFInd_GUI
Conflicts:      rEFInd_GUI

%description
rEFInd_GUI

%prep
rm -rf %{_builddir}/rEFInd_GUI
cd %{_builddir}
# Pinned to the release tag so rebuilding an old version never silently
# packages newer main-branch code.
git clone --branch v%{version} --depth 1 %{url}
cd $RPM_SOURCE_DIR
cp -f %{_builddir}/rEFInd_GUI/{rEFInd_GUI.desktop,rEFInd_bg_randomizer.service} $RPM_SOURCE_DIR

%build
cd %{_builddir}/rEFInd_GUI/GUI/src
mkdir -p build
cd build
cmake ..
make

%install
mkdir -p %{buildroot}/etc/rEFInd
cp %{_builddir}/rEFInd_GUI/GUI/src/build/rEFInd_GUI %{buildroot}/etc/rEFInd/rEFInd_GUI

mkdir -p %{buildroot}/etc/systemd/system

install -m 644 %{SOURCE0} %{buildroot}/etc/systemd/system

%files
/etc/systemd/system/rEFInd_bg_randomizer.service
/etc/rEFInd/rEFInd_GUI

%changelog
* Mon Jul 20 2026 Jon LoBue <jlobue10@gmail.com> [2.7.0-1]
- Add a Boot Icon Size option (96/128/160/192/256/512 px): non-default
  sizes emit big_icon_size and a proportionally scaled small_icon_size
  into the generated refind.conf; the selection persists in the GUI
  settings.
- Ship 256 and 512 px Steam and Windows 11 icons so the larger sizes
  have native-resolution art.

* Sun Jul 19 2026 Jon LoBue <jlobue10@gmail.com> [2.6.2-1]
- Refuse to run the config-install script when it does not match the copy
  shipped with this build (SHA-256 tamper check): the GUI now hashes
  /etc/rEFInd/install_config_from_GUI.sh against an embedded reference
  before running it as root, and shows a warning suggesting reinstall on
  a mismatch instead of running it.

* Sun Jul 19 2026 Jon LoBue <jlobue10@gmail.com> [2.6.1-1]
- Fix OS detection and Deep Scan on systems that mount the ESP through a
  systemd automount (SteamOS 3.9's /esp and /efi): detection now triggers
  the automounts, recovers entries for unmounted ESPs from the scan cache
  or firmware boot variables, Deep Scan temp-mounts unmounted ESPs, and
  the scripts no longer mistake the autofs row for the ESP device.

* Sun Jul 19 2026 Jon LoBue <jlobue10@gmail.com> [2.6.0-1]
- Steam Deck LCD (Jupiter) touchscreen support in rEFInd: the install
  scripts now fetch the TouchI2cDxe driver (v1.2.0 adds the Jupiter
  profile) on both Steam Deck models, not just the OLED.

* Sun Jul 19 2026 Jon LoBue <jlobue10@gmail.com> [2.5.0-1]
- Steam Deck OLED touchscreen support in rEFInd: the install scripts now
  fetch the TouchI2cDxe UEFI driver (successor of AllyTouchI2cDxe) for
  supported HID-over-I2C touch panels (ROG Xbox Ally / Ally X and Steam
  Deck OLED), removing a stale AllyTouchI2cDxe.efi after a successful
  download.

* Sun Jul 19 2026 Jon LoBue <jlobue10@gmail.com> [2.4.1-1]
- Fix the overlap between the Rescan OSes and Deep Scan buttons by moving
  the Enable Mouse checkbox up into the Boot Option #3 row.

* Sun Jul 19 2026 Jon LoBue <jlobue10@gmail.com> [2.4.0-1]
- OS detection now recovers boot entries when the ESP is mounted root-only
  (the Fedora-family umask=0077 default): entries are read back from the
  firmware's boot variables, and a new opt-in Deep Scan button performs an
  elevated filesystem scan for loaders with no boot entry.
- Install Config and the background randomizer now write to the ESP the
  firmware actually boots rEFInd from, skipping stale NVRAM entries and
  scanning all ESPs before falling back to the mounted one.
- Install Config reports real success/failure detail from the install script
  on both platforms, and fails clearly when no config has been created yet.

* Sat Jul 18 2026 Jon LoBue <jlobue10@gmail.com> [2.3.7-1]
- The GUI-generated refind.conf now sets log_level 0 alongside enable_mouse
  so rEFInd never writes a boot log.

* Fri Jul 17 2026 Jon LoBue <jlobue10@gmail.com> [2.3.6-1]
- All three rEFInd install scripts now also install the AllyTouchI2cDxe
  UEFI touchscreen driver on the ROG Xbox Ally / Ally X (DMI board
  RC73XA/RC73YA), making the built-in touchscreen usable in the rEFInd
  boot menu. Confirmed working on an Xbox Ally X. Other devices are
  unaffected; a failed download warns and continues, like the controller
  driver.

* Fri Jul 17 2026 Jon LoBue <jlobue10@gmail.com> [2.3.5-1]
- Windows install script: the summary now reports what actually landed on
  the ESP (rEFInd loader, config, Xbox 360 controller driver with its
  timestamp), calls out a failed controller-driver download including
  whether a stale copy was kept, and reports success-with-warning instead
  of plain success when the driver was not updated (issue #23 follow-up)

* Fri Jul 17 2026 Jon LoBue <jlobue10@gmail.com> [2.3.4-1]
- Build against Qt6 instead of Qt5 so the app gets the native KDE Plasma
  file dialog (icon view modes and PNG previews); Plasma 6 systems only
  ship the Qt6 platform theme plugin, which left Qt5 builds with Qt's
  bare fallback dialog

* Thu Jul 16 2026 Jon LoBue <jlobue10@gmail.com> [2.3.3-1]
- Windows install script: numbered colored progress steps with an overall
  progress bar, colorized SUCCESS/WARNING/FAILED summary, a Secure Boot
  warning when sbctl signing must be done from the Linux side, and the
  window now stays open until the user closes it.

* Thu Jul 16 2026 Jon LoBue <jlobue10@gmail.com> [2.3.2-1]
- The Windows installer no longer pre-checks "Launch rEFInd GUI" on the
  finish page; the app now only starts after installation when explicitly
  requested.

* Thu Jul 16 2026 Jon LoBue <jlobue10@gmail.com> [2.3.1-1]
- New Linux uninstall script (uninstall_rEFInd.sh, installed to
  ~/.local/rEFInd_GUI): removes the rEFInd boot entries for this distro's
  ESP (leaving a Windows-side rEFInd alone), re-activates the Windows boot
  entry, removes the ESP files and refind_linux.conf, and disables the
  background randomizer; --remove-app also removes the GUI package and its
  files, --keep-esp-files limits cleanup to the boot entries.
- README overhaul: uninstall documentation for both platforms, current
  supported-distro list, v2.3.0 boot-entry semantics, and consolidated
  display-resolution notes.

* Wed Jul 15 2026 Jon LoBue <jlobue10@gmail.com> [2.3.0-1]
- The Windows installer now creates a dedicated "rEFInd" NVRAM boot entry
  (like efibootmgr on Linux) instead of repointing the Windows Boot Manager
  entry, whose optional-data blob showed up as a long hex tail after
  refind_x64.efi in efibootmgr and was passed to rEFInd as junk load options.
  Windows Boot Manager is left untouched; a repointed entry from older
  versions is restored automatically.
- New automated Windows uninstall: the app uninstaller (and the standalone
  windows/uninstall_rEFInd.ps1) removes the rEFInd boot entry and EFI
  partition files, restores direct Windows boot, unregisters the background
  randomizer task, and scrubs the per-user app directory.

* Wed Jul 15 2026 Jon LoBue <jlobue10@gmail.com> [2.2.0-1]
- Generated configs on devices without a specific quirk now use the built-in
  panel's native resolution (from EDID/DRM, preferring the internal display
  over external outputs) instead of the numbered video mode 3; mode 3 remains
  the fallback when no panel can be detected.

* Wed Jul 15 2026 Jon LoBue <jlobue10@gmail.com> [2.1.4-1]
- Install Config and the background randomizer now target the ESP that the
  firmware rEFInd boot entry actually points at, so a stale EFI/refind on
  another ESP no longer receives the config (multi-ESP dual-boot machines).
- The Windows GUI now scans letterless non-system ESPs, so Linux
  bootloaders such as CachyOS systemd-boot are detected and named after
  their boot entry titles.
- The ROG Xbox Ally and Ally X get "resolution 1920 1080" in generated
  configs instead of the numbered mode 3, which picked the wrong mode.

* Wed Jul 15 2026 Jon LoBue <jlobue10@gmail.com> [2.1.3-1]
- The last-used browse folder is now saved immediately when a file is
  picked, so it persists even when the app is force-closed (overlay or
  task switcher) rather than exited normally.

* Wed Jul 15 2026 Jon LoBue <jlobue10@gmail.com> [2.1.2-1]
- The background and OS icon Browse buttons now reopen in the folder the
  previous browse picked from (persisted across restarts), falling back
  to the home directory when that folder no longer exists

* Tue Jul 14 2026 Jon LoBue <jlobue10@gmail.com> [2.1.1-1]
- Windows: embed the app icon in the exe so Explorer, the desktop
  shortcut, and the taskbar show the rEFInd_GUI logo instead of the
  generic executable icon
- Load the window (title bar) icon from an embedded Qt resource so it
  displays regardless of working directory

* Tue Jul 14 2026 Jon LoBue <jlobue10@gmail.com> [2.1.0-1]
- Add an Open Folder button (left of Exit) that opens the GUI data folder
  holding refind.conf and the staged background/icon PNGs in the file
  manager
- Installers now create a "backgrounds" shortcut inside the GUI folder
  pointing at the background randomizer's image folder (symlink on Linux,
  .lnk on Windows)

* Tue Jul 14 2026 Jon LoBue <jlobue10@gmail.com> [2.0.5-1]
- Windows: register the background randomizer scheduled task with explicit
  settings so it starts on battery power; the default settings meant the
  logon trigger never fired on handhelds running on battery
- Windows: the randomizer now targets whichever ESP actually contains
  rEFInd, logs each run to rEFInd_bg_randomizer.log, avoids re-picking the
  installed background, and reports its first run after enabling
- Windows: all mountvol calls go through an exit-code-checked wrapper so
  PowerShell 5.1 stderr redirection can no longer terminate the
  install/config/randomizer scripts mid-mount

* Mon Jul 13 2026 Jon LoBue <jlobue10@gmail.com> [2.0.4-1]
- Resolve the ESP mountpoint via findmnt --target so /boot-mounted ESPs
  (CachyOS/systemd-boot layout) no longer get a nested EFI/EFI/refind install
- Create the rEFInd NVRAM boot entry before deleting old ones and fall back
  to sysfs when lsblk PKNAME is empty, so a failed create can no longer
  leave the machine with no rEFInd entry at all
- Remove refind-install's duplicate "rEFInd Boot Manager" entry; handle
  efibootmgr >= 18 output that appends a device path after the label
- End the install scripts with a verification summary read back from live
  NVRAM, and keep the terminal open so the result is visible
- Sign rEFInd's EFI binaries with sbctl-batch-sign on Secure Boot CachyOS
  installs so the firmware accepts them
- Windows: exit-code-checked bcdedit (failures no longer pass silently),
  backup before any modification, put Windows Boot Manager first in the
  firmware boot order, and the same read-back installation summary

* Mon Jul 13 2026 Jon LoBue <jlobue10@gmail.com> [2.0.3-1]
- Fetch the Xbox 360 controller UEFI driver from the jlobue10 fork (v1.4.0)
  so the rEFInd boot menu supports Legion Go 2 controllers and carries the
  ASUS Ally poll-timeout lockup fix; temporary until upstream PR #6 merges

* Mon Jul 13 2026 Jon LoBue <jlobue10@gmail.com> [2.0.2-1]
- Detect the Legion Go 2 (DMI product name 83N0/83N1) and default the
  generated refind.conf to resolution 1920 1200 on it; the Go 2 shares the
  original Go's board name, which previously forced 2560 1600 there

* Sun Jul 12 2026 Jon LoBue <jlobue10@gmail.com> [2.0.1-1]
- Fix OS detection finding neither Windows nor Linux on multi-ESP systems:
  request NAME from lsblk (its omission produced a flat device list that hid
  every partition) and scan every ESP by its real mount point
- Generated boot stanzas now carry each OS's ESP partition GUID as volume, so
  they boot regardless of which ESP rEFInd itself launched from
- Name a bare systemd-boot install after the running distro (e.g. CachyOS)
- Install the generated config to the ESP firmware actually boots rEFInd from

* Sun Jul 12 2026 Jon LoBue <jlobue10@gmail.com> [2.0.0-1]
- Windows support: the GUI now builds and runs on Windows (Qt6) with a
  per-user Inno Setup installer and SignPath code signing
- Automatic detection of installed OSes/bootloaders on the EFI system partition
- Removed the Linux distro selection box (now auto-detected)
- Security hardening: no shell string interpolation from user input, safer
  efibootmgr parsing, dynamic ESP discovery instead of hardcoded /dev/nvme0n1
- Dropped hwinfo dependency

* Sun Mar 31 2024 Jon LoBue <jlobue10@gmail.com> [1.4.2-1]
- Updated refind install scripts to drop Zenity in favor of xterm

* Mon Mar 18 2024 Jon LoBue <jlobue10@gmail.com> [1.4.1-1]
- Fixed typo in Sourceforge installation method script launching

* Mon Mar 18 2024 Jon LoBue <jlobue10@gmail.com> [1.4.0-1]
- Added Exit push button and other UI alignment updates

* Sat Mar 16 2024 Jon LoBue <jlobue10@gmail.com> [1.3.0-1]
- Xterm window size change

* Sat Mar 16 2024 Jon LoBue <jlobue10@gmail.com> [1.3.0-1]
- Legion Go specific code added for resolution config line

* Sat Mar 16 2024 Jon LoBue <jlobue10@gmail.com> [1.2.0-1]
- More Bazzite friendly changes

* Fri Mar 15 2024 Jon LoBue <jlobue10@gmail.com> [1.1.0-1]
- Bazzite friendly changes

* Wed Nov 15 2023 Jon LoBue <jlobue10@gmail.com> [1.0.0-1]
- Initial package
