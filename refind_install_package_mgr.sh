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
		# The candidate may sit behind a systemd automount (SteamOS mounts /esp
		# and /efi that way): resolving "<dir>/." establishes the real mount
		# and tail -1 below skips the autofs row findmnt lists first.
		stat "$_cand/." >/dev/null 2>&1
		_mp="$(findmnt -no TARGET --target "$_cand" 2>/dev/null | head -1)"
		[ -n "$_mp" ] || continue
		case "$(findmnt -no FSTYPE --target "$_cand" 2>/dev/null | tail -1)" in
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
	# Ally lockup fix); revert to SkorionOS once upstream PR #7 is merged/released.
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
	# TouchI2cDxe touchscreen UEFI driver (successor of AllyTouchI2cDxe):
	# built-in HID-over-I2C touchscreens -- ROG Xbox Ally / Ally X (DMI board
	# RC73YA / RC73XA, Novatek) and Steam Deck OLED/LCD (DMI product Galileo /
	# Jupiter, FocalTech) -- are structurally invisible to a USB driver; this
	# driver produces AbsolutePointer so the rEFInd menu is touch-usable.
	# Only these devices get it. Like the controller driver, download failure
	# is non-fatal.
	TOUCH_DEVICE=""
	case "$(cat /sys/class/dmi/id/board_name 2>/dev/null)" in
	RC73XA*|RC73YA*) TOUCH_DEVICE=1 ;;
	esac
	case "$(cat /sys/class/dmi/id/product_name 2>/dev/null)" in
	Galileo|Jupiter) TOUCH_DEVICE=1 ;;
	esac
	if [ -n "$TOUCH_DEVICE" ]; then
		echo "# Installing touchscreen driver..."
		TOUCH_DRV_URL="https://github.com/jlobue10/TouchI2cDxe/releases/latest/download/TouchI2cDxe.efi"
		TOUCH_DRV_TMP="$(mktemp)"
		if curl -fsSL "$TOUCH_DRV_URL" -o "$TOUCH_DRV_TMP" 2>/dev/null \
			|| wget -q -O "$TOUCH_DRV_TMP" "$TOUCH_DRV_URL"; then
			sudo cp -f "$TOUCH_DRV_TMP" "$ESP_MP/EFI/refind/drivers_x64/TouchI2cDxe.efi"
			# TouchI2cDxe supersedes AllyTouchI2cDxe; leaving both would load
			# two AbsolutePointer producers for the same panel.
			sudo rm -f "$ESP_MP/EFI/refind/drivers_x64/AllyTouchI2cDxe.efi"
		else
			echo "# Warning: failed to download TouchI2cDxe.efi; skipping touchscreen driver."
		fi
		rm -f "$TOUCH_DRV_TMP"
	fi
	echo 95
	echo "# Updating EFI boot entries..."
	# Resolve the ESP's parent disk and partition number for efibootmgr.
	# `lsblk -no PKNAME` has been observed returning empty here (util-linux
	# 2.42), which produced `efibootmgr -c -d /dev/ ...` -- a failed create --
	# so fall back to sysfs, where a partition's parent directory is its disk.
	# Diagnostics go to stderr: stdout is zenity's progress protocol.
	ESP_DEV="$(findmnt -no SOURCE "$ESP_MP" | grep -m1 "^/dev/")"
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
		# refind-install just created its own "rEFInd Boot Manager" entry;
		# remove it up front so the firmware list never carries it alongside
		# our "rEFInd" entry. Only that exact label is deleted pre-create --
		# plain "rEFInd" entries from previous installs are kept until the
		# new entry verifiably exists (see below).
		# efibootmgr >= 18 appends "\t<device path>" after the label even
		# without -v, so label matches must allow an optional tab suffix.
		while read -r _num; do
			echo "Deleting refind-install's rEFInd Boot Manager entry Boot$_num..." >&2
			sudo efibootmgr -b "$_num" -B >/dev/null 2>&1 \
				|| echo "Warning: could not delete Boot$_num." >&2
		done < <(efibootmgr | sed -nE 's/^Boot([0-9A-Fa-f]{4})\*? +rEFInd Boot Manager(\t.*)?$/\1/p')
		# Create the new entry BEFORE deleting old rEFInd entries. The old
		# delete-then-create order left the machine with no rEFInd entry at
		# all whenever the create failed, because the entry refind-install
		# had just made was already gone.
		if CREATE_OUT="$(sudo efibootmgr -c -d "$ESP_DISK" -p "$ESP_PARTNUM" -L "rEFInd" -l '\EFI\refind\refind_x64.efi' 2>&1)"; then
			# efibootmgr -c puts the new entry first in BootOrder; use that
			# to identify it so the cleanup below never deletes it.
			NEW_BOOTNUM="$(efibootmgr | sed -nE 's/^BootOrder: ([0-9A-Fa-f]{4}).*/\1/p')"
			if [ -n "$NEW_BOOTNUM" ] && efibootmgr | sed -nE 's/^Boot([0-9A-Fa-f]{4})\*? +rEFInd(\t.*)?$/\1/p' | grep -qx "$NEW_BOOTNUM"; then
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
	# CachyOS manages Secure Boot with sbctl: when Secure Boot is enabled, the
	# rEFInd binaries just written to the ESP must be signed with the enrolled
	# sbctl keys or the firmware will refuse to load them. The SecureBoot
	# efivar's fifth byte (after the 4-byte attribute header) is 1 when
	# enforcing. sbctl output goes to stderr: stdout is zenity's protocol.
	if grep -qE '^ID="?cachyos"?$' /etc/os-release 2>/dev/null; then
		SB_STATE="$(od -An -tu1 -j4 -N1 \
			/sys/firmware/efi/efivars/SecureBoot-8be4df61-93ca-11d2-aa0d-00e098032b8c 2>/dev/null \
			| tr -d '[:space:]')"
		if [ "$SB_STATE" = "1" ]; then
			echo 97
			echo "# Signing EFI binaries for Secure Boot (sbctl)..."
			if command -v sbctl-batch-sign >/dev/null 2>&1; then
				sudo sbctl-batch-sign >&2 \
					|| echo "Warning: sbctl-batch-sign failed; rEFInd may not load with Secure Boot enabled." >&2
			else
				echo "Warning: Secure Boot is enabled but sbctl-batch-sign was not found; EFI binaries were not signed." >&2
			fi
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
