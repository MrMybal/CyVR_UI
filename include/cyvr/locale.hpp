// SPDX-License-Identifier: MIT
#pragma once
#include <cstddef>
#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>

namespace cyvr {
struct LocaleInfo {
    const char *code;
    const char *nativeName;
};
const LocaleInfo *availableLocales();
size_t localeCount();
// Exact locale, then base language (fr-CA -> fr), then English.
const LocaleInfo &resolveLocale(std::string_view language);
const char *translate(std::string_view language, std::string_view key);
std::string formatNumber(std::string_view language, double value, int decimals = 1);
std::string formatMessage(std::string_view language, std::string_view key,
                          std::initializer_list<std::pair<std::string_view, std::string>> arguments);
} // namespace cyvr
