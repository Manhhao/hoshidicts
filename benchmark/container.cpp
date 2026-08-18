#include <algorithm>
#include <chrono>
#include <format>
#include <fstream>
#include <iostream>
#include <numeric>
#include <string>
#include <vector>

#include "hoshidicts/query.hpp"

namespace {
std::vector<std::string> read_word_list(const std::string& path) {
  std::vector<std::string> words;
  std::ifstream file(path);
  std::string line;
  while (std::getline(file, line)) {
    if (line.ends_with('\r')) {
      line.pop_back();
    }
    std::string word(line, 0, line.find(','));
    if (!word.empty() && word != "Word") {
      words.push_back(std::move(word));
    }
  }
  return words;
}

struct Stats {
  double total = 0;
  double average = 0;
  double min = 0;
  double max = 0;
};

Stats summarize(const std::vector<double>& durations) {
  const auto [min, max] = std::ranges::minmax_element(durations);
  const double total = std::accumulate(durations.begin(), durations.end(), 0.0);
  return {.total = total, .average = total / durations.size(), .min = *min, .max = *max};
}

void report(const std::string& label, const std::vector<double>& open, const std::vector<double>& lookup) {
  const Stats opened = summarize(open);
  const Stats looked_up = summarize(lookup);
  std::cout << std::format("{} open avg: {:.3f}ms min: {:.3f}ms max: {:.3f}ms\n", label, opened.average, opened.min,
                           opened.max);
  std::cout << std::format("{} lookup avg: {:.4f}ms min: {:.4f}ms max: {:.4f}ms total: {:.2f}ms\n", label,
                           looked_up.average, looked_up.min, looked_up.max, looked_up.total);
}

void measure(const std::string& path, const std::vector<std::string>& words, int iterations,
             std::vector<double>& open_durations, std::vector<double>& lookup_durations) {
  for (int i = 0; i < iterations; ++i) {
    const auto open_start = std::chrono::high_resolution_clock::now();
    DictionaryQuery query;
    query.add_term_dict(path);
    const auto open_end = std::chrono::high_resolution_clock::now();
    open_durations.push_back(std::chrono::duration<double, std::milli>(open_end - open_start).count());

    for (const auto& word : words) {
      const auto start = std::chrono::high_resolution_clock::now();
      const auto results = query.query(word);
      const auto end = std::chrono::high_resolution_clock::now();
      lookup_durations.push_back(std::chrono::duration<double, std::milli>(end - start).count());
    }
  }
}
}

int main(int argc, char** argv) {
  if (argc < 5) {
    std::cout << std::format("{} <csv_path> <iterations> <dictionary_dir> <dictionary.hoshi>\n", argv[0]);
    return 1;
  }

  const std::string csv_path = argv[1];
  const int iterations = std::stoi(argv[2]);
  const std::vector<std::string> words = read_word_list(csv_path);
  if (words.empty()) {
    std::cout << std::format("no words in {}\n", csv_path);
    return 1;
  }

  std::vector<double> directory_open;
  std::vector<double> directory_lookup;
  std::vector<double> container_open;
  std::vector<double> container_lookup;
  measure(argv[3], words, iterations, directory_open, directory_lookup);
  measure(argv[4], words, iterations, container_open, container_lookup);

  if (directory_lookup.empty() || container_lookup.empty()) {
    return 1;
  }

  std::cout << std::format("words: {} ({}) iterations: {}\n", csv_path, words.size(), iterations);
  report("directory", directory_open, directory_lookup);
  report("container", container_open, container_lookup);

  return 0;
}
