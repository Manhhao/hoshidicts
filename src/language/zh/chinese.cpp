// Chinese ranges, radical processing and pinyin normalization adapted from Yomitan (GPLv3):
// https://github.com/yomidevs/yomitan/tree/0273cbb33d774861d2470be62623dde56c8059d6/ext/js/language
// See zh/chinese.js and CJK-util.js. Copyright (C) 2024-2026 Yomitan Authors.
#include <utf8.h>
#include <utf8proc.h>

#include <algorithm>
#include <memory>
#include <span>
#include <stdexcept>
#include <string_view>

#include "zh.hpp"

namespace {

struct CodepointRange {
  char32_t first;
  char32_t last;
};

bool in_ranges(char32_t codepoint, std::span<const CodepointRange> ranges) {
  return std::ranges::any_of(
      ranges, [codepoint](const auto& range) { return codepoint >= range.first && codepoint <= range.last; });
}

constexpr CodepointRange CHINESE_RANGES[] = {
    {0x4e00, 0x9fff},   {0x3400, 0x4dbf},   {0x20000, 0x2a6df}, {0x2a700, 0x2b73f}, {0x2b740, 0x2b81f},
    {0x2b820, 0x2ceaf}, {0x2ceb0, 0x2ebef}, {0x30000, 0x3134f}, {0x31350, 0x323af}, {0x2ebf0, 0x2ee5f},
    {0x323b0, 0x3347f}, {0xf900, 0xfaff},   {0x2f800, 0x2fa1f}, {0x3000, 0x303f},   {0xff10, 0xff19},
    {0xff21, 0xff3a},   {0xff41, 0xff5a},   {0xff01, 0xff0f},   {0xff1a, 0xff1f},   {0xff3b, 0xff3f},
    {0xff5b, 0xff60},   {0xffe0, 0xffee},   {0x3100, 0x312f},   {0x31a0, 0x31bf},   {0x16fe0, 0x16fff},
    {0xfe50, 0xfe6f},   {0xfe10, 0xfe1f},
};

constexpr CodepointRange RADICAL_RANGES[] = {{0x2f00, 0x2fdf}, {0x2e80, 0x2eff}, {0x31c0, 0x31ef}};

// Unicode 17.0 DerivedCoreProperties, matching the bundled utf8proc data.
// These are the Cased characters outside Lu/Ll/Lt and the Case_Ignorable
// characters outside Mn/Me/Cf/Lm/Sk. Both properties are needed for Final_Sigma;
// utf8proc's "ignorable" flag is the different Default_Ignorable_Code_Point property.
constexpr CodepointRange CASED_EXTRA[] = {
    {0xaa, 0xaa},       {0xba, 0xba},       {0x2b0, 0x2b8},     {0x2c0, 0x2c1},     {0x2e0, 0x2e4},
    {0x345, 0x345},     {0x37a, 0x37a},     {0x10fc, 0x10fc},   {0x1d2c, 0x1d6a},   {0x1d78, 0x1d78},
    {0x1d9b, 0x1dbf},   {0x2071, 0x2071},   {0x207f, 0x207f},   {0x2090, 0x209c},   {0x2160, 0x217f},
    {0x24b6, 0x24e9},   {0x2c7c, 0x2c7d},   {0xa69c, 0xa69d},   {0xa770, 0xa770},   {0xa7f1, 0xa7f4},
    {0xa7f8, 0xa7f9},   {0xab5c, 0xab5f},   {0xab69, 0xab69},   {0x10780, 0x10780}, {0x10783, 0x10785},
    {0x10787, 0x107b0}, {0x107b2, 0x107ba}, {0x1e030, 0x1e06d}, {0x1f130, 0x1f149}, {0x1f150, 0x1f169},
    {0x1f170, 0x1f189},
};

constexpr CodepointRange CASE_IGNORABLE_EXTRA[] = {
    {0x27, 0x27},     {0x2e, 0x2e},     {0x3a, 0x3a},     {0xb7, 0xb7},     {0x387, 0x387},   {0x55f, 0x55f},
    {0x5f4, 0x5f4},   {0x2018, 0x2019}, {0x2024, 0x2024}, {0x2027, 0x2027}, {0xfe13, 0xfe13}, {0xfe52, 0xfe52},
    {0xfe55, 0xfe55}, {0xff07, 0xff07}, {0xff0e, 0xff0e}, {0xff1a, 0xff1a},
};

bool is_cased(char32_t codepoint) {
  const auto category = utf8proc_category(static_cast<utf8proc_int32_t>(codepoint));
  return category == UTF8PROC_CATEGORY_LU || category == UTF8PROC_CATEGORY_LL || category == UTF8PROC_CATEGORY_LT ||
         in_ranges(codepoint, CASED_EXTRA);
}

bool is_case_ignorable(char32_t codepoint) {
  const auto category = utf8proc_category(static_cast<utf8proc_int32_t>(codepoint));
  return category == UTF8PROC_CATEGORY_MN || category == UTF8PROC_CATEGORY_ME || category == UTF8PROC_CATEGORY_CF ||
         category == UTF8PROC_CATEGORY_LM || category == UTF8PROC_CATEGORY_SK ||
         in_ranges(codepoint, CASE_IGNORABLE_EXTRA);
}

bool is_final_sigma(std::u32string_view text, size_t index) {
  size_t before = index;
  while (before > 0 && is_case_ignorable(text[before - 1])) {
    --before;
  }
  if (before == 0 || !is_cased(text[before - 1])) {
    return false;
  }
  size_t after = index + 1;
  while (after < text.size() && is_case_ignorable(text[after])) {
    ++after;
  }
  return after == text.size() || !is_cased(text[after]);
}

std::string normalize_unicode(std::string_view text, utf8proc_option_t options) {
  utf8proc_uint8_t* buffer = nullptr;
  const auto size = utf8proc_map(reinterpret_cast<const utf8proc_uint8_t*>(text.data()),
                                 static_cast<utf8proc_ssize_t>(text.size()), &buffer, options);
  std::unique_ptr<utf8proc_uint8_t, decltype(&utf8proc_free)> output(buffer, &utf8proc_free);
  if (size < 0) {
    throw std::runtime_error(utf8proc_errmsg(size));
  }
  return {reinterpret_cast<const char*>(output.get()), static_cast<size_t>(size)};
}

std::u32string unicode_lower(std::u32string_view text) {
  std::u32string result;
  result.reserve(text.size());
  for (size_t i = 0; i < text.size(); ++i) {
    const char32_t codepoint = text[i];
    // Default (locale-independent) SpecialCasing rules used by JavaScript toLowerCase.
    if (codepoint == 0x130) {
      result += U"i\u0307";
    } else if (codepoint == 0x3a3 && is_final_sigma(text, i)) {
      result += U'\u03c2';
    } else {
      result += static_cast<char32_t>(utf8proc_tolower(static_cast<utf8proc_int32_t>(codepoint)));
    }
  }
  return result;
}

bool is_pinyin_separator(char32_t codepoint) {
  // ECMAScript \s includes BOM but excludes U+0085; do not use locale whitespace or Unicode White_Space.
  return (codepoint >= 0x9 && codepoint <= 0xd) || codepoint == 0x20 || codepoint == 0xa0 || codepoint == 0x1680 ||
         (codepoint >= 0x2000 && codepoint <= 0x200a) || codepoint == 0x2028 || codepoint == 0x2029 ||
         codepoint == 0x202f || codepoint == 0x205f || codepoint == 0x3000 || codepoint == 0xfeff ||
         codepoint == U'・' || codepoint == U':' || codepoint == U'\'' || codepoint == U'’' || codepoint == U'-';
}

}

