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

command -v apt-get >/dev/null 2>&1
DEB_BASE=$?

# Bail out before touching the system (this script installs a sudoers rule)
# if no supported package path exists for this distro.
if [ "$FEDORA_BASE" != 0 ] && [ "$BAZZITE" != 0 ] && [ "$CACHYOS" != 0 ] && [ "$DEB_BASE" != 0 ]; then
	echo "Error: unsupported distro (no dnf, apt, Bazzite, or CachyOS detected). Aborting." >&2
	exit 1
fi

mkdir -p "$HOME/.local/rEFInd_GUI"
cp -rf "$CURRENT_WD/GUI/" "$HOME/.local/rEFInd_GUI"
cp -rf "$CURRENT_WD/icons/" "$HOME/.local/rEFInd_GUI"
cp -rf "$CURRENT_WD/backgrounds/" "$HOME/.local/rEFInd_GUI"
cp -f "$CURRENT_WD/refind_install_package_mgr.sh" "$CURRENT_WD/refind_install_Sourceforge.sh" "$CURRENT_WD/uninstall_rEFInd.sh" "$HOME/.local/rEFInd_GUI"
cp -f "$CURRENT_WD/refind-GUI.conf" "$HOME/.local/rEFInd_GUI/GUI/refind.conf"
# Shortcut inside GUI/ (the folder the app's Open Folder button shows) to the
# backgrounds folder the randomizer picks from.
ln -sfn ../backgrounds "$HOME/.local/rEFInd_GUI/GUI/backgrounds"

chmod +x "$HOME/.local/rEFInd_GUI/refind_install_package_mgr.sh" "$HOME/.local/rEFInd_GUI/refind_install_Sourceforge.sh" "$HOME/.local/rEFInd_GUI/uninstall_rEFInd.sh"

if [ "$FEDORA_BASE" = 0 ] && [ "$BAZZITE" != 0 ]; then
	echo -e '\nFedora based installation starting.\n'
	sudo dnf install -y xterm
	# Prefer the CI-built rpm from the latest release; fall back to a local
	# rpmbuild when the release carries no rpm or the download fails.
	mkdir -p "$HOME/Downloads"
	cd "$HOME/Downloads" || exit 1
	rm -f rEFInd_GUI*.rpm
	RPM_URL="$(curl -s https://api.github.com/repos/jlobue10/rEFInd_GUI/releases/latest | grep "browser_download_url.*\.rpm" | grep -vE "\.src\.rpm|debuginfo|debugsource" | head -n 1 | cut -d : -f 2,3 | tr -d '" ')"
	if [ -n "$RPM_URL" ] && wget "$RPM_URL"; then
		echo -e '\nInstalling the prebuilt release rpm.\n'
	else
		echo -e '\nNo release rpm available; building locally with rpmbuild.\n'
		sudo dnf install -y rpm-build cmake gcc-c++ git-core make qt6-qtbase-devel qt6-qttools-devel
		mkdir -p "$HOME/rpmbuild/SPECS" "$HOME/rpmbuild/SOURCES"
		cp -f "$CURRENT_WD/rEFInd_GUI.spec" "$HOME/rpmbuild/SPECS"
		cp -f "$CURRENT_WD/rEFInd_bg_randomizer.service" "$HOME/rpmbuild/SOURCES"
		# Fresh output dir so the copy below can't pick up stale rpms from a
		# previous build.
		rm -f "$HOME"/rpmbuild/RPMS/x86_64/rEFInd_GUI*.rpm
		if ! rpmbuild -bb "$HOME/rpmbuild/SPECS/rEFInd_GUI.spec"; then
			echo "Error: rpmbuild failed. Aborting." >&2
			exit 1
		fi
		cp -f "$HOME"/rpmbuild/RPMS/x86_64/rEFInd_GUI-[0-9]*.rpm "$HOME/Downloads/"
	fi
	if dnf list --installed | grep -q rEFInd_GUI; then
		sudo dnf remove -y rEFInd_GUI
	fi
	# rEFInd_GUI-[0-9]* skips src/debug rpms and anything stale
	sudo dnf install -y "$HOME"/Downloads/rEFInd_GUI-[0-9]*.rpm
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
	# Prefer the CI-built package from the latest release; fall back to a
	# local makepkg build when the release carries no package or the
	# download fails.
	mkdir -p "$HOME/Downloads"
	cd "$HOME/Downloads" || exit 1
	rm -f rEFInd_GUI*.pkg.tar.zst
	PKG_URL="$(curl -s https://api.github.com/repos/jlobue10/rEFInd_GUI/releases/latest | grep "browser_download_url.*x86_64\.pkg\.tar\.zst" | grep -v "debug" | head -n 1 | cut -d : -f 2,3 | tr -d '" ')"
	if [ -n "$PKG_URL" ] && wget "$PKG_URL"; then
		echo -e '\nInstalling the prebuilt release package.\n'
		INSTALL_PKG="$(basename "$PKG_URL")"
		if pacman -Qs rEFInd_GUI > /dev/null; then
			sudo pacman -R --noconfirm rEFInd_GUI
		fi
		if ! sudo pacman -U --noconfirm "./$INSTALL_PKG"; then
			echo "Error: pacman failed to install $INSTALL_PKG. Aborting." >&2
			exit 1
		fi
		rm -f "$INSTALL_PKG"
	else
		echo -e '\nNo release package available; building locally with makepkg.\n'
		sudo pacman -S --needed base-devel git cmake qt6-base qt6-tools
		if ! (cd "$CURRENT_WD" && makepkg -si); then
			echo "Error: makepkg failed. Aborting." >&2
			exit 1
		fi
	fi
fi

if [ "$DEB_BASE" = 0 ]; then
	echo -e '\nDebian/Ubuntu based installation starting.\n'
	# Prefer the CI-built package from the latest release; fall back to a
	# local dpkg-buildpackage build when the release carries no package or
	# the download fails.
	mkdir -p "$HOME/Downloads"
	cd "$HOME/Downloads" || exit 1
	rm -f refind-gui*.deb
	DEB_URL="$(curl -s https://api.github.com/repos/jlobue10/rEFInd_GUI/releases/latest | grep "browser_download_url.*_amd64\.deb" | grep -v "dbgsym" | head -n 1 | cut -d : -f 2,3 | tr -d '" ')"
	if [ -n "$DEB_URL" ] && wget "$DEB_URL"; then
		echo -e '\nInstalling the prebuilt release package.\n'
		INSTALL_DEB="$(basename "$DEB_URL")"
		if dpkg -s refind-gui >/dev/null 2>&1; then
			sudo apt-get remove -y refind-gui
		fi
		if ! sudo apt-get install -y "./$INSTALL_DEB"; then
			echo "Error: apt-get failed to install $INSTALL_DEB. Aborting." >&2
			exit 1
		fi
		rm -f "$INSTALL_DEB"
	else
		echo -e '\nNo release package available; building locally with dpkg-buildpackage.\n'
		sudo apt-get update
		sudo apt-get install -y build-essential debhelper cmake qt6-base-dev qt6-tools-dev qt6-tools-dev-tools qt6-l10n-tools
		if ! (cd "$CURRENT_WD" && dpkg-buildpackage -us -uc -b); then
			echo "Error: dpkg-buildpackage failed. Aborting." >&2
			exit 1
		fi
		if ! sudo apt-get install -y "$HOME"/refind-gui_*.deb; then
			echo "Error: apt-get failed to install the built package. Aborting." >&2
			exit 1
		fi
		rm -f "$HOME"/refind-gui_*.deb "$HOME"/refind-gui_*.buildinfo "$HOME"/refind-gui_*.changes
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
