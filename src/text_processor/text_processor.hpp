#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct TextVariant {
  std::string text;
  int steps;
  size_t min_rewrite = 0;
  size_t max_rewrite = SIZE_MAX;
};

namespace text_processor {
std::vector<TextVariant> process(const std::string& src, char32_t next_char);
}
