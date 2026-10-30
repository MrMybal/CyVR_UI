// SPDX-License-Identifier: MIT
#include "cyvr/spatial.hpp"
#include <cstring>
namespace cyvr {
static Vec3 cross3(Vec3 a, Vec3 b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
Mat4 viewProjection(Pose pose, float left, float right, float up, float down) {
    float l = std::tan(left), r = std::tan(right), u = std::tan(up), d = std::tan(down), near = spatialNearZ,
          far = spatialFarZ;
    Mat4 projection{};
    projection[0] = 2 / (r - l);
    projection[5] = 2 / (d - u);
    projection[8] = (r + l) / (r - l);
    projection[9] = (u + d) / (d - u);
    projection[10] = -far / (far - near);
    projection[11] = -1;
    projection[14] = -far * near / (far - near);
    Quat inverse{-pose.orientation.x, -pose.orientation.y, -pose.orientation.z, pose.orientation.w};
    Vec3 x = rotate(inverse, {1, 0, 0}), y = rotate(inverse, {0, 1, 0}), z = rotate(inverse, {0, 0, 1}),
         t = rotate(inverse, pose.position * -1);
    Mat4 view{x.x, x.y, x.z, 0, y.x, y.y, y.z, 0, z.x, z.y, z.z, 0, t.x, t.y, t.z, 1}, out{};
    for (int c = 0; c < 4; c++)
        for (int row = 0; row < 4; row++)
            for (int k = 0; k < 4; k++)
                out[c * 4 + row] += projection[k * 4 + row] * view[c * 4 + k];
    return out;
}
void SpatialMesh::triangle(Vec3 a, Vec3 b, Vec3 c, Color color) {
    Vec3 n = normalized(cross3(b - a, c - a));
    smoothTriangle(a, b, c, n, n, n, color);
}
void SpatialMesh::smoothTriangle(Vec3 a, Vec3 b, Vec3 c, Vec3 na, Vec3 nb, Vec3 nc, Color color) {
    Vec3 points[]{a, b, c}, normals[]{na, nb, nc};
    for (int i = 0; i < 3; i++) {
        auto p = points[i], n = normals[i];
        vertices.push_back({p.x, p.y, p.z, color.r, color.g, color.b, color.a, n.x, n.y, n.z});
    }
}
void SpatialMesh::bone(Vec3 a, Vec3 b, float radius, Color color, int sides) {
    if (length(b - a) < .0001f)
        return;
    Vec3 axis = normalized(b - a),
         x = normalized(cross3(axis, std::abs(axis.y) < .9f ? Vec3{0, 1, 0} : Vec3{1, 0, 0})),
         y = cross3(axis, x);
    for (int i = 0; i < sides; i++) {
        float angle = i * 2 * pi / sides, next = (i + 1) * 2 * pi / sides;
        Vec3 u = x * std::cos(angle) + y * std::sin(angle), v = x * std::cos(next) + y * std::sin(next);
        Vec3 a1 = a + u * radius, a2 = a + v * radius, b1 = b + u * radius, b2 = b + v * radius;
        smoothTriangle(a1, b1, b2, u, u, v, color);
        smoothTriangle(a1, b2, a2, u, v, v, color);
        triangle(a, a2, a1, color);
        triangle(b, b1, b2, color);
    }
}
void SpatialMesh::joint(Vec3 p, float r, Color c) {
    ellipsoid({p, {}}, {r, r, r}, c, 12, 6);
}
void SpatialMesh::ellipsoid(Pose pose, Vec3 radii, Color color, int sides, int rings) {
    auto normal = [&](float u, float v) {
        return Vec3{std::sin(v) * std::cos(u), std::cos(v), std::sin(v) * std::sin(u)};
    };
    auto point = [&](Vec3 n) {
        return pose.position + rotate(pose.orientation, {n.x * radii.x, n.y * radii.y, n.z * radii.z});
    };
    auto worldNormal = [&](Vec3 n) {
        return normalized(rotate(pose.orientation, {n.x / radii.x, n.y / radii.y, n.z / radii.z}));
    };
    for (int j = 0; j < rings; j++)
        for (int i = 0; i < sides; i++) {
            Vec3 a = normal(i * 2 * pi / sides, j * pi / rings),
                 b = normal((i + 1) * 2 * pi / sides, j * pi / rings),
                 c = normal((i + 1) * 2 * pi / sides, (j + 1) * pi / rings),
                 d = normal(i * 2 * pi / sides, (j + 1) * pi / rings);
            if (j > 0)
                smoothTriangle(point(a), point(b), point(c), worldNormal(a), worldNormal(b), worldNormal(c),
                               color);
            if (j < rings - 1)
                smoothTriangle(point(a), point(c), point(d), worldNormal(a), worldNormal(c), worldNormal(d),
                               color);
        }
}
HandSurface HandSurface::load(const std::vector<uint8_t> &bytes) {
    HandSurface surface;
    if (bytes.size() < 12 || std::memcmp(bytes.data(), "CYRH", 4))
        return surface;
    uint32_t version, count;
    std::memcpy(&version, bytes.data() + 4, 4);
    std::memcpy(&count, bytes.data() + 8, 4);
    if (version != 1 || !count || count > 90000 || count % 3 ||
        bytes.size() != 12 + size_t(count) * sizeof(HandSurfaceVertex))
        return surface;
    surface.vertices.resize(count);
    std::memcpy(surface.vertices.data(), bytes.data() + 12, count * sizeof(HandSurfaceVertex));
    for (auto &v : surface.vertices)
        if (!std::isfinite(length(v.position)) || length(v.position) > .3f ||
            !std::isfinite(length(v.normal)) || length(v.normal) < .9f)
            return {};
    return surface;
}
void SpatialMesh::controllerHand(Pose grip, bool left, float trigger, HandModel model, float scale,
                                 float pitch, const HandSurface *surface) {
    if (model == HandModel::Hidden)
        return;
    auto point = [&](Vec3 p) {
        // Grip +X is the palm normal, -Z runs from little finger to index (OpenXR 1.0).
        // The authored palm uses +Y for the back, +Z towards the wrist.
        p = {left ? -p.y : p.y, p.z, p.x};
        float a = pitch * pi / 180;
        p = {p.x, p.y * std::cos(a) - p.z * std::sin(a), p.y * std::sin(a) + p.z * std::cos(a)};
        return grip.position + rotate(grip.orientation, p * scale);
    };
    Color hand = Color::hex(0xcbdcde), accent = Color::hex(0x86e7cf);
    bool skeleton = model == HandModel::Skeleton;
    if (!skeleton && surface && !surface->vertices.empty()) {
        float a = pitch * pi / 180;
        for (auto v : surface->vertices) {
            // Localized trigger deformation; the authored union stays connected at the knuckle.
            float weight = std::clamp((-v.position.z - .025f) / .045f, 0.f, 1.f) *
                           std::clamp((-.010f - v.position.x) / .012f, 0.f, 1.f);
            v.position.y -= weight * std::clamp(trigger, 0.f, 1.f) * .006f;
            Vec3 p = point(v.position), n = v.normal;
            n = {left ? -n.y : n.y, n.z, n.x};
            n = {n.x, n.y * std::cos(a) - n.z * std::sin(a), n.y * std::sin(a) + n.z * std::cos(a)};
            n = rotate(grip.orientation, n);
            vertices.push_back({p.x, p.y, p.z, hand.r, hand.g, hand.b, 1, n.x, n.y, n.z});
        }
        return;
    }
    auto capsule = [&](Vec3 a, Vec3 b, float r) {
        r *= scale * (skeleton ? .32f : 1.f);
        bone(point(a), point(b), r, skeleton ? accent : hand, 12);
        joint(point(a), r, skeleton ? accent : hand);
        joint(point(b), r, skeleton ? accent : hand);
    };
    // An ellipsoid in grip space avoids a palm facing the wrong axis.
    if (!skeleton) {
        Pose palm{point({0, 0, .005f}), grip.orientation};
        // Apply pitch to palm radii orientation as well.
        Quat q{std::sin(pitch * pi / 360), 0, 0, std::cos(pitch * pi / 360)}, g = grip.orientation;
        palm.orientation = {g.w * q.x + g.x * q.w, g.y * q.w + g.z * q.x, g.z * q.w - g.y * q.x,
                            g.w * q.w - g.x * q.x};
        ellipsoid(palm, {.015f * scale, .042f * scale, .037f * scale}, hand, 24, 12);
    }
    capsule({0, 0, .033f}, {0, -.001f, .061f}, .020f);
    for (int i = 0; i < 4; i++) {
        float x = -.027f + i * .018f, base = -.025f + (i == 3 ? .009f : 0), r = .0087f - i * .00055f;
        float curl = i == 0 ? std::clamp(trigger, 0.f, 1.f) : .85f;
        Vec3 a{x, 0, base}, b{x, -.006f, base - .027f},
            c{x, -.020f - .014f * curl, base - .034f + .012f * curl},
            d{x, -.033f - .011f * curl, base - .016f + .012f * curl};
        capsule(a, b, r);
        capsule(b, c, r * .88f);
        capsule(c, d, r * .72f);
        if (skeleton)
            capsule({0, 0, .028f}, a, .004f);
    }
    capsule({-.026f, -.004f, .022f}, {-.047f, -.014f, -.002f}, .012f);
    capsule({-.047f, -.014f, -.002f}, {-.041f, -.030f, -.029f}, .010f);
    capsule({-.041f, -.030f, -.029f}, {-.025f, -.033f, -.038f}, .008f);
}
void SpatialMesh::trackedHand(const HandJoints &joints, HandModel model) {
    if (model == HandModel::Hidden)
        return;
    const int parents[]{1,  -1, 1,  2, 3,  4,  1,  6,  7, 8,  9,  1,  11,
                        12, 13, 14, 1, 16, 17, 18, 19, 1, 21, 22, 23, 24};
    bool skeleton = model == HandModel::Skeleton;
    Color color = Color::hex(skeleton ? 0x86e7cf : 0xcbdcde);
    if (!skeleton && joints[0].valid)
        ellipsoid(joints[0].pose, {.034f, .013f, .037f}, color, 24, 12);
    for (int j = 2; j < 26; j++) {
        if (!joints[j].valid)
            continue;
        int p = parents[j];
        if (!joints[p].valid || (!skeleton && (j == 6 || j == 11 || j == 16 || j == 21)))
            continue;
        float r = std::clamp(joints[j].radius, .004f, .012f) * (skeleton ? .3f : .92f);
        bone(joints[p].pose.position, joints[j].pose.position, r, color, 12);
        joint(joints[j].pose.position, r, color);
    }
}
bool HandSkin::valid() const {
    if (vertices.empty() || vertices.size() > 10000 || indices.empty() || indices.size() > 60000 ||
        indices.size() % 3)
        return false;
    for (auto i : indices)
        if (i >= vertices.size())
            return false;
    for (auto &v : vertices) {
        float sum = 0;
        if (!std::isfinite(length(v.position)) || !std::isfinite(length(v.normal)) || length(v.normal) < .5f)
            return false;
        for (int k = 0; k < 4; k++) {
            if (v.joints[k] < 0 || v.joints[k] >= 26 || !std::isfinite(v.weights[k]) || v.weights[k] < 0)
                return false;
            sum += v.weights[k];
        }
        if (std::abs(sum - 1) > .02f)
            return false;
    }
    return true;
}
bool SpatialMesh::skinnedHand(const HandSkin &skin, const HandJoints &joints, Color color, float scale) {
    // validate once when loading; joint validity can change every frame.
    for (auto &j : joints)
        if (!j.valid)
            return false;
    std::array<Vec3, 10000> positions, normals;
    if (skin.vertices.size() > positions.size())
        return false;
    for (size_t i = 0; i < skin.vertices.size(); i++) {
        auto &v = skin.vertices[i];
        Vec3 p{}, n{};
        for (int k = 0; k < 4; k++) {
            int j = v.joints[k];
            float w = v.weights[k];
            if (w == 0)
                continue;
            Pose bind = skin.bindPoses[j], pose = joints[j].pose;
            Quat inverse{-bind.orientation.x, -bind.orientation.y, -bind.orientation.z, bind.orientation.w};
            p = p + (pose.position +
                     rotate(pose.orientation, rotate(inverse, (v.position - bind.position) * scale))) *
                        w;
            n = n + rotate(pose.orientation, rotate(inverse, v.normal)) * w;
        }
        positions[i] = p;
        normals[i] = normalized(n);
    }
    for (size_t i = 0; i < skin.indices.size(); i += 3) {
        int a = skin.indices[i], b = skin.indices[i + 1], c = skin.indices[i + 2];
        smoothTriangle(positions[a], positions[b], positions[c], normals[a], normals[b], normals[c], color);
    }
    return true;
}
} // namespace cyvr
