#!/bin/bash
#Updated Sourceforge based installation

REFIND_VER="0.14.2"
DOWNLOAD_DIR="$HOME/Downloads"
TEMP_FILES=()

fail_install() {
	echo "ERROR: $*" >&2
	exit 1
}

# The GUI runs this script in a transient xterm that vanishes the instant the
# script exits, taking any error output with it. Hold the window open so the
# final status can actually be read.
pause_before_exit() {
	local temp
	for temp in "${TEMP_FILES[@]}"; do
		[ -n "$temp" ] && rm -f -- "$temp" 2>/dev/null
	done
	if [ -t 0 ]; then
		echo
		read -rp "Press Enter to close this window..."
	fi
}
trap pause_before_exit EXIT

echo "Installation started..."
echo "Downloading rEFInd zip file..."

# Check if the directory exists
if [ ! -d "$DOWNLOAD_DIR" ]; then
	echo "Directory $DOWNLOAD_DIR does not exist. Creating it now..."
	if mkdir -p "$DOWNLOAD_DIR"; then
		echo "Directory created successfully."
	else
		echo "Failed to create directory." >&2
		exit 1
	fi
else
	echo "Directory $DOWNLOAD_DIR already exists."
fi

cd "$DOWNLOAD_DIR" || exit 1
ZIP_PATH="$DOWNLOAD_DIR/refind-bin-gnuefi-${REFIND_VER}.zip"
ZIP_STAGE="${ZIP_PATH}.new.$$"
TEMP_FILES+=("$ZIP_STAGE")
if ! wget -O "$ZIP_STAGE" \
	"https://sourceforge.net/projects/refind/files/${REFIND_VER}/refind-bin-gnuefi-${REFIND_VER}.zip"; then
	fail_install "The rEFInd archive download failed."
fi
mv -f "$ZIP_STAGE" "$ZIP_PATH" \
	|| fail_install "The downloaded rEFInd archive could not be published."
echo "Unzipping rEFInd zip..."
unzip -o "$ZIP_PATH" \
	|| fail_install "The downloaded rEFInd archive is invalid or could not be extracted."

# Resolve the real ESP mountpoint. The ESP may be mounted at /boot/efi,
# /efi, or directly at /boot (CachyOS/systemd-boot single-partition layout).
# Plain `findmnt /boot/efi` only matches an exact mountpoint, so on a
# /boot-mounted ESP it returned nothing and the old fallback wrote to the
# literal path /boot/efi -- a *subdir inside* the ESP -- producing a nested
# EFI/EFI/refind the firmware never loads. `findmnt --target` resolves a path
# to its containing mount; we pick the FAT ESP that already holds an
# EFI/refind install so writes hit the booting copy.
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

REFIND_BIN="$DOWNLOAD_DIR/refind-bin-${REFIND_VER}"
if [ ! -x "$REFIND_BIN/refind-install" ]; then
	fail_install "rEFInd download or unzip failed ($REFIND_BIN is missing)."
fi
DEST="$ESP_MP/EFI/refind"
[ -s "$REFIND_BIN/refind/refind_x64.efi" ] \
	|| fail_install "The extracted rEFInd loader is missing or empty."
sudo mkdir -p "$DEST" \
	|| fail_install "Could not create $DEST; the ESP may be read-only."
sudo cp -f "$REFIND_BIN/refind/refind_x64.efi" "$DEST/" \
	&& sudo cp -rf "$REFIND_BIN/refind/drivers_x64/" "$DEST" \
	&& sudo cp -rf "$REFIND_BIN/refind/tools_x64/" "$DEST" \
	|| fail_install "Could not copy the core rEFInd files to the ESP."

# SkorionOS Xbox 360 USB controller UEFI driver: dropping it into rEFInd's
# drivers_x64 folder makes wired/handheld gamepads (ROG Ally, Legion Go, etc.)
# usable in the boot menu. The driver auto-creates its own config at
# \EFI\Xbox360\config.ini on first boot, so only the .efi is needed here.
# NOTE: temporarily fetched from the jlobue10 fork (adds Legion Go 2 PIDs +
# Ally lockup fix); revert to SkorionOS once upstream PR #7 is merged/released.
XBOX360_DRV_URL="https://github.com/jlobue10/UsbXbox360Dxe/releases/latest/download/UsbXbox360Dxe.efi"
echo "Downloading UsbXbox360Dxe.efi controller driver..."
sudo mkdir -p "$ESP_MP/EFI/refind/drivers_x64"
# A truncated or HTML error-page download must not reach the ESP:
# require a non-empty file with the PE "MZ" signature before copying.
if wget -q -O "$DOWNLOAD_DIR/UsbXbox360Dxe.efi" "$XBOX360_DRV_URL" \
	&& [ -s "$DOWNLOAD_DIR/UsbXbox360Dxe.efi" ] && [ "$(head -c2 "$DOWNLOAD_DIR/UsbXbox360Dxe.efi")" = "MZ" ]; then
	sudo cp -f "$DOWNLOAD_DIR/UsbXbox360Dxe.efi" "$ESP_MP/EFI/refind/drivers_x64/UsbXbox360Dxe.efi"
	rm -f "$DOWNLOAD_DIR/UsbXbox360Dxe.efi"
else
	echo "Warning: failed to download UsbXbox360Dxe.efi; skipping controller driver." >&2
