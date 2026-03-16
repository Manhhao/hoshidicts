#include "hoshidicts/query.hpp"

#include <ankerl/unordered_dense.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif
#include <zstd.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <memory>
#include <mutex>
#include <ranges>
#include <string_view>

#include "hash/hash.hpp"
#include "json/yomitan_parser.hpp"

namespace {
std::pair<void*, size_t> map_file(const std::string& path) {
#ifdef _WIN32
  HANDLE file =
      CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    return {};
  }

  LARGE_INTEGER file_size;
  if (!GetFileSizeEx(file, &file_size)) {
    CloseHandle(file);
    return {};
  }

  HANDLE mapping = CreateFileMappingA(file, nullptr, PAGE_READONLY, 0, 0, nullptr);
  CloseHandle(file);
  if (!mapping) {
    return {};
  }

  void* data = MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0);
  CloseHandle(mapping);
  if (!data) {
    return {};
  }

  return {data, static_cast<size_t>(file_size.QuadPart)};
#else
  int fd = open(path.c_str(), O_RDONLY);
  if (fd == -1) {
    return {};
  }

  struct stat st{};
  if (fstat(fd, &st) != 0) {
    close(fd);
    return {};
  }

  void* data = mmap(nullptr, st.st_size, PROT_READ, MAP_SHARED, fd, 0);
  close(fd);
  if (data == MAP_FAILED) {
    return {};
  }

  return {data, static_cast<size_t>(st.st_size)};
#endif
}

void unmap_file(void* data, size_t size) {
  if (!data) {
    return;
  }
#ifdef _WIN32
  UnmapViewOfFile(data);
#else
  munmap(data, size);
#endif
}

uint8_t read_u8(const uint8_t*& addr) { return *addr++; }

uint16_t read_u16(const uint8_t*& addr) {
  uint16_t result;
  std::memcpy(&result, addr, sizeof(uint16_t));
  addr += sizeof(uint16_t);
  return result;
}

uint32_t read_u32(const uint8_t*& addr) {
  uint32_t result;
  std::memcpy(&result, addr, sizeof(uint32_t));
  addr += sizeof(uint32_t);
  return result;
}

uint64_t read_u64(const uint8_t*& addr) {
  uint64_t result;
  std::memcpy(&result, addr, sizeof(uint64_t));
  addr += sizeof(uint64_t);
  return result;
}

std::string_view read_str(const uint8_t*& addr, uint32_t len) {
  std::string_view result(reinterpret_cast<const char*>(addr), len);
  addr += len;
  return result;
}
}

struct DictionaryQuery::DictionaryData {
  hash::linear table;
  uint8_t* blobs = nullptr;
  size_t blobs_size = 0;
  uint8_t* hash_table = nullptr;
  size_t hash_table_size = 0;
  uint8_t* media = nullptr;
  size_t media_size = 0;
  mutable std::once_flag media_index_once;
  mutable ankerl::unordered_dense::map<std::string_view, std::pair<uint32_t, uint32_t>> media_index;

  void build_media_index() const {
    if (!media) {
      return;
    }

    const uint8_t* addr = media;
    const uint8_t* eof = addr + media_size;
    while (addr < eof) {
      uint16_t path_size = read_u16(addr);
      std::string_view media_path = read_str(addr, path_size);
      uint32_t blob_size = read_u32(addr);

      media_index.emplace(media_path, std::pair<uint32_t, uint32_t>{blob_size, static_cast<uint32_t>(addr - media)});
      addr += blob_size;
    }
  }

  ~DictionaryData() {
    unmap_file(blobs, blobs_size);
    unmap_file(hash_table, hash_table_size);
    unmap_file(media, media_size);
  }
};

DictionaryQuery::DictionaryQuery() = default;
DictionaryQuery::~DictionaryQuery() = default;

DictionaryQuery::DictionaryQuery(DictionaryQuery&&) noexcept = default;
DictionaryQuery& DictionaryQuery::operator=(DictionaryQuery&&) noexcept = default;

