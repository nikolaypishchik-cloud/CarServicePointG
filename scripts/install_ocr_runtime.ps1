$ErrorActionPreference = 'Stop'

param(
    [string]$AppRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path,
    [switch]$SkipTesseractInstall,
    [switch]$Force
)

function Write-Step([string]$m) { Write-Host "[OCR] $m" -ForegroundColor Cyan }
function Get-Sha256([string]$p) { (Get-FileHash -Algorithm SHA256 -LiteralPath $p).Hash.ToLowerInvariant() }
function Ensure-Dir([string]$p) { New-Item -ItemType Directory -Force -Path $p | Out-Null }
function Test-IsWindowsAppsShim([string]$PathValue) {
    if ([string]::IsNullOrWhiteSpace($PathValue)) { return $false }
    try {
        $normalized = ([string]$PathValue).Replace('/', '\\').ToLowerInvariant()
        return ($normalized -match '\\microsoft\\windowsapps\\' -or $normalized -match '\\windowsapps\\')
    } catch { return $false }
}
function Download-File([string]$Url, [string]$OutFile, [string]$ExpectedHash) {
    if ((Test-Path $OutFile) -and -not $Force) {
        $existing = Get-Sha256 $OutFile
        if ($existing -eq $ExpectedHash.ToLowerInvariant()) {
            Write-Step "Using cached file: $([IO.Path]::GetFileName($OutFile))"
            return
        }
    }
    Write-Step "Downloading $Url"
    Invoke-WebRequest -Uri $Url -OutFile $OutFile
    $actual = Get-Sha256 $OutFile
    if ($actual -ne $ExpectedHash.ToLowerInvariant()) {
        throw "SHA256 mismatch for $OutFile. Expected $ExpectedHash, got $actual"
    }
}

$appRoot = (Resolve-Path $AppRoot).Path
$ocrLayerDir = Join-Path $appRoot 'resources\windows\ocr-layer'
$manifestPath = Join-Path $ocrLayerDir 'manifest.json'
if (!(Test-Path $manifestPath)) { throw "Manifest not found: $manifestPath" }
$manifest = Get-Content $manifestPath -Raw | ConvertFrom-Json

$downloads = Join-Path $ocrLayerDir 'downloads'
$popplerDir = Join-Path $ocrLayerDir 'poppler'
$naps2Dir = Join-Path $ocrLayerDir 'NAPS2'
Ensure-Dir $downloads
Ensure-Dir $popplerDir
Ensure-Dir $naps2Dir
Ensure-Dir $ocrLayerDir

$popplerZip = Join-Path $downloads ([IO.Path]::GetFileName($manifest.poppler.url))
Download-File $manifest.poppler.url $popplerZip $manifest.poppler.sha256
$popplerExtract = Join-Path $downloads ('poppler_extract_' + $manifest.poppler.version)
if (Test-Path $popplerExtract) { Remove-Item $popplerExtract -Recurse -Force }
Ensure-Dir $popplerExtract
Write-Step 'Extracting Poppler'
Expand-Archive -Path $popplerZip -DestinationPath $popplerExtract -Force
$libraryRoot = Get-ChildItem $popplerExtract -Directory | Where-Object { $_.Name -like 'poppler-*' } | Select-Object -First 1
if (-not $libraryRoot) { throw 'Poppler archive format not recognized.' }
$libraryPath = Join-Path $libraryRoot.FullName 'Library'
Copy-Item (Join-Path $libraryPath '*') $popplerDir -Recurse -Force

$naps2Zip = Join-Path $downloads ([IO.Path]::GetFileName($manifest.naps2.url))
Download-File $manifest.naps2.url $naps2Zip $manifest.naps2.sha256
$naps2Extract = Join-Path $downloads ('naps2_extract_' + $manifest.naps2.version)
if (Test-Path $naps2Extract) { Remove-Item $naps2Extract -Recurse -Force }
Ensure-Dir $naps2Extract
Write-Step 'Extracting NAPS2 portable archive'
Expand-Archive -Path $naps2Zip -DestinationPath $naps2Extract -Force
$portableRoot = Get-ChildItem $naps2Extract -Directory | Select-Object -First 1
if (-not $portableRoot) { throw 'NAPS2 archive format not recognized.' }
Copy-Item (Join-Path $portableRoot.FullName '*') $naps2Dir -Recurse -Force

$env:CS_PDF_OCR_LAYER = $ocrLayerDir
[Environment]::SetEnvironmentVariable('CS_PDF_OCR_LAYER', $ocrLayerDir, 'User')
Write-Step "Set CS_PDF_OCR_LAYER=$ocrLayerDir"

$tessRootCandidates = @(
    (Join-Path ${env:ProgramFiles} 'Tesseract-OCR'),
    (Join-Path ${env:ProgramFiles(x86)} 'Tesseract-OCR')
) | Where-Object { $_ -and $_.Trim() -ne '' }
$tessRoot = $tessRootCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1

