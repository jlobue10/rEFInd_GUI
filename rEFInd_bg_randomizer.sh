#!/bin/bash
# Copies a random background PNG to the rEFInd directory on the EFI system
# partition that firmware actually launches rEFInd from. Runs as root via
# rEFInd_bg_randomizer.service; the account name below is filled in at install
# time. The ESP resolution mirrors install_config_from_GUI.sh (and the Windows
# randomizer) -- these scripts are deliberately standalone; keep them in sync.
BG_DIR="/home/USER/.local/rEFInd_GUI/backgrounds"

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

refind_esp_partuuid() {
	command -v efibootmgr >/dev/null 2>&1 || return 1
	local out entry
	out="$(efibootmgr -v 2>/dev/null)" || return 1
	# Loader-path match first, then an entry labelled "rEFInd". efibootmgr >= 18
	# appends a tab + device path after the label, so never anchor the label.
	entry="$(printf '%s\n' "$out" | grep -iE '\\refind\\refind[^\\]*\.efi' | head -n1)"
	[ -z "$entry" ] && entry="$(printf '%s\n' "$out" | grep -iE '^Boot[0-9A-Fa-f]{4}\*?[[:space:]]+rEFInd([[:space:]]|$)' | head -n1)"
	[ -z "$entry" ] && return 1
	printf '%s\n' "$entry" | grep -oiE 'GPT,[0-9a-fA-F-]{36}' | head -n1 | cut -d, -f2 | tr 'A-F' 'a-f'
}

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

# 1. The ESP the firmware boots rEFInd from (skipping a stale NVRAM entry).
ESP=""
PARTUUID="$(refind_esp_partuuid)"
if [ -n "$PARTUUID" ]; then
	MP="$(esp_root_for_partuuid "$PARTUUID")"
	[ -n "$MP" ] && esp_has_refind "$MP" && ESP="$MP"
fi

# 2. Any ESP that has rEFInd on it.
if [ -z "$ESP" ]; then
	while read -r dev; do
		[ -n "$dev" ] || continue
		MP="$(esp_root_for_dev "$dev")" || continue
		if [ -n "$MP" ] && esp_has_refind "$MP"; then
			ESP="$MP"
			break
		fi
	done < <(lsblk -rno PATH,PARTTYPE 2>/dev/null \
		| awk -v t="$ESP_TYPE_GUID" '$2==t {print $1}')
fi

# 3. The ESP mounted at the usual location.
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
fi

RAND_BG="$(find "$BG_DIR" -maxdepth 1 -type f -name '*.png' | shuf -n1)"
if [ -n "$RAND_BG" ] && [ -d "$ESP/EFI/refind" ]; then
	cp -f "$RAND_BG" "$ESP/EFI/refind/background.png"
	# Flush before any temporary mount is torn down by the trap.
	sync
fi
