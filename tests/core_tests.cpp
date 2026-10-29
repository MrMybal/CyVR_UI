// SPDX-License-Identifier: MIT
#include "cyvr/core.hpp"
#include "cyvr/spatial.hpp"
#include "cyvr/storage.hpp"
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
        click(panelUi, panelDraw, {945, 35});
        REQUIRE(panelUi.immersiveRequested && !panelUi.panelRequested);
        panelUi.presentation = Presentation::Immersive;
        panelUi.drawMain(panelDraw);
        click(panelUi, panelDraw, {945, 35});
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
        ui.pointer(0, {500, 390}, true);
        ui.pointer(0, {3000, 390}, true);
        ui.pointer(0, {3000, 390}, false);
        REQUIRE(near(ui.settings.distance, 2));
        REQUIRE(ui.recenterRequested);
        REQUIRE(ui.saveRequested);
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
        std::remove("test-preferences.txt");
        for (int p = 0; p < 4; p++) {
            ui.navigate(Page(p));
            ui.drawMain(draw);
            REQUIRE(draw.vertices.size() < 100000);
            for (auto v : draw.vertices) {
                REQUIRE(v.x >= 0 && v.x <= 1440);
                REQUIRE(v.y >= 0 && v.y <= 990);
            }
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
