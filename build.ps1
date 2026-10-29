param([ValidateSet('all','windows','android','tests')][string]$Target='all')
$ErrorActionPreference = 'Stop'
& python (Join-Path $PSScriptRoot 'tools/build.py') --target $Target
if ($LASTEXITCODE -ne 0) { throw "CyVR build failed ($LASTEXITCODE)" }
