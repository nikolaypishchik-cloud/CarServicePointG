$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$backup = Join-Path $root 'private-data/sync-audit-20261001-194216'
$container = 'carservicepointg-owner-api-1'
$postgres = 'carservicepointg-owner-postgres-1'
function Docker-Step {
    & docker @args
    if ($LASTEXITCODE -ne 0) { throw "Docker failed: $($args[0])" }
}
Docker-Step exec $postgres sh -c 'pg_dump -U "$POSTGRES_USER" -d "$POSTGRES_DB" -Fc -f /tmp/sync-audit-20261001.dump'
Docker-Step cp "${postgres}:/tmp/sync-audit-20261001.dump" (Join-Path $backup 'server-before.dump')
Docker-Step tag carservicepointg-owner-api carservicepointg-owner-api:before-mobile-auth-20261001
Docker-Step cp (Join-Path $root 'server/app/models/auth_security.py') "${container}:/app/app/models/auth_security.py"
Docker-Step cp (Join-Path $root 'server/app/services/auth_security_service.py') "${container}:/app/app/services/auth_security_service.py"
Docker-Step exec $container python -c 'from app.db.session import engine; from app.models.auth_security import AuthSecurity; AuthSecurity.__table__.create(engine,checkfirst=True); print("AuthSecurity schema ready")'
Docker-Step restart $container
$healthy = $false
for ($i=0; $i -lt 15; $i++) {
    Start-Sleep -Seconds 2
    & docker exec $container python -c 'import urllib.request; print(urllib.request.urlopen("http://127.0.0.1:8000/api/v1/health",timeout=3).status)'
    if ($LASTEXITCODE -eq 0) { $healthy=$true; break }
}
if (-not $healthy) { throw 'Server health check failed; previous image retained for rollback' }
Docker-Step tag carservicepointg-owner-api:mobile-sync-audit-tested carservicepointg-owner-api
Write-Output 'Mobile authentication repair deployed; previous image and database backup retained.'
