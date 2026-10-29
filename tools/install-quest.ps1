param([string]$Serial='')
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$apkPath = Join-Path $projectRoot 'dist/android/CyVR_UI-debug.apk'
if (!(Test-Path -LiteralPath $apkPath)) { throw "Compilez d’abord avec .\build.ps1 -Target android." }
$adbPath = if ($env:ANDROID_HOME) { Join-Path $env:ANDROID_HOME 'platform-tools/adb.exe' } else { 'adb' }
if (!$Serial) {
    $devices = @(& $adbPath devices | Where-Object { $_ -match '^\S+\s+device$' } | ForEach-Object { ($_ -split '\s+')[0] })
    if ($devices.Count -ne 1) { throw 'Connectez un seul casque autorisé, ou précisez -Serial IDENTIFIANT_ADB.' }
    $Serial = $devices[0]
}
& $adbPath -s $Serial install -r $apkPath
if ($LASTEXITCODE -ne 0) { throw 'Installation échouée.' }
& $adbPath -s $Serial shell am start -a android.intent.action.MAIN -c com.oculus.intent.category.VR -n com.cyvr.ui/android.app.NativeActivity
if ($LASTEXITCODE -ne 0) { throw 'Lancement échoué.' }
