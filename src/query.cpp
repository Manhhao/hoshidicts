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
#include <mutex>
#include <ranges>
#include <string_view>
#include <vector>

#include "hash/phf.hpp"
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

std::vector<int> read_ints(const uint8_t*& addr) {
  std::vector<int> result(read_val<uint16_t>(addr));
  for (int& value : result) {
    value = read_val<int32_t>(addr);
  }
  return result;
}

bool read_frequency(uint8_t type, const uint8_t* addr, ParsedFrequency& out) {
  if (type == 1) {
    auto size = read_val<uint32_t>(addr);
    return yomitan_parser::parse_frequency(read_str(addr, size), out);
  }
  auto reading_len = read_val<uint16_t>(addr);
  out.reading = read_str(addr, reading_len);
  out.value = read_val<int32_t>(addr);
  auto display_len = read_val<uint16_t>(addr);
  out.display_value = read_str(addr, display_len);
  return true;
}

bool read_pitch(uint8_t type, const uint8_t* addr, ParsedPitch& out) {
  if (type == 1) {
    auto size = read_val<uint32_t>(addr);
    return yomitan_parser::parse_pitch(read_str(addr, size), out);
  }
  auto reading_len = read_val<uint16_t>(addr);
  out.reading = read_str(addr, reading_len);
  out.pitches.resize(read_val<uint16_t>(addr));
  for (auto& accent : out.pitches) {
    accent.position = read_val<int32_t>(addr);
    auto pattern_len = read_val<uint16_t>(addr);
    accent.pattern = read_str(addr, pattern_len);
    accent.nasal = read_ints(addr);
    accent.devoice = read_ints(addr);
  }
  return true;
}

bool read_ipa(uint8_t type, const uint8_t* addr, ParsedPitch& out) {
  if (type == 1) {
    auto size = read_val<uint32_t>(addr);
    return yomitan_parser::parse_ipa(read_str(addr, size), out);
  }
  auto reading_len = read_val<uint16_t>(addr);
  out.reading = read_str(addr, reading_len);
  out.transcriptions.resize(read_val<uint16_t>(addr));
  for (auto& transcription : out.transcriptions) {
    auto len = read_val<uint16_t>(addr);
    transcription = read_str(addr, len);
  }
  return true;
}

ZSTD_DCtx* thread_dctx() {
  static thread_local std::unique_ptr<ZSTD_DCtx, decltype(&ZSTD_freeDCtx)> ctx(ZSTD_createDCtx(), ZSTD_freeDCtx);
  return ctx.get();
}
}

struct DictionaryQuery::DictionaryData {
  int version;
  hash::phf table;
  memory::mapped_file blobs;
  memory::mapped_file phf_table;
  memory::mapped_file media_index;
  std::mutex media_mutex;
  ankerl::unordered_dense::map<uint64_t, memory::mapped_file> media_views;
  ZSTD_DDict* zstd_dict = nullptr;

