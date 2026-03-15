#include <utf8.h>

#include <chrono>
#include <filesystem>
#include <iostream>
#include <ranges>
#include <string>

#include "../src/text_processor/text_processor.hpp"
#include "hoshidicts/deconjugator.hpp"
#include "hoshidicts/importer.hpp"
#include "hoshidicts/lookup.hpp"
#include "hoshidicts/query.hpp"

void print_usage(const char* program) {
  std::cout << std::format("Usage:\n");
  std::cout << std::format("{} import <path/to/dictionary.zip>\n", program);
  std::cout << std::format("{} deconjugate <word>\n", program);
  std::cout << std::format("{} preprocess <word>\n", program);
  std::cout << std::format("{} query <path/to/dictionary> <word>\n", program);
  std::cout << std::format("{} lookup <path/to/dictionary> <lookup_string>\n", program);
  std::cout << std::format("{} freq <path/to/dictionary> <word>\n", program);
}

void cmd_import(const std::string& path) {
  std::filesystem::path zip_path(path);
  std::string output_dir = zip_path.parent_path().string();
  if (output_dir.empty()) {
    output_dir = ".";
  }
  ImportResult result = dictionary_importer::import(path, output_dir);

  if (result.success) {
    std::cout << std::format("title: {}\n", result.title);
    std::cout << std::format("term_count: {}\n", result.term_count);
    std::cout << std::format("meta_count: {}\n", result.meta_count);
    std::cout << std::format("media_count: {}\n", result.media_count);
  } else {
    std::cout << std::format("could not import dictionary:\n");
    for (const auto& error : result.errors) {
      std::cout << std::format(" {}\n", error);
    }
  }
}

void cmd_deconjugate(const std::string& text) {
  Deconjugator deconjugator;
  auto results = deconjugator.deconjugate(text);

  std::cout << std::format("deconjugations for: {} length: {}\n", text, utf8::distance(text.begin(), text.end()));
  std::cout << std::format("found {} candidates\n\n", results.size());

  for (const auto& r : results) {
    std::cout << std::format("{}\n", r.text);
    if (!r.tags.empty()) {
      std::cout << std::format("  tags: ");
      for (size_t i = 0; i < r.tags.size(); ++i) {
        std::cout << std::format("{}{}", r.tags[i], i < r.tags.size() - 1 ? ", " : "");
      }
      std::cout << std::format("\n");
    }
    if (!r.process.empty()) {
      std::cout << std::format("  process: ");
      for (size_t i = 0; i < r.process.size(); ++i) {
        std::cout << std::format("{}{}", r.process[i], i < r.process.size() - 1 ? " -> " : "");
      }
      std::cout << std::format("\n");
    }
  }
}

void cmd_preprocess(const std::string& text) {
  auto results = text_processor::process(text);

  std::cout << std::format("preproccesing for: {} length: {}\n", text, utf8::distance(text.begin(), text.end()));
  std::cout << std::format("found {} variants\n", results.size());

  for (const auto& r : results) {
    std::cout << std::format("{}\n", r.text);
  }
}

void cmd_query(const std::string& db_path, const std::string& expression) {
  DictionaryQuery dict_query;
  dict_query.add_term_dict(db_path);
  auto result = dict_query.query(expression);

  std::cout << std::format("query results for: {} length: {}\n", expression,
                           utf8::distance(expression.begin(), expression.end()));
  std::cout << std::format("{} entries\n", result.size());
  for (const auto& r : result) {
    std::cout << std::format("---------------------------------------------------------------\n");
    std::cout << std::format("{} {} {}\n", r.expression, r.reading, r.rules);
    std::cout << std::format("{} glossary entries\n", r.glossaries.size());
    for (const auto& g : r.glossaries) {
      std::cout << std::format("------\n");
      std::cout << std::format("{}\n", g.dict_name);
      std::cout << std::format("{}\n", g.glossary);
    }
  }
}

void cmd_freq(const std::string& path, const std::string& expression, const std::string& reading) {
  std::vector<TermResult> terms;
  terms.emplace_back(TermResult{.expression = expression, .reading = reading});

  DictionaryQuery query;
  query.add_freq_dict(path);
  query.query_freq(terms);
  std::cout << std::format("frequency entries for: {}\n", expression);
  int count = 0;
  for (auto& freq : terms[0].frequencies) {
    std::cout << std::format("dict: {}\n", freq.dict_name);
    for (auto& freq_entry : freq.frequencies) {
      std::cout << std::format("val: {} display_val: {}\n", freq_entry.value, freq_entry.display_value);
      count++;
    }
  }
  std::cout << std::format("count: {}\n", count);
}

void cmd_lookup(const std::vector<std::string>& db_paths, const std::string& lookup_string, int max_results = 8,
                int scan_length = 16) {
  DictionaryQuery dict_query;
  for (const auto& path : db_paths) {
    dict_query.add_term_dict(path);
  }
  Deconjugator deconjugator;
  Lookup lookup(dict_query, deconjugator);
  auto result = lookup.lookup(lookup_string, max_results, scan_length);

  std::cout << std::format("lookup results for: {} max_results: {} scan_length: {}\n", lookup_string, max_results,
                           scan_length);
  std::cout << std::format("{} results\n", result.size());

  for (const auto& r : result) {
    std::cout << std::format("---------------------------------------------------------------\n");
    std::cout << std::format("{}\n", r.matched);
    if (!r.process.empty()) {
      std::cout << "  ";
      for (size_t i = 0; i < r.process.size(); ++i) {
        std::cout << std::format("{}{}", r.process[i], i < r.process.size() - 1 ? " -> " : "");
      }
      std::cout << "\n";
    }
    std::cout << std::format("{} {}\n", r.term.expression, r.term.reading);
    for (const auto& g : r.term.glossaries) {
      std::cout << std::format("------\n");
      std::cout << std::format("{}\n", g.dict_name);
      std::cout << std::format("{}\n", g.glossary);
    }
  }

  std::cout << std::format("styles: \n");
  for (const auto& s : dict_query.get_styles()) {
    std::cout << std::format("{}\n", s.dict_name);
    std::cout << std::format("{}\n", s.styles);
  }
}

int main(int argc, char* argv[]) {
  if (argc < 2) {
    print_usage(argv[0]);
    return 1;
  }

  const auto begin = std::chrono::steady_clock::now();
  std::string_view command = argv[1];

  if (command == "import" && argc >= 3) {
    cmd_import(argv[2]);
  } else if (command == "deconjugate" && argc >= 3) {
    cmd_deconjugate(argv[2]);
  } else if (command == "preprocess" && argc >= 3) {
    cmd_preprocess(argv[2]);
  } else if (command == "query" && argc >= 4) {
    cmd_query(argv[2], argv[3]);
  } else if (command == "lookup" && argc >= 4) {
    auto db_paths = std::views::counted(argv + 2, argc - 3) |
                    std::views::transform([](const char* arg) { return std::string(arg); }) |
                    std::ranges::to<std::vector>();
    std::string term = argv[argc - 1];
    cmd_lookup(db_paths, term);
  } else if (command == "freq" && argc >= 5) {
    cmd_freq(argv[2], argv[3], argv[4]);
  } else {
    print_usage(argv[0]);
    return 1;
  }

  const auto end = std::chrono::steady_clock::now();
  std::chrono::duration<double, std::milli> duration = end - begin;
  std::cout << std::format("runtime: {}ms\n", duration.count());

  return 0;
}