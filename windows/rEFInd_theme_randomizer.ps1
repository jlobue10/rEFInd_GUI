#Requires -RunAsAdministrator
# Swaps themes\active_theme.conf on whichever EFI System Partition actually
# contains rEFInd (on multi-ESP machines that may not be the system ESP) for a
# random installed theme's theme.conf. Unlike the background randomizer, this
# picks from the ESP's own EFI\refind\themes tree rather than the user's data
# directory: the copied theme.conf references assets by themes\<name>\...
# paths, so only themes already installed on the ESP are valid choices. Run
# hidden at logon by the "rEFInd_GUI_theme_randomizer" scheduled task, so
# progress and errors are also written to rEFInd_theme_randomizer.log in the
# app data directory.
$ErrorActionPreference = 'Stop'

$EspGuid = '{c12a7328-f81f-11d2-ba4b-00a0c93ec93b}'
$RefindLoader = 'EFI\refind\refind_x64.efi'
$DataDir = Join-Path $env:LOCALAPPDATA 'rEFInd_GUI'
$LogFile = Join-Path $DataDir 'rEFInd_theme_randomizer.log'
. (Join-Path $PSScriptRoot 'uefi_refind.ps1')

Set-Content -Path $LogFile -Value @() -ErrorAction SilentlyContinue
function Log($msg) {
    $line = '[{0}] {1}' -f (Get-Date -Format 'yyyy-MM-dd HH:mm:ss'), $msg
    Write-Host $line
    Add-Content -Path $LogFile -Value $line -ErrorAction SilentlyContinue
}

# mountvol reports failure on stderr, which Windows PowerShell 5.1 turns into a
# terminating RemoteException when redirected under ErrorActionPreference Stop;
# run it with the preference relaxed so a failed mount stays a plain exit code.
function Invoke-Mountvol([string[]]$mvArgs) {
    $eap = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try { mountvol @mvArgs 2>$null | Out-Null } finally { $ErrorActionPreference = $eap }
    return $LASTEXITCODE
}

# Make a specific ESP partition reachable, returning its filesystem root and how
# it was mounted. Handles: an already-lettered ESP, the letterless system ESP
# (mountvol /S), and a letterless non-system ESP (temporary directory access
# path, which does not consume a drive letter).
function Mount-EspPartition($part) {
    if ([char]::IsLetter([char]$part.DriveLetter)) {
        return @{ Root = "$($part.DriveLetter):"; Kind = 'letter' }
    }
    if ($part.IsSystem) {
        $used = (Get-PSDrive -PSProvider FileSystem).Name
        foreach ($c in 'Z','Y','X','W','V','U','T') {
            if ($used -notcontains $c) {
                if ((Invoke-Mountvol @("${c}:", '/S')) -eq 0) {
                    return @{ Root = "${c}:"; Kind = 'mountvol' }
                }
            }
        }
        throw 'Could not mount the system EFI System Partition.'
    }
    $dir = Join-Path $env:TEMP ('refind-esp-' + [guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Force $dir | Out-Null
    Add-PartitionAccessPath -DiskNumber $part.DiskNumber -PartitionNumber $part.PartitionNumber -AccessPath $dir
    return @{ Root = $dir; Kind = 'accesspath'; DiskNumber = $part.DiskNumber;
              PartitionNumber = $part.PartitionNumber; Dir = $dir }
}

function Dismount-Esp($m) {
    switch ($m.Kind) {
        'mountvol' { $null = Invoke-Mountvol @($m.Root, '/D') }
        'accesspath' {
            Remove-PartitionAccessPath -DiskNumber $m.DiskNumber -PartitionNumber $m.PartitionNumber `
                -AccessPath $m.Dir -ErrorAction SilentlyContinue
            Remove-Item -Force -ErrorAction SilentlyContinue $m.Dir
        }
    }
}

try {
    $esps = @(Get-Partition | Where-Object { $_.GptType -eq $EspGuid })

    # First choice: the ESP the firmware's rEFInd boot entry points at -- on
    # multi-ESP machines a stale EFI\refind on another ESP must not shadow it.
    $mount = $null
    $nvramGuid = Get-RefindBootPartitionGuid
    $nvramPart = $esps | Where-Object {
        $nvramGuid -and ([guid]$_.Guid -eq $nvramGuid)
    } | Select-Object -First 1
    if ($nvramPart) {
        try {
            $m = Mount-EspPartition $nvramPart
            if (Test-Path (Join-Path $m.Root $RefindLoader)) {
                Log "Using the ESP from the firmware rEFInd boot entry (disk $($nvramPart.DiskNumber), partition $($nvramPart.PartitionNumber))."
                $mount = $m
            } else {
                Dismount-Esp $m
            }
        } catch {
            Log "Could not mount the firmware rEFInd entry's ESP: $_"
        }
    }

    # No usable firmware entry: pick the ESP that contains rEFInd, system ESP first.
    if (-not $mount) {
        $ordered = @($esps | Where-Object { $_.IsSystem }) + @($esps | Where-Object { -not $_.IsSystem })
        foreach ($p in $ordered) {
            try { $m = Mount-EspPartition $p } catch {
                Log "Skipping unreachable ESP (disk $($p.DiskNumber) partition $($p.PartitionNumber)): $_"
                continue
            }
            if (Test-Path (Join-Path $m.Root $RefindLoader)) { $mount = $m; break }
            Dismount-Esp $m
        }
    }
    if (-not $mount) {
        Log 'rEFInd was not found on any EFI System Partition; nothing to do.'
        exit 0
    }

    try {
        $themesDir = Join-Path $mount.Root 'EFI\refind\themes'
        $active = Join-Path $themesDir 'active_theme.conf'
        $confs = @(Get-ChildItem -Path $themesDir -Directory -ErrorAction SilentlyContinue |
            Where-Object { $_.Name -notlike '.*' } |
            ForEach-Object { Join-Path $_.FullName 'theme.conf' } |
            Where-Object {
                (Test-Path -LiteralPath $_ -PathType Leaf) -and
                (Get-Item -LiteralPath $_).Length -gt 0
            })
        if (-not $confs) {
            Log "No themes found in $themesDir; nothing to do. (Use Install Themes in the GUI first.)"
            exit 0
        }
        # With more than one theme available, avoid re-picking the one that is
        # already active.
        $candidates = $confs
        if ($confs.Count -gt 1 -and (Test-Path -LiteralPath $active)) {
            $current = (Get-FileHash -Algorithm SHA256 $active).Hash
            $fresh = @($confs | Where-Object { (Get-FileHash -Algorithm SHA256 $_).Hash -ne $current })
            if ($fresh.Count) { $candidates = $fresh }
        }
        $pick = $candidates | Get-Random
        # Note this only changes which theme an already theme-enabled
        # refind.conf includes; a config created with Theme = None has no
        # include line and is unaffected.
        Copy-Item -Force -LiteralPath $pick -Destination $active
        Log "Theme set to $(Split-Path -Leaf (Split-Path -Parent $pick))"
    } finally {
        Dismount-Esp $mount
    }
} catch {
    Log "ERROR: $_"
    exit 1
}
