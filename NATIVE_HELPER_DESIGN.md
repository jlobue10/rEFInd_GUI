# Native helper consolidation — design

Status: **Linux §7.1 and Windows §7.2 hardware-verified; §7.3 cleanup done
in both repos. What is left before release: the version bump, the reboot
checks, the Deck-side Linux run, and the SignPath artifact-config edit.**
§7.2 hardware QA (Windows 11 desktop, single ESP, 2026-08-14): all three
checks pass against the live ESP. **Install Config / Install Themes**
resolve NVRAM-first to the ESP in the firmware's rEFInd entry
(`\\?\Volume{f7633b6f-…}` = disk 4 partition 2) and publish for real —
the ESP's `refind.conf` came out byte-identical to the staged one, with
`refind.conf.prev` written, images updated in the same transaction, seven
theme trees installed, no `.new.*` staging left behind, and no orphaned
`espops-*` mount points. **Scheduled Task registration** (native COM)
produces exactly the wrappers' settings — action = helper exe +
subcommand, `HighestAvailable`, `InteractiveToken`, LogonTrigger,
DisallowStartIfOnBatteries/StopIfGoingOnBatteries false,
StartWhenAvailable true, IgnoreNew, PT5M — re-registration is idempotent
(CREATE_OR_UPDATE, still one task), unregister removes it, and a second
unregister is a no-op. **`bootnext`** set BootNext to Boot0002, the
rEFInd entry (verified against an independent NVRAM walk, then restored
to its pre-QA unset state). The `randomize-theme` payload was exercised
end-to-end against the real ESP: six runs, five distinct themes, rc 0
each, anti-repeat holding, no staging residue. Reboot-into-rEFInd is the
one item left to the owner (it needs an actual reboot).
Caveat: this desktop has a **single ESP**, so the multi-ESP
stale-shadow case — the one that motivated NVRAM-first resolution — could
not be exercised here; it still wants a run on the ROG Xbox Ally X.
Two QA-only defects were found and fixed in the process: the
letterless-ESP temp mount point leaked into user-facing messages
("Installed 4 file(s) to C:/…/Temp/espops-ERRTzD/EFI/refind", which reads
as "it installed into Temp"), now presented as "EFI\refind on the target
ESP" by the Windows caller in `platform.cpp` (presentation stays out of
the parity-locked espops files); the themes variant substitutes the
`/themes` path first so it doesn't render a mixed tail.
§7.2 compile pass (Windows 11 / MSYS2 UCRT64, 2026-08-14): both
`rEFInd_GUI.exe` and `rEFInd_GUI_helper.exe` build warning-free after four
fixes: (1) `wintasks.cpp` passed a BSTR where `RegisterTaskDefinition`
takes a VARIANT sddl — a VT_EMPTY VARIANT serves for userId/password/sddl;
(2) MinGW resolves `CLSID_TaskScheduler`/`IID_ITaskService`/
`IID_IExecAction` from `libtaskschd.a`, not libuuid — `taskschd` added to
espops' link libraries; (3) `std::rename` on Windows (UCRT) refuses to
overwrite an existing destination, so staged publishes silently failed
with exit 5 — `publishRename` (now shared in `userio.cpp`, used by
configinstall and the randomizers) goes through `MoveFileExW(...,
MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)` on Windows;
(4) `QTemporaryFile` keeps its native handle open even after `close()`
(name reservation) and Windows cannot rename an open file — the
config-backup block and the randomizers' `publishStaged` now destruct the
QTemporaryFile before publishing. The four platform-neutral test suites
(loadoption, configinstall, themesinstall, randomize) were un-gated from
`NOT WIN32` and all pass on Windows (`ctest`: 4/4; POSIX-only cases QSKIP);
osdetect/espresolve stay Linux-only. `helper --version` prints 3.3.0 and
usage now lists the Windows `bootnext` subcommand. Failures (3) and (4)
were real Windows behavior bugs the unit tests caught — the
themesinstall directory swaps are unaffected (same-volume renames to
absent names). Still pending from §7.2: the on-hardware QA (elevated GUI
Install Config on the multi-ESP machine, Task Scheduler registration
surviving a logon, `bootnext` + reboot landing in rEFInd).
§7.1 complete (CachyOS desktop, 2026-08-13): build + all 6 ctest suites
pass; helper installed to /etc/rEFInd/; `sudo -n … install-config` and
`… install-themes` both land on the ESP from the firmware's rEFInd boot
entry (tier 1, /dev/nvme0n1p2) — this required fixing mountPointOf, whose
atEnd()-guarded loop read nothing from /proc/self/mounts (procfs reports
size 0; regression test `tests/tst_espresolve.cpp`); GUI-launched Install
Config / Install Themes buttons succeed; both randomizer units, once
enabled, ran at boot with exit 0 and a clean journal (active (exited),
ExecStart the helper), and the owner confirmed the randomization takes
effect at the rEFInd menu. Note the runbook's manual unit install only
copies + daemon-reloads — the units must be enabled (GUI toggle or
`systemctl enable`) before the reboot test, or it silently no-ops.
Phases 1-3 are done for Linux: the espops library, the helper binary, both
sudo-gated installs, both randomizers, the version handshake, the
exact-argument sudoers rules, unit repointing, installer/packaging updates,
and deletion of the superseded Linux scripts. The Windows espresolve
port and the in-process Install Config / Install Themes switchover are
written (espresolve_win.cpp: native NVRAM walk + volume enumeration +
letterless-volume directory mounts) but are **compile- and
hardware-unverified** — build on MSYS2 UCRT64 and exercise before trusting.
The Scheduled-Task migration is
also written (wintasks.cpp: native COM registration with the battery
settings preserved, unchanged task names; helper gains the Windows
`bootnext` subcommand) — same unverified status. Still to do once Windows
is verified on hardware: delete the superseded .ps1 files and update
assemble-deploy/Inno/signing accordingly, ship the helper exe in deploy/,
CLAUDE.md/README rewrites, and the on-hardware QA pass (§6 step 4). Until
then the .ps1 files remain shipped and the installers/uninstallers keep
their existing flows.
Scope decisions made with the project owner (2026-08):

