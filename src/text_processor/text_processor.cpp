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
  std::function<std::u32string(const std::u32string&, int)> process;
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
std::u32string hiragana_to_katakana(const std::u32string& text) {
  std::u32string result;
  result.reserve(text.size());
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
  middle.reserve(static_cast<size_t>(right - left + 1));
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
  return result;
}

std::u32string standardize_kanji(const std::u32string& text) {
  static const auto map = [] {
    ankerl::unordered_dense::map<char32_t, char32_t> m;
    m.reserve(kanji_variants_count);
    for (unsigned i = 0; i < kanji_variants_count; ++i) {
      m[kanji_variants[i][0]] = kanji_variants[i][1];
    }
    return m;
  }();

  std::u32string result;
  result.reserve(text.size());
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
  return result;
}

constexpr std::u32string_view KANJI_NUMBERS = U"〇一二三四五六七八九";
std::u32string numbers_to_kanji(const std::u32string& text) {
  std::u32string result;
  result.reserve(text.size());
  for (char32_t c : text) {
    if (is_in_range(c, 0xff10, 0xff19)) {
      result += KANJI_NUMBERS[c - 0xff10];
    } else {
      result += c;
    }
  }
  return result;
}

std::u32string strip_middle_dots(const std::u32string& text) {
  std::u32string result;
  result.reserve(text.size());
  for (char32_t c : text) {
    if (c != KATAKANA_MIDDLE_DOT) {
      result += c;
    }
  }
  return result;
}

const std::vector<TextProcessor>& get_japanese_processors() {
  static const std::vector<TextProcessor> processors = {
      {.options = {0, 1},
       .process = [](const std::u32string& text, int opt) -> std::u32string { return opt == 1 ? nfkc(text) : text; }},
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
      {.options = {0, 1},
       .process = [](const std::u32string& text, int opt) -> std::u32string {
         return opt == 1 ? numbers_to_kanji(text) : text;
       }},
      {.options = {0, 1}, .process = [](const std::u32string& text, int opt) -> std::u32string {
         return opt == 1 ? strip_middle_dots(text) : text;
       }}};
  return processors;
}

std::u32string fold(const std::u32string& text) { return standardize_kanji(katakana_to_hiragana(text)); }

bool is_hiragana(char32_t c) { return is_in_range(c, 0x3041, 0x309f); }

bool is_katakana(char32_t c) { return is_in_range(c, 0x30a1, 0x30fa); }

// fold() for a single character of a deinflection rule, which never contains a prolonged sound mark
char32_t fold_char(char32_t c) { return is_hiragana(c) ? c : fold(std::u32string(1, c)).front(); }
}

SourceText text_processor::normalize_source(const std::string& src, char32_t next) {
  std::u32string text = nfkc(utf8::utf8to32(src));
  std::u32string folded = fold(text);

  // katakana following hiragana starts a new word, katakana following kanji may be okurigana (分カラナイ)
  std::vector<bool> inside_katakana_word(text.size());
  bool katakana = false;
  bool word = false;
  for (size_t i = 0; i < text.size(); ++i) {
    bool was_katakana = katakana;
    katakana = is_katakana(text[i]) || (was_katakana && text[i] == KANA_PROLONGED_SOUND_MARK);
    if (katakana && !was_katakana) {
      word = i > 0 && is_hiragana(text[i - 1]);
    } else {
      inside_katakana_word[i] = katakana && word;
    }
    word = word && katakana;
  }
  bool continues = word && (is_katakana(next) || next == KANA_PROLONGED_SOUND_MARK || is_in_range(next, 0xff66, 0xff9f));

  return {.text = std::move(text),
          .folded = std::move(folded),
          .inside_katakana_word = std::move(inside_katakana_word),
          .katakana_word_continues = continues};
}

SuffixCheck text_processor::check_rewritten_suffix(const SourceText& source, std::string_view suffix) {
  // align the suffix with the end of the source, skipping emphatic characters collapsed out of it
  SuffixCheck check{.in_source = true, .collapsed_sokuon = false, .reaches_katakana_word = false};
  size_t start = source.text.size();
  for (auto it = suffix.end(); check.in_source && it != suffix.begin();) {
    char32_t c = fold_char(utf8::prior(it, suffix.begin()));
    while (start > 0 && source.folded[start - 1] != c && is_emphatic(source.text[start - 1])) {
      check.collapsed_sokuon = check.collapsed_sokuon || source.text[start - 1] != KANA_PROLONGED_SOUND_MARK;
      --start;
    }
    check.in_source = start > 0 && source.folded[start - 1] == c;
    start -= check.in_source ? 1 : 0;
  }
  // the stem takes part of the word (はグラン -> はぐる), or the word goes on past the ending (老いてコン|ビニ)
  check.reaches_katakana_word = start < source.text.size() &&
                                (source.inside_katakana_word[start] || source.katakana_word_continues);
  return check;
}

// https://github.com/yomidevs/yomitan/blob/81d17d877fb18c62ba826210bf6db2b7f4d4deed/ext/js/language/translator.js#L564
std::vector<TextVariant> text_processor::process(const std::string& src) {
  std::u32string text = utf8::utf8to32(src);
  std::vector<std::pair<std::u32string, int>> variants;
  variants.emplace_back(std::move(text), 0);

  for (const auto& processor : get_japanese_processors()) {
    std::vector<std::pair<std::u32string, int>> next;
    next.reserve(variants.size() * processor.options.size());

    for (const auto& [variant, steps] : variants) {
      for (int option : processor.options) {
        auto processed = option == 0 ? variant : processor.process(variant, option);
        int new_steps = (option == 0 || processed == variant) ? steps : steps + 1;

        auto it = std::ranges::find_if(next, [&](const auto& entry) { return entry.first == processed; });
        if (it == next.end()) {
          next.emplace_back(std::move(processed), new_steps);
        } else if (new_steps < it->second) {
          it->second = new_steps;
        }
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
