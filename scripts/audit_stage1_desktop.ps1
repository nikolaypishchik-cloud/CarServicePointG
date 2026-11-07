$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$output = Join-Path $root 'docs\audit-stage1-2026-09-30\evidence'
New-Item -ItemType Directory -Force -Path $output | Out-Null
$env:PATH = 'C:\Qt\6.10.2\msvc2022_64\bin;' + $env:PATH
$env:QT_QPA_PLATFORM = 'offscreen'
$env:APPDATA = Join-Path $output 'test-profile\roaming'
$env:LOCALAPPDATA = Join-Path $output 'test-profile\local'
$env:CS_REPORT_PDF_TEST_OUTPUT = Join-Path $output 'test-parts.pdf'
$results = @()
$names = @('PurchasePaymentTests','MobileSyncTests','InvoicePdfMappingTests',
    'BackupRecoveryAcceptanceTests','RegressionCloseReopenLifecycleTests',
    'RegressionP2ArchitectureLayerRulesTests','RegressionActivityAuditServiceTests')
foreach ($name in $names) {
    $exe = Join-Path $root "build-single-instance-audit\tests\$name.exe"
    $log = Join-Path $output "$name.txt"
    $argsList = @()
    if ($name -ne 'RegressionActivityAuditServiceTests') { $argsList = @('-o', ('"' + $log + ',txt"')) }
    $watch = [Diagnostics.Stopwatch]::StartNew()
    $params = @{FilePath=$exe; WorkingDirectory=$root; WindowStyle='Hidden'; PassThru=$true}
    if ($argsList.Count) { $params.ArgumentList = $argsList }
    else { $params.RedirectStandardOutput=$log; $params.RedirectStandardError=(Join-Path $output "$name.stderr.txt") }
    $process = Start-Process @params
    $timedOut = -not $process.WaitForExit(120000)
    if ($timedOut) { $process.Kill(); $process.WaitForExit() }
    $watch.Stop()
    $text = if (Test-Path -LiteralPath $log) { Get-Content -LiteralPath $log -Raw } else { '' }
    $totals = ''
    if (-not [string]::IsNullOrEmpty($text)) { $totals = [regex]::Match($text, 'Totals:.*').Value }
    $row = [pscustomobject]@{suite=$name; exitCode=$process.ExitCode; timeout=$timedOut; elapsedMs=$watch.ElapsedMilliseconds; totals=$totals}
    $results += $row
    $row | ConvertTo-Json -Compress | Write-Output
}
$results | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $output 'desktop-tests.json') -Encoding utf8
Get-CimInstance Win32_Processor | Select-Object Name,NumberOfCores,NumberOfLogicalProcessors | ConvertTo-Json | Set-Content (Join-Path $output 'cpu.json')
