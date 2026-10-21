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
  size_t katakana_word_end;  // end of the last katakana character following a hiragana or kanji one, 0 if none
};

struct SuffixCheck {
  bool in_source;              // the suffix ends the source as written, e.g. no emphatic っ was collapsed in it
  bool reaches_katakana_word;  // the suffix reaches into a katakana word following a hiragana or kanji one
};

namespace text_processor {
std::vector<TextVariant> process(const std::string& src);

SourceText normalize_source(const std::string& src);
// checks `suffix`, the trailing part of a variant of `source` rewritten by deinflection
SuffixCheck check_rewritten_suffix(const SourceText& source, std::string_view suffix);
}
