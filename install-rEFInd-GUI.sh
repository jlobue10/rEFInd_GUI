#!/bin/bash
# A simple script to install the rEFInd customization GUI

echo -e "Installing rEFInd Customization GUI...\n"
cd $HOME
sudo rm -rf ./rEFInd_GUI
git clone https://github.com/jlobue10/rEFInd_GUI
cd rEFInd_GUI
CURRENT_WD=$(pwd)

mkdir -p $HOME/.local/rEFInd_GUI 2>/dev/null
cp -rf $CURRENT_WD/GUI/ $HOME/.local/rEFInd_GUI 2>/dev/null
cp -rf $CURRENT_WD/icons/ $HOME/.local/rEFInd_GUI 2>/dev/null
cp -rf $CURRENT_WD/backgrounds/ $HOME/.local/rEFInd_GUI 2>/dev/null
cp -f $CURRENT_WD/{refind_install_package_mgr.sh,refind_install_Sourceforge.sh} $HOME/.local/rEFInd_GUI 2>/dev/null
cp -f $CURRENT_WD/refind-GUI.conf $HOME/.local/rEFInd_GUI/GUI/refind.conf 2>/dev/null

which dnf 2>/dev/null
FEDORA_BASE=$?

cat /etc/nobara-release
NOBARA=$?

if [ $FEDORA_BASE == 0 ]; then
	echo -e '\nFedora based installation starting.\n'
 	if [ $NOBARA == 0 ]; then
		sudo dnf install cmake hwinfo gcc-c++ qt6-qtbase-devel qt6-qttools-devel qt5-qtbase-devel qt5-qttools-devel xterm --allowerasing
  	else
   		sudo dnf install cmake hwinfo gcc-c++ qt6-qtbase-devel qt6-qttools-devel qt5-qtbase-devel qt5-qttools-devel xterm
     	fi
	mkdir -p $HOME/rpmbuild/{SPECS,SOURCES}
	cp rEFInd_GUI.spec $HOME/rpmbuild/SPECS
	rpmbuild -bb $HOME/rpmbuild/SPECS/rEFInd_GUI.spec
 	sudo dnf list --installed | grep rEFInd_GUI
  	REFIND_GUI_STATUS=$?
   	if [ $REFIND_GUI_STATUS == 0 ]; then
    		sudo dnf remove -y rEFInd_GUI
	fi
	sudo dnf install -y $HOME/rpmbuild/RPMS/x86_64/rEFInd_GUI*.rpm
fi

which apt 2>/dev/null
UBUNTU_BASE=$?

if [ $UBUNTU_BASE == 0 ]; then
	echo -e '\nUbuntu based installation starting.\n'
	#sudo apt-get update && sudo apt-get install build deps
fi

which pacman 2>/dev/null
ARCH_BASE=$?

if [ $ARCH_BASE == 0 ]; then
	echo -e '\nArch based installation starting.\n'
	#sudo pacman init and then pacman install build deps
fi

if [ ! -f /usr/bin/rEFInd_GUI ]; then
	echo -e "\nGUI compile failed. Please try again after ensuring that your cloned repo is up to date and your pacman config is normal.\n"
	exit 1
fi

if [ $NOBARA == 0 ]; then
	#fix packaging after compile (if necessary)
	sudo dnf install gstreamer1-plugins-good-qt6 --allowerasing
fi

cp /usr/share/applications/rEFInd_GUI.desktop $HOME/.local/rEFInd_GUI/GUI/refind_GUI.desktop
chmod +x $HOME/.local/rEFInd_GUI/GUI/refind_GUI.desktop 2>/dev/null
