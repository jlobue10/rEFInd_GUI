%global _name   rEFInd_GUI

Name:           rEFInd_GUI
Version:        1.1.0
Release:        1%{?dist}
Summary:        Small GUI for customizing and installing rEFInd bootloader

License:        GPL3
URL:            https://github.com/jlobue10/rEFInd_GUI
Source0:        rEFInd_GUI.desktop
Source1:        rEFInd_bg_randomizer.service

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
mkdir -p %{buildroot}/usr/bin
cp %{_builddir}/rEFInd_GUI/GUI/src/build/rEFInd_GUI %{buildroot}/usr/bin/rEFInd_GUI

mkdir -p %{buildroot}/etc/systemd/system
mkdir -p %{buildroot}/usr/share/applications

install -m 777 %{SOURCE0} %{buildroot}/usr/share/applications
install -m 644 %{SOURCE1} %{buildroot}/etc/systemd/system

%files
/etc/systemd/system/rEFInd_bg_randomizer.service
/usr/bin/rEFInd_GUI
/usr/share/applications/rEFInd_GUI.desktop

%changelog
* Fri Mar 15 2024 Jon LoBue <jlobue10@gmail.com> [1.1.0-1]
- Bazzite friendly changes

* Wed Nov 15 2023 Jon LoBue <jlobue10@gmail.com> [1.0.0-1]
- Initial package
