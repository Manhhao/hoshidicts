#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "deinflector.hpp"
#include "query.hpp"

struct LookupResult {
  std::string matched;
  std::string deinflected;
  std::vector<TransformGroup> trace;
  TermResult term;
  int preprocessor_steps;
};

enum class LookupFrequencyOrder { Auto, Ascending, Descending, Disabled };

struct LookupOptions {
  std::optional<std::string_view> primary_reading;
  std::optional<std::string_view> frequency_dictionary;
  LookupFrequencyOrder frequency_order = LookupFrequencyOrder::Auto;
};

class Lookup {
 public:
  Lookup(DictionaryQuery& query, Deinflector& deinflector) : query_(query), deinflector_(deinflector) {};
  std::vector<LookupResult> lookup(const std::string& lookup_string, int max_results = 16,
                                   size_t scan_length = 16) const;
  std::vector<LookupResult> lookup(const std::string& lookup_string, int max_results, size_t scan_length,
                                   const LookupOptions& options) const;

 private:
  static void filter_by_pos(std::vector<TermResult>& terms, const DeinflectionResult& d);

  DictionaryQuery& query_;
  Deinflector& deinflector_;
};
