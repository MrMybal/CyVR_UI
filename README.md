# CyVR UI

A native, open-source **2D interface foundation** for applications on **Meta Quest 2, Quest 3S, Quest 3 and Quest Pro**, with an optional immersive host. Built with **C++17 and Vulkan**, plus **OpenXR only for immersion**, with a custom UI renderer and no game engine or web runtime.

Version **0.8.0** starts in a resizable native Android 2D window managed by Horizon OS. This lets ordinary apps share the Quest workspace. Apps that need a full 3D experience can opt into the immersive activity; both hosts reuse the same UI components, themes, translations and preferences. A 2D-only build excludes OpenXR entirely. Hybrid transitions require Horizon OS v69 or later.

The project includes reusable C++ libraries, a standalone Quest reference app and a Windows inspector. The previous immersive implementation has been exercised on Quest 3; see [Validation](docs/VALIDATION.md) for the status of the new 2D host and other devices.

![Shared interface shown in the Windows immersive-layout inspector](docs/images/interface.png)

The image shows the shared catalogue in the Windows inspector. Native 2D mode uses the Quest system window controls instead of the floating immersive bar.

## Features

- A component catalogue: buttons, toggles, sliders with pointer capture, segmented controls, badges, toasts and modal dialogs.
- Native 2D windows with system pointer/touch input and Quest window management; no OpenXR session in the panel host.
- Optional immersive mode, selected explicitly through a headset icon; its overlapping-window icon returns to 2D.
- In immersive mode, a floating icon bar for moving, minimizing, closing, restoring and changing window placement. Action names appear on hover.
- In immersive mode, world anchoring, viewer-facing orientation and smooth view following. Following pauses during interaction.
- Graphite, Light and Ocean themes; surface opacity; frosted glass over the native environment; 4K Sunlit Lounge and Venice Sunset photographic backgrounds.
- Three original, fully modeled 3D environments: Modern Lounge, Observatory and Sunset Terrace, with stereo depth and translational parallax.
- Optional immersive headset passthrough. Its camera image is not exposed to the application or blurred by the UI renderer.
- In immersive mode, smooth tracked-hand meshes when available; an original Studio glove for controllers; skeleton and hidden-hand modes; optional laser pointers.
- An independent diagnostics HUD, local preferences and JSON export.
- English by default, French included, and UTF-8 translation catalogues separated from rendering code.

## Build and run

The supplied scripts run on Windows. Install Python 3, LLVM/Clang 19 or newer with the Windows C++ SDK, an Android SDK, and a JDK. The build uses these pinned Android components:

| Component | Version |
|---|---|
| Android NDK | 27.2.12479018 |
| CMake | 3.22.1, with Ninja |
| Android Build Tools | 35.0.1 |
| Compile / target SDK | 34 |
| Minimum Android SDK | 32 |

Set `ANDROID_HOME` and `JAVA_HOME` when using installations outside the default locations. The Windows build detects the Clang bundled with Visual Studio before the standalone LLVM installation. Quest builds use the NDK compiler.

```powershell
.\build.ps1                 # Windows inspector, C++ tests and Quest APK
.\build.ps1 -Target windows # Windows inspector and tests
.\build.ps1 -Target android # ARM64 Quest APK: 2D by default, optional immersion
.\build.ps1 -Target android -NoImmersive # Native 2D APK, no OpenXR library
.\build.ps1 -Target tests   # Portable core tests
```

The first build downloads the official Khronos OpenXR loader AAR and verifies its pinned SHA-256. Headers, fonts, generated translation data and model assets are included in the repository. No proprietary Meta SDK is embedded. APK packaging uses CMake/NDK, javac, d8, aapt2, zipalign and apksigner; Gradle is not required. A small Java NativeActivity bridge handles activity transitions; the UI and rendering remain C++. The 2D-only build does not download or package the OpenXR loader.

- **Quest:** install `dist/android/CyVR_UI-debug.apk`, then open **CyVR UI** under **Unknown Sources**. It opens in a **2D window**. The 2D-only APK is `dist/android/CyVR_UI-panel-debug.apk` (same package, alternative build).
- **Windows:** run `dist/windows/CyVR_UI.exe` with its `assets` folder alongside it. A Vulkan graphics driver is required.

For a connected development headset:

```powershell
.\tools\install-quest.ps1
# With multiple devices connected:
.\tools\install-quest.ps1 -Serial DEVICE_SERIAL
```

