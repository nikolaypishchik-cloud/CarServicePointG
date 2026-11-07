<#
.SYNOPSIS
  Windows acceptance runner for Performance TZ 100% verification.

.DESCRIPTION
  Runs the final checks that cannot be proven inside the Linux sandbox:
  Release x64 configure/build, CTest, launch smoke, and optional real DB/PDF/OCR scenarios.
  Execute from the extracted project root in Visual Studio Developer PowerShell.

.PARAMETER ProjectRoot
  Extracted project root. Defaults to parent directory of this script.

.PARAMETER BuildDir
  Build directory. Defaults to .\build-release.

.PARAMETER QtPrefixPath
  Optional Qt installation prefix, e.g. C:\Qt\6.7.3\msvc2022_64.

.PARAMETER RealDbPath
  Optional real application DB for manual/perf acceptance.

.PARAMETER InvoicePath
  Optional PDF/XLS/XLSX invoice with 50-100 rows for import acceptance.
#>
param(
    [string]$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path,
    [string]$BuildDir = "",
    [string]$QtPrefixPath = "",
    [string]$RealDbPath = "",
    [string]$InvoicePath = ""
)

$ErrorActionPreference = "Stop"
if ([string]::IsNullOrWhiteSpace($BuildDir)) { $BuildDir = Join-Path $ProjectRoot "build-release" }

function Write-Step($Message) { Write-Host "`n=== $Message ===" -ForegroundColor Cyan }
function Require-File($Path, $Name) { if (!(Test-Path $Path)) { throw "$Name not found: $Path" } }

Write-Step "Performance TZ acceptance environment"
Write-Host "ProjectRoot: $ProjectRoot"
Write-Host "BuildDir:    $BuildDir"
if ($QtPrefixPath) { Write-Host "QtPrefix:    $QtPrefixPath" }
if ($RealDbPath) { Write-Host "RealDb:      $RealDbPath" }
if ($InvoicePath) { Write-Host "Invoice:     $InvoicePath" }

Require-File (Join-Path $ProjectRoot "CMakeLists.txt") "CMakeLists.txt"
New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null

Write-Step "Static gates"
$python = (Get-Command python -ErrorAction SilentlyContinue)
if (-not $python) { $python = (Get-Command py -ErrorAction SilentlyContinue) }
if (-not $python) { throw "Python is required for static gates" }
$pyCmd = $python.Source
$gates = @(
    "scripts/check_build_sanity.py",
    "scripts/check_required_sources.py",
    "scripts/check_runtime_blocking_gate.py",
    "scripts/check_perf_guards.py",
    "scripts/check_p0_safety_gate.py",
    "scripts/check_runtime_conflicts.py",
    "scripts/check_sql_safety_gate.py",
    "scripts/check_sql_exec_accountability.py",
    "scripts/check_no_inc_fragments.py",
    "scripts/check_architecture.py",
    "scripts/check_regression_checklist.py",
    "scripts/check_project_layout.py",
    "scripts/check_p2_acceptance_tests.py",
    "scripts/check_performance_tz_100_completion.py",
    "scripts/check_test_inventory.py",
    "scripts/check_update_release_contract.py",
    "scripts/check_legacy_marker_ci_gate.py"
)
foreach ($gate in $gates) {
    $gatePath = Join-Path $ProjectRoot $gate
    Require-File $gatePath $gate
    & $pyCmd $gatePath
    if ($LASTEXITCODE -ne 0) { throw "Gate failed: $gate" }
}

Write-Step "CMake configure Release x64"
$cmakeArgs = @("-S", $ProjectRoot, "-B", $BuildDir, "-DCMAKE_BUILD_TYPE=Release", "-DBUILD_TESTING=ON")
if ($QtPrefixPath) { $cmakeArgs += "-DCMAKE_PREFIX_PATH=$QtPrefixPath" }
& cmake @cmakeArgs
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed" }

Write-Step "Build Release"
& cmake --build $BuildDir --config Release --parallel
if ($LASTEXITCODE -ne 0) { throw "Release build failed" }

Write-Step "CTest regression/acceptance suite"
& ctest --test-dir $BuildDir -C Release --output-on-failure
if ($LASTEXITCODE -ne 0) { throw "CTest failed" }

Write-Step "Locate application executable"
$excluded = "setup|installer|unins|vc_redist|maintenancetool|Regression|Smoke|Tests|ctest"
$exe = Get-ChildItem -Path $BuildDir -Recurse -Filter *.exe |
    Where-Object { $_.FullName -notmatch $excluded -and $_.Name -match "CarService|PointG" } |
    Sort-Object Length -Descending |
    Select-Object -First 1
if (-not $exe) { throw "Application exe not found under $BuildDir" }
Write-Host "EXE: $($exe.FullName)"

Write-Step "Launch smoke"
$env:CS_DEEP_DEBUG = "0"
if ($RealDbPath) { $env:CS_ACCEPTANCE_DB_PATH = $RealDbPath }
if ($InvoicePath) { $env:CS_ACCEPTANCE_INVOICE_PATH = $InvoicePath }
$p = Start-Process -FilePath $exe.FullName -PassThru
Start-Sleep -Seconds 3
if ($p.HasExited) {
    throw "Application exited during launch smoke. ExitCode=$($p.ExitCode)"
}
Write-Host "Application is running: PID=$($p.Id)"

Write-Step "Manual acceptance required"
Write-Host "1. Open Поточні наряди, База виконаних, Замовлення запчастин. UI must not freeze."
Write-Host "2. Switch date/search filters quickly. Old refresh must not overwrite newer data."
Write-Host "3. Verify overdue rows stay at top and column 'Сплачено' is not recolored."
Write-Host "4. Import PDF/OCR/Excel invoice 50-100 rows. UI must remain responsive."
Write-Host "5. Generate all reports and PDF export. Progress must show stages."
Write-Host "6. Switch tabs 30 times and close app after import; shutdown must finish in 2-3 seconds."
Write-Host "Acceptance runner completed automated part successfully."
