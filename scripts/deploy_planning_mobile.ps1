param([ValidateSet('planning-mobile', 'parts-overdue', 'mobile-close', 'mobile-workflow-planning')][string]$Feature = 'planning-mobile')
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$backup = Join-Path $root "private-data/$Feature-$stamp"
$api = 'carservicepointg-owner-api-1'
$postgres = 'carservicepointg-owner-postgres-1'
$snapshot = "carservicepointg-owner-api:before-planning-$stamp"
$candidate = "carservicepointg-owner-api:planning-tested-$stamp"
$files = @('api/v1/mobile.py', 'services/planning_sync_service.py', 'services/sync_emit_service.py', 'services/part_overdue_policy.py')
function Docker-Step {
    & docker @args
    if ($LASTEXITCODE -ne 0) { throw "Docker failed: $($args[0])" }
}
New-Item -ItemType Directory -Path $backup | Out-Null
foreach ($file in $files) {
    $destination = Join-Path $backup $file
    New-Item -ItemType Directory -Path (Split-Path $destination -Parent) -Force | Out-Null
    & docker exec $api test -f "/app/app/$file"
    if ($LASTEXITCODE -eq 0) {
        Docker-Step cp "${api}:/app/app/$file" $destination
    } elseif ($file -ne 'services/part_overdue_policy.py') {
        throw "Required deployed module missing: $file"
    }
}
# Snapshot the running code, including earlier fixes not present in the base image.
Docker-Step commit $api $snapshot
Docker-Step exec $postgres sh -c 'pg_dump -U "$POSTGRES_USER" -d "$POSTGRES_DB" -Fc -f /tmp/planning-before.dump && pg_restore -l /tmp/planning-before.dump >/dev/null'
Docker-Step cp "${postgres}:/tmp/planning-before.dump" (Join-Path $backup 'database-before.dump')
Docker-Step exec $postgres rm /tmp/planning-before.dump
Docker-Step build --network none --build-arg "BASE_IMAGE=$snapshot" -f (Join-Path $root 'server/Dockerfile.planning-mobile') -t $candidate (Join-Path $root 'server')
$testCode = "import unittest, runpy; suite=unittest.defaultTestLoader.discover('/audit/tests', pattern='test_mobile_sync.py'); result=unittest.TextTestRunner(verbosity=1).run(suite); result.wasSuccessful() or exit(1); runpy.run_path('/audit/check_planning.py')"
Docker-Step run --rm --network none --read-only --tmpfs /tmp:rw --workdir /tmp --env PYTHONDONTWRITEBYTECODE=1 --env PYTHONPATH=/app --env DATABASE_URL=sqlite:// --env STAGE2_PG_TEST_URL= --env APP_ENV=development --env AI_ENABLED=false --env AI_ONLINE_ENABLED=false --env ALLOW_BOOTSTRAP_OWNER=false --mount "type=bind,source=$root/server/tests,target=/audit/tests,readonly" --mount "type=bind,source=$root/scripts/check_deployed_planning_mobile.py,target=/audit/check_planning.py,readonly" --entrypoint python $candidate -B -c $testCode
try {
    foreach ($file in $files) {
        Docker-Step cp (Join-Path $root "server/app/$file") "${api}:/app/app/$file"
    }
    Docker-Step restart $api
    $healthy = $false
    for ($attempt = 0; $attempt -lt 15; $attempt++) {
        Start-Sleep -Seconds 2
        & docker exec $api python -c "import urllib.request; assert urllib.request.urlopen('http://127.0.0.1:8000/api/v1/health', timeout=3).status == 200"
        if ($LASTEXITCODE -eq 0) { $healthy = $true; break }
    }
    if (-not $healthy) { throw 'API health check failed' }
    Docker-Step exec $api python -B -c "import inspect; from app.main import app; from app.api.v1.mobile import my_overdue_parts; paths={r.path for r in app.routes}; assert {'/api/v1/mobile/planning-availability','/api/v1/mobile/planning/update','/api/v1/mobile/planning/delete'}.issubset(paths); assert 'scope' in inspect.signature(my_overdue_parts).parameters; print('Planning mutation routes and desktop debt scope installed')"
    Docker-Step tag $candidate carservicepointg-owner-api:latest
} catch {
    foreach ($file in $files) {
        $previousFile = Join-Path $backup $file
        if (Test-Path -LiteralPath $previousFile) {
            Docker-Step cp $previousFile "${api}:/app/app/$file"
        } else {
            Docker-Step exec $api rm "/app/app/$file"
        }
    }
    Docker-Step restart $api
    throw
}
Write-Output "Deployed $Feature; code and database backup: $backup; rollback image: $snapshot"
