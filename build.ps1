param([ValidateSet('all','windows','android','tests')][string]$Target='all', [switch]$NoImmersive)
$ErrorActionPreference = 'Stop'
$buildArgs = @('--target', $Target)
if ($NoImmersive) { $buildArgs += '--no-immersive' }
& python (Join-Path $PSScriptRoot 'tools/build.py') @buildArgs
if ($LASTEXITCODE -ne 0) { throw "CyVR build failed ($LASTEXITCODE)" }
