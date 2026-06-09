#pragma once

#include <cstdint>

namespace language::ja {

enum Conditions : uint32_t {
  NONE = 0,
  V1D = 1 << 0,
  V1P = 1 << 1,
  V5D = 1 << 2,
  V5SS = 1 << 3,
  V5SP = 1 << 4,
  VK = 1 << 5,
  VS = 1 << 6,
  VZ = 1 << 7,
  ADJ_I = 1 << 8,
  MASU = 1 << 9,
  MASEN = 1 << 10,
  TE = 1 << 11,
  BA = 1 << 12,
  KU = 1 << 13,
  TA = 1 << 14,
  NN = 1 << 15,
  NASAI = 1 << 16,
  YA = 1 << 17,
  V1 = V1D | V1P,
  V5S = V5SS | V5SP,
  V5 = V5D | V5S,
  V = V1 | V5 | VK | VS | VZ,
};

}