void DictionaryQuery::add_dict(const std::string& path, DictionaryType type) {
  if (!std::filesystem::is_regular_file(path + "/.hoshidicts_1")) {
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

  auto [hash_table, hash_table_size] = map_file(path + "/hash.table");
  if (!hash_table) {
    return;
  }
  dict.data->hash_table_size = hash_table_size;
  dict.data->hash_table = reinterpret_cast<uint8_t*>(hash_table);
  dict.data->table.load(hash_table);

  auto [blobs, blobs_size] = map_file(path + "/blobs.bin");
  if (!blobs) {
    return;
  }
  dict.data->blobs_size = blobs_size;
  dict.data->blobs = reinterpret_cast<uint8_t*>(blobs);

  auto [media, media_size] = map_file(path + "/media.bin");
  if (media) {
    dict.data->media_size = media_size;
    dict.data->media = reinterpret_cast<uint8_t*>(media);
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
  std::map<std::pair<std::string_view, std::string_view>, TermResult> term_map;
  for (const auto& [name, styles, data] : term_dicts_) {
    uint64_t offset_addr = data->table(expression);
    if (offset_addr == 0) {
      continue;
    }
    const uint8_t* index_addr = data->blobs + offset_addr;

    uint32_t count = read_u32(index_addr);
    for (uint32_t i = 0; i < count; i++) {
      uint64_t offset = read_u64(index_addr);
      const uint8_t* blob_addr = data->blobs + offset;

      // first byte encodes term (0) or meta (1) entry
      uint8_t type = read_u8(blob_addr);
      if (type != 0) {
        continue;
      }

      uint16_t expr_len = read_u16(blob_addr);
      std::string_view expr = read_str(blob_addr, expr_len);

      uint16_t reading_len = read_u16(blob_addr);
      std::string_view reading = read_str(blob_addr, reading_len);

      if (expr != expression && reading != expression) {
        continue;
      }

      uint64_t glossary_offset = read_u64(blob_addr);
      uint32_t glossary_size = read_u32(blob_addr);
      std::string glossary = decompress_glossary(data->blobs + glossary_offset, glossary_size);

      uint8_t def_tags_size = read_u8(blob_addr);
      std::string_view definition_tags = read_str(blob_addr, def_tags_size);

      uint8_t rules_size = read_u8(blob_addr);
      std::string_view rules = read_str(blob_addr, rules_size);

      uint8_t term_tag_size = read_u8(blob_addr);
      std::string_view term_tags = read_str(blob_addr, term_tag_size);

      GlossaryEntry entry;
      entry.dict_name = name;
      entry.definition_tags = definition_tags;
      entry.term_tags = term_tags;
      entry.glossary = glossary;

      auto [it, inserted] = term_map.try_emplace({expr, reading});
      if (inserted) {
        it->second = {.expression = std::string(expr),
                      .reading = std::string(reading),
                      .rules = std::string(rules),
                      .glossaries = {},
                      .frequencies = {}};
      } else {
        if (!rules.empty()) {
          if (!it->second.rules.empty()) {
            it->second.rules += " ";
          }
          it->second.rules += rules;
        }
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
      const uint8_t* index_addr = data->blobs + offset_addr;
      uint32_t count = read_u32(index_addr);

      std::vector<Frequency> frequencies;
      for (uint32_t i = 0; i < count; i++) {
        uint64_t offset = read_u64(index_addr);
        const uint8_t* blob_addr = data->blobs + offset;

        uint8_t type = read_u8(blob_addr);
        if (type != 1) {
          continue;
        }

        uint16_t expr_len = read_u16(blob_addr);
        std::string_view expr = read_str(blob_addr, expr_len);
        if (expr != term.expression) {
          continue;
        }

        uint8_t mode_len = read_u8(blob_addr);
        std::string_view mode = read_str(blob_addr, mode_len);
        if (mode != "freq") {
          continue;
        }

        uint32_t freq_data_size = read_u32(blob_addr);
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
      const uint8_t* index_addr = data->blobs + offset_addr;
      uint32_t count = read_u32(index_addr);

      std::vector<int> pitch_positions;
      for (uint32_t i = 0; i < count; i++) {
        uint64_t offset = read_u64(index_addr);
        const uint8_t* blob_addr = data->blobs + offset;

        uint8_t type = read_u8(blob_addr);
        if (type != 1) {
          continue;
        }

        uint16_t expr_len = read_u16(blob_addr);
        std::string_view expr = read_str(blob_addr, expr_len);
        if (expr != term.expression) {
          continue;
        }

        uint8_t mode_len = read_u8(blob_addr);
        std::string_view mode = read_str(blob_addr, mode_len);
        if (mode != "pitch") {
          continue;
        }

        uint32_t pitch_data_size = read_u32(blob_addr);
        std::string_view pitch_data = read_str(blob_addr, pitch_data_size);

        ParsedPitch parsed;
        if (yomitan_parser::parse_pitch(pitch_data, parsed)) {
          if (!parsed.reading.empty() && parsed.reading != term.reading) {
            continue;
          }
          pitch_positions.insert(pitch_positions.end(), parsed.pitches.begin(), parsed.pitches.end());
        }
      }
      if (!pitch_positions.empty()) {
        term.pitches.emplace_back(PitchEntry{.dict_name = name, .pitch_positions = std::move(pitch_positions)});
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

std::vector<char> DictionaryQuery::get_media_file(const std::string& dict_name, const std::string& media_path) const {
  for (const auto& [name, styles, data] : term_dicts_) {
    if (name != dict_name) {
      continue;
    }

    std::call_once(data->media_index_once, [data_ptr = data.get()]() { data_ptr->build_media_index(); });

    auto it = data->media_index.find(media_path);
    if (it == data->media_index.end()) {
      return {};
    }

    const auto [size, offset] = it->second;
    const char* media_data = reinterpret_cast<const char*>(data->media + offset);
    return {media_data, media_data + size};
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