namespace language::zh {

std::string normalize_radicals(const std::string& text) {
  std::string result;
  result.reserve(text.size());
  auto it = text.begin();
  while (it != text.end()) {
    const auto start = it;
    const auto codepoint = utf8::next(it, text.end());
    const std::string_view character(&*start, static_cast<size_t>(it - start));
    if (in_ranges(codepoint, RADICAL_RANGES)) {
      result += normalize_unicode(
          character, static_cast<utf8proc_option_t>(UTF8PROC_STABLE | UTF8PROC_COMPAT | UTF8PROC_DECOMPOSE));
    } else {
      result += character;
    }
  }
  return result;
}

std::string normalize_pinyin(const std::string& text) {
  const auto nfc = normalize_unicode(text, static_cast<utf8proc_option_t>(UTF8PROC_STABLE | UTF8PROC_COMPOSE));
  const auto lower = unicode_lower(utf8::utf8to32(nfc));
  std::u32string result;
  result.reserve(lower.size());
  for (size_t i = 0; i < lower.size(); ++i) {
    if (is_pinyin_separator(lower[i])) {
      continue;
    }
    if (lower[i] == U'/' && i + 1 < lower.size() && lower[i + 1] == U'/') {
      ++i;
      continue;
    }
    result += lower[i];
  }
  return utf8::utf32to8(result);
}

bool is_string_partially_chinese(const std::string& text) {
  auto it = text.begin();
  while (it != text.end()) {
    if (in_ranges(utf8::next(it, text.end()), CHINESE_RANGES)) {
      return true;
    }
  }
  return false;
}

}
