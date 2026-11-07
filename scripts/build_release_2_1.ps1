param(
    [string]$QtRoot = "C:\Qt\6.10.2\msvc2022_64",
    [string]$Configuration = "Release",
    [string]$BuildDir = "build-release",
    [string]$DistDir = "dist",
    [string]$IssFile = "CarService_PointG_v2_1.iss",
    [string]$CertificateThumbprint = "",
    [string]$SignTool = "",
    [string]$TimestampUrl = "http://timestamp.digicert.com"
)

$ErrorActionPreference = 'Stop'
$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
Set-Location $ProjectRoot

$ReleaseManifest = Get-Content -LiteralPath (Join-Path $ProjectRoot 'github_release_manifest.json') -Raw | ConvertFrom-Json
$Version = [string]$ReleaseManifest.version
if ($Version -notmatch '^\d+\.\d+\.\d+(\.\d+)?$') { throw 'Release manifest version is invalid.' }
if ($CertificateThumbprint -notmatch '^[0-9a-fA-F]{40}$') { throw 'A trusted code-signing certificate thumbprint is required. Unsigned releases are prohibited.' }
$Certificate = Get-Item -LiteralPath "Cert:\CurrentUser\My\$CertificateThumbprint" -ErrorAction Stop
if (!$Certificate.HasPrivateKey -or $Certificate.NotAfter -le (Get-Date) -or $Certificate.NotBefore -gt (Get-Date)) {
    throw 'Code-signing certificate is unavailable or expired.'
}
if (@($Certificate.EnhancedKeyUsageList | Where-Object { $_.ObjectId.Value -eq '1.3.6.1.5.5.7.3.3' }).Count -eq 0) {
    throw 'Certificate does not permit code signing.'
}
if (!$SignTool -or !(Test-Path -LiteralPath $SignTool -PathType Leaf)) { throw 'Pass the Windows SDK signtool.exe path through -SignTool.' }
function Sign-ReleaseFile([string]$Path) {
    & $SignTool sign /sha1 $CertificateThumbprint /fd SHA256 /tr $TimestampUrl /td SHA256 $Path
    if ($LASTEXITCODE -ne 0) { throw "Signing failed: $Path" }
    & $SignTool verify /pa /all $Path
    if ($LASTEXITCODE -ne 0) { throw "Signature verification failed: $Path" }
    $signature = Get-AuthenticodeSignature -LiteralPath $Path
    if ($signature.Status -ne 'Valid' -or $signature.SignerCertificate.Thumbprint -ne $CertificateThumbprint) {
        throw "Untrusted release signature: $Path"
    }
}
$ExeName = 'CarServicePointG.exe'
$SetupBaseName = "CarServicePointG_Setup_x86_x64_v$Version"
$SetupName = "$SetupBaseName.exe"
$OutputDir = Join-Path $ProjectRoot 'Output'
if (Test-Path -LiteralPath (Join-Path $OutputDir $SetupName)) { throw "Release $Version already exists. Assign a new version; overwriting releases is prohibited." }
$QtCMake = Join-Path $QtRoot 'bin\qt-cmake.bat'
$WinDeployQt = Join-Path $QtRoot 'bin\windeployqt.exe'

if (!(Test-Path -LiteralPath $QtCMake)) { throw "qt-cmake.bat not found: $QtCMake" }
if (!(Test-Path -LiteralPath $WinDeployQt)) { throw "windeployqt.exe not found: $WinDeployQt" }

$VsWhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (!(Test-Path -LiteralPath $VsWhere)) { throw "vswhere.exe not found: $VsWhere" }
$VsInstall = & $VsWhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if ([string]::IsNullOrWhiteSpace($VsInstall)) { throw 'Visual Studio x64 C++ toolset was not found.' }
$VcVars = Join-Path $VsInstall 'VC\Auxiliary\Build\vcvars64.bat'
if (!(Test-Path -LiteralPath $VcVars)) { throw "vcvars64.bat not found: $VcVars" }

$InnoCandidates = @(
    (Join-Path ${env:ProgramFiles(x86)} 'Inno Setup 6\ISCC.exe'),
    (Join-Path ${env:ProgramFiles} 'Inno Setup 6\ISCC.exe')
) | Where-Object { $_ -and (Test-Path -LiteralPath $_) }
if (!$InnoCandidates -or $InnoCandidates.Count -eq 0) { throw 'Inno Setup 6 ISCC.exe was not found.' }
$ISCC = $InnoCandidates[0]

Write-Host "=== CarService PointG $Version x64 release build ==="
Write-Host "PROJECT: $ProjectRoot"
Write-Host "VS:      $VsInstall"
Write-Host "Qt:      $QtRoot"
Write-Host "ISCC:    $ISCC"

