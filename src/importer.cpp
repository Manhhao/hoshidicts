#include "hoshidicts/importer.hpp"

#include <ankerl/unordered_dense.h>
#include <xxh3.h>
#define ZDICT_STATIC_LINKING_ONLY
#include <zdict.h>
#include <zstd.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <future>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "hash/bloom.hpp"
#include "hash/hash.hpp"
#include "json/yomitan_parser.hpp"
#include "path_utils.hpp"
#include "zip/zip.hpp"

namespace {
struct Files {
  std::vector<int> term_banks;
  std::vector<int> meta_banks;
  std::vector<int> kanji_banks;
  std::vector<int> kanji_meta_banks;
  std::vector<int> tag_banks;
  std::vector<int> media_files;
};

struct ProcessedFile {
  std::vector<char> data;
  std::vector<std::pair<uint64_t, uint64_t>> offsets;
  ankerl::unordered_dense::map<uint64_t, std::vector<char>> glossaries;
  std::vector<std::pair<uint64_t, uint64_t>> glossary_offsets;
  SummaryMetaCount meta_counts;
  size_t count = 0;
};

struct DictionaryRedirectDefinition {
  std::string form_of;
  std::vector<std::string> inflection_rules;
};

struct ProcessedGlossary {
  std::string display_glossary;
  std::vector<DictionaryRedirectDefinition> redirects;
};

}

template <>
struct glz::meta<DictionaryRedirectDefinition> {
  using T = DictionaryRedirectDefinition;
  static constexpr auto value = glz::array(&T::form_of, &T::inflection_rules);
};

