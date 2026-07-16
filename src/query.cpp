#include "hoshidicts/query.hpp"

#include <ankerl/unordered_dense.h>
#include <zstd.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <ranges>
#include <string_view>
#include <utility>
#include <vector>

#include "hash/hash.hpp"
#include "json/yomitan_parser.hpp"
#include "memory/memory.hpp"

namespace {
template <typename T>
T read_val(const uint8_t*& addr) {
  T val;
  std::memcpy(&val, addr, sizeof(T));
  addr += sizeof(T);
  return val;
}

std::string_view read_str(const uint8_t*& addr, uint32_t len) {
  std::string_view result(reinterpret_cast<const char*>(addr), len);
  addr += len;
  return result;
}
}

struct DictionaryQuery::DictionaryData {
  uint8_t format_version = 1;
  hash::linear table;
  hash::bloom bloom;
  memory::mapped_file blobs;
  memory::mapped_file hash_table;
  memory::mapped_file bloom_filter;
  memory::mapped_file media;
  memory::mapped_file media_index;

  ~DictionaryData() {
    memory::unmap(blobs);
    memory::unmap(hash_table);
    memory::unmap(bloom_filter);
    memory::unmap(media);
    memory::unmap(media_index);
  }
};

DictionaryQuery::DictionaryQuery() = default;
DictionaryQuery::~DictionaryQuery() = default;

DictionaryQuery::DictionaryQuery(DictionaryQuery&&) noexcept = default;
DictionaryQuery& DictionaryQuery::operator=(DictionaryQuery&&) noexcept = default;

DictionaryQuery::Dictionary::Dictionary() = default;
DictionaryQuery::Dictionary::~Dictionary() = default;

DictionaryQuery::Dictionary::Dictionary(Dictionary&&) noexcept = default;
DictionaryQuery::Dictionary& DictionaryQuery::Dictionary::operator=(Dictionary&&) noexcept = default;

void DictionaryQuery::add_dict(const std::string& path, DictionaryType type) {
  uint8_t format_version = 0;
  if (std::filesystem::is_regular_file(path + "/.hoshidicts_3")) {
    format_version = 3;
  } else if (std::filesystem::is_regular_file(path + "/.hoshidicts_2")) {
    format_version = 2;
  } else if (std::filesystem::is_regular_file(path + "/.hoshidicts_1")) {
    format_version = 1;
  } else {
    return;
  }

  Dictionary dict;
  Index index;
  std::string buf{};
  if (glz::read_file_json(index, path + "/index.json", buf)) {
    return;
  }

  dict.name = index.title.empty() ? std::filesystem::path(path).stem().string() : index.title;
  if (std::filesystem::exists(path + "/styles.css")) {
    std::ifstream f(path + "/styles.css");
    dict.styles = std::string(std::istreambuf_iterator<char>(f), {});
  }

  dict.data = std::make_unique<DictionaryData>();
  dict.data->format_version = format_version;

  dict.data->hash_table = memory::map_rd(path + "/hash.table");
  if (!dict.data->hash_table) {
    return;
  }
  dict.data->table.load(dict.data->hash_table.data);

  dict.data->bloom_filter = memory::map_rd(path + "/bloom.filter");
  if (!dict.data->bloom_filter) {
    hash::bloom::build_to_file(dict.data->table.populated(), path + "/bloom.filter");
    dict.data->bloom_filter = memory::map_rd(path + "/bloom.filter");
  }
  dict.data->bloom.load(dict.data->bloom_filter.data);
  dict.data->table.set_bloom(&dict.data->bloom);

  dict.data->blobs = memory::map_rd(path + "/blobs.bin");
  if (!dict.data->blobs) {
    return;
  }

  dict.data->media = memory::map_rd(path + "/media.bin");
  if (dict.data->media) {
    dict.data->media_index = memory::map_rd(path + "/media.idx");
  }

  switch (type) {
    case TERM:
      term_dicts_.push_back(std::move(dict));
      break;
    case FREQ:
      freq_dicts_.push_back(std::move(dict));
      break;
    case PITCH:
      pitch_dicts_.push_back(std::move(dict));
      break;
  }
}

void DictionaryQuery::add_term_dict(const std::string& path) { add_dict(path, DictionaryQuery::DictionaryType::TERM); }

