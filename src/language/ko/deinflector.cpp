#include <cstddef>
#include <utility>

#include "../transform_engine.hpp"
#include "conditions.hpp"
#include "ko.hpp"

namespace language::ko {
namespace {

struct GroupDefinition {
  const char* name;
  const char* description;
};

struct RuleDefinition {
  const char* from;
  const char* to;
  uint32_t conditions_in;
  uint32_t conditions_out;
  int group_id;
};

#include "rules.hpp"

const language::internal::TransformEngine& engine() {
  static const auto result = [] {
    language::internal::TransformEngine engine;
    for (const auto& group : GROUPS) {
      engine.add_group({.name = group.name, .description = group.description});
    }
    for (const auto& rule : RULES) {
      if (rule.from[0] == '\0') {
        // Yomitan's suffixInflection uses slice(0, -suffix.length). For an empty
        // suffix that is slice(0, -0), so this rule replaces the entire input.
        engine.add_custom_rule([replacement = std::string(rule.to)](const std::string&) { return replacement; },
                               rule.conditions_in, rule.conditions_out, rule.group_id);
      } else {
        engine.add_suffix_rule(rule.from, rule.to, rule.conditions_in, rule.conditions_out, rule.group_id);
      }
    }
    return engine;
  }();
  return result;
}

}

std::vector<DeinflectionResult> deinflect(const std::string& text) {
  auto results = engine().deinflect(text);
  const auto& rule = RULES[EMPTY_SUFFIX_RULE_INDEX];
  if (text == rule.to) {
    // The shared engine skips unchanged text. Preserve the upstream "다" ->
    // "다" candidate with its ida condition without changing other languages.
    const auto& group = GROUPS[rule.group_id];
    results.push_back({.text = text,
                       .conditions = rule.conditions_out,
                       .trace_candidates = {{.deinflected = text,
                                             .preprocessor_steps = 0,
                                             .source = TraceSource::Algorithm,
                                             .trace = {{.name = group.name, .description = group.description}}}}});
  }
  return results;
}

uint32_t pos_to_conditions(const std::vector<std::string>& part_of_speech) {
  uint32_t conditions = 0;
  for (const auto& pos : part_of_speech) {
    if (pos == "v") {
      conditions |= V;
    } else if (pos == "adj") {
      conditions |= ADJ;
    } else if (pos == "ida") {
      conditions |= IDA;
    }
  }
  return conditions;
}

}
