#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "hoshidicts/language.hpp"

namespace language::ja {

class JapaneseDeinflector final {
 public:
  JapaneseDeinflector();

  std::vector<DeinflectionResult> deinflect(const std::string& text) const;

 private:
  struct Rule {
    std::string from;
    std::string to;
    uint32_t conditions_in;
    uint32_t conditions_out;
    int group_id;
  };

  void deinflect_recursive(const std::string& text, uint32_t conditions, std::vector<TransformGroup>& trace,
                           std::vector<DeinflectionResult>& results) const;

  void init_transforms();

  int add_group(const TransformGroup& group);
  void add_rule(const Rule& rule);
  void add_irregular(std::string_view suffix, uint32_t conditions_in, uint32_t conditions_out, int group_id);

  std::unordered_map<std::string, std::vector<Rule>> transforms_;
  std::vector<TransformGroup> groups_;
  size_t max_length_;
};

}
