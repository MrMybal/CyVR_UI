// SPDX-License-Identifier: MIT
#pragma once
#include "cyvr/spatial.hpp"
#include <openxr/openxr.h>

namespace cyvr {
// Frame-local copies keep application layer headers and their existing next chains intact.
struct DepthComposition {
    static constexpr size_t capacity = 16;
    std::array<XrCompositionLayerDepthTestFB, capacity> tests{};
    std::array<XrCompositionLayerQuad, capacity> quads{};
    std::array<XrCompositionLayerCylinderKHR, capacity> cylinders{};

    const XrCompositionLayerBaseHeader *test(const XrCompositionLayerBaseHeader *layer, size_t index) {
        if (!layer || index >= capacity)
            return layer;
        // Respect depth policies explicitly supplied by application extensions.
        for (auto next = static_cast<const XrBaseInStructure *>(layer->next); next; next = next->next)
            if (next->type == XR_TYPE_COMPOSITION_LAYER_DEPTH_TEST_FB)
                return layer;
        auto &depth = tests[index];
        depth = {XR_TYPE_COMPOSITION_LAYER_DEPTH_TEST_FB};
        depth.next = layer->next;
        depth.depthMask = XR_FALSE; // Transparent panel corners must not occlude other layers.
        depth.compareOp = XR_COMPARE_OP_LESS_OR_EQUAL_FB;
        if (layer->type == XR_TYPE_COMPOSITION_LAYER_QUAD) {
            quads[index] = *reinterpret_cast<const XrCompositionLayerQuad *>(layer);
            quads[index].next = &depth;
            return reinterpret_cast<const XrCompositionLayerBaseHeader *>(&quads[index]);
        }
        if (layer->type == XR_TYPE_COMPOSITION_LAYER_CYLINDER_KHR) {
            cylinders[index] = *reinterpret_cast<const XrCompositionLayerCylinderKHR *>(layer);
            cylinders[index].next = &depth;
            return reinterpret_cast<const XrCompositionLayerBaseHeader *>(&cylinders[index]);
        }
        return layer;
    }
};
} // namespace cyvr
