# Native 2D interface editor

CyVR_Editor is a Windows application for designing and discussing CyVR interfaces. It uses C++, native Windows controls and the same Vulkan/DrawList renderer as the application. It runs locally without a browser, account or AI service.

Build with:

    .\build.ps1 -Target windows

Run:

    .\dist\windows\CyVR_Editor.exe

Keep the executable alongside its assets folder. The editor requires a Vulkan driver. Its default interface language is English; French is available in the top-right selector.

## Start from the application

The initial document captures six actual reference states: Home, Components, Accessibility, Appearance, Diagnostics and the confirmation dialog. From app recreates these states from the current C++ renderer, using the editor's selected language. It asks before replacing unsaved work.

The capture records text, shapes, vector icons and existing input regions. A button's shapes and label belong to a group with its original action ID. This makes it possible to move or resize the control as a whole while preserving its hit region. Alt-click selects an individual child instead.

Custom CyVR applications can export their own screens with captureLayoutScreen after setting up their UiModel and application rendering hooks. This captures an application's DrawList calls; it does not inspect arbitrary APKs or other desktop applications.

## Edit

- Select a screen from the top-left list.
- Select an element on the canvas or in the elements list.
- Drag to move; drag the bottom-right handle to resize.
- Use the wheel to zoom and the middle mouse button to pan. Fit restores the fitted view.
- Snap uses an 8-pixel grid on release. Hold Shift to bypass snapping for a gesture.
- Arrow keys move the selection by one logical pixel; Shift uses eight.
- Alt-click selects a child shape, text or icon inside a control.
- The inspector edits position, size, name, label, font size, corner radius, RGBA color, action ID, translation key and icon ID. Apply commits the fields; Save also applies pending fields.
- For groups, Text and Font edit the first text child; Color and Radius edit the first rectangle child. Select children for more detailed edits.
- Visibility hides an entire group. Locking prevents canvas movement and deletion; the inspector can unlock it.
- Add text, panels, buttons or icons; duplicate, delete or change paint order. A duplicate starts without the original root action binding.
- Add screen is available from Insert. Its name and canvas size can be edited on the left.
- Schematic simplifies decoration while retaining the structure and text.

Coordinates use logical pixels. A group's children use its local viewBox; resizing the group scales its contents and input region. Resizing a screen changes the canvas, without automatically reflowing its elements.

Keyboard shortcuts:

| Action | Shortcut |
|---|---|
| New / open | Ctrl+N / Ctrl+O |
| Save / save as | Ctrl+S / Ctrl+Shift+S |
| Export AI bundle | Ctrl+E |
| Undo / redo | Ctrl+Z / Ctrl+Y |
| Duplicate / delete | Ctrl+D / Delete |

When a text field has focus, normal text editing takes priority. The Edit menu remains available for document undo/redo. The document history retains up to 48 changes.

## Files and AI workflow

Save writes a versioned UTF-8 JSON document. The example is [design/reference.cyvr.json](../design/reference.cyvr.json); the schema is [design/layout.schema.json](../design/layout.schema.json).

Export AI writes:

- The complete JSON document, including IDs, groups, bounds, labels, colors and action bindings.
- A Markdown outline with element IDs and resolved bounds.
- A PNG for each screen, using the current faithful or schematic view.

No content is uploaded or sent to an AI. Give these files to an assistant yourself, then open the edited JSON in the editor. Stable element IDs let you refer to an exact control instead of relying only on a screenshot.

Example request for an assistant:

> Edit the supplied CyVR layout JSON. In the home screen, move control-100 below the description and widen it to 340 logical pixels. Preserve IDs, action bindings and other screens. Return the updated JSON and validate it against layout.schema.json.

Command-line workflows:

    # Validate without creating a window or initializing Vulkan.
    .\dist\windows\CyVR_Editor.exe --open design/reference.cyvr.json --validate

    # Capture the current C++ reference screens.
    .\dist\windows\CyVR_Editor.exe --export-reference my-layout.cyvr.json

    # Render one screen from an edited document.
    .\dist\windows\CyVR_Editor.exe --open my-layout.cyvr.json --screen 1 --snapshot components.png

    # Export the document, outline and all schematic screen images.
    .\dist\windows\CyVR_Editor.exe --open my-layout.cyvr.json --schematic --export-ai review.ai.json

File names passed through the command line and native dialogs support Unicode.

## Reuse a document in an application

Link the optional cyvr_layout CMake target. Parse once when loading an interface, then draw the selected screen with drawLayout. The same code works in a native 2D host and on an immersive panel. The stock Android reference does not automatically load design files.

Example, with document and activeScreen kept alive by the application:

    #include "cyvr/layout.hpp"

    cyvr::LayoutDocument document = cyvr::parseLayout(jsonText);
    size_t activeScreen = 0;

    ui.renderApplication = [&](cyvr::DrawList &draw) {
        draw.clear();
        ui.clearApplicationWidgets();
        cyvr::drawLayout(document.screens.at(activeScreen), draw, &ui,
                         false, ui.settings.language);
        return true;
    };
    ui.handleApplicationAction = [&](int action) {
        if (action == 100 && document.screens.size() > 1) {
            activeScreen = 1;
            ui.invalidate();
            return true;
        }
        return false;
    };

Existing action IDs are registered with the edited bounds, enabled and visibility state. Actions, live values, navigation and business logic remain application-owned. A snapshot of a toggle or slider is its captured visual state; it does not generate its dynamic behavior.

For an application-owned export:

    cyvr::LayoutDocument document;
    document.name = "My application";
    document.screens.push_back(
        cyvr::captureLayoutScreen(ui, "main", "Main screen"));
    cyvr::saveLayout("my-app.cyvr.json", document);

Set a text element's localizationKey to a key in the application catalogues to resolve it at rendering time. Literal captured text remains editable. Icon IDs follow the existing Icon enum; common values are 0 Home, 1 Components, 2 Settings, 3 Diagnostics, 7 Headset, 13 Camera and 26 Search.

## Format and initial limits

The format identifier is cyvr-ui-layout, version 1. Screen and element IDs are stable strings containing letters, digits, periods, hyphens or underscores. Elements are painted in array order; groups do not paint. References to parent IDs must name groups and cannot form a cycle.

Supported elements: group, rect, border, text, icon, logo, circle, ring, line, path, triangle, image, video and overlay. Path points are normalized to element bounds. Image/video/overlay captures use placeholders because borrowed application textures are not portable design assets.

The initial editor uses explicit positions. It does not yet implement automatic responsive constraints, rich text, clipping frames, animations, 3D scene editing or automatic C++ source rewriting. Saving a captured design does not silently change the compiled reference catalogue. Applications opt into document rendering through the API above; an assistant can also use the export to make corresponding changes in C++.

Documents are bounded to 8 MiB, 64 screens and 8,192 total elements. The parser rejects malformed JSON, duplicate object keys, invalid UTF-8, unsupported versions, invalid bounds and group cycles. Rendering has a vertex budget. Normal Quest rendering leaves authoring capture disabled, and the layout parser/editor are separate from the stock application target.

## Validation

The portable tests cover capture, JSON round trips, transformed groups and input regions, visibility, duplication/deletion, undo/redo, repeated saves and invalid documents. Vulkan tests compare document playback against the actual six reference states in English and French. The native editor test exercises Windows controls, property editing, clicks versus drags, undo/redo and canvas rendering. PNG/Markdown/JSON exports are exercised separately.
