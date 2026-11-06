#pragma once

#include <functional>
#include <string>
#include <vector>

#include "hoshidicts/language.hpp"

namespace language::internal {

struct TextProcessorDefinition {
  std::function<std::vector<std::string>(const std::string&)> process;
};

std::vector<TextVariant> process_text_variants(const std::string& src,
                                               const std::vector<TextProcessorDefinition>& processors);

}