namespace {

void setup_stream_exceptions(std::ofstream& stream) { stream.exceptions(std::ios::failbit | std::ios::badbit); }

Files get_files(const Zip& zip) {
  Files files;
  for (int i = 0; i < static_cast<int>(zip.entries.size()); i++) {
    const auto& name = zip.entries[i].name;
    if (name.empty() || name.back() == '/') {
      continue;
    }

    if (name.starts_with("term_bank_")) {
      files.term_banks.push_back(i);
    } else if (name.starts_with("term_meta_bank_")) {
      files.meta_banks.push_back(i);
    } else if (name.starts_with("kanji_bank_")) {
      files.kanji_banks.push_back(i);
    } else if (name.starts_with("kanji_meta_bank_")) {
      files.kanji_meta_banks.push_back(i);
    } else if (name.starts_with("tag_bank_")) {
      files.tag_banks.push_back(i);
    } else if (!(name == "styles.css" || name == "index.json")) {
      files.media_files.push_back(i);
    }
  }
  return files;
}

template <typename T>
void write_val(std::vector<char>& out, T value) {
  const size_t old_size = out.size();
  out.resize(old_size + sizeof(T));
  std::memcpy(out.data() + old_size, &value, sizeof(T));
}

void write_str(std::vector<char>& out, std::string_view value) {
  if (value.empty()) {
    return;
  }
  const size_t old_size = out.size();
  out.resize(old_size + value.size());
  std::memcpy(out.data() + old_size, value.data(), value.size());
}

void write_bytes(std::vector<char>& out, const void* data, size_t n) {
  const size_t old_size = out.size();
  out.resize(old_size + n);
  std::memcpy(out.data() + old_size, data, n);
}

std::optional<DictionaryRedirectDefinition> parse_dictionary_redirect_definition(std::string_view definition) {
  auto fields = glz::read_json<std::vector<glz::raw_json_view>>(definition);
  if (!fields || fields->size() != 2) {
    return std::nullopt;
  }

  auto redirect = glz::read_json<DictionaryRedirectDefinition>(definition);
  if (!redirect) {
    return std::nullopt;
  }
  return std::move(*redirect);
}

ProcessedGlossary process_glossary(std::string_view glossary) {
  auto definitions = glz::read_json<std::vector<glz::raw_json_view>>(glossary);
  if (!definitions) {
    return {.display_glossary = std::string(glossary), .redirects = {}};
  }

  ProcessedGlossary result;
  std::string filtered = "[";
  bool first = true;
  bool removed_redirect = false;
  for (const auto& definition : *definitions) {
    auto redirect = parse_dictionary_redirect_definition(definition.str);
    if (redirect) {
      removed_redirect = true;
      result.redirects.push_back(std::move(*redirect));
      continue;
    }

    if (!first) {
      filtered += ",";
    }
    filtered += definition.str;
    first = false;
  }
  filtered += "]";

  if (removed_redirect) {
    result.display_glossary = first ? "" : std::move(filtered);
  } else {
    result.display_glossary = std::string(glossary);
  }
  return result;
}

void write_redirects(std::vector<char>& out, const std::vector<DictionaryRedirectDefinition>& redirects) {
  write_val<uint32_t>(out, redirects.size());
  for (const auto& redirect : redirects) {
    write_val<uint32_t>(out, redirect.form_of.size());
    write_str(out, redirect.form_of);
    write_val<uint32_t>(out, redirect.inflection_rules.size());
    for (const auto& rule : redirect.inflection_rules) {
      write_val<uint32_t>(out, rule.size());
      write_str(out, rule);
    }
  }
}

void radix_sort(std::vector<std::pair<uint64_t, uint64_t>>& offsets) {
  if (offsets.size() < 2) {
    return;
  }

  const size_t n = offsets.size();
  const size_t num_threads = std::max<size_t>(1, std::thread::hardware_concurrency());
  std::vector<std::pair<uint64_t, uint64_t>> temp(n);
  auto* src = &offsets;
  auto* dst = &temp;

  std::vector<std::array<size_t, 65536>> local_counts(num_threads);
  auto global_count = std::make_unique<std::array<size_t, 65536>>();
  auto global_pos = std::make_unique<std::array<size_t, 65536>>();

  for (uint32_t shift = 0; shift < 64; shift += 16) {
    const size_t chunk = (n + num_threads - 1) / num_threads;
    std::vector<std::future<void>> futures;
    for (size_t t = 0; t < num_threads; t++) {
      const size_t begin = t * chunk;
      const size_t end = std::min(begin + chunk, n);
      if (begin >= n) {
        break;
      }

      local_counts[t].fill(0);
      futures.push_back(std::async(std::launch::async, [src, shift, begin, end, &local_counts, t]() {
        for (size_t i = begin; i < end; i++) {
          local_counts[t][((*src)[i].first >> shift) & 0xffff]++;
        }
      }));
    }
    for (auto& future : futures) {
      future.get();
    }

    global_count->fill(0);
    for (size_t t = 0; t < futures.size(); t++) {
      for (size_t bucket = 0; bucket < 65536; bucket++) {
        (*global_count)[bucket] += local_counts[t][bucket];
      }
    }

    global_pos->fill(0);
    size_t total = 0;
    for (size_t bucket = 0; bucket < 65536; bucket++) {
      (*global_pos)[bucket] = total;
      total += (*global_count)[bucket];
    }

    std::vector<std::array<size_t, 65536>> thread_pos(futures.size());
    for (size_t bucket = 0; bucket < 65536; bucket++) {
      size_t pos = (*global_pos)[bucket];
      for (size_t t = 0; t < futures.size(); t++) {
        thread_pos[t][bucket] = pos;
        pos += local_counts[t][bucket];
      }
    }

    std::vector<std::future<void>> scatter_futures;
    for (size_t t = 0; t < futures.size(); t++) {
      const size_t begin = t * chunk;
      const size_t end = std::min(begin + chunk, n);
      scatter_futures.push_back(std::async(std::launch::async, [src, dst, shift, begin, end, &thread_pos, t]() {
        for (size_t i = begin; i < end; i++) {
          const size_t bucket = ((*src)[i].first >> shift) & 0xffff;
          (*dst)[thread_pos[t][bucket]++] = (*src)[i];
        }
      }));
    }
    for (auto& future : scatter_futures) {
      future.get();
    }

    std::swap(src, dst);
  }
}

std::vector<char> train_zstd_dict(const Zip& zip, const Files& files, bool low_ram) {
  if (files.term_banks.empty()) {
    return {};
  }

  const std::string content = zip.read(files.term_banks[0]);
  std::vector<Term> terms;
  if (!yomitan_parser::parse_term_bank(content, terms)) {
    return {};
  }

  size_t bank_bytes = 0;
  for (const auto& term : terms) {
    bank_bytes += term.glossary.str.size();
  }

  std::vector<char> samples;
  std::vector<size_t> sizes;
  constexpr size_t max_sample_bytes = 2L * 1024 * 1024;
  const size_t step = std::max<size_t>(1, bank_bytes / max_sample_bytes);
  for (size_t i = 0; i < terms.size() && samples.size() < max_sample_bytes; i += step) {
    write_str(samples, terms[i].glossary.str);
    sizes.push_back(terms[i].glossary.str.size());
  }

  if (sizes.size() < 8) {
    return {};
  }

  ZDICT_fastCover_params_t params = {};
  params.d = 8;
  params.steps = 4;
  params.splitPoint = 1.0;
  params.nbThreads = low_ram ? 1 : std::max<unsigned int>(1, std::thread::hardware_concurrency());

  std::vector<char> dict(static_cast<size_t>(110 * 1024));
  const size_t dict_size = ZDICT_optimizeTrainFromBuffer_fastCover(
      dict.data(), dict.size(), samples.data(), sizes.data(), static_cast<unsigned>(sizes.size()), &params);
  if (ZDICT_isError(dict_size)) {
    return {};
  }

  dict.resize(dict_size);
  return dict;
}

ProcessedFile process_term_bank(const std::string& content, const ZSTD_CDict* cdict) {
  ProcessedFile processed;
  if (content.empty()) {
    return processed;
  }

  std::vector<Term> out;
  if (!yomitan_parser::parse_term_bank(content, out)) {
    return processed;
  }

  std::vector<char> compressed;
  ZSTD_CCtx* cctx = ZSTD_createCCtx();
  if (!cctx) {
    return processed;
  }
  ZSTD_CCtx_refCDict(cctx, cdict);

  for (auto& term : out) {
    const ProcessedGlossary glossary = process_glossary(term.glossary.str);
    uint64_t glossary_hash = 0;
    uint32_t blob_size = 0;
    if (!glossary.display_glossary.empty()) {
      glossary_hash = XXH3_64bits(glossary.display_glossary.data(), glossary.display_glossary.size());
      auto it = processed.glossaries.find(glossary_hash);
      if (it == processed.glossaries.end()) {
        const size_t bound = ZSTD_compressBound(glossary.display_glossary.size());
        compressed.resize(bound);
        const size_t compressed_size = ZSTD_compress2(cctx, compressed.data(), bound, glossary.display_glossary.data(),
                                                      glossary.display_glossary.size());
        if (ZSTD_isError(compressed_size)) {
          ZSTD_freeCCtx(cctx);
          throw std::runtime_error("failed to compress glossary");
        }
        compressed.resize(compressed_size);
        processed.glossaries.emplace(glossary_hash, compressed);
      }
      blob_size = processed.glossaries[glossary_hash].size();
    }

    uint64_t offset = processed.data.size();
    std::string_view expr = term.expression;
    std::string_view reading = term.reading.empty() ? expr : term.reading;
    std::string_view definition_tags = term.definition_tags.value_or("");

    write_val<uint8_t>(processed.data, 0);
    write_val<uint16_t>(processed.data, expr.size());
    write_str(processed.data, expr);
    write_val<uint16_t>(processed.data, reading.size());
    write_str(processed.data, reading);

    uint64_t glossary_offset = processed.data.size();
    write_val<uint64_t>(processed.data, 0);
    write_val<uint32_t>(processed.data, blob_size);
    if (blob_size > 0) {
      processed.glossary_offsets.emplace_back(glossary_hash, glossary_offset);
    }

    write_val<uint8_t>(processed.data, definition_tags.size());
    write_str(processed.data, definition_tags);
    write_val<uint8_t>(processed.data, term.rules.size());
    write_str(processed.data, term.rules);
    write_val<uint8_t>(processed.data, term.term_tags.size());
    write_str(processed.data, term.term_tags);
    write_redirects(processed.data, glossary.redirects);
    write_val<int32_t>(processed.data, static_cast<int32_t>(term.score));

    processed.offsets.emplace_back(XXH3_64bits(expr.data(), expr.size()), offset);
    if (reading != expr) {
      processed.offsets.emplace_back(XXH3_64bits(reading.data(), reading.size()), offset);
    }
    processed.count++;
  }
  ZSTD_freeCCtx(cctx);

  return processed;
}

ProcessedFile process_meta_bank(const std::string& content) {
  ProcessedFile processed;
  if (content.empty()) {
    return processed;
  }

  std::vector<Meta> out;
  if (!yomitan_parser::parse_meta_bank(content, out)) {
    return processed;
  }

  for (auto& meta : out) {
    uint64_t offset = processed.data.size();
    std::string_view expr = meta.expression;
    std::string_view mode = meta.mode;
    std::string_view data = meta.data.str;

    write_val<uint8_t>(processed.data, 1);
    write_val<uint16_t>(processed.data, expr.size());
    write_str(processed.data, expr);
    write_val<uint8_t>(processed.data, mode.size());
    write_str(processed.data, mode);
    write_val<uint32_t>(processed.data, data.size());
    write_str(processed.data, data);

    processed.offsets.emplace_back(XXH3_64bits(expr.data(), expr.size()), offset);
    processed.count++;
    processed.meta_counts[std::string(mode)]++;
  }

  processed.meta_counts["total"] = processed.count;
  return processed;
}

ProcessedFile process_kanji_bank(const std::string& content) {
  ProcessedFile processed;
  if (content.empty()) {
    return processed;
  }

  std::vector<Kanji> out;
  if (!yomitan_parser::parse_kanji_bank(content, out)) {
    return processed;
  }

  for (auto& kanji : out) {
    uint64_t offset = processed.data.size();
    std::string_view character = kanji.character;
    std::string_view onyomi = kanji.onyomi;
    std::string_view kunyomi = kanji.kunyomi;
    std::string_view tags = kanji.tags;

    write_val<uint8_t>(processed.data, 2);
    write_val<uint8_t>(processed.data, character.size());
    write_str(processed.data, character);
    write_val<uint16_t>(processed.data, onyomi.size());
    write_str(processed.data, onyomi);
    write_val<uint16_t>(processed.data, kunyomi.size());
    write_str(processed.data, kunyomi);
    write_val<uint16_t>(processed.data, tags.size());
    write_str(processed.data, tags);

    write_val<uint16_t>(processed.data, kanji.definitions.size());
    for (auto& def : kanji.definitions) {
      write_val<uint16_t>(processed.data, def.size());
      write_str(processed.data, def);
    }

    write_val<uint16_t>(processed.data, kanji.stats.size());
    for (auto& [k, v] : kanji.stats) {
      write_val<uint16_t>(processed.data, k.size());
      write_str(processed.data, k);
      write_val<uint16_t>(processed.data, v.size());
      write_str(processed.data, v);
    }

    processed.offsets.emplace_back(XXH3_64bits(character.data(), character.size()), offset);
    processed.count++;
  }

  return processed;
}

size_t count_json_array(const std::string& content) {
  if (content.empty()) {
    return 0;
  }

  std::vector<glz::raw_json> entries;
  if (glz::read<glz::opts{.error_on_unknown_keys = false, .error_on_missing_keys = false}>(entries, content)) {
    return 0;
  }
  return entries.size();
}

SummaryMetaCount count_meta_modes(const std::string& content) {
  SummaryMetaCount counts{{"total", 0}};
  if (content.empty()) {
    return counts;
  }

  std::vector<Meta> entries;
  if (!yomitan_parser::parse_meta_bank(content, entries)) {
    return counts;
  }

  for (const auto& entry : entries) {
    counts[std::string(entry.mode)]++;
    counts["total"]++;
  }
  return counts;
}

void count_unprocessed_banks(const Zip& zip, const Files& files, ImportResult& result) {
  for (int file_index : files.kanji_meta_banks) {
    SummaryMetaCount modes = count_meta_modes(zip.read(file_index));
    for (const auto& [name, count] : modes) {
      result.summary.counts.kanjiMeta[name] += count;
    }
  }

  for (int file_index : files.tag_banks) {
    result.summary.counts.tagMeta.total += count_json_array(zip.read(file_index));
  }
}

template <typename T>
std::optional<std::string> copy_optional_string(T value) {
  if (!value.has_value()) {
    return std::nullopt;
  }
  return std::string(*value);
}

uint64_t unix_time_ms() {
  const auto now = std::chrono::system_clock::now().time_since_epoch();
  return std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
}

Summary create_summary(const Index& index, std::string styles) {
  Summary summary;
  summary.title = std::string(index.title);
  summary.revision = std::string(index.revision);
  summary.sequenced = index.sequenced;
  summary.minimumYomitanVersion = copy_optional_string(index.minimumYomitanVersion);
  summary.version = index.version.value_or(index.format.value_or(3));
  summary.importDate = unix_time_ms();
  summary.prefixWildcardsSupported = false;
  summary.styles = std::move(styles);
  summary.isUpdatable = index.isUpdatable;
  summary.indexUrl = copy_optional_string(index.indexUrl);
  summary.downloadUrl = copy_optional_string(index.downloadUrl);
  summary.author = copy_optional_string(index.author);
  summary.url = copy_optional_string(index.url);
  summary.description = copy_optional_string(index.description);
  summary.attribution = copy_optional_string(index.attribution);
  summary.sourceLanguage = copy_optional_string(index.sourceLanguage);
  summary.targetLanguage = copy_optional_string(index.targetLanguage);
  summary.frequencyMode = copy_optional_string(index.frequencyMode);
  summary.importSuccess = true;
  summary.counts.termMeta["total"] = 0;
  summary.counts.kanjiMeta["total"] = 0;
  return summary;
}

void write_terms(std::ofstream& file, std::vector<std::pair<uint64_t, uint64_t>>& offsets, const Zip& zip,
                 const std::vector<int>& files, uint64_t& write_offset, ImportResult& result, bool low_ram,
                 const ZSTD_CDict* cdict) {
  if (files.empty()) {
    return;
  }

  size_t max_threads =
      low_ram ? 2 : std::max<size_t>(4, static_cast<const unsigned long>(std::thread::hardware_concurrency()) + 4);
  std::deque<std::future<ProcessedFile>> threads;

  ankerl::unordered_dense::map<uint64_t, uint64_t> glossaries;
  auto write_processed = [&](ProcessedFile&& processed) {
    if (processed.data.empty()) {
      return;
    }

    std::vector<char> glossary_buf;
    for (auto& [hash, compressed] : processed.glossaries) {
      auto [it, inserted] = glossaries.try_emplace(hash, write_offset);
      if (inserted) {
        write_bytes(glossary_buf, compressed.data(), compressed.size());
        write_offset += compressed.size();
      }
    }
    if (!glossary_buf.empty()) {
      file.write(glossary_buf.data(), static_cast<std::streamsize>(glossary_buf.size()));
    }

    for (auto& [hash, pos] : processed.glossary_offsets) {
      uint64_t glossary_offset = glossaries[hash];
      std::memcpy(processed.data.data() + pos, &glossary_offset, sizeof(uint64_t));
    }

    file.write(processed.data.data(), static_cast<std::streamsize>(processed.data.size()));

    for (auto& [hash, offset] : processed.offsets) {
      offsets.emplace_back(hash, offset + write_offset);
    }

    write_offset += processed.data.size();
    result.summary.counts.terms.total += processed.count;
  };

  for (int file_index : files) {
    threads.push_back(std::async(
        std::launch::async, [&zip, file_index, cdict]() { return process_term_bank(zip.read(file_index), cdict); }));

    if (threads.size() == max_threads) {
      write_processed(threads.front().get());
      threads.pop_front();
    }
  }

  while (!threads.empty()) {
    write_processed(threads.front().get());
    threads.pop_front();
  }
}

void write_meta(std::ofstream& file, std::vector<std::pair<uint64_t, uint64_t>>& offsets, const Zip& zip,
                const std::vector<int>& files, uint64_t& write_offset, ImportResult& result, bool low_ram) {
  if (files.empty()) {
    return;
  }

  size_t max_threads =
      low_ram ? 2 : std::max<size_t>(4, static_cast<const unsigned long>(std::thread::hardware_concurrency()) + 4);
  std::deque<std::future<ProcessedFile>> threads;
  auto write_processed = [&](ProcessedFile&& processed) {
    if (processed.data.empty()) {
      return;
    }
    file.write(processed.data.data(), static_cast<std::streamsize>(processed.data.size()));

    for (auto& [hash, offset] : processed.offsets) {
      offsets.emplace_back(hash, offset + write_offset);
    }

    write_offset += processed.data.size();
    for (const auto& [mode, count] : processed.meta_counts) {
      result.summary.counts.termMeta[mode] += count;
    }
  };

  for (int file_index : files) {
    threads.push_back(
        std::async(std::launch::async, [&zip, file_index]() { return process_meta_bank(zip.read(file_index)); }));

    if (threads.size() == max_threads) {
      write_processed(threads.front().get());
      threads.pop_front();
    }
  }

  while (!threads.empty()) {
    write_processed(threads.front().get());
    threads.pop_front();
  }
}

void write_kanji(std::ofstream& file, std::vector<std::pair<uint64_t, uint64_t>>& offsets, const Zip& zip,
                 const std::vector<int>& files, uint64_t& write_offset, ImportResult& result, bool low_ram) {
  if (files.empty()) {
    return;
  }

  size_t max_threads =
      low_ram ? 2 : std::max<size_t>(4, static_cast<const unsigned long>(std::thread::hardware_concurrency()) + 4);
  std::deque<std::future<ProcessedFile>> threads;
  auto write_processed = [&](ProcessedFile&& processed) {
    if (processed.data.empty()) {
      return;
    }
    file.write(processed.data.data(), static_cast<std::streamsize>(processed.data.size()));

    for (auto& [hash, offset] : processed.offsets) {
      offsets.emplace_back(hash, offset + write_offset);
    }

    write_offset += processed.data.size();
    result.summary.counts.kanji.total += processed.count;
  };

  for (int file_index : files) {
    threads.push_back(
        std::async(std::launch::async, [&zip, file_index]() { return process_kanji_bank(zip.read(file_index)); }));

    if (threads.size() == max_threads) {
      write_processed(threads.front().get());
      threads.pop_front();
    }
  }

  while (!threads.empty()) {
    write_processed(threads.front().get());
    threads.pop_front();
  }
}

std::vector<char> build_offset_index(std::vector<std::pair<uint64_t, uint64_t>>& offsets, uint64_t& write_offset,
                                     std::vector<std::pair<uint64_t, uint64_t>>& hash_entries) {
  std::vector<char> offset_buf;
  radix_sort(offsets);
  for (size_t i = 0; i < offsets.size();) {
    size_t j = i + 1;
    while (j < offsets.size() && offsets[j].first == offsets[i].first) {
      j++;
    }

    hash_entries.emplace_back(offsets[i].first, write_offset);

    auto count = static_cast<uint32_t>(j - i);
    write_val<uint32_t>(offset_buf, count);
    for (size_t k = i; k < j; ++k) {
      write_val<uint64_t>(offset_buf, offsets[k].second);
    }

    write_offset += sizeof(uint32_t) + count * sizeof(uint64_t);
    i = j;
  }
  return offset_buf;
}

size_t write_media(const std::filesystem::path& path, const Zip& zip, const std::vector<int>& files) {
  if (files.empty()) {
    return 0;
  }

  std::ofstream media(path / "media.bin", std::ios::binary);
  std::ofstream media_idx(path / "media.idx", std::ios::binary);
  setup_stream_exceptions(media);
  setup_stream_exceptions(media_idx);

  size_t media_count = 0;
  uint32_t write_pos = 0;
  std::vector<char> buf;
  std::vector<std::pair<std::string, uint32_t>> index_entries;
  for (int file_index : files) {
    auto media_file = zip.read_media(file_index);
    if (!media_file.has_value()) {
      continue;
    }

    uint32_t record_start = write_pos;
    buf.clear();
    write_val<uint16_t>(buf, media_file->path.size());
    write_str(buf, media_file->path);
    write_val<uint32_t>(buf, media_file->blob.size());
    write_bytes(buf, media_file->blob.data(), media_file->blob.size());
    media.write(buf.data(), static_cast<std::streamsize>(buf.size()));
    write_pos += buf.size();

    index_entries.emplace_back(std::move(media_file->path), record_start);
    media_count++;
  }

  std::ranges::sort(index_entries);
  std::vector<char> index_buf;
  write_val<uint32_t>(index_buf, index_entries.size());
  for (const auto& [name, offset] : index_entries) {
    write_val<uint64_t>(index_buf, offset);
  }

  media_idx.write(index_buf.data(), static_cast<std::streamsize>(index_buf.size()));
  return media_count;
}
}

