// Extracted from src/core/import/InvoiceImportEngine.cpp for P1/P2 monolith reduction.
// Included exactly once by the original translation unit; generated split boundary is outside C++ raw strings.

    script += QString::fromUtf8(R"PS(    $tmpRoot = Join-Path ([System.IO.Path]::GetTempPath()) ('cs_pdf_ocr_' + [Guid]::NewGuid().ToString('N'))
    $imgPrefix = Join-Path $tmpRoot 'page'
    New-Item -ItemType Directory -Force -Path $tmpRoot | Out-Null
    try {
        $render = Start-Process -FilePath $pdftoppm -ArgumentList @('-r', '300', '-png', $PdfPath, $imgPrefix) -Wait -PassThru -NoNewWindow
        if ($render.ExitCode -ne 0) { return $null }
        $images = Get-ChildItem -LiteralPath $tmpRoot -Filter 'page-*.png' -File | Sort-Object Name
        if (-not $images -or (Get-SafeCount $images) -eq 0) { return $null }
        $parts = New-Object System.Collections.Generic.List[string]
        foreach ($img in $images) {
            $ocrBase = [System.IO.Path]::Combine($tmpRoot, [System.IO.Path]::GetFileNameWithoutExtension($img.Name) + '_ocr')
            $layer = Resolve-OcrLayerPaths
            $ocrArgs = @($img.FullName, $ocrBase)
            if (-not [string]::IsNullOrWhiteSpace($layer.tessdata)) {
                $ocrArgs += @('--tessdata-dir', $layer.tessdata)
            }
            $ocrArgs += @('-l', 'ukr+rus+eng', '--psm', '6')
            $ocr = Start-Process -FilePath $tesseract -ArgumentList $ocrArgs -Wait -PassThru -NoNewWindow
            if ($ocr.ExitCode -ne 0) { continue }
            $txt = Read-TextIfExists ($ocrBase + '.txt')
            if (-not [string]::IsNullOrWhiteSpace($txt)) {
                [void]$parts.Add($txt)
            }
        }
        if ((Get-SafeCount $parts) -gt 0) {
            return @{ text = (($parts.ToArray()) -join "`n"); engine = 'ocr(pdftoppm+tesseract)' }
        }
    } catch { }
    finally {
        if (Test-Path $tmpRoot) { Remove-Item $tmpRoot -Recurse -Force -ErrorAction SilentlyContinue }
    }
    return $null
}


function Get-WinRtAsTaskMethod([bool]$Generic) {
    try { Add-Type -AssemblyName System.Runtime.WindowsRuntime -ErrorAction SilentlyContinue } catch { }
    $methods = [System.WindowsRuntimeSystemExtensions].GetMethods() | Where-Object {
        $ok = ($_.Name -eq 'AsTask' -and $_.GetParameters().Count -eq 1 -and $_.IsGenericMethodDefinition -eq $Generic)
        if ($ok -and $Generic) {
            # There are also IAsyncOperationWithProgress overloads with two generic
            # arguments. Selecting them and then calling MakeGenericMethod with one
            # type silently broke the Windows OCR path for scanned PDF invoices.
            $ok = ($_.GetGenericArguments().Count -eq 1 -and $_.GetParameters()[0].ParameterType.Name -like 'IAsyncOperation*')
        } elseif ($ok) {
            $ok = ($_.GetParameters()[0].ParameterType.Name -eq 'IAsyncAction')
        }
        $ok
    }
    return @($methods | Select-Object -First 1)[0]
}

function Await-WinRtOperation($Operation, [Type]$ResultType) {
    if ($null -eq $Operation) { return $null }
    $method = Get-WinRtAsTaskMethod $true
    if ($null -eq $method) { return $null }
    try {
        $task = $method.MakeGenericMethod($ResultType).Invoke($null, @($Operation))
        $task.Wait()
        return $task.Result
    } catch {
        Write-Diagnostic ("[windows-media-ocr] winrt_operation_error=" + (Format-ExceptionDeep $_.Exception))
        return $null
    }
}

function Await-WinRtAction($Action) {
    if ($null -eq $Action) { return }
    $method = Get-WinRtAsTaskMethod $false
    if ($null -eq $method) { return }
    try {
        $task = $method.Invoke($null, @($Action))
        $task.Wait()
    } catch {
        Write-Diagnostic ("[windows-media-ocr] winrt_action_error=" + (Format-ExceptionDeep $_.Exception))
    }
}

function New-WindowsOcrEngine() {
    $engine = $null
    foreach ($langName in @('uk', 'ru', 'en')) {
        try {
            $lang = [Windows.Globalization.Language]::new($langName)
            if ([Windows.Media.Ocr.OcrEngine]::IsLanguageSupported($lang)) {
                $engine = [Windows.Media.Ocr.OcrEngine]::TryCreateFromLanguage($lang)
                if ($null -ne $engine) { return $engine }
            }
        } catch { }
    }
    try { $engine = [Windows.Media.Ocr.OcrEngine]::TryCreateFromUserProfileLanguages() } catch { }
    if ($null -eq $engine) { Write-Diagnostic "[windows-media-ocr] no_supported_ocr_language" }
    return $engine
}

function Read-WindowsMediaOcrImageText([string]$ImagePath, $Engine) {
    if ([string]::IsNullOrWhiteSpace($ImagePath) -or $null -eq $Engine) { return $null }
    $stream = $null
    try {
        $file = Await-WinRtOperation ([Windows.Storage.StorageFile]::GetFileFromPathAsync($ImagePath)) ([Windows.Storage.StorageFile])
        if ($null -eq $file) { return $null }
        $stream = Await-WinRtOperation ($file.OpenAsync([Windows.Storage.FileAccessMode]::Read)) ([Windows.Storage.Streams.IRandomAccessStream])
        if ($null -eq $stream) { return $null }
        $decoder = Await-WinRtOperation ([Windows.Graphics.Imaging.BitmapDecoder]::CreateAsync($stream)) ([Windows.Graphics.Imaging.BitmapDecoder])
        if ($null -eq $decoder) { return $null }
        $bitmap = Await-WinRtOperation ($decoder.GetSoftwareBitmapAsync()) ([Windows.Graphics.Imaging.SoftwareBitmap])
        if ($null -eq $bitmap) { return $null }
        try {
            $converted = [Windows.Graphics.Imaging.SoftwareBitmap]::Convert(
                $bitmap,
                [Windows.Graphics.Imaging.BitmapPixelFormat]::Bgra8,
                [Windows.Graphics.Imaging.BitmapAlphaMode]::Ignore)
            if ($null -ne $converted) { $bitmap = $converted }
        } catch { }
        $ocr = Await-WinRtOperation ($Engine.RecognizeAsync($bitmap)) ([Windows.Media.Ocr.OcrResult])
        if ($null -eq $ocr -or $null -eq $ocr.Lines) { return $null }
        $lines = New-Object System.Collections.Generic.List[string]
        foreach ($line in $ocr.Lines) {
            if ($null -ne $line -and -not [string]::IsNullOrWhiteSpace($line.Text)) {
                [void]$lines.Add([string]$line.Text)
            }
        }
        if ((Get-SafeCount $lines) -gt 0) { return (($lines.ToArray()) -join "`n") }
    } catch {
        Write-Diagnostic ("[windows-media-image-ocr] image_error=" + (Format-ExceptionDeep $_.Exception) + " path=" + $ImagePath)
    }
    finally {
        try { if ($stream -and ($stream -is [System.IDisposable])) { $stream.Dispose() } } catch { }
    }
    return $null
}

