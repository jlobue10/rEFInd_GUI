#!/bin/bash
# Installs the GUI-generated refind.conf and PNGs onto the EFI system
# partition that firmware actually launches rEFInd from. Runs as root via a
# sudoers rule; the home directory below is filled in at install time.
#
# Everything printed here is captured by the GUI and shown in its result
# dialog, so keep the output short and human-readable.
SRC="HOME/.local/rEFInd_GUI/GUI"

ESP_TYPE_GUID=c12a7328-f81f-11d2-ba4b-00a0c93ec93b

# Temp mounts are recorded in a file, not a shell array: the resolver helpers
# run inside command substitutions, so an array appended there would be lost to
# the parent and the EXIT trap would unmount nothing, leaving removable ESPs
# mounted read-write.
CLEANUP_MOUNT_LIST="$(mktemp)"
cleanup() {
	local m
	if [ -n "${CLEANUP_MOUNT_LIST:-}" ] && [ -f "$CLEANUP_MOUNT_LIST" ]; then
		while read -r m; do
			[ -n "$m" ] || continue
			umount "$m" 2>/dev/null
			rmdir "$m" 2>/dev/null
		done < "$CLEANUP_MOUNT_LIST"
		rm -f "$CLEANUP_MOUNT_LIST"
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
	# failing that, an entry labelled "rEFInd". efibootmgr >= 18 appends a tab
	# + device path after the label, so never anchor the label to end-of-line.
	entry="$(printf '%s\n' "$out" | grep -iE '\\refind\\refind[^\\]*\.efi' | head -n1)"
	[ -z "$entry" ] && entry="$(printf '%s\n' "$out" | grep -iE '^Boot[0-9A-Fa-f]{4}\*?[[:space:]]+rEFInd([[:space:]]|$)' | head -n1)"
	[ -z "$entry" ] && return 1
	printf '%s\n' "$entry" | grep -oiE 'GPT,[0-9a-fA-F-]{36}' | head -n1 | cut -d, -f2 | tr 'A-F' 'a-f'
}

# Echo a mount point for the given partition device, mounting it on a temporary
# directory if it is not already mounted (recorded for the EXIT-trap unmount).
esp_root_for_dev() {
	local dev="$1" mp
	[ -n "$dev" ] || return 1
	mp="$(findmnt -no TARGET "$dev" 2>/dev/null | head -n1)"
	if [ -z "$mp" ]; then
		mp="$(mktemp -d /tmp/refind-esp.XXXXXX)" || return 1
		if mount "$dev" "$mp" 2>/dev/null; then
			printf '%s\n' "$mp" >> "$CLEANUP_MOUNT_LIST"
		else
			rmdir "$mp" 2>/dev/null
			return 1
		fi
	fi
	printf '%s\n' "$mp"
}

esp_root_for_partuuid() {
	local partuuid="$1" dev
	[ -n "$partuuid" ] || return 1
	dev="$(blkid -o device -t PARTUUID="$partuuid" 2>/dev/null | head -n1)"
	[ -z "$dev" ] && dev="$(lsblk -rno PATH,PARTUUID 2>/dev/null | awk -v u="$partuuid" 'tolower($2)==u {print $1; exit}')"
	esp_root_for_dev "$dev"
}

esp_has_refind() { compgen -G "$1/EFI/refind/refind*.efi" >/dev/null 2>&1; }

# 1. The ESP the firmware boots rEFInd from -- but only when rEFInd is really
#    there, so a stale NVRAM entry falls through instead of shadowing the live
#    install on another ESP.
ESP=""
HOW=""
PARTUUID="$(refind_esp_partuuid)"
if [ -n "$PARTUUID" ]; then
	MP="$(esp_root_for_partuuid "$PARTUUID")"
	if [ -n "$MP" ] && esp_has_refind "$MP"; then
		ESP="$MP"
		HOW="the ESP in the firmware's rEFInd boot entry"
	fi
fi

# 2. Any ESP that has rEFInd on it.
if [ -z "$ESP" ]; then
	while read -r dev; do
		[ -n "$dev" ] || continue
		MP="$(esp_root_for_dev "$dev")" || continue
		if [ -n "$MP" ] && esp_has_refind "$MP"; then
			ESP="$MP"
			HOW="an ESP containing rEFInd ($dev)"
			break
		fi
	done < <(lsblk -rno PATH,PARTTYPE 2>/dev/null \
		| awk -v t="$ESP_TYPE_GUID" '$2==t {print $1}')
fi

# 3. Fallback for a first install not yet booted: the ESP mounted at the usual
#    location.
if [ -z "$ESP" ]; then
	# head -1: on an automounted path findmnt lists the autofs row and the
	# real mount with the same target; stat of "<dir>/." triggers the
	# automount first (a plain stat does not - AT_NO_AUTOMOUNT).
	for _cand in /boot/efi /efi /boot; do
		stat "$_cand/." >/dev/null 2>&1
		ESP="$(findmnt -no TARGET "$_cand" 2>/dev/null | head -1)"
		[ -n "$ESP" ] && break
	done
	[ -z "$ESP" ] && ESP="/boot/efi"
	HOW="the running system's ESP"
fi

DEST="$ESP/EFI/refind"

if ! mkdir -p "$DEST" 2>/dev/null; then
	echo "Could not create $DEST -- the EFI System Partition may be mounted read-only."
	exit 4
fi

COPIED=0
for f in refind.conf background.png os_icon1.png os_icon2.png os_icon3.png os_icon4.png; do
	if [ -f "$SRC/$f" ]; then
		if ! cp -f "$SRC/$f" "$DEST/$f" 2>/dev/null; then
			echo "Failed while copying $f to $DEST -- the ESP may be full or read-only."
			exit 5
		fi
		COPIED=$((COPIED + 1))
	fi
done

if [ "$COPIED" -eq 0 ]; then
	echo "No config files were found in $SRC."
	echo "Use Create Config in the GUI first."
	exit 6
fi

# Flush to the ESP before any temporary mount goes away.
sync
echo "Installed $COPIED file(s) to $DEST"
[ -n "$HOW" ] && echo "(chosen as $HOW)"
exit 0
