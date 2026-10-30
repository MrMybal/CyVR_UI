// SPDX-License-Identifier: MIT
#include "cyvr/core.hpp"
#include "cyvr/font_data.hpp"
#include <cstdio>
#include <numeric>
#include <sstream>

namespace cyvr {
CanvasMapping fitCanvas(float width, float height, float canvasWidth, float canvasHeight) {
    if (!std::isfinite(width) || !std::isfinite(height) || !std::isfinite(canvasWidth) ||
        !std::isfinite(canvasHeight) || width <= 0 || height <= 0 || canvasWidth <= 0 || canvasHeight <= 0)
        return {{}, 1};
    float scale = std::min(width / canvasWidth, height / canvasHeight);
    return {{(width - canvasWidth * scale) * .5f, (height - canvasHeight * scale) * .5f, canvasWidth * scale,
             canvasHeight * scale},
            scale};
}
Hit intersectPanel(Vec3 origin, Vec3 direction, const PanelGeometry &p, bool clip) {
    direction = normalized(direction);
    Vec3 normal = rotate(p.pose.orientation, {0, 0, 1});
    float denom = dot(direction, normal);
    if (denom >= -1e-5f)
        return {};
    float t = dot(p.pose.position - origin, normal) / denom;
    if (t <= 0 || t > 8)
        return {};
    Vec3 world = origin + direction * t;
    Quat inverse{-p.pose.orientation.x, -p.pose.orientation.y, -p.pose.orientation.z, p.pose.orientation.w};
    Vec3 local = rotate(inverse, world - p.pose.position);
    if (clip && (std::abs(local.x) > p.width * .5f || std::abs(local.y) > p.height * .5f))
        return {};
    return {true, {(local.x / p.width + .5f) * p.pixelsW, (.5f - local.y / p.height) * p.pixelsH}, world, t};
}
PanelGeometry placePanel(Pose head, float distance, float scale, int w, int h) {
    distance = std::clamp(distance, 1.f, 2.f);
    scale = std::clamp(scale, .85f, 1.25f);
    Vec3 f = rotate(head.orientation, {0, 0, -1});
    f.y = 0;
    f = normalized(f);
    float yaw = std::atan2(-f.x, -f.z);
    float width = 1.2f * (distance / 1.35f) * scale;
    return {{head.position + f * distance + Vec3{0, -.10f, 0}, yawRotation(yaw)},
            width,
            width * float(h) / w,
            w,
            h};
}
Quat faceViewer(Vec3 panel, Vec3 viewer) {
    Vec3 direction = viewer - panel;
    float yaw = std::atan2(direction.x, direction.z);
    float pitch =
        -std::clamp(std::atan2(direction.y, std::sqrt(direction.x * direction.x + direction.z * direction.z)),
                    -pi * .25f, pi * .25f);
    float sy = std::sin(yaw * .5f), cy = std::cos(yaw * .5f), sx = std::sin(pitch * .5f),
          cx = std::cos(pitch * .5f);
    return {cy * sx, sy * cx, -sy * sx, cy * cx};
}
Pose approachPose(Pose current, Pose target, float dt, bool immediate) {
    float t = immediate ? 1.f : 1 - std::exp(-std::clamp(dt, 0.f, .1f) * 5.f);
    current.position = current.position + (target.position - current.position) * t;
    Quat a = current.orientation, b = target.orientation;
    float sign = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w < 0 ? -1.f : 1.f;
    Quat q{a.x + (b.x * sign - a.x) * t, a.y + (b.y * sign - a.y) * t, a.z + (b.z * sign - a.z) * t,
           a.w + (b.w * sign - a.w) * t};
    float norm = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    current.orientation = {q.x / norm, q.y / norm, q.z / norm, q.w / norm};
    return current;
}
std::vector<uint32_t> decodeUtf8(std::string_view text) {
    std::vector<uint32_t> result;
    result.reserve(text.size());
    for (size_t i = 0; i < text.size();) {
        uint8_t first = uint8_t(text[i++]);
        if (first < 128) {
            result.push_back(first);
            continue;
        }
        int n = first >= 0xf0 && first <= 0xf4   ? 3
                : first >= 0xe0 && first <= 0xef ? 2
                : first >= 0xc2 && first <= 0xdf ? 1
                                                 : 0;
        uint32_t value = n ? first & ((1u << (6 - n)) - 1) : uint32_t('?');
        if (!n || i + n > text.size()) {
            result.push_back('?');
            continue;
        }
        bool valid = true;
        for (int k = 0; k < n; k++) {
            uint8_t c = uint8_t(text[i + k]);
            if ((c & 0xc0) != 0x80) {
                valid = false;
                break;
            }
            value = (value << 6) | (c & 0x3f);
        }
        if (!valid) {
            result.push_back('?');
            continue;
        }
        i += n;
        if (value < (n == 1   ? 0x80u
                     : n == 2 ? 0x800u
                              : 0x10000u) ||
            value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff))
            value = '?';
        result.push_back(value);
    }
    return result;
}
Color Color::hex(uint32_t rgb, float alpha) {
    auto linear = [](float c) {
        c /= 255;
        return c <= .04045f ? c / 12.92f : std::pow((c + .055f) / 1.055f, 2.4f);
    };
    return {linear(float((rgb >> 16) & 255)), linear(float((rgb >> 8) & 255)), linear(float(rgb & 255)),
            alpha};
}
static const Glyph &glyph(uint32_t code) {
    auto it = std::lower_bound(std::begin(glyphs), std::end(glyphs), code,
                               [](const Glyph &g, uint32_t c) { return g.code < c; });
    return it != std::end(glyphs) && it->code == code ? *it : glyphs['?' - 32];
}
void DrawList::triangle(Vec2 a, Vec2 b, Vec2 c, Color color) {
    for (auto p : {a, b, c})
        vertices.push_back({p.x, p.y, .0015f, .0015f, color.r, color.g, color.b, color.a});
}
// ui.frag treats u >= 8 as a distance shape: u = 10 + 4 * band + p.x, v = p.y, where |p| = 1 is the
// outer edge and band / 255 the inner edge of a ring. Band 0 is solid.
static void shape(std::vector<Vertex> &out, Rect r, Vec2 p0, Vec2 p1, Color color, float inner = 0) {
    const float offset = 10 + 4 * std::round(std::clamp(inner, 0.f, 1.f) * 255);
    auto v = [&](float x, float y, float px, float py) {
        return Vertex{x, y, offset + px, py, color.r, color.g, color.b, color.a};
    };
    Vertex a = v(r.x, r.y, p0.x, p0.y), b = v(r.x + r.w, r.y, p1.x, p0.y),
           c = v(r.x + r.w, r.y + r.h, p1.x, p1.y), d = v(r.x, r.y + r.h, p0.x, p1.y);
    for (auto vertex : {a, b, c, a, c, d})
        out.push_back(vertex);
}
static void solidRect(DrawList &d, Rect r, Color color) {
    if (r.w <= 0 || r.h <= 0)
        return;
    d.triangle({r.x, r.y}, {r.x + r.w, r.y}, {r.x, r.y + r.h}, color);
    d.triangle({r.x + r.w, r.y}, {r.x + r.w, r.y + r.h}, {r.x, r.y + r.h}, color);
}
static void corners(std::vector<Vertex> &out, Rect r, float radius, Color color, float inner) {
    float right = r.x + r.w - radius, bottom = r.y + r.h - radius;
    shape(out, {r.x, r.y, radius, radius}, {-1, -1}, {0, 0}, color, inner);
    shape(out, {right, r.y, radius, radius}, {0, -1}, {1, 0}, color, inner);
    shape(out, {right, bottom, radius, radius}, {0, 0}, {1, 1}, color, inner);
    shape(out, {r.x, bottom, radius, radius}, {-1, 0}, {0, 1}, color, inner);
}
void DrawList::rect(Rect r, Color color, float radius) {
    if (r.w <= 0 || r.h <= 0)
        return;
    radius = std::min({radius, r.w * .5f, r.h * .5f});
    if (radius < 1) {
        solidRect(*this, r, color);
        return;
    }
    // Nine slices share their vertices, so no T-junction can open a crack when scaled.
    float x0 = r.x + radius, x1 = r.x + r.w - radius, y0 = r.y + radius, y1 = r.y + r.h - radius;
    solidRect(*this, {x0, r.y, x1 - x0, radius}, color);
    solidRect(*this, {r.x, y0, radius, y1 - y0}, color);
    solidRect(*this, {x0, y0, x1 - x0, y1 - y0}, color);
    solidRect(*this, {x1, y0, radius, y1 - y0}, color);
    solidRect(*this, {x0, y1, x1 - x0, radius}, color);
    corners(vertices, r, radius, color, 0);
}
void DrawList::border(Rect r, Color color, float radius, float width) {
    if (r.w <= 0 || r.h <= 0 || width <= 0)
        return;
    radius = std::max(width, std::min({radius, r.w * .5f, r.h * .5f}));
    float x0 = r.x + radius, x1 = r.x + r.w - radius, y0 = r.y + radius, y1 = r.y + r.h - radius;
    solidRect(*this, {x0, r.y, x1 - x0, width}, color);
    solidRect(*this, {x0, r.y + r.h - width, x1 - x0, width}, color);
    solidRect(*this, {r.x, y0, width, y1 - y0}, color);
    solidRect(*this, {r.x + r.w - width, y0, width, y1 - y0}, color);
    corners(vertices, r, radius, color, (radius - width) / radius);
}
void DrawList::circle(Vec2 c, float radius, Color color) {
    ring(c, radius, radius, color);
}
void DrawList::ring(Vec2 c, float radius, float width, Color color) {
    if (radius <= 0)
        return;
    // One pixel of margin keeps the edge filter from flattening the four tangent points.
    float pad = std::min(radius, 1.f), extent = 1 + pad / radius;
    shape(vertices, {c.x - radius - pad, c.y - radius - pad, 2 * (radius + pad), 2 * (radius + pad)},
          {-extent, -extent}, {extent, extent}, color, 1 - width / radius);
}
static void capsule(std::vector<Vertex> &out, Vec2 a, Vec2 b, Color color, float width) {
    float dx = b.x - a.x, dy = b.y - a.y, l = std::sqrt(dx * dx + dy * dy);
    if (l < .001f)
        return;
    float half = width * .5f, pad = half + 1, extent = pad / half;
    Vec2 o{-dy / l * pad, dx / l * pad};
    auto v = [&](Vec2 p, float across) {
        return Vertex{p.x, p.y, 10, across, color.r, color.g, color.b, color.a};
    };
    Vertex p = v({a.x + o.x, a.y + o.y}, extent), q = v({b.x + o.x, b.y + o.y}, extent),
           r = v({b.x - o.x, b.y - o.y}, -extent), s = v({a.x - o.x, a.y - o.y}, -extent);
    for (auto vertex : {p, q, r, p, r, s})
        out.push_back(vertex);
}
void DrawList::polyline(const Vec2 *points, size_t count, Color color, float width, bool closed) {
    for (size_t i = 0; i < count; i++) {
        circle(points[i], width * .5f, color);
        if (i + 1 < count)
            capsule(vertices, points[i], points[i + 1], color, width);
    }
    if (closed && count > 2)
        capsule(vertices, points[count - 1], points[0], color, width);
}
static void roundedFan(DrawList &d, Rect r, Color color, float radius) {
    if (radius <= 0) {
        solidRect(d, r, color);
        return;
    }
    Vec2 middle{r.x + r.w * .5f, r.y + r.h * .5f};
    std::array<Vec2, 4> centers{{{r.x + r.w - radius, r.y + radius},
                                 {r.x + r.w - radius, r.y + r.h - radius},
                                 {r.x + radius, r.y + r.h - radius},
                                 {r.x + radius, r.y + radius}}};
    Vec2 first{}, previous{};
    bool begin = true;
    for (int corner = 0; corner < 4; corner++)
        for (int j = 0; j <= 8; j++) {
            float angle = (-.5f + corner * .5f + j / 16.f) * pi;
            Vec2 point{centers[corner].x + std::cos(angle) * radius,
                       centers[corner].y + std::sin(angle) * radius};
            if (begin) {
                first = point;
                begin = false;
            } else
                d.triangle(middle, previous, point, color);
            previous = point;
        }
    d.triangle(middle, previous, first, color);
}
void DrawList::line(Vec2 a, Vec2 b, Color color, float width) {
    float dx = b.x - a.x, dy = b.y - a.y, l = std::sqrt(dx * dx + dy * dy);
    if (l < .001f)
        return;
    if (a.x == b.x || a.y == b.y) {
        // Axis-aligned hairlines stay on the pixel grid instead of softening over two rows.
        Vec2 o{-dy / l * width * .5f, dx / l * width * .5f};
        Vec2 p{a.x + o.x, a.y + o.y}, q{b.x + o.x, b.y + o.y}, r{b.x - o.x, b.y - o.y},
            s{a.x - o.x, a.y - o.y};
        triangle(p, q, r, color);
        triangle(p, r, s, color);
        return;
    }
    capsule(vertices, a, b, color, width);
}
void DrawList::image(Rect r, Rect uv, Color tint, float radius) {
    if (r.w <= 0 || r.h <= 0) return;
    const auto first = vertices.size();
    // Texture coordinates are derived from positions, so images keep plain geometry.
    roundedFan(*this, r, tint, std::min({radius, r.w * .5f, r.h * .5f}));
    for (size_t i=first;i<vertices.size();++i) {
        auto &v=vertices[i];
        v.u=-2.f+uv.x+(v.x-r.x)/r.w*uv.w;
        v.v=uv.y+(v.y-r.y)/r.h*uv.h;
    }
}
void DrawList::video(Rect r, Rect uv) {
    const auto first=vertices.size();image(r,uv);
    for(auto i=first;i<vertices.size();++i)vertices[i].u-=2.f;
}
void DrawList::overlay(Rect r, Rect uv) {
    const auto first=vertices.size();image(r,uv);
    for(auto i=first;i<vertices.size();++i)vertices[i].u-=4.f;
}
float DrawList::textWidth(float size, std::string_view text) const {
    float width = 0;
    for (auto c : decodeUtf8(text))
        width += glyph(c).advance * size / 48;
    return width;
}
void DrawList::text(float x, float y, float size, std::string_view value, Color color) {
    float start = x, scale = size / 48;
    for (uint32_t code : decodeUtf8(value)) {
        if (code == '\n') {
            x = start;
            y += size * 1.5f;
            continue;
        }
        const auto &g = glyph(code);
        float left = x + g.left * scale, top = y + size + g.top * scale;
        if (g.w && g.h) {
            auto v = [&](float px, float py, float u, float vv) {
                return Vertex{px, py, u, vv, color.r, color.g, color.b, color.a};
            };
            float u = g.x / 1024.f, vv = g.y / 1024.f, u2 = (g.x + g.w) / 1024.f, v2 = (g.y + g.h) / 1024.f;
            float right = left + g.w * scale, bottom = top + g.h * scale;
            Vertex a = v(left, top, u, vv), b = v(right, top, u2, vv), c = v(right, bottom, u2, v2),
                   d = v(left, bottom, u, v2);
            for (auto p : {a, b, c, a, c, d})
                vertices.push_back(p);
        }
        x += g.advance * scale;
    }
}
void Settings::sanitize() {
    language = resolveLocale(language).code;
    if (!std::isfinite(distance))
        distance = 1.35f;
    if (!std::isfinite(scale))
        scale = 1;
    distance = std::clamp(distance, 1.f, 2.f);
    scale = std::clamp(scale, .85f, 1.25f);
    if (requestedHz != 72 && requestedHz != 90 && requestedHz != 120)
        requestedHz = 72;
    auto clamp = [](float &v, float lo, float hi, float fallback) {
        v = std::isfinite(v) ? std::clamp(v, lo, hi) : fallback;
    };
    clamp(opacity, .25f, 1.f, .9f);
    clamp(blur, 0, 1, .5f);
    clamp(brightness, .15f, 1, .65f);
    clamp(environmentDim, 0, 1, 1);
    clamp(handScale, .75f, 1.25f, 1);
    clamp(handPitch, -90, 90, 0);
    theme = Theme(std::clamp(int(theme), 0, 2));
    background = Background(std::clamp(int(background), 0, 7));
    virtualBackground = Background(std::clamp(int(virtualBackground), 0, 7));
    if (virtualBackground == Background::Passthrough)
        virtualBackground = Background::Studio;
    if (background != Background::Passthrough)
        virtualBackground = background;
    handModel = HandModel(std::clamp(int(handModel), 0, 3));
    follow = FollowMode(std::clamp(int(follow), 0, 2));
}
void FrameStats::sample(double ms) {
    if (!std::isfinite(ms) || ms <= 0 || ms > 1000)
        return;
    intervals[next] = ms;
    next = (next + 1) % intervals.size();
    count = std::min(count + 1, intervals.size());
}
double FrameStats::fps() const {
    if (!count)
        return 0;
    return 1000 / (std::accumulate(intervals.begin(), intervals.begin() + count, 0.0) / count);
}
double FrameStats::percentile95() const {
    if (!count)
        return 0;
    auto copy = intervals;
    std::sort(copy.begin(), copy.begin() + count);
    return copy[size_t(std::ceil(count * .95)) - 1];
}
void UiModel::log(std::string value) {
    if (logs.size() >= 80)
        logs.erase(logs.begin());
    logs.push_back(std::move(value));
    invalidate();
}
void UiModel::navigate(Page next) {
    cancelApplicationSliders();
    page = next;
    modalOpen = false;
    widgets_.clear();
    for (auto &p : pointers_) {
        p.capture = 0;
        p.hover = 0;
    }
    hover_ = 0;
    log("Navigation: " + std::to_string(int(next) + 1));
}
void UiModel::setToast(std::string value) {
    toast_ = std::move(value);
    toastUntil_ = time_ + 3;
    invalidate();
}
void UiModel::setLanguage(std::string_view language) {
    cancelApplicationSliders();
    settings.language = resolveLocale(language).code;
    saveRequested = true;
    toast_.clear();
    widgets_.clear();
    for (auto &pointer : pointers_) {
        pointer.capture = 0;
        pointer.hover = 0;
    }
    hover_ = 0;
    invalidate();
}
void UiModel::tick(double seconds) {
    time_ = seconds;
    if (applicationTick)
        applicationTick(seconds);
    if (!toast_.empty() && time_ > toastUntil_) {
        toast_.clear();
        invalidate();
    }
}
void UiModel::addWidget(int id, Rect rect, Control type, bool enabled) {
    widgets_.push_back({id, rect, type, enabled});
}
void UiModel::setInterfaceHidden(bool hidden) {
    if (interfaceHidden == hidden) return;
    cancelApplicationSliders();
    interfaceHidden = hidden;
    widgets_.clear();
    pointers_ = {};
    hover_ = 0;
    hapticRequested[0] = hapticRequested[1] = false;
    if (!hidden) windowState = WindowState::Open;
    invalidate();
}
bool UiModel::revealInterface() {
    if (!interfaceHidden) return false;
    setInterfaceHidden(false);
    return true;
}
void UiModel::pointer(size_t index, Vec2 pos, bool pressed) {
    if (index >= pointers_.size())
        return;
    auto &pointer = pointers_[index];
    if (interfaceHidden) {
        if (pressed && !pointer.down) {
            revealInterface();
            // Consume the whole waking gesture, including its later release.
            pointer.down = true;
            pointer.pos = pos;
        } else pointer.down = pressed;
        return;
    }
    int hit = 0;
    for (const auto &widget : widgets_)
        if (widget.enabled && widget.rect.contains(pos))
            hit = widget.id;
    pointer.hover = hit;
    int activeHover = pointers_[0].hover ? pointers_[0].hover : pointers_[1].hover;
    if (hover_ != activeHover) {
        hover_ = activeHover;
        invalidate();
    }
    const bool began = pressed && !pointer.down;
    if (began) {
        pointer.capture = hit;
        if (hit)
            invalidate(); // Pressed state.
    }
    if (pressed && pointer.capture && !(pos.x == -100 && pos.y == -100))
        for (const auto &w : widgets_)
            if (w.id == pointer.capture && w.enabled && w.type == Control::Slider && (began || pos.x != pointer.pos.x))
                slide(w.id, (pos.x - w.rect.x) / w.rect.w, began ? SliderPhase::Begin : SliderPhase::Preview);
    if (!pressed && pointer.down) {
        bool sliderHandled = false;
        if (pointer.capture && handleApplicationSlider) {
            auto dragged = std::find_if(widgets_.begin(), widgets_.end(),
                [&](const Widget& w){return w.id == pointer.capture && w.type == Control::Slider;});
            const bool cancelled = dragged == widgets_.end() || !dragged->enabled ||
                                   (pos.x == -100 && pos.y == -100);
            const float fraction = cancelled ? 0.f : std::clamp((pos.x-dragged->rect.x)/dragged->rect.w,0.f,1.f);
            sliderHandled = handleApplicationSlider(pointer.capture, fraction,
                cancelled ? SliderPhase::Cancel : SliderPhase::Commit);
        }
        if (!sliderHandled && pointer.capture && pointer.capture == hit) {
            action(hit);
            hapticRequested[index] = settings.haptics;
        }
        if (pointer.capture)
            invalidate();
        pointer.capture = 0;
        requestSliderSave();
    }
    pointer.down = pressed;
    pointer.pos = pos;
}
void UiModel::slide(int id, float value, SliderPhase phase) {
    if (presentation == Presentation::Immersive && settings.background == Background::Passthrough &&
        (id == 351 || id == 352))
        return;
    value = std::clamp(value, 0.f, 1.f);
    if (handleApplicationSlider && handleApplicationSlider(id, value, phase)) return;
    if (id == 210)
        exampleValue_ = value;
    else if (id == 310) {
        settings.distance = 1 + value;
        recenterRequested = true;
        sliderUnsaved_ = true;
    } else if (id == 311) {
        settings.scale = .85f + .4f * value;
        recenterRequested = true;
        sliderUnsaved_ = true;
    } else if (id >= 350 && id <= 354) {
        if (id == 350)
            settings.opacity = .25f + value * .75f;
        if (id == 351)
            settings.blur = value;
        if (id == 352)
            settings.brightness = .15f + value * .85f;
        if (id == 353)
            settings.handScale = .75f + value * .5f;
        if (id == 354)
            settings.handPitch = -90 + value * 180;
        sliderUnsaved_ = true;
    }
    invalidate();
}
void UiModel::requestSliderSave() {
    // Hosts write preferences when polling this flag, so a drag saves once, not every frame.
    saveRequested = saveRequested || sliderUnsaved_;
    sliderUnsaved_ = false;
}
void UiModel::cancelApplicationSliders() {
    requestSliderSave();
    if (handleApplicationSlider)
        for (const auto& pointer : pointers_)
            if (pointer.capture)
                handleApplicationSlider(pointer.capture, 0.f, SliderPhase::Cancel);
}
bool UiModel::setAugmentedReality(bool enabled) {
    if (presentation != Presentation::Immersive)
        return false;
    if (enabled && !passthroughAvailable) {
        notify("toast.passthrough_unavailable");
        return false;
    }
    if (enabled == (settings.background == Background::Passthrough))
        return true;
    if (enabled) {
        settings.virtualBackground = settings.background;
        settings.background = Background::Passthrough;
    } else {
        settings.background = settings.virtualBackground;
        if (settings.background == Background::Passthrough)
            settings.background = Background::Studio;
    }
    saveRequested = true;
    invalidate();
    return true;
}
void UiModel::action(int id) {
    if (handleApplicationAction && handleApplicationAction(id))
        return;
    if (id >= 1 && id <= 4) {
        navigate(Page(id - 1));
        return;
    }
    if (id >= 430 && id <= 435 && presentation == Presentation::Immersive) {
        constexpr Background choices[]{Background::Lounge, Background::Observatory, Background::Terrace,
                                       Background::Studio, Background::Night,       Background::Passthrough};
        if (choices[id - 430] == Background::Passthrough) {
            setAugmentedReality(true);
            return;
        }
        settings.background = settings.virtualBackground = choices[id - 430];
        saveRequested = true;
        invalidate();
        return;
    }
    if ((id >= 340 && id <= 342) || (id >= 360 && id <= 362) || (id >= 370 && id <= 372) ||
        (id >= 380 && id <= 383) || (id >= 390 && id <= 392)) {
        if (id >= 340 && id <= 342)
            settingsTab = id - 340;
        if (id >= 360 && id <= 362)
            settings.theme = Theme(id - 360);
        if (id >= 370 && id <= 372) {
            if (id == 372) {
                setAugmentedReality(true);
                return;
            }
            settings.background = settings.virtualBackground = Background(id - 370);
        }
        if (id >= 380 && id <= 383)
            settings.handModel = HandModel(id - 380);
        if (id >= 390 && id <= 392) {
            settings.follow = FollowMode(id - 390);
        }
        saveRequested = true;
        invalidate();
        return;
    }
    switch (id) {
    case 412:
        setAugmentedReality(settings.background != Background::Passthrough);
        break;
    case 411:
        if (presentation == Presentation::Immersive)
            panelRequested = true;
        else if (immersiveAvailable)
            immersiveRequested = true;
        break;
    case 410: {
        const auto *current = &resolveLocale(settings.language);
        size_t next = (size_t(current - availableLocales()) + 1) % localeCount();
        setLanguage(availableLocales()[next].code);
        break;
    }
    case 610:
        settings.follow = FollowMode((int(settings.follow) + 1) % 3);
        saveRequested = true;
        invalidate();
        break;
    case 611:
        windowState = windowState == WindowState::Open ? WindowState::Minimized : WindowState::Open;
        modalOpen = false;
        widgets_.clear();
        invalidate();
        break;
    case 612:
        windowState = WindowState::Closed;
        debugVisible = false;
        navigate(Page::Home);
        break;
    case 613:
        windowState = WindowState::Open;
        widgets_.clear();
        invalidate();
        break;
    case 384:
        settings.rays = !settings.rays;
        saveRequested = true;
        invalidate();
        break;
    case 100:
        navigate(Page::Components);
        break;
    case 101:
        navigate(Page::Settings);
        break;
    case 102:
        recenterRequested = true;
        notify("toast.recentered");
        break;
    case 103:
        debugVisible = !debugVisible;
        log(debugVisible ? "HUD opened" : "HUD closed");
        break;
    case 104:
        modalOpen = true;
        widgets_.clear();
        invalidate();
        break;
    case 201:
        modalOpen = true;
        widgets_.clear();
        invalidate();
        break;
    case 202:
        notify("toast.secondary");
        log("Secondary action");
        break;
    case 203:
        exampleToggle_ = !exampleToggle_;
        invalidate();
        break;
    case 204:
    case 205:
    case 206:
        exampleChoice_ = id - 204;
        invalidate();
        break;
    case 312:
        settings.highContrast = !settings.highContrast;
        saveRequested = true;
        invalidate();
        break;
    case 313:
        settings.reducedMotion = !settings.reducedMotion;
        saveRequested = true;
        invalidate();
        break;
    case 314:
        settings.haptics = !settings.haptics;
        saveRequested = true;
        invalidate();
        break;
    case 320:
    case 321:
    case 322:
        settings.requestedHz = float(id == 320 ? 72 : id == 321 ? 90 : 120);
        saveRequested = true;
        invalidate();
        break;
    case 330: {
        auto language = settings.language;
        settings = Settings{};
        settings.language = language;
        saveRequested = true;
        recenterRequested = true;
        notify("toast.defaults");
        break;
    }
    case 401:
        exportRequested = true;
        break;
    case 402:
        logs.clear();
        log("Log cleared");
        break;
    case 501:
        modalOpen = false;
        notify("toast.confirmed");
        log("Confirmation accepted");
        break;
    case 502:
        modalOpen = false;
        invalidate();
        break;
    default:
        break;
    }
}
static std::string escapeJson(const std::string &value) {
    std::string out;
    for (unsigned char c : value) {
        if (c == '"' || c == '\\') {
            out += '\\';
            out += char(c);
        } else if (c == '\n')
            out += "\\n";
        else if (c >= 32)
            out += char(c);
    }
    return out;
}
std::string UiModel::diagnosticJson() const {
    std::ostringstream out;
    out << "{\n  \"schema\": \"cyvr.diagnostic.v1\",\n  \"language\": \"" << escapeJson(settings.language)
        << "\",\n  \"runtime\": \"" << escapeJson(runtime) << "\",\n  \"renderer\": \"Vulkan\",\n  \"fps\": "
        << (stats.count ? std::to_string(stats.fps()) : "null")
        << ",\n  \"frameP95Ms\": " << (stats.count ? std::to_string(stats.percentile95()) : "null")
        << ",\n  \"frameWorkMs\": " << stats.cpuMs
        << ",\n  \"displayHz\": " << (stats.displayHz ? std::to_string(stats.displayHz) : "null")
        << ",\n  \"rssMiB\": " << stats.rssMiB
        << ",\n  \"gpuMs\": null,\n  \"distanceM\": " << settings.distance
        << ",\n  \"scale\": " << settings.scale << ",\n  \"theme\": " << int(settings.theme)
        << ",\n  \"background\": " << int(settings.background) << ",\n  \"opacity\": " << settings.opacity
        << ",\n  \"blur\": " << settings.blur << ",\n  \"followMode\": " << int(settings.follow)
        << ",\n  \"windowState\": " << int(windowState) << ",\n  \"handModel\": " << int(settings.handModel)
        << ",\n  \"passthroughAvailable\": " << (passthroughAvailable ? "true" : "false")
        << ",\n  \"passthroughActive\": " << (passthroughActive ? "true" : "false")
        << ",\n  \"virtualBackground\": " << int(settings.virtualBackground) << ",\n  \"events\": [";
    for (size_t i = 0; i < logs.size(); i++) {
        if (i)
            out << ',';
        out << "\n    \"" << escapeJson(logs[i]) << '"';
    }
    out << "\n  ]\n}\n";
    return out.str();
}
} // namespace cyvr
