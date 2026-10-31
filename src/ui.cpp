// SPDX-License-Identifier: MIT
#include "cyvr/core.hpp"
#include "cyvr/icons.hpp"
#include <cstdio>

namespace cyvr {
namespace {
thread_local Color mint = Color::hex(0x86e7cf), ink = Color::hex(0x102d28), white = Color::hex(0xf0f5f6),
                   muted = Color::hex(0xa7b7bf), lineColor = Color::hex(0x354851),
                   warning = Color::hex(0xf2c683);
thread_local Color panelColor, surfaceColor, hoverColor, selectedColor;
void palette(const Settings &s) {
    bool light = s.theme == Theme::Light, ocean = s.theme == Theme::Ocean;
    mint = Color::hex(light ? 0x126854 : ocean ? 0x95bdff : 0x86e7cf);
    ink = Color::hex(light ? 0xffffff : 0x102d28);
    white = Color::hex(light ? 0x142b35 : 0xf0f5f6);
    muted = Color::hex(light ? 0x48616b : 0xa7b7bf);
    lineColor = Color::hex(light ? 0xb0c3c9 : 0x354851, .5f);
    warning = Color::hex(light ? 0x865014 : 0xf2c683);
    panelColor = Color::hex(light ? 0xe4eef1 : ocean ? 0x141f3b : 0x182228, s.highContrast ? 1 : s.opacity);
    surfaceColor = Color::hex(light ? 0xc1d3db : ocean ? 0x293e66 : 0x2f414b, .45f);
    hoverColor = Color::hex(light ? 0xacd6ce : ocean ? 0x385984 : 0x3a505a);
    selectedColor = Color::hex(light ? 0xc0e5dc : ocean ? 0x294c70 : 0x26423d);
}
Color mix(Color a, Color b, float t) {
    return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t};
}
Color alpha(Color c, float a) {
    c.a = a;
    return c;
}
void frame(DrawList &d, Rect r, Color fill, float radius = 18) {
    d.rect(r, fill, radius);
    d.border(r, lineColor, radius);
}
void label(DrawList &d, float x, float y, const char *value) {
    d.text(x, y, 18, value, mint);
}
// DrawList::text places the top of a line; this centres capitals on a row.
float textTop(float centerY, float size) {
    return centerY - size * .66f;
}
float fitSize(const DrawList &d, float size, float maxWidth, std::string_view value) {
    float width = d.textWidth(size, value);
    return width > maxWidth ? std::max(size * maxWidth / width, size * .7f) : size;
}
void textRight(DrawList &d, float right, float y, float size, std::string_view value, Color c) {
    d.text(right - d.textWidth(size, value), y, size, value, c);
}
std::string elide(const DrawList &d, float size, float maxWidth, std::string value) {
    if (d.textWidth(size, value) <= maxWidth)
        return value;
    maxWidth -= d.textWidth(size, "...");
    while (!value.empty() && d.textWidth(size, value) > maxWidth) {
        while (!value.empty() && (uint8_t(value.back()) & 0xc0) == 0x80)
            value.pop_back();
        if (!value.empty())
            value.pop_back();
    }
    return value + "...";
}
std::string wrap(const DrawList &d, float size, float maxWidth, std::string_view value) {
    std::string out, row;
    size_t start = 0;
    while (start <= value.size()) {
        size_t end = std::min(value.find_first_of(" \n", start), value.size());
        std::string_view word = value.substr(start, end - start);
        std::string candidate = row.empty() ? std::string(word) : row + " " + std::string(word);
        if (!row.empty() && d.textWidth(size, candidate) > maxWidth) {
            out += row + "\n";
            row = std::string(word);
        } else
            row = candidate;
        if (end == value.size() || value[end] == '\n') {
            out += row;
            row.clear();
            if (end != value.size())
                out += "\n";
        }
        start = end + 1;
    }
    return out;
}
// Every icon shares one 24-unit grid and stroke, so only size and color vary between surfaces.
void drawIconImpl(DrawList &d, Icon icon, Vec2 center, float size, Color c, float weight = 2) {
    const float s = size / 24, stroke = weight * s;
    auto at = [&](float x, float y) { return Vec2{center.x + (x - 12) * s, center.y + (y - 12) * s}; };
    std::array<Vec2, 40> points;
    auto path = [&](std::initializer_list<Vec2> list, bool closed = false) {
        size_t n = 0;
        for (auto p : list)
            points[n++] = at(p.x, p.y);
        d.polyline(points.data(), n, c, stroke, closed);
    };
    auto arc = [&](float x, float y, float rx, float ry, float from, float to, int segments, bool closed) {
        for (int i = 0; i <= segments; i++) {
            float a = (from + (to - from) * i / segments) * pi / 180;
            points[size_t(i)] = at(x + std::cos(a) * rx, y + std::sin(a) * ry);
        }
        d.polyline(points.data(), size_t(segments) + 1, c, stroke, closed);
    };
    auto box = [&](float x, float y, float w, float h, float r) {
        size_t n = 0;
        const Vec2 corners[]{{x + w - r, y + r}, {x + w - r, y + h - r}, {x + r, y + h - r}, {x + r, y + r}};
        for (int corner = 0; corner < 4; corner++)
            for (int i = 0; i <= 3; i++) {
                float a = (corner - 1 + i / 3.f) * pi * .5f;
                points[n++] = at(corners[corner].x + std::cos(a) * r, corners[corner].y + std::sin(a) * r);
            }
        d.polyline(points.data(), n, c, stroke, true);
    };
    auto ring = [&](float x, float y, float r) { d.ring(at(x, y), r * s, stroke, c); };
    auto dot = [&](float x, float y, float r) { d.circle(at(x, y), r * s, c); };
    switch (icon) {
    case Icon::NewWindow:
        box(2,3,20,18,2);path({{2,8},{22,8}});path({{12,11},{12,18}});path({{8.5f,14.5f},{15.5f,14.5f}});break;
    case Icon::Shield:
        path({{12,2.5f},{21,6},{20,14},{17,19},{12,22},{7,19},{4,14},{3,6}},true);
        path({{8,12},{11,15},{16,9}});break;
    case Icon::Back:
        path({{14.5f,5},{7.5f,12},{14.5f,19}});break;
    case Icon::Forward:
        path({{9.5f,5},{16.5f,12},{9.5f,19}});break;
    case Icon::Reload:
        arc(12,12,8,8,45,315,26,false);path({{18,4},{18,10},{12,10}});break;
    case Icon::Plus:
        path({{12,5},{12,19}});path({{5,12},{19,12}});break;
    case Icon::Bookmark:
        path({{6,3.5f},{18,3.5f},{18,21},{12,17},{6,21}},true);break;
    case Icon::Extensions:
        path({{3,4},{9,4},{9,3},{10,1.5f},{12,1},{14,1.5f},{15,3},{15,4},{21,4},{21,10},
              {20,10},{18.5f,11},{18,13},{18.5f,15},{20,16},{21,16},{21,21},{15,21},{15,20},
              {14,18.5f},{12,18},{10,18.5f},{9,20},{9,21},{3,21},{3,16},{4,16},{5.5f,15},
              {6,13},{5.5f,11},{4,10},{3,10}},true);break;
    case Icon::Search:
        ring(10.5f,10.5f,6.5f);path({{15.5f,15.5f},{21,21}});break;
    case Icon::More:
        dot(5,12,1.5f);dot(12,12,1.5f);dot(19,12,1.5f);break;
    case Icon::Stop:
        box(5,5,14,14,2);break;
    case Icon::ArrowEnter:
        path({{20,5},{20,13},{4,13},{9,8}});path({{4,13},{9,18}});break;
    case Icon::Keyboard:
        box(2,5,20,14,2);path({{6,9},{6,10}});path({{10,9},{10,10}});path({{14,9},{14,10}});path({{18,9},{18,10}});path({{7,15},{17,15}});break;
    case Icon::Trash:
        path({{3,6},{21,6}});path({{9,3},{15,3}});path({{5,6},{6,21},{18,21},{19,6}});path({{10,10},{10,17}});path({{14,10},{14,17}});break;
    case Icon::Home:
        path({{3, 11.5f}, {12, 3.5f}, {21, 11.5f}});
        path({{5.5f, 9.5f}, {5.5f, 20.5f}, {18.5f, 20.5f}, {18.5f, 9.5f}});
        path({{9.5f, 20.5f}, {9.5f, 14}, {14.5f, 14}, {14.5f, 20.5f}});
        break;
    case Icon::Components:
        path({{12, 3}, {21.5f, 8.5f}, {12, 14}, {2.5f, 8.5f}}, true);
        path({{2.5f, 13}, {12, 18.5f}, {21.5f, 13}});
        path({{5.5f, 18.5f}, {12, 22}, {18.5f, 18.5f}});
        break;
    case Icon::Settings:
        path({{2.5f, 6}, {11.5f, 6}});
        ring(15, 6, 3);
        path({{18.5f, 6}, {21.5f, 6}});
        path({{2.5f, 12}, {5, 12}});
        ring(8.5f, 12, 3);
        path({{12, 12}, {21.5f, 12}});
        path({{2.5f, 18}, {12.5f, 18}});
        ring(16, 18, 3);
        path({{19.5f, 18}, {21.5f, 18}});
        break;
    case Icon::Diagnostics:
        path({{2, 12.5f}, {6.5f, 12.5f}, {9.5f, 4.5f}, {14.5f, 19.5f}, {17.5f, 12.5f}, {22, 12.5f}});
        break;
    case Icon::Move:
        path({{12, 2.5f}, {12, 21.5f}});
        path({{2.5f, 12}, {21.5f, 12}});
        path({{9, 5.5f}, {12, 2.5f}, {15, 5.5f}});
        path({{9, 18.5f}, {12, 21.5f}, {15, 18.5f}});
        path({{5.5f, 9}, {2.5f, 12}, {5.5f, 15}});
        path({{18.5f, 9}, {21.5f, 12}, {18.5f, 15}});
        break;
    case Icon::Pin: {
        // A map pin marks the world anchor.
        size_t n = 0;
        for (int i = 0; i <= 12; i++) {
            float a = (140.f + 260.f * i / 12) * pi / 180;
            points[n++] = at(12 + std::cos(a) * 7.5f, 9.5f + std::sin(a) * 7.5f);
        }
        points[n++] = at(12, 21.5f);
        d.polyline(points.data(), n, c, stroke, true);
        ring(12, 9.5f, 2.5f);
        break;
    }
    case Icon::Eye: {
        // Keep the window facing the viewer.
        size_t n = 0;
        for (int i = 0; i <= 24; i++) {
            float t = i / 24.f, a = t * 2 * pi;
            points[n++] = at(12 + std::cos(a) * 10, 12 + std::sin(a) * 6.5f * (1 - std::cos(a) * std::cos(a) * .3f));
        }
        d.polyline(points.data(), n, c, stroke, true);
        ring(12, 12, 2.8f);
        break;
    }
    case Icon::Headset:
        path({{6.5f, 6.5f}, {17.5f, 6.5f}, {20, 7.5f}, {21.5f, 10}, {21.5f, 14}, {20, 16.5f}, {17.5f, 17.5f},
              {15.5f, 17.5f}, {14, 16.5f}, {13.2f, 14.5f}, {12, 14}, {10.8f, 14.5f}, {10, 16.5f}, {8.5f, 17.5f},
              {6.5f, 17.5f}, {4, 16.5f}, {2.5f, 14}, {2.5f, 10}, {4, 7.5f}},
             true);
        break;
    case Icon::Follow:
        // The headset leads and the window follows.
        path({{7, 10}, {17, 10}, {20, 11.5f}, {21, 14}, {20, 17.5f}, {17, 19.5f}, {14.5f, 19.5f}, {13, 17.5f},
              {11, 17.5f}, {9.5f, 19.5f}, {7, 19.5f}, {4, 17.5f}, {3, 14}, {4, 11.5f}},
             true);
        path({{5, 5}, {19, 5}});
        path({{15.5f, 1.5f}, {19, 5}, {15.5f, 8.5f}});
        break;
    case Icon::Minimize:
        path({{5, 17.5f}, {19, 17.5f}});
        break;
    case Icon::Close:
        path({{5.5f, 5.5f}, {18.5f, 18.5f}});
        path({{18.5f, 5.5f}, {5.5f, 18.5f}});
        break;
    case Icon::Restore:
        box(3, 8.5f, 12.5f, 12.5f, 2.5f);
        path({{8.5f, 8.5f}, {8.5f, 5.5f}, {9.5f, 3.8f}, {11, 3}, {18.5f, 3}, {20.2f, 3.8f}, {21, 5.5f}, {21, 13},
              {20.2f, 14.7f}, {18.5f, 15.5f}, {15.5f, 15.5f}});
        break;
    case Icon::Open:
        box(3, 3, 7.5f, 7.5f, 2);
        box(13.5f, 3, 7.5f, 7.5f, 2);
        box(3, 13.5f, 7.5f, 7.5f, 2);
        box(13.5f, 13.5f, 7.5f, 7.5f, 2);
        break;
    case Icon::Camera:
        // Optional real-world AR view.
        path({{2.5f, 18}, {2.5f, 9}, {3.5f, 7.5f}, {5, 6.8f}, {7.5f, 6.8f}, {9.2f, 4}, {14.8f, 4}, {16.5f, 6.8f},
              {19, 6.8f}, {20.5f, 7.5f}, {21.5f, 9}, {21.5f, 18}, {20.5f, 19.5f}, {19, 20.2f}, {5, 20.2f},
              {3.5f, 19.5f}},
             true);
        ring(12, 13, 3.8f);
        break;
    case Icon::Globe:
        ring(12, 12, 9.5f);
        path({{2.5f, 12}, {21.5f, 12}});
        arc(12, 12, 4.3f, 9.5f, 0, 345, 23, true);
        break;
    case Icon::Recenter:
        ring(12, 12, 5.5f);
        dot(12, 12, 1.6f);
        path({{12, 2}, {12, 6.5f}});
        path({{12, 17.5f}, {12, 22}});
        path({{2, 12}, {6.5f, 12}});
        path({{17.5f, 12}, {22, 12}});
        break;
    case Icon::Bars:
        path({{5, 20.5f}, {5, 13}});
        path({{12, 20.5f}, {12, 4}});
        path({{19, 20.5f}, {19, 9.5f}});
        break;
    case Icon::Check:
        path({{4, 12.5f}, {9.8f, 18}, {20, 6.5f}});
        break;
    case Icon::Warning:
        path({{12, 3.5f}, {22, 20.5f}, {2, 20.5f}}, true);
        path({{12, 9.5f}, {12, 14.5f}});
        dot(12, 17.5f, 1.2f);
        break;
    case Icon::Selector:
        path({{7.5f, 9.5f}, {12, 5}, {16.5f, 9.5f}});
        path({{7.5f, 14.5f}, {12, 19}, {16.5f, 14.5f}});
        break;
    }
}
void logo(DrawList &d, Vec2 center, Color c) {
    std::array<Vec2, 17> arc;
    for (size_t i = 0; i < arc.size(); i++) {
        float a = (48.f + 264.f * i / (arc.size() - 1)) * pi / 180;
        arc[i] = {center.x + std::cos(a) * 15, center.y + std::sin(a) * 15};
    }
    d.polyline(arc.data(), arc.size(), c, 3.5f);
    const Vec2 chevron[]{{center.x + 19, center.y - 11}, {center.x + 30, center.y}, {center.x + 19, center.y + 11}};
    d.polyline(chevron, 3, c, 3.5f);
}
void toggleSwitch(DrawList &d, Rect r, bool checked) {
    d.rect(r, checked ? mint : lineColor, r.h * .5f);
    float knob = r.h * .5f - 4;
    d.circle({checked ? r.x + r.w - r.h * .5f : r.x + r.h * .5f, r.y + r.h * .5f}, knob, checked ? ink : muted);
}
} // namespace
void drawIcon(DrawList& d,Icon icon,Vec2 center,float size,Color color,float weight){drawIconImpl(d,icon,center,size,color,weight);}
void drawLogo(DrawList& d,Vec2 center,Color color){logo(d,center,color);}
ThemeColors themeColors(const Settings& settings){palette(settings);return {mint,white,muted,lineColor,panelColor,surfaceColor,hoverColor,selectedColor};}
void UiModel::drawMain(DrawList &d) {
    if (renderApplication && renderApplication(d))
        return;
    d.clear();
    widgets_.clear();
    palette(settings);
    const Color panel = panelColor, surface = surfaceColor;
    const Color sub = settings.highContrast ? white : muted;
    const bool immersive = presentation == Presentation::Immersive;
    const float x = 266, right = 1394, width = right - x;
    std::string tooltip;
    Rect tooltipAnchor{};
    frame(d, {0, 0, 1440, 990}, panel, 30);
    logo(d, {44, 39}, mint);
    d.text(88, textTop(39, 26), 26, "CyVR", white);
    d.text(88 + d.textWidth(26, "CyVR") + 14, textTop(39, 16), 16,
           workspaceLabel.empty() ? tr("header.workspace") : workspaceLabel.c_str(), muted);
    auto iconButton = [&](int id, Rect r, Icon glyph, Color c, const char *hint, bool selected = false,
                          bool enabled = true) {
        if (selected)
            d.rect(r, selectedColor, 14);
        else if (pressed(id))
            d.rect(r, selectedColor, 14);
        else if (hover_ == id)
            d.rect(r, hoverColor, 14);
        drawIcon(d, glyph, {r.x + r.w * .5f, r.y + r.h * .5f}, 30, c);
        if (hover_ == id) {
            tooltip = hint;
            tooltipAnchor = r;
        }
        addWidget(id, r, Control::Button, enabled);
    };
    float headerRight = right;
    if (immersive) {
        const bool selected = settings.background == Background::Passthrough;
        Rect ar{right - 46, 10, 58, 58}; // Icon, not hover plate, sits on the content margin.
        iconButton(412, ar, Icon::Camera, passthroughAvailable ? mint : alpha(muted, .55f), tr("mode.ar"), selected,
                   passthroughAvailable || selected);
        if (selected)
            d.circle({ar.x + 45, ar.y + 13}, 5, passthroughActive ? mint : warning);
        headerRight = ar.x - 4;
    }
    if (immersiveAvailable) {
        Rect mode{headerRight - (immersive ? 58 : 46), 10, 58, 58};
        iconButton(411, mode, immersive ? Icon::Restore : Icon::Headset, mint,
                   tr(immersive ? "mode.panel" : "mode.immersive"));
        headerRight = mode.x - 4;
    }
    if (immersive) {
        Rect handle{566, 8, 296, 61};
        const char *moveLabel = tr("dock.move_window");
        if (pressed(605) || hover_ == 605) {
            d.rect(handle, pressed(605) ? selectedColor : hoverColor, 14);
            float contentWidth = 38 + d.textWidth(20, moveLabel);
            drawIcon(d, Icon::Move, {handle.x + (handle.w - contentWidth) * .5f + 13, 39}, 26, mint);
            d.text(handle.x + (handle.w - contentWidth) * .5f + 38, textTop(39, 20), 20, moveLabel, white);
        } else
            drawIcon(d, Icon::Move, {714, 39}, 26, muted);
        addWidget(605, handle, Control::Drag);
    }
    {
        const char *context = contextLabel.empty() ? tr("header.personal") : contextLabel.c_str();
        float size = fitSize(d, 16, headerRight - 884, context), textX = headerRight - 12 - d.textWidth(size, context);
        d.circle({textX - 14, 39}, 3.5f, mint);
        d.text(textX, textTop(39, size), size, context, muted);
    }
    d.line({0, 78}, {1440, 78}, lineColor, 1);
    d.line({222, 78}, {222, 920}, lineColor, 1);
    label(d, 28, 100, tr("header.space"));
    auto buttonContent = [&](Rect r, std::string_view text, Color c, const Icon *glyph) {
        const float size = fitSize(d, std::min(25.f, r.h * .42f), r.w - (glyph ? 62 : 28), text),
                    textWidth = d.textWidth(size, text), cy = r.y + r.h * .5f;
        float start = r.x + (r.w - textWidth - (glyph ? 34 : 0)) * .5f;
        if (glyph) {
            drawIcon(d, *glyph, {start + 12, cy}, 24, c);
            start += 34;
        }
        d.text(start, textTop(cy, size), size, text, c);
    };
    auto button = [&](int id, Rect r, std::string_view text, bool primary = false, bool enabled = true,
                      const Icon *glyph = nullptr) {
        Color fill = surface, content = white;
        if (primary) {
            fill = pressed(id) ? mix(mint, Color{}, .18f) : hover_ == id ? mix(mint, Color{1, 1, 1, 1}, .16f) : mint;
            content = ink;
        } else if (pressed(id))
            fill = selectedColor;
        else if (hover_ == id)
            fill = hoverColor;
        if (!enabled) {
            fill = alpha(surface, surface.a * .55f);
            content = alpha(muted, .6f);
        }
        frame(d, r, fill, 12);
        buttonContent(r, text, content, glyph);
        addWidget(id, r, Control::Button, enabled);
    };
    // Single-choice controls: selection stays distinct from the primary action.
    auto choice = [&](int id, Rect r, std::string_view text, bool selected, bool enabled = true,
                      const Icon *glyph = nullptr) {
        if (!selected || !enabled) {
            button(id, r, text, false, enabled, glyph);
            return;
        }
        d.rect(r, pressed(id) || hover_ == id ? hoverColor : selectedColor, 12);
        d.border(r, mint, 12, 2);
        buttonContent(r, text, mint, glyph);
        addWidget(id, r);
    };
    for (int i = 0; i < 4; i++) {
        static constexpr Icon glyphs[]{Icon::Home, Icon::Components, Icon::Settings, Icon::Diagnostics};
        Rect r{12, 148.f + i * 76, 198, 64};
        const bool selected = page == Page(i);
        if (selected) {
            d.rect(r, selectedColor, 12);
            d.rect({r.x, r.y + 18, 4, r.h - 36}, mint, 2);
        } else if (pressed(i + 1))
            d.rect(r, hoverColor, 12);
        else if (hover_ == i + 1)
            d.rect(r, surface, 12);
        Color c = selected ? mint : sub;
        drawIcon(d, glyphs[i], {40, r.y + r.h * .5f}, 26, c);
        float size = fitSize(d, 22, 140, pageLabel(i));
        d.text(66, textTop(r.y + r.h * .5f, size), size, pageLabel(i), c);
        addWidget(i + 1, r);
    }
    label(d, 28, 520, tr("language.label"));
    Rect languageButton{12, 552, 198, 58};
    frame(d, languageButton, pressed(410) ? selectedColor : hover_ == 410 ? hoverColor : surface, 12);
    drawIcon(d, Icon::Globe, {40, 581}, 24, sub);
    auto languageName = resolveLocale(settings.language).nativeName;
    float languageSize = fitSize(d, 22, 108, languageName);
    d.text(66, textTop(581, languageSize), languageSize, languageName, white);
    drawIcon(d, Icon::Selector, {189, 581}, 20, muted);
    addWidget(410, languageButton);
    d.line({28, 650}, {194, 650}, lineColor, 1);
    d.text(28, 672, 20, sidebarTitle.empty() ? tr("brand.custom") : sidebarTitle, white);
    d.text(28, 706, 17, sidebarSubtitle.empty() ? tr("brand.open") : sidebarSubtitle, muted);
    d.text(28, 852, 15, immersive ? "C++ / OPENXR" : "C++ / ANDROID", muted);
    d.text(28, 878, 15, "VULKAN / 0.8", muted);
    if (renderContent && renderContent(d)) {
        // Application owns only the content region; chrome, modal and comfort remain shared.
    } else if (page == Page::Home) {
        label(d, x, 112, tr("home.eyebrow"));
        d.text(x, 146, 60, tr("home.title"), white);
        d.text(x, 234, 27, tr("home.subtitle"), sub);
        Rect hero{x, 300, 700, 282};
        frame(d, hero, selectedColor, 20);
        label(d, x + 30, 330, tr("home.lab"));
        d.text(x + 30, 368, 40, tr("home.hero"), white);
        d.text(x + 30, 432, 24, tr("home.hero_help"), muted);
        button(100, {x + 30, 492, 300, 62}, tr("home.explore"), true);
        // A small wire sculpture uses only shared UI geometry, no models or textures.
        for (int ring = 0; ring < 3; ring++)
            for (int j = 0; j < 64; j++) {
                float a = j * pi / 32, b = (j + 1) * pi / 32, rx = 42 + ring * 18.f, ry = 80 + ring * 15.f;
                d.line({x + 590 + std::cos(a) * rx, 441 + std::sin(a) * ry},
                       {x + 590 + std::cos(b) * rx, 441 + std::sin(b) * ry}, alpha(mint, .4f), 1.5f);
            }
        frame(d, {978, 300, 416, 282}, surface, 20);
        drawIcon(d, Icon::Components, {1022, 346}, 32, mint);
        label(d, 1006, 386, tr("home.blocks"));
        d.text(1006, 424, 29, tr("home.foundation"), white);
        d.text(1006, 480, 23, tr("home.blocks_help"), sub);
        label(d, x, 616, tr("home.personalize_label"));
        frame(d, {x, 652, width, 138}, surface, 16);
        drawIcon(d, Icon::Settings, {x + 48, 721}, 36, mint);
        d.text(x + 92, 674, 30, tr("home.balance"), white);
        d.text(x + 92, 724, 23, tr(immersive ? "home.balance_help" : "panel.balance_help"), sub);
        button(101, {1122, 690, 244, 62}, tr("home.personalize"));
        d.text(x, 826, 20, tr(immersive ? "home.dock_help" : "panel.window_help"), muted);
    } else if (page == Page::Components) {
        label(d, x, 112, tr("components.eyebrow"));
        d.text(x, 146, 47, tr("components.title"), white);
        d.text(x, 218, 25, tr("components.help"), sub);
        float column = 550;
        auto cell = [&](float cx, float cy, const char *name, const char *badge) {
            frame(d, {cx, 288 + cy, column, 187}, surface, 16);
            label(d, cx + 23, 307 + cy, name);
            float badgeWidth = d.textWidth(14, badge) + 24, badgeRight = cx + column - 22;
            d.rect({badgeRight - badgeWidth, 306 + cy, badgeWidth, 28}, lineColor, 14);
            d.text(badgeRight - badgeWidth + 12, textTop(320 + cy, 14), 14, badge, sub);
        };
        cell(x, 0, tr("components.actions"), tr("badge.button"));
        button(201, {x + 22, 363, 245, 70}, tr("components.continue"), true);
        button(202, {x + 283, 363, 245, 70}, tr("components.secondary"));
        cell(844, 0, tr("components.preference"), tr("badge.toggle"));
        Rect toggle{866, 363, 506, 70};
        if (pressed(203) || hover_ == 203)
            d.rect(toggle, pressed(203) ? selectedColor : hoverColor, 12);
        d.text(884, textTop(398, 26), 26, tr("components.enable"), white);
        toggleSwitch(d, {1284, 381, 70, 34}, exampleToggle_);
        addWidget(203, toggle);
        cell(x, 210, tr("components.adjustment"), tr("badge.slider"));
        label(d, x + 26, 570, tr("components.value"));
        textRight(d, x + 524, 566, 23, number(exampleValue_ * 100, 0) + " %", white);
        d.rect({x + 27, 637, 496, 7}, lineColor, 3);
        d.rect({x + 27, 637, 496 * exampleValue_, 7}, mint, 3);
        d.circle({x + 27 + 496 * exampleValue_, 640.5f}, pressed(210) || hover_ == 210 ? 16 : 13, white);
        addWidget(210, {x + 27, 610, 496, 62}, Control::Slider);
        cell(844, 210, tr("components.choice"), tr("badge.segments"));
        for (int i = 0; i < 3; i++)
            choice(204 + i, {866.f + i * 171, 587, 164, 66},
                   i == 0   ? tr("components.calm")
                   : i == 1 ? tr("components.balanced")
                            : tr("components.detailed"),
                   exampleChoice_ == i);
        cell(x, 420, tr("components.states"), tr("badge.feedback"));
        Rect available{x + 23, 786, 240, 51}, check{x + 277, 786, 250, 51};
        d.rect(available, selectedColor, 9);
        drawIcon(d, Icon::Check, {available.x + 26, available.y + 25.5f}, 22, mint);
        float stateSize = fitSize(d, 22, 170, tr("components.available"));
        d.text(available.x + 48, textTop(811, stateSize), stateSize, tr("components.available"), mint);
        d.rect(check, Color::hex(settings.theme == Theme::Light ? 0xf4dfc2 : 0x493f30), 9);
        drawIcon(d, Icon::Warning, {check.x + 26, check.y + 25.5f}, 22, warning);
        stateSize = fitSize(d, 22, 180, tr("components.check"));
        d.text(check.x + 48, textTop(811, stateSize), stateSize, tr("components.check"), warning);
        cell(844, 420, tr("components.disabled"), tr("badge.disabled"));
        button(299, {866, 782, 506, 66}, tr("components.unavailable"), false, false);
    } else if (page == Page::Settings) {
        label(d, x, 106, tr("settings.eyebrow"));
        d.text(x, 136, 48, tr("settings.title"), white);
        if (!immersive && settingsTab == 2) settingsTab = 1;
        // Three columns and four columns both end exactly on the content's right margin.
        const auto third = [&](int i, float y, float h) { return Rect{x + i * 380.f, y, 368, h}; };
        const int tabs = immersive ? 3 : 2;
        for (int i = 0; i < tabs; i++)
            choice(340 + i, {x + i * (width + 12) / tabs, 214, (width + 12) / tabs - 12, 60},
                   i == 0   ? tr(immersive ? "settings.placement" : "panel.accessibility")
                   : i == 1 ? tr("settings.appearance")
                            : tr("settings.hands"),
                   settingsTab == i);
        auto slider = [&](int id, float y, const char *title, float fraction, std::string value,
                          const char *help, bool enabled = true) {
            frame(d, {x, y, width, 104}, surface, 16);
            d.text(x + 25, y + 9, 24, title, enabled ? white : muted);
            textRight(d, right - 27, y + 10, 23, value, enabled ? mint : muted);
            d.text(x + 25, y + 44, 18, help, sub);
            const float track = width - 54, knobX = x + 27 + track * fraction;
            d.rect({x + 27, y + 83, track, 6}, lineColor, 3);
            if (enabled)
                d.rect({x + 27, y + 83, track * fraction, 6}, mint, 3);
            d.circle({knobX, y + 86}, !enabled ? 11 : pressed(id) || hover_ == id ? 16 : 13, enabled ? white : muted);
            addWidget(id, {x + 27, y + 62, track, 42}, Control::Slider, enabled);
        };
        auto setting = [&](int id, Rect r, const char *title, bool checked) {
            frame(d, r, pressed(id) ? selectedColor : hover_ == id ? hoverColor : surface, 12);
            float size = fitSize(d, 22, r.w - 110, title);
            d.text(r.x + 20, textTop(r.y + r.h * .5f, size), size, title, white);
            toggleSwitch(d, {r.x + r.w - 78, r.y + r.h * .5f - 16, 58, 32}, checked);
            addWidget(id, r);
        };
        if (!immersive && settingsTab == 0) {
            frame(d, {x, 300, width, 150}, surface, 16);
            drawIcon(d, Icon::Restore, {x + 52, 375}, 40, mint);
            d.text(x + 96, 330, 30, tr("mode.panel"), white);
            d.text(x + 96, 382, 23, tr("panel.window_help"), sub);
            setting(312, {x, 472, 558, 76}, tr("settings.contrast"), settings.highContrast);
            setting(313, {836, 472, 558, 76}, tr("settings.motion"), settings.reducedMotion);
            d.text(x, 582, 23, tr("panel.input_help"), sub);
            button(330, {1024, 810, 370, 60}, tr("settings.restore"));
        } else if (settingsTab == 0) {
            slider(310, 300, tr("settings.distance"), settings.distance - 1,
                   number(settings.distance, 2) + " m", tr("settings.distance_help"));
            slider(311, 416, tr("settings.scale"), (settings.scale - .85f) / .4f,
                   number(settings.scale * 100, 0) + " %", tr("settings.scale_help"));
            setting(312, third(0, 536, 72), tr("settings.contrast"), settings.highContrast);
            setting(313, third(1, 536, 72), tr("settings.motion"), settings.reducedMotion);
            setting(314, third(2, 536, 72), tr("settings.haptics"), settings.haptics);
            label(d, x, 632, tr("settings.anchor"));
            for (int i = 0; i < 3; i++)
                choice(390 + i, third(i, 664, 58),
                       i == 0   ? tr("settings.world")
                       : i == 1 ? tr("settings.face")
                                : tr("settings.follow"),
                       int(settings.follow) == i);
            label(d, x, 746, tr("settings.refresh"));
            for (int i = 0; i < 3; i++) {
                int hz = i == 0 ? 72 : i == 1 ? 90 : 120;
                bool available =
                    std::find(refreshRates.begin(), refreshRates.end(), float(hz)) != refreshRates.end();
                choice(320 + i, {x + i * 192, 778, 176, 60}, std::to_string(hz) + " Hz",
                       settings.requestedHz == hz, available);
            }
            button(330, third(2, 778, 60), tr("settings.restore"));
        } else if (settingsTab == 1) {
            label(d, x, 292, tr("appearance.theme"));
            for (int i = 0; i < 3; i++)
                choice(360 + i, third(i, 322, 54),
                       i == 0   ? tr("appearance.graphite")
                       : i == 1 ? tr("appearance.light")
                                : tr("appearance.ocean"),
                       int(settings.theme) == i);
            label(d, x, 390, tr(immersive ? "appearance.environment" : "panel.decor"));
            if (immersive) {
                textRight(d, right, 392, 17,
                          tr(!passthroughAvailable                            ? "ar.unavailable"
                             : settings.background == Background::Passthrough ? "ar.appearance_active"
                                                                              : "ar.appearance_help"),
                          sub);
                constexpr Background choices[]{Background::Lounge,  Background::Observatory,
                                               Background::Terrace, Background::Studio,
                                               Background::Night,   Background::Passthrough};
                const char *keys[]{"scene.lounge",       "scene.observatory",  "scene.terrace",
                                   "scene.photo_lounge", "scene.photo_venice", "appearance.passthrough"};
                for (int i = 0; i < 6; ++i)
                    choice(430 + i, third(i % 3, 420.f + (i / 3) * 54, 46), tr(keys[i]),
                           settings.background == choices[i], i != 5 || passthroughAvailable);
            } else {
                for (int i = 0; i < 2; ++i)
                    choice(370 + i, third(i, 420, 54), tr(i == 0 ? "panel.decor_studio" : "panel.decor_night"),
                           (settings.background == Background::Night ? 1 : 0) == i);
            }
            const float sliderTop = immersive ? 532 : 490;
            slider(350, sliderTop, tr("appearance.opacity"), (settings.opacity - .25f) / .75f,
                   number(settings.opacity * 100, 0) + " %",
                   settings.highContrast ? tr("appearance.opaque_help")
                                         : tr(immersive ? "appearance.opacity_help" : "panel.material_help"));
            slider(351, sliderTop + 116, tr("appearance.blur"), settings.blur,
                   immersive && settings.background == Background::Passthrough
                       ? tr("ar.disabled")
                       : number(settings.blur * 100, 0) + " %",
                   !immersive                                       ? tr("panel.material_help")
                   : settings.background == Background::Passthrough ? tr("appearance.passthrough_blur_help")
                                                                    : tr("appearance.blur_help"),
                   !immersive || settings.background != Background::Passthrough);
            slider(352, sliderTop + 232, tr("appearance.brightness"),
                   (settings.brightness - .15f) / .85f,
                   immersive && settings.background == Background::Passthrough
                       ? tr("common.auto")
                       : number(settings.brightness * 100, 0) + " %",
                   tr(!immersive                                       ? "panel.material_help"
                      : settings.background == Background::Passthrough ? "ar.brightness_help"
                                                                       : "appearance.brightness_help"),
                   !immersive || settings.background != Background::Passthrough);
        } else {
            label(d, x, 296, tr("hands.model"));
            for (int i = 0; i < 4; i++)
                choice(380 + i, {x + i * 285, 328, 273, 62},
                       i == 0   ? tr("hands.auto")
                       : i == 1 ? tr("hands.studio")
                       : i == 2 ? tr("hands.skeleton")
                                : tr("hands.hidden"),
                       int(settings.handModel) == i);
            d.text(x, 406, 20, tr("hands.auto_help"), sub);
            slider(353, 450, tr("hands.scale"), (settings.handScale - .75f) / .5f,
                   number(settings.handScale * 100, 0) + " %", tr("hands.scale_help"));
            slider(354, 566, tr("hands.pitch"), (settings.handPitch + 90) / 180,
                   number(settings.handPitch, 0) + "°", tr("hands.pitch_help"));
            setting(384, {x, 682, width, 72}, settings.rays ? tr("hands.rays_on") : tr("hands.rays_off"),
                    settings.rays);
            d.text(x, 776, 20, tr("hands.select_help"), sub);
        }
    } else {
        label(d, x, 106, tr("diagnostics.eyebrow"));
        d.text(x, 136, 48, tr("diagnostics.title"), white);
        d.text(x, 208, 23, elide(d, 23, width, runtime), sub);
        const char *titles[] = {tr("diagnostics.app_rate"), tr("diagnostics.p95"), tr("diagnostics.work")};
        std::string values[] = {stats.count ? number(stats.fps(), 1) + " FPS" : "—",
                                stats.count ? number(stats.percentile95(), 2) + " ms" : "—",
                                number(stats.cpuMs, 2) + " ms"};
        for (int i = 0; i < 3; i++) {
            float cx = x + i * 382.f;
            frame(d, {cx, 262, 364, 138}, surface, 16);
            label(d, cx + 20, 280, titles[i]);
            d.text(cx + 20, 322, 40, values[i], white);
        }
        d.text(x, 424, 22,
               formatMessage(settings.language, "diagnostics.summary",
                             {{"display", stats.displayHz ? number(stats.displayHz, 0) + " Hz"
                                                          : tr("diagnostics.unmeasured")},
                              {"memory", number(stats.rssMiB, 1)},
                              {"unit", tr("diagnostics.mib")}}),
               sub);
        drawIcon(d, Icon::Warning, {x + 11, 476}, 22, warning);
        d.text(x + 32, 462, 20, tr("diagnostics.gpu_help"), warning);
        frame(d, {x, 510, width, 248}, Color::hex(settings.theme == Theme::Light ? 0xc6d7de : 0x101c23), 14);
        label(d, x + 20, 526, tr("diagnostics.log"));
        size_t begin = logs.size() > 5 ? logs.size() - 5 : 0;
        for (size_t i = begin; i < logs.size(); i++)
            d.text(x + 21, 562 + (i - begin) * 36.f, 21, elide(d, 21, width - 42, logs[i]), muted);
        button(401, {x, 782, 360, 62}, tr("diagnostics.export"), true);
        button(402, {x + 376, 782, 300, 62}, tr("diagnostics.clear"));
    }
    d.line({1, 920}, {1439, 920}, lineColor, 1);
    d.circle({33, 955}, 4, mint);
    d.text(49, textTop(955, 19), 19, statusText(), sub);
    static constexpr Icon recenterGlyph = Icon::Recenter, hudGlyph = Icon::Bars;
    if (immersive) button(102, {982, 931, 200, 48}, tr("common.recenter"), false, true, &recenterGlyph);
    choice(103, {1194, 931, 200, 48}, debugVisible ? tr("common.hud_active") : tr("common.hud"), debugVisible, true,
           &hudGlyph);
    if (!toast_.empty()) {
        const float size = 22, textWidth = std::min(d.textWidth(size, toast_), 940.f), toastWidth = textWidth + 80;
        Rect toastRect{830 - toastWidth * .5f, 848, toastWidth, 58};
        d.rect(toastRect, alpha(selectedColor, 1), 14);
        d.border(toastRect, mint, 14);
        drawIcon(d, Icon::Check, {toastRect.x + 34, toastRect.y + 29}, 24, mint);
        d.text(toastRect.x + 56, textTop(toastRect.y + 29, size), size, elide(d, size, textWidth, toast_), white);
    }
    if (!tooltip.empty()) {
        const float tipWidth = d.textWidth(18, tooltip) + 28;
        float tipX = std::min(tooltipAnchor.x + (tooltipAnchor.w - tipWidth) * .5f, 1426 - tipWidth);
        d.rect({tipX, 84, tipWidth, 36}, alpha(hoverColor, 1), 10);
        d.border({tipX, 84, tipWidth, 36}, lineColor, 10);
        d.text(tipX + 14, textTop(102, 18), 18, tooltip, white);
    }
    if (modalOpen) {
        // The modal replaces the hit list so background controls cannot receive input.
        widgets_.clear();
        d.rect({0, 0, 1440, 990}, Color::hex(0x071219, .84f), 30);
        Rect dialog{310, 295, 820, 400};
        Color modalSurface = panelColor;
        modalSurface.a = 1;
        frame(d, dialog, modalSurface, 24);
        label(d, dialog.x + 38, 323, tr("modal.eyebrow"));
        d.text(dialog.x + 38, 366, 42, modalTitle.empty() ? tr("modal.title") : modalTitle, white);
        d.text(dialog.x + 38, 442, 25,
               wrap(d, 25, dialog.w - 76, modalMessage.empty() ? tr("modal.help") : modalMessage), sub);
        button(502, {536, 586, 242, 67}, tr("common.cancel"));
        button(501, {794, 586, 298, 67}, modalConfirm.empty() ? tr("common.confirm") : modalConfirm, true);
    }
}
void UiModel::drawDebug(DrawList &d) {
    if(renderApplicationDebug&&renderApplicationDebug(d))return;
    d.clear();
    palette(settings);
    Color debugSurface = panelColor;
    debugSurface.a = .97f;
    frame(d, {0, 0, 620, 760}, debugSurface, 22);
    label(d, 28, 26, tr("hud.eyebrow"));
    d.text(28, 74, 36, tr("hud.session"), white);
    d.line({28, 140}, {592, 140}, lineColor, 1);
    auto row = [&](float y, const char *title, std::string value) {
        d.text(28, y, 22, title, muted);
        d.text(334, y, 24, value, white);
    };
    row(173, tr("hud.app_rate"), stats.count ? number(stats.fps()) + " FPS" : "—");
    row(232, tr("hud.p95"), stats.count ? number(stats.percentile95(), 2) + " ms" : "—");
    row(291, tr("hud.work"), number(stats.cpuMs, 2) + " ms");
    row(350, tr("hud.display"),
        stats.displayHz ? number(stats.displayHz, 0) + " Hz" : tr("diagnostics.unmeasured"));
    row(409, tr("hud.memory"), number(stats.rssMiB) + " " + tr("diagnostics.mib"));
    row(468, tr("hud.gpu"), tr("diagnostics.unmeasured"));
    d.line({28, 531}, {592, 531}, lineColor, 1);
    d.text(28, 555, 20, tr("hud.budgets"), muted);
    d.text(28, 656, 21, tr("hud.controls"), mint);
    d.text(28, 705, 17, tr("hud.measurements"), muted);
}
void UiModel::drawDock(DrawList &d) {
    palette(settings);
    d.clear();
    if (interfaceHidden) { widgets_.clear(); return; }
    if (compactInterface) return;
    if (presentation != Presentation::Immersive) return;
    widgets_.erase(std::remove_if(widgets_.begin(), widgets_.end(),
                                  [](auto &w) { return w.id >= 600 && w.id < 700 && w.id != 605; }),
                   widgets_.end());
    if (windowState != WindowState::Open)
        widgets_.erase(std::remove_if(widgets_.begin(), widgets_.end(),
                                      [](auto &w) { return w.id < 600 || w.id >= 700 || w.id == 605; }),
                       widgets_.end());
    if (renderApplicationDock && renderApplicationDock(d))
        return;
    frame(d, {0, 0, float(dockWidth), float(dockHeight)},
          Color::hex(settings.theme == Theme::Light ? 0xe4eef1 : 0x17272f, .97f), 32);
    std::string hint;
    auto control = [&](int id, Rect r, Icon glyph, const char *text, bool selected = false,
                       Control type = Control::Button) {
        if (pressed(id) || hover_ == id || selected)
            d.rect(r, selected || pressed(id) ? selectedColor : hoverColor, 17);
        drawIcon(d, glyph, {r.x + r.w * .5f, r.y + r.h * .5f}, 30, selected ? mint : white);
        if (hover_ == id)
            hint = text;
        addWidget(id, {r.x + dockOriginX, r.y + dockOriginY, r.w, r.h}, type);
    };
    static constexpr Icon placement[]{Icon::Pin, Icon::Eye, Icon::Follow};
    control(600, {10, 6, 112, 58}, Icon::Move, tr("dock.move"), false, Control::Drag);
    control(610, {130, 6, 112, 58}, placement[int(settings.follow)],
            settings.follow == FollowMode::World        ? tr("dock.world")
            : settings.follow == FollowMode::FaceViewer ? tr("settings.face")
                                                        : tr("settings.follow"),
            settings.follow != FollowMode::World);
    d.line({246, 22}, {246, 48}, lineColor, 1);
    if (windowState == WindowState::Open) {
        control(611, {250, 6, 104, 58}, Icon::Minimize, tr("dock.minimize"));
        control(612, {362, 6, 108, 58}, Icon::Close, tr("dock.close"));
    } else
        control(613, {250, 6, 220, 58}, windowState == WindowState::Closed ? Icon::Open : Icon::Restore,
                windowState == WindowState::Closed ? tr("dock.open") : tr("dock.restore"));
    if (!hint.empty())
        d.text((dockWidth - d.textWidth(16, hint)) * .5f, 68, 16, hint, muted);
}
} // namespace cyvr
