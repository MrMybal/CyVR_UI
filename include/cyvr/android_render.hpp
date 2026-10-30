// SPDX-License-Identifier: MIT
#pragma once
#include "cyvr/gpu.hpp"

namespace cyvr {
// Optional application-owned GPU resources. Called on the host render thread.
// A surface/renderer can be recreated while the VkDevice and application remain alive.
class AndroidRenderExtension {
public:
    virtual ~AndroidRenderExtension() = default;
    virtual void configureDevice(VulkanContext&, VkDeviceCreateInfo&) {}
    virtual void graphicsReady(VulkanContext&, VulkanRenderer&, VkFormat, const AssetReader&, UiModel&) {}
    virtual void graphicsStopping(bool /*deviceStopping*/) {}
    virtual bool beforeRender(UiModel&, double /*seconds*/) { return false; }
    virtual int pollTimeoutMs() const { return 33; }
    // A host frame has been queued successfully; not a compositor visibility claim.
    virtual void frameSubmitted(UiModel&) {}
};
}
