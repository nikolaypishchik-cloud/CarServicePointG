// Extracted from src/core/import/InvoiceImportEngine.cpp for P1/P2 monolith reduction.
// Included exactly once by the original translation unit; generated split boundary is outside C++ raw strings.

        add(QDir(root).filePath(QStringLiteral("python.exe")));
        add(QDir(root).filePath(QStringLiteral("Python/python.exe")));
    }

    add(QStandardPaths::findExecutable(QStringLiteral("python.exe")));
    add(QStandardPaths::findExecutable(QStringLiteral("py.exe")));

    const QString localAppData = qEnvironmentVariable("LOCALAPPDATA");
    if (!localAppData.trimmed().isEmpty()) {
        const QDir pyRoot(QDir(localAppData).filePath(QStringLiteral("Programs/Python")));
        const QFileInfoList versions = pyRoot.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name | QDir::Reversed);
        for (const QFileInfo& fi : versions) {
            add(QDir(fi.absoluteFilePath()).filePath(QStringLiteral("python.exe")));
        }
    }

    for (const QString& root : QStringList{qEnvironmentVariable("ProgramFiles"), qEnvironmentVariable("ProgramFiles(x86)"), appDir}) {
        if (root.trimmed().isEmpty()) continue;
        add(QDir(root).filePath(QStringLiteral("Python/python.exe")));
        add(QDir(root).filePath(QStringLiteral("Python312/python.exe")));
        add(QDir(root).filePath(QStringLiteral("Python311/python.exe")));
        add(QDir(root).filePath(QStringLiteral("Python310/python.exe")));
    }
    return candidates;
}


static QStringList candidatePythonExecutablesForSpreadsheetImport(const QString& appDir)
{
    QStringList candidates = candidatePythonExecutablesForPdfImport(appDir);
    auto add = [&](const QString& path) {
        if (path.trimmed().isEmpty()) return;
        const QString clean = QDir::toNativeSeparators(QDir::cleanPath(path));
        if (clean.toLower().contains(QStringLiteral("\\windowsapps\\"))) return;
        if (!QFileInfo::exists(clean) && QFileInfo(clean).isAbsolute()) return;
        if (!candidates.contains(clean)) candidates << clean;
    };

    const QString ocrLayerRoot = qEnvironmentVariable("CS_PDF_OCR_LAYER").trimmed();
    if (!ocrLayerRoot.isEmpty()) {
        add(QDir(ocrLayerRoot).filePath(QStringLiteral("python/python.exe")));
        add(QDir(ocrLayerRoot).filePath(QStringLiteral("python.exe")));
    }

    const QString username = qEnvironmentVariable("USERNAME").trimmed();
    if (!username.isEmpty()) {
        add(QStringLiteral("C:/Users/%1/AppData/Local/Programs/Python/Python313/python.exe").arg(username));
        add(QStringLiteral("C:/Users/%1/AppData/Local/Programs/Python/Python312/python.exe").arg(username));
        add(QStringLiteral("C:/Users/%1/AppData/Local/Programs/Python/Python311/python.exe").arg(username));
        add(QStringLiteral("C:/Users/%1/AppData/Local/Programs/Python/Python310/python.exe").arg(username));
        add(QStringLiteral("C:/Users/%1/AppData/Local/Programs/Python/Python39/python.exe").arg(username));
        add(QStringLiteral("C:/Users/%1/AppData/Local/Programs/Python/Python38/python.exe").arg(username));
        add(QStringLiteral("C:/Users/%1/AppData/Local/Programs/Python/Launcher/py.exe").arg(username));
    }

    // The py launcher is intentionally deprioritized because in some Windows setups it can
    // block on interactive resolution and stall the import path.
    add(QStandardPaths::findExecutable(QStringLiteral("py.exe")));
    add(QStandardPaths::findExecutable(QStringLiteral("py")));
    return candidates;
}

