#include "phf.hpp"

#include <xxh3.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <ranges>
#include <span>
#include <stdexcept>

#include "../memory/memory.hpp"

namespace hash {
namespace {
constexpr uint64_t partition_size = 4096;
constexpr uint64_t num_buckets = 256;
constexpr uint64_t pilots_size = num_buckets * sizeof(uint16_t);
constexpr uint64_t num_slots = (partition_size - pilots_size) / (2 * sizeof(uint32_t));
constexpr uint64_t mean_keys = 370;
constexpr uint64_t max_keys = 440;
constexpr uint64_t filter_bits_per_key = 10;
constexpr uint64_t header_size = 2 * sizeof(uint64_t);
constexpr uint32_t occupied_bit = 1U << 30;
constexpr uint32_t single_bit = 1U << 31;
constexpr std::array<uint32_t, 8> filter_salts = {0x47b6137bU, 0x44974d91U, 0x8824ad5bU, 0xa2b7289dU,
                                                  0x705495c7U, 0x2df1424bU, 0x9efc4947U, 0x5c6bfb31U};

using block = std::array<uint32_t, 8>;

struct slot {
  uint32_t tag;
  uint32_t offset;
};

uint64_t fastrange(uint64_t x, uint64_t n) { return ((x >> 32) * n) >> 32; }

uint64_t bucket(uint64_t h) { return (h * 0x9e3779b97f4a7c15ULL) >> 56; }

uint64_t position(uint64_t h, uint64_t pilot) {
  uint64_t x = h ^ (pilot * 0x9e3779b97f4a7c15ULL);
  x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
  x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
  return fastrange(x ^ (x >> 31), num_slots);
}

uint32_t tag(uint64_t h) { return (static_cast<uint32_t>(h) & (occupied_bit - 1)) | occupied_bit; }

uint32_t filter_mask(uint64_t h, size_t i) {
  return 1U << ((static_cast<uint32_t>(h ^ (h >> 32)) * filter_salts[i]) >> 27);
}

uint64_t partitions_offset(uint64_t num_blocks) {
  return (header_size + num_blocks * sizeof(block) + partition_size - 1) / partition_size * partition_size;
}

bool build_partition(std::span<const std::pair<uint64_t, phf::location>> keys, uint8_t* out) {
  std::array<uint16_t, num_buckets + 1> starts{};
  for (const auto& [h, loc] : keys) {
    starts[bucket(h) + 1]++;
  }

  std::array<uint16_t, max_keys + 2> size_starts{};
  for (uint16_t b = 0; b < num_buckets; b++) {
    size_starts[max_keys - starts[b + 1] + 1]++;
    starts[b + 1] += starts[b];
  }
  for (size_t i = 0; i <= max_keys; i++) {
    size_starts[i + 1] += size_starts[i];
  }
  std::array<uint16_t, num_buckets> order;
  for (uint16_t b = 0; b < num_buckets; b++) {
    order[size_starts[max_keys - (starts[b + 1] - starts[b])]++] = b;
  }

  std::array<std::pair<uint64_t, phf::location>, max_keys> sorted;
  std::array<uint16_t, num_buckets> fill;
  std::copy(starts.begin(), starts.end() - 1, fill.begin());
  for (const auto& entry : keys) {
    sorted[fill[bucket(entry.first)]++] = entry;
  }

  auto* pilots = reinterpret_cast<uint16_t*>(out);
  auto* slots = reinterpret_cast<slot*>(out + pilots_size);
  std::array<uint64_t, (num_slots + 63) / 64> taken{};
  std::array<uint64_t, max_keys> positions;
  for (uint16_t b : order) {
    size_t begin = starts[b];
    size_t end = starts[b + 1];
    if (begin == end) {
      break;
    }

    auto fits = [&](uint64_t pilot) {
      for (size_t k = begin; k < end; k++) {
        uint64_t p = position(sorted[k].first, pilot);
        auto placed_end = positions.begin() + static_cast<std::ptrdiff_t>(k - begin);
        if (((taken[p >> 6] >> (p & 63)) & 1) || std::find(positions.begin(), placed_end, p) != placed_end) {
          return false;
        }
        positions[k - begin] = p;
      }
      return true;
    };

    uint64_t pilot = 0;
    while (!fits(pilot)) {
      if (++pilot > UINT16_MAX) {
        return false;
      }
    }

    pilots[b] = static_cast<uint16_t>(pilot);
    for (size_t k = begin; k < end; k++) {
      const auto& [h, loc] = sorted[k];
      uint64_t p = positions[k - begin];
      taken[p >> 6] |= 1ULL << (p & 63);
      slots[p] = {.tag = tag(h) | (loc.single ? single_bit : 0), .offset = static_cast<uint32_t>(loc.offset)};
    }
  }
  return true;
}
}

void phf::build_to_file(const std::vector<std::pair<uint64_t, location>>& hash_entries,
                        const std::filesystem::path& path) {
  if (std::ranges::any_of(hash_entries, [](const auto& entry) { return entry.second.offset > UINT32_MAX; })) {
    throw std::runtime_error("dictionary too large");
  }

  uint64_t n = hash_entries.size();
  uint64_t num_blocks = (n * filter_bits_per_key + 255) / 256;
  std::vector<std::pair<uint64_t, location>> keys(n);
  for (uint64_t num_partitions = (n + mean_keys - 1) / mean_keys;; num_partitions += num_partitions / 64 + 1) {
    std::vector<uint32_t> starts(num_partitions + 1);
    for (const auto& [h, loc] : hash_entries) {
      starts[fastrange(h, num_partitions) + 1]++;
    }
    if (std::ranges::max(starts) > max_keys) {
      continue;
    }

    for (uint64_t p = 0; p < num_partitions; p++) {
      starts[p + 1] += starts[p];
    }
    std::vector<uint32_t> fill(starts.begin(), starts.end() - 1);
    for (const auto& entry : hash_entries) {
      keys[fill[fastrange(entry.first, num_partitions)]++] = entry;
    }

    uint64_t offset = partitions_offset(num_blocks);
    auto out = memory::map_rw(path, offset + num_partitions * partition_size);
    if (!out) {
      throw std::runtime_error("failed to create phf");
    }

    std::array<uint64_t, 2> header = {num_partitions, num_blocks};
    std::memcpy(out.data, header.data(), header_size);

    auto* filter = reinterpret_cast<block*>(out.data + header_size);
    for (const auto& [h, loc] : hash_entries) {
      block& blk = filter[fastrange(h, num_blocks)];
      for (size_t i = 0; i < blk.size(); i++) {
        blk[i] |= filter_mask(h, i);
      }
    }

    bool built = true;
    for (uint64_t p = 0; p < num_partitions && built; p++) {
      built = build_partition(std::span(keys).subspan(starts[p], starts[p + 1] - starts[p]),
                              out.data + offset + p * partition_size);
    }

    memory::unmap(out);
    if (built) {
      return;
    }
  }
}

bool phf::migrate(const std::filesystem::path& dir) {
  auto table = memory::map_rd(dir / "hash.table");
  if (!table) {
    return false;
  }
  uint32_t capacity;
  std::memcpy(&capacity, table.data, sizeof(uint32_t));
  if (table.size != sizeof(uint32_t) + static_cast<size_t>(capacity) * 2 * sizeof(uint64_t)) {
    memory::unmap(table);
    return false;
  }

  std::vector<std::pair<uint64_t, uint64_t>> lists;
  for (uint32_t i = 0; i < capacity; i++) {
    std::array<uint64_t, 2> s;
    std::memcpy(s.data(), table.data + sizeof(uint32_t) + i * sizeof(s), sizeof(s));
    if (s[0] != 0) {
      lists.emplace_back(s[0], s[1]);
    }
  }
  memory::unmap(table);

  uint64_t base = std::ranges::min(lists | std::views::values);
  std::ifstream blobs(dir / "blobs.bin", std::ios::binary);
  std::vector<char> buf(std::filesystem::file_size(dir / "blobs.bin") - base);
  blobs.seekg(static_cast<std::streamoff>(base));
  if (!blobs.read(buf.data(), static_cast<std::streamsize>(buf.size()))) {
    return false;
  }

  std::vector<std::pair<uint64_t, location>> hash_entries;
  hash_entries.reserve(lists.size());
  for (const auto& [h, offset] : lists) {
    const char* list = buf.data() + (offset - base);
    uint32_t count;
    std::memcpy(&count, list, sizeof(uint32_t));
    if (count == 1) {
      uint64_t record;
      std::memcpy(&record, list + sizeof(uint32_t), sizeof(uint64_t));
      hash_entries.emplace_back(h, location{.offset = record, .single = true});
    } else {
      hash_entries.emplace_back(h, location{.offset = offset, .single = false});
    }
  }

  build_to_file(hash_entries, dir / "phf.table.tmp");
  std::filesystem::rename(dir / "phf.table.tmp", dir / "phf.table");
  std::filesystem::remove(dir / "hash.table");
  std::filesystem::remove(dir / "bloom.filter");
  return true;
}

bool phf::load(const uint8_t* ptr, size_t size) {
  std::array<uint64_t, 2> header;
  std::memcpy(header.data(), ptr, header_size);
  if (size != partitions_offset(header[1]) + header[0] * partition_size) {
    return false;
  }

  num_partitions_ = header[0];
  filter_.resize(header[1]);
  std::memcpy(filter_.data(), ptr + header_size, header[1] * sizeof(block));
  partitions_ = ptr + partitions_offset(header[1]);
  return true;
}

std::optional<phf::location> phf::operator()(std::string_view key) const {
  uint64_t h = XXH3_64bits(key.data(), key.size());
  const block& blk = filter_[fastrange(h, filter_.size())];
  for (size_t i = 0; i < blk.size(); i++) {
    if ((blk[i] & filter_mask(h, i)) == 0) {
      return std::nullopt;
    }
  }

  const uint8_t* partition = partitions_ + fastrange(h, num_partitions_) * partition_size;
  uint16_t pilot;
  std::memcpy(&pilot, partition + bucket(h) * sizeof(uint16_t), sizeof(uint16_t));
  slot s;
  std::memcpy(&s, partition + pilots_size + position(h, pilot) * sizeof(slot), sizeof(slot));

  if ((s.tag & ~single_bit) != tag(h)) {
    return std::nullopt;
  }
  return location{.offset = s.offset, .single = (s.tag & single_bit) != 0};
}
}
