#include <utility>

#include "ko.hpp"

namespace {

class KoreanLanguageProcessor final : public LanguageProcessor {
 public:
  std::string_view id() const override { return "ko"; }

  std::vector<TextVariant> preprocess(const std::string& text) const override {
    auto processed = language::ko::disassemble_hangul(text);
    const int steps = processed == text ? 0 : 1;
    return {{.text = std::move(processed), .steps = steps}};
  }

  std::vector<DeinflectionResult> deinflect(const std::string& text) const override {
    return language::ko::deinflect(text);
  }

  std::vector<TextVariant> postprocess(const std::string& text) const override {
    auto processed = language::ko::assemble_hangul(text);
    const int steps = processed == text ? 0 : 1;
    return {{.text = std::move(processed), .steps = steps}};
  }

  uint32_t pos_to_conditions(const std::vector<std::string>& part_of_speech) const override {
    return language::ko::pos_to_conditions(part_of_speech);
  }
};

}

namespace language::ko {

const LanguageProcessor& get() {
  static const KoreanLanguageProcessor processor;
  return processor;
}

}
