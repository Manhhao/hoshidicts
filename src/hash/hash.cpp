#include "hash.hpp"

#include <xxh3.h>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <stdexcept>

namespace hash {
linear::linear() : ptr_(std::make_unique<table>()) {};
linear::~linear() = default;

uint64_t linear::operator()(std::string_view key) const {
  uint64_t h = XXH3_64bits(key.data(), key.size());
  uint64_t pos = h % ptr_->capacity;
  while (true) {
    if (ptr_->table[pos].hash == 0) {
      return 0;
    }
    if (ptr_->table[pos].hash == h) {
      return ptr_->table[pos].offset;
    }
    pos = (pos + 1) % ptr_->capacity;
  }
}

void linear::build(const std::vector<std::pair<uint64_t, uint64_t>>& hash_entries) {
  ptr_->capacity = std::max<uint64_t>(hash_entries.size() * 10 / 7, 16);
  ptr_->table = static_cast<slot*>(std::malloc(ptr_->capacity * sizeof(slot)));
  std::memset(ptr_->table, 0, ptr_->capacity * sizeof(slot));

  for (const auto& he : hash_entries) {
    uint64_t h = he.first;
    uint64_t pos = h % ptr_->capacity;
    while (true) {
      if (ptr_->table[pos].hash == 0) {
        ptr_->table[pos] = {.hash = h, .offset = he.second};
        break;
      }
      pos = (pos + 1) % ptr_->capacity;
    }
  }
}

void linear::free() {
  std::free(static_cast<void*>(ptr_->table));
  ptr_->capacity = 0;
  ptr_->table = nullptr;
}

void linear::save(const std::string& path) {
  std::ofstream out(path, std::ios::binary);
  if (!out) {
    throw std::runtime_error("failed to save hash");
  }
  out.write(reinterpret_cast<const char*>(&ptr_->capacity), sizeof(uint32_t));
  out.write(reinterpret_cast<const char*>(ptr_->table), static_cast<std::streamsize>(ptr_->capacity * sizeof(slot)));
}

void linear::load(void* ptr) {
  auto* base = static_cast<std::uint8_t*>(ptr);
  ptr_->capacity = *reinterpret_cast<uint32_t*>(base);
  ptr_->table = reinterpret_cast<slot*>(base + sizeof(uint32_t));
}
}