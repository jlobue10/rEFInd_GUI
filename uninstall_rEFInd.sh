#!/bin/bash
# Fully removes the Linux-side rEFInd install -- the counterpart of
# windows/uninstall_rEFInd.ps1:
#   - deletes the rEFInd boot entries that target this distro's ESP (rEFInd
#     entries pointing at another ESP -- e.g. a Windows-side install -- are
#     reported and left alone)
#   - re-activates the Windows boot entry the installer deactivated
#   - removes EFI/refind and EFI/Xbox360 from the ESP, plus refind-install's
#     /boot/refind_linux.conf (pass --keep-esp-files to keep the files and
#     only undo the boot entries)
#   - disables the rEFInd_bg_randomizer service
#   - with --remove-app, also removes the rEFInd_GUI app itself (package,
#     ~/.local/rEFInd_GUI, /etc/rEFInd, the sudoers rule, desktop entries)
#
# Usage: sudo ./uninstall_rEFInd.sh [--keep-esp-files] [--remove-app]

KEEP_ESP_FILES=0
REMOVE_APP=0
for arg in "$@"; do
	case "$arg" in
		--keep-esp-files) KEEP_ESP_FILES=1 ;;
		--remove-app) REMOVE_APP=1 ;;
		-h|--help)
			sed -n '2,16p' "$0" | sed 's/^# \{0,1\}//'
			exit 0
			;;
		*)
			echo "Unknown option: $arg (try --help)" >&2
			exit 1
			;;
	esac
done

if [ "$(id -u)" -ne 0 ]; then
	exec sudo -- "$0" "$@"
fi

# Under sudo, $HOME is root's; resolve the invoking user's home for the app
# data directory and desktop shortcut.
REAL_USER="${SUDO_USER:-$USER}"
REAL_HOME="$(getent passwd "$REAL_USER" | cut -d: -f6)"
[ -z "$REAL_HOME" ] && REAL_HOME="$HOME"

echo "Removing the Linux-side rEFInd install..."

# Snapshot the current NVRAM boot entries before changing them: a copy on
# disk makes manual recovery trivial if anything goes sideways.
NVRAM_BK_DIR="$REAL_HOME/.local/rEFInd_GUI/nvram-backups"
if mkdir -p "$NVRAM_BK_DIR" 2>/dev/null; then
	efibootmgr -v > "$NVRAM_BK_DIR/efibootmgr-$(date +%Y%m%d-%H%M%S).txt" 2>/dev/null
	chown -R "$REAL_USER" "$NVRAM_BK_DIR" 2>/dev/null
	# Keep the ten most recent snapshots.
	ls -1t "$NVRAM_BK_DIR"/efibootmgr-*.txt 2>/dev/null | tail -n +11 | xargs -r rm -f
fi

# Resolve this distro's ESP the same way the install scripts do: the FAT
# filesystem containing /boot/efi, /efi, or /boot, preferring one that
# already holds an EFI/refind install.
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

ESP_PARTUUID=""
if [ -n "$ESP_MP" ]; then
	ESP_DEV="$(findmnt -no SOURCE "$ESP_MP" 2>/dev/null | grep -m1 "^/dev/")"
	[ -n "$ESP_DEV" ] && ESP_PARTUUID="$(lsblk -rno PARTUUID "$ESP_DEV" 2>/dev/null | head -1 | tr 'A-F' 'a-f')"
fi
if [ -z "$ESP_MP" ] || [ -z "$ESP_PARTUUID" ]; then
	echo "Warning: could not resolve this distro's EFI System Partition;" >&2
	echo "skipping boot entry and ESP file cleanup." >&2
fi

# 1. Delete rEFInd boot entries that target this ESP; report entries pointing
# elsewhere (a Windows-side install) and leave them alone. Match by loader
# path (\EFI\refind\refind*.efi) so both "rEFInd" and refind-install's
# "rEFInd Boot Manager" labels are covered. An entry whose loader matches but
# whose partition GUID differs belongs to another install -- use the Windows
# app's uninstaller for a Windows-side one.
REMOVED_ENTRIES=""
FOREIGN_ENTRIES=""
if [ -n "$ESP_PARTUUID" ] && command -v efibootmgr >/dev/null 2>&1; then
	while IFS= read -r line; do
		num="$(printf '%s\n' "$line" | sed -nE 's/^Boot([0-9A-Fa-f]{4})\*?[[:space:]].*/\1/p')"
		[ -n "$num" ] || continue
		printf '%s\n' "$line" | grep -qiE '\\refind\\refind[^\\]*\.efi' || continue
		label="$(printf '%s\n' "$line" | sed -E 's/^Boot[0-9A-Fa-f]{4}\*?[[:space:]]+//; s/[[:space:]]*(HD\(|\t).*$//')"
		entry_uuid="$(printf '%s\n' "$line" | grep -oiE 'GPT,[0-9a-fA-F-]{36}' | head -1 | cut -d, -f2 | tr 'A-F' 'a-f')"
		if [ "$entry_uuid" = "$ESP_PARTUUID" ]; then
			if efibootmgr -b "$num" -B >/dev/null 2>&1; then
				echo "Deleted boot entry Boot$num ('$label')."
				REMOVED_ENTRIES="$REMOVED_ENTRIES Boot$num"
			else
				echo "Warning: could not delete boot entry Boot$num ('$label')." >&2
			fi
		else
			FOREIGN_ENTRIES="$FOREIGN_ENTRIES Boot$num('$label')"
		fi
	done < <(efibootmgr -v 2>/dev/null)
	if [ -n "$FOREIGN_ENTRIES" ]; then
		echo "Left untouched (rEFInd on another ESP, e.g. a Windows-side install):$FOREIGN_ENTRIES"
	fi
