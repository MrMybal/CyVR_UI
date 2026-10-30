// SPDX-License-Identifier: MIT
#pragma once
#include "cyvr/scene.hpp"

namespace cyvr {
// Baked vertex radiance: scene identity/count and finite bounded values checked before applying.
void applySceneLighting(SceneMesh &mesh, Background scene, const std::vector<uint8_t> &bytes);
} // namespace cyvr
