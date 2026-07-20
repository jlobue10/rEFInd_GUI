# rEFInd_GUI
A graphical setup and customization utility to use alongside rEFInd (work in progress)

## Installation on Linux

Supported distros: **Fedora / Nobara / Bazzite** (RPM), **CachyOS / Arch** (pacman), and **Debian / Ubuntu** (deb).

```
curl -L https://github.com/jlobue10/rEFInd_GUI/raw/main/install-rEFInd-GUI.sh | sh
```

The installer prefers the prebuilt package from the latest release and falls back to building locally.

## Uninstalling (Linux)

To remove rEFInd itself (boot entry, ESP files, background randomizer service), run:

```
sudo ~/.local/rEFInd_GUI/uninstall_rEFInd.sh
```

It deletes the rEFInd boot entries that target your distro's ESP (a rEFInd installed from the Windows side of a dual-boot machine is detected and left alone — use the Windows app's uninstaller for that one), re-activates the Windows boot entry the installer deactivated, and removes `EFI/refind`, `EFI/Xbox360`, and `/boot/refind_linux.conf`. Flags:

- `--keep-esp-files` — undo only the boot entries; keep rEFInd's files on the ESP
- `--remove-app` — also remove the rEFInd_GUI app itself: the distro package (`rEFInd_GUI` / `refind-gui`, via dnf, pacman, apt, or rpm-ostree on Bazzite — the rpm-ostree change needs a reboot to finish), `~/.local/rEFInd_GUI`, `/etc/rEFInd`, the sudoers rule, and the desktop entries

## Windows support (new in 2.0.0)

rEFInd_GUI also builds and runs on Windows (Qt 6), so you can configure and install rEFInd from the Windows side of a dual-boot machine — handy on a fresh handheld before Linux is even installed.

### Installing (recommended)

