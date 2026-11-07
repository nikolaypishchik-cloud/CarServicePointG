param(
    [string]$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path,
    [int]$InstallerTimeoutSeconds = 180,
    [switch]$SkipThirdPartyInstallers
)
$ErrorActionPreference = 'Stop'
$ocrRoot = Join-Path $ProjectRoot 'resources\windows\ocr-layer'
$installers = Join-Path $ocrRoot 'installers'
$tessInstaller = Join-Path $installers 'tesseract-ocr-w64-setup-5.5.0.20241111.exe'
$gsInstaller = Join-Path $installers 'gs10070w64.exe'
New-Item -ItemType Directory -Force -Path $ocrRoot | Out-Null

Write-Host "OCR root: $ocrRoot" -ForegroundColor Cyan

function Copy-TreeIfExists([string]$src, [string]$dst) {
    if (Test-Path -LiteralPath $src) {
        New-Item -ItemType Directory -Force -Path $dst | Out-Null
        Copy-Item -LiteralPath (Join-Path $src '*') -Destination $dst -Recurse -Force -ErrorAction Stop
        return $true
    }
    return $false
}

function Invoke-InstallerWithTimeout([string]$FilePath, [string[]]$Arguments, [string]$Name) {
    if ($SkipThirdPartyInstallers) {
        Write-Warning "$Name installer skipped by -SkipThirdPartyInstallers."
        return $false
    }
    if (!(Test-Path -LiteralPath $FilePath)) {
        Write-Warning "$Name installer not found: $FilePath"
        return $false
    }
    Write-Host "Running $Name installer with timeout ${InstallerTimeoutSeconds}s..." -ForegroundColor Yellow
    $p = Start-Process -FilePath $FilePath -ArgumentList $Arguments -WindowStyle Hidden -PassThru
    if (-not $p.WaitForExit($InstallerTimeoutSeconds * 1000)) {
        try { Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue } catch {}
        throw "$Name installer timeout after ${InstallerTimeoutSeconds}s. Do not put this installer into [Run]; prepare portable files before packaging or skip this engine."
    }
    if ($p.ExitCode -ne 0) {
        Write-Warning "$Name installer exit code: $($p.ExitCode)"
        return $false
    }
    return $true
}

# Tesseract: prepare app-local files on the build PC, not during end-user setup.
$tessDst = Join-Path $ocrRoot 'Tesseract-OCR'
if (!(Test-Path -LiteralPath (Join-Path $tessDst 'tesseract.exe'))) {
    New-Item -ItemType Directory -Force -Path $tessDst | Out-Null
    Invoke-InstallerWithTimeout -FilePath $tessInstaller -Arguments @('/S', "/D=$tessDst") -Name 'Tesseract' | Out-Null
}

# Ghostscript: prepare app-local files on the build PC, not during end-user setup.
$gsDst = Join-Path $ocrRoot 'ghostscript'
$gsBinCandidates = @(
    (Join-Path $gsDst 'bin\gswin64c.exe'),
    (Join-Path $gsDst 'gs10.07.0\bin\gswin64c.exe'),
    (Join-Path $gsDst 'gs10.07.1\bin\gswin64c.exe'),
    (Join-Path $gsDst 'gs10.06.0\bin\gswin64c.exe')
)
if (-not ($gsBinCandidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1)) {
    New-Item -ItemType Directory -Force -Path $gsDst | Out-Null
    Invoke-InstallerWithTimeout -FilePath $gsInstaller -Arguments @('/S', "/D=$gsDst") -Name 'Ghostscript' | Out-Null
}

# Copy already-installed engines from Program Files when installers are absent or extraction was skipped.
if (!(Test-Path -LiteralPath (Join-Path $tessDst 'tesseract.exe'))) {
    Copy-TreeIfExists 'C:\Program Files\Tesseract-OCR' $tessDst | Out-Null
}

$gsReady = $false
foreach ($p in $gsBinCandidates) { if (Test-Path -LiteralPath $p) { $gsReady = $true; break } }
if (-not $gsReady) {
    foreach ($c in @('C:\Program Files\gs\gs10.07.0','C:\Program Files\gs\gs10.07.1','C:\Program Files\gs\gs10.06.0')) {
        if (Test-Path -LiteralPath (Join-Path $c 'bin\gswin64c.exe')) {
            New-Item -ItemType Directory -Force -Path (Join-Path $gsDst 'bin') | Out-Null
            Copy-Item -Path (Join-Path $c 'bin\*') -Destination (Join-Path $gsDst 'bin') -Force
            if (Test-Path -LiteralPath (Join-Path $c 'lib')) {
                New-Item -ItemType Directory -Force -Path (Join-Path $gsDst 'lib') | Out-Null
                Copy-Item -Path (Join-Path $c 'lib\*') -Destination (Join-Path $gsDst 'lib') -Recurse -Force
            }
            break
        }
    }
}

# Final hard verification for release packaging. This intentionally runs before ISCC, not at install time.
& (Join-Path $ProjectRoot 'scripts\check_ocr_runtime.ps1') -AppRoot $ProjectRoot
