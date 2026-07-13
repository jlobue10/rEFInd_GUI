# rEFInd_GUI
A graphical setup and customization utility to use alongside rEFInd (work in progress)

## Installation (Currently Fedora / Nobara / Bazzite are supported. Others coming in a future update...)

```
curl -L https://github.com/jlobue10/rEFInd_GUI/raw/main/install-rEFInd-GUI.sh | sh
```

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

- The app requests **Administrator** rights at launch — everything it does (mounting the EFI System Partition via `mountvol`, `bcdedit`) needs them.
- **Install rEFInd** downloads rEFInd from SourceForge and points the Windows Boot Manager firmware entry at rEFInd (`bcdedit /set {bootmgr} path \EFI\refind\refind_x64.efi`). Your previous settings are saved to `%LOCALAPPDATA%\rEFInd_GUI\bootmgr-backup.txt`, and the revert command is printed after installation. It also fetches the [Xbox 360 controller driver](#controller-support-in-the-boot-menu) into `EFI\refind\drivers_x64`.
- The background randomizer runs as a Scheduled Task at logon instead of a systemd service.
- The SteamOS `firmware_bootnum` option is Linux-only (Windows has no `efibootmgr` equivalent).

## Automatic OS detection (new in 2.0.0)

The GUI now scans the EFI System Partition(s) at startup (and via the **Rescan OSes** button) to detect what is actually installed: Windows, SteamOS, and any Linux distro with a vendor directory under `/boot/efi/EFI/` (shim, GRUB, or systemd-boot). The Boot Option boxes are populated with the detected OSes, and the generated `refind.conf` uses the real loader paths found on disk. The old Linux Distro selection box is gone — no more picking your distro by hand.

## Bazzite specific information

Auto partitioning when installing Bazzite is supported with these manual boot stanzas.
If you've used the cloud recovery on ASUS ROG ALLY or the system image to install Windows on Legion Go (or kept it at default), the 'SYSTEM' or 'SYSTEM_DRV' label on Windows' EFI partition is now detected automatically (on any distro, not just Bazzite) and used for the `volume` line in the generated `refind.conf` file when Create Config is pressed.

If you need to uninstall rEFInd_GUI (for instance in order to re-run installation with a newer version), run:

```
sudo rpm-ostree uninstall rEFInd_GUI
```

Let that command finish and then either run `systemctl reboot` or reboot another way.

## Legion Go

Some simple logic has been added to default to 2560 x 1600 in the generated `refind.conf` file on a Legion Go device (1920 x 1200 on a Legion Go 2). This solves a portrait rotation issue.

## Controller support in the boot menu

Every rEFInd install path now drops the **[SkorionOS UsbXbox360Dxe](https://github.com/SkorionOS/UsbXbox360Dxe)** UEFI driver into rEFInd's `drivers_x64` folder on the ESP, so Xbox 360 / handheld gamepads (ASUS ROG Ally/Ally X, Legion Go, GPD, OneXPlayer, MSI Claw, 8BitDo, and 40+ others) can drive the rEFInd boot menu with mouse-emulation and key mappings. rEFInd auto-loads every driver it finds in `drivers_x64`, so nothing else is required.

The latest release of the driver is fetched at install time from `https://github.com/SkorionOS/UsbXbox360Dxe/releases/latest`, so you always get the newest build. Only the `.efi` is installed — on first boot the driver auto-creates its own config at `\EFI\Xbox360\config.ini` on the ESP, which you can edit to remap buttons/sticks. If the download fails (no network), the rEFInd install still completes; the driver is simply skipped.

## Secure boot considerations

User mileage may vary on this topic, but for handheld devices such as the ASUS ROG ALLY/ ALLY X and others, finding a way to dual boot your Linux distro of choice alongside Windows and using rEFInd is a nice quality of life improvement.

What I've done on my own personal ASUS ROG ALLY X is install Nobara (latest version as of now, 41) and rEFInd and then install **[sbctl](https://github.com/Foxboron/sbctl)** . `sbctl` makes secure boot installation and management nearly trivial.
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

Now sign any efi file (this includes `refind_x64.efi` and `fwbootmgr.efi` for Windows) that is involved with your system's boot process (recommend creating backup copies beforehand) with this command. I've saved this `sudo sbctl sign -s` as an alias --> `securesign`.

```
sudo sbctl sign -s object-to-be-signed
```

Replace the "object-to-be-signed" portion with the full path efi file(s) or Linux kernel to be signed. Remember to sign new kernels before trying to boot into them with secure boot enabled.

If you use the bundled Xbox 360 controller driver (see [Controller support in the boot menu](#controller-support-in-the-boot-menu)) with secure boot enabled, sign it too — the firmware verifies drivers rEFInd loads:

```
sudo sbctl sign -s /boot/efi/EFI/refind/drivers_x64/UsbXbox360Dxe.efi
```

Re-enable secure boot in BIOS, and enjoy the benefits of being able to play anti-cheat games in Windows and a fully functioning Linux distro, side-by-side without toggling the secure boot setting in BIOS.

## Misc.

This is basically a variation of my [SteamDeck_rEFInd](https://github.com/jlobue10/SteamDeck_rEFInd) repo with various improvements including generic username support, support for multiple Linux distros and installing the config file, icons and background PNGs without needing to type the password for `sudo` privileges.

More coming soon...
