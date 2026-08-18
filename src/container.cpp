#include "hoshidicts/container.hpp"

#include <xxh3.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <glaze/glaze.hpp>
#include <memory>
#include <string>
#include <vector>

#include "hoshidicts/importer.hpp"
#include "path_utils.hpp"

namespace dictionary_container {
namespace {
constexpr char MAGIC[8] = {'H', 'O', 'S', 'H', 'I', 'D', 'C', 'T'};
constexpr uint32_t CONTAINER_VERSION = 1;
constexpr uint32_t HEADER_SIZE = 64;
constexpr uint32_t DESCRIPTOR_SIZE = 32;
constexpr uint32_t MAX_SECTIONS = 32;
constexpr uint64_t ALIGNMENT = 64;
constexpr uint32_t LITTLE_ENDIAN_FLAG = 1U << 0;
constexpr uint32_t REQUIRED_FLAG = 1U << 0;
constexpr size_t CHUNK_SIZE = 1U << 20;

template <typename T>
T read_val(const uint8_t*& addr) {
  T val;
  std::memcpy(&val, addr, sizeof(T));
  addr += sizeof(T);
  return val;
}

bool known_type(uint32_t type) { return type >= INDEX && type <= MEDIA_INDEX; }

OpenResult open_failed(std::string error) { return {.error = std::move(error)}; }

PackResult pack_failed(std::string error) { return {.error = std::move(error)}; }

VerifyResult verify_failed(std::string error) { return {.error = std::move(error)}; }

IndexResult index_failed(std::string error) { return {.error = std::move(error)}; }

constexpr uint64_t align_up(uint64_t value) { return (value + ALIGNMENT - 1) / ALIGNMENT * ALIGNMENT; }

// hashes length bytes of in, optionally copying them to out
bool digest_section(std::istream& in, std::ostream* out, std::vector<char>& chunk, uint64_t length,
                    uint64_t& checksum) {
  const std::unique_ptr<XXH3_state_t, decltype(&XXH3_freeState)> state(XXH3_createState(), XXH3_freeState);
  if (!state || XXH3_64bits_reset(state.get()) == XXH_ERROR) {
    return false;
  }

  uint64_t left = length;
  while (left > 0) {
    const auto step = static_cast<std::streamsize>(std::min<uint64_t>(left, chunk.size()));
    if (!in.read(chunk.data(), step)) {
      return false;
    }
    if (out != nullptr && !out->write(chunk.data(), step)) {
      return false;
    }
    XXH3_64bits_update(state.get(), chunk.data(), static_cast<size_t>(step));
    left -= static_cast<uint64_t>(step);
  }

  checksum = XXH3_64bits_digest(state.get());
  return true;
}

bool write_zeros(std::ostream& out, uint64_t count) {
  static constexpr std::array<char, ALIGNMENT> zeros{};
  while (count > 0) {
    const auto step = static_cast<std::streamsize>(std::min<uint64_t>(count, zeros.size()));
    if (!out.write(zeros.data(), step)) {
      return false;
    }
    count -= static_cast<uint64_t>(step);
  }
  return true;
}

template <typename T>
void write_val(uint8_t*& addr, T val) {
  std::memcpy(addr, &val, sizeof(T));
  addr += sizeof(T);
}

struct PlannedSection {
  const char* name;
  Section section;
};

std::vector<uint8_t> encode_header(uint32_t payload_version, uint64_t total_size,
                                   const std::vector<PlannedSection>& planned) {
  std::vector<uint8_t> encoded(HEADER_SIZE + planned.size() * DESCRIPTOR_SIZE, 0);
  uint8_t* addr = encoded.data();

  std::memcpy(addr, MAGIC, sizeof(MAGIC));
  addr += sizeof(MAGIC);
  write_val<uint32_t>(addr, CONTAINER_VERSION);
  write_val<uint32_t>(addr, HEADER_SIZE);
  write_val<uint32_t>(addr, payload_version);
  write_val<uint32_t>(addr, LITTLE_ENDIAN_FLAG);
  write_val<uint32_t>(addr, static_cast<uint32_t>(planned.size()));
  write_val<uint32_t>(addr, DESCRIPTOR_SIZE);
  write_val<uint64_t>(addr, HEADER_SIZE);
  write_val<uint64_t>(addr, total_size);
  write_val<uint64_t>(addr, 0);
  write_val<uint64_t>(addr, 0);

  for (const auto& [name, section] : planned) {
    write_val<uint32_t>(addr, section.type);
    write_val<uint32_t>(addr, section.flags);
    write_val<uint64_t>(addr, section.offset);
    write_val<uint64_t>(addr, section.length);
    write_val<uint64_t>(addr, section.checksum);
  }

  return encoded;
}

// returns an empty string on success, leaves the output file closed either way
std::string write_container(const std::filesystem::path& dir, const std::filesystem::path& output,
                            uint32_t payload_version, std::vector<PlannedSection>& planned, uint64_t total_size) {
  std::ofstream out(output, std::ios::binary | std::ios::trunc);
  if (!out) {
    return std::format("cannot create {}", path_utils::to_utf8(output));
  }

  std::vector<char> chunk(CHUNK_SIZE);
  uint64_t written = 0;
  for (auto& [name, section] : planned) {
    if (!write_zeros(out, section.offset - written)) {
      return "cannot pad the container";
    }
    std::ifstream in(dir / name, std::ios::binary);
    if (!in || !digest_section(in, &out, chunk, section.length, section.checksum)) {
      return std::format("cannot copy {} into the container", name);
    }
    written = section.offset + section.length;
  }

  const std::vector<uint8_t> header = encode_header(payload_version, total_size, planned);
  out.seekp(0);
  if (!out.write(reinterpret_cast<const char*>(header.data()), static_cast<std::streamsize>(header.size()))) {
    return "cannot write the container header";
  }
  out.close();
  return out ? "" : "cannot finish writing the container";
}
}

const Section* Container::find(uint32_t type) const {
  const auto it = std::ranges::find(sections, type, &Section::type);
  return it == sections.end() ? nullptr : &*it;
}

OpenResult open(const std::string& path_utf8) {
  const std::filesystem::path path = path_utils::from_utf8(path_utf8);

  std::error_code ec;
  const uintmax_t actual_size = std::filesystem::file_size(path, ec);
  if (ec) {
    return open_failed(std::format("cannot read {}", path_utf8));
  }

  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return open_failed(std::format("cannot open {}", path_utf8));
  }