if (!(Test-Path -LiteralPath (Join-Path $ProjectRoot $IssFile))) { throw "ISS file not found: $IssFile" }
foreach ($RequiredReleaseInput in @(
    'resources\assets\app_icon.png',
    'resources\assets\app_icon.ico',
    'resources\assets\app_shortcut.png',
    'resources\assets\app_shortcut.ico',
    'resources\assets\app_icon_2_1.png',
    'resources\assets\app_icon_2_1.ico',
    'resources\assets\app_shortcut_2_1.png',
    'resources\assets\app_shortcut_2_1.ico',
    'resources\windows\app_icon.ico',
    'scripts\install_ocr_runtime.ps1',
    'scripts\check_ocr_runtime.ps1',
    'github_release_manifest.json',
    'GITHUB_RELEASE_UPLOAD_LIST_2.1.txt'
)) {
    $RequiredPath = Join-Path $ProjectRoot $RequiredReleaseInput
    if (!(Test-Path -LiteralPath $RequiredPath)) {
        throw "Required release input missing: $RequiredReleaseInput"
    }
}

$UpdateContractGate = Join-Path $ProjectRoot 'scripts\check_update_release_contract.py'
if (!(Test-Path -LiteralPath $UpdateContractGate)) { throw "Update release contract gate not found: $UpdateContractGate" }
$PythonCmd = Get-Command python -ErrorAction SilentlyContinue
if (!$PythonCmd) { $PythonCmd = Get-Command py -ErrorAction SilentlyContinue }
if (!$PythonCmd) { throw 'Python was not found; cannot run update release contract gate.' }
& $PythonCmd.Source $UpdateContractGate
if ($LASTEXITCODE -ne 0) { throw "Update release contract preflight failed with exit code $LASTEXITCODE" }

foreach ($directory in @($BuildDir, $DistDir)) {
    $absolute = [IO.Path]::GetFullPath((Join-Path $ProjectRoot $directory))
    if (!$absolute.StartsWith($ProjectRoot.TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase)) { throw "Output must stay inside the project: $absolute" }
    if ((Test-Path -LiteralPath $absolute) -and @(Get-ChildItem -LiteralPath $absolute -Force).Count -gt 0) {
        throw "Output directory is not empty; choose a fresh directory: $absolute"
    }
}
if (!(Test-Path -LiteralPath $OutputDir)) { New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null }

$BuildScript = @"
call "$VcVars"
"$QtCMake" -S "$ProjectRoot" -B "$BuildDir" -G Ninja -DCMAKE_BUILD_TYPE=$Configuration -DCS_UPDATE_SIGNER_THUMBPRINT=$CertificateThumbprint
if errorlevel 1 exit /b 1
cmake --build "$BuildDir" --config $Configuration --parallel
"@
cmd.exe /d /s /c $BuildScript
if ($LASTEXITCODE -ne 0) { throw "CMake/Ninja build failed with exit code $LASTEXITCODE" }

# Strictly locate the real Qt application executable.
# Do not perform a generic "largest *.exe" search: the build tree also contains
# OCR/PDF runtime installers such as gs10070w64.exe, and copying one of those as
# CarServicePointG.exe makes windeployqt fail with "does not seem to be a Qt executable".
$BlockedExePathRegex = '\\(CMakeFiles|resources|ocr-layer|installers|dist|Output)\\'
$PreferredExePaths = @(
    (Join-Path $BuildDir $ExeName),
    (Join-Path $BuildDir "Release\$ExeName"),
    (Join-Path $BuildDir "src\$ExeName"),
    (Join-Path $BuildDir "app\$ExeName")
)

$BuiltExe = $null
foreach ($PreferredExePath in $PreferredExePaths) {
    if (Test-Path -LiteralPath $PreferredExePath) {
        $BuiltExe = Get-Item -LiteralPath $PreferredExePath
        break
    }
}

if (!$BuiltExe) {
    $BuiltExe = Get-ChildItem -Path $BuildDir -Recurse -Filter $ExeName -File |
        Where-Object { $_.FullName -notmatch $BlockedExePathRegex } |
        Sort-Object LastWriteTime -Descending |
        Select-Object -First 1
}

if (!$BuiltExe) { throw "$ExeName not found under $BuildDir" }
if ($BuiltExe.FullName -match $BlockedExePathRegex) {
    throw "Unsafe EXE candidate rejected: $($BuiltExe.FullName)"
}

$VersionInfo = (Get-Item -LiteralPath $BuiltExe.FullName).VersionInfo
if ($VersionInfo.OriginalFilename -and $VersionInfo.OriginalFilename -ne $ExeName) {
    throw "Unsafe EXE candidate rejected by OriginalFilename: $($BuiltExe.FullName) => $($VersionInfo.OriginalFilename)"
}