if (-not $SkipTesseractInstall -and -not $tessRoot) {
    $tessExe = Join-Path $downloads ([IO.Path]::GetFileName($manifest.tesseract.url))
    Download-File $manifest.tesseract.url $tessExe $manifest.tesseract.sha256
    Write-Step 'Running Tesseract installer in silent mode'
    $p = Start-Process -FilePath $tessExe -ArgumentList '/S' -Wait -PassThru
    if ($p.ExitCode -ne 0) { throw "Tesseract installer failed with exit code $($p.ExitCode)" }
    $tessRoot = $tessRootCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
}
if (-not $tessRoot) {
    throw 'Tesseract was not found after install. Install manually or rerun without -SkipTesseractInstall.'
}

$tessdataDir = Join-Path $tessRoot 'tessdata'
Ensure-Dir $tessdataDir
foreach ($lang in $manifest.tessdata_fast.languages) {
    $dst = Join-Path $tessdataDir ($lang + '.traineddata')
    if ((Test-Path $dst) -and -not $Force) { continue }
    $url = "$($manifest.tessdata_fast.base_url)/$lang.traineddata"
    Write-Step "Downloading tessdata: $lang"
    Invoke-WebRequest -Uri $url -OutFile $dst
}

$tessExePath = Join-Path $tessRoot 'tesseract.exe'
if (!(Test-Path $tessExePath)) { throw "tesseract.exe not found in $tessRoot" }
$naps2Console = Join-Path $naps2Dir 'NAPS2.Console.exe'
if (!(Test-Path $naps2Console)) { throw "NAPS2.Console.exe not found in $naps2Dir" }
foreach ($lang in @('eng','rus','ukr')) {
    Write-Step "Installing NAPS2 OCR language: $lang"
    $p = Start-Process -FilePath $naps2Console -ArgumentList @('--install', ('ocr-' + $lang)) -Wait -PassThru
    if ($p.ExitCode -ne 0) { throw "NAPS2 OCR language install failed for $lang with exit code $($p.ExitCode)" }
}

$tessDataLayer = Join-Path $ocrLayerDir 'tessdata'
Ensure-Dir $tessDataLayer
Copy-Item (Join-Path $tessdataDir '*.traineddata') $tessDataLayer -Force -ErrorAction SilentlyContinue

$pythonCmd = (Get-Command py -ErrorAction SilentlyContinue | Select-Object -First 1 -ExpandProperty Source)
$pythonArgs = @('-3')
if (Test-IsWindowsAppsShim $pythonCmd) { $pythonCmd = $null }
if (-not $pythonCmd) {
    $pythonCmd = (Get-Command python -ErrorAction SilentlyContinue | Select-Object -First 1 -ExpandProperty Source)
    $pythonArgs = @()
    if (Test-IsWindowsAppsShim $pythonCmd) { $pythonCmd = $null }
}
if ($pythonCmd) {
    $pySite = Join-Path $ocrLayerDir 'python\site-packages'
    Ensure-Dir $pySite
    Write-Step 'Installing local Python PDF packages into OCR layer'
    $pipArgs = @()
    if ($pythonArgs.Count -gt 0) { $pipArgs += $pythonArgs }
    $pipArgs += @('-m','pip','install','--upgrade','--target',$pySite,'pypdf','pdfminer.six','pdfplumber')
    $p = Start-Process -FilePath $pythonCmd -ArgumentList $pipArgs -Wait -PassThru -NoNewWindow
    if ($p.ExitCode -ne 0) {
        Write-Warning 'pip install for local PDF packages failed; OCR runtime will continue without Python PDF packages.'
    }
} else {
    Write-Warning 'Python not found. Local Python PDF packages were not installed.'
}

Write-Step 'Smoke check: pdftoppm'
& (Join-Path $popplerDir 'bin\pdftoppm.exe') -v | Out-Host
Write-Step 'Smoke check: pdftotext'
& (Join-Path $popplerDir 'bin\pdftotext.exe') -v | Out-Host
Write-Step 'Smoke check: tesseract'
& $tessExePath --version | Out-Host
Write-Step 'Smoke check: NAPS2.Console'
& $naps2Console --help | Out-Null

Write-Step 'OCR runtime is ready.'
Write-Host "Poppler: $popplerDir" -ForegroundColor Green
Write-Host "NAPS2: $naps2Dir" -ForegroundColor Green
Write-Host "Tesseract: $tessRoot" -ForegroundColor Green
Write-Host "Layer env: CS_PDF_OCR_LAYER=$ocrLayerDir" -ForegroundColor Green
if (Test-Path (Join-Path $ocrLayerDir 'python\site-packages\pypdf')) { Write-Host 'Python PDF packages: installed' -ForegroundColor Green }
