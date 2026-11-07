$ErrorActionPreference='Stop'
Set-Location (Split-Path $PSScriptRoot -Parent)
$env:PATH='C:\Qt\6.10.2\msvc2022_64\bin;'+$env:PATH
$env:QT_PLUGIN_PATH='C:\Qt\6.10.2\msvc2022_64\plugins'
$env:QT_QPA_PLATFORM='windows'
$env:CS_ELIT_DISABLE_STARTUP='1'
$env:APPDATA="$PWD/build-single-instance-audit/splash-20261026/profile/roaming"
$env:LOCALAPPDATA="$PWD/build-single-instance-audit/splash-20261026/profile/local"
$env:QTEST_FUNCTION_TIMEOUT='60000'
Remove-Item Env:CS_TAB_AUDIT_COPY -ErrorAction SilentlyContinue
& .\build-single-instance-audit\tests\PurchasePaymentTests.exe debtWindowAsyncLifecycle debtBannerOpensDetails dialogPartialAndCancel supplierReconciliationDialogSavesEditedAmount supplierReconciliationDialogDefaultsToComputedBalanceAndCanCancel masterScopeAndDebtTotals -o build-single-instance-audit/debt-windows-regression.txt,txt
exit $LASTEXITCODE
