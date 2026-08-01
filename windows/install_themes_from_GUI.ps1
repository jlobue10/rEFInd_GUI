#Requires -RunAsAdministrator
# Installs the GUI data-dir themes tree onto the EFI System Partition that
# firmware actually launches rEFInd from: %LOCALAPPDATA%\rEFInd_GUI\themes\<name>
# is copied to EFI\refind\themes\<name>, where the include line the GUI appends
# to refind.conf (and every theme.conf's own asset paths) expect it. The ESP
# selection mirrors install_config_from_GUI.ps1 (NVRAM rEFInd entry first, then
# a filesystem scan) -- these scripts are deliberately standalone; keep them in
# sync.
$ErrorActionPreference = 'Stop'

$EspGuid = '{c12a7328-f81f-11d2-ba4b-00a0c93ec93b}'
$RefindLoader = 'EFI\refind\refind_x64.efi'
. (Join-Path $PSScriptRoot 'uefi_refind.ps1')

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
# path, which does not consume a drive letter). Disk/partition numbers ride
# along for the free-space check below.
function Mount-EspPartition($part) {
    if ([char]::IsLetter([char]$part.DriveLetter)) {
        return @{ Root = "$($part.DriveLetter):"; Kind = 'letter';
                  DiskNumber = $part.DiskNumber; PartitionNumber = $part.PartitionNumber }
    }
    if ($part.IsSystem) {
        $used = (Get-PSDrive -PSProvider FileSystem).Name
        foreach ($c in 'Z','Y','X','W','V','U','T') {
            if ($used -notcontains $c) {
                if ((Invoke-Mountvol @("${c}:", '/S')) -eq 0) {
                    return @{ Root = "${c}:"; Kind = 'mountvol';
                              DiskNumber = $part.DiskNumber; PartitionNumber = $part.PartitionNumber }
                }
            }
        }
        throw 'Could not mount the system EFI System Partition. Run this script as Administrator.'
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

# Free bytes on the mounted ESP, via its volume object (works for letterless
# access-path mounts too, where System.IO.DriveInfo cannot). $null when the
# volume cannot be resolved -- the caller then skips the space check rather
# than fail an otherwise possible install.
function Get-EspFreeBytes($m) {
    try {
        $vol = Get-Partition -DiskNumber $m.DiskNumber -PartitionNumber $m.PartitionNumber |
            Get-Volume -ErrorAction Stop
        return [long]$vol.SizeRemaining
    } catch {
        return $null
    }
}

$esps = @(Get-Partition | Where-Object { $_.GptType -eq $EspGuid })
$system = $esps | Where-Object { $_.IsSystem } | Select-Object -First 1

# First choice: the ESP the firmware's rEFInd boot entry points at.
$mount = $null
$nvramGuid = Get-RefindBootPartitionGuid
$nvramPart = $esps | Where-Object {
    $nvramGuid -and ([guid]$_.Guid -eq $nvramGuid)
} | Select-Object -First 1
if ($nvramPart) {
    try {
        $m = Mount-EspPartition $nvramPart
        if (Test-Path (Join-Path $m.Root $RefindLoader)) {
            Write-Host "Using the ESP from the firmware rEFInd boot entry (disk $($nvramPart.DiskNumber), partition $($nvramPart.PartitionNumber))."
            $mount = $m
        } else {
            # Stale NVRAM entry; fall through to the filesystem scan.
            Dismount-Esp $m
        }
    } catch {}
}

# No usable firmware entry: scan for an ESP that contains rEFInd, system ESP
# first (that is where the Windows installer places it), then any others for
# multi-ESP setups; fall back to the system ESP (a fresh-install location).
if (-not $mount) {
    $ordered = @()
    if ($system) { $ordered += $system }
    $ordered += ($esps | Where-Object { -not $_.IsSystem })
    foreach ($p in $ordered) {
        try { $m = Mount-EspPartition $p } catch { continue }
        $found = Test-Path (Join-Path $m.Root $RefindLoader)
        if ($found) { $mount = $m; break }
        Dismount-Esp $m
    }
}
if (-not $mount) {
    if (-not $system) {
        throw 'No EFI System Partition found. Run this script as Administrator.'
    }
    $mount = Mount-EspPartition $system
}

$stageDirs = @()
try {
    $src = Join-Path $env:LOCALAPPDATA 'rEFInd_GUI\themes'
    $dest = Join-Path $mount.Root 'EFI\refind\themes'
    New-Item -ItemType Directory -Force $dest | Out-Null

    # Best-effort sweep of staging/backup directories left behind by an
    # earlier interrupted run (the finally block cannot run across a kill or
    # power loss, and every run mints new random staging names, so leftovers
    # would otherwise accumulate on the small ESP). Only the exact
    # ".<name>.new.<suffix>" / ".<name>.old.<suffix>" shapes created below
    # are matched, so no live theme can be touched.
    Get-ChildItem -LiteralPath $dest -Directory -Force -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -match '^\..+\.(new|old)\.[0-9a-f]+$' } |
        Remove-Item -Recurse -Force -ErrorAction SilentlyContinue

    # The themes to install: every themes\<name>\theme.conf that exists
    # non-empty -- the same validity rule the GUI's Theme box applies. Names
    # starting with a dot would collide with the staging pattern.
    $themes = @(Get-ChildItem -LiteralPath $src -Directory -ErrorAction SilentlyContinue |
        Where-Object {
            $_.Name -notlike '.*' -and
            (Test-Path -LiteralPath (Join-Path $_.FullName 'theme.conf') -PathType Leaf) -and
            (Get-Item -LiteralPath (Join-Path $_.FullName 'theme.conf')).Length -gt 0
        })
    if (-not $themes) {
        throw "No themes were found in $src. Each theme must be a folder containing a non-empty theme.conf."
    }

    # Free-space check before anything is copied: the bundled set is ~12 MB
    # and a replace briefly keeps old and new copies of one theme side by
    # side, so require the full new size plus 4 MB of headroom.
    $needBytes = (Get-ChildItem -LiteralPath $src -Recurse -File |
        Measure-Object -Property Length -Sum).Sum + 4MB
    $freeBytes = Get-EspFreeBytes $mount
    if ($null -ne $freeBytes -and $freeBytes -lt $needBytes) {
        throw ("Not enough free space on the EFI System Partition: " +
               "$([math]::Ceiling($needBytes / 1KB)) KB needed, " +
               "$([math]::Floor($freeBytes / 1KB)) KB free. Nothing was copied.")
    }

    # Per-theme staged replace: each theme directory is fully copied to a
    # hidden staging directory on the ESP first, then swapped in (live ->
    # .old, staged -> live, delete .old). Directory renames stay on the ESP,
    # so a failure at any point leaves the theme either fully old (rolled
    # back below) or fully new -- never a half-copied mix that a theme.conf
    # could reference. Publishing per-theme (rather than one giant swap)
    # keeps the peak space cost at one extra theme instead of the whole tree.
    $installed = 0
    foreach ($theme in $themes) {
        $name = $theme.Name
        $token = [guid]::NewGuid().ToString('N')
        $stage = Join-Path $dest (".$name.new.$token")
        $stageDirs += $stage
        Copy-Item -Recurse -LiteralPath $theme.FullName -Destination $stage
        if (-not (Test-Path -LiteralPath (Join-Path $stage 'theme.conf') -PathType Leaf) -or
            (Get-Item -LiteralPath (Join-Path $stage 'theme.conf')).Length -le 0) {
            throw "Theme '$name' lost its theme.conf while being staged; the installed themes were not changed."
        }
        $live = Join-Path $dest $name
        $old = Join-Path $dest (".$name.old.$token")
        $movedAside = $false
        if (Test-Path -LiteralPath $live -PathType Container) {
            Move-Item -LiteralPath $live -Destination $old
            $movedAside = $true
            $stageDirs += $old
        }
        try {
            Move-Item -LiteralPath $stage -Destination $live
        } catch {
            # Restore the old copy so the theme is never left missing.
            if ($movedAside) {
                Move-Item -LiteralPath $old -Destination $live -ErrorAction SilentlyContinue
            }
            throw "Failed while publishing theme '$name'; its previous version was kept. $_"
        }
        if ($movedAside) {
            Remove-Item -Recurse -Force -LiteralPath $old -ErrorAction SilentlyContinue
        }
        Write-Host "Installed theme $name"
        $installed++
    }

    # A temp access-path mount's directory name means nothing to the user, so
    # describe that ESP by disk/partition instead.
    $where = if ($mount.Kind -eq 'accesspath') {
        "EFI\refind\themes on disk $($mount.DiskNumber), partition $($mount.PartitionNumber)"
    } else {
        $dest
    }
    Write-Host "$installed theme(s) installed to $where"
} finally {
    foreach ($stage in $stageDirs) {
        Remove-Item -Recurse -Force -LiteralPath $stage -ErrorAction SilentlyContinue
    }
    Dismount-Esp $mount
}
