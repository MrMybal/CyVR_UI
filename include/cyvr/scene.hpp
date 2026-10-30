// SPDX-License-Identifier: MIT
#pragma once
#include "cyvr/spatial.hpp"

namespace cyvr {
// All dimensions are in metres, +Y up, -Z forward, with the floor at Y=0.
// Original geometry, generated only on environment changes; no external model files.
struct SceneVertex {
    // surface.a stores baked sunlight visibility; scene surfaces are opaque.
    SpatialVertex surface;
    float material{}, ambient{1};
    Vec2 uv;
    Vec3 tangent;
    // Cosine-weighted, occluded diffuse environment + approximate single-bounce radiance.
    Vec3 indirect;
};
struct SceneMesh {
    std::vector<SceneVertex> vertices;
    // First vertex of each triangle seen from both sides: flat rings and leaves.
    std::vector<uint32_t> openTriangles;
};
bool isScene3D(Background background);
SceneMesh buildScene(Background background, bool fallbackLighting = true);
// Returns how many leading vertices form closed surfaces, whose back faces can be culled.
// Baked lighting follows the original order, so call this after applying it.
uint32_t moveOpenTrianglesLast(SceneMesh &mesh);
Pose relativePose(Pose origin, Pose pose);
Mat4 modelProjection(const Mat4 &viewProjection, Pose model);
// Generalized off-axis projection through a rectangle, used for the 3D glass backdrop.
Mat4 rectangleProjection(Vec3 eye, Vec3 centerDirection, Vec3 halfRight, Vec3 halfUp);
// A room is placed once, with its centre at the user's initial floor position.
Pose placeScene(Pose head, float floorY);
} // namespace cyvr