function Try-WindowsMediaImageOcr([string]$ImageDir) {
    Add-Attempt 'windows-media-image-ocr'
    if ([string]::IsNullOrWhiteSpace($ImageDir) -or -not (Test-Path $ImageDir)) { return $null }
    try {
        [Windows.Storage.StorageFile, Windows.Storage, ContentType = WindowsRuntime] | Out-Null
        [Windows.Storage.FileAccessMode, Windows.Storage, ContentType = WindowsRuntime] | Out-Null
        [Windows.Storage.Streams.IRandomAccessStream, Windows.Storage.Streams, ContentType = WindowsRuntime] | Out-Null
        [Windows.Graphics.Imaging.BitmapDecoder, Windows.Graphics.Imaging, ContentType = WindowsRuntime] | Out-Null
        [Windows.Graphics.Imaging.SoftwareBitmap, Windows.Graphics.Imaging, ContentType = WindowsRuntime] | Out-Null
        [Windows.Media.Ocr.OcrEngine, Windows.Media.Ocr, ContentType = WindowsRuntime] | Out-Null
        [Windows.Globalization.Language, Windows.Globalization, ContentType = WindowsRuntime] | Out-Null

        $engine = New-WindowsOcrEngine
        if ($null -eq $engine) { return $null }
        $images = Get-ChildItem -LiteralPath $ImageDir -File -ErrorAction SilentlyContinue |
            Where-Object { $_.Extension -match '^\.(png|jpg|jpeg|bmp|tif|tiff)$' } |
            Sort-Object Name
        if (-not $images -or (Get-SafeCount $images) -eq 0) {
            Write-Diagnostic ("[windows-media-image-ocr] no_rendered_images=" + $ImageDir)
            return $null
        }
        $parts = New-Object System.Collections.Generic.List[string]
        foreach ($img in $images) {
            $txt = Read-WindowsMediaOcrImageText $img.FullName $engine
            if (-not [string]::IsNullOrWhiteSpace($txt)) { [void]$parts.Add($txt) }
        }
        if ((Get-SafeCount $parts) -gt 0) {
            return @{ text = (($parts.ToArray()) -join "`n"); engine = 'windows-media-image-ocr' }
        }
    } catch {
        Write-Diagnostic ("[windows-media-image-ocr] stderr=" + (Format-ExceptionDeep $_.Exception))
    }
    return $null
}

