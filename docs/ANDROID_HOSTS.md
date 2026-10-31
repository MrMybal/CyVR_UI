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

### Application integration hooks

`AndroidXrRenderExtension::beginXrFrame(XrTime, bool focused, bool shouldRender)` runs once after input updates and before scene/layer composition. Applications with an external immersive producer can publish predicted poses and input there. They must preserve the validity flags returned by OpenXR and must not infer tracked poses from session focus alone.

`UiModel::applicationPointer(size_t index, Vec2 canvasPosition, bool pressed)` optionally consumes pointer events for embedded application content. It runs only while the interface is visible, no modal is open and no shared widget owns capture. A consuming application owns the continuation/cancellation of its gesture. Native UI controls and modal isolation remain the foundation's responsibility.

`cyvr/icons.hpp` exposes `Icon`, `drawIcon`, `drawLogo` and `themeColors` for application chrome. Icons use the catalogue's shared 24-unit vector grid and scale with the same UI geometry; applications do not need text characters as icon substitutes.

The immersive `AndroidXrRenderExtension::processEye(AndroidXrEyeFrame)` callback is optional and defaults to a no-op. It runs once for each successfully located eye after its scene/hand render completes and before the color/depth swapchain images are released. The frame exposes the acquired single-layer 2D color `Target`, its Vulkan format and dimensions, the eye index, predicted display time and whether the requested background is passthrough. Color supports sampling and color attachment use, and enters in `COLOR_ATTACHMENT_OPTIMAL`. An extension that processes it must finish its GPU work, restore that color layout, preserve the depth image and retain no ownership of the target. The target's views remain valid until graphics shutdown; release extension-owned framebuffers before host targets. UI quads, dock and pointer layers are separate and are not passed to this callback.

The panel host offers optional callbacks on its native thread. `AndroidAppConfig::activityState(bool resumed)` runs on resume/pause; use it to refresh application data without resetting navigation and to commit durable state. `prepareRenderer(VulkanRenderer&, bool recreated)` runs before drawing. Retain source image data in the application and upload it when `recreated` is true, because resizing can replace the renderer while the application model remains alive.

`releaseApplication()` runs while the host's `UiModel` is still alive, including cleanup after a failed panel `configure` callback. Use it to destroy controllers whose destructors detach UI callbacks. The configuration object and its captures must outlive the blocking `runAndroidApp()` call. `UiModel::applicationBack` can consume Android Back for application navigation before the host handles it. Both hosts invoke `activityState` and `releaseApplication`; `prepareRenderer` currently applies to the panel host.

Applications must keep servicing native-glue commands until `destroyRequested` after requesting activity finish, even when `runAndroidApp()` returns earlier for a presentation transition. Java's NativeActivity pause/stop/destroy callbacks synchronously wait for those acknowledgements. Returning immediately from `android_main` can leave a callback waiting and cause an ANR. CyVR Player's entry point implements this final event drain.

### GPU application extensions and native quality

`UiModel::handleApplicationSlider` lets applications use the shared captured slider input with `SliderPhase::Begin`, `Preview`, `Commit` and `Cancel`. Expensive operations such as media seeking can update a preview during a drag and submit one command on release. Releasing outside the track clamps the selection; focus cancellation, navigation, language changes and hidden-interface transitions cancel it. Applications should only start a new selection on `Begin`, so a cancelled held pointer cannot resume an old operation.

For application video-only/cinema viewing, `UiModel::setInterfaceHidden(true)` suppresses immersive chrome (main panel, dock, HUD and cursors), hand/controller geometry and rays while retaining application composition layers. `revealInterface()` returns true when a waking input was consumed. Pointer/trigger activation consumes the whole first gesture, and Touch A restores the UI without requiring an invisible panel hit. Applications should also route Android Back/keyboard commands through their reveal action before normal navigation. In 2D, hidden-interface content spans the native surface rather than the regular fitted UI canvas; `viewportSize` exposes that surface's dimensions so the application can preserve image aspect ratio. Applications must draw only their content when this flag is set. The flag defaults to false and is not persisted by the foundation.

The optional `AndroidAppConfig::graphics` uses `AndroidRenderExtension` (`android_render.hpp`) for Vulkan device configuration, resource creation/teardown, due-frame updates and the active poll timeout. `AndroidXrRenderExtension` (`android_xr.hpp`) adds requested XR extensions, the created XR context and bounded application composition layers. These hooks keep application video/scene code outside the shared runtime. `graphicsStopping(false)` marks a panel renderer replacement, followed by a new `graphicsReady`. A resize that keeps the surface format keeps the renderer and its resources, and fires neither callback; `true` releases application GPU resources before device destruction. Both hosts wait for the device to go idle before calling `graphicsStopping(true)` and `releaseApplication()`, and `false` is never sent after `true`. If the immersive host throws, it releases its session and reopens the panel on the page it started from, with a message. Borrowed image views passed to `VulkanRenderer::setImageTexture` remain owned by the application and must outlive their GPU use.

`nativeResolution=true` removes the reference scene's 1440-pixel cap, uses the runtime-recommended eye targets and scales UI layer textures while retaining logical component coordinates. The 2D host already renders at the Android surface extent. `viewFallback=true` permits VIEW-relative panel placement while head tracking is unavailable, then recenters when a valid pose returns. These options do not change source image resolution or promise physical-panel pixel equivalence through the compositor and optics.

Scene content can keep a pose independent of `AndroidXrFrame::panel`. This allows controls to be moved without moving a video or document in the scene. The optional `AndroidXrRenderExtension::referenceSpaceChanging()` callback forwards reference-space change events before their change time, so applications can rebase independent poses when a known origin transform takes effect. Applications own those poses and any explicit scene-content recenter command; panel drag/release should not implicitly recenter scene content.

## Rendering and input

The 2D host owns an Android Vulkan surface, a FIFO swapchain and retained UI geometry. It acquires an image with a fence, waits for acquisition, renders and transitions the image to `PRESENT_SRC_KHR`, then presents on the same graphics queue. Rendering completes before present; multiple frames in flight are not implemented. Surface loss and resize recreate window targets; same-size Android redraw/resume notifications retain existing resources unless the swapchain is out of date or suboptimal. Pause stops rendering and cancels active pointer capture.

Applications with multiple system windows can opt into `AndroidAppConfig::renderWhileVisible`. With this option, a STARTED, visible panel keeps rendering while another window holds focus; STOPPED panels stop rendering. The default remains false. Input capture still cancels on pause or focus loss. Applications must keep their model, JNI references, Vulkan resources and embedded surfaces independent for each activity. Vulkan loader initialization is serialized, and shared preference writes use a process mutex around the atomic replacement.

Window dimensions map to a centered 1440 × 990 logical canvas with matching inverse input mapping. This preserves the same component layout across hosts; narrow windows currently letterbox rather than reflow. Android motion events are delivered by the Quest system, so no application hand meshes or hand-tracking permissions are needed in strictly 2D apps. The system owns window movement, resizing, minimizing and closing.

In a 2D window, themes and frosted materials apply to the app's procedural backdrop. The renderer cannot blur other applications or the passthrough image. The HUD does not infer headset FPS from a window that redraws only when content changes.

Hybrid activity categories, launch overrides and the Home pending-intent transition follow the [Meta hybrid apps documentation](https://developers.meta.com/vr/documentation/spatial-sdk/hybrid-apps-overview/). The manifest declares Horizon OS v69 as the minimum for these transitions; it embeds no Meta SDK.
