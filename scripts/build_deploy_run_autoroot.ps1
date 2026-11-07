param(
    [string]$SeedPath = (Get-Location).Path,
    [string]$QtPrefix = "C:\Qt\6.10.2\msvc2022_64",
    [string]$VsVcVars64 = "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat",
    [switch]$NoRun
)

$ErrorActionPreference = "Stop"

function Write-Step([string]$Text) {
    Write-Host "`n=== $Text ===" -ForegroundColor Cyan
}

function Show-TextTail([string]$Path, [int]$Lines = 80) {
    if ([string]::IsNullOrWhiteSpace($Path) -or !(Test-Path -LiteralPath $Path)) { return }
    $item = Get-Item -LiteralPath $Path -ErrorAction SilentlyContinue
    if (!$item -or $item.Length -le 0) { return }
    Write-Host "`n--- tail: $Path ---" -ForegroundColor DarkCyan
    Get-Content -LiteralPath $Path -Tail $Lines -ErrorAction SilentlyContinue | ForEach-Object { Write-Host $_ }
}

function Resolve-FullPath([string]$Path) {
    if ([string]::IsNullOrWhiteSpace($Path)) { return (Get-Location).Path }
    $expanded = [Environment]::ExpandEnvironmentVariables($Path)
    try { return (Resolve-Path -LiteralPath $expanded -ErrorAction Stop).Path }
    catch {
        $parent = Split-Path -Parent $expanded
        if ($parent -and (Test-Path -LiteralPath $parent)) {
            return [System.IO.Path]::GetFullPath($expanded)
        }
        throw "Path not found: $Path"
    }
}

function Test-ProjectRoot([string]$Path) {
    if ([string]::IsNullOrWhiteSpace($Path)) { return $false }
    return (Test-Path -LiteralPath (Join-Path $Path "CMakeLists.txt")) -and
           (Test-Path -LiteralPath (Join-Path $Path "src")) -and
           (Test-Path -LiteralPath (Join-Path $Path "cmake"))
}

function Find-ProjectRoot([string]$Seed) {
    $seedFull = Resolve-FullPath $Seed

    # 1) Current/seed folder.
    if (Test-ProjectRoot $seedFull) { return $seedFull }

    # 2) Parents. This fixes the common case where user runs from a nested folder.
    $cursor = $seedFull
    while ($cursor) {
        $parent = Split-Path -Parent $cursor
        if (!$parent -or $parent -eq $cursor) { break }
        if (Test-ProjectRoot $parent) { return $parent }
        $cursor = $parent
    }

    # 3) Shallow child search around seed and its parent. This fixes wrong extraction level.
    $rootsToSearch = New-Object System.Collections.Generic.List[string]
    $rootsToSearch.Add($seedFull)
    $seedParent = Split-Path -Parent $seedFull
    if ($seedParent -and (Test-Path -LiteralPath $seedParent)) { $rootsToSearch.Add($seedParent) }

    foreach ($root in $rootsToSearch | Select-Object -Unique) {
        $queue = New-Object System.Collections.Generic.Queue[object]
        $queue.Enqueue([pscustomobject]@{ Path = $root; Depth = 0 })
        while ($queue.Count -gt 0) {
            $item = $queue.Dequeue()
            if (Test-ProjectRoot $item.Path) { return $item.Path }
            if ($item.Depth -ge 5) { continue }
            Get-ChildItem -LiteralPath $item.Path -Directory -Force -ErrorAction SilentlyContinue | ForEach-Object {
                # Skip heavy/generated folders.
                if ($_.Name -in @("build-release", "build", "dist", ".git", ".vs", "Output", "logs")) { return }
                $queue.Enqueue([pscustomobject]@{ Path = $_.FullName; Depth = ($item.Depth + 1) })
            }
        }
    }

    throw "CMakeLists.txt was not found. Run this script from the extracted full project archive, or pass the parent folder that contains the full project. SeedPath=$seedFull"
}

