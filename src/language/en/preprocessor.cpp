#include <utf8proc.h>

#include <string>
#include <string_view>
#include <vector>

#include "../text_variants.hpp"
#include "en.hpp"

namespace {

void append_codepoint(std::string& text, utf8proc_int32_t codepoint) {
  utf8proc_uint8_t encoded[4];
  const auto size = utf8proc_encode_char(codepoint, encoded);
  text.append(reinterpret_cast<const char*>(encoded), static_cast<size_t>(size));
}

std::string unicode_lower(std::string_view text) {
  std::string result;
  result.reserve(text.size());

  size_t offset = 0;
  while (offset < text.size()) {
    utf8proc_int32_t codepoint = 0;
    const auto size = utf8proc_iterate(reinterpret_cast<const utf8proc_uint8_t*>(text.data() + offset),
                                       static_cast<utf8proc_ssize_t>(text.size() - offset), &codepoint);
    if (size < 0) {
      result.append(text.substr(offset));
      break;
    }

    append_codepoint(result, utf8proc_tolower(codepoint));
    offset += static_cast<size_t>(size);
  }
  return result;
}

std::string capitalize_first(std::string_view text) {
  if (text.empty()) {
    return std::string(text);
  }

  utf8proc_int32_t codepoint = 0;
  const auto size = utf8proc_iterate(reinterpret_cast<const utf8proc_uint8_t*>(text.data()),
                                     static_cast<utf8proc_ssize_t>(text.size()), &codepoint);
  if (size < 0) {
    return std::string(text);
  }

  std::string result;
  result.reserve(text.size());
  append_codepoint(result, utf8proc_toupper(codepoint));
  result.append(text.substr(static_cast<size_t>(size)));
  return result;
}

const std::vector<language::internal::TextProcessorDefinition>& preprocessors() {
  static const std::vector<language::internal::TextProcessorDefinition> definitions = {
      {.process = [](const std::string& str) { return std::vector<std::string>{str, unicode_lower(str)}; }},
      {.process = [](const std::string& str) { return std::vector<std::string>{str, capitalize_first(str)}; }},
  };
  return definitions;
}

}

namespace language::en {

std::vector<TextVariant> preprocess(const std::string& text) {
  return language::internal::process_text_variants(text, preprocessors());
}

std::vector<TextVariant> postprocess(const std::string& text) { return {{.text = text, .steps = 0}}; }

}
