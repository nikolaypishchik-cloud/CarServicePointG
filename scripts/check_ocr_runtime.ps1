param([string]$AppRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path, [switch]$SoftCheck)
$ErrorActionPreference = 'Stop'
$ocrRoot = Join-Path $AppRoot 'resources\windows\ocr-layer'
function HasFile([string[]]$Paths) { foreach ($p in $Paths) { if (Test-Path -LiteralPath $p) { return $true } }; return $false }
function FirstFile([string[]]$Paths) { foreach ($p in $Paths) { if (Test-Path -LiteralPath $p) { return $p } }; return $null }
$popplerTextPath = FirstFile @((Join-Path $ocrRoot 'poppler\bin\pdftotext.exe'), (Join-Path $ocrRoot 'xpdf\bin\pdftotext.exe'))
$popplerRenderPath = FirstFile @((Join-Path $ocrRoot 'poppler\bin\pdftoppm.exe'), (Join-Path $ocrRoot 'xpdf\bin\pdftoppm.exe'))
$popplerHtmlPath = FirstFile @((Join-Path $ocrRoot 'poppler\bin\pdftohtml.exe'), (Join-Path $ocrRoot 'xpdf\bin\pdftohtml.exe'))
$mutoolPath = FirstFile @((Join-Path $ocrRoot 'mupdf\mutool.exe'), (Join-Path $ocrRoot 'mupdf\bin\mutool.exe'), (Join-Path $ocrRoot 'mutool.exe'))
$ghostscriptPath = FirstFile @(
    (Join-Path $ocrRoot 'ghostscript\bin\gswin64c.exe'),
    (Join-Path $ocrRoot 'ghostscript\gs10.07.0\bin\gswin64c.exe'),
    (Join-Path $ocrRoot 'ghostscript\gs10.07.1\bin\gswin64c.exe'),
    (Join-Path $ocrRoot 'ghostscript\gs10.06.0\bin\gswin64c.exe'),
    (Join-Path $ocrRoot 'gs\bin\gswin64c.exe'),
    (Join-Path $ocrRoot 'gswin64c.exe')
)
$pdfiumPath = FirstFile @((Join-Path $ocrRoot 'pdfium\bin\pdfium_test.exe'), (Join-Path $ocrRoot 'pdfium_test.exe'))
$naps2Path = FirstFile @((Join-Path $ocrRoot 'NAPS2\NAPS2.Console.exe'), (Join-Path $ocrRoot 'NAPS2\App\NAPS2.Console.exe'))
$tesseractPath = FirstFile @((Join-Path $ocrRoot 'Tesseract-OCR\tesseract.exe'), (Join-Path $ocrRoot 'tesseract\tesseract.exe'), (Join-Path $ocrRoot 'tesseract\bin\tesseract.exe'), (Join-Path $ocrRoot 'tesseract.exe'))
$tessdataDir = $null
foreach ($d in @((Join-Path $ocrRoot 'Tesseract-OCR\tessdata'), (Join-Path $ocrRoot 'tesseract\tessdata'), (Join-Path $ocrRoot 'tesseract\share\tessdata'), (Join-Path $ocrRoot 'tessdata'))) {
    if ((Test-Path (Join-Path $d 'ukr.traineddata')) -and (Test-Path (Join-Path $d 'rus.traineddata')) -and (Test-Path (Join-Path $d 'eng.traineddata'))) { $tessdataDir = $d; break }
}
$textEngines = @()
if ($popplerTextPath) { $textEngines += "Poppler pdftotext=$popplerTextPath" }
if ($mutoolPath) { $textEngines += "MuPDF mutool=$mutoolPath" }
if ($ghostscriptPath) { $textEngines += "Ghostscript=$ghostscriptPath" }
if ($pdfiumPath) { $textEngines += "PDFium=$pdfiumPath" }
$ocrRenderEngines = @()
if ($popplerRenderPath) { $ocrRenderEngines += "Poppler pdftoppm=$popplerRenderPath" }
if ($mutoolPath) { $ocrRenderEngines += "MuPDF render=$mutoolPath" }
if ($ghostscriptPath) { $ocrRenderEngines += "Ghostscript render=$ghostscriptPath" }
$ocrReady = ($tesseractPath -and $tessdataDir -and (($ocrRenderEngines.Count -gt 0) -or $naps2Path))
$hardReady = (($textEngines.Count -ge 2) -and $ocrReady)
Write-Host "OCR root: $ocrRoot"
Write-Host ('Text engines: ' + ($(if ($textEngines.Count) { $textEngines -join '; ' } else { '<none>' })))
Write-Host ('OCR render engines: ' + ($(if ($ocrRenderEngines.Count) { $ocrRenderEngines -join '; ' } else { '<none>' })))
Write-Host "Tesseract: $tesseractPath"
Write-Host "Tessdata ukr/rus/eng: $tessdataDir"
Write-Host "NAPS2: $naps2Path"
if (-not $hardReady) {
    Write-Host 'PDF import runtime is incomplete for offline any-PC import.' -ForegroundColor Red
    Write-Host 'Required minimum: at least 2 text engines + Tesseract+tessdata + at least 1 PDF render engine or NAPS2.' -ForegroundColor Red
    Write-Host 'If installers are present, run scripts\prepare_ocr_runtime_from_installers.ps1 before final installer build.' -ForegroundColor Yellow
    if ($SoftCheck) {
        Write-Host 'SoftCheck mode: installation will continue; PDF import may use available engines only.' -ForegroundColor Yellow
        exit 0
    }
    exit 1
}
Write-Host 'PDF import runtime is complete for offline fallback chain.' -ForegroundColor Green
exit 0
