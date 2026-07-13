#!/bin/bash
# A simple rEFInd automated install script using a distro's native package manager
# Please make sure that a password exists for the user before running
(
	echo 0
	echo "# Installation started: Password prompt..."
	PASSWD="$(zenity --password --title="Enter sudo password" 2>/dev/null)"
	if ! printf '%s\n' "$PASSWD" | sudo -S -v 2>/dev/null; then
		zenity --error --title="Password Error" --text="Incorrect password provided.\nPlease try again providing the correct sudo password." --width=400 2>/dev/null
		echo 100
		echo "# Installation Failed. Please try again with correct sudo password"
		exit 1
	fi
	unset PASSWD
	echo 20
	echo "# Installation continuing..."
	echo 25
	echo "# Installing rEFInd package..."
	if command -v dnf >/dev/null 2>&1; then
		echo -e '\nFedora based installation starting.\n'
		sudo dnf install -y refind
	elif command -v apt-get >/dev/null 2>&1; then
		echo -e '\nUbuntu based installation starting.\n'
		sudo apt-get update && sudo apt-get install -y refind
	elif command -v pacman >/dev/null 2>&1; then
		echo -e '\nArch based installation starting.\n'
		sudo pacman-key --init
		sudo pacman-key --populate archlinux
		sudo pacman -Sy --noconfirm --needed refind
	fi
	sudo refind-install
	echo 50
	echo "# Fixing EFI entries..."
	EFILIST="$(mktemp)"
	efibootmgr | tee "$EFILIST"
	get_bootnum() {
		grep -m1 "$1" "$EFILIST" | sed -nE 's/^Boot([0-9A-Fa-f]{4}).*/\1/p'
	}
	BOOTNUM_RE='^[0-9A-Fa-f]{4}$'
	WINDOWS_BOOTNUM="$(get_bootnum 'Windows')"
	if [[ $WINDOWS_BOOTNUM =~ $BOOTNUM_RE ]]; then
		sudo efibootmgr -b "$WINDOWS_BOOTNUM" -A
	fi
	REFIND_BOOTNUM="$(get_bootnum 'rEFInd Boot Manager')"
	if [[ $REFIND_BOOTNUM =~ $BOOTNUM_RE ]]; then
		sudo efibootmgr -b "$REFIND_BOOTNUM" -B
	fi
	REFIND_BOOTNUM_ALT="$(get_bootnum 'rEFInd')"
	if [[ $REFIND_BOOTNUM_ALT =~ $BOOTNUM_RE ]]; then
		sudo efibootmgr -b "$REFIND_BOOTNUM_ALT" -B
	fi
	rm -f "$EFILIST"
	echo 75
	echo "# Installing files to EFI system partition..."
	# Resolve the real ESP mountpoint. The ESP may be mounted at /boot/efi,
	# /efi, or directly at /boot (CachyOS/systemd-boot single-partition
	# layout). Plain `findmnt /boot/efi` only matches an exact mountpoint, so
	# on a /boot-mounted ESP it returned nothing and the old fallback wrote to
	# the literal path /boot/efi -- a *subdir inside* the ESP -- producing a
	# nested EFI/EFI/refind the firmware never loads. `findmnt --target`
	# resolves a path to its containing mount; we pick the FAT ESP that
	# already holds an EFI/refind install so writes hit the booting copy.
	ESP_MP=""
	for _cand in /boot/efi /efi /boot; do
		[ -e "$_cand" ] || continue
		_mp="$(findmnt -no TARGET --target "$_cand" 2>/dev/null | head -1)"
		[ -n "$_mp" ] || continue
		case "$(findmnt -no FSTYPE --target "$_cand" 2>/dev/null | head -1)" in
			vfat|msdos|fat) ;; *) continue ;;
		esac
		if [ -d "$_mp/EFI/refind" ]; then ESP_MP="$_mp"; break; fi
		[ -z "$ESP_MP" ] && ESP_MP="$_mp"
	done
	[ -z "$ESP_MP" ] && ESP_MP="/boot/efi"
	ESP_DEV="$(findmnt -no SOURCE "$ESP_MP")"
	ESP_DISK="/dev/$(lsblk -no PKNAME "$ESP_DEV")"
	ESP_PARTNUM="$(cat "/sys/class/block/$(basename "$ESP_DEV")/partition")"
	if sudo test -f "$ESP_MP/EFI/refind/refind.conf"; then
		sudo mv "$ESP_MP/EFI/refind/refind.conf" "$ESP_MP/EFI/refind/refind-bkp.conf"
	fi
	sudo cp -f "$HOME/.local/rEFInd_GUI/GUI/refind.conf" "$ESP_MP/EFI/refind/refind.conf"
	sudo cp -rf "$HOME/.local/rEFInd_GUI/icons/" "$ESP_MP/EFI/refind"
	echo 90
	echo "# Installing Xbox 360 controller driver..."
	# SkorionOS Xbox 360 USB controller UEFI driver: dropping it into rEFInd's
	# drivers_x64 folder makes wired/handheld gamepads (ROG Ally, Legion Go, etc.)
	# usable in the boot menu. The driver auto-creates its own config at
	# \EFI\Xbox360\config.ini on first boot, so only the .efi is needed here.
	# NOTE: temporarily fetched from the jlobue10 fork (adds Legion Go 2 PIDs +
	# Ally lockup fix); revert to SkorionOS once upstream PR #6 is merged/released.
	XBOX360_DRV_URL="https://github.com/jlobue10/UsbXbox360Dxe/releases/latest/download/UsbXbox360Dxe.efi"
	XBOX360_DRV_TMP="$(mktemp)"
	sudo mkdir -p "$ESP_MP/EFI/refind/drivers_x64"
	if curl -fsSL "$XBOX360_DRV_URL" -o "$XBOX360_DRV_TMP" 2>/dev/null \
		|| wget -q -O "$XBOX360_DRV_TMP" "$XBOX360_DRV_URL"; then
		sudo cp -f "$XBOX360_DRV_TMP" "$ESP_MP/EFI/refind/drivers_x64/UsbXbox360Dxe.efi"
	else
		echo "# Warning: failed to download UsbXbox360Dxe.efi; skipping controller driver."
	fi
	rm -f "$XBOX360_DRV_TMP"
	sudo efibootmgr -c -d "$ESP_DISK" -p "$ESP_PARTNUM" -L "rEFInd" -l '\EFI\refind\refind_x64.efi'
	echo 100
	echo "# Installation completed successfully."
) | zenity --title "Installing rEFInd" --progress --no-cancel --width=500 2>/dev/null
