// SPDX-License-Identifier: MIT
#pragma once
#define VK_NO_PROTOTYPES
#if defined(__ANDROID__) && !defined(VK_USE_PLATFORM_ANDROID_KHR)
#define VK_USE_PLATFORM_ANDROID_KHR
#endif
#include "cyvr/core.hpp"
#include "cyvr/pbr.hpp"
#include "cyvr/scene.hpp"
#include "cyvr/spatial.hpp"
#include <functional>
#include <stdexcept>
#include <vulkan/vulkan.h>

namespace cyvr {
#define CYVR_VK(name) extern PFN_##name name;
#include "cyvr/vulkan_functions.inc"
#undef CYVR_VK
void loadVulkan();
void vkCheck(VkResult result, const char *operation);
struct VulkanContext {
    VkInstance instance{};
    VkPhysicalDevice physical{};
    VkDevice device{};
    VkQueue queue{};
    uint32_t queueFamily{};
    VulkanContext() = default;
    VulkanContext(const VulkanContext &) = delete;
    VulkanContext &operator=(const VulkanContext &) = delete;
    ~VulkanContext();
    void createStandalone();
    uint32_t memoryType(uint32_t mask, VkMemoryPropertyFlags flags) const;
    void selectQueue();
};
using AssetReader = std::function<std::vector<uint8_t>(const std::string &)>;
struct Target {
    VkImage image{};
    VkDeviceMemory memory{};
    VkImageView view{};
    VkFramebuffer framebuffer{};
    VkImage depthImage{};
    VkDeviceMemory depthMemory{};
    VkImageView depthView{};
    uint32_t width{}, height{};
};
struct EnvironmentView {
    std::array<float, 4> origin{0,0,0,1}, forward{0, 0, -1, 0}, right{1, 0, 0, 0}, up{0, 1, 0, 0};
    // background, brightness, blur angular radius, alpha; then mask aspect/radius/panel.
    std::array<float, 4> options{0, .65f, 0, 1}, shape{};
};
EnvironmentView environmentView(Pose eye, float left, float right, float up, float down,
                                const Settings &settings);
EnvironmentView glassView(Pose eye, const PanelGeometry &panel, const Settings &settings);
class VulkanRenderer {
  public:
    explicit VulkanRenderer(VulkanContext &context) : ctx_(context) {}
    ~VulkanRenderer();
    VulkanRenderer(const VulkanRenderer &) = delete;
    VulkanRenderer &operator=(const VulkanRenderer &) = delete;
    void initialize(VkFormat format, const AssetReader &read);
    VkFormat colorFormat() const { return format_; }
    // Optional photographic environments; missing assets retain the procedural fallback.
    void loadEnvironments(const AssetReader &read);
    bool photographicEnvironments() const { return sceneMask_ == 3; }
    // Upload original 3D geometry only when the selected scene changes. Anchor is floor-relative.
    void setScene(Background background, Pose anchor = {}, const AssetReader &read = {});
    uint32_t sceneVertexCount() const { return sceneVertexCount_; }
    size_t sceneTextureBytes() const { return pbrBytes_; }
    bool bakedSceneLighting() const { return bakedSceneLighting_; }
    void uploadImages(uint32_t width, uint32_t height, const std::vector<uint8_t> &rgba);
    // Borrowed texture; retain it until all draw() calls using it have completed.
    // Null handles restore the owned image atlas. No upload or CPU readback.
    void setImageTexture(VkImageView view = VK_NULL_HANDLE, VkSampler sampler = VK_NULL_HANDLE);
    void setVideoTexture(VkImageView view = VK_NULL_HANDLE, VkSampler sampler = VK_NULL_HANDLE);
    void setOverlayTexture(VkImageView view = VK_NULL_HANDLE, VkSampler sampler = VK_NULL_HANDLE);
    void useImageAtlasForVideo() { setVideoTexture(images_.view, sampler_); }
    VkImageView imageAtlasView() const { return images_.view; }
    VkSampler imageSampler() const { return sampler_; }
    Target createTarget(uint32_t width, uint32_t height, VkImage external = VK_NULL_HANDLE,
                        bool depth = false, VkImage externalDepth = VK_NULL_HANDLE);
    void destroyTarget(Target &target);
    void draw(const Target &target, const DrawList &commands, const EnvironmentView *glass = nullptr,
              bool present=false, Vec2 logicalSize = {});
    void drawSpatial(const Target &target, const SpatialMesh &mesh, const Mat4 &matrix,
                     const EnvironmentView *environment = nullptr);
    std::vector<uint8_t> readback(const Target &target);
    uint32_t lastVertexCount{};

  private:
    VulkanContext &ctx_;
    VkFormat format_{};
    VkFormat depthFormat_{VK_FORMAT_D32_SFLOAT};
    VkCommandPool commandPool_{};
    VkCommandBuffer command_{};
    VkFence fence_{};
    VkBuffer vertexBuffer_{};
    VkDeviceMemory vertexMemory_{};
    void *vertices_{};
    Target atlas_, glass_, images_, scenes_[2], sceneGlass_;
    VkImageView videoView_{}, overlayView_{}; // Current bindings 3 and 4.
    VkSampler videoSampler_{}, overlaySampler_{};
    Target materialColor_, materialSurface_, probe_, brdf_;
    VkSampler materialSampler_{}, probeSampler_{};
    size_t pbrBytes_{}, probeBytes_{};
    bool bakedSceneLighting_{};
    VkBuffer sceneBuffer_{};
    VkDeviceMemory sceneMemory_{};
    uint32_t sceneVertexCount_{}, sceneClosedVertexCount_{};
    Background sceneKind_{Background::Passthrough};
    Pose sceneAnchor_{};
    VkSampler sampler_{}, sceneSampler_{};
    uint32_t sceneMask_{};
    void uploadPanorama(Target &target, const std::vector<uint8_t> &jpeg);
    void uploadPbr(Target &target, const PbrTexture &texture, uint32_t binding);
    void loadPbr(Background background, const AssetReader &read);
    VkRenderPass renderPass_{};
    VkRenderPass spatialPass_{}, spatialDiscardPass_{}; // Discard: depth not read after the pass.
    VkDescriptorSetLayout setLayout_{};
    VkDescriptorPool descriptorPool_{};
    VkDescriptorSet descriptor_{};
    VkPipelineLayout pipelineLayout_{};
    VkPipeline pipeline_{};
    VkPipelineLayout spatialLayout_{};
    VkPipeline spatialPipeline_{};
    VkPipeline scenePipeline_{}, sceneOpenPipeline_{}, sceneBlurPipeline_{};
    VkPipelineLayout sceneLayout_{};
    VkPipelineLayout environmentLayout_{};
    VkPipeline environmentPipeline_{}, spatialEnvironmentPipeline_{}, glassPipeline_{};
    static constexpr size_t maxVertices = 180000;
    void environment(const Target &target, const EnvironmentView &view, bool spatial);
    void drawScene(const Mat4 &matrix, const EnvironmentView &view);
    void buffer(VkDeviceSize size, VkBufferUsageFlags usage, VkBuffer &buffer, VkDeviceMemory &memory);
    void image(uint32_t width, uint32_t height, VkFormat format, VkImageUsageFlags usage, Target &target,
               uint32_t levels = 1, uint32_t layers = 1, bool cube = false);
    void begin();
    void submit();
};
} // namespace cyvr
