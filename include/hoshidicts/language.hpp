#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

struct TextVariant {
  std::string text;
  int steps = 0;
  std::size_t min_rewrite = 0;
  std::size_t max_rewrite = SIZE_MAX;
};

struct TransformGroup {
  std::string name;
  std::string description;
};

enum class TraceSource : uint8_t { Algorithm, Dictionary, Both };

enum class MissingPosPolicy : uint8_t { Keep, Filter };

struct TraceCandidate {
  std::string deinflected;
  int preprocessor_steps = 0;
  TraceSource source = TraceSource::Algorithm;
  std::vector<TransformGroup> trace;
};

struct DeinflectionResult {
  std::string text;
  uint32_t conditions = 0;
  std::vector<TraceCandidate> trace_candidates;
};

class LanguageProcessor {
 public:
  virtual ~LanguageProcessor() = default;

  virtual std::string_view id() const = 0;
  virtual std::vector<TextVariant> preprocess(const std::string& text) const = 0;
  virtual std::vector<DeinflectionResult> deinflect(const std::string& text) const = 0;
  virtual std::vector<TextVariant> postprocess(const std::string& text) const = 0;
  virtual uint32_t pos_to_conditions(const std::vector<std::string>& part_of_speech) const = 0;
  virtual MissingPosPolicy missing_pos_policy() const { return MissingPosPolicy::Filter; }
  // Normalize grouping keys without changing the dictionary's displayed reading.
  virtual std::string normalize_reading(const std::string& reading) const { return reading; }
  // A hint for text scanners; explicit lookups are not restricted by this predicate.
  virtual bool is_text_lookup_worthy(const std::string&) const { return true; }
  virtual std::vector<TextVariant> preprocess_for_lookup(const std::string& text, char32_t) const {
    return preprocess(text);
  }
};

namespace language {
const LanguageProcessor& get(std::string_view id);
}
