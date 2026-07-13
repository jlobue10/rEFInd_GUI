#!/bin/bash
# Installs the GUI-generated refind.conf and PNGs onto the EFI system
# partition that firmware actually launches rEFInd from. Runs as root via a
# sudoers rule; the home directory below is filled in at install time.
SRC="HOME/.local/rEFInd_GUI/GUI"

CLEANUP_MOUNT=""
cleanup() {
	if [ -n "$CLEANUP_MOUNT" ]; then
		umount "$CLEANUP_MOUNT" 2>/dev/null
		rmdir "$CLEANUP_MOUNT" 2>/dev/null
	fi
}
trap cleanup EXIT

# Partition GUID of the ESP that the firmware boots rEFInd from, taken from the
# HD(...,GPT,<guid>,...) device path of the rEFInd entry in efibootmgr. This is
# authoritative on multi-ESP systems, where rEFInd's ESP is not necessarily the
# one mounted at /boot or /boot/efi.
refind_esp_partuuid() {
	command -v efibootmgr >/dev/null 2>&1 || return 1
	local out entry
	out="$(efibootmgr -v 2>/dev/null)" || return 1
	# Match the rEFInd entry by its loader path (\EFI\refind\refind*.efi) or,
	# failing that, an entry labelled "rEFInd".
	entry="$(printf '%s\n' "$out" | grep -iE '\\refind\\refind[^\\]*\.efi' | head -n1)"
	[ -z "$entry" ] && entry="$(printf '%s\n' "$out" | grep -iE '^Boot[0-9A-Fa-f]{4}\*?[[:space:]]+rEFInd([[:space:]]|$)' | head -n1)"
	[ -z "$entry" ] && return 1
	printf '%s\n' "$entry" | grep -oiE 'GPT,[0-9a-fA-F-]{36}' | head -n1 | cut -d, -f2 | tr 'A-F' 'a-f'
}

# Echo a mount point for the ESP with the given partition GUID, mounting it on a
# temporary directory if it is not already mounted (recording that in
# CLEANUP_MOUNT so the EXIT trap unmounts it).
esp_root_for_partuuid() {
	local partuuid="$1" dev mp
	[ -n "$partuuid" ] || return 1
	dev="$(blkid -o device -t PARTUUID="$partuuid" 2>/dev/null | head -n1)"
	[ -z "$dev" ] && dev="$(lsblk -rno PATH,PARTUUID 2>/dev/null | awk -v u="$partuuid" 'tolower($2)==u {print $1; exit}')"
	[ -z "$dev" ] && return 1
	mp="$(findmnt -no TARGET "$dev" 2>/dev/null | head -n1)"
	if [ -z "$mp" ]; then
		mp="$(mktemp -d /tmp/refind-esp.XXXXXX)" || return 1
		if mount "$dev" "$mp" 2>/dev/null; then
			CLEANUP_MOUNT="$mp"
		else
			rmdir "$mp" 2>/dev/null
			return 1
		fi
	fi
	printf '%s\n' "$mp"
}

ESP=""
PARTUUID="$(refind_esp_partuuid)"
[ -n "$PARTUUID" ] && ESP="$(esp_root_for_partuuid "$PARTUUID")"

# Fallback for systems without efibootmgr / an NVRAM rEFInd entry: the ESP
# mounted at the usual location.
if [ -z "$ESP" ]; then
	ESP="$(findmnt -no TARGET /boot/efi 2>/dev/null || findmnt -no TARGET /efi 2>/dev/null || findmnt -no TARGET /boot 2>/dev/null)"
	[ -z "$ESP" ] && ESP="/boot/efi"
fi

DEST="$ESP/EFI/refind"

mkdir -p "$DEST"
for f in refind.conf background.png os_icon1.png os_icon2.png os_icon3.png os_icon4.png; do
	if [ -f "$SRC/$f" ]; then
		cp -f "$SRC/$f" "$DEST/$f"
	fi
done