1. **Full consolidation** — Install Config, Install Themes, both randomizers,
   the Windows bootnext task payload, and ESP resolution all move from
   bash/PowerShell into compiled code.
2. **Vehicle: a small dedicated helper binary** (QtCore-only console app),
   not the GUI executable with CLI flags.
3. **Passwordless-only** — the zenity password fallback for Install Config is
   dropped (Install Themes is already passwordless-only).
4. This document lands first and is reviewed before any implementation.

A tailored copy of this document exists in the sibling SteamDeck_rEFInd repo
(`NATIVE_HELPER_DESIGN.md` there); the architecture is shared, the
per-platform sections differ (SteamOS bootnext unit, /etc-overlay survival,
pinned toolchain).

## 1. Motivation

Today every privileged GUI action runs an on-disk script, which forces three
pieces of trust machinery to exist:

- **The tamper hash-check** (`Platform::installConfigScriptTrusted` /
  `installThemesScriptTrusted`, `matchesShippedScript()`, script bytes
  embedded via `resources.qrc`): the GUI refuses to run
  `/etc/rEFInd/install_config_from_GUI.sh` or
  `install_themes_from_GUI.sh` unless they SHA-256-match the copies baked
  into the binary. Consequence: *any* edit to those scripts requires a
  rebuilt, re-released binary or every user's Install Config is blocked as
  "modified".
- **The Windows location-trust check** (`Platform::trustedScriptPath()`):
  hash-checking is impossible on Windows (Authenticode signing rewrites the
  `.ps1` files), so trust degrades to "resolves under Program Files".
- **Behavioral-parity duplication**: the NVRAM-first ESP resolution exists
  in bash (`install_config_from_GUI.sh`, `install_themes_from_GUI.sh`,
  `rEFInd_bg_randomizer.sh` — each inlining or sourcing the same logic) *and*
  in PowerShell (`windows/uefi_refind.ps1`), and the two families must be
  kept in behavioral parity by hand.

If the logic is compiled into trusted binaries, the hash-check machinery,
the location-trust check for these actions, and the bash↔PowerShell parity
burden all disappear. What **cannot** disappear on Linux is the privilege
boundary itself: the GUI deliberately runs unprivileged, and sudo/systemd can
only grant trust to a root-owned path on disk. The sudoers rule therefore
stays — it just points at a binary instead of a script.

