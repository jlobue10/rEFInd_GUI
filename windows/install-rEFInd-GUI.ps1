# Windows setup script for the rEFInd Customization GUI.
# Copies the app data and scripts to %LOCALAPPDATA%\rEFInd_GUI and creates
# Start Menu / Desktop shortcuts. Run from a checkout of the repository after
# building the exe (see README "Windows" section).
param(
    [string]$ExePath = ''
)
$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent $PSScriptRoot
if (-not $ExePath) {
    # Prefer a deployed build dir, fall back to the plain build dir.
    foreach ($candidate in "$repo\GUI\src\build-win\rEFInd_GUI.exe", "$repo\GUI\src\build\rEFInd_GUI.exe") {
        if (Test-Path $candidate) { $ExePath = $candidate; break }
    }
}

$dest = Join-Path $env:LOCALAPPDATA 'rEFInd_GUI'
New-Item -ItemType Directory -Force $dest | Out-Null

foreach ($d in 'GUI','icons','backgrounds') {
    Copy-Item -Recurse -Force (Join-Path $repo $d) $dest
}
New-Item -ItemType Directory -Force (Join-Path $dest 'windows') | Out-Null
Copy-Item -Force (Join-Path $repo 'windows\*.ps1') (Join-Path $dest 'windows')
Copy-Item -Force (Join-Path $repo 'refind-GUI.conf') (Join-Path $dest 'GUI\refind.conf')

if ($ExePath -and (Test-Path $ExePath)) {
    $exeDir = Split-Path -Parent $ExePath
    Copy-Item -Force $ExePath (Join-Path $dest 'rEFInd_GUI.exe')
    # Bring along Qt runtime files if windeployqt was run into the build dir.
    Get-ChildItem -Path $exeDir -Filter '*.dll' -File -ErrorAction SilentlyContinue |
        ForEach-Object { Copy-Item -Force $_.FullName $dest }
    foreach ($sub in 'platforms','styles','imageformats','iconengines','translations') {
        $p = Join-Path $exeDir $sub
        if (Test-Path $p) { Copy-Item -Recurse -Force $p $dest }
    }
} else {
    Write-Warning 'No built rEFInd_GUI.exe found; data files installed, but you must build the exe (see README) and re-run this script.'
}

$exeTarget = Join-Path $dest 'rEFInd_GUI.exe'
if (Test-Path $exeTarget) {
    $ws = New-Object -ComObject WScript.Shell
    $startMenu = Join-Path ([Environment]::GetFolderPath('StartMenu')) 'Programs'
    foreach ($lnkDir in ([Environment]::GetFolderPath('Desktop')), $startMenu) {
        if (-not (Test-Path $lnkDir)) { continue }
        $lnk = $ws.CreateShortcut((Join-Path $lnkDir 'rEFInd GUI.lnk'))
        $lnk.TargetPath = $exeTarget
        $lnk.WorkingDirectory = $dest
        $lnk.IconLocation = "$exeTarget,0"
        $lnk.Description = 'rEFInd Customization GUI'
        $lnk.Save()
    }
    Write-Host "Installed to $dest with Desktop and Start Menu shortcuts."
    Write-Host 'Note: the app requests Administrator rights on launch (needed for EFI partition access).'
}
