#!/bin/bash
# A simple script to install the rEFInd customization GUI

echo -e "Installing rEFInd Customization GUI...\n"
cd "$HOME" || exit 1
rm -rf "$HOME/rEFInd_GUI"
if ! git clone --depth 1 https://github.com/jlobue10/rEFInd_GUI; then
	echo "Error: failed to clone the rEFInd_GUI repository. Aborting." >&2
	exit 1
fi
cd rEFInd_GUI || exit 1
CURRENT_WD="$(pwd)"

command -v dnf >/dev/null 2>&1
FEDORA_BASE=$?

test -f /etc/bazzite/image_name
BAZZITE=$?

grep -q 'CachyOS' /etc/os-release
CACHYOS=$?

# Bail out before touching the system (this script installs a sudoers rule)
# if no supported package path exists for this distro.
if [ "$FEDORA_BASE" != 0 ] && [ "$BAZZITE" != 0 ] && [ "$CACHYOS" != 0 ]; then
	echo "Error: unsupported distro (no dnf, Bazzite, or CachyOS detected). Aborting." >&2
	exit 1
fi

mkdir -p "$HOME/.local/rEFInd_GUI"
cp -rf "$CURRENT_WD/GUI/" "$HOME/.local/rEFInd_GUI"
cp -rf "$CURRENT_WD/icons/" "$HOME/.local/rEFInd_GUI"
cp -rf "$CURRENT_WD/backgrounds/" "$HOME/.local/rEFInd_GUI"
cp -f "$CURRENT_WD/refind_install_package_mgr.sh" "$CURRENT_WD/refind_install_Sourceforge.sh" "$HOME/.local/rEFInd_GUI"
cp -f "$CURRENT_WD/refind-GUI.conf" "$HOME/.local/rEFInd_GUI/GUI/refind.conf"

chmod +x "$HOME/.local/rEFInd_GUI/refind_install_package_mgr.sh" "$HOME/.local/rEFInd_GUI/refind_install_Sourceforge.sh"

if [ "$FEDORA_BASE" = 0 ] && [ "$BAZZITE" != 0 ]; then
	echo -e '\nFedora based installation starting.\n'
	sudo dnf install -y cmake gcc-c++ qt6-qtbase-devel qt6-qttools-devel qt5-qtbase-devel qt5-qttools-devel xterm
	mkdir -p "$HOME/rpmbuild/SPECS" "$HOME/rpmbuild/SOURCES"
	cp -f "$CURRENT_WD/rEFInd_GUI.spec" "$HOME/rpmbuild/SPECS"
	# Fresh output dir so the install glob below can't pick up stale rpms
	# from a previous build.
	rm -f "$HOME"/rpmbuild/RPMS/x86_64/rEFInd_GUI*.rpm
	if ! rpmbuild -bb "$HOME/rpmbuild/SPECS/rEFInd_GUI.spec"; then
		echo "Error: rpmbuild failed. Aborting." >&2
		exit 1
	fi
	if dnf list --installed | grep -q rEFInd_GUI; then
		sudo dnf remove -y rEFInd_GUI
	fi
	# rEFInd_GUI-[0-9]* skips the -debuginfo/-debugsource split packages
	sudo dnf install -y "$HOME"/rpmbuild/RPMS/x86_64/rEFInd_GUI-[0-9]*.rpm
fi

if [ "$BAZZITE" = 0 ]; then
	echo -e '\nBazzite based installation starting.\n'
	if ! rpm-ostree status | grep -q xterm; then
		sudo rpm-ostree install xterm
	fi
	cd "$HOME/Downloads" || exit 1
	rm -f rEFInd_GUI*.rpm
	# Thanks to Maclay74 steam-patch for the following syntax
	RPM_URL="$(curl -s https://api.github.com/repos/jlobue10/rEFInd_GUI/releases/latest | grep "browser_download_url.*\.rpm" | grep -vE "\.src\.rpm|debuginfo|debugsource" | head -n 1 | cut -d : -f 2,3 | tr -d '" ')"
	if [ -z "$RPM_URL" ]; then
		echo "Error: the latest GitHub release has no .rpm asset. Aborting." >&2
		exit 1
	fi
	if ! wget "$RPM_URL"; then
		echo "Error: failed to download $RPM_URL. Aborting." >&2
		exit 1
	fi
	# A previously layered rEFInd_GUI blocks installing the new local rpm
	if rpm-ostree status | grep -q rEFInd_GUI; then
		sudo rpm-ostree uninstall rEFInd_GUI
	fi
	if ! sudo rpm-ostree install ./rEFInd_GUI*.rpm; then
		echo "Error: rpm-ostree install failed. Aborting." >&2
		exit 1
	fi
fi

if [ "$CACHYOS" = 0 ]; then
	echo "Starting CachyOS based installation."
	sudo pacman -S --needed base-devel git cmake qt5-base qt5-tools
	if ! (cd "$CURRENT_WD" && makepkg -si); then
		echo "Error: makepkg failed. Aborting." >&2
		exit 1
	fi
fi

sed -i "s@USER@$USER@g" "$CURRENT_WD/install_config_from_GUI"
sed -i "s@HOME@$HOME@g" "$CURRENT_WD/rEFInd_GUI.desktop"
sed -i "s@HOME@$HOME@g" "$CURRENT_WD/install_config_from_GUI.sh"
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
	echo -e "\nA reboot is required to finish applying the rpm-ostree changes."
	read -r -p "Reboot now? [y/N] " REPLY
	case "$REPLY" in
		[Yy]*) systemctl reboot ;;
		*) echo "Reboot skipped; remember to reboot before using the GUI." ;;
	esac
fi

echo -e "Installation complete...\n"
