#include "hoshidicts/lookup.hpp"

#include <utf8.h>

#include <algorithm>
#include <climits>
#include <cstddef>
#include <map>
#include <ranges>
#include <sstream>
#include <tuple>
#include <utility>

namespace {
using HeadwordKey = std::pair<std::string, std::string>;
using LookupResultKey = HeadwordKey;
using TraceSortKey = std::tuple<int, size_t, bool>;

struct EffectivePosConditions {
  bool missing;
  uint32_t conditions;
};

struct TermMetadata {
  std::vector<FrequencyEntry> frequencies;
  std::vector<PitchEntry> pitches;
};

std::vector<std::string> split_whitespace(const std::string& str) {
  std::vector<std::string> result;
  std::istringstream iss(str);
  std::string token;
  while (iss >> token) {
    result.push_back(std::move(token));
  }
  return result;
}

HeadwordKey make_headword_key(const TermResult& term) { return {term.expression, term.reading}; }

bool has_pos_rules(const TermResult& term) { return !split_whitespace(term.rules).empty(); }

uint32_t pos_conditions_for_term(const LanguageProcessor& language, const TermResult& term) {
  return language.pos_to_conditions(split_whitespace(term.rules));
}

std::map<HeadwordKey, uint32_t> aggregate_pos_conditions(const LanguageProcessor& language,
                                                         const std::vector<TermResult>& terms) {
  std::map<HeadwordKey, uint32_t> result;
  for (const auto& term : terms) {
    if (!has_pos_rules(term)) {
      continue;
    }
    const auto conditions = pos_conditions_for_term(language, term);
    if (conditions == 0) {
      continue;
    }
    result[make_headword_key(term)] |= conditions;
  }
  return result;
}

EffectivePosConditions effective_pos_conditions_for_term(
    const LanguageProcessor& language, const TermResult& term,
    const std::map<HeadwordKey, uint32_t>& aggregated_pos_conditions) {
  if (has_pos_rules(term)) {
    return {.missing = false, .conditions = pos_conditions_for_term(language, term)};
  }

  const auto it = aggregated_pos_conditions.find(make_headword_key(term));
  if (it == aggregated_pos_conditions.end()) {
    return {.missing = true, .conditions = 0};
  }
  return {.missing = false, .conditions = it->second};
}

void filter_terms_by_pos(const LanguageProcessor& language, std::vector<TermResult>& terms,
                         const DeinflectionResult& deinflection) {
  if (deinflection.conditions == 0) {
    return;
  }

  const auto aggregated_pos_conditions = aggregate_pos_conditions(language, terms);
  std::erase_if(terms, [&](const TermResult& term) {
    const auto dict_conditions = effective_pos_conditions_for_term(language, term, aggregated_pos_conditions);
    if (dict_conditions.missing) {
      return language.missing_pos_policy() == MissingPosPolicy::Filter;
    }
    return (dict_conditions.conditions & deinflection.conditions) == 0;
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

bool same_glossary_entry_key(const GlossaryEntry& a, const GlossaryEntry& b) {
  return a.dict_name == b.dict_name && a.definition_tags == b.definition_tags && a.term_tags == b.term_tags &&
         a.compressed_data == b.compressed_data && a.compressed_size == b.compressed_size &&
         a.dictionary_format_version == b.dictionary_format_version && a.glossary == b.glossary;
}

void add_glossary_entry(std::vector<GlossaryEntry>& glossaries, GlossaryEntry glossary) {
  const auto duplicate = std::ranges::any_of(
      glossaries, [&](const GlossaryEntry& existing) { return same_glossary_entry_key(existing, glossary); });
  if (!duplicate) {
    glossaries.push_back(std::move(glossary));
  }
}

void merge_term_glossaries(TermResult& target, TermResult source) {
  for (auto& glossary : source.glossaries) {
    add_glossary_entry(target.glossaries, std::move(glossary));
  }
}

void merge_term_rules(TermResult& target, const std::string& source_rules) {
  if (source_rules.empty()) {
    return;
  }
  if (target.rules.empty()) {
    target.rules = source_rules;
    return;
  }

  auto existing_rules = split_whitespace(target.rules);
  for (const auto& rule : split_whitespace(source_rules)) {
    if (std::ranges::find(existing_rules, rule) != existing_rules.end()) {
      continue;
    }
    target.rules += " ";
    target.rules += rule;
    existing_rules.push_back(rule);
  }
}

void merge_term_data(TermResult& target, TermResult source) {
  merge_term_rules(target, source.rules);
  merge_term_glossaries(target, std::move(source));
}

LookupResultKey make_lookup_result_key(const TermResult& term) { return make_headword_key(term); }

std::vector<TermResult> merge_lookup_terms(std::vector<TermResult> terms) {
  std::map<LookupResultKey, TermResult> term_map;
  for (auto& term : terms) {
    auto [it, inserted] = term_map.try_emplace(make_lookup_result_key(term));
    if (inserted) {
      it->second = std::move(term);
      continue;
    }
    merge_term_data(it->second, std::move(term));
  }

  return term_map | std::views::values | std::views::as_rvalue | std::ranges::to<std::vector>();
}

TraceSource merge_trace_sources(TraceSource a, TraceSource b) {
  if (a == b) {
    return a;
  }
  return TraceSource::Both;
}

bool traces_equal(const std::vector<TransformGroup>& a, const std::vector<TransformGroup>& b) {
  if (a.size() != b.size()) {
    return false;
  }

  for (size_t i = 0; i < a.size(); ++i) {
    if (a[i].name != b[i].name) {
      return false;
    }
  }
  return true;
}

bool same_trace_candidate_key(const TraceCandidate& a, const TraceCandidate& b) {
  return a.deinflected == b.deinflected && a.preprocessor_steps == b.preprocessor_steps &&
         traces_equal(a.trace, b.trace);
}

void merge_trace_descriptions(std::vector<TransformGroup>& existing, const std::vector<TransformGroup>& candidate) {
  for (size_t i = 0; i < existing.size() && i < candidate.size(); ++i) {
    if (existing[i].description.empty() && !candidate[i].description.empty()) {
      existing[i].description = candidate[i].description;
    }
  }
}

void add_trace_candidate(std::vector<TraceCandidate>& candidates, TraceCandidate candidate) {
  auto it = std::ranges::find_if(
      candidates, [&](const TraceCandidate& existing) { return same_trace_candidate_key(existing, candidate); });
  if (it == candidates.end()) {
    candidates.push_back(std::move(candidate));
    return;
  }

  it->source = merge_trace_sources(it->source, candidate.source);
  merge_trace_descriptions(it->trace, candidate.trace);
}

void add_deinflection_candidate(std::vector<DeinflectionResult>& results, const std::string& text, uint32_t conditions,
                                TraceCandidate candidate) {
  auto it = std::ranges::find_if(results, [&](const DeinflectionResult& result) {
    return result.text == text && result.conditions == conditions;
  });
  if (it == results.end()) {
    DeinflectionResult result{.text = text, .conditions = conditions};
    result.trace_candidates.push_back(std::move(candidate));
    results.push_back(std::move(result));
    return;
  }

  add_trace_candidate(it->trace_candidates, std::move(candidate));
}

std::vector<TraceCandidate> make_lookup_trace_candidates(const DeinflectionResult& deinflection,
                                                         const std::string& deinflected, int preprocessor_steps) {
  std::vector<TraceCandidate> candidates;
  if (deinflection.trace_candidates.empty()) {
    candidates.push_back({.deinflected = deinflected,
                          .preprocessor_steps = preprocessor_steps,
                          .source = TraceSource::Algorithm,
                          .trace = {}});
    return candidates;
  }

  candidates.reserve(deinflection.trace_candidates.size());
  for (auto candidate : deinflection.trace_candidates) {
    candidate.deinflected = deinflected;
    candidate.preprocessor_steps = preprocessor_steps;
    candidates.push_back(std::move(candidate));
  }
  return candidates;
}

void add_dictionary_redirect(std::vector<DeinflectionResult>& results, const DictionaryRedirect& redirect,
                             const std::vector<TraceCandidate>& source_candidates) {
  if (redirect.form_of.empty()) {
    return;
  }

  for (const auto& source_candidate : source_candidates) {
    TraceCandidate candidate = source_candidate;
    candidate.deinflected = redirect.form_of;
    candidate.source = candidate.trace.empty() ? TraceSource::Dictionary : TraceSource::Both;
    for (const auto& inflection_rule : redirect.inflection_rules) {
      if (!inflection_rule.empty()) {
        candidate.trace.push_back({.name = inflection_rule, .description = ""});
      }
    }

    add_deinflection_candidate(results, redirect.form_of, 0, std::move(candidate));
  }
}

std::vector<DeinflectionResult> get_dictionary_deinflections(const std::vector<TermResult>& terms,
                                                             const std::vector<TraceCandidate>& source_candidates) {
  std::vector<DeinflectionResult> results;

  for (const auto& term : terms) {
    for (const auto& glossary : term.glossaries) {
      if (glossary.dictionary_format_version < 2) {
        continue;
      }
      for (const auto& redirect : glossary.redirects) {
        add_dictionary_redirect(results, redirect, source_candidates);
      }
    }
  }

  return results;
}

TraceSortKey best_trace_sort_key(const LookupResult& result) {
  if (result.trace_candidates.empty()) {
    return {INT_MAX, static_cast<size_t>(INT_MAX), true};
  }

  return std::ranges::min(result.trace_candidates | std::views::transform([&](const TraceCandidate& candidate) {
                            return TraceSortKey{candidate.preprocessor_steps, candidate.trace.size(),
                                                result.term.expression != candidate.deinflected};
                          }));
}

}

std::vector<LookupResult> Lookup::lookup(const std::string& lookup_string, int max_results, size_t scan_length) const {
  std::map<LookupResultKey, LookupResult> result_map;
  std::map<std::string, std::vector<TermResult>> raw_entries_cache;
  std::map<HeadwordKey, TermMetadata> term_metadata_cache;

  auto query_raw_entries_cached = [&](const std::string& expression) {
    const auto cached = raw_entries_cache.find(expression);
    if (cached != raw_entries_cache.end()) {
      return cached->second;
    }

    auto terms = query_.query_raw_entries(expression);
    raw_entries_cache.emplace(expression, terms);
    return terms;
  };

  auto add_term_metadata = [&](std::vector<TermResult>& terms) {
    std::vector<TermResult> terms_to_query;
    std::vector<std::pair<size_t, HeadwordKey>> uncached_terms;
    std::map<HeadwordKey, size_t> uncached_query_indices;

    for (size_t i = 0; i < terms.size(); ++i) {
      auto key = make_headword_key(terms[i]);
      const auto cached = term_metadata_cache.find(key);
      if (cached != term_metadata_cache.end()) {
        terms[i].frequencies = cached->second.frequencies;
        terms[i].pitches = cached->second.pitches;
        continue;
      }

      uncached_terms.emplace_back(i, key);
      auto [uncached, inserted] = uncached_query_indices.try_emplace(key, terms_to_query.size());
      if (inserted) {
        terms_to_query.push_back({.expression = terms[i].expression, .reading = terms[i].reading});
      }
    }

    if (terms_to_query.empty()) {
      return;
    }

    query_.query_freq(terms_to_query);
    query_.query_pitch(terms_to_query);

    for (const auto& [key, query_index] : uncached_query_indices) {
      auto metadata = TermMetadata{.frequencies = terms_to_query[query_index].frequencies,
                                   .pitches = terms_to_query[query_index].pitches};
      term_metadata_cache.emplace(key, std::move(metadata));
    }

    for (const auto& [term_index, key] : uncached_terms) {
      const auto cached = term_metadata_cache.find(key);
      terms[term_index].frequencies = cached->second.frequencies;
      terms[term_index].pitches = cached->second.pitches;
    }
  };

  auto add_lookup_terms = [&](const std::string& search_str, const std::vector<TraceCandidate>& trace_candidates,
                              std::vector<TermResult> terms) {
    remove_dictionary_redirect_only_terms(terms);
    terms = merge_lookup_terms(std::move(terms));
    add_term_metadata(terms);

    for (auto& term : terms) {
      auto key = make_lookup_result_key(term);
      auto it = result_map.find(key);
      if (it != result_map.end()) {
        // we only need the longest matched form
        if (utf8::distance(search_str.begin(), search_str.end()) >
            utf8::distance(it->second.matched.begin(), it->second.matched.end())) {
          it->second =
              LookupResult{.matched = search_str, .term = std::move(term), .trace_candidates = trace_candidates};
        } else if (utf8::distance(search_str.begin(), search_str.end()) ==
                   utf8::distance(it->second.matched.begin(), it->second.matched.end())) {
          for (const auto& candidate : trace_candidates) {
            add_trace_candidate(it->second.trace_candidates, candidate);
          }
          merge_term_data(it->second.term, std::move(term));
        }
      } else {
        result_map.emplace(
            key, LookupResult{.matched = search_str, .term = std::move(term), .trace_candidates = trace_candidates});
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
          auto terms = query_raw_entries_cached(postprocessed.text);
          filter_by_pos(terms, deinflection);

          auto trace_candidates =
              make_lookup_trace_candidates(deinflection, postprocessed.text, variant.steps + postprocessed.steps);
          const auto dictionary_deinflections = get_dictionary_deinflections(terms, trace_candidates);
          add_lookup_terms(search_str, trace_candidates, std::move(terms));

          for (const auto& dictionary_deinflection : dictionary_deinflections) {
            auto redirected_terms = query_raw_entries_cached(dictionary_deinflection.text);
            filter_by_pos(redirected_terms, dictionary_deinflection);
            add_lookup_terms(search_str, dictionary_deinflection.trace_candidates, std::move(redirected_terms));
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

    auto trace_key_a = best_trace_sort_key(a);
    auto trace_key_b = best_trace_sort_key(b);
    if (trace_key_a != trace_key_b) {
      return trace_key_a < trace_key_b;
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
