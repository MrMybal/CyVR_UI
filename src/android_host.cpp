// SPDX-License-Identifier: MIT
#include "cyvr/android.hpp"
#include "cyvr/android_render.hpp"
#include "cyvr/gpu.hpp"
#include "cyvr/storage.hpp"
#include <android/asset_manager.h>
#include <android/log.h>
#include <android_native_app_glue.h>
#include <chrono>
#include <limits>
#include <memory>
#include <unistd.h>

using namespace cyvr;
namespace {
JNIEnv *environment(android_app *app) {
    JNIEnv *env{};
    app->activity->vm->GetEnv(reinterpret_cast<void **>(&env), JNI_VERSION_1_6);
    if (!env)
        throw std::runtime_error("Android host thread is not attached to the VM");
    return env;
}
int activityValue(android_app *app, const char *method, int fallback) {
    auto env = environment(app);
    auto type = env->GetObjectClass(app->activity->clazz);
    auto id = env->GetMethodID(type, method, "()I");
    int result = fallback;
    if (id)
        result = env->CallIntMethod(app->activity->clazz, id);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        result = fallback;
    }
    env->DeleteLocalRef(type);
    return result;
}

class PanelApp {
    android_app *app_;
    const AndroidAppConfig &config_;
    VulkanContext gpu_;
    VkSurfaceKHR surface_{};
    VkSwapchainKHR swapchain_{};
    VkFence acquired_{};
    std::unique_ptr<VulkanRenderer> renderer_;
    std::vector<Target> targets_;
    CanvasMapping mapping_;
    UiModel ui_;
    DrawList draw_;
    bool resumed_{}, visible_{}, dirty_{true}, quit_{}, rendererRecreated_{true}, swapchainInvalid_{}, closing_{};
    int32_t touchId_{-1};
    uint64_t revision_{};
    std::string storage_;
    using Clock = std::chrono::steady_clock;
    Clock::time_point start_{Clock::now()}, nextMemorySample_{};

