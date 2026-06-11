#include "hoshidicts/lookup.hpp"

#include <utf8.h>

#include <algorithm>
#include <climits>
#include <map>
#include <ranges>
#include <set>
#include <sstream>

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

void filter_terms_by_pos(const LanguageProcessor& language, std::vector<TermResult>& terms,
                         const DeinflectionResult& deinflection) {
  if (deinflection.conditions == 0) {
    return;
  }

  std::erase_if(terms, [&](const TermResult& term) {
    const auto parts_of_speech = split_whitespace(term.rules);
    const auto dict_conditions = language.pos_to_conditions(parts_of_speech);
    return (dict_conditions & deinflection.conditions) == 0;
  });
}

std::vector<int> get_freq_values_for_dict(const TermResult& term, const std::string& dict_name) {
  for (const auto& frequency_entry : term.frequencies) {
    if (frequency_entry.dict_name != dict_name) {
      continue;
    }

    std::vector<int> values;
    for (const auto& frequency : frequency_entry.frequencies) {
      if (frequency.value >= 0) {
        values.push_back(frequency.value);
      }
    }
    std::ranges::sort(values);
    return values;
  }

  return {INT_MAX};
}

bool is_v2_redirect_only_glossary(const GlossaryEntry& glossary) {
  return glossary.dictionary_format_version >= 2 && !glossary.redirects.empty() && glossary.compressed_size == 0 &&
         glossary.glossary.empty();
}

bool is_dictionary_redirect_only_term(const TermResult& term) {
  return !term.glossaries.empty() && std::ranges::all_of(term.glossaries, is_v2_redirect_only_glossary);
}

void remove_dictionary_redirect_only_terms(std::vector<TermResult>& terms) {
  std::erase_if(terms, [](const TermResult& term) { return is_dictionary_redirect_only_term(term); });
}

void add_dictionary_redirect(std::vector<DeinflectionResult>& results, std::set<std::string>& seen,
                             const DictionaryRedirect& redirect, const DeinflectionResult& source) {
  if (redirect.form_of.empty()) {
    return;
  }

  DeinflectionResult result{.text = redirect.form_of, .conditions = 0, .trace = source.trace};
  for (const auto& inflection_rule : redirect.inflection_rules) {
    if (!inflection_rule.empty()) {
      result.trace.push_back({.name = inflection_rule, .description = ""});
    }
  }

  std::string key = result.text;
  key.push_back('\n');
  for (const auto& group : result.trace) {
    key += group.name;
    key.push_back('\n');
  }
  if (seen.insert(std::move(key)).second) {
    results.push_back(std::move(result));
  }
}

std::vector<DeinflectionResult> get_dictionary_deinflections(const std::vector<TermResult>& terms,
                                                             const DeinflectionResult& source) {
  std::vector<DeinflectionResult> results;
  std::set<std::string> seen;

  for (const auto& term : terms) {
    for (const auto& glossary : term.glossaries) {
      if (glossary.dictionary_format_version < 2) {
        continue;
      }
      for (const auto& redirect : glossary.redirects) {
        add_dictionary_redirect(results, seen, redirect, source);
      }
    }
  }

  return results;
}
}

std::vector<LookupResult> Lookup::lookup(const std::string& lookup_string, int max_results, size_t scan_length) const {
  std::map<std::pair<std::string, std::string>, LookupResult> result_map;

  auto add_lookup_terms = [&](const std::string& search_str, const std::string& deinflected,
                              const std::vector<TransformGroup>& trace, int preprocessor_steps,
                              std::vector<TermResult> terms) {
    remove_dictionary_redirect_only_terms(terms);
    terms = DictionaryQuery::merge_term_entries(std::move(terms));
    query_.query_freq(terms);
    query_.query_pitch(terms);

    for (auto& term : terms) {
      // deduplicate glossaries
      auto key = std::make_pair(term.expression, term.reading);
      auto it = result_map.find(key);
      if (it != result_map.end()) {
        // we only need the longest matched form
        if (utf8::distance(search_str.begin(), search_str.end()) >
            utf8::distance(it->second.matched.begin(), it->second.matched.end())) {
          it->second = LookupResult{.matched = search_str,
                                    .deinflected = deinflected,
                                    .trace = trace,
                                    .term = std::move(term),
                                    .preprocessor_steps = preprocessor_steps};
        }
      } else {
        result_map.emplace(key, LookupResult{.matched = search_str,
                                             .deinflected = deinflected,
                                             .trace = trace,
                                             .term = std::move(term),
                                             .preprocessor_steps = preprocessor_steps});
      }
    }
  };

  size_t text_len = utf8::distance(lookup_string.begin(), lookup_string.end());
  size_t start = std::min(scan_length, text_len);
  auto search_str_it = lookup_string.begin();
  utf8::advance(search_str_it, start, lookup_string.end());

  for (size_t i = std::min(scan_length, text_len); i > 0; i--) {
    std::string search_str(lookup_string.begin(), search_str_it);
    auto processor_results = language_.preprocess(search_str);
    for (auto& variant : processor_results) {
      auto deinflection_results = language_.deinflect(variant.text);
      for (auto& deinflection : deinflection_results) {
        auto postprocessor_results = language_.postprocess(deinflection.text);
        for (auto& postprocessed : postprocessor_results) {
          auto terms = query_.query_raw_entries(postprocessed.text);
          filter_by_pos(terms, deinflection);

          const auto dictionary_deinflections = get_dictionary_deinflections(terms, deinflection);
          add_lookup_terms(search_str, postprocessed.text, deinflection.trace, variant.steps + postprocessed.steps,
                           std::move(terms));

          for (const auto& dictionary_deinflection : dictionary_deinflections) {
            auto redirected_terms = query_.query_raw_entries(dictionary_deinflection.text);
            filter_by_pos(redirected_terms, dictionary_deinflection);
            add_lookup_terms(search_str, dictionary_deinflection.text, dictionary_deinflection.trace,
                             variant.steps + postprocessed.steps, std::move(redirected_terms));
          }
        }
      }
    }
    if (i > 1) {
      utf8::prior(search_str_it, lookup_string.begin());
    }
  }

  auto results = result_map | std::views::values | std::views::as_rvalue | std::ranges::to<std::vector>();
  const auto freq_dict_order = query_.get_freq_dict_order();
  auto middle_iter = std::ranges::next(results.begin(), max_results, results.end());
  std::ranges::partial_sort(results, middle_iter, [&freq_dict_order](const auto& a, const auto& b) {
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

    for (const auto& dict_name : freq_dict_order) {
      const auto freq_a = get_freq_values_for_dict(a.term, dict_name);
      const auto freq_b = get_freq_values_for_dict(b.term, dict_name);
      if (freq_a != freq_b) {
        return freq_a < freq_b;
      }
    }

    auto a_reading_expr_match = a.term.expression == a.term.reading;
    auto b_reading_expr_match = b.term.expression == b.term.reading;
    return a_reading_expr_match > b_reading_expr_match;
  });

  if (results.size() > static_cast<size_t>(max_results)) {
    results.resize(max_results);
  }

  for (auto& r : results) {
    query_.materialize(r.term);
  }
  std::erase_if(results, [](const LookupResult& result) { return result.term.glossaries.empty(); });

  return results;
}

void Lookup::filter_by_pos(std::vector<TermResult>& terms, const DeinflectionResult& d) const {
  filter_terms_by_pos(language_, terms, d);
}
