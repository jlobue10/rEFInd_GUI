#!/bin/bash
# A simple script to install the rEFInd customization GUI

echo -e "Installing rEFInd Customization GUI...\n"
cd "$HOME" || exit 1
rm -rf "$HOME/rEFInd_GUI"
git clone https://github.com/jlobue10/rEFInd_GUI
cd rEFInd_GUI || exit 1
CURRENT_WD="$(pwd)"

mkdir -p "$HOME/.local/rEFInd_GUI"
cp -rf "$CURRENT_WD/GUI/" "$HOME/.local/rEFInd_GUI"
cp -rf "$CURRENT_WD/icons/" "$HOME/.local/rEFInd_GUI"
cp -rf "$CURRENT_WD/backgrounds/" "$HOME/.local/rEFInd_GUI"
cp -f "$CURRENT_WD/refind_install_package_mgr.sh" "$CURRENT_WD/refind_install_Sourceforge.sh" "$HOME/.local/rEFInd_GUI"
cp -f "$CURRENT_WD/refind-GUI.conf" "$HOME/.local/rEFInd_GUI/GUI/refind.conf"

chmod +x "$HOME/.local/rEFInd_GUI/refind_install_package_mgr.sh" "$HOME/.local/rEFInd_GUI/refind_install_Sourceforge.sh"

command -v dnf >/dev/null 2>&1
FEDORA_BASE=$?

test -f /etc/bazzite/image_name
BAZZITE=$?

if [ "$FEDORA_BASE" = 0 ] && [ "$BAZZITE" != 0 ]; then
	echo -e '\nFedora based installation starting.\n'
	sudo dnf install -y cmake gcc-c++ qt6-qtbase-devel qt6-qttools-devel qt5-qtbase-devel qt5-qttools-devel xterm
	mkdir -p "$HOME/rpmbuild/SPECS" "$HOME/rpmbuild/SOURCES"
	cp rEFInd_GUI.spec "$HOME/rpmbuild/SPECS"
	rpmbuild -bb "$HOME/rpmbuild/SPECS/rEFInd_GUI.spec"
	if sudo dnf list --installed | grep -q rEFInd_GUI; then
		sudo dnf remove -y rEFInd_GUI
	fi
	sudo dnf install -y "$HOME"/rpmbuild/RPMS/x86_64/rEFInd_GUI*.rpm
fi

if [ "$BAZZITE" = 0 ]; then
	echo -e '\nBazzite based installation starting.\n'
	if ! rpm-ostree status | grep -q xterm; then
		sudo rpm-ostree install xterm
	fi
	cd "$HOME/Downloads" || exit 1
	rm -f rEFInd_GUI*.rpm
	wget "$(curl -s https://api.github.com/repos/jlobue10/rEFInd_GUI/releases/latest | grep "browser_download_url.*\.rpm" | grep -v "\.src\.rpm" | cut -d : -f 2,3 | tr -d '" ')"
	sudo rpm-ostree install ./rEFInd_GUI*.rpm
fi

if grep -q 'CachyOS' /etc/os-release; then
	echo "Starting CachyOS based installation."
	sudo pacman -S --needed base-devel git cmake qt5-base qt5-tools
	(cd "$CURRENT_WD" && makepkg -si)
fi

sed -i "s@USER@$USER@g" "$CURRENT_WD/install_config_from_GUI"
sed -i "s@HOME@/home/$USER@g" "$CURRENT_WD/rEFInd_GUI.desktop"
sed -i "s@HOME@/home/$USER@g" "$CURRENT_WD/install_config_from_GUI.sh"
sed -i "s@USER@$USER@g" "$CURRENT_WD/rEFInd_bg_randomizer.sh"

sudo mkdir -p /etc/rEFInd
sudo cp -f "$CURRENT_WD/install_config_from_GUI.sh" /etc/rEFInd/install_config_from_GUI.sh
sudo cp -f "$CURRENT_WD/rEFInd_bg_randomizer.sh" /etc/rEFInd/rEFInd_bg_randomizer.sh
sudo cp -f "$CURRENT_WD/GUI/UEFI_icon.png" /etc/rEFInd/UEFI_icon.png
sudo chown root:root /etc/rEFInd/install_config_from_GUI.sh /etc/rEFInd/rEFInd_bg_randomizer.sh
sudo chmod 755 /etc/rEFInd/install_config_from_GUI.sh /etc/rEFInd/rEFInd_bg_randomizer.sh

# The sudoers rule must only be installed after the root-owned script it
# whitelists is in place, and must be root-owned mode 0440.
sudo cp -f "$CURRENT_WD/install_config_from_GUI" /etc/sudoers.d/install_config_from_GUI
sudo chown root:root /etc/sudoers.d/install_config_from_GUI
sudo chmod 440 /etc/sudoers.d/install_config_from_GUI

if [ "$BAZZITE" = 0 ]; then
	sudo cp -f "$CURRENT_WD/rEFInd_GUI.desktop" /etc/rEFInd/rEFInd_GUI.desktop
	sudo chmod +x /etc/rEFInd/rEFInd_GUI.desktop
	if [ -f "$CURRENT_WD/.Xresources" ]; then
		cp "$CURRENT_WD/.Xresources" "$HOME/.Xresources"
		xrdb "$HOME/.Xresources"
	fi
else
	sudo cp -f "$CURRENT_WD/rEFInd_GUI.desktop" /usr/share/applications/rEFInd_GUI.desktop
fi

if [ -d "$HOME/Desktop" ]; then
	cp -f "$CURRENT_WD/rEFInd_GUI.desktop" "$HOME/Desktop/refind_GUI.desktop"
	chmod +x "$HOME/Desktop/refind_GUI.desktop"
fi

if [ "$BAZZITE" = 0 ]; then
	systemctl reboot
fi
