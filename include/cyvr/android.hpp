// SPDX-License-Identifier: MIT
#pragma once
#include "cyvr/core.hpp"
struct android_app;
namespace cyvr {
struct AndroidAppConfig {
    std::string name{"CyVR UI"};
    uint32_t version{4};
    std::function<void(UiModel &)> configure;
};
// The callback and everything it captures must outlive this blocking call.
void runAndroidApp(android_app *app, const AndroidAppConfig &config);
}
