// SPDX-License-Identifier: MIT
#include "cyvr/gpu.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>

using namespace cyvr;
namespace {
void check(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
void rectangle(SpatialMesh &mesh, float x, float z, float width, Color color) {
    mesh.triangle({x - width, -.20f, z}, {x + width, -.20f, z}, {x + width, .20f, z}, color);
    mesh.triangle({x - width, -.20f, z}, {x + width, .20f, z}, {x - width, .20f, z}, color);
}
} // namespace
int main(int argc, char **argv) {
    try {
        check(argc == 2, "Pass the shader asset directory");
        VulkanContext context;
        context.createStandalone();
        VulkanRenderer renderer(context);
        AssetReader assets = [&](const std::string &name) {
            std::ifstream file(std::filesystem::path(argv[1]) / name, std::ios::binary);
            return std::vector<uint8_t>((std::istreambuf_iterator<char>(file)), {});
        };
        renderer.initialize(VK_FORMAT_R8G8B8A8_SRGB, assets);
        check(renderer.sceneTextureBytes() == 0, "Native 2D must allocate no PBR textures");
        check(!renderer.photographicEnvironments(), "2D initialization must not load panoramas");
        renderer.loadEnvironments([](const std::string &) { return std::vector<uint8_t>{}; });
        check(!renderer.photographicEnvironments(), "Missing optional assets must retain the fallback");
        auto target = renderer.createTarget(256, 256, VK_NULL_HANDLE, true);
        auto camera = viewProjection({}, -.7f, .7f, .7f, -.7f);
        std::vector<uint8_t> reference;
        for (bool reverse : {false, true}) {
            SpatialMesh mesh;
            auto panel = [&] { rectangle(mesh, 0, -1, .65f, Color::hex(0x154fd5)); };
            if (reverse)
                panel();
            // Two white hand proxies: one nearer than the blue panel, the other behind it.
            rectangle(mesh, -.20f, -.70f, .10f, Color::hex(0xffffff));
            rectangle(mesh, .25f, -1.40f, .10f, Color::hex(0xffffff));
            if (!reverse)
                panel();
            renderer.drawSpatial(target, mesh, camera);
            auto pixels = renderer.readback(target);
            auto sample = [&](float x, float distance) {
                int px = int((x / (distance * std::tan(.7f)) + 1) * 128);
                return pixels.data() + (128 * 256 + px) * 4;
            };
            auto front = sample(-.20f, .70f), behind = sample(.25f, 1.40f);
            check(front[0] > 190 && front[1] > 190, "Front hand must cover the panel");
            check(behind[2] > behind[0] + 70, "Panel must cover the hand behind it");
            if (!reference.empty())
                check(reference == pixels, "Depth ordering must not depend on draw order");
            reference = std::move(pixels);
        }
        renderer.destroyTarget(target);
        std::cout << "PASS: GPU depth rejects a hand behind a panel and preserves a hand in front, in both "
                     "draw orders\n";
        {
            auto stereo = renderer.createTarget(256, 256, VK_NULL_HANDLE, true);
            auto panelTarget = renderer.createTarget(256, 256);
            std::vector<uint8_t> previous;
            for (auto background : {Background::Lounge, Background::Observatory, Background::Terrace}) {
                Settings settings;
                settings.background = background;
                settings.opacity = .5f;
                renderer.setScene(background, {}, assets);
                check(renderer.bakedSceneLighting(),
                      "Reference scenes must load their offline triangle bake");
                check(renderer.sceneTextureBytes() > 0 && renderer.sceneTextureBytes() < 3 * 1024 * 1024,
                      "Only one HDR probe and the material arrays may be resident");
                check(renderer.sceneVertexCount() * sizeof(SceneVertex) < 6 * 1024 * 1024,
                      "3D scene must fit the static geometry budget");
                Pose eye{{0, 1.6f, 0}, {}};
                auto environment = environmentView(eye, -.7f, .7f, .7f, -.7f, settings);
                renderer.drawSpatial(stereo, {}, viewProjection(eye, -.7f, .7f, .7f, -.7f), &environment);
                auto first = renderer.readback(stereo);
                check(first != previous, "Three scenes must render distinct geometry");
                previous = first;
                eye.position.x = .4f;
                environment = environmentView(eye, -.7f, .7f, .7f, -.7f, settings);
                renderer.drawSpatial(stereo, {}, viewProjection(eye, -.7f, .7f, .7f, -.7f), &environment);
                check(first != renderer.readback(stereo), "Head translation must create real scene parallax");
                auto panel = placePanel(eye, 1.4f, 1);
                auto glass = glassView(eye, panel, settings);
                renderer.draw(panelTarget, {}, &glass);
                auto blurred = renderer.readback(panelTarget);
                auto center = (128 * 256 + 128) * 4;
                check(blurred[center + 3] > 200 &&
                          (blurred[center] + blurred[center + 1] + blurred[center + 2]) > 20,
                      "3D backdrop must reach the frosted-glass rendering path");
                renderer.setScene(background, {{.4f, 0, 0}, {}}, assets);
                renderer.drawSpatial(stereo, {}, viewProjection(eye, -.7f, .7f, .7f, -.7f), &environment);
                auto translated = renderer.readback(stereo);
                size_t changed{};
                for (size_t i = 0; i < first.size(); i += 4)
                    if (std::abs(int(first[i]) - translated[i]) > 2 ||
                        std::abs(int(first[i + 1]) - translated[i + 1]) > 2 ||
                        std::abs(int(first[i + 2]) - translated[i + 2]) > 2)
                        ++changed;
                check(changed < 256 * 256 / 100, "Moving eye and room together must preserve the view");
            }
            renderer.destroyTarget(panelTarget);
            renderer.destroyTarget(stereo);
            std::cout
                << "PASS: three budgeted 3D scenes, head parallax, floor anchors and scene-backed glass\n";
        }
        {
            Settings ar;
            ar.background = Background::Passthrough;
            ar.opacity = .4f;
            auto target = renderer.createTarget(128, 128, VK_NULL_HANDLE, true);
            Pose eye{};
            auto env = environmentView(eye, -.7f, .7f, .7f, -.7f, ar);
            SpatialMesh hands;
            rectangle(hands, 0, -1, .2f, Color::hex(0xffffff));
            renderer.setScene(Background::Passthrough);
            renderer.drawSpatial(target, hands, viewProjection(eye, -.7f, .7f, .7f, -.7f), &env);
            auto image = renderer.readback(target);
            check(image[3] == 0,
                  "AR projection must leave the room visible through clear pixels, even after a 3D scene");
            check(image[(64 * 128 + 64) * 4 + 3] > 250,
                  "AR hands must remain opaque in the transparent projection");
            renderer.destroyTarget(target);
            auto panel = placePanel(eye, 1.4f, 1);
            auto glass = glassView(eye, panel, ar);
            check(glass.options[3] == 0, "AR must not sample a virtual backdrop over the real room");
            auto uiTarget = renderer.createTarget(128, 128);
            DrawList ui;
            ui.rect({0, 0, 1440, 990}, Color::hex(0x182228, .4f));
            renderer.draw(uiTarget, ui, &glass);
            image = renderer.readback(uiTarget);
            auto alpha = image[(64 * 128 + 64) * 4 + 3];
            check(alpha > 90 && alpha < 115, "AR panel alpha must preserve the requested transparent tint");
            renderer.destroyTarget(uiTarget);
            std::cout
                << "PASS: AR clears the 3D backdrop, retains hand alpha and preserves transparent panels\n";
        }
        if (std::filesystem::exists(std::filesystem::path(argv[1]) / "pbr/basecolor.bin")) {
            std::vector<uint8_t> reference;
            for (int variant = 0; variant < 4; ++variant) {
                VulkanRenderer materialRenderer(context);
                materialRenderer.initialize(VK_FORMAT_R8G8B8A8_SRGB, assets);
                AssetReader changed = [&](const std::string &name) {
                    auto bytes = assets(name);
                    if (name == "pbr/normal-rough-metal.bin" && variant == 1)
                        for (size_t i = 32; i < bytes.size(); i += 4)
                            bytes[i] = bytes[i + 1] = 128;
                    if (name == "pbr/normal-rough-metal.bin" && variant == 2)
                        for (size_t i = 32; i < bytes.size(); i += 4) {
                            bytes[i + 2] = 255;
                            bytes[i + 3] = 0;
                        }
                    if (name == "pbr/lounge.bin" && variant == 3)
                        std::fill(bytes.begin() + 32, bytes.end(), 0);
                    return bytes;
                };
                materialRenderer.setScene(Background::Lounge, {}, changed);
                auto target = materialRenderer.createTarget(256, 256, VK_NULL_HANDLE, true);
                Settings settings;
                settings.background = Background::Lounge;
                Pose eye{{0, 1.6f, 0}, {}};
                auto env = environmentView(eye, -.7f, .7f, .7f, -.7f, settings);
                materialRenderer.drawSpatial(target, {}, viewProjection(eye, -.7f, .7f, .7f, -.7f), &env);
                auto image = materialRenderer.readback(target);
                if (variant == 0)
                    reference = image;
                else {
                    size_t changedPixels{};
                    for (size_t i = 0; i < image.size(); i += 4)
                        if (std::abs(int(image[i]) - reference[i]) +
                                std::abs(int(image[i + 1]) - reference[i + 1]) +
                                std::abs(int(image[i + 2]) - reference[i + 2]) >
                            3)
                            ++changedPixels;
                    check(changedPixels > 100,
                          "Normal maps, roughness/metallic and HDR IBL must each affect visible pixels");
                }
                // Exposure remains effective with the PBR tone mapping path.
                env.options[1] = .25f;
                materialRenderer.drawSpatial(target, {}, viewProjection(eye, -.7f, .7f, .7f, -.7f), &env);
                check(image != materialRenderer.readback(target),
                      "PBR exposure must respond to brightness changes");
                materialRenderer.destroyTarget(target);
            }
            std::cout << "PASS: normal maps, metallic/roughness, HDR reflections and exposure affect the "
                         "actual GPU render\n";
        }
        {
            VulkanRenderer fallback(context);
            fallback.initialize(VK_FORMAT_R8G8B8A8_SRGB, assets);
            fallback.setScene(Background::Terrace);
            check(!fallback.bakedSceneLighting(),
                  "Missing optional lighting must retain the neutral fallback");
            check(fallback.sceneTextureBytes() < 1024,
                  "Missing optional PBR resources must use small neutral fallbacks");
            auto target = fallback.createTarget(64, 64, VK_NULL_HANDLE, true);
            Settings settings;
            settings.background = Background::Terrace;
            Pose eye{{0, 1.6f, 0}, {}};
            auto env = environmentView(eye, -.7f, .7f, .7f, -.7f, settings);
            fallback.drawSpatial(target, {}, viewProjection(eye, -.7f, .7f, .7f, -.7f), &env);
            auto image = fallback.readback(target);
            check(image[(32 * 64 + 32) * 4 + 3] == 255, "Fallback PBR scene must render opaque pixels");
            fallback.destroyTarget(target);
        }
        if (std::filesystem::exists(std::filesystem::path(argv[1]) / "scenes/lebombo.jpg")) {
            renderer.loadEnvironments(assets);
            check(renderer.photographicEnvironments(), "Both bundled panoramas must decode and upload");
            renderer.loadEnvironments([](const std::string &) -> std::vector<uint8_t> {
                throw std::runtime_error("Loaded scenes must not be read again");
            });
            auto backdrop = renderer.createTarget(256, 256, VK_NULL_HANDLE, true);
            EnvironmentView view;
            std::vector<uint8_t> first;
            for (int scene : {0, 1}) {
                view.options[0] = float(scene);
                renderer.drawSpatial(backdrop, {}, camera, &view);
                auto pixels = renderer.readback(backdrop);
                if (scene == 0) first = pixels;
                else check(first != pixels, "Selecting another environment must change rendered pixels");
                // Application icon-atlas replacements must leave panorama descriptors valid.
                renderer.uploadImages(1, 1, {255, 0, 255, 255});
                renderer.drawSpatial(backdrop, {}, camera, &view);
                check(pixels == renderer.readback(backdrop), "Image-atlas updates must preserve environments");
            }
            renderer.destroyTarget(backdrop);
            std::cout << "PASS: optional panoramas load once, render independently and survive atlas replacement\n";
        }
        {
            // The overlay and video bindings can sample the application atlas, and must follow its replacement.
            VulkanRenderer atlasRenderer(context);
            atlasRenderer.initialize(VK_FORMAT_R8G8B8A8_SRGB, assets);
            atlasRenderer.useImageAtlasForVideo();
            auto target = atlasRenderer.createTarget(32, 32);
            auto sample = [&](auto surface) {
                DrawList ui;
                surface(ui);
                atlasRenderer.draw(target, ui);
                auto image = atlasRenderer.readback(target);
                return std::array<uint8_t, 3>{image[(16 * 32 + 16) * 4], image[(16 * 32 + 16) * 4 + 1],
                                              image[(16 * 32 + 16) * 4 + 2]};
            };
            auto overlay = [](DrawList &ui) { ui.overlay({0, 0, 1440, 990}); };
            auto video = [](DrawList &ui) { ui.video({0, 0, 1440, 990}, {0, 0, 1, 1}); };
            atlasRenderer.uploadImages(1, 1, {0, 255, 0, 255});
            check(sample(overlay)[1] > 250, "Overlay binding must sample the replaced atlas");
            check(sample(video)[1] > 250, "Video binding must sample the replaced atlas");
            std::vector<uint8_t> larger(64 * 64 * 4);
            for (size_t i = 0; i < larger.size(); i += 4)
                larger[i] = larger[i + 3] = 255;
            atlasRenderer.uploadImages(64, 64, larger);
            auto overlayPixel = sample(overlay), videoPixel = sample(video);
            check(overlayPixel[0] > 250 && overlayPixel[1] < 5, "Overlay binding must follow every atlas upload");
            check(videoPixel[0] > 250 && videoPixel[1] < 5, "Video binding must follow every atlas upload");
            atlasRenderer.destroyTarget(target);
            std::cout << "PASS: atlas replacement rebinds every descriptor that sampled it\n";
        }
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
