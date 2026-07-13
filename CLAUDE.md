# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

A Qt (C++17) GUI for customizing and installing the rEFInd bootloader, aimed especially at dual-boot handhelds (ASUS ROG Ally/Ally X, Legion Go, Steam Deck). It builds for **both Linux and Windows** from the same sources. Linux install targets: Fedora/Nobara/Bazzite (RPM) and CachyOS (PKGBUILD); Windows: `windows/install-rEFInd-GUI.ps1` into `%LOCALAPPDATA%\rEFInd_GUI`.

## Build

The only compiled component lives in `GUI/src` (CMake, auto-detects Qt6 then Qt5). Linux:

```
cd GUI/src
mkdir -p build && cd build
cmake ..
make
```

Windows (MSYS2 UCRT64 shell — installed at `C:\msys64` on this machine; launch via `MSYSTEM=UCRT64 C:\msys64\usr\bin\bash.exe -lc "..."`):

```
cmake -G Ninja -S GUI/src -B <builddir>
cmake --build <builddir>
windeployqt6 <builddir>/rEFInd_GUI.exe   # plus MinGW/ICU DLLs for standalone use (ldd the exe)
```

**This machine's quirk:** Norton AV silently deletes `CMakeCache.txt` for build dirs inside `Documents` — configure into a build dir outside the repo (e.g. the scratchpad). The Windows exe embeds a `requireAdministrator` manifest (`rEFInd_GUI.manifest` via `rEFInd_GUI.rc` for MinGW, link flag for MSVC), so it triggers UAC on launch. Norton also MITMs TLS here; its root CA was exported into MSYS2's trust store (`/etc/pki/ca-trust/source/anchors/norton-root.pem`) — if pacman ever fails with certificate errors again, that's why, and `curl --ssl-no-revoke` is how to confirm a URL works past Norton's revocation break.

### Windows packaging

`windows/assemble-deploy.sh <exe> deploy` gathers the exe + Qt plugins (windeployqt) + the MinGW/ICU DLL closure (`windows/copydeps.sh`, an `ldd`-based best-effort copy) + `windows/*.ps1` + `icons/` + `backgrounds/` + a seed `GUI/refind.conf` into `deploy/` — the exact final runtime layout. `windows/rEFInd_GUI.iss` (Inno Setup) packages `deploy/` into a **per-user** installer targeting `%LOCALAPPDATA%\rEFInd_GUI`, which equals `Platform::dataDir()`, so install dir and runtime data dir are the same directory (no Program Files, no admin needed to install; the app elevates itself). `.github/workflows/windows-release.yml` runs the whole chain on a `v*` tag, and Authenticode-signs the exe, the `.ps1` scripts, and the installer via **SignPath Foundation** (free OSS signing) — two-stage: sign `deploy/` contents, then build and sign the installer. Signing is gated on the `SIGNPATH_ORGANIZATION_ID` repo variable, so the workflow still produces unsigned builds until SignPath is configured (`windows/SIGNING.md`). Build artifacts (`deploy/`, `build-*/`, `windows/Output/`) are gitignored.

There are no tests and no linter.

**Important:** both `rEFInd_GUI.spec` (`%prep`) and `PKGBUILD` (`build()`) do a fresh `git clone` of this repo from GitHub rather than using the local checkout. Package builds therefore only pick up changes after they are pushed to `main`.

## Releasing / version bumps

The version is duplicated in several places that must be kept in sync:

- `VERSION` (plain text, e.g. `2.0.0`) — fetched at runtime by the "Check For Update" button and compared component-wise (`QVersionNumber`) against `APP_VERSION`
- `GUI/src/mainwindow.cpp`: `static const char APP_VERSION[]` (also feeds the About box text)
- `GUI/src/CMakeLists.txt`: `project(rEFInd_GUI VERSION ...)`
- `GUI/src/rEFInd_GUI.manifest`: `assemblyIdentity version="X.Y.Z.0"` (four-part)
- `windows/rEFInd_GUI.iss`: `#define AppVersion`
- `rEFInd_GUI.spec`: `Version:` (plus a `%changelog` entry)
- `PKGBUILD`: `pkgver`

## Architecture

Platform-specific code is confined to three files; everything else is shared. Never build shell command strings from user input — external commands go through `QProcess` with argument lists, file copies through `QFile::copy`.

