/*
 * Adapted from hangul-js 0.2.6, as bundled by Yomitan at 0273cbb3.
 * Copyright (c) 2012-2018 Jaemin Jo
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */
#include <utf8.h>

#include <cstddef>
#include <span>
#include <string_view>

#include "ko.hpp"

namespace {

constexpr std::u32string_view CHO = U"ㄱㄲㄴㄷㄸㄹㅁㅂㅃㅅㅆㅇㅈㅉㅊㅋㅌㅍㅎ";
constexpr std::u32string_view JUNG = U"ㅏㅐㅑㅒㅓㅔㅕㅖㅗㅘㅙㅚㅛㅜㅝㅞㅟㅠㅡㅢㅣ";
constexpr std::u32string_view JONG{U"\0ㄱㄲㄳㄴㄵㄶㄷㄹㄺㄻㄼㄽㄾㄿㅀㅁㅂㅄㅅㅆㅇㅈㅊㅋㅌㅍㅎ", 28};
constexpr std::u32string_view JUNG_PARTS[] = {U"ㅏ",   U"ㅐ",   U"ㅑ",   U"ㅒ",   U"ㅓ",   U"ㅔ",   U"ㅕ",
                                              U"ㅖ",   U"ㅗ",   U"ㅗㅏ", U"ㅗㅐ", U"ㅗㅣ", U"ㅛ",   U"ㅜ",
                                              U"ㅜㅓ", U"ㅜㅔ", U"ㅜㅣ", U"ㅠ",   U"ㅡ",   U"ㅡㅣ", U"ㅣ"};
constexpr std::u32string_view JONG_PARTS[] = {U"",     U"ㄱ",   U"ㄲ",   U"ㄱㅅ", U"ㄴ",   U"ㄴㅈ", U"ㄴㅎ",
                                              U"ㄷ",   U"ㄹ",   U"ㄹㄱ", U"ㄹㅁ", U"ㄹㅂ", U"ㄹㅅ", U"ㄹㅌ",
                                              U"ㄹㅍ", U"ㄹㅎ", U"ㅁ",   U"ㅂ",   U"ㅂㅅ", U"ㅅ",   U"ㅆ",
                                              U"ㅇ",   U"ㅈ",   U"ㅊ",   U"ㅋ",   U"ㅌ",   U"ㅍ",   U"ㅎ"};

struct JamoPair {
  char32_t first;
  char32_t second;
  char32_t combined;
};
constexpr JamoPair COMPLEX_CONSONANTS[] = {
    {U'ㄱ', U'ㅅ', U'ㄳ'}, {U'ㄴ', U'ㅈ', U'ㄵ'}, {U'ㄴ', U'ㅎ', U'ㄶ'}, {U'ㄹ', U'ㄱ', U'ㄺ'},
    {U'ㄹ', U'ㅁ', U'ㄻ'}, {U'ㄹ', U'ㅂ', U'ㄼ'}, {U'ㄹ', U'ㅅ', U'ㄽ'}, {U'ㄹ', U'ㅌ', U'ㄾ'},
    {U'ㄹ', U'ㅍ', U'ㄿ'}, {U'ㄹ', U'ㅎ', U'ㅀ'}, {U'ㅂ', U'ㅅ', U'ㅄ'},
};
constexpr JamoPair COMPLEX_VOWELS[] = {
    {U'ㅗ', U'ㅏ', U'ㅘ'}, {U'ㅗ', U'ㅐ', U'ㅙ'}, {U'ㅗ', U'ㅣ', U'ㅚ'}, {U'ㅜ', U'ㅓ', U'ㅝ'},
    {U'ㅜ', U'ㅔ', U'ㅞ'}, {U'ㅜ', U'ㅣ', U'ㅟ'}, {U'ㅡ', U'ㅣ', U'ㅢ'},
};

int jamo_index(std::u32string_view table, char32_t codepoint) {
  // hangul-js initializes every jamo lookup with the sentinel {0: 0}.
  if (codepoint == 0) {
    return 0;
  }
  const auto index = table.find(codepoint);
  return index == std::u32string_view::npos ? -1 : static_cast<int>(index);
}

bool is_cho(char32_t c) { return jamo_index(CHO, c) >= 0; }
bool is_jung(char32_t c) { return jamo_index(JUNG, c) >= 0; }
bool is_jong(char32_t c) { return jamo_index(JONG, c) >= 0; }

char32_t join_jamo(char32_t first, char32_t second, std::span<const JamoPair> pairs) {
  for (const auto& pair : pairs) {
    if (pair.first == first && pair.second == second) {
      return pair.combined;
    }
  }
  return 0;
}

char32_t join_jung(char32_t a, char32_t b) { return join_jamo(a, b, COMPLEX_VOWELS); }
char32_t join_jong(char32_t a, char32_t b) { return join_jamo(a, b, COMPLEX_CONSONANTS); }

char32_t syllable(char32_t cho, char32_t jung, char32_t jong = 0) {
  return static_cast<char32_t>((jamo_index(CHO, cho) * 21 + jamo_index(JUNG, jung)) * 28 + jamo_index(JONG, jong) +
                               0xac00);
}

std::u32string disassemble(std::u32string_view text) {
  std::u32string result;
  result.reserve(text.size());
  for (char32_t c : text) {
    if (c >= 0xac00 && c <= 0xd7a3) {
      const auto offset = c - 0xac00;
      result += CHO[offset / (21 * 28)];
      result += JUNG_PARTS[(offset / 28) % 21];
      result += JONG_PARTS[offset % 28];
    } else if (c == 0 || (c >= 0x3131 && c <= 0x314e)) {
      if (is_cho(c)) {
        result += CHO[jamo_index(CHO, c)];
      } else {
        result += JONG_PARTS[jamo_index(JONG, c)];
      }
    } else if (is_jung(c)) {
      result += JUNG_PARTS[jamo_index(JUNG, c)];
    } else {
      result += c;
    }
  }
  return result;
}

std::u32string assemble(std::u32string_view input) {
  // The string overload of Hangul.assemble first disassembles its input, too.
  const auto text = disassemble(input);
  const auto length = static_cast<std::ptrdiff_t>(text.size());
  std::u32string result;
  result.reserve(text.size());
  int stage = 0;
  std::ptrdiff_t complete_index = -1;
  char32_t previous = 0;
  bool jong_joined = false;

  const auto make_hangul = [&](std::ptrdiff_t index) {
    jong_joined = false;
    if (complete_index + 1 > index) {
      return;
    }
    char32_t cho = 0, jung = 0, jong = 0, combined = 0;
    for (std::ptrdiff_t step = 1;; ++step) {
      const auto c = text[complete_index + step];
      if (step == 1) {
        cho = c;
        if (is_jung(cho)) {
          result += complete_index + step + 1 <= index && is_jung(text[complete_index + step + 1])
                        ? join_jung(cho, text[complete_index + step + 1])
                        : cho;
          complete_index = index;
          return;
        }
        if (!is_cho(cho)) {
          result += cho;
          complete_index = index;
          return;
        }
        combined = cho;
      } else if (step == 2) {
        jung = c;
        if (is_cho(jung)) {
          result += join_jong(cho, jung);
          complete_index = index;
          return;
        }
        combined = syllable(cho, jung);
      } else if (step == 3) {
        if (const auto joined = join_jung(jung, c); joined != 0) {
          jung = joined;
        } else {
          jong = c;
        }
        combined = syllable(cho, jung, jong);
      } else if (step == 4) {
        const auto joined = join_jong(jong, c);
        jong = joined != 0 ? joined : c;
        combined = syllable(cho, jung, jong);
      } else if (step == 5) {
        jong = join_jong(jong, c);
        combined = syllable(cho, jung, jong);
      }
      if (complete_index + step >= index) {
        result += combined;
        complete_index = index;
        return;
      }
    }
  };

  for (std::ptrdiff_t i = 0; i < length; ++i) {
    const auto c = text[i];
    if (!is_cho(c) && !is_jung(c) && !is_jong(c)) {
      make_hangul(i - 1);
      make_hangul(i);
      stage = 0;
      continue;
    }
    switch (stage) {
      case 0:
        if (is_cho(c)) {
          stage = 1;
        } else if (is_jung(c)) {
          stage = 4;
        }
        break;
      case 1:
        if (is_jung(c)) {
          stage = 2;
        } else if (join_jong(previous, c) != 0) {
          stage = 5;
        } else {
          make_hangul(i - 1);
        }
        break;
      case 2:
        if (is_jong(c)) {
          stage = 3;
        } else if (is_jung(c)) {
          if (join_jung(previous, c) == 0) {
            make_hangul(i - 1);
            stage = 4;
          }
        } else {
          make_hangul(i - 1);
          stage = 1;
        }
        break;
      case 3:
        if (is_jong(c)) {
          if (!jong_joined && join_jong(previous, c) != 0) {
            jong_joined = true;
          } else {
            make_hangul(i - 1);
            stage = 1;
          }
        } else if (is_cho(c)) {
          make_hangul(i - 1);
          stage = 1;
        } else if (is_jung(c)) {
          make_hangul(i - 2);
          stage = 2;
        }
        break;
      case 4:
        if (is_jung(c)) {
          if (join_jung(previous, c) != 0) {
            make_hangul(i);
            stage = 0;
          } else {
            make_hangul(i - 1);
          }
        } else {
          make_hangul(i - 1);
          stage = 1;
        }
        break;
      case 5:
        if (is_jung(c)) {
          make_hangul(i - 2);
          stage = 2;
        } else {
          make_hangul(i - 1);
          stage = 1;
        }
        break;
    }
    previous = c;
  }
  make_hangul(length - 1);
  return result;
}

}

namespace language::ko {

std::string disassemble_hangul(const std::string& text) { return utf8::utf32to8(disassemble(utf8::utf8to32(text))); }

std::string assemble_hangul(const std::string& text) { return utf8::utf32to8(assemble(utf8::utf8to32(text))); }

}
