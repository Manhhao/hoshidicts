#include "hoshidicts/query.hpp"

#include <ankerl/unordered_dense.h>
#include <zstd.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <ranges>
#include <string_view>
#include <vector>

#include "compression/glossary_codec.hpp"
#include "hash/hash.hpp"
#include "hoshidicts/importer.hpp"
#include "json/yomitan_parser.hpp"
#include "memory/memory.hpp"
#include "path_utils.hpp"

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
  int version;
  hash::linear table;
  hash::bloom bloom;
  memory::mapped_file blobs;
  memory::mapped_file hash_table;
  memory::mapped_file bloom_filter;
  memory::mapped_file media;
  memory::mapped_file media_index;
  memory::mapped_file glossary_dictionary;
  glossary_codec::Decoder glossary_decoder;

  ~DictionaryData() {
    memory::unmap(blobs);
    memory::unmap(hash_table);
    memory::unmap(bloom_filter);
    memory::unmap(media);
    memory::unmap(media_index);
    memory::unmap(glossary_dictionary);
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

void DictionaryQuery::add_dict(const std::string& path_utf8, DictionaryType type) {
  const std::filesystem::path path = path_utils::from_utf8(path_utf8);
  int version = 0;
  if (std::filesystem::is_regular_file(path / ".hoshidicts_4")) {
    version = 4;
  } else if (std::filesystem::is_regular_file(path / ".hoshidicts_3")) {
    version = 3;
  } else if (std::filesystem::is_regular_file(path / ".hoshidicts_2")) {
    version = 2;
  } else if (std::filesystem::is_regular_file(path / ".hoshidicts_1")) {
    version = 1;
  } else {
    return;
  }

  Dictionary dict;
  Summary summary;
  std::ifstream index_file(path / "index.json", std::ios::binary);
  if (!index_file) {
    return;
  }
  std::string buf(std::istreambuf_iterator<char>(index_file), {});
  if (glz::read<glz::opts{.error_on_unknown_keys = false}>(summary, buf)) {
    return;
  }

  dict.name = summary.title.empty() ? path_utils::to_utf8(path.stem()) : summary.title;
  dict.styles = summary.styles;
  if (dict.styles.empty() && std::filesystem::exists(path / "styles.css")) {
    std::ifstream f(path / "styles.css");
    dict.styles = std::string(std::istreambuf_iterator<char>(f), {});
  }

  dict.data = std::make_unique<DictionaryData>();
  dict.data->version = version;

  dict.data->hash_table = memory::map_rd(path / "hash.table");
  if (!dict.data->hash_table) {
    return;
  }
  if (!dict.data->table.load(dict.data->hash_table.data, dict.data->hash_table.size)) {
    return;
  }

  dict.data->bloom_filter = memory::map_rd(path / "bloom.filter");
  if (!dict.data->bloom_filter) {
    return;
  }
  if (!dict.data->bloom.load(dict.data->bloom_filter.data, dict.data->bloom_filter.size)) {
    return;
  }
  dict.data->table.set_bloom(&dict.data->bloom);

  dict.data->blobs = memory::map_rd(path / "blobs.bin");
  if (!dict.data->blobs) {
    return;
  }

  if (version >= 4) {
    dict.data->glossary_dictionary = memory::map_rd(path / "glossary.dict");
    if (!dict.data->glossary_dictionary) {
      return;
    }
    std::vector<char> dictionary(dict.data->glossary_dictionary.data,
                                 dict.data->glossary_dictionary.data + dict.data->glossary_dictionary.size);
    dict.data->glossary_decoder = glossary_codec::make_decoder(dictionary);
    if (!dict.data->glossary_decoder) {
      return;
    }
  }

  dict.data->media = memory::map_rd(path / "media.bin");
  if (dict.data->media) {
    dict.data->media_index = memory::map_rd(path / "media.idx");
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
    case KANJI:
      kanji_dicts_.push_back(std::move(dict));
      break;
  }
}

void DictionaryQuery::add_term_dict(const std::string& path) { add_dict(path, DictionaryQuery::DictionaryType::TERM); }

void DictionaryQuery::add_freq_dict(const std::string& path) { add_dict(path, DictionaryQuery::DictionaryType::FREQ); }

void DictionaryQuery::add_pitch_dict(const std::string& path) {
  add_dict(path, DictionaryQuery::DictionaryType::PITCH);
}

void DictionaryQuery::add_kanji_dict(const std::string& path) {
  add_dict(path, DictionaryQuery::DictionaryType::KANJI);
}

std::vector<TermResult> DictionaryQuery::query(const std::string& expression) const {
  auto results = query_raw(expression);
  for (auto& term : results) {
    materialize(term);
  }
  return results;
}

std::vector<TermResult> DictionaryQuery::query_raw(const std::string& expression) const {
  std::map<std::pair<std::string_view, std::string_view>, TermResult> term_map;
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

      if (data->version >= 2) {
        auto redirect_count = read_val<uint32_t>(blob_addr);
        for (uint32_t r = 0; r < redirect_count; r++) {
          auto form_of_len = read_val<uint32_t>(blob_addr);
          read_str(blob_addr, form_of_len);
          auto rule_count = read_val<uint32_t>(blob_addr);
          for (uint32_t j = 0; j < rule_count; j++) {
            auto rule_len = read_val<uint32_t>(blob_addr);
            read_str(blob_addr, rule_len);
          }
        }
      }

      int score = 0;
      if (data->version >= 3) {
        score = read_val<int32_t>(blob_addr);
      }

      GlossaryEntry entry;
      entry.dict_name = name;
      entry.definition_tags = definition_tags;
      entry.term_tags = term_tags;
      entry.compressed_data = data->blobs.data + glossary_offset;
      entry.compressed_size = glossary_size;
      entry.compression_dictionary = data->glossary_decoder.get();

      auto [it, inserted] = term_map.try_emplace({expr, reading});
      if (inserted) {
        it->second = {.expression = std::string(expr),
                      .reading = std::string(reading),
                      .rules = std::string(rules),
                      .score = score,
                      .glossaries = {},
                      .frequencies = {}};
      } else {
        if (!rules.empty()) {
          if (!it->second.rules.empty()) {
            it->second.rules += " ";
          }
          it->second.rules += rules;
        }
        it->second.score = std::max(it->second.score, score);
      }
      it->second.glossaries.push_back(std::move(entry));
    }
  }

  auto results = term_map | std::views::values | std::views::as_rvalue | std::ranges::to<std::vector>();
  query_freq(results);
  query_pitch(results);

  return results;
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

      std::vector<Pitch> pitches;
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
            for (auto& accent : parsed.pitches) {
              pitches.emplace_back(Pitch{.position = accent.position,
                                         .pattern = std::move(accent.pattern),
                                         .nasal = std::move(accent.nasal),
                                         .devoice = std::move(accent.devoice)});
            }
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
      if (!pitches.empty() || !transcriptions.empty()) {
        term.pitches.emplace_back(PitchEntry{
            .dict_name = name,
            .pitches = std::move(pitches),
            .transcriptions = std::move(transcriptions),
        });
      }
    }
  }
}

