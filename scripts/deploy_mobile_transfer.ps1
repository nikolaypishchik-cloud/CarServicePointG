$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$backup=Join-Path $root ('private-data/mobile-transfer-'+(Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Path $backup | Out-Null
$api='carservicepointg-owner-api-1'
$pg='carservicepointg-owner-postgres-1'
function Docker-Step {
    & docker @args
    if ($LASTEXITCODE -ne 0) { throw "Docker failed: $($args[0])" }
}
Docker-Step exec $pg sh -c 'pg_dump -U "$POSTGRES_USER" -d "$POSTGRES_DB" -Fc -f /tmp/mobile-transfer.dump'
Docker-Step cp "${pg}:/tmp/mobile-transfer.dump" (Join-Path $backup 'server-before.dump')
Docker-Step cp "${api}:/app/app/core/security.py" (Join-Path $backup 'security-before.py')
Docker-Step tag carservicepointg-owner-api carservicepointg-owner-api:before-device-transfer-20261001
Docker-Step cp (Join-Path $root 'server/app/services/mobile_device_binding.py') "${api}:/app/app/services/mobile_device_binding.py"
Docker-Step cp (Join-Path $root 'server/patch_mobile_binding.py') "${api}:/tmp/patch_mobile_binding.py"
Docker-Step exec $api python /tmp/patch_mobile_binding.py
Docker-Step restart $api
$healthy=$false
for ($i=0;$i -lt 15;$i++) {
    Start-Sleep -Seconds 2
    & docker exec $api python -c 'import urllib.request; print(urllib.request.urlopen("http://127.0.0.1:8000/api/v1/health",timeout=3).status)'
    if ($LASTEXITCODE -eq 0) { $healthy=$true; break }
}
if (-not $healthy) {
    Docker-Step cp (Join-Path $backup 'security-before.py') "${api}:/app/app/core/security.py"
    Docker-Step restart $api
    throw 'Health check failed; restored security module'
}
Docker-Step tag carservicepointg-owner-api:device-transfer-tested carservicepointg-owner-api
Docker-Step cp (Join-Path $root 'scripts/transfer_mobile_binding.py') "${api}:/tmp/transfer_mobile_binding.py"
Docker-Step exec -e PYTHONPATH=/app $api python /tmp/transfer_mobile_binding.py --device f7decac9-a1e0-45d7-8c8a-612d409b7e3f --from-employee m1 --to-employee m2
Write-Output "Backup: $backup"
