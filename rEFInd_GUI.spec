%global _name   rEFInd_GUI
# No debuginfo/debugsource split packages; install scripts and the release
# workflow glob the built package by name.
%global debug_package %{nil}

Name:           rEFInd_GUI
Version:        2.0.2
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
