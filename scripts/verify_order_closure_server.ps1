param([switch]$Install)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$audit=Join-Path $root 'build-single-instance-audit/order-closure-20261028'
New-Item -ItemType Directory -Path $audit -Force | Out-Null
$api='carservicepointg-owner-api-1'
$postgres='carservicepointg-owner-postgres-1'
$base='carservicepointg-owner-api:before-order-closure-20261028'
$candidate='carservicepointg-owner-api:order-closure-20261028'
$test='cs-closure-copy-'+[guid]::NewGuid().ToString('N')
function Docker-Step {
    & docker @args
    if($LASTEXITCODE -ne 0){throw "Docker failed: $($args[0])"}
}
if(-not (Test-Path (Join-Path $audit 'database-before.dump'))) {
    Docker-Step commit $api $base
    Docker-Step exec $postgres sh -c 'pg_dump -U "$POSTGRES_USER" -d "$POSTGRES_DB" -Fc -f /tmp/order-closure-before.dump'
    Docker-Step cp "${postgres}:/tmp/order-closure-before.dump" (Join-Path $audit 'database-before.dump')
    Docker-Step exec $postgres rm /tmp/order-closure-before.dump
    foreach($file in @('api/v1/bays.py','api/v1/reports.py','services/bay_corrections.py')) {
        $destination=Join-Path $audit "before/$file"
        New-Item -ItemType Directory -Force (Split-Path $destination -Parent) | Out-Null
        Docker-Step cp "${api}:/app/app/$file" $destination
    }
}
$syncBackup=Join-Path $audit 'before/services/sync_service.py'
if(-not (Test-Path $syncBackup)){Docker-Step cp "${api}:/app/app/services/sync_service.py" $syncBackup}
Docker-Step build --network none --build-arg "BASE_IMAGE=$base" -f (Join-Path $root 'server/Dockerfile.order-closure') -t $candidate (Join-Path $root 'server')
try {
    Docker-Step run -d --rm --name $test --network none --tmpfs /var/lib/postgresql/data:rw --env POSTGRES_USER=bay --env POSTGRES_PASSWORD=bay-test-only --env POSTGRES_DB=bay_test postgres:16-alpine
    $ready=$false
    for($i=0;$i -lt 30;$i++) {
        & docker exec $test pg_isready -U bay -d bay_test 2>$null
        if($LASTEXITCODE -eq 0){$ready=$true;break}
        Start-Sleep -Seconds 1
    }
    if(-not $ready){throw 'Isolated PostgreSQL unavailable'}
    Docker-Step cp (Join-Path $audit 'database-before.dump') "${test}:/tmp/copy.dump"
    Docker-Step exec $test pg_restore --no-owner --no-privileges --exit-on-error -U bay -d bay_test /tmp/copy.dump
    $testArgs=@('run','--rm','--network',"container:$test",'--read-only','--tmpfs','/tmp:rw','--workdir','/tmp',
        '--env','PYTHONDONTWRITEBYTECODE=1','--env','PYTHONPATH=/app:/audit/tests','--env','APP_ENV=development',
        '--env','DATABASE_URL=postgresql+psycopg://bay:bay-test-only@127.0.0.1/bay_test',
        '--env','BAY_TEST_PG_URL=postgresql+psycopg://bay:bay-test-only@127.0.0.1/bay_test',
        '--mount',"type=bind,source=$root/scripts/install_client_payment_tables.py,target=/audit/install.py,readonly",
        '--mount',"type=bind,source=$root/server/tests,target=/audit/tests,readonly",
        '--mount',"type=bind,source=$root/server/alembic,target=/audit/alembic,readonly",'--entrypoint','python',$candidate)
    Docker-Step @testArgs -B /audit/install.py
    Docker-Step @testArgs -B -m unittest test_client_payments test_order_closure test_bay_occupancy -v
    Docker-Step @testArgs -B -c "from app.main import app; assert '/api/v1/bays/closure-commit' in {r.path for r in app.routes}; print('Closure routes verified')"
} finally {
    & docker rm -f $test 2>$null
}
if(-not $Install){Write-Output 'Verified candidate only; live server unchanged';exit 0}
foreach($file in @('models/client_payment.py','services/client_payment_service.py','services/order_closure_service.py','services/bay_corrections.py','services/sync_service.py')) {
    Docker-Step cp (Join-Path $root "server/app/$file") "${api}:/app/app/$file"
}
Docker-Step cp (Join-Path $root 'scripts/install_client_payment_tables.py') "${api}:/tmp/install_client_payment_tables.py"
try {
    Docker-Step exec --env PYTHONPATH=/app $api python -B /tmp/install_client_payment_tables.py
    foreach($file in @('api/v1/bays.py','api/v1/reports.py')) {Docker-Step cp (Join-Path $root "server/app/$file") "${api}:/app/app/$file"}
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
    foreach($file in @('api/v1/bays.py','api/v1/reports.py','services/bay_corrections.py','services/sync_service.py')) {Docker-Step cp (Join-Path $audit "before/$file") "${api}:/app/app/$file"}
    Docker-Step restart $api
    throw
} finally {
    & docker exec $api rm -f /tmp/install_client_payment_tables.py
}
Write-Output 'Order closure server installed; existing financial data preserved'
