#!/bin/bash
# A simple rEFInd automated install script using a distro's native package manager
# Please make sure that a password exists for the user before running
(
	fail_install() {
		echo "ERROR: $*" >&2
		echo 100
		echo "# Installation failed. See the terminal for details."
		# Exit 2 tells the summary block outside the zenity pipe that boot
		# state changes had already started; exit 1 means nothing was changed.
		if [ "${BOOT_CHANGES_STARTED:-0}" -eq 1 ]; then
			exit 2
		fi
		exit 1
	}

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
	# A failed package-DB refresh (offline reinstall) must not abort when
	# rEFInd is already installed: warn and reuse the installed copy, and
	# hard-fail only when rEFInd is absent and cannot be installed.
	if command -v dnf >/dev/null 2>&1; then
		echo -e '\nFedora based installation starting.\n'
		if ! sudo dnf install -y refind; then
			command -v refind-install >/dev/null 2>&1 \
				|| fail_install "The rEFInd package could not be installed with dnf."
			echo "Warning: dnf could not refresh/install refind; using the already-installed copy." >&2
		fi
	elif command -v apt-get >/dev/null 2>&1; then
		echo -e '\nUbuntu based installation starting.\n'
		if ! { sudo apt-get update && sudo apt-get install -y refind; }; then
			command -v refind-install >/dev/null 2>&1 \
				|| fail_install "The rEFInd package could not be installed with apt."
			echo "Warning: apt could not refresh/install refind; using the already-installed copy." >&2
		fi
	elif command -v pacman >/dev/null 2>&1; then
		echo -e '\nArch based installation starting.\n'
		if ! { sudo pacman-key --init \
			&& sudo pacman-key --populate archlinux \
			&& sudo pacman -Sy --noconfirm --needed refind; }; then
			command -v refind-install >/dev/null 2>&1 \
				|| fail_install "The rEFInd package could not be installed with pacman."
			echo "Warning: pacman could not refresh/install refind; using the already-installed copy." >&2
		fi
	else
		fail_install "No supported package manager (dnf, apt, or pacman) was found."
	fi
	command -v refind-install >/dev/null 2>&1 \
		|| fail_install "refind-install was not found after package installation."
	# refind-install creates a fallback NVRAM boot entry, so boot state
	# changes start here; failures before this point changed nothing.
	BOOT_CHANGES_STARTED=1
	sudo refind-install \
		|| fail_install "refind-install failed; Windows was not changed."
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
	DEST="$ESP_MP/EFI/refind"
	GUI_CONF="$HOME/.local/rEFInd_GUI/GUI/refind.conf"
	[ -s "$GUI_CONF" ] \
		|| fail_install "No non-empty GUI refind.conf exists. Use Create Config first."
	sudo mkdir -p "$DEST" \
		|| fail_install "Could not create $DEST; the ESP may be read-only."
	if sudo test -f "$DEST/refind.conf"; then
		sudo cp -f "$DEST/refind.conf" "$DEST/refind-bkp.conf" \
			|| fail_install "Could not preserve the previous refind.conf."
	fi
	CONF_STAGE="$DEST/.refind.conf.new.$$"
	if ! sudo cp -f "$GUI_CONF" "$CONF_STAGE" \
		|| ! sudo test -s "$CONF_STAGE" \
		|| ! sudo mv -f "$CONF_STAGE" "$DEST/refind.conf"; then
		sudo rm -f "$CONF_STAGE" 2>/dev/null
		fail_install "Could not install refind.conf completely; the previous config was preserved."
	fi
	sudo cp -rf "$HOME/.local/rEFInd_GUI/icons/" "$DEST" \
		|| fail_install "Could not copy the rEFInd icon set to the ESP."
	sudo test -s "$DEST/refind_x64.efi" && sudo test -s "$DEST/refind.conf" \
		|| fail_install "The installed rEFInd loader or config is missing or empty."
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
	# A truncated or HTML error-page download must not reach the ESP:
	# require a non-empty file with the PE "MZ" signature before copying.
	if { curl -fsSL "$XBOX360_DRV_URL" -o "$XBOX360_DRV_TMP" 2>/dev/null \
		|| wget -q -O "$XBOX360_DRV_TMP" "$XBOX360_DRV_URL"; } \
		&& [ -s "$XBOX360_DRV_TMP" ] && [ "$(head -c2 "$XBOX360_DRV_TMP")" = "MZ" ]; then
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
		# A truncated or HTML error-page download must not reach the ESP:
		# require a non-empty file with the PE "MZ" signature before copying.
		if { curl -fsSL "$TOUCH_DRV_URL" -o "$TOUCH_DRV_TMP" 2>/dev/null \
			|| wget -q -O "$TOUCH_DRV_TMP" "$TOUCH_DRV_URL"; } \
			&& [ -s "$TOUCH_DRV_TMP" ] && [ "$(head -c2 "$TOUCH_DRV_TMP")" = "MZ" ]; then
			sudo cp -f "$TOUCH_DRV_TMP" "$ESP_MP/EFI/refind/drivers_x64/TouchI2cDxe.efi"
			# TouchI2cDxe supersedes AllyTouchI2cDxe; leaving both would load
			# two AbsolutePointer producers for the same panel.
			sudo rm -f "$ESP_MP/EFI/refind/drivers_x64/AllyTouchI2cDxe.efi"
		else
			echo "# Warning: failed to download TouchI2cDxe.efi; skipping touchscreen driver."
		fi
		rm -f "$TOUCH_DRV_TMP"
	fi
	# CachyOS manages Secure Boot with sbctl. Signing is a prerequisite for
	# publishing the new NVRAM entry or changing any fallback entry: an
	# unsigned loader is not a usable replacement while Secure Boot enforces.
	if grep -qE '^ID="?cachyos"?$' /etc/os-release 2>/dev/null; then
		SB_STATE="$(od -An -tu1 -j4 -N1 \
			/sys/firmware/efi/efivars/SecureBoot-8be4df61-93ca-11d2-aa0d-00e098032b8c 2>/dev/null \
			| tr -d '[:space:]')"
		if [ "$SB_STATE" = "1" ]; then
			echo 93
			echo "# Signing EFI binaries for Secure Boot (sbctl)..."
			command -v sbctl-batch-sign >/dev/null 2>&1 \
				|| fail_install "Secure Boot is enabled but sbctl-batch-sign was not found."
			sudo sbctl-batch-sign >&2 \
				|| fail_install "sbctl-batch-sign failed; Windows was left active."
		fi
	fi
	echo 95
	echo "# Updating EFI boot entries..."
	# Snapshot the current NVRAM boot entries before changing them: a copy on
	# disk makes manual recovery trivial if anything goes sideways.
	NVRAM_BK_DIR="$HOME/.local/rEFInd_GUI/nvram-backups"
	if mkdir -p "$NVRAM_BK_DIR" 2>/dev/null; then
		efibootmgr -v > "$NVRAM_BK_DIR/efibootmgr-$(date +%Y%m%d-%H%M%S).txt" 2>/dev/null
		# Keep the ten most recent snapshots.
		ls -1t "$NVRAM_BK_DIR"/efibootmgr-*.txt 2>/dev/null | tail -n +11 | xargs -r rm -f
	fi
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
		fail_install "Could not resolve the ESP's disk and partition number (device: '$ESP_DEV', disk: '$ESP_DISK', partition: '$ESP_PARTNUM'); boot entries were left untouched."
	else
		NEW_BOOTNUM=""
		REFIND_READY=0
		# Verification below matches the new entry's HD(n,GPT,<PARTUUID>,...)
		# device path, so it needs a GPT ESP with a PARTUUID. On MBR or other
		# PARTUUID-less setups, fail before creating anything rather than
		# create an entry that can never be verified.
		ESP_PARTUUID="$(lsblk -no PARTUUID "$ESP_DEV" 2>/dev/null | head -1 | tr 'A-F' 'a-f')"
		[ -n "$ESP_PARTUUID" ] \
			|| fail_install "The ESP has no GPT PARTUUID (MBR-partitioned disk?). Automatic verification of the new boot entry requires a GPT ESP with a PARTUUID; no new rEFInd entry was created and Windows was left active."
		# Keep refind-install's fallback entry and every old rEFInd entry until
		# the replacement has been created and verified end to end.
		if CREATE_OUT="$(sudo efibootmgr -c -d "$ESP_DISK" -p "$ESP_PARTNUM" -L "rEFInd" -l '\EFI\refind\refind_x64.efi' 2>&1)"; then
			# efibootmgr -c puts the new entry first in BootOrder; use that
			# to identify it. Verify both the exact label/path and the live
			# loader/config before disabling any fallback boot entry.
			CANDIDATE_BOOTNUM="$(efibootmgr | sed -nE 's/^BootOrder: ([0-9A-Fa-f]{4}).*/\1/p')"
			NVRAM_VERBOSE="$(efibootmgr -v 2>/dev/null)"
			if [ -n "$CANDIDATE_BOOTNUM" ] \
				&& printf '%s\n' "$NVRAM_VERBOSE" | grep -qiE "^Boot${CANDIDATE_BOOTNUM}\\*?[[:space:]]+rEFInd[[:space:]]+HD\\([0-9]+,GPT,${ESP_PARTUUID},[^)]*\\)/(File\\()?\\\\EFI\\\\refind\\\\refind_x64\\.efi" \
				&& sudo test -s "$ESP_MP/EFI/refind/refind_x64.efi" \
				&& sudo test -s "$ESP_MP/EFI/refind/refind.conf"; then
				NEW_BOOTNUM="$CANDIDATE_BOOTNUM"
				REFIND_READY=1
				while read -r _num; do
					[ "$_num" = "$NEW_BOOTNUM" ] && continue
					echo "Deleting old rEFInd entry Boot$_num..." >&2
					sudo efibootmgr -b "$_num" -B >/dev/null 2>&1 \
						|| echo "Warning: could not delete Boot$_num." >&2
				done < <(efibootmgr | sed -nE 's/^Boot([0-9A-Fa-f]{4})\*? +rEFInd.*/\1/p')
			else
				fail_install "The new rEFInd entry or its installed files could not be verified; old entries and Windows were left active."
			fi
		else
			printf '%s\n' "$CREATE_OUT" >&2
			fail_install "Creating the rEFInd boot entry failed; existing entries and Windows were left active."
		fi
		# fail_install exits on any unverified entry, so reaching this point
		# means the replacement rEFInd entry is verified and ready.
		WINDOWS_BOOTNUM="$(efibootmgr | sed -nE 's/^Boot([0-9A-Fa-f]{4})\*? +Windows Boot Manager(\t.*)?$/\1/p' | head -1)"
		if [ "$REFIND_READY" -eq 1 ] && [ -n "$NEW_BOOTNUM" ] && [ -n "$WINDOWS_BOOTNUM" ]; then
			sudo efibootmgr -b "$WINDOWS_BOOTNUM" -A >/dev/null 2>&1 \
				|| echo "Warning: could not deactivate the Windows boot entry." >&2
		fi
	fi
	echo 100
	echo "# Installation finished."
) | zenity --title "Installing rEFInd" --progress --no-cancel --width=500 2>/dev/null
INSTALL_RC=${PIPESTATUS[0]}

