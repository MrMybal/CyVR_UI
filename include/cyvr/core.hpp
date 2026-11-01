// SPDX-License-Identifier: MIT
#pragma once
#include "cyvr/locale.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace cyvr {
constexpr float pi = 3.14159265358979323846f;
constexpr int dockWidth = 480, dockHeight = 90, dockOriginX = (1440 - dockWidth) / 2, dockOriginY = 1014;
constexpr bool isDockWidget(int id) { return id >= 600 && id < 700 && id != 605; }
struct Vec2 {
    float x{}, y{};
};
struct Vec3 {
    float x{}, y{}, z{};
    Vec3 operator+(Vec3 v) const { return {x + v.x, y + v.y, z + v.z}; }
    Vec3 operator-(Vec3 v) const { return {x - v.x, y - v.y, z - v.z}; }
    Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
};
inline float dot(Vec3 a, Vec3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}
inline float length(Vec3 a) {
    return std::sqrt(dot(a, a));
}
inline Vec3 normalized(Vec3 a) {
    float l = length(a);
    return l > 1e-6f ? a * (1 / l) : Vec3{0, 0, -1};
}
struct Quat {
    float x{}, y{}, z{}, w{1};
};
inline Vec3 rotate(Quat q, Vec3 p) {
    Vec3 u{q.x, q.y, q.z};
    float s = q.w;
    Vec3 cross{u.y * p.z - u.z * p.y, u.z * p.x - u.x * p.z, u.x * p.y - u.y * p.x};
    return u * (2 * dot(u, p)) + p * (s * s - dot(u, u)) + cross * (2 * s);
}
inline Quat yawRotation(float angle) {
    return {0, std::sin(angle / 2), 0, std::cos(angle / 2)};
}
struct Pose {
    Vec3 position;
    Quat orientation;
};
struct Rect {
    float x{}, y{}, w{}, h{};
    bool contains(Vec2 p) const { return p.x >= x && p.y >= y && p.x < x + w && p.y < y + h; }
};
struct CanvasMapping {
    Rect viewport;
    float scale{1};
    Vec2 toCanvas(Vec2 point) const {
        return {(point.x-viewport.x)/scale,(point.y-viewport.y)/scale};
    }
    Vec2 toSurface(Vec2 point) const {
        return {viewport.x+point.x*scale,viewport.y+point.y*scale};
    }
};
CanvasMapping fitCanvas(float width, float height, float canvasWidth = 1440, float canvasHeight = 990);
struct PanelGeometry {
    Pose pose{{0, 1.45f, -1.35f}, {}};
    float width{1.2f}, height{0.825f};
    int pixelsW{1440}, pixelsH{990};
};
struct Hit {
    bool valid{};
    Vec2 pixels{};
    Vec3 world{};
    float distance{};
};
// One predicted controller sample, including the hit on the current UI panel.
// Pose/ray use the host's LOCAL space; timeNs is its monotonic prediction time.
struct ControllerInput {
    size_t hand{};
    Vec2 stick{};
    float trigger{}, grip{};
    Pose pose{};
    bool poseValid{};
    Hit pointer{};
    Vec3 rayOrigin{}, rayDirection{0,0,-1};
    bool rayValid{};
    float deltaSeconds{1.f/72};
    int64_t timeNs{};
};
Hit intersectPanel(Vec3 origin, Vec3 direction, const PanelGeometry &panel, bool clip = true);
PanelGeometry placePanel(Pose head, float distance, float scale, int pixelsW = 1440, int pixelsH = 990);
Quat faceViewer(Vec3 panel, Vec3 viewer);
Pose approachPose(Pose current, Pose target, float deltaSeconds, bool immediate = false);
std::vector<uint32_t> decodeUtf8(std::string_view text);
struct Color {
    float r{}, g{}, b{}, a{1};
    static Color hex(uint32_t rgb, float alpha = 1);
};
struct Vertex {
    float x, y, u, v, r, g, b, a;
};
// Optional authoring capture. Null during normal application rendering.
enum class PaintKind { Rect, Border, Circle, Ring, Line, Path, Triangle, Text, Image, Video, Overlay, Icon, Logo };
struct PaintCommand {
    PaintKind kind{PaintKind::Rect};
    Rect bounds{}, uv{};
    Color color;
    float radius{}, stroke{1}, fontSize{24};
    int icon{};
    bool closed{};
    std::string text;
    std::vector<Vec2> points;
};
struct DrawList {
    std::vector<Vertex> vertices;
    std::vector<PaintCommand> *capture{};
    DrawList() { vertices.reserve(48000); }
    void clear() { vertices.clear(); if (capture) capture->clear(); }
    template <class Render> void record(PaintCommand command, Render render) {
        auto *sink = capture;
        capture = nullptr;
        try {
            sink->push_back(std::move(command));
            render();
        } catch (...) { capture = sink; throw; }
        capture = sink;
    }
    void triangle(Vec2 a, Vec2 b, Vec2 c, Color color);
    // Rounded corners, circles and strokes are anti-aliased by ui.frag at any target resolution.
    void rect(Rect rect, Color color, float radius = 0);
    void border(Rect rect, Color color, float radius = 0, float width = 1);
    void circle(Vec2 center, float radius, Color color);
    void ring(Vec2 center, float radius, float width, Color color);
    void line(Vec2 from, Vec2 to, Color color, float width = 2);
    // Round caps and joins. Joints overlap, so use an opaque color.
    void polyline(const Vec2 *points, size_t count, Color color, float width = 2, bool closed = false);
    void text(float x, float y, float size, std::string_view value, Color color);
    void image(Rect rect, Rect uv, Color tint = {1, 1, 1, 1}, float radius = 0);
    // Separate borrowed video surface; application image atlases stay available.
    void video(Rect rect, Rect uv);
    void overlay(Rect rect, Rect uv = {0,0,1,1});
    float textWidth(float size, std::string_view value) const;
};
enum class Theme { Graphite, Light, Ocean };
// Original saved preference values remain stable.
enum class Background { Studio, Night, Passthrough, Lounge, Observatory, Terrace, Black, Dark };
enum class HandModel { Automatic, Studio, Skeleton, Hidden };
enum class FollowMode { World, FaceViewer, FollowView };
enum class WindowState { Open, Minimized, Closed };
enum class Presentation { Panel2D, Immersive };
enum WindowEdge : uint8_t { EdgeNone = 0, EdgeLeft = 1, EdgeRight = 2, EdgeTop = 4, EdgeBottom = 8 };
struct WindowOptions {
    bool movable{true}, resizable{true}, faceViewerWhileMoving{true};
    // Immersive resizing keeps the logical canvas aspect ratio.
    float minWidth{.45f}, maxWidth{3.f}, minDistance{.65f}, maxDistance{4.f};
    float edgePixels{28}, moveSpeed{1.3f}, turnSpeed{1.6f}, stickDeadZone{.18f};
};
struct Settings {
    std::string language{"en"};
    float distance{1.35f}, scale{1};
    bool highContrast{}, reducedMotion{}, haptics{true};
    float requestedHz{72};
    Theme theme{Theme::Graphite};
    Background background{Background::Studio};
    Background virtualBackground{Background::Studio}; // Restored when leaving optional AR.
    HandModel handModel{HandModel::Automatic};
    FollowMode follow{FollowMode::FaceViewer};
    bool minimalDock{true};
    float opacity{.9f}, blur{.5f}, brightness{.65f}, handScale{1}, handPitch{};
    float environmentDim{1}; // Linear environment-only dimmer; video layers are independent.
    bool rays{true};
    void sanitize();
};
struct FrameStats {
    std::array<double, 240> intervals{};
    size_t count{}, next{};
    double cpuMs{}, displayHz{}, rssMiB{};
    void sample(double ms);
    double fps() const;
    double percentile95() const;
};
enum class Page { Home, Components, Settings, Diagnostics };
enum class Control { Button, Slider, Drag };
enum class SliderPhase { Begin, Preview, Commit, Cancel };
struct Widget {
    int id;
    Rect rect;
    Control type{Control::Button};
    bool enabled{true};
};
struct Pointer {
    Vec2 pos{-100, -100};
    bool down{};
    int capture{}, hover{};
};
class UiModel {
  public:
    // Optional application layer. Returning false preserves the reference catalogue.
    std::function<bool(DrawList &)> renderApplication;
    std::function<bool(DrawList &)> renderApplicationDock;
    std::function<bool(DrawList &)> renderApplicationDebug;
    std::function<bool(int)> handleApplicationAction;
    // Optional embedded surface input. Consume only when no shared widget owns capture.
    std::function<bool(size_t, Vec2, bool)> applicationPointer;
    // Captured slider gestures: preview while dragging, commit once on release.
    std::function<bool(int, float, SliderPhase)> handleApplicationSlider;
    // Return true to consume selection for an application controller gesture.
    std::function<bool(int, Vec2, float, float, Pose, bool)> applicationController;
    // Preferred input hook, after resolving the current ray hit. True consumes
    // selection; return false for scrolling that should retain normal hovering.
    std::function<bool(const ControllerInput&)> applicationInput;
    // Logical-pixel scroll delta at the current system pointer (2D wheel/Quest).
    std::function<bool(Vec2,Vec2)> applicationScroll;
    bool scroll(Vec2 point,Vec2 pixels){
        return !interfaceHidden && !modalOpen && applicationScroll && applicationScroll(point,pixels);
    }
    bool controllerInput(const ControllerInput& input) {
        if(applicationInput)return applicationInput(input);
        return applicationController && applicationController(int(input.hand),input.stick,input.trigger,input.grip,input.pose,input.poseValid);
    }
    // Resolved controller/hand ray and pinch state, before stock panel hit testing.
    std::function<bool(size_t, Vec3, Vec3, bool, bool, Vec2, float)> applicationRay;
    // Observe button state even while a native widget owns pointer capture.
    std::function<void(size_t, Vec2, bool)> applicationPointerState;
    // Runs after shared chrome and before catalogue content. Settings can keep the stock page.
    std::function<bool(DrawList &)> renderContent;
    std::function<void(double)> applicationTick;
    std::function<bool()> applicationBack;
    std::array<std::string, 4> pageLabels{};
    std::string workspaceLabel, contextLabel; // Empty labels use the translated defaults.
    std::string sidebarTitle, sidebarSubtitle;
    std::string modalTitle;
    std::string modalMessage;
    std::string modalConfirm;
    void applicationWidget(int id, Rect rect, Control type = Control::Button, bool enabled = true) {
        addWidget(id, rect, type, enabled);
    }
    void clearApplicationWidgets() { widgets_.clear(); }
    const std::string &toast() const { return toast_; }
    Settings settings;
    WindowOptions window;
    uint8_t windowEdges{};
    bool windowGrabbed{}, windowResizing{};
    Presentation presentation{Presentation::Panel2D};
    bool immersiveAvailable{}, immersiveRequested{}, panelRequested{};
    // Application cinema mode. Hosts suppress their chrome; content keeps rendering.
    bool interfaceHidden{};
    // Application supplies its own compact playback controls, without stock dock/glass.
    bool compactInterface{};
    Vec2 viewportSize{1440, 990};
    FrameStats stats;
    Page page{Page::Home};
    bool debugVisible{}, recenterRequested{true}, exportRequested{}, exitRequested{}, saveRequested{};
    bool hapticRequested[2]{};
    bool modalOpen{};
    bool passthroughAvailable{};
    bool passthroughActive{}; // Runtime/compositor state, separate from the saved AR request.
    WindowState windowState{WindowState::Open};
    int settingsTab{};
    std::vector<float> refreshRates{72};
    std::string runtime{"Windows reference"};
    std::string status;
    std::string statusKey{"status.desktop"};
    std::vector<std::string> logs;
    uint64_t revision{1};
    void drawMain(DrawList &draw);
    void drawDebug(DrawList &draw);
    // Dock widgets use a separate surface in virtual coordinates below the main panel.
    void drawDock(DrawList &draw);
    void drawWindowAffordances(DrawList &draw) const;
    bool dockHitTest(Vec2 pixels) const;
    void updateDockPointer(size_t index, Vec2 pixels);
    float dockReveal() const { return settings.minimalDock ? dockReveal_ : 1.f; }
    void setWindowFeedback(uint8_t edges, bool grabbed, bool resizing);
    void pointer(size_t index, Vec2 position, bool pressed);
    void setInterfaceHidden(bool hidden);
    // True means the activating input was consumed solely to restore the UI.
    bool revealInterface();
    void navigate(Page next);
    void log(std::string value);
    void tick(double seconds);
    void invalidate() { ++revision; }
    void setToast(std::string text);
    void notify(std::string_view key) { setToast(tr(key)); }
    const char *tr(std::string_view key) const { return translate(settings.language, key); }
    const char *pageLabel(size_t index) const {
        static constexpr const char *keys[]{"nav.home", "nav.components", "nav.settings", "nav.diagnostics"};
        return index < pageLabels.size()
                   ? (pageLabels[index].empty() ? tr(keys[index]) : pageLabels[index].c_str())
                   : "";
    }
    void setLanguage(std::string_view language);
    bool setAugmentedReality(bool enabled);
    std::string statusText() const { return status.empty() ? tr(statusKey) : status; }
    std::string diagnosticJson() const;
    const std::vector<Widget> &widgets() const { return widgets_; }
    int hovered() const { return hover_; }
    int dragPointer() const {
        for (int i = 0; i < 2; i++)
            if (pointers_[i].down && (pointers_[i].capture == 600 || pointers_[i].capture == 605))
                return i;
        return -1;
    }
    bool interacting() const { return pointers_[0].down || pointers_[1].down; }
    int captured(size_t index) const { return index < 2 ? pointers_[index].capture : 0; }
    Vec2 pointerPosition(size_t index) const { return index<2?pointers_[index].pos:Vec2{-100,-100}; }

  private:
    std::string number(double value, int decimals = 1) const {
        return formatNumber(settings.language, value, decimals);
    }
    std::vector<Widget> widgets_;
    std::array<Pointer, 2> pointers_{};
    int hover_{};
    float exampleValue_{.62f};
    bool exampleToggle_{true};
    int exampleChoice_{};
    std::string toast_;
    double time_{}, toastUntil_{};
    bool sliderUnsaved_{};
    std::array<bool, 2> dockHovered_{};
    double dockHoldUntil_{};
    float dockReveal_{};
    bool pressed(int id) const { return pointers_[0].capture == id || pointers_[1].capture == id; }
    void action(int id);
    void slide(int id, float fraction, SliderPhase phase);
    void requestSliderSave();
    void cancelApplicationSliders();
    void addWidget(int id, Rect rect, Control type = Control::Button, bool enabled = true);
};
} // namespace cyvr