    static void command(android_app *app, int32_t cmd) {
        auto &self = *static_cast<PanelApp *>(app->userData);
        switch (cmd) {
        case APP_CMD_START:
            self.visible_ = true;
            self.dirty_ = true;
            break;
        case APP_CMD_STOP:
            self.visible_ = false;
            break;
        case APP_CMD_INIT_WINDOW:
            self.dirty_ = true;
            break;
        case APP_CMD_TERM_WINDOW:
            self.releaseSurface();
            break;
        case APP_CMD_WINDOW_RESIZED:
        case APP_CMD_CONFIG_CHANGED:
        case APP_CMD_WINDOW_REDRAW_NEEDED:
            self.dirty_ = true;
            break;
        case APP_CMD_RESUME:
            self.resumed_ = true;
            self.dirty_ = true;
            if (self.config_.activityState) self.config_.activityState(true);
            break;
        case APP_CMD_PAUSE:
        case APP_CMD_LOST_FOCUS:
            self.ui_.pointer(0, {-100, -100}, false);
            self.touchId_ = -1;
            if (cmd == APP_CMD_PAUSE)
                self.resumed_ = false;
            if (cmd == APP_CMD_PAUSE && self.config_.activityState) self.config_.activityState(false);
            break;
        }
    }
    static int32_t input(android_app *app, AInputEvent *event) {
        auto &self = *static_cast<PanelApp *>(app->userData);
        if (AInputEvent_getType(event) == AINPUT_EVENT_TYPE_KEY) {
            if (AKeyEvent_getKeyCode(event) != AKEYCODE_BACK)
                return 0;
            if (AKeyEvent_getAction(event) == AKEY_EVENT_ACTION_UP) {
                if (self.ui_.applicationBack && self.ui_.applicationBack()) {
                    return 1;
                } else if (self.ui_.modalOpen) {
                    self.ui_.modalOpen = false;
                    self.ui_.invalidate();
                } else if (self.ui_.page != Page::Home)
                    self.ui_.navigate(Page::Home);
                else
                    self.quit_ = true;
            }
            return 1;
        }
        if (AInputEvent_getType(event) != AINPUT_EVENT_TYPE_MOTION)
            return 0;
        const int action = AMotionEvent_getAction(event) & AMOTION_EVENT_ACTION_MASK;
        const size_t changed = (AMotionEvent_getAction(event) & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK) >>
                               AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT;
        if (action == AMOTION_EVENT_ACTION_DOWN)
            self.touchId_ = AMotionEvent_getPointerId(event, 0);
        if (action == AMOTION_EVENT_ACTION_CANCEL || action == AMOTION_EVENT_ACTION_HOVER_EXIT) {
            self.ui_.pointer(0, {-100, -100}, false);
            self.touchId_ = -1;
            return 1;
        }
        bool hover = action == AMOTION_EVENT_ACTION_HOVER_MOVE || action == AMOTION_EVENT_ACTION_HOVER_ENTER;
        for (size_t i = 0; i < AMotionEvent_getPointerCount(event); ++i) {
            if (!hover && AMotionEvent_getPointerId(event, i) != self.touchId_)
                continue;
            const bool up = action == AMOTION_EVENT_ACTION_UP ||
                            (action == AMOTION_EVENT_ACTION_POINTER_UP && i == changed);
            Vec2 position{AMotionEvent_getX(event, i), AMotionEvent_getY(event, i)};
            self.ui_.pointer(0, self.mapping_.toCanvas(position), !up && !hover);
            if (up)
                self.touchId_ = -1;
            return 1;
        }
        return 1;
    }
    std::vector<uint8_t> asset(const std::string &name) {
        auto file = AAssetManager_open(app_->activity->assetManager, name.c_str(), AASSET_MODE_BUFFER);
        if (!file)
            throw std::runtime_error("Missing asset: " + name);
        std::vector<uint8_t> data(AAsset_getLength(file));
        int read = AAsset_read(file, data.data(), data.size());
        AAsset_close(file);
        if (read != int(data.size()))
            throw std::runtime_error("Incomplete asset: " + name);
        return data;
    }
    void createSurface() {
        if (!gpu_.instance) {
            loadVulkan();
            VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
            app.pApplicationName = config_.name.c_str();
            app.applicationVersion = config_.version;
            app.apiVersion = VK_API_VERSION_1_1;
            const char *extensions[]{VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_ANDROID_SURFACE_EXTENSION_NAME};
            VkInstanceCreateInfo info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
            info.pApplicationInfo = &app;
            info.enabledExtensionCount = 2;
            info.ppEnabledExtensionNames = extensions;
            vkCheck(vkCreateInstance(&info, nullptr, &gpu_.instance), "create panel instance");
        }
        VkAndroidSurfaceCreateInfoKHR info{VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR};
        info.window = app_->window;
        vkCheck(vkCreateAndroidSurfaceKHR(gpu_.instance, &info, nullptr, &surface_),
                "create Android surface");
        if (!gpu_.device) {
            uint32_t count{};
            vkCheck(vkEnumeratePhysicalDevices(gpu_.instance, &count, nullptr), "panel GPU count");
            std::vector<VkPhysicalDevice> devices(count);
            vkCheck(vkEnumeratePhysicalDevices(gpu_.instance, &count, devices.data()), "panel GPUs");
            for (auto physical : devices) {
                uint32_t n{};
                vkGetPhysicalDeviceQueueFamilyProperties(physical, &n, nullptr);
                std::vector<VkQueueFamilyProperties> queues(n);
                vkGetPhysicalDeviceQueueFamilyProperties(physical, &n, queues.data());
                for (uint32_t i = 0; i < n; ++i) {
                    VkBool32 present{};
                    vkCheck(vkGetPhysicalDeviceSurfaceSupportKHR(physical, i, surface_, &present),
                            "panel present support");
                    if (present && (queues[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
                        gpu_.physical = physical;
                        gpu_.queueFamily = i;
                        break;
                    }
                }
                if (gpu_.physical)
                    break;
            }
            if (!gpu_.physical)
                throw std::runtime_error("No graphics/present queue for the Android window");
            float priority = 1;
            VkDeviceQueueCreateInfo queue{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
            queue.queueFamilyIndex = gpu_.queueFamily;
            queue.queueCount = 1;
            queue.pQueuePriorities = &priority;
            const char *extension = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
            VkDeviceCreateInfo device{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
            device.queueCreateInfoCount = 1;
            device.pQueueCreateInfos = &queue;
            device.enabledExtensionCount = 1;
            device.ppEnabledExtensionNames = &extension;
            if(config_.graphics)config_.graphics->configureDevice(gpu_,device);
            vkCheck(vkCreateDevice(gpu_.physical, &device, nullptr, &gpu_.device), "create panel device");
            vkGetDeviceQueue(gpu_.device, gpu_.queueFamily, 0, &gpu_.queue);
            VkFenceCreateInfo fence{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
            vkCheck(vkCreateFence(gpu_.device, &fence, nullptr, &acquired_), "create acquire fence");
        }
    }
    void releaseSwapchain(bool keepRenderer = false) {
        if (gpu_.device)
            vkDeviceWaitIdle(gpu_.device);
        if(renderer_ && config_.graphics && !closing_ && !keepRenderer)config_.graphics->graphicsStopping(false);
        for (auto &target : targets_)
            renderer_->destroyTarget(target);
        targets_.clear();
        if (!keepRenderer)
            renderer_.reset();
        if (swapchain_)
            vkDestroySwapchainKHR(gpu_.device, swapchain_, nullptr);
        swapchain_ = VK_NULL_HANDLE;
    }
    void releaseSurface() {
        releaseSwapchain();
        if (surface_)
            vkDestroySurfaceKHR(gpu_.instance, surface_, nullptr);
        surface_ = VK_NULL_HANDLE;
        dirty_ = true;
    }
    void resize() {
        if (!surface_)
            createSurface();
        VkSurfaceCapabilitiesKHR caps{};
        vkCheck(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(gpu_.physical, surface_, &caps),
                "panel capabilities");
        VkExtent2D extent = caps.currentExtent;
        if (extent.width == UINT32_MAX) {
            extent.width = std::clamp(uint32_t(std::max(1, ANativeWindow_getWidth(app_->window))),
                                      caps.minImageExtent.width, caps.maxImageExtent.width);
            extent.height = std::clamp(uint32_t(std::max(1, ANativeWindow_getHeight(app_->window))),
                                       caps.minImageExtent.height, caps.maxImageExtent.height);
        }
        if (!extent.width || !extent.height)
            return;
        // Android can send redraw/configuration/resume commands without a size
        // change. Keep pipelines, atlases and video descriptors in that case.
        // Out-of-date/suboptimal swapchains must still be recreated.
        if (swapchain_ && !swapchainInvalid_ && !targets_.empty() &&
            targets_[0].width == extent.width && targets_[0].height == extent.height) {
            dirty_ = false;
            revision_ = 0;
            return;
        }
        uint32_t count{};
        vkCheck(vkGetPhysicalDeviceSurfaceFormatsKHR(gpu_.physical, surface_, &count, nullptr),
                "panel format count");
        std::vector<VkSurfaceFormatKHR> formats(count);
        vkCheck(vkGetPhysicalDeviceSurfaceFormatsKHR(gpu_.physical, surface_, &count, formats.data()),
                "panel formats");
        if (formats.empty())
            throw std::runtime_error("Android surface exposes no Vulkan formats");
        auto format = formats.front();
        for (auto candidate : formats)
            if ((candidate.format == VK_FORMAT_R8G8B8A8_SRGB ||
                 candidate.format == VK_FORMAT_B8G8R8A8_SRGB) &&
                candidate.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
                format = candidate;
                break;
            }
        // Pipelines, atlases and application resources survive a window drag: only the
        // swapchain depends on its size.
        const bool keepRenderer = renderer_ && renderer_->colorFormat() == format.format;
        releaseSwapchain(keepRenderer);
        VkCompositeAlphaFlagBitsKHR alpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        for (auto candidate :
             {VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR, VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,
              VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR, VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR})
            if (caps.supportedCompositeAlpha & candidate) {
                alpha = candidate;
                break;
            }
        VkSwapchainCreateInfoKHR info{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
        info.surface = surface_;
        info.minImageCount = std::max(2u, caps.minImageCount);
        if (caps.maxImageCount)
            info.minImageCount = std::min(info.minImageCount, caps.maxImageCount);
        info.imageFormat = format.format;
        info.imageColorSpace = format.colorSpace;
        info.imageExtent = extent;
        info.imageArrayLayers = 1;
        info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        info.preTransform = caps.currentTransform;
        info.compositeAlpha = alpha;
        info.presentMode = VK_PRESENT_MODE_FIFO_KHR;
        info.clipped = VK_TRUE;
        vkCheck(vkCreateSwapchainKHR(gpu_.device, &info, nullptr, &swapchain_), "create panel swapchain");
        vkCheck(vkGetSwapchainImagesKHR(gpu_.device, swapchain_, &count, nullptr), "panel image count");
        std::vector<VkImage> images(count);
        vkCheck(vkGetSwapchainImagesKHR(gpu_.device, swapchain_, &count, images.data()), "panel images");
        if (!keepRenderer) {
            renderer_ = std::make_unique<VulkanRenderer>(gpu_);
            renderer_->initialize(format.format, [&](const std::string &name) { return asset(name); });
            rendererRecreated_ = true;
        }
        for (auto image : images)
            targets_.push_back(renderer_->createTarget(extent.width, extent.height, image));
        mapping_ = fitCanvas(float(extent.width), float(extent.height));
        ui_.viewportSize = {float(extent.width), float(extent.height)};
        if(config_.graphics && !keepRenderer)config_.graphics->graphicsReady(gpu_,*renderer_,format.format,
            [&](const std::string& name){return asset(name);},ui_);
        dirty_ = false;
        swapchainInvalid_ = false;
        revision_ = 0;
        __android_log_print(ANDROID_LOG_INFO, "CyVR",
                            "Panel2D / Vulkan Android surface %ux%u / OpenXR inactive", extent.width,
                            extent.height);
    }
    void render() {
        uint32_t index{};
        vkCheck(vkResetFences(gpu_.device, 1, &acquired_), "reset panel acquire fence");
        auto result =
            vkAcquireNextImageKHR(gpu_.device, swapchain_, 1000000000, VK_NULL_HANDLE, acquired_, &index);
        if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_ERROR_SURFACE_LOST_KHR) {
            swapchainInvalid_ = true;
            if (result == VK_ERROR_SURFACE_LOST_KHR)
                releaseSurface();
            dirty_ = true;
            return;
        }
        if (result == VK_TIMEOUT || result == VK_NOT_READY)
            return;
        if (result != VK_SUBOPTIMAL_KHR)
            vkCheck(result, "acquire panel image");
        vkCheck(vkWaitForFences(gpu_.device, 1, &acquired_, VK_TRUE, UINT64_MAX), "wait panel acquisition");
        bool suboptimal = result == VK_SUBOPTIMAL_KHR;
        // Pointer moves can redraw continuously; the figure is displayed at the HUD's 2 Hz.
        if (auto now = Clock::now(); now >= nextMemorySample_) {
            nextMemorySample_ = now + std::chrono::milliseconds(500);
            std::ifstream memory("/proc/self/statm");
            uint64_t total{}, resident{};
            if (memory >> total >> resident)
                ui_.stats.rssMiB = double(resident) * sysconf(_SC_PAGESIZE) / 1048576.;
        }
        if (config_.prepareRenderer) config_.prepareRenderer(*renderer_, rendererRecreated_);
        rendererRecreated_ = false;
        ui_.drawMain(draw_);
        if (ui_.debugVisible && !ui_.interfaceHidden) {
            DrawList hud;
            ui_.drawDebug(hud);
            // Application HUDs may contain video metrics. Keep them legible
            // inside the main panel without covering its navigation bar.
            const bool applicationHud = bool(ui_.renderApplicationDebug);
            const float hudScale = applicationHud ? .62f : .46f;
            for (auto vertex : hud.vertices) {
                vertex.x = (applicationHud ? 1020 : 1120) + vertex.x * hudScale;
                vertex.y = (applicationHud ? 170 : 100) + vertex.y * hudScale;
                draw_.vertices.push_back(vertex);
            }
        }
        if (!ui_.interfaceHidden) {
            for (auto &vertex : draw_.vertices) {
                auto point = mapping_.toSurface({vertex.x, vertex.y});
                vertex.x = point.x;
                vertex.y = point.y;
            }
        }
        auto before = Clock::now();
        // Opaque system window; translucency and blur apply to this app's own decor only.
        auto materials = ui_.settings;
        if (materials.background == Background::Passthrough)
            materials.background = Background::Studio;
        auto glass = glassView({}, PanelGeometry{}, materials);
        glass.options[3] = !materials.highContrast && materials.opacity < .999f ? 1.f : 0.f;
        renderer_->draw(targets_[index], draw_, ui_.interfaceHidden ? nullptr : &glass, true,
                        ui_.interfaceHidden ? Vec2{1440, 990} : Vec2{});
        VkPresentInfoKHR present{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
        present.swapchainCount = 1;
        present.pSwapchains = &swapchain_;
        present.pImageIndices = &index;
        result = vkQueuePresentKHR(gpu_.queue, &present);
        if ((result == VK_SUCCESS || result == VK_SUBOPTIMAL_KHR) && config_.graphics)
            config_.graphics->frameSubmitted(ui_);
        if (result == VK_ERROR_SURFACE_LOST_KHR)
            releaseSurface();
        else if (result != VK_ERROR_OUT_OF_DATE_KHR && result != VK_SUBOPTIMAL_KHR)
            vkCheck(result, "present panel");
        dirty_ = suboptimal || result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR ||
                 result == VK_ERROR_SURFACE_LOST_KHR;
        swapchainInvalid_ = dirty_;
        ui_.stats.cpuMs = std::chrono::duration<double, std::milli>(Clock::now() - before).count();
        revision_ = ui_.revision;
    }

  public:
    PanelApp(android_app *app, const AndroidAppConfig &config) : app_(app), config_(config) {
        app->userData = this;
        app->onAppCmd = command;
        app->onInputEvent = input;
        storage_ = app->activity->externalDataPath ? app->activity->externalDataPath
                                                   : app->activity->internalDataPath;
        ui_.settings = loadSettings(storage_ + "/preferences.txt");
        ui_.presentation = Presentation::Panel2D;
        ui_.immersiveAvailable = config.immersiveEnabled;
        ui_.runtime = "Android native window / Vulkan";
        ui_.statusKey = "status.panel";
        ui_.page = Page(std::clamp(androidInitialPage(app), 0, 3));
        ui_.settingsTab = 1;
        try {
            if (config.configure) config.configure(ui_);
        } catch (...) {
            if (config.releaseApplication) config.releaseApplication();
            throw;
        }
        ui_.log("Panel2D host / system touch input / OpenXR inactive");
        if (activityValue(app, "immersiveFailed", 0))
            ui_.notify("toast.immersive_failed");
    }
    ~PanelApp() {
        // Same order as the immersive host: applications release GPU resources only once idle.
        if (gpu_.device)
            vkDeviceWaitIdle(gpu_.device);
        closing_ = true;
        if(config_.graphics)config_.graphics->graphicsStopping(true);
        if (config_.releaseApplication) config_.releaseApplication();
        releaseSurface();
        if (acquired_)
            vkDestroyFence(gpu_.device, acquired_, nullptr);
        app_->userData = nullptr;
        app_->onAppCmd = nullptr;
        app_->onInputEvent = nullptr;
    }
    int run() {
        while (!app_->destroyRequested && !quit_) {
            android_poll_source *source{};
            int events{};
            // Multi-window apps may keep drawing a visible but unfocused panel.
            int timeout = (resumed_ || (config_.renderWhileVisible && visible_)) && app_->window
                ? (config_.graphics ? std::clamp(config_.graphics->pollTimeoutMs(),1,33) : 33) : -1;
            while (ALooper_pollOnce(timeout, nullptr, &events, reinterpret_cast<void **>(&source)) >= 0) {
                if (source)
                    source->process(app_, source);
                if (app_->destroyRequested)
                    break;
                timeout = 0;
            }
            if (app_->destroyRequested)
                break;
            ui_.tick(std::chrono::duration<double>(Clock::now() - start_).count());
            if (ui_.saveRequested) {
                ui_.saveRequested = false;
                if (!saveSettings(storage_ + "/preferences.txt", ui_.settings))
                    ui_.notify("toast.save_failed");
            }
            if (ui_.exportRequested) {
                ui_.exportRequested = false;
                exportDiagnostic(storage_ + "/diagnostic.json", ui_);
            }
            if (ui_.immersiveRequested && config_.immersiveEnabled) {
                saveSettings(storage_ + "/preferences.txt", ui_.settings);
                return int(ui_.page);
            }
            if (ui_.exitRequested)
                break;
            if ((!resumed_ && !(config_.renderWhileVisible && visible_)) || !app_->window)
                continue;
            if (dirty_)
                resize();
            if(swapchain_ && config_.graphics && config_.graphics->beforeRender(ui_,
                std::chrono::duration<double>(Clock::now()-start_).count()))ui_.invalidate();
            if (swapchain_ && revision_ != ui_.revision)
                render();
        }
        return -1;
    }
};
} // namespace
int cyvr::androidInitialPage(android_app *app) {
    return activityValue(app, "initialPage", 0);
}
void cyvr::androidSwitchPresentation(android_app *app, Presentation presentation, int page,
                                     bool immersiveFailed) {
    auto env = environment(app);
    auto type = env->GetObjectClass(app->activity->clazz);
    auto id = env->GetMethodID(type, "launchPresentation", immersiveFailed ? "(IIZ)V" : "(II)V");
    if (id && immersiveFailed)
        env->CallVoidMethod(app->activity->clazz, id, int(presentation), page, JNI_TRUE);
    else if (id)
        env->CallVoidMethod(app->activity->clazz, id, int(presentation), page);
    env->DeleteLocalRef(type);
    if (env->ExceptionCheck()) {
        env->ExceptionDescribe();
        env->ExceptionClear();
        throw std::runtime_error("Android presentation transition failed");
    }
}
void cyvr::runPanelAndroidApp(android_app *app, const AndroidAppConfig &config) {
    int page;
    {
        PanelApp panel(app, config);
        page = panel.run();
    }
    if (page >= 0)
        androidSwitchPresentation(app, Presentation::Immersive, page);
}
void cyvr::runAndroidApp(android_app *app, const AndroidAppConfig &config) {
    auto mode = activityValue(app, "presentationMode", int(config.presentation));
#if CYVR_ENABLE_IMMERSIVE
    if (mode == int(Presentation::Immersive) && config.immersiveEnabled) {
        runImmersiveAndroidApp(app, config);
        return;
    }
#else
    (void)mode;
#endif
    auto effective = config;
#if !CYVR_ENABLE_IMMERSIVE
    effective.immersiveEnabled = false;
#endif
    runPanelAndroidApp(app, effective);
}
