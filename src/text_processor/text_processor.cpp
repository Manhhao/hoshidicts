#include "text_processor.hpp"

#include <ankerl/unordered_dense.h>
#include <utf8.h>
#include <utf8proc.h>

#include <array>
#include <cstdint>
#include <functional>
#include <glaze/glaze.hpp>
#include <map>
#include <ranges>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace internal {
struct KanjiMapping {
  std::string oyaji;
  std::vector<std::string> itaiji;
};
}

namespace {
struct TextProcessor {
  std::vector<int> options;
  std::function<std::u32string(const std::u32string&, int)> process;
};

// https://github.com/yomidevs/yomitan/blob/81d17d877fb18c62ba826210bf6db2b7f4d4deed/ext/js/language/ja/japanese.js#L21
constexpr uint32_t KATAKANA_SMALL_KA = 0x30f5;
constexpr uint32_t KATAKANA_SMALL_KE = 0x30f6;
constexpr uint32_t KANA_PROLONGED_SOUND_MARK = 0x30fc;
constexpr uint32_t HIRAGANA_SMALL_TSU = 0x3063;
constexpr uint32_t KATAKANA_SMALL_TSU = 0x30c3;

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
std::u32string hiragana_to_katakana(const std::u32string& text) {
  std::u32string result;
  const uint32_t offset = (KATAKANA_CONVERSION_RANGE_START - HIRAGANA_CONVERSION_RANGE_START);
  for (char32_t c : text) {
    if (is_in_range(c, HIRAGANA_CONVERSION_RANGE_START, HIRAGANA_CONVERSION_RANGE_END)) {
      c = static_cast<char32_t>(c + offset);
    }
    result += c;
  }
  return result;
}

// https://github.com/yomidevs/yomitan/blob/81d17d877fb18c62ba826210bf6db2b7f4d4deed/ext/js/language/ja/japanese.js#L441
std::u32string katakana_to_hiragana(const std::u32string& text) {
  std::u32string result;
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
  return result;
}

bool is_emphatic(char32_t c) {
  return c == HIRAGANA_SMALL_TSU || c == KATAKANA_SMALL_TSU || c == KANA_PROLONGED_SOUND_MARK;
}

// https://github.com/yomidevs/yomitan/blob/81d17d877fb18c62ba826210bf6db2b7f4d4deed/ext/js/language/ja/japanese.js#L776
std::u32string collapse_emphatic_sequences(const std::u32string& text, bool full_collapse) {
  ptrdiff_t left = 0;
  while (left < static_cast<ptrdiff_t>(text.size()) && is_emphatic(text[left])) {
    ++left;
  }
  ptrdiff_t right = static_cast<ptrdiff_t>(text.size()) - 1;
  while (right >= 0 && is_emphatic(text[right])) {
    --right;
  }
  if (left > right) {
    return text;
  }

  std::u32string leading_emphatics = text.substr(0, left);
  std::u32string trailing_emphatics = text.substr(right + 1);
  std::u32string middle;
  auto current_collapsed_code_point = static_cast<char32_t>(-1);

  for (ptrdiff_t i = left; i <= right; ++i) {
    char32_t c = text[i];
    if (is_emphatic(c)) {
      if (current_collapsed_code_point != c) {
        current_collapsed_code_point = c;
        if (!full_collapse) {
          middle += c;
          continue;
        }
      }
    } else {
      current_collapsed_code_point = static_cast<char32_t>(-1);
      middle += c;
    }
  }

  return leading_emphatics + middle + trailing_emphatics;
}

std::u32string nfkc(const std::u32string& text) {
  std::string utf8 = utf8::utf32to8(text);
  utf8proc_uint8_t* out = utf8proc_NFKC(reinterpret_cast<const utf8proc_uint8_t*>(utf8.c_str()));
  if (!out) {
    return text;
  }
  std::string result(reinterpret_cast<char*>(out));
  utf8proc_free(out);
  return utf8::utf8to32(result);
}

// https://github.com/yomidevs/yomitan/blob/3440451aecb23a43f308857969c890a55ce34a91/ext/js/language/ja/japanese.js#L489
std::u32string alphanumeric_to_fullwidth(const std::u32string& text) {
  std::u32string result;
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
  return result;
}

constexpr auto mapping_list = std::to_array<unsigned char>({
#embed "../../external/kanji-processor/src/full_list.json"
});

std::u32string standardize_kanji(const std::u32string& text) {
  static const auto map = [] {
    std::vector<internal::KanjiMapping> list;
    if (glz::read_json(list,
                       std::string_view{reinterpret_cast<const char*>(mapping_list.data()), mapping_list.size()})) {
      return ankerl::unordered_dense::map<char32_t, char32_t>{};
    };

    ankerl::unordered_dense::map<char32_t, char32_t> m;
    for (const auto& [oyaji, itaiji] : list) {
      const char32_t parent = utf8::utf8to32(oyaji).front();
      for (const auto& variant : itaiji) {
        m[utf8::utf8to32(variant).front()] = parent;
      }
    }
    return m;
  }();

  std::u32string result;
  for (char32_t c : text) {
    auto it = map.find(c);
    result += it != map.end() ? it->second : c;
  }
  return result;
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

std::u32string expand_iteration_marks(const std::u32string& text) {
  std::u32string result;
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
  return result;
}

constexpr std::u32string_view KANJI_NUMBERS = U"〇一二三四五六七八九";
std::u32string numbers_to_kanji(const std::u32string& text) {
  std::u32string result;
  for (char32_t c : text) {
    if (is_in_range(c, 0xff10, 0xff19)) {
      result += KANJI_NUMBERS[c - 0xff10];
    } else {
      result += c;
    }
  }
  return result;
}

// TODO: implement rest of preprocessors
std::vector<TextProcessor> get_japanese_processors() {
  return {
      // https://github.com/yomidevs/yomitan/blob/81d17d877fb18c62ba826210bf6db2b7f4d4deed/ext/js/language/ja/japanese-text-preprocessors.js#L66
      {.options = {0, 1, 2},
       .process = [](const std::u32string& text, int opt) -> std::u32string {
         switch (opt) {
           case 1:
             return katakana_to_hiragana(text);
           case 2:
             return hiragana_to_katakana(text);
           default:
             return text;
         }
       }},
      {.options = {0, 1, 2},
       .process = [](const std::u32string& text, int opt) -> std::u32string {
         switch (opt) {
           case 1:
             return collapse_emphatic_sequences(text, false);
           case 2:
             return collapse_emphatic_sequences(text, true);
           default:
             return text;
         }
       }},
      {.options = {0, 1},
       .process = [](const std::u32string& text, int opt) -> std::u32string { return opt == 1 ? nfkc(text) : text; }},
      {.options = {0, 1},
       .process = [](const std::u32string& text, int opt) -> std::u32string {
         return opt == 1 ? alphanumeric_to_fullwidth(text) : text;
       }},
      {.options = {0, 1},
       .process = [](const std::u32string& text, int opt) -> std::u32string {
         return opt == 1 ? standardize_kanji(text) : text;
       }},
      {.options = {0, 1},
       .process = [](const std::u32string& text, int opt) -> std::u32string {
         return opt == 1 ? expand_iteration_marks(text) : text;
       }},
      {.options = {0, 1}, .process = [](const std::u32string& text, int opt) -> std::u32string {
         return opt == 1 ? numbers_to_kanji(text) : text;
       }}};
}
}

// https://github.com/yomidevs/yomitan/blob/81d17d877fb18c62ba826210bf6db2b7f4d4deed/ext/js/language/translator.js#L564
std::vector<TextVariant> text_processor::process(const std::string& src) {
  std::u32string text = utf8::utf8to32(src);
  std::map<std::u32string, int> variants = {{text, 0}};

  for (const auto& processor : get_japanese_processors()) {
    std::map<std::u32string, int> next;

    for (const auto& [variant, steps] : variants) {
      for (int option : processor.options) {
        auto processed = processor.process(variant, option);
        int new_steps = (processed == variant) ? steps : steps + 1;

        auto [it, inserted] = next.try_emplace(processed, new_steps);
        if (!inserted && new_steps < it->second) {
          it->second = new_steps;
        }
      }
    }
    variants = std::move(next);
  }

  return variants |
         std::views::transform([](const auto& v) { return TextVariant{utf8::utf32to8(v.first), v.second}; }) |
         std::ranges::to<std::vector>();
}
