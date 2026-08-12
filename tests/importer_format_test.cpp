#include "hoshidicts/importer.hpp"
#include "hoshidicts/query.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace fs = std::filesystem;

namespace {
void require(bool condition, const char* message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}
}

int main(int argc, char** argv) {
  if (argc != 4) {
    std::cerr << "usage: importer-format-test <fixture.zip> <large.zip> <workdir>\n";
    return 2;
  }
  const fs::path fixture = argv[1];
  const fs::path large = argv[2];
  const fs::path workdir = argv[3];
  fs::remove_all(workdir);
  fs::create_directories(workdir);

  const auto small_result = dictionary_importer::import(fixture.string(), workdir.string());
  require(small_result.success, "small fixture import failed");
  const fs::path small = workdir / small_result.title;
  require(fs::is_regular_file(small / ".hoshidicts_3"), "small import must retain v3 fallback");
  require(!fs::exists(small / "glossary.dict"), "small import unexpectedly wrote a dictionary");

  const auto large_result = dictionary_importer::import(large.string(), workdir.string());
  require(large_result.success, "large fixture import failed");
  const fs::path dictionary = workdir / large_result.title;
  require(fs::is_regular_file(dictionary / ".hoshidicts_4"), "large import must use format v4");
  require(fs::file_size(dictionary / "glossary.dict") > 0, "large import dictionary is empty");

  DictionaryQuery query;
  query.add_term_dict(dictionary.string());
  const auto results = query.query("日本");
  require(!results.empty(), "v4 dictionary query returned no entries");
  require(!results.front().glossaries.empty(), "v4 dictionary query returned no glossary");
  require(!results.front().glossaries.front().glossary.empty(), "v4 glossary failed to decompress");

  fs::remove(dictionary / "glossary.dict");
  DictionaryQuery missing_dictionary;
  missing_dictionary.add_term_dict(dictionary.string());
  require(missing_dictionary.query("日本").empty(), "corrupt v4 dictionary must fail closed");
  return 0;
}
