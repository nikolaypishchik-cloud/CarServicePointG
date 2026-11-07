param(
    [string]$ManifestPath,
    [string]$DataRoot,
    [string]$ApplicationPath,
    [switch]$ValidateOnly
)

$ErrorActionPreference = 'Stop'

function Assert-RecoveryPath([string]$Path, [string]$Root) {
    $full = [IO.Path]::GetFullPath($Path)
    $base = [IO.Path]::GetFullPath($Root).TrimEnd('\') + '\'
    if (!$full.StartsWith($base, [StringComparison]::OrdinalIgnoreCase)) { throw "Path outside approved root: $full" }
    $ancestor = $full
    while ($ancestor) {
        if (Test-Path -LiteralPath $ancestor) {
            $item = Get-Item -LiteralPath $ancestor -Force
            if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Reparse point is not permitted: $ancestor" }
        }
        $ancestor = Split-Path -Parent $ancestor
    }
    return $full
}

function Invoke-UpdateDataRecovery([string]$Manifest, [string]$Root, [string]$Exe, [bool]$OnlyValidate = $false) {
    if (!(Test-Path -LiteralPath $Root -PathType Container)) { throw 'DataRoot must be an existing data directory.' }
    if (!(Test-Path -LiteralPath $Exe -PathType Leaf)) { throw 'ApplicationPath must identify the installed executable.' }
    $exePath = [IO.Path]::GetFullPath($Exe)
    foreach ($process in @(Get-Process -Name ([IO.Path]::GetFileNameWithoutExtension($exePath)) -ErrorAction SilentlyContinue)) {
        if (!$process.Path -or $process.Path -eq $exePath) { throw 'Close the application before restoring its data.' }
    }
    $manifestFull = (Resolve-Path -LiteralPath $Manifest).Path
    $backupRoot = Split-Path -Parent $manifestFull
    $null = Assert-RecoveryPath $manifestFull $backupRoot
    $document = Get-Content -LiteralPath $manifestFull -Raw | ConvertFrom-Json
    if (!$document.previous_version -or @($document.files).Count -eq 0) { throw 'Invalid pre-update backup manifest.' }
    $seen = @{}
    $entries = @()
    foreach ($entry in $document.files) {
        $source = Assert-RecoveryPath ([string]$entry.source) $Root
        $backup = Assert-RecoveryPath ([string]$entry.backup) $backupRoot
        if ($seen.ContainsKey($source)) { throw 'Duplicate restore target.' }
        $seen[$source] = $true
        if (!(Test-Path -LiteralPath $backup -PathType Leaf) -or
            (Get-FileHash -LiteralPath $backup -Algorithm SHA256).Hash -ne $entry.sha256) { throw 'Backup hash verification failed.' }
        if (Test-Path -LiteralPath $source -PathType Container) { throw 'A restore target is a directory.' }
        $entries += @{ Target=$source; Backup=$backup; Hash=$entry.sha256 }
    }
    # WAL/SHM created after the backup must not replay changes into restored files.
    foreach ($entry in @($entries)) {
        if ($entry.Target -match '-(wal|shm)$') { continue }
        foreach ($suffix in @('-wal','-shm')) {
            $sidecar = Assert-RecoveryPath ($entry.Target + $suffix) $Root
            if (!$seen.ContainsKey($sidecar)) {
                $seen[$sidecar] = $true
                $entries += @{ Target=$sidecar; Backup=$null; Hash=$null }
            }
        }
    }
    foreach ($entry in $entries) {
        if (Test-Path -LiteralPath $entry.Target -PathType Container) { throw 'A restore target is a directory.' }
    }
    if ($OnlyValidate) { return 'Backup hashes and restore paths verified; no data changed.' }
    $undo = Join-Path $Root ('pre-restore-' + [Guid]::NewGuid().ToString('N'))
    $null = New-Item -ItemType Directory -Path $undo
    $captured = @()
    foreach ($entry in $entries) {
        $prior = $null
        if (Test-Path -LiteralPath $entry.Target -PathType Leaf) {
            $prior = Join-Path $undo ($captured.Count.ToString() + '.backup')
            Copy-Item -LiteralPath $entry.Target -Destination $prior
            if ((Get-FileHash -LiteralPath $prior).Hash -ne (Get-FileHash -LiteralPath $entry.Target).Hash) { throw 'Could not verify undo copy.' }
        }
        $captured += @{ Target=$entry.Target; Prior=$prior }
    }
    $captured | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $undo 'undo.json') -Encoding UTF8
    try {
        foreach ($entry in $entries) {
            if ($entry.Backup) {
                Copy-Item -LiteralPath $entry.Backup -Destination $entry.Target -Force
                if ((Get-FileHash -LiteralPath $entry.Target).Hash -ne $entry.Hash) { throw 'Restored file hash mismatch.' }
            } elseif (Test-Path -LiteralPath $entry.Target) {
                Remove-Item -LiteralPath $entry.Target -Force
            }
        }
    } catch {
        $originalError = $_
        foreach ($entry in $captured) {
            if ($entry.Prior) { Copy-Item -LiteralPath $entry.Prior -Destination $entry.Target -Force }
            elseif (Test-Path -LiteralPath $entry.Target) { Remove-Item -LiteralPath $entry.Target -Force }
        }
        throw "Restore failed; undo copies retained at $undo. $originalError"
    }
    return "Data restored and hashes verified. Undo copies: $undo. Install the matching previous application version before opening this database."
}

if ($MyInvocation.InvocationName -ne '.') {
    if (!$ManifestPath -or !$DataRoot -or !$ApplicationPath) { throw 'Specify ManifestPath, DataRoot and ApplicationPath.' }
    Invoke-UpdateDataRecovery $ManifestPath $DataRoot $ApplicationPath $ValidateOnly.IsPresent
}
