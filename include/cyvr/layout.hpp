// SPDX-License-Identifier: MIT
#pragma once
#include "cyvr/core.hpp"

namespace cyvr {
// Coordinates are logical pixels. Children live in their parent's viewBox.
struct LayoutElement {
    std::string id, name, parent, type{"rect"};
    Rect bounds{0, 0, 200, 60}, uv{0, 0, 1, 1};
    Vec2 viewBox{200, 60};
    Color color{1, 1, 1, 1};
    float radius{}, stroke{2}, fontSize{24};
    std::string text, localizationKey;
    std::vector<Vec2> points;
    int icon{}, action{};
    Control control{Control::Button};
    bool visible{true}, locked{}, enabled{true}, closed{};
};
struct LayoutScreen {
    std::string id{"screen"}, name{"Screen"};
    float width{1440}, height{990};
    std::vector<LayoutElement> elements;
};
struct LayoutDocument {
    std::string name{"Untitled interface"}, language{"en"};
    std::vector<LayoutScreen> screens;
};
void validateLayout(const LayoutDocument &document);
LayoutDocument parseLayout(std::string_view json);
std::string serializeLayout(const LayoutDocument &document);
LayoutDocument loadLayout(const std::string &path);
void saveLayout(const std::string &path, const LayoutDocument &document);
LayoutScreen captureLayoutScreen(UiModel &model, std::string id, std::string name);
LayoutDocument referenceLayout(std::string language = "en", Theme theme = Theme::Graphite);
Rect layoutBounds(const LayoutScreen &screen, const LayoutElement &element);
bool layoutVisible(const LayoutScreen &screen, const LayoutElement &element);
// Register original action IDs; callbacks and live state remain application-owned.
void drawLayout(const LayoutScreen &screen, DrawList &draw, UiModel *input = nullptr, bool schematic = false,
                std::string_view language = "en");
std::string layoutOutline(const LayoutDocument &document);

class LayoutEditor {
  public:
    LayoutDocument document;
    size_t screenIndex{};
    std::string selection;
    LayoutScreen &screen() { return document.screens.at(screenIndex); }
    const LayoutScreen &screen() const { return document.screens.at(screenIndex); }
    LayoutElement *selected();
    void checkpoint();
    bool undo();
    bool redo();
    void reset(LayoutDocument next);
    std::string pick(Vec2 point, bool individual = false) const;
    void moveSelected(float dx, float dy, bool snap);
    void resizeSelected(float width, float height, bool snap);
    void removeSelected();
    void duplicateSelected();
    void add(std::string_view type);
    void reorderSelected(int direction);

  private:
    struct State {
        LayoutDocument document;
        size_t screen;
        std::string selection;
    };
    std::vector<State> undo_, redo_;
};
} // namespace cyvr
