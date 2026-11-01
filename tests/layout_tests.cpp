// SPDX-License-Identifier: MIT
#include "cyvr/layout.hpp"
#include <filesystem>
#include <iostream>
#include <stdexcept>
using namespace cyvr;
#define REQUIRE(x)                                                                                           \
    do {                                                                                                     \
        if (!(x))                                                                                            \
            throw std::runtime_error("Layout check failed: " #x);                                            \
    } while (false)
void rejects(const std::string &text) {
    try {
        parseLayout(text);
    } catch (const std::exception &) {
        return;
    }
    throw std::runtime_error("Invalid document was accepted");
}
int main() {
    try {
        for (auto language : {"en", "fr"}) {
            auto doc = referenceLayout(language);
            REQUIRE(doc.screens.size() == 6);
            auto text = serializeLayout(doc);
            auto loaded = parseLayout(text);
            REQUIRE(serializeLayout(loaded) == text);
            auto &s = loaded.screens[0];
            auto button = std::find_if(s.elements.begin(), s.elements.end(),
                                       [](auto &e) { return e.id == "control-100"; });
            REQUIRE(button != s.elements.end());
            int children = 0;
            for (auto &e : s.elements)
                if (e.parent == button->id)
                    children++;
            REQUIRE(children >= 2);
            UiModel input;
            DrawList draw;
            drawLayout(s, draw, &input);
            REQUIRE(!draw.vertices.empty());
            REQUIRE(std::any_of(input.widgets().begin(), input.widgets().end(),
                                [](auto &w) { return w.id == 100; }));
        }
        LayoutEditor editor;
        editor.reset(referenceLayout());
        editor.selection = "control-100";
        auto old = editor.selected()->bounds;
        editor.checkpoint();
        editor.moveSelected(40, 24, false);
        REQUIRE(editor.selected()->bounds.x == old.x + 40);
        REQUIRE(editor.pick({old.x + 50, old.y + 34}) == "control-100");
        REQUIRE(editor.undo());
        REQUIRE(editor.selected()->bounds.x == old.x);
        REQUIRE(editor.redo());
        editor.resizeSelected(old.w * 2, old.h * 2, false);
        auto &s = editor.screen();
        auto child = std::find_if(s.elements.begin(), s.elements.end(),
                                  [](auto &e) { return e.parent == "control-100" && e.type == "rect"; });
        REQUIRE(child != s.elements.end());
        REQUIRE(std::abs(layoutBounds(s, *child).w - old.w * 2) < .01f);
        editor.checkpoint();
        editor.duplicateSelected();
        REQUIRE(editor.selection != "control-100");
        REQUIRE(editor.selected()->action == 0);
        auto duplicate = editor.selection;
        editor.removeSelected();
        REQUIRE(std::none_of(editor.screen().elements.begin(), editor.screen().elements.end(),
                             [&](auto &e) { return e.id == duplicate || e.parent == duplicate; }));
        REQUIRE(editor.undo());
        validateLayout(editor.document);
        editor.add("button");
        validateLayout(editor.document);
        editor.selected()->visible = false;
        for (auto &e : editor.screen().elements)
            if (e.parent == editor.selection)
                REQUIRE(!layoutVisible(editor.screen(), e));
        auto file = std::filesystem::temp_directory_path() / "cyvr-layout-test.cyvr.json";
        saveLayout(file.u8string(), editor.document);
        saveLayout(file.u8string(), editor.document);
        REQUIRE(serializeLayout(loadLayout(file.u8string())) == serializeLayout(editor.document));
        std::filesystem::remove(file);
        auto valid = serializeLayout(editor.document);
        rejects(valid + "garbage");
        rejects("{\"format\":\"cyvr-ui-layout\",\"format\":\"duplicate\"}");
        rejects("{\"format\":\"\\uD800\"}");
        rejects("{\"format\":\"\\uDC00\"}");
        rejects("{\"n\":1e9999}");
        auto invalid = editor.document;
        invalid.screens[0].elements[0].parent = invalid.screens[0].elements[0].id;
        try {
            validateLayout(invalid);
            throw 42;
        } catch (const std::runtime_error &) {
        }
        invalid = editor.document;
        invalid.screens[0].elements[1].id = invalid.screens[0].elements[0].id;
        try {
            validateLayout(invalid);
            throw 42;
        } catch (const std::runtime_error &) {
        }
        invalid = editor.document;
        invalid.screens[0].elements[0].bounds.w = 0;
        try {
            validateLayout(invalid);
            throw 42;
        } catch (const std::runtime_error &) {
        }
        REQUIRE(layoutOutline(editor.document).find("control-100") != std::string::npos);
        std::cout << "Layout capture, JSON, groups, input, undo/redo and persistence passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}
