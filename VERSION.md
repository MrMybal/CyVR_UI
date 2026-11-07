# CyVR UI — version notes

[Project overview](README.md) · [Français](VERSION.fr.md)

CyVR UI is **in development** and distributed as experimental, development-signed test builds. Source and downloadable packages currently have different versions.

| Component | Version | Availability |
| --- | --- | --- |
| Reference application, shared libraries and Windows tools | 0.9.1 | Current source; build with the repository scripts. The editor has no separate published Windows package. |
| Published Quest reference app | 0.8.0, release tag `v0.8.0-test.1` | [Hybrid and strictly 2D ARM64 APKs](https://github.com/MrMybal/CyVR_UI/releases/tag/v0.8.0-test.1), with checksums. |

## Current source: 0.9.1

- Native Windows 2D interface editor: reference-screen capture, property editing, faithful/schematic views, undo/redo, versioned JSON and JSON/Markdown/PNG export.
- Optional `cyvr_layout` library for loading and drawing edited documents while preserving action IDs and hit regions.
- A fixed immersive window bar with side icons on hover. Full-surface controller/hand grips move the managed window; enabled edges/corners resize it while preserving the canvas aspect ratio.
- Viewer-facing movement and thumbstick distance/yaw controls. Compact application content retains its grip input; an explicitly exposed trigger drag handle can still move its window.
- Resolved application controller input, Android scrolling and extended host/rendering integration hooks.

These additions are newer than the downloadable 0.8.0-test.1 APK. See the [editor guide](docs/INTERFACE_EDITOR.md), [window controls](docs/WINDOW_INTERACTIONS.md) and [Android host contracts](docs/ANDROID_HOSTS.md).

## Published test build: 0.8.0-test.1

The published APK opens in a native Android 2D window and offers optional OpenXR immersion. It includes refreshed controls, themes, English/French localization, three original PBR scenes, two CC0 photographic backgrounds, optional headset passthrough and hand/panel depth composition where supported.

- [Hybrid APK](https://github.com/MrMybal/CyVR_UI/releases/download/v0.8.0-test.1/CyVR_UI-0.8.0-test.1-arm64.apk): native 2D with optional immersive VR/AR.
- [Strictly 2D APK](https://github.com/MrMybal/CyVR_UI/releases/download/v0.8.0-test.1/CyVR_UI-0.8.0-test.1-panel-arm64.apk): excludes the OpenXR loader and immersive environment assets.
- [Checksums](https://github.com/MrMybal/CyVR_UI/releases/download/v0.8.0-test.1/SHA256SUMS.txt) · [Release notes](https://github.com/MrMybal/CyVR_UI/releases/tag/v0.8.0-test.1).

The APKs share package ID `com.cyvr.ui` and are installed as alternatives. Hybrid transitions require Horizon OS v69 or later.

## Documented limitations

- Quest 2, Quest 3S and Quest Pro are declared targets; full device validation remains pending. Quest 3 observations do not establish compatibility across all models.
- Sustained performance, thermal behavior, passthrough lifecycle, compositor hand occlusion and newer grab gestures still need extended in-headset validation.
- The 1440 × 990 logical canvas scales with preserved aspect ratio and letterboxing; it does not automatically reflow at narrow sizes.
- Window blur samples the application's virtual environment. It does not blur other apps or headset camera imagery. Photographic panoramas are monoscopic and have no translational scene parallax.
- Edited layouts require application integration; the stock Quest reference does not automatically load editor JSON.
- Manual pitch/roll rotation, two-hand resizing, curved panels, editable text fields, a virtual keyboard, eye tracking, complex text shaping and right-to-left layout are not implemented. GPU execution time is not instrumented.

## Reported issues

The [0.8.0 release notes](https://github.com/MrMybal/CyVR_UI/releases/tag/v0.8.0-test.1) do not provide a bug-by-bug list. Consult the [issue tracker](https://github.com/MrMybal/CyVR_UI/issues) and include the build, headset model and steps to reproduce when reporting a problem. The limitations above are not a list of confirmed bug tickets.
