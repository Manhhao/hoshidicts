#include "hash.hpp"

#include <xxh3.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <vector>

#include "../memory/memory.hpp"

namespace hash {
namespace {
uint64_t slot_hash(const uint8_t* slot) {
  uint64_t h;
  std::memcpy(&h, slot, sizeof(h));
  return h;
}

uint64_t slot_offset(const uint8_t* slot) {
  uint64_t offset;
  std::memcpy(&offset, slot + sizeof(uint64_t), sizeof(offset));
  return offset;
}

void write_slot(uint8_t* slot, uint64_t hash, uint64_t offset) {
  std::memcpy(slot, &hash, sizeof(hash));
  std::memcpy(slot + sizeof(uint64_t), &offset, sizeof(offset));
}
}

uint64_t linear::operator()(std::string_view key) const {
  uint64_t h = XXH3_64bits(key.data(), key.size());
  if (!bloom_->contains(h)) {
    return 0;
  }
  uint64_t pos = h % capacity_;
  for (uint32_t probes = 0; probes < capacity_; probes++) {
    const uint8_t* slot = slots_ + pos * SLOT_SIZE;
    const uint64_t stored = slot_hash(slot);
    if (stored == 0) {
      return 0;
    }
    if (stored == h) {
      return slot_offset(slot);
    }
    pos = (pos + 1) % capacity_;
  }
  return 0;
}

void linear::build_to_file(const std::vector<std::pair<uint64_t, uint64_t>>& hash_entries,
                           const std::filesystem::path& path) {
  const uint32_t capacity = static_cast<uint32_t>(std::max<uint64_t>(hash_entries.size() * 10 / 7, 16));
  const size_t file_size = sizeof(uint32_t) + static_cast<size_t>(capacity) * SLOT_SIZE;

  auto out = memory::map_rw(path, file_size);
  if (!out) {
    throw std::runtime_error("failed to create hash table");
  }

  std::memcpy(out.data, &capacity, sizeof(capacity));
  uint8_t* slots = out.data + sizeof(uint32_t);
  std::memset(slots, 0, static_cast<size_t>(capacity) * SLOT_SIZE);
  for (const auto& [h, offset] : hash_entries) {
    uint64_t pos = h % capacity;
    while (slot_hash(slots + pos * SLOT_SIZE) != 0) {
      pos = (pos + 1) % capacity;
    }
    write_slot(slots + pos * SLOT_SIZE, h, offset);
  }
  memory::unmap(out);
}

bool linear::load(const uint8_t* ptr, size_t size) {
  if (size < sizeof(uint32_t)) {
    return false;
  }
  uint32_t capacity;
  std::memcpy(&capacity, ptr, sizeof(capacity));
  if (capacity == 0) {
    return false;
  }
  const size_t slots_bytes = size - sizeof(uint32_t);
  if (slots_bytes / SLOT_SIZE != capacity || slots_bytes % SLOT_SIZE != 0) {
    return false;
  }
  capacity_ = capacity;
  slots_ = ptr + sizeof(uint32_t);
  return true;
}
}
