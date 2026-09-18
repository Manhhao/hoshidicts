#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "hoshidicts/query.hpp"

struct RawGlossary {
  const std::string* dict_name;
  std::string_view definition_tags;
  std::string_view term_tags;
  std::string_view rules;
  const uint8_t* compressed_data;
  uint32_t compressed_size;
  const ZSTD_DDict_s* zstd_dict;
  uint32_t next;
};

struct RawTerm {
  std::string_view expression;
  std::string_view reading;
  int score;
  uint32_t first_glossary;
  uint32_t last_glossary;
  std::vector<FrequencyEntry> frequencies;
  std::vector<PitchEntry> pitches;
};

struct RawTerms {
  std::vector<RawTerm> terms;
  std::vector<RawGlossary> glossaries;
};
