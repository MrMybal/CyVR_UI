// SPDX-License-Identifier: MIT
#pragma once
#include "cyvr/core.hpp"
#include <memory>
struct android_app;
namespace cyvr {
class AndroidRenderExtension;
class VulkanRenderer;
struct AndroidAppConfig {
    std::string name{"CyVR UI"};
    uint32_t version{7};
    Presentation presentation{Presentation::Panel2D};
    bool immersiveEnabled{false};
    std::function<void(UiModel &)> configure;
    // Optional host integration for video, custom GPU content and activity cleanup.
    std::shared_ptr<AndroidRenderExtension> graphics;
    bool nativeResolution{false};
    bool viewFallback{false};
    // Runs on the native host thread. prepareRenderer is currently panel-only.
    std::function<void(VulkanRenderer &, bool recreated)> prepareRenderer;
    std::function<void(bool resumed)> activityState;
    std::function<void()> releaseApplication;
};
// The callback and everything it captures must outlive this blocking call.
void runAndroidApp(android_app *app, const AndroidAppConfig &config);
void runPanelAndroidApp(android_app *app, const AndroidAppConfig &config);
void runImmersiveAndroidApp(android_app *app, const AndroidAppConfig &config);
int androidInitialPage(android_app *app);
// immersiveFailed asks the restored panel to tell the user why it came back.
void androidSwitchPresentation(android_app *app, Presentation presentation, int page,
                               bool immersiveFailed = false);
} // namespace cyvr
