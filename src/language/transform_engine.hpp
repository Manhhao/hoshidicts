#pragma once

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "hoshidicts/language.hpp"

namespace language::internal {

class TransformEngine {
 public:
  enum class TraceOrder { Append, Prepend };

  explicit TransformEngine(TraceOrder trace_order = TraceOrder::Prepend);

  int add_group(TransformGroup group);
  void add_suffix_rule(std::string from, std::string to, uint32_t conditions_in, uint32_t conditions_out, int group_id);
  void add_prefix_rule(std::string from, std::string to, uint32_t conditions_in, uint32_t conditions_out, int group_id);
  void add_whole_word_rule(std::string from, std::string to, uint32_t conditions_in, uint32_t conditions_out,
                           int group_id);
  void add_custom_rule(std::function<std::optional<std::string>(const std::string&)> apply, uint32_t conditions_in,
                       uint32_t conditions_out, int group_id);

  std::vector<DeinflectionResult> deinflect(const std::string& text) const;

 private:
  enum class RuleType { Suffix, Prefix, WholeWord, Custom };

  struct Rule {
    RuleType type;
    std::string from;
    std::string to;
    std::function<std::optional<std::string>(const std::string&)> apply;
    uint32_t conditions_in;
    uint32_t conditions_out;
    int group_id;
  };

  struct HistoryFrame {
    size_t rule_index;
    std::string text;
  };

  static bool conditions_match(uint32_t current_conditions, uint32_t next_conditions);
  static bool ends_with(std::string_view text, std::string_view suffix);
  static bool starts_with(std::string_view text, std::string_view prefix);

  std::optional<std::string> apply_rule(const Rule& rule, const std::string& text) const;
  bool has_cycle(const std::vector<HistoryFrame>& history, size_t rule_index, const std::string& text) const;
  std::vector<TransformGroup> extend_trace(const std::vector<TransformGroup>& trace, int group_id) const;

  TraceOrder trace_order_;
  std::vector<TransformGroup> groups_;
  std::vector<Rule> rules_;
};

}
