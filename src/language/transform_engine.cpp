#include "transform_engine.hpp"

#include <algorithm>
#include <cstddef>
#include <utility>

namespace language::internal {

TransformEngine::TransformEngine(TraceOrder trace_order) : trace_order_(trace_order) {}

int TransformEngine::add_group(TransformGroup group) {
  const auto id = static_cast<int>(groups_.size());
  groups_.emplace_back(std::move(group));
  return id;
}

void TransformEngine::add_suffix_rule(std::string from, std::string to, uint32_t conditions_in, uint32_t conditions_out,
                                      int group_id) {
  rules_.push_back({.type = RuleType::Suffix,
                    .from = std::move(from),
                    .to = std::move(to),
                    .conditions_in = conditions_in,
                    .conditions_out = conditions_out,
                    .group_id = group_id});
}

void TransformEngine::add_prefix_rule(std::string from, std::string to, uint32_t conditions_in, uint32_t conditions_out,
                                      int group_id) {
  rules_.push_back({.type = RuleType::Prefix,
                    .from = std::move(from),
                    .to = std::move(to),
                    .conditions_in = conditions_in,
                    .conditions_out = conditions_out,
                    .group_id = group_id});
}

void TransformEngine::add_whole_word_rule(std::string from, std::string to, uint32_t conditions_in,
                                          uint32_t conditions_out, int group_id) {
  rules_.push_back({.type = RuleType::WholeWord,
                    .from = std::move(from),
                    .to = std::move(to),
                    .conditions_in = conditions_in,
                    .conditions_out = conditions_out,
                    .group_id = group_id});
}

void TransformEngine::add_custom_rule(std::function<std::optional<std::string>(const std::string&)> apply,
                                      uint32_t conditions_in, uint32_t conditions_out, int group_id) {
  rules_.push_back({.type = RuleType::Custom,
                    .apply = std::move(apply),
                    .conditions_in = conditions_in,
                    .conditions_out = conditions_out,
                    .group_id = group_id});
}

namespace {
bool traces_equal(const std::vector<TransformGroup>& a, const std::vector<TransformGroup>& b) {
  if (a.size() != b.size()) {
    return false;
  }

  for (size_t i = 0; i < a.size(); ++i) {
    if (a[i].name != b[i].name) {
      return false;
    }
  }
  return true;
}

void add_deinflection_result(std::vector<DeinflectionResult>& results, const std::string& text, uint32_t conditions,
                             const std::vector<TransformGroup>& trace) {
  TraceCandidate candidate{
      .deinflected = text, .preprocessor_steps = 0, .source = TraceSource::Algorithm, .trace = trace};

  auto it = std::ranges::find_if(results, [&](const DeinflectionResult& result) {
    return result.text == text && result.conditions == conditions;
  });
  if (it == results.end()) {
    DeinflectionResult result{.text = text, .conditions = conditions};
    result.trace_candidates.push_back(std::move(candidate));
    results.push_back(std::move(result));
    return;
  }

  const bool duplicate = std::ranges::any_of(it->trace_candidates, [&](const TraceCandidate& existing) {
    return traces_equal(existing.trace, candidate.trace);
  });
  if (!duplicate) {
    it->trace_candidates.push_back(std::move(candidate));
  }
}
}

std::vector<DeinflectionResult> TransformEngine::deinflect(const std::string& text) const {
  struct WorkItem {
    std::string text;
    uint32_t conditions;
    std::vector<TransformGroup> trace;
    std::vector<HistoryFrame> history;
  };

  std::vector<WorkItem> work_items = {{.text = text, .conditions = 0, .trace = {}, .history = {}}};
  std::vector<DeinflectionResult> results;

  for (size_t i = 0; i < work_items.size(); ++i) {
    auto item = std::move(work_items[i]);
    add_deinflection_result(results, item.text, item.conditions, item.trace);

    for (size_t rule_index = 0; rule_index < rules_.size(); ++rule_index) {
      const auto& rule = rules_[rule_index];
      if (!conditions_match(item.conditions, rule.conditions_in)) {
        continue;
      }

      auto transformed = apply_rule(rule, item.text);
      if (!transformed || *transformed == item.text || has_cycle(item.history, rule_index, item.text)) {
        continue;
      }

      auto trace = extend_trace(item.trace, rule.group_id);
      auto history = item.history;
      history.push_back({.rule_index = rule_index, .text = item.text});
      work_items.push_back({.text = std::move(*transformed),
                            .conditions = rule.conditions_out,
                            .trace = std::move(trace),
                            .history = std::move(history)});
    }
  }

  return results;
}

bool TransformEngine::conditions_match(uint32_t current_conditions, uint32_t next_conditions) {
  return current_conditions == 0 || (current_conditions & next_conditions) != 0;
}

bool TransformEngine::ends_with(std::string_view text, std::string_view suffix) {
  return text.size() >= suffix.size() && text.substr(text.size() - suffix.size()) == suffix;
}

bool TransformEngine::starts_with(std::string_view text, std::string_view prefix) {
  return text.size() >= prefix.size() && text.substr(0, prefix.size()) == prefix;
}

std::optional<std::string> TransformEngine::apply_rule(const Rule& rule, const std::string& text) const {
  switch (rule.type) {
    case RuleType::Suffix:
      if (!ends_with(text, rule.from)) {
        return std::nullopt;
      }
      return text.substr(0, text.size() - rule.from.size()) + rule.to;
    case RuleType::Prefix:
      if (!starts_with(text, rule.from)) {
        return std::nullopt;
      }
      return rule.to + text.substr(rule.from.size());
    case RuleType::WholeWord:
      return text == rule.from ? std::optional<std::string>{rule.to} : std::nullopt;
    case RuleType::Custom:
      return rule.apply(text);
  }
  return std::nullopt;
}

bool TransformEngine::has_cycle(const std::vector<HistoryFrame>& history, size_t rule_index,
                                const std::string& text) const {
  return std::ranges::any_of(
      history, [&](const HistoryFrame& frame) { return frame.rule_index == rule_index && frame.text == text; });
}

std::vector<TransformGroup> TransformEngine::extend_trace(const std::vector<TransformGroup>& trace,
                                                          int group_id) const {
  std::vector<TransformGroup> result;
  result.reserve(trace.size() + 1);

  if (trace_order_ == TraceOrder::Prepend) {
    result.push_back(groups_[group_id]);
    result.insert(result.end(), trace.begin(), trace.end());
  } else {
    result.insert(result.end(), trace.begin(), trace.end());
    result.push_back(groups_[group_id]);
  }

  return result;
}

}
