#!/bin/bash
# A simple rEFInd automated install script using a distro's native package manager
# Please make sure that a password exists for the user before running
(
	echo 0
	echo "# Installation started: Password prompt..."
	PASSWD="$(zenity --password --title="Enter sudo password" 2>/dev/null)"
	echo "$PASSWD" | sudo -v -S
	ANS=$?
	if [[ $ANS == 1 ]]; then
		zenity --error --title="Password Error" --text="`printf "Incorrect password provided.\nPlease try again providing the correct sudo password."`" --width=400 2>/dev/null
		echo 100
		echo "# Installation Failed. Please try again with correct sudo password"
		exit 1
	fi
	which dnf 2>/dev/null
	FEDORA_BASE=$?
	which apt 2>/dev/null
	UBUNTU_BASE=$?
	which pacman 2>/dev/null
	ARCH_BASE=$?
	echo 20
	echo "# Installation continuing..."
	echo 25
	echo "# Installing rEFInd package..."
	if [ $FEDORA_BASE == 0 ]; then
		echo -e '\nFedora based installation starting.\n'
		sudo dnf install rEFInd
	fi
	if [ $UBUNTU_BASE == 0 ]; then
		echo -e '\nUbuntu based installation starting.\n'
		sudo apt-get update && sudo apt-get install refind
	fi
	if [ $ARCH_BASE == 0 ]; then
		echo -e '\nArch based installation starting.\n'
		#sudo pacman init and then pacman install build deps
		sudo pacman-key --init
		sudo pacman-key --populate archlinux
		sudo pacman -Sy --noconfirm --needed refind
	fi
	sudo refind-install
	efibootmgr | tee $HOME/efibootlist.txt
	WINDOWS_BOOTNUM="$(grep -A0 'Windows' $HOME/efibootlist.txt | grep -Eo '[0-9]{1,4}' | head -1)"
	sudo efibootmgr -b $WINDOWS_BOOTNUM -A
	REFIND_BOOTNUM="$(grep -A0 'rEFInd Boot Manager' $HOME/efibootlist.txt | grep -Eo '[0-9]{1,4}' | head -1)"
	sudo efibootmgr -b $REFIND_BOOTNUM -B
	echo 50
	echo "# Fixing EFI entries..."
	REFIND_BOOTNUM_ALT="$(grep -A0 'rEFInd' $HOME/efibootlist.txt | grep -Eo '[0-9]{1,4}' | head -1)"
	STEAMOS_BOOTNUM="$(grep -A0 'SteamOS' $HOME/efibootlist.txt | grep -Eo '[0-9]{1,4}' | head -1)"
	re='^[0-9]+$'
	if [[ $REFIND_BOOTNUM_ALT =~ $re ]]; then
		sudo efibootmgr -b $REFIND_BOOTNUM_ALT -B
	fi
	echo 75
	echo "# Installing files to /boot/efi partition..."
	sudo mv /boot/efi/EFI/refind/refind.conf /boot/efi/EFI/refind/refind-bkp.conf
	sudo cp $HOME/.local/rEFInd_GUI/GUI/refind.conf /boot/efi/EFI/refind/refind.conf
	#sudo cp -rf $HOME/.local/rEFInd_GUI/backgrounds/ /boot/efi/EFI/refind
	sudo cp -rf $HOME/.local/rEFInd_GUI/icons/ /boot/efi/EFI/refind
	sudo efibootmgr -c -d /dev/nvme0n1 -p 1 -L "rEFInd" -l \\EFI\\refind\\refind_x64.efi
	echo 100
	echo "# Installation completed succesfully."
) | zenity --title "Installing rEFInd with Pacman" --progress --no-cancel --width=500 2>/dev/null
