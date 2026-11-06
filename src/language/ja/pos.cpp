#include "conditions.hpp"
#include "ja.hpp"

namespace language::ja {

uint32_t pos_to_conditions(const std::vector<std::string>& part_of_speech) {
  uint32_t result = 0;
  for (const auto& p : part_of_speech) {
    if (p == "v1") {
      result |= V1;
    } else if (p == "v5") {
      result |= V5;
    } else if (p == "vk") {
      result |= VK;
    } else if (p == "vs") {
      result |= VS;
    } else if (p == "vz") {
      result |= VZ;
    } else if (p == "adj-i") {
      result |= ADJ_I;
    }
  }
  return result;
}

}
