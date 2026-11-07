param(
    [string]$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
)
$ErrorActionPreference = 'Stop'
Set-Location $ProjectRoot
$out = Join-Path $ProjectRoot 'Output'
$manifestPath = Join-Path $ProjectRoot 'github_release_manifest.json'
$releaseVersion = [string](Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json).version
if ($releaseVersion -notmatch '^\d+\.\d+\.\d+(\.\d+)?$') { throw 'Invalid release version' }
$installerName = "CarServicePointG_Setup_x86_x64_v$releaseVersion.exe"
$installerPath = Join-Path $out $installerName
if (!(Test-Path -LiteralPath $installerPath)) { throw "Installer not found: $installerPath" }
if ((Get-AuthenticodeSignature -LiteralPath $installerPath).Status -ne 'Valid') { throw 'Cannot publish an unsigned or untrusted installer.' }

function Write-ShaFile([string]$filePath, [string]$displayName) {
    $hash = (Get-FileHash -LiteralPath $filePath -Algorithm SHA256).Hash.ToLowerInvariant()
    [System.IO.File]::WriteAllText($filePath + '.sha256', $hash + '  ' + $displayName + [Environment]::NewLine, [System.Text.Encoding]::ASCII)
    return $hash
}

$hash = Write-ShaFile $installerPath $installerName
$sumPath = Join-Path $out 'SHA256SUMS.txt'
[System.IO.File]::WriteAllText($sumPath, $hash + '  ' + $installerName + [Environment]::NewLine, [System.Text.Encoding]::ASCII)

$manifestPath = Join-Path $ProjectRoot 'github_release_manifest.json'
if (Test-Path -LiteralPath $manifestPath) {
    $manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
    $manifest.asset_sha256 = $hash
    $manifest | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath $manifestPath -Encoding UTF8
    Write-Host "Manifest asset_sha256 updated: $manifestPath" -ForegroundColor Green
}

Write-Host "GitHub release assets ready:" -ForegroundColor Green
Write-Host "  $installerPath"
Write-Host "  $($installerPath).sha256"
Write-Host "  $sumPath"
