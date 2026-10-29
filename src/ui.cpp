// SPDX-License-Identifier: MIT
#include "cyvr/core.hpp"
#include <cstdio>

namespace cyvr {
namespace {
thread_local Color mint = Color::hex(0x86e7cf), ink = Color::hex(0x102d28), white = Color::hex(0xf0f5f6),
                   muted = Color::hex(0xa7b7bf), lineColor = Color::hex(0x354851),
                   warning = Color::hex(0xf2c683);
thread_local Color panelColor, surfaceColor, hoverColor, selectedColor, headerColor;
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
    headerColor = Color::hex(light ? 0xc7d8df : ocean ? 0x223258 : 0x1d2a31, .45f);
}
void frame(DrawList &d, Rect r, Color fill, float radius = 18) {
    d.rect(r, fill, radius);
    d.line({r.x + radius, r.y + .5f}, {r.x + r.w - radius, r.y + .5f}, lineColor, 1);
    d.line({r.x + radius, r.y + r.h - .5f}, {r.x + r.w - radius, r.y + r.h - .5f}, lineColor, 1);
    d.line({r.x + .5f, r.y + radius}, {r.x + .5f, r.y + r.h - radius}, lineColor, 1);
    d.line({r.x + r.w - .5f, r.y + radius}, {r.x + r.w - .5f, r.y + r.h - radius}, lineColor, 1);
}
void label(DrawList &d, float x, float y, const char *value) {
    d.text(x, y, 18, value, mint);
}
void icon(DrawList &d, int index, float x, float y, Color c) {
    if (index == 0) {
        for (int j = 0; j < 2; j++)
            for (int i = 0; i < 2; i++)
                d.rect({x + i * 15, y + j * 15, 10, 10}, c, 2);
    }
    if (index == 1) {
        for (int i = 0; i < 3; i++) {
            float yy = y + 4 + i * 8;
            d.line({x, yy}, {x + 12, yy + 6}, c);
            d.line({x + 12, yy + 6}, {x + 24, yy}, c);
        }
    }
    if (index == 2) {
        for (int i = 0; i < 3; i++) {
            d.line({x + i * 10, y}, {x + i * 10, y + 25}, c);
            d.rect({x - 3 + i * 10, y + 4 + i * 5, 6, 5}, c, 2);
        }
    }
    if (index == 3) {
        d.line({x, y + 14}, {x + 5, y + 14}, c);
        d.line({x + 5, y + 14}, {x + 10, y + 2}, c);
        d.line({x + 10, y + 2}, {x + 16, y + 25}, c);
        d.line({x + 16, y + 25}, {x + 21, y + 14}, c);
        d.line({x + 21, y + 14}, {x + 27, y + 14}, c);
    }
}
void windowIcon(DrawList &d, int kind, float x, float y, Color c) {
    auto line = [&](float a, float b, float e, float f) { d.line({x + a, y + b}, {x + e, y + f}, c, 2.5f); };
    auto outline = [&](Rect r) {
        line(r.x, r.y, r.x + r.w, r.y);
        line(r.x + r.w, r.y, r.x + r.w, r.y + r.h);
        line(r.x + r.w, r.y + r.h, r.x, r.y + r.h);
        line(r.x, r.y + r.h, r.x, r.y);
    };
    if (kind == 0) {
        for (auto axis : {Vec2{1, 0}, Vec2{0, 1}, Vec2{-1, 0}, Vec2{0, -1}}) {
            Vec2 end{axis.x * 15, axis.y * 15}, base{axis.x * 9, axis.y * 9}, side{-axis.y * 5, axis.x * 5};
            line(axis.x * 3, axis.y * 3, end.x, end.y);
            line(end.x, end.y, base.x + side.x, base.y + side.y);
            line(end.x, end.y, base.x - side.x, base.y - side.y);
        }
    } else if (kind == 1) {
        // A pinned window represents the world anchor.
        outline({-16, -12, 29, 23});
        line(-5, 15, 4, 15);
        line(0, 11, 0, 15);
        d.rect({x + 6, y - 18, 13, 13}, c, 3);
        line(12, -5, 12, 4);
    } else if (kind == 2) {
        // Eye: keep the window facing the viewer.
        for (int i = 0; i < 24; i++) {
            float a = i * 2 * pi / 24, b = (i + 1) * 2 * pi / 24;
            line(std::cos(a) * 18, std::sin(a) * 10, std::cos(b) * 18, std::sin(b) * 10);
        }
        d.rect({x - 5, y - 5, 10, 10}, c, 5);
    } else if (kind == 3) {
        // Headset with a return arrow: follow the view.
        outline({-16, -7, 32, 17});
        line(-7, 10, 0, 5);
        line(0, 5, 7, 10);
        line(-12, -15, 12, -15);
        line(12, -15, 7, -20);
        line(12, -15, 7, -10);
    } else if (kind == 4)
        line(-14, 8, 14, 8);
    else if (kind == 5) {
        line(-11, -11, 11, 11);
        line(-11, 11, 11, -11);
    } else if (kind == 6) {
        outline({-14, -6, 22, 20});
        line(-7, -13, 15, -13);
        line(15, -13, 15, 7);
        line(8, 7, 15, 7);
    } else if (kind == 7) {
        for (int j = 0; j < 2; j++)
            for (int i = 0; i < 2; i++)
                d.rect({x - 15 + i * 18, y - 15 + j * 18, 12, 12}, c, 3);
    }
}
} // namespace
void UiModel::drawMain(DrawList &d) {
    if (renderApplication && renderApplication(d))
        return;
    d.clear();
    widgets_.clear();
    palette(settings);
    const Color panel = panelColor, surface = surfaceColor;
    const Color sub = settings.highContrast ? white : muted;
    frame(d, {0, 0, 1440, 990}, panel, 30);
    d.rect({1, 1, 1438, 77}, headerColor, 29);
    d.rect({1, 50, 1438, 28}, headerColor);
    d.text(28, 13, 38, "C", mint);
    d.line({61, 27}, {72, 38}, mint, 3);
    d.line({72, 38}, {61, 49}, mint, 3);
    d.text(94, 24, 24, "CyVR", white);
    d.text(175, 27, 19, workspaceLabel, muted);
    d.rect({1093, 35, 7, 7}, mint, 3);
    d.text(1114, 24, 20, contextLabel.empty() ? tr("header.personal") : contextLabel, muted);
    d.rect({566, 8, 444, 61}, hover_ == 605 ? hoverColor : surface, 12);
    windowIcon(d, 0, 788, 38, mint);
    if (hover_ == 605)
        d.text(625, 48, 16, tr("dock.move_window"), white);
    addWidget(605, {566, 8, 444, 61}, Control::Drag);
    d.line({0, 78}, {1440, 78}, lineColor, 1);
    d.line({222, 78}, {222, 920}, lineColor, 1);
    label(d, 28, 114, tr("header.space"));
    auto button = [&](int id, Rect r, std::string_view text, bool primary = false, bool enabled = true) {
        bool active = hover_ == id;
        Color fill = primary ? mint : (active ? hoverColor : surface);
        if (!enabled)
            fill = surface;
        frame(d, r, fill, 12);
        float textX = r.x + (r.w - d.textWidth(25, text)) * .5f;
        d.text(textX, r.y + 14, 25, text, enabled ? (primary ? ink : white) : muted);
        addWidget(id, r, Control::Button, enabled);
    };
    for (int i = 0; i < 4; i++) {
        Rect r{16, 165.f + i * 83, 190, 66};
        if (page == Page(i))
            d.rect(r, selectedColor, 12);
        else if (hover_ == i + 1)
            d.rect(r, surface, 12);
        Color c = page == Page(i) ? mint : sub;
        icon(d, i, 29, r.y + 21, c);
        d.text(67, r.y + 18, 23, pageLabel(i), c);
        addWidget(i + 1, r);
    }
    label(d, 28, 533, tr("language.label"));
    Rect languageButton{28, 574, 166, 62};
    frame(d, languageButton, hover_ == 410 ? hoverColor : surface, 12);
    auto languageName = resolveLocale(settings.language).nativeName;
    float languageSize = std::min(23.f, 135.f / std::max(1.f, d.textWidth(1, languageName)));
    d.text(languageButton.x + (languageButton.w - d.textWidth(languageSize, languageName)) * .5f, 590,
           languageSize, languageName, white);
    addWidget(410, languageButton);
    d.line({28, 685}, {193, 685}, lineColor, 1);
    d.text(29, 713, 21, sidebarTitle.empty() ? tr("brand.custom") : sidebarTitle, white);
    d.text(29, 751, 18, sidebarSubtitle.empty() ? tr("brand.open") : sidebarSubtitle, muted);
    label(d, 29, 815, "C++ / OPENXR");
    label(d, 29, 848, "VULKAN / 0.3");
    const float x = 266, right = 1394, width = right - x;
    if (renderContent && renderContent(d)) {
        // Application owns only the content region; chrome, modal and comfort remain shared.
    } else if (page == Page::Home) {
        label(d, x, 118, tr("home.eyebrow"));
        d.text(x, 157, 60, tr("home.title"), white);
        d.text(x, 244, 27, tr("home.subtitle"), sub);
        Rect hero{x, 315, 694, 274};
        frame(d, hero, selectedColor, 20);
        label(d, x + 27, 344, tr("home.lab"));
        d.text(x + 27, 384, 40, tr("home.hero"), white);
        d.text(x + 27, 446, 25, tr("home.hero_help"), muted);
        button(100, {x + 27, 507, 302, 62}, tr("home.explore"), true);
        // A small wire sculpture uses only shared UI geometry, no models or textures.
        for (int ring = 0; ring < 3; ring++)
            for (int j = 0; j < 64; j++) {
                float a = j * pi / 32, b = (j + 1) * pi / 32, rx = 47 + ring * 18.f, ry = 88 + ring * 15.f;
                d.line({x + 575 + std::cos(a) * rx, 448 + std::sin(a) * ry},
                       {x + 575 + std::cos(b) * rx, 448 + std::sin(b) * ry}, Color::hex(0x5c9382), 1.5f);
            }
        frame(d, {992, 315, 402, 274}, surface, 20);
        icon(d, 1, 1022, 346, mint);
        label(d, 1022, 403, tr("home.blocks"));
        d.text(1022, 443, 29, tr("home.foundation"), white);
        d.text(1022, 497, 23, tr("home.blocks_help"), sub);
        label(d, x, 636, tr("home.personalize_label"));
        frame(d, {x, 681, width, 138}, surface, 16);
        icon(d, 2, x + 28, 713, mint);
        d.text(x + 85, 701, 30, tr("home.balance"), white);
        d.text(x + 85, 751, 23, tr("home.balance_help"), sub);
        button(101, {1122, 721, 244, 62}, tr("home.personalize"));
        d.text(x, 851, 20, tr("home.dock_help"), muted);
    } else if (page == Page::Components) {
        label(d, x, 118, tr("components.eyebrow"));
        d.text(x, 154, 47, tr("components.title"), white);
        d.text(x, 224, 25, tr("components.help"), sub);
        float column = 550;
        auto cell = [&](float cx, float cy, const char *name, const char *badge) {
            frame(d, {cx, 288 + cy, column, 187}, surface, 16);
            label(d, cx + 23, 307 + cy, name);
            d.text(cx + 370, 310 + cy, 16, badge, muted);
        };
        cell(x, 0, tr("components.actions"), "BUTTON");
        button(201, {x + 22, 363, 238, 70}, tr("components.continue"), true);
        button(202, {x + 277, 363, 250, 70}, tr("components.secondary"));
        cell(844, 0, tr("components.preference"), "TOGGLE");
        Rect toggle{866, 363, 506, 70};
        d.text(884, 382, 26, tr("components.enable"), white);
        d.rect({1276, 382, 71, 36}, exampleToggle_ ? mint : lineColor, 18);
        d.rect({float(exampleToggle_ ? 1315 : 1281), 387, 26, 26}, exampleToggle_ ? ink : muted, 13);
        addWidget(203, toggle);
        cell(x, 210, tr("components.adjustment"), "SLIDER");
        label(d, x + 26, 570, tr("components.value"));
        d.text(x + 433, 570, 23, number(exampleValue_ * 100, 0) + " %", white);
        d.rect({x + 27, 637, 496, 7}, lineColor, 3);
        d.rect({x + 27, 637, 496 * exampleValue_, 7}, mint, 3);
        d.rect({x + 14 + 496 * exampleValue_, 627, 27, 27}, white, 14);
        addWidget(210, {x + 27, 610, 496, 62}, Control::Slider);
        cell(844, 210, tr("components.choice"), "SEGMENTS");
        for (int i = 0; i < 3; i++)
            button(204 + i, {866.f + i * 169, 587, 161, 66},
                   i == 0   ? tr("components.calm")
                   : i == 1 ? tr("components.balanced")
                            : tr("components.detailed"),
                   exampleChoice_ == i);
        cell(x, 420, tr("components.states"), "FEEDBACK");
        d.rect({x + 23, 786, 210, 51}, selectedColor, 9);
        d.text(x + 39, 797, 23, tr("components.available"), mint);
        d.rect({x + 254, 786, 269, 51}, Color::hex(settings.theme == Theme::Light ? 0xf4dfc2 : 0x493f30), 9);
        d.text(x + 271, 797, 23, tr("components.check"), warning);
        cell(844, 420, tr("components.disabled"), "DISABLED");
        button(299, {866, 782, 506, 66}, tr("components.unavailable"), false, false);
    } else if (page == Page::Settings) {
        label(d, x, 118, tr("settings.eyebrow"));
        d.text(x, 154, 48, tr("settings.title"), white);
        for (int i = 0; i < 3; i++)
            button(340 + i, {x + i * 378, 229, 366, 58},
                   i == 0   ? tr("settings.placement")
                   : i == 1 ? tr("settings.appearance")
                            : tr("settings.hands"),
                   settingsTab == i);
        auto slider = [&](int id, float y, const char *title, float fraction, std::string value,
                          const char *help) {
            frame(d, {x, y, width, 114}, surface, 16);
            d.text(x + 25, y + 10, 25, title, white);
            d.text(1233, y + 12, 24, value, mint);
            d.text(x + 25, y + 46, 19, help, sub);
            d.rect({x + 27, y + 91, width - 54, 6}, lineColor, 3);
            d.rect({x + 27, y + 91, (width - 54) * fraction, 6}, mint, 3);
            d.rect({x + 14 + (width - 54) * fraction, y + 81, 27, 27}, white, 14);
            addWidget(id, {x + 27, y + 67, width - 54, 47}, Control::Slider);
        };
        if (settingsTab == 0) {
            slider(310, 309, tr("settings.distance"), settings.distance - 1,
                   number(settings.distance, 2) + " m", tr("settings.distance_help"));
            slider(311, 437, tr("settings.scale"), (settings.scale - .85f) / .4f,
                   number(settings.scale * 100, 0) + " %", tr("settings.scale_help"));
            auto setting = [&](int id, float cx, const char *title, bool checked) {
                Rect r{cx, 569, 364, 76};
                frame(d, r, surface, 12);
                d.text(cx + 16, 591, 22, title, white);
                d.rect({cx + 283, 594, 58, 30}, checked ? mint : lineColor, 15);
                d.rect({cx + (checked ? 312 : 287), 598, 22, 22}, checked ? ink : muted, 11);
                addWidget(id, r);
            };
            setting(312, x, tr("settings.contrast"), settings.highContrast);
            setting(313, 648, tr("settings.motion"), settings.reducedMotion);
            setting(314, 1030, tr("settings.haptics"), settings.haptics);
            label(d, x, 665, tr("settings.anchor"));
            for (int i = 0; i < 3; i++)
                button(390 + i, {x + i * 378, 698, 366, 58},
                       i == 0   ? tr("settings.world")
                       : i == 1 ? tr("settings.face")
                                : tr("settings.follow"),
                       int(settings.follow) == i);
            label(d, x, 777, tr("settings.refresh"));
            for (int i = 0; i < 3; i++) {
                int hz = i == 0 ? 72 : i == 1 ? 90 : 120;
                bool available =
                    std::find(refreshRates.begin(), refreshRates.end(), float(hz)) != refreshRates.end();
                button(320 + i, {x + i * 192, 810, 176, 60}, std::to_string(hz) + " Hz",
                       settings.requestedHz == hz, available);
            }
            button(330, {1024, 810, 370, 60}, tr("settings.restore"));
        } else if (settingsTab == 1) {
            label(d, x, 311, tr("appearance.theme"));
            for (int i = 0; i < 3; i++)
                button(360 + i, {x + i * 378, 344, 366, 58},
                       i == 0   ? tr("appearance.graphite")
                       : i == 1 ? tr("appearance.light")
                                : tr("appearance.ocean"),
                       int(settings.theme) == i);
            label(d, x, 422, tr("appearance.environment"));
            for (int i = 0; i < 3; i++)
                button(370 + i, {x + i * 378, 455, 366, 58},
                       i == 0   ? tr("appearance.studio")
                       : i == 1 ? tr("appearance.night")
                                : tr("appearance.passthrough"),
                       int(settings.background) == i, i != 2 || passthroughAvailable);
            slider(350, 531, tr("appearance.opacity"), (settings.opacity - .25f) / .75f,
                   number(settings.opacity * 100, 0) + " %",
                   settings.highContrast ? tr("appearance.opaque_help") : tr("appearance.opacity_help"));
            slider(351, 659, tr("appearance.blur"), settings.blur, number(settings.blur * 100, 0) + " %",
                   settings.background == Background::Passthrough ? tr("appearance.passthrough_blur_help")
                                                                  : tr("appearance.blur_help"));
            slider(352, 787, tr("appearance.brightness"), (settings.brightness - .15f) / .85f,
                   number(settings.brightness * 100, 0) + " %", tr("appearance.brightness_help"));
        } else {
            label(d, x, 314, tr("hands.model"));
            for (int i = 0; i < 4; i++)
                button(380 + i, {x + i * 284, 350, 272, 66},
                       i == 0   ? tr("hands.auto")
                       : i == 1 ? tr("appearance.studio")
                       : i == 2 ? tr("hands.skeleton")
                                : tr("hands.hidden"),
                       int(settings.handModel) == i);
            d.text(x, 438, 22, tr("hands.auto_help"), sub);
            slider(353, 490, tr("hands.scale"), (settings.handScale - .75f) / .5f,
                   number(settings.handScale * 100, 0) + " %", tr("hands.scale_help"));
            slider(354, 623, tr("hands.pitch"), (settings.handPitch + 90) / 180,
                   number(settings.handPitch, 0) + "°", tr("hands.pitch_help"));
            button(384, {x, 767, 540, 64}, settings.rays ? tr("hands.rays_on") : tr("hands.rays_off"),
                   settings.rays);
            d.text(x, 857, 21, tr("hands.select_help"), sub);
        }
    } else {
        label(d, x, 118, tr("diagnostics.eyebrow"));
        d.text(x, 155, 49, tr("diagnostics.title"), white);
        d.text(x, 229, 23, runtime, sub);
        const char *titles[] = {tr("diagnostics.app_rate"), tr("diagnostics.p95"), tr("diagnostics.work")};
        std::string values[] = {stats.count ? number(stats.fps(), 1) + " FPS" : "—",
                                stats.count ? number(stats.percentile95(), 2) + " ms" : "—",
                                number(stats.cpuMs, 2) + " ms"};
        for (int i = 0; i < 3; i++) {
            float cx = x + i * 382.f;
            frame(d, {cx, 292, 364, 150}, surface, 16);
            label(d, cx + 20, 311, titles[i]);
            d.text(cx + 20, 356, 40, values[i], white);
        }
        d.text(x, 473, 22,
               formatMessage(settings.language, "diagnostics.summary",
                             {{"display", stats.displayHz ? number(stats.displayHz, 0) + " Hz"
                                                          : tr("diagnostics.unmeasured")},
                              {"memory", number(stats.rssMiB, 1)},
                              {"unit", tr("diagnostics.mib")}}),
               sub);
        d.text(x, 513, 20, tr("diagnostics.gpu_help"), warning);
        frame(d, {x, 570, width, 226}, Color::hex(settings.theme == Theme::Light ? 0xc6d7de : 0x101c23), 14);
        label(d, x + 20, 588, tr("diagnostics.log"));
        size_t begin = logs.size() > 4 ? logs.size() - 4 : 0;
        for (size_t i = begin; i < logs.size(); i++)
            d.text(x + 21, 627 + (i - begin) * 35.f, 21, logs[i], muted);
        button(401, {x, 823, 360, 66}, tr("diagnostics.export"), true);
        button(402, {x + 380, 823, 315, 66}, tr("diagnostics.clear"));
        button(103, {1104, 823, 290, 66},
               debugVisible ? tr("diagnostics.hide_hud") : tr("diagnostics.show_hud"));
    }
    d.line({1, 920}, {1439, 920}, lineColor, 1);
    d.rect({29, 951, 7, 7}, mint, 3);
    d.text(49, 938, 20, statusText(), sub);
    button(102, {962, 927, 227, 59}, tr("common.recenter"));
    button(103, {1204, 927, 208, 59}, debugVisible ? tr("common.hud_active") : tr("common.hud"));
    if (!toast_.empty()) {
        frame(d, {370, 844, 970, 65}, settings.theme == Theme::Light ? selectedColor : Color::hex(0x38564b),
              12);
        d.text(394, 860, 24, toast_, white);
    }
    if (modalOpen) {
        // The modal replaces the hit list so background controls cannot receive input.
        widgets_.clear();
        d.rect({0, 0, 1440, 990}, Color::hex(0x071219, .84f), 30);
        Color modalSurface = panelColor;
        modalSurface.a = 1;
        frame(d, {355, 289, 820, 401}, modalSurface, 24);
        label(d, 393, 323, "CONFIRMATION");
        d.text(393, 372, 42, modalTitle.empty() ? tr("modal.title") : modalTitle, white);
        d.text(393, 447, 26, modalMessage.empty() ? tr("modal.help") : modalMessage, sub);
        button(502, {574, 580, 242, 67}, tr("common.cancel"));
        button(501, {836, 580, 298, 67}, modalConfirm.empty() ? tr("common.confirm") : modalConfirm, true);
    }
}
void UiModel::drawDebug(DrawList &d) {
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
    auto control = [&](int id, Rect r, int iconKind, const char *text, bool selected = false,
                       Control type = Control::Button) {
        if (hover_ == id || selected)
            d.rect(r, selected ? selectedColor : hoverColor, 17);
        windowIcon(d, iconKind, r.x + r.w * .5f, r.y + 27, selected ? mint : white);
        if (hover_ == id)
            hint = text;
        addWidget(id, {r.x + dockOriginX, r.y + dockOriginY, r.w, r.h}, type);
    };
    control(600, {10, 6, 112, 58}, 0, tr("dock.move"), false, Control::Drag);
    control(610, {130, 6, 112, 58}, 1 + int(settings.follow),
            settings.follow == FollowMode::World        ? tr("dock.world")
            : settings.follow == FollowMode::FaceViewer ? tr("settings.face")
                                                        : tr("settings.follow"),
            settings.follow != FollowMode::World);
    if (windowState == WindowState::Open) {
        control(611, {250, 6, 104, 58}, 4, tr("dock.minimize"));
        control(612, {362, 6, 108, 58}, 5, tr("dock.close"));
    } else
        control(613, {250, 6, 220, 58}, windowState == WindowState::Closed ? 7 : 6,
                windowState == WindowState::Closed ? tr("dock.open") : tr("dock.restore"));
    if (!hint.empty())
        d.text((dockWidth - d.textWidth(16, hint)) * .5f, 68, 16, hint, muted);
}
} // namespace cyvr