function Try-WindowsMediaOcr([string]$PdfPath) {
    Add-Attempt 'windows-media-ocr'
    $tmpPdfForWinRt = $null
    $winRtPdfPath = $PdfPath
    try {
        [Windows.Storage.StorageFile, Windows.Storage, ContentType = WindowsRuntime] | Out-Null
        [Windows.Data.Pdf.PdfDocument, Windows.Data.Pdf, ContentType = WindowsRuntime] | Out-Null
        [Windows.Data.Pdf.PdfPageRenderOptions, Windows.Data.Pdf, ContentType = WindowsRuntime] | Out-Null
        [Windows.Storage.Streams.InMemoryRandomAccessStream, Windows.Storage.Streams, ContentType = WindowsRuntime] | Out-Null
        [Windows.Graphics.Imaging.BitmapDecoder, Windows.Graphics.Imaging, ContentType = WindowsRuntime] | Out-Null
        [Windows.Graphics.Imaging.SoftwareBitmap, Windows.Graphics.Imaging, ContentType = WindowsRuntime] | Out-Null
        [Windows.Media.Ocr.OcrEngine, Windows.Media.Ocr, ContentType = WindowsRuntime] | Out-Null
        [Windows.Globalization.Language, Windows.Globalization, ContentType = WindowsRuntime] | Out-Null

        try {
            # WinRT PDF loading is more reliable from a short ASCII temp path
            # than from Downloads paths with localized characters, cloud sync
            # placeholders, or long Unicode names.
            $tmpPdfForWinRt = Join-Path ([System.IO.Path]::GetTempPath()) ('cs_pdf_winocr_' + [Guid]::NewGuid().ToString('N') + '.pdf')
            Copy-Item -LiteralPath $PdfPath -Destination $tmpPdfForWinRt -Force -ErrorAction Stop
            if (Test-Path $tmpPdfForWinRt) { $winRtPdfPath = $tmpPdfForWinRt }
        } catch {
            Write-Diagnostic ("[windows-media-ocr] temp_copy_warning=" + (Format-ExceptionDeep $_.Exception))
            $winRtPdfPath = $PdfPath
        }

        $file = Await-WinRtOperation ([Windows.Storage.StorageFile]::GetFileFromPathAsync($winRtPdfPath)) ([Windows.Storage.StorageFile])
        if ($null -eq $file) { return $null }
        $doc = Await-WinRtOperation ([Windows.Data.Pdf.PdfDocument]::LoadFromFileAsync($file)) ([Windows.Data.Pdf.PdfDocument])
        if ($null -eq $doc -or $doc.PageCount -le 0) { return $null }

        $engine = New-WindowsOcrEngine
        if ($null -eq $engine) { return $null }

        $parts = New-Object System.Collections.Generic.List[string]
        for ($i = 0; $i -lt [int]$doc.PageCount; $i++) {
            $page = $null
            $stream = $null
            try {
                $page = $doc.GetPage([uint32]$i)
                if ($null -eq $page) { continue }
                $stream = [Windows.Storage.Streams.InMemoryRandomAccessStream]::new()
                $options = [Windows.Data.Pdf.PdfPageRenderOptions]::new()
                try {
                    $width = [double]$page.Size.Width
                    if ($width -gt 0) {
                        $renderWidth = [Math]::Max(1400, [Math]::Min(2600, [int]($width * 2.75)))
                        $options.DestinationWidth = [uint32]$renderWidth
                    }
                } catch { }
                Await-WinRtAction ($page.RenderToStreamAsync($stream, $options))
                $stream.Seek(0) | Out-Null
                $decoder = Await-WinRtOperation ([Windows.Graphics.Imaging.BitmapDecoder]::CreateAsync($stream)) ([Windows.Graphics.Imaging.BitmapDecoder])
                if ($null -eq $decoder) { continue }
                $bitmap = Await-WinRtOperation ($decoder.GetSoftwareBitmapAsync()) ([Windows.Graphics.Imaging.SoftwareBitmap])
                if ($null -eq $bitmap) { continue }
                try {
                    # Windows OCR accepts only OCR-compatible SoftwareBitmap formats.
                    # PDF decoder output can be another format/alpha mode, so normalize
)PS");
    script += QString::fromUtf8(R"PS(                    # before RecognizeAsync instead of treating the page as unreadable.
                    $converted = [Windows.Graphics.Imaging.SoftwareBitmap]::Convert(
                        $bitmap,
                        [Windows.Graphics.Imaging.BitmapPixelFormat]::Bgra8,
                        [Windows.Graphics.Imaging.BitmapAlphaMode]::Ignore)
                    if ($null -ne $converted) { $bitmap = $converted }
                } catch { }
                $ocr = Await-WinRtOperation ($engine.RecognizeAsync($bitmap)) ([Windows.Media.Ocr.OcrResult])
                if ($null -eq $ocr -or $null -eq $ocr.Lines) { continue }
                foreach ($line in $ocr.Lines) {
                    if ($null -ne $line -and -not [string]::IsNullOrWhiteSpace($line.Text)) {
                        [void]$parts.Add([string]$line.Text)
                    }
                }
            } catch {
                Write-Diagnostic ("[windows-media-ocr] page_error=" + (Format-ExceptionDeep $_.Exception))
            }
            finally {
                try { if ($stream -and ($stream -is [System.IDisposable])) { $stream.Dispose() } } catch { }
                try { if ($page -and ($page -is [System.IDisposable])) { $page.Dispose() } } catch { }
            }
        }
        if ((Get-SafeCount $parts) -gt 0) {
            return @{ text = (($parts.ToArray()) -join "`n"); engine = 'windows-media-ocr' }
        }
    } catch {
        Write-Diagnostic ("[windows-media-ocr] stderr=" + (Format-ExceptionDeep $_.Exception))
    }
    finally {
        try {
            if (-not [string]::IsNullOrWhiteSpace($tmpPdfForWinRt) -and (Test-Path $tmpPdfForWinRt)) {
                Remove-Item $tmpPdfForWinRt -Force -ErrorAction SilentlyContinue
            }
        } catch { }
    }
    return $null
}
)PS");
    script += QString::fromUtf8(R"PS(

$text = Try-PdfToText $Path
$engine = $null
if (-not [string]::IsNullOrWhiteSpace($text)) {
    $engine = 'pdftotext'
}
if ([string]::IsNullOrWhiteSpace($text)) {
    $text = Try-MutoolText $Path
    if (-not [string]::IsNullOrWhiteSpace($text)) { $engine = 'mutool' }
}
if ([string]::IsNullOrWhiteSpace($text)) {
    $basicText = Try-BasicPdfText $Path
    if (-not [string]::IsNullOrWhiteSpace($basicText)) {
        $eliteStructured = Convert-BasicElitePdfToStructuredText -PdfPath $Path -BasicText $basicText
        if (-not [string]::IsNullOrWhiteSpace($eliteStructured)) {
            $text = $eliteStructured
            $engine = 'elite-basic-structured'
        } else {
            $text = $basicText
            $engine = 'basic-pdf-text'
        }
    }
}
if ([string]::IsNullOrWhiteSpace($text)) {
    $pythonResult = Try-PythonPdf $Path
    if ($pythonResult -and -not [string]::IsNullOrWhiteSpace($pythonResult.text)) {
        $text = [string]$pythonResult.text
        $engine = [string]$pythonResult.engine
    }
}
if ([string]::IsNullOrWhiteSpace($text)) {
    $imageOcrResult = Try-WindowsMediaImageOcr $RenderedImageDir
    if ($imageOcrResult -and -not [string]::IsNullOrWhiteSpace($imageOcrResult.text)) {
        $text = [string]$imageOcrResult.text
        $engine = [string]$imageOcrResult.engine
    }
}
if ([string]::IsNullOrWhiteSpace($text)) {
    $winOcrResult = Try-WindowsMediaOcr $Path
    if ($winOcrResult -and -not [string]::IsNullOrWhiteSpace($winOcrResult.text)) {
        $text = [string]$winOcrResult.text
        $engine = [string]$winOcrResult.engine
    }
}
if ([string]::IsNullOrWhiteSpace($text)) {
    $naps2Result = Try-Naps2Ocr $Path
    if ($naps2Result -and -not [string]::IsNullOrWhiteSpace($naps2Result.text)) {
        $text = [string]$naps2Result.text
        $engine = [string]$naps2Result.engine
    }
}
if ([string]::IsNullOrWhiteSpace($text)) {
    $ocrResult = Try-OcrFallback $Path
    if ($ocrResult -and -not [string]::IsNullOrWhiteSpace($ocrResult.text)) {
        $text = [string]$ocrResult.text
        $engine = [string]$ocrResult.engine
    }
}
if ([string]::IsNullOrWhiteSpace($text)) {
    $engines = if ((Get-SafeCount $script:AttemptedEngines) -gt 0) { ($script:AttemptedEngines.ToArray() -join ', ') } else { 'QtPdf' }
    $layer = Resolve-OcrLayerPaths
    $hasOcrTools = ($layer.pdftoppm -and $layer.tesseract) -or $layer.naps2 -or $layer.pdftotext -or $layer.mutool
    if (-not [string]::IsNullOrWhiteSpace($layer.root)) {
        if ($hasOcrTools) {
            $layerHint = " OCR layer: $($layer.root)."
        } elseif (-not [string]::IsNullOrWhiteSpace($layer.pySite)) {
            $layerHint = " OCR layer знайдено: $($layer.root), Python PDF packages: $($layer.pySite), але немає повного OCR runtime (Poppler/Tesseract/NAPS2)."
        } else {
            $layerHint = " OCR layer знайдено: $($layer.root), але в ньому немає OCR executables."
        }
    } else {
        $layerHint = " OCR layer не знайдено поруч із застосунком."
    }
    throw ("Не вдалося прочитати PDF накладної жодним рушієм. Перевірено: {0}.{1} Для scanned/PDF-image накладних потрібен текстовий PDF, Windows OCR або app-local OCR layer поруч із застосунком: Poppler/pdftotext, MuPDF/mutool, Tesseract+tessdata або NAPS2.Console. Python і PowerShell більше не є єдиною гілкою імпорту: застосунок спочатку пробує вбудований QtPdf та локальні exe-рушії. Якщо OCR runtime ще не встановлено, запустіть scripts\install_ocr_runtime.ps1 або покладіть portable Poppler/MuPDF/Tesseract у resources\windows\ocr-layer." -f $engines, $layerHint)
}
Emit-Json $text $engine
)PS");
    return script;
}


