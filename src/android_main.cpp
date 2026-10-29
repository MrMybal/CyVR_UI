// SPDX-License-Identifier: MIT
#include <android/log.h>
#include <android_native_app_glue.h>
#include <exception>
void runCyVR(android_app *app);
extern "C" void android_main(android_app *app) {
    JNIEnv *environment = nullptr;
    app->activity->vm->AttachCurrentThread(&environment, nullptr);
    try {
        runCyVR(app);
    } catch (const std::exception &error) {
        __android_log_print(ANDROID_LOG_ERROR, "CyVR", "Fatal: %s", error.what());
    }
    app->activity->vm->DetachCurrentThread();
    ANativeActivity_finish(app->activity);
}