function Stop-CarServiceProcesses([string]$ProjectRoot) {
    Write-Step "Stopping running CarService processes that can lock dist"
    $names = @("CarService", "CarServicePointG", "CarService PointG")
    $procs = Get-Process -ErrorAction SilentlyContinue | Where-Object { $names -contains $_.ProcessName }
    foreach ($p in $procs) {
        $path = $null
        try { $path = $p.MainModule.FileName } catch { $path = $null }
        if ($path -and $path.StartsWith($ProjectRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
            Write-Host "Stopping PID=$($p.Id) $path"
            Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
        } elseif (!$path) {
            Write-Host "Stopping PID=$($p.Id) $($p.ProcessName) (path unavailable)"
            Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
        }
    }
    Start-Sleep -Milliseconds 800
}

function Remove-DirectoryWithRetry([string]$Path) {
    if (!(Test-Path -LiteralPath $Path)) { return }
    for ($i = 1; $i -le 8; $i++) {
        try {
            Remove-Item -LiteralPath $Path -Recurse -Force -ErrorAction Stop
            return
        } catch {
            Write-Host "Cannot remove '$Path' attempt $i/8: $($_.Exception.Message)" -ForegroundColor Yellow
            Start-Sleep -Milliseconds (400 * $i)
        }
    }
    throw "Cannot remove '$Path'. Close running CarService/Explorer windows that point to this folder and retry."
}

function Copy-DirectoryContentsIfExists([string]$Source, [string]$Destination) {
    if (!(Test-Path -LiteralPath $Source)) { return }
    New-Item -ItemType Directory -Force -Path $Destination | Out-Null
    Copy-Item -LiteralPath (Join-Path $Source '*') -Destination $Destination -Recurse -Force
}

function Copy-RuntimeSidecars([string]$ProjectRoot, [string]$DistDir) {
    # windeployqt copies Qt DLLs/plugins only. These folders are app-owned runtime
    # assets and must also be present for direct dist smoke runs/import flows.
    Copy-DirectoryContentsIfExists (Join-Path $ProjectRoot 'resources\assets') (Join-Path $DistDir 'resources\assets')
    Copy-DirectoryContentsIfExists (Join-Path $ProjectRoot 'resources\python_vendor') (Join-Path $DistDir 'resources\python_vendor')
    Copy-DirectoryContentsIfExists (Join-Path $ProjectRoot 'resources\windows\ocr-layer') (Join-Path $DistDir 'resources\windows\ocr-layer')
}

$projectRoot = Find-ProjectRoot $SeedPath
Write-Step "Project root resolved"
Write-Host $projectRoot -ForegroundColor Green

Set-Location -LiteralPath $projectRoot

$buildOk = $true
$logDir = Join-Path $projectRoot "logs"
New-Item -ItemType Directory -Force -Path $logDir | Out-Null
$stamp = Get-Date -Format "yyyy-MM-dd_HH-mm-ss"
$buildLog = Join-Path $logDir ("build_deploy_run_autoroot_" + $stamp + ".log")
$appStdoutLog = Join-Path $logDir ("app_stdout_" + $stamp + ".log")
$appStderrLog = Join-Path $logDir ("app_stderr_" + $stamp + ".log")
$appMetaLog = Join-Path $logDir ("app_meta_" + $stamp + ".log")
$appDataDir = Join-Path $env:APPDATA "CarServicePointG\CarServicePointG"

Start-Transcript -Path $buildLog -Force | Out-Null
try {
    Write-Step "Build log paths"
    Write-Host "Build log: $buildLog" -ForegroundColor Cyan
    Write-Host "App stdout: $appStdoutLog" -ForegroundColor Cyan
    Write-Host "App stderr: $appStderrLog" -ForegroundColor Cyan
    Write-Host "App meta: $appMetaLog" -ForegroundColor Cyan

    Stop-CarServiceProcesses $projectRoot
    Remove-DirectoryWithRetry (Join-Path $projectRoot "build-release")
    Remove-DirectoryWithRetry (Join-Path $projectRoot "dist")

    if (!(Test-Path -LiteralPath $VsVcVars64)) { throw "vcvars64.bat not found: $VsVcVars64" }
    if (!(Test-Path -LiteralPath (Join-Path $QtPrefix "bin\windeployqt.exe"))) { throw "Qt/windeployqt not found under: $QtPrefix" }

    Write-Step "Configure and build"
    $cmd = 'call "' + $VsVcVars64 + '" && where cl && cmake -S "' + $projectRoot + '" -B "' + (Join-Path $projectRoot 'build-release') + '" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="' + $QtPrefix + '" && cmake --build "' + (Join-Path $projectRoot 'build-release') + '" --config Release'
    cmd /c $cmd
    if ($LASTEXITCODE -ne 0) { throw "CMake build failed with exit code $LASTEXITCODE" }

    Write-Step "Deploy"
    New-Item -ItemType Directory -Force -Path (Join-Path $projectRoot "dist") | Out-Null
    # Pick the real application executable explicitly. The build tree may contain
    # test/helper/runtime executables that can start and exit immediately if copied
    # as dist\CarService.exe.
    $builtExe = @(
        (Join-Path $projectRoot "build-release\CarServicePointG.exe"),
        (Join-Path $projectRoot "build-release\Release\CarServicePointG.exe"),
        (Join-Path $projectRoot "build-release\CarService.exe"),
        (Join-Path $projectRoot "build-release\Release\CarService.exe")
    ) | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
    if (-not $builtExe) { throw "Main application EXE not found after build: CarServicePointG.exe" }
    $builtExe = Get-Item -LiteralPath $builtExe

    $distExe = Join-Path $projectRoot "dist\CarService.exe"
    Copy-Item -LiteralPath $builtExe.FullName -Destination $distExe -Force
    & (Join-Path $QtPrefix "bin\windeployqt.exe") --release --compiler-runtime $distExe -force-openssl
    if ($LASTEXITCODE -ne 0) { throw "windeployqt failed with exit code $LASTEXITCODE" }
    Copy-RuntimeSidecars $projectRoot (Join-Path $projectRoot "dist")

    if (!$NoRun) {
        Write-Step "Launch"
        "Start time: $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')" | Out-File $appMetaLog -Encoding utf8
        "EXE: $distExe" | Out-File $appMetaLog -Append -Encoding utf8
        "WorkingDir: $((Split-Path $distExe -Parent))" | Out-File $appMetaLog -Append -Encoding utf8
        "AppDataDir: $appDataDir" | Out-File $appMetaLog -Append -Encoding utf8
        $env:QT_FORCE_STDERR_LOGGING = "1"
        $env:QT_LOGGING_RULES = "*.debug=true;qt.*=true"
        $p = Start-Process $distExe -WorkingDirectory (Split-Path $distExe -Parent) -PassThru -RedirectStandardOutput $appStdoutLog -RedirectStandardError $appStderrLog
        "PID: $($p.Id)" | Out-File $appMetaLog -Append -Encoding utf8
        Start-Sleep 10
        if ($p.HasExited) {
            # Refresh native Process metadata before reading ExitCode; without
            # WaitForExit() Windows PowerShell can print an empty ExitCode.
            try { $p.WaitForExit() } catch { }
            Write-Host ("Process exited. ExitCode=" + $p.ExitCode) -ForegroundColor Yellow
            "ExitCode: $($p.ExitCode)" | Out-File $appMetaLog -Append -Encoding utf8

            # Make the real startup reason visible directly in the transcript.
            # Earlier smoke logs only printed file paths, hiding the useful
            # app_stderr/app_stdout/app.log details.
            Show-TextTail $appStderrLog 120
            Show-TextTail $appStdoutLog 120
            $appLog = Join-Path $appDataDir "logs\app.log"
            Show-TextTail $appLog 160
        } else {
            Write-Host ("Process started. PID=" + $p.Id) -ForegroundColor Green
            "Status: running after 10 sec" | Out-File $appMetaLog -Append -Encoding utf8
        }
    }

    Write-Step "RESULT"
    Write-Host "Build/deploy script completed successfully." -ForegroundColor Green
} finally {
    Stop-Transcript | Out-Null
    Write-Host "Build log: $buildLog" -ForegroundColor Cyan
}
