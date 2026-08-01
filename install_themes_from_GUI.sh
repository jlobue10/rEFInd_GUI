#!/bin/bash
# Installs the GUI data-dir themes tree onto the EFI system partition that
# firmware actually launches rEFInd from: <data dir>/themes/<name>/ is copied
# to EFI/refind/themes/<name>/, where the include line the GUI appends to
# refind.conf (and every theme.conf's own asset paths) expect it. Runs as
# root via a sudoers rule; the home directory below is filled in at install
# time.
#
# Everything printed here is captured by the GUI and shown in its result
# dialog, so keep the output short and human-readable.
SRC="HOME/.local/rEFInd_GUI/themes"

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
	echo "Launch Install Themes from the rEFInd_GUI app."
	exit 2
fi

ESP_TYPE_GUID=c12a7328-f81f-11d2-ba4b-00a0c93ec93b

# Temp mounts are recorded in a file, not a shell array: the resolver helpers
# run inside command substitutions, so an array appended there would be lost to
# the parent and the EXIT trap would unmount nothing, leaving removable ESPs
# mounted read-write.
CLEANUP_MOUNT_LIST="$(mktemp)"
STAGED_DIRS=()
cleanup() {
	local m staged
	for staged in "${STAGED_DIRS[@]}"; do
		[ -n "$staged" ] && rm -rf -- "$staged" 2>/dev/null
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

DEST="$ESP/EFI/refind/themes"

esp_make_writable "$ESP"

if ! mkdir -p "$DEST" 2>/dev/null; then
	echo "Could not create $DEST -- the EFI System Partition may be mounted read-only."
	exit 4
fi

# Best-effort sweep of staging/backup directories left behind by an earlier
# interrupted run (the EXIT trap cannot run across SIGKILL or power loss, and
# every run mints new random staging names, so leftovers would otherwise
# accumulate on the small ESP). Only the exact ".<name>.new.<suffix>" /
# ".<name>.old.<suffix>" shapes created below are matched, so no live theme
# can be touched.
for stale in "$DEST"/.*.new.* "$DEST"/.*.old.*; do
	[ -e "$stale" ] && rm -rf -- "$stale" 2>/dev/null
done

# The themes each user can install: every $SRC/<name>/theme.conf that exists
# non-empty -- the same validity rule the GUI's Theme box applies. All reads
# run as the invoking user (see the copy loop for why).
THEMES=()
while IFS= read -r -d '' dir; do
	name="$(basename "$dir")"
	# Names starting with a dot would collide with this script's staging
	# pattern (and are junk on an ESP anyway).
	case "$name" in .*) continue ;; esac
	runuser -u "$RUN_USER" -- test -s "$SRC/$name/theme.conf" 2>/dev/null || continue
	THEMES+=("$name")
done < <(runuser -u "$RUN_USER" -- find "$SRC" -mindepth 1 -maxdepth 1 -type d -print0 2>/dev/null)

if [ "${#THEMES[@]}" -eq 0 ]; then
	echo "No themes were found in $SRC."
	echo "Each theme must be a folder containing a non-empty theme.conf."
	exit 3
fi

# Free-space check before anything is copied: the bundled set is ~12 MB and a
# replace briefly keeps old and new copies of one theme side by side, so
# require the full new size plus 4 MB of headroom. df -P for portable output.
NEED_KB="$(runuser -u "$RUN_USER" -- du -sk "$SRC" 2>/dev/null | cut -f1)"
NEED_KB=$((${NEED_KB:-0} + 4096))
FREE_KB="$(df -Pk "$DEST" 2>/dev/null | awk 'NR==2 {print $4}')"
if [ -n "$FREE_KB" ] && [ "$FREE_KB" -lt "$NEED_KB" ]; then
	echo "Not enough free space on the EFI System Partition: ${NEED_KB} KB needed, ${FREE_KB} KB free."
	echo "Nothing was copied."
	exit 7
fi

# Per-theme staged replace: each theme directory is fully copied to a hidden
# staging directory on the ESP first, then swapped in (live -> .old, staged ->
# live, delete .old). Directory renames stay on the ESP, so a failure at any
# point leaves the theme either fully old (rolled back below) or fully new --
# never a half-copied mix that a theme.conf could reference. Publishing
# per-theme (rather than one giant swap) keeps the peak space cost at one
# extra theme instead of the whole tree.
INSTALLED=0
for name in "${THEMES[@]}"; do
	stage="$DEST/.$name.new.$$"
	rm -rf -- "$stage" 2>/dev/null
	STAGED_DIRS+=("$stage")
	if ! mkdir -p "$stage" 2>/dev/null; then
		echo "Could not create a staging directory in $DEST -- the ESP may be full or read-only."
		exit 5
	fi
	# Every file is read as the invoking user, never as root: $SRC is
	# user-writable and this script needs no password, so a root-privileged
	# recursive copy would follow a symlink or hardlink planted under
	# ~/.local and copy any root-readable file (/etc/shadow) onto the ESP,
	# which is world-readable once mounted. runuser + cat can only read what
	# the user can, and writing through cat creates plain files on the FAT
	# target no matter what the source was.
	COPY_FAILED=0
	while IFS= read -r -d '' file; do
		rel="${file#"$SRC/$name/"}"
		case "$rel" in */*) mkdir -p "$stage/$(dirname "$rel")" 2>/dev/null ;; esac
		if ! runuser -u "$RUN_USER" -- cat -- "$file" > "$stage/$rel" 2>/dev/null; then
			COPY_FAILED=1
			break
		fi
	done < <(runuser -u "$RUN_USER" -- find "$SRC/$name" -type f -print0 2>/dev/null)
	if [ "$COPY_FAILED" -ne 0 ]; then
		echo "Failed while staging theme '$name' -- the ESP may be full."
		echo "The previously installed themes were not changed."
		exit 5
	fi
	if ! runuser -u "$RUN_USER" -- test -s "$SRC/$name/theme.conf" 2>/dev/null \
		|| [ ! -s "$stage/theme.conf" ]; then
		echo "Theme '$name' lost its theme.conf while being staged; nothing was changed."
		exit 5
	fi
	# Swap: keep the old copy until the new one is in place, then drop it.
	old="$DEST/.$name.old.$$"
	if [ -d "$DEST/$name" ]; then
		if ! mv -f -- "$DEST/$name" "$old" 2>/dev/null; then
			echo "Could not move the old copy of theme '$name' aside; it was left as it was."
			exit 5
		fi
	fi
	if ! mv -f -- "$stage" "$DEST/$name" 2>/dev/null; then
		# Restore the old copy so the theme is never left missing.
		[ -d "$old" ] && mv -f -- "$old" "$DEST/$name" 2>/dev/null
		echo "Failed while publishing theme '$name'; its previous version was kept."
		exit 5
	fi
	rm -rf -- "$old" 2>/dev/null
	INSTALLED=$((INSTALLED + 1))
done

# Flush to the ESP before any temporary mount goes away.
sync
echo "Installed $INSTALLED theme(s) to $DEST"
[ -n "$HOW" ] && echo "(chosen as $HOW)"
exit 0
