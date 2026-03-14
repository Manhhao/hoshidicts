#include "hoshidicts/lookup.hpp"

#include <algorithm>
#include <chrono>
#include <format>
#include <iostream>
#include <numeric>
#include <vector>

#include "hoshidicts/deinflector.hpp"
#include "hoshidicts/query.hpp"

int main(int argc, char** argv) {
  if (argc < 4) {
    std::cout << std::format("{} <dict_path> <word> <iterations>", argv[0]);
    return 1;
  }

  const std::string dict_path = argv[1];
  const std::string word = argv[2];
  const int iterations = std::stoi(argv[3]);

  DictionaryQuery query;
  query.add_term_dict(dict_path);
  Deinflector deinflector;
  Lookup lookup(query, deinflector);

  std::vector<double> durations;
  for (int i = 0; i < iterations; ++i) {
    const auto start = std::chrono::high_resolution_clock::now();
    const auto results = lookup.lookup(word);
    const auto end = std::chrono::high_resolution_clock::now();

    const std::chrono::duration<double, std::milli> elapsed = end - start;
    durations.push_back(elapsed.count());
  }

  if (durations.empty()) {
    return 1;
  }

  const auto [min, max] = std::ranges::minmax_element(durations);
  const double total = std::accumulate(durations.begin(), durations.end(), 0.0);
  const double average = total / durations.size();

  std::cout << std::format("word: {} iterations: {}", word, iterations);
  std::cout << std::format("total: {:.2f}ms", total);
  std::cout << std::format("avg: {:.2f}ms", average);
  std::cout << std::format("min: {:.2f}ms", *min);
  std::cout << std::format("max: {:.2f}ms", *max);

  return 0;
}