  std::array<uint8_t, HEADER_SIZE> header{};
  if (!in.read(reinterpret_cast<char*>(header.data()), header.size())) {
    return open_failed("truncated container header");
  }

  const uint8_t* addr = header.data();
  if (std::memcmp(addr, MAGIC, sizeof(MAGIC)) != 0) {
    return open_failed(std::format("{} is not a .hoshi container", path_utf8));
  }
  addr += sizeof(MAGIC);

  const auto container_version = read_val<uint32_t>(addr);
  const auto header_size = read_val<uint32_t>(addr);
  const auto payload_version = read_val<uint32_t>(addr);
  const auto flags = read_val<uint32_t>(addr);
  const auto section_count = read_val<uint32_t>(addr);
  const auto descriptor_size = read_val<uint32_t>(addr);
  const auto table_offset = read_val<uint64_t>(addr);
  const auto declared_size = read_val<uint64_t>(addr);

  if (container_version != CONTAINER_VERSION) {
    return open_failed(std::format("unsupported container version {}", container_version));
  }
  if (header_size != HEADER_SIZE || descriptor_size != DESCRIPTOR_SIZE || table_offset != HEADER_SIZE) {
    return open_failed("unexpected container header layout");
  }
  if ((flags & LITTLE_ENDIAN_FLAG) == 0) {
    return open_failed("container is not little-endian");
  }
  if (payload_version < 3 || payload_version > 4) {
    return open_failed(std::format("unsupported payload version {}", payload_version));
  }
  if (section_count == 0 || section_count > MAX_SECTIONS) {
    return open_failed(std::format("section count {} out of range", section_count));
  }
  if (declared_size != actual_size) {
    return open_failed(std::format("container declares {} bytes but is {}", declared_size, actual_size));
  }