Secondary wins:

- A compiled helper does the privilege-hygiene parts *better* than bash:
  `runuser -u "$SUDO_USER" -- cat` becomes fork + setgroups/setgid/setuid +
  open + fd handoff — no PAM stack, no shell, no `timeout(1)`.
- The load-option parsing that PowerShell does byte-wise
  (`GetFirmwareEnvironmentVariableW`) and bash does by scraping
  `efibootmgr -v` text unifies into **one** `EFI_LOAD_OPTION` parser used on
  both platforms — retiring the whole "efibootmgr ≥ 18 label anchoring"
  class of bugs from the resolution path.
- Helper messages become `tr()`-translatable later (the scripts are
  English-only today partly *because* of the tamper hash). Out of scope for
  the first release, but unlocked.

## 2. Architecture

### 2.1 Components

```
GUI/src/
  espops/            NEW — static library, platform-split like osdetect:
    espops.h           public surface (results, exit codes, progress sink)
    loadoption.cpp     EFI_LOAD_OPTION / device-path parser (pure, testable)
    espresolve_*.cpp   NVRAM-first rEFInd-ESP resolution (linux/win/common)
    configinstall.cpp  staged publish-last config install
    themesinstall.cpp  staged per-theme dir swap with rollback
    randomize.cpp      bg + theme randomizer payloads
    userio_linux.cpp   fork/setuid fd-handoff reads of the invoking user's files
  helper/            NEW — rEFInd_GUI_helper (QtCore console app)
    main.cpp           subcommand dispatch, --version
  ...                gui target links espops too
```

- **`espops`** holds all logic. It is built into both the GUI and the helper.
  Like the `osdetect_*` files, `espops/` is kept **byte-identical** between
  rEFInd_GUI and SteamDeck_rEFInd, with the per-repo differences (data-dir
  name, `/esp`-first mountpoint preference, product name in messages)
  isolated in one small constants header that each repo owns.
- **`rEFInd_GUI_helper`** is a QtCore-only console binary (no Widgets, no
  Network). One source file; everything real lives in `espops`.

### 2.2 Subcommand surface

| Subcommand | Invoked by | Replaces |
|---|---|---|
| `install-config` | GUI via `sudo -n` (Linux) | `install_config_from_GUI.sh` (both variants) |
| `install-themes` | GUI via `sudo -n` (Linux) | `install_themes_from_GUI.sh` |
| `randomize-background` | systemd unit (Linux), Scheduled Task (Windows) | `rEFInd_bg_randomizer.sh` / `.ps1` |
| `randomize-theme` | systemd unit (Linux), Scheduled Task (Windows) | `rEFInd_theme_randomizer.sh` / `.ps1` |
| `bootnext` | Scheduled Task (Windows) | `bootnext_refind_task.ps1`'s payload path via `uefi_refind.ps1` |
| `--version` | GUI (version handshake, both platforms) | the tamper hash-check |

Exit codes preserve today's contract exactly (the GUI keys its dialogs off
them): 0 success, 2 no/invalid invoking user, 3 no rEFInd ESP, 4 read-only
target, 5 staging/space failure, 6 no source config / no themes. The
randomizer subcommands keep the "cosmetic service" rule: content and
environment problems warn on stderr and **exit 0**; nonzero stays reserved
for internal errors.

On **Windows the GUI itself is already elevated** (requireAdministrator
manifest), so the Install Config / Install Themes buttons call straight into
`espops` **in-process** — no helper launch, no PowerShell, no
`windowsSystemExecutable()`, no UTF-8 prologue. The helper exe exists on
Windows solely so the Scheduled Tasks have something to run when the GUI
isn't up.

On **Linux the GUI stays unprivileged** and runs
`sudo -n /etc/rEFInd/rEFInd_GUI_helper install-config` synchronously,
capturing combined output for the result dialog — the exact model the
passwordless script path uses today.

### 2.3 Trust model

**Linux.** `install-rEFInd-GUI.sh` installs the helper root-owned 0755 at
`/etc/rEFInd/rEFInd_GUI_helper` (precedent: the GUI binary itself already
ships root-owned in `/etc/rEFInd/`). The sudoers drop-in keeps its name,
`zz_` ordering rationale, 0440 mode, and visudo-gating, and its two lines
become exact-argument commands:

