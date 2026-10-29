# Validation

## Build and automated checks

- Windows x64: Clang 19.1.5, native Vulkan rendering and the portable C++ test suite.
- Android ARM64: NDK 27.2.12479018, minimum SDK 32 and target SDK 34.
- APK signature verification and 16 KiB native-library alignment.
- SPIR-V validation for Vulkan 1.1.
- Ray intersections, front/back rejection, rotated panel placement and angular size.
- UTF-8 decoding, settings bounds, statistics and diagnostic JSON escaping.
- Pointer capture, simultaneous pointers, modal isolation, slider bounds and window handles.
- Window minimization, closing, restoration and removal of hidden-content hit regions.
- Viewer-facing orientation, damped placement, hand coordinate conventions, mesh bounds and skinning.
- English defaults, locale resolution, French number formatting, named placeholders, language switching and preference persistence.
- Translation-catalogue keys, placeholders, generated-table freshness and font coverage.
- 2D as the default presentation; optional mode requests; removal of spatial-only controls from 2D.
- Canvas fit and inverse input mapping at landscape, narrow and portrait window sizes.
- Both hybrid and strictly 2D Android builds, including Java/dex compilation, APK signatures and alignment.
- The strictly 2D ELF/APK must contain no OpenXR loader dependency or immersive activity.

The Windows inspector exercises the same UI geometry and Vulkan renderer as the Quest app. Public preview screenshots use fixed demonstration values.

## Native 2D validation (0.4.0)

The hybrid APK installs on the connected Quest 3 and launches `PanelActivity` as a standard task with `multi-window` configuration at 1280 × 880 pixels. The device was paused/not visible during the initial automated observation, so that observation alone does not confirm presented pixels, input or transitions. In-headset 2D rendering, resize, pause/resume, 2D-to-immersive-to-2D transitions, and coexistence with another app still require an active headset test.

## Earlier immersive hardware observations

The native application has been installed and rendered on Quest 3. OpenXR instance/session creation, stereo UI presentation, tracked hands, smooth hand meshes, laser pointers and access to the window bar have been exercised. The runtime reported passthrough capability and accepted creation of its objects.

These observations do not validate every feature or every declared device. Desktop screenshots do not replace an in-headset stereo evaluation.

## Remaining device validation

- Quest 2, Quest 3S and Quest Pro readability, input and performance.
- Sustained sessions, thermal behavior, battery use and compositor missed frames.
- All hand/controller modes, pinch interactions and window-state transitions in extended use.
- View-follow comfort, live language switching, themes, translucent materials and passthrough appearance.
- GPU timestamps; the current HUD does not report GPU execution time.
