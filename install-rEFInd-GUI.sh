#!/bin/bash
# A simple script to install the rEFInd customization GUI

echo ""

read -p "Please make sure a sudo password is already set before continuing. If you have not set the user\
 or sudo password, please exit this installer with 'Ctrl+c' and then create a password either using 'passwd'\
 from a command line or by using the KDE Plasma User settings GUI. Otherwise, press Enter/Return to continue with the install."

CURRENT_WD=$(pwd)
mkdir -p $HOME/.local/rEFInd_GUI
yes | cp -rf $CURRENT_WD/GUI/ $HOME/.local/rEFInd_GUI
yes | cp -rf $CURRENT_WD/icons/ $HOME/.local/rEFInd_GUI
yes | cp -rf $CURRENT_WD/backgrounds/ $HOME/.local/rEFInd_GUI
yes | cp $CURRENT_WD/{install_config_from_GUI.sh,refind_install.sh,refind_install_Sourceforge.sh} $HOME/.local/rEFInd_GUI
yes | cp $CURRENT_WD/refind-GUI.conf $HOME/.local/rEFInd_GUI/GUI/refind.conf
chmod +x $HOME/.local/rEFInd_GUI/*.sh
# chmod +x $HOME/.local/rEFInd_GUI/GUI/refind_GUI.desktop
cd $HOME/.local/rEFInd_GUI/GUI/src

cat /etc/nobara-release 2>/dev/null
NOBARA_BASE=$?

cat /etc/fedora-release 2>/dev/null
FEDORA_BASE=$?

if [ $NOBARA_BASE == 0 ] || [ $FEDORA_BASE == 0 ]; then
	echo -e '\nFedora based.\n'
	#commands for Fedora based GUI install
fi

cd $HOME/.local/rEFInd_GUI/GUI/src
mkdir -p build
cd build
#qmake
cmake ..
make

if [ ! -f $HOME/.local/rEFInd_GUI/GUI/src/build/rEFInd_GUI ]; then
	echo -e "\nGUI compile failed. Please try again after ensuring that your cloned repo is up to date and your pacman config is normal.\n"
	sudo steamos-readonly enable
	exit 1
fi

cp rEFInd_GUI ../../


#Creat .desktop icon entry. Needs cat with generic username

#Create file for passwordless sudo for config file, background and icon changes
cat > $HOME/.local/rEFInd_GUI/install_config_from_GUI <<EOF
$USER ALL = NOPASSWD: $HOME/.local/rEFInd_GUI/install_config_from_GUI.sh
EOF

while true; do
	echo ""
	read -p "Do you want to copy the rEFInd_GUI icon to the desktop? (y/n) " YN
	case $YN in
		[yY]) echo -e "\nOk, icon will be copied to the desktop.\n"
			cp $HOME/.local/rEFInd_GUI/GUI/refind_GUI.desktop $HOME/Desktop
			chmod +x $HOME/Desktop/refind_GUI.desktop
			break;;
		[nN]) echo -e "\nIcon will not be copied to the desktop.\n"
			exit;;
		*) echo -e "\nInvalid response.";;
	esac
done

#need to cat create sudoers entry for passwordless sudo of install config
