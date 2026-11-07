$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'restore_update_data.ps1')
$root = Join-Path ([IO.Path]::GetTempPath()) ('pointg-recovery-test-' + [Guid]::NewGuid().ToString('N'))
$data = Join-Path $root 'data'
$backup = Join-Path $root 'backup'
$null = New-Item -ItemType Directory -Path $data,$backup
$source = Join-Path $data 'synthetic.db'
$snapshot = Join-Path $backup '0.backup'
$exe = Join-Path $root 'synthetic-test-app.exe'
$manifest = Join-Path $backup 'manifest.json'
[IO.File]::WriteAllText($exe, 'not an executable')
[IO.File]::WriteAllText($snapshot, 'original synthetic bytes')
[IO.File]::WriteAllText($source, 'new synthetic bytes')
[IO.File]::WriteAllText($source + '-wal', 'obsolete wal')
$record = @{previous_version='2.1.1'; target_version='2.1.2'; files=@(@{source=$source; backup=$snapshot; sha256=(Get-FileHash -LiteralPath $snapshot).Hash})}
$record | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $manifest
$null = Invoke-UpdateDataRecovery $manifest $data $exe $true
if ([IO.File]::ReadAllText($source) -ne 'new synthetic bytes') { throw 'ValidateOnly changed data.' }
$null = Invoke-UpdateDataRecovery $manifest $data $exe
if ([IO.File]::ReadAllText($source) -ne 'original synthetic bytes' -or (Test-Path -LiteralPath ($source + '-wal'))) { throw 'Restore or sidecar handling failed.' }
Write-Output 'PASS: validate-only; restored hash; obsolete WAL removed; undo retained'
[IO.File]::WriteAllText($snapshot, 'tampered')
$rejected = $false
try { Invoke-UpdateDataRecovery $manifest $data $exe | Out-Null } catch { $rejected = $true }
if (!$rejected -or [IO.File]::ReadAllText($source) -ne 'original synthetic bytes') { throw 'Tampered snapshot was accepted.' }
Write-Output 'PASS: tampered snapshot rejected without changing data'
$record.files[0].source = Join-Path $root 'outside.db'
$record | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $manifest
$rejected = $false
try { Invoke-UpdateDataRecovery $manifest $data $exe | Out-Null } catch { $rejected = $true }
if (!$rejected) { throw 'Out-of-root target was accepted.' }
Write-Output 'PASS: out-of-root target rejected'
[IO.File]::WriteAllText($snapshot, 'original synthetic bytes')
[IO.File]::WriteAllText($source, 'before failed restore')
$source2 = Join-Path $data 'second.db'
[IO.File]::WriteAllText($source2, 'second before failure')
$record.files = @(@{source=$source; backup=$snapshot; sha256=(Get-FileHash -LiteralPath $snapshot).Hash},
                  @{source=$source2; backup=$snapshot; sha256=(Get-FileHash -LiteralPath $snapshot).Hash})
$record | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $manifest
$script:copies = 0
function Copy-Item {
    [CmdletBinding()]
    param([string]$LiteralPath, [string]$Destination, [switch]$Force)
    $script:copies++
    if ($script:copies -eq 4) { throw 'Injected second-file restore failure' }
    Microsoft.PowerShell.Management\Copy-Item @PSBoundParameters
}
$rejected = $false
try { Invoke-UpdateDataRecovery $manifest $data $exe | Out-Null } catch { $rejected = $true }
Remove-Item Function:\Copy-Item
if (!$rejected -or [IO.File]::ReadAllText($source) -ne 'before failed restore' -or
    [IO.File]::ReadAllText($source2) -ne 'second before failure') { throw 'Partial restore was not rolled back.' }
Write-Output 'PASS: injected mid-restore failure rolled back both files'

$updaterSource = Get-Content -LiteralPath (Join-Path $PSScriptRoot '../src/app/update/internal/UpdateVersionDetector.ipp') -Raw
$backupCode = [regex]::Match($updaterSource, '(?s)R"PS\((.*?)\)PS"').Groups[1].Value
if (!$backupCode) { throw 'Pre-update backup code not found.' }
function Mark-Failed([string]$Reason) { throw $Reason }
function Restart-Old { throw 'Unexpected restart in synthetic test' }
function Write-UpdateLog([string]$Message) { }
function Save-JsonAtomic([string]$Path, $Object) {
    $Object | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $Path -Encoding UTF8
    return $true
}
$PreviousVersion = '2.1.1'
$ExpectedVersion = '2.1.2'
$ActiveDbPath = $source
$LegacyDbPath = $ArchiveDbPath = $RegistryDbPath = ''
$SealedDbPath = $SealedLegacyDbPath = $SealedArchiveDbPath = $SealedRegistryDbPath = ''
$logDirectory = Join-Path $root 'update-run'
$null = New-Item -ItemType Directory -Path $logDirectory
$LogPath = Join-Path $logDirectory 'apply.log'
& ([scriptblock]::Create($backupCode))
$createdManifest = Join-Path $logDirectory 'pre_update_data/manifest.json'
$null = Invoke-UpdateDataRecovery $createdManifest $data $exe $true
Write-Output 'PASS: actual updater preflight copied database and produced a recoverable hash manifest'
$PreviousVersion = '1.0.0'
$rejected = $false
try { & ([scriptblock]::Create($backupCode)) } catch { $rejected = $_.Exception.Message -eq 'unsupported_upgrade_path' }
if (!$rejected) { throw 'Unsupported upgrade path was accepted.' }
Write-Output 'PASS: unsupported upgrade family rejected'
Write-Output "Synthetic fixtures retained at $root"
