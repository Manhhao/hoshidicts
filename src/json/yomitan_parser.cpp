#include "yomitan_parser.hpp"

#include <charconv>
#include <cmath>
#include <regex>
#include <string>
#include <string_view>
#include <variant>

template <>
struct glz::meta<Index> {
  using T = Index;
  static constexpr auto value =
      object("title", glz::raw_string<&T::title>, "format", &T::format, "version", &T::version, "revision",
             glz::raw_string<&T::revision>, "minimumYomitanVersion", glz::raw_string<&T::minimumYomitanVersion>,
             "sequenced", &T::sequenced, "isUpdatable", &T::isUpdatable, "indexUrl", glz::raw_string<&T::indexUrl>,
             "downloadUrl", glz::raw_string<&T::downloadUrl>, "author", glz::raw_string<&T::author>, "url",
             glz::raw_string<&T::url>, "description", glz::raw_string<&T::description>, "attribution",
             glz::raw_string<&T::attribution>, "sourceLanguage", glz::raw_string<&T::sourceLanguage>, "targetLanguage",
             glz::raw_string<&T::targetLanguage>, "frequencyMode", glz::raw_string<&T::frequencyMode>);
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
  static constexpr auto value =
      array(glz::raw_string<&T::character>, glz::raw_string<&T::onyomi>, glz::raw_string<&T::kunyomi>,
            glz::raw_string<&T::tags>, &T::definitions, &T::stats);
};

template <>
struct glz::meta<Tag> {
  using T = Tag;
  static constexpr auto value =
      array(glz::raw_string<&T::name>, glz::raw_string<&T::category>, &T::order, glz::raw_string<&T::notes>, &T::score);
};

namespace internal {
struct FrequencyValue {
  double value;
  std::optional<std::string> display_value;
};

struct RawFrequencyFlat {
  std::optional<std::string_view> reading;
  double value;
  std::optional<std::string> display_value;
};

struct RawFrequency {
  std::optional<std::string_view> reading;
  std::variant<double, std::string, FrequencyValue> frequency;
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
  auto parse_string_number = [](const std::string& value) {
    // Keep this in sync with Yomitan's Translator._numberRegex.
    static const std::regex number_regex(R"([+-]?(\d+(\.\d*)?|\.\d+)([eE][+-]?\d+)?)");
    std::smatch match;
    if (!std::regex_search(value, match, number_regex)) {
      return 0.0;
    }

    double parsed = 0;
    const std::string matched = match.str();
    const char* begin = matched.data();
    const char* end = begin + matched.size();
    if (begin != end && *begin == '+') {
      begin++;
    }
    const auto result = std::from_chars(begin, end, parsed);
    return result.ec == std::errc{} && result.ptr == end && std::isfinite(parsed) ? parsed : 0.0;
  };

  auto assign_value = [&](const auto& value) {
    using T = std::decay_t<decltype(value)>;
    if constexpr (std::is_same_v<T, double>) {
      if (!std::isfinite(value)) {
        return false;
      }
      out.value = value;
      out.display_value = std::nullopt;
    } else if constexpr (std::is_same_v<T, std::string>) {
      out.value = parse_string_number(value);
      out.display_value = value;
    } else {
      if (!std::isfinite(value.value)) {
        return false;
      }
      out.value = value.value;
      out.display_value = value.display_value;
    }
    return true;
  };

  internal::RawFrequency parsed;
  auto error = glz::read<glz::opts{.error_on_unknown_keys = false, .error_on_missing_keys = true}>(parsed, content);
  if (!error) {
    out.reading = parsed.reading;
    return std::visit(assign_value, parsed.frequency);
  }

  internal::RawFrequencyFlat parsed_flat;
  error = glz::read<glz::opts{.error_on_unknown_keys = false, .error_on_missing_keys = true}>(parsed_flat, content);
  if (!error) {
    if (!std::isfinite(parsed_flat.value)) {
      return false;
    }
    out.reading = parsed_flat.reading;
    out.value = parsed_flat.value;
    out.display_value = parsed_flat.display_value;
    return true;
  }

  double val;
  error = glz::read_json(val, content);
  if (!error) {
    if (!std::isfinite(val)) {
      return false;
    }
    out.value = val;
    out.display_value = std::nullopt;
    out.reading = std::nullopt;
    return true;
  }

  std::string string_value;
  error = glz::read_json(string_value, content);
  if (!error) {
    out.reading = std::nullopt;
    out.value = parse_string_number(string_value);
    out.display_value = std::move(string_value);
    return true;
  }

  return false;
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
  out.transcriptions = parsed.transcriptions | std::views::transform(&internal::TranscriptionsArray::ipa) |
                       std::ranges::to<std::vector>();
  return true;
}
