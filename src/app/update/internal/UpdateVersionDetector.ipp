// Extracted from src/app/update/UpdateServiceSupport.cpp for P1/P2 monolith reduction.
// Included exactly once by the original translation unit/header; do not compile separately.

    add(QStringLiteral("$AllowUnsignedUpdates = $false"));
    add(QStringLiteral("$RequireSignedUpdates = $true"));
    add(QStringLiteral("$TrustedSignerThumbprint = %1").arg(psSingleQuoted(CarService::AppVersion::updateSignerThumbprint())));
    add(QStringLiteral("New-Item -ItemType Directory -Force -Path (Split-Path -Parent $LogPath) | Out-Null"));
    add(QStringLiteral("New-Item -ItemType Directory -Force -Path $InnoLogDir | Out-Null"));
    add(QStringLiteral("# apply_update.log is preserved from CHECK_UPDATE/DOWNLOAD/VERIFY stages; handoff appends to it."));
    add(QStringLiteral("Remove-Item -LiteralPath $StatusPath -Force -ErrorAction SilentlyContinue"));
    add(QStringLiteral("Remove-Item -LiteralPath $StatusJsonPath -Force -ErrorAction SilentlyContinue"));
    add(QStringLiteral("Remove-Item -LiteralPath $LatestStatusPath -Force -ErrorAction SilentlyContinue"));
    add(QStringLiteral("Remove-Item -LiteralPath $LatestStatusJsonPath -Force -ErrorAction SilentlyContinue"));
    add(QStringLiteral("Remove-Item -LiteralPath $InnoLogPath -Force -ErrorAction SilentlyContinue"));
    add(QStringLiteral("function Write-UpdateLog([string]$Message) { $line = '[{0:yyyy-MM-dd HH:mm:ss.fff}] run_id={1} {2}' -f (Get-Date), $RunId, $Message; Add-Content -LiteralPath $LogPath -Value $line -Encoding UTF8; Add-Content -LiteralPath $LatestLogPath -Value $line -Encoding UTF8 }"));
    add(QStringLiteral("try { $script:UpdateOnceMutex = [System.Threading.Mutex]::new($false, 'Global\\CarServicePointG.Update.Once'); if (-not $script:UpdateOnceMutex.WaitOne(0)) { Write-UpdateLog 'UPDATE_ONCE_MUTEX busy; another updater instance is active'; Set-Content -LiteralPath $StatusPath -Value 'failed:update_already_running' -Encoding UTF8; exit 2 }; Write-UpdateLog 'UPDATE_ONCE_MUTEX acquired Global\\CarServicePointG.Update.Once' } catch { Write-UpdateLog ('UPDATE_ONCE_MUTEX acquisition failed: ' + $_.Exception.Message); exit 2 }"));
    add(QStringLiteral("function Save-JsonAtomic([string]$Path, $Object) { try { New-Item -ItemType Directory -Force -Path (Split-Path -Parent $Path) | Out-Null; $tmp = $Path + '.tmp'; ($Object | ConvertTo-Json -Depth 8) | Set-Content -LiteralPath $tmp -Encoding UTF8; Move-Item -LiteralPath $tmp -Destination $Path -Force; return $true } catch { Write-UpdateLog ('JSON atomic write failed path=' + $Path + ': ' + $_.Exception.Message); return $false } }"));
    add(QStringLiteral("function Set-UpdateTransactionState([string]$State, [string]$Reason = '') { $obj=[ordered]@{ transaction_id=$RunId; state=$State; current_version=$PreviousVersion; expected_version=$ExpectedVersion; asset_name=[System.IO.Path]::GetFileName($PackagePath); asset_sha256=$PackageSha256; package_path=$PackagePath; app_pid=$AppProcessId; updater_pid=$PID; installer_pid=$script:InstallerPid; started_at_utc=$script:TransactionStartedAtUtc; updated_at_utc=(Get-Date).ToUniversalTime().ToString('o'); heartbeat_utc=(Get-Date).ToUniversalTime().ToString('o'); last_error=$Reason }; if ($State -match '^(success|failed|cancelled)$') { $obj.finished_at_utc=(Get-Date).ToUniversalTime().ToString('o') }; Save-JsonAtomic $TransactionPath $obj | Out-Null; Save-JsonAtomic $UpdateFileLockPath $obj | Out-Null }"));
    add(QStringLiteral("function Update-HandoffHeartbeat([string]$Stage) { try { $obj=[ordered]@{ run_id=$RunId; transaction_id=$RunId; owner='updater'; stage=$Stage; app_pid=$AppProcessId; updater_pid=$PID; installer_pid=$script:InstallerPid; asset_name=[System.IO.Path]::GetFileName($PackagePath); expected_version=$ExpectedVersion; asset_sha256=$PackageSha256; heartbeat_utc=(Get-Date).ToUniversalTime().ToString('o'); updated_at_utc=(Get-Date).ToUniversalTime().ToString('o') }; Save-JsonAtomic $LockStatePath $obj | Out-Null; Set-UpdateTransactionState $Stage } catch { Write-UpdateLog ('Heartbeat warning: ' + $_.Exception.Message) } }"));
    add(QStringLiteral("function Add-FailedPackageMarker([string]$Reason) { try { $root=[ordered]@{ failures=@() }; if (Test-Path -LiteralPath $FailedPackagesPath) { try { $root = Get-Content -Raw -LiteralPath $FailedPackagesPath | ConvertFrom-Json } catch {} }; $items=@(); if ($root.failures) { $items=@($root.failures | Where-Object { $_.asset_sha256 -ne $PackageSha256 }) }; $attempt=1; if ($root.failures) { foreach($f in @($root.failures)) { if ($f.asset_sha256 -eq $PackageSha256 -and $f.attempt_count) { $attempt=[Math]::Max($attempt, [int]$f.attempt_count + 1) } } }; $items += [ordered]@{ asset_sha256=$PackageSha256; asset_name=[System.IO.Path]::GetFileName($PackagePath); expected_version=$ExpectedVersion; failure_reason=$Reason; failed_at_utc=(Get-Date).ToUniversalTime().ToString('o'); retry_block_until_utc=(Get-Date).ToUniversalTime().AddHours(24).ToString('o'); attempt_count=$attempt }; Save-JsonAtomic $FailedPackagesPath ([ordered]@{ failures=$items }) | Out-Null; Write-UpdateLog ('FAILED_PACKAGE_COOLDOWN written sha=' + $PackageSha256 + ' reason=' + $Reason) } catch { Write-UpdateLog ('FAILED_PACKAGE_COOLDOWN warning: ' + $_.Exception.Message) } }"));
    add(QStringLiteral("function Clear-FailedPackageMarker() { try { if (!(Test-Path -LiteralPath $FailedPackagesPath)) { return }; $root = Get-Content -Raw -LiteralPath $FailedPackagesPath | ConvertFrom-Json; $items=@(); if ($root.failures) { $items=@($root.failures | Where-Object { $_.asset_sha256 -ne $PackageSha256 }) }; Save-JsonAtomic $FailedPackagesPath ([ordered]@{ failures=$items }) | Out-Null; Write-UpdateLog ('FAILED_PACKAGE_COOLDOWN cleared sha=' + $PackageSha256) } catch { Write-UpdateLog ('FAILED_PACKAGE_COOLDOWN clear warning: ' + $_.Exception.Message) } }"));
    add(QStringLiteral("function Release-UpdateOnceMutex() { try { if ($script:UpdateOnceMutex) { $script:UpdateOnceMutex.ReleaseMutex() | Out-Null; $script:UpdateOnceMutex.Dispose(); $script:UpdateOnceMutex=$null; Write-UpdateLog 'UPDATE_ONCE_MUTEX released' } } catch { Write-UpdateLog ('UPDATE_ONCE_MUTEX release warning: ' + $_.Exception.Message) } }"));
    add(QStringLiteral("function Write-Status([string]$Text) { Set-Content -LiteralPath $StatusPath -Value $Text -Encoding UTF8; Set-Content -LiteralPath $LatestStatusPath -Value $Text -Encoding UTF8; $result='unknown'; if ($Text -match '^failed') { $result='failed' } elseif ($Text -match '^success') { $result='success' }; $reason=''; if ($Text -match '^failed:([^;]+)') { $reason=$Matches[1] }; $obj=[ordered]@{ run_id=$RunId; stage='handoff'; result=$result; reason=$reason; status=$Text; current_version=$PreviousVersion; expected_version=$ExpectedVersion; installed_version=(Get-ExeVersion $ExePath); asset_name=[System.IO.Path]::GetFileName($PackagePath); asset_sha256=$PackageSha256; installer_path=$PackagePath; updated_at_utc=(Get-Date).ToUniversalTime().ToString('o') }; $json=($obj | ConvertTo-Json -Depth 5); Set-Content -LiteralPath $StatusJsonPath -Value $json -Encoding UTF8; Set-Content -LiteralPath $LatestStatusJsonPath -Value $json -Encoding UTF8 }"));
    add(QStringLiteral("function Mark-Failed([string]$Reason) { Write-UpdateLog ('STAGE UPDATE_FAILED reason=' + $Reason); Write-UpdateLog ('FAILED ' + $Reason); Add-FailedPackageMarker $Reason; Set-UpdateTransactionState 'failed' $Reason; Write-Status ('failed:' + $Reason + ';run_id=' + $RunId + ';expected=' + $ExpectedVersion); Release-UpdateOnceMutex }"));
    add(QStringLiteral("function Append-InnoTail([string]$Reason) { try { if (Test-Path -LiteralPath $InnoLogPath) { Write-UpdateLog ('INNO_LOG_TAIL begin reason=' + $Reason + ' path=' + $InnoLogPath); Get-Content -LiteralPath $InnoLogPath -Tail 120 -ErrorAction Stop | ForEach-Object { Write-UpdateLog ('INNO_LOG: ' + $_) }; Write-UpdateLog 'INNO_LOG_TAIL end' } else { Write-UpdateLog ('INNO_LOG missing reason=' + $Reason + ' path=' + $InnoLogPath) } } catch { Write-UpdateLog ('INNO_LOG tail read failed: ' + $_.Exception.Message) } }"));
    add(QStringLiteral("function Write-AppliedUpdateMarker([string]$InstalledVersion, [string]$InstalledExeSha256) { try { New-Item -ItemType Directory -Force -Path (Split-Path -Parent $AppliedMarkerPath) | Out-Null; $obj = [ordered]@{ version=$InstalledVersion; expectedVersion=$ExpectedVersion; packageSha256=$PackageSha256; installedExeSha256=$InstalledExeSha256; exePath=$ExePath; packagePath=$PackagePath; installedAt=(Get-Date).ToUniversalTime().ToString('o') }; ($obj | ConvertTo-Json -Compress) | Set-Content -LiteralPath $AppliedMarkerPath -Encoding UTF8; Write-UpdateLog ('APPLIED_MARKER written path=' + $AppliedMarkerPath + ' version=' + $InstalledVersion + ' packageSha256=' + $PackageSha256) } catch { Write-UpdateLog ('APPLIED_MARKER warning: ' + $_.Exception.Message) } }"));
    add(QStringLiteral("function Write-UpdateState([string]$InstalledVersion, [string]$InstalledExeSha256) { try { New-Item -ItemType Directory -Force -Path (Split-Path -Parent $UpdateStatePath) | Out-Null; $shortcut = ''; try { $shortcut = Get-ShortcutTarget (Join-Path $UserDesktopDir 'CarService PointG 2.1.2.lnk') } catch { }; $obj = [ordered]@{ installed_version=$InstalledVersion; expectedVersion=$ExpectedVersion; installed_build_id=$ExpectedVersion; installed_release_id=$ExpectedVersion; installed_asset_name=[System.IO.Path]::GetFileName($PackagePath); installed_asset_sha256=$PackageSha256; installed_exe_path=$ExePath; installed_exe_sha256=$InstalledExeSha256; install_dir=$AppDir; success_at_utc=(Get-Date).ToUniversalTime().ToString('o'); shortcut_target=$shortcut; installer_path=$PackagePath; installer_sha256=$PackageSha256 }; ($obj | ConvertTo-Json -Compress) | Set-Content -LiteralPath $UpdateStatePath -Encoding UTF8; Write-UpdateLog ('UPDATE_STATE written path=' + $UpdateStatePath + ' version=' + $InstalledVersion + ' assetSha256=' + $PackageSha256) } catch { Write-UpdateLog ('UPDATE_STATE warning: ' + $_.Exception.Message) } }"));
    add(QStringLiteral("function Mark-Success([string]$InstalledVersion) { Write-UpdateLog ('STAGE UPDATE_SUCCESS installed=' + $InstalledVersion); $line = 'success'; if (![string]::IsNullOrWhiteSpace($ExpectedVersion)) { $line = 'success;expected=' + $ExpectedVersion + ';installed=' + $InstalledVersion + ';run_id=' + $RunId }; Write-Status $line; $installedHash = Get-FileSha256Safe $ExePath; Write-AppliedUpdateMarker $InstalledVersion $installedHash; Write-UpdateState $InstalledVersion $installedHash; Clear-FailedPackageMarker; Set-UpdateTransactionState 'success'; Write-UpdateLog ('SUCCESS installed=' + $InstalledVersion); Release-UpdateOnceMutex }"));
    add(QStringLiteral("function Normalize-Version([string]$Version) { if ($null -eq $Version) { return '' }; return (($Version -replace '[^0-9.]','').Trim('.')) }"));
    add(QStringLiteral("function Compare-Version([string]$Left, [string]$Right) { $l = (Normalize-Version $Left).Split('.', [System.StringSplitOptions]::RemoveEmptyEntries); $r = (Normalize-Version $Right).Split('.', [System.StringSplitOptions]::RemoveEmptyEntries); $n = [Math]::Max($l.Length, $r.Length); for ($i=0; $i -lt $n; $i++) { $lv = 0; $rv = 0; if ($i -lt $l.Length) { [int]::TryParse($l[$i], [ref]$lv) | Out-Null }; if ($i -lt $r.Length) { [int]::TryParse($r[$i], [ref]$rv) | Out-Null }; if ($lv -lt $rv) { return -1 }; if ($lv -gt $rv) { return 1 } }; return 0 }"));
    add(QStringLiteral("function Get-ExeVersion([string]$Path) { if (!(Test-Path -LiteralPath $Path)) { return '' }; $vi = (Get-Item -LiteralPath $Path).VersionInfo; if (![string]::IsNullOrWhiteSpace($vi.ProductVersion)) { return $vi.ProductVersion }; return $vi.FileVersion }"));
    add(QStringLiteral("function Get-FileSha256Safe([string]$Path) { try { if (!(Test-Path -LiteralPath $Path)) { return '' }; return ((Get-FileHash -LiteralPath $Path -Algorithm SHA256 -ErrorAction Stop).Hash).ToLowerInvariant() } catch { Write-UpdateLog ('SHA256 read failed for ' + $Path + ': ' + $_.Exception.Message); return '' } }"));
    add(QStringLiteral("function Normalize-Dir([string]$Path) { if ([string]::IsNullOrWhiteSpace($Path)) { return '' }; try { return ([System.IO.Path]::GetFullPath($Path).TrimEnd('\\')).ToLowerInvariant() } catch { return $Path.TrimEnd('\\').ToLowerInvariant() } }"));
    add(QStringLiteral("function Test-UnderDir([string]$Candidate, [string]$Parent) { $c = Normalize-Dir $Candidate; $p = Normalize-Dir $Parent; if ([string]::IsNullOrWhiteSpace($c) -or [string]::IsNullOrWhiteSpace($p)) { return $false }; return ($c -eq $p -or $c.StartsWith($p + [System.IO.Path]::DirectorySeparatorChar)) }"));
    add(QStringLiteral("function Test-SameDir([string]$Left, [string]$Right) { return ((Normalize-Dir $Left) -eq (Normalize-Dir $Right)) }"));
    add(QStringLiteral("function Test-IsAdministrator() { try { $id=[Security.Principal.WindowsIdentity]::GetCurrent(); $p=[Security.Principal.WindowsPrincipal]::new($id); return $p.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator) } catch { return $false } }"));
    add(QStringLiteral("function Start-ElevatedSelfIfNeeded() {"));
    // A generated script in a user-writable directory must never elevate itself.
    // Only the verified signed installer may request UAC elevation.
    add(QStringLiteral("  return $false"));
    add(QStringLiteral("}"));
    add(QStringLiteral("function Test-AuthenticodeSignatureStrict([string]$Path, [string]$ReasonPrefix) {"));
    add(QStringLiteral("  Write-UpdateLog ('STAGE VERIFY_SIGNATURE path=' + $Path)"));
    add(QStringLiteral("  if ([string]::IsNullOrWhiteSpace($Path) -or !(Test-Path -LiteralPath $Path)) { Write-UpdateLog ('VERIFY_SIGNATURE missing path reason=' + $ReasonPrefix); Mark-Failed 'signature_invalid'; return $false }"));
    add(QStringLiteral("  if ([System.IO.Path]::GetExtension($Path).ToLowerInvariant() -ne '.exe' -or [string]::IsNullOrWhiteSpace($TrustedSignerThumbprint)) { Mark-Failed 'trusted_signed_installer_required'; return $false }"));
    add(QStringLiteral("  $sig = $null; $subject = ''"));
    add(QStringLiteral("  try { $sig = Get-AuthenticodeSignature -LiteralPath $Path -ErrorAction Stop; try { if ($sig.SignerCertificate) { $subject = [string]$sig.SignerCertificate.Subject } } catch {}; Write-UpdateLog ('VERIFY_SIGNATURE status=' + $sig.Status + ' subject=' + $subject) }"));
    add(QStringLiteral("  catch { Write-UpdateLog ('VERIFY_SIGNATURE failed: ' + $_.Exception.Message); Mark-Failed 'signature_invalid'; return $false }"));
    add(QStringLiteral("  if ($sig.Status -ne 'Valid') { Mark-Failed 'signature_invalid'; return $false }"));
    add(QStringLiteral("  if ($sig.SignerCertificate.Thumbprint -ne $TrustedSignerThumbprint) { Mark-Failed 'untrusted_publisher'; return $false }"));
    add(QStringLiteral("  return $true"));
    add(QStringLiteral("}"));
    add(QStringLiteral("function Test-InnoTargetDirectory() {"));
    add(QStringLiteral("  if (!(Test-Path -LiteralPath $InnoLogPath)) { Write-UpdateLog ('INNO_TARGET_CHECK warning: inno_setup.log missing at ' + $InnoLogPath); return $true }"));
    add(QStringLiteral("  $outside = @()"));
    add(QStringLiteral("  $destRx = [regex]'(?i)Dest(?:ination)? filename:\\s*(.+)$'"));
    add(QStringLiteral("  foreach ($line in (Get-Content -LiteralPath $InnoLogPath -ErrorAction SilentlyContinue)) { $m = $destRx.Match($line); if (!$m.Success) { continue }; $dest = $m.Groups[1].Value.Trim(); if ($dest -notmatch '(?i)(\\\\|^)(CarServicePointG\\.exe|Qt6Core\\.dll|platforms\\\\qwindows\\.dll)$') { continue }; $destDir = Split-Path -Parent $dest; if (!(Test-UnderDir $destDir $AppDir)) { $outside += $dest } }"));
    add(QStringLiteral("  if ($outside.Count -gt 0) { Write-UpdateLog ('CRITICAL installer wrote application payload outside target /DIR. Target AppDir=' + $AppDir); $outside | ForEach-Object { Write-UpdateLog ('Outside target payload: ' + $_) }; return $false }"));
    add(QStringLiteral("  Write-UpdateLog ('INNO_TARGET_CHECK passed for AppDir=' + $AppDir)"));
    add(QStringLiteral("  return $true"));
    add(QStringLiteral("}"));
    add(QStringLiteral("function Wait-ProcessExit([int]$ProcessId, [int]$Retries) { for ($i=0; $i -lt $Retries; $i++) { if ($null -eq (Get-Process -Id $ProcessId -ErrorAction SilentlyContinue)) { return $true }; Start-Sleep -Seconds 1 }; return $false }"));
    add(QStringLiteral("function Wait-FileUnlock([string]$Path, [int]$Retries) { if ([string]::IsNullOrWhiteSpace($Path) -or !(Test-Path -LiteralPath $Path)) { return $true }; for ($i=0; $i -lt $Retries; $i++) { try { $fs=[System.IO.File]::Open($Path,[System.IO.FileMode]::Open,[System.IO.FileAccess]::Read,[System.IO.FileShare]::None); $fs.Close(); return $true } catch { Start-Sleep -Seconds 1 } }; return $false }"));
    add(QStringLiteral("function Wait-UpdateResourceLocks() {"));
    add(QStringLiteral("  $targets = @($ExePath, $PackagePath, $AppLogPath)"));
    add(QStringLiteral("  foreach ($dbPath in @($ActiveDbPath,$LegacyDbPath,$ArchiveDbPath,$RegistryDbPath,$SealedDbPath,$SealedLegacyDbPath,$SealedArchiveDbPath,$SealedRegistryDbPath)) { if (![string]::IsNullOrWhiteSpace($dbPath)) { $targets += $dbPath; $targets += ($dbPath + '-wal'); $targets += ($dbPath + '-shm') } }"));
    add(QStringLiteral("  foreach ($target in $targets) { if ([string]::IsNullOrWhiteSpace($target)) { continue }; Write-UpdateLog ('Checking file lock: ' + $target); if (!(Wait-FileUnlock $target 180)) { Write-UpdateLog ('File still locked: ' + $target); return $false } }"));
    add(QStringLiteral("  Write-UpdateLog 'All update-critical file locks are released'"));
    add(QStringLiteral("  return $true"));
    add(QStringLiteral("}"));
    add(QStringLiteral("function Verify-PackageIntegrity() {"));
    add(QStringLiteral("  Write-UpdateLog 'STAGE VERIFY_SHA256 handoff package verification'"));
    add(QStringLiteral("  if (!(Test-Path -LiteralPath $PackagePath)) { Mark-Failed 'handoff_package_missing'; return $false }"));
    add(QStringLiteral("  $pkg = Get-Item -LiteralPath $PackagePath -ErrorAction Stop"));
    add(QStringLiteral("  Write-UpdateLog ('Handoff package size=' + $pkg.Length + ' path=' + $PackagePath)"));
    add(QStringLiteral("  $actual = Get-FileSha256Safe $PackagePath"));
    add(QStringLiteral("  Write-UpdateLog ('Handoff package SHA256 expected=' + $PackageSha256 + ' actual=' + $actual)"));
    add(QStringLiteral("  if ([string]::IsNullOrWhiteSpace($actual)) { Mark-Failed 'sha256_missing'; return $false }"));
    add(QStringLiteral("  if ([string]::IsNullOrWhiteSpace($PackageSha256)) { Mark-Failed 'sha256_missing'; return $false }"));
    add(QStringLiteral("  if ($actual -ne $PackageSha256) { Mark-Failed 'sha256_mismatch'; return $false }"));
    add(QStringLiteral("  if (!(Test-AuthenticodeSignatureStrict $PackagePath 'installer_signature')) { try { Remove-Item -LiteralPath $PackagePath -Force -ErrorAction SilentlyContinue } catch {}; return $false }"));
    add(QStringLiteral("  return $true"));
    add(QStringLiteral("}"));
    add(QStringLiteral("function Restart-Old() { try { if (Test-Path -LiteralPath $ExePath) { Write-UpdateLog ('Restarting previous application: ' + $ExePath); Start-Process -FilePath $ExePath -WorkingDirectory $AppDir | Out-Null } } catch { Write-UpdateLog ('Restart previous application failed: ' + $_.Exception.Message) } }"));
    add(QStringLiteral("function Touch-InstalledExeTimestamp() { try { if (Test-Path -LiteralPath $ExePath) { (Get-Item -LiteralPath $ExePath).LastWriteTimeUtc = (Get-Date).ToUniversalTime(); Write-UpdateLog ('Installed EXE timestamp touched to prevent repeated update-state prompts: ' + $ExePath) } } catch { Write-UpdateLog ('Installed EXE timestamp touch warning: ' + $_.Exception.Message) } }"));
    add(QStringLiteral("function Start-Updated() {"));
    add(QStringLiteral("  Write-UpdateLog 'STAGE START_UPDATED_APP'"));
    add(QStringLiteral("  if (!(Test-Path -LiteralPath $ExePath)) { Write-UpdateLog 'updated_app_start_failed: exe missing'; return $false }"));
    add(QStringLiteral("  try { $p = Start-Process -FilePath $ExePath -WorkingDirectory $AppDir -PassThru -ErrorAction Stop; Start-Sleep -Seconds 3; $running = $null -ne (Get-Process -Id $p.Id -ErrorAction SilentlyContinue); if ($p.HasExited) { Write-UpdateLog ('Updated application exited quickly with code ' + $p.ExitCode); return $false }; if (!$running) { Write-UpdateLog 'Updated application process is not running after start check'; return $false }; Write-UpdateLog ('Updated application started PID=' + $p.Id); return $true }"));
    add(QStringLiteral("  catch { Write-UpdateLog ('Updated application start failed: ' + $_.Exception.Message); return $false }"));
    add(QStringLiteral("}"));
    add(QStringLiteral("function Get-ShortcutTarget([string]$ShortcutPath) { try { $shell = New-Object -ComObject WScript.Shell; $lnk = $shell.CreateShortcut($ShortcutPath); return [string]$lnk.TargetPath } catch { return '' } }"));
    add(QStringLiteral("function Set-CarServiceShortcut([string]$ShortcutPath) {"));
    add(QStringLiteral("  $iconPath = Join-Path $AppDir 'resources/assets/app_shortcut.ico'"));
    add(QStringLiteral("  $dir = Split-Path -Parent $ShortcutPath; if (![string]::IsNullOrWhiteSpace($dir)) { New-Item -ItemType Directory -Force -Path $dir | Out-Null }"));
    add(QStringLiteral("  $shell = New-Object -ComObject WScript.Shell"));
    add(QStringLiteral("  $shortcut = $shell.CreateShortcut($ShortcutPath)"));
    add(QStringLiteral("  $shortcut.TargetPath = $ExePath"));
    add(QStringLiteral("  $shortcut.WorkingDirectory = $AppDir"));
    add(QStringLiteral("  if (Test-Path -LiteralPath $iconPath) { $shortcut.IconLocation = $iconPath } else { $shortcut.IconLocation = $ExePath }"));
    add(QStringLiteral("  $shortcut.Save()"));
    add(QStringLiteral("  Write-UpdateLog ('Shortcut refreshed: ' + $ShortcutPath + ' -> ' + $ExePath)"));
    add(QStringLiteral("}"));
    add(QStringLiteral("function Refresh-Shortcuts() {"));
    add(QStringLiteral("  Write-UpdateLog 'STAGE SHORTCUT_REFRESH'"));
    add(QStringLiteral("  try {"));
    add(QStringLiteral("    $shortcutPatterns = @('*.lnk')"));
    add(QStringLiteral("    $canonicalShortcutNames = @('CarService PointG 2.1.2.lnk','CarService PointG 2.1.1.lnk','CarService PointG 2.1.lnk','CarService PointG.lnk','CarServicePointG.lnk')"));
    add(QStringLiteral("    $commonDesktopDir = [Environment]::GetFolderPath('CommonDesktopDirectory')"));
    add(QStringLiteral("    $commonProgramsDir = [Environment]::GetFolderPath('CommonPrograms')"));
    add(QStringLiteral("    $locations = @($UserDesktopDir,$commonDesktopDir,$UserProgramsDir,$commonProgramsDir)"));
    add(QStringLiteral("    foreach ($root in @($UserProgramsDir,$commonProgramsDir)) { if (![string]::IsNullOrWhiteSpace($root) -and (Test-Path -LiteralPath $root)) { Get-ChildItem -LiteralPath $root -Directory -Filter 'CarService*' -ErrorAction SilentlyContinue | ForEach-Object { $locations += $_.FullName } } }"));
    add(QStringLiteral("    $locations = $locations | Where-Object { ![string]::IsNullOrWhiteSpace($_) -and (Test-Path -LiteralPath $_) } | Select-Object -Unique"));
    add(QStringLiteral("    foreach ($location in $locations) { foreach ($pattern in $shortcutPatterns) { Get-ChildItem -LiteralPath $location -Filter $pattern -File -ErrorAction SilentlyContinue | ForEach-Object { $target = Get-ShortcutTarget $_.FullName; $targetDir = ''; if (![string]::IsNullOrWhiteSpace($target)) { try { $targetDir = Split-Path -Parent $target } catch {} }; $isCanonical = ($canonicalShortcutNames -contains $_.Name); $pointsToApp = (![string]::IsNullOrWhiteSpace($targetDir) -and (Test-UnderDir $targetDir $AppDir)); if ($isCanonical -or $pointsToApp) { Write-UpdateLog ('Removing owned/stale shortcut before canonical refresh: ' + $_.FullName + ' target=' + $target); Remove-Item -LiteralPath $_.FullName -Force -ErrorAction SilentlyContinue } else { Write-UpdateLog ('Shortcut preserved, not owned by current AppDir: ' + $_.FullName + ' target=' + $target) } } } }"));
    add(QStringLiteral("    if (![string]::IsNullOrWhiteSpace($UserDesktopDir) -and (Test-Path -LiteralPath $UserDesktopDir)) { Set-CarServiceShortcut (Join-Path $UserDesktopDir 'CarService PointG 2.1.2.lnk') } else { Write-UpdateLog ('Shortcut desktop skipped, user desktop path unavailable: ' + $UserDesktopDir) }"));
    add(QStringLiteral("    if (![string]::IsNullOrWhiteSpace($UserProgramsDir)) { Set-CarServiceShortcut (Join-Path (Join-Path $UserProgramsDir 'CarService PointG 2.1.2') 'CarService PointG 2.1.2.lnk') } else { Write-UpdateLog 'Shortcut Start Menu skipped, user programs path unavailable' }"));
    add(QStringLiteral("    return $true"));
    add(QStringLiteral("  } catch { Write-UpdateLog ('Shortcut refresh failed: ' + $_.Exception.Message); Mark-Failed 'shortcut_refresh_failed'; return $false }"));
    add(QStringLiteral("}"));
    add(QStringLiteral("function Split-UninstallCommand([string]$Command) {"));
    add(QStringLiteral("  if ([string]::IsNullOrWhiteSpace($Command)) { return @('', '') }"));
    add(QStringLiteral("  $c = $Command.Trim()"));
    add(QStringLiteral("  if ($c.StartsWith('\"')) { $idx = $c.IndexOf('\"', 1); if ($idx -gt 1) { return @($c.Substring(1, $idx-1), $c.Substring($idx+1).Trim()) } }"));
    add(QStringLiteral("  $parts = $c.Split(@(' '), 2, [System.StringSplitOptions]::RemoveEmptyEntries)"));
    add(QStringLiteral("  if ($parts.Count -eq 1) { return @($parts[0], '') }"));
    add(QStringLiteral("  return @($parts[0], $parts[1])"));
    add(QStringLiteral("}"));
    add(QStringLiteral("function Resolve-InstallLocationFromEntry($Entry) {"));
    add(QStringLiteral("  $loc = [string]$Entry.InstallLocation; if (![string]::IsNullOrWhiteSpace($loc)) { return $loc }"));
    add(QStringLiteral("  $cmd = [string]$Entry.QuietUninstallString; if ([string]::IsNullOrWhiteSpace($cmd)) { $cmd = [string]$Entry.UninstallString }"));
    add(QStringLiteral("  $split = Split-UninstallCommand $cmd; $unExe = $split[0]; if (![string]::IsNullOrWhiteSpace($unExe) -and (Test-Path -LiteralPath $unExe)) { return (Split-Path -Parent $unExe) }"));
    add(QStringLiteral("  return ''"));
    add(QStringLiteral("}"));
    add(QStringLiteral("function Write-OldInstallFileList([string]$Location, [string]$Key) {"));
    add(QStringLiteral("  try { if ([string]::IsNullOrWhiteSpace($Location) -or !(Test-Path -LiteralPath $Location)) { return }; $safeKey = (($Key -replace '[^A-Za-z0-9_.-]','_').Trim('_')); if ([string]::IsNullOrWhiteSpace($safeKey)) { $safeKey = 'unknown' }; $listPath = Join-Path (Split-Path -Parent $LogPath) ('old_install_files_' + $safeKey + '.txt'); Get-ChildItem -LiteralPath $Location -Recurse -Force -ErrorAction SilentlyContinue | ForEach-Object { $_.FullName } | Set-Content -LiteralPath $listPath -Encoding UTF8; Write-UpdateLog ('OLD_VERSION_CLEANUP file list written: ' + $listPath) }"));
    add(QStringLiteral("  catch { Write-UpdateLog ('OLD_VERSION_CLEANUP file list warning: ' + $_.Exception.Message) }"));
    add(QStringLiteral("}"));
    add(QStringLiteral("function Get-OldInstallations() {"));
    add(QStringLiteral("  Write-UpdateLog 'STAGE OLD_VERSION_SCAN'"));
    add(QStringLiteral("  $knownAppIds = @('2C8A7F2D-9E2D-49E7-8BE9-9EAA7F0B0001')"));
    add(QStringLiteral("  $roots = @('HKCU:\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall','HKLM:\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall','HKCU:\\Software\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall','HKLM:\\Software\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall')"));
    add(QStringLiteral("  $items = @()"));
    add(QStringLiteral("  foreach ($root in $roots) {"));
    add(QStringLiteral("    if (!(Test-Path $root)) { continue }"));
    add(QStringLiteral("    Get-ChildItem -Path $root -ErrorAction SilentlyContinue | ForEach-Object {"));
    add(QStringLiteral("      $key = $_.PSPath; $props = Get-ItemProperty -Path $key -ErrorAction SilentlyContinue; if ($null -eq $props) { return }"));
    add(QStringLiteral("      $name = [string]$props.DisplayName; $keyName = [string]$_.PSChildName"));
    add(QStringLiteral("      $matchesName = ($name -match '^(CarService|CarService PointG|CarServicePointG)(\\s|$)')"));
    add(QStringLiteral("      $matchesAppId = $false; foreach ($appId in $knownAppIds) { if ($keyName -match [regex]::Escape($appId)) { $matchesAppId = $true; break } }"));
    add(QStringLiteral("      if (!$matchesName -and !$matchesAppId) { return }"));
    add(QStringLiteral("      $obj = [pscustomobject]@{ DisplayName=$name; DisplayVersion=[string]$props.DisplayVersion; InstallLocation=[string]$props.InstallLocation; UninstallString=[string]$props.UninstallString; QuietUninstallString=[string]$props.QuietUninstallString; Publisher=[string]$props.Publisher; Key=$keyName; Root=$root }"));
    add(QStringLiteral("      $items += $obj"));
    add(QStringLiteral("    }"));
    add(QStringLiteral("  }"));
    add(QStringLiteral("  Write-UpdateLog ('OLD_VERSION_SCAN found=' + $items.Count)"));
    add(QStringLiteral("  foreach ($item in $items) { Write-UpdateLog ('OLD_VERSION_SCAN item name=' + $item.DisplayName + ' version=' + $item.DisplayVersion + ' location=' + $item.InstallLocation + ' key=' + $item.Key) }"));
    add(QStringLiteral("  return $items"));
    add(QStringLiteral("}"));
    add(QStringLiteral("function Invoke-OldInstallationCleanup() {"));
    add(QStringLiteral("  Write-UpdateLog 'STAGE OLD_VERSION_CLEANUP'"));
    add(QStringLiteral("  $entries = @(Get-OldInstallations)"));
    add(QStringLiteral("  foreach ($entry in $entries) {"));
    add(QStringLiteral("    $loc = Resolve-InstallLocationFromEntry $entry"));
    add(QStringLiteral("    if ([string]::IsNullOrWhiteSpace($loc)) { Write-UpdateLog ('OLD_VERSION_CLEANUP skip empty location key=' + $entry.Key); continue }"));
    add(QStringLiteral("    if (Test-SameDir $loc $AppDir) { Write-UpdateLog ('OLD_VERSION_CLEANUP skip same install dir: ' + $loc); continue }"));
    add(QStringLiteral("    if (Test-UnderDir $AppDir $loc -or Test-UnderDir $loc $AppDir) { Write-UpdateLog ('OLD_VERSION_CLEANUP skip overlapping dirs old=' + $loc + ' new=' + $AppDir); continue }"));
    add(QStringLiteral("    if (![string]::IsNullOrWhiteSpace($entry.DisplayVersion) -and ![string]::IsNullOrWhiteSpace($ExpectedVersion) -and (Compare-Version $entry.DisplayVersion $ExpectedVersion) -ge 0) { Write-UpdateLog ('OLD_VERSION_CLEANUP skip not older version=' + $entry.DisplayVersion); continue }"));
    add(QStringLiteral("    if (([string]$entry.Publisher) -ne 'CarService PointG' -and ([string]$entry.Publisher) -ne 'CarServicePointG') { Write-UpdateLog ('OLD_VERSION_CLEANUP skip publisher mismatch: ' + $entry.Publisher); continue }"));
    add(QStringLiteral("    $cmd = [string]$entry.QuietUninstallString"));
    add(QStringLiteral("    $argsExtra = ''"));
    add(QStringLiteral("    if ([string]::IsNullOrWhiteSpace($cmd)) { $cmd = [string]$entry.UninstallString; $argsExtra = ' /VERYSILENT /SUPPRESSMSGBOXES /NORESTART' }"));
    add(QStringLiteral("    if ([string]::IsNullOrWhiteSpace($cmd)) { Write-UpdateLog ('OLD_VERSION_CLEANUP warning no uninstall command for ' + $loc); continue }"));
    add(QStringLiteral("    $split = Split-UninstallCommand $cmd; $unExe = $split[0]; $unArgs = ($split[1] + $argsExtra).Trim()"));
    add(QStringLiteral("    if ([string]::IsNullOrWhiteSpace($unExe) -or !(Test-Path -LiteralPath $unExe)) { Write-UpdateLog ('OLD_VERSION_CLEANUP warning uninstall exe missing: ' + $unExe); continue }"));
    add(QStringLiteral("    Write-OldInstallFileList $loc $entry.Key"));
    add(QStringLiteral("    Write-UpdateLog ('OLD_VERSION_CLEANUP start exe=' + $unExe + ' args=' + $unArgs + ' location=' + $loc)"));
    add(QStringLiteral("    try { $p = Start-Process -FilePath $unExe -ArgumentList $unArgs -WorkingDirectory (Split-Path -Parent $unExe) -Wait -PassThru -WindowStyle Hidden -ErrorAction Stop; Write-UpdateLog ('OLD_VERSION_CLEANUP exit=' + $p.ExitCode + ' location=' + $loc); if ($p.ExitCode -ne 0) { Write-UpdateLog 'old_cleanup_warning' } }"));
    add(QStringLiteral("    catch { Write-UpdateLog ('old_cleanup_warning: ' + $_.Exception.Message) }"));
    add(QStringLiteral("  }"));
    add(QStringLiteral("}"));
    add(QStringLiteral("function Verify-InstalledVersion() {"));
    add(QStringLiteral("  Write-UpdateLog 'STAGE POST_INSTALL_VERIFY'"));
    add(QStringLiteral("  $exeDir = Split-Path -Parent $ExePath"));
    add(QStringLiteral("  if (!(Test-UnderDir $exeDir $AppDir)) { Append-InnoTail 'installed_exe_path_outside_target_dir'; Mark-Failed 'installed_exe_path_outside_target_dir'; return $false }"));
    add(QStringLiteral("  if (!(Test-Path -LiteralPath $ExePath)) { Append-InnoTail 'installed_executable_missing'; Mark-Failed 'installed_executable_missing'; return $false }"));
    add(QStringLiteral("  if (!(Wait-FileUnlock $ExePath 180)) { Mark-Failed 'update_resource_lock_timeout'; return $false }"));
    add(QStringLiteral("  $installed = Get-ExeVersion $ExePath"));
    add(QStringLiteral("  Write-UpdateLog ('Post-install version check installed=' + $installed + ' expected=' + $ExpectedVersion + ' previous=' + $PreviousVersion)"));
    add(QStringLiteral("  $installedSha = Get-FileSha256Safe $ExePath"));
    add(QStringLiteral("  Write-UpdateLog ('Post-install SHA256 check installed=' + $installedSha + ' previous=' + $PreviousExeSha256)"));
    add(QStringLiteral("  if (!(Test-InnoTargetDirectory)) { Append-InnoTail 'installer_installed_outside_target_dir'; Mark-Failed 'installer_installed_outside_target_dir'; return $false }"));
    add(QStringLiteral("  if ([string]::IsNullOrWhiteSpace($installed)) { Append-InnoTail 'installed_version_missing'; Mark-Failed 'installed_version_missing'; return $false }"));
    add(QStringLiteral("  if (![string]::IsNullOrWhiteSpace($ExpectedVersion) -and (Compare-Version $installed $ExpectedVersion) -lt 0) { Append-InnoTail 'installed_version_not_updated'; Mark-Failed 'installed_version_not_updated'; return $false }"));
    add(QStringLiteral("  if ([string]::IsNullOrWhiteSpace($installedSha)) { Mark-Failed 'installed_exe_hash_missing'; return $false }"));
    add(QStringLiteral("  if (![string]::IsNullOrWhiteSpace($PreviousVersion) -and (Compare-Version $installed $PreviousVersion) -lt 0) { Append-InnoTail 'installed_version_rolled_back'; Mark-Failed 'installed_version_rolled_back'; return $false }"));
    add(QStringLiteral("  if (![string]::IsNullOrWhiteSpace($PreviousExeSha256) -and $installedSha -eq $PreviousExeSha256) { if (![string]::IsNullOrWhiteSpace($PreviousVersion) -and ![string]::IsNullOrWhiteSpace($ExpectedVersion) -and (Compare-Version $PreviousVersion $ExpectedVersion) -lt 0) { Append-InnoTail 'installed_exe_hash_not_changed'; Mark-Failed 'installed_exe_hash_not_changed'; return $false }; Write-UpdateLog 'Post-install SHA256 unchanged; expected version is already installed, marking idempotent success' }"));
    add(QStringLiteral("  foreach ($required in @('Qt6Core.dll','platforms/qwindows.dll','resources/assets/app_shortcut.ico','resources/assets/app_icon.ico')) { $path = Join-Path $AppDir $required; if (!(Test-Path -LiteralPath $path)) { Write-UpdateLog ('Post-install required resource missing: ' + $path); Mark-Failed 'installed_required_resource_missing'; return $false } }"));
    add(QStringLiteral("  if (!(Test-AuthenticodeSignatureStrict $ExePath 'installed_exe_signature')) { return $false }"));
    add(QStringLiteral("  return $true"));
    add(QStringLiteral("}"));
    add(QStringLiteral("try {"));
    add(QStringLiteral("  Write-UpdateLog 'STAGE PREPARE_HANDOFF'"));
    add(QStringLiteral("  Write-UpdateLog 'Update handoff script started'"));
    add(QStringLiteral("  Write-UpdateLog ('Package path: ' + $PackagePath)"));
    add(QStringLiteral("  Write-UpdateLog ('Expected SHA256: ' + $PackageSha256)"));
    add(QStringLiteral("  Write-UpdateLog ('Package extension: ' + $PackageExtension)"));
    add(QStringLiteral("  Write-UpdateLog ('Target exe: ' + $ExePath)"));
    add(QStringLiteral("  Write-UpdateLog ('Application log path: ' + $AppLogPath)"));
    add(QStringLiteral("  Write-UpdateLog ('Application directory: ' + $AppDir)"));
    add(QStringLiteral("  Write-UpdateLog ('Installer /DIR argument: ' + $InstallDirArg)"));
    add(QStringLiteral("  Write-UpdateLog ('Installer /LOG argument: ' + $InnoLogArg)"));
    add(QStringLiteral("  Write-UpdateLog ('Installer arguments list: ' + ($InstallerArgs -join ' '))"));
    add(QStringLiteral("  Write-UpdateLog ('Installer command line: ' + $InstallerArgsLine)"));
    add(QStringLiteral("  Write-UpdateLog ('Installer working directory: ' + $InstallerWorkingDir)"));
    add(QStringLiteral("  Write-UpdateLog ('Updater PowerShell elevated/admin: ' + (Test-IsAdministrator) + '; elevatedChild=' + $ElevatedChild)"));
    add(QStringLiteral("  if ($PackageExtension -ne '.exe' -and $PackageExtension -ne '.zip') { Mark-Failed ('unsupported_package_extension_' + $PackageExtension.TrimStart('.')); Restart-Old; exit 1 }"));
    add(QStringLiteral("  Update-HandoffHeartbeat 'elevating'"));
    add(QStringLiteral("  if (Start-ElevatedSelfIfNeeded) { Release-UpdateOnceMutex; exit 0 }"));
    add(QStringLiteral("  Update-HandoffHeartbeat 'handoff_waiting_app_exit'"));
    add(QStringLiteral("  Write-UpdateLog 'STAGE DOWNLOAD_INSTALLER handoff package already downloaded; no re-download will be attempted'"));
    add(QStringLiteral("  Write-UpdateLog 'STAGE EXIT_APP'"));
    add(QStringLiteral("  Write-UpdateLog ('Waiting for application process exit PID=' + $AppProcessId)"));
    add(QStringLiteral("  if (!(Wait-ProcessExit $AppProcessId 300)) { Mark-Failed 'app_exit_timeout'; exit 1 }"));
    add(QStringLiteral("  Update-HandoffHeartbeat 'app_exited'"));
    add(QStringLiteral("  Write-UpdateLog 'Application process exited'"));
    add(QStringLiteral("  Write-UpdateLog 'Runtime shutdown verification: process exited; checking EXE/DB/WAL/SHM/log/package locks'"));
    add(QStringLiteral("  if (!(Wait-UpdateResourceLocks)) { Mark-Failed 'update_resource_lock_timeout'; exit 1 }"));
    add(QStringLiteral("  Update-HandoffHeartbeat 'resources_unlocked'"));
    add(QStringLiteral("  if (!(Verify-PackageIntegrity)) { Restart-Old; exit 1 }"));
    add(QStringLiteral(R"PS(
  # The first supported upgrade family is explicit; never guess schema compatibility.
  if ([version]$PreviousVersion -lt [version]'2.1.1' -or
      ([version]$PreviousVersion).Major -ne 2 -or ([version]$PreviousVersion).Minor -ne 1 -or
      ([version]$ExpectedVersion).Major -ne 2 -or ([version]$ExpectedVersion).Minor -ne 1 -or
      [version]$ExpectedVersion -le [version]$PreviousVersion) {
    Mark-Failed 'unsupported_upgrade_path'; Restart-Old; exit 1
  }
  $dataBackup = Join-Path (Split-Path -Parent $LogPath) 'pre_update_data'
  if (Test-Path -LiteralPath $dataBackup) { Mark-Failed 'pre_update_backup_already_exists'; Restart-Old; exit 1 }
  New-Item -ItemType Directory -Path $dataBackup | Out-Null
  $inventory = @()
  $dbPaths = @($ActiveDbPath,$LegacyDbPath,$ArchiveDbPath,$RegistryDbPath,
               $SealedDbPath,$SealedLegacyDbPath,$SealedArchiveDbPath,$SealedRegistryDbPath) |
               Where-Object { ![string]::IsNullOrWhiteSpace($_) } | Select-Object -Unique
  if (!(Test-Path -LiteralPath $ActiveDbPath -PathType Leaf) -and !(Test-Path -LiteralPath $SealedDbPath -PathType Leaf)) {
    Mark-Failed 'pre_update_database_missing'; Restart-Old; exit 1
  }
  foreach ($database in $dbPaths) {
    foreach ($source in @($database,($database + '-wal'),($database + '-shm'))) {
      if (!(Test-Path -LiteralPath $source -PathType Leaf)) { continue }
      $destination = Join-Path $dataBackup ($inventory.Count.ToString() + '.backup')
      $hash = (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash
      Copy-Item -LiteralPath $source -Destination $destination -ErrorAction Stop
      if ((Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash -ne $hash) {
        Mark-Failed 'pre_update_backup_hash_mismatch'; Restart-Old; exit 1
      }
      $inventory += [ordered]@{ source=$source; backup=$destination; sha256=$hash }
    }
  }
  if (!(Save-JsonAtomic (Join-Path $dataBackup 'manifest.json') ([ordered]@{
      previous_version=$PreviousVersion; target_version=$ExpectedVersion; files=$inventory }))) {
    Mark-Failed 'pre_update_manifest_failed'; Restart-Old; exit 1
  }
  Write-UpdateLog ('PRE_UPDATE_BACKUP verified files=' + $inventory.Count + ' path=' + $dataBackup)
)PS"));
    add(QStringLiteral("  if ($PackagePath.ToLowerInvariant().EndsWith('.exe')) {"));
    add(QStringLiteral("    Write-UpdateLog 'Installer package selected'"));
    add(QStringLiteral("    Write-UpdateLog ('Installer file path: ' + $PackagePath)"));
    add(QStringLiteral("    Write-UpdateLog 'STAGE START_INSTALLER'"));
    add(QStringLiteral("    $installerStarted = $false"));
    add(QStringLiteral("    $installerExit = -9999"));
    add(QStringLiteral("    $launchFailure = 'installer_launch_failed'"));
    add(QStringLiteral("    for ($attempt = 1; $attempt -le $InstallerStartMaxAttempts -and -not $installerStarted; $attempt++) {"));
    add(QStringLiteral("      try {"));
    add(QStringLiteral("        Write-UpdateLog ('START_INSTALLER attempt=' + $attempt + ' file=' + $PackagePath + ' args=' + $InstallerArgsLine + ' elevated=' + (Test-IsAdministrator))"));
    add(QStringLiteral("        if (!(Test-IsAdministrator)) { Write-UpdateLog 'Installer requires administrator rights; launching with UAC runas mode'; $p = Start-Process -FilePath $PackagePath -ArgumentList $InstallerArgs -WorkingDirectory $InstallerWorkingDir -Verb RunAs -PassThru -Wait -ErrorAction Stop } else { $p = Start-Process -FilePath $PackagePath -ArgumentList $InstallerArgs -WorkingDirectory $InstallerWorkingDir -PassThru -Wait -WindowStyle Hidden -ErrorAction Stop }"));
    add(QStringLiteral("        if ($null -eq $p) { $launchFailure = 'installer_process_not_returned'; Write-UpdateLog 'Installer process object was null'; if ($attempt -lt $InstallerStartMaxAttempts) { Start-Sleep -Seconds 2 }; continue }"));
    add(QStringLiteral("        $installerStarted = $true"));
    add(QStringLiteral("        $script:InstallerPid = $p.Id"));
    add(QStringLiteral("        Write-UpdateLog ('Installer PID: ' + $p.Id)"));
    add(QStringLiteral("        Update-HandoffHeartbeat 'installing'"));
    add(QStringLiteral("        Write-UpdateLog 'STAGE INNO_INSTALL'"));
    add(QStringLiteral("        $installerExit = [int]$p.ExitCode"));
    add(QStringLiteral("      } catch {"));
    add(QStringLiteral("        $hresult = $null; $native = $null"));
    add(QStringLiteral("        try { $hresult = $_.Exception.HResult } catch {}"));
    add(QStringLiteral("        try { $native = $_.Exception.NativeErrorCode } catch {}"));
    add(QStringLiteral("        Write-UpdateLog ('Installer launch exception details attempt=' + $attempt + ' hresult=' + $hresult + ' native=' + $native)"));
    add(QStringLiteral("        if ($hresult -eq -2147023673 -or $native -eq 1223 -or $_.Exception.Message -match '(?i)(cancel|cancelled|canceled|operation was canceled|operation cancelled|uac|elevation)') { Mark-Failed 'uac_cancelled_or_elevation_denied'; Restart-Old; exit 1 }"));
    add(QStringLiteral("        $launchFailure = 'installer_launch_failed'"));
    add(QStringLiteral("        Write-UpdateLog ('Installer launch exception attempt=' + $attempt + ': ' + $_.Exception.Message)"));
    add(QStringLiteral("        if ($attempt -lt $InstallerStartMaxAttempts) { Write-UpdateLog 'Retrying installer launch once after start failure'; Start-Sleep -Seconds 2 }"));
    add(QStringLiteral("      }"));
    add(QStringLiteral("    }"));
    add(QStringLiteral("    if (-not $installerStarted) { Mark-Failed $launchFailure; Restart-Old; exit 1 }"));
    add(QStringLiteral("    Write-UpdateLog ('Installer process finished with exit code ' + $installerExit)"));
    add(QStringLiteral("    if ($installerExit -ne 0) { Append-InnoTail ('installer_exit_' + $installerExit); Write-UpdateLog ('Installer returned non-zero exit code; running post-install verification before marking failure: ' + $installerExit); if (!(Verify-InstalledVersion)) { Mark-Failed ('installer_exit_' + $installerExit); Restart-Old; exit 1 } }"));
    add(QStringLiteral("    if (!(Verify-InstalledVersion)) { Restart-Old; exit 1 }"));
    add(QStringLiteral("    Update-HandoffHeartbeat 'post_install_verify'"));
    add(QStringLiteral("    Invoke-OldInstallationCleanup"));
    add(QStringLiteral("    if (!(Refresh-Shortcuts)) { Restart-Old; exit 1 }"));
    add(QStringLiteral("    Update-HandoffHeartbeat 'shortcut_refresh'"));
    add(QStringLiteral("    Touch-InstalledExeTimestamp"));
    add(QStringLiteral("    Mark-Success (Get-ExeVersion $ExePath)"));
    add(QStringLiteral("    Update-HandoffHeartbeat 'starting_updated_app'"));
    add(QStringLiteral("    if (!(Start-Updated)) { Write-UpdateLog 'Updated app start warning after successful install; package remains marked success to prevent update loop'; Restart-Old; exit 0 }"));
    add(QStringLiteral("    exit 0"));
    add(QStringLiteral("  }"));
    add(QStringLiteral("  Write-UpdateLog 'Portable package selected'"));
    add(QStringLiteral("  $ExtractDir = Join-Path (Split-Path -Parent $LogPath) 'extracted'"));
    add(QStringLiteral("  $StageDir = Join-Path (Split-Path -Parent $LogPath) 'stage_new'"));
    add(QStringLiteral("  Remove-Item -LiteralPath $ExtractDir -Recurse -Force -ErrorAction SilentlyContinue"));
    add(QStringLiteral("  Remove-Item -LiteralPath $StageDir -Recurse -Force -ErrorAction SilentlyContinue"));
    add(QStringLiteral("  Remove-Item -LiteralPath $BackupDir -Recurse -Force -ErrorAction SilentlyContinue"));
    add(QStringLiteral("  New-Item -ItemType Directory -Force -Path $ExtractDir,$StageDir,$BackupDir | Out-Null"));
    add(QStringLiteral("  Write-UpdateLog 'Expanding downloaded archive with Expand-Archive -LiteralPath'"));
    add(QStringLiteral("  try { Expand-Archive -LiteralPath $PackagePath -DestinationPath $ExtractDir -Force } catch { Mark-Failed 'archive_expand_failed'; Restart-Old; exit 1 }"));
    add(QStringLiteral("  $source = $null"));
    add(QStringLiteral("  foreach ($candidate in @($ExtractDir) + (Get-ChildItem -LiteralPath $ExtractDir -Directory -ErrorAction SilentlyContinue | ForEach-Object { $_.FullName })) { if ((Test-Path -LiteralPath (Join-Path $candidate 'CarServicePointG.exe')) -or (Test-Path -LiteralPath (Join-Path $candidate 'CarService.exe'))) { $source = $candidate; break } }"));
    add(QStringLiteral("  if ([string]::IsNullOrWhiteSpace($source)) { Mark-Failed 'portable_payload_not_found'; Restart-Old; exit 1 }"));
    add(QStringLiteral("  try { Get-ChildItem -LiteralPath $source -Force | Copy-Item -Destination $StageDir -Recurse -Force } catch { Mark-Failed 'stage_copy_failed'; Restart-Old; exit 1 }"));
    add(QStringLiteral("  if (!(Test-Path -LiteralPath (Join-Path $StageDir 'CarServicePointG.exe')) -and !(Test-Path -LiteralPath (Join-Path $StageDir 'CarService.exe'))) { Mark-Failed 'staged_executable_missing'; Restart-Old; exit 1 }"));
    add(QStringLiteral("  if (!(Test-Path -LiteralPath (Join-Path $StageDir 'Qt6Core.dll'))) { Mark-Failed 'staged_qt_core_missing'; Restart-Old; exit 1 }"));
    add(QStringLiteral("  if (!(Test-Path -LiteralPath (Join-Path $StageDir 'platforms/qwindows.dll'))) { Mark-Failed 'staged_platforms_missing'; Restart-Old; exit 1 }"));
    add(QStringLiteral("  try { Get-ChildItem -LiteralPath $AppDir -Force | Copy-Item -Destination $BackupDir -Recurse -Force -ErrorAction Stop } catch { Mark-Failed 'backup_copy_failed'; Restart-Old; exit 1 }"));
    add(QStringLiteral("  try { Get-ChildItem -LiteralPath $StageDir -Force | Copy-Item -Destination $AppDir -Recurse -Force -ErrorAction Stop } catch { Mark-Failed 'apply_copy_failed'; try { Get-ChildItem -LiteralPath $BackupDir -Force | Copy-Item -Destination $AppDir -Recurse -Force -ErrorAction SilentlyContinue } catch {}; Restart-Old; exit 1 }"));
    add(QStringLiteral("  if (!(Verify-InstalledVersion)) { try { Get-ChildItem -LiteralPath $BackupDir -Force | Copy-Item -Destination $AppDir -Recurse -Force -ErrorAction SilentlyContinue } catch {}; Restart-Old; exit 1 }"));
    add(QStringLiteral("  if (!(Refresh-Shortcuts)) { Restart-Old; exit 1 }"));
    add(QStringLiteral("  Update-HandoffHeartbeat 'shortcut_refresh'"));
    add(QStringLiteral("  Touch-InstalledExeTimestamp"));
    add(QStringLiteral("  Mark-Success (Get-ExeVersion $ExePath)"));
    add(QStringLiteral("  Update-HandoffHeartbeat 'starting_updated_app'"));
    add(QStringLiteral("  if (!(Start-Updated)) { Write-UpdateLog 'Updated app start warning after successful portable update; package remains marked success to prevent update loop'; Restart-Old; exit 0 }"));
    add(QStringLiteral("  exit 0"));
    add(QStringLiteral("} catch { Write-UpdateLog ('Unhandled handoff exception: ' + $_.Exception.Message); Mark-Failed 'handoff_unhandled_exception'; Restart-Old; exit 1 }"));

    ps.write(script.toUtf8());
    ps.close();

    const QString nativePsPath = QDir::toNativeSeparators(psPath);
    const QString nativeWorkDir = QDir::toNativeSeparators(workDir);
    const QString powerShellExe = QDir::toNativeSeparators(QDir::cleanPath(qEnvironmentVariable("SystemRoot") + QStringLiteral("/System32/WindowsPowerShell/v1.0/powershell.exe")));
    if (qEnvironmentVariable("SystemRoot").trimmed().isEmpty() || !QFileInfo::exists(powerShellExe)) {
        appendUpdateRuntimeLog(QStringLiteral("UPDATE_FAILED"), QStringLiteral("powershell_not_found path=%1").arg(powerShellExe));
        if (errorOut) *errorOut = QString::fromUtf8("Не знайдено системний PowerShell для запуску оновлення: %1").arg(powerShellExe);
        return false;
    }
    if (!QFileInfo::exists(psPath) || !QFileInfo::exists(downloadedFile) || !QDir(workDir).exists()) {
        appendUpdateRuntimeLog(QStringLiteral("UPDATE_FAILED"), QStringLiteral("handoff_preflight_missing_files ps=%1 package=%2 workDir=%3").arg(psPath, downloadedFile, workDir));
        if (errorOut) *errorOut = QString::fromUtf8("Не пройдена preflight-перевірка handoff-файлів оновлення.");
        return false;
    }
    const QStringList psArgs = {QStringLiteral("-NoProfile"),
                                QStringLiteral("-ExecutionPolicy"),
                                QStringLiteral("Bypass"),
                                QStringLiteral("-WindowStyle"),
                                QStringLiteral("Hidden"),
                                QStringLiteral("-File"),
                                nativePsPath};
    qint64 updaterPid = 0;
    qInfo().noquote() << QStringLiteral("UPDATE launching detached updater PowerShell path=%1 workDir=%2 args=%3")
                         .arg(nativePsPath, nativeWorkDir, psArgs.join(QStringLiteral(" ")));
    const bool ok = QProcess::startDetached(powerShellExe,
                                            psArgs,
                                            nativeWorkDir,
                                            &updaterPid);
    if (!ok) {
        qWarning().noquote() << QStringLiteral("UPDATE failed to start detached updater PowerShell path=%1 workDir=%2 args=%3")
                                .arg(psPath, nativeWorkDir, psArgs.join(QStringLiteral(" ")));
        appendUpdateRuntimeLog(QStringLiteral("UPDATE_FAILED"),
                               QStringLiteral("failed to start detached updater PowerShell path=%1 workDir=%2 args=%3")
                                   .arg(psPath, nativeWorkDir, psArgs.join(QStringLiteral(" "))));
        if (errorOut) *errorOut = QString::fromUtf8("Не вдалося запустити локальний PowerShell-оновлювач");
        return false;
    }

    qInfo().noquote() << QStringLiteral("UPDATE detached updater launched pid=%1; runtime services and database will be explicitly shut down before process exit").arg(updaterPid);
    appendUpdateRuntimeLog(QStringLiteral("PREPARE_HANDOFF"),
                           QStringLiteral("detached updater PowerShell started pid=%1 path=%2 workDir=%3 args=%4")
                               .arg(updaterPid)
                               .arg(nativePsPath, nativeWorkDir, psArgs.join(QStringLiteral(" "))));
    appendUpdateRuntimeLog(QStringLiteral("PREPARE_HANDOFF"),
                           lower.endsWith(QStringLiteral(".exe"))
                               ? QStringLiteral("installer handoff prepared; application will quit automatically and elevated executor will continue")
                               : QStringLiteral("portable handoff prepared; application will quit automatically and updater will continue"));
    qInfo() << "UPDATE handoff launched; shutting down runtime services, database and file logger before application exit";
    writeUpdateLockState(runId, QStringLiteral("handoff_started"), QFileInfo(downloadedFile).fileName(), expectedVersion, packageSha256, QStringLiteral("updater"), updaterPid);
    writeUpdateTransactionState(runId, QStringLiteral("handoff_waiting_app_exit"), UpdateService::currentVersion(), expectedVersion, QFileInfo(downloadedFile).fileName(), packageSha256, downloadedFile, updaterPid);
    appendUpdateRuntimeLog(QStringLiteral("EXIT_APP"), QStringLiteral("handoff launched; staged shutdown started before Database::close()"));
    QElapsedTimer shutdownTimer;
    shutdownTimer.start();
    AppContext::instance().shutdownRuntimeServices();
    const qint64 shutdownElapsedMs = shutdownTimer.elapsed();
    appendUpdateRuntimeLog(QStringLiteral("EXIT_APP"), QStringLiteral("AppContext runtime services shutdown completed elapsedMs=%1; draining posted events").arg(shutdownElapsedMs));
    if (shutdownElapsedMs > 30000) {
        appendUpdateRuntimeLog(QStringLiteral("UPDATE_WARNING"), QStringLiteral("app_shutdown_stage_timeout stage=runtime_services elapsedMs=%1; handoff already owns transaction, lock is preserved").arg(shutdownElapsedMs));
        writeUpdateTransactionState(runId, QStringLiteral("handoff_waiting_app_exit"), UpdateService::currentVersion(), expectedVersion, QFileInfo(downloadedFile).fileName(), packageSha256, downloadedFile, updaterPid, 0, QStringLiteral("runtime_services_shutdown_timeout"));
    }
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);

    appendUpdateRuntimeLog(QStringLiteral("EXIT_APP"), QStringLiteral("closing Database::close() before quit"));
    QElapsedTimer dbCloseTimer;
    dbCloseTimer.start();
    AppRuntimeService::closeThreadConnection();
    const qint64 dbCloseElapsedMs = dbCloseTimer.elapsed();
    appendUpdateRuntimeLog(QStringLiteral("EXIT_APP"), QStringLiteral("Database::close() elapsedMs=%1").arg(dbCloseElapsedMs));
    if (dbCloseElapsedMs > 30000) {
        appendUpdateRuntimeLog(QStringLiteral("UPDATE_WARNING"), QStringLiteral("app_shutdown_stage_timeout stage=database_close elapsedMs=%1; handoff already owns transaction, lock is preserved").arg(dbCloseElapsedMs));
        writeUpdateTransactionState(runId, QStringLiteral("handoff_waiting_app_exit"), UpdateService::currentVersion(), expectedVersion, QFileInfo(downloadedFile).fileName(), packageSha256, downloadedFile, updaterPid, 0, QStringLiteral("database_close_timeout"));
    }
    appendUpdateRuntimeLog(QStringLiteral("EXIT_APP"), QStringLiteral("Database::close() completed; closing application file log handler"));
    qInfo() << "UPDATE Database::close() completed before application exit; closing file log handler";
    App::shutdownLogHandler();

    QTimer::singleShot(0, qApp, &QCoreApplication::quit);
    return true;
}

void checkPreviousUpdateResult(QWidget* parent)
{
    const QString workDir = updateRuntimeDirPath();
    const QString statusPath = updateLatestStatusPath();
    const QString logPath = updateRuntimeLogPath();

    QFile statusFile(statusPath);
    if (!statusFile.exists() || !statusFile.open(QIODevice::ReadOnly | QIODevice::Text)) return;
    const QString status = QString::fromUtf8(statusFile.readAll()).trimmed();
    statusFile.close();
    if (status.isEmpty()) return;

    const QString logTail = readLastLogLines(logPath, 12);
    auto showResult = [&](const QString& title, const QString& body, bool okStyle) {
        UpdateDialog dlg(parent);
        dlg.setInfoMode(title, body, QString::fromUtf8("Закрити"), okStyle);
        dlg.exec();
    };

    if (status.startsWith(QStringLiteral("failed:"), Qt::CaseInsensitive)) {
        const QString reason = status.mid(QStringLiteral("failed:").size()).trimmed();
        QString text = QString::fromUtf8("Попереднє оновлення не було встановлено.\n\nПричина: %1\n\nЛог: %2")
                           .arg(reason.isEmpty() ? QString::fromUtf8("невідома") : reason, logPath);
        if (!logTail.isEmpty()) {
            text += QString::fromUtf8("\n\nОстанні рядки apply_update.log:\n%1").arg(logTail);
        }
        showResult(QString::fromUtf8("Оновлення не встановлено"), text, false);
        safeRemoveUpdateFile(statusPath, {}, {QStringLiteral(".json")});
        safeRemoveUpdateFile(QDir(workDir).filePath(QStringLiteral("update_status.json")), {QStringLiteral("update_status")}, {QStringLiteral(".json")});
        clearUpdateLockState(QString(), true);
        return;
    }

    if (status.startsWith(QStringLiteral("success"), Qt::CaseInsensitive)) {
        const QString expected = statusField(status, QStringLiteral("expected"));
        if (!expected.isEmpty() && compareVersions(UpdateService::currentVersion(), expected) < 0) {
            QString text = QString::fromUtf8("Installer записав success, але фактична версія застосунку не змінилася.\n\nОчікувана версія: %1\nПоточна версія: %2\n\nЛог: %3")
                               .arg(expected, UpdateService::currentVersion(), logPath);
            if (!logTail.isEmpty()) text += QString::fromUtf8("\n\nОстанні рядки apply_update.log:\n%1").arg(logTail);
            showResult(QString::fromUtf8("Перевірка оновлення не пройдена"), text, false);
        } else {
            qInfo().noquote() << QStringLiteral("UPDATE_STATUS previous update success status=%1 log=%2").arg(status, logPath);
        }
        safeRemoveUpdateFile(statusPath, {}, {QStringLiteral(".json")});
        safeRemoveUpdateFile(QDir(workDir).filePath(QStringLiteral("update_status.json")), {QStringLiteral("update_status")}, {QStringLiteral(".json")});
        clearUpdateLockState(QString(), true);
    }
}

bool runUpdateFlow(QWidget* parent, bool silentIfNoUpdates)
{
    if (RuntimeJob::RuntimeJobDispatcher::instance().isShutdownRequested()) {
        appendUpdateRuntimeLog(QStringLiteral("CHECK_UPDATE"), QStringLiteral("update skipped: application shutdown is already requested"));
        return false;
    }
    QString lockReason;
    if (!s_updateFlowActive.testAndSetAcquire(0, 1)) {
        appendUpdateRuntimeLog(QStringLiteral("CHECK_UPDATE"), QStringLiteral("update already running in this process"));
        if (!silentIfNoUpdates && parent) {
            UpdateDialog dlg(parent);
            dlg.setInfoMode(QString::fromUtf8("Оновлення вже виконується"),
                            QString::fromUtf8("Перевірка або встановлення оновлення вже запущені. Дочекайтесь завершення поточної операції."),
                            QString::fromUtf8("Закрити"),
                            false);
            dlg.exec();
        }
        return false;
    }
    if (isFreshUpdateLockState(&lockReason)) {
        s_updateFlowActive.storeRelease(0);
        appendUpdateRuntimeLog(QStringLiteral("CHECK_UPDATE"), QStringLiteral("update blocked by active lock: %1").arg(lockReason));
        if (!silentIfNoUpdates && parent) {
            UpdateDialog dlg(parent);
            dlg.setInfoMode(QString::fromUtf8("Оновлення вже виконується"),
                            QString::fromUtf8("Попередній процес оновлення ще активний або нещодавно був запущений.\n\n%1").arg(lockReason),
                            QString::fromUtf8("Закрити"),
                            false);
            dlg.exec();
        }
        return false;
    }
    const QString runId = makeUpdateRunId();
    if (!acquireUpdateFileLock(runId, &lockReason)) {
        s_updateFlowActive.storeRelease(0);
        appendUpdateRuntimeLog(QStringLiteral("CHECK_UPDATE"), QStringLiteral("update blocked by update-once file lock: %1").arg(lockReason));
        if (!silentIfNoUpdates && parent) {
            UpdateDialog dlg(parent);
            dlg.setInfoMode(QString::fromUtf8("Оновлення вже виконується"),
                            QString::fromUtf8("Інший процес оновлення вже активний.\n\n%1").arg(lockReason),
                            QString::fromUtf8("Закрити"),
                            false);
            dlg.exec();
        }
        return false;
    }
    writeUpdateLockState(runId, QStringLiteral("flow_created"));
    writeUpdateTransactionState(runId, QStringLiteral("checking"), UpdateService::currentVersion());
    auto* flow = new UpdateFlow(parent, silentIfNoUpdates, runId);
    flow->start();
    return true;
}

} // namespace UpdateServiceSupport
