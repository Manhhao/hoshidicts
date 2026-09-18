#include "text_processor.hpp"

#include <ankerl/unordered_dense.h>
#include <utf8.h>
#include <utf8proc.h>

#include <algorithm>
#include <cstdint>
#include <functional>
#include <ranges>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

extern const char32_t kanji_variants[][2];
extern const unsigned kanji_variants_count;

namespace {
struct TextProcessor {
  std::vector<int> options;
  std::function<void(const std::u32string&, int, std::u32string&)> process;
};

// https://github.com/yomidevs/yomitan/blob/81d17d877fb18c62ba826210bf6db2b7f4d4deed/ext/js/language/ja/japanese.js#L21
constexpr uint32_t KATAKANA_SMALL_KA = 0x30f5;
constexpr uint32_t KATAKANA_SMALL_KE = 0x30f6;
constexpr uint32_t KANA_PROLONGED_SOUND_MARK = 0x30fc;
constexpr uint32_t HIRAGANA_SMALL_TSU = 0x3063;
constexpr uint32_t KATAKANA_SMALL_TSU = 0x30c3;

constexpr char32_t KATAKANA_MIDDLE_DOT = 0x30fb;

constexpr uint32_t HIRAGANA_CONVERSION_RANGE_START = 0x3041;
constexpr uint32_t HIRAGANA_CONVERSION_RANGE_END = 0x3096;

constexpr uint32_t KATAKANA_CONVERSION_RANGE_START = 0x30a1;
constexpr uint32_t KATAKANA_CONVERSION_RANGE_END = 0x30f6;

constexpr char32_t KANJI_ITERATION_MARK = 0x3005;
constexpr char32_t HIRAGANA_ITERATION_MARK = 0x309d;
constexpr char32_t HIRAGANA_VOICED_ITERATION_MARK = 0x309e;
constexpr char32_t KATAKANA_ITERATION_MARK = 0x30fd;
constexpr char32_t KATAKANA_VOICED_ITERATION_MARK = 0x30fe;
constexpr char32_t DAKUTEN = 0x3099;

// https://github.com/yomidevs/yomitan/blob/81d17d877fb18c62ba826210bf6db2b7f4d4deed/ext/js/language/ja/japanese.js#L121
const std::unordered_map<char32_t, std::u32string> VOWEL_TO_KANA{
    {U'a', U"ぁあかがさざただなはばぱまゃやらゎわヵァアカガサザタダナハバパマャヤラヮワヵヷ"},
    {U'i', U"ぃいきぎしじちぢにひびぴみりゐィイキギシジチヂニヒビピミリヰヸ"},
    {U'u', U"ぅうくぐすずっつづぬふぶぷむゅゆるゥウクグスズッツヅヌフブプムュユルヴ"},
    {U'e', U"ぇえけげせぜてでねへべぺめれゑヶェエケゲセゼテデネヘベペメレヱヶヹ"},
    {U'o', U"ぉおこごそぞとどのほぼぽもょよろをォオコゴソゾトドノホボポモョヨロヲヺ"}};

// https://github.com/yomidevs/yomitan/blob/81d17d877fb18c62ba826210bf6db2b7f4d4deed/ext/js/language/ja/japanese.js#L131
std::unordered_map<char32_t, char32_t> build_kana_to_vowel_map() {
  std::unordered_map<char32_t, char32_t> map;
  for (const auto& [vowel, kana_string] : VOWEL_TO_KANA) {
    for (char32_t c : kana_string) {
      map.try_emplace(c, vowel);
    }
  }
  return map;
}

char32_t kana_to_vowel(char32_t kana) {
  static const auto KANA_TO_VOWEL = build_kana_to_vowel_map();
  auto it = KANA_TO_VOWEL.find(kana);
  if (it != KANA_TO_VOWEL.end()) {
    return it->second;
  }
  return 0;
}

// https://github.com/yomidevs/yomitan/blob/81d17d877fb18c62ba826210bf6db2b7f4d4deed/ext/js/language/ja/japanese.js#L155
char32_t get_prolonged_hiragana(char32_t prev) {
  switch (kana_to_vowel(prev)) {
    case U'a':
      return U'あ';
    case U'i':
      return U'い';
    case U'u':
      return U'う';
    case U'e':
      return U'え';
    case U'o':
      return U'う';
    default:
      return 0;
  }
}

bool is_in_range(uint32_t c, uint32_t range_start, uint32_t range_end) { return c >= range_start && c <= range_end; }

// https://github.com/yomidevs/yomitan/blob/81d17d877fb18c62ba826210bf6db2b7f4d4deed/ext/js/language/ja/japanese.js#L472
void hiragana_to_katakana(const std::u32string& text, std::u32string& result) {
  result.clear();
  result.reserve(text.size());
  const uint32_t offset = (KATAKANA_CONVERSION_RANGE_START - HIRAGANA_CONVERSION_RANGE_START);
  for (char32_t c : text) {
    if (is_in_range(c, HIRAGANA_CONVERSION_RANGE_START, HIRAGANA_CONVERSION_RANGE_END)) {
      c = static_cast<char32_t>(c + offset);
    }
    result += c;
  }
}

// https://github.com/yomidevs/yomitan/blob/81d17d877fb18c62ba826210bf6db2b7f4d4deed/ext/js/language/ja/japanese.js#L441
void katakana_to_hiragana(const std::u32string& text, std::u32string& result) {
  result.clear();
  result.reserve(text.size());
  const uint32_t offset = (HIRAGANA_CONVERSION_RANGE_START - KATAKANA_CONVERSION_RANGE_START);
  for (char32_t c : text) {
    switch (c) {
      case KATAKANA_SMALL_KA:
      case KATAKANA_SMALL_KE:
        break;
      case KANA_PROLONGED_SOUND_MARK:
        if (result.length() > 0) {
          const auto prolonged = get_prolonged_hiragana(result.at(result.length() - 1));
          if (prolonged != 0) {
            c = prolonged;
          }
        }
        break;
      default:
        if (is_in_range(c, KATAKANA_CONVERSION_RANGE_START, KATAKANA_CONVERSION_RANGE_END)) {
          c = static_cast<char32_t>(c + offset);
        }
        break;
    }
    result += c;
  }
}

bool is_emphatic(char32_t c) {
  return c == HIRAGANA_SMALL_TSU || c == KATAKANA_SMALL_TSU || c == KANA_PROLONGED_SOUND_MARK;
}

// https://github.com/yomidevs/yomitan/blob/81d17d877fb18c62ba826210bf6db2b7f4d4deed/ext/js/language/ja/japanese.js#L776
void collapse_emphatic_sequences(const std::u32string& text, bool full_collapse, std::u32string& result) {
  ptrdiff_t left = 0;
  while (left < static_cast<ptrdiff_t>(text.size()) && is_emphatic(text[left])) {
    ++left;
  }
  ptrdiff_t right = static_cast<ptrdiff_t>(text.size()) - 1;
  while (right >= 0 && is_emphatic(text[right])) {
    --right;
  }
  if (left > right) {
    result = text;
    return;
  }

  result.clear();
  result.reserve(text.size());
  result.append(text, 0, static_cast<size_t>(left));
  auto current_collapsed_code_point = static_cast<char32_t>(-1);

  for (ptrdiff_t i = left; i <= right; ++i) {
    char32_t c = text[i];
    if (is_emphatic(c)) {
      if (current_collapsed_code_point != c) {
        current_collapsed_code_point = c;
        if (!full_collapse) {
          result += c;
          continue;
        }
      }
    } else {
      current_collapsed_code_point = static_cast<char32_t>(-1);
      result += c;
    }
  }

  result.append(text, static_cast<size_t>(right + 1), std::u32string::npos);
}

void nfkc(const std::u32string& text, std::u32string& result) {
  std::string utf8 = utf8::utf32to8(text);
  utf8proc_uint8_t* out = utf8proc_NFKC(reinterpret_cast<const utf8proc_uint8_t*>(utf8.c_str()));
  if (!out) {
    result = text;
    return;
  }
  std::string composed(reinterpret_cast<char*>(out));
  utf8proc_free(out);
  result.clear();
  utf8::utf8to32(composed.begin(), composed.end(), std::back_inserter(result));
}

// https://github.com/yomidevs/yomitan/blob/3440451aecb23a43f308857969c890a55ce34a91/ext/js/language/ja/japanese.js#L489
void alphanumeric_to_fullwidth(const std::u32string& text, std::u32string& result) {
  result.clear();
  result.reserve(text.size());
  for (char32_t c : text) {
    if (is_in_range(c, U'0', U'9')) {
      c = static_cast<char32_t>(c + (0xff10 - 0x30));
    } else if (is_in_range(c, U'A', U'Z')) {
      c = static_cast<char32_t>(c + (0xff21 - 0x41));
    } else if (is_in_range(c, U'a', U'z')) {
      c = static_cast<char32_t>(c + (0xff41 - 0x61));
    }
    result += c;
  }
}

void standardize_kanji(const std::u32string& text, std::u32string& result) {
  static const auto map = [] {
    ankerl::unordered_dense::map<char32_t, char32_t> m;
    m.reserve(kanji_variants_count);
    for (unsigned i = 0; i < kanji_variants_count; ++i) {
      m[kanji_variants[i][0]] = kanji_variants[i][1];
    }
    return m;
  }();

  result.clear();
  result.reserve(text.size());
  for (char32_t c : text) {
    auto it = map.find(c);
    result += it != map.end() ? it->second : c;
  }
}

char32_t add_dakuten(char32_t kana) {
  std::u32string pair = {kana, DAKUTEN};
  std::string utf8 = utf8::utf32to8(pair);
  utf8proc_uint8_t* out = utf8proc_NFC(reinterpret_cast<const utf8proc_uint8_t*>(utf8.c_str()));
  if (!out) {
    return kana;
  }
  std::u32string composed = utf8::utf8to32(std::string(reinterpret_cast<char*>(out)));
  utf8proc_free(out);
  return composed.size() == 1 ? composed.front() : kana;
}

char32_t expand_mark(char32_t prev, char32_t mark) {
  switch (mark) {
    case KANJI_ITERATION_MARK:
    case HIRAGANA_ITERATION_MARK:
    case KATAKANA_ITERATION_MARK:
      return prev;
    case HIRAGANA_VOICED_ITERATION_MARK:
    case KATAKANA_VOICED_ITERATION_MARK:
      return add_dakuten(prev);
    default:
      return 0;
  }
}

void expand_iteration_marks(const std::u32string& text, std::u32string& result) {
  result.clear();
  result.reserve(text.size());
  for (size_t i = 0; i < text.size(); ++i) {
    result += text[i];
    if (i + 1 < text.size()) {
      char32_t expanded = expand_mark(text[i], text[i + 1]);
      if (expanded != 0) {
        result += expanded;
        ++i;
      }
    }
  }
}

constexpr std::u32string_view KANJI_NUMBERS = U"〇一二三四五六七八九";
void numbers_to_kanji(const std::u32string& text, std::u32string& result) {
  result.clear();
  result.reserve(text.size());
  for (char32_t c : text) {
    if (is_in_range(c, 0xff10, 0xff19)) {
      result += KANJI_NUMBERS[c - 0xff10];
    } else {
      result += c;
    }
  }
}

void strip_middle_dots(const std::u32string& text, std::u32string& result) {
  result.clear();
  result.reserve(text.size());
  for (char32_t c : text) {
    if (c != KATAKANA_MIDDLE_DOT) {
      result += c;
    }
  }
}

const std::vector<TextProcessor>& get_japanese_processors() {
  static const std::vector<TextProcessor> processors = {
      {.options = {0, 1},
       .process = [](const std::u32string& text, int opt, std::u32string& out) { nfkc(text, out); }},
      // https://github.com/yomidevs/yomitan/blob/81d17d877fb18c62ba826210bf6db2b7f4d4deed/ext/js/language/ja/japanese-text-preprocessors.js#L66
      {.options = {0, 1, 2},
       .process =
           [](const std::u32string& text, int opt, std::u32string& out) {
             if (opt == 1) {
               katakana_to_hiragana(text, out);
             } else {
               hiragana_to_katakana(text, out);
             }
           }},
      {.options = {0, 1, 2},
       .process =
           [](const std::u32string& text, int opt, std::u32string& out) {
             collapse_emphatic_sequences(text, opt == 2, out);
           }},
      {.options = {0, 1},
       .process = [](const std::u32string& text, int opt, std::u32string& out) { alphanumeric_to_fullwidth(text, out); }},
      {.options = {0, 1},
       .process = [](const std::u32string& text, int opt, std::u32string& out) { standardize_kanji(text, out); }},
      {.options = {0, 1},
       .process = [](const std::u32string& text, int opt, std::u32string& out) { expand_iteration_marks(text, out); }},
      {.options = {0, 1},
       .process = [](const std::u32string& text, int opt, std::u32string& out) { numbers_to_kanji(text, out); }},
      {.options = {0, 1}, .process = [](const std::u32string& text, int opt, std::u32string& out) {
         strip_middle_dots(text, out);
       }}};
  return processors;
}
}