static QString normalizePdfInvoiceDateToIso(QString text)
{
    text = text.trimmed();
    if (text.isEmpty()) return {};
    const QString normalized = PartImportTextSupport::normalizeImportedDateDisplay(text).trimmed();
    static const QRegularExpression shortDot(QStringLiteral(R"((\d{2})\.(\d{2})\.(\d{2}))"));
    static const QRegularExpression longDot(QStringLiteral(R"((\d{2})\.(\d{2})\.(\d{4}))"));
    const auto shortMatch = shortDot.match(normalized);
    if (shortMatch.hasMatch()) {
        const QDate d(2000 + shortMatch.captured(3).toInt(), shortMatch.captured(2).toInt(), shortMatch.captured(1).toInt());
        if (d.isValid()) return d.toString(Qt::ISODate);
    }
    const auto longMatch = longDot.match(normalized);
    if (longMatch.hasMatch()) {
        int year = longMatch.captured(3).toInt();
        if (year >= 1900 && year < 2000) year += 100;
        const QDate d(year, longMatch.captured(2).toInt(), longMatch.captured(1).toInt());
        if (d.isValid()) return d.toString(Qt::ISODate);
    }

    static const QMap<QString, int> months = {
        {QString::fromUtf8("січня"), 1}, {QString::fromUtf8("лютого"), 2}, {QString::fromUtf8("березня"), 3},
        {QString::fromUtf8("квітня"), 4}, {QString::fromUtf8("травня"), 5}, {QString::fromUtf8("червня"), 6},
        {QString::fromUtf8("липня"), 7}, {QString::fromUtf8("серпня"), 8}, {QString::fromUtf8("вересня"), 9},
        {QString::fromUtf8("жовтня"), 10}, {QString::fromUtf8("листопада"), 11}, {QString::fromUtf8("грудня"), 12}
    };
    const QRegularExpression rx(QStringLiteral(R"((\d{1,2})\s+([\p{L}]+)\s+(\d{4}))"), QRegularExpression::CaseInsensitiveOption);
    const auto m = rx.match(text.toLower());
    if (!m.hasMatch()) return {};
    const int day = m.captured(1).toInt();
    const int month = months.value(m.captured(2), 0);
    const int year = m.captured(3).toInt();
    const QDate parsed(year, month, day);
    return parsed.isValid() ? parsed.toString(Qt::ISODate) : QString();
}

static QString cleanPdfInvoiceCode(QString value)
{
    value.replace(QChar(0x00A0), QLatin1Char(' '));
    value = value.simplified();
    value.remove(QRegularExpression(QStringLiteral(R"(^[\|:;,_\-\s]+|[\|:;,_\-\s]+$)")));
    return value.trimmed();
}

static QStringList splitPdfInvoiceLines(QString text)
{
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    text.replace(QChar('\r'), QChar('\n'));
    QStringList raw = text.split(QChar('\n'));
    QStringList lines;
    for (QString line : raw) {
        line.replace(QChar(0x00A0), QLatin1Char(' '));
        line = line.simplified();
        if (!line.isEmpty()) lines << line;
    }
    return lines;
}

static QStringList splitPdfInvoiceLayoutLines(QString text)
{
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    text.replace(QChar('\r'), QChar('\n'));
    QStringList raw = text.split(QChar('\n'));
    QStringList lines;
    for (QString line : raw) {
        line.replace(QChar(0x00A0), QLatin1Char(' '));
        line.replace(QChar('\t'), QLatin1Char(' '));
        while (!line.isEmpty() && (line.endsWith(QLatin1Char(' ')) || line.endsWith(QLatin1Char('\t')))) {
            line.chop(1);
        }
        if (!line.trimmed().isEmpty()) lines << line;
    }
    return lines;
}

struct ElitePdfLayoutColumns {
    int headerStart = -1;
    int bodyStart = -1;
    int oem = -1;
    int alt = -1;
    int name = -1;
    int unit = -1;
    int price = -1;
    int qty = -1;
    int amount = -1;

    bool valid() const {
        return headerStart >= 0 && bodyStart >= 0 && oem >= 0 && name >= 0 && price >= 0 && qty >= 0;
    }
};

static QString buildElitePdfLayoutOverlay(const QStringList& lines, int start, int count)
{
    QString overlay;
    const int end = qMin(lines.size(), start + count);
    for (int i = start; i < end; ++i) {
        const QString& line = lines.at(i);
        if (line.size() > overlay.size()) overlay = overlay.leftJustified(line.size(), QLatin1Char(' '), true);
        for (int j = 0; j < line.size(); ++j) {
            const QChar ch = line.at(j);
            if (!ch.isSpace()) overlay[j] = ch;
        }
    }
    return overlay;
}

static int findElitePdfLayoutColumn(const QString& loweredOverlay, const QStringList& probes)
{
    int best = -1;
    for (const QString& probe : probes) {
        if (probe.trimmed().isEmpty()) continue;
        const int idx = loweredOverlay.indexOf(probe);
        if (idx >= 0 && (best < 0 || idx < best)) best = idx;
    }
    return best;
}

