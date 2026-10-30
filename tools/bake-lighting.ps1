# SPDX-License-Identifier: MIT
param([ValidateRange(64, 2048)][int]$Samples = 512)
$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path -Parent $PSScriptRoot
$taskSdk = if ($env:ANDROID_HOME) { $env:ANDROID_HOME } else { Join-Path $env:LOCALAPPDATA 'Android\Sdk' }
$taskCmake = Join-Path $taskSdk 'cmake\3.22.1\bin\cmake.exe'
$taskBuild = Join-Path $taskRoot 'build\windows-clang'
if (!(Test-Path -LiteralPath (Join-Path $taskBuild 'CMakeCache.txt'))) {
    throw 'Run .\build.ps1 -Target windows once to configure the native compiler.'
}
& $taskCmake -S $taskRoot -B $taskBuild -DCYVR_BUILD_SCENE_BAKER=ON
if ($LASTEXITCODE -ne 0) { throw 'Baker configuration failed.' }
& $taskCmake --build $taskBuild --target cyvr_bake_lighting --parallel 4
if ($LASTEXITCODE -ne 0) { throw 'Baker compilation failed.' }
$taskBaker = Join-Path $taskBuild 'cyvr_bake_lighting.exe'
foreach ($taskScene in @(@(3, 'lounge'), @(4, 'observatory'), @(5, 'terrace'))) {
    & $taskBaker $taskScene[0] (Join-Path $taskRoot "assets\pbr\lighting-$($taskScene[1]).bin") $Samples
    if ($LASTEXITCODE -ne 0) { throw "Lighting bake failed: $($taskScene[1])" }
    & $taskBaker --smooth $taskScene[0] (Join-Path $taskRoot "assets\pbr\lighting-$($taskScene[1]).bin")
    if ($LASTEXITCODE -ne 0) { throw "Lighting filter failed: $($taskScene[1])" }
}
& python (Join-Path $PSScriptRoot 'prepare_pbr.py') --record-lighting
if ($LASTEXITCODE -ne 0) { throw 'Lighting checksum recording failed.' }
