// SPDX-License-Identifier: MIT
#pragma once
#include "cyvr/core.hpp"
struct android_app;
namespace cyvr {
struct AndroidAppConfig {
    std::string name{"CyVR UI"};
    uint32_t version{5};
    Presentation presentation{Presentation::Panel2D};
    bool immersiveEnabled{false};
    std::function<void(UiModel &)> configure;
};
// The callback and everything it captures must outlive this blocking call.
void runAndroidApp(android_app *app, const AndroidAppConfig &config);
void runPanelAndroidApp(android_app *app, const AndroidAppConfig &config);
void runImmersiveAndroidApp(android_app *app, const AndroidAppConfig &config);
int androidInitialPage(android_app *app);
void androidSwitchPresentation(android_app *app, Presentation presentation, int page);
} // namespace cyvr
