// SPDX-License-Identifier: MIT
#include "cyvr/core.hpp"
#include "cyvr/pbr.hpp"
#include "cyvr/scene.hpp"
#include "cyvr/scene_lighting.hpp"
#include "cyvr/spatial.hpp"
#include "cyvr/storage.hpp"
#include "cyvr/xr_depth.hpp"
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace cyvr;
#define REQUIRE(x)                                                                                           \
    do {                                                                                                     \
        if (!(x))                                                                                            \
            throw std::runtime_error("Check failed at line " + std::to_string(__LINE__) + ": " #x);          \
    } while (false)
static bool near(float a, float b) {
    return std::abs(a - b) < .001f;
}
static void click(UiModel &ui, DrawList &d, Vec2 point) {
    ui.pointer(0, point, true);
    ui.pointer(0, point, false);
    ui.drawMain(d);
}
int main() {
    try {
        {
            // Embedded browser input must not steal a shared control's active drag.
            UiModel ui;
            ui.applicationWidget(1000,{0,0,100,100});
            int consumed=0;bool surfaceCapture=false;
            ui.applicationPointer=[&](size_t,Vec2 point,bool down){
                if(!surfaceCapture && !Rect{200,0,100,100}.contains(point))return false;
                surfaceCapture=down;consumed++;return true;
            };
            ui.pointer(0,{220,20},true);
            REQUIRE(consumed==1 && ui.captured(0)==0 && surfaceCapture);
            ui.pointer(0,{-100,-100},false);
            REQUIRE(consumed==2 && !surfaceCapture);
            ui.pointer(0,{20,20},true);
            REQUIRE(ui.captured(0)==1000);
            ui.pointer(0,{220,20},true);
            ui.pointer(0,{220,20},false);
            REQUIRE(consumed==2 && ui.captured(0)==0);
            ui.modalOpen=true;
            ui.pointer(0,{220,20},true);
            ui.pointer(0,{220,20},false);
            REQUIRE(consumed==2); // Shared modals retain input priority.
        }
        for (auto background : {Background::Lounge, Background::Observatory, Background::Terrace}) {
            auto scene = buildScene(background);
            REQUIRE(scene.vertices.size() > 3000 && scene.vertices.size() <= 120000 &&
                    scene.vertices.size() % 3 == 0);
            for (const auto &vertex : scene.vertices) {
                auto v = vertex.surface;
                REQUIRE(std::isfinite(v.x + v.y + v.z + v.r + v.g + v.b + vertex.ambient + v.a));
                REQUIRE(near(length({v.nx, v.ny, v.nz}), 1));
                REQUIRE(vertex.ambient >= 0 && vertex.ambient <= 1 && v.a >= 0 && v.a <= 1);
                Vec3 normal{v.nx, v.ny, v.nz};
                REQUIRE(std::isfinite(vertex.uv.x + vertex.uv.y));
                REQUIRE(near(length(vertex.tangent), 1) && std::abs(dot(normal, vertex.tangent)) < .002f);
                auto light = vertex.indirect;
                REQUIRE(std::isfinite(light.x + light.y + light.z));
                REQUIRE(light.x >= 0 && light.y >= 0 && light.z >= 0);
            }
            // Back-face culling relies on every triangle winding the same way as its normals.
            for (size_t i = 0; i + 2 < scene.vertices.size(); i += 3) {
                auto p = [&](size_t k) {
                    auto s = scene.vertices[i + k].surface;
                    return Vec3{s.x, s.y, s.z};
                };
                auto n = [&](size_t k) {
                    auto s = scene.vertices[i + k].surface;
                    return Vec3{s.nx, s.ny, s.nz};
                };
                Vec3 e = p(1) - p(0), f = p(2) - p(0);
                Vec3 geometric{e.y * f.z - e.z * f.y, e.z * f.x - e.x * f.z, e.x * f.y - e.y * f.x};
                REQUIRE(dot(geometric, n(0) + n(1) + n(2)) > 0);
            }
            REQUIRE(!scene.openTriangles.empty());
            auto culled = scene;
            const auto closed = moveOpenTrianglesLast(culled);
            REQUIRE(culled.vertices.size() == scene.vertices.size() && culled.openTriangles.empty());
            REQUIRE(closed > 0 && closed % 3 == 0 && closed + scene.openTriangles.size() * 3 <= scene.vertices.size());
            for (uint32_t i = 0; i < closed; ++i)
                REQUIRE(culled.vertices[i].material != 4); // Lights keep their back faces.
            if (background == Background::Observatory)
                // The planet's flat rings face up, above the viewer.
                REQUIRE(std::any_of(culled.vertices.begin() + closed, culled.vertices.end(),
                                    [](const SceneVertex &v) { return v.surface.y > 5 && v.material == 7; }));
            std::cout << "3D scene " << int(background) << ": " << scene.vertices.size() / 3
                      << " triangles\n";
        }
        REQUIRE(buildScene(Background::Passthrough).vertices.empty());
        REQUIRE(!isScene3D(Background::Night));
        {
            // Validate the binary boundary, including dimensions, mip counts and payload length.
            auto texture = fallbackMaterial(false);
            std::vector<uint8_t> bytes{'C', 'Y', 'T', 'X'};
            auto word = [&](uint32_t x) {
                for (int i = 0; i < 4; ++i)
                    bytes.push_back(uint8_t(x >> (i * 8)));
            };
            for (uint32_t x : {1u, 1u, 1u, 9u, 1u, 1u, 0u})
                word(x);
            bytes.insert(bytes.end(), texture.pixels.begin(), texture.pixels.end());
            REQUIRE(decodePbrTexture(bytes).pixels == texture.pixels);
            auto rejected = [](std::vector<uint8_t> data) {
                try {
                    decodePbrTexture(data);
                } catch (const std::runtime_error &) {
                    return true;
                }
                return false;
            };
            auto bad = bytes;
            bad.pop_back();
            REQUIRE(rejected(bad));
            bad = bytes;
            bad.push_back(0);
            REQUIRE(rejected(bad));
            bad = bytes;
            bad[8] = 0;
            REQUIRE(rejected(bad));
            bad = bytes;
            bad[20] = 32;
            REQUIRE(rejected(bad));
            bad = bytes;
            bad[28] = 1;
            REQUIRE(rejected(bad));
            bad = bytes;
            bad[24] = 99;
            REQUIRE(rejected(bad));
            REQUIRE(fallbackMaterial(true).layers == 9);
            REQUIRE(fallbackProbe(Background::Lounge).cube);
            REQUIRE(near(length(sceneSunDirection(Background::Terrace)), 1));
        }
        {
            SceneMesh mesh;
            mesh.vertices.resize(1);
            std::vector<uint8_t> bytes{'C', 'Y', 'L', 'I'};
            auto word = [&](uint32_t v) {
                for (int i = 0; i < 4; ++i)
                    bytes.push_back(uint8_t(v >> (i * 8)));
            };
            word(1);
            word(uint32_t(Background::Lounge));
            word(1);
            for (float f : {.5f, .8f, .2f, .3f, .4f}) {
                uint32_t v;
                std::memcpy(&v, &f, 4);
                word(v);
            }
            applySceneLighting(mesh, Background::Lounge, bytes);
            REQUIRE(near(mesh.vertices[0].indirect.y, .3f));
            auto bad = bytes;
            bad[8] = uint8_t(Background::Terrace);
            bool rejected = false;
            try {
                applySceneLighting(mesh, Background::Lounge, bad);
            } catch (const std::runtime_error &) {
                rejected = true;
            }
            REQUIRE(rejected);
            bad = bytes;
            bad.pop_back();
            rejected = false;
            try {
                applySceneLighting(mesh, Background::Lounge, bad);
            } catch (const std::runtime_error &) {
                rejected = true;
            }
            REQUIRE(rejected);
        }
        {
            UiModel model;
            DrawList draw;
            model.presentation = Presentation::Immersive;
            model.passthroughAvailable = true;
            model.navigate(Page::Settings);
            model.settingsTab = 1;
            constexpr Background choices[]{Background::Lounge,  Background::Observatory,
                                           Background::Terrace, Background::Studio,
                                           Background::Night,   Background::Passthrough};
            for (int i = 0; i < 6; ++i) {
                model.drawMain(draw);
                Rect target{};
                for (auto &widget : model.widgets())
                    if (widget.id == 430 + i)
                        target = widget.rect;
                REQUIRE(target.w > 0 && target.y + target.h < 920);
                click(model, draw, {target.x + target.w * .5f, target.y + target.h * .5f});
                REQUIRE(model.settings.background == choices[i] && model.saveRequested);
            }
            model.presentation = Presentation::Panel2D;
            model.drawMain(draw);
            for (const auto &widget : model.widgets())
                REQUIRE(widget.id < 430 || widget.id > 435);
        }
        Pose head{{1, 1.7f, 2}, yawRotation(.4f)};
        {
            UiModel ar;
            DrawList draw;
            ar.presentation = Presentation::Immersive;
            ar.immersiveAvailable = true;
            ar.passthroughAvailable = true;
            ar.settings.background = Background::Terrace;
            ar.settings.blur = .7f;
            ar.settings.brightness = .8f;
            REQUIRE(ar.setAugmentedReality(true));
            REQUIRE(ar.settings.background == Background::Passthrough &&
                    ar.settings.virtualBackground == Background::Terrace && ar.saveRequested);
            REQUIRE(ar.setAugmentedReality(true)); // Repeated activation must preserve the remembered scene.
            REQUIRE(ar.settings.virtualBackground == Background::Terrace);
            REQUIRE(saveSettings("test-ar-preferences.txt", ar.settings));
            auto restored = loadSettings("test-ar-preferences.txt");
            REQUIRE(restored.background == Background::Passthrough &&
                    restored.virtualBackground == Background::Terrace);
            ar.drawMain(draw);
            Rect camera{};
            for (const auto &w : ar.widgets())
                if (w.id == 412 && w.enabled)
                    camera = w.rect;
            REQUIRE(camera.w > 0);
            click(ar, draw, {camera.x + camera.w * .5f, camera.y + camera.h * .5f});
            REQUIRE(ar.settings.background == Background::Terrace);
            ar.navigate(Page::Settings);
            ar.settingsTab = 1;
            ar.drawMain(draw);
            Rect blur{};
            for (const auto &w : ar.widgets())
                if (w.id == 351)
                    blur = w.rect;
            ar.pointer(0, {blur.x + blur.w * .25f, blur.y + blur.h * .5f}, true);
            float capturedBlur = ar.settings.blur;
            REQUIRE(ar.setAugmentedReality(true));
            ar.drawMain(draw);
            for (const auto &w : ar.widgets()) {
                if (w.id == 351 || w.id == 352)
                    REQUIRE(!w.enabled);
                if (w.id == 350)
                    REQUIRE(w.enabled);
            }
            ar.pointer(0, {blur.x + blur.w * .9f, blur.y + blur.h * .5f}, true);
            REQUIRE(near(ar.settings.blur, capturedBlur));
            ar.pointer(0, {-100, -100}, false);
            REQUIRE(near(ar.settings.brightness, .8f));
            ar.passthroughAvailable = false;
            REQUIRE(ar.setAugmentedReality(false)); // Recovery still works when the camera service fails.
            REQUIRE(ar.settings.background == Background::Terrace);
            REQUIRE(!ar.setAugmentedReality(true));
            REQUIRE(ar.settings.background == Background::Terrace);
            ar.presentation = Presentation::Panel2D;
            ar.drawMain(draw);
            for (const auto &w : ar.widgets())
                REQUIRE(w.id != 412);
            REQUIRE(!ar.setAugmentedReality(true));
            {
                std::ofstream legacy("test-ar-preferences.txt");
                legacy << "background 4\n";
            }
            REQUIRE(loadSettings("test-ar-preferences.txt").virtualBackground == Background::Observatory);
            std::remove("test-ar-preferences.txt");
            restored.virtualBackground = Background::Passthrough;
            restored.sanitize();
            REQUIRE(restored.virtualBackground == Background::Studio);
        }
        auto room = placeScene(head, -.05f);
        REQUIRE(near(room.position.y, -.05f));
        auto localHead = relativePose(room, head);
        REQUIRE(near(localHead.position.x, 0) && near(localHead.position.z, 0) &&
                near(localHead.position.y, 1.75f));
        PanelGeometry rectangle = placePanel(head, 1.4f, 1);
        Vec3 right = rotate(rectangle.pose.orientation, {rectangle.width * .5f, 0, 0});
        Vec3 up = rotate(rectangle.pose.orientation, {0, rectangle.height * .5f, 0});
        auto offAxis = rectangleProjection(head.position, rectangle.pose.position - head.position, right, up);
        for (float x : {-1.f, 1.f})
            for (float y : {-1.f, 1.f}) {
                auto p = rectangle.pose.position + right * x + up * y;
                float w = offAxis[3] * p.x + offAxis[7] * p.y + offAxis[11] * p.z + offAxis[15];
                float u = (offAxis[0] * p.x + offAxis[4] * p.y + offAxis[8] * p.z + offAxis[12]) / w;
                float v = (offAxis[1] * p.x + offAxis[5] * p.y + offAxis[9] * p.z + offAxis[13]) / w;
                REQUIRE(near(u, x) && near(v, -y));
            }
        // Layer chains are copied, never rewritten in application-owned memory.
        DepthComposition compositor;
        XrCompositionLayerQuad quad{XR_TYPE_COMPOSITION_LAYER_QUAD};
        quad.pose.orientation.w = 1; quad.size = {1.2f, .825f};
        auto header = reinterpret_cast<const XrCompositionLayerBaseHeader *>(&quad);
        auto wrapped = compositor.test(header, 0);
        REQUIRE(wrapped != header && quad.next == nullptr);
        auto depth = static_cast<const XrCompositionLayerDepthTestFB *>(wrapped->next);
        REQUIRE(depth->compareOp == XR_COMPARE_OP_LESS_OR_EQUAL_FB && !depth->depthMask);
        REQUIRE(reinterpret_cast<const XrCompositionLayerQuad *>(wrapped)->size.width == 1.2f);
        XrCompositionLayerDepthTestFB appPolicy{XR_TYPE_COMPOSITION_LAYER_DEPTH_TEST_FB};
        quad.next = &appPolicy;
        REQUIRE(compositor.test(header, 1) == header);
        XrCompositionLayerCylinderKHR cylinder{XR_TYPE_COMPOSITION_LAYER_CYLINDER_KHR};
        auto cylinderHeader = reinterpret_cast<const XrCompositionLayerBaseHeader *>(&cylinder);
        REQUIRE(compositor.test(cylinderHeader, 2)->next != nullptr && !cylinder.next);
        REQUIRE(compositor.test(header, DepthComposition::capacity) == header);
        for (float distance : {spatialNearZ, 1.f, 2.f, spatialFarZ}) {
            auto projection = viewProjection({}, -.7f, .7f, .6f, -.6f);
            float z = (projection[10] * -distance + projection[14]) / distance;
            float expected = spatialFarZ * (distance - spatialNearZ) /
                             (distance * (spatialFarZ - spatialNearZ));
            REQUIRE(near(z, expected));
        }
        auto panel = placePanel({{0, 1.6f, 0}, {}}, 1.35f, 1);
        auto hit = intersectPanel({0, 1.5f, 0}, {0, 0, -1}, panel);
        REQUIRE(hit.valid);
        REQUIRE(near(hit.pixels.x, 720));
        REQUIRE(near(hit.pixels.y, 495));
        REQUIRE(!intersectPanel({0, 1.5f, 0}, {0, 0, 1}, panel).valid);
        REQUIRE(!intersectPanel({0, 1.5f, -2}, {0, 0, 1}, panel).valid);
        auto moved = placePanel({{1, 1.2f, 2}, yawRotation(pi / 2)}, 2, 1);
        REQUIRE(near(moved.pose.position.x, -1));
        REQUIRE(intersectPanel({1, 1.1f, 2}, {-1, 0, 0}, moved).valid);
        REQUIRE(near(panel.width / 1.35f, moved.width / 2));
        auto text = decodeUtf8("Échelle → 90 %");
        REQUIRE(text[0] == 0xc9);
        REQUIRE(text[8] == 0x2192);
        REQUIRE(decodeUtf8("\xc0\xaf")[0] == '?');
        REQUIRE(decodeUtf8("\xf4\x90\x80\x80")[0] == '?');
        Settings settings;
        settings.distance = std::numeric_limits<float>::quiet_NaN();
        settings.scale = 5;
        settings.requestedHz = 77;
        settings.sanitize();
        REQUIRE(near(settings.distance, 1.35f));
        REQUIRE(near(settings.scale, 1.25f));
        REQUIRE(settings.requestedHz == 72);
        settings.opacity = -1;
        settings.blur = std::numeric_limits<float>::infinity();
        settings.theme = Theme(99);
        settings.handModel = HandModel(-1);
        settings.sanitize();
        REQUIRE(near(settings.opacity, .25f));
        REQUIRE(near(settings.blur, .5f));
        REQUIRE(settings.theme == Theme::Ocean && settings.handModel == HandModel::Automatic);
        auto facing = faceViewer({2, 1, -2}, {0, 1, 0});
        REQUIRE(dot(rotate(facing, {0, 0, 1}), normalized({-2, 0, 2})) > .999f);
        REQUIRE(dot(rotate(faceViewer({0, -.5f, -2}, {0, 0, 0}), {0, 0, 1}), normalized({0, .5f, 2})) >
                .999f);
        Pose start{{0, 0, 0}, {}}, target{{1, 0, -1}, yawRotation(pi * .75f)};
        Pose smooth = approachPose(start, target, 1 / 72.f);
        REQUIRE(smooth.position.x > 0 && smooth.position.x < 1);
        REQUIRE(near(length(rotate(smooth.orientation, {0, 0, 1})), 1));
        REQUIRE(near(approachPose(start, target, 0, true).position.x, 1));
        FrameStats stats;
        for (int i = 0; i < 100; i++)
            stats.sample(10);
        for (int i = 0; i < 10; i++)
            stats.sample(20);
        REQUIRE(stats.fps() > 90 && stats.fps() < 92);
        REQUIRE(stats.percentile95() == 20);
        stats.sample(-1);
        REQUIRE(stats.count == 110);
        UiModel panelUi;
        REQUIRE(panelUi.presentation == Presentation::Panel2D && !panelUi.immersiveAvailable);
        DrawList panelDraw;
        auto hasWidget = [&](int id) {
            return std::any_of(panelUi.widgets().begin(), panelUi.widgets().end(), [=](auto &w) { return w.id == id; });
        };
        panelUi.drawMain(panelDraw);
        REQUIRE(!hasWidget(605) && !hasWidget(411) && !hasWidget(102));
        panelUi.immersiveAvailable = true;
        panelUi.drawMain(panelDraw);
        REQUIRE(hasWidget(411));
        auto modeButton = [&] {
            for (const auto &w : panelUi.widgets())
                if (w.id == 411)
                    return Vec2{w.rect.x + w.rect.w * .5f, w.rect.y + w.rect.h * .5f};
            return Vec2{};
        };
        click(panelUi, panelDraw, modeButton());
        REQUIRE(panelUi.immersiveRequested && !panelUi.panelRequested);
        panelUi.presentation = Presentation::Immersive;
        panelUi.drawMain(panelDraw);
        click(panelUi, panelDraw, modeButton());
        REQUIRE(panelUi.panelRequested);
        panelUi.presentation = Presentation::Panel2D;
        panelUi.navigate(Page::Settings);
        panelUi.settingsTab = 0;
        panelUi.drawMain(panelDraw);
        REQUIRE(hasWidget(312) && !hasWidget(310) && !hasWidget(342));
        panelUi.settingsTab = 1;
        panelUi.drawMain(panelDraw);
        REQUIRE(hasWidget(360) && hasWidget(350) && !hasWidget(372));
        panelUi.drawDock(panelDraw);
        REQUIRE(panelDraw.vertices.empty());
        for (Vec2 dimensions : {Vec2{1440, 990}, Vec2{1024, 704}, Vec2{720, 1280}, Vec2{2048, 800}}) {
            auto mapping = fitCanvas(dimensions.x, dimensions.y);
            REQUIRE(mapping.scale > 0);
            auto surface = mapping.toSurface({945, 35});
            auto logical = mapping.toCanvas(surface);
            REQUIRE(near(logical.x, 945) && near(logical.y, 35));
            REQUIRE(mapping.viewport.x >= 0 && mapping.viewport.y >= 0);
            REQUIRE(mapping.viewport.x + mapping.viewport.w <= dimensions.x + .001f);
            REQUIRE(mapping.viewport.y + mapping.viewport.h <= dimensions.y + .001f);
        }
        REQUIRE(fitCanvas(0, 0).viewport.w == 0);
        REQUIRE(fitCanvas(100, 100, std::numeric_limits<float>::quiet_NaN()).viewport.w == 0);
        REQUIRE(fitCanvas(std::numeric_limits<float>::infinity(), 100).viewport.w == 0);
        UiModel ui;
        ui.presentation = Presentation::Immersive;
        REQUIRE(ui.settings.language == "en");
        REQUIRE(std::string(ui.tr("nav.home")) == "Home");
        REQUIRE(std::string(ui.pageLabel(0)) == "Home");
        REQUIRE(std::string(translate("xx", "nav.home")) == "Home");
        REQUIRE(std::string(resolveLocale("FR_ca").code) == "fr");
        REQUIRE(formatNumber("en", 1.25, 2) == "1.25");
        REQUIRE(formatNumber("fr", 1.25, 2) == "1,25");
        REQUIRE(formatMessage("en", "header.language", {{"language", "French"}}) == "Language: French");
        REQUIRE(formatMessage("fr", "header.language", {{"language", "English"}}) == "Langue : English");
        auto englishRevision = ui.revision;
        ui.setLanguage("fr-CA");
        REQUIRE(ui.revision > englishRevision && ui.saveRequested);
        REQUIRE(std::string(ui.tr("nav.home")) == "Accueil");
        REQUIRE(std::string(ui.pageLabel(0)) == "Accueil");
        ui.pageLabels[0] = "Application";
        REQUIRE(std::string(ui.pageLabel(0)) == "Application");
        ui.pageLabels[0].clear();
        REQUIRE(ui.statusText() == "Vulkan natif / mode bureau");
        ui.setLanguage("en");
        DrawList draw;
        ui.drawMain(draw);
        REQUIRE(!draw.vertices.empty());
        click(ui, draw, {80, 600});
        REQUIRE(ui.settings.language == "fr");
        REQUIRE(std::string(ui.tr("dock.move")) == "Déplacer");
        click(ui, draw, {80, 600});
        REQUIRE(ui.settings.language == "en");
        for (auto v : draw.vertices) {
            REQUIRE(std::isfinite(v.x));
            REQUIRE(v.x >= 0 && v.x <= 1440);
            REQUIRE(v.y >= 0 && v.y <= 990);
        }
        ui.pointer(0, {700, 35}, true);
        REQUIRE(ui.dragPointer() == 0);
        ui.pointer(1, {-100, -100}, false);
        REQUIRE(ui.hovered() == 605);
        ui.pointer(0, {-100, -100}, false);
        REQUIRE(ui.dragPointer() == -1);
        SpatialMesh mesh;
        mesh.controllerHand({{0, 0, -1}, {}}, true, .5f);
        REQUIRE(mesh.vertices.size() > 500 && mesh.vertices.size() < 60000);
        auto matrix = viewProjection({{0, 0, 0}, {}}, -.7f, .7f, .7f, -.7f);
        REQUIRE(near(matrix[0], -matrix[5]));
        REQUIRE(matrix[11] == -1);
        for (auto v : mesh.vertices)
            REQUIRE(std::isfinite(v.x) && v.z < 0);
        mesh.clear();
        mesh.controllerHand({{}, {}}, false, 0, HandModel::Hidden);
        REQUIRE(mesh.vertices.empty());
        // The same authored surface mirrors the palm normal, not the finger order, in OpenXR grip space.
        HandSurface sample;
        sample.vertices = {{{-.02f, .01f, -.03f}, {0, 1, 0}}};
        mesh.controllerHand({{}, {}}, false, 0, HandModel::Studio, 1, 0, &sample);
        REQUIRE(near(mesh.vertices[0].x, .01f) && near(mesh.vertices[0].y, -.03f) &&
                near(mesh.vertices[0].z, -.02f));
        mesh.clear();
        mesh.controllerHand({{}, {}}, true, 0, HandModel::Studio, 1, 0, &sample);
        REQUIRE(near(mesh.vertices[0].x, -.01f) && near(mesh.vertices[0].nx, -1));
        HandSkin skin;
        HandJoints rig;
        for (auto &j : rig)
            j = {{{1, 2, 3}, {}}, .01f, true};
        skin.vertices = {{{0, 0, 0}, {0, 1, 0}, {0, 0, 0, 0}, {1, 0, 0, 0}},
                         {{.01f, 0, 0}, {0, 1, 0}, {0, 0, 0, 0}, {1, 0, 0, 0}},
                         {{0, 0, .01f}, {0, 1, 0}, {0, 0, 0, 0}, {1, 0, 0, 0}}};
        skin.indices = {0, 1, 2};
        REQUIRE(skin.valid());
        mesh.clear();
        REQUIRE(mesh.skinnedHand(skin, rig, Color::hex(0xffffff), 2));
        REQUIRE(near(mesh.vertices[1].x, 1.02f) && near(mesh.vertices[2].z, 3.02f));
        skin.indices[0] = 10;
        REQUIRE(!skin.valid());
        REQUIRE(HandSurface::load({0, 1, 2}).vertices.empty());
        // A click completes only if press and release belong to the same control.
        ui.pointer(0, {80, 270}, true);
        ui.pointer(0, {1400, 100}, false);
        REQUIRE(ui.page == Page::Home);
        click(ui, draw, {80, 270});
        REQUIRE(ui.page == Page::Components);
        click(ui, draw, {400, 390});
        REQUIRE(ui.modalOpen);
        REQUIRE(ui.widgets().size() == 2);
        click(ui, draw, {70, 440});
        REQUIRE(ui.page == Page::Components);
        REQUIRE(ui.modalOpen);
        click(ui, draw, {900, 610});
        REQUIRE(!ui.modalOpen);
        ui.navigate(Page::Settings);
        ui.drawMain(draw);
        ui.saveRequested = false;
        ui.pointer(0, {500, 390}, true);
        ui.pointer(0, {3000, 390}, true);
        // Storage waits for the release: hosts poll saveRequested on every rendered frame.
        REQUIRE(!ui.saveRequested);
        const auto dragRevision = ui.revision;
        ui.pointer(0, {3000, 390}, true);
        REQUIRE(ui.revision == dragRevision); // A held, motionless pointer does not redraw.
        ui.pointer(0, {3000, 390}, false);
        REQUIRE(near(ui.settings.distance, 2));
        REQUIRE(ui.recenterRequested);
        REQUIRE(ui.saveRequested);
        ui.saveRequested = false;
        ui.drawMain(draw);
        ui.pointer(0, {700, 390}, true);
        REQUIRE(!ui.saveRequested);
        ui.navigate(Page::Settings); // An interrupted drag keeps its value, so it must still be saved.
        REQUIRE(ui.saveRequested);
        ui.pointer(0, {-100, -100}, false);
        ui.settings.distance = 2;
        ui.saveRequested = true;
        ui.drawMain(draw);
        // Separate dock survives minimization/closing; content hit regions disappear.
        ui.drawDock(draw);
        for (auto v : draw.vertices) {
            REQUIRE(v.x >= 0 && v.x <= dockWidth);
            REQUIRE(v.y >= 0 && v.y <= dockHeight);
        }
        ui.pointer(0, {780, 1050}, true);
        ui.pointer(0, {780, 1050}, false);
        REQUIRE(ui.windowState == WindowState::Minimized);
        ui.drawDock(draw);
        for (auto &w : ui.widgets())
            REQUIRE(w.id >= 600 && w.id != 605);
        ui.pointer(0, {840, 1050}, true);
        ui.pointer(0, {840, 1050}, false);
        REQUIRE(ui.windowState == WindowState::Open);
        ui.drawMain(draw);
        ui.drawDock(draw);
        ui.pointer(0, {900, 1050}, true);
        ui.pointer(0, {900, 1050}, false);
        REQUIRE(ui.windowState == WindowState::Closed && ui.page == Page::Home);
        ui.drawDock(draw);
        ui.pointer(0, {840, 1050}, true);
        ui.pointer(0, {840, 1050}, false);
        REQUIRE(ui.windowState == WindowState::Open);
        ui.drawMain(draw);
        ui.drawDock(draw);
        ui.pointer(0, {660, 1050}, true);
        ui.pointer(0, {660, 1050}, false);
        REQUIRE(ui.settings.follow == FollowMode::FaceViewer && ui.saveRequested);
        for (int theme = 0; theme < 3; theme++)
            for (int tab = 0; tab < 3; tab++) {
                ui.settings.theme = Theme(theme);
                ui.settingsTab = tab;
                ui.navigate(Page::Settings);
                ui.drawMain(draw);
                for (auto &w : ui.widgets()) {
                    REQUIRE(w.rect.x >= 0 && w.rect.x + w.rect.w <= 1440);
                    REQUIRE(w.rect.y >= 0 && w.rect.y + w.rect.h <= 990);
                }
            }
        Settings saved;
        saved.language = "fr";
        saved.theme = Theme::Ocean;
        saved.background = Background::Night;
        saved.handModel = HandModel::Hidden;
        saved.follow = FollowMode::FollowView;
        saved.opacity = .4f;
        saved.blur = .7f;
        saved.handPitch = -35;
        saved.rays = false;
        REQUIRE(saveSettings("test-preferences.txt", saved));
        auto loaded = loadSettings("test-preferences.txt");
        REQUIRE(loaded.language == "fr");
        for (int environment = 0; environment < 6; ++environment) {
            saved.background = Background(environment);
            REQUIRE(saveSettings("test-preferences.txt", saved));
            REQUIRE(loadSettings("test-preferences.txt").background == saved.background);
        }
        REQUIRE(loaded.theme == saved.theme && loaded.handModel == saved.handModel &&
                loaded.follow == saved.follow);
        REQUIRE(near(loaded.opacity, .4f) && near(loaded.blur, .7f) && near(loaded.handPitch, -35) &&
                !loaded.rays);
        std::remove("test-preferences.txt");
        {
            std::ofstream legacy("test-preferences.txt");
            legacy << "distance 1.5\nscale 1\n";
        }
        REQUIRE(loadSettings("test-preferences.txt").language == "en");
        // Replacing an existing file must work and leave no temporary file behind.
        REQUIRE(saveSettings("test-preferences.txt", saved));
        REQUIRE(!std::ifstream("test-preferences.txt.tmp").good());
        REQUIRE(loadSettings("test-preferences.txt").theme == saved.theme);
        REQUIRE(!saveSettings("missing-directory/preferences.txt", saved));
        std::remove("test-preferences.txt");
        for (int p = 0; p < 4; p++) {
            ui.navigate(Page(p));
            ui.drawMain(draw);
            REQUIRE(draw.vertices.size() < 100000);
            for (auto v : draw.vertices) {
                REQUIRE(v.x >= 0 && v.x <= 1440);
                REQUIRE(v.y >= 0 && v.y <= 990);
            }
            std::cout << "UI page " << p << ": " << draw.vertices.size() << " vertices\n";
        }
        {
            // Distance shapes must stay inside their rectangle and in ui.frag's u range.
            DrawList shapes;
            shapes.rect({10, 20, 100, 60}, Color::hex(0xffffff), 12);
            REQUIRE(shapes.vertices.size() == 54);
            size_t curved = 0;
            for (const auto &v : shapes.vertices) {
                REQUIRE(v.x >= 10 && v.x <= 110 && v.y >= 20 && v.y <= 80);
                curved += v.u > 7;
            }
            REQUIRE(curved == 24);
            shapes.clear();
            shapes.rect({0, 0, 40, 40}, Color::hex(0xffffff));
            REQUIRE(shapes.vertices.size() == 6 && shapes.vertices[0].u < 1);
            shapes.clear();
            shapes.ring({50, 50}, 20, 5, Color::hex(0xffffff));
            REQUIRE(near(shapes.vertices[0].u, 10 + 4 * std::round(.75f * 255) - 1.05f));
            shapes.clear();
            shapes.circle({50, 50}, 20, Color::hex(0xffffff));
            REQUIRE(near(shapes.vertices[0].u, 10 - 1.05f));
            shapes.clear();
            // Images derive their texture coordinates from positions, so corners stay plain geometry.
            shapes.image({0, 0, 40, 40}, {0, 0, 1, 1}, {1, 1, 1, 1}, 8);
            for (const auto &v : shapes.vertices)
                REQUIRE(v.u <= 0);
            shapes.clear();
            const Vec2 path[]{{0, 0}, {10, 10}, {20, 0}};
            shapes.polyline(path, 3, Color::hex(0xffffff));
            REQUIRE(shapes.vertices.size() == 3 * 6 + 2 * 6);
            shapes.clear();
            shapes.line({0, 5.5f}, {40, 5.5f}, Color::hex(0xffffff), 1);
            REQUIRE(shapes.vertices.size() == 6 && shapes.vertices[0].u < 1);
            // Press and release both redraw, or pressed states would never appear on cached layers.
            UiModel pressable;
            DrawList pressDraw;
            pressable.drawMain(pressDraw);
            const auto before = pressable.revision;
            pressable.pointer(0, {80, 270}, true);
            REQUIRE(pressable.revision > before);
            const auto held = pressable.revision;
            pressable.pointer(0, {1400, 100}, false);
            REQUIRE(pressable.revision > held && pressable.page == Page::Home);
        }
        ui.log("Quote \" and slash \\");
        REQUIRE(ui.diagnosticJson().find("Quote \\\" and slash \\\\") != std::string::npos);
        for (int i = 0; i < 200; i++)
            ui.log("event");
        REQUIRE(ui.logs.size() == 80);
        std::cout << "PASS: ray geometry, angular scale, UTF-8, settings, statistics, pointer capture, modal "
                     "isolation, slider bounds, mesh bounds, diagnostic escaping\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
