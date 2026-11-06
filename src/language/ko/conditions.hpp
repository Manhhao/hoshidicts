#pragma once

#include <cstdint>

namespace language::ko {

enum Conditions : uint32_t {
  V = 1u << 0,
  ADJ = 1u << 1,
  IDA = 1u << 2,
  P = 1u << 3,
  F = 1u << 4,
  EUSI = 1u << 5,
  EUOB = 1u << 6,
  EUO = 1u << 7,
  SAO = 1u << 8,
  SAOB = 1u << 9,
  SAB = 1u << 10,
  JAOB = 1u << 11,
  JAO = 1u << 12,
  JAB = 1u << 13,
  DO = 1u << 14,
};

}
