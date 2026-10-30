param()
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$depsRoot = Join-Path $projectRoot '.deps'
New-Item -ItemType Directory -Force -Path $depsRoot | Out-Null
function Get-PinnedFile([string]$Uri, [string]$Path) {
    if (!(Test-Path -LiteralPath $Path)) {
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $Path) | Out-Null
        Invoke-WebRequest -Uri $Uri -OutFile $Path
    }
}
$xrVersion = '1.1.49'
$xrBase = "https://raw.githubusercontent.com/KhronosGroup/OpenXR-SDK/release-$xrVersion"
foreach ($name in @('openxr.h','openxr_platform.h','openxr_platform_defines.h')) {
    Get-PinnedFile "$xrBase/include/openxr/$name" (Join-Path $projectRoot "third_party/openxr/include/openxr/$name")
}
Get-PinnedFile "$xrBase/LICENSE" (Join-Path $projectRoot 'third_party/openxr/LICENSE')
$aarUri = "https://repo.maven.apache.org/maven2/org/khronos/openxr/openxr_loader_for_android/$xrVersion/openxr_loader_for_android-$xrVersion.aar"
$aarPath = Join-Path $depsRoot "openxr-$xrVersion.zip"
Get-PinnedFile $aarUri $aarPath
if ((Get-FileHash -LiteralPath $aarPath -Algorithm SHA256).Hash.ToLower() -ne '9a731db8a21bfc5c4be47c8862a508a2e4db3d7167062dc65683cedf7563f0cd') { throw 'OpenXR AAR checksum mismatch' }
if (!(Test-Path -LiteralPath (Join-Path $depsRoot 'openxr'))) { Expand-Archive -LiteralPath $aarPath -DestinationPath (Join-Path $depsRoot 'openxr') }
$vkBase = 'https://raw.githubusercontent.com/KhronosGroup/Vulkan-Headers/v1.3.290'
foreach ($name in @('vulkan.h','vulkan_core.h','vk_platform.h','vulkan_win32.h','vulkan_android.h')) {
    Get-PinnedFile "$vkBase/include/vulkan/$name" (Join-Path $projectRoot "third_party/vulkan/include/vulkan/$name")
}
foreach ($name in @('vulkan_video_codecs_common.h','vulkan_video_codec_h264std.h','vulkan_video_codec_h264std_decode.h','vulkan_video_codec_h264std_encode.h','vulkan_video_codec_h265std.h','vulkan_video_codec_h265std_decode.h','vulkan_video_codec_h265std_encode.h','vulkan_video_codec_av1std.h','vulkan_video_codec_av1std_decode.h')) {
    Get-PinnedFile "$vkBase/include/vk_video/$name" (Join-Path $projectRoot "third_party/vulkan/include/vk_video/$name")
}
Get-PinnedFile "$vkBase/LICENSE.md" (Join-Path $projectRoot 'third_party/vulkan/LICENSE.md')
Get-PinnedFile 'https://www.apache.org/licenses/LICENSE-2.0.txt' (Join-Path $projectRoot 'third_party/Apache-2.0.txt')
Get-PinnedFile "$xrBase/src/external/jsoncpp/LICENSE" (Join-Path $projectRoot 'third_party/notices/jsoncpp-LICENSE.txt')
Get-PinnedFile "$xrBase/src/external/jnipp/LICENSE" (Join-Path $projectRoot 'third_party/notices/jnipp-LICENSE.txt')
Get-PinnedFile 'https://www.boost.org/LICENSE_1_0.txt' (Join-Path $projectRoot 'third_party/notices/BSL-1.0.txt')
$stbBase = 'https://raw.githubusercontent.com/nothings/stb/013ac3beddff3dbffafd5177e7972067cd2b5083'
Get-PinnedFile "$stbBase/stb_image.h" (Join-Path $projectRoot 'third_party/stb/stb_image.h')
Get-PinnedFile "$stbBase/LICENSE" (Join-Path $projectRoot 'third_party/stb/LICENSE')
Get-PinnedFile 'https://raw.githubusercontent.com/spdx/license-list-data/v3.27.0/text/CC0-1.0.txt' (Join-Path $projectRoot 'third_party/notices/CC0-1.0.txt')
# Noto Sans is redistributed under SIL OFL 1.1; the source font remains in the repository.
Get-PinnedFile 'https://raw.githubusercontent.com/notofonts/noto-fonts/v20201206-phase3/hinted/ttf/NotoSans/NotoSans-Regular.ttf' (Join-Path $projectRoot 'assets/fonts/NotoSans-Regular.ttf')
Get-PinnedFile 'https://raw.githubusercontent.com/notofonts/noto-fonts/v20201206-phase3/LICENSE' (Join-Path $projectRoot 'assets/fonts/OFL.txt')
Get-ChildItem -File -Recurse -LiteralPath (Join-Path $projectRoot 'third_party'),(Join-Path $projectRoot 'assets/fonts') | Where-Object { $_.Name -ne 'SHA256SUMS' } | ForEach-Object {
    "$( (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLower())  $($_.FullName.Substring($projectRoot.Length + 1).Replace('\','/'))"
} | Set-Content -Encoding utf8 (Join-Path $projectRoot 'third_party/SHA256SUMS')
Write-Output "Dependencies ready: OpenXR $xrVersion / Vulkan headers 1.3.290 / Noto Sans"
