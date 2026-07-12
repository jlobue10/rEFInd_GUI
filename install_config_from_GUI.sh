#!/bin/bash
# Installs the GUI-generated refind.conf and PNGs onto the EFI system
# partition. Runs as root via a sudoers rule; the home directory below is
# filled in at install time.
SRC="HOME/.local/rEFInd_GUI/GUI"

ESP="$(findmnt -no TARGET /boot/efi 2>/dev/null || findmnt -no TARGET /efi 2>/dev/null || findmnt -no TARGET /boot 2>/dev/null)"
[ -z "$ESP" ] && ESP="/boot/efi"
DEST="$ESP/EFI/refind"

mkdir -p "$DEST"
for f in refind.conf background.png os_icon1.png os_icon2.png os_icon3.png os_icon4.png; do
	if [ -f "$SRC/$f" ]; then
		cp -f "$SRC/$f" "$DEST/$f"
	fi
done
