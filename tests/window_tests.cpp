// SPDX-License-Identifier: MIT
#include "cyvr/storage.hpp"
#include "cyvr/window.hpp"
#include <iostream>
#include <stdexcept>
using namespace cyvr;
#define REQUIRE(x)                                                                                           \
    do {                                                                                                     \
        if (!(x))                                                                                            \
            throw std::runtime_error("Window check failed at " + std::to_string(__LINE__) + ": " #x);        \
    } while (false)
bool near(float a, float b) {
    return std::abs(a - b) < .0001f;
}
Pose viewer{{0, 1.5f, 0}, {}};
WindowPointer point(const PanelGeometry &panel, float x = 720, float y = 495) {
    WindowPointer input;
    input.valid = true;
    input.onPanel = true;
    input.origin = viewer.position;
    input.deltaSeconds = .05f;
    auto target =
        panel.pose.position + rotate(panel.pose.orientation, {(x / panel.pixelsW - .5f) * panel.width,
                                                              (.5f - y / panel.pixelsH) * panel.height, 0});
    input.direction = normalized(target - input.origin);
    input.hit = intersectPanel(input.origin, input.direction, panel);
    return input;
}
void grab(WindowMotion &motion, WindowPointer &input, PanelGeometry &panel,
          const WindowOptions &options = {}) {
    input.grip = 0;
    motion.update(0, input, panel, viewer, true, options);
    input.grip = .9f;
    auto result = motion.update(0, input, panel, viewer, true, options);
    REQUIRE(result.started && result.consumed);
}
int main() {
    try {
        WindowOptions options;
        {
            // Compact applications have no grabbable surface. An explicit trigger handle
            // becomes a target only after capture, and must still start on that first press.
            auto panel = placePanel(viewer, 1.35f, 1);
            WindowMotion motion;
            auto input = point(panel);
            const auto handleHit = input.hit;
            input.hit = {};
            input.onPanel = false;
            REQUIRE(!motion.update(0, input, panel, viewer, true, options).consumed);
            input.pointerDrag = true;
            input.hit = handleHit;
            auto start = motion.update(0, input, panel, viewer, true, options);
            REQUIRE(start.started && start.pointerDrag && !start.resizing);
            const auto before = panel.pose.position;
            input.origin.x += .2f;
            REQUIRE(motion.update(0, input, panel, viewer, true, options).changed);
            REQUIRE(panel.pose.position.x > before.x + .1f);
            input.pointerDrag = false;
            REQUIRE(motion.update(0, input, panel, viewer, true, options).ended);
        }
        REQUIRE(Settings{}.follow == FollowMode::FaceViewer && Settings{}.minimalDock);
        {
            HandJoints joints{};
            auto finger = [&](int base, bool closed) {
                std::array<Vec3, 4> positions =
                    closed
                        ? std::array<Vec3, 4>{{{0, 0, 0}, {0, .03f, 0}, {0, .03f, .025f}, {0, .01f, .015f}}}
                        : std::array<Vec3, 4>{{{0, 0, 0}, {0, .03f, 0}, {0, .06f, 0}, {0, .08f, 0}}};
                for (int j = 0; j < 4; j++)
                    joints[base + j] = {{positions[j], {}}, .005f, true};
            };
            for (int base : {7, 12, 17, 22})
                finger(base, false);
            REQUIRE(handGrabStrength(joints) == 0); // An open hand/index pinch does not grab.
            finger(12, true);
            REQUIRE(handGrabStrength(joints) == 0);
            finger(17, true);
            finger(22, true);
            REQUIRE(handGrabStrength(joints)==0); // Pointing with curled lower fingers is not a fist.
            finger(7,true);
            REQUIRE(handGrabStrength(joints) > .99f);
            HandGrabRay handRay;
            Pose wrist{{0, 1.2f, -.25f}, {}}, aim{{.02f, 1.25f, -.3f}, {}}, ray;
            REQUIRE(!handRay.update(wrist, true, aim, false, 1, ray)); // No stale ray before initial aim.
            REQUIRE(handRay.update(wrist, true, aim, true, 0, ray));
            REQUIRE(handRay.update(wrist, true, aim, false, 1, ray));
            REQUIRE(length(ray.position - aim.position) < .0001f);
            wrist.position.x += .2f;
            wrist.orientation = yawRotation(.3f);
            REQUIRE(handRay.update(wrist, true, {}, false, 1, ray));
            REQUIRE(ray.position.x > .19f && std::abs(ray.orientation.y) > .1f);
            REQUIRE(!handRay.update(wrist, false, {}, false, 1, ray));
            REQUIRE(!handRay.update(wrist, true, {}, false, 1, ray));
            REQUIRE(handRay.update(wrist, true, aim, true, 0, ray));
            joints[24].valid = false;
            REQUIRE(handGrabStrength(joints) == 0);
        }
        {
            // Surface grips have no widget requirement: navigation, buttons and empty content.
            for (Vec2 p : {Vec2{90, 170}, Vec2{450, 520}, Vec2{800, 450}, Vec2{1300, 950}, Vec2{730, 95}}) {
                auto panel = placePanel(viewer, 1.35f, 1);
                WindowMotion motion;
                auto input = point(panel, p.x, p.y);
                grab(motion, input, panel);
                REQUIRE(!motion.edges());
                auto before = panel.pose.position;
                input.origin.x += .1f;
                motion.update(0, input, panel, viewer, true, options);
                REQUIRE(panel.pose.position.x > before.x + .07f);
            }
        }
        {
            auto panel = placePanel(viewer, 1.35f, 1);
            auto initial = panel;
            WindowMotion motion;
            auto input = point(panel);
            grab(motion, input, panel);
            REQUIRE(near(length(panel.pose.position - initial.pose.position), 0));
            input.origin.x += .2f;
            auto result = motion.update(0, input, panel, viewer, true, options);
            REQUIRE(result.changed && panel.pose.position.x > .19f);
            REQUIRE(std::abs(panel.pose.orientation.y) > .03f);
            float before = length(panel.pose.position - viewer.position);
            input.stick = {0, 1};
            for (int n = 0; n < 8; n++)
                motion.update(0, input, panel, viewer, true, options);
            REQUIRE(length(panel.pose.position - viewer.position) > before + .35f);
            auto forward = rotate(panel.pose.orientation, {0, 0, 1});
            input.stick = {1, 0};
            for (int n = 0; n < 8; n++)
                motion.update(0, input, panel, viewer, true, options);
            REQUIRE(length(rotate(panel.pose.orientation, {0, 0, 1}) - forward) > .3f);
            input.grip = .5f;
            REQUIRE(motion.update(0, input, panel, viewer, true, options).consumed); // Hysteresis.
            input.grip = 0;
            result = motion.update(0, input, panel, viewer, true, options);
            REQUIRE(result.ended && !motion.active());
            auto rotation = motion.facing(panel.pose.position, viewer.position);
            motion.recenter();
            REQUIRE(std::abs(rotation.y - motion.facing(panel.pose.position, viewer.position).y) > .1f);
        }
        {
            auto panel = placePanel(viewer, 1.35f, 1);
            WindowMotion motion;
            auto input = point(panel, 500, 400);
            grab(motion, input, panel);
            motion.update(0, input, panel, viewer, true, options);
            motion.update(0, input, panel, viewer, true, options);
            auto center = panel.pose.position;
            input.stick = {.8f, 0};
            motion.update(0, input, panel, viewer, true, options);
            REQUIRE(length(panel.pose.position - center) < .001f);
            center = panel.pose.position;
            input.stick = {};
            motion.update(0, input, panel, viewer, true, options);
            REQUIRE(length(panel.pose.position - center) < .001f);
        }
        {
            auto panel = placePanel(viewer, 1.35f, 1);
            auto initial = panel;
            WindowMotion motion;
            auto input = point(panel, 1430, 495);
            grab(motion, input, panel);
            REQUIRE(motion.edges() == EdgeRight);
            auto fixed = panel.pose.position + rotate(panel.pose.orientation, {-panel.width / 2, 0, 0});
            auto target = input.hit.world + Vec3{.35f, 0, 0};
            input.direction = normalized(target - input.origin);
            auto result = motion.update(0, input, panel, viewer, true, options);
            REQUIRE(result.resizing && panel.width > initial.width + .34f);
            REQUIRE(near(panel.width / panel.height, initial.width / initial.height));
            REQUIRE(length(fixed - (panel.pose.position +
                                    rotate(panel.pose.orientation, {-panel.width / 2, 0, 0}))) < .0001f);
            input.stick = {.5f, .5f};
            motion.update(0, input, panel, viewer, true, options);
            float width = panel.width;
            auto center = panel.pose.position;
            input.stick = {};
            motion.update(0, input, panel, viewer, true, options);
            REQUIRE(near(panel.width, width) && length(panel.pose.position - center) < .0001f);
            options.resizable = false;
            REQUIRE(motion.update(0, input, panel, viewer, true, options).ended);
            options = {};
        }
        {
            auto panel = placePanel(viewer, 1.35f, 1);
            WindowMotion motion;
            auto input = point(panel, 1430, 10);
            grab(motion, input, panel);
            REQUIRE(motion.edges() == (EdgeRight | EdgeTop));
            auto fixed = panel.pose.position +
                         rotate(panel.pose.orientation, {-panel.width / 2, -panel.height / 2, 0});
            input.direction = normalized(input.hit.world + Vec3{.25f, .2f, 0} - input.origin);
            motion.update(0, input, panel, viewer, true, options);
            REQUIRE(length(fixed - (panel.pose.position + rotate(panel.pose.orientation,
                                                                 {-panel.width / 2, -panel.height / 2, 0}))) <
                    .0001f);
            input.direction = normalized(input.hit.world + Vec3{4, 4, 0} - input.origin);
            motion.update(0, input, panel, viewer, true, options);
            REQUIRE(near(panel.width, options.maxWidth));
            input.direction = normalized(input.hit.world + Vec3{-4, -4, 0} - input.origin);
            motion.update(0, input, panel, viewer, true, options);
            REQUIRE(near(panel.width, options.minWidth));
        }
        {
            auto panel = placePanel(viewer, 1.35f, 1);
            WindowMotion motion;
            auto input = point(panel, 10, 495);
            options.resizable = false;
            grab(motion, input, panel, options);
            REQUIRE(motion.edges() == 0);
            options = {};
            input.valid = false;
            REQUIRE(motion.update(0, input, panel, viewer, true, options).ended);
            input.valid = true;
            REQUIRE(!motion.update(0, input, panel, viewer, true, options).started);
            input.grip = 0;
            motion.update(0, input, panel, viewer, true, options);
            input.grip = .9f;
            REQUIRE(motion.update(0, input, panel, viewer, true, options).started);
            motion.cancel();
            input.grip = 0;
            options.movable = options.resizable = false;
            motion.update(0, input, panel, viewer, true, options);
            input.grip = 1;
            REQUIRE(!motion.update(0, input, panel, viewer, true, options).consumed);
            options = {};
        }
        {
            auto panel = placePanel(viewer, 1.35f, 1);
            WindowMotion motion;
            auto left = point(panel), right = left;
            motion.update(0, left, panel, viewer, true, options);
            motion.update(1, right, panel, viewer, true, options);
            left.grip = 1;
            REQUIRE(motion.update(0, left, panel, viewer, true, options).started);
            right.grip = 1;
            REQUIRE(!motion.update(1, right, panel, viewer, true, options).consumed);
            REQUIRE(motion.hand() == 0);
            left.grip = 0;
            motion.update(0, left, panel, viewer, true, options);
            REQUIRE(!motion.update(1, right, panel, viewer, true, options).started);
            right.grip = 0;
            motion.update(1, right, panel, viewer, true, options);
            right.grip = 1;
            REQUIRE(motion.update(1, right, panel, viewer, true, options).started);
        }
        {
            auto panel = placePanel(viewer, 1.35f, 1);
            WindowMotion motion;
            auto input = point(panel);
            input.onPanel = false;
            motion.update(0, input, panel, viewer, true, options);
            input.pointerDrag = true;
            auto result = motion.update(0, input, panel, viewer, true, options);
            REQUIRE(result.started && result.pointerDrag);
            input.origin.x += .15f;
            motion.update(0, input, panel, viewer, true, options);
            REQUIRE(panel.pose.position.x > .14f);
            input.pointerDrag = false;
            REQUIRE(motion.update(0, input, panel, viewer, true, options).ended);
        }
        {
            auto panel = placePanel(viewer, 1.35f, 1);
            WindowMotion motion;
            auto input = point(panel);
            grab(motion, input, panel);
            motion.update(0, input, panel, viewer, true, options);
            auto settled = panel;
            input.stick = {.1f, -.1f};
            auto result = motion.update(0, input, panel, viewer, true, options);
            REQUIRE(!result.changed);
            input.stick = {0, 1};
            for (int n = 0; n < 200; n++)
                motion.update(0, input, panel, viewer, true, options);
            REQUIRE(length(panel.pose.position - viewer.position) <= options.maxDistance + .001f);
            input.stick = {0, -1};
            for (int n = 0; n < 200; n++)
                motion.update(0, input, panel, viewer, true, options);
            REQUIRE(length(panel.pose.position - viewer.position) >= options.minDistance - .001f);
            REQUIRE(std::isfinite(settled.width));
        }
        {
            UiModel ui;
            DrawList main, dock;
            ui.presentation = Presentation::Immersive;
            ui.drawMain(main);
            ui.drawDock(dock);
            REQUIRE(ui.dockReveal() == 0);
            const auto fixedBar = dock.vertices;
            REQUIRE(std::none_of(ui.widgets().begin(), ui.widgets().end(),
                                 [](auto &w) { return w.id == 611 || w.id == 612; }));
            REQUIRE(!ui.dockHitTest({float(dockOriginX + 20), float(dockOriginY + 40)}));
            REQUIRE(ui.dockHitTest({float(dockOriginX + 116), float(dockOriginY + 44)}));
            ui.pointer(0, {float(dockOriginX + 116), float(dockOriginY + 44)}, false);
            ui.tick(.1);
            ui.drawMain(main);
            ui.drawDock(dock);
            REQUIRE(ui.dockReveal() > .99f);
            REQUIRE(dock.vertices.size() > fixedBar.size());
            for (size_t i = 0; i < fixedBar.size(); i++) {
                const auto &a = fixedBar[i], &b = dock.vertices[i];
                REQUIRE(a.x == b.x && a.y == b.y && a.a == b.a && a.r == b.r && a.g == b.g && a.b == b.b);
            }
            for (const auto &w : ui.widgets())
                if (w.id == 610 || w.id == 611 || w.id == 612) {
                    REQUIRE(w.rect.x + w.rect.w <= dockOriginX + 148 || w.rect.x >= dockOriginX + 332);
                }
            REQUIRE(
                std::any_of(ui.widgets().begin(), ui.widgets().end(), [](auto &w) { return w.id == 611; }));
            ui.pointer(0, {-100, -100}, false);
            ui.tick(.3);
            REQUIRE(ui.dockReveal() > .99f);
            ui.tick(.7);
            ui.tick(.9);
            ui.drawMain(main);
            ui.drawDock(dock);
            REQUIRE(ui.dockReveal() == 0);
            ui.pointer(0, {900, 1050}, true);
            ui.pointer(0, {900, 1050}, false);
            REQUIRE(ui.windowState == WindowState::Open);
            ui.settings.reducedMotion = true;
            ui.pointer(0, {720, 1058}, false);
            ui.tick(.91);
            REQUIRE(ui.dockReveal() == 1);
            ui.navigate(Page::Settings);
            ui.settingsTab = 0;
            ui.drawMain(main);
            ui.pointer(0, {700, 875}, true);
            ui.pointer(0, {700, 875}, false);
            REQUIRE(!ui.settings.minimalDock); // The real setting control must dispatch.
            ui.setWindowFeedback(EdgeLeft | EdgeTop, true, true);
            ui.drawMain(main);
            REQUIRE(ui.windowResizing);
            ui.presentation = Presentation::Panel2D;
            ui.drawDock(dock);
            REQUIRE(dock.vertices.empty());
            Settings saved;
            saved.minimalDock = false;
            REQUIRE(saveSettings("window-test-preferences.txt", saved));
            REQUIRE(!loadSettings("window-test-preferences.txt").minimalDock);
            std::filesystem::remove("window-test-preferences.txt");
        }
        std::cout << "Window grip, resize, joystick, ownership, tracking loss and minimal dock passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}