The APK uses a local development signing key. A production distribution needs its own signing and release configuration. Build output is kept in `build/` and `dist/`; generated application packages are not committed to the source repository.

## Controls

**Native 2D:** use the Quest system pointer, hand pinch or touch to activate controls. Drag sliders through the same shared input-capture code. Move, resize, minimize and close the window with Horizon OS controls. Settings show appearance and accessibility controls; hand meshes, 3D distance and world anchoring belong to the immersive host.

**Optional immersion:** select the headset icon in the header. The app closes its 2D activity and starts OpenXR. Select the overlapping-window icon to return to the Home workspace as a 2D panel. The current page and saved appearance/language preferences carry across. Entering immersion takes the active immersive slot; ordinary 2D windows remain managed by Horizon OS.

| Immersive action | Controllers | Tracked hands |
|---|---|---|
| Point | Aim a controller; the pointer and reticle indicate the target | Aim the hand pointer |
| Select | Press and release the trigger | Pinch and release thumb and index |
| Move a window | Hold the four-arrow handle in the floating bar or title area | Hold the same handle with a pinch |
| Minimize / restore | Use the minus / overlapping-window icon | Select the same icon |
| Close / reopen | Use the cross / CyVR grid icon | Select the same icon |
| Placement mode | Cycle the pin, eye and headset icon, or use Settings | Select the same controls |
| AR / virtual scene | Camera icon in the header, or Settings → Appearance → AR / Your room | Select the same controls |
| Recenter | UI button or right stick click | UI button |
| Diagnostics HUD | UI button or left X button | UI button |

The floating bar remains available while the content is minimized or closed. Minimizing preserves the page; closing returns to Home when reopened. Moving a followed window switches it back to a world anchor. On release, the window faces the viewer without copying head roll. Reduced motion slows automatic following.

The Windows inspector uses the mouse. The move handle drags the native window; keys **1–4** select pages, **D** toggles the HUD, **R** recenters the preview, and **Escape** dismisses a modal.

## Optional AR view

In the immersive host, select the **camera / AR icon** in the header or **Settings → Appearance → AR / Your room** to see your real surroundings behind the interface. Select the camera icon again to return to the exact virtual environment you were using. The selected mode and previous virtual environment are saved. A new installation keeps the standard virtual background; the app still launches as a native **2D window**.

The real-world view uses the runtime's `XR_FB_passthrough` reconstruction layer, composed below the hands, application content and panels. No virtual sky or room is drawn over it. The camera icon indicates the requested mode, with a green dot once the passthrough feature and layer have started. Unsupported runtimes disable the AR option. A start failure or service error restores the remembered virtual background and displays a message. The feature pauses when the activity/session pauses and restarts when a selected AR session resumes; returning to the native 2D host releases its XR resources.

