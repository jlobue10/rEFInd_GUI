# Multi-language (i18n) audit — rEFInd_GUI

Audit of user-facing text across the Qt GUI and the bash/PowerShell scripts,
what this branch fixes, and what is deliberately left for later. The sibling
SteamDeck_rEFInd repo received the same audit and the same fixes (its GUI
sources mirror this repo's).

## Summary

| Area | State before | State after this branch |
|---|---|---|
| Qt GUI translation pipeline | Dead: no `.qm` ever built or embedded | Working: `.qm` compiled at build time and embedded under `:/i18n` |
| GUI `tr()` coverage | Good, with a few gaps | Complete (About box, "None" combo entry, `Platform` error strings) |
| Shipped languages | English only | English + German (`de`), Spanish (`es`), French (`fr`), Japanese (`ja`), Korean (`ko`), Simplified Chinese (`zh_CN`), Ukrainian (`uk`), Arabic (`ar`), Persian/Farsi (`fa`), Hindi (`hi`), Portuguese (`pt`), Bengali (`bn`), Russian (`ru`), Turkish (`tr`), Vietnamese (`vi`), Urdu (`ur`), Indonesian (`id`), Italian (`it`), Sicilian (`scn`) |
| Desktop entry | English `Comment=` only | Localized `Comment[<lang>]=` |
| Bash scripts | English only | Unchanged — audited, recommendations below |
| PowerShell scripts | English only | Unchanged — audited, recommendations below |
| Inno Setup installer | English only | Unchanged — recommendation below |

## GUI findings and fixes

1. **The translation pipeline was dead code.** `main.cpp` installed a
   `QTranslator` loading from `:/i18n/`, but `CMakeLists.txt` only called
   `qt_create_translation()`, whose `QM_FILES` output no target depended on —
   so no `.qm` was ever compiled, nothing was embedded in resources, and the
   load silently failed on every start. Fixed with `qt_add_translations()`
   (Qt 6), which compiles the `.ts` files during the build and embeds the
   `.qm` output at `:/i18n` where `main.cpp` looks. The Qt 5 fallback path
   compiles `.qm` into `translations/` next to the binary (`main.cpp` tries
   that location second).

2. **Locale fallback was broken by construction.** The old loop built file
   names from `QLocale::name()` (e.g. `rEFInd_GUI_es_MX`), which never matches
   a generic `rEFInd_GUI_es.qm`. Replaced with the
   `QTranslator::load(QLocale(), prefix, "_", dir)` overload, which walks the
   locale's fallback chain (es_MX → es → en). A second translator now loads
   Qt's own `qtbase` strings (standard dialog buttons), from the Qt install
   on Linux or from windeployqt's `translations/` directory on deployed
   Windows builds.

3. **`tr()` gaps closed:**
   - The About box text was a plain `QStringLiteral`.
   - The "None" boot-slot combo entry was a file-scope `static const QString`
     — initialized before `main()` installs the translator, so it could never
     be translated. It is now a function (`noneOption()`), evaluated at use
     time. Note: settings persist combo selections *by text*, so switching UI
     language makes a previously saved "None" fail its `findText` lookup; the
     slot then falls back to defaults, which is the intended graceful path.
   - `platform.cpp`'s two user-visible launch-failure strings
     (`powershell.exe/sudo could not be started.`) now go through
     `QCoreApplication::translate("Platform", ...)`.

4. **Translator noise removed.** `.ui` strings that are overwritten at
   runtime (the path placeholders replaced by `Platform::dataDir()`-based
   hints, the install-source combo items replaced by
   `Platform::installSourceOptions()`) and the numeral `5` are now marked
   `notr="true"` so they never reach translators.

5. **Deliberately NOT translated** — these are identifiers, not prose:
   - `BootEntry` `displayName`/`menuName` values ("Windows", "SteamOS",
     "Windows (SD)", "Ventoy", "Batocera"…). They are OS proper nouns, they
     are compared as matching keys (`applyAutoSelection()`, settings
     persistence by text, dedup in `comboOptions()`), and the `osdetect_*`
     files that produce them are kept byte-identical with the sibling repo.
     Translating them would break matching and the cross-repo parity rule.
   - `menuentry` names written into `refind.conf` — rendered by rEFInd at
     boot; keep them ASCII proper nouns.
   - Settings keys, `refind.conf` directives, file names.

## Shipped translations

`GUI/src/rEFInd_GUI_{ar,bn,de,es,fa,fr,hi,id,it,ja,ko,pt,ru,scn,tr,uk,ur,vi,zh_CN}.ts` cover all 82 messages.
`rEFInd_GUI_en_US.ts` is the source-language reference and intentionally has
empty translations (source text is used as-is). The language is picked from
the system locale automatically; a Language combo in the GUI can
override it at runtime (persisted as `Language/UiLanguage` in the INI; the
switch retranslates live, including the RTL layout flip).

Arabic, Persian, and Urdu are right-to-left languages: their catalogs translate Qt's `QT_LAYOUT_DIRECTION` key to `RTL` (anchored in `main.cpp` so `lupdate` keeps the key), which makes Qt mirror the entire widget layout automatically. The rEFInd boot screen itself stays left-to-right, so strings that reference on-screen icon order ("leftmost icon") still mean the physical left.

Sicilian (`scn`) has one extra requirement: `QLocale` only gained the Sicilian language code in Qt 6.7, so automatic pickup from an `scn_IT` system locale needs Qt ≥ 6.7 — satisfied by the pinned SteamOS build (Qt 6.9) and current Windows builds; on older Qt the catalog is still embedded but the UI falls back to English.

### Adding a language (contributor guide)

1. Add `rEFInd_GUI_<lang>.ts` to `TS_FILES` in `GUI/src/CMakeLists.txt`.
2. Generate/refresh it from the sources:
   `lupdate main.cpp mainwindow.cpp mainwindow.ui platform.cpp osdetect_*.cpp -ts rEFInd_GUI_<lang>.ts`
   (or build the `update_translations` CMake target, Qt 6).
3. Translate with Qt Linguist (`linguist rEFInd_GUI_<lang>.ts`).
4. Build — `qt_add_translations` compiles and embeds it; nothing else to do.
   Leave untranslated entries `unfinished`: they fall back to English.

When GUI strings change, re-run `lupdate` for **all** files in `TS_FILES` so
the `.ts` files stay in sync with the sources.

## Scripts audit (not changed in this branch)

All bash and PowerShell user-facing text is English-only: zenity dialogs
(`refind_install_package_mgr.sh` ~9, `scan_esp.sh` ~5), xterm installer
output and `read` prompts (`refind_install_Sourceforge.sh`,
`refind_install_package_mgr.sh`), and the PowerShell `Write-Step` banners and
result summaries (`windows/install_rEFInd.ps1` ~55 `Write-*` calls,
`uninstall_rEFInd.ps1` ~19, plus the task/randomizer/config scripts).

Localizing them was deferred on purpose — the constraints are real:

1. **The tamper hash check.** `install_config_from_GUI.sh` is embedded in the
   GUI binary at build time and SHA-256-verified before every run. Any edit
   to it must ship together with a rebuilt, re-released GUI, or every
   existing user's Install Config button reports the script as "modified".
   Script localization therefore has to ride a coordinated release, not a
   standalone branch.
2. **Cross-repo parity.** The `windows/*.ps1` scripts are kept identical to
   SteamDeck_rEFInd's copies modulo renames, and several bash/ps1 pairs must
   stay in behavioral parity. Localization must land in both repos in
   lockstep to keep the diffs auditable.
3. **Output is part of the contract.** `Platform::installConfig()` captures
   script output for the result dialog and relies on *exit codes* for
   success/failure — that part is localization-safe — but
   `refind_install_package_mgr.sh` also keeps a strict zenity stdout
   protocol (diagnostics on stderr only). Localized text must never move
   output between streams.

Recommended approach when it is tackled:

- **Bash**: GNU gettext (`gettext.sh`, `TEXTDOMAIN=refind_gui`, `.po/.mo`
  under a new `po/` directory), with a no-op fallback
  (`command -v gettext >/dev/null || gettext() { printf '%s' "$1"; }`) so the
  scripts keep working on minimal systems. Route zenity `--text` arguments
  through it. Keep diagnostics/log lines English (they end up in bug
  reports); localize only summaries and prompts.
- **PowerShell**: the standard `Import-LocalizedData` mechanism (`.psd1`
  string tables per culture next to each script), falling back to `en-US`.
  Keep the numbered `Write-Step` arithmetic intact.
- **Inno Setup** (`windows/rEFInd_GUI.iss`): add a `[Languages]` section —
  the compiler ships official translations; this is a cheap, isolated win
  for the installer UI.
- Priority order: GUI-launched dialogs (highest visibility) → installer
  banners/prompts → uninstaller. Plain log output can stay English.

## Other surfaces

- `rEFInd_GUI.desktop`: localized `Comment[<lang>]` added (done).
- `README.md`: English-only; per-language READMEs are only worth it with a
  commitment to keep them in sync — not recommended now.
- `refind.conf` / rEFInd boot menu: rEFInd itself has no i18n; the icon-based
  menu needs no text. Nothing to do here.
