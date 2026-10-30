// SPDX-License-Identifier: MIT
#pragma once
#include "cyvr/core.hpp"

namespace cyvr {
// Original offline-baked textures. No GPU-dependent preprocessing at startup.
enum class PbrFormat : uint32_t { SRGBA8 = 1, RGBA8 = 2, RGBA16F = 3, RG16F = 4 };
struct PbrTexture {
    uint32_t width{}, height{}, layers{}, levels{};
    PbrFormat format{};
    bool cube{};
    std::vector<uint8_t> pixels;
    size_t texelBytes() const;
    size_t levelBytes(uint32_t level) const;
};
PbrTexture decodePbrTexture(const std::vector<uint8_t> &bytes);
// Small neutral fallback for applications that do not bundle the reference assets.
PbrTexture fallbackMaterial(bool normalRoughMetal);
PbrTexture fallbackProbe(Background background);
PbrTexture fallbackBrdf();
Vec3 sceneSunDirection(Background background);
Vec3 sceneSunRadiance(Background background);
// Linear HDR distant lighting, excluding the direct sun disk (evaluated separately).
Vec3 sceneSkyRadiance(Background background, Vec3 direction);
} // namespace cyvr
