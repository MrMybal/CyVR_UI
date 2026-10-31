// SPDX-License-Identifier: MIT
#pragma once
#include "cyvr/core.hpp"
namespace cyvr {
enum class Icon {
    Home, Components, Settings, Diagnostics, Move, Pin, Eye, Headset, Follow,
    Minimize, Close, Restore, Open, Camera, Globe, Recenter, Bars, Check, Warning,
    Selector, Back, Forward, Reload, Plus, Bookmark, Extensions, Search, More,
    Stop, ArrowEnter, Keyboard, Trash, Shield, NewWindow
};
// Shared 24-unit vector grid, matching the foundation's component catalogue.
void drawIcon(DrawList&, Icon, Vec2 center, float size, Color, float weight=2);
void drawLogo(DrawList&, Vec2 center, Color);
struct ThemeColors {Color accent, text, muted, border, panel, surface, hover, selected;};
ThemeColors themeColors(const Settings&);
}
