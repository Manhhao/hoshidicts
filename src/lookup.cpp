#include "hoshidicts/lookup.hpp"

#include <utf8.h>

#include <algorithm>
#include <climits>
#include <map>
#include <optional>
#include <ranges>
#include <sstream>

#include "reading/reading.hpp"
#include "text_processor/text_processor.hpp"

namespace {
std::vector<std::string> split_whitespace(const std::string& str) {
  std::vector<std::string> result;
  std::istringstream iss(str);
  std::string token;
  while (iss >> token) {
    result.push_back(std::move(token));
  }
  return result;
}

std::optional<int> get_freq_value_for_dict(const TermResult& term, std::string_view dictionary_name, bool descending) {
  std::optional<int> frequency;
  for (const auto& frequency_entry : term.frequencies) {
    if (frequency_entry.dict_name != dictionary_name || frequency_entry.frequencies.empty()) {
      continue;
    }

    for (const auto& candidate : frequency_entry.frequencies) {
      if (candidate.value < 0) {
        continue;
      }
      frequency = frequency.has_value() ? std::optional<int>(descending ? std::max(*frequency, candidate.value)
                                                                        : std::min(*frequency, candidate.value))
                                        : std::optional<int>(candidate.value);
    }
  }

  return frequency;
}
}

std::vector<LookupResult> Lookup::lookup(const std::string& lookup_string, int max_results, size_t scan_length,
                                         const LookupOptions& options) const {
  std::map<std::pair<std::string, std::string>, LookupResult> result_map;

  size_t text_len = utf8::distance(lookup_string.begin(), lookup_string.end());
  size_t start = std::min(scan_length, text_len);
  auto search_str_it = lookup_string.begin();
  utf8::advance(search_str_it, start, lookup_string.end());

  for (size_t i = std::min(scan_length, text_len); i > 0; i--) {
    std::string search_str(lookup_string.begin(), search_str_it);
    auto processor_results = text_processor::process(search_str);
    for (auto& variant : processor_results) {
      auto deinflection_results = deinflector_.deinflect(variant.text);
      for (auto& deinflection : deinflection_results) {
        auto terms = query_.query_raw(deinflection.text);
        filter_by_pos(terms, deinflection);

        for (auto& term : terms) {
          // deduplicate glossaries
          auto key = std::make_pair(term.expression, term.reading);
          auto it = result_map.find(key);
          if (it != result_map.end()) {
            // we only need the longest matched form
            if (utf8::distance(search_str.begin(), search_str.end()) >
                utf8::distance(it->second.matched.begin(), it->second.matched.end())) {
              it->second = LookupResult{.matched = search_str,
                                        .deinflected = deinflection.text,
                                        .trace = deinflection.trace,
                                        .term = std::move(term),
                                        .preprocessor_steps = variant.steps};
            }
          } else {
            result_map.emplace(key, LookupResult{.matched = search_str,
                                                 .deinflected = deinflection.text,
                                                 .trace = deinflection.trace,
                                                 .term = std::move(term),
                                                 .preprocessor_steps = variant.steps});
          }
        }
      }
    }
    if (i > 1) {
      utf8::prior(search_str_it, lookup_string.begin());
    }
  }

  auto results = result_map | std::views::values | std::views::as_rvalue | std::ranges::to<std::vector>();
  std::vector<std::string> auto_frequency_dictionaries;
  std::optional<std::string_view> frequency_dictionary;
  bool frequency_descending = false;
  switch (options.frequency_order) {
    case LookupFrequencyOrder::Auto:
      auto_frequency_dictionaries = query_.get_freq_dict_order();
      break;
    case LookupFrequencyOrder::Ascending:
    case LookupFrequencyOrder::Descending:
      if (options.frequency_dictionary.has_value()) {
        const auto selected =
            std::ranges::find(query_.freq_dicts_, *options.frequency_dictionary, &DictionaryQuery::Dictionary::name);
        if (selected != query_.freq_dicts_.end()) {
          frequency_dictionary = selected->name;
          frequency_descending = options.frequency_order == LookupFrequencyOrder::Descending;
        }
      }
      break;
    case LookupFrequencyOrder::Disabled:
      break;
  }
  // An empty preference is no preference, as in Yomitan.
  const std::optional<std::string_view> primary_reading =
      options.primary_reading.has_value() && !options.primary_reading->empty() ? options.primary_reading : std::nullopt;
  const size_t retained_count = std::min(results.size(), static_cast<size_t>(max_results));
  auto middle_iter = std::ranges::next(results.begin(), static_cast<std::ptrdiff_t>(retained_count));
  std::ranges::partial_sort(
      results, middle_iter,
      [&auto_frequency_dictionaries, frequency_dictionary, frequency_descending, primary_reading](const auto& a,
                                                                                                  const auto& b) {
        // Yomitan ranks a reading the caller asked for above every other
        // criterion, the longest match included.
        if (primary_reading.has_value()) {
          const bool primary_a = reading::matches_primary(a.term, *primary_reading);
          const bool primary_b = reading::matches_primary(b.term, *primary_reading);
          if (primary_a != primary_b) {
            return primary_a;
          }
        }

        auto len_a = utf8::distance(a.matched.begin(), a.matched.end());
        auto len_b = utf8::distance(b.matched.begin(), b.matched.end());
        if (len_a != len_b) {
          return len_a > len_b;
        }

        auto steps_a = a.preprocessor_steps;
        auto steps_b = b.preprocessor_steps;
        if (steps_a != steps_b) {
          return steps_a < steps_b;
        }

        auto trace_len_a = a.trace.size();
        auto trace_len_b = b.trace.size();
        if (trace_len_a != trace_len_b) {
          return trace_len_a < trace_len_b;
        }

        auto match_a = a.term.expression == a.deinflected;
        auto match_b = b.term.expression == b.deinflected;
        if (match_a != match_b) {
          return match_a > match_b;
        }

        for (const auto& dictionary_name : auto_frequency_dictionaries) {
          const int freq_a = get_freq_value_for_dict(a.term, dictionary_name, false).value_or(INT_MAX);
          const int freq_b = get_freq_value_for_dict(b.term, dictionary_name, false).value_or(INT_MAX);
          if (freq_a != freq_b) {
            return freq_a < freq_b;
          }
        }

        if (frequency_dictionary.has_value()) {
          const auto freq_a = get_freq_value_for_dict(a.term, *frequency_dictionary, frequency_descending);
          const auto freq_b = get_freq_value_for_dict(b.term, *frequency_dictionary, frequency_descending);
          if (freq_a.has_value() != freq_b.has_value()) {
            return freq_a.has_value();
          }
          if (freq_a.has_value() && *freq_a != *freq_b) {
            return frequency_descending ? *freq_a > *freq_b : *freq_a < *freq_b;
          }
        }

        if (a.term.score != b.term.score) {
          return a.term.score > b.term.score;
        }

        auto a_reading_expr_match = a.term.expression == a.term.reading;
        auto b_reading_expr_match = b.term.expression == b.term.reading;
        return a_reading_expr_match > b_reading_expr_match;
      });

  if (results.size() > retained_count) {
    results.resize(retained_count);
  }

  for (auto& r : results) {
    query_.materialize(r.term);
  }

  return results;
}

void Lookup::filter_by_pos(std::vector<TermResult>& terms, const DeinflectionResult& d) {
  if (d.conditions == 0) {
    return;
  }
  std::erase_if(terms, [&](const TermResult& term) {
    auto dict_conditions = Deinflector::pos_to_conditions(split_whitespace(term.rules));
    return (dict_conditions & d.conditions) == 0;
  });
}
