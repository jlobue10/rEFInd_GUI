#!/bin/bash
# Copies a random background PNG to the rEFInd directory on the EFI system
# partition. Runs as root via rEFInd_bg_randomizer.service; the account name
# below is filled in at install time.
BG_DIR="/home/USER/.local/rEFInd_GUI/backgrounds"

ESP="$(findmnt -no TARGET /boot/efi 2>/dev/null || findmnt -no TARGET /efi 2>/dev/null || findmnt -no TARGET /boot 2>/dev/null)"
[ -z "$ESP" ] && ESP="/boot/efi"

RAND_BG="$(find "$BG_DIR" -maxdepth 1 -type f -name '*.png' | shuf -n1)"
if [ -n "$RAND_BG" ] && [ -d "$ESP/EFI/refind" ]; then
	cp -f "$RAND_BG" "$ESP/EFI/refind/background.png"
fi
