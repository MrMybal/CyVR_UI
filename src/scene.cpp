// SPDX-License-Identifier: MIT
#include "cyvr/scene.hpp"
#include "cyvr/pbr.hpp"
#include <stdexcept>

namespace cyvr {
namespace {
Vec3 cross(Vec3 a, Vec3 b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
Quat multiply(Quat a, Quat b) {
    return {a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
            a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}
enum Material { Matte, Wood, Fabric, Metal, Glow, Water, Leaf, Ceramic, Planet };
struct Builder {
    SceneMesh mesh;
    Pose group{};
    bool open{};
    void bake(Background background) {
        // Smooth inexpensive fallback only. The reference application loads an offline triangle/BVH bake.
        for (auto &vertex : mesh.vertices) {
            auto &v = vertex.surface;
            Vec3 n{v.nx, v.ny, v.nz};
            Vec3 tangent = normalized(cross(std::abs(n.y) < .95f ? Vec3{0, 1, 0} : Vec3{0, 0, 1}, n));
            Vec3 bitangent = cross(n, tangent);
            for (int i = 0; i < 16; ++i) {
                float u = (i + .5f) / 16, angle = i * 2.39996323f;
                Vec3 direction = tangent * (std::sqrt(u) * std::cos(angle)) +
                                 bitangent * (std::sqrt(u) * std::sin(angle)) + n * std::sqrt(1 - u);
                vertex.indirect = vertex.indirect + sceneSkyRadiance(background, direction) * (1.f / 16);
            }
        }
    }
    void triangle(Vec3 a, Vec3 b, Vec3 c, Color color, int material = Matte, Vec3 na = {}, Vec3 nb = {},
                  Vec3 nc = {}, float ao = 1) {
        if (length(cross(b - a, c - a)) < .000001f)
            return;
        if (length(na) < .1f)
            na = nb = nc = normalized(cross(b - a, c - a));
        if (open)
            mesh.openTriangles.push_back(uint32_t(mesh.vertices.size()));
        Vec3 p[]{a, b, c}, n[]{na, nb, nc};
        Vec3 average = normalized(na + nb + nc);
        Vec3 axis = std::abs(average.x) > .8f ? Vec3{0, 0, -1} : Vec3{1, 0, 0};
        Vec3 tangent = normalized(axis - average * dot(axis, average));
        Vec3 bitangent = cross(average, tangent);
        const float tiling[]{1.f, 1.f, 4.f, 2.f, 1.f, .5f, 3.f, 2.f, .15f};
        for (int i = 0; i < 3; ++i) {
            auto world = group.position + rotate(group.orientation, p[i]);
            auto normal = rotate(group.orientation, n[i]);
            auto t = rotate(group.orientation, normalized(tangent - n[i] * dot(tangent, n[i])));
            mesh.vertices.push_back(
                {{world.x, world.y, world.z, color.r, color.g, color.b, 1, normal.x, normal.y, normal.z},
                 float(material),
                 ao,
                 {dot(p[i], tangent) * tiling[material], dot(p[i], bitangent) * tiling[material]},
                 t,
                 {}});
        }
    }

    void quad(Vec3 a, Vec3 b, Vec3 c, Vec3 d, Color color, int material = Matte, float ao = 1) {
        // Large static faces need interior lighting samples; water keeps its inexpensive plane.
        int columns = material == Water ? 1 : std::clamp(int(std::ceil(length(b - a) / .8f)), 1, 20);
        int rows = material == Water ? 1 : std::clamp(int(std::ceil(length(d - a) / .8f)), 1, 20);
        auto point = [&](int x, int y) {
            float u = float(x) / columns, v = float(y) / rows;
            return (a + (b - a) * u) * (1 - v) + (d + (c - d) * u) * v;
        };
        for (int y = 0; y < rows; ++y)
            for (int x = 0; x < columns; ++x) {
                Vec3 p = point(x, y), q = point(x + 1, y), r = point(x + 1, y + 1), s = point(x, y + 1);
                triangle(p, q, r, color, material, {}, {}, {}, ao);
                triangle(p, r, s, color, material, {}, {}, {}, ao);
            }
    }
    void box(Vec3 center, Vec3 size, Color color, int material = Matte, float bevel = .025f) {
        const float h[]{size.x * .5f, size.y * .5f, size.z * .5f};
        bevel = std::clamp(bevel, 0.f, std::min({h[0], h[1], h[2]}) * .8f);
        auto point = [&](float x, float y, float z) { return center + Vec3{x, y, z}; };
        // Face centres, edge chamfers and corner triangles form a closed bevelled solid.
        for (int axis = 0; axis < 3; ++axis)
            for (float sign : {-1.f, 1.f}) {
                int u = (axis + 1) % 3, v = (axis + 2) % 3;
                Vec3 p[4];
                const float us[]{-1, 1, 1, -1}, vs[]{-1, -1, 1, 1};
                for (int j = 0; j < 4; ++j) {
                    float q[3]{};
                    q[axis] = sign * h[axis];
                    q[u] = us[j] * (h[u] - bevel);
                    q[v] = vs[j] * (h[v] - bevel);
                    p[j] = point(q[0], q[1], q[2]);
                }
                if (sign < 0)
                    std::swap(p[1], p[3]);
                quad(p[0], p[1], p[2], p[3], color, material);
            }
        if (bevel <= 0)
            return;
        for (int axis = 0; axis < 3; ++axis)
            for (float su : {-1.f, 1.f})
                for (float sv : {-1.f, 1.f}) {
                    int u = (axis + 1) % 3, v = (axis + 2) % 3;
                    Vec3 p[4];
                    for (int j = 0; j < 4; ++j) {
                        float q[3]{};
                        q[axis] = (j < 2 ? -1 : 1) * (h[axis] - bevel);
                        bool towardU = j == 0 || j == 3;
                        q[u] = su * (h[u] - (towardU ? 0 : bevel));
                        q[v] = sv * (h[v] - (towardU ? bevel : 0));
                        p[j] = point(q[0], q[1], q[2]);
                    }
                    // Choose the outward normal without relying on the permutation parity.
                    Vec3 outward{};
                    float q[3]{};
                    q[u] = su;
                    q[v] = sv;
                    outward = {q[0], q[1], q[2]};
                    if (dot(cross(p[1] - p[0], p[2] - p[0]), outward) < 0)
                        std::swap(p[1], p[3]);
                    quad(p[0], p[1], p[2], p[3], color, material);
                }
        for (float x : {-1.f, 1.f})
            for (float y : {-1.f, 1.f})
                for (float z : {-1.f, 1.f}) {
                    Vec3 p[]{point(x * h[0], y * (h[1] - bevel), z * (h[2] - bevel)),
                             point(x * (h[0] - bevel), y * h[1], z * (h[2] - bevel)),
                             point(x * (h[0] - bevel), y * (h[1] - bevel), z * h[2])};
                    if (dot(cross(p[1] - p[0], p[2] - p[0]), Vec3{x, y, z}) < 0)
                        std::swap(p[1], p[2]);
                    triangle(p[0], p[1], p[2], color, material);
                }
    }
    void sphere(Vec3 center, Vec3 radii, Color color, int material = Matte, int sides = 20, int rings = 10) {
        auto unit = [](float u, float v) {
            return Vec3{std::sin(v) * std::cos(u), std::cos(v), std::sin(v) * std::sin(u)};
        };
        auto p = [&](Vec3 n) { return center + Vec3{n.x * radii.x, n.y * radii.y, n.z * radii.z}; };
        auto n = [&](Vec3 a) { return normalized(Vec3{a.x / radii.x, a.y / radii.y, a.z / radii.z}); };
        for (int j = 0; j < rings; ++j)
            for (int i = 0; i < sides; ++i) {
                Vec3 a = unit(i * 2 * pi / sides, j * pi / rings),
                     b = unit((i + 1) * 2 * pi / sides, j * pi / rings),
                     c = unit((i + 1) * 2 * pi / sides, (j + 1) * pi / rings),
                     d = unit(i * 2 * pi / sides, (j + 1) * pi / rings);
                if (j > 0)
                    triangle(p(a), p(b), p(c), color, material, n(a), n(b), n(c));
                if (j < rings - 1)
                    triangle(p(a), p(c), p(d), color, material, n(a), n(c), n(d));
            }
    }
    void cylinder(Vec3 bottom, float height, float radius, float topRadius, Color color, int material = Matte,
                  int sides = 24) {
        for (int i = 0; i < sides; ++i) {
            float a = 2 * pi * i / sides, b = 2 * pi * (i + 1) / sides;
            Vec3 u{std::cos(a), 0, std::sin(a)}, v{std::cos(b), 0, std::sin(b)}, up{0, height, 0};
            Vec3 p = bottom + u * radius, q = bottom + v * radius, r = bottom + up + v * topRadius,
                 s = bottom + up + u * topRadius;
            Vec3 nu = normalized(u + Vec3{0, (radius - topRadius) / height, 0}),
                 nv = normalized(v + Vec3{0, (radius - topRadius) / height, 0});
            triangle(p, r, q, color, material, nu, nv, nv);
            triangle(p, s, r, color, material, nu, nu, nv);
            triangle(bottom, p, q, color, material);
            triangle(bottom + up, r, s, color, material);
        }
    }
    void beam(Vec3 a, Vec3 b, float radius, Color color, int material = Metal, int sides = 12) {
        Vec3 axis = normalized(b - a),
             u = normalized(cross(axis, std::abs(axis.y) < .9f ? Vec3{0, 1, 0} : Vec3{1, 0, 0})),
             v = cross(axis, u);
        for (int i = 0; i < sides; ++i) {
            auto ring = [&](int k) {
                float t = k * 2 * pi / sides;
                return u * std::cos(t) + v * std::sin(t);
            };
            Vec3 p = ring(i), q = ring(i + 1);
            triangle(a + p * radius, b + q * radius, b + p * radius, color, material, p, q, p);
            triangle(a + p * radius, a + q * radius, b + q * radius, color, material, p, q, q);
            triangle(a, a + q * radius, a + p * radius, color, material);
            triangle(b, b + p * radius, b + q * radius, color, material);
        }
    }
    void ring(Vec3 center, float radius, float width, Color color, int material = Matte, int sides = 96) {
        // Flat and facing up, but ceiling and planet rings are seen from below.
        open = true;
        for (int i = 0; i < sides; ++i) {
            float a = i * 2 * pi / sides, b = (i + 1) * 2 * pi / sides;
            auto p = [&](float t, float r) { return center + Vec3{std::cos(t) * r, 0, std::sin(t) * r}; };
            quad(p(a, radius - width), p(b, radius - width), p(b, radius), p(a, radius), color, material);
        }
        open = false;
    }
    void plant(Vec3 base, float scale = 1) {
        Color pot = Color::hex(0xc5b8a0), green = Color::hex(0x496d46);
        cylinder(base, .40f * scale, .20f * scale, .27f * scale, pot, Ceramic);
        cylinder(base + Vec3{0, .405f * scale, 0}, .015f * scale, .245f * scale, .245f * scale,
                 Color::hex(0x35372c));
        for (int i = 0; i < 13; ++i) {
            float angle = i * 2.399963f, spread = (.30f + .10f * (i % 3)) * scale,
                  height = (.55f + .10f * (i % 5)) * scale;
            Vec3 a = base + Vec3{0, .38f * scale, 0},
                 b = base + Vec3{std::cos(angle) * spread, height + .35f * scale, std::sin(angle) * spread};
            Vec3 mid = a + (b - a) * .60f + Vec3{0, .20f * scale, 0},
                 side = Vec3{-std::sin(angle), 0, std::cos(angle)} * (.065f * scale);
            auto tint = green;
            float variation = .8f + .07f * (i % 4);
            tint.r *= variation;
            tint.g *= variation;
            tint.b *= variation;
            open = true;
            triangle(a, mid - side, mid + side, tint, Leaf);
            triangle(mid - side, b, mid + side, tint, Leaf);
            open = false;
            beam(a, b, .006f * scale, tint, Leaf, 5);
        }
    }
    void sofa(Vec3 position, float yaw, Color fabric) {
        Pose saved = group;
        group = {position, yawRotation(yaw)};
        Color frame = Color::hex(0x65513b), metal = Color::hex(0x302f2a);
        for (float x : {-.83f, .83f})
            for (float z : {-.32f, .32f})
                box({x, .12f, z}, {.08f, .24f, .08f}, metal, Metal, .01f);
        box({0, .28f, 0}, {2.15f, .22f, .95f}, frame, Wood, .06f);
        for (float x : {-.48f, .48f})
            box({x, .47f, -.04f}, {.94f, .26f, .78f}, fabric, Fabric, .075f);
        box({0, .79f, .36f}, {2.10f, .66f, .23f}, fabric, Fabric, .075f);
        for (float x : {-1.f, 1.f})
            box({x, .60f, 0}, {.20f, .55f, 1.f}, fabric, Fabric, .055f);
        box({-.66f, .74f, .12f}, {.38f, .38f, .15f}, Color::hex(0xb56d4f), Fabric, .055f);
        box({.66f, .74f, .12f}, {.38f, .38f, .15f}, Color::hex(0xd2c5a9), Fabric, .055f);
        group = saved;
    }
    void table(Vec3 base, float radius = .48f) {
        cylinder(base, .42f, .16f, .12f, Color::hex(0x74604a), Wood);
        cylinder(base + Vec3{0, .42f, 0}, .065f, radius, radius, Color::hex(0xb9a98e), Ceramic, 40);
        cylinder(base + Vec3{.12f, .485f, .03f}, .14f, .065f, .060f, Color::hex(0xe1d8c3), Ceramic, 16);
    }
    void hills(bool sunset) {
        const Color shades[]{Color::hex(sunset ? 0x8f8e9d : 0xaababa),
                             Color::hex(sunset ? 0x626c7d : 0x7d9684),
                             Color::hex(sunset ? 0x414e62 : 0x526e5d)};
        for (int row = 0; row < 3; ++row)
            for (int i = 0; i < 72; ++i) {
                float x = (i - 36) * .85f, z = -29.f + row * 4.f;
                auto height = [&](float a) {
                    return .8f + std::sin(a * .21f + row) * .65f + std::sin(a * .49f + row * 2) * .27f +
                           std::sin(a * .94f) * .12f;
                };
                size_t first = mesh.vertices.size();
                quad({x, -.7f, z}, {x + .85f, -.7f, z}, {x + .85f, height(x + .85f), z - 1.8f},
                     {x, height(x), z - 1.8f}, shades[row]);
                for (size_t j = first; j < mesh.vertices.size(); ++j) {
                    auto &v = mesh.vertices[j].surface;
                    float light = .94f + .06f * std::sin(v.x * .7f);
                    v.r *= light;
                    v.g *= light;
                    v.b *= light;
                }
            }
    }
    void lounge() {
        Color wall = Color::hex(0xc9c6b8), wood = Color::hex(0x998165), dark = Color::hex(0x3f4a44),
              rug = Color::hex(0xb5b09c);
        box({0, -.15f, -.8f}, {8.8f, .30f, 10.4f}, wood, Wood, 0);
        for (int i = 0; i < 32; ++i) {
            float x = -4.4f + (i + .5f) * .275f;
            auto c = wood;
            float v = .94f + .02f * (i % 5);
            c.r *= v;
            c.g *= v;
            c.b *= v;
            for (int j = 0; j < 30; ++j) {
                float z = -6.f + j * 10.4f / 30;
                quad({x - .1335f, .004f, z}, {x - .1335f, .004f, z + 10.4f / 30},
                     {x + .1335f, .004f, z + 10.4f / 30}, {x + .1335f, .004f, z}, c, Wood);
            }
        }
        box({-4.45f, 1.8f, -.8f}, {.15f, 3.6f, 10.4f}, wall);
        box({4.45f, 1.8f, -.8f}, {.15f, 3.6f, 10.4f}, wall);
        box({0, 1.8f, 4.45f}, {9, .15f, 0.15f}, wall); // rear shelf rail
        box({0, 1.8f, 4.5f}, {8.8f, 3.6f, .15f}, wall);
        box({0, 3.65f, -.8f}, {9, .18f, 10.6f}, Color::hex(0xe2dfd1));
        for (float x : {-4.25f, 4.25f}) {
            box({x, .08f, -.8f}, {.12f, .16f, 10.4f}, dark, Wood, .01f);
            box({x, 3.39f, -.8f}, {.10f, .035f, 10.0f}, Color::hex(0xffdd9d), Glow, .008f);
        }
        // A real opening with separate mullions: landscape stays beyond the room.
        for (float x : {-4.32f, -2.15f, 0.f, 2.15f, 4.32f})
            box({x, 1.8f, -5.9f}, {.07f, 3.6f, .14f}, dark, Metal, .012f);
        for (float y : {.08f, 3.49f})
            box({0, y, -5.9f}, {8.8f, .10f, .16f}, dark, Metal, .01f);
        for (float x : {-4.2f, 4.2f})
            box({x, 1.8f, -5.75f}, {.22f, 3.5f, .22f}, Color::hex(0xb5ad96), Fabric);
        hills(false);
        box({0, -.12f, -10}, {24, .18f, 9}, Color::hex(0x7d9672), Matte, 0);
        box({0, .009f, -.65f}, {3.7f, .018f, 4.4f}, rug, Fabric, .008f);
        for (float x : {-1.72f, 1.72f})
            box({x, .021f, -.65f}, {.035f, .004f, 4.1f}, Color::hex(0x8e8b78), Fabric, 0);
        sofa({-3.10f, 0, -2.35f}, -pi * .5f, Color::hex(0xc7c9b9));
        sofa({3.10f, 0, -2.35f}, pi * .5f, Color::hex(0x647c72));
        table({2.38f, 0, -4.15f}, .50f);
        table({-2.4f, 0, -4.2f}, .42f);
        plant({-3.6f, 0, -4.8f}, 1.5f);
        plant({3.6f, 0, -4.8f}, 1.35f);
        plant({3.6f, 0, 2.8f}, 1.6f);
        for (int i = 0; i < 9; ++i) {
            float x = -11.f + i * 2.7f, z = -12.f - std::sin(i * 1.6f) * 2.f, h = 2.4f + .35f * (i % 3);
            cylinder({x, 0, z}, h * .7f, .11f, .07f, Color::hex(0x696448), Wood, 8);
            sphere({x, h, z}, {1.2f, .95f, 1.2f}, Color::hex(0x648360), Leaf, 12, 6);
            sphere({x - .5f, h - .6f, z + .2f}, {.8f, .8f, .8f}, Color::hex(0x587855), Leaf, 10, 5);
        }
        for (int i = 0; i < 20; ++i)
            box({-4.32f, 1.65f, -3.2f + i * .18f}, {.055f, 2.8f, .06f}, wood, Wood, .01f);
        cylinder({2.6f, 0, -.8f}, .04f, .21f, .21f, dark, Metal);
        cylinder({2.6f, .04f, -.8f}, 1.68f, .016f, .016f, Color::hex(0xb49a5d), Metal, 12);
        cylinder({2.6f, 1.72f, -.8f}, .32f, .29f, .17f, Color::hex(0xe8d6ad), Fabric);
        sphere({2.6f, 1.78f, -.8f}, {.10f, .06f, .10f}, Color::hex(0xffdc9e), Glow);
        // Books and a low console behind the user make the room complete in 360 degrees.
        box({0, .42f, 4.12f}, {3.1f, .72f, .54f}, wood, Wood, .04f);
        for (int i = 0; i < 9; ++i)
            box({-.9f + i * .13f, .89f, 4.05f}, {.09f, .25f + .05f * (i % 3), .20f},
                Color::hex(i % 2 ? 0x546f68 : 0x956652), Fabric, .008f);
        plant({1.1f, .79f, 4.02f}, .65f);
    }
    void observatory() {
        Color floor = Color::hex(0x202b38), shell = Color::hex(0x354455), edge = Color::hex(0x768594),
              cyan = Color::hex(0x69cfdb);
        cylinder({0, -.20f, 0}, .20f, 5.8f, 5.8f, floor, Metal, 64);
        ring({0, .006f, 0}, 4.8f, .06f, cyan, Glow);
        ring({0, .009f, 0}, 2.2f, .025f, Color::hex(0x4d6875), Metal);
        for (int i = 0; i < 12; ++i) {
            float a = i * 2 * pi / 12;
            group = {{0, 0, 0}, yawRotation(a)};
            box({0, .004f, 3.55f}, {.024f, .008f, 2.35f}, edge, Metal, 0);
            box({0, .42f, 5.45f}, {2.75f, .84f, .45f}, shell, Metal, .10f);
            box({0, .87f, 5.42f}, {2.45f, .018f, .08f}, cyan, Glow, .005f);
            if (i < 4 || i > 8) {
                beam({-1.15f, .85f, 5.25f}, {-.85f, 3.2f, 4.8f}, .10f, edge, Metal);
                beam({.85f, 3.2f, 4.8f}, {1.15f, .85f, 5.25f}, .10f, edge, Metal);
            }
        }
        group = {};
        ring({0, 3.45f, 0}, 5.18f, .32f, shell, Metal);
        ring({0, 3.42f, 0}, 4.92f, .035f, cyan, Glow);
        for (float x : {-3.1f, 3.1f}) {
            box({x, .26f, -1.9f}, {1.55f, .52f, 2.f}, shell, Metal, .15f);
            box({x, .61f, -1.9f}, {1.42f, .18f, 1.88f}, Color::hex(0x49515a), Fabric, .06f);
            box({x, .14f, -1.9f}, {1.36f, .025f, 1.65f}, cyan, Glow, .009f);
            cylinder({x, 0, -3.5f}, .8f, .35f, .25f, shell, Metal);
            cylinder({x, .8f, -3.5f}, .08f, .50f, .50f, edge, Metal, 32);
            cylinder({x, .887f, -3.5f}, .008f, .37f, .37f, cyan, Glow, 32);
        }
        // Forward aperture, planet and moon are geometric objects at different depths.
        beam({-4.2f, 1.2f, -3.7f}, {-3.0f, 3.7f, -3.8f}, .13f, edge);
        beam({-3.0f, 3.7f, -3.8f}, {3.0f, 3.7f, -3.8f}, .13f, edge);
        beam({3.0f, 3.7f, -3.8f}, {4.2f, 1.2f, -3.7f}, .13f, edge);
        beam({-3.f, 3.53f, -3.78f}, {3.f, 3.53f, -3.78f}, .025f, cyan, Glow);
        sphere({0, 5.8f, -21.f}, {4.8f, 4.8f, 4.8f}, Color::hex(0x7aa6b5), Planet, 48, 24);
        ring({0, 5.8f, -21.f}, 7.3f, .65f, Color::hex(0xadbab9), Ceramic, 128);
        ring({0, 5.8f, -21.f}, 6.55f, .42f, Color::hex(0x798f95), Ceramic, 128);
        sphere({8.5f, 5.5f, -22.f}, {1.15f, 1.15f, 1.15f}, Color::hex(0xaca89e), Matte, 24, 12);
        box({0, .55f, 3.8f}, {2.6f, 1.1f, .68f}, shell, Metal, .1f);
        for (int i = 0; i < 9; ++i)
            box({-.88f + i * .22f, 1.115f, 3.76f}, {.12f, .012f, .23f},
                Color::hex(i % 3 ? 0x62b4c8 : 0xe3ad74), Glow, .005f);
    }
    void terrace() {
        Color deck = Color::hex(0x8c725b), stone = Color::hex(0xaaa99c), rail = Color::hex(0x384647);
        box({0, -.17f, -.4f}, {9.6f, .34f, 10.f}, deck, Wood, 0);
        for (int i = 0; i < 36; ++i) {
            auto color = deck;
            float v = .94f + .022f * (i % 5);
            color.r *= v;
            color.g *= v;
            color.b *= v;
            float x = -4.8f + (i + .5f) * 9.6f / 36, w = (9.6f / 36 - .008f) * .5f;
            for (int j = 0; j < 30; ++j) {
                float z = -5.4f + j / 3.f;
                quad({x - w, .004f, z}, {x - w, .004f, z + 1 / 3.f}, {x + w, .004f, z + 1 / 3.f},
                     {x + w, .004f, z}, color, Wood);
            }
        }
        for (int i = 0; i < 9; ++i) {
            float x = -4.6f + i * 1.15f;
            box({x, .49f, -5.3f}, {.045f, .98f, .045f}, rail, Metal, .008f);
        }
        for (float x : {-4.6f, 4.6f})
            for (int i = 0; i < 8; ++i)
                box({x, .49f, -4.15f + i * 1.2f}, {.045f, .98f, .045f}, rail, Metal, .008f);
        for (float y : {.18f, .60f, 1.0f}) {
            box({0, y, -5.3f}, {9.25f, .025f, .025f}, rail, Metal, .005f);
            for (float x : {-4.6f, 4.6f})
                box({x, y, -.4f}, {.025f, .025f, 9.8f}, rail, Metal, .005f);
        }
        box({0, -.55f, -18}, {65, .08f, 40}, Color::hex(0x6c9297), Water, 0);
        hills(true);
        // Pergola and seating remain outside the central standing/panel area.
        for (float x : {-4.1f, -1.85f})
            for (float z : {-4.3f, .7f})
                box({x, 1.5f, z}, {.13f, 3.f, .13f}, deck, Wood, .015f);
        for (int i = 0; i < 15; ++i)
            box({-2.98f, 3.03f, -4.4f + i * .37f}, {2.65f, .14f, .11f}, deck, Wood, .015f);
        sofa({-3.1f, 0, -2.f}, -pi * .5f, Color::hex(0xd4cbb7));
        table({-2.38f, 0, -4.05f}, .46f);
        sofa({3.2f, 0, -2.9f}, pi * .5f, Color::hex(0x9ca99d));
        table({2.65f, 0, -.85f}, .48f);
        for (float x : {-3.7f, 3.7f}) {
            box({x, .28f, -4.55f}, {.85f, .56f, .72f}, stone, Ceramic, .04f);
            for (int i = 0; i < 4; ++i)
                plant({x - .25f + i * .17f, .56f, -4.55f}, .65f);
        }
        plant({3.8f, 0, 2.5f}, 1.7f);
        plant({-3.8f, 0, 2.5f}, 1.6f);
        for (int i = 0; i < 8; ++i) {
            float z = -3.9f + i * .6f;
            beam({-4.06f, 2.55f, z}, {-1.9f, 2.55f, z}, .006f, rail, Metal, 6);
            sphere({-2.95f, 2.54f, z}, {.035f, .045f, .035f}, Color::hex(0xffd899), Glow, 10, 5);
        }
        box({0, .02f, 3.65f}, {4.3f, .04f, 1.4f}, stone, Ceramic, .02f);
    }
};
} // namespace
bool isScene3D(Background background) {
    return background == Background::Lounge || background == Background::Observatory ||
           background == Background::Terrace;
}
SceneMesh buildScene(Background background, bool fallbackLighting) {
    Builder b;
    if (background == Background::Lounge)
        b.lounge();
    else if (background == Background::Observatory)
        b.observatory();
    else if (background == Background::Terrace)
        b.terrace();
    if (fallbackLighting && isScene3D(background))
        b.bake(background);
    if (b.mesh.vertices.size() > 120000)
        throw std::runtime_error("Environment geometry budget exceeded");
    return std::move(b.mesh);
}
uint32_t moveOpenTrianglesLast(SceneMesh &mesh) {
    std::vector<bool> open(mesh.vertices.size() / 3);
    for (uint32_t first : mesh.openTriangles)
        open[first / 3] = true;
    // Back faces hide hairline seams; lights would show them brightly, and cost almost nothing to shade.
    for (size_t triangle = 0; triangle < open.size(); ++triangle)
        if (mesh.vertices[triangle * 3].material == float(Glow))
            open[triangle] = true;
    std::vector<SceneVertex> ordered;
    ordered.reserve(mesh.vertices.size());
    for (bool wanted : {false, true})
        for (size_t triangle = 0; triangle < open.size(); ++triangle)
            if (open[triangle] == wanted)
                ordered.insert(ordered.end(), mesh.vertices.begin() + triangle * 3,
                               mesh.vertices.begin() + (triangle + 1) * 3);
    const auto closed = uint32_t(ordered.size() - std::count(open.begin(), open.end(), true) * 3);
    mesh.vertices = std::move(ordered);
    mesh.openTriangles.clear();
    return closed;
}
Pose relativePose(Pose origin, Pose pose) {
    Quat inverse{-origin.orientation.x, -origin.orientation.y, -origin.orientation.z, origin.orientation.w};
    return {rotate(inverse, pose.position - origin.position), multiply(inverse, pose.orientation)};
}
Pose placeScene(Pose head, float floorY) {
    Vec3 forward = rotate(head.orientation, {0, 0, -1});
    float yaw = std::atan2(-forward.x, -forward.z);
    return {{head.position.x, floorY, head.position.z}, yawRotation(yaw)};
}
Mat4 modelProjection(const Mat4 &vp, Pose model) {
    Vec3 x = rotate(model.orientation, {1, 0, 0}), y = rotate(model.orientation, {0, 1, 0}),
         z = rotate(model.orientation, {0, 0, 1}), t = model.position;
    Mat4 transform{x.x, x.y, x.z, 0, y.x, y.y, y.z, 0, z.x, z.y, z.z, 0, t.x, t.y, t.z, 1}, out{};
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r)
            for (int k = 0; k < 4; ++k)
                out[c * 4 + r] += vp[k * 4 + r] * transform[c * 4 + k];
    return out;
}
Mat4 rectangleProjection(Vec3 eye, Vec3 direction, Vec3 right, Vec3 up) {
    float w = length(right), h = length(up);
    Vec3 r = normalized(right), u = normalized(up), f = normalized(cross(u, r));
    float depth = dot(direction, f);
    if (w < .001f || h < .001f || depth < spatialNearZ)
        return {};
    Vec3 a = r * (depth / w) - f * (dot(direction, r) / w),
         b = u * (-depth / h) + f * (dot(direction, u) / h),
         c = f * (spatialFarZ / (spatialFarZ - spatialNearZ));
    return {
        a.x,          b.x,          c.x,
        f.x,          a.y,          b.y,
        c.y,          f.y,          a.z,
        b.z,          c.z,          f.z,
        -dot(a, eye), -dot(b, eye), -dot(c, eye) - spatialNearZ * spatialFarZ / (spatialFarZ - spatialNearZ),
        -dot(f, eye)};
}
} // namespace cyvr
