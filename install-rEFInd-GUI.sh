#!/bin/bash
# A simple script to install the rEFInd customization GUI

echo -e "Installing rEFInd Customization GUI...\n"
cd $HOME
sudo rm -rf ./rEFInd_GUI
git clone https://github.com/jlobue10/rEFInd_GUI
cd rEFInd_GUI
CURRENT_WD=$(pwd)

mkdir -p $HOME/.local/rEFInd_GUI
cp -rf $CURRENT_WD/GUI/ $HOME/.local/rEFInd_GUI
cp -rf $CURRENT_WD/icons/ $HOME/.local/rEFInd_GUI
cp -rf $CURRENT_WD/backgrounds/ $HOME/.local/rEFInd_GUI
cp -f $CURRENT_WD/{refind_install_package_mgr.sh,refind_install_Sourceforge.sh} $HOME/.local/rEFInd_GUI
cp -f $CURRENT_WD/refind-GUI.conf $HOME/.local/rEFInd_GUI/GUI/refind.conf

chmod 777 $HOME/.local/rEFInd_GUI/{refind_install_package_mgr.sh,refind_install_Sourceforge.sh}

which dnf
FEDORA_BASE=$?

cat /etc/nobara-release
NOBARA=$?

cat /etc/bazzite/image_name
BAZZITE=$?

if [ $FEDORA_BASE == 0 ] && [ $BAZZITE != 0 ]; then
	echo -e '\nFedora based installation starting.\n'
 	if [ $NOBARA == 0 ]; then
		sudo dnf install -y cmake hwinfo gcc-c++ qt6-qtbase-devel qt6-qttools-devel qt5-qtbase-devel qt5-qttools-devel xterm
  	else
   		sudo dnf install -y cmake hwinfo gcc-c++ qt6-qtbase-devel qt6-qttools-devel qt5-qtbase-devel qt5-qttools-devel xterm
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

if [ $BAZZITE == 0 ]; then
	echo -e '\nBazzite based installation starting.\n'
 	rpm-ostree status | grep xterm
  	XTERM_STATUS=$?
   	if [ $XTERM_STATUS != 0]; then
		sudo rpm-ostree install xterm
    	fi
 	cd $HOME/Downloads
  	rm -f rEFInd_GUI*.rpm
  	wget $(curl -s https://api.github.com/repos/jlobue10/rEFInd_GUI/releases/latest | grep "browser_download_url.*\.rpm" | grep -v "\.src\.rpm" | cut -d : -f 2,3 | tr -d \"\ )
   	sudo rpm-ostree install ./rEFInd_GUI*.rpm
fi   

which apt 2>/dev/null
UBUNTU_BASE=$?

if [ $UBUNTU_BASE == 0 ]; then
	echo -e '\nUbuntu based installation starting.\n'
	#sudo apt-get update && sudo apt-get install build deps
fi

which pacman 2>/dev/null
ARCH_BASE=$?

if [ ! -f /usr/bin/rEFInd_GUI ] && [ $BAZZITE != 0 ]; then
	echo -e "\nGUI compile failed. Please try again after ensuring that your cloned repo is up to date and your pacman config is normal.\n"
	exit 1
fi

# if [ $NOBARA == 0 ]; then
#	#fix packaging after compile (if necessary)
#	sudo dnf install gstreamer1-plugins-good-qt6 --allowerasing
# fi

sed -i "s@USER@$USER@g" $CURRENT_WD/install_config_from_GUI
sed -i "s@HOME@/home/$USER@g" $CURRENT_WD/rEFInd_GUI.desktop
sed -i "s@HOME@/home/$USER@g" $CURRENT_WD/install_config_from_GUI.sh
sed -i "s@USER@$USER@g" $CURRENT_WD/rEFInd_bg_randomizer.sh

sudo mkdir -p /etc/rEFInd
sudo cp -f $CURRENT_WD/install_config_from_GUI /etc/sudoers.d/install_config_from_GUI
sudo cp -f $CURRENT_WD/install_config_from_GUI.sh /etc/rEFInd/install_config_from_GUI.sh
sudo cp -f $CURRENT_WD/rEFInd_GUI.desktop /usr/share/applications/rEFInd_GUI.desktop
sudo cp -f $CURRENT_WD/rEFInd_bg_randomizer.sh /etc/rEFInd/rEFInd_bg_randomizer.sh

if [ $BAZZITE == 0 ]; then
	sudo cp -f $CURRENT_WD/rEFInd_GUI.desktop /etc/rEFInd/rEFInd_GUI.desktop
fi

sudo chmod 777 /etc/rEFInd/{install_config_from_GUI.sh,rEFInd_bg_randomizer.sh,rEFInd_GUI.desktop}

#if [ $BAZZITE == 0 ]; then
#	systemctl reboot
#fi

cp /usr/share/applications/rEFInd_GUI.desktop $HOME/Desktop/refind_GUI.desktop
chmod +x $HOME/Desktop/refind_GUI.desktop
