#include "zh.hpp"

namespace {

class ChineseLanguageProcessor final : public LanguageProcessor {
 public:
  std::string_view id() const override { return "zh"; }

  std::vector<TextVariant> preprocess(const std::string& text) const override { return language::zh::preprocess(text); }

  std::vector<DeinflectionResult> deinflect(const std::string& text) const override {
    return {{.text = text, .conditions = 0, .trace_candidates = {}}};
  }

  std::vector<TextVariant> postprocess(const std::string& text) const override { return {{.text = text, .steps = 0}}; }

  uint32_t pos_to_conditions(const std::vector<std::string>&) const override { return 0; }

  std::string normalize_reading(const std::string& reading) const override {
    return language::zh::normalize_pinyin(reading);
  }

  bool is_text_lookup_worthy(const std::string& text) const override {
    return language::zh::is_string_partially_chinese(text);
  }
};

}

namespace language::zh {

const LanguageProcessor& get() {
  static const ChineseLanguageProcessor processor;
  return processor;
}

}
