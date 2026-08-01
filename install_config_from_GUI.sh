#!/bin/bash
# Installs the GUI-generated refind.conf and PNGs onto the EFI system
# partition that firmware actually launches rEFInd from. Runs as root via a
# sudoers rule; the home directory below is filled in at install time.
#
# Everything printed here is captured by the GUI and shown in its result
# dialog, so keep the output short and human-readable.
SRC="HOME/.local/rEFInd_GUI/GUI"

# The source files live in a user-writable directory but this script is
# reachable as root without a password, so every read of $SRC is done with the
# invoking user's privileges (see the copy loop below). sudo sets SUDO_USER;
# fall back to the owner of $SRC when the script is run as root directly.
RUN_USER="${SUDO_USER:-}"
if [ -z "$RUN_USER" ] || [ "$RUN_USER" = root ]; then
	RUN_USER="$(stat -c %U "$SRC" 2>/dev/null)"
fi
if [ -z "$RUN_USER" ] || [ "$RUN_USER" = root ]; then
	echo "Could not determine the unprivileged user that owns $SRC."
	echo "Launch Install Config from the rEFInd_GUI app."
	exit 2
fi

ESP_TYPE_GUID=c12a7328-f81f-11d2-ba4b-00a0c93ec93b

# Temp mounts are recorded in a file, not a shell array: the resolver helpers
# run inside command substitutions, so an array appended there would be lost to
# the parent and the EXIT trap would unmount nothing, leaving removable ESPs
# mounted read-write.
CLEANUP_MOUNT_LIST="$(mktemp)"
STAGED_FILES=()
cleanup() {
	local m staged
	for staged in "${STAGED_FILES[@]}"; do
		[ -n "$staged" ] && rm -f -- "$staged" 2>/dev/null
	done
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
		# Read-only for the probe: this loop mounts EVERY ESP-typed partition
		# just to test whether rEFInd is on it, including attacker-supplied
		# removable media, and it runs as root. Only the ESP actually chosen as
		# the target is remounted writable (esp_make_writable below).
		if mount -o ro,nosuid,nodev,noexec "$dev" "$mp" 2>/dev/null; then
			printf '%s\n' "$mp" >> "$CLEANUP_MOUNT_LIST"
		else
			rmdir "$mp" 2>/dev/null
			return 1
		fi
	fi
	printf '%s\n' "$mp"
}

# Make the ESP behind $1 (a mount point or any path under one) writable, but
# only if it is one of our own read-only probe mounts. ESPs the system already
# had mounted are left exactly as they were.
esp_make_writable() {
	local path="$1" m
	[ -n "$path" ] || return 0
	[ -f "$CLEANUP_MOUNT_LIST" ] || return 0
	while read -r m; do
		[ -n "$m" ] || continue
		case "$path" in
			"$m" | "$m"/*)
				mount -o remount,rw "$m" 2>/dev/null
				return 0
				;;
		esac
	done < "$CLEANUP_MOUNT_LIST"
	return 0
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

esp_make_writable "$ESP"

if ! mkdir -p "$DEST" 2>/dev/null; then
	echo "Could not create $DEST -- the EFI System Partition may be mounted read-only."
	exit 4
fi

# A config is mandatory. Images are optional, but images alone must never make
# the helper report success while leaving an absent or empty live config.
if ! runuser -u "$RUN_USER" -- test -f "$SRC/refind.conf" 2>/dev/null \
	|| ! runuser -u "$RUN_USER" -- test -s "$SRC/refind.conf" 2>/dev/null; then
	echo "No non-empty refind.conf was found in $SRC."
	echo "Use Create Config in the GUI first."
	exit 6
fi

COPIED=0
declare -A STAGED
for f in refind.conf background.png os_icon1.png os_icon2.png os_icon3.png os_icon4.png; do
	# The existence test AND the content read both run as the invoking user,
	# never as root: $SRC is user-writable and this script needs no password,
	# so a root-privileged `cp` would follow a symlink or hardlink planted
	# under ~/.local and copy any root-readable file (/etc/shadow) onto the
	# ESP, which is world-readable once mounted. runuser can only read what
	# the user can.
	runuser -u "$RUN_USER" -- test -f "$SRC/$f" 2>/dev/null || continue
	stage="$(mktemp "$DEST/.${f}.new.XXXXXX")" || {
		echo "Could not create a staging file in $DEST -- the ESP may be full or read-only."
		exit 5
	}
	STAGED["$f"]="$stage"
	STAGED_FILES+=("$stage")
	if ! runuser -u "$RUN_USER" -- cat -- "$SRC/$f" > "$stage" 2>/dev/null; then
		echo "Failed while copying $f to $DEST -- the ESP may be full or read-only."
		exit 5
	fi
	if [ "$f" = refind.conf ] && [ ! -s "$stage" ]; then
		echo "The staged refind.conf is empty; the live config was not changed."
		exit 5
	fi
	COPIED=$((COPIED + 1))
done

if [ -z "${STAGED[refind.conf]:-}" ]; then
	echo "refind.conf disappeared while it was being staged; the live config was not changed."
	exit 6
fi

# Keep one rollback copy of the live config. Build it under a temporary name
# and rename it into place so a short backup write cannot corrupt .prev.
if [ -f "$DEST/refind.conf" ]; then
	backup_stage="$(mktemp "$DEST/.refind.conf.prev.new.XXXXXX")" || {
		echo "Could not stage the rollback copy in $DEST."
		exit 5
	}
	STAGED_FILES+=("$backup_stage")
	if ! cp -- "$DEST/refind.conf" "$backup_stage" 2>/dev/null \
		|| ! mv -f -- "$backup_stage" "$DEST/refind.conf.prev" 2>/dev/null; then
		echo "Could not preserve the previous refind.conf; the live config was not changed."
		exit 5
	fi
fi

# Publish assets first and the config last. Each rename stays on the ESP, so a
# failed staging copy never truncates an existing live file and a failed asset
# update cannot leave a new config referring to an incomplete set.
for f in background.png os_icon1.png os_icon2.png os_icon3.png os_icon4.png; do
	[ -n "${STAGED[$f]:-}" ] || continue
	if ! mv -f -- "${STAGED[$f]}" "$DEST/$f" 2>/dev/null; then
		echo "Failed while publishing $f; the live config was not changed."
		exit 5
	fi
done
if ! mv -f -- "${STAGED[refind.conf]}" "$DEST/refind.conf" 2>/dev/null; then
	echo "Failed while publishing refind.conf; the previous config is still active."
	exit 5
fi

# Flush to the ESP before any temporary mount goes away.
sync
echo "Installed $COPIED file(s) to $DEST"
[ -n "$HOW" ] && echo "(chosen as $HOW)"
exit 0
