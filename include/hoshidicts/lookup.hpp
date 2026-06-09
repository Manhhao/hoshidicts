#pragma once

#include <string>
#include <vector>

#include "deinflector.hpp"
#include "language.hpp"
#include "query.hpp"

struct LookupResult {
  std::string matched;
  std::string deinflected;
  std::vector<TransformGroup> trace;
  TermResult term;
  int preprocessor_steps;
};

class Lookup {
 public:
  Lookup(DictionaryQuery& query, Deinflector& deinflector)
      : query_(query), language_(static_cast<const LanguageProcessor&>(deinflector)) {};
  Lookup(DictionaryQuery& query, const LanguageProcessor& language) : query_(query), language_(language) {};
  std::vector<LookupResult> lookup(const std::string& lookup_string, int max_results = 16,
                                   size_t scan_length = 16) const;

 private:
  void filter_by_pos(std::vector<TermResult>& terms, const DeinflectionResult& d) const;

  DictionaryQuery& query_;
  const LanguageProcessor& language_;
};
