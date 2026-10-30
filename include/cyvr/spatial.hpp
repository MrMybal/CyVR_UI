// SPDX-License-Identifier: MIT
#pragma once
#include "cyvr/core.hpp"
namespace cyvr {
using Mat4 = std::array<float, 16>;
inline constexpr float spatialNearZ = .04f, spatialFarZ = 32.f;
Mat4 viewProjection(Pose view, float left, float right, float up, float down);
struct SpatialVertex {
    float x, y, z, r, g, b, a, nx, ny, nz;
};
struct HandJoint {
    Pose pose;
    float radius{};
    bool valid{};
};
using HandJoints = std::array<HandJoint, 26>;
// Runtime-neutral, replaceable skinned hand geometry. Bind poses and vertices share one model space.
struct HandVertex {
    Vec3 position, normal;
    std::array<int16_t, 4> joints;
    std::array<float, 4> weights;
};
struct HandSurfaceVertex {
    Vec3 position, normal;
};
struct HandSurface {
    std::vector<HandSurfaceVertex> vertices;
    static HandSurface load(const std::vector<uint8_t> &bytes);
};
struct HandSkin {
    std::vector<HandVertex> vertices;
    std::vector<uint16_t> indices;
    std::array<Pose, 26> bindPoses;
    bool valid() const;
};
struct SpatialMesh {
    std::vector<SpatialVertex> vertices;
    SpatialMesh() { vertices.reserve(20000); }
    void clear() { vertices.clear(); }
    void triangle(Vec3 a, Vec3 b, Vec3 c, Color color);
    void smoothTriangle(Vec3 a, Vec3 b, Vec3 c, Vec3 na, Vec3 nb, Vec3 nc, Color color);
    void bone(Vec3 a, Vec3 b, float radius, Color color, int sides = 8);
    void joint(Vec3 position, float radius, Color color);
    void ellipsoid(Pose pose, Vec3 radii, Color color, int sides = 16, int rings = 10);
    void controllerHand(Pose grip, bool left, float trigger, HandModel model = HandModel::Studio,
                        float scale = 1, float pitch = 0, const HandSurface *surface = nullptr);
    void trackedHand(const HandJoints &joints, HandModel model);
    bool skinnedHand(const HandSkin &skin, const HandJoints &joints, Color color, float scale = 1);
};
} // namespace cyvr