  ~DictionaryData() {
    memory::unmap(blobs);
    memory::unmap(phf_table);
    memory::unmap(media_index);
    for (const auto& [offset, view] : media_views) {
      memory::unmap(view);
    }
    ZSTD_freeDDict(zstd_dict);
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
  try {
    add_dict_(path_utf8, type);
  } catch (const std::exception&) {
  }
}

void DictionaryQuery::add_dict_(const std::string& path_utf8, DictionaryType type) {
  const std::filesystem::path path = path_utils::from_utf8(path_utf8);
  int version = 0;
  if (std::filesystem::is_regular_file(path / ".hoshidicts_5")) {
    version = 5;
  } else if (std::filesystem::is_regular_file(path / ".hoshidicts_4")) {
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
  dict.path = path_utf8;
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

  if (!std::filesystem::exists(path / "phf.table") && !hash::phf::migrate(path)) {
    return;
  }
  dict.data->phf_table = memory::map_rd(path / "phf.table");
  if (!dict.data->phf_table) {
    return;
  }
  if (!dict.data->table.load(dict.data->phf_table.data, dict.data->phf_table.size)) {
    return;
  }

  dict.data->blobs = memory::map_rd(path / "blobs.bin");
  if (!dict.data->blobs) {
    return;
  }

  dict.data->media_index = memory::map_rd(path / "media.idx");

  if (std::filesystem::exists(path / "dict.zstd")) {
    std::ifstream f(path / "dict.zstd", std::ios::binary);
    const std::string blob(std::istreambuf_iterator<char>(f), {});
    dict.data->zstd_dict = ZSTD_createDDict(blob.data(), blob.size());
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

size_t DictionaryQuery::remove_dict(const std::string& path) {
  size_t removed = 0;
  for (auto* dicts : {&term_dicts_, &freq_dicts_, &pitch_dicts_, &kanji_dicts_}) {
    removed += std::erase_if(*dicts, [&path](const Dictionary& d) { return d.path == path; });
  }
  return removed;
}

bool DictionaryQuery::set_dict_order(const std::vector<std::string>& paths) {
  for (const auto& path : paths) {
    const auto loaded = [&path](const std::vector<Dictionary>& dicts) {
      return std::ranges::any_of(dicts, [&path](const Dictionary& d) { return d.path == path; });
    };
    if (!loaded(term_dicts_) && !loaded(freq_dicts_) && !loaded(pitch_dicts_) && !loaded(kanji_dicts_)) {
      return false;
    }
  }
  const auto rank = [&paths](const Dictionary& d) {
    const auto it = std::ranges::find(paths, d.path);
    return it == paths.end() ? paths.size() : static_cast<size_t>(it - paths.begin());
  };
  for (auto* dicts : {&term_dicts_, &freq_dicts_, &pitch_dicts_, &kanji_dicts_}) {
    std::ranges::stable_sort(*dicts, {}, rank);
  }
  return true;
}

std::vector<TermResult> DictionaryQuery::query(const std::string& expression) const {
  auto results = query_raw(expression);
  query_freq(results);
  query_pitch(results);
  for (auto& term : results) {
    materialize(term);
  }
  return results;
}

std::vector<TermResult> DictionaryQuery::query_raw(const std::string& expression) const {
  std::map<std::pair<std::string_view, std::string_view>, TermResult> term_map;
  for (const auto& [path, name, styles, data] : term_dicts_) {
    auto location = data->table(expression);
    if (!location) {
      continue;
    }
    const uint8_t* index_addr = data->blobs.data + location->offset;

    uint32_t count = location->single ? 1 : read_val<uint32_t>(index_addr);
    for (uint32_t i = 0; i < count; i++) {
      const uint8_t* blob_addr = location->single ? index_addr : data->blobs.data + read_val<uint64_t>(index_addr);

      // first byte encodes term (0), meta (1 or 3) or kanji (2) entry
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
      entry.zstd_dict = data->zstd_dict;

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

  return term_map | std::views::values | std::views::as_rvalue | std::ranges::to<std::vector>();
}

void DictionaryQuery::query_freq(std::vector<TermResult>& terms) const {
  for (auto& term : terms) {
    for (const auto& [path, name, styles, data] : freq_dicts_) {
      auto location = data->table(term.expression);
      if (!location) {
        continue;
      }
      const uint8_t* index_addr = data->blobs.data + location->offset;
      uint32_t count = location->single ? 1 : read_val<uint32_t>(index_addr);

      std::vector<Frequency> frequencies;
      for (uint32_t i = 0; i < count; i++) {
        const uint8_t* blob_addr = location->single ? index_addr : data->blobs.data + read_val<uint64_t>(index_addr);

        auto type = read_val<uint8_t>(blob_addr);
        if (type != 1 && type != 3) {
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

        ParsedFrequency parsed;
        if (read_frequency(type, blob_addr, parsed)) {
          if (!parsed.reading.empty() && parsed.reading != term.reading) {
            continue;
          }
          frequencies.emplace_back(Frequency{.value = parsed.value, .display_value = std::move(parsed.display_value)});
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
    query_pitch(term);
  }
}

void DictionaryQuery::query_pitch(TermResult& term) const {
  for (const auto& [path, name, styles, data] : pitch_dicts_) {
    auto location = data->table(term.expression);
    if (!location) {
      continue;
    }
    const uint8_t* index_addr = data->blobs.data + location->offset;
    uint32_t count = location->single ? 1 : read_val<uint32_t>(index_addr);

    std::vector<Pitch> pitches;
    std::vector<std::string> transcriptions;
    for (uint32_t i = 0; i < count; i++) {
      const uint8_t* blob_addr = location->single ? index_addr : data->blobs.data + read_val<uint64_t>(index_addr);

      auto type = read_val<uint8_t>(blob_addr);
      if (type != 1 && type != 3) {
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
        if (read_pitch(type, blob_addr, parsed)) {
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
        if (read_ipa(type, blob_addr, parsed)) {
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

KanjiResult DictionaryQuery::query_kanji(const std::string& kanji) const {
  KanjiResult result;
  result.character = kanji;

  for (const auto& [path, name, styles, data] : kanji_dicts_) {
    auto location = data->table(kanji);
    if (!location) {
      continue;
    }
    const uint8_t* index_addr = data->blobs.data + location->offset;
    uint32_t count = location->single ? 1 : read_val<uint32_t>(index_addr);

    for (uint32_t i = 0; i < count; i++) {
      const uint8_t* blob_addr = location->single ? index_addr : data->blobs.data + read_val<uint64_t>(index_addr);

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

std::string DictionaryQuery::decompress_glossary(const void* data, size_t size, const ZSTD_DDict_s* dict) {
  if (!data || size == 0) {
    return "";
  }

  unsigned long long decompressed_size = ZSTD_getFrameContentSize(data, size);
  if (decompressed_size == ZSTD_CONTENTSIZE_ERROR || decompressed_size == ZSTD_CONTENTSIZE_UNKNOWN) {
    return "";
  }

  std::string result;
  result.resize(decompressed_size);

  size_t actual_size = ZSTD_decompress_usingDDict(thread_dctx(), result.data(), result.size(), data, size, dict);
  if (ZSTD_isError(actual_size)) {
    return "";
  }

  result.resize(actual_size);
  return result;
}

void DictionaryQuery::materialize(TermResult& term) const {
  for (auto& g : term.glossaries) {
    g.glossary = decompress_glossary(g.compressed_data, g.compressed_size, g.zstd_dict);
  }
}

std::vector<char> DictionaryQuery::get_media_file(const std::string& dict_name, const std::string& media_path) const {
  auto view = get_media_file_view(dict_name, media_path);
  return {view.data, view.data + view.size};
}

MediaFileView DictionaryQuery::get_media_file_view(const std::string& dict_name, const std::string& media_path) const {
  for (const auto& [path, name, styles, data] : term_dicts_) {
    if (name != dict_name) {
      continue;
    }

    const auto media_file = path_utils::from_utf8(path) / "media.bin";
    const intptr_t media = memory::open_rd(media_file);
    if (media == -1 || !data->media_index) {
      return {};
    }

    const uint8_t* ptr = data->media_index.data;
    auto count = read_val<uint32_t>(ptr);

    MediaFileView result{};
    std::string header(sizeof(uint16_t) + media_path.size() + sizeof(uint32_t), '\0');
    size_t left = 0;
    size_t right = count;
    while (left < right) {
      const size_t mid = left + (right - left) / 2;
      uint64_t record_offset;
      std::memcpy(&record_offset, data->media_index.data + sizeof(uint32_t) + mid * sizeof(uint64_t), sizeof(uint64_t));

      memory::read_at(media, record_offset, header.data(), header.size());
      const auto* record = reinterpret_cast<const uint8_t*>(header.data());
      auto path_size = read_val<uint16_t>(record);
      std::string_view indexed_path = read_str(record, std::min<size_t>(path_size, header.size() - sizeof(uint16_t)));
      if (indexed_path < media_path) {
        left = mid + 1;
      } else if (indexed_path > media_path) {
        right = mid;
      } else {
        auto blob_size = read_val<uint32_t>(record);
        std::lock_guard lock(data->media_mutex);
        auto& view = data->media_views[record_offset];
        if (!view && blob_size > 0) {
          view = memory::map_rd(media_file, record_offset + header.size(), blob_size);
        }
        result = {.data = reinterpret_cast<const char*>(view.data), .size = blob_size};
        break;
      }
    }
    memory::close_rd(media);
    return result;
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