```
USER ALL = NOPASSWD: /etc/rEFInd/rEFInd_GUI_helper install-config
USER ALL = NOPASSWD: /etc/rEFInd/rEFInd_GUI_helper install-themes
```

(sudo matches listed arguments exactly, so this is as tight as today's
`""` args-forbidden rules: only those two argument vectors run without a
password.) The systemd randomizer units repoint their `ExecStart` at
`/etc/rEFInd/rEFInd_GUI_helper randomize-…`. Nothing user-writable is ever
executed or sourced by root — same invariant as today, with a smaller and
non-interpreted root target.

The GUI's `passwordlessConfigInstallReady()` probe becomes
`sudo -n -l -- /etc/rEFInd/rEFInd_GUI_helper install-config`. When the rule
or helper is missing the GUI shows "reinstall rEFInd_GUI to repair" — the
zenity fallback is **dropped** (decision 3), which also deletes
`install_config_from_GUI.sh` outright rather than porting it.

**Version handshake replaces the tamper hash.** The hash-check's real jobs
were (a) refusing to password-elevate user-writable staged scripts on the
fallback path — gone with the fallback — and (b) catching version skew
between the binary and the `/etc` scripts. (b) is replaced by the GUI
running `helper --version` (no sudo needed; the file is world-executable)
and comparing against `APP_VERSION`: mismatch → the same "reinstall"
dialog. This is *skew detection, not a security boundary* — the security
boundary is root ownership of `/etc/rEFInd/rEFInd_GUI_helper`, exactly as it
is for the scripts today. `matchesShippedScript()`, the four `resources.qrc`
script embeds, and both `*ScriptTrusted()` implementations are deleted.

**Windows.** The helper exe ships beside the GUI under Program Files
(unwritable by standard users — the same reason no portable build is
published), carries the requireAdministrator manifest, and is
Authenticode-signed like the GUI exe. `trustedScriptPath()` disappears for
these actions (nothing interpreted is launched); the Scheduled Tasks' action
is the absolute Program Files path of the helper.

### 2.4 The shared EFI load-option parser

`espops/loadoption.cpp` parses raw `Boot####` variable bytes
(EFI_LOAD_OPTION: attributes, file-path-list length, description,
device-path list) into {description, GPT partition GUID, loader path,
optional-data}. Platform shims feed it:

- Linux: read `/sys/firmware/efi/efivars/Boot####-8be4df61-…` and
  `BootOrder-…` directly (world-readable; skip the 4-byte attributes
  prefix). No `efibootmgr -v` text scraping anywhere in the resolution path.
- Windows: `GetFirmwareEnvironmentVariableW` — the same call
  `uefi_refind.ps1` makes today, now in C++.

Resolution order is a straight port of `resolve_refind_dir()` /
`Get-RefindNvramEsp`, preserved bug-for-bug: BootOrder-ordered entries
first, then entries missing from BootOrder (conventional `Boot0000`–
`Boot00FF` sweep on Windows); loader-path tier
(`\EFI\refind\refind*.efi`) before exact-label tier (`rEFInd`); the chosen
ESP must actually contain `EFI/refind/refind*.efi` (stale-NVRAM guard);
then any ESP containing rEFInd; then the running system's ESP. The
"WINDOWS" optional-data blob rule and every other *write*-side behavior is
untouched — see §2.6.

### 2.5 Ported hardening (behavior contracts, not suggestions)

Each of these exists because of a real field failure; the C++ port keeps the
semantics:

- **Staged, publish-last, never atomic-claiming**: every file lands as
  `.<name>.new.<suffix>` in its destination dir, a stale-staging sweep runs
  right after the destination is ensured, assets publish before
  `refind.conf`, `refind.conf.prev` backup, `sync`/`FlushFileBuffers`
  before temp mounts go away.
