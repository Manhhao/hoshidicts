#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

struct TextVariant {
  std::string text;
  int steps = 0;
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
};

namespace language {
const LanguageProcessor& get(std::string_view id);
}
