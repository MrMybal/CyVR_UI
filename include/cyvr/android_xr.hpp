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
class AndroidXrRenderExtension : public AndroidRenderExtension {
public:
    virtual std::vector<const char*> instanceExtensions() const { return {}; }
    virtual std::vector<const char*> optionalInstanceExtensions() const { return {}; }
    virtual void xrReady(const AndroidXrContext&) {}
    // Delivered before changeTime so scene anchors can rebase when it takes effect.
    virtual void referenceSpaceChanging(const XrEventDataReferenceSpaceChangePending&) {}
    // Returned headers and nested arrays must remain valid until xrEndFrame.
    virtual uint32_t compositionLayers(const AndroidXrFrame&, const XrCompositionLayerBaseHeader**,
                                       uint32_t /*capacity*/) { return 0; }
};
}