static ElitePdfLayoutColumns detectElitePdfLayoutColumns(const QStringList& layoutLines)
{
    ElitePdfLayoutColumns cols;
    for (int i = 0; i < layoutLines.size(); ++i) {
        const QString normalized = normalizedImportHeader(layoutLines.at(i));
        if (!isElitePdfTableHeaderAnchor(normalized)) continue;

        const QString overlay = buildElitePdfLayoutOverlay(layoutLines, i, 4);
        const QString lowered = overlay.toLower();
        const int oem = findElitePdfLayoutColumn(lowered, {QString::fromUtf8("активн"), QString::fromUtf8("код товару")});
        const int alt = findElitePdfLayoutColumn(lowered, {QString::fromUtf8("альтерн")});
        const int name = findElitePdfLayoutColumn(lowered, {QString::fromUtf8("наймен")});
        const int unit = findElitePdfLayoutColumn(lowered, {QString::fromUtf8("од."), QString::fromUtf8("од "), QString::fromUtf8("вим")});
        const int priceWithVat = findElitePdfLayoutColumn(lowered, {QString::fromUtf8("ціна з"), QString::fromUtf8("цiна з")});
        const int price = priceWithVat >= 0
            ? priceWithVat
            : findElitePdfLayoutColumn(lowered, {QString::fromUtf8("ціна")});
        const int qty = findElitePdfLayoutColumn(lowered, {QString::fromUtf8("кіл"), QString::fromUtf8("к-сть"), QString::fromUtf8("кільк")});
        const int amount = findElitePdfLayoutColumn(lowered, {QString::fromUtf8("разом")});

        QSet<int> starts;
        for (int value : {oem, alt, name, unit, price, qty, amount}) {
            if (value >= 0) starts.insert(value);
        }
        if (oem < 0 || name < 0 || price < 0 || qty < 0 || starts.size() < 4) continue;

        cols.headerStart = i;
        cols.bodyStart = qMin(layoutLines.size(), i + 1);
        while (cols.bodyStart < qMin(layoutLines.size(), i + 5)) {
            const QString probeNormalized = normalizedImportHeader(layoutLines.at(cols.bodyStart));
            if (!isElitePdfTableHeaderSignal(probeNormalized)) break;
            ++cols.bodyStart;
        }
        cols.oem = oem;
        cols.alt = alt;
        cols.name = name;
        cols.unit = unit;
        cols.price = price;
        cols.qty = qty;
        cols.amount = amount >= 0 ? amount : -1;
        return cols;
    }
    return cols;
}

static QString elitePdfLayoutSlice(QString line, int start, int end)
{
    if (start < 0) return QString();
    if (end >= 0 && end < start) return QString();
    const int targetWidth = end > start ? end : qMax(start + 1, line.size());
    if (line.size() < targetWidth) line = line.leftJustified(targetWidth, QLatin1Char(' '), true);
    const QString sliced = (end > start) ? line.mid(start, end - start) : line.mid(start);
    return sliced.simplified();
}

static QString firstPdfInvoiceNumberFromText(const QString& text)
{
    const QRegularExpression rx(QStringLiteral(R"((\d[\d\s]*[\.,]\d{2}))"));
    const auto match = rx.match(text);
    return match.hasMatch() ? match.captured(1).trimmed() : QString();
}

static QString lastPdfInvoiceNumberFromText(const QString& text)
{
    const QRegularExpression rx(QStringLiteral(R"((\d[\d\s]*[\.,]\d{2}))"));
    QRegularExpressionMatchIterator it = rx.globalMatch(text);
    QString last;
    while (it.hasNext()) {
        last = it.next().captured(1).trimmed();
    }
    return last;
}

static QString formatElitePdfMoneyText(double value)
{
    const double rounded = std::round(qMax(0.0, value) * 100.0) / 100.0;
    QString text = QString::number(rounded, 'f', 2);
    text.replace(QLatin1Char('.'), QLatin1Char(','));
    return text;
}

static QString elitePdfAmountWithVatText(const QString& priceWithVat, const QString& qty, const QString& fallback = QString())
{
    bool priceOk = false;
    bool qtyOk = false;
    const double priceValue = parseLooseNumberFallback(priceWithVat, &priceOk);
    const double qtyValue = parseLooseNumberFallback(qty, &qtyOk);
    if (!priceOk || !qtyOk || priceValue <= 0.0 || qtyValue <= 0.0) {
        return fallback.trimmed();
    }
    return formatElitePdfMoneyText(priceValue * qtyValue);
}

static bool elitePdfLooksLikeVatPricePair(const QString& priceWithoutVat, const QString& priceWithVat)
{
    bool withoutOk = false;
    bool withOk = false;
    const double withoutValue = parseLooseNumberFallback(priceWithoutVat, &withoutOk);
    const double withValue = parseLooseNumberFallback(priceWithVat, &withOk);
    if (!withoutOk || !withOk || withoutValue <= 0.0 || withValue <= 0.0) return false;
    if (withValue <= withoutValue) return false;
    const double expected = withoutValue * 1.20;
    return qAbs(withValue - expected) <= qMax(0.10, expected * 0.03);
}

static bool isElitePdfStandardBrandToken(QString token)
{
    token = token.trimmed();
    token.remove(QRegularExpression(QStringLiteral(R"(^[^\p{L}]+|[^\p{L}]+$)")));
    if (token.size() < 2 || token.size() > 32) return false;
    if (token.contains(QRegularExpression(QStringLiteral(R"(\d)")))) return false;
    if (!token.contains(QRegularExpression(QStringLiteral(R"(\p{L})")))) return false;
    if (token.contains(QRegularExpression(QStringLiteral(R"(\p{Ll})")))) return false;
    return true;
}

static bool isElitePdfStandardVendorPrefixToken(QString token)
{
    token = token.trimmed();
    token.remove(QRegularExpression(QStringLiteral(R"(^[^A-ZА-ЯІЇЄҐ]+|[^A-ZА-ЯІЇЄҐ]+$)")));
    if (token.size() < 2 || token.size() > 4) return false;
    return token.contains(QRegularExpression(QStringLiteral(R"(^[A-ZА-ЯІЇЄҐ]{2,4}$)")));
}