KanjiResult DictionaryQuery::query_kanji(const std::string& kanji) const {
  KanjiResult result;
  result.character = kanji;

  for (const auto& [name, styles, data] : kanji_dicts_) {
    uint64_t offset_addr = data->table(kanji);
    if (offset_addr == 0) {
      continue;
    }
    const uint8_t* index_addr = data->blobs.data + offset_addr;
    auto count = read_val<uint32_t>(index_addr);

    for (uint32_t i = 0; i < count; i++) {
      auto offset = read_val<uint64_t>(index_addr);
      const uint8_t* blob_addr = data->blobs.data + offset;

      auto type = read_val<uint8_t>(blob_addr);
      if (type != 2) {
        continue;
      }

      auto char_len = read_val<uint8_t>(blob_addr);
      std::string_view char_sv = read_str(blob_addr, char_len);
      if (char_sv != kanji) {
        continue;
      }

      auto onyomi_len = read_val<uint16_t>(blob_addr);
      std::string_view onyomi = read_str(blob_addr, onyomi_len);

      auto kunyomi_len = read_val<uint16_t>(blob_addr);
      std::string_view kunyomi = read_str(blob_addr, kunyomi_len);

      auto tags_len = read_val<uint16_t>(blob_addr);
      std::string_view tags = read_str(blob_addr, tags_len);

      KanjiEntry entry;
      entry.dict_name = name;
      entry.onyomi = onyomi;
      entry.kunyomi = kunyomi;
      entry.tags = tags;

      auto def_count = read_val<uint16_t>(blob_addr);
      for (uint16_t j = 0; j < def_count; j++) {
        auto def_len = read_val<uint16_t>(blob_addr);
        std::string_view def = read_str(blob_addr, def_len);
        entry.definitions.emplace_back(def);
      }

      auto stat_count = read_val<uint16_t>(blob_addr);
      for (uint16_t j = 0; j < stat_count; j++) {
        auto key_len = read_val<uint16_t>(blob_addr);
        std::string_view key = read_str(blob_addr, key_len);
        auto val_len = read_val<uint16_t>(blob_addr);
        std::string_view val = read_str(blob_addr, val_len);
        entry.stats.emplace(key, val);
      }

      result.entries.push_back(std::move(entry));
    }
  }

  return result;
}

std::string DictionaryQuery::decompress_glossary(const void* data, size_t size, const void* dictionary) {
  return glossary_codec::decompress(data, size, static_cast<const ZSTD_DDict*>(dictionary));
}

void DictionaryQuery::materialize(TermResult& term) const {
  for (auto& g : term.glossaries) {
    g.glossary = decompress_glossary(g.compressed_data, g.compressed_size, g.compression_dictionary);
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
