#include <string_view>

#include "ja.hpp"

namespace {

class JapaneseLanguageProcessor final : public LanguageProcessor {
 public:
  std::string_view id() const override { return "ja"; }

  std::vector<TextVariant> preprocess(const std::string& text) const override { return language::ja::preprocess(text); }

  std::vector<DeinflectionResult> deinflect(const std::string& text) const override {
    return language::ja::deinflect(text);
  }

  std::vector<TextVariant> postprocess(const std::string& text) const override {
    return language::ja::postprocess(text);
  }

  uint32_t pos_to_conditions(const std::vector<std::string>& part_of_speech) const override {
    return language::ja::pos_to_conditions(part_of_speech);
  }

  MissingPosPolicy missing_pos_policy() const override { return MissingPosPolicy::Filter; }
};

}

namespace language::ja {

const LanguageProcessor& get() {
  static const JapaneseLanguageProcessor processor;
  return processor;
}

}
