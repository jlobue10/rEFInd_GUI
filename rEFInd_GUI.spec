%global _name   rEFInd_GUI

Name:           rEFInd_GUI
Version:        1.0.0
Release:        1%{?dist}
Summary:        Small GUI for customizing and installing rEFInd bootloader

License:        GPL3
URL:            https://github.com/jlobue10/rEFInd_GUI
Source0:        rEFInd_GUI-main.zip
Source1:        refind_GUI.desktop
Source2:        rEFInd_bg_randomizer.sh
Source3:        rEFInd_bg_randomizer.service
Source4:        install_config_from_GUI
Source5:        install_config_from_GUI.sh

BuildRequires:  cmake hwinfo gcc-c++ qt6-qtbase-devel qt6-qttools-devel qt5-qtbase-devel qt5-qttools-devel
Requires:       mokutil sbsigntools xterm zenity
Provides:       rEFInd_GUI
Conflicts:      rEFInd_GUI

%description
rEFInd_GUI

%prep
rm -rf %{_builddir}/rEFInd_GUI
cd $RPM_SOURCE_DIR
rm -f rEFInd_GUI-main.zip
wget https://github.com/jlobue10/rEFInd_GUI/archive/refs/heads/main.zip
mv main.zip rEFInd_GUI-main.zip
unzip $RPM_SOURCE_DIR/rEFInd_GUI-main.zip -d %{_builddir}
mkdir -p %{_builddir}/rEFInd_GUI
cp -rf %{_builddir}/rEFInd_GUI-main/* %{_builddir}/rEFInd_GUI
rm -rf %{_builddir}/rEFInd_GUI-main
cp -f %{_builddir}/rEFInd_GUI/{rEFInd_GUI.desktop,rEFInd_bg_randomizer.sh,rEFInd_bg_randomizer.service,install_config_from_GUI,install_config_from_GUI.sh} $RPM_SOURCE_DIR

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
mkdir -p %{buildroot}/etc/sudoers.d

install -m 777 %{SOURCE1} %{buildroot}/usr/share/applications
install -m 777 %{SOURCE2} %{buildroot}/usr/bin
install -m 644 %{SOURCE3} %{buildroot}/etc/systemd/system
install -m 644 %{SOURCE4} %{buildroot}/etc/sudoers.d
install -m 777 %{SOURCE5} %{buildroot}/usr/bin

%post
sed -i "s@\$USER@${SUDO_USER}@g" /etc/sudoers.d/install_config_from_GUI
sed -i "s@\$HOME@/home/${SUDO_USER}@g" /usr/share/applications/rEFInd_GUI.desktop
sed -i 's/(/"$(/g' /usr/bin/rEFInd_bg_randomizer.sh
sed -i 's/)/)"/g' /usr/bin/rEFInd_bg_randomizer.sh
sed -i 's/lss/ls/g' /usr/bin/rEFInd_bg_randomizer.sh
sed -i 's/grepp/grep/g' /usr/bin/rEFInd_bg_randomizer.sh
sed -i 's/shuff/shuf/g' /usr/bin/rEFInd_bg_randomizer.sh
sed -i 's/USE_RAND_BG/$RAND_BG/g' /usr/bin/rEFInd_bg_randomizer.sh

%files
/etc/systemd/system/rEFInd_bg_randomizer.service
/usr/bin/rEFInd_bg_randomizer.sh
/usr/bin/rEFInd_GUI
/usr/share/applications/rEFInd_GUI.desktop
/etc/sudoers.d/install_config_from_GUI
/usr/bin/install_config_from_GUI.sh

%changelog
* Wed Nov 15 2023 Jon LoBue <jlobue10@gmail.com> [1.0.0-1]
- Initial package