static bool tryParseEliteInvoiceStandardTailMetrics(const QString& tail,
                                                   QString* qtyOut,
                                                   QString* priceOut,
                                                   QString* amountOut)
{
    if (qtyOut) qtyOut->clear();
    if (priceOut) priceOut->clear();
    if (amountOut) amountOut->clear();

    const QString moneyPattern = QStringLiteral(R"((?:\d{1,3}(?:\s\d{3})+|\d+)[\.,]\d{2})");
    const QRegularExpression tailRx(QStringLiteral(R"(^\s*(%1)\s+(%1)\s+(\d+(?:[\.,]\d{1,3})?)\s+(%1)\s*$)").arg(moneyPattern));
    const QRegularExpressionMatch match = tailRx.match(tail.simplified());
    if (!match.hasMatch()) return false;

    const QString priceWithoutVat = match.captured(1).trimmed();
    const QString priceWithVat = match.captured(2).trimmed();
    const QString qty = match.captured(3).trimmed();
    const QString amountWithoutVat = match.captured(4).trimmed();

    auto parseEliteStdNumber = [](QString text, bool* ok) -> double {
        text = text.trimmed();
        text.remove(QLatin1Char(' '));
        text.replace(QLatin1Char(','), QLatin1Char('.'));
        return text.toDouble(ok);
    };

    bool qtyOk = false;
    bool priceOk = false;
    bool amountOk = false;
    bool priceWithVatOk = false;
    const double qtyValue = parseEliteStdNumber(qty, &qtyOk);
    const double priceValue = parseEliteStdNumber(priceWithoutVat, &priceOk);
    const double priceWithVatValue = parseEliteStdNumber(priceWithVat, &priceWithVatOk);
    const double amountValue = parseEliteStdNumber(amountWithoutVat, &amountOk);
    if (!qtyOk || !priceOk || !priceWithVatOk || !amountOk ||
        qtyValue <= 0.0 || priceValue <= 0.0 || priceWithVatValue <= 0.0 || amountValue <= 0.0) return false;

    const double tolerance = qMax(0.10, amountValue * 0.03);
    if (qAbs((priceValue * qtyValue) - amountValue) > tolerance) return false;

    if (qtyOut) *qtyOut = qty;
    if (priceOut) *priceOut = priceWithVat;
    if (amountOut) *amountOut = formatElitePdfMoneyText(priceWithVatValue * qtyValue);
    return true;
}