fi

# 2. Re-activate the Windows boot entry the installer deactivated (inactive
# entries print without the '*' after Boot####).
if command -v efibootmgr >/dev/null 2>&1; then
	while read -r _num; do
		if efibootmgr -b "$_num" -a >/dev/null 2>&1; then
			echo "Re-activated the Windows boot entry Boot$_num."
		else
			echo "Warning: could not re-activate the Windows boot entry Boot$_num." >&2
		fi
	done < <(efibootmgr | sed -nE 's/^Boot([0-9A-Fa-f]{4})[[:space:]]+Windows.*/\1/p')
fi

# 3. Remove rEFInd's files (and the Xbox 360 driver's config dir) from the
# ESP, plus the kernel-options file refind-install drops in /boot.
if [ "$KEEP_ESP_FILES" -eq 0 ] && [ -n "$ESP_MP" ]; then
	for d in "$ESP_MP/EFI/refind" "$ESP_MP/EFI/Xbox360"; do
		if [ -d "$d" ]; then
			rm -rf "$d"
			echo "Removed ${d#"$ESP_MP"/} from the EFI System Partition."
		fi
	done
	if [ -f /boot/refind_linux.conf ]; then
		rm -f /boot/refind_linux.conf
		echo "Removed /boot/refind_linux.conf."
	fi
fi

# 4. Disable the background randomizer service, if enabled.
if systemctl list-unit-files rEFInd_bg_randomizer.service >/dev/null 2>&1; then
	if systemctl is-enabled rEFInd_bg_randomizer.service >/dev/null 2>&1; then
		systemctl disable --now rEFInd_bg_randomizer.service >/dev/null 2>&1
		echo "Disabled the rEFInd_bg_randomizer service."
	fi
fi

# 5. Optionally remove the GUI app itself (the Linux analog of uninstalling
# "rEFInd GUI" from Windows Settings > Apps).
if [ "$REMOVE_APP" -eq 1 ]; then
	echo "Removing the rEFInd_GUI app..."
	if [ -f /etc/bazzite/image_name ] && rpm-ostree status 2>/dev/null | grep -q rEFInd_GUI; then
		rpm-ostree uninstall rEFInd_GUI
		echo "NOTE: reboot to finish applying the rpm-ostree change."
	elif command -v dnf >/dev/null 2>&1 && rpm -q rEFInd_GUI >/dev/null 2>&1; then
		dnf remove -y rEFInd_GUI
	elif command -v pacman >/dev/null 2>&1 && pacman -Qq rEFInd_GUI >/dev/null 2>&1; then
		pacman -R --noconfirm rEFInd_GUI
	elif command -v dpkg >/dev/null 2>&1 && dpkg -s refind-gui >/dev/null 2>&1; then
		apt-get remove -y refind-gui
	else
		echo "No installed rEFInd_GUI package found; removing files only."
	fi
	rm -rf "$REAL_HOME/.local/rEFInd_GUI"
	rm -f /etc/sudoers.d/zz_install_config_from_GUI /etc/sudoers.d/install_config_from_GUI
	rm -rf /etc/rEFInd
	rm -f /usr/share/applications/rEFInd_GUI.desktop "$REAL_HOME/Desktop/refind_GUI.desktop"
	echo "Removed the app data, /etc/rEFInd, the sudoers rule, and desktop entries."
fi

# Summary, read back from live NVRAM.
echo
echo "==================== Uninstall summary ===================="
if command -v efibootmgr >/dev/null 2>&1; then
	efibootmgr
	echo "------------------------------------------------------------"
fi
if [ -n "$REMOVED_ENTRIES" ]; then
	echo "Removed entries:$REMOVED_ENTRIES"
else
	echo "No rEFInd boot entries for this distro's ESP were present."
fi
echo "Done."
