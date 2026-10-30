// SPDX-License-Identifier: MIT
#include "cyvr/pbr.hpp"
#include <stdexcept>

namespace cyvr {
size_t PbrTexture::texelBytes() const {
    return format == PbrFormat::RGBA16F ? 8 : 4;
}
size_t PbrTexture::levelBytes(uint32_t level) const {
    return size_t(std::max(1u, width >> level)) * std::max(1u, height >> level) * layers * texelBytes();
}
PbrTexture decodePbrTexture(const std::vector<uint8_t> &bytes) {
    if (bytes.size() < 32 || std::string(bytes.begin(), bytes.begin() + 4) != "CYTX")
        throw std::runtime_error("Invalid PBR texture header");
    auto word = [&](size_t offset) {
        return uint32_t(bytes[offset]) | uint32_t(bytes[offset + 1]) << 8 |
               uint32_t(bytes[offset + 2]) << 16 | uint32_t(bytes[offset + 3]) << 24;
    };
    PbrTexture t{word(8), word(12), word(16), word(20), PbrFormat(word(24)), word(28) == 1, {}};
    uint32_t maxLevels = 1;
    for (auto size = std::max(t.width, t.height); size > 1; size >>= 1)
        ++maxLevels;
    if (word(4) != 1 || !t.width || !t.height || t.width > 256 || t.height > 256 || !t.layers ||
        t.layers > 16 || !t.levels || t.levels > maxLevels || word(24) < 1 || word(24) > 4 || word(28) > 1 ||
        (t.cube && (t.layers != 6 || t.width != t.height)))
        throw std::runtime_error("Unsupported PBR texture dimensions or format");
    size_t size{};
    for (uint32_t level = 0; level < t.levels; ++level)
        size += t.levelBytes(level);
    if (bytes.size() != size + 32)
        throw std::runtime_error("Truncated or oversized PBR texture payload");
    t.pixels.assign(bytes.begin() + 32, bytes.end());
    return t;
}
PbrTexture fallbackMaterial(bool normal) {
    PbrTexture t{1, 1, 9, 1, normal ? PbrFormat::RGBA8 : PbrFormat::SRGBA8, false, {}};
    const float rough[]{.85f, .50f, .95f, .25f, .7f, .12f, .75f, .27f, .9f};
    for (int i = 0; i < 9; ++i) {
        if (normal)
            t.pixels.insert(t.pixels.end(), {128, 128, uint8_t(rough[i] * 255), uint8_t(i == 3 ? 255 : 0)});
        else
            t.pixels.insert(t.pixels.end(), {255, 255, 255, 255});
    }
    return t;
}
PbrTexture fallbackProbe(Background background) {
    PbrTexture t{1, 1, 6, 1, PbrFormat::RGBA8, true, {}};
    for (Vec3 direction : {Vec3{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}}) {
        auto c = sceneSkyRadiance(background, direction);
        for (float v : {c.x, c.y, c.z})
            t.pixels.push_back(uint8_t(std::clamp(v, 0.f, 1.f) * 255));
        t.pixels.push_back(255);
    }
    return t;
}
PbrTexture fallbackBrdf() {
    // Conservative constant DFG response if the optional integration table is absent.
    return {1, 1, 1, 1, PbrFormat::RGBA8, false, {192, 4, 0, 255}};
}
Vec3 sceneSunDirection(Background b) {
    return normalized({-.5f,
                       b == Background::Observatory ? .35f
                       : b == Background::Terrace   ? .30f
                                                    : .65f,
                       b == Background::Observatory ? .7f : -.7f});
}
Vec3 sceneSunRadiance(Background b) {
    return b == Background::Observatory ? Vec3{.65f, 1.15f, 2.1f}
           : b == Background::Terrace   ? Vec3{4.5f, 2.55f, 1.15f}
                                        : Vec3{3.4f, 3.12f, 2.6f};
}
Vec3 sceneSkyRadiance(Background b, Vec3 d) {
    float h = std::clamp(d.y, 0.f, 1.f), ground = std::clamp(-d.y * 3.f, 0.f, 1.f);
    Vec3 horizon, top, bottom;
    if (b == Background::Observatory) {
        horizon = {.075f, .12f, .22f};
        top = {.018f, .035f, .09f};
        bottom = {.06f, .09f, .12f};
    } else if (b == Background::Terrace) {
        horizon = {.75f, .40f, .22f};
        top = {.16f, .32f, .58f};
        bottom = {.18f, .12f, .08f};
    } else {
        horizon = {.66f, .74f, .82f};
        top = {.25f, .48f, .78f};
        bottom = {.19f, .15f, .10f};
    }
    auto color = horizon * (1 - h) + top * h;
    return color * (1 - ground) + bottom * ground;
}
} // namespace cyvr
