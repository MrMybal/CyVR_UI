// SPDX-License-Identifier: MIT
#include "cyvr/android.hpp"
#include "cyvr/gpu.hpp"
#include "cyvr/storage.hpp"
#include <android/asset_manager.h>
#include <android/log.h>
#include <android_native_app_glue.h>
#include <chrono>
#include <cstring>
#include <jni.h>
#include <memory>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include <unistd.h>

using namespace cyvr;
namespace {
void check(XrResult result, const char *operation) {
    if (XR_FAILED(result))
        throw std::runtime_error(std::string(operation) + " / OpenXR " + std::to_string(result));
}
template <class T> T function(XrInstance instance, const char *name) {
    PFN_xrVoidFunction p{};
    check(xrGetInstanceProcAddr(instance, name, &p), name);
    return reinterpret_cast<T>(p);
}
XrPosef xrPose(Pose pose) {
    return {{pose.orientation.x, pose.orientation.y, pose.orientation.z, pose.orientation.w},
            {pose.position.x, pose.position.y, pose.position.z}};
}
Pose poseOf(XrPosef pose) {
    return {{pose.position.x, pose.position.y, pose.position.z},
            {pose.orientation.x, pose.orientation.y, pose.orientation.z, pose.orientation.w}};
}
struct Layer {
    XrSwapchain swapchain{};
    std::vector<Target> targets;
    uint32_t width{}, height{};
    bool initialized{};
};
class QuestApp {
  public:
    explicit QuestApp(android_app *android, const AndroidAppConfig &config)
        : android_(android), config_(config) {
        android_->userData = this;
        android_->onAppCmd = command;
    }
    ~QuestApp() {
        if (gpu_.device)
            vkDeviceWaitIdle(gpu_.device);
        for (auto *layer : {&mainLayer_, &debugLayer_, &dockLayer_, &cursorLayer_, &eyes_[0], &eyes_[1]}) {
            if (renderer_)
                for (auto &target : layer->targets)
                    renderer_->destroyTarget(target);
            if (layer->swapchain)
                xrDestroySwapchain(layer->swapchain);
        }
        renderer_.reset();
        if (passthroughLayer_ && destroyPassthroughLayer_)
            destroyPassthroughLayer_(passthroughLayer_);
        if (passthrough_ && destroyPassthrough_)
            destroyPassthrough_(passthrough_);
        for (auto space : aimSpaces_)
            if (space)
                xrDestroySpace(space);
        for (auto space : gripSpaces_)
            if (space)
                xrDestroySpace(space);
        for (auto tracker : handTrackers_)
            if (tracker && destroyHand_)
                destroyHand_(tracker);
        if (view_)
            xrDestroySpace(view_);
        if (local_)
            xrDestroySpace(local_);
        if (session_)
            xrDestroySession(session_);
        if (actionSet_)
            xrDestroyActionSet(actionSet_);
        if (instance_)
            xrDestroyInstance(instance_);
    }
    void run() {
        while (!android_->destroyRequested && (!android_->window || !resumed_))
            pump(100);
        if (android_->destroyRequested)
            return;
        initialize();
        while (!android_->destroyRequested && !quit_) {
            pump(running_ ? 0 : 50);
            pollEvents();
            if (running_)
                frame();
        }
    }

