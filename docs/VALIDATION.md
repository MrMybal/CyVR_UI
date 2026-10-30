# Validation

## Build and automated checks

- Windows x64: Clang 19.1.5, native Vulkan rendering and the portable C++ test suite.
- Android ARM64: NDK 27.2.12479018, minimum SDK 32 and target SDK 34.
- APK signature verification and 16 KiB native-library alignment.
- SPIR-V validation for Vulkan 1.1.
- Frame-local quad/cylinder depth-policy wrapping, extension-chain preservation and matching projection near/far planes.
- Optional Windows Vulkan regression test: a hand proxy in front of a panel remains visible, a proxy behind it is covered, independently of draw order.
- Optional GPU test: missing panoramas retain the fallback, both bundled JPEGs decode/upload, and environment rendering survives application image-atlas replacement.
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

## Immersive rendering changes (0.5.0)

Windows and Android ARM64 builds pass, including shader validation, APK signature/alignment and the core test suite. The optional Windows GPU regression test passes. Native Vulkan previews exercise both bundled 4K photographic backgrounds. These checks validate the renderer and layer descriptions; they do not establish that a particular headset compositor has presented the expected occlusion.

The 0.5.0 hybrid APK installs successfully on the connected Quest 3 and starts its native panel activity. The headset was sleeping during automated observation, so this does not confirm presented pixels. The strictly 2D build also passes; its APK contains neither the OpenXR loader nor the photographic assets, and its ELF has no OpenXR dependency.

The new cross-layer depth path requires both OpenXR depth extensions and D32 swapchain support. Visual front/behind ordering in the actual Quest compositor, passthrough ordering, entry/return lifecycle and sustained performance at the 1440-pixel eye cap require in-headset verification.

## Modeled environments (0.6.0)

The Windows build, hybrid ARM64 APK and strictly 2D APK compile. Core and GPU tests cover all three modeled scenes, geometry/normal budgets, persisted environment IDs, six immersive choices, removal of those choices in the panel host, floor placement and off-axis glass projection. GPU rendering confirms translational parallax, distinct scene images and camera/room translation invariance. Scene-backed frosted glass is exercised. Native previews were inspected from the front, side and rear, with English and French settings.

The hybrid APK installs on Quest 3. The tester confirmed that both the interface and the modeled environment are visible. At that point, device logs reported valid tracked head poses (flags 15), anchoring to the tracked floor and submitted projection/UI layers in a focused session.

An earlier launch attempt produced a black screen: the runtime reported a focused session but no valid head pose, and MemoryBrokerClient refused tracking access. Briefly putting the headset to sleep and waking it restored tracking and the visible scene. This is an observed recovery on this Quest 3, not a permanent fix or proof of the root cause. No system permissions or security settings were changed. A VIEW-relative UI fallback is available while waiting for valid poses, but cannot restore tracking access itself.

Floor-height accuracy, compositor hand occlusion, all three scene choices and sustained frame rate still need an in-headset evaluation. An empty or UI-only frame cadence is not a scene-performance measurement.

## PBR materials and lighting (0.7.x)

The metallic/roughness GGX path builds on Windows and Android ARM64. GPU tests independently flatten normal maps, change roughness/metallic and remove the HDR probe to confirm that each changes actual rendered pixels. Exposure, scene-backed glass, geometry budgets, anchored parallax and small missing-asset fallbacks are exercised. All ten SPIR-V shaders validate for Vulkan 1.1. The hybrid APK contains the original resources; the strictly 2D APK contains neither PBR assets nor OpenXR. Material arrays, one active probe and the integration table use 2,637,784 bytes of texture payload.

Version 0.7.0 installed and ran on Quest 3 with valid tracked head poses and a focused immersive session. The tester confirmed fluid head motion but rejected the coarse lighting bake as visually poor. That feedback is not considered a successful visual-quality validation.

Version 0.7.1 replaces box-occluder/point-light baking with an offline triangle/BVH bake, area-light integration and coplanar noise filtering. Native previews of the three scenes were inspected before headset delivery. The finished lighting is validated and loaded into the existing vertex buffer, with no per-frame ray tracing. Small shadow detail is still limited by vertex density. The 0.7.1 APK installs on Quest 3; device logs confirm that the offline triangle bake is loaded, the immersive session is focused, head tracking is valid and both hands are tracked. Final in-headset quality and sustained thermal/performance measurements remain separate checks.

## Optional immersive AR (0.8.0)

Core tests exercise AR activation through the shared API and camera control, repeated activation, restoration of the previous scene, saved-mode round trips and legacy preference migration. Unavailable capability and the native 2D host reject AR activation. Virtual blur and brightness controls are disabled in AR; a previously captured slider cannot continue changing them after a mode switch.

The Vulkan regression renders an AR frame after a modeled scene and checks that background pixels retain zero alpha while hand proxies remain opaque. Panel rendering retains the requested alpha and does not sample the cached virtual glass backdrop. These tests validate app-side transparency, not the headset camera/compositor output. English and French appearance previews were inspected as UI fixtures; desktop snapshots contain no headset camera imagery.

Windows core/GPU tests and Android hybrid/strictly 2D builds pass, including APK signature/alignment and Vulkan 1.1 SPIR-V validation. The strictly 2D APK contains no OpenXR loader or immersive environment assets. The 0.8.0 hybrid APK installs on the connected Quest 3 and its panel launch is accepted. In-headset AR visibility, repeated toggling and sleep/resume still need tester confirmation. Passthrough error/reinitialization handling is implemented; runtime fault cases still require device testing.

## Remaining device validation

- Quest 2, Quest 3S and Quest Pro readability, input and performance.
- Sustained sessions, thermal behavior, battery use and compositor missed frames.
- All hand/controller modes, pinch interactions and window-state transitions in extended use.
- View-follow comfort, live language switching, themes, translucent materials and passthrough appearance.
- GPU timestamps; the current HUD does not report GPU execution time.
