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
  std::vector<bool> inside_katakana_word;  // see text_processor::inside_katakana_words
};

struct SuffixCheck {
  bool in_source;              // the suffix ends the source, apart from collapsed emphatic characters
  bool collapsed_sokuon;       // an emphatic っ was collapsed out of the suffix
  bool reaches_katakana_word;  // the suffix starts inside a katakana word following hiragana
};

namespace text_processor {
std::vector<TextVariant> process(const std::string& src);

// marks the characters past the first of each katakana word following hiragana; katakana following kanji may be
// okurigana (分カラナイ), so it doesn't start a word
std::vector<bool> inside_katakana_words(const std::u32string& text);

SourceText normalize_source(const std::string& src);
// checks `suffix`, the trailing part of a variant of `source` rewritten by deinflection
SuffixCheck check_rewritten_suffix(const SourceText& source, std::string_view suffix);
}
