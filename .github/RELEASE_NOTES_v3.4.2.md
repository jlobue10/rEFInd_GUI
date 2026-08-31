# v3.4.2 — Security hardening

This release remediates the actionable findings from a full defensive security
audit of both repos (`SECURITY_AUDIT_2026-08.md`). **The audit found no
unprivileged→root escalation on Linux** — the sudoers vector-locking, root-owned
systemd units, the `espops` privilege-drop and `SUDO_USER` validation, the
config-injection sanitizers, the EFI-load-option parser, and the NVRAM
fail-closed logic were all verified sound. The one genuinely exploitable issue
was a **Windows local privilege escalation** in the sibling SteamDeck_rEFInd
repo's legacy dual-boot-fix tooling (not present here).

The GUI and helper are released together, as always (version handshake).

## Fixed

### Backgrounds-randomizer symlink — both repos (espops, parity-locked)
`randomize.cpp` now `lstat`s the backgrounds directory and refuses a symlink or
non-directory before dropping privileges. Previously a `stat()` followed a
symlink, so on a multi-user system a user could point the final path component at
another user's directory and have the randomizer drop to that user and copy their
PNGs onto the shared ESP. Identical, byte-for-byte, in both repos.

### Rich-text result dialogs — both repos
Install Config / Install Themes result dialogs now render captured helper output
as `Qt::PlainText`. The output can contain ESP-derived strings (the
`refind.conf.origin` sidecar another product/user wrote onto the shared ESP), and
the default `Qt::AutoText` would render an embedded `<a href>` as a live,
clickable link inside a trusted success dialog.

### PATH-hijack hardening — Windows
`windows/uninstall_rEFInd.ps1` now resolves `mountvol`/`bcdedit` by absolute
`%SystemRoot%\System32` path rather than by name, so an elevated run can't be
redirected by a same-named binary planted earlier on the machine PATH.

### Download & install hardening
- `refind_install_Sourceforge.sh` verifies the downloaded rEFInd archive against a
  version-gated pinned SHA-256 (0.14.2 = `f0f90fcc…`) before extracting an EFI
  binary that boots pre-OS; a future `REFIND_VER` bump fails loudly rather than
  checking the wrong hash.
- `install-rEFInd-GUI.sh` validates the username before splicing it into the
  sudoers rule (`sed` metacharacters / rule widening); `visudo -cf` remains the
  backstop.
- `scan_esp.sh` now probe-mounts ESPs `ro,nosuid,nodev,noexec`.

### CI / supply chain
- `windows-release.yml` requires a valid Authenticode signature on `ISCC.exe`
  before building the installer that SignPath then signs.
- `rpm-release.yml` now stages `Source1` (the theme randomizer unit) so
  `rpmbuild`'s source pre-check passes — the RPM build was previously missing it.

## Known follow-ups (not in this release)
Documented with rationale in `SECURITY_AUDIT_2026-08.md`:
- UEFI driver downloads still use `releases/latest` — the URL deliberately tracks
  the in-development jlobue10 driver fork; pinning waits until it's upstreamed.
- Release-artifact signing (`SHA256SUMS`) and commit-pinned package sources.

## Version sync
`VERSION`, `mainwindow.cpp` (`APP_VERSION`), `CMakeLists.txt`, the manifest, the
Inno script, `PKGBUILD`, `rEFInd_GUI.spec` (`%changelog`), and `debian/changelog`
all bumped to 3.4.2.
