$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$backup = Join-Path $root "private-data/mobile29-server-$stamp"
New-Item -ItemType Directory -Path $backup | Out-Null
$services = @(
    @{ Container = 'carservicepointg-owner-api-1'; Image = 'carservicepointg-owner-api' }
)
function Docker-Step {
    & docker @args
    if ($LASTEXITCODE -ne 0) { throw "Docker step failed: $($args[0])" }
}
foreach ($service in $services) {
    $name = $service.Container
    $image = $service.Image
    $snapshot = "${image}:before-mobile29-$stamp"
    $folder = Join-Path $backup $name
    New-Item -ItemType Directory -Path $folder | Out-Null
    Docker-Step cp "${name}:/app/app/api/v1/mobile.py" (Join-Path $folder 'mobile.py')
    Docker-Step cp "${name}:/app/app/services/planning_sync_service.py" (Join-Path $folder 'planning_sync_service.py')
    Docker-Step tag $image $snapshot
    Docker-Step build --build-arg "BASE_IMAGE=$snapshot" -f (Join-Path $root 'server/Dockerfile.mobile29') -t $image (Join-Path $root 'server')
    try {
        Docker-Step cp (Join-Path $root 'server/app/api/v1/mobile.py') "${name}:/app/app/api/v1/mobile.py"
        Docker-Step cp (Join-Path $root 'server/app/services/planning_sync_service.py') "${name}:/app/app/services/planning_sync_service.py"
        Docker-Step restart $name
        $healthy = $false
        for ($attempt = 0; $attempt -lt 15; $attempt++) {
            Start-Sleep -Seconds 2
            & docker exec $name python -c "import urllib.request; print(urllib.request.urlopen('http://127.0.0.1:8000/api/v1/health', timeout=3).status)"
            if ($LASTEXITCODE -eq 0) { $healthy = $true; break }
        }
        if (-not $healthy) { throw 'API health check failed' }
    } catch {
        Docker-Step cp (Join-Path $folder 'mobile.py') "${name}:/app/app/api/v1/mobile.py"
        Docker-Step cp (Join-Path $folder 'planning_sync_service.py') "${name}:/app/app/services/planning_sync_service.py"
        Docker-Step tag $snapshot $image
        Docker-Step restart $name
        throw
    }
}
Write-Output "Backups: $backup"
