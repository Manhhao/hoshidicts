#include "../condition_mapper.hpp"
#include "conditions.hpp"
#include "en.hpp"

namespace {

const language::internal::ConditionMapper& mapper() {
  static const auto result = [] {
    language::internal::ConditionMapper conditions;
    conditions.add_condition("v", language::en::V, true);
    conditions.add_condition("v_phr", language::en::V_PHR, true);
    conditions.add_condition("n", language::en::N, true);
    conditions.add_condition("np", language::en::NP, true);
    conditions.add_condition("ns", language::en::NS, true);
    conditions.add_condition("adj", language::en::ADJ, true);
    conditions.add_condition("adv", language::en::ADV, true);
    return conditions;
  }();
  return result;
}

}

namespace language::en {

uint32_t pos_to_conditions(const std::vector<std::string>& part_of_speech) {
  return mapper().from_parts_of_speech(part_of_speech);
}

}
