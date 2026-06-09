#include <string_view>

#include "en.hpp"

namespace {

class EnglishLanguageProcessor final : public LanguageProcessor {
 public:
  std::string_view id() const override { return "en"; }

  std::vector<TextVariant> preprocess(const std::string& text) const override { return language::en::preprocess(text); }

  std::vector<DeinflectionResult> deinflect(const std::string& text) const override {
    return language::en::deinflect(text);
  }

  std::vector<TextVariant> postprocess(const std::string& text) const override {
    return language::en::postprocess(text);
  }

  uint32_t pos_to_conditions(const std::vector<std::string>& part_of_speech) const override {
    return language::en::pos_to_conditions(part_of_speech);
  }
};

}

namespace language::en {

const LanguageProcessor& get() {
  static const EnglishLanguageProcessor processor;
  return processor;
}

}