  std::vector<uint8_t> table(static_cast<size_t>(section_count) * DESCRIPTOR_SIZE);
  if (!in.read(reinterpret_cast<char*>(table.data()), static_cast<std::streamsize>(table.size()))) {
    return open_failed("truncated container section table");
  }
  const uint64_t payload_start = HEADER_SIZE + table.size();

  Container container;
  container.payload_version = payload_version;
  container.size = declared_size;

  addr = table.data();
  for (uint32_t i = 0; i < section_count; i++) {
    Section section;
    section.type = read_val<uint32_t>(addr);
    section.flags = read_val<uint32_t>(addr);
    section.offset = read_val<uint64_t>(addr);
    section.length = read_val<uint64_t>(addr);
    section.checksum = read_val<uint64_t>(addr);

    if (!known_type(section.type) && (section.flags & REQUIRED_FLAG) != 0) {
      return open_failed(std::format("unknown required section type {}", section.type));
    }
    if (container.find(section.type) != nullptr) {
      return open_failed(std::format("duplicate section type {}", section.type));
    }
    if (section.offset % ALIGNMENT != 0) {
      return open_failed(std::format("section {} is not 64-byte aligned", section.type));
    }
    if (section.offset < payload_start) {
      return open_failed(std::format("section {} overlaps the container header", section.type));
    }
    if (section.length == 0) {
      return open_failed(std::format("section {} is empty", section.type));
    }
    if (section.offset > declared_size || section.length > declared_size - section.offset) {
      return open_failed(std::format("section {} runs past the end of the container", section.type));
    }
    container.sections.push_back(section);
  }

  auto ordered = container.sections;
  std::ranges::sort(ordered, {}, &Section::offset);
  for (size_t i = 1; i < ordered.size(); i++) {
    if (ordered[i].offset < ordered[i - 1].offset + ordered[i - 1].length) {
      return open_failed(std::format("sections {} and {} overlap", ordered[i - 1].type, ordered[i].type));
    }
  }

  for (const uint32_t type : {INDEX, BLOBS, HASH_TABLE, BLOOM_FILTER}) {
    if (container.find(type) == nullptr) {
      return open_failed(std::format("missing required section type {}", type));
    }
  }
  if ((container.find(ZSTD_DICT) != nullptr) != (payload_version >= 4)) {
    return open_failed("the zstd dictionary section does not match the payload version");
  }
  if ((container.find(MEDIA) != nullptr) != (container.find(MEDIA_INDEX) != nullptr)) {
    return open_failed("media and media index must either both be present or both be absent");
  }

  return {.ok = true, .container = std::move(container)};
}

