// SPDX-License-Identifier: MIT
#include "cyvr/layout.hpp"
#include "cyvr/icons.hpp"
#include <set>
#include <sstream>
#include <stdexcept>

namespace cyvr {
namespace {
const LayoutElement *find(const LayoutScreen &s, std::string_view id) {
    for (auto &e : s.elements)
        if (e.id == id)
            return &e;
    return nullptr;
}
struct Transform {
    float x{}, y{}, sx{1}, sy{1};
};
Transform parentTransform(const LayoutScreen &s, const LayoutElement &e, int depth = 0) {
    if (e.parent.empty())
        return {};
    if (depth >= 32)
        throw std::runtime_error("Layout hierarchy is too deep");
    auto *p = find(s, e.parent);
    if (!p || p->type != "group")
        throw std::runtime_error("Missing layout group");
    auto t = parentTransform(s, *p, depth + 1);
    return {t.x + p->bounds.x * t.sx, t.y + p->bounds.y * t.sy, t.sx * p->bounds.w / p->viewBox.x,
            t.sy * p->bounds.h / p->viewBox.y};
}
bool inside(Rect a, Rect b) {
    return a.x >= b.x - 1 && a.y >= b.y - 1 && a.x + a.w <= b.x + b.w + 1 && a.y + a.h <= b.y + b.h + 1;
}
std::string uniqueId(const LayoutScreen &s, std::string base) {
    for (int n = 1;; n++) {
        auto id = base + "-" + std::to_string(n);
        if (!find(s, id))
            return id;
    }
}
const char *kindName(PaintKind kind) {
    static const char *names[]{"rect", "border", "circle", "ring",    "line", "path", "triangle",
                               "text", "image",  "video",  "overlay", "icon", "logo"};
    return names[int(kind)];
}
bool isDescendant(const LayoutScreen &s, const LayoutElement &e, std::string_view ancestor) {
    const LayoutElement *p = &e;
    for (int n = 0; n < 32 && !p->parent.empty(); n++) {
        if (p->parent == ancestor)
            return true;
        p = find(s, p->parent);
        if (!p)
            break;
    }
    return false;
}
} // namespace
Rect layoutBounds(const LayoutScreen &s, const LayoutElement &e) {
    auto t = parentTransform(s, e);
    return {t.x + e.bounds.x * t.sx, t.y + e.bounds.y * t.sy, e.bounds.w * t.sx, e.bounds.h * t.sy};
}
bool layoutVisible(const LayoutScreen &s, const LayoutElement &e) {
    auto *p = &e;
    for (int depth = 0; depth < 33; depth++) {
        if (!p->visible)
            return false;
        if (p->parent.empty())
            return true;
        p = find(s, p->parent);
        if (!p)
            return false;
    }
    return false;
}
LayoutScreen captureLayoutScreen(UiModel &model, std::string id, std::string name) {
    LayoutScreen s;
    s.id = std::move(id);
    s.name = std::move(name);
    s.width = model.viewportSize.x;
    s.height = model.viewportSize.y;
    std::vector<PaintCommand> commands;
    DrawList d;
    d.capture = &commands;
    model.drawMain(d);
    for (auto &w : model.widgets()) {
        LayoutElement e;
        e.id = "control-" + std::to_string(w.id);
        e.name = e.id;
        e.type = "group";
        e.bounds = w.rect;
        e.viewBox = {w.rect.w, w.rect.h};
        e.action = w.id;
        e.control = w.type;
        e.enabled = w.enabled;
        s.elements.push_back(std::move(e));
    }
    int n = 0;
    for (auto &c : commands) {
        LayoutElement e;
        e.id = "element-" + std::to_string(++n);
        e.type = kindName(c.kind);
        e.bounds = c.bounds;
        e.color = c.color;
        e.radius = c.radius;
        e.stroke = c.stroke;
        e.fontSize = c.fontSize;
        e.text = c.text;
        e.icon = c.icon;
        e.uv = c.uv;
        e.closed = c.closed;
        e.name = e.text.empty() || e.text.size() > 256 ? e.type : e.text;
        if (c.kind == PaintKind::Text) {
            size_t start = 0, lines = 0;
            float width = 0;
            do {
                auto end = std::min(c.text.find('\n', start), c.text.size());
                width = std::max(
                    width, d.textWidth(c.fontSize, std::string_view(c.text).substr(start, end - start)));
                lines++;
                start = end + 1;
            } while (start <= c.text.size());
            e.bounds.w = std::max(1.f, width);
            e.bounds.h = c.fontSize * (1.5f * float(lines - 1) + 1.25f);
        }
        if (!c.points.empty()) {
            float x = c.points[0].x, y = c.points[0].y, right = x, bottom = y;
            for (auto p : c.points) {
                x = std::min(x, p.x);
                y = std::min(y, p.y);
                right = std::max(right, p.x);
                bottom = std::max(bottom, p.y);
            }
            e.bounds = {x, y, std::max(1.f, right - x), std::max(1.f, bottom - y)};
            for (auto p : c.points)
                e.points.push_back({(p.x - x) / e.bounds.w, (p.y - y) / e.bounds.h});
        }
        e.bounds.w = std::max(1.f, e.bounds.w);
        e.bounds.h = std::max(1.f, e.bounds.h);
        LayoutElement *group = nullptr;
        for (auto &g : s.elements)
            if (g.type == "group" && inside(e.bounds, g.bounds) &&
                (!group || g.bounds.w * g.bounds.h < group->bounds.w * group->bounds.h))
                group = &g;
        if (group) {
            e.parent = group->id;
            e.bounds.x -= group->bounds.x;
            e.bounds.y -= group->bounds.y;
            if (!e.text.empty() && group->name == group->id)
                group->name = e.text;
        }
        if (n == 1) {
            e.name = "Window surface";
            e.locked = true;
        }
        s.elements.push_back(std::move(e));
    }
    return s;
}
LayoutDocument referenceLayout(std::string language, Theme theme) {
    LayoutDocument doc;
    doc.name = "CyVR UI / 2D reference";
    doc.language = resolveLocale(language).code;
    UiModel model;
    model.settings.language = doc.language;
    model.settings.theme = theme;
    model.presentation = Presentation::Panel2D;
    model.immersiveAvailable = true;
    model.runtime = "Reference preview";
    struct Sample {
        const char *id, *name;
        Page page;
        int tab;
        bool modal;
    };
    const Sample samples[]{{"home", "Home", Page::Home, 0, false},
                           {"components", "Components", Page::Components, 0, false},
                           {"accessibility", "Settings / Accessibility", Page::Settings, 0, false},
                           {"appearance", "Settings / Appearance", Page::Settings, 1, false},
                           {"diagnostics", "Diagnostics", Page::Diagnostics, 0, false},
                           {"dialog", "Confirmation dialog", Page::Home, 0, true}};
    for (auto &sample : samples) {
        model.page = sample.page;
        model.settingsTab = sample.tab;
        model.modalOpen = sample.modal;
        doc.screens.push_back(captureLayoutScreen(model, sample.id, sample.name));
    }
    validateLayout(doc);
    return doc;
}
void drawLayout(const LayoutScreen &s, DrawList &d, UiModel *input, bool schematic,
                std::string_view language) {
    const auto accent = Color::hex(0x86e7cf), muted = Color::hex(0x8eabb7);
    for (auto &e : s.elements) {
        if (!layoutVisible(s, e))
            continue;
        Rect r = layoutBounds(s, e);
        auto t = parentTransform(s, e);
        float scale = std::min(t.sx, t.sy), stroke = e.stroke * scale;
        if (e.type == "group") {
            if (input && e.action)
                input->applicationWidget(e.action, r, e.control, e.enabled);
            continue;
        }
        if (schematic && (e.type == "line" || e.type == "path" || e.type == "triangle" || e.type == "logo"))
            continue;
        auto c = e.color;
        if (schematic)
            c = e.type == "text" ? Color::hex(0xd5e6ed) : muted;
        if (e.type == "rect") {
            if (schematic) {
                d.rect(r, Color::hex(0x182a35), std::min(e.radius, 6.f));
                d.border(r, muted, 4, 1);
            } else
                d.rect(r, c, e.radius * scale);
        } else if (e.type == "border") {
            if (!schematic)
                d.border(r, c, e.radius * scale, stroke);
        } else if (e.type == "text") {
            const auto value =
                e.localizationKey.empty() ? e.text : std::string(translate(language, e.localizationKey));
            d.text(r.x, r.y, e.fontSize * scale, value, c);
        } else if (e.type == "circle")
            d.circle({r.x + r.w / 2, r.y + r.h / 2}, std::min(r.w, r.h) / 2, c);
        else if (e.type == "ring")
            d.ring({r.x + r.w / 2, r.y + r.h / 2}, std::min(r.w, r.h) / 2, stroke, c);
        else if (e.type == "icon")
            drawIcon(d, Icon(e.icon), {r.x + r.w / 2, r.y + r.h / 2}, std::min(r.w, r.h),
                     schematic ? accent : c, e.stroke);
        else if (e.type == "logo")
            drawLogo(d, {r.x + 15 * t.sx, r.y + 15 * t.sy}, c);
        else if (e.type == "image" || e.type == "video" || e.type == "overlay") {
            d.rect(r, Color::hex(0x223b47), e.radius);
            d.border(r, muted, 0, 1);
            d.line({r.x, r.y}, {r.x + r.w, r.y + r.h}, muted, 1);
            d.line({r.x + r.w, r.y}, {r.x, r.y + r.h}, muted, 1);
        } else {
            std::vector<Vec2> points;
            for (auto p : e.points)
                points.push_back({r.x + p.x * r.w, r.y + p.y * r.h});
            if (e.type == "triangle" && points.size() == 3)
                d.triangle(points[0], points[1], points[2], c);
            else if (e.type == "line" && points.size() == 2)
                d.line(points[0], points[1], c, stroke);
            else if (e.type == "path")
                d.polyline(points.data(), points.size(), c, stroke, e.closed);
        }
        if (d.vertices.size() > 170000)
            throw std::runtime_error("Layout exceeds the rendering budget");
    }
}
std::string layoutOutline(const LayoutDocument &doc) {
    std::ostringstream out;
    out << "# " << doc.name << "\n\nLanguage: " << doc.language
        << "\nCoordinates: logical pixels, children relative to group viewBox.\n\n";
    for (auto &s : doc.screens) {
        out << "## " << s.id << " / " << s.name << " (" << s.width << " x " << s.height << ")\n\n";
        for (auto &e : s.elements) {
            if (e.type != "group" && e.type != "text" && e.type != "icon")
                continue;
            auto r = layoutBounds(s, e);
            out << "- " << e.id << " [" << e.type << "] " << e.name << " | x=" << r.x << " y=" << r.y
                << " w=" << r.w << " h=" << r.h;
            if (!e.parent.empty())
                out << " | parent=" << e.parent;
            if (e.action)
                out << " | action=" << e.action;
            out << "\n";
        }
        out << "\n";
    }
    return out.str();
}
LayoutElement *LayoutEditor::selected() {
    for (auto &e : screen().elements)
        if (e.id == selection)
            return &e;
    return nullptr;
}
void LayoutEditor::checkpoint() {
    undo_.push_back({document, screenIndex, selection});
    if (undo_.size() > 48)
        undo_.erase(undo_.begin());
    redo_.clear();
}
bool LayoutEditor::undo() {
    if (undo_.empty())
        return false;
    redo_.push_back({document, screenIndex, selection});
    auto state = std::move(undo_.back());
    undo_.pop_back();
    document = std::move(state.document);
    screenIndex = state.screen;
    selection = std::move(state.selection);
    return true;
}
bool LayoutEditor::redo() {
    if (redo_.empty())
        return false;
    undo_.push_back({document, screenIndex, selection});
    auto state = std::move(redo_.back());
    redo_.pop_back();
    document = std::move(state.document);
    screenIndex = state.screen;
    selection = std::move(state.selection);
    return true;
}
void LayoutEditor::reset(LayoutDocument next) {
    validateLayout(next);
    document = std::move(next);
    screenIndex = 0;
    selection.clear();
    undo_.clear();
    redo_.clear();
}
std::string LayoutEditor::pick(Vec2 point, bool individual) const {
    for (auto it = screen().elements.rbegin(); it != screen().elements.rend(); ++it) {
        auto &e = *it;
        if (e.locked || e.type == "group" || !layoutVisible(screen(), e) ||
            !layoutBounds(screen(), e).contains(point))
            continue;
        if (!individual && !e.parent.empty()) {
            auto *p = find(screen(), e.parent);
            if (p && !p->locked)
                return p->id;
        }
        return e.id;
    }
    for (auto it = screen().elements.rbegin(); it != screen().elements.rend(); ++it)
        if (it->type == "group" && !it->locked && layoutVisible(screen(), *it) &&
            layoutBounds(screen(), *it).contains(point))
            return it->id;
    return {};
}
void LayoutEditor::moveSelected(float dx, float dy, bool snap) {
    if (auto *e = selected(); e && !e->locked) {
        auto t = parentTransform(screen(), *e);
        e->bounds.x = std::clamp(e->bounds.x + dx / t.sx, -32768.f, 32768.f);
        e->bounds.y = std::clamp(e->bounds.y + dy / t.sy, -32768.f, 32768.f);
        if (snap) {
            e->bounds.x = std::round(e->bounds.x / 8) * 8;
            e->bounds.y = std::round(e->bounds.y / 8) * 8;
        }
    }
}
void LayoutEditor::resizeSelected(float w, float h, bool snap) {
    if (auto *e = selected(); e && !e->locked) {
        if (snap) {
            w = std::round(w / 8) * 8;
            h = std::round(h / 8) * 8;
        }
        e->bounds.w = std::clamp(w, 1.f, 8192.f);
        e->bounds.h = std::clamp(h, 1.f, 8192.f);
    }
}
void LayoutEditor::removeSelected() {
    auto *e = selected();
    if (!e || e->locked)
        return;
    std::set<std::string> remove{selection};
    for (auto &n : screen().elements)
        if (isDescendant(screen(), n, selection))
            remove.insert(n.id);
    auto &nodes = screen().elements;
    nodes.erase(std::remove_if(nodes.begin(), nodes.end(), [&](auto &n) { return remove.count(n.id) > 0; }),
                nodes.end());
    selection.clear();
}
void LayoutEditor::duplicateSelected() {
    auto *selectedNode = selected();
    if (!selectedNode || selectedNode->locked)
        return;
    std::string old = selection, parent = selectedNode->parent;
    std::vector<LayoutElement> copies;
    for (auto &e : screen().elements)
        if (e.id == old || isDescendant(screen(), e, old))
            copies.push_back(e);
    std::vector<std::pair<std::string, std::string>> ids;
    for (auto &copy : copies) {
        auto before = copy.id;
        copy.id = uniqueId(screen(), "copy-" + before);
        ids.push_back({before, copy.id});
    }
    for (auto &copy : copies) {
        for (auto &pair : ids)
            if (copy.parent == pair.first) {
                copy.parent = pair.second;
                break;
            }
        if (copy.parent == parent) {
            copy.bounds.x += 24;
            copy.bounds.y += 24;
            copy.action = 0;
        }
    }
    for (auto &pair : ids)
        if (pair.first == old)
            selection = pair.second;
    auto &nodes = screen().elements;
    nodes.insert(nodes.end(), copies.begin(), copies.end());
}
void LayoutEditor::add(std::string_view type) {
    LayoutElement e;
    e.id = uniqueId(screen(), std::string(type));
    e.name = std::string(type);
    e.type = std::string(type);
    e.bounds = {screen().width * .3f, screen().height * .4f, 240, 64};
    e.color = Color::hex(0x86e7cf);
    e.radius = 12;
    selection = e.id;
    if (type == "text") {
        e.text = "New text";
        e.fontSize = 28;
        e.color = Color::hex(0xf0f5f6);
    } else if (type == "button") {
        e.type = "group";
        e.viewBox = {240, 64};
        LayoutElement fill;
        fill.id = uniqueId(screen(), "button-fill");
        fill.parent = e.id;
        fill.bounds = {0, 0, 240, 64};
        fill.color = e.color;
        fill.radius = 12;
        LayoutElement label;
        label.id = uniqueId(screen(), "button-label");
        label.parent = e.id;
        label.type = "text";
        label.bounds = {28, 12, 185, 40};
        label.text = "Button";
        label.fontSize = 26;
        label.color = Color::hex(0x102d28);
        screen().elements.push_back(e);
        screen().elements.push_back(fill);
        screen().elements.push_back(label);
        return;
    } else if (type == "icon") {
        e.bounds.w = e.bounds.h = 48;
        e.icon = int(Icon::Headset);
    }
    screen().elements.push_back(std::move(e));
}
void LayoutEditor::reorderSelected(int direction) {
    if (!selected() || selected()->locked)
        return;
    auto &nodes = screen().elements;
    std::vector<LayoutElement> moving, remaining;
    for (auto &e : nodes)
        (e.id == selection || isDescendant(screen(), e, selection) ? moving : remaining).push_back(e);
    if (direction > 0)
        remaining.insert(remaining.end(), moving.begin(), moving.end());
    else {
        auto at = std::find_if(remaining.begin(), remaining.end(),
                               [](auto &e) { return e.locked && e.type == "rect"; });
        if (at != remaining.end())
            ++at;
        else
            at = remaining.begin();
        remaining.insert(at, moving.begin(), moving.end());
    }
    nodes = std::move(remaining);
}
} // namespace cyvr
