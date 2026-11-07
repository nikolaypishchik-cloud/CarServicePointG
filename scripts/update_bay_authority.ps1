param([switch]$Install)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$stamp=Get-Date -Format 'yyyyMMdd-HHmmss'
$backup=Join-Path $root "build-single-instance-audit/server-bay-update-$stamp"
$api='carservicepointg-owner-api-1'
$postgres='carservicepointg-owner-postgres-1'
$snapshot="carservicepointg-owner-api:before-bay-update-$stamp"
$candidate="carservicepointg-owner-api:bay-update-tested-$stamp"
$test='cs-bay-update-test-'+[guid]::NewGuid().ToString('N')
$files=@('api/v1/bays.py','services/bay_occupancy_service.py','services/bay_corrections.py')
function Docker-Step {
    & docker @args
    if($LASTEXITCODE -ne 0){throw "Docker failed: $($args[0])"}
}
New-Item -ItemType Directory -Path $backup | Out-Null
Docker-Step commit $api $snapshot
$existing=@()
foreach($file in $files) {
    & docker exec $api test -f "/app/app/$file"
    if($LASTEXITCODE -eq 0) {
        $existing+=$file
        $destination=Join-Path $backup $file
        New-Item -ItemType Directory -Path (Split-Path $destination -Parent) -Force | Out-Null
        Docker-Step cp "${api}:/app/app/$file" $destination
    }
}
$dump="/tmp/bay-update-$stamp.dump"
Docker-Step exec $postgres sh -c ('pg_dump -U "$POSTGRES_USER" -d "$POSTGRES_DB" -Fc -f '+$dump+' && pg_restore -l '+$dump+' >/dev/null')
Docker-Step cp "${postgres}:$dump" (Join-Path $backup 'database-before.dump')
Docker-Step exec $postgres rm $dump
Docker-Step build --network none --build-arg "BASE_IMAGE=$snapshot" -f (Join-Path $root 'server/Dockerfile.bay-occupancy') -t $candidate (Join-Path $root 'server')
try {
    Docker-Step run -d --rm --name $test --network none --tmpfs /var/lib/postgresql/data:rw --env POSTGRES_USER=bay --env POSTGRES_PASSWORD=bay-test-only --env POSTGRES_DB=bay_test postgres:16-alpine
    $ready=$false
    for($i=0;$i -lt 30;$i++) {
        & docker exec $test pg_isready -U bay -d bay_test 2>$null
        if($LASTEXITCODE -eq 0){$ready=$true;break}
        Start-Sleep -Seconds 1
    }
    if(-not $ready){throw 'Isolated PostgreSQL unavailable'}
    Docker-Step cp (Join-Path $backup 'database-before.dump') "${test}:/tmp/copy.dump"
    Docker-Step exec $test pg_restore --no-owner --no-privileges --exit-on-error -U bay -d bay_test /tmp/copy.dump
    $testArgs=@('run','--rm','--network',"container:$test",'--read-only','--tmpfs','/tmp:rw','--workdir','/tmp',
        '--env','PYTHONDONTWRITEBYTECODE=1','--env','PYTHONPATH=/app','--env','APP_ENV=development',
        '--env','DATABASE_URL=postgresql+psycopg://bay:bay-test-only@127.0.0.1/bay_test',
        '--env','BAY_TEST_PG_URL=postgresql+psycopg://bay:bay-test-only@127.0.0.1/bay_test',
        '--mount',"type=bind,source=$root/server/tests,target=/audit/tests,readonly",
        '--mount',"type=bind,source=$root/server/alembic,target=/audit/alembic,readonly",'--entrypoint','python',$candidate)
    Docker-Step @testArgs -B -c "from app.main import app; paths={r.path for r in app.routes}; assert all('/api/v1/bays/'+p in paths for p in ['close-orders','correct-interval','correct-calendar']); print('Updated routes verified on restored copy')"
    # Every test creates a random schema, never touches restored working rows.
    Docker-Step @testArgs -B -m unittest discover -s /audit/tests -p test_bay_occupancy.py -v
} finally {
    & docker rm -f $test 2>$null
}
if(-not $Install){Write-Output "Verified only. Backup: $backup. Candidate: $candidate";exit 0}
try {
    foreach($file in $files){Docker-Step cp (Join-Path $root "server/app/$file") "${api}:/app/app/$file"}
    # Code-only update: no migration, SQL command or bay command is executed.
    Docker-Step restart $api
    $healthy=$false
    for($i=0;$i -lt 20;$i++) {
        Start-Sleep -Seconds 2
        & docker exec $api python -B -c "import urllib.request; assert urllib.request.urlopen('http://127.0.0.1:8000/api/v1/health',timeout=3).status==200"
        if($LASTEXITCODE -eq 0){$healthy=$true;break}
    }
    if(-not $healthy){throw 'Server health check failed'}
    Docker-Step tag $candidate carservicepointg-owner-api:latest
} catch {
    foreach($file in $existing){Docker-Step cp (Join-Path $backup $file) "${api}:/app/app/$file"}
    Docker-Step restart $api
    throw
}
Write-Output "Code-only bay update installed. No schema/data migration. Backup: $backup"
