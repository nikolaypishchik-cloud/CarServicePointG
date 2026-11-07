$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$name = 'cs-bay-test-' + [guid]::NewGuid().ToString('N')
function Docker-Step {
    & docker @args
    if ($LASTEXITCODE -ne 0) { throw "Docker step failed: $($args[0])" }
}
try {
    # Isolated network namespace and tmpfs: never connect to an existing database.
    Docker-Step run -d --rm --name $name --network none --tmpfs /var/lib/postgresql/data:rw --env POSTGRES_USER=bay --env POSTGRES_PASSWORD=bay-test-only --env POSTGRES_DB=bay_test postgres:16-alpine
    $ready = $false
    for ($attempt = 0; $attempt -lt 30; $attempt++) {
        & docker exec $name pg_isready -U bay -d bay_test 2>$null
        if ($LASTEXITCODE -eq 0) { $ready = $true; break }
        Start-Sleep -Seconds 1
    }
    if (-not $ready) { throw 'Isolated PostgreSQL did not start' }
    Docker-Step run --rm --network "container:$name" --read-only --tmpfs /tmp:rw --workdir /tmp --env PYTHONDONTWRITEBYTECODE=1 --env PYTHONPATH=/audit --env DATABASE_URL=sqlite:// --env APP_ENV=development --env BAY_TEST_PG_URL=postgresql+psycopg://bay:bay-test-only@127.0.0.1/bay_test --mount "type=bind,source=$root/server/app,target=/audit/app,readonly" --mount "type=bind,source=$root/server/tests,target=/audit/tests,readonly" --mount "type=bind,source=$root/server/alembic,target=/audit/alembic,readonly" --entrypoint python carservicepointg-owner-api -B -m unittest discover -s /audit/tests -p test_bay_occupancy.py -v
} finally {
    # Only this script's fresh, unshared test container is removed.
    & docker rm -f $name 2>$null
}
