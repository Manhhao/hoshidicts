#pragma once

#include <optional>
#include <string>
#include <vector>

#include "language.hpp"
#include "query.hpp"

struct LookupResult {
  std::string matched;
  TermResult term;
  std::vector<TraceCandidate> trace_candidates;
};

enum class LookupFrequencyOrder { Auto, Ascending, Descending, Disabled };

struct LookupOptions {
  std::optional<std::string> frequency_dictionary;
  LookupFrequencyOrder frequency_order = LookupFrequencyOrder::Auto;
  std::optional<std::string> primary_reading;
};

class Lookup {
 public:
  Lookup(DictionaryQuery& query, const LanguageProcessor& language) : query_(query), language_(language) {};
  std::vector<LookupResult> lookup(const std::string& lookup_string, int max_results = 16, size_t scan_length = 16,
                                   const LookupOptions& options = {}) const;

 private:
  void filter_by_pos(std::vector<TermResult>& terms, const DeinflectionResult& d) const;

  DictionaryQuery& query_;
  const LanguageProcessor& language_;
};