Panel opacity remains adjustable. The virtual-background blur and brightness controls are disabled in AR, retaining their values for a later return to VR. Passthrough supplies a transparent panel tint rather than a blur of the camera image. Image quality and color capabilities are supplied by the headset/runtime. This display path uses the [OpenXR passthrough compositor API](https://developers.meta.com/vr/documentation/native/android/mobile-passthrough/); the application does not receive camera frames through this feature and requests no camera-access permission.

Applications reusing the shared UI can call `UiModel::setAugmentedReality(bool)`. It returns false outside the immersive host or when enabling an unavailable passthrough service. `passthroughAvailable` reports capability/resource readiness; `passthroughActive` reports the started composition state. Both are included in exported diagnostics. `settings.virtualBackground` remembers the non-AR scene; existing numeric background IDs and older preference files remain supported.

## Languages

The sidebar language button cycles through the available catalogues. A new installation uses **English**; the selected language is saved locally. Existing preferences without a language field also default to English. Resetting comfort settings keeps the current language.

- Source messages: [`assets/locales/en.json`](assets/locales/en.json).
- French translation: [`assets/locales/fr.json`](assets/locales/fr.json).
- Runtime API: [`include/cyvr/locale.hpp`](include/cyvr/locale.hpp).
- Generated table: `include/cyvr/locale_data.hpp`.

Use stable keys such as `nav.home` with `UiModel::tr()` or `cyvr::translate()`. `formatMessage()` accepts named placeholders, and `formatNumber()` formats numbers for the selected locale. Unsupported locales fall back to English; regional locales first resolve to their base language. Missing translated messages fall back to the English source message.

To add a language, copy the English JSON catalogue to `<language-code>.json`, set `_name` to its native name and translate its values without changing keys or placeholder names. Then run:

```powershell
python tools/generate_locales.py
python tools/generate_locales.py --check
```

The build script regenerates the table automatically; the language selector discovers all compiled catalogues. The runtime needs no JSON parser or localization dependency. The generator checks message keys, named placeholders and font coverage. The current atlas covers Western European Latin text. Other writing systems require an appropriate font atlas; right-to-left layout and complex text shaping are not implemented.

## Reuse the foundation

`cyvr_ui` provides drawing primitives, geometry, input capture, settings, translations and statistics. `cyvr_gpu` provides the Vulkan renderer. The reference pages live in `src/ui.cpp`; interaction logic is in `src/core.cpp`.

Applications can replace the reference pages with `UiModel::renderApplication` and `handleApplicationAction`, and register controls through `applicationWidget()`. On Android, `AndroidAppConfig` and `runAndroidApp()` allow an application to configure its own name and UI while reusing the native 2D host by default. Link `cyvr_android`; set `AndroidAppConfig::immersiveEnabled` only for apps that expose immersion, and build with `CYVR_ENABLE_IMMERSIVE=OFF` for strictly 2D apps. Keep business state above activity lifetimes. See [Android hosts and migration](docs/ANDROID_HOSTS.md) and [Architecture](docs/ARCHITECTURE.md).

Hand models can be supplied through `HandSurface` or `HandSkin`. The included controller glove is generated by `tools/bake_hands.py` and is covered by the project MIT license. Importing an alternative model currently requires changing the code or assets; the headset app does not contain a model-file picker.

## Rendering and diagnostics

The 2D host presents directly to an Android Vulkan swapchain, redraws on UI changes, fits the logical canvas to resized windows and maps system coordinates back into the shared input model. It stops GPU work while paused and does no CPU image readback. Idle UI does not submit repeated frames; UI timers are serviced at up to 30 Hz. Panel diagnostics leave display cadence unmeasured rather than treating idle redraw intervals as headset FPS.

The optional immersive host draws UI text and controls into cached OpenXR quad layers. Frosted glass samples the same local environment through a 480 × 330 intermediate target. The backdrop viewpoint is updated at up to 30 Hz while it moves, in addition to content updates. Text remains sharp. Hands and the environment use per-eye rendering with depth, capped at 1440 pixels on the longest side in the reference app. The Quest path does not read images back to the CPU.

On runtimes with `XR_KHR_composition_layer_depth`, `XR_FB_composition_layer_depth_test` and a D32 swapchain format, stereo depth is submitted to the compositor. Quad and cylinder panels test against that depth, so a hand in front can cover a panel while a hand behind remains behind it. Application-provided depth policies are preserved. Runtimes lacking this combination retain the older layer ordering and log that cross-layer occlusion is unavailable.

The two bundled CC0 panoramas are loaded on first selection in immersion, using about 64 MiB of GPU texture memory. They are monoscopic 360-degree photographs: looking around changes the view, while moving through the room does not create scene parallax. Panels and hands remain stereo 3D. See [environment sources and credits](assets/scenes/README.md). Reusable applications can opt into asset copying with `CYVR_INCLUDE_SCENES=ON`; applications without the images retain the lightweight procedural fallback.

The HUD reports application cadence, P95 frame intervals, work time excluding `xrWaitFrame`, display refresh rate and RSS memory. **GPU time is not instrumented.** Work time includes explicit driver waits and is not a CPU-utilization metric. Requested 72/90/120 Hz modes are offered only when reported by the runtime; they are not performance guarantees.

Diagnostics are exported locally to `/sdcard/Android/data/com.cyvr.ui/files/diagnostic.json` on Quest and alongside the executable on Windows. A startup report is written after 240 visible frames. Screenshot mode uses fixed demonstration metrics. See [Validation](docs/VALIDATION.md).

## Original 3D environments

In immersion, open **Settings → Appearance** and choose **3D / Modern lounge**, **3D / Observatory** or **3D / Sunset terrace**. The two **360** choices retain photographic backgrounds; **Passthrough** shows the runtime-provided environment. Choices are translated and saved without changing the numeric values of existing preferences.

The 3D scenes use original MIT geometry generated by `src/scene.cpp`: chamfered furniture, architecture, plants, distant landscapes and a geometric planet with rings. Objects have actual metre-scale positions and stereo depth. Physical head translation produces parallax; no artificial locomotion or collision physics is implemented. Materials use a metallic/roughness PBR shader with GGX microfacets, Fresnel reflection, normal maps, prefiltered HDR environment reflections and a preintegrated BRDF table. Original seamless textures distinguish wood, woven fabric, brushed metal, ceramic, foliage and water. The lighting calculation runs in linear space with sRGB base colors and a luminance-preserving tone mapper. No external scene models or textures are required.

| Scene | Triangles | Static vertex memory |
|---|---:|---:|
| Modern Lounge | 20,648 | 4.73 MiB |
| Observatory | 9,480 | 2.17 MiB |
| Sunset Terrace | 19,012 | 4.35 MiB |

Diffuse environment light, color transfer, fixture lighting, ambient occlusion and soft sun visibility are baked **offline** by `tools/bake_scene_lighting.cpp`. The original baker traces the actual scene triangles through a BVH: 512 paths per unique surface sample, up to two diffuse surface interactions, 2,048 area-light samples and 64 sun-disk samples. A coplanar filter reduces remaining Monte Carlo noise. This replaces the earlier coarse box/point-light bake, which produced visible blotches. The app validates and loads the finished vertex lighting when selecting a scene; it performs no ray tracing on the headset. Missing optional lighting uses a neutral fallback. HDR reflection probes are static approximations of each scene and do not reflect moving hands or application windows.

The two 128 × 128 material arrays, 64 × 64 BRDF table and one active 128-pixel HDR cube probe occupy about **2.52 MiB** of texture payload. Mip chains and specular anti-aliasing reduce distant texture shimmer. PBR textures are loaded only when selecting a 3D scene, independently of the optional photographic panoramas. A strictly 2D build excludes these texture files. See [`assets/pbr/README.md`](assets/pbr/README.md) for channels, generation, licensing and reusable-app integration.

Only the active scene is retained as a dedicated GPU vertex buffer, rendered with two geometry draws per eye (closed surfaces with back-face culling, then open surfaces and lights) in addition to the shared sky and hand passes. The 2D host neither builds these meshes nor allocates their backdrop/depth targets. The floor anchor uses `LOCAL_FLOOR` when the optional extension is available, then `STAGE`; otherwise it estimates the floor 1.65 m below the initial head pose and logs that fallback. Room placement stays fixed during physical movement and panel dragging. Known reference-space origin changes rebase the room anchor at the specified change time.

Frosted glass renders the actual scene through the panel's off-axis frustum into a 480 × 330 depth target, then applies a small Gaussian filter. Only the scene is blurred; UI text stays sharp and hand depth is handled by the compositor. The cached glass view follows the existing update limit of 30 Hz during movement. These budgets do not establish a sustained headset frame rate; see [Validation](docs/VALIDATION.md).

## Current limits

The shared layout currently fits a 1440 × 990 logical canvas while preserving aspect ratio; it letterboxes rather than reflowing at narrow window sizes. In 2D, opacity and blur affect only the app-owned backdrop, not other apps or the system environment.

Immersive windows are flat. Cross-layer hand occlusion requires the depth-composition capabilities described above and still needs visual verification on each headset/runtime. Blur covers the native environment, not hands or passthrough. Free window rotation, two-hand resizing, curved panels, editable text fields, a virtual keyboard and eye tracking are not implemented.

## Asset tools

Generated assets are included, so these tools are optional:

```powershell
python -m pip install -r tools/requirements-font.txt
python tools/bake_font.py
python -m pip install -r tools/requirements-hands.txt
python tools/bake_hands.py
# Optional environment regeneration (requires Pillow):
python tools/prepare_scenes.py
# Optional PBR material / HDR probe regeneration (requires NumPy):
python tools/prepare_pbr.py
python tools/prepare_pbr.py --check
# Rebuild offline triangle lighting after editing scene geometry or lights:
.\tools\bake-lighting.ps1
```

`python tools/package.py` creates local Windows and source archives after a build.

## License

Original CyVR code, UI control icons, 3D environment geometry, PBR textures/lighting probes and glove geometry are licensed under [MIT](LICENSE). The application may be used in open-source and commercial projects under the applicable licenses. The logo artwork in `assets/branding/` is excluded from this MIT grant and is not included in the repository or distribution archives. Third-party components retain their own terms; see [THIRD_PARTY.md](THIRD_PARTY.md). The Quest operating system, system OpenXR runtime and graphics drivers are provided by the device platform.
