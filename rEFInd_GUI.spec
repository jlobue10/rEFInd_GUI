%global _name   rEFInd_GUI
# No debuginfo/debugsource split packages; install scripts and the release
# workflow glob the built package by name.
%global debug_package %{nil}

Name:           rEFInd_GUI
Version:        2.3.0
Release:        1%{?dist}
Summary:        Small GUI for customizing and installing rEFInd bootloader

License:        GPL3
URL:            https://github.com/jlobue10/rEFInd_GUI
Source0:        rEFInd_bg_randomizer.service

BuildRequires:  cmake gcc-c++ git-core make qt5-qtbase-devel qt5-qttools-devel
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
