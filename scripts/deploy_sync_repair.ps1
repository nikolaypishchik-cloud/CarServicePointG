$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$backup = Join-Path $root 'private-data/sync-repair-20261001-173532'
$container = 'carservicepointg-owner-api-1'
$image = 'carservicepointg-owner-api'
$previous = 'carservicepointg-owner-api:before-sync-repair-20261001'
$files = @('planning_sync_service.py', 'replication_lock.py', 'booking_availability.py')
function Docker-Step {
    & docker @args
    if ($LASTEXITCODE -ne 0) { throw "Docker failed: $($args[0])" }
}
Docker-Step cp "${container}:/app/app/services/planning_sync_service.py" (Join-Path $backup 'planning-original.py')
Docker-Step tag $image $previous
Docker-Step build --build-arg "BASE_IMAGE=$previous" -f (Join-Path $root 'server/Dockerfile.sync-repair') -t "${image}:sync-repair-tested" (Join-Path $root 'server')
Docker-Step run --rm --network none --mount "type=bind,source=$root/scripts/check_deployed_planning.py,target=/tmp/check.py,readonly" -e PYTHONPATH=/app "${image}:sync-repair-tested" python /tmp/check.py
try {
    foreach ($file in $files) {
        Docker-Step cp (Join-Path $root "server/app/services/$file") "${container}:/app/app/services/$file"
    }
    Docker-Step restart $container
    $healthy = $false
    for ($i=0; $i -lt 15; $i++) {
        Start-Sleep -Seconds 2
        & docker exec $container python -c "import urllib.request; print(urllib.request.urlopen('http://127.0.0.1:8000/api/v1/health',timeout=3).status)"
        if ($LASTEXITCODE -eq 0) { $healthy=$true; break }
    }
    if (-not $healthy) { throw 'Health check failed' }
    Docker-Step tag "${image}:sync-repair-tested" $image
} catch {
    Docker-Step cp (Join-Path $backup 'planning-original.py') "${container}:/app/app/services/planning_sync_service.py"
    Docker-Step restart $container
    throw
}