- **`OSDetector`** (interface in `osdetect.h`): `osdetect_common.cpp` holds the platform-neutral logic. `detect()` iterates **every** ESP in the partition list and scans each reachable one (`espScanRoot(part)` — platform-specific), so multi-ESP machines (Windows ESP on one disk, Linux ESP on another) are fully covered rather than just one "system" ESP. Every discovered entry is tagged with its ESP's **partition GUID** as `volume` (`espVolumeId`), so the generated `menuentry` boots regardless of which ESP rEFInd itself launched from. The per-ESP `EFI/` vendor-dir scan (loader preference shim → GRUB → systemd-boot; `bootmgfw.efi` → Windows, `steamcl.efi` → SteamOS) also renames a bare systemd-boot loader (`/EFI/systemd`) to the running distro when it's on that distro's own ESP (so CachyOS shows "CachyOS", not "systemd-boot"). Cross-volume rules in `assembleEntries` cover ESPs that couldn't be scanned (unmounted `SYSTEM`/`SYSTEM_DRV`-labelled Windows ESP → Windows by partition GUID; `BATOCERA`/`VTOYEFI`; removable ESPs by partition GUID). `osdetect_linux.cpp` enumerates via `lsblk -J` — **`NAME` must be in the `-o` column list** or lsblk (≥ 2.42) drops the disk→children tree and emits partitions flat; the parser walks recursively so either shape works. `MOUNTPOINT` is the pre-2.37 util-linux fallback for `MOUNTPOINTS`. It names the running distro from `/etc/os-release` `ID`/`ID_LIKE` (so `/EFI/fedora` shows "Nobara" on Nobara); an ESP mounted at `/boot`, `/boot/efi`, or `/efi` is the running system's. On Linux an ESP is scannable only where already mounted (mounting needs root, which detection avoids). `osdetect_win.cpp` enumerates via one PowerShell `Get-Partition`/`Get-Disk`/`Get-Volume` → JSON call and mounts the letterless system ESP with `mountvol <X>: /S` (unmounting after). Windows gotcha: letterless partitions serialize `DriveLetter` as a NUL char, not empty — hence the "is it actually a letter" check.
- **`Platform` namespace** (`platform.h/.cpp`, the only `#ifdef Q_OS_WIN` site): data dir (`~/.local/rEFInd_GUI` vs `%LOCALAPPDATA%\rEFInd_GUI` — same layout below it), installer/config-install/randomizer launches (xterm + .sh + systemd vs PowerShell + .ps1 + Scheduled Task), firmware_bootnum availability (Linux only; the checkbox is disabled on Windows), Install Source options (Windows has only "Sourceforge").
- **`MainWindow`** (`mainwindow.cpp`, platform-free): detection results feed the four Boot Option combos (`BootEntry` payloads via `QVariant`), refreshed by the Rescan button. `on_Create_Config_clicked` → `createBootStanza` writes `<dataDir>/GUI/refind.conf`, one data-driven stanza per non-None slot; `default_selection` is the chosen entry's position among generated stanzas. Legion Go (DMI/CIM board `LNVNB161216`) forces `resolution 2560 1600`. Selected PNGs are copied to canonical names (`background.png`, `os_icon1..4.png`).
- **Privileged actions, Linux**: rEFInd install scripts run in `xterm`; "Install Config" runs `sudo -n /etc/rEFInd/install_config_from_GUI.sh`, passwordless via the sudoers entry (`install_config_from_GUI` → `/etc/sudoers.d/`, root-owned 0440) that `install-rEFInd-GUI.sh` installs. **Windows**: the exe itself runs elevated (manifest), and actions run the `windows/*.ps1` scripts from the data dir; `install_rEFInd.ps1` repoints the `{bootmgr}` firmware entry via `bcdedit` (backup + revert command printed) — never run it casually on a dev machine.
- **Config install targets rEFInd's real ESP**, not just the conventionally-mounted one — critical on multi-ESP machines where rEFInd lives on the Windows ESP while the running distro mounts its own ESP at `/boot`. `install_config_from_GUI.sh` reads the rEFInd entry's partition GUID from `efibootmgr -v` (`HD(...,GPT,<guid>,...)`), resolves it via `blkid`/`lsblk`, and mounts it on a temp dir if it isn't already mounted (EXIT-trap unmount); it falls back to the old `/boot/efi → /efi → /boot` findmnt heuristic when there's no efibootmgr/NVRAM entry. `windows/install_config_from_GUI.ps1` scans ESPs (system ESP first) for the one that actually contains `EFI\refind\refind_x64.efi`, mounting letterless non-system ESPs via a temporary `Add-PartitionAccessPath`, and falls back to the system ESP.
- **Settings persistence**: `QSettings` INI at `<dataDir>/GUI/rEFInd_GUI.ini`, read on construct / written on destruct. Boot-option selections are stored by *text* (`BootOption0XText`), not index — combo contents are dynamic.

## Install-time file layout (context for editing the shell scripts)

`install-rEFInd-GUI.sh` is the user-facing entry point (curl | sh). It copies `GUI/`, `icons/`, `backgrounds/`, and the install scripts to `~/.local/rEFInd_GUI/`, builds/installs the RPM (or rpm-ostree layers a release RPM on Bazzite, or `makepkg -si` on CachyOS), and installs root-owned pieces to `/etc/rEFInd/` (the built binary, `install_config_from_GUI.sh`, `rEFInd_bg_randomizer.sh`).

The literal tokens `USER` and `HOME` inside `install_config_from_GUI`, `install_config_from_GUI.sh`, `rEFInd_GUI.desktop`, and `rEFInd_bg_randomizer.sh` are placeholders replaced by `sed` during installation — don't "fix" them to `$USER`/`$HOME`.

**Xbox 360 controller driver**: all three rEFInd install scripts (`refind_install_Sourceforge.sh`, `refind_install_package_mgr.sh`, `windows/install_rEFInd.ps1`) download the SkorionOS `UsbXbox360Dxe.efi` into the ESP's `EFI/refind/drivers_x64/` so gamepads work in the boot menu. The URL is version-agnostic (`.../releases/latest/download/UsbXbox360Dxe.efi`) so no version needs syncing; only the `.efi` is copied (the driver self-creates `\EFI\Xbox360\config.ini` on first boot). Download failure is non-fatal — it warns and continues. The package-manager script is the one that must `mkdir -p .../drivers_x64` first, since `refind-install` doesn't populate it. Keep this step in sync across all three scripts when editing.

`rEFInd_bg_randomizer.service` + `rEFInd_bg_randomizer.sh` implement the optional random-background-on-boot feature, toggled from the GUI via `systemctl enable/disable`.
