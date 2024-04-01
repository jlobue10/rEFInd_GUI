%global _name   rEFInd_GUI

Name:           rEFInd_GUI
Version:        1.4.2
Release:        1%{?dist}
Summary:        Small GUI for customizing and installing rEFInd bootloader

License:        GPL3
URL:            https://github.com/jlobue10/rEFInd_GUI
Source0:        rEFInd_bg_randomizer.service

BuildRequires:  cmake gcc-c++ qt5-qtbase-devel qt5-qttools-devel
Requires:       mokutil sbsigntools xterm zenity
Provides:       rEFInd_GUI
Conflicts:      rEFInd_GUI

%description
rEFInd_GUI

%prep
rm -rf %{_builddir}/rEFInd_GUI
cd %{_builddir}
git clone %{url}
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
