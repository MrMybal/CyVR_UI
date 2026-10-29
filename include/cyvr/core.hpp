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
#include <vector>

namespace cyvr {
constexpr float pi = 3.14159265358979323846f;
constexpr int dockWidth = 480, dockHeight = 90, dockOriginX = (1440 - dockWidth) / 2, dockOriginY = 1014;
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
struct DrawList {
    std::vector<Vertex> vertices;
    DrawList() { vertices.reserve(48000); }
    void clear() { vertices.clear(); }
    void triangle(Vec2 a, Vec2 b, Vec2 c, Color color);
    void rect(Rect rect, Color color, float radius = 0);
    void line(Vec2 from, Vec2 to, Color color, float width = 2);
    void text(float x, float y, float size, std::string_view value, Color color);
    void image(Rect rect, Rect uv, Color tint = {1, 1, 1, 1}, float radius = 0);
    float textWidth(float size, std::string_view value) const;
};
enum class Theme { Graphite, Light, Ocean };
enum class Background { Studio, Night, Passthrough };
enum class HandModel { Automatic, Studio, Skeleton, Hidden };
enum class FollowMode { World, FaceViewer, FollowView };
enum class WindowState { Open, Minimized, Closed };
struct Settings {
    std::string language{"en"};
    float distance{1.35f}, scale{1};
    bool highContrast{}, reducedMotion{}, haptics{true};
    float requestedHz{72};
    Theme theme{Theme::Graphite};
    Background background{Background::Studio};
    HandModel handModel{HandModel::Automatic};
    FollowMode follow{FollowMode::World};
    float opacity{.9f}, blur{.5f}, brightness{.65f}, handScale{1}, handPitch{};
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
    std::function<bool(int)> handleApplicationAction;
    // Runs after shared chrome and before catalogue content. Settings can keep the stock page.
    std::function<bool(DrawList &)> renderContent;
    std::function<void(double)> applicationTick;
    std::array<std::string, 4> pageLabels{};
    std::string workspaceLabel{"WORKSPACE"}, contextLabel;
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
    FrameStats stats;
    Page page{Page::Home};
    bool debugVisible{}, recenterRequested{true}, exportRequested{}, exitRequested{}, saveRequested{};
    bool hapticRequested[2]{};
    bool modalOpen{};
    bool passthroughAvailable{};
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
    void pointer(size_t index, Vec2 position, bool pressed);
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
    void action(int id);
    void slide(int id, float fraction);
    void addWidget(int id, Rect rect, Control type = Control::Button, bool enabled = true);
};
} // namespace cyvr