  private:
    android_app *android_;
    const AndroidAppConfig &config_;
    bool resumed_{}, running_{}, quit_{}, focused_{}, panelPlaced_{}, startupReport_{};
    uint32_t visibleFrames_{};
    XrInstance instance_{};
    XrSystemId system_{};
    XrSession session_{};
    XrSpace local_{}, view_{};
    XrActionSet actionSet_{};
    XrAction aim_{}, grip_{}, trigger_{}, haptic_{}, recenter_{}, debug_{};
    std::array<XrPath, 2> hands_{};
    std::array<XrSpace, 2> aimSpaces_{}, gripSpaces_{};
    std::array<bool, 2> pressed_{};
    bool handEnabled_{}, handAimEnabled_{}, handMeshEnabled_{}, passthroughEnabled_{}, passthroughRunning_{};
    std::array<HandSkin, 2> handSkins_;
    XrPassthroughFB passthrough_{};
    XrPassthroughLayerFB passthroughLayer_{};
    PFN_xrDestroyPassthroughFB destroyPassthrough_{};
    PFN_xrDestroyPassthroughLayerFB destroyPassthroughLayer_{};
    PFN_xrPassthroughStartFB startPassthrough_{};
    PFN_xrPassthroughPauseFB pausePassthrough_{};
    PFN_xrPassthroughLayerResumeFB resumePassthroughLayer_{};
    PFN_xrPassthroughLayerPauseFB pausePassthroughLayer_{};
    std::array<XrHandTrackerEXT, 2> handTrackers_{};
    PFN_xrLocateHandJointsEXT locateHand_{};
    PFN_xrDestroyHandTrackerEXT destroyHand_{};
    SpatialMesh spatial_;
    HandSurface controllerSurface_;
    std::array<Vec3, 2> rayOrigin_{}, rayDirection_{};
    std::array<bool, 2> rayValid_{}, handReported_{};
    int windowDragIndex_{-1};
    float windowDragDepth_{};
    Vec3 windowDragOffset_{};
    std::array<Hit, 2> hits_{};
    std::array<float, 2> cursorSize_{{.012f, .012f}};
    VulkanContext gpu_;
    std::unique_ptr<VulkanRenderer> renderer_;
    Layer mainLayer_, debugLayer_, dockLayer_, cursorLayer_;
    std::array<Layer, 2> eyes_;
    PanelGeometry panel_, debugPanel_, dockPanel_;
    std::array<PanelGeometry, 2> hitPanels_;
    Pose headPose_;
    bool headValid_{};
    UiModel ui_;
    DrawList mainDraw_, debugDraw_, dockDraw_;
    uint64_t dockRevision_{};
    uint64_t drawnRevision_{};
    double lastStats_{-1};
    double lastDebugRender_{-1};
    double lastGlassRender_{-1};
    Pose glassEye_, glassPanel_;
    XrTime recenterAfter_{};
    std::chrono::steady_clock::time_point clock_ = std::chrono::steady_clock::now(), lastFrame_ = clock_;
    PFN_xrEnumerateDisplayRefreshRatesFB enumerateRates_{};
    PFN_xrRequestDisplayRefreshRateFB requestRate_{};
    PFN_xrGetDisplayRefreshRateFB getRate_{};
    float lastRequested_{};
    std::string storage_;
    static void command(android_app *app, int32_t command) {
        auto &self = *static_cast<QuestApp *>(app->userData);
        if (command == APP_CMD_RESUME)
            self.resumed_ = true;
        if (command == APP_CMD_PAUSE)
            self.resumed_ = false;
    }
    void pump(int timeout) {
        int events;
        android_poll_source *source = nullptr;
        int id;
        while ((id = ALooper_pollOnce(timeout, nullptr, &events, reinterpret_cast<void **>(&source))) >= 0) {
            if (source)
                source->process(android_, source);
            if (android_->destroyRequested)
                break;
            timeout = 0;
        }
    }
    XrPath path(const char *value) {
        XrPath out{};
        check(xrStringToPath(instance_, value, &out), value);
        return out;
    }
    void log(const std::string &value) {
        __android_log_print(ANDROID_LOG_INFO, "CyVR", "%s", value.c_str());
        ui_.log(value);
    }
    std::vector<uint8_t> asset(const std::string &name) {
        AAsset *handle =
            AAssetManager_open(android_->activity->assetManager, name.c_str(), AASSET_MODE_BUFFER);
        if (!handle)
            throw std::runtime_error("Missing asset: " + name);
        size_t length = AAsset_getLength64(handle);
        std::vector<uint8_t> bytes(length);
        int count = AAsset_read(handle, bytes.data(), length);
        AAsset_close(handle);
        if (count != int(length))
            throw std::runtime_error("Incomplete asset: " + name);
        return bytes;
    }
    void initialize() {
        auto loader = function<PFN_xrInitializeLoaderKHR>(XR_NULL_HANDLE, "xrInitializeLoaderKHR");
        XrLoaderInitInfoAndroidKHR loaderInfo{XR_TYPE_LOADER_INIT_INFO_ANDROID_KHR};
        loaderInfo.applicationVM = android_->activity->vm;
        loaderInfo.applicationContext = android_->activity->clazz;
        check(loader(reinterpret_cast<XrLoaderInitInfoBaseHeaderKHR *>(&loaderInfo)),
              "initialize Android loader");
        uint32_t count = 0;
        check(xrEnumerateInstanceExtensionProperties(nullptr, 0, &count, nullptr), "extension count");
        std::vector<XrExtensionProperties> available(count, {XR_TYPE_EXTENSION_PROPERTIES});
        check(xrEnumerateInstanceExtensionProperties(nullptr, count, &count, available.data()), "extensions");
        auto supports = [&](const char *name) {
            return std::any_of(available.begin(), available.end(),
                               [&](auto &e) { return std::strcmp(name, e.extensionName) == 0; });
        };
        std::vector<const char *> extensions;
        for (auto name :
             {XR_KHR_ANDROID_CREATE_INSTANCE_EXTENSION_NAME, XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME}) {
            if (!supports(name))
                throw std::runtime_error(std::string("Required extension: ") + name);
            extensions.push_back(name);
        }
        bool refresh = supports(XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME);
        if (refresh)
            extensions.push_back(XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME);
        handEnabled_ = supports(XR_EXT_HAND_TRACKING_EXTENSION_NAME);
        handAimEnabled_ = handEnabled_ && supports(XR_FB_HAND_TRACKING_AIM_EXTENSION_NAME);
        handMeshEnabled_ = handEnabled_ && supports(XR_FB_HAND_TRACKING_MESH_EXTENSION_NAME);
        passthroughEnabled_ = supports(XR_FB_PASSTHROUGH_EXTENSION_NAME);
        if (handEnabled_)
            extensions.push_back(XR_EXT_HAND_TRACKING_EXTENSION_NAME);
        if (handAimEnabled_)
            extensions.push_back(XR_FB_HAND_TRACKING_AIM_EXTENSION_NAME);
        if (handMeshEnabled_)
            extensions.push_back(XR_FB_HAND_TRACKING_MESH_EXTENSION_NAME);
        if (passthroughEnabled_)
            extensions.push_back(XR_FB_PASSTHROUGH_EXTENSION_NAME);
        XrInstanceCreateInfoAndroidKHR androidInfo{XR_TYPE_INSTANCE_CREATE_INFO_ANDROID_KHR};
        androidInfo.applicationVM = android_->activity->vm;
        androidInfo.applicationActivity = android_->activity->clazz;
        XrInstanceCreateInfo info{XR_TYPE_INSTANCE_CREATE_INFO};
        info.next = &androidInfo;
        std::strncpy(info.applicationInfo.applicationName, config_.name.c_str(),
                     XR_MAX_APPLICATION_NAME_SIZE - 1);
        info.applicationInfo.applicationVersion = config_.version;
        info.applicationInfo.apiVersion = XR_API_VERSION_1_0;
        info.enabledExtensionCount = uint32_t(extensions.size());
        info.enabledExtensionNames = extensions.data();
        check(xrCreateInstance(&info, &instance_), "create instance");
        XrSystemGetInfo systemInfo{XR_TYPE_SYSTEM_GET_INFO};
        systemInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
        check(xrGetSystem(instance_, &systemInfo, &system_), "get system");
        XrInstanceProperties runtime{XR_TYPE_INSTANCE_PROPERTIES};
        check(xrGetInstanceProperties(instance_, &runtime), "runtime properties");
        XrSystemProperties headset{XR_TYPE_SYSTEM_PROPERTIES};
        XrSystemHandTrackingPropertiesEXT handProperties{XR_TYPE_SYSTEM_HAND_TRACKING_PROPERTIES_EXT};
        XrSystemPassthroughPropertiesFB passthroughProperties{XR_TYPE_SYSTEM_PASSTHROUGH_PROPERTIES_FB};
        if (passthroughEnabled_)
            headset.next = &passthroughProperties;
        if (handEnabled_) {
            handProperties.next = headset.next;
            headset.next = &handProperties;
        }
        check(xrGetSystemProperties(instance_, system_, &headset), "headset properties");
        handEnabled_ = handEnabled_ && handProperties.supportsHandTracking;
        passthroughEnabled_ = passthroughEnabled_ && passthroughProperties.supportsPassthrough;
        ui_.runtime = std::string(headset.systemName) + " / " + runtime.runtimeName;
        ui_.statusKey = "status.waiting";
        log(ui_.runtime);
        createGraphics();
        XrGraphicsBindingVulkan2KHR binding{XR_TYPE_GRAPHICS_BINDING_VULKAN2_KHR};
        binding.instance = gpu_.instance;
        binding.physicalDevice = gpu_.physical;
        binding.device = gpu_.device;
        binding.queueFamilyIndex = gpu_.queueFamily;
        binding.queueIndex = 0;
        XrSessionCreateInfo sessionInfo{XR_TYPE_SESSION_CREATE_INFO};
        sessionInfo.next = &binding;
        sessionInfo.systemId = system_;
        check(xrCreateSession(instance_, &sessionInfo, &session_), "create session");
        createPassthrough();
        XrReferenceSpaceCreateInfo reference{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
        reference.poseInReferenceSpace.orientation.w = 1;
        reference.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
        check(xrCreateReferenceSpace(session_, &reference, &local_), "local space");
        reference.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
        check(xrCreateReferenceSpace(session_, &reference, &view_), "view space");
        createActions();
        if (handEnabled_) {
            auto createHand = function<PFN_xrCreateHandTrackerEXT>(instance_, "xrCreateHandTrackerEXT");
            locateHand_ = function<PFN_xrLocateHandJointsEXT>(instance_, "xrLocateHandJointsEXT");
            destroyHand_ = function<PFN_xrDestroyHandTrackerEXT>(instance_, "xrDestroyHandTrackerEXT");
            for (int i = 0; i < 2; i++) {
                XrHandTrackerCreateInfoEXT create{XR_TYPE_HAND_TRACKER_CREATE_INFO_EXT};
                create.hand = i == 0 ? XR_HAND_LEFT_EXT : XR_HAND_RIGHT_EXT;
                create.handJointSet = XR_HAND_JOINT_SET_DEFAULT_EXT;
                XrResult result = createHand(session_, &create, &handTrackers_[i]);
                if (XR_FAILED(result)) {
                    handTrackers_[i] = XR_NULL_HANDLE;
                    log("Hand tracking unavailable");
                } else if (handMeshEnabled_)
                    loadHandMesh(i);
            }
        }
        check(xrEnumerateSwapchainFormats(session_, 0, &count, nullptr), "format count");
        std::vector<int64_t> formats(count);
        check(xrEnumerateSwapchainFormats(session_, count, &count, formats.data()), "formats");
        VkFormat format = VK_FORMAT_UNDEFINED;
        for (auto candidate : {VK_FORMAT_R8G8B8A8_SRGB, VK_FORMAT_B8G8R8A8_SRGB})
            if (std::find(formats.begin(), formats.end(), int64_t(candidate)) != formats.end()) {
                format = candidate;
                break;
            }
        if (format == VK_FORMAT_UNDEFINED)
            throw std::runtime_error("An sRGB color swapchain is required");
        renderer_ = std::make_unique<VulkanRenderer>(gpu_);
        renderer_->initialize(format, [&](const std::string &name) { return asset(name); });
        controllerSurface_ = HandSurface::load(asset("hand_studio.bin"));
        if (controllerSurface_.vertices.empty())
            log("Studio model unavailable / fallback geometry");
        createLayer(mainLayer_, 1440, 990, format);
        createLayer(debugLayer_, 620, 760, format);
        createLayer(dockLayer_, dockWidth, dockHeight, format);
        createLayer(cursorLayer_, 64, 64, format);
        check(xrEnumerateViewConfigurationViews(instance_, system_, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
                                                0, &count, nullptr),
              "view count");
        if (count != 2)
            throw std::runtime_error("Stereo views required");
        std::vector<XrViewConfigurationView> views(count, {XR_TYPE_VIEW_CONFIGURATION_VIEW});
        check(xrEnumerateViewConfigurationViews(instance_, system_, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
                                                count, &count, views.data()),
              "view sizes");
        for (int i = 0; i < 2; i++) {
            float ratio = std::min(1.f, 1024.f / std::max(views[i].recommendedImageRectWidth,
                                                          views[i].recommendedImageRectHeight));
            createLayer(eyes_[i], uint32_t(views[i].recommendedImageRectWidth * ratio),
                        uint32_t(views[i].recommendedImageRectHeight * ratio), format, true);
        }
        DrawList cursor;
        cursor.rect({4, 4, 56, 56}, Color::hex(0x86e7cf), 28);
        cursor.rect({9, 9, 46, 46}, Color::hex(0x142c29), 23);
        cursor.rect({25, 25, 14, 14}, Color::hex(0xf0f5f6), 7);
        render(cursorLayer_, cursor);
        storage_ = android_->activity->externalDataPath ? android_->activity->externalDataPath
                                                        : android_->activity->internalDataPath;
        ui_.settings = loadSettings(storage_ + "/preferences.txt");
        if (ui_.settings.background == Background::Passthrough && !ui_.passthroughAvailable)
            ui_.settings.background = Background::Studio;
        ui_.refreshRates.clear();
        if (refresh) {
            enumerateRates_ =
                function<PFN_xrEnumerateDisplayRefreshRatesFB>(instance_, "xrEnumerateDisplayRefreshRatesFB");
            requestRate_ =
                function<PFN_xrRequestDisplayRefreshRateFB>(instance_, "xrRequestDisplayRefreshRateFB");
            getRate_ = function<PFN_xrGetDisplayRefreshRateFB>(instance_, "xrGetDisplayRefreshRateFB");
            check(enumerateRates_(session_, 0, &count, nullptr), "refresh count");
            ui_.refreshRates.resize(count);
            check(enumerateRates_(session_, count, &count, ui_.refreshRates.data()), "refresh rates");
        }
        if (config_.configure)
            config_.configure(ui_);
        ui_.drawMain(mainDraw_);
        ui_.drawDock(dockDraw_);
        log("Composited panels / Vulkan / shared atlas");
    }
    void createPassthrough() {
        if (!passthroughEnabled_)
            return;
        auto create = function<PFN_xrCreatePassthroughFB>(instance_, "xrCreatePassthroughFB");
        destroyPassthrough_ = function<PFN_xrDestroyPassthroughFB>(instance_, "xrDestroyPassthroughFB");
        destroyPassthroughLayer_ =
            function<PFN_xrDestroyPassthroughLayerFB>(instance_, "xrDestroyPassthroughLayerFB");
        startPassthrough_ = function<PFN_xrPassthroughStartFB>(instance_, "xrPassthroughStartFB");
        pausePassthrough_ = function<PFN_xrPassthroughPauseFB>(instance_, "xrPassthroughPauseFB");
        resumePassthroughLayer_ =
            function<PFN_xrPassthroughLayerResumeFB>(instance_, "xrPassthroughLayerResumeFB");
        pausePassthroughLayer_ =
            function<PFN_xrPassthroughLayerPauseFB>(instance_, "xrPassthroughLayerPauseFB");
        XrPassthroughCreateInfoFB info{XR_TYPE_PASSTHROUGH_CREATE_INFO_FB};
        XrResult result = create(session_, &info, &passthrough_);
        if (XR_SUCCEEDED(result)) {
            XrPassthroughLayerCreateInfoFB layer{XR_TYPE_PASSTHROUGH_LAYER_CREATE_INFO_FB};
            layer.passthrough = passthrough_;
            layer.purpose = XR_PASSTHROUGH_LAYER_PURPOSE_RECONSTRUCTION_FB;
            result = function<PFN_xrCreatePassthroughLayerFB>(instance_, "xrCreatePassthroughLayerFB")(
                session_, &layer, &passthroughLayer_);
        }
        ui_.passthroughAvailable = XR_SUCCEEDED(result) && passthroughLayer_;
        log(ui_.passthroughAvailable ? "Passthrough available"
                                     : "Passthrough unavailable / native environment active");
    }
    void updateBackground() {
        bool requested =
            ui_.settings.background == Background::Passthrough && ui_.passthroughAvailable && running_;
        if (requested == passthroughRunning_)
            return;
        if (requested) {
            XrResult result = startPassthrough_(passthrough_);
            if (XR_SUCCEEDED(result))
                result = resumePassthroughLayer_(passthroughLayer_);
            if (XR_FAILED(result)) {
                pausePassthrough_(passthrough_);
                ui_.settings.background = Background::Studio;
                ui_.saveRequested = true;
                ui_.notify("toast.passthrough_unavailable");
                return;
            }
        } else {
            pausePassthroughLayer_(passthroughLayer_);
            pausePassthrough_(passthrough_);
        }
        passthroughRunning_ = requested;
    }
    void loadHandMesh(int index) {
        auto get = function<PFN_xrGetHandMeshFB>(instance_, "xrGetHandMeshFB");
        XrHandTrackingMeshFB mesh{XR_TYPE_HAND_TRACKING_MESH_FB};
        if (XR_FAILED(get(handTrackers_[index], &mesh)) || mesh.jointCountOutput != 26 ||
            !mesh.vertexCountOutput || mesh.vertexCountOutput > 10000 || mesh.indexCountOutput > 60000)
            return;
        mesh.jointCapacityInput = mesh.jointCountOutput;
        mesh.vertexCapacityInput = mesh.vertexCountOutput;
        mesh.indexCapacityInput = mesh.indexCountOutput;
        std::vector<XrPosef> bind(mesh.jointCountOutput);
        std::vector<float> radii(mesh.jointCountOutput);
        std::vector<XrHandJointEXT> parents(mesh.jointCountOutput);
        std::vector<XrVector3f> positions(mesh.vertexCountOutput), normals(mesh.vertexCountOutput);
        std::vector<XrVector2f> uv(mesh.vertexCountOutput);
        std::vector<XrVector4sFB> blendIndices(mesh.vertexCountOutput);
        std::vector<XrVector4f> blendWeights(mesh.vertexCountOutput);
        std::vector<int16_t> indices(mesh.indexCountOutput);
        mesh.jointBindPoses = bind.data();
        mesh.jointRadii = radii.data();
        mesh.jointParents = parents.data();
        mesh.vertexPositions = positions.data();
        mesh.vertexNormals = normals.data();
        mesh.vertexUVs = uv.data();
        mesh.vertexBlendIndices = blendIndices.data();
        mesh.vertexBlendWeights = blendWeights.data();
        mesh.indices = indices.data();
        if (XR_FAILED(get(handTrackers_[index], &mesh)))
            return;
        HandSkin skin;
        for (int j = 0; j < 26; j++)
            skin.bindPoses[j] = poseOf(bind[j]);
        for (uint32_t i = 0; i < mesh.vertexCountOutput; i++) {
            auto p = positions[i], n = normals[i];
            auto b = blendIndices[i];
            auto w = blendWeights[i];
            // Unused slots in runtime data can be -1; canonicalize zero-weight slots.
            std::array<int16_t, 4> bones{b.x, b.y, b.z, b.w};
            std::array<float, 4> weights{w.x, w.y, w.z, w.w};
            for (int k = 0; k < 4; k++)
                if (weights[k] == 0)
                    bones[k] = 0;
            skin.vertices.push_back({{p.x, p.y, p.z}, {n.x, n.y, n.z}, bones, weights});
        }
        for (auto i : indices)
            skin.indices.push_back(uint16_t(i));
        if (skin.valid()) {
            handSkins_[index] = std::move(skin);
            log("Smooth hand mesh available");
        }
    }
    void createGraphics() {
        loadVulkan();
        auto requirements = function<PFN_xrGetVulkanGraphicsRequirements2KHR>(
            instance_, "xrGetVulkanGraphicsRequirements2KHR");
        XrGraphicsRequirementsVulkan2KHR req{XR_TYPE_GRAPHICS_REQUIREMENTS_VULKAN2_KHR};
        check(requirements(instance_, system_, &req), "Vulkan requirements");
        if (req.minApiVersionSupported > XR_MAKE_VERSION(1, 1, 0) ||
            req.maxApiVersionSupported < XR_MAKE_VERSION(1, 1, 0))
            throw std::runtime_error("Vulkan 1.1 is required");
        VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
        app.pApplicationName = "CyVR UI";
        app.apiVersion = VK_API_VERSION_1_1;
        VkInstanceCreateInfo info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
        info.pApplicationInfo = &app;
        XrVulkanInstanceCreateInfoKHR xrInfo{XR_TYPE_VULKAN_INSTANCE_CREATE_INFO_KHR};
        xrInfo.systemId = system_;
        xrInfo.pfnGetInstanceProcAddr = vkGetInstanceProcAddr;
        xrInfo.vulkanCreateInfo = &info;
        VkResult result{};
        check(function<PFN_xrCreateVulkanInstanceKHR>(instance_, "xrCreateVulkanInstanceKHR")(
                  instance_, &xrInfo, &gpu_.instance, &result),
              "XR Vulkan instance");
        vkCheck(result, "Vulkan instance");
        XrVulkanGraphicsDeviceGetInfoKHR deviceInfo{XR_TYPE_VULKAN_GRAPHICS_DEVICE_GET_INFO_KHR};
        deviceInfo.systemId = system_;
        deviceInfo.vulkanInstance = gpu_.instance;
        check(function<PFN_xrGetVulkanGraphicsDevice2KHR>(instance_, "xrGetVulkanGraphicsDevice2KHR")(
                  instance_, &deviceInfo, &gpu_.physical),
              "XR Vulkan device");
        gpu_.selectQueue();
        float priority = 1;
        VkDeviceQueueCreateInfo queue{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        queue.queueFamilyIndex = gpu_.queueFamily;
        queue.queueCount = 1;
        queue.pQueuePriorities = &priority;
        VkDeviceCreateInfo device{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
        device.queueCreateInfoCount = 1;
        device.pQueueCreateInfos = &queue;
        XrVulkanDeviceCreateInfoKHR deviceCreate{XR_TYPE_VULKAN_DEVICE_CREATE_INFO_KHR};
        deviceCreate.systemId = system_;
        deviceCreate.pfnGetInstanceProcAddr = vkGetInstanceProcAddr;
        deviceCreate.vulkanPhysicalDevice = gpu_.physical;
        deviceCreate.vulkanCreateInfo = &device;
        check(function<PFN_xrCreateVulkanDeviceKHR>(instance_, "xrCreateVulkanDeviceKHR")(
                  instance_, &deviceCreate, &gpu_.device, &result),
              "XR create device");
        vkCheck(result, "Vulkan device");
        vkGetDeviceQueue(gpu_.device, gpu_.queueFamily, 0, &gpu_.queue);
    }
    void createActions() {
        hands_ = {path("/user/hand/left"), path("/user/hand/right")};
        XrActionSetCreateInfo set{XR_TYPE_ACTION_SET_CREATE_INFO};
        std::strcpy(set.actionSetName, "cyvr_ui");
        std::strcpy(set.localizedActionSetName, "CyVR UI");
        check(xrCreateActionSet(instance_, &set, &actionSet_), "action set");
        auto create = [&](const char *name, const char *label, XrActionType type, bool both, XrAction &out) {
            XrActionCreateInfo info{XR_TYPE_ACTION_CREATE_INFO};
            std::strcpy(info.actionName, name);
            std::strcpy(info.localizedActionName, label);
            info.actionType = type;
            if (both) {
                info.countSubactionPaths = 2;
                info.subactionPaths = hands_.data();
            }
            check(xrCreateAction(actionSet_, &info, &out), name);
        };
        create("aim", "Pointer", XR_ACTION_TYPE_POSE_INPUT, true, aim_);
        create("grip", "Hand pose", XR_ACTION_TYPE_POSE_INPUT, true, grip_);
        create("trigger", "Select", XR_ACTION_TYPE_FLOAT_INPUT, true, trigger_);
        create("haptic", "Feedback", XR_ACTION_TYPE_VIBRATION_OUTPUT, true, haptic_);
        create("recenter", "Recenter", XR_ACTION_TYPE_BOOLEAN_INPUT, false, recenter_);
        create("debug", "Debug HUD", XR_ACTION_TYPE_BOOLEAN_INPUT, false, debug_);
        std::vector<XrActionSuggestedBinding> bindings;
        for (auto side : {"left", "right"}) {
            std::string base = std::string("/user/hand/") + side;
            bindings.push_back({aim_, path((base + "/input/aim/pose").c_str())});
            bindings.push_back({grip_, path((base + "/input/grip/pose").c_str())});
            bindings.push_back({trigger_, path((base + "/input/trigger/value").c_str())});
            bindings.push_back({haptic_, path((base + "/output/haptic").c_str())});
        }
        bindings.push_back({recenter_, path("/user/hand/right/input/thumbstick/click")});
        bindings.push_back({debug_, path("/user/hand/left/input/x/click")});
        XrInteractionProfileSuggestedBinding profile{XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
        profile.interactionProfile = path("/interaction_profiles/oculus/touch_controller");
        profile.countSuggestedBindings = uint32_t(bindings.size());
        profile.suggestedBindings = bindings.data();
        check(xrSuggestInteractionProfileBindings(instance_, &profile), "Touch bindings");
        for (int i = 0; i < 2; i++) {
            XrActionSpaceCreateInfo space{XR_TYPE_ACTION_SPACE_CREATE_INFO};
            space.action = aim_;
            space.subactionPath = hands_[i];
            space.poseInActionSpace.orientation.w = 1;
            check(xrCreateActionSpace(session_, &space, &aimSpaces_[i]), "aim space");
            space.action = grip_;
            check(xrCreateActionSpace(session_, &space, &gripSpaces_[i]), "grip space");
        }
        XrSessionActionSetsAttachInfo attach{XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO};
        attach.countActionSets = 1;
        attach.actionSets = &actionSet_;
        check(xrAttachSessionActionSets(session_, &attach), "attach actions");
    }
    void createLayer(Layer &layer, uint32_t w, uint32_t h, VkFormat format, bool depth = false) {
        layer.width = w;
        layer.height = h;
        XrSwapchainCreateInfo info{XR_TYPE_SWAPCHAIN_CREATE_INFO};
        info.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
        info.format = format;
        info.sampleCount = 1;
        info.width = w;
        info.height = h;
        info.faceCount = 1;
        info.arraySize = 1;
        info.mipCount = 1;
        check(xrCreateSwapchain(session_, &info, &layer.swapchain), "create panel swapchain");
        uint32_t count = 0;
        check(xrEnumerateSwapchainImages(layer.swapchain, 0, &count, nullptr), "swapchain image count");
        std::vector<XrSwapchainImageVulkan2KHR> images(count, {XR_TYPE_SWAPCHAIN_IMAGE_VULKAN2_KHR});
        check(xrEnumerateSwapchainImages(layer.swapchain, count, &count,
                                         reinterpret_cast<XrSwapchainImageBaseHeader *>(images.data())),
              "swapchain images");
        for (auto &image : images)
            layer.targets.push_back(renderer_->createTarget(w, h, image.image, depth));
    }
    void render(Layer &layer, const DrawList &draw, const EnvironmentView *glass = nullptr) {
        uint32_t index;
        XrSwapchainImageAcquireInfo acquire{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
        check(xrAcquireSwapchainImage(layer.swapchain, &acquire, &index), "acquire UI image");
        XrSwapchainImageWaitInfo wait{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
        wait.timeout = XR_INFINITE_DURATION;
        check(xrWaitSwapchainImage(layer.swapchain, &wait), "wait UI image");
        renderer_->draw(layer.targets.at(index), draw, glass);
        XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
        check(xrReleaseSwapchainImage(layer.swapchain, &release), "release UI image");
        layer.initialized = true;
    }
    void pollEvents() {
        XrEventDataBuffer event{XR_TYPE_EVENT_DATA_BUFFER};
        XrResult result;
        while ((result = xrPollEvent(instance_, &event)) == XR_SUCCESS) {
            if (event.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {
                auto &state = *reinterpret_cast<XrEventDataSessionStateChanged *>(&event);
                focused_ = state.state == XR_SESSION_STATE_FOCUSED;
                ui_.statusKey = focused_ ? "status.active" : "status.waiting";
                log("OpenXR state: " + std::to_string(int(state.state)));
                if (state.state == XR_SESSION_STATE_READY) {
                    XrSessionBeginInfo begin{XR_TYPE_SESSION_BEGIN_INFO};
                    begin.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
                    check(xrBeginSession(session_, &begin), "begin session");
                    running_ = true;
                    ui_.recenterRequested = true;
                    lastFrame_ = std::chrono::steady_clock::now();
                    ui_.stats.count = ui_.stats.next = 0;
                    log("Session started");
                }
                if (state.state == XR_SESSION_STATE_STOPPING) {
                    running_ = false;
                    check(xrEndSession(session_), "end session");
                    log("Session suspended");
                }
                if (state.state == XR_SESSION_STATE_EXITING || state.state == XR_SESSION_STATE_LOSS_PENDING)
                    quit_ = true;
            } else if (event.type == XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING)
                quit_ = true;
            else if (event.type == XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING) {
                auto &change = *reinterpret_cast<XrEventDataReferenceSpaceChangePending *>(&event);
                if (change.referenceSpaceType == XR_REFERENCE_SPACE_TYPE_LOCAL) {
                    recenterAfter_ = change.changeTime;
                    ui_.recenterRequested = true;
                }
            }
            event = {XR_TYPE_EVENT_DATA_BUFFER};
        }
        if (result != XR_EVENT_UNAVAILABLE)
            check(result, "poll events");
    }
    void recenter(XrTime time) {
        XrSpaceLocation head{XR_TYPE_SPACE_LOCATION};
        check(xrLocateSpace(view_, local_, time, &head), "locate head");
        constexpr XrSpaceLocationFlags valid =
            XR_SPACE_LOCATION_POSITION_VALID_BIT | XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
        if ((head.locationFlags & valid) != valid)
            return;
        panel_ = placePanel(poseOf(head.pose), ui_.settings.distance, ui_.settings.scale);
        debugPanel_ = panel_;
        positionAccessories();
        ui_.recenterRequested = false;
        recenterAfter_ = 0;
        panelPlaced_ = true;
    }
    void positionAccessories() {
        float scale = panel_.width / 1.2f;
        debugPanel_ = panel_;
        debugPanel_.width = .60f * scale;
        debugPanel_.height = debugPanel_.width * 760 / 620;
        debugPanel_.pixelsW = 620;
        debugPanel_.pixelsH = 760;
        debugPanel_.pose.position =
            panel_.pose.position + rotate(panel_.pose.orientation, {.99f * scale, -.015f, -.10f * scale});
        float yaw = std::atan2(panel_.pose.orientation.y, panel_.pose.orientation.w) * 2;
        debugPanel_.pose.orientation = yawRotation(yaw - .38f);
        dockPanel_ = panel_;
        dockPanel_.width = panel_.width * dockWidth / 1440.f;
        dockPanel_.height = panel_.height * dockHeight / 990.f;
        dockPanel_.pixelsW = dockWidth;
        dockPanel_.pixelsH = dockHeight;
        dockPanel_.pose.position =
            panel_.pose.position +
            rotate(panel_.pose.orientation,
                   {0, -panel_.height * (dockOriginY + dockHeight * .5f - 495) / 990.f, .008f});
    }
    void updatePlacement(XrTime time, float dt) {
        XrSpaceLocation location{XR_TYPE_SPACE_LOCATION};
        constexpr auto valid = XR_SPACE_LOCATION_POSITION_VALID_BIT | XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
        headValid_ = XR_SUCCEEDED(xrLocateSpace(view_, local_, time, &location)) &&
                     (location.locationFlags & valid) == valid;
        if (!headValid_)
            return;
        headPose_ = poseOf(location.pose);
        if (panelPlaced_ && !ui_.interacting() && windowDragIndex_ < 0) {
            Pose target = panel_.pose;
            if (ui_.settings.follow == FollowMode::FollowView) {
                target = placePanel(headPose_, ui_.settings.distance, ui_.settings.scale).pose;
                Vec3 forward = rotate(headPose_.orientation, {0, 0, -1});
                forward.y = std::clamp(forward.y, -.45f, .45f);
                forward = normalized(forward);
                target.position = headPose_.position + forward * ui_.settings.distance + Vec3{0, -.1f, 0};
            }
            if (ui_.settings.follow != FollowMode::World) {
                target.orientation = faceViewer(target.position, headPose_.position);
                panel_.pose =
                    approachPose(panel_.pose, target, dt * (ui_.settings.reducedMotion ? .4f : 1.f));
            }
        }
        positionAccessories();
    }
    bool booleanAction(XrAction action) {
        XrActionStateGetInfo get{XR_TYPE_ACTION_STATE_GET_INFO};
        get.action = action;
        XrActionStateBoolean state{XR_TYPE_ACTION_STATE_BOOLEAN};
        check(xrGetActionStateBoolean(session_, &get, &state), "button state");
        return state.isActive && state.changedSinceLastSync && state.currentState;
    }
    bool trackedHand(int index, XrTime time, bool &down) {
        if (!handTrackers_[index])
            return false;
        std::array<XrHandJointLocationEXT, XR_HAND_JOINT_COUNT_EXT> joints{};
        XrHandTrackingAimStateFB aim{XR_TYPE_HAND_TRACKING_AIM_STATE_FB};
        XrHandTrackingScaleFB scale{XR_TYPE_HAND_TRACKING_SCALE_FB};
        XrHandJointLocationsEXT locations{XR_TYPE_HAND_JOINT_LOCATIONS_EXT};
        locations.jointCount = uint32_t(joints.size());
        locations.jointLocations = joints.data();
        if (handAimEnabled_)
            locations.next = &aim;
        if (handMeshEnabled_) {
            scale.next = locations.next;
            locations.next = &scale;
        }
        XrHandJointsLocateInfoEXT info{XR_TYPE_HAND_JOINTS_LOCATE_INFO_EXT};
        info.baseSpace = local_;
        info.time = time;
        XrResult result = locateHand_(handTrackers_[index], &info, &locations);
        if (XR_FAILED(result) || !locations.isActive)
            return false;
        auto valid = [&](int j) {
            return bool(joints[j].locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT);
        };
        auto position = [&](int j) { return poseOf(joints[j].pose).position; };
        HandJoints rig;
        constexpr auto jointValid =
            XR_SPACE_LOCATION_POSITION_VALID_BIT | XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
        for (int j = 0; j < 26; j++)
            rig[j] = {poseOf(joints[j].pose), joints[j].radius,
                      (joints[j].locationFlags & jointValid) == jointValid};
        if (ui_.settings.handModel != HandModel::Hidden) {
            bool skinned = ui_.settings.handModel == HandModel::Automatic &&
                           !handSkins_[index].vertices.empty() &&
                           spatial_.skinnedHand(handSkins_[index], rig, Color::hex(0xcbdcde),
                                                scale.currentOutput > 0 ? scale.currentOutput : 1);
            if (!skinned)
                spatial_.trackedHand(rig, ui_.settings.handModel);
        }
        if (!handReported_[index]) {
            handReported_[index] = true;
            log(index == 0 ? "Left hand tracked" : "Right hand tracked");
        }
        if (handAimEnabled_) {
            if (!(aim.status & XR_HAND_TRACKING_AIM_VALID_BIT_FB) ||
                (aim.status & XR_HAND_TRACKING_AIM_SYSTEM_GESTURE_BIT_FB))
                return false;
            Pose pose = poseOf(aim.aimPose);
            rayOrigin_[index] = pose.position;
            rayDirection_[index] = rotate(pose.orientation, {0, 0, -1});
            down = aim.pinchStrengthIndex > (pressed_[index] ? .6f : .8f);
        } else {
            if (!valid(7) || !valid(10) || !valid(5))
                return false;
            rayOrigin_[index] = position(7);
            rayDirection_[index] = normalized(position(10) - position(7));
            down = length(position(10) - position(5)) < (pressed_[index] ? .034f : .024f);
        }
        return true;
    }
    void moveWindow(XrTime time) {
        int drag = ui_.dragPointer();
        if (drag >= 0 && rayValid_[drag]) {
            if (windowDragIndex_ != drag) {
                const auto &surface = ui_.captured(drag) == 600 ? dockPanel_ : panel_;
                Hit hit = intersectPanel(rayOrigin_[drag], rayDirection_[drag], surface, false);
                if (!hit.valid)
                    return;
                windowDragIndex_ = drag;
                windowDragDepth_ = hit.distance;
                windowDragOffset_ = panel_.pose.position - hit.world;
                if (ui_.settings.follow == FollowMode::FollowView) {
                    ui_.settings.follow = FollowMode::World;
                    ui_.saveRequested = true;
                    ui_.invalidate();
                }
                log("Moving window");
            }
            Vec3 position = rayOrigin_[drag] + rayDirection_[drag] * windowDragDepth_ + windowDragOffset_;
            XrSpaceLocation head{XR_TYPE_SPACE_LOCATION};
            if (XR_SUCCEEDED(xrLocateSpace(view_, local_, time, &head)) &&
                (head.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT)) {
                Vec3 h = poseOf(head.pose).position, offset = position - h;
                float distance = length(offset);
                if (distance < .8f || distance > 3.f)
                    position = h + normalized(offset) * std::clamp(distance, .8f, 3.f);
            }
            panel_.pose.position = position;
            positionAccessories();
        } else if (windowDragIndex_ >= 0) {
            windowDragIndex_ = -1;
            if (headValid_)
                panel_.pose.orientation = faceViewer(panel_.pose.position, headPose_.position);
            positionAccessories();
            ui_.notify("toast.window_placed");
        }
    }
    void input(XrTime time) {
        spatial_.clear();
        rayValid_.fill(false);
        if (!focused_) {
            for (int i = 0; i < 2; i++) {
                hits_[i] = {};
                pressed_[i] = false;
                ui_.pointer(i, {-100, -100}, false);
            }
            windowDragIndex_ = -1;
            return;
        }
        XrActiveActionSet active{actionSet_, XR_NULL_PATH};
        XrActionsSyncInfo sync{XR_TYPE_ACTIONS_SYNC_INFO};
        sync.countActiveActionSets = 1;
        sync.activeActionSets = &active;
        XrResult result = xrSyncActions(session_, &sync);
        check(result, "sync actions");
        if (focused_ && booleanAction(recenter_))
            ui_.recenterRequested = true;
        if (focused_ && booleanAction(debug_)) {
            ui_.debugVisible = !ui_.debugVisible;
            ui_.invalidate();
        }
        for (int i = 0; i < 2; i++) {
            hits_[i] = {};
            XrActionStateGetInfo get{XR_TYPE_ACTION_STATE_GET_INFO};
            get.action = aim_;
            get.subactionPath = hands_[i];
            XrActionStatePose aim{XR_TYPE_ACTION_STATE_POSE};
            check(xrGetActionStatePose(session_, &get, &aim), "aim state");
            get.action = trigger_;
            XrActionStateFloat trigger{XR_TYPE_ACTION_STATE_FLOAT};
            check(xrGetActionStateFloat(session_, &get, &trigger), "trigger state");
            bool down = focused_ && trigger.isActive && trigger.currentState > (pressed_[i] ? .35f : .55f);
            constexpr XrSpaceLocationFlags valid =
                XR_SPACE_LOCATION_POSITION_VALID_BIT | XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
            if (aim.isActive) {
                XrSpaceLocation location{XR_TYPE_SPACE_LOCATION};
                check(xrLocateSpace(aimSpaces_[i], local_, time, &location), "locate pointer");
                if ((location.locationFlags & valid) == valid) {
                    Pose aimPose = poseOf(location.pose);
                    rayOrigin_[i] = aimPose.position;
                    rayDirection_[i] = rotate(aimPose.orientation, {0, 0, -1});
                    rayValid_[i] = true;
                    XrSpaceLocation grip{XR_TYPE_SPACE_LOCATION};
                    check(xrLocateSpace(gripSpaces_[i], local_, time, &grip), "locate hand");
                    if ((grip.locationFlags & valid) == valid)
                        spatial_.controllerHand(poseOf(grip.pose), i == 0, trigger.currentState,
                                                ui_.settings.handModel, ui_.settings.handScale,
                                                ui_.settings.handPitch, &controllerSurface_);
                }
            }
            if (!rayValid_[i]) {
                down = false;
                rayValid_[i] = trackedHand(i, time, down);
            }
            if (!rayValid_[i])
                down = false;
            Hit planeHit = pointerHit(i, true);
            ui_.pointer(i, planeHit.valid ? planeHit.pixels : Vec2{-100, -100}, down);
            pressed_[i] = down;
            if (ui_.hapticRequested[i]) {
                ui_.hapticRequested[i] = false;
                XrHapticActionInfo info{XR_TYPE_HAPTIC_ACTION_INFO};
                info.action = haptic_;
                info.subactionPath = hands_[i];
                XrHapticVibration vibration{XR_TYPE_HAPTIC_VIBRATION};
                vibration.duration = 25'000'000;
                vibration.amplitude = .18f;
                vibration.frequency = XR_FREQUENCY_UNSPECIFIED;
                XrResult h = xrApplyHapticFeedback(session_, &info,
                                                   reinterpret_cast<XrHapticBaseHeader *>(&vibration));
                if (XR_FAILED(h))
                    __android_log_print(ANDROID_LOG_WARN, "CyVR", "Haptic unavailable: %d", h);
            }
        }
        moveWindow(time);
        for (int i = 0; i < 2; i++)
            if (rayValid_[i]) {
                hits_[i] = pointerHit(i, false);
                Vec3 end = hits_[i].valid ? hits_[i].world : rayOrigin_[i] + rayDirection_[i] * 2.5f;
                if (ui_.settings.rays)
                    spatial_.bone(rayOrigin_[i] + rayDirection_[i] * .025f, end, .0010f,
                                  Color::hex(pressed_[i] ? 0xc9fff0 : 0x86e7cf, .9f), 6);
            }
    }
    Hit pointerHit(int i, bool capture) {
        if (!rayValid_[i] || !panelPlaced_)
            return {};
        int id = ui_.captured(i);
        bool inDock = id >= 600 && id != 605;
        Hit main;
        if (ui_.windowState == WindowState::Open && !inDock)
            main = intersectPanel(rayOrigin_[i], rayDirection_[i], panel_, !(capture && id));
        Hit dock = intersectPanel(rayOrigin_[i], rayDirection_[i], dockPanel_, !(capture && inDock && id));
        if (dock.valid && (!main.valid || dock.distance < main.distance)) {
            dock.pixels.x += dockOriginX;
            dock.pixels.y += dockOriginY;
            hitPanels_[i] = dockPanel_;
            return dock;
        }
        hitPanels_[i] = panel_;
        return main;
    }
    bool projectSpatial(XrTime time, std::array<XrCompositionLayerProjectionView, 2> &output) {
        if (spatial_.vertices.empty() && ui_.settings.background == Background::Passthrough)
            return false;
        std::array<XrView, 2> views{{{XR_TYPE_VIEW}, {XR_TYPE_VIEW}}};
        XrViewLocateInfo locate{XR_TYPE_VIEW_LOCATE_INFO};
        locate.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
        locate.displayTime = time;
        locate.space = local_;
        XrViewState state{XR_TYPE_VIEW_STATE};
        uint32_t count = 0;
        check(xrLocateViews(session_, &locate, &state, 2, &count, views.data()), "locate stereo views");
        if (count != 2 || (state.viewStateFlags &
                           (XR_VIEW_STATE_POSITION_VALID_BIT | XR_VIEW_STATE_ORIENTATION_VALID_BIT)) !=
                              (XR_VIEW_STATE_POSITION_VALID_BIT | XR_VIEW_STATE_ORIENTATION_VALID_BIT))
            return false;
        for (int i = 0; i < 2; i++) {
            auto &layer = eyes_[i];
            uint32_t image;
            XrSwapchainImageAcquireInfo acquire{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
            check(xrAcquireSwapchainImage(layer.swapchain, &acquire, &image), "acquire hands");
            XrSwapchainImageWaitInfo wait{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
            wait.timeout = XR_INFINITE_DURATION;
            check(xrWaitSwapchainImage(layer.swapchain, &wait), "wait hands");
            auto &fov = views[i].fov;
            auto environment = environmentView(poseOf(views[i].pose), fov.angleLeft, fov.angleRight,
                                               fov.angleUp, fov.angleDown, ui_.settings);
            renderer_->drawSpatial(layer.targets.at(image), spatial_,
                                   viewProjection(poseOf(views[i].pose), fov.angleLeft, fov.angleRight,
                                                  fov.angleUp, fov.angleDown),
                                   &environment);
            XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
            check(xrReleaseSwapchainImage(layer.swapchain, &release), "release hands");
            output[i] = {XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW};
            output[i].pose = views[i].pose;
            output[i].fov = fov;
            output[i].subImage.swapchain = layer.swapchain;
            output[i].subImage.imageRect = {{0, 0}, {int32_t(layer.width), int32_t(layer.height)}};
        }
        return true;
    }
    XrCompositionLayerQuad quad(const Layer &layer, const PanelGeometry &geometry) {
        XrCompositionLayerQuad out{XR_TYPE_COMPOSITION_LAYER_QUAD};
        out.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
        out.space = local_;
        out.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
        out.subImage.swapchain = layer.swapchain;
        out.subImage.imageRect = {{0, 0}, {int32_t(layer.width), int32_t(layer.height)}};
        out.pose = xrPose(geometry.pose);
        out.size = {geometry.width, geometry.height};
        return out;
    }
    void frame() {
        XrFrameWaitInfo wait{XR_TYPE_FRAME_WAIT_INFO};
        XrFrameState state{XR_TYPE_FRAME_STATE};
        check(xrWaitFrame(session_, &wait, &state), "wait frame");
        XrFrameBeginInfo begin{XR_TYPE_FRAME_BEGIN_INFO};
        check(xrBeginFrame(session_, &begin), "begin frame");
        auto before = std::chrono::steady_clock::now();
        double seconds = std::chrono::duration<double>(before - clock_).count();
        ui_.stats.sample(std::chrono::duration<double, std::milli>(before - lastFrame_).count());
        lastFrame_ = before;
        if (state.predictedDisplayPeriod > 0)
            ui_.stats.displayHz = 1e9 / state.predictedDisplayPeriod;
        ui_.tick(seconds);
        if (focused_ && state.shouldRender && ui_.recenterRequested &&
            state.predictedDisplayTime >= recenterAfter_)
            recenter(state.predictedDisplayTime);
        updatePlacement(state.predictedDisplayTime,
                        float(std::min(.1, ui_.stats.count ? 1 / ui_.stats.fps() : 1 / 72.)));
        input(state.predictedDisplayTime);
        updateBackground();
        if (ui_.saveRequested) {
            ui_.saveRequested = false;
            if (!saveSettings(storage_ + "/preferences.txt", ui_.settings))
                ui_.notify("toast.save_failed");
        }
        if (requestRate_ && lastRequested_ != ui_.settings.requestedHz) {
            lastRequested_ = ui_.settings.requestedHz;
            if (std::find(ui_.refreshRates.begin(), ui_.refreshRates.end(), lastRequested_) !=
                ui_.refreshRates.end()) {
                XrResult r = requestRate_(session_, lastRequested_);
                if (XR_FAILED(r))
                    log("Requested refresh rate unavailable");
            }
        }
        if (seconds - lastStats_ > .5) {
            lastStats_ = seconds;
            std::ifstream memory("/proc/self/statm");
            long total = 0, resident = 0;
            if (memory >> total >> resident)
                ui_.stats.rssMiB = resident * double(sysconf(_SC_PAGESIZE)) / 1048576.;
            if (ui_.page == Page::Diagnostics || ui_.debugVisible)
                ui_.invalidate();
        }
        if (ui_.exportRequested) {
            ui_.exportRequested = false;
            bool written = exportDiagnostic(storage_ + "/diagnostic.json", ui_);
            ui_.notify(written ? "toast.exported_quest" : "toast.export_failed");
        }
        std::array<XrCompositionLayerQuad, 8> quads{};
        std::array<const XrCompositionLayerBaseHeader *, 8> layers{};
        uint32_t count = 0;
        std::array<XrCompositionLayerProjectionView, 2> projectedViews{};
        XrCompositionLayerProjection projection{XR_TYPE_COMPOSITION_LAYER_PROJECTION};
        projection.space = local_;
        projection.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
        projection.viewCount = 2;
        projection.views = projectedViews.data();
        XrCompositionLayerPassthroughFB passthrough{XR_TYPE_COMPOSITION_LAYER_PASSTHROUGH_FB};
        passthrough.layerHandle = passthroughLayer_;
        if (state.shouldRender && panelPlaced_) {
            if (passthroughRunning_)
                layers[count++] = reinterpret_cast<XrCompositionLayerBaseHeader *>(&passthrough);
            if (projectSpatial(state.predictedDisplayTime, projectedViews))
                layers[count++] = reinterpret_cast<XrCompositionLayerBaseHeader *>(&projection);
            bool changed = drawnRevision_ != ui_.revision;
            if (changed || !mainLayer_.initialized) {
                ui_.drawMain(mainDraw_);
                ui_.drawDock(dockDraw_);
                drawnRevision_ = ui_.revision;
            }
            if (dockRevision_ != ui_.revision || !dockLayer_.initialized) {
                render(dockLayer_, dockDraw_);
                dockRevision_ = ui_.revision;
            }
            if (ui_.windowState == WindowState::Open) {
                auto glass = glassView(headPose_, panel_, ui_.settings);
                auto orientationDelta = [](Quat a, Quat b) {
                    return 1 - std::abs(a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w);
                };
                bool moved = length(glassEye_.position - headPose_.position) > .002f ||
                             length(glassPanel_.position - panel_.pose.position) > .002f ||
                             orientationDelta(glassPanel_.orientation, panel_.pose.orientation) > .00001f;
                if (changed || !mainLayer_.initialized ||
                    (glass.options[3] > 0 && moved && seconds - lastGlassRender_ > 1 / 30.)) {
                    render(mainLayer_, mainDraw_, &glass);
                    lastGlassRender_ = seconds;
                    glassEye_ = headPose_;
                    glassPanel_ = panel_.pose;
                }
                quads[count] = quad(mainLayer_, panel_);
                layers[count] = reinterpret_cast<XrCompositionLayerBaseHeader *>(&quads[count]);
                count++;
            }
            visibleFrames_++;
            if (ui_.debugVisible && ui_.windowState == WindowState::Open) {
                if (seconds - lastDebugRender_ >= .5 || !debugLayer_.initialized) {
                    ui_.drawDebug(debugDraw_);
                    render(debugLayer_, debugDraw_);
                    lastDebugRender_ = seconds;
                }
                quads[count] = quad(debugLayer_, debugPanel_);
                layers[count] = reinterpret_cast<XrCompositionLayerBaseHeader *>(&quads[count]);
                count++;
            }
            quads[count] = quad(dockLayer_, dockPanel_);
            layers[count] = reinterpret_cast<XrCompositionLayerBaseHeader *>(&quads[count]);
            count++;
            for (int i = 0; i < 2; i++)
                if (hits_[i].valid) {
                    PanelGeometry cursor;
                    cursor.pose = hitPanels_[i].pose;
                    cursor.pose.position =
                        hits_[i].world + rotate(hitPanels_[i].pose.orientation, {0, 0, .003f});
                    float target = ui_.hovered() ? .016f : .012f;
                    cursorSize_[i] += (target - cursorSize_[i]) * (ui_.settings.reducedMotion ? 1.f : .18f);
                    cursor.width = cursor.height = cursorSize_[i] * ui_.settings.distance / 1.35f;
                    quads[count] = quad(cursorLayer_, cursor);
                    layers[count] = reinterpret_cast<XrCompositionLayerBaseHeader *>(&quads[count]);
                    count++;
                }
        }
        ui_.stats.cpuMs =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - before).count();
        XrFrameEndInfo end{XR_TYPE_FRAME_END_INFO};
        end.displayTime = state.predictedDisplayTime;
        end.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
        end.layerCount = count;
        end.layers = layers.data();
        check(xrEndFrame(session_, &end), "end frame");
        if (!startupReport_ && focused_ && visibleFrames_ >= 240) {
            startupReport_ = true;
            exportDiagnostic(storage_ + "/startup-diagnostic.json", ui_);
            log("240 UI frames submitted / startup diagnostics saved");
        }
    }
};
} // namespace
void runCyVR(android_app *android) {
    cyvr::runAndroidApp(android, {});
}
void cyvr::runAndroidApp(android_app *android, const AndroidAppConfig &config) {
    QuestApp app(android, config);
    app.run();
}