PackResult pack(const std::string& dictionary_dir_utf8, const std::string& output_path_utf8) {
  const std::filesystem::path dir = path_utils::from_utf8(dictionary_dir_utf8);

  uint32_t payload_version = 0;
  if (std::filesystem::is_regular_file(dir / ".hoshidicts_4")) {
    payload_version = 4;
  } else if (std::filesystem::is_regular_file(dir / ".hoshidicts_3")) {
    payload_version = 3;
  } else {
    return pack_failed(std::format("{} is not an imported dictionary of payload version 3 or 4", dictionary_dir_utf8));
  }

  std::ifstream index(dir / "index.json", std::ios::binary);
  if (!index) {
    return pack_failed("cannot read index.json");
  }
  const std::string buf(std::istreambuf_iterator<char>(index), {});
  Summary summary;
  if (glz::read<glz::opts{.error_on_unknown_keys = false}>(summary, buf)) {
    return pack_failed("index.json is not a dictionary summary");
  }

  std::vector<PlannedSection> planned = {
      {"index.json", {.type = INDEX}},
      {"blobs.bin", {.type = BLOBS}},
      {"hash.table", {.type = HASH_TABLE}},
      {"bloom.filter", {.type = BLOOM_FILTER}},
  };
  if (payload_version >= 4) {
    planned.push_back({"dict.zstd", {.type = ZSTD_DICT}});
  }
  if (std::filesystem::exists(dir / "media.bin")) {
    planned.push_back({"media.bin", {.type = MEDIA}});
    planned.push_back({"media.idx", {.type = MEDIA_INDEX}});
  }

  uint64_t offset = align_up(HEADER_SIZE + planned.size() * DESCRIPTOR_SIZE);
  for (auto& [name, section] : planned) {
    std::error_code ec;
    const uintmax_t size = std::filesystem::file_size(dir / name, ec);
    if (ec || size == 0) {
      return pack_failed(std::format("{} is missing or empty", name));
    }
    section.flags = REQUIRED_FLAG;
    section.offset = offset;
    section.length = size;
    offset = align_up(offset + size);
  }
  const uint64_t total_size = planned.back().section.offset + planned.back().section.length;

  const std::string tmp_utf8 = output_path_utf8 + ".tmp";
  const std::filesystem::path tmp = path_utils::from_utf8(tmp_utf8);

  std::string error = write_container(dir, tmp, payload_version, planned, total_size);
  if (error.empty()) {
    const VerifyResult verified = verify(tmp_utf8);
    if (!verified.ok) {
      error = std::format("the packed container failed verification: {}", verified.error);
    }
  }

  std::error_code ec;
  if (!error.empty()) {
    std::filesystem::remove(tmp, ec);
    return pack_failed(std::move(error));
  }

  const std::filesystem::path output = path_utils::from_utf8(output_path_utf8);
  std::filesystem::rename(tmp, output, ec);
  if (ec) {
    std::filesystem::remove(output, ec);
    std::filesystem::rename(tmp, output, ec);
  }
  if (ec) {
    std::filesystem::remove(tmp, ec);
    return pack_failed(std::format("cannot move the container to {}", output_path_utf8));
  }

  return {.ok = true, .bytes = total_size};
}

VerifyResult verify(const std::string& path_utf8) {
  OpenResult opened = open(path_utf8);
  if (!opened.ok) {
    return verify_failed(std::move(opened.error));
  }

  std::ifstream in(path_utils::from_utf8(path_utf8), std::ios::binary);
  if (!in) {
    return verify_failed(std::format("cannot open {}", path_utf8));
  }

  std::vector<char> chunk(CHUNK_SIZE);
  for (const Section& section : opened.container.sections) {
    in.seekg(static_cast<std::streamoff>(section.offset));
    uint64_t checksum = 0;
    if (!digest_section(in, nullptr, chunk, section.length, checksum)) {
      return verify_failed(std::format("cannot read section {}", section.type));
    }
    if (checksum != section.checksum) {
      return verify_failed(std::format("section {} does not match its checksum", section.type));
    }
  }

  return {.ok = true, .container = std::move(opened.container)};
}

IndexResult read_index(const std::string& path_utf8) {
  const OpenResult opened = open(path_utf8);
  if (!opened.ok) {
    return index_failed(opened.error);
  }

  const Section* section = opened.container.find(INDEX);
  if (section == nullptr) {
    return index_failed("the container has no index section");
  }

  std::ifstream in(path_utils::from_utf8(path_utf8), std::ios::binary);
  if (!in) {
    return index_failed(std::format("cannot open {}", path_utf8));
  }

  std::string json(section->length, '\0');
  in.seekg(static_cast<std::streamoff>(section->offset));
  if (!in.read(json.data(), static_cast<std::streamsize>(json.size()))) {
    return index_failed("cannot read the index section");
  }

  return {.ok = true, .json = std::move(json)};
}
}