Download **`rEFInd_GUI-<version>-setup.exe`** from the [Releases](https://github.com/jlobue10/rEFInd_GUI/releases) page and run it. It's a per-user install (no admin prompt to install; the app itself requests Administrator when you launch it), and it creates Start Menu and optional Desktop shortcuts. A portable ZIP is also attached to each release if you prefer not to install.

### Building from source

In an MSYS2 UCRT64 shell:

```
pacman -S --needed mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja mingw-w64-ucrt-x86_64-qt6-base mingw-w64-ucrt-x86_64-qt6-tools
cmake -G Ninja -S GUI/src -B build-win
cmake --build build-win
bash windows/assemble-deploy.sh build-win/rEFInd_GUI.exe deploy   # gathers the exe + all runtime DLLs + data into deploy/
```

To produce the installer, compile `windows/rEFInd_GUI.iss` with [Inno Setup](https://jrsoftware.org/isinfo.php) (`ISCC.exe windows\rEFInd_GUI.iss`) — the output lands in `windows/Output/`. Pushing a `v*` tag builds the installer and portable ZIP automatically via GitHub Actions and attaches them to the release.

Release builds are Authenticode-signed (executable, installer, and PowerShell scripts) through [SignPath Foundation](https://signpath.org/)'s free OSS code-signing program; see [windows/SIGNING.md](windows/SIGNING.md) for the one-time setup. Until that's configured, CI still produces working but unsigned artifacts, and Windows will show an "unknown publisher" prompt.

Windows notes:

- The app requests **Administrator** rights at launch — everything it does (mounting the EFI System Partition, writing firmware boot variables) needs them.
- **Install rEFInd** (v2.3.0+) downloads rEFInd from SourceForge and creates a dedicated `rEFInd` firmware boot entry — the exact equivalent of `efibootmgr -c` on Linux — first in the boot order. **Windows Boot Manager is left untouched** (rEFInd chainloads it); versions before 2.3.0 repointed `{bootmgr}` instead, which made the entry carry Windows' optional-data blob (the "long hex after refind_x64.efi" in `efibootmgr -v`) — installing 2.3.0+ over an old version restores `{bootmgr}` automatically. Previous boot settings are saved to `%LOCALAPPDATA%\rEFInd_GUI\bootmgr-backup.txt`. The installer also fetches the [Xbox 360 controller driver](#controller-support-in-the-boot-menu) into `EFI\refind\drivers_x64` — and, on the ROG Xbox Ally / Ally X and Steam Decks, the [touchscreen driver](#touchscreen-support-in-the-boot-menu) too.
- **Uninstalling** "rEFInd GUI" from Settings > Apps asks whether to also remove rEFInd itself; choosing Yes deletes the rEFInd boot entry, removes `EFI\refind` and `EFI\Xbox360` from the ESP, restores direct Windows boot, unregisters the background randomizer task, and scrubs `%LOCALAPPDATA%\rEFInd_GUI`. A rEFInd installed from the Linux side (on another ESP) is detected and left alone. The same cleanup can be run standalone as Administrator: `powershell -ExecutionPolicy Bypass -File "%LOCALAPPDATA%\rEFInd_GUI\windows\uninstall_rEFInd.ps1"` (add `-KeepEspFiles` to keep rEFInd's files).
- The background randomizer runs as a Scheduled Task at logon instead of a systemd service.
- The SteamOS `firmware_bootnum` option is Linux-only (Windows has no `efibootmgr` equivalent).

## Automatic OS detection (new in 2.0.0)

The GUI scans **every reachable EFI System Partition** at startup (and via the **Rescan OSes** button) to detect what is actually installed: Windows, SteamOS, and any Linux distro with a vendor directory under `EFI/` (shim, GRUB, or systemd-boot). The Boot Option boxes are populated with the detected OSes, and the generated `refind.conf` uses the real loader paths and volumes found on disk — multi-ESP machines (e.g. Windows and Linux each with their own ESP) are fully covered. Since 2.2.0 the Windows build also mounts and scans letterless Linux ESPs, so a distro like CachyOS booting via systemd-boot is detected from the Windows side and named after its boot entries rather than a generic "systemd-boot". **Install Config** and the background randomizer write to the ESP that the firmware's rEFInd boot entry actually points at, so a stale rEFInd copy on another ESP can't swallow your config.

## Bazzite specific information

Auto partitioning when installing Bazzite is supported with these manual boot stanzas.
If you've used the cloud recovery on ASUS ROG ALLY or the system image to install Windows on Legion Go (or kept it at default), the 'SYSTEM' or 'SYSTEM_DRV' label on Windows' EFI partition is now detected automatically (on any distro, not just Bazzite) and used for the `volume` line in the generated `refind.conf` file when Create Config is pressed.

If you need to uninstall rEFInd_GUI on Bazzite (for instance in order to re-run installation with a newer version), run `sudo rpm-ostree uninstall rEFInd_GUI` and reboot — or use `uninstall_rEFInd.sh --remove-app` (see [Uninstalling](#uninstalling-linux)), which handles the rpm-ostree case too.

## Display resolution defaults

The generated `refind.conf` picks a sensible `resolution` line per device:

- **Legion Go**: `2560 1600`, **Legion Go 2**: `1920 1200` — some of these panels are portrait-native, and forcing landscape solves a rotation issue.
- **ROG Xbox Ally / Ally X**: `1920 1080` (the numbered video mode 3 picks the wrong mode on these).
- **Any other device** (since 2.2.0): the built-in panel's **native resolution**, read from EDID/DRM — the internal display is preferred over external outputs, so a docked handheld still gets its own screen. Only when no panel can be detected does the config fall back to rEFInd's numbered mode 3.

## Controller support in the boot menu

Every rEFInd install path now drops the **[SkorionOS UsbXbox360Dxe](https://github.com/SkorionOS/UsbXbox360Dxe)** UEFI driver into rEFInd's `drivers_x64` folder on the ESP, so Xbox 360 / handheld gamepads (ASUS ROG Ally/Ally X, Legion Go, Legion Go 2, GPD, OneXPlayer, MSI Claw, 8BitDo, and 40+ others) can drive the rEFInd boot menu with mouse-emulation and key mappings. rEFInd auto-loads every driver it finds in `drivers_x64`, so nothing else is required.

The latest release of the driver is fetched at install time from `https://github.com/jlobue10/UsbXbox360Dxe/releases/latest` (temporarily a fork of the SkorionOS driver that adds Legion Go 2 controller support — [upstream PR #6](https://github.com/SkorionOS/UsbXbox360Dxe/pull/6) — plus an ASUS Ally lockup fix and a right-stick fix so both sticks move the cursor, confirmed working on a Legion Go 2 in [issue #23](https://github.com/jlobue10/rEFInd_GUI/issues/23); the source will switch back to SkorionOS once **all** of those are merged and released upstream), so you always get the newest build. Only the `.efi` is installed — on first boot the driver auto-creates its own config at `\EFI\Xbox360\config.ini` on the ESP, which you can edit to remap buttons/sticks. If the download fails (no network), the rEFInd install still completes; the driver is simply skipped.

## Touchscreen support in the boot menu

On the **ROG Xbox Ally, Xbox Ally X** (since 2.3.6), **Steam Deck OLED** (since 2.4.2) and **Steam Deck LCD** (since 2.6.0), every rEFInd install path also drops the **[TouchI2cDxe](https://github.com/jlobue10/TouchI2cDxe)** UEFI driver (formerly AllyTouchI2cDxe) into `drivers_x64`, making the built-in touchscreen work in the rEFInd boot menu — tap to move the selection and launch entries. The touchscreen on these devices is HID-over-I2C (a Novatek panel on the Ally, a FocalTech panel on both Steam Decks, all on the SoC's I2C bus), which a USB driver structurally cannot see; TouchI2cDxe speaks HID-over-I2C directly and feeds rEFInd's native touch support (`EFI_ABSOLUTE_POINTER_PROTOCOL`). Confirmed working on an Xbox Ally X and both Steam Deck models (LCD and OLED). On the Steam Decks the driver also rotates the panel's portrait touch matrix onto rEFInd's landscape screen mode.

Other devices are unaffected — the driver is only installed when an Xbox Ally / Ally X (DMI board `RC73XA`/`RC73YA`) or Steam Deck (DMI product `Galileo` for OLED, `Jupiter` for LCD) is detected. Like the controller driver, it's fetched from `releases/latest` at install time, and a failed download only skips it. Installs that previously received `AllyTouchI2cDxe.efi` get it replaced by `TouchI2cDxe.efi` on the next rEFInd install.

## Secure boot considerations

User mileage may vary on this topic, but for handheld devices such as the ASUS ROG ALLY/ ALLY X and others, finding a way to dual boot your Linux distro of choice alongside Windows and using rEFInd is a nice quality of life improvement.

What I've done on my own personal ASUS ROG ALLY X in the past (I've since switched to CachyOS) is to install Nobara and rEFInd and then install **[sbctl](https://github.com/Foxboron/sbctl)** . `sbctl` makes secure boot installation and management nearly trivial.
For Fedora based distros such as Nobara, run these steps to get `sbctl` installed.

```
sudo dnf copr enable chenxiaolong/sbctl fedora-41-x86_64
sudo dnf install sbctl
```

Afterwards, go into BIOS and enter the secure boot setup mode (will delete existing keys). Reboot into your Linux distro and run these commands.

```
sudo sbctl create-keys
sudo sbctl enroll-keys --microsoft
```

Now sign any efi file (this includes `refind_x64.efi` and `bootmgfw.efi` for Windows) that is involved with your system's boot process (recommend creating backup copies beforehand) with this command. I've saved this `sudo sbctl sign -s` as an alias --> `securesign`.

```
sudo sbctl sign -s object-to-be-signed
```

Replace the "object-to-be-signed" portion with the full path efi file(s) or Linux kernel to be signed. Remember to sign new kernels before trying to boot into them with secure boot enabled.

If you use the bundled Xbox 360 controller driver (see [Controller support in the boot menu](#controller-support-in-the-boot-menu)) with secure boot enabled, sign it too — the firmware verifies drivers rEFInd loads:

```
sudo sbctl sign -s /boot/efi/EFI/refind/drivers_x64/UsbXbox360Dxe.efi
```

The same goes for the touchscreen driver (see [Touchscreen support in the boot menu](#touchscreen-support-in-the-boot-menu)) if it was installed:

```
sudo sbctl sign -s /boot/efi/EFI/refind/drivers_x64/TouchI2cDxe.efi
```

Re-enable secure boot in BIOS, and enjoy the benefits of being able to play anti-cheat games in Windows and a fully functioning Linux distro, side-by-side without toggling the secure boot setting in BIOS.

On **CachyOS** (which manages Secure Boot with sbctl out of the box — see the [CachyOS Secure Boot setup guide](https://wiki.cachyos.org/configuration/secure_boot_setup/)) the rEFInd install scripts detect enabled Secure Boot and run `sbctl-batch-sign` automatically after installing, so the freshly written EFI binaries are signed without any manual steps.

## Troubleshooting

### Browse dialog shows no PNG previews / no "view as icons" option (KDE Plasma)

The app requests the desktop's **native** file dialog, and KDE only supplies
that (with previews and view options) when the Qt **platform-integration**
plugin matching the GUI's Qt version is installed — otherwise Qt silently falls
back to a bare built-in dialog that has neither. Since v2.3.4 the Linux
packages build against **Qt6**, whose KDE integration ships by default on
Plasma 6 distros (CachyOS, Fedora/Nobara, Bazzite), so this works out of the
box — if you still see the bare dialog, update to v2.3.4 or newer.

On an older Qt5 build (check `ldd /etc/rEFInd/rEFInd_GUI` for `libQt5Widgets`),
install the Qt5 KDE integration (`plasma5-integration`, where the distro still
ships it) — or just update. GNOME gives the same result via its platform theme
(`qgnomeplatform` + `xdg-desktop-portal-gtk`).

## Misc.

This is basically a variation of my [SteamDeck_rEFInd](https://github.com/jlobue10/SteamDeck_rEFInd) repo with various improvements including generic username support, support for multiple Linux distros and installing the config file, icons and background PNGs without needing to type the password for `sudo` privileges.

## Acknowledgements

This project is only intended to simplify the installation and configuration of the rEFInd boot manager — including some Secure Boot setup support (CachyOS with sbctl signing). All credit for the rEFInd boot manager itself goes to **Roderick W. Smith** ([rodsbooks.com/refind](https://www.rodsbooks.com/refind/)); rEFInd performs all of the complicated bootloader tasks.
