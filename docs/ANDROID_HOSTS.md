# Android hosts

The primary application surface is a native Android **2D window**. Immersion is an optional capability of an app, not a requirement of the UI library. A quad displayed by an OpenXR session is still immersive and does not provide system 2D multitasking.

```text
Application model, actions, translations, preferences
                         |
             cyvr_ui / UiModel / DrawList
                         |
                cyvr_gpu / Vulkan
                  /              \
     PanelActivity                 ImmersiveActivity (optional)
     Android surface               OpenXR layers and stereo scene
     System pointer/touch          Controller rays / tracked hands
     Quest window management       App-owned spatial placement
```

Both hosts call the same UI callbacks. There is no separate Android widget implementation or web view. The small Java bridge selects activities and passes the current page; rendering remains in C++.

## Build choices

`build.ps1 -Target android` produces an APK with a 2D launcher and an immersive activity available through the header headset icon. `build.ps1 -Target android -NoImmersive` produces an alternative APK without `xr_app.cpp`, the OpenXR loader, hand-tracking permission or immersive activity. Both use the same package ID and development key, so installing either replaces the other while keeping preferences.

The equivalent CMake switch is `-DCYVR_ENABLE_IMMERSIVE=OFF`. `AndroidAppConfig::immersiveEnabled` defaults to `false`, even in builds capable of immersion. The reference entry point explicitly enables it when compiled in.

## Reuse and migration

Link a native application entry point against `cyvr_android`, rather than compiling `src/xr_app.cpp` directly:

```cpp
cyvr::AndroidAppConfig config;
config.name = "Example";
config.immersiveEnabled = false; // Ordinary utility app: native 2D only.
config.configure = [&](cyvr::UiModel &ui) {
    // Register the same content/action callbacks for either presentation.
    // Keep business state in the application model or persistent storage.
};
cyvr::runAndroidApp(app, config);
```

An app that supports immersion can set `immersiveEnabled = true` and include both activity declarations. `runAndroidApp()` dispatches from the activity's `presentationMode()` method. The default is `Presentation::Panel2D`; a bare NativeActivity without the Java bridge can still run a strictly 2D app. Java methods `initialPage()` and `launchPresentation(int, int)` provide page transfer and optional transitions.

When adapting the sample package, update activity class/package names, native library metadata and the two task affinities together. Keep `PanelActivity` as the launcher in Home and Overlay. Put `com.oculus.intent.category.2D` only on the panel and `com.oculus.intent.category.VR` only on the immersive activity. The immersive activity does not have a launcher category. Keep it unexported unless an app deliberately supports external immersive intents.

## Lifetimes and state

This reference uses the exclusive hybrid model: a transition releases the current renderer/session and finishes its activity. The new activity creates a new `UiModel`, reloads shared preferences and receives the current page. Other transient fields and application callback captures are **not automatically serialized**. Apps must persist documents, unsaved work and navigation deeper than the catalogue page in their own model/storage before switching. A shared component library does not make separate app processes share state.

Returning from immersion destroys the XR session before requesting Home with a pending panel intent. The 2D activity can also be launched as a system overlay while another application owns immersion. It does not request an immersive session. No simultaneous immersive sessions are attempted.

## Rendering and input

The 2D host owns an Android Vulkan surface, a FIFO swapchain and retained UI geometry. It acquires an image with a fence, waits for acquisition, renders and transitions the image to `PRESENT_SRC_KHR`, then presents on the same graphics queue. Rendering completes before present; multiple frames in flight are not implemented. Surface loss and resize recreate window targets; pause stops rendering and cancels active pointer capture.

Window dimensions map to a centered 1440 × 990 logical canvas with matching inverse input mapping. This preserves the same component layout across hosts; narrow windows currently letterbox rather than reflow. Android motion events are delivered by the Quest system, so no application hand meshes or hand-tracking permissions are needed in strictly 2D apps. The system owns window movement, resizing, minimizing and closing.

In a 2D window, themes and frosted materials apply to the app's procedural backdrop. The renderer cannot blur other applications or the passthrough image. The HUD does not infer headset FPS from a window that redraws only when content changes.

Hybrid activity categories, launch overrides and the Home pending-intent transition follow the [Meta hybrid apps documentation](https://developers.meta.com/vr/documentation/spatial-sdk/hybrid-apps-overview/). The manifest declares Horizon OS v69 as the minimum for these transitions; it embeds no Meta SDK.