- **Install origin sidecar** (added after the 2026-08-14 on-hardware
  diagnosis of "Install Config succeeds but nothing changes at boot"): after
  publishing, the installer writes `refind.conf.origin` next to the config —
  key=value lines recording product, platform, version, the sha256 of the
  published bytes, and a timestamp. One live config is shared by every
  installer that can reach the ESP (this product's Linux and Windows builds;
  on a dual-boot Deck also the sibling product's), and each publishes its own
  independently generated copy, so two GUIs used alternately silently undo
  each other's changes while both report success. Each install therefore
  reads the outgoing config's sidecar and appends a `Note:` to its output
  when the config it replaces was installed by a different product/platform
  (sha matches, identity differs) or was modified since its recorded install
  (sha differs). Informational only: no sidecar → no note (every install
  over a pre-sidecar version), a failed sidecar write never fails the
  install, and nothing keys behavior off the file.
- **Read-only probe mounts** (Linux): un-mounted ESPs are probed
  `ro,nosuid,nodev,noexec` via exec of absolute-path `/usr/bin/mount`
  (parity with the scripts; no libmount dependency), only the chosen target
  is remounted rw, and only if it was our own probe mount. Cleanup
  unmounts on every exit path.
- **User-context reads**: root never opens anything under the invoking
  user's home. `userio_linux.cpp` forks, drops to `SUDO_USER`
  (setgroups → setgid → setuid, in that order), opens the source file in
  the child, and streams it to the parent over a pipe — the fd-passing
  equivalent of today's `runuser … cat`, with the same "images optional,
  refind.conf mandatory and non-empty" rules, and a size cap enforced
  in-stream (replacing the `head -c` pipe). Theme trees transfer the same
  way (child enumerates and streams; parent writes), replacing the
  `tar | tar --no-same-owner` pipe while keeping its guarantees: symlinks
  refused (vfat can't hold them anyway), no `..`/absolute path members, no
  extraction escape.
- **Themes install**: free-space precheck, per-theme staged directory swap
  with `.old-` rollback, same exit codes.
- **Randomizers**: enumerate candidates with a wall-clock timeout; bg
  randomizer reads the root-owned `/etc/rEFInd/background-dir` pointer as
  data; theme randomizer takes **no user-writable input at all**
  (candidates are the ESP's own root-owned `themes/*/theme.conf`) and
  exits 0 silently unless `refind.conf` contains the include line;
  anti-repeat compare against the current file.
- **Windows encoding**: moot — no PowerShell in these paths anymore. (The
  UTF-8 prologue rule still applies to `osdetect_win.cpp`, which keeps
  using PowerShell for enumeration.)

### 2.6 What still shells out

Deliberately unchanged:

- **NVRAM writes on Linux** keep going through exec'd `efibootmgr`
  (absolute path). Reading efivars is safe; *writing* them carries the
  immutable-attribute dance and, on SteamOS 3.9, the
  can't-rewrite-existing-Boot#### quirk with the chmod-666 retry. That
  logic stays exactly where it is (this repo's install/uninstall scripts;
  the helper itself never writes NVRAM on Linux — no subcommand needs to).
- **Windows NVRAM writes** (`bootnext` subcommand) reuse the
  already-validated `SetFirmwareEnvironmentVariableW` pattern from
  `install_rEFInd.ps1`, ported to C++ with the same rules: never touch
  entries carrying the `"WINDOWS"` blob, BootNext only.
- **mount/umount** on Linux: exec'd absolute paths, as above.
- `Confirm-SecureBootUEFI`-adjacent logic: not needed by any helper
  subcommand (install scripts keep it).

### 2.7 Scheduled Task registration (Windows)

The three `*_task.ps1` wrappers register/unregister tasks whose action is
`powershell.exe -File <payload>.ps1`. With the payloads gone, registration
moves into the GUI natively via the Task Scheduler COM API (`ITaskService`),
preserving the handheld-critical settings the wrappers set
(`AllowStartIfOnBatteries`, `DontStopIfGoingOnBatteries`,
`StartWhenAvailable`, `MultipleInstances IgnoreNew`, 5-minute execution
limit, at-logon trigger, RunLevel Highest) — bare `schtasks.exe` cannot
express the battery settings, which is why COM and not a one-liner. Task
actions point at the Program Files helper exe with the subcommand argument.
The Inno `[UninstallRun]` step keeps disabling all three tasks; old task
names from previous versions are unregistered on upgrade.

## 3. File-by-file impact (this repo)

**Deleted** (after migration completes):

- `install_config_from_GUI.sh`, `install_themes_from_GUI.sh` (root helpers)
- `rEFInd_bg_randomizer.sh`, `rEFInd_theme_randomizer.sh`
- `windows/install_config_from_GUI.ps1`, `windows/install_themes_from_GUI.ps1`
- `windows/rEFInd_bg_randomizer.ps1`, `windows/rEFInd_theme_randomizer.ps1`,
  `windows/bootnext_refind_task.ps1`'s payload logic and all three
  `windows/*_task.ps1` wrappers
- `windows/uefi_refind.ps1` (its logic lives in `espops`; the install and
  uninstall `.ps1` scripts that dot-source it keep a private copy of the
  helper block they already duplicate — see "kept" below)
- In `GUI/src`: `matchesShippedScript()`, both `*ScriptTrusted()`
  Linux implementations, the script entries in `resources.qrc`
- The `HOME`/`USER` sed placeholder machinery for the deleted scripts in
  `install-rEFInd-GUI.sh`

**Kept, unchanged in role**:

- `refind_install_Sourceforge.sh`, `refind_install_package_mgr.sh`,
  `uninstall_rEFInd.sh`, `windows/install_rEFInd.ps1`,
  `windows/uninstall_rEFInd.ps1` — standalone installer/uninstaller
  tooling run in terminals, not sudo-gated GUI helpers. The uninstall
  scripts already carry their own copy of the NVRAM helper block
  (documented "duplicated — keep in sync"); after `uefi_refind.ps1` is
  deleted that duplicate becomes the single PowerShell copy, and its new
  parity partner is `espops/espresolve_*` (called out in CLAUDE.md when
  this ships).
- `scan_esp.sh` / Deep Scan — password-gated, standalone-safe, zenity UX;
  not part of the NOPASSWD surface. Candidate for a later
  `helper scan-esp`, explicitly out of scope now.
- `zz_install_config_from_GUI` — same file, new two lines (see §2.3).
- Both systemd `.service` units — same names, `ExecStart` repointed.

**Modified**:

- `GUI/src/CMakeLists.txt`: new `espops` static lib + `rEFInd_GUI_helper`
  target; helper version stamped from the same `project(VERSION)` so the
  handshake needs no new sync point.
- `platform.cpp`: Linux `installConfig()`/`installThemes()` run the helper
  via `sudo -n`; Windows variants call `espops` in-process; randomizer/
  bootnext toggles use systemctl (unchanged) / COM task registration (new).
- `install-rEFInd-GUI.sh`: install helper binary to `/etc/rEFInd/`, new
  sudoers content, remove superseded `/etc/rEFInd/*.sh` on upgrade.
- `rEFInd_GUI.spec`, `PKGBUILD`, `debian/*`: build + package the helper
  binary and repointed units.
- `windows/assemble-deploy.sh`, `windows/rEFInd_GUI.iss`,
  `.github/workflows/windows-release.yml`: ship + Authenticode-sign the
  helper exe (SignPath artifact set shrinks: fewer `.ps1`, one more exe).
- `GUI/src/tests/`: `loadoption.cpp` and the staging/publish logic are the
  first genuinely unit-testable privileged code in the project — add ctest
  coverage under the existing `BUILD_GUI_TESTS` gate (parser fixtures from
  real `efibootmgr -v` dumps and Windows variable reads).
- `CLAUDE.md`, `README.md`, `I18N_AUDIT.md`: rewritten sections after
  implementation.

## 4. Migration and compatibility

Upgrade is atomic per machine because the installer replaces both sides in
one run; the cases that matter are the mixed ones:

- **New GUI, old `/etc`** (user updated the package but never re-ran the
  installer): the `sudo -n -l` probe for the new argument vector fails and
  the version handshake finds no helper → "reinstall to repair" dialog.
  No old script is ever executed by the new GUI.
- **Old GUI, new `/etc`**: the old GUI's hash check fails against the
  now-absent scripts → its existing tamper dialog, which already says
  reinstall. Acceptable; this state only exists mid-upgrade.
- **Old sudoers + new helper**: the old rules whitelist paths that no
  longer exist; they are inert. The installer overwrites the file
  (visudo-gated) and removes the legacy rule names from earlier versions,
  as it already does for `install_config_from_GUI` → `zz_…` renames.
- **systemd units**: the installer re-copies the repointed units and
  `systemctl daemon-reload`s; a unit left enabled across the upgrade
  starts the helper on next boot with no user action.
- **Windows upgrade**: Inno replaces the Program Files tree; first GUI
  launch re-registers any enabled tasks against the helper exe path and
  unregisters the old powershell.exe-action tasks by name.
- `uninstall_rEFInd.sh` / `windows/uninstall_rEFInd.ps1`: remove the
  helper, the sudoers file, and both old and new task/unit generations.

## 5. Risks and mitigations

- **~600 lines of battle-tested bash re-implemented without a test suite.**
  Largest real risk. Mitigations: `espops` is structured so the pure parts
  (load-option parsing, staging plans, path sanitization) are unit-tested
  under `BUILD_GUI_TESTS`; the port is contract-first (this document's §2.5
  list); on-hardware QA checklist run before release (the `qa/` checklist
  in the sibling repo is the template — this change is exactly what it
  exists for).
- **Root attack surface**: QtCore linked into a NOPASSWD root binary.
  Smaller and less interpretable than bash+coreutils+the shell itself, and
  Widgets/Network/DBus are not linked. Argument surface is two fixed
  vectors enforced by sudoers.
- **Auditability**: users lose "read the privileged script on the device".
  Partially compensated: the helper source is short, `--version` ties the
  binary to a tag, and the security-relevant invariants are documented
  here rather than in script comments.
- **Cross-repo parity**: `espops/` becomes parity-locked like `osdetect_*`
  (byte-identical, one constants header per repo). This *replaces* the
  current three-way bash/ps1/repo parity, it doesn't add to it.
- **A second binary in every package format** (RPM, PKGBUILD, deb, Inno):
  mechanical, but four packaging files plus two CI workflows must move in
  the same release. The version handshake turns any skew into a visible
  dialog instead of silent misbehavior.
- **Helper output i18n**: helper stdout is shown in GUI dialogs. First
  release keeps English (status quo); once the tamper-hash constraint is
  gone, `tr()` in `espops` + shipping `.qm` lookup in the helper is a
  follow-up (`I18N_AUDIT.md` updated then).

## 6. Implementation order (one release train)

Phases are implementation order, **not** separate releases — phase 2 alone
would ship a C++↔bash parity split for the randomizers, so the release that
ships any of this ships all of it.

1. `espops` static lib + helper skeleton + `--version` handshake + unit
   tests for the load-option parser and staging planner.
2. `install-config` / `install-themes` native on both platforms; GUI paths
   switched; tamper machinery deleted; sudoers/installer updated.
3. Randomizers + Windows bootnext into the helper; units repointed; COM
   task registration; payload/wrapper/`uefi_refind.ps1` scripts deleted.
4. Packaging (spec/PKGBUILD/deb/Inno/workflows), uninstallers, docs
   (CLAUDE.md/README/I18N_AUDIT), QA pass on real hardware.

Cross-repo: implement here first (this repo is the parity source for shared
detection code already), then port to SteamDeck_rEFInd by copying `espops/`
+ `helper/` and re-applying that repo's constants header — the same workflow
the `osdetect_*` files use today.

## 7. Verification runbook (CachyOS desktop session)

Everything below assumes this branch checked out locally
(`git fetch origin claude/refind-gui-config-helper-2defin && git checkout
claude/refind-gui-config-helper-2defin`). Work through it in order and
update the status header at the top of this file as steps complete.

**The branch cannot be tested through the normal install paths**:
`install-rEFInd-GUI.sh` checks out the latest release tag, and the RPM
spec/PKGBUILD `git clone` from GitHub main — all three would ignore the
branch. Install the branch build by hand instead.

### 7.1 Linux (native on CachyOS)

```
cd GUI/src && cmake -B build && cmake --build build -j
cmake -B build -DBUILD_GUI_TESTS=ON && cmake --build build && ctest --test-dir build   # 5 suites
# Manual test install (what install-rEFInd-GUI.sh will do from the release):
sudo install -o root -g root -m 755 build/rEFInd_GUI_helper /etc/rEFInd/rEFInd_GUI_helper
sed "s/^USER /$USER /" ../../zz_install_config_from_GUI | sudo tee /tmp/zz_helper >/dev/null
sudo visudo -cf /tmp/zz_helper && sudo install -o root -g root -m 440 /tmp/zz_helper /etc/sudoers.d/zz_install_config_from_GUI
printf '%s\n' "$HOME/.local/rEFInd_GUI/backgrounds" | sudo tee /etc/rEFInd/background-dir >/dev/null
sudo chmod 644 /etc/rEFInd/background-dir
sudo cp ../../rEFInd_bg_randomizer.service ../../rEFInd_theme_randomizer.service /etc/systemd/system/ && sudo systemctl daemon-reload
```

Then run `build/rEFInd_GUI` (same tree → the version handshake matches) and
check, in order: `/etc/rEFInd/rEFInd_GUI_helper --version` prints 3.3.0;
`sudo -n /etc/rEFInd/rEFInd_GUI_helper install-config` runs without a
password (the zz_ ordering vs any passworded drop-in is what's under test);
Install Config and Install Themes from the GUI land on the ESP the firmware
rEFInd entry points at (`(chosen as ...)` line names it); the randomizer
units run clean (`sudo systemctl start rEFInd_theme_randomizer` — inactive
theme config must be a silent no-op); reboot check of both randomizers —
enable them first (GUI toggles or `sudo systemctl enable
rEFInd_bg_randomizer rEFInd_theme_randomizer`; the manual install above
only copies + daemon-reloads, so an un-enabled reboot tests nothing).
Secure Boot/sbctl is unaffected — the helper never lands on the ESP.

### 7.2 Windows (MSYS2 UCRT64, same machine)

```
cmake -G Ninja -S GUI/src -B <builddir-outside-repo>   # Norton deletes CMakeCache under Documents
cmake --build <builddir>
```
Expect the compile fixes (if any) in the three blind-written places:
`espops/espresolve_win.cpp`, `espops/wintasks.cpp`, and the `Q_OS_WIN`
blocks of `platform.cpp`/`randomize.cpp`. Fix here first, then copy the
parity-locked files to the sibling repo unchanged.

QA once it builds: run the elevated GUI → Install Config must pick the ESP
the NVRAM rEFInd entry points at (the multi-ESP stale-shadow case is the
critical test); enable a randomizer → Task Scheduler must show
`rEFInd_GUI_helper.exe randomize-...` RunLevel Highest with
start-on-battery allowed, and the task must survive a logon;
`rEFInd_GUI_helper.exe bootnext` then reboot must land in rEFInd.

### 7.3 After both platforms pass

1. ~~Delete the superseded `windows/*.ps1`~~ — **done** (both repos). Only
   `install_rEFInd.ps1`, `uninstall_rEFInd.ps1`, and the manual installer
   remain; each keeps its own private NVRAM block. Deleting them exposed an
   upgrade gap the design had assumed away: a Scheduled Task registered by an
   older version keeps an action pointing at a wrapper that no longer ships
   and fails silently at every logon. Fixed with `helper migrate-tasks`
   (re-points what exists, converts legacy names, creates nothing the user
   had not enabled) plus `helper enable-logon-task <subcommand>` for the
   installer's opt-in checkboxes; the task tables live in `espconstants.h`.
   Inno `[InstallDelete]` scrubs the removed scripts on upgrade (Inno never
   deletes files it no longer ships) and `[UninstallRun]` drops every task
   name, current and legacy, with `schtasks`.
2. ~~Extend the SignPath deploy artifact-configuration~~ — **documented** in
   `windows/SIGNING.md`; the configuration itself is server-side, so add
   `rEFInd_GUI_helper.exe` in SignPath before the next signed release.
3. ~~Rewrite CLAUDE.md / remove the banners~~ — **done** (both repos).
4. Sync every change to SteamDeck_rEFInd — **done** for code and docs
   (`espops`/`helper`/`tests` byte-identical, repo-specific files by hand).
   **Remaining:** the version bump across the documented list, the release
   itself, the `qa/` checklist on the Deck, and the Deck-side Linux run of
   this runbook (the shared Linux code is §7.1-verified on a CachyOS
   desktop, but never on SteamOS's immutable rootfs + /etc overlay).
