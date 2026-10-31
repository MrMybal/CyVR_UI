// SPDX-License-Identifier: MIT
#pragma once
#include "cyvr/android_render.hpp"
#include <openxr/openxr.h>
namespace cyvr {
struct AndroidXrContext {
    XrInstance instance;
    XrSession session;
    XrSpace local, view;
    XrSystemId system{};
    std::vector<std::string> enabledApplicationExtensions;
};
struct AndroidXrFrame {
    XrTime displayTime;
    XrSpace panelSpace;
    const PanelGeometry& panel;
};
// Called after an eye has finished rendering and before xrReleaseSwapchainImage.
// Color is a single-layer sampled/attachment image in COLOR_ATTACHMENT_OPTIMAL.
// The extension must finish its GPU work and restore that layout before return.
// The host owns the target and its depth; depth must remain unchanged.
struct AndroidXrEyeFrame {
    XrTime displayTime;
    uint32_t eyeIndex;
    const Target& target;
    VkFormat format;
    bool passthrough;
};
class AndroidXrRenderExtension : public AndroidRenderExtension {
public:
    virtual std::vector<const char*> instanceExtensions() const { return {}; }
    virtual std::vector<const char*> optionalInstanceExtensions() const { return {}; }
    virtual void xrReady(const AndroidXrContext&) {}
    // Frame timing for application-owned immersive producers, before scene/layer composition.
    virtual void beginXrFrame(XrTime, bool /*focused*/, bool /*shouldRender*/) {}
    virtual void processEye(const AndroidXrEyeFrame&) {}
    // Delivered before changeTime so scene anchors can rebase when it takes effect.
    virtual void referenceSpaceChanging(const XrEventDataReferenceSpaceChangePending&) {}
    // Returned headers and nested arrays must remain valid until xrEndFrame.
    virtual uint32_t compositionLayers(const AndroidXrFrame&, const XrCompositionLayerBaseHeader**,
                                       uint32_t /*capacity*/) { return 0; }
};
}
