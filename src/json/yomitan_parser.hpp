#pragma once
#include <cstdint>
#include <glaze/glaze.hpp>
#include <optional>
#include <string_view>
#include <vector>

struct Index {
  std::string_view title;
  std::optional<int> format;
  std::optional<int> version;
  std::string_view revision;
  std::optional<std::string_view> minimumYomitanVersion;
  bool sequenced = false;
  std::optional<bool> isUpdatable;
  std::optional<std::string_view> indexUrl;
  std::optional<std::string_view> downloadUrl;
  std::optional<std::string_view> author;
  std::optional<std::string_view> url;
  std::optional<std::string_view> description;
  std::optional<std::string_view> attribution;
  std::optional<std::string_view> sourceLanguage;
  std::optional<std::string_view> targetLanguage;
  std::optional<std::string_view> frequencyMode;
};

struct Term {
  std::string_view expression;
  std::string_view reading;
  std::optional<std::string_view> definition_tags;
  std::string_view rules;
  double score = 0;
  glz::raw_json_view glossary;
  int64_t sequence = 0;
  std::string_view term_tags;
};

struct Meta {
  std::string_view expression;
  std::string_view mode;
  glz::raw_json_view data;
};

struct Kanji {
  std::string_view character;
  std::string_view onyomi;
  std::string_view kunyomi;
  std::string_view tags;
  std::vector<std::string_view> definitions;
  std::unordered_map<std::string, std::string> stats;
};

struct Tag {
  std::string_view name;
  std::string_view category;
  int order = 0;
  std::string_view notes;
  int score = 0;
};

struct ParsedFrequency {
  std::optional<std::string_view> reading;
  double value = 0;
  std::optional<std::string> display_value;
};

struct ParsedAccent {
  int position = 0;
  std::string pattern;
  std::vector<int> nasal;
  std::vector<int> devoice;
};

struct ParsedPitch {
  std::string_view reading;
  std::vector<ParsedAccent> pitches;
  std::vector<std::string_view> transcriptions;
};

namespace yomitan_parser {
bool parse_index(std::string_view content, Index& out);
bool parse_term_bank(std::string_view content, std::vector<Term>& out);
bool parse_meta_bank(std::string_view content, std::vector<Meta>& out);
bool parse_kanji_bank(std::string_view content, std::vector<Kanji>& out);
bool parse_tag_bank(std::string_view content, std::vector<Tag>& out);
bool parse_frequency(std::string_view content, ParsedFrequency& out);
bool parse_pitch(std::string_view content, ParsedPitch& out);
bool parse_ipa(std::string_view content, ParsedPitch& out);
};