void DictionaryQuery::add_freq_dict(const std::string& path) { add_dict(path, DictionaryQuery::DictionaryType::FREQ); }

void DictionaryQuery::add_pitch_dict(const std::string& path) {
  add_dict(path, DictionaryQuery::DictionaryType::PITCH);
}

std::vector<TermResult> DictionaryQuery::query(const std::string& expression) const {
  auto results = query_raw(expression);
  for (auto& term : results) {
    materialize(term);
  }
  return results;
}

std::vector<TermResult> DictionaryQuery::query_raw(const std::string& expression) const {
  auto results = merge_term_entries(query_raw_entries(expression));
  query_freq(results);
  query_pitch(results);

  return results;
}

std::vector<TermResult> DictionaryQuery::query_raw_entries(const std::string& expression) const {
  std::vector<TermResult> results;
  for (const auto& [name, styles, data] : term_dicts_) {
    uint64_t offset_addr = data->table(expression);
    if (offset_addr == 0) {
      continue;
    }
    const uint8_t* index_addr = data->blobs.data + offset_addr;

    auto count = read_val<uint32_t>(index_addr);
    for (uint32_t i = 0; i < count; i++) {
      auto offset = read_val<uint64_t>(index_addr);
      const uint8_t* blob_addr = data->blobs.data + offset;

      // first byte encodes term (0) or meta (1) entry
      auto type = read_val<uint8_t>(blob_addr);
      if (type != 0) {
        continue;
      }

      auto expr_len = read_val<uint16_t>(blob_addr);
      std::string_view expr = read_str(blob_addr, expr_len);

      auto reading_len = read_val<uint16_t>(blob_addr);
      std::string_view reading = read_str(blob_addr, reading_len);

      if (expr != expression && reading != expression) {
        continue;
      }

      auto glossary_offset = read_val<uint64_t>(blob_addr);
      auto glossary_size = read_val<uint32_t>(blob_addr);

      auto def_tags_size = read_val<uint8_t>(blob_addr);
      std::string_view definition_tags = read_str(blob_addr, def_tags_size);

      auto rules_size = read_val<uint8_t>(blob_addr);
      std::string_view rules = read_str(blob_addr, rules_size);

      auto term_tag_size = read_val<uint8_t>(blob_addr);
      std::string_view term_tags = read_str(blob_addr, term_tag_size);

      GlossaryEntry entry;
      entry.dict_name = name;
      entry.definition_tags = definition_tags;
      entry.term_tags = term_tags;
      entry.compressed_data = data->blobs.data + glossary_offset;
      entry.compressed_size = glossary_size;
      entry.dictionary_format_version = data->format_version;

      if (data->format_version >= 2) {
        const auto redirect_count = read_val<uint32_t>(blob_addr);
        entry.redirects.reserve(redirect_count);
        for (uint32_t redirect_index = 0; redirect_index < redirect_count; ++redirect_index) {
          auto form_of_len = read_val<uint32_t>(blob_addr);
          auto form_of = read_str(blob_addr, form_of_len);

          auto inflection_rule_count = read_val<uint32_t>(blob_addr);
          std::vector<std::string> inflection_rules;
          inflection_rules.reserve(inflection_rule_count);
          for (uint32_t rule_index = 0; rule_index < inflection_rule_count; ++rule_index) {
            auto rule_len = read_val<uint32_t>(blob_addr);
            inflection_rules.emplace_back(read_str(blob_addr, rule_len));
          }

          entry.redirects.push_back(
              DictionaryRedirect{.form_of = std::string(form_of), .inflection_rules = std::move(inflection_rules)});
        }
      }

      int score = 0;
      if (data->format_version >= 3) {
        score = read_val<int32_t>(blob_addr);
      }

      results.push_back({.expression = std::string(expr),
                         .reading = std::string(reading),
                         .rules = std::string(rules),
                         .score = score,
                         .glossaries = {std::move(entry)},
                         .frequencies = {}});
    }
  }

  return results;
}

