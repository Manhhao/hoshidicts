#pragma once

#include <cstdint>

namespace language::en {

enum Conditions : uint32_t {
  V_PHR = 1 << 0,
  V = V_PHR,
  NP = 1 << 1,
  NS = 1 << 2,
  N = NP | NS,
  ADJ = 1 << 3,
  ADV = 1 << 4,
};

}
