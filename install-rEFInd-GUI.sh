#!/bin/bash
# A simple script to install the rEFInd customization GUI

echo ""

read -p "Please make sure a sudo password is already set before continuing. If you have not set the user\
 or sudo password, please exit this installer with 'Ctrl+c' and then create a password either using 'passwd'\
 from a command line or by using the KDE Plasma User settings GUI. Otherwise, press Enter/Return to continue with the install."

CURRENT_WD=$(pwd) 2>/dev/null
mkdir -p $HOME/.local/rEFInd_GUI 2>/dev/null
yes | cp -rf $CURRENT_WD/GUI/ $HOME/.local/rEFInd_GUI 2>/dev/null
yes | cp -rf $CURRENT_WD/icons/ $HOME/.local/rEFInd_GUI 2>/dev/null
yes | cp -rf $CURRENT_WD/backgrounds/ $HOME/.local/rEFInd_GUI 2>/dev/null

# Create Install config from file to be used with passwordless sudo
cat > $HOME/.local/rEFInd_GUI/install_config_from_GUI.sh <<EOF
#!/bin/bash
cp $HOME/.local/rEFInd_GUI/GUI/{refind.conf,background.png,os_icon1.png,os_icon2.png,os_icon3.png,os_icon4.png} /boot/efi/EFI/refind/ 2>/dev/null

ANS=$? 2>/dev/null
if [[ $ANS == 0 ]]; then
    zenity --info --title="Success" --text="`printf "The refind.conf config file, OS icons and background image\nwere successfully moved to the refind folder on the /esp partition."`" --width=500 2>/dev/null
else
    zenity --error --title="Password Error" --text="`printf "Incorrect password provided, or some files were not found for installation.\nPlease try again providing the correct sudo password,\nand ensuring that the refind.conf config file, 4 OS icons, and background image\nexist in the /home/deck/.SteamDeck_rEFInd/GUI/ directory."`" --width=600 2>/dev/null
    exit 1
fi
EOF

chmod 555 $HOME/.local/rEFInd_GUI/install_config_from_GUI.sh 2>/dev/null

#Create file for passwordless sudo for config file, background and icon installation
cat > $HOME/.local/rEFInd_GUI/install_config_from_GUI <<EOF
$USER ALL = NOPASSWD: $HOME/.local/rEFInd_GUI/install_config_from_GUI.sh
EOF

sudo cp $HOME/.local/rEFInd_GUI/install_config_from_GUI /etc/sudoers.d 2>/dev/null

yes | cp $CURRENT_WD/{refind_install.sh,refind_install_Sourceforge.sh} $HOME/.local/rEFInd_GUI 2>/dev/null
yes | cp $CURRENT_WD/refind-GUI.conf $HOME/.local/rEFInd_GUI/GUI/refind.conf 2>/dev/null
chmod +x $HOME/.local/rEFInd_GUI/*.sh 2>/dev/null

which dnf 2>/dev/null
FEDORA_BASE=$?

if [ $FEDORA_BASE == 0 ]; then
	echo -e '\nFedora based installation starting.\n'
	sudo dnf install cmake hwinfo gcc-c++ qt6-qtbase-devel qt6-qttools-devel qt5-qtbase-devel qt5-qttools-devel xterm
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

cd $HOME/.local/rEFInd_GUI/GUI/src 2>/dev/null
ls -l $HOME/.local/rEFInd_GUI/GUI/src/build/CMakeCache.txt 2>/dev/null
if [ $? == 0 ]; then
	rm $HOME/.local/rEFInd_GUI/GUI/src/build/CMakeCache.txt 2>/dev/null
fi
ls -l $HOME/rEFInd_GUI/GUI/src/build/CMakeCache.txt 2>/dev/null
if [ $? == 0 ]; then
	rm $HOME/rEFInd_GUI/GUI/src/build/CMakeCache.txt 2>/dev/null
fi
mkdir -p build 2>/dev/null
cd build 2>/dev/null
cmake ..
make

if [ ! -f $HOME/.local/rEFInd_GUI/GUI/src/build/rEFInd_GUI ]; then
	echo -e "\nGUI compile failed. Please try again after ensuring that your cloned repo is up to date and your pacman config is normal.\n"
	exit 1
fi

# Move compiled rEFInd_GUI binary into GUI folder
cp rEFInd_GUI ../../ 2>/dev/null

#Create .desktop icon entry. Needs cat with generic username
cat > $HOME/.local/rEFInd_GUI/GUI/refind_GUI.desktop <<EOF
[Desktop Entry]
Categories=System;
Comment=rEFInd Customization GUI
Exec=$HOME/.local/rEFInd_GUI/GUI/rEFInd_GUI
GenericName=rEFInd GUI
Name=rEFInd GUI
Path=$HOME/.local/rEFInd_GUI/GUI
Icon=$HOME/.local/rEFInd_GUI/GUI/UEFI_icon.png
StartupNotify=true
Terminal=false
Type=Application
X-KDE-SubstituteUID=false
EOF

chmod +x $HOME/.local/rEFInd_GUI/GUI/refind_GUI.desktop 2>/dev/null

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

#Create script to pick random PNG background from backgrounds folder
cat > $HOME/.local/rEFInd_GUI/rEFInd_bg_randomizer.sh <<EOF
#!/bin/bash
RAND_BG="$(ls $HOME/.local/rEFInd_GUI/backgrounds | grep .png | shuf -n 1)" 2>/dev/null
sudo cp $HOME/.local/rEFInd_GUI/backgrounds/$RAND_BG /boot/efi/EFI/refind/ 2>/dev/null
EOF

chmod +x $HOME/.local/rEFInd_GUI/rEFInd_bg_randomizer.sh 2>/dev/null

#Create systemd service file for optional rEFInd background randomizer
cat > $HOME/.local/rEFInd_GUI/rEFInd_bg_randomizer.service <<EOF
[Unit]
Description=Randomize Background on each rEFInd boot

[Service]
Type=oneshot
RemainAfterExit=yes
ExecStart=/bin/bash -c '$HOME/.local/rEFInd_GUI/rEFInd_bg_randomizer.sh'

[Install]
WantedBy=multi-user.target
EOF