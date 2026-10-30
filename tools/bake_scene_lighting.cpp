// SPDX-License-Identifier: MIT
// Offline authoring tool. Uses the actual scene triangles, not runtime box proxies.
#include "cyvr/pbr.hpp"
#include "cyvr/scene.hpp"
#include "cyvr/scene_lighting.hpp"
#include <atomic>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <numeric>
#include <thread>
#include <unordered_map>

using namespace cyvr;
namespace {
Vec3 cross(Vec3 a, Vec3 b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
Vec3 product(Vec3 a, Vec3 b) {
    return {a.x * b.x, a.y * b.y, a.z * b.z};
}
Vec3 minimum(Vec3 a, Vec3 b) {
    return {std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z)};
}
Vec3 maximum(Vec3 a, Vec3 b) {
    return {std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z)};
}
float component(Vec3 a, int axis) {
    return axis == 0 ? a.x : axis == 1 ? a.y : a.z;
}
float luminance(Vec3 a) {
    return dot(a, {.2126f, .7152f, .0722f});
}
float radical(uint32_t i) {
    i = (i << 16) | (i >> 16);
    i = ((i & 0x55555555u) << 1) | ((i & 0xaaaaaaaau) >> 1);
    i = ((i & 0x33333333u) << 2) | ((i & 0xccccccccu) >> 2);
    i = ((i & 0x0f0f0f0fu) << 4) | ((i & 0xf0f0f0f0u) >> 4);
    i = ((i & 0x00ff00ffu) << 8) | ((i & 0xff00ff00u) >> 8);
    return float(double(i) / 4294967296.0);
}
Vec3 hemisphere(Vec3 n, float u, float v) {
    Vec3 t = normalized(cross(std::abs(n.y) < .95f ? Vec3{0, 1, 0} : Vec3{0, 0, 1}, n)), b = cross(n, t);
    float r = std::sqrt(u), angle = v * 2 * pi;
    return t * (r * std::cos(angle)) + b * (r * std::sin(angle)) + n * std::sqrt(1 - u);
}
Vec3 linear(Vec3 c) {
    auto f = [](float v) { return v <= .04045f ? v / 12.92f : std::pow((v + .055f) / 1.055f, 2.4f); };
    return {f(c.x), f(c.y), f(c.z)};
}
struct Triangle {
    Vec3 a, b, c, na, nb, nc, reflectance, emission, low, high, center, normal;
    float area{};
};
struct Hit {
    int triangle{-1};
    float distance{}, u{}, v{};
};
struct Node {
    Vec3 low, high;
    int begin{}, end{}, left{-1}, right{-1};
};
struct World {
    std::vector<Triangle> triangles;
    std::vector<Node> nodes;
    struct Light {
        int triangle;
        float cumulative, probability;
    };
    std::vector<Light> lights;
    float lightWeight{};
    Background background;
    explicit World(const SceneMesh &mesh, Background scene) : background(scene) {
        for (size_t i = 0; i < mesh.vertices.size(); i += 3) {
            auto a = mesh.vertices[i].surface, b = mesh.vertices[i + 1].surface,
                 c = mesh.vertices[i + 2].surface;
            Triangle t{};
            t.a = {a.x, a.y, a.z};
            t.b = {b.x, b.y, b.z};
            t.c = {c.x, c.y, c.z};
            t.na = {a.nx, a.ny, a.nz};
            t.nb = {b.nx, b.ny, b.nz};
            t.nc = {c.nx, c.ny, c.nz};
            t.normal = normalized(cross(t.b - t.a, t.c - t.a));
            t.area = length(cross(t.b - t.a, t.c - t.a)) * .5f;
            t.low = minimum(t.a, minimum(t.b, t.c));
            t.high = maximum(t.a, maximum(t.b, t.c));
            t.center = (t.a + t.b + t.c) * (1.f / 3);
            t.reflectance = linear({(a.r + b.r + c.r) / 3, (a.g + b.g + c.g) / 3, (a.b + b.b + c.b) / 3});
            int material = int(mesh.vertices[i].material);
            if (material == 4) {
                t.emission = t.reflectance * 12.f;
                t.reflectance = {};
            }
            if (material == 3)
                t.reflectance = {}; // No diffuse bounce from a conductor.
            triangles.push_back(t);
        }
        nodes.reserve(triangles.size());
        build(0, int(triangles.size()));
        for (size_t i = 0; i < triangles.size(); ++i) {
            float w = triangles[i].area * luminance(triangles[i].emission);
            if (w > 0) {
                lightWeight += w;
                lights.push_back({int(i), lightWeight, w});
            }
        }
        for (auto &light : lights) {
            light.cumulative /= lightWeight;
            light.probability /= lightWeight;
        }
    }
    int build(int begin, int end) {
        Vec3 low{1e9f, 1e9f, 1e9f}, high{-1e9f, -1e9f, -1e9f}, clow = low, chigh = high;
        for (int i = begin; i < end; ++i) {
            low = minimum(low, triangles[i].low);
            high = maximum(high, triangles[i].high);
            clow = minimum(clow, triangles[i].center);
            chigh = maximum(chigh, triangles[i].center);
        }
        int index = int(nodes.size());
        nodes.push_back({low, high, begin, end});
        if (end - begin > 6) {
            Vec3 size = chigh - clow;
            int axis = size.x > size.y && size.x > size.z ? 0 : size.y > size.z ? 1 : 2;
            int middle = (begin + end) / 2;
            std::nth_element(triangles.begin() + begin, triangles.begin() + middle, triangles.begin() + end,
                             [&](const Triangle &a, const Triangle &b) {
                                 return component(a.center, axis) < component(b.center, axis);
                             });
            int left = build(begin, middle), right = build(middle, end);
            nodes[index].left = left;
            nodes[index].right = right;
        }
        return index;
    }
    Hit trace(Vec3 origin, Vec3 direction, float limit = 100.f) const {
        Vec3 inv{1 / (std::abs(direction.x) < 1e-8f ? 1e-8f : direction.x),
                 1 / (std::abs(direction.y) < 1e-8f ? 1e-8f : direction.y),
                 1 / (std::abs(direction.z) < 1e-8f ? 1e-8f : direction.z)};
        Hit hit{-1, limit, 0, 0};
        int stack[64], count = 1;
        stack[0] = 0;
        while (count) {
            const auto &node = nodes[stack[--count]];
            Vec3 a = product(node.low - origin, inv), b = product(node.high - origin, inv);
            Vec3 near = minimum(a, b), far = maximum(a, b);
            if (std::max({near.x, near.y, near.z, 0.f}) > std::min({far.x, far.y, far.z, hit.distance}))
                continue;
            if (node.left >= 0) {
                stack[count++] = node.left;
                stack[count++] = node.right;
                continue;
            }
            for (int i = node.begin; i < node.end; ++i) {
                const auto &t = triangles[i];
                Vec3 edge = t.b - t.a, other = t.c - t.a;
                Vec3 p = cross(direction, other);
                float det = dot(edge, p);
                if (std::abs(det) < 1e-9f)
                    continue;
                float inverse = 1 / det;
                Vec3 q = origin - t.a;
                float u = dot(q, p) * inverse;
                if (u < 0 || u > 1)
                    continue;
                Vec3 r = cross(q, edge);
                float v = dot(direction, r) * inverse;
                if (v < 0 || u + v > 1)
                    continue;
                float distance = dot(other, r) * inverse;
                if (distance > .00015f && distance < hit.distance)
                    hit = {i, distance, u, v};
            }
        }
        return hit;
    }
    Vec3 areaLight(Vec3 p, Vec3 n, float choose, float u, float v) const {
        if (lights.empty())
            return {};
        auto it = std::lower_bound(lights.begin(), lights.end(), choose,
                                   [](const Light &l, float value) { return l.cumulative < value; });
        if (it == lights.end())
            it = lights.end() - 1;
        const auto &t = triangles[it->triangle];
        float r = std::sqrt(u);
        Vec3 point = t.a * (1 - r) + t.b * (r * (1 - v)) + t.c * (r * v);
        Vec3 delta = point - p;
        float d2 = dot(delta, delta);
        if (d2 < .0001f)
            return {};
        float distance = std::sqrt(d2);
        Vec3 direction = delta * (1 / distance);
        float cosine = std::max(0.f, dot(n, direction)) * std::abs(dot(t.normal, direction));
        if (cosine <= 0 || trace(p, direction, distance - .0025f).triangle >= 0)
            return {};
        return t.emission * (cosine * t.area / (pi * d2 * it->probability));
    }
    struct Lighting {
        float sun{1}, ao{1};
        Vec3 indirect;
    };
    Lighting sample(Vec3 position, Vec3 n, int samples) const {
        Vec3 p = position + n * .002f, sun = sceneSunDirection(background),
             radiance = sceneSunRadiance(background);
        Lighting output{};
        if (background == Background::Observatory && position.z < -12.f) {
            // The astronomical backdrop is staged geometry: foreground rails must not shadow a planet.
            for (int i = 0; i < 128; ++i)
                output.indirect =
                    output.indirect +
                    sceneSkyRadiance(background, hemisphere(n, (i + .5f) / 128, radical(i))) * (1.f / 128);
            return output;
        }
        // Continuous emitter geometry; no point-source hot spots along long cove strips.
        int lightSamples = std::max(1024, samples * 4);
        for (int i = 0; i < lightSamples; ++i)
            output.indirect = output.indirect + areaLight(p, n, (i + .5f) / lightSamples, radical(i),
                                                          std::fmod((i + .5f) * .6180339887f, 1.f)) *
                                                    (1.f / lightSamples);
        output.sun = 0;
        output.ao = 0;
        for (int i = 0; i < 64; ++i) {
            Vec3 t = normalized(cross({0, 1, 0}, sun)), b = cross(sun, t);
            float angle = radical(i) * 2 * pi, r = std::sqrt((i + .5f) / 64) * .018f;
            auto d = normalized(sun + t * (r * std::cos(angle)) + b * (r * std::sin(angle)));
            if (trace(p, d).triangle < 0)
                output.sun += 1.f / 64;
        }
        for (int i = 0; i < samples; ++i) {
            Vec3 direction = hemisphere(n, (i + .5f) / samples, radical(i));
            Vec3 origin = p, throughput{1, 1, 1}, value{};
            for (int bounce = 0; bounce < 2; ++bounce) {
                auto hit = trace(origin, direction);
                if (bounce == 0)
                    output.ao +=
                        (hit.triangle < 0 ? 1.f : std::clamp(hit.distance / .8f, .05f, 1.f)) / samples;
                if (hit.triangle < 0) {
                    value = value + product(throughput, sceneSkyRadiance(background, direction));
                    break;
                }
                const auto &tri = triangles[hit.triangle];
                // Area light next-event estimates already include emitter hits; do not count twice.
                if (luminance(tri.emission) > 0)
                    break;
                Vec3 normal = normalized(tri.na * (1 - hit.u - hit.v) + tri.nb * hit.u + tri.nc * hit.v);
                if (dot(normal, direction) > 0)
                    normal = normal * -1;
                origin = origin + direction * hit.distance + normal * .002f;
                throughput = product(throughput, tri.reflectance);
                float nl = std::max(0.f, dot(normal, sun));
                if (nl > 0 && trace(origin, sun).triangle < 0)
                    value = value + product(throughput, radiance) * (nl / pi);
                auto light = areaLight(origin, normal, radical(uint32_t(i + samples * bounce)),
                                       std::fmod((i + .5f) * .754877666f, 1.f),
                                       std::fmod((i + .5f) * .569840296f, 1.f));
                value = value + product(throughput, light);
                direction = hemisphere(normal, std::fmod((i + .5f) * .414213562f, 1.f),
                                       radical(uint32_t(i + 731 + bounce * 1543)));
            }
            output.indirect = output.indirect + value * (1.f / samples);
        }
        return output;
    }
};
struct KeyHash {
    size_t operator()(const std::array<int, 6> &key) const {
        size_t h = 0;
        for (int v : key)
            h = (h * 16777619u) ^ uint32_t(v);
        return h;
    }
};
void bake(Background background, const std::filesystem::path &destination, int samples) {
    auto mesh = buildScene(background, false);
    World world(mesh, background);
    std::unordered_map<std::array<int, 6>, size_t, KeyHash> unique;
    std::vector<size_t> remap;
    std::vector<Vec3> positions, normals;
    for (const auto &vertex : mesh.vertices) {
        const auto &v = vertex.surface;
        std::array<int, 6> key{int(std::round(v.x * 1000)),  int(std::round(v.y * 1000)),
                               int(std::round(v.z * 1000)),  int(std::round(v.nx * 1000)),
                               int(std::round(v.ny * 1000)), int(std::round(v.nz * 1000))};
        auto added = unique.emplace(key, positions.size());
        if (added.second) {
            positions.push_back({v.x, v.y, v.z});
            normals.push_back({v.nx, v.ny, v.nz});
        }
        remap.push_back(added.first->second);
    }
    std::cout << "Baking scene " << int(background) << ": " << positions.size() << " unique samples, "
              << samples << " paths each, actual triangle BVH / area lights" << std::endl;
    std::vector<World::Lighting> values(positions.size());
    std::atomic<size_t> next{};
    std::vector<std::thread> workers;
    for (int i = 0; i < 4; ++i)
        workers.emplace_back([&] {
            for (;;) {
                size_t index = next.fetch_add(1);
                if (index >= positions.size())
                    break;
                values[index] = world.sample(positions[index], normals[index], samples);
            }
        });
    for (auto &worker : workers)
        worker.join();
    // A local coplanar filter reduces residual Monte Carlo noise without crossing surface edges.
    std::unordered_map<std::array<int, 6>, std::vector<size_t>, KeyHash> grid;
    auto cell = [](Vec3 p) {
        return std::array<int, 6>{
            int(std::floor(p.x / .5f)), int(std::floor(p.y / .5f)), int(std::floor(p.z / .5f)), 0, 0, 0};
    };
    for (size_t i = 0; i < positions.size(); ++i)
        grid[cell(positions[i])].push_back(i);
    auto filtered = values;
    for (size_t i = 0; i < positions.size(); ++i) {
        auto c = cell(positions[i]);
        Vec3 sum = values[i].indirect;
        float weight = 1;
        for (int x = -1; x <= 1; ++x)
            for (int y = -1; y <= 1; ++y)
                for (int z = -1; z <= 1; ++z) {
                    auto at = c;
                    at[0] += x;
                    at[1] += y;
                    at[2] += z;
                    auto found = grid.find(at);
                    if (found == grid.end())
                        continue;
                    for (size_t j : found->second) {
                        if (i == j || dot(normals[i], normals[j]) < .985f)
                            continue;
                        Vec3 d = positions[j] - positions[i];
                        float d2 = dot(d, d);
                        if (d2 > .20f || std::abs(dot(d, normals[i])) > .008f)
                            continue;
                        float w = std::exp(-d2 / .06f);
                        sum = sum + values[j].indirect * w;
                        weight += w;
                    }
                }
        filtered[i].indirect = sum * (1 / weight);
    }
    std::ofstream out(destination, std::ios::binary);
    out.write("CYLI", 4);
    auto word = [&](uint32_t v) {
        for (int i = 0; i < 4; ++i)
            out.put(char(v >> (8 * i)));
    };
    word(1);
    word(uint32_t(background));
    word(uint32_t(mesh.vertices.size()));
    for (size_t i : remap) {
        auto v = filtered[i];
        float values[]{v.sun, v.ao, v.indirect.x, v.indirect.y, v.indirect.z};
        for (float &f : values)
            f = std::clamp(f, 0.f, 64.f);
        out.write(reinterpret_cast<const char *>(values), sizeof(values));
    }
    if (!out)
        throw std::runtime_error("Could not save baked lighting");
    std::cout << "Saved " << destination.string() << std::endl;
}
void smoothLighting(Background background, const std::filesystem::path &path) {
    auto mesh = buildScene(background, false);
    std::ifstream input(path, std::ios::binary);
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(input)), {});
    applySceneLighting(mesh, background, bytes);
    std::unordered_map<std::array<int, 6>, size_t, KeyHash> unique;
    std::unordered_map<std::array<int, 6>, std::vector<size_t>, KeyHash> cells;
    std::vector<size_t> remap;
    std::vector<Vec3> positions, normals, lighting;
    auto cell = [](Vec3 p) {
        return std::array<int, 6>{
            int(std::floor(p.x / 1.2f)), int(std::floor(p.y / 1.2f)), int(std::floor(p.z / 1.2f)), 0, 0, 0};
    };
    for (const auto &v : mesh.vertices) {
        const auto &s = v.surface;
        std::array<int, 6> key{int(std::round(s.x * 1000)),  int(std::round(s.y * 1000)),
                               int(std::round(s.z * 1000)),  int(std::round(s.nx * 1000)),
                               int(std::round(s.ny * 1000)), int(std::round(s.nz * 1000))};
        auto added = unique.emplace(key, positions.size());
        if (added.second) {
            positions.push_back({s.x, s.y, s.z});
            normals.push_back({s.nx, s.ny, s.nz});
            lighting.push_back(v.indirect);
            cells[cell(positions.back())].push_back(positions.size() - 1);
        }
        remap.push_back(added.first->second);
    }
    for (int pass = 0; pass < 2; ++pass) {
        auto next = lighting;
        for (size_t i = 0; i < positions.size(); ++i) {
            // Architectural planes only: preserve curved prop shading and geometric surface edges.
            if (std::max({std::abs(normals[i].x), std::abs(normals[i].y), std::abs(normals[i].z)}) < .9995f)
                continue;
            auto c = cell(positions[i]);
            Vec3 sum = lighting[i];
            float weight = 1;
            for (int x = -1; x <= 1; ++x)
                for (int y = -1; y <= 1; ++y)
                    for (int z = -1; z <= 1; ++z) {
                        auto at = c;
                        at[0] += x;
                        at[1] += y;
                        at[2] += z;
                        auto found = cells.find(at);
                        if (found == cells.end())
                            continue;
                        for (size_t j : found->second) {
                            Vec3 delta = positions[j] - positions[i];
                            float d2 = dot(delta, delta);
                            if (i == j || dot(normals[i], normals[j]) < .999f ||
                                std::abs(dot(delta, normals[i])) > .006f || d2 > 1.f)
                                continue;
                            float w = std::exp(-d2 / .60f);
                            sum = sum + lighting[j] * w;
                            weight += w;
                        }
                    }
            next[i] = sum * (1 / weight);
        }
        lighting = std::move(next);
    }
    for (size_t i = 0; i < mesh.vertices.size(); ++i) {
        const auto l = lighting[remap[i]];
        const float value[]{l.x, l.y, l.z};
        std::memcpy(bytes.data() + 16 + i * 20 + 8, value, sizeof(value));
    }
    std::ofstream output(path, std::ios::binary);
    output.write(reinterpret_cast<const char *>(bytes.data()), std::streamsize(bytes.size()));
    if (!output)
        throw std::runtime_error("Could not save filtered scene lighting");
    std::cout << "Filtered architectural diffuse lighting: " << path.string() << std::endl;
}
void selfTest() {
    SceneMesh mesh;
    for (int plane = 0; plane < 16; ++plane) {
        float z = -2.f - plane;
        for (Vec3 p : {Vec3{-1, -1, z}, {1, -1, z}, {1, 1, z}, {-1, -1, z}, {1, 1, z}, {-1, 1, z}})
            mesh.vertices.push_back({{p.x, p.y, p.z, 1, 1, 1, 1, 0, 0, 1}, 0, 1, {}, {1, 0, 0}, {}});
    }
    World world(mesh, Background::Lounge);
    for (int x = -8; x <= 8; ++x)
        for (int y = -8; y <= 8; ++y) {
            auto hit = world.trace({x * .1f, y * .1f, 0}, {0, 0, -1});
            if (hit.triangle < 0 || std::abs(hit.distance - 2) > .0001f)
                throw std::runtime_error("BVH missed the nearest actual triangle");
        }
    if (world.trace({0, 0, 0}, {0, 0, -1}, 1).triangle >= 0 ||
        world.trace({2, 0, 0}, {0, 0, -1}).triangle >= 0)
        throw std::runtime_error("BVH accepted an out-of-range intersection");
    auto reverse = world.trace({0, 0, -20}, {0, 0, 1});
    if (reverse.triangle < 0 || std::abs(reverse.distance - 3) > .0001f)
        throw std::runtime_error("BVH two-sided traversal failed");
    std::cout << "PASS: BVH triangle visibility, nearest hit, range limit and back faces\n";
}
} // namespace
int main(int argc, char **argv) {
    try {
        if (argc == 4 && std::string(argv[1]) == "--smooth") {
            smoothLighting(Background(std::stoi(argv[2])), argv[3]);
            return 0;
        }
        if (argc == 2 && std::string(argv[1]) == "--self-test") {
            selfTest();
            return 0;
        }
        if (argc < 3)
            throw std::runtime_error("Usage: cyvr_bake_lighting BACKGROUND OUTPUT [SAMPLES=256]");
        auto scene = Background(std::stoi(argv[1]));
        if (!isScene3D(scene))
            throw std::runtime_error("Select a modeled scene (3..5)");
        bake(scene, argv[2], argc > 3 ? std::clamp(std::stoi(argv[3]), 64, 2048) : 256);
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
