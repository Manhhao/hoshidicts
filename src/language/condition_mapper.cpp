#include "condition_mapper.hpp"

#include <utility>

namespace language::internal {

void ConditionMapper::add_condition(std::string name, uint32_t flags, bool is_dictionary_form) {
  if (is_dictionary_form) {
    part_of_speech_flags_[std::move(name)] = flags;
  }
}

uint32_t ConditionMapper::from_parts_of_speech(const std::vector<std::string>& part_of_speech) const {
  uint32_t flags = 0;
  for (const auto& pos : part_of_speech) {
    if (auto it = part_of_speech_flags_.find(pos); it != part_of_speech_flags_.end()) {
      flags |= it->second;
    }
  }
  return flags;
}

}
