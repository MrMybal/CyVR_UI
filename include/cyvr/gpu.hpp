// SPDX-License-Identifier: MIT
#pragma once
#define VK_NO_PROTOTYPES
#if defined(__ANDROID__) && !defined(VK_USE_PLATFORM_ANDROID_KHR)
#define VK_USE_PLATFORM_ANDROID_KHR
#endif
#include "cyvr/core.hpp"
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
    std::array<float, 4> origin{}, forward{0, 0, -1, 0}, right{1, 0, 0, 0}, up{0, 1, 0, 0};
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
    void uploadImages(uint32_t width, uint32_t height, const std::vector<uint8_t> &rgba);
    Target createTarget(uint32_t width, uint32_t height, VkImage external = VK_NULL_HANDLE,
                        bool depth = false);
    void destroyTarget(Target &target);
    void draw(const Target &target, const DrawList &commands, const EnvironmentView *glass = nullptr,
              bool present=false);
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
    Target atlas_, glass_, images_;
    VkSampler sampler_{};
    VkRenderPass renderPass_{};
    VkRenderPass spatialPass_{};
    VkDescriptorSetLayout setLayout_{};
    VkDescriptorPool descriptorPool_{};
    VkDescriptorSet descriptor_{};
    VkPipelineLayout pipelineLayout_{};
    VkPipeline pipeline_{};
    VkPipelineLayout spatialLayout_{};
    VkPipeline spatialPipeline_{};
    VkPipelineLayout environmentLayout_{};
    VkPipeline environmentPipeline_{}, spatialEnvironmentPipeline_{}, glassPipeline_{};
    static constexpr size_t maxVertices = 180000;
    void environment(const Target &target, const EnvironmentView &view, bool spatial);
    void buffer(VkDeviceSize size, VkBufferUsageFlags usage, VkBuffer &buffer, VkDeviceMemory &memory);
    void image(uint32_t width, uint32_t height, VkFormat format, VkImageUsageFlags usage, Target &target);
    void begin();
    void submit();
};
} // namespace cyvr
