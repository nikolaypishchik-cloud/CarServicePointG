param([ValidateSet('baseline','scaled5')][string]$Dataset='baseline', [switch]$Pilot, [switch]$DebtWindows)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
Set-Location $root
$audit=Join-Path $root 'build-single-instance-audit\performance-20261026'
$env:CS_PERF_OUTPUT=Join-Path $audit $(if($Pilot){'pilot'}else{'results'})
if($DebtWindows) {
    $env:CS_PERF_SCENARIOS='ui.parts.open-close,ui.debts.open-close'
    $env:CS_PERF_OUTPUT=Join-Path $audit $(if($Pilot){'debt-windows-pilot'}else{'debt-windows-fixed'})
} else { Remove-Item Env:CS_PERF_SCENARIOS -ErrorAction SilentlyContinue }
$env:CS_PERF_DATASET=$Dataset
$env:CS_TAB_AUDIT_COPY=Join-Path $audit $Dataset
if($Pilot){$env:CS_PERF_PILOT='1'}else{Remove-Item Env:CS_PERF_PILOT -ErrorAction SilentlyContinue}
$env:TEMP=Join-Path $audit 'temp'
$env:TMP=$env:TEMP
$env:APPDATA=Join-Path $audit 'profile\roaming'
$env:LOCALAPPDATA=Join-Path $audit 'profile\local'
New-Item -ItemType Directory -Force $env:CS_PERF_OUTPUT,$env:TEMP,$env:APPDATA,$env:LOCALAPPDATA | Out-Null
$env:PATH='C:\Qt\6.10.2\msvc2022_64\bin;'+$env:PATH
$env:QT_PLUGIN_PATH='C:\Qt\6.10.2\msvc2022_64\plugins'
$env:QT_QPA_PLATFORM='windows'
$env:CS_ELIT_DISABLE_STARTUP='1'
$env:CS_DEEP_DEBUG='0'
$env:QTEST_FUNCTION_TIMEOUT='3600000'
Remove-Item Env:CS_SEARCH_ENFORCE_SLA,Env:CS_TEST_SCREENSHOTS -ErrorAction SilentlyContinue
$log=Join-Path $env:CS_PERF_OUTPUT ('test-'+$Dataset+'.txt')
& '.\build-single-instance-audit\tests\PurchasePaymentTests.exe' performanceAuditWorkingCopy -o "$log,txt"
exit $LASTEXITCODE
