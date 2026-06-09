#pragma once

#include "language.hpp"

class Deinflector {
 public:
  Deinflector() = default;

  std::vector<DeinflectionResult> deinflect(const std::string& text) const {
    return language::get("ja").deinflect(text);
  }

  static uint32_t pos_to_conditions(const std::vector<std::string>& part_of_speech) {
    return language::get("ja").pos_to_conditions(part_of_speech);
  }

  operator const LanguageProcessor&() const { return language::get("ja"); }
};