# Verify the result from live NVRAM and show it both in the terminal (the GUI
# runs this in a transient xterm -- keep it open so the status can be read)
# and as a zenity dialog.
echo
echo "==================== Installation summary ===================="
FINAL_LIST="$(efibootmgr 2>&1)"
EFIBOOTMGR_RC=$?
printf '%s\n' "$FINAL_LIST"
echo "---------------------------------------------------------------"
REFIND_NUMS="$(printf '%s\n' "$FINAL_LIST" | sed -nE 's/^Boot([0-9A-Fa-f]{4})\*? +rEFInd.*/\1/p')"
FIRST_BOOT="$(printf '%s\n' "$FINAL_LIST" | sed -nE 's/^BootOrder: ([0-9A-Fa-f]{4}).*/\1/p')"
FINAL_RC=0
if [ "$INSTALL_RC" -ne 0 ]; then
	FINAL_RC="$INSTALL_RC"
	# Exit code 2 from the install subshell means boot state changes had
	# already started when it failed; any other failure changed nothing.
	if [ "$INSTALL_RC" -eq 2 ]; then
		echo "*** FAILED: installation stopped after a critical error. ***"
		echo "*** Windows and unverified fallback entries were left active. ***"
		zenity --error --title="rEFInd installation failed" --width=450 \
			--text="Installation stopped after a critical error.\nWindows and unverified fallback entries were left active.\nSee the terminal window for details." 2>/dev/null
	else
		echo "*** FAILED: installation stopped before any boot entries were changed. ***"
		echo "*** Nothing was changed. ***"
		zenity --error --title="rEFInd installation failed" --width=450 \
			--text="Installation failed before any boot entries were changed.\nNothing was changed.\nSee the terminal window for details." 2>/dev/null
	fi
elif [ "$EFIBOOTMGR_RC" -ne 0 ]; then
	FINAL_RC=1
	echo "*** FAILED: efibootmgr could not read the firmware boot list. ***"
	zenity --error --title="rEFInd installation failed" --width=450 \
		--text="efibootmgr could not read the firmware boot list. See the terminal window for details." 2>/dev/null
elif [ -z "$REFIND_NUMS" ]; then
	FINAL_RC=1
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
exit "$FINAL_RC"
