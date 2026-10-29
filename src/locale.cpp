// SPDX-License-Identifier: MIT
#include "cyvr/locale.hpp"
#include "cyvr/locale_data.hpp"
#include <algorithm>
#include <iomanip>
#include <locale>
#include <sstream>

namespace cyvr {
const LocaleInfo *availableLocales() {
    return locale_data::locales;
}
size_t localeCount() {
    return std::size(locale_data::locales);
}
const LocaleInfo &resolveLocale(std::string_view language) {
    std::string normalized(language);
    for (auto &c : normalized) {
        if (c >= 'A' && c <= 'Z')
            c = char(c - 'A' + 'a');
        if (c == '_')
            c = '-';
    }
    for (auto &locale : locale_data::locales)
        if (normalized == locale.code)
            return locale;
    auto split = normalized.find('-');
    if (split != std::string::npos)
        normalized.resize(split);
    for (auto &locale : locale_data::locales)
        if (normalized == locale.code)
            return locale;
    return locale_data::locales[0];
}
const char *translate(std::string_view language, std::string_view key) {
    const auto *first = std::begin(locale_data::messages), *last = std::end(locale_data::messages);
    auto message =
        std::lower_bound(first, last, key, [](auto &entry, std::string_view k) { return entry.key < k; });
    if (message == last || message->key != key)
        return "??";
    size_t index = size_t(&resolveLocale(language) - availableLocales());
    return message->text[index] ? message->text[index] : message->text[0];
}
std::string formatNumber(std::string_view language, double value, int decimals) {
    std::ostringstream stream;
    stream.imbue(std::locale::classic());
    stream << std::fixed << std::setprecision(std::clamp(decimals, 0, 3)) << value;
    auto result = stream.str();
    if (std::string_view(resolveLocale(language).code) == "fr")
        std::replace(result.begin(), result.end(), '.', ',');
    return result;
}
std::string formatMessage(std::string_view language, std::string_view key,
                          std::initializer_list<std::pair<std::string_view, std::string>> arguments) {
    std::string text = translate(language, key), result;
    // Single pass: inserted values are never reinterpreted as placeholders.
    for (size_t p = 0; p < text.size();) {
        if (text[p] == '{') {
            size_t end = text.find('}', p + 1);
            if (end != std::string::npos) {
                auto name = std::string_view(text).substr(p + 1, end - p - 1);
                bool found = false;
                for (auto &argument : arguments)
                    if (argument.first == name) {
                        result += argument.second;
                        found = true;
                        break;
                    }
                if (found) {
                    p = end + 1;
                    continue;
                }
            }
        }
        result += text[p++];
    }
    return result;
}
} // namespace cyvr
