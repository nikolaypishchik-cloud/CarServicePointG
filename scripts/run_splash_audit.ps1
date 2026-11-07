$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
Set-Location $root
$audit=Join-Path $root 'build-single-instance-audit/splash-20261026'
$env:CS_SPLASH_OUTPUT=$audit
$env:TEMP=Join-Path $audit 'temp'
$env:TMP=$env:TEMP
$env:APPDATA=Join-Path $audit 'profile/roaming'
$env:LOCALAPPDATA=Join-Path $audit 'profile/local'
New-Item -ItemType Directory -Force $audit,$env:TEMP,$env:APPDATA,$env:LOCALAPPDATA | Out-Null
$env:PATH='C:\Qt\6.10.2\msvc2022_64\bin;'+$env:PATH
$env:QT_PLUGIN_PATH='C:\Qt\6.10.2\msvc2022_64\plugins'
$env:QT_QPA_PLATFORM='windows'
$env:CS_ELIT_DISABLE_STARTUP='1'
$env:CS_DEEP_DEBUG='0'
$env:QTEST_FUNCTION_TIMEOUT='120000'
& .\build-single-instance-audit\tests\SplashScreenTests.exe -o "$audit/splash-tests.txt,txt"
if($LASTEXITCODE -ne 0){exit $LASTEXITCODE}
$env:CS_TAB_AUDIT_COPY=Join-Path $root 'build-single-instance-audit/performance-20261026/baseline'
& .\build-single-instance-audit\tests\PurchasePaymentTests.exe splashHandsOffToMainWindow -o "$audit/main-handoff.txt,txt"
exit $LASTEXITCODE