New-Item -ItemType Directory -Force -Path $DistDir | Out-Null
Copy-Item -LiteralPath $BuiltExe.FullName -Destination (Join-Path $DistDir $ExeName) -Force
if ([version]$VersionInfo.ProductVersion -ne [version]$Version) { throw 'Built executable version does not match the release manifest.' }
Sign-ReleaseFile (Join-Path $DistDir $ExeName)
Write-Host "APP EXE: $($BuiltExe.FullName)" -ForegroundColor Green

& $WinDeployQt --release --compiler-runtime --no-opengl-sw --dir $DistDir (Join-Path $DistDir $ExeName)
if ($LASTEXITCODE -ne 0) { throw "windeployqt failed with exit code $LASTEXITCODE" }

# Preserve runtime resources explicitly. Do not strip assets, OCR/PDF helper files, scripts or bundled Python vendor modules.
$ResourceCopies = @(
    @{ Source = 'resources\assets'; Destination = 'resources\assets' },
    @{ Source = 'resources\python_vendor'; Destination = 'resources\python_vendor' },
    @{ Source = 'resources\windows\ocr-layer'; Destination = 'resources\windows\ocr-layer' }
)
foreach ($item in $ResourceCopies) {
    $src = Join-Path $ProjectRoot $item.Source
    if (Test-Path -LiteralPath $src) {
        $dst = Join-Path $DistDir $item.Destination
        New-Item -ItemType Directory -Force -Path $dst | Out-Null
        Copy-Item -Path (Join-Path $src '*') -Destination $dst -Recurse -Force
    }
}

$RuntimeScripts = @('install_ocr_runtime.ps1', 'check_ocr_runtime.ps1', 'prepare_ocr_runtime_from_installers.ps1')
New-Item -ItemType Directory -Force -Path (Join-Path $DistDir 'scripts') | Out-Null
foreach ($name in $RuntimeScripts) {
    Copy-Item -LiteralPath (Join-Path $ProjectRoot "scripts\$name") -Destination (Join-Path $DistDir "scripts\$name")
}

$RedistCandidates = @(
    (Join-Path $QtRoot 'bin\vc_redist.x64.exe'),
    (Join-Path $ProjectRoot 'vc_redist.x64.exe'),
    (Join-Path $ProjectRoot 'resources\windows\vc_redist.x64.exe')
) | Where-Object { $_ -and (Test-Path -LiteralPath $_) }
if ($RedistCandidates -and $RedistCandidates.Count -gt 0) {
    Copy-Item -LiteralPath $RedistCandidates[0] -Destination (Join-Path $DistDir 'vc_redist.x64.exe') -Force
}

& $PythonCmd.Source (Join-Path $ProjectRoot 'scripts\check_runtime_package.py') --runtime $DistDir --write-manifest
if ($LASTEXITCODE -ne 0) { throw 'Runtime package validation failed.' }
& $ISCC "/DRuntimeDir=$([IO.Path]::GetFullPath($DistDir))" $IssFile
if ($LASTEXITCODE -ne 0) { throw "ISCC failed with exit code $LASTEXITCODE" }

$InstallerPath = Join-Path $OutputDir $SetupName
if (!(Test-Path -LiteralPath $InstallerPath)) { throw "Installer was not created: $InstallerPath" }
Sign-ReleaseFile $InstallerPath

$MakeAssets = Join-Path $ProjectRoot 'scripts\make_github_release_assets_v21.ps1'
if (!(Test-Path -LiteralPath $MakeAssets)) { throw "GitHub release asset maker not found: $MakeAssets" }
& $MakeAssets -ProjectRoot $ProjectRoot
if ($LASTEXITCODE -ne 0) { throw "GitHub release asset generation failed with exit code $LASTEXITCODE" }

$InstallerShaPath = "$InstallerPath.sha256"
$SumsPath = Join-Path $OutputDir 'SHA256SUMS.txt'
foreach ($ReleaseOutput in @($InstallerPath, $InstallerShaPath, $SumsPath)) {
    if (!(Test-Path -LiteralPath $ReleaseOutput)) { throw "Required GitHub release asset missing after asset generation: $ReleaseOutput" }
}
& $PythonCmd.Source $UpdateContractGate --final
if ($LASTEXITCODE -ne 0) { throw "Update release contract final gate failed with exit code $LASTEXITCODE" }

Copy-Item -LiteralPath $SumsPath -Destination (Join-Path $ProjectRoot 'SHA256SUMS.txt') -Force

Write-Host ""
Write-Host "=== RELEASE FILES ==="
Write-Host "INSTALLER: $InstallerPath"
Write-Host "SHA256:    $InstallerShaPath"
Write-Host "SUMS:      $SumsPath"
Write-Host ""
Write-Host "Upload to GitHub Release tag v${Version}:"
Write-Host " - $InstallerPath"
Write-Host " - $InstallerShaPath"
Write-Host " - $SumsPath"