static bool tryParseEliteInvoiceStandardTextRow(const QString& rawLine,
                                               QString* oem,
                                               QString* name,
                                               QString* qty,
                                               QString* price,
                                               QString* amount)
{
    if (oem) oem->clear();
    if (name) name->clear();
    if (qty) qty->clear();
    if (price) price->clear();
    if (amount) amount->clear();

    QString line = rawLine;
    line.replace(QChar(0x00A0), QLatin1Char(' '));
    line = line.simplified();
    if (line.isEmpty()) return false;

    const QString normalized = normalizedImportHeader(line);
    if (isPdfInvoiceLikelyStopLine(normalized) || isElitePdfTableHeaderSignal(normalized)) return false;

    const QRegularExpression unitRx(QStringLiteral(R"((?:^|\s)(шт|штука|шт\.|од|од\.|компл|компл\.|к-т|л|кг|г|м|см|уп|уп\.|упак|пара|пар)(?:\s|$))"),
                                    QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatchIterator unitIt = unitRx.globalMatch(line);
    while (unitIt.hasNext()) {
        const QRegularExpressionMatch unitMatch = unitIt.next();
        const int unitStart = unitMatch.capturedStart(1);
        const int unitEnd = unitMatch.capturedEnd(1);
        if (unitStart <= 0 || unitEnd >= line.size()) continue;

        QString parsedQty;
        QString parsedPrice;
        QString parsedAmount;
        if (!tryParseEliteInvoiceStandardTailMetrics(line.mid(unitEnd).trimmed(), &parsedQty, &parsedPrice, &parsedAmount)) continue;

        QString body = line.left(unitStart).trimmed();
        const QRegularExpression ukTZedTailRx(QStringLiteral(R"(^(.*)\s+(\d{8,12})\s+[XХ]\s*$)"),
                                              QRegularExpression::CaseInsensitiveOption);
        const QRegularExpressionMatch ukTZedMatch = ukTZedTailRx.match(body);
        if (ukTZedMatch.hasMatch()) {
            body = ukTZedMatch.captured(1).trimmed();
        } else {
            body.replace(QRegularExpression(QStringLiteral(R"(\s+[XХ]\s*$)"), QRegularExpression::CaseInsensitiveOption), QString());
            body = body.trimmed();
        }
        if (body.isEmpty()) continue;

        QStringList tokens = body.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if (tokens.size() < 2) continue;

        QString brandToken;
        if (isElitePdfStandardBrandToken(tokens.constLast())) {
            brandToken = normalizePdfImportedBrandToken(tokens.takeLast());
        }
        if (tokens.size() < 2) continue;

        int nameStart = -1;
        const QRegularExpression lowerLetterRx(QStringLiteral(R"(\p{Ll})"));
        for (int i = 0; i < tokens.size(); ++i) {
            if (tokens.at(i).contains(lowerLetterRx)) {
                nameStart = i;
                break;
            }
        }
        if (nameStart <= 0) continue;

        QStringList codeTokens = tokens.mid(0, nameStart);
        QStringList nameTokens = tokens.mid(nameStart);
        if (codeTokens.isEmpty() || nameTokens.isEmpty()) continue;

        QStringList oemTokens = codeTokens;
        if (oemTokens.size() >= 2 && isElitePdfStandardVendorPrefixToken(oemTokens.constFirst())) {
            oemTokens.removeFirst();
        }
        QString parsedOem = cleanPdfInvoiceCode(oemTokens.join(QLatin1Char(' ')));
        if (parsedOem.isEmpty()) parsedOem = cleanPdfInvoiceCode(codeTokens.join(QLatin1Char(' ')));
        if (parsedOem.isEmpty() || !isElitePdfLikelyItemCode(parsedOem)) continue;

        QString parsedName = nameTokens.join(QLatin1Char(' ')).simplified();
        if (!brandToken.isEmpty()) {
            parsedName = (parsedName + QLatin1Char(' ') + brandToken).simplified();
        }
        if (!isPdfInvoiceLikelyNameLine(parsedName)) continue;

        if (oem) *oem = parsedOem;
        if (name) *name = parsedName;
        if (qty) *qty = parsedQty;
        if (price) *price = parsedPrice;
        if (amount) *amount = parsedAmount;
        return true;
    }

    return false;
}

static bool tryParseEliteInvoiceStandardTextTable(const QStringList& lines, ExcelReadResult& result)
{
    int parsedCount = 0;
    bool parsedStarted = false;

    for (const QString& rawLine : lines) {
        const QString normalized = normalizedImportHeader(rawLine);
        if (parsedStarted && isPdfInvoiceLikelyStopLine(normalized)) break;

        QString parsedOem;
        QString parsedName;
        QString parsedQty;
        QString parsedPrice;
        QString parsedAmount;
        if (!tryParseEliteInvoiceStandardTextRow(rawLine, &parsedOem, &parsedName, &parsedQty, &parsedPrice, &parsedAmount)) {
            continue;
        }

        appendPdfInvoiceRow(result,
                            QString::fromUtf8("Еліт"),
                            parsedOem,
                            parsedName,
                            parsedQty,
                            parsedPrice,
                            parsedAmount);
        parsedStarted = true;
        ++parsedCount;
    }

    return parsedCount > 0;
}

static bool tryParseEliteInvoiceVatWarehousePrefix(const QString& prefix,
                                                  QString* amountOut,
                                                  QString* qtyOut,
                                                  QString* priceOut)
{
    if (amountOut) amountOut->clear();
    if (qtyOut) qtyOut->clear();
    if (priceOut) priceOut->clear();

    const QString compactPrefix = prefix.trimmed();
    if (compactPrefix.isEmpty()) return false;

    const QRegularExpression moneyRx(QStringLiteral(R"(^\d[\d\s]*[\.,]\d{2}$)"));
    QString bestAmount;
    QString bestQty;
    QString bestPrice;
    double bestDiff = (std::numeric_limits<double>::max)();

    for (int split = 1; split < compactPrefix.size(); ++split) {
        const QString candidateAmount = compactPrefix.left(split).trimmed();
        QString rest = compactPrefix.mid(split).trimmed();
        if (candidateAmount.isEmpty() || rest.isEmpty()) continue;
        if (!moneyRx.match(candidateAmount).hasMatch()) continue;

        const int maxCandidateLength = qMin(rest.size(), 6);
        for (int candidateLength = 1; candidateLength <= maxCandidateLength; ++candidateLength) {
            const QString candidateQty = rest.left(candidateLength).trimmed();
            const QString candidatePrice = rest.mid(candidateLength).trimmed();
            if (candidateQty.isEmpty() || candidatePrice.isEmpty()) continue;
            if (!candidateQty.contains(QRegularExpression(QStringLiteral(R"(^\d+(?:[\.,]\d{1,3})?$)")))) continue;
            if (!moneyRx.match(candidatePrice).hasMatch()) continue;

            bool qtyOk = false;
            bool priceOk = false;
            bool amountOk = false;
            const double qtyValue = parseLooseNumberFallback(candidateQty, &qtyOk);
            const double priceValue = parseLooseNumberFallback(candidatePrice, &priceOk);
            const double amountValue = parseLooseNumberFallback(candidateAmount, &amountOk);
            if (!qtyOk || !priceOk || !amountOk || qtyValue <= 0.0 || priceValue <= 0.0 || amountValue <= 0.0) continue;

            const double diff = qAbs((qtyValue * priceValue) - amountValue);
            if (diff > 0.05) continue;
            if (diff + 0.0001 >= bestDiff) continue;

            bestDiff = diff;
            bestAmount = candidateAmount;
            bestQty = candidateQty;
            bestPrice = candidatePrice;
        }
    }

    if (bestAmount.isEmpty() || bestQty.isEmpty() || bestPrice.isEmpty()) return false;
    if (amountOut) *amountOut = bestAmount;
    if (qtyOut) *qtyOut = bestQty;
    if (priceOut) *priceOut = bestPrice;
    return true;
}

static bool splitElitePdfTrailingItemCode(const QString& body, QString* nameOut, QString* oemOut)
{
    if (nameOut) nameOut->clear();
    if (oemOut) oemOut->clear();

    const QString simplified = body.simplified();
    if (simplified.isEmpty()) return false;

    QStringList tokens = simplified.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (tokens.size() < 2) return false;

    const auto isTrailingCodeToken = [](const QString& token) {
        const QString trimmed = token.trimmed();
        if (trimmed.isEmpty()) return false;
        if (trimmed.contains(QRegularExpression(QStringLiteral(R"([\p{Ll}])")))) return false;
        return trimmed.contains(QRegularExpression(QStringLiteral(R"(\d)")));
    };

    int runStart = tokens.size() - 1;
    while (runStart >= 0 && isTrailingCodeToken(tokens.at(runStart))) {
        --runStart;
    }
    ++runStart;
    if (runStart <= 0 || runStart >= tokens.size()) return false;

    QStringList nameTokens = tokens.mid(0, runStart);
    QStringList codeTokens = tokens.mid(runStart);
    if (nameTokens.isEmpty() || codeTokens.isEmpty()) return false;

    {
        const QRegularExpression mergedBrandDigitsRx(QStringLiteral(R"(^([A-Z]{2,})(\d[\dA-Z./\-]*)$)"));
        const QRegularExpressionMatch match = mergedBrandDigitsRx.match(codeTokens.first());
        if (match.hasMatch()) {
            nameTokens << match.captured(1);
            codeTokens[0] = match.captured(2);
        }
    }

    if (!nameTokens.isEmpty() && !codeTokens.isEmpty() &&
        codeTokens.first().contains(QRegularExpression(QStringLiteral(R"(^\d[\dA-Z./\-]*$)")))) {
        const QString lastNameToken = nameTokens.constLast();
        if (lastNameToken.contains(QRegularExpression(QStringLiteral(R"(^[A-Z]{5,}$)")))) {
            const bool dotInCode = codeTokens.first().contains(QLatin1Char('.'));
            const QList<int> suffixLengths = dotInCode ? QList<int>{2, 3, 4} : QList<int>{3, 2, 4};
            for (int suffixLength : suffixLengths) {
                if (lastNameToken.size() <= suffixLength + 1) continue;
                const QString brandPart = lastNameToken.left(lastNameToken.size() - suffixLength);
                const QString codePrefix = lastNameToken.right(suffixLength);
                if (brandPart.size() < 2) continue;
                const QString candidateOem = cleanPdfInvoiceCode(codePrefix + QLatin1Char(' ') + codeTokens.join(QLatin1Char(' ')));
                if (!isElitePdfLikelyItemCode(candidateOem)) continue;
                nameTokens.removeLast();
                nameTokens << brandPart;
                codeTokens.prepend(codePrefix);
                break;
            }
        }
    }

    if (codeTokens.size() > 3) return false;
    const QString lastCodeToken = codeTokens.constLast().trimmed();
    if (lastCodeToken.size() < 4 && !lastCodeToken.contains(QLatin1Char('.'))) return false;

    const QString parsedName = nameTokens.join(QLatin1Char(' ')).simplified();
    const QString parsedOem = cleanPdfInvoiceCode(codeTokens.join(QLatin1Char(' ')));
    if (parsedName.isEmpty() || parsedOem.isEmpty() || !isElitePdfLikelyItemCode(parsedOem)) return false;
    if (!isPdfInvoiceLikelyNameLine(parsedName)) return false;

    if (nameOut) *nameOut = parsedName;
    if (oemOut) *oemOut = parsedOem;
    return true;
}

static bool tryParseEliteInvoiceVatWarehouseRow(const QString& rawLine,
                                                QString* oem,
                                                QString* name,
                                                QString* qty,
                                                QString* price,
                                                QString* amount)
{
    if (oem) oem->clear();
    if (name) name->clear();
    if (qty) qty->clear();
    if (price) price->clear();
    if (amount) amount->clear();

    QString line = rawLine;
    line.replace(QChar(0x00A0), QLatin1Char(' '));
    line = line.simplified();
    if (line.isEmpty()) return false;

    const QRegularExpression unitRx(QStringLiteral(R"((шт|штука|шт\.|од|од\.|компл|компл\.|к-т|л|кг|г|м|см|уп|уп\.|упак|пара|пар))"),
                                    QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatchIterator unitIt = unitRx.globalMatch(line);
    while (unitIt.hasNext()) {
        const QRegularExpressionMatch unitMatch = unitIt.next();
        const int unitStart = unitMatch.capturedStart(1);
        const int unitEnd = unitMatch.capturedEnd(1);
        if (unitStart <= 0 || unitEnd >= line.size()) continue;
        const QChar prev = line.at(unitStart - 1);
        const QChar next = line.at(unitEnd);
        if (!prev.isDigit()) continue;
        if (!next.isLetter()) continue;

        QString parsedAmount;
        QString parsedQty;
        QString parsedPrice;
        if (!tryParseEliteInvoiceVatWarehousePrefix(line.left(unitStart), &parsedAmount, &parsedQty, &parsedPrice)) continue;

        const QString afterUnit = line.mid(unitEnd).trimmed();
        const QRegularExpression taxTailRx(QStringLiteral(R"(^(.*)\s*X\s*(\d{8,10})$)"));
        const QRegularExpressionMatch taxTailMatch = taxTailRx.match(afterUnit);
        if (!taxTailMatch.hasMatch()) continue;

        const QString beforeVat = taxTailMatch.captured(1).trimmed();
        const QRegularExpression moneyRx(QStringLiteral(R"(^\d[\d\s]*[\.,]\d{2}$)"));
        QString parsedName;
        QString parsedOem;
        QString parsedPriceWithVat;
        bool bodyMatched = false;
        for (int split = beforeVat.size() - 1; split > 0; --split) {
            const QString candidateBody = beforeVat.left(split).trimmed();
            const QString candidateVat = beforeVat.mid(split).trimmed();
            if (candidateBody.isEmpty() || candidateVat.isEmpty()) continue;
            if (!moneyRx.match(candidateVat).hasMatch()) continue;
            if (!splitElitePdfTrailingItemCode(candidateBody, &parsedName, &parsedOem)) continue;
            parsedPriceWithVat = candidateVat;
            bodyMatched = true;
            break;
        }
        if (!bodyMatched || parsedPriceWithVat.isEmpty()) continue;

        bool qtyOk = false;
        bool priceOk = false;
        bool amountOk = false;
        bool priceWithVatOk = false;
        const double qtyValue = parseLooseNumberFallback(parsedQty, &qtyOk);
        const double priceValue = parseLooseNumberFallback(parsedPrice, &priceOk);
        const double priceWithVatValue = parseLooseNumberFallback(parsedPriceWithVat, &priceWithVatOk);
        const double amountValue = parseLooseNumberFallback(parsedAmount, &amountOk);
        if (!qtyOk || !priceOk || !priceWithVatOk || !amountOk ||
            qtyValue <= 0.0 || priceValue <= 0.0 || priceWithVatValue <= 0.0 || amountValue <= 0.0) continue;
        if (qAbs((qtyValue * priceValue) - amountValue) > 0.05) continue;

        if (oem) *oem = parsedOem;
        if (name) *name = parsedName;
        if (qty) *qty = parsedQty;
        if (price) *price = parsedPriceWithVat;
        if (amount) *amount = formatElitePdfMoneyText(priceWithVatValue * qtyValue);
        return true;
    }

    return false;
}

static bool tryParseEliteInvoiceVatWarehouseTable(const QStringList& lines, ExcelReadResult& result)
{
    const int headerStart = findElitePdfTableHeaderStart(lines);
    if (headerStart < 0) return false;

    QStringList bodyLines;
    bool bodyStarted = false;
    for (int i = headerStart; i < lines.size(); ++i) {
        const QString line = lines.at(i).trimmed();
        const QString normalized = normalizedImportHeader(line);
        if (line.isEmpty()) continue;
        if (isElitePdfTableHeaderSignal(normalized)) continue;
        if (isPdfInvoiceLikelyStopLine(normalized) && bodyStarted) break;
        bodyStarted = true;
        bodyLines << line;
    }
    if (bodyLines.isEmpty()) return false;

    int parsedCount = 0;
    for (const QString& bodyLine : bodyLines) {
        QString parsedOem;
        QString parsedName;
        QString parsedQty;
        QString parsedPrice;
        QString parsedAmount;
        if (!tryParseEliteInvoiceVatWarehouseRow(bodyLine, &parsedOem, &parsedName, &parsedQty, &parsedPrice, &parsedAmount)) continue;
        appendPdfInvoiceRow(result, QString::fromUtf8("Еліт"), parsedOem, parsedName, parsedQty, parsedPrice, parsedAmount);
        ++parsedCount;
    }

    return parsedCount >= 2;
}

static bool tryParseEliteInvoiceWideVatLayoutRow(const QString& rawLine,
                                                 QString* oem,
                                                 QString* name,
                                                 QString* qty,
                                                 QString* price,
                                                 QString* amount)
{
    if (oem) oem->clear();
    if (name) name->clear();
    if (qty) qty->clear();
