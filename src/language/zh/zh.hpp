#pragma once

#include "hoshidicts/language.hpp"

namespace language::zh {
const LanguageProcessor& get();
std::vector<TextVariant> preprocess(const std::string& text);
std::string normalize_radicals(const std::string& text);
std::string normalize_pinyin(const std::string& text);
bool is_string_partially_chinese(const std::string& text);
}