fi
# TouchI2cDxe touchscreen UEFI driver (successor of AllyTouchI2cDxe):
# built-in HID-over-I2C touchscreens -- ROG Xbox Ally / Ally X (DMI board
# RC73YA / RC73XA, Novatek), Steam Deck OLED/LCD (DMI product Galileo /
# Jupiter, FocalTech) and ASUS Zenbook Pro 14 Duo (DMI board UX8402VV,
# ELAN, driver v1.3.0) -- are structurally invisible to a USB driver; this
# driver produces AbsolutePointer so the rEFInd menu is touch-usable. Only
# these devices get it. Like the controller driver, download failure is
# non-fatal.
TOUCH_DEVICE=""
case "$(cat /sys/class/dmi/id/board_name 2>/dev/null)" in
RC73XA*|RC73YA*|UX8402VV*) TOUCH_DEVICE=1 ;;
esac
case "$(cat /sys/class/dmi/id/product_name 2>/dev/null)" in
Galileo|Jupiter) TOUCH_DEVICE=1 ;;
esac
if [ -n "$TOUCH_DEVICE" ]; then
	TOUCH_DRV_URL="https://github.com/jlobue10/TouchI2cDxe/releases/latest/download/TouchI2cDxe.efi"
	echo "Downloading TouchI2cDxe.efi touchscreen driver..."
	# A truncated or HTML error-page download must not reach the ESP:
	# require a non-empty file with the PE "MZ" signature before copying.
	if wget -q -O "$DOWNLOAD_DIR/TouchI2cDxe.efi" "$TOUCH_DRV_URL" \
		&& [ -s "$DOWNLOAD_DIR/TouchI2cDxe.efi" ] && [ "$(head -c2 "$DOWNLOAD_DIR/TouchI2cDxe.efi")" = "MZ" ]; then
		sudo cp -f "$DOWNLOAD_DIR/TouchI2cDxe.efi" "$ESP_MP/EFI/refind/drivers_x64/TouchI2cDxe.efi"
		rm -f "$DOWNLOAD_DIR/TouchI2cDxe.efi"
		# TouchI2cDxe supersedes AllyTouchI2cDxe; leaving both would load
		# two AbsolutePointer producers for the same panel.
		sudo rm -f "$ESP_MP/EFI/refind/drivers_x64/AllyTouchI2cDxe.efi"
	else
		echo "Warning: failed to download TouchI2cDxe.efi; skipping touchscreen driver." >&2
	fi
fi
echo "Installing rEFInd files..."
sudo "$REFIND_BIN/refind-install" \
	|| fail_install "refind-install failed; Windows was not changed."
sudo cp -rf "$REFIND_BIN/refind/icons/" "$DEST" \
	&& sudo cp -rf "$REFIND_BIN/fonts/" "$DEST" \
	&& sudo cp -rf "$HOME/.local/rEFInd_GUI/backgrounds/" "$DEST" \
	&& sudo cp -rf "$HOME/.local/rEFInd_GUI/icons/" "$DEST" \
	|| fail_install "Could not copy the rEFInd assets to the ESP."

GUI_CONF="$HOME/.local/rEFInd_GUI/GUI/refind.conf"
[ -s "$GUI_CONF" ] \
	|| fail_install "No non-empty GUI refind.conf exists. Use Create Config first."
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
sudo test -s "$DEST/refind_x64.efi" && sudo test -s "$DEST/refind.conf" \
	|| fail_install "The installed rEFInd loader or config is missing or empty."

# Sign before publishing the new NVRAM entry or changing Windows. With Secure
# Boot enforcing, an unsigned loader is not a usable replacement.
if grep -qE '^ID="?cachyos"?$' /etc/os-release 2>/dev/null; then
	SB_STATE="$(od -An -tu1 -j4 -N1 \
		/sys/firmware/efi/efivars/SecureBoot-8be4df61-93ca-11d2-aa0d-00e098032b8c 2>/dev/null \
		| tr -d '[:space:]')"
	if [ "$SB_STATE" = "1" ]; then
		command -v sbctl-batch-sign >/dev/null 2>&1 \
			|| fail_install "Secure Boot is enabled but sbctl-batch-sign was not found."
		echo "Secure Boot is enabled: signing EFI binaries with sbctl-batch-sign..."
		sudo sbctl-batch-sign \
			|| fail_install "sbctl-batch-sign failed; Windows was left active."
	fi
fi

echo "Updating EFI boot entries..."
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
	echo "Creating rEFInd boot entry ($ESP_DISK partition $ESP_PARTNUM)..."
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
				echo "Deleting old rEFInd entry Boot$_num..."
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

echo
echo "==================== Installation summary ===================="
if ! FINAL_LIST="$(efibootmgr 2>&1)"; then
	printf '%s\n' "$FINAL_LIST" >&2
	fail_install "efibootmgr could not read the firmware boot list."
fi
printf '%s\n' "$FINAL_LIST"
echo "---------------------------------------------------------------"
REFIND_NUMS="$(printf '%s\n' "$FINAL_LIST" | sed -nE 's/^Boot([0-9A-Fa-f]{4})\*? +rEFInd.*/\1/p')"
FIRST_BOOT="$(printf '%s\n' "$FINAL_LIST" | sed -nE 's/^BootOrder: ([0-9A-Fa-f]{4}).*/\1/p')"
if [ -z "$REFIND_NUMS" ]; then
	echo "*** FAILED: no rEFInd entry exists in the firmware boot list. ***"
	echo "*** rEFInd will NOT be offered at boot -- see errors above.   ***"
	exit 1
elif printf '%s\n' "$REFIND_NUMS" | grep -qx "$FIRST_BOOT"; then
	echo "SUCCESS: rEFInd is installed and first in the boot order."
else
	echo "WARNING: a rEFInd entry exists but is NOT first in the boot order"
	echo "(boot order starts with Boot$FIRST_BOOT)."
fi
echo "Installation completed."