std::vector<TermResult> DictionaryQuery::merge_term_entries(std::vector<TermResult> terms) {
  std::map<std::pair<std::string, std::string>, TermResult> term_map;
  for (auto& term : terms) {
    auto key = std::make_pair(term.expression, term.reading);
    auto [it, inserted] = term_map.try_emplace(std::move(key));
    if (inserted) {
      it->second = std::move(term);
      continue;
    }

    if (!term.rules.empty()) {
      if (!it->second.rules.empty()) {
        it->second.rules += " ";
      }
      it->second.rules += term.rules;
    }
    it->second.glossaries.insert(it->second.glossaries.end(), std::make_move_iterator(term.glossaries.begin()),
                                 std::make_move_iterator(term.glossaries.end()));
    it->second.score = std::max(it->second.score, term.score);
  }

  return term_map | std::views::values | std::views::as_rvalue | std::ranges::to<std::vector>();
}

void DictionaryQuery::order_glossaries(std::vector<GlossaryEntry>& glossaries) const {
  std::unordered_map<std::string_view, size_t> dictionary_order;
  dictionary_order.reserve(term_dicts_.size());
  for (size_t i = 0; i < term_dicts_.size(); ++i) {
    dictionary_order.try_emplace(term_dicts_[i].name, i);
  }

  const auto rank = [&](const GlossaryEntry& glossary) {
    const auto it = dictionary_order.find(glossary.dict_name);
    return it == dictionary_order.end() ? term_dicts_.size() : it->second;
  };
  std::ranges::stable_sort(glossaries, {}, rank);
}

void DictionaryQuery::query_freq(std::vector<TermResult>& terms) const {
  for (auto& term : terms) {
    for (const auto& [name, styles, data] : freq_dicts_) {
      uint64_t offset_addr = data->table(term.expression);
      if (offset_addr == 0) {
        continue;
      }
      const uint8_t* index_addr = data->blobs.data + offset_addr;
      auto count = read_val<uint32_t>(index_addr);

      std::vector<Frequency> frequencies;
      for (uint32_t i = 0; i < count; i++) {
        auto offset = read_val<uint64_t>(index_addr);
        const uint8_t* blob_addr = data->blobs.data + offset;

        auto type = read_val<uint8_t>(blob_addr);
        if (type != 1) {
          continue;
        }

        auto expr_len = read_val<uint16_t>(blob_addr);
        std::string_view expr = read_str(blob_addr, expr_len);
        if (expr != term.expression) {
          continue;
        }

        auto mode_len = read_val<uint8_t>(blob_addr);
        std::string_view mode = read_str(blob_addr, mode_len);
        if (mode != "freq") {
          continue;
        }

        auto freq_data_size = read_val<uint32_t>(blob_addr);
        std::string_view freq_data = read_str(blob_addr, freq_data_size);

        ParsedFrequency parsed;
        if (yomitan_parser::parse_frequency(freq_data, parsed)) {
          if (!parsed.reading.empty() && parsed.reading != term.reading) {
            continue;
          }
          frequencies.emplace_back(
              Frequency{.value = parsed.value, .display_value = std::string(parsed.display_value)});
        }
      }
      if (!frequencies.empty()) {
        term.frequencies.emplace_back(FrequencyEntry{.dict_name = name, .frequencies = std::move(frequencies)});
      }
    }
  }
}

