#pragma once

#include "hoshidicts/language.hpp"

namespace language::ko {
const LanguageProcessor& get();
std::string disassemble_hangul(const std::string& text);
std::string assemble_hangul(const std::string& text);
std::vector<DeinflectionResult> deinflect(const std::string& text);
uint32_t pos_to_conditions(const std::vector<std::string>& part_of_speech);
}