static QString extractPdfTextViaDirectPython(const QString& filePath,
                                             const QString& appDir,
                                             QString* engineOut = nullptr,
                                             QString* errorText = nullptr)
{
#if defined(Q_OS_WIN)
    if (rejectBlockingImportProcessOnUiThread(QStringLiteral("pdf_direct_python"), errorText)) {
        return {};
    }

    const QStringList launchers = candidatePythonExecutablesForPdfImport(appDir);
    if (launchers.isEmpty()) {
        if (errorText) *errorText = QString::fromUtf8("Не знайдено python.exe для прямого читання PDF.");
        return {};
    }

    static const char* kPythonCode = R"PY(
import sys, re, zlib, json
path = sys.argv[1]

def emit(text, engine):
    if not text or not text.strip():
        return False
    sys.stdout.write(json.dumps({'text': text, 'engine': engine}, ensure_ascii=False))
    return True

def basic_pdf_extract(pdf_path):
    data = open(pdf_path, 'rb').read()
    out = []
    for m in re.finditer(rb'stream\r?\n', data):
        start = m.end()
        end = data.find(b'endstream', start)
        if end < 0:
            continue
        chunk = data[start:end].rstrip(b'\r\n')
        try:
            dec = zlib.decompress(chunk)
        except Exception:
            continue
        if b'BT' not in dec or (b'Tj' not in dec and b'TJ' not in dec):
            continue
        text = dec.decode('latin1', 'ignore')
        for token in re.findall(r'\((?:\\.|[^\)])*\)\s*Tj', text):
            s = token[:-2] if token.endswith('Tj') else token
            out.append(s)
        for block in re.findall(r'\[(.*?)\]\s*TJ', text, re.S):
            for token in re.findall(r'\((?:\\.|[^\)])*\)', block):
                out.append(token)
    if not out:
        return ''
    merged = '\n'.join(out)
    merged = merged.replace('\\(', '(').replace('\\)', ')').replace('\\n', '\n').replace('\\r', '\n')
    merged = re.sub(r'\\([0-7]{3})', lambda m: chr(int(m.group(1), 8)), merged)
    merged = merged.replace('(', '').replace(')', '')
    merged = re.sub(r'\n{3,}', '\n\n', merged)
    return merged

for name in ('pypdf', 'PyPDF2'):
    try:
        mod = __import__(name)
        reader_cls = getattr(mod, 'PdfReader', None)
        if reader_cls is None:
            continue
        reader = reader_cls(path)
        parts = []
        for page in reader.pages:
            txt = ''
            for mode in ('layout', None):
                try:
                    if mode is None:
                        txt = page.extract_text() or ''
                    else:
                        txt = page.extract_text(extraction_mode=mode) or ''
                except TypeError:
                    continue
                except Exception:
                    txt = ''
                if txt and txt.strip():
                    break
            if txt and txt.strip():
                parts.append(txt)
        if emit('\n'.join(parts), name):
            raise SystemExit(0)
    except SystemExit:
        raise
    except Exception:
        pass

try:
    import pdfplumber
    parts = []
    with pdfplumber.open(path) as pdf:
        for page in pdf.pages:
            try:
                txt = page.extract_text(x_tolerance=1, y_tolerance=1) or ''
            except Exception:
                txt = ''
            if txt and txt.strip():
                parts.append(txt)
    if emit('\n'.join(parts), 'pdfplumber'):
        raise SystemExit(0)
except SystemExit:
    raise
except Exception:
    pass

try:
    from pdfminer.high_level import extract_text
    txt = extract_text(path) or ''
    if emit(txt, 'pdfminer.six'):
        raise SystemExit(0)
except SystemExit:
    raise
except Exception:
    pass

try:
    import fitz
    doc = fitz.open(path)
    parts = []
    for page in doc:
        try:
            txt = page.get_text('text') or ''
        except Exception:
            txt = ''
        if txt and txt.strip():
            parts.append(txt)
    if emit('\n'.join(parts), 'pymupdf'):
        raise SystemExit(0)
except SystemExit:
    raise
except Exception:
    pass

try:
    txt = basic_pdf_extract(path)
    if emit(txt, 'python-stdlib-basic-pdf'):
        raise SystemExit(0)
except SystemExit:
    raise
except Exception:
    pass
raise SystemExit(9)
)PY";

    QTemporaryDir tempDir;
    if (!tempDir.isValid()) {
        if (errorText) *errorText = QString::fromUtf8("Не вдалося створити тимчасову папку для прямого Python-читання PDF.");
        return {};
    }

    const QString scriptPath = tempDir.filePath(QStringLiteral("extract_pdf_text.py"));
    QFile pyFile(scriptPath);
    if (!pyFile.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        if (errorText) *errorText = QString::fromUtf8("Не вдалося створити тимчасовий Python-скрипт для читання PDF.");
        return {};
    }
    pyFile.write(QByteArray(kPythonCode));
    pyFile.close();

    const QStringList vendorPaths = pythonVendorPathsForPdfImport(appDir);
    appendInvoiceImportLog(QStringLiteral("direct python begin | launchers=%1 | vendorPaths=%2 | script=%3")
                               .arg(launchers.join(QStringLiteral(" || ")),
                                    vendorPaths.join(QStringLiteral(" || ")),
                                    scriptPath));

    for (const QString& launcher : launchers) {
        QProcess process;
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        if (!vendorPaths.isEmpty()) {
            const QString joined = vendorPaths.join(QDir::listSeparator());
            const QString existing = env.value(QStringLiteral("PYTHONPATH"));
            env.insert(QStringLiteral("PYTHONPATH"), existing.trimmed().isEmpty() ? joined : (joined + QDir::listSeparator() + existing));
        }
        env.insert(QStringLiteral("PYTHONDONTWRITEBYTECODE"), QStringLiteral("1"));
        env.insert(QStringLiteral("PYTHONIOENCODING"), QStringLiteral("utf-8"));
        process.setProcessEnvironment(env);
        process.setProgram(launcher);
        process.setArguments({scriptPath, filePath});

        const IncrementalImportProcessResult processResult =
            runImportProcessIncrementally(process, 10000, 60000, QStringLiteral("pdf_direct_python"));
        if (!processResult.started) {
            appendInvoiceImportLog(QStringLiteral("direct python failedToStart | launcher=%1 | err=%2")
                                       .arg(launcher, processResult.errorString));
            continue;
        }
        if (processResult.timedOut) {
            appendInvoiceImportLog(QStringLiteral("direct python timeout | launcher=%1").arg(launcher));
            continue;
        }

        const QByteArray out = processResult.stdOut;
        const QByteArray err = processResult.stdErr;
        appendInvoiceImportLog(QStringLiteral("direct python finished | launcher=%1 | exitStatus=%2 | exitCode=%3 | stdout=%4 | stderr=%5")
                                   .arg(launcher)
                                   .arg(int(processResult.exitStatus))
                                   .arg(processResult.exitCode)
                                   .arg(importProcessLogPreview(out, 400))
                                   .arg(importProcessLogPreview(err, 400)));

        if (processResult.exitStatus != QProcess::NormalExit || processResult.exitCode != 0 || out.trimmed().isEmpty()) {
            if (errorText && !QString::fromUtf8(err).trimmed().isEmpty()) *errorText = QString::fromUtf8(err).trimmed();
            continue;
        }

        QJsonParseError jsonError;
        const QJsonDocument doc = QJsonDocument::fromJson(out, &jsonError);
        if (jsonError.error != QJsonParseError::NoError || !doc.isObject()) {
            if (errorText) *errorText = QString::fromUtf8("Прямий Python-рушій повернув не JSON відповідь.");
            appendInvoiceImportLog(QStringLiteral("direct python badJson | launcher=%1 | err=%2 | out=%3")
                                       .arg(launcher,
                                            jsonError.errorString(),
                                            QString::fromUtf8(out).left(400).replace(QRegularExpression(QStringLiteral(R"([\r\n]+)")), QStringLiteral(" "))));
            continue;
        }

        const QJsonObject payload = doc.object();
        const QString text = payload.value(QStringLiteral("text")).toString();
        const QString engine = payload.value(QStringLiteral("engine")).toString();
        if (!text.trimmed().isEmpty()) {
            if (engineOut) *engineOut = engine;
            if (errorText) errorText->clear();
            return text;
        }
    }
    return {};
#else
    Q_UNUSED(filePath);
    Q_UNUSED(appDir);
    if (engineOut) engineOut->clear();
    if (errorText) errorText->clear();
    return {};
#endif
}


