#include "yomitan_parser.hpp"

#include <string_view>
#include <tuple>
#include <variant>

template <>
struct glz::meta<Index> {
  using T = Index;
  static constexpr auto value =
      object("title", glz::raw_string<&T::title>, "format", &T::format, "version", &T::version,
             "revision", glz::raw_string<&T::revision>, "minimumYomitanVersion", glz::raw_string<&T::minimumYomitanVersion>,
             "sequenced", &T::sequenced, "isUpdatable", &T::isUpdatable, "indexUrl", glz::raw_string<&T::indexUrl>,
             "downloadUrl", glz::raw_string<&T::downloadUrl>, "author", glz::raw_string<&T::author>,
             "url", glz::raw_string<&T::url>, "description", glz::raw_string<&T::description>,
             "attribution", glz::raw_string<&T::attribution>, "sourceLanguage", glz::raw_string<&T::sourceLanguage>,
             "targetLanguage", glz::raw_string<&T::targetLanguage>, "frequencyMode", glz::raw_string<&T::frequencyMode>);
};

template <>
struct glz::meta<Term> {
  using T = Term;
  static constexpr auto value =
      array(glz::raw_string<&T::expression>, glz::raw_string<&T::reading>, glz::raw_string<&T::definition_tags>,
            glz::raw_string<&T::rules>, &T::score, &T::glossary, &T::sequence, glz::raw_string<&T::term_tags>);
};

template <>
struct glz::meta<Meta> {
  using T = Meta;
  static constexpr auto value = array(glz::raw_string<&T::expression>, glz::raw_string<&T::mode>, &T::data);
};

template <>
struct glz::meta<Kanji> {
  using T = Kanji;
  static constexpr auto value = array(glz::raw_string<&T::character>, glz::raw_string<&T::onyomi>,
                                      glz::raw_string<&T::kunyomi>, glz::raw_string<&T::tags>, &T::definitions, &T::stats);
};

template <>
struct glz::meta<Tag> {
  using T = Tag;
  static constexpr auto value =
      array(glz::raw_string<&T::name>, glz::raw_string<&T::category>, &T::order, glz::raw_string<&T::notes>, &T::score);
};

namespace internal {
struct FrequencyValue {
  int value;
  std::optional<std::string> display_value;
};

struct RawFrequencyFlat {
  std::optional<std::string_view> reading;
  int value;
  std::optional<std::string> display_value;
};

struct RawFrequency {
  std::optional<std::string_view> reading;
  std::variant<int, FrequencyValue> frequency;
};

struct PitchesArray {
  std::variant<int, std::string> position;
  std::optional<std::variant<int, std::vector<int>>> nasal;
  std::optional<std::variant<int, std::vector<int>>> devoice;
};

struct RawPitch {
  std::string_view reading;
  std::vector<PitchesArray> pitches;
};

struct TranscriptionsArray {
  std::string_view ipa;
};

struct RawIPA {
  std::string_view reading;
  std::vector<TranscriptionsArray> transcriptions;
};
};

template <>
struct glz::meta<internal::RawFrequencyFlat> {
  using T = internal::RawFrequencyFlat;
  static constexpr auto value = object("reading", &T::reading, "value", &T::value, "displayValue", &T::display_value);
};

template <>
struct glz::meta<internal::FrequencyValue> {
  using T = internal::FrequencyValue;
  static constexpr auto value = object("value", &T::value, "displayValue", &T::display_value);
};

template <>
struct glz::meta<internal::RawFrequency> {
  using T = internal::RawFrequency;
  static constexpr auto value = object("reading", &T::reading, "frequency", &T::frequency);
};

template <>
struct glz::meta<internal::PitchesArray> {
  using T = internal::PitchesArray;
  static constexpr auto value = object("position", &T::position, "nasal", &T::nasal, "devoice", &T::devoice);
};

template <>
struct glz::meta<internal::RawPitch> {
  using T = internal::RawPitch;
  static constexpr auto value = object("reading", glz::raw_string<&T::reading>, "pitches", &T::pitches);
};

template <>
struct glz::meta<internal::TranscriptionsArray> {
  using T = internal::TranscriptionsArray;
  static constexpr auto value = object("ipa", glz::raw_string<&T::ipa>);
};

template <>
struct glz::meta<internal::RawIPA> {
  using T = internal::RawIPA;
  static constexpr auto value = object("reading", glz::raw_string<&T::reading>, "transcriptions", &T::transcriptions);
};

bool yomitan_parser::parse_index(std::string_view content, Index& out) {
  auto error = glz::read<glz::opts{.error_on_unknown_keys = false, .error_on_missing_keys = false}>(out, content);
  return !error;
}

bool yomitan_parser::parse_term_bank(std::string_view content, std::vector<Term>& out) {
  auto error = glz::read<glz::opts{.error_on_unknown_keys = false, .error_on_missing_keys = false}>(out, content);
  return !error;
}

