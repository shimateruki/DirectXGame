$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$exe = Join-Path $root 'generated\outputs\Development\DirectXGame.exe'
if (-not (Test-Path -LiteralPath $exe)) {
    Write-Host 'Development executable was not found. Build Development x64 once first.'
    exit 1
}
Start-Process -FilePath $exe -WorkingDirectory $root