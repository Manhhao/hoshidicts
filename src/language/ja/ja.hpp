#pragma once

#include "hoshidicts/language.hpp"

namespace language::ja {
const LanguageProcessor& get();
std::vector<TextVariant> preprocess(const std::string& text);
std::vector<DeinflectionResult> deinflect(const std::string& text);
std::vector<TextVariant> postprocess(const std::string& text);
uint32_t pos_to_conditions(const std::vector<std::string>& part_of_speech);
}