static QString pdfTextExtractPowerShellScript()
{
    QString script;
    script += QString::fromUtf8(R"PS(
param(
    [Parameter(Mandatory=$true)][string]$Path,
    [string]$AppDir = '',
    [string]$RenderedImageDir = ''
)
Set-StrictMode -Version 2
$ErrorActionPreference = 'Stop'
try {
    $utf8NoBom = New-Object System.Text.UTF8Encoding($false)
    [Console]::InputEncoding = $utf8NoBom
    [Console]::OutputEncoding = $utf8NoBom
    $OutputEncoding = $utf8NoBom
} catch { }

$script:AttemptedEngines = New-Object System.Collections.Generic.List[string]
$script:AppDir = $AppDir
$script:LayerRoot = $null
$script:LayerPaths = $null

function Emit-Json([string]$Text, [string]$Engine) {
    $payload = [ordered]@{ text = $Text; engine = $Engine }
    $payload | ConvertTo-Json -Depth 4 -Compress
}

function Add-Attempt([string]$Name) {
    if (-not [string]::IsNullOrWhiteSpace($Name) -and -not $script:AttemptedEngines.Contains($Name)) {
        [void]$script:AttemptedEngines.Add($Name)
    }
}

function Write-Diagnostic([string]$Message) {
    if ([string]::IsNullOrWhiteSpace($Message)) { return }
    try { [Console]::Error.WriteLine($Message) } catch { }
}

function Add-UniquePath([System.Collections.Generic.List[string]]$List, [string]$PathValue) {
    if ([string]::IsNullOrWhiteSpace($PathValue)) { return }
    try { $resolved = [System.IO.Path]::GetFullPath($PathValue) } catch { $resolved = $PathValue }
    if (-not [string]::IsNullOrWhiteSpace($resolved) -and (Test-Path $resolved) -and -not $List.Contains($resolved)) {
        [void]$List.Add($resolved)
    }
}

function Test-IsWindowsAppsShim([string]$PathValue) {
    if ([string]::IsNullOrWhiteSpace($PathValue)) { return $false }
    try {
        $normalized = ([string]$PathValue).Replace('/', '\').ToLowerInvariant()
        return ($normalized -match '\\microsoft\\windowsapps\\' -or
                $normalized -match '\\windowsapps\\' -or
                $normalized.EndsWith('\\windowsapps\\python.exe') -or
                $normalized.EndsWith('\\windowsapps\\py.exe'))
    } catch {
        return $false
    }
}


function Format-ExceptionDeep($Exception) {
    if ($null -eq $Exception) { return '' }
    $parts = New-Object System.Collections.Generic.List[string]
    $seen = New-Object System.Collections.Generic.HashSet[string]
    $queue = New-Object System.Collections.Generic.Queue[object]
    [void]$queue.Enqueue($Exception)
    while ($queue.Count -gt 0 -and $parts.Count -lt 12) {
        $current = $queue.Dequeue()
        if ($null -eq $current) { continue }
        try {
            $message = (([string]$current.Message -replace '[\r\n]+',' ').Trim())
            if (-not [string]::IsNullOrWhiteSpace($message) -and $seen.Add($message)) { [void]$parts.Add($message) }
        } catch { }
        try {
            if ($current -is [System.AggregateException]) {
                foreach ($inner in $current.Flatten().InnerExceptions) { if ($null -ne $inner) { [void]$queue.Enqueue($inner) } }
            }
        } catch { }
        try { if ($current.InnerException) { [void]$queue.Enqueue($current.InnerException) } } catch { }
    }
    return (($parts.ToArray()) -join ' | ')
}



function Add-OcrLayerRootFamily([System.Collections.Generic.List[string]]$Roots, [string]$BasePath) {
    if ([string]::IsNullOrWhiteSpace($BasePath)) { return }
    Add-UniquePath $Roots $BasePath
    Add-UniquePath $Roots (Join-Path $BasePath 'ocr-layer')
    Add-UniquePath $Roots (Join-Path $BasePath 'ocr')
    Add-UniquePath $Roots (Join-Path $BasePath 'runtime\ocr')
    Add-UniquePath $Roots (Join-Path $BasePath 'tools\ocr')
    Add-UniquePath $Roots (Join-Path $BasePath 'support\ocr')
    Add-UniquePath $Roots (Join-Path $BasePath 'resources\windows\ocr-layer')
}

function Get-OcrLayerSearchRoots() {
    $roots = New-Object System.Collections.Generic.List[string]
    if (-not [string]::IsNullOrWhiteSpace($script:AppDir) -and (Test-Path $script:AppDir)) {
        $parent = Split-Path -Parent $script:AppDir
        $grand = if (-not [string]::IsNullOrWhiteSpace($parent)) { Split-Path -Parent $parent } else { $null }
        foreach ($base in @($script:AppDir, $parent, $grand)) {
            Add-OcrLayerRootFamily $roots $base
        }
    }
    if (-not [string]::IsNullOrWhiteSpace($env:CS_PDF_OCR_LAYER)) {
        Add-UniquePath $roots $env:CS_PDF_OCR_LAYER
    }
    foreach ($root in @($env:ProgramFiles, ${env:ProgramFiles(x86)})) {
        if ([string]::IsNullOrWhiteSpace($root)) { continue }
        Add-UniquePath $roots (Join-Path $root 'Tesseract-OCR')
        Add-UniquePath $roots (Join-Path $root 'Tesseract-OCR\bin')
        Add-UniquePath $roots (Join-Path $root 'poppler')
        Add-UniquePath $roots (Join-Path $root 'poppler\bin')
        Add-UniquePath $roots (Join-Path $root 'poppler\Library\bin')
        Add-UniquePath $roots (Join-Path $root 'NAPS2')
        Add-UniquePath $roots (Join-Path $root 'NAPS2\bin')
    }
    return $roots.ToArray()
}

function Resolve-OcrLayerPaths() {
    if ($script:LayerPaths) { return $script:LayerPaths }
    foreach ($root in (Get-OcrLayerSearchRoots)) {
        $result = [ordered]@{
            root = $root; searchDirs = @(); pdftotext = $null; mutool = $null; pdftoppm = $null;
            tesseract = $null; python = $null; naps2 = $null; tessdata = $null; pySite = $null
        }
        $dirs = New-Object System.Collections.Generic.List[string]
        Add-UniquePath $dirs $root
        foreach ($rel in @('bin', 'Library\bin', 'poppler\bin', 'poppler\Library\bin', 'tools', 'Tesseract-OCR', 'Tesseract-OCR\bin', 'tesseract', 'python', 'NAPS2', 'NAPS2\bin', 'NAPS2\App', 'NAPS2\App\bin')) {
            Add-UniquePath $dirs (Join-Path $root $rel)
        }
        $result.searchDirs = $dirs.ToArray()
        foreach ($dir in $result.searchDirs) {
            foreach ($item in @(
                @{ key = 'pdftotext'; names = @('pdftotext.exe') },
                @{ key = 'mutool'; names = @('mutool.exe') },
                @{ key = 'pdftoppm'; names = @('pdftoppm.exe') },
                @{ key = 'tesseract'; names = @('tesseract.exe') },
                @{ key = 'python'; names = @('python.exe') },
                @{ key = 'naps2'; names = @('NAPS2.Console.exe','naps2.console.exe') }
            )) {
                if ($result[$item.key]) { continue }
                foreach ($name in $item.names) {
                    $candidate = Join-Path $dir $name
                    if (Test-Path $candidate) {
                        $result[$item.key] = $candidate
                        break
                    }
                }
            }
            if (-not $result.tessdata) {
                $candidateTess = Join-Path $dir 'tessdata'
                if (Test-Path $candidateTess) { $result.tessdata = $candidateTess }
            }
        }
        if (-not $result.tessdata) {
            $candidateProgramFilesTess = Join-Path $root 'Tesseract-OCR\tessdata'
            if (Test-Path $candidateProgramFilesTess) { $result.tessdata = $candidateProgramFilesTess }
        }
        if (-not $result.pySite) {
            foreach ($rel in @('python\Lib\site-packages', 'python\site-packages', 'site-packages')) {
                $candidateSite = Join-Path $root $rel
                if (Test-Path $candidateSite) { $result.pySite = $candidateSite; break }
            }
        }
        $hasRuntimeTool = ($result.pdftotext -or $result.mutool -or $result.pdftoppm -or $result.tesseract -or $result.python -or $result.naps2)
        $hasPythonPdfVendor = (-not [string]::IsNullOrWhiteSpace($result.pySite))
        $hasLayerMarker = (Test-Path (Join-Path $root 'manifest.json')) -or (Test-Path (Join-Path $root 'README.md'))
        if ($hasRuntimeTool -or $hasPythonPdfVendor -or $hasLayerMarker) {
            $script:LayerRoot = $root
            $script:LayerPaths = $result
            return $result
        }
    }
    $script:LayerPaths = [ordered]@{
        root = $null; searchDirs = @(); pdftotext = $null; mutool = $null; pdftoppm = $null;
        tesseract = $null; python = $null; naps2 = $null; tessdata = $null; pySite = $null
    }
    return $script:LayerPaths
}

function Find-Executable([string[]]$Candidates, [string]$PreferredKey = '') {
    $layer = Resolve-OcrLayerPaths
    if (-not [string]::IsNullOrWhiteSpace($PreferredKey) -and $layer.Contains($PreferredKey) -and -not [string]::IsNullOrWhiteSpace($layer[$PreferredKey])) {
        return $layer[$PreferredKey]
    }
)PS");
    script += QString::fromUtf8(R"PS(    $programFilesRoots = @($env:ProgramFiles, ${env:ProgramFiles(x86)}, $env:LocalAppData)
    $extraSearchDirs = New-Object System.Collections.Generic.List[string]
    foreach ($root in $programFilesRoots) {
        if ([string]::IsNullOrWhiteSpace($root)) { continue }
        Add-UniquePath $extraSearchDirs (Join-Path $root 'Tesseract-OCR')
        Add-UniquePath $extraSearchDirs (Join-Path $root 'Tesseract-OCR\bin')
        Add-UniquePath $extraSearchDirs (Join-Path $root 'poppler\Library\bin')
        Add-UniquePath $extraSearchDirs (Join-Path $root 'poppler\bin')
        Add-UniquePath $extraSearchDirs (Join-Path $root 'NAPS2')
        Add-UniquePath $extraSearchDirs (Join-Path $root 'NAPS2\bin')
    }
    foreach ($candidate in $Candidates) {
        foreach ($dir in $extraSearchDirs) {
            $full = Join-Path $dir $candidate
            if (Test-Path $full) { return $full }
        }
        if ($candidate -match '^python(\.exe)?$' -or $candidate -eq 'py.exe') {
            foreach ($root in @($env:LocalAppData, $env:ProgramFiles, ${env:ProgramFiles(x86)})) {
                if ([string]::IsNullOrWhiteSpace($root)) { continue }
                foreach ($pat in @('Programs\Python\Python*\python.exe', 'Python\Python*\python.exe', 'Python*\python.exe')) {
                    $hits = Get-ChildItem -Path (Join-Path $root $pat) -File -ErrorAction SilentlyContinue | Sort-Object FullName -Descending
                    foreach ($hit in @($hits)) {
                        if ($null -eq $hit) { continue }
                        $full = [string]$hit.FullName
                        if (-not (Test-IsWindowsAppsShim $full)) { return $full }
                    }
                }
            }
        }
        $cmd = Get-Command $candidate -ErrorAction SilentlyContinue | Select-Object -First 1 -ExpandProperty Source
        if (-not [string]::IsNullOrWhiteSpace($cmd) -and -not (Test-IsWindowsAppsShim $cmd)) { return $cmd }
    }
    return $null
}

function Read-TextIfExists([string]$Path) {
    if (Test-Path $Path) {
        try {
            return [System.IO.File]::ReadAllText($Path, [System.Text.Encoding]::UTF8)
        } catch {
            try { return Get-Content -LiteralPath $Path -Raw -Encoding UTF8 } catch { }
        }
    }
    return $null
}

function Get-PythonVendorPaths() {
    $paths = New-Object System.Collections.Generic.List[string]
    if (-not [string]::IsNullOrWhiteSpace($script:AppDir)) {
        $app = $script:AppDir
        $parent = Split-Path -Parent $app
        $grand = if (-not [string]::IsNullOrWhiteSpace($parent)) { Split-Path -Parent $parent } else { $null }
        foreach ($root in @($app, $parent, $grand)) {
            if ([string]::IsNullOrWhiteSpace($root)) { continue }
            Add-UniquePath $paths (Join-Path $root 'resources\python_vendor')
            Add-UniquePath $paths (Join-Path $root 'resources\python')
            Add-UniquePath $paths (Join-Path $root 'python_vendor')
            Add-UniquePath $paths (Join-Path $root 'python')
            # The OCR layer may contain only app-local Python PDF packages
            # (pypdf/pdfminer/pdfplumber) without Poppler/Tesseract executables.
            # Keep those packages visible to a real system Python even when the
            # full OCR runtime has not been installed yet.
            Add-UniquePath $paths (Join-Path $root 'resources\windows\ocr-layer\python\Lib\site-packages')
            Add-UniquePath $paths (Join-Path $root 'resources\windows\ocr-layer\python\site-packages')
            Add-UniquePath $paths (Join-Path $root 'ocr-layer\python\Lib\site-packages')
            Add-UniquePath $paths (Join-Path $root 'ocr-layer\python\site-packages')
        }
    }
    $layer = Resolve-OcrLayerPaths
    if (-not [string]::IsNullOrWhiteSpace($layer.pySite)) {
        Add-UniquePath $paths $layer.pySite
    }
    return $paths.ToArray()
}


function Try-PdfToText([string]$PdfPath) {
    Add-Attempt 'pdftotext'
    $pdftotext = Find-Executable @('pdftotext.exe', 'pdftotext') 'pdftotext'
    if ([string]::IsNullOrWhiteSpace($pdftotext)) { return $null }
    foreach ($modeArgs in @(
        @('-layout', '-enc', 'UTF-8'),
        @('-raw', '-enc', 'UTF-8'),
        @('-enc', 'UTF-8')
    )) {
        $tmp = [System.IO.Path]::ChangeExtension([System.IO.Path]::GetTempFileName(), '.txt')
        try {
            $args = @()
            $args += $modeArgs
            $args += @($PdfPath, $tmp)
            $proc = Start-Process -FilePath $pdftotext -ArgumentList $args -Wait -PassThru -NoNewWindow
            if ($proc.ExitCode -eq 0) {
                $text = Read-TextIfExists $tmp
                if (-not [string]::IsNullOrWhiteSpace($text)) { return $text }
            }
        } catch { }
        finally { if (Test-Path $tmp) { Remove-Item $tmp -Force -ErrorAction SilentlyContinue } }
    }
    return $null
}

function Try-MutoolText([string]$PdfPath) {
    Add-Attempt 'mutool'
    $mutool = Find-Executable @('mutool.exe', 'mutool') 'mutool'
    if ([string]::IsNullOrWhiteSpace($mutool)) { return $null }
    $tmp = [System.IO.Path]::ChangeExtension([System.IO.Path]::GetTempFileName(), '.txt')
    try {
        $proc = Start-Process -FilePath $mutool -ArgumentList @('draw', '-F', 'text', '-o', $tmp, $PdfPath) -Wait -PassThru -NoNewWindow
        if ($proc.ExitCode -eq 0) {
            $text = Read-TextIfExists $tmp
            if (-not [string]::IsNullOrWhiteSpace($text)) { return $text }
        }
    } catch { }
    finally { if (Test-Path $tmp) { Remove-Item $tmp -Force -ErrorAction SilentlyContinue } }
    return $null
}

function ConvertFrom-PdfDocEncoding([int]$CodePoint) {
    switch ($CodePoint) {
        0 { return '' }
        9 { return "`t" }
        10 { return "`n" }
        13 { return "`n" }
        default {
            try { return [char]$CodePoint } catch { return '' }
        }
    }
}

function ConvertFrom-PdfHexToText([string]$Hex, $MapTable) {
    if ([string]::IsNullOrWhiteSpace($Hex)) { return '' }
    $clean = ($Hex -replace '[^0-9A-Fa-f]', '')
    if (($clean.Length % 2) -ne 0) { $clean += '0' }
    $sb = New-Object System.Text.StringBuilder
    for ($i = 0; $i -lt $clean.Length; $i += 2) {
        $code = [Convert]::ToInt32($clean.Substring($i, 2), 16)
        $key = ('{0:X2}' -f $code)
        if ($MapTable -and $MapTable.ContainsKey($key)) {
            [void]$sb.Append([string]$MapTable[$key])
        } else {
            [void]$sb.Append((ConvertFrom-PdfDocEncoding $code))
        }
    }
    return $sb.ToString()
}

function ConvertFrom-PdfLiteralToText([byte[]]$Bytes, $MapTable) {
    if (-not $Bytes -or $Bytes.Length -eq 0) { return '' }
    $sb = New-Object System.Text.StringBuilder
    $i = 0
    while ($i -lt $Bytes.Length) {
        $b = [int]$Bytes[$i]
        if ($b -eq 92) {
            $i++
            if ($i -ge $Bytes.Length) { break }
            $n = [int]$Bytes[$i]
            switch ($n) {
                110 { [void]$sb.Append("`n") }
                114 { [void]$sb.Append("`n") }
                116 { [void]$sb.Append("`t") }
                98 { [void]$sb.Append([char]8) }
                102 { [void]$sb.Append([char]12) }
                40 { [void]$sb.Append('(') }
                41 { [void]$sb.Append(')') }
                92 { [void]$sb.Append('\\') }
                default {
                    if ($n -ge 48 -and $n -le 55) {
                        $oct = [string][char]$n
                        for ($j = 0; $j -lt 2 -and ($i + 1) -lt $Bytes.Length; $j++) {
                            $peek = [int]$Bytes[$i + 1]
                            if ($peek -ge 48 -and $peek -le 55) {
                                $i++
                                $oct += [char]$peek
                            } else {
                                break
                            }
                        }
)PS");
    script += QString::fromUtf8(R"PS(                        $code = [Convert]::ToInt32($oct, 8)
                        $key = ('{0:X2}' -f $code)
                        if ($MapTable -and $MapTable.ContainsKey($key)) {
                            [void]$sb.Append([string]$MapTable[$key])
                        } else {
                            [void]$sb.Append((ConvertFrom-PdfDocEncoding $code))
                        }
                    } else {
                        $key = ('{0:X2}' -f $n)
                        if ($MapTable -and $MapTable.ContainsKey($key)) {
                            [void]$sb.Append([string]$MapTable[$key])
                        } else {
                            [void]$sb.Append((ConvertFrom-PdfDocEncoding $n))
                        }
                    }
                }
            }
        } else {
            $key = ('{0:X2}' -f $b)
            if ($MapTable -and $MapTable.ContainsKey($key)) {
                [void]$sb.Append([string]$MapTable[$key])
            } else {
                [void]$sb.Append((ConvertFrom-PdfDocEncoding $b))
            }
        }
        $i++
    }
    return $sb.ToString()
}

 )PS");
    script += QString::fromUtf8(R"PS(

function Get-SafeCount($Value) {
    if ($null -eq $Value) { return 0 }
    if ($Value -is [string]) { return 1 }
    if ($Value -is [System.Collections.ICollection]) { return [int]$Value.Count }
    if ($Value -is [System.Array]) { return [int]$Value.Length }
    return @($Value).Count
}

function Get-PdfObjectMap([byte[]]$Data) {
    $latin = [System.Text.Encoding]::GetEncoding('iso-8859-1').GetString($Data)
    $objectRegex = [regex]'(?s)(\d+)\s+(\d+)\s+obj(.*?)endobj'
    $maps = @{}
    foreach ($m in $objectRegex.Matches($latin)) {
        $num = $m.Groups[1].Value
        if (-not $maps.ContainsKey($num)) { $maps[$num] = $m.Groups[3].Value }
    }
    foreach ($m in $objectRegex.Matches($latin)) {
        $body = $m.Groups[3].Value
        if ($body -notmatch '/Type\s*/ObjStm' -and $body -notmatch '/ObjStm') { continue }
        if ($body -notmatch '(?s)stream\s*(.*?)\s*endstream') { continue }
        $streamText = $Matches[1]
        $streamBytes = [System.Text.Encoding]::GetEncoding('iso-8859-1').GetBytes($streamText)
        try {
            $ms = New-Object System.IO.MemoryStream(,$streamBytes)
            $zs = New-Object System.IO.Compression.ZLibStream($ms, [System.IO.Compression.CompressionMode]::Decompress)
            $out = New-Object System.IO.MemoryStream
            $zs.CopyTo($out)
            $zs.Dispose(); $ms.Dispose()
            $decodedBytes = $out.ToArray()
            $out.Dispose()
        } catch { continue }
        if ($body -notmatch '/First\s+(\d+)') { continue }
        $first = [int]$Matches[1]
        if ($body -notmatch '/N\s+(\d+)') { continue }
        $count = [int]$Matches[1]
        if ($decodedBytes.Length -le $first) { continue }
        $header = [System.Text.Encoding]::GetEncoding('iso-8859-1').GetString($decodedBytes, 0, $first)
        $content = [System.Text.Encoding]::GetEncoding('iso-8859-1').GetString($decodedBytes, $first, $decodedBytes.Length - $first)
        $tokens = @($header -split '\s+' | Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
        if ((Get-SafeCount $tokens) -lt 2) { continue }
        $pairs = New-Object System.Collections.Generic.List[object]
        for ($i = 0; $i + 1 -lt (Get-SafeCount $tokens) -and (Get-SafeCount $pairs) -lt $count; $i += 2) {
            $objNum = $tokens[$i]
            $offset = 0
            if (-not [int]::TryParse($tokens[$i + 1], [ref]$offset)) { continue }
            [void]$pairs.Add([pscustomobject]@{ obj = $objNum; offset = $offset })
        }
        for ($i = 0; $i -lt (Get-SafeCount $pairs); $i++) {
            $start = [int]$pairs[$i].offset
            if ($start -lt 0 -or $start -ge $content.Length) { continue }
            $end = $content.Length
            if ($i + 1 -lt (Get-SafeCount $pairs)) { $end = [int]$pairs[$i + 1].offset }
            if ($end -le $start) { continue }
            $objBody = $content.Substring($start, $end - $start).Trim()
            if (-not [string]::IsNullOrWhiteSpace($objBody)) {
                $maps[[string]$pairs[$i].obj] = $objBody
            }
        }
    }
    return $maps
}

function Get-PdfToUnicodeMaps([byte[]]$Data, $ObjectBodies) {
    $maps = @{}
    foreach ($entry in $ObjectBodies.GetEnumerator()) {
        $num = [string]$entry.Key
        $body = [string]$entry.Value
        if ($body -notmatch '/ToUnicode\s+(\d+)\s+\d+\s+R') { continue }
        $cmapObj = $Matches[1]
        if (-not $ObjectBodies.ContainsKey($cmapObj)) { continue }
        $cmapBody = [string]$ObjectBodies[$cmapObj]
        if ($cmapBody -notmatch '(?s)stream\s*(.*?)\s*endstream') { continue }
        $streamText = $Matches[1]
        $streamBytes = [System.Text.Encoding]::GetEncoding('iso-8859-1').GetBytes($streamText)
        try {
            $ms = New-Object System.IO.MemoryStream(,$streamBytes)
            $zs = New-Object System.IO.Compression.ZLibStream($ms, [System.IO.Compression.CompressionMode]::Decompress)
            $out = New-Object System.IO.MemoryStream
            $zs.CopyTo($out)
            $zs.Dispose(); $ms.Dispose()
            $decoded = [System.Text.Encoding]::ASCII.GetString($out.ToArray())
            $out.Dispose()
        } catch { continue }
        $fontMap = @{}
        foreach ($line in ($decoded -split "`r?`n")) {
            if ($line -match '<([0-9A-Fa-f]+)>\s*<([0-9A-Fa-f]+)>\s*<([0-9A-Fa-f]+)>') {
                $start = [Convert]::ToInt32($Matches[1], 16)
                $end = [Convert]::ToInt32($Matches[2], 16)
                $dst = [Convert]::ToInt32($Matches[3], 16)
                for ($k = $start; $k -le $end; $k++) {
                    $fontMap[('{0:X2}' -f $k)] = [char]($dst + ($k - $start))
                }
            } elseif ($line -match '<([0-9A-Fa-f]+)>\s*<([0-9A-Fa-f]+)>') {
                $src = ('{0:X2}' -f [Convert]::ToInt32($Matches[1], 16))
                $dst = [Convert]::ToInt32($Matches[2], 16)
                $fontMap[$src] = [char]$dst
            }
        }
        if ((Get-SafeCount $fontMap) -gt 0) { $maps[$num] = $fontMap }
    }
    return $maps
}

function Get-PdfFontResourceMaps($ObjectBodies, $ToUnicodeMaps) {
    $resourceMaps = @{}
    foreach ($entry in $ObjectBodies.GetEnumerator()) {
        $body = [string]$entry.Value
        foreach ($fontMatch in [regex]::Matches($body, '/(R\d+)\s+(\d+)\s+\d+\s+R')) {
            $resourceName = '/' + $fontMatch.Groups[1].Value
            $fontObj = $fontMatch.Groups[2].Value
            if ($ToUnicodeMaps.ContainsKey($fontObj) -and -not $resourceMaps.ContainsKey($resourceName)) {
                $resourceMaps[$resourceName] = $ToUnicodeMaps[$fontObj]
            }
        }
    }
    return $resourceMaps
}

)PS");
    script += QString::fromUtf8(R"PS(

function Try-BasicPdfText([string]$PdfPath) {
    Add-Attempt 'basic-pdf-text'
    try { $data = [System.IO.File]::ReadAllBytes($PdfPath) } catch { return $null }
    if (-not $data -or $data.Length -eq 0) { return $null }
    $objectBodies = Get-PdfObjectMap $data
    $toUnicodeMaps = Get-PdfToUnicodeMaps $data $objectBodies
    $resourceMaps = Get-PdfFontResourceMaps $objectBodies $toUnicodeMaps
    $latin = [System.Text.Encoding]::GetEncoding('iso-8859-1').GetString($data)
    $objRegex = [regex]'(?s)(\d+)\s+\d+\s+obj(.*?)endobj'
    $parts = New-Object System.Collections.Generic.List[string]
    foreach ($m in $objRegex.Matches($latin)) {
        $body = $m.Groups[2].Value
        if ($body -notmatch '/Filter\s*/FlateDecode') { continue }
        if ($body -notmatch '(?s)stream\s*(.*?)\s*endstream') { continue }
        $streamBytes = [System.Text.Encoding]::GetEncoding('iso-8859-1').GetBytes($Matches[1])
        try {
            $ms = New-Object System.IO.MemoryStream(,$streamBytes)
            $zs = New-Object System.IO.Compression.ZLibStream($ms, [System.IO.Compression.CompressionMode]::Decompress)
            $out = New-Object System.IO.MemoryStream
            $zs.CopyTo($out)
            $zs.Dispose(); $ms.Dispose()
            $decodedBytes = $out.ToArray()
            $out.Dispose()
        } catch { continue }
        $decoded = [System.Text.Encoding]::GetEncoding('iso-8859-1').GetString($decodedBytes)
        if ($decoded.IndexOf('BT') -lt 0 -or ($decoded.IndexOf('Tj') -lt 0 -and $decoded.IndexOf('TJ') -lt 0)) { continue }
        $currentMap = $null
        foreach ($line in ($decoded -split "`r?`n")) {
            foreach ($tf in [regex]::Matches($line, '/R\d+\s+[0-9\.]+\s+Tf')) {
                if ($tf.Value -match '(/R\d+)') {
                    $fontKey = $Matches[1]
                    if ($resourceMaps.ContainsKey($fontKey)) { $currentMap = $resourceMaps[$fontKey] }
                }
            }
            foreach ($mj in [regex]::Matches($line, '\((?:\\.|[^\\)])*\)\s*Tj')) {
                $token = $mj.Value -replace '\s*Tj$',''
                if ($token.Length -ge 2) {
                    $literalBytes = [System.Text.Encoding]::GetEncoding('iso-8859-1').GetBytes($token.Substring(1, $token.Length - 2))
                    $text = ConvertFrom-PdfLiteralToText $literalBytes $currentMap
                    if (-not [string]::IsNullOrWhiteSpace($text)) { [void]$parts.Add($text) }
                }
            }
            foreach ($mh in [regex]::Matches($line, '<([0-9A-Fa-f\s]+)>\s*Tj')) {
                $text = ConvertFrom-PdfHexToText $mh.Groups[1].Value $currentMap
                if (-not [string]::IsNullOrWhiteSpace($text)) { [void]$parts.Add($text) }
            }
            foreach ($block in [regex]::Matches($line, '\[(.*?)\]\s*TJ')) {
                $chunk = $block.Groups[1].Value
                foreach ($tok in [regex]::Matches($chunk, '\((?:\\.|[^\\)])*\)|<([0-9A-Fa-f\s]+)>')) {
                    $value = $tok.Value
                    if ($value.StartsWith('(')) {
                        $literalBytes = [System.Text.Encoding]::GetEncoding('iso-8859-1').GetBytes($value.Substring(1, $value.Length - 2))
                        $text = ConvertFrom-PdfLiteralToText $literalBytes $currentMap
                    } elseif ($value.StartsWith('<')) {
                        $text = ConvertFrom-PdfHexToText $value.Substring(1, $value.Length - 2) $currentMap
                    } else {
                        $text = ''
                    }
                    if (-not [string]::IsNullOrWhiteSpace($text)) { [void]$parts.Add($text) }
                }
            }
        }
    }
    if ((Get-SafeCount $parts) -eq 0) { return $null }
    return (($parts.ToArray()) -join "`n")
}


function Test-PdfNumericAmount([string]$Value) {
    if ([string]::IsNullOrWhiteSpace($Value)) { return $false }
    return $Value.Trim() -match '^\d[\d\s]*[\.,]\d{2}$'
}

function Test-PdfQtyToken([string]$Value) {
    if ([string]::IsNullOrWhiteSpace($Value)) { return $false }
    return $Value.Trim() -match '^\d+(?:[\.,]\d+)?$'
}

function Test-PdfUnitToken([string]$Value) {
    if ([string]::IsNullOrWhiteSpace($Value)) { return $false }
    return $Value.Trim().ToLowerInvariant() -match '^(шт|шт\.|штука|од|од\.|компл|компл\.|к-т|л|кг|г|м|см|уп|уп\.|упак|пара|пар)$'
}

function Test-ElitePdfCode([string]$Value) {
    if ([string]::IsNullOrWhiteSpace($Value)) { return $false }
    $clean = (($Value -replace '\s+', ' ').Trim())
    if ($clean.Length -lt 3 -or $clean.Length -gt 48) { return $false }
    if ($clean -match '^(X|x)$') { return $false }
    if ($clean -match '(?i)(постачаль|одержувач|адреса|дата|документ|догов|банк|тел|сума|пдв|простроч|баланс|гарант|долуча)') { return $false }
    if ($clean -notmatch '[A-ZА-Я0-9]') { return $false }
    if ($clean -notmatch '\d') { return $false }
    return $true
}

function Normalize-ElitePdfName([string[]]$Parts) {
    $joined = (($Parts | Where-Object { -not [string]::IsNullOrWhiteSpace($_) }) -join ' ').Trim()
    if ([string]::IsNullOrWhiteSpace($joined)) { return '' }
    $joined = $joined -replace '\s+', ' '
    $joined = $joined -replace '\(\(', '('
    $joined = $joined -replace '\)\)', ')'
    return $joined.Trim()
}

function Convert-BasicElitePdfToStructuredText([string]$PdfPath, [string]$BasicText) {
    if ([string]::IsNullOrWhiteSpace($BasicText)) { return $null }
    $lines = @($BasicText -split "`r?`n" | ForEach-Object { ($_ -replace '\s+', ' ').Trim() } | Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
    if ((Get-SafeCount $lines) -lt 8) { return $null }

    $joined = ($lines -join "`n")
    $joinedLower = $joined.ToLowerInvariant()
    if ($joinedLower -notmatch 'еліт|elit') { return $null }

    $documentNumber = $null
    $documentDate = $null
    $headerPatterns = @(
        '(?is)видаткова\s+накладна\s*№\s*([^\s]+)\s*від\s*([^\r\n]+?)(?:\s+р\.?|$)',
        '(?is)рахунок\s*-\s*фактура\s*№\s*([^\s]+)\s*від\s*([^\r\n]+?)(?:\s+р\.?|$)'
    )
    foreach ($pattern in $headerPatterns) {
        $m = [regex]::Match($joined, $pattern)
        if ($m.Success) {
            $documentNumber = $m.Groups[1].Value.Trim()
            $documentDate = $m.Groups[2].Value.Trim()
            break
        }
    }
    if ([string]::IsNullOrWhiteSpace($documentNumber)) {
        $documentNumber = [System.IO.Path]::GetFileNameWithoutExtension($PdfPath)
    }

    $rowPattern = '^\d[\d\s]*[\,\.]\d{2}\d+(?:[\,\.]\d+)?(?:шт|шт\.|штука|од|од\.|компл|компл\.|к-т|л|кг|г|м|см|уп|уп\.|упак|пара|пар).*[XХ]\d{8,10}$'
    $rows = New-Object System.Collections.Generic.List[string]
    foreach ($line in $lines) {
        if ($line -match $rowPattern) {
            [void]$rows.Add($line)
        }
    }

    if ((Get-SafeCount $rows) -eq 0) { return $null }

    $header = New-Object System.Collections.Generic.List[string]
    [void]$header.Add('ТОВ "ЕЛІТ-Україна"')
    if (-not [string]::IsNullOrWhiteSpace($documentNumber) -and -not [string]::IsNullOrWhiteSpace($documentDate)) {
        [void]$header.Add(('ВИДАТКОВА НАКЛАДНА № ' + $documentNumber + ' від ' + $documentDate))
    } elseif (-not [string]::IsNullOrWhiteSpace($documentNumber)) {
        [void]$header.Add(('ВИДАТКОВА НАКЛАДНА № ' + $documentNumber))
    }
    [void]$header.Add('Кіл-ть Ціна з ПДВ Од. вим. Найменування Активн. код товару Ціна без ПДВ X Код товару')
    foreach ($row in $rows) { [void]$header.Add($row) }
    return (($header.ToArray()) -join "`n")
}

function Try-PythonPdf([string]$PdfPath) {
    Add-Attempt 'python-pdf-engines'
    $python = Find-Executable @('python.exe', 'python') 'python'
    $py = Find-Executable @('py.exe')
    $launcher = $null
    $args = @()
    if (-not [string]::IsNullOrWhiteSpace($python)) {
        $launcher = $python
    } elseif (-not [string]::IsNullOrWhiteSpace($py)) {
        $launcher = $py
        $args = @('-3')
    }
    if ([string]::IsNullOrWhiteSpace($launcher)) { return $null }
    if (Test-IsWindowsAppsShim $launcher) {
        Write-Diagnostic ("[python-pdf] rejected_windowsapps_launcher=" + $launcher)
        return $null
    }
)PS");
    script += QString::fromUtf8(R"PS(

    $script = @"
import sys, re, zlib
path = sys.argv[1]

def _emit(text, engine):
    if not text or not text.strip():
        return False
    sys.stdout.buffer.write(text.encode('utf-8', 'ignore'))
    sys.stderr.write(engine)
    return True

def _basic_pdf_extract(pdf_path):
    data = open(pdf_path, 'rb').read()
    out = []
    for m in re.finditer(rb'stream\r?\n', data):
        start = m.end()
        end = data.find(b'endstream', start)
        if end < 0:
            continue
        chunk = data[start:end].rstrip(b'\r\n')
        try:
            dec = zlib.decompress(chunk)
        except Exception:
            continue
        if b'BT' not in dec or b'Tj' not in dec:
            continue
        text = dec.decode('latin1', 'ignore')
        for token in re.findall(r'\((?:\\.|[^\)])*\)\s*Tj', text):
            s = token[:-2] if token.endswith('Tj') else token
            out.append(s)
        for block in re.findall(r'\[(.*?)\]\s*TJ', text, re.S):
            for token in re.findall(r'\((?:\\.|[^\)])*\)', block):
                out.append(token)
    if not out:
        return ''
    merged = '\n'.join(out)
    merged = merged.replace('\\(', '(').replace('\\)', ')').replace('\\n', '\n').replace('\\r', '\n')
    merged = re.sub(r'\\([0-7]{3})', lambda m: chr(int(m.group(1), 8)), merged)
    merged = merged.replace('(', '').replace(')', '')
    merged = re.sub(r'\n{3,}', '\n\n', merged)
    return merged

# 1) pypdf / PyPDF2 with layout-ish extraction
for name in ('pypdf', 'PyPDF2'):
    try:
        mod = __import__(name)
        reader_cls = getattr(mod, 'PdfReader', None)
        if reader_cls is None:
            continue
        reader = reader_cls(path)
        parts = []
        for page in reader.pages:
            txt = ''
            for mode in ('layout', None):
                try:
                    if mode is None:
                        txt = page.extract_text() or ''
                    else:
                        txt = page.extract_text(extraction_mode=mode) or ''
                except TypeError:
                    continue
                except Exception:
                    txt = ''
                if txt and txt.strip():
                    break
            if txt and txt.strip():
                parts.append(txt)
        if _emit('\n'.join(parts), name):
            raise SystemExit(0)
    except SystemExit:
        raise
    except Exception:
        pass

# 2) pdfplumber
try:
    import pdfplumber
    parts = []
    with pdfplumber.open(path) as pdf:
        for page in pdf.pages:
            try:
                txt = page.extract_text(x_tolerance=1, y_tolerance=1) or ''
            except Exception:
                txt = ''
            if txt and txt.strip():
                parts.append(txt)
    if _emit('\n'.join(parts), 'pdfplumber'):
        raise SystemExit(0)
except SystemExit:
    raise
except Exception:
    pass

# 3) pdfminer.six
try:
    from pdfminer.high_level import extract_text
    txt = extract_text(path) or ''
    if _emit(txt, 'pdfminer.six'):
        raise SystemExit(0)
except SystemExit:
    raise
except Exception:
    pass

# 4) PyMuPDF / fitz
try:
    import fitz
    doc = fitz.open(path)
    parts = []
    for page in doc:
        try:
            txt = page.get_text('text') or ''
        except Exception:
            txt = ''
        if txt and txt.strip():
            parts.append(txt)
    if _emit('\n'.join(parts), 'pymupdf'):
        raise SystemExit(0)
except SystemExit:
    raise
except Exception:
    pass

# 5) built-in raw PDF stream extractor for simple digital PDFs
try:
    txt = _basic_pdf_extract(path)
    if _emit(txt, 'python-stdlib-basic-pdf'):
        raise SystemExit(0)
except SystemExit:
    raise
except Exception:
    pass

raise SystemExit(9)
"@

    $tmpPy = [System.IO.Path]::ChangeExtension([System.IO.Path]::GetTempFileName(), '.py')
    $stderrTmp = [System.IO.Path]::ChangeExtension([System.IO.Path]::GetTempFileName(), '.stderr.txt')
    $oldPythonPath = $env:PYTHONPATH
    $oldDontWriteBytecode = $env:PYTHONDONTWRITEBYTECODE
    try {
        [System.IO.File]::WriteAllText($tmpPy, $script, (New-Object System.Text.UTF8Encoding($false)))
        $vendorPaths = Get-PythonVendorPaths
        Write-Diagnostic ("[python-pdf] launcher=" + $launcher + " vendorPaths=" + (($vendorPaths | ForEach-Object { [string]$_ }) -join ';'))
        if ($vendorPaths -and (Get-SafeCount $vendorPaths) -gt 0) {
            $joinedVendorPaths = ($vendorPaths -join [System.IO.Path]::PathSeparator)
            if ([string]::IsNullOrWhiteSpace($env:PYTHONPATH)) {
                $env:PYTHONPATH = $joinedVendorPaths
            } else {
                $env:PYTHONPATH = $joinedVendorPaths + [System.IO.Path]::PathSeparator + $env:PYTHONPATH
            }
        }
        $env:PYTHONDONTWRITEBYTECODE = '1'

        $allArgs = @()
        if ((Get-SafeCount $args) -gt 0) { $allArgs += @($args) }
        $allArgs += @($tmpPy, $PdfPath)
        $stdout = & $launcher @allArgs 2> $stderrTmp
        $exitCode = $LASTEXITCODE
        $stdoutText = ''
        if ($stdout -is [System.Array]) {
            $stdoutText = (($stdout | ForEach-Object { [string]$_ }) -join "`n")
        } elseif ($null -ne $stdout) {
            $stdoutText = [string]$stdout
        }
        $stderr = ''
        if (Test-Path $stderrTmp) {
            $stderr = [System.IO.File]::ReadAllText($stderrTmp, [System.Text.Encoding]::UTF8)
        }
        if ($exitCode -eq 0 -and -not [string]::IsNullOrWhiteSpace($stdoutText)) {
            return @{ text = $stdoutText; engine = ($stderr.Trim() -replace '[\r\n]+', ' ').Trim() }
        }
        if (-not [string]::IsNullOrWhiteSpace($stderr)) {
            Write-Diagnostic ("[python-pdf] stderr=" + (($stderr -replace '[\r\n]+', ' ').Trim()))
        }
    } catch {
    }
    finally {
        if ($null -eq $oldPythonPath) { Remove-Item Env:PYTHONPATH -ErrorAction SilentlyContinue } else { $env:PYTHONPATH = $oldPythonPath }
        if ($null -eq $oldDontWriteBytecode) { Remove-Item Env:PYTHONDONTWRITEBYTECODE -ErrorAction SilentlyContinue } else { $env:PYTHONDONTWRITEBYTECODE = $oldDontWriteBytecode }
        if (Test-Path $tmpPy) { Remove-Item $tmpPy -Force -ErrorAction SilentlyContinue }
        if (Test-Path $stderrTmp) { Remove-Item $stderrTmp -Force -ErrorAction SilentlyContinue }
    }
    return $null
}

function Try-Naps2Ocr([string]$PdfPath) {
    Add-Attempt 'naps2-ocr'
    $naps2 = Find-Executable @('NAPS2.Console.exe', 'naps2.console.exe') 'naps2'
    if ([string]::IsNullOrWhiteSpace($naps2)) { return $null }
    $ocrPdf = [System.IO.Path]::ChangeExtension([System.IO.Path]::GetTempFileName(), '.ocr.pdf')
    try {
        foreach ($lang in @('eng','rus','ukr')) {
            try {
                $null = Start-Process -FilePath $naps2 -ArgumentList @('--install', ('ocr-' + $lang)) -Wait -PassThru -NoNewWindow
            } catch { }
        }
        $proc = Start-Process -FilePath $naps2 -ArgumentList @('-i', $PdfPath, '-o', $ocrPdf, '-n', '0', '--enableocr', '--ocrlang', 'ukr+rus+eng') -Wait -PassThru -NoNewWindow
        if ($proc.ExitCode -ne 0 -or -not (Test-Path $ocrPdf)) { return $null }
        $text = Try-PdfToText $ocrPdf
        if (-not [string]::IsNullOrWhiteSpace($text)) { return @{ text = $text; engine = 'naps2-ocr' } }
        $text = Try-MutoolText $ocrPdf
        if (-not [string]::IsNullOrWhiteSpace($text)) { return @{ text = $text; engine = 'naps2-ocr' } }
    } catch { }
    finally { if (Test-Path $ocrPdf) { Remove-Item $ocrPdf -Force -ErrorAction SilentlyContinue } }
    return $null
}

function Try-OcrFallback([string]$PdfPath) {
    Add-Attempt 'ocr(pdftoppm+tesseract)'
    $pdftoppm = Find-Executable @('pdftoppm.exe', 'pdftoppm') 'pdftoppm'
    $tesseract = Find-Executable @('tesseract.exe', 'tesseract') 'tesseract'
    if ([string]::IsNullOrWhiteSpace($pdftoppm) -or [string]::IsNullOrWhiteSpace($tesseract)) { return $null }

)PS");
