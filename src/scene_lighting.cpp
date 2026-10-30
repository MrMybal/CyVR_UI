// SPDX-License-Identifier: MIT
#include "cyvr/scene_lighting.hpp"
#include <cstring>
#include <stdexcept>

namespace cyvr {
void applySceneLighting(SceneMesh &mesh, Background scene, const std::vector<uint8_t> &bytes) {
    auto invalid = [] { throw std::runtime_error("Invalid baked scene lighting"); };
    if (bytes.size() < 16 || std::string(bytes.begin(), bytes.begin() + 4) != "CYLI")
        invalid();
    auto word = [&](size_t offset) {
        return uint32_t(bytes[offset]) | uint32_t(bytes[offset + 1]) << 8 |
               uint32_t(bytes[offset + 2]) << 16 | uint32_t(bytes[offset + 3]) << 24;
    };
    if (word(4) != 1 || word(8) != uint32_t(scene) || word(12) != mesh.vertices.size() ||
        bytes.size() != 16 + mesh.vertices.size() * 20)
        invalid();
    // Validate the full resource before modifying any vertex.
    for (size_t i = 16; i < bytes.size(); i += 4) {
        float value;
        std::memcpy(&value, bytes.data() + i, 4);
        if (!std::isfinite(value) || value < 0 || value > 64)
            invalid();
        if ((i - 16) % 20 < 8 && value > 1)
            invalid();
    }
    for (size_t i = 0; i < mesh.vertices.size(); ++i) {
        float light[5];
        std::memcpy(light, bytes.data() + 16 + i * 20, sizeof(light));
        mesh.vertices[i].surface.a = light[0];
        mesh.vertices[i].ambient = light[1];
        mesh.vertices[i].indirect = {light[2], light[3], light[4]};
    }
}
} // namespace cyvr
