#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <numeric>
#include <vector>

#include "hoshidicts/importer.hpp"

int main(int argc, char** argv) {
  if (argc < 3) {
    std::cout << std::format("{} <zip_path> <iterations>", argv[0]);
    return 1;
  }

  const std::string zip_path = argv[1];
  const int iterations = std::stoi(argv[2]);
  std::vector<double> durations;
  std::string dict_title;
  size_t term_count = 0;
  size_t media_count = 0;

  for (int i = 0; i < iterations; ++i) {
    const auto start = std::chrono::high_resolution_clock::now();
    const auto result = dictionary_importer::import(zip_path, ".");
    const auto end = std::chrono::high_resolution_clock::now();

    if (result.success) {
      if (dict_title.empty()) {
        dict_title = result.title;
      }
      if (term_count == 0) {
        term_count = result.term_count;
        media_count = result.media_count;
      }
      const std::chrono::duration<double, std::milli> elapsed = end - start;
      durations.push_back(elapsed.count());
      std::filesystem::remove_all(result.title);
    }
  }

  if (durations.empty()) {
    return 1;
  }

  const auto [min, max] = std::ranges::minmax_element(durations);
  const double total = std::accumulate(durations.begin(), durations.end(), 0.0);
  const double average = total / durations.size();

  std::cout << std::format("dict: {} iterations: {}\n", dict_title, iterations);
  std::cout << std::format("term_count: {}\n", term_count);
  std::cout << std::format("media_count: {}\n", media_count);
  std::cout << std::format("total: {:.2f}ms\n", total);
  std::cout << std::format("avg: {:.2f}ms\n", average);
  std::cout << std::format("min: {:.2f}ms\n", *min);
  std::cout << std::format("max: {:.2f}ms\n", *max);

  return 0;
}
