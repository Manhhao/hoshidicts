#include "hoshidicts/lookup.hpp"

#include <ankerl/unordered_dense.h>
#include <utf8.h>
#include <xxh3.h>

#include <algorithm>
#include <climits>
#include <numeric>
#include <optional>
#include <ranges>
#include <string_view>
#include <vector>

#include "query_internal.hpp"
#include "text_processor/text_processor.hpp"

namespace {
bool is_space(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' || c == '\r'; }

void split_whitespace(std::string_view str, std::vector<std::string>& result) {
  result.clear();
  size_t i = 0;
  const size_t n = str.size();
  while (i < n) {
    while (i < n && is_space(str[i])) {
      ++i;
    }
    const size_t begin = i;
    while (i < n && !is_space(str[i])) {
      ++i;
    }
    if (i > begin) {
      result.emplace_back(str, begin, i - begin);
    }
  }
}

struct Candidate {
  size_t matched_len;
  const DeinflectionResult* deinflection;
  RawTerm* term;
  uint32_t store_index;
  int steps;
};

uint64_t key_hash(std::string_view expression, std::string_view reading) {
  return XXH3_64bits_withSeed(reading.data(), reading.size(), XXH3_64bits(expression.data(), expression.size()));
}

bool key_less(const RawTerm& a, const RawTerm& b) {
  const int c = a.expression.compare(b.expression);
  return c != 0 ? c < 0 : a.reading < b.reading;
}

std::optional<int> get_freq_value_for_dict(const RawTerm& term, std::string_view dictionary_name, bool descending) {
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

bool matches_primary_reading(const RawTerm& term, std::string_view primary_reading) {
  return term.reading == primary_reading;
}
}

std::vector<LookupResult> Lookup::lookup(const std::string& lookup_string, int max_results, size_t scan_length,
                                         const LookupOptions& options) const {
  std::vector<Candidate> candidates;
  ankerl::unordered_dense::map<uint64_t, uint32_t> index;
  std::vector<std::vector<DeinflectionResult>> deinflection_store;
  std::vector<RawTerms> term_store;

  size_t text_len = utf8::distance(lookup_string.begin(), lookup_string.end());
  size_t start = std::min(scan_length, text_len);
  auto search_str_it = lookup_string.begin();
  utf8::advance(search_str_it, start, lookup_string.end());

  auto find_candidate = [&](uint64_t h, const RawTerm& term) -> Candidate* {
    auto it = index.find(h);
    if (it == index.end()) {
      return nullptr;
    }
    Candidate& hit = candidates[it->second];
    if (hit.term->expression == term.expression && hit.term->reading == term.reading) {
      return &hit;
    }
    for (auto& c : candidates) {
      if (c.term->expression == term.expression && c.term->reading == term.reading) {
        return &c;
      }
    }
    return nullptr;
  };

  for (size_t i = start; i > 0; i--) {
    const std::string_view search_str(lookup_string.begin(), search_str_it);
    auto processor_results = text_processor::process(search_str);
    for (auto& variant : processor_results) {
      auto deinflection_results = deinflector_.deinflect(variant.text);
      for (auto& deinflection : deinflection_results) {
        auto terms = query_.query_raw(deinflection.text);
        filter_by_pos(terms, deinflection);

        const auto store_index = static_cast<uint32_t>(term_store.size());
        for (auto& term : terms.terms) {
          const uint64_t h = key_hash(term.expression, term.reading);
          Candidate* existing = find_candidate(h, term);
          if (existing != nullptr) {
            if (search_str.size() > existing->matched_len) {
              existing->matched_len = search_str.size();
              existing->deinflection = &deinflection;
              existing->term = &term;
              existing->store_index = store_index;
              existing->steps = variant.steps;
            }
          } else {
            index.try_emplace(h, static_cast<uint32_t>(candidates.size()));
            candidates.push_back(Candidate{.matched_len = search_str.size(),
                                           .deinflection = &deinflection,
                                           .term = &term,
                                           .store_index = store_index,
                                           .steps = variant.steps});
          }
        }
        term_store.push_back(std::move(terms));
      }
      deinflection_store.push_back(std::move(deinflection_results));
    }
    if (i > 1) {
      utf8::prior(search_str_it, lookup_string.begin());
    }
  }

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
  std::string_view primary_reading;
  if (options.primary_reading.has_value()) {
    primary_reading = *options.primary_reading;
  }
  const size_t retained_count = std::min(candidates.size(), static_cast<size_t>(max_results));
  auto less = [&auto_frequency_dictionaries, frequency_dictionary, frequency_descending, primary_reading](
                  const Candidate& a, const Candidate& b) {
        if (!primary_reading.empty()) {
          const bool primary_a = matches_primary_reading(*a.term, primary_reading);
          const bool primary_b = matches_primary_reading(*b.term, primary_reading);
          if (primary_a != primary_b) {
            return primary_a;
          }
        }

        if (a.matched_len != b.matched_len) {
          return a.matched_len > b.matched_len;
        }

        auto steps_a = a.steps;
        auto steps_b = b.steps;
        if (steps_a != steps_b) {
          return steps_a < steps_b;
        }

        auto trace_len_a = a.deinflection->trace.size();
        auto trace_len_b = b.deinflection->trace.size();
        if (trace_len_a != trace_len_b) {
          return trace_len_a < trace_len_b;
        }

        auto match_a = a.term->expression == a.deinflection->text;
        auto match_b = b.term->expression == b.deinflection->text;
        if (match_a != match_b) {
          return match_a > match_b;
        }

        for (const auto& dictionary_name : auto_frequency_dictionaries) {
          const int freq_a = get_freq_value_for_dict(*a.term, dictionary_name, false).value_or(INT_MAX);
          const int freq_b = get_freq_value_for_dict(*b.term, dictionary_name, false).value_or(INT_MAX);
          if (freq_a != freq_b) {
            return freq_a < freq_b;
          }
        }

        if (frequency_dictionary.has_value()) {
          const auto freq_a = get_freq_value_for_dict(*a.term, *frequency_dictionary, frequency_descending);
          const auto freq_b = get_freq_value_for_dict(*b.term, *frequency_dictionary, frequency_descending);
          if (freq_a.has_value() != freq_b.has_value()) {
            return freq_a.has_value();
          }
          if (freq_a.has_value() && *freq_a != *freq_b) {
            return frequency_descending ? *freq_a > *freq_b : *freq_a < *freq_b;
          }
        }

        if (a.term->score != b.term->score) {
          return a.term->score > b.term->score;
        }

        auto a_reading_expr_match = a.term->expression == a.term->reading;
        auto b_reading_expr_match = b.term->expression == b.term->reading;
        return a_reading_expr_match > b_reading_expr_match;
      };

  std::vector<uint32_t> order(candidates.size());
  std::iota(order.begin(), order.end(), uint32_t{0});
  std::ranges::sort(order,
                    [&](uint32_t ia, uint32_t ib) { return key_less(*candidates[ia].term, *candidates[ib].term); });
  auto order_middle = std::ranges::next(order.begin(), static_cast<std::ptrdiff_t>(retained_count));
  std::ranges::partial_sort(order, order_middle,
                            [&](uint32_t ia, uint32_t ib) { return less(candidates[ia], candidates[ib]); });

  std::vector<LookupResult> retained;
  retained.reserve(retained_count);
  for (auto it = order.begin(); it != order_middle; ++it) {
    Candidate& c = candidates[*it];
    retained.push_back(LookupResult{.matched = lookup_string.substr(0, c.matched_len),
                                    .deinflected = c.deinflection->text,
                                    .trace = c.deinflection->trace,
                                    .term = query_.build_term(term_store[c.store_index], *c.term),
                                    .preprocessor_steps = c.steps});
  }

  for (auto& r : retained) {
    query_.materialize(r.term);
  }

  return retained;
}

void Lookup::filter_by_pos(RawTerms& terms, const DeinflectionResult& d) {
  if (d.conditions == 0) {
    return;
  }
  std::vector<std::string> tokens;
  std::erase_if(terms.terms, [&](const RawTerm& term) {
    uint32_t dict_conditions = 0;
    for (uint32_t i = term.first_glossary; i != UINT32_MAX; i = terms.glossaries[i].next) {
      split_whitespace(terms.glossaries[i].rules, tokens);
      dict_conditions |= Deinflector::pos_to_conditions(tokens);
    }
    return (dict_conditions & d.conditions) == 0;
  });
}
