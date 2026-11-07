param([switch]$Install)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$backup = Join-Path $root "build-single-instance-audit/server-bays-$stamp"
$api = 'carservicepointg-owner-api-1'
$postgres = 'carservicepointg-owner-postgres-1'
$snapshot = "carservicepointg-owner-api:before-bays-$stamp"
$candidate = "carservicepointg-owner-api:bays-tested-$stamp"
$test = 'cs-bay-copy-' + [guid]::NewGuid().ToString('N')
$dump = "/tmp/bays-$stamp.dump"
$files = @('api/v1/api.py','api/v1/bays.py','models/bay_occupancy.py','services/bay_occupancy_service.py','services/sync_access.py')
function Docker-Step {
    & docker @args
    if ($LASTEXITCODE -ne 0) { throw "Docker failed: $($args[0])" }
}
New-Item -ItemType Directory -Path $backup | Out-Null
# Preserve the running image, including earlier hotfixes.
Docker-Step commit $api $snapshot
foreach ($file in $files) {
    & docker exec $api test -f "/app/app/$file"
    if ($LASTEXITCODE -eq 0) {
        $destination = Join-Path $backup $file
        New-Item -ItemType Directory -Path (Split-Path $destination -Parent) -Force | Out-Null
        Docker-Step cp "${api}:/app/app/$file" $destination
    }
}
Docker-Step exec $postgres sh -c ('pg_dump -U "$POSTGRES_USER" -d "$POSTGRES_DB" -Fc -f ' + $dump + ' && pg_restore -l ' + $dump + ' >/dev/null')
Docker-Step cp "${postgres}:$dump" (Join-Path $backup 'database-before.dump')
Docker-Step exec $postgres rm $dump
Docker-Step build --network none --build-arg "BASE_IMAGE=$snapshot" -f (Join-Path $root 'server/Dockerfile.bay-occupancy') -t $candidate (Join-Path $root 'server')
try {
    Docker-Step run -d --rm --name $test --network none --tmpfs /var/lib/postgresql/data:rw --env POSTGRES_USER=bay --env POSTGRES_PASSWORD=bay-test-only --env POSTGRES_DB=bay_test postgres:16-alpine
    $ready = $false
    for ($attempt=0; $attempt -lt 30; $attempt++) {
        & docker exec $test pg_isready -U bay -d bay_test 2>$null
        if ($LASTEXITCODE -eq 0) { $ready=$true;break }
        Start-Sleep -Seconds 1
    }
    if (-not $ready) { throw 'Test database unavailable' }
    Docker-Step cp (Join-Path $backup 'database-before.dump') "${test}:/tmp/copy.dump"
    Docker-Step exec $test pg_restore --no-owner --no-privileges --exit-on-error -U bay -d bay_test /tmp/copy.dump
    $testArgs = @('run','--rm','--network',"container:$test",'--read-only','--tmpfs','/tmp:rw','--workdir','/tmp',
        '--env','PYTHONDONTWRITEBYTECODE=1','--env','PYTHONPATH=/app','--env','APP_ENV=development',
        '--env','DATABASE_URL=postgresql+psycopg://bay:bay-test-only@127.0.0.1/bay_test',
        '--env','BAY_TEST_PG_URL=postgresql+psycopg://bay:bay-test-only@127.0.0.1/bay_test',
        '--mount',"type=bind,source=$root/scripts/install_bay_tables.py,target=/audit/install.py,readonly",
        '--mount',"type=bind,source=$root/server/tests,target=/audit/tests,readonly",
        '--mount',"type=bind,source=$root/server/alembic,target=/audit/alembic,readonly",'--entrypoint','python',$candidate)
    Docker-Step @testArgs -B /audit/install.py
    Docker-Step @testArgs -B -c "from app.main import app; from app.db.session import engine; from sqlalchemy import text; paths={r.path for r in app.routes}; assert '/api/v1/bays/change' in paths; c=engine.connect(); assert c.scalar(text('select count(*) from bay_intervals'))==0; c.close(); print('Candidate routes and empty history verified on restored copy')"
    Docker-Step @testArgs -B -m unittest discover -s /audit/tests -p test_bay_occupancy.py -v
} finally {
    # This random test container has no persistent volumes or production network.
    & docker rm -f $test 2>$null
}
if (-not $Install) { Write-Output "Verified only; backup=$backup; candidate=$candidate"; exit 0 }
# Install only the reviewed addition, not the unrelated migration chain.
Docker-Step cp (Join-Path $root 'scripts/install_bay_tables.py') "${api}:/tmp/install_bay_tables.py"
foreach ($file in $files | Where-Object { $_ -ne 'api/v1/api.py' }) {
    Docker-Step cp (Join-Path $root "server/app/$file") "${api}:/app/app/$file"
}
try {
    Docker-Step exec --env PYTHONPATH=/app $api python -B /tmp/install_bay_tables.py
    Docker-Step cp (Join-Path $root 'server/app/api/v1/api.py') "${api}:/app/app/api/v1/api.py"
    Docker-Step restart $api
    $healthy=$false
    for ($attempt=0; $attempt -lt 20; $attempt++) {
        Start-Sleep -Seconds 2
        & docker exec $api python -B -c "import urllib.request; assert urllib.request.urlopen('http://127.0.0.1:8000/api/v1/health',timeout=3).status==200"
        if ($LASTEXITCODE -eq 0) { $healthy=$true;break }
    }
    if (-not $healthy) { throw 'Updated server did not become healthy' }
    Docker-Step exec $api python -B -c "from app.main import app; assert '/api/v1/bays/change' in {r.path for r in app.routes}; print('Bay API installed')"
    Docker-Step tag $candidate carservicepointg-owner-api:latest
} catch {
    # Retain new history tables; roll back routing, never restore an old data dump over current data.
    Docker-Step cp (Join-Path $backup 'api/v1/api.py') "${api}:/app/app/api/v1/api.py"
    Docker-Step restart $api
    throw
} finally {
    & docker exec $api rm -f /tmp/install_bay_tables.py
}
Write-Output "Installed bay API and new empty tables. Backup: $backup. Existing data verified unchanged."
