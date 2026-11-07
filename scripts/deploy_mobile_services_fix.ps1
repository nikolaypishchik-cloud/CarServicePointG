param(
    [string]$TestedImage='carservicepointg-owner-api:order-services-tested',
    [string]$BackupLabel='mobile-services'
)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$stamp=Get-Date -Format 'yyyyMMdd-HHmmss'
$backup=Join-Path $root ("private-data/$BackupLabel-$stamp")
New-Item -ItemType Directory -Path $backup | Out-Null
$api='carservicepointg-owner-api-1'
function Docker-Step {
    & docker @args
    if ($LASTEXITCODE -ne 0) { throw "Docker failed: $($args[0])" }
}
Docker-Step cp "${api}:/app/app/api/v1/mobile.py" (Join-Path $backup 'mobile-before.py')
Docker-Step tag carservicepointg-owner-api "carservicepointg-owner-api:before-$BackupLabel-$stamp"
try {
    Docker-Step cp (Join-Path $root 'server/app/api/v1/mobile.py') "${api}:/app/app/api/v1/mobile.py"
    Docker-Step restart $api
    $healthy=$false
    for ($i=0;$i -lt 15;$i++) {
        Start-Sleep -Seconds 2
        & docker exec $api python -c 'import urllib.request; print(urllib.request.urlopen("http://127.0.0.1:8000/api/v1/health",timeout=3).status)'
        if ($LASTEXITCODE -eq 0) { $healthy=$true; break }
    }
    if (-not $healthy) { throw 'Health check failed' }
    Docker-Step tag $TestedImage carservicepointg-owner-api
} catch {
    Docker-Step cp (Join-Path $backup 'mobile-before.py') "${api}:/app/app/api/v1/mobile.py"
    Docker-Step restart $api
    throw
}
Write-Output "Deployed; backup: $backup"