// https://github.com/yomidevs/yomitan/blob/81d17d877fb18c62ba826210bf6db2b7f4d4deed/ext/js/language/translator.js#L564
std::vector<TextVariant> text_processor::process(const std::string& src) {
  std::u32string text = utf8::utf8to32(src);
  std::vector<std::pair<std::u32string, int>> variants;
  variants.emplace_back(std::move(text), 0);

  std::u32string scratch;
  for (const auto& processor : get_japanese_processors()) {
    std::vector<std::pair<std::u32string, int>> next;
    next.reserve(variants.size() * processor.options.size());

    for (auto& [variant, steps] : variants) {
      for (int option : processor.options) {
        if (option == 0) {
          continue;
        }
        processor.process(variant, option, scratch);
        int new_steps = scratch == variant ? steps : steps + 1;

        auto it = std::ranges::find_if(next, [&](const auto& entry) { return entry.first == scratch; });
        if (it == next.end()) {
          next.emplace_back(scratch, new_steps);
        } else if (new_steps < it->second) {
          it->second = new_steps;
        }
      }

      auto it = std::ranges::find_if(next, [&](const auto& entry) { return entry.first == variant; });
      if (it == next.end()) {
        next.emplace_back(std::move(variant), steps);
      } else if (steps < it->second) {
        it->second = steps;
      }
    }
    std::ranges::sort(next, {}, &std::pair<std::u32string, int>::first);
    variants = std::move(next);
  }

  std::vector<TextVariant> result;
  result.reserve(variants.size());
  for (const auto& [variant, steps] : variants) {
    result.emplace_back(utf8::utf32to8(variant), steps);
  }
  return result;
}
