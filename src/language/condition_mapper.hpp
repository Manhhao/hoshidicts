#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace language::internal {

class ConditionMapper {
 public:
  void add_condition(std::string name, uint32_t flags, bool is_dictionary_form);
  uint32_t from_parts_of_speech(const std::vector<std::string>& part_of_speech) const;

 private:
  std::unordered_map<std::string, uint32_t> part_of_speech_flags_;
};

}
