#include "../text_variants.hpp"
#include "zh.hpp"

namespace language::zh {

std::vector<TextVariant> preprocess(const std::string& text) {
  static const std::vector<language::internal::TextProcessorDefinition> processors = {
      {.process = [](const std::string& str) { return std::vector<std::string>{str, normalize_radicals(str)}; }},
  };
  return language::internal::process_text_variants(text, processors);
}

}
