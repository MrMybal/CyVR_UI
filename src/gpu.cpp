// SPDX-License-Identifier: MIT
#include "cyvr/gpu.hpp"
#include <cstring>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace cyvr {
EnvironmentView environmentView(Pose eye, float left, float right, float up, float down, const Settings &s) {
    EnvironmentView e;
    auto vector = [](Vec3 v) { return std::array<float, 4>{v.x, v.y, v.z, 0}; };
    float l = std::tan(left), r = std::tan(right), u = std::tan(up), d = std::tan(down);
    e.origin = vector(eye.position);
    e.forward = vector(rotate(eye.orientation, {(r + l) * .5f, (u + d) * .5f, -1}));
    e.right = vector(rotate(eye.orientation, {(r - l) * .5f, 0, 0}));
    e.up = vector(rotate(eye.orientation, {0, (u - d) * .5f, 0}));
    e.options = {float(int(s.background)), s.brightness, 0, 1};
    return e;
}
EnvironmentView glassView(Pose eye, const PanelGeometry &p, const Settings &s) {
    auto vector = [](Vec3 v) { return std::array<float, 4>{v.x, v.y, v.z, 0}; };
    EnvironmentView e;
    e.origin = vector(eye.position);
    e.forward = vector(p.pose.position - eye.position);
    e.right = vector(rotate(p.pose.orientation, {p.width * .5f, 0, 0}));
    e.up = vector(rotate(p.pose.orientation, {0, p.height * .5f, 0}));
    bool active =
        s.background != Background::Passthrough && s.blur > .001f && s.opacity < .999f && !s.highContrast;
    e.options = {float(int(s.background)), s.brightness, s.blur * .11f, active ? 1.f : 0.f};
    e.shape = {float(p.pixelsW) / p.pixelsH, 30.f / p.pixelsH, 1, 0};
    return e;
}
#define CYVR_VK(name) PFN_##name name = nullptr;
#include "cyvr/vulkan_functions.inc"
#undef CYVR_VK
void vkCheck(VkResult result, const char *operation) {
    if (result != VK_SUCCESS)
        throw std::runtime_error(std::string(operation) + " / Vulkan error " + std::to_string(result));
}
void loadVulkan() {
    if (vkGetInstanceProcAddr)
        return;
#ifdef _WIN32
    static HMODULE library = LoadLibraryW(L"vulkan-1.dll");
    if (!library)
        throw std::runtime_error("Vulkan is unavailable. Install the graphics driver from your GPU manufacturer.");
    auto address = [&](const char *name) { return reinterpret_cast<void *>(GetProcAddress(library, name)); };
#else
    static void *library = dlopen("libvulkan.so", RTLD_NOW | RTLD_LOCAL);
    if (!library)
        throw std::runtime_error("Vulkan loader unavailable");
    auto address = [&](const char *name) { return dlsym(library, name); };
#endif
#define CYVR_VK(name)                                                                                        \
    name = reinterpret_cast<PFN_##name>(address(#name));                                                     \
    if (!name)                                                                                               \
        throw std::runtime_error("Vulkan function unavailable: " #name);
#include "cyvr/vulkan_functions.inc"
#undef CYVR_VK
}
VulkanContext::~VulkanContext() {
    if (device) {
        vkDeviceWaitIdle(device);
        vkDestroyDevice(device, nullptr);
    }
    if (instance)
        vkDestroyInstance(instance, nullptr);
}
void VulkanContext::selectQueue() {
    uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &count, nullptr);
    std::vector<VkQueueFamilyProperties> families(count);
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &count, families.data());
    for (uint32_t i = 0; i < count; i++)
        if (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            queueFamily = i;
            return;
        }
    throw std::runtime_error("No graphics queue");
}
void VulkanContext::createStandalone() {
    loadVulkan();
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "CyVR UI Reference";
    app.apiVersion = VK_API_VERSION_1_1;
    VkInstanceCreateInfo info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    info.pApplicationInfo = &app;
    vkCheck(vkCreateInstance(&info, nullptr, &instance), "create instance");
    uint32_t count = 0;
    vkCheck(vkEnumeratePhysicalDevices(instance, &count, nullptr), "enumerate GPU");
    if (!count)
        throw std::runtime_error("No Vulkan GPU");
    std::vector<VkPhysicalDevice> devices(count);
    vkCheck(vkEnumeratePhysicalDevices(instance, &count, devices.data()), "enumerate GPU");
    physical = devices.front();
    selectQueue();
    float priority = 1;
    VkDeviceQueueCreateInfo queueInfo{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    queueInfo.queueFamilyIndex = queueFamily;
    queueInfo.queueCount = 1;
    queueInfo.pQueuePriorities = &priority;
    VkDeviceCreateInfo deviceInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    deviceInfo.queueCreateInfoCount = 1;
    deviceInfo.pQueueCreateInfos = &queueInfo;
    vkCheck(vkCreateDevice(physical, &deviceInfo, nullptr, &device), "create device");
    vkGetDeviceQueue(device, queueFamily, 0, &queue);
}
uint32_t VulkanContext::memoryType(uint32_t mask, VkMemoryPropertyFlags flags) const {
    VkPhysicalDeviceMemoryProperties props{};
    vkGetPhysicalDeviceMemoryProperties(physical, &props);
    for (uint32_t i = 0; i < props.memoryTypeCount; i++)
        if ((mask & (1u << i)) && (props.memoryTypes[i].propertyFlags & flags) == flags)
            return i;
    throw std::runtime_error("No compatible Vulkan memory type");
}
void VulkanRenderer::buffer(VkDeviceSize size, VkBufferUsageFlags usage, VkBuffer &out,
                            VkDeviceMemory &memory) {
    VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    info.size = size;
    info.usage = usage;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    vkCheck(vkCreateBuffer(ctx_.device, &info, nullptr, &out), "create buffer");
    VkMemoryRequirements req{};
    vkGetBufferMemoryRequirements(ctx_.device, out, &req);
    VkMemoryAllocateInfo alloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    alloc.allocationSize = req.size;
    alloc.memoryTypeIndex = ctx_.memoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                                    VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    vkCheck(vkAllocateMemory(ctx_.device, &alloc, nullptr, &memory), "allocate buffer");
    vkCheck(vkBindBufferMemory(ctx_.device, out, memory, 0), "bind buffer");
}
void VulkanRenderer::image(uint32_t w, uint32_t h, VkFormat format, VkImageUsageFlags usage, Target &t) {
    t.width = w;
    t.height = h;
    VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    info.imageType = VK_IMAGE_TYPE_2D;
    info.format = format;
    info.extent = {w, h, 1};
    info.mipLevels = 1;
    info.arrayLayers = 1;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = usage;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    vkCheck(vkCreateImage(ctx_.device, &info, nullptr, &t.image), "create image");
    VkMemoryRequirements req{};
    vkGetImageMemoryRequirements(ctx_.device, t.image, &req);
    VkMemoryAllocateInfo alloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    alloc.allocationSize = req.size;
    alloc.memoryTypeIndex = ctx_.memoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    vkCheck(vkAllocateMemory(ctx_.device, &alloc, nullptr, &t.memory), "allocate image");
    vkCheck(vkBindImageMemory(ctx_.device, t.image, t.memory, 0), "bind image");
}
void VulkanRenderer::begin() {
    vkCheck(vkWaitForFences(ctx_.device, 1, &fence_, VK_TRUE, 5'000'000'000ull), "wait rendering");
    vkCheck(vkResetFences(ctx_.device, 1, &fence_), "reset fence");
    vkCheck(vkResetCommandBuffer(command_, 0), "reset commands");
    VkCommandBufferBeginInfo info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkCheck(vkBeginCommandBuffer(command_, &info), "begin commands");
}
void VulkanRenderer::submit() {
    vkCheck(vkEndCommandBuffer(command_), "end commands");
    VkSubmitInfo info{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    info.commandBufferCount = 1;
    info.pCommandBuffers = &command_;
    vkCheck(vkQueueSubmit(ctx_.queue, 1, &info, fence_), "submit commands");
    vkCheck(vkWaitForFences(ctx_.device, 1, &fence_, VK_TRUE, 5'000'000'000ull), "finish rendering");
}
void VulkanRenderer::initialize(VkFormat format, const AssetReader &read) {
    format_ = format;
    VkCommandPoolCreateInfo pool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool.queueFamilyIndex = ctx_.queueFamily;
    vkCheck(vkCreateCommandPool(ctx_.device, &pool, nullptr, &commandPool_), "create command pool");
    VkCommandBufferAllocateInfo alloc{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    alloc.commandPool = commandPool_;
    alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc.commandBufferCount = 1;
    vkCheck(vkAllocateCommandBuffers(ctx_.device, &alloc, &command_), "allocate commands");
    VkFenceCreateInfo fence{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    fence.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    vkCheck(vkCreateFence(ctx_.device, &fence, nullptr, &fence_), "create fence");
    buffer(maxVertices * sizeof(Vertex), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, vertexBuffer_, vertexMemory_);
    vkCheck(vkMapMemory(ctx_.device, vertexMemory_, 0, VK_WHOLE_SIZE, 0, &vertices_), "map vertices");
    auto pixels = read("font_atlas.bin");
    if (pixels.size() != 1024 * 1024)
        throw std::runtime_error("Invalid font atlas");
    VkBuffer staging{};
    VkDeviceMemory stagingMemory{};
    buffer(pixels.size(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, staging, stagingMemory);
    void *mapped{};
    vkCheck(vkMapMemory(ctx_.device, stagingMemory, 0, VK_WHOLE_SIZE, 0, &mapped), "map atlas");
    std::memcpy(mapped, pixels.data(), pixels.size());
    vkUnmapMemory(ctx_.device, stagingMemory);
    image(1024, 1024, VK_FORMAT_R8_UNORM, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
          atlas_);
    begin();
    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = atlas_.image;
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdPipelineBarrier(command_, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0,
                         nullptr, 0, nullptr, 1, &barrier);
    VkBufferImageCopy copy{};
    copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    copy.imageExtent = {1024, 1024, 1};
    vkCmdCopyBufferToImage(command_, staging, atlas_.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(command_, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
                         0, nullptr, 0, nullptr, 1, &barrier);
    submit();
    vkDestroyBuffer(ctx_.device, staging, nullptr);
    vkFreeMemory(ctx_.device, stagingMemory, nullptr);
    VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    view.image = atlas_.image;
    view.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view.format = VK_FORMAT_R8_UNORM;
    view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCheck(vkCreateImageView(ctx_.device, &view, nullptr, &atlas_.view), "atlas view");
    VkSamplerCreateInfo sampler{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sampler.magFilter = sampler.minFilter = VK_FILTER_LINEAR;
    sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sampler.addressModeU = sampler.addressModeV = sampler.addressModeW =
        VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    vkCheck(vkCreateSampler(ctx_.device, &sampler, nullptr, &sampler_), "sampler");
    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo set{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    VkDescriptorSetLayoutBinding bindings[]{binding, binding, binding};
    bindings[1].binding = 1;
    bindings[2].binding = 2;
    set.bindingCount = 3;
    set.pBindings = bindings;
    vkCheck(vkCreateDescriptorSetLayout(ctx_.device, &set, nullptr, &setLayout_), "descriptor layout");
    VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 3};
    VkDescriptorPoolCreateInfo descriptors{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    descriptors.maxSets = 1;
    descriptors.poolSizeCount = 1;
    descriptors.pPoolSizes = &size;
    vkCheck(vkCreateDescriptorPool(ctx_.device, &descriptors, nullptr, &descriptorPool_), "descriptor pool");
    VkDescriptorSetAllocateInfo sets{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    sets.descriptorPool = descriptorPool_;
    sets.descriptorSetCount = 1;
    sets.pSetLayouts = &setLayout_;
    vkCheck(vkAllocateDescriptorSets(ctx_.device, &sets, &descriptor_), "descriptor set");
    VkDescriptorImageInfo texture{sampler_, atlas_.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = descriptor_;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &texture;
    vkUpdateDescriptorSets(ctx_.device, 1, &write, 0, nullptr);
    uploadImages(1,1,{255,255,255,255});
    VkAttachmentDescription attachment{};
    attachment.format = format;
    attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    VkAttachmentReference ref{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription sub{};
    sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.colorAttachmentCount = 1;
    sub.pColorAttachments = &ref;
    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    VkRenderPassCreateInfo pass{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    pass.attachmentCount = 1;
    pass.pAttachments = &attachment;
    pass.subpassCount = 1;
    pass.pSubpasses = &sub;
    pass.dependencyCount = 1;
    pass.pDependencies = &dependency;
    vkCheck(vkCreateRenderPass(ctx_.device, &pass, nullptr, &renderPass_), "render pass");
    VkFormatProperties depthProperties{};
    vkGetPhysicalDeviceFormatProperties(ctx_.physical, depthFormat_, &depthProperties);
    if (!(depthProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT))
        throw std::runtime_error("D32 depth attachment unavailable");
    VkAttachmentDescription attachments[]{attachment, {}};
    auto &depth = attachments[1];
    depth.format = depthFormat_;
    depth.samples = VK_SAMPLE_COUNT_1_BIT;
    depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depth.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depth.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    VkAttachmentReference depthRef{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    sub.pDepthStencilAttachment = &depthRef;
    pass.attachmentCount = 2;
    pass.pAttachments = attachments;
    dependency.srcStageMask |=
        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dependency.dstStageMask = dependency.srcStageMask;
    dependency.dstAccessMask |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    vkCheck(vkCreateRenderPass(ctx_.device, &pass, nullptr, &spatialPass_), "spatial depth pass");
    auto shader = [&](const char *name) {
        auto bytes = read(name);
        if (bytes.empty() || bytes.size() % 4)
            throw std::runtime_error("Invalid SPIR-V shader");
        std::vector<uint32_t> words(bytes.size() / 4);
        std::memcpy(words.data(), bytes.data(), bytes.size());
        VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        info.codeSize = bytes.size();
        info.pCode = words.data();
        VkShaderModule result{};
        vkCheck(vkCreateShaderModule(ctx_.device, &info, nullptr, &result), "shader");
        return result;
    };
    VkShaderModule vert = shader("ui.vert.spv"), frag = shader("ui.frag.spv");
    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vert;
    stages[0].pName = "main";
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = frag;
    stages[1].pName = "main";
    VkVertexInputBindingDescription vertexBinding{0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription attrs[] = {{0, 0, VK_FORMAT_R32G32_SFLOAT, 0},
                                                 {1, 0, VK_FORMAT_R32G32_SFLOAT, 8},
                                                 {2, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 16}};
    VkPipelineVertexInputStateCreateInfo vertex{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vertex.vertexBindingDescriptionCount = 1;
    vertex.pVertexBindingDescriptions = &vertexBinding;
    vertex.vertexAttributeDescriptionCount = 3;
    vertex.pVertexAttributeDescriptions = attrs;
    VkPipelineInputAssemblyStateCreateInfo assembly{
        VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewport.viewportCount = 1;
    viewport.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = VK_CULL_MODE_NONE;
    raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster.lineWidth = 1;
    VkPipelineMultisampleStateCreateInfo multisample{
        VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineColorBlendAttachmentState blend{};
    blend.blendEnable = VK_TRUE;
    blend.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    blend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blend.colorBlendOp = VK_BLEND_OP_ADD;
    blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blend.alphaBlendOp = VK_BLEND_OP_ADD;
    blend.colorWriteMask = 15;
    VkPipelineColorBlendStateCreateInfo blending{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    blending.attachmentCount = 1;
    blending.pAttachments = &blend;
    VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamic.dynamicStateCount = 2;
    dynamic.pDynamicStates = dynamicStates;
    VkPushConstantRange push{VK_SHADER_STAGE_VERTEX_BIT, 0, 8};
    VkPipelineLayoutCreateInfo layout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    layout.setLayoutCount = 1;
    layout.pSetLayouts = &setLayout_;
    layout.pushConstantRangeCount = 1;
    layout.pPushConstantRanges = &push;
    vkCheck(vkCreatePipelineLayout(ctx_.device, &layout, nullptr, &pipelineLayout_), "pipeline layout");
    VkGraphicsPipelineCreateInfo pipeline{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    pipeline.stageCount = 2;
    pipeline.pStages = stages;
    pipeline.pVertexInputState = &vertex;
    pipeline.pInputAssemblyState = &assembly;
    pipeline.pViewportState = &viewport;
    pipeline.pRasterizationState = &raster;
    pipeline.pMultisampleState = &multisample;
    pipeline.pColorBlendState = &blending;
    pipeline.pDynamicState = &dynamic;
    pipeline.layout = pipelineLayout_;
    pipeline.renderPass = renderPass_;
    VkResult result =
        vkCreateGraphicsPipelines(ctx_.device, VK_NULL_HANDLE, 1, &pipeline, nullptr, &pipeline_);
    vkDestroyShaderModule(ctx_.device, vert, nullptr);
    vkDestroyShaderModule(ctx_.device, frag, nullptr);
    vkCheck(result, "graphics pipeline");
    vert = shader("spatial.vert.spv");
    frag = shader("spatial.frag.spv");
    stages[0].module = vert;
    stages[1].module = frag;
    VkPushConstantRange spatialPush{VK_SHADER_STAGE_VERTEX_BIT, 0, 64};
    layout.setLayoutCount = 0;
    layout.pSetLayouts = nullptr;
    layout.pPushConstantRanges = &spatialPush;
    vkCheck(vkCreatePipelineLayout(ctx_.device, &layout, nullptr, &spatialLayout_), "spatial layout");
    vertexBinding.stride = sizeof(SpatialVertex);
    attrs[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0};
    attrs[1] = {1, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 12};
    attrs[2] = {2, 0, VK_FORMAT_R32G32B32_SFLOAT, 28};
    vertex.vertexAttributeDescriptionCount = 3;
    VkPipelineDepthStencilStateCreateInfo depthState{
        VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    depthState.depthTestEnable = VK_TRUE;
    depthState.depthWriteEnable = VK_TRUE;
    depthState.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    pipeline.pDepthStencilState = &depthState;
    pipeline.renderPass = spatialPass_;
    pipeline.layout = spatialLayout_;
    result = vkCreateGraphicsPipelines(ctx_.device, VK_NULL_HANDLE, 1, &pipeline, nullptr, &spatialPipeline_);
    vkDestroyShaderModule(ctx_.device, vert, nullptr);
    vkDestroyShaderModule(ctx_.device, frag, nullptr);
    vkCheck(result, "spatial pipeline");
    vert = shader("environment.vert.spv");
    frag = shader("environment.frag.spv");
    stages[0].module = vert;
    stages[1].module = frag;
    VkPushConstantRange environmentPush{VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(EnvironmentView)};
    layout.setLayoutCount = 1;
    layout.pSetLayouts = &setLayout_;
    layout.pPushConstantRanges = &environmentPush;
    vkCheck(vkCreatePipelineLayout(ctx_.device, &layout, nullptr, &environmentLayout_), "environment layout");
    vertex.vertexBindingDescriptionCount = 0;
    vertex.vertexAttributeDescriptionCount = 0;
    depthState.depthTestEnable = VK_FALSE;
    depthState.depthWriteEnable = VK_FALSE;
    pipeline.layout = environmentLayout_;
    result = vkCreateGraphicsPipelines(ctx_.device, VK_NULL_HANDLE, 1, &pipeline, nullptr,
                                       &spatialEnvironmentPipeline_);
    vkCheck(result, "spatial environment pipeline");
    pipeline.renderPass = renderPass_;
    pipeline.pDepthStencilState = nullptr;
    result =
        vkCreateGraphicsPipelines(ctx_.device, VK_NULL_HANDLE, 1, &pipeline, nullptr, &environmentPipeline_);
    vkCheck(result, "glass environment pipeline");
    vkDestroyShaderModule(ctx_.device, frag, nullptr);
    frag = shader("glass.frag.spv");
    stages[1].module = frag;
    // Both full-screen pipelines share the environment constants; the glass pass also samples a texture.
    pipeline.layout = environmentLayout_;
    result = vkCreateGraphicsPipelines(ctx_.device, VK_NULL_HANDLE, 1, &pipeline, nullptr, &glassPipeline_);
    vkDestroyShaderModule(ctx_.device, vert, nullptr);
    vkDestroyShaderModule(ctx_.device, frag, nullptr);
    vkCheck(result, "glass sampling pipeline");
    glass_ = createTarget(480, 330);
    VkDescriptorImageInfo glassTexture{sampler_, glass_.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    write.dstBinding = 1;
    write.pImageInfo = &glassTexture;
    vkUpdateDescriptorSets(ctx_.device, 1, &write, 0, nullptr);
}
void VulkanRenderer::uploadImages(uint32_t width, uint32_t height, const std::vector<uint8_t> &rgba) {
    if (!width || !height || width>2048 || height>2048 || rgba.size()!=size_t(width)*height*4)
        throw std::runtime_error("Invalid application image atlas");
    vkCheck(vkDeviceWaitIdle(ctx_.device), "wait image atlas upload");
    destroyTarget(images_);
    image(width,height,VK_FORMAT_R8G8B8A8_SRGB,VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_SAMPLED_BIT,images_);
    VkBuffer staging{};
    VkDeviceMemory memory{};
    buffer(rgba.size(),VK_BUFFER_USAGE_TRANSFER_SRC_BIT,staging,memory);
    void *mapped{};
    vkCheck(vkMapMemory(ctx_.device,memory,0,VK_WHOLE_SIZE,0,&mapped),"map image atlas");
    std::memcpy(mapped,rgba.data(),rgba.size());
    vkUnmapMemory(ctx_.device,memory);
    begin();
    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.oldLayout=VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
    barrier.image=images_.image;
    barrier.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
    vkCmdPipelineBarrier(command_,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,1,&barrier);
    VkBufferImageCopy copy{};
    copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};
    copy.imageExtent={width,height,1};
    vkCmdCopyBufferToImage(command_,staging,images_.image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&copy);
    barrier.oldLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(command_,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,0,0,nullptr,0,nullptr,1,&barrier);
    submit();
    vkDestroyBuffer(ctx_.device,staging,nullptr);
    vkFreeMemory(ctx_.device,memory,nullptr);
    VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    view.image=images_.image;
    view.viewType=VK_IMAGE_VIEW_TYPE_2D;
    view.format=VK_FORMAT_R8G8B8A8_SRGB;
    view.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
    vkCheck(vkCreateImageView(ctx_.device,&view,nullptr,&images_.view),"image atlas view");
    VkDescriptorImageInfo texture{sampler_,images_.view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet=descriptor_;
    write.dstBinding=2;
    write.descriptorCount=1;
    write.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo=&texture;
    vkUpdateDescriptorSets(ctx_.device,1,&write,0,nullptr);
}
Target VulkanRenderer::createTarget(uint32_t w, uint32_t h, VkImage external, bool depth) {
    Target t{};
    t.width = w;
    t.height = h;
    if (external)
        t.image = external;
    else
        image(w, h, format_,
              VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                  VK_IMAGE_USAGE_SAMPLED_BIT,
              t);
    VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    view.image = t.image;
    view.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view.format = format_;
    view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCheck(vkCreateImageView(ctx_.device, &view, nullptr, &t.view), "target view");
    VkFramebufferCreateInfo frame{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    frame.renderPass = renderPass_;
    frame.attachmentCount = 1;
    VkImageView attachments[]{t.view, VK_NULL_HANDLE};
    if (depth) {
        Target z;
        image(w, h, depthFormat_, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, z);
        t.depthImage = z.image;
        t.depthMemory = z.memory;
        view.image = z.image;
        view.format = depthFormat_;
        view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        vkCheck(vkCreateImageView(ctx_.device, &view, nullptr, &t.depthView), "depth view");
        attachments[1] = t.depthView;
        frame.attachmentCount = 2;
        frame.renderPass = spatialPass_;
    }
    frame.pAttachments = attachments;
    frame.width = w;
    frame.height = h;
    frame.layers = 1;
    vkCheck(vkCreateFramebuffer(ctx_.device, &frame, nullptr, &t.framebuffer), "framebuffer");
    return t;
}
void VulkanRenderer::destroyTarget(Target &t) {
    if (t.framebuffer)
        vkDestroyFramebuffer(ctx_.device, t.framebuffer, nullptr);
    if (t.view)
        vkDestroyImageView(ctx_.device, t.view, nullptr);
    if (t.depthView)
        vkDestroyImageView(ctx_.device, t.depthView, nullptr);
    if (t.depthImage)
        vkDestroyImage(ctx_.device, t.depthImage, nullptr);
    if (t.depthMemory)
        vkFreeMemory(ctx_.device, t.depthMemory, nullptr);
    if (t.memory) {
        vkDestroyImage(ctx_.device, t.image, nullptr);
        vkFreeMemory(ctx_.device, t.memory, nullptr);
    }
    t = {};
}
void VulkanRenderer::draw(const Target &t, const DrawList &d, const EnvironmentView *glass) {
    if (d.vertices.size() > maxVertices)
        throw std::runtime_error("UI vertex budget exceeded");
    std::memcpy(vertices_, d.vertices.data(), d.vertices.size() * sizeof(Vertex));
    lastVertexCount = uint32_t(d.vertices.size());
    begin();
    if (glass && glass->options[3] > 0) {
        VkClearValue transparent{};
        VkRenderPassBeginInfo blurPass{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
        blurPass.renderPass = renderPass_;
        blurPass.framebuffer = glass_.framebuffer;
        blurPass.renderArea = {{0, 0}, {glass_.width, glass_.height}};
        blurPass.clearValueCount = 1;
        blurPass.pClearValues = &transparent;
        vkCmdBeginRenderPass(command_, &blurPass, VK_SUBPASS_CONTENTS_INLINE);
        EnvironmentView blur = *glass;
        blur.shape[2] = 0;
        environment(glass_, blur, false);
        vkCmdEndRenderPass(command_);
        VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = glass_.image;
        barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCmdPipelineBarrier(command_, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    }
    VkClearValue clear{};
    VkRenderPassBeginInfo pass{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    pass.renderPass = renderPass_;
    pass.framebuffer = t.framebuffer;
    pass.renderArea = {{0, 0}, {t.width, t.height}};
    pass.clearValueCount = 1;
    pass.pClearValues = &clear;
    vkCmdBeginRenderPass(command_, &pass, VK_SUBPASS_CONTENTS_INLINE);
    if (glass && glass->options[3] > 0) {
        vkCmdBindPipeline(command_, VK_PIPELINE_BIND_POINT_GRAPHICS, glassPipeline_);
        vkCmdBindDescriptorSets(command_, VK_PIPELINE_BIND_POINT_GRAPHICS, environmentLayout_, 0, 1,
                                &descriptor_, 0, nullptr);
        vkCmdPushConstants(command_, environmentLayout_, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(*glass),
                           glass);
        VkViewport viewport{0, 0, float(t.width), float(t.height), 0, 1};
        VkRect2D scissor{{0, 0}, {t.width, t.height}};
        vkCmdSetViewport(command_, 0, 1, &viewport);
        vkCmdSetScissor(command_, 0, 1, &scissor);
        vkCmdDraw(command_, 3, 1, 0, 0);
    }
    vkCmdBindPipeline(command_, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
    VkViewport viewport{0, 0, float(t.width), float(t.height), 0, 1};
    VkRect2D scissor{{0, 0}, {t.width, t.height}};
    vkCmdSetViewport(command_, 0, 1, &viewport);
    vkCmdSetScissor(command_, 0, 1, &scissor);
    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(command_, 0, 1, &vertexBuffer_, &offset);
    vkCmdBindDescriptorSets(command_, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout_, 0, 1, &descriptor_, 0,
                            nullptr);
    float size[] = {float(t.width), float(t.height)};
    vkCmdPushConstants(command_, pipelineLayout_, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(size), size);
    vkCmdDraw(command_, lastVertexCount, 1, 0, 0);
    vkCmdEndRenderPass(command_);
    submit();
}
std::vector<uint8_t> VulkanRenderer::readback(const Target &t) {
    size_t size = size_t(t.width) * t.height * 4;
    VkBuffer bufferHandle{};
    VkDeviceMemory memory{};
    buffer(size, VK_BUFFER_USAGE_TRANSFER_DST_BIT, bufferHandle, memory);
    begin();
    VkImageMemoryBarrier imageBarrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    imageBarrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    imageBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    imageBarrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    imageBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    imageBarrier.srcQueueFamilyIndex = imageBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    imageBarrier.image = t.image;
    imageBarrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdPipelineBarrier(command_, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &imageBarrier);
    VkBufferImageCopy copy{};
    copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    copy.imageExtent = {t.width, t.height, 1};
    vkCmdCopyImageToBuffer(command_, t.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, bufferHandle, 1, &copy);
    VkBufferMemoryBarrier host{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
    host.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    host.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    host.srcQueueFamilyIndex = host.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    host.buffer = bufferHandle;
    host.size = VK_WHOLE_SIZE;
    vkCmdPipelineBarrier(command_, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 0, nullptr,
                         1, &host, 0, nullptr);
    submit();
    void *mapped{};
    vkCheck(vkMapMemory(ctx_.device, memory, 0, VK_WHOLE_SIZE, 0, &mapped), "map readback");
    std::vector<uint8_t> bytes(size);
    std::memcpy(bytes.data(), mapped, size);
    vkUnmapMemory(ctx_.device, memory);
    vkDestroyBuffer(ctx_.device, bufferHandle, nullptr);
    vkFreeMemory(ctx_.device, memory, nullptr);
    return bytes;
}
void VulkanRenderer::environment(const Target &t, const EnvironmentView &view, bool spatial) {
    vkCmdBindPipeline(command_, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      spatial ? spatialEnvironmentPipeline_ : environmentPipeline_);
    VkViewport viewport{0, 0, float(t.width), float(t.height), 0, 1};
    VkRect2D scissor{{0, 0}, {t.width, t.height}};
    vkCmdSetViewport(command_, 0, 1, &viewport);
    vkCmdSetScissor(command_, 0, 1, &scissor);
    vkCmdPushConstants(command_, environmentLayout_, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(view), &view);
    vkCmdDraw(command_, 3, 1, 0, 0);
}
void VulkanRenderer::drawSpatial(const Target &t, const SpatialMesh &mesh, const Mat4 &matrix,
                                 const EnvironmentView *env) {
    if (!t.depthImage)
        throw std::runtime_error("Spatial rendering requires a depth target");
    if (mesh.vertices.size() * sizeof(SpatialVertex) > maxVertices * sizeof(Vertex))
        throw std::runtime_error("Spatial vertex budget exceeded");
    std::memcpy(vertices_, mesh.vertices.data(), mesh.vertices.size() * sizeof(SpatialVertex));
    begin();
    VkClearValue clear[2]{};
    clear[1].depthStencil = {1, 0};
    VkRenderPassBeginInfo pass{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    pass.renderPass = spatialPass_;
    pass.framebuffer = t.framebuffer;
    pass.renderArea = {{0, 0}, {t.width, t.height}};
    pass.clearValueCount = 2;
    pass.pClearValues = clear;
    vkCmdBeginRenderPass(command_, &pass, VK_SUBPASS_CONTENTS_INLINE);
    if (env && env->options[0] < 1.5f)
        environment(t, *env, true);
    vkCmdBindPipeline(command_, VK_PIPELINE_BIND_POINT_GRAPHICS, spatialPipeline_);
    VkViewport viewport{0, 0, float(t.width), float(t.height), 0, 1};
    VkRect2D scissor{{0, 0}, {t.width, t.height}};
    vkCmdSetViewport(command_, 0, 1, &viewport);
    vkCmdSetScissor(command_, 0, 1, &scissor);
    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(command_, 0, 1, &vertexBuffer_, &offset);
    vkCmdPushConstants(command_, spatialLayout_, VK_SHADER_STAGE_VERTEX_BIT, 0, 64, matrix.data());
    vkCmdDraw(command_, uint32_t(mesh.vertices.size()), 1, 0, 0);
    vkCmdEndRenderPass(command_);
    submit();
}
VulkanRenderer::~VulkanRenderer() {
    if (!ctx_.device)
        return;
    vkDeviceWaitIdle(ctx_.device);
    for (auto p : {environmentPipeline_, spatialEnvironmentPipeline_, glassPipeline_})
        if (p)
            vkDestroyPipeline(ctx_.device, p, nullptr);
    if (environmentLayout_)
        vkDestroyPipelineLayout(ctx_.device, environmentLayout_, nullptr);
    if (spatialPipeline_)
        vkDestroyPipeline(ctx_.device, spatialPipeline_, nullptr);
    if (spatialLayout_)
        vkDestroyPipelineLayout(ctx_.device, spatialLayout_, nullptr);
    if (pipeline_)
        vkDestroyPipeline(ctx_.device, pipeline_, nullptr);
    if (pipelineLayout_)
        vkDestroyPipelineLayout(ctx_.device, pipelineLayout_, nullptr);
    if (renderPass_)
        vkDestroyRenderPass(ctx_.device, renderPass_, nullptr);
    if (spatialPass_)
        vkDestroyRenderPass(ctx_.device, spatialPass_, nullptr);
    if (descriptorPool_)
        vkDestroyDescriptorPool(ctx_.device, descriptorPool_, nullptr);
    if (setLayout_)
        vkDestroyDescriptorSetLayout(ctx_.device, setLayout_, nullptr);
    if (sampler_)
        vkDestroySampler(ctx_.device, sampler_, nullptr);
    destroyTarget(atlas_);
    destroyTarget(glass_);
    destroyTarget(images_);
    if (vertices_)
        vkUnmapMemory(ctx_.device, vertexMemory_);
    if (vertexBuffer_)
        vkDestroyBuffer(ctx_.device, vertexBuffer_, nullptr);
    if (vertexMemory_)
        vkFreeMemory(ctx_.device, vertexMemory_, nullptr);
    if (fence_)
        vkDestroyFence(ctx_.device, fence_, nullptr);
    if (commandPool_)
        vkDestroyCommandPool(ctx_.device, commandPool_, nullptr);
}
} // namespace cyvr
