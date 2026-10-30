// SPDX-License-Identifier: MIT
#include "cyvr/android.hpp"
#include <android/log.h>
#include <android_native_app_glue.h>
#include <exception>
extern "C" void android_main(android_app *app) {
    JNIEnv *environment = nullptr;
    app->activity->vm->AttachCurrentThread(&environment, nullptr);
    try {
        cyvr::AndroidAppConfig config;
        config.immersiveEnabled = CYVR_ENABLE_IMMERSIVE;
        config.viewFallback = true;
        cyvr::runAndroidApp(app, config);
    } catch (const std::exception &error) {
        __android_log_print(ANDROID_LOG_ERROR, "CyVR", "Fatal: %s", error.what());
    }
    app->userData = nullptr;
    app->onAppCmd = nullptr;
    app->onInputEvent = nullptr;
    ANativeActivity_finish(app->activity);
    // NativeActivity waits for native-glue acknowledgements during pause/stop/destroy.
    // Continue servicing the old activity after a 2D/immersive transition request.
    while (!app->destroyRequested) {
        android_poll_source *source = nullptr;
        int events{};
        if (ALooper_pollOnce(100, nullptr, &events, reinterpret_cast<void **>(&source)) >= 0 && source)
            source->process(app, source);
    }
    app->activity->vm->DetachCurrentThread();
}
