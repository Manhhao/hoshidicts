#pragma once

#include <string>
#include <string_view>
#include <vector>

struct TextVariant {
  std::string text;
  int steps;
};

struct SourceText {
  std::u32string text;    // NFKC normalized source
  std::u32string folded;  // text with katakana and kanji variants folded
  // katakana words following hiragana
  std::vector<bool> inside_katakana_word;  // past the first character of such a word
  bool katakana_word_continues;            // the source ends inside such a word, which continues after it
};

struct SuffixCheck {
  bool in_source;              // the suffix ends the source, apart from collapsed emphatic characters
  bool collapsed_sokuon;       // an emphatic っ was collapsed out of the suffix
  bool reaches_katakana_word;  // the suffix takes part of a katakana word following hiragana, not all of it
};

namespace text_processor {
std::vector<TextVariant> process(const std::string& src);

// `next` is the character following `src` in the text, 0 if none
SourceText normalize_source(const std::string& src, char32_t next);
// checks `suffix`, the trailing part of a variant of `source` rewritten by deinflection
SuffixCheck check_rewritten_suffix(const SourceText& source, std::string_view suffix);
}
