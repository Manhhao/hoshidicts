#include <ankerl/unordered_dense.h>
#include <utf8.h>
#include <utf8proc.h>

#include <cstdint>
#include <functional>
#include <glaze/glaze.hpp>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "../text_variants.hpp"
#include "ja.hpp"

namespace japanese_language {
struct KanjiMapping {
  std::string oyaji;
  std::vector<std::string> itaiji;
};
}

namespace {

// https://github.com/yomidevs/yomitan/blob/81d17d877fb18c62ba826210bf6db2b7f4d4deed/ext/js/language/ja/japanese.js#L21
constexpr uint32_t KATAKANA_SMALL_KA = 0x30f5;
constexpr uint32_t KATAKANA_SMALL_KE = 0x30f6;
constexpr uint32_t KANA_PROLONGED_SOUND_MARK = 0x30fc;

constexpr uint32_t HIRAGANA_CONVERSION_RANGE_START = 0x3041;
constexpr uint32_t HIRAGANA_CONVERSION_RANGE_END = 0x3096;

constexpr uint32_t KATAKANA_CONVERSION_RANGE_START = 0x30a1;
constexpr uint32_t KATAKANA_CONVERSION_RANGE_END = 0x30f6;

// https://github.com/yomidevs/yomitan/blob/81d17d877fb18c62ba826210bf6db2b7f4d4deed/ext/js/language/ja/japanese.js#L121
const std::unordered_map<char32_t, std::u32string> VOWEL_TO_KANA{
    {U'a', U"ぁあかがさざただなはばぱまゃやらゎわヵァアカガサザタダナハバパマャヤラヮワヵヷ"},
    {U'i', U"ぃいきぎしじちぢにひびぴみりゐィイキギシジチヂニヒビピミリヰヸ"},
    {U'u', U"ぅうくぐすずっつづぬふぶぷむゅゆるゥウクグスズッツヅヌフブプムュユルヴ"},
    {U'e', U"ぇえけげせぜてでねへべぺめれゑヶェエケゲセゼテデネヘベペメレヱヶヹ"},
    {U'o', U"ぉおこごそぞとどのほぼぽもょよろをォオコゴソゾトドノホボポモョヨロヲヺ"}};

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
        if (!result.empty()) {
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

constexpr unsigned char mapping_list[] = {
#embed "../../../external/kanji-processor/src/full_list.json"
};

std::u32string standardize_kanji(const std::u32string& text) {
  static const auto map = [] {
    std::vector<japanese_language::KanjiMapping> list;
    if (glz::read_json(list, std::string_view{reinterpret_cast<const char*>(mapping_list), sizeof(mapping_list)})) {
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

std::string process_u32(const std::string& src, const std::function<std::u32string(const std::u32string&)>& process) {
  return utf8::utf32to8(process(utf8::utf8to32(src)));
}

std::vector<language::internal::TextProcessorDefinition> get_japanese_processors() {
  return {{.process =
               [](const std::string& text) {
                 return std::vector<std::string>{text, process_u32(text, katakana_to_hiragana),
                                                 process_u32(text, hiragana_to_katakana)};
               }},
          {.process = [](const std::string& text) { return std::vector<std::string>{text, process_u32(text, nfkc)}; }},
          {.process =
               [](const std::string& text) {
                 return std::vector<std::string>{text, process_u32(text, alphanumeric_to_fullwidth)};
               }},
          {.process = [](const std::string& text) {
            return std::vector<std::string>{text, process_u32(text, standardize_kanji)};
          }}};
}

}

namespace language::ja {

std::vector<TextVariant> preprocess(const std::string& text) {
  static const auto processors = get_japanese_processors();
  return language::internal::process_text_variants(text, processors);
}

std::vector<TextVariant> postprocess(const std::string& text) { return {{.text = text, .steps = 0}}; }

}
