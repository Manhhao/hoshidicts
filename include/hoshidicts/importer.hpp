#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

struct SummaryItemCount {
  size_t total = 0;
};

using SummaryMetaCount = std::map<std::string, size_t>;

struct SummaryCounts {
  SummaryItemCount terms;
  SummaryMetaCount termMeta;
  SummaryItemCount kanji;
  SummaryMetaCount kanjiMeta;
  SummaryItemCount tagMeta;
  SummaryItemCount media;
};

struct Summary {
  std::string title;
  std::string revision;
  bool sequenced = false;
  std::optional<std::string> minimumYomitanVersion;
  int version = 3;
  uint64_t importDate = 0;
  bool prefixWildcardsSupported = false;
  SummaryCounts counts;
  std::string styles;
  std::optional<bool> isUpdatable;
  std::optional<std::string> indexUrl;
  std::optional<std::string> downloadUrl;
  std::optional<std::string> author;
  std::optional<std::string> url;
  std::optional<std::string> description;
  std::optional<std::string> attribution;
  std::optional<std::string> sourceLanguage;
  std::optional<std::string> targetLanguage;
  std::optional<std::string> frequencyMode;
  std::optional<bool> importSuccess;
};

struct ImportResult {
  bool success = false;
  std::string title;
  Summary summary;
  size_t term_count = 0;
  size_t meta_count = 0;
  size_t freq_count = 0;
  size_t pitch_count = 0;
  size_t media_count = 0;
  std::vector<std::string> errors;
};

namespace dictionary_importer {
ImportResult import(const std::string& zip_path, const std::string& output_dir, bool low_ram = false);
};
