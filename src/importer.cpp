#include "hoshidicts/importer.hpp"

#include <ankerl/unordered_dense.h>
#include <xxh3.h>
#include <zip.h>
#include <zstd.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <future>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "hash/hash.hpp"
#include "json/yomitan_parser.hpp"

namespace {
struct Files {
  std::vector<int> term_banks;
  std::vector<int> meta_banks;
  std::vector<int> tag_banks;
  std::vector<int> media_files;
};

struct ProcessedFile {
  std::vector<char> data;
  std::vector<std::pair<uint64_t, uint64_t>> term_offsets;
  ankerl::unordered_dense::map<uint64_t, std::vector<char>> glossaries;
  std::vector<std::pair<uint64_t, uint64_t>> glossary_offsets;
  size_t count = 0;
};

struct MediaFile {
  std::string path;
  std::vector<char> blob;
};

void setup_stream_exceptions(std::ofstream& stream) { stream.exceptions(std::ios::failbit | std::ios::badbit); }

std::string read_file_by_index(zip_t* archive, int index) {
  if (zip_entry_openbyindex(archive, index) != 0) {
    return "";
  }

  const size_t size = zip_entry_size(archive);
  std::string buffer;
  buffer.resize(size);
  ssize_t bytes_read = 0;
  if (size > 0) {
    bytes_read = zip_entry_noallocread(archive, buffer.data(), size);
  }
  zip_entry_close(archive);

  if (bytes_read < 0) {
    return "";
  }

  return buffer;
}

std::string read_file_by_name(zip_t* archive, const char* name) {
  if (zip_entry_open(archive, name) != 0) {
    return "";
  }

  const size_t size = zip_entry_size(archive);
  std::string buffer;
  buffer.resize(size);
  ssize_t bytes_read = 0;
  if (size > 0) {
    bytes_read = zip_entry_noallocread(archive, buffer.data(), size);
  }
  zip_entry_close(archive);

  if (bytes_read < 0) {
    return "";
  }

  return buffer;
}

std::optional<MediaFile> read_media_by_index(zip_t* archive, int index) {
  if (zip_entry_openbyindex(archive, index) != 0) {
    return std::nullopt;
  }

  MediaFile out;
  const size_t size = zip_entry_size(archive);
  out.path = zip_entry_name(archive);
  out.blob.resize(size);
  ssize_t bytes_read = 0;
  if (size > 0) {
    bytes_read = zip_entry_noallocread(archive, out.blob.data(), size);
  }
  zip_entry_close(archive);

  if (bytes_read < 0) {
    return std::nullopt;
  }

  return out;
}

Files get_files(zip_t* archive) {
  Files files;
  const ssize_t num_entries = zip_entries_total(archive);
  if (num_entries < 0) {
    return files;
  }

  for (int i = 0; i < num_entries; ++i) {
    if (zip_entry_openbyindex(archive, i) != 0) {
      continue;
    }

    if (zip_entry_isdir(archive) == 1) {
      zip_entry_close(archive);
      continue;
    }

    const char* raw_name = zip_entry_name(archive);
    if (raw_name != nullptr) {
      const std::string_view name(raw_name);
      if (name.starts_with("term_bank_")) {
        files.term_banks.push_back(i);
      } else if (name.starts_with("term_meta_bank_")) {
        files.meta_banks.push_back(i);
      } else if (name.starts_with("tag_bank_")) {
        files.tag_banks.push_back(i);
      } else if (!(name == "styles.css" || name == "index.json")) {
        files.media_files.push_back(i);
      }
    }
    zip_entry_close(archive);
  }

  return files;
}

void write_u8(std::vector<char>& out, uint8_t value) { out.push_back(static_cast<char>(value)); }

void write_u16(std::vector<char>& out, uint16_t value) {
  const size_t old_size = out.size();
  out.resize(old_size + sizeof(uint16_t));
  std::memcpy(out.data() + old_size, &value, sizeof(uint16_t));
}

void write_u32(std::vector<char>& out, uint32_t value) {
  const size_t old_size = out.size();
  out.resize(old_size + sizeof(uint32_t));
  std::memcpy(out.data() + old_size, &value, sizeof(uint32_t));
}

void write_u64(std::vector<char>& out, uint64_t value) {
  const size_t old_size = out.size();
  out.resize(old_size + sizeof(uint64_t));
  std::memcpy(out.data() + old_size, &value, sizeof(uint64_t));
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

void radix_sort(std::vector<std::pair<uint64_t, uint64_t>>& offsets) {
  if (offsets.size() < 2) {
    return;
  }

  std::vector<std::pair<uint64_t, uint64_t>> temp(offsets.size());
  auto* src = &offsets;
  auto* dst = &temp;

  for (uint32_t shift = 0; shift < 64; shift += 8) {
    std::array<size_t, 256> pos{};
    for (const auto& entry : *src) {
      pos[(entry.first >> shift) & 0xff]++;
    }

    size_t total = 0;
    for (size_t& p : pos) {
      size_t count = p;
      p = total;
      total += count;
    }

    for (const auto& entry : *src) {
      (*dst)[pos[(entry.first >> shift) & 0xff]++] = entry;
    }

    std::swap(src, dst);
  }
}

ProcessedFile process_term_bank(const std::string& content) {
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

  for (auto& term : out) {
    const std::string_view glossary = term.glossary.str;
    uint64_t glossary_hash = XXH3_64bits(glossary.data(), glossary.size());
    auto it = processed.glossaries.find(glossary_hash);
    if (it == processed.glossaries.end()) {
      const size_t bound = ZSTD_compressBound(glossary.size());
      compressed.resize(bound);
      const size_t compressed_size =
          ZSTD_compressCCtx(cctx, compressed.data(), bound, glossary.data(), glossary.size(), 0);
      if (ZSTD_isError(compressed_size)) {
        ZSTD_freeCCtx(cctx);
        throw std::runtime_error("failed to compress glossary");
      }
      compressed.resize(compressed_size);
      processed.glossaries.emplace(glossary_hash, compressed);
    }

    uint64_t offset = processed.data.size();
    uint32_t blob_size = processed.glossaries[glossary_hash].size();
    std::string_view expr = term.expression;
    std::string_view reading = term.reading.empty() ? expr : term.reading;
    std::string_view definition_tags = term.definition_tags.value_or("");

    write_u8(processed.data, 0);
    write_u16(processed.data, expr.size());
    write_str(processed.data, expr);
    write_u16(processed.data, reading.size());
    write_str(processed.data, reading);

    uint64_t glossary_offset = processed.data.size();
    write_u64(processed.data, 0);
    write_u32(processed.data, blob_size);
    processed.glossary_offsets.emplace_back(glossary_hash, glossary_offset);

    write_u8(processed.data, definition_tags.size());
    write_str(processed.data, definition_tags);
    write_u8(processed.data, term.rules.size());
    write_str(processed.data, term.rules);
    write_u8(processed.data, term.term_tags.size());
    write_str(processed.data, term.term_tags);

    processed.term_offsets.emplace_back(XXH3_64bits(expr.data(), expr.size()), offset);
    if (reading != expr) {
      processed.term_offsets.emplace_back(XXH3_64bits(reading.data(), reading.size()), offset);
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

    write_u8(processed.data, 1);
    write_u16(processed.data, expr.size());
    write_str(processed.data, expr);
    write_u8(processed.data, mode.size());
    write_str(processed.data, mode);
    write_u32(processed.data, data.size());
    write_str(processed.data, data);

    processed.term_offsets.emplace_back(XXH3_64bits(expr.data(), expr.size()), offset);
    processed.count++;
  }

  return processed;
}

void write_terms(std::ofstream& file, std::vector<std::pair<uint64_t, uint64_t>>& offsets, const std::string& zip_path,
                 const std::vector<int>& files, uint64_t& write_offset, ImportResult& result, bool low_ram) {
  if (files.empty()) {
    return;
  }

  size_t max_threads =
      low_ram ? 2 : std::max<size_t>(4, static_cast<const unsigned long>(std::thread::hardware_concurrency()));
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

    for (auto& [hash, offset] : processed.term_offsets) {
      offsets.emplace_back(hash, offset + write_offset);
    }

    write_offset += processed.data.size();
    result.term_count += processed.count;
  };

  for (int file_index : files) {
    threads.push_back(std::async(std::launch::async, [&zip_path, file_index]() {
      zip_t* archive = zip_open(zip_path.c_str(), 0, 'r');
      if (!archive) {
        return ProcessedFile{};
      }
      std::string content = read_file_by_index(archive, file_index);
      zip_close(archive);
      return process_term_bank(content);
    }));

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

void write_meta(std::ofstream& file, std::vector<std::pair<uint64_t, uint64_t>>& offsets, const std::string& zip_path,
                const std::vector<int>& files, uint64_t& write_offset, ImportResult& result, bool low_ram) {
  if (files.empty()) {
    return;
  }

  size_t max_threads =
      low_ram ? 2 : std::max<size_t>(4, static_cast<const unsigned long>(std::thread::hardware_concurrency()));
  std::deque<std::future<ProcessedFile>> threads;
  auto write_processed = [&](ProcessedFile&& processed) {
    if (processed.data.empty()) {
      return;
    }
    file.write(processed.data.data(), static_cast<std::streamsize>(processed.data.size()));

    for (auto& [hash, offset] : processed.term_offsets) {
      offsets.emplace_back(hash, offset + write_offset);
    }

    write_offset += processed.data.size();
    result.meta_count += processed.count;
  };

  for (int file_index : files) {
    threads.push_back(std::async(std::launch::async, [&zip_path, file_index]() {
      zip_t* archive = zip_open(zip_path.c_str(), 0, 'r');
      if (!archive) {
        return ProcessedFile{};
      }
      std::string content = read_file_by_index(archive, file_index);
      zip_close(archive);
      return process_meta_bank(content);
    }));

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

void write_offset_index(std::ostream& file, std::vector<std::pair<uint64_t, uint64_t>>& offsets, uint64_t& write_offset,
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
    write_u32(offset_buf, count);
    for (size_t k = i; k < j; ++k) {
      write_u64(offset_buf, offsets[k].second);
    }

    write_offset += sizeof(uint32_t) + count * sizeof(uint64_t);
    i = j;
  }
  file.write(offset_buf.data(), static_cast<std::streamsize>(offset_buf.size()));
}

size_t write_media(const std::string& path, zip_t* archive, const std::vector<int>& files) {
  if (files.empty()) {
    return 0;
  }

  std::ofstream media(path + "/media.bin", std::ios::binary);
  setup_stream_exceptions(media);

  size_t media_count = 0;
  std::vector<char> blobs_buf;
  for (int file_index : files) {
    auto media = read_media_by_index(archive, file_index);
    if (!media.has_value()) {
      continue;
    }

    const auto blob_size = media->blob.size();
    write_u16(blobs_buf, media->path.size());
    write_str(blobs_buf, media->path);
    write_u32(blobs_buf, blob_size);
    write_bytes(blobs_buf, media->blob.data(), blob_size);

    media_count++;
  }
  media.write(blobs_buf.data(), static_cast<std::streamsize>(blobs_buf.size()));
  return media_count;
}
}

ImportResult dictionary_importer::import(const std::string& zip_path, const std::string& output_dir, bool low_ram) {
  ImportResult result;
  zip_t* archive = nullptr;
  try {
    archive = zip_open(zip_path.c_str(), 0, 'r');
    if (!archive) {
      throw std::runtime_error("failed to open zip");
    }

    std::string index_content = read_file_by_name(archive, "index.json");
    if (index_content.empty()) {
      throw std::runtime_error("could not find or read index.json");
    }

    Index index;
    if (!yomitan_parser::parse_index(index_content, index)) {
      throw std::runtime_error("failed to parse index.json");
    }

    result.title = index.title;

    std::filesystem::path dict_path = std::filesystem::path(output_dir) / result.title;
    std::string path = dict_path.string();
    std::filesystem::create_directories(dict_path);

    if (glz::write_file_json(index, path + "/index.json", std::string{})) {
      throw std::runtime_error("failed to write index.json");
    }

    std::string styles = read_file_by_name(archive, "styles.css");
    if (!styles.empty()) {
      std::ofstream styles_file(path + "/styles.css", std::ios::binary);
      setup_stream_exceptions(styles_file);
      styles_file.write(styles.data(), static_cast<std::streamsize>(styles.size()));
    }

    const Files files = get_files(archive);
    std::future<size_t> media_thread = std::async(std::launch::async, [&path, archive, &files = files.media_files]() {
      return write_media(path, archive, files);
    });

    std::ofstream blobs(path + "/blobs.bin", std::ios::binary);
    setup_stream_exceptions(blobs);
    std::vector<std::pair<uint64_t, uint64_t>> offsets;
    uint64_t write_offset = 0;
    write_terms(blobs, offsets, zip_path, files.term_banks, write_offset, result, low_ram);
    write_meta(blobs, offsets, zip_path, files.meta_banks, write_offset, result, low_ram);
    if (offsets.empty()) {
      throw std::runtime_error("empty dictionary");
    }

    std::vector<std::pair<uint64_t, uint64_t>> hash_entries;
    write_offset_index(blobs, offsets, write_offset, hash_entries);
    std::vector<std::pair<uint64_t, uint64_t>>().swap(offsets);

    hash::linear table;
    table.build(hash_entries);
    table.save(path + "/hash.table");
    table.free();

    result.media_count = media_thread.get();

    std::ofstream sui(path + "/.hoshidicts_1", std::ios::binary);
    result.success = true;
  } catch (const std::exception& e) {
    result.success = false;
    result.errors.emplace_back(e.what());
  }

  if (archive) {
    zip_close(archive);
  }

  if (!result.success && !result.title.empty()) {
    std::filesystem::remove_all(std::filesystem::path(output_dir) / result.title);
  }

  return result;
}
