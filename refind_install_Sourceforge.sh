#!/bin/bash
#Updated Sourceforge based installation

REFIND_VER="0.14.2"
DOWNLOAD_DIR="$HOME/Downloads"

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
wget "https://sourceforge.net/projects/refind/files/${REFIND_VER}/refind-bin-gnuefi-${REFIND_VER}.zip"
echo "Unzipping rEFInd zip..."
unzip -o "refind-bin-gnuefi-${REFIND_VER}.zip"

ESP_MP="$(findmnt -no TARGET /boot/efi 2>/dev/null || findmnt -no TARGET /efi 2>/dev/null)"
[ -z "$ESP_MP" ] && ESP_MP="/boot/efi"

REFIND_BIN="$DOWNLOAD_DIR/refind-bin-${REFIND_VER}"
sudo mkdir -p "$ESP_MP/EFI/refind"
sudo cp -f "$REFIND_BIN/refind/refind_x64.efi" "$ESP_MP/EFI/refind/"
sudo cp -rf "$REFIND_BIN/refind/drivers_x64/" "$ESP_MP/EFI/refind"
sudo cp -rf "$REFIND_BIN/refind/tools_x64/" "$ESP_MP/EFI/refind"

# SkorionOS Xbox 360 USB controller UEFI driver: dropping it into rEFInd's
# drivers_x64 folder makes wired/handheld gamepads (ROG Ally, Legion Go, etc.)
# usable in the boot menu. The driver auto-creates its own config at
# \EFI\Xbox360\config.ini on first boot, so only the .efi is needed here.
XBOX360_DRV_URL="https://github.com/SkorionOS/UsbXbox360Dxe/releases/latest/download/UsbXbox360Dxe.efi"
echo "Downloading UsbXbox360Dxe.efi controller driver..."
sudo mkdir -p "$ESP_MP/EFI/refind/drivers_x64"
if wget -q -O "$DOWNLOAD_DIR/UsbXbox360Dxe.efi" "$XBOX360_DRV_URL"; then
	sudo cp -f "$DOWNLOAD_DIR/UsbXbox360Dxe.efi" "$ESP_MP/EFI/refind/drivers_x64/UsbXbox360Dxe.efi"
	rm -f "$DOWNLOAD_DIR/UsbXbox360Dxe.efi"
else
	echo "Warning: failed to download UsbXbox360Dxe.efi; skipping controller driver." >&2
fi
echo "Installing rEFInd files..."
sudo "$REFIND_BIN/refind-install"
sudo cp -rf "$REFIND_BIN/refind/icons/" "$ESP_MP/EFI/refind"
sudo cp -rf "$REFIND_BIN/fonts/" "$ESP_MP/EFI/refind"
sudo cp -f "$HOME/.local/rEFInd_GUI/GUI/refind.conf" "$ESP_MP/EFI/refind/refind.conf"
sudo cp -rf "$HOME/.local/rEFInd_GUI/backgrounds/" "$ESP_MP/EFI/refind"
sudo cp -rf "$HOME/.local/rEFInd_GUI/icons/" "$ESP_MP/EFI/refind"

echo "Fixing EFI entries..."
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

echo "Finishing up..."
ESP_DEV="$(findmnt -no SOURCE "$ESP_MP")"
ESP_DISK="/dev/$(lsblk -no PKNAME "$ESP_DEV")"
ESP_PARTNUM="$(cat "/sys/class/block/$(basename "$ESP_DEV")/partition")"
sudo efibootmgr -c -d "$ESP_DISK" -p "$ESP_PARTNUM" -L "rEFInd" -l '\EFI\refind\refind_x64.efi'
echo "Installation completed."
