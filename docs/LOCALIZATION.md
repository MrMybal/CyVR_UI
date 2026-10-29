# Localization

English is the source language and the default for new or legacy preferences. French is included. Catalogues are UTF-8 JSON objects under `assets/locales`, with `_name` identifying the native language name. New catalogues are compiled into the library and appear automatically in the sidebar language selector.

`tools/generate_locales.py` produces a sorted C++ lookup table. Run it after catalogue changes; `--check` verifies that the checked-in table matches the source catalogues. The standard build script runs generation automatically. Direct CMake users may use the checked-in table or regenerate it first. No parser, network access or external localization library is required at runtime.

## API

```cpp
UiModel ui;
ui.setLanguage("fr");
const char *title = ui.tr("nav.home");
auto value = cyvr::formatNumber(ui.settings.language, 1.25, 2);
auto label = cyvr::formatMessage(ui.settings.language, "header.language",
                               {{"language", "English"}});
```

`resolveLocale()` accepts region variants and underscore separators, resolves the base language where supported and otherwise falls back to English. A missing message in a translated catalogue uses the English source value. A missing source key produces `??`; the generator rejects unknown literal keys used by the built-in UI.

Prefer named placeholders over concatenated translated fragments. Translated values must preserve source placeholder names. Formatting inserts argument values once and does not reinterpret braces inside them. Machine-readable JSON exports and technical event logs retain stable English wording and locale-independent field names; labels, status messages, toasts, numbers and HUD controls follow the UI language.

`UiModel::statusKey` provides a live translated built-in status. Applications can instead set `status` to display their own string, and use their own message keys in a catalogue. `renderApplication`, input hooks and custom workspace/context labels remain available to embedding applications.

The current font atlas supports Western European Latin text. The generator rejects uncovered characters so missing glyphs cannot silently ship in a new catalogue. Extend the font repertoire before adding another script. Right-to-left layout, bidirectional text, complex shaping and grammatical plural rules require additional work.