bool yomitan_parser::parse_glossary(std::string_view content, ParsedGlossary& out) {
  std::vector<glz::raw_json> definitions;
  if (glz::read_json(definitions, content)) {
    return false;
  }

  ParsedGlossary result;
  result.display_json.push_back('[');
  for (const auto& definition : definitions) {
    const size_t first_token = definition.str.find_first_not_of(" \t\r\n");
    if (first_token == std::string::npos || definition.str[first_token] != '[') {
      if (result.has_display_definitions) {
        result.display_json.push_back(',');
      }
      result.display_json.append(definition.str);
      result.has_display_definitions = true;
      continue;
    }

    // Yomitan treats every top-level array as dictionary deinflection metadata
    // and never displays it as a definition. Only schema-valid tuples are
    // followed; malformed arrays are safely stripped.
    std::tuple<std::string, std::vector<std::string>> tuple;
    if (glz::read_json(tuple, definition.str)) {
      continue;
    }
    auto& [form_of, inflection_rules] = tuple;
    if (!form_of.empty()) {
      result.redirects.push_back(
          TermRedirect{.form_of = std::move(form_of), .inflection_rules = std::move(inflection_rules)});
    }
  }
  result.display_json.push_back(']');
  out = std::move(result);
  return true;
}

bool yomitan_parser::parse_meta_bank(std::string_view content, std::vector<Meta>& out) {
  auto error = glz::read<glz::opts{.error_on_unknown_keys = false, .error_on_missing_keys = false}>(out, content);
  return !error;
}

bool yomitan_parser::parse_kanji_bank(std::string_view content, std::vector<Kanji>& out) {
  auto error = glz::read<glz::opts{.error_on_unknown_keys = false, .error_on_missing_keys = false}>(out, content);
  return !error;
}

bool yomitan_parser::parse_tag_bank(std::string_view content, std::vector<Tag>& out) {
  auto error = glz::read<glz::opts{.error_on_unknown_keys = false, .error_on_missing_keys = false}>(out, content);
  return !error;
}

bool yomitan_parser::parse_frequency(std::string_view content, ParsedFrequency& out) {
  internal::RawFrequencyFlat parsed_flat;
  auto error =
      glz::read<glz::opts{.error_on_unknown_keys = false, .error_on_missing_keys = true}>(parsed_flat, content);
  if (!error) {
    out.reading = parsed_flat.reading.value_or("");
    out.value = parsed_flat.value;
    out.display_value = parsed_flat.display_value.value_or(std::to_string(parsed_flat.value));
    return true;
  }

  int val;
  error = glz::read_json(val, content);
  if (!error) {
    out.value = val;
    out.display_value = std::to_string(val);
    out.reading = "";
    return true;
  }

  internal::RawFrequency parsed;
  error = glz::read<glz::opts{.error_on_unknown_keys = false, .error_on_missing_keys = true}>(parsed, content);
  if (error) {
    return false;
  }

  out.reading = parsed.reading.value_or("");
  if (std::holds_alternative<int>(parsed.frequency)) {
    int freq = std::get<int>(parsed.frequency);
    out.value = freq;
    out.display_value = std::to_string(freq);
  } else {
    auto& freq = std::get<internal::FrequencyValue>(parsed.frequency);
    out.value = freq.value;
    out.display_value = freq.display_value.value_or(std::to_string(freq.value));
  }
  return true;
}

bool yomitan_parser::parse_pitch(std::string_view content, ParsedPitch& out) {
  internal::RawPitch parsed;
  auto error = glz::read<glz::opts{.error_on_unknown_keys = false, .error_on_missing_keys = true}>(parsed, content);
  if (error) {
    return false;
  }

  auto to_number_array = [](const std::optional<std::variant<int, std::vector<int>>>& value) -> std::vector<int> {
    if (!value) {
      return {};
    }
    if (std::holds_alternative<int>(*value)) {
      return {std::get<int>(*value)};
    }
    return std::get<std::vector<int>>(*value);
  };

  out.reading = parsed.reading;
  for (auto& pitch : parsed.pitches) {
    ParsedAccent accent{.nasal = to_number_array(pitch.nasal), .devoice = to_number_array(pitch.devoice)};
    if (std::holds_alternative<int>(pitch.position)) {
      accent.position = std::get<int>(pitch.position);
    } else {
      accent.pattern = std::move(std::get<std::string>(pitch.position));
    }
    out.pitches.emplace_back(std::move(accent));
  }
  return true;
}

bool yomitan_parser::parse_ipa(std::string_view content, ParsedPitch& out) {
  internal::RawIPA parsed;
  auto error = glz::read<glz::opts{.error_on_unknown_keys = false, .error_on_missing_keys = false}>(parsed, content);
  if (error) {
    return false;
  }

  out.reading = parsed.reading;
  out.transcriptions =
      parsed.transcriptions | std::views::transform(&internal::TranscriptionsArray::ipa) | std::ranges::to<std::vector>();
  return true;
}