ImportResult dictionary_importer::import(const std::string& zip_path, const std::string& output_dir, bool low_ram) {
  ImportResult result;
  std::filesystem::path dict_path;
  try {
    const std::filesystem::path native_zip_path = path_utils::from_utf8(zip_path);
    const std::filesystem::path native_output_dir = path_utils::from_utf8(output_dir);
    Zip zip;
    if (!zip.open(native_zip_path)) {
      throw std::runtime_error("failed to open zip");
    }

    int index_idx = zip.find("index.json");
    if (index_idx < 0) {
      throw std::runtime_error("could not find index.json");
    }
    std::string index_content = zip.read(index_idx);
    if (index_content.empty()) {
      throw std::runtime_error("could not read index.json");
    }

    Index index;
    if (!yomitan_parser::parse_index(index_content, index)) {
      throw std::runtime_error("failed to parse index.json");
    }

    result.title = index.title;

    dict_path = native_output_dir / path_utils::from_utf8(result.title);
    std::filesystem::create_directories(dict_path);

    std::string styles;
    int styles_idx = zip.find("styles.css");
    if (styles_idx >= 0) {
      styles = zip.read(styles_idx);
    }

    result.summary = create_summary(index, styles);
    const Files files = get_files(zip);
    std::future<size_t> media_thread = std::async(
        std::launch::async, [&dict_path, &zip, &files]() { return write_media(dict_path, zip, files.media_files); });

    const std::vector<char> zstd_dict = train_zstd_dict(zip, files, low_ram);
    std::unique_ptr<ZSTD_CDict, decltype(&ZSTD_freeCDict)> cdict(nullptr, ZSTD_freeCDict);
    if (!zstd_dict.empty()) {
      cdict.reset(ZSTD_createCDict(zstd_dict.data(), zstd_dict.size(), 0));

      std::ofstream dict_file(dict_path / "dict.zstd", std::ios::binary);
      setup_stream_exceptions(dict_file);
      dict_file.write(zstd_dict.data(), static_cast<std::streamsize>(zstd_dict.size()));
    }

    std::ofstream blobs(dict_path / "blobs.bin", std::ios::binary);
    setup_stream_exceptions(blobs);
    std::vector<std::pair<uint64_t, uint64_t>> offsets;
    uint64_t write_offset = 0;
    write_terms(blobs, offsets, zip, files.term_banks, write_offset, result, low_ram, cdict.get());
    write_meta(blobs, offsets, zip, files.meta_banks, write_offset, result, low_ram);
    write_kanji(blobs, offsets, zip, files.kanji_banks, write_offset, result, low_ram);
    count_unprocessed_banks(zip, files, result);
    if (offsets.empty()) {
      throw std::runtime_error("empty dictionary");
    }

    std::vector<std::pair<uint64_t, uint64_t>> hash_entries;
    auto offset_buf = build_offset_index(offsets, write_offset, hash_entries);
    std::vector<std::pair<uint64_t, uint64_t>>().swap(offsets);

    auto hash_thread = std::async(std::launch::async, [&hash_entries, &dict_path]() {
      hash::linear table;
      table.build_to_file(hash_entries, dict_path / "hash.table");
      auto hashes = hash_entries | std::views::keys | std::ranges::to<std::vector>();
      hash::bloom::build_to_file(hashes, dict_path / "bloom.filter");
    });

    blobs.write(offset_buf.data(), static_cast<std::streamsize>(offset_buf.size()));
    hash_thread.get();

    result.summary.counts.media.total = media_thread.get();

    std::string summary_json;
    if (glz::write_json(result.summary, summary_json)) {
      throw std::runtime_error("failed to write index.json");
    }
    std::ofstream index_file(dict_path / "index.json", std::ios::binary);
    setup_stream_exceptions(index_file);
    index_file.write(summary_json.data(), static_cast<std::streamsize>(summary_json.size()));

    const std::filesystem::path marker = dict_path / (zstd_dict.empty() ? ".hoshidicts_3" : ".hoshidicts_4");
    std::ofstream marker_file(marker, std::ios::binary);
    setup_stream_exceptions(marker_file);
    marker_file.close();
    for (int version = 1; version <= 4; ++version) {
      const auto candidate = dict_path / (".hoshidicts_" + std::to_string(version));
      if (candidate != marker) {
        std::filesystem::remove(candidate);
      }
    }
    if (zstd_dict.empty()) {
      std::filesystem::remove(dict_path / "dict.zstd");
    }
    result.success = true;
  } catch (const std::exception& e) {
    result.success = false;
    if (!result.summary.title.empty()) {
      result.summary.importSuccess = false;
    }
    result.error = e.what();
  }

  if (!result.success && !dict_path.empty()) {
    std::error_code error;
    std::filesystem::remove_all(dict_path, error);
  }

  return result;
}