void DictionaryQuery::query_pitch(std::vector<TermResult>& terms) const {
  for (auto& term : terms) {
    for (const auto& [name, styles, data] : pitch_dicts_) {
      uint64_t offset_addr = data->table(term.expression);
      if (offset_addr == 0) {
        continue;
      }
      const uint8_t* index_addr = data->blobs.data + offset_addr;
      auto count = read_val<uint32_t>(index_addr);

      std::vector<int> pitch_positions;
      std::vector<std::string> transcriptions;
      for (uint32_t i = 0; i < count; i++) {
        auto offset = read_val<uint64_t>(index_addr);
        const uint8_t* blob_addr = data->blobs.data + offset;

        auto type = read_val<uint8_t>(blob_addr);
        if (type != 1) {
          continue;
        }

        auto expr_len = read_val<uint16_t>(blob_addr);
        std::string_view expr = read_str(blob_addr, expr_len);
        if (expr != term.expression) {
          continue;
        }

        auto mode_len = read_val<uint8_t>(blob_addr);
        std::string_view mode = read_str(blob_addr, mode_len);
        ParsedPitch parsed;
        if (mode == "pitch") {
          auto pitch_data_size = read_val<uint32_t>(blob_addr);
          std::string_view pitch_data = read_str(blob_addr, pitch_data_size);

          if (yomitan_parser::parse_pitch(pitch_data, parsed)) {
            if (!parsed.reading.empty() && parsed.reading != term.reading) {
              continue;
            }
            pitch_positions.insert(pitch_positions.end(), parsed.pitches.begin(), parsed.pitches.end());
          }
        } else if (mode == "ipa") {
          auto transcriptions_data_size = read_val<uint32_t>(blob_addr);
          std::string_view transcriptions_data = read_str(blob_addr, transcriptions_data_size);
          if (yomitan_parser::parse_ipa(transcriptions_data, parsed)) {
            if (!parsed.reading.empty() && parsed.reading != term.reading) {
              continue;
            }
            for (std::string_view transcription : parsed.transcriptions) {
              transcriptions.emplace_back(transcription);
            }
          }
        }
      }
      if (!pitch_positions.empty() || !transcriptions.empty()) {
        term.pitches.emplace_back(PitchEntry{
            .dict_name = name,
            .pitch_positions = std::move(pitch_positions),
            .transcriptions = std::move(transcriptions),
        });
      }
    }
  }
}

std::string DictionaryQuery::decompress_glossary(const void* data, size_t size) {
  if (!data || size == 0) {
    return "";
  }

  unsigned long long decompressed_size = ZSTD_getFrameContentSize(data, size);
  if (decompressed_size == ZSTD_CONTENTSIZE_ERROR || decompressed_size == ZSTD_CONTENTSIZE_UNKNOWN) {
    return "";
  }

  std::string result;
  result.resize(decompressed_size);

  size_t actual_size = ZSTD_decompress(result.data(), result.size(), data, size);
  if (ZSTD_isError(actual_size)) {
    return "";
  }

  result.resize(actual_size);
  return result;
}

void DictionaryQuery::materialize(TermResult& term) const {
  for (auto& g : term.glossaries) {
    if (!g.glossary.empty()) {
      continue;
    }
    g.glossary = decompress_glossary(g.compressed_data, g.compressed_size);
  }
}

std::vector<char> DictionaryQuery::get_media_file(const std::string& dict_name, const std::string& media_path) const {
  auto view = get_media_file_view(dict_name, media_path);
  return {view.data, view.data + view.size};
}

MediaFileView DictionaryQuery::get_media_file_view(const std::string& dict_name, const std::string& media_path) const {
  for (const auto& [name, styles, data] : term_dicts_) {
    if (name != dict_name) {
      continue;
    }

    if (!data->media || !data->media_index) {
      return {};
    }

    const uint8_t* ptr = data->media_index.data;
    auto count = read_val<uint32_t>(ptr);

    size_t left = 0;
    size_t right = count;
    while (left < right) {
      const size_t mid = left + (right - left) / 2;
      uint64_t record_offset;
      std::memcpy(&record_offset, data->media_index.data + sizeof(uint32_t) + mid * sizeof(uint64_t), sizeof(uint64_t));

      const uint8_t* record = data->media.data + record_offset;
      auto path_size = read_val<uint16_t>(record);
      std::string_view indexed_path = read_str(record, path_size);
      if (indexed_path < media_path) {
        left = mid + 1;
      } else if (indexed_path > media_path) {
        right = mid;
      } else {
        auto blob_size = read_val<uint32_t>(record);
        const char* blob_data = reinterpret_cast<const char*>(record);
        return {.data = blob_data, .size = blob_size};
      }
    }
    return {};
  }
  return {};
}

std::vector<DictionaryStyle> DictionaryQuery::get_styles() const {
  return term_dicts_ | std::views::filter([](const auto& d) { return !d.styles.empty(); }) |
         std::views::transform([](const auto& d) { return DictionaryStyle{d.name, d.styles}; }) |
         std::ranges::to<std::vector>();
}

std::vector<std::string> DictionaryQuery::get_freq_dict_order() const {
  return freq_dicts_ | std::views::transform([](const auto& d) { return d.name; }) | std::ranges::to<std::vector>();
}
