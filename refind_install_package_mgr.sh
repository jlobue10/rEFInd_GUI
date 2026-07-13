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
	echo 95
	echo "# Updating EFI boot entries..."
	# Resolve the ESP's parent disk and partition number for efibootmgr.
	# `lsblk -no PKNAME` has been observed returning empty here (util-linux
	# 2.42), which produced `efibootmgr -c -d /dev/ ...` -- a failed create --
	# so fall back to sysfs, where a partition's parent directory is its disk.
	# Diagnostics go to stderr: stdout is zenity's progress protocol.
	ESP_DEV="$(findmnt -no SOURCE "$ESP_MP" | head -1)"
	ESP_PART="$(basename "$ESP_DEV")"
	ESP_PARTNUM="$(cat "/sys/class/block/$ESP_PART/partition" 2>/dev/null)"
	ESP_PARENT="$(lsblk -no PKNAME "$ESP_DEV" 2>/dev/null | head -1)"
	if [ -z "$ESP_PARENT" ]; then
		ESP_PARENT="$(basename "$(dirname "$(readlink -f "/sys/class/block/$ESP_PART")")")"
	fi
	ESP_DISK="/dev/$ESP_PARENT"
	if [ ! -b "$ESP_DISK" ] || [ -z "$ESP_PARTNUM" ]; then
		echo "ERROR: could not resolve the ESP's disk and partition number" >&2
		echo "(device: '$ESP_DEV', disk: '$ESP_DISK', partition: '$ESP_PARTNUM')." >&2
		echo "Existing boot entries were left untouched." >&2
	else
		# Create the new entry BEFORE deleting old rEFInd entries. The old
		# delete-then-create order left the machine with no rEFInd entry at
		# all whenever the create failed, because the entry refind-install
		# had just made was already gone.
		if CREATE_OUT="$(sudo efibootmgr -c -d "$ESP_DISK" -p "$ESP_PARTNUM" -L "rEFInd" -l '\EFI\refind\refind_x64.efi' 2>&1)"; then
			# efibootmgr -c puts the new entry first in BootOrder; use that
			# to identify it so the cleanup below never deletes it.
			NEW_BOOTNUM="$(efibootmgr | sed -nE 's/^BootOrder: ([0-9A-Fa-f]{4}).*/\1/p')"
			if [ -n "$NEW_BOOTNUM" ] && efibootmgr | grep -qE "^Boot${NEW_BOOTNUM}\*? +rEFInd$"; then
				while read -r _num; do
					[ "$_num" = "$NEW_BOOTNUM" ] && continue
					echo "Deleting old rEFInd entry Boot$_num..." >&2
					sudo efibootmgr -b "$_num" -B >/dev/null 2>&1 \
						|| echo "Warning: could not delete Boot$_num." >&2
				done < <(efibootmgr | sed -nE 's/^Boot([0-9A-Fa-f]{4})\*? +rEFInd.*/\1/p')
			else
				echo "Warning: could not identify the new rEFInd entry; skipping cleanup of old entries." >&2
			fi
		else
			echo "ERROR: creating the rEFInd boot entry failed:" >&2
			printf '%s\n' "$CREATE_OUT" >&2
			echo "Existing rEFInd entries (if any) were left in place." >&2
		fi
		WINDOWS_BOOTNUM="$(efibootmgr | sed -nE 's/^Boot([0-9A-Fa-f]{4})\*? +Windows.*/\1/p' | head -1)"
		if [ -n "$WINDOWS_BOOTNUM" ]; then
			sudo efibootmgr -b "$WINDOWS_BOOTNUM" -A >/dev/null 2>&1 \
				|| echo "Warning: could not deactivate the Windows boot entry." >&2
		fi
	fi
	echo 100
	echo "# Installation finished."
) | zenity --title "Installing rEFInd" --progress --no-cancel --width=500 2>/dev/null

# Verify the result from live NVRAM and show it both in the terminal (the GUI
# runs this in a transient xterm -- keep it open so the status can be read)
# and as a zenity dialog.
echo
echo "==================== Installation summary ===================="
FINAL_LIST="$(efibootmgr)"
printf '%s\n' "$FINAL_LIST"
echo "---------------------------------------------------------------"
REFIND_NUMS="$(printf '%s\n' "$FINAL_LIST" | sed -nE 's/^Boot([0-9A-Fa-f]{4})\*? +rEFInd.*/\1/p')"
FIRST_BOOT="$(printf '%s\n' "$FINAL_LIST" | sed -nE 's/^BootOrder: ([0-9A-Fa-f]{4}).*/\1/p')"
if [ -z "$REFIND_NUMS" ]; then
	echo "*** FAILED: no rEFInd entry exists in the firmware boot list. ***"
	echo "*** rEFInd will NOT be offered at boot -- see errors above.   ***"
	zenity --error --title="rEFInd installation failed" --width=450 \
		--text="No rEFInd boot entry exists in the firmware boot list.\nrEFInd will NOT be offered at boot. See the terminal window for details." 2>/dev/null
elif printf '%s\n' "$REFIND_NUMS" | grep -qx "$FIRST_BOOT"; then
	echo "SUCCESS: rEFInd is installed and first in the boot order."
	zenity --info --title="rEFInd installed" --width=400 \
		--text="rEFInd is installed and first in the boot order." 2>/dev/null
else
	echo "WARNING: a rEFInd entry exists but is NOT first in the boot order"
	echo "(boot order starts with Boot$FIRST_BOOT)."
	zenity --warning --title="rEFInd installed with warnings" --width=450 \
		--text="A rEFInd boot entry exists but is NOT first in the boot order." 2>/dev/null
fi
if [ -t 0 ]; then
	echo
	read -rp "Press Enter to close this window..."
fi
