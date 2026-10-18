#pragma once
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string_view>
#include <vector>

#include "bloom.hpp"

namespace hash {
class linear {
 public:
  uint64_t operator()(std::string_view key) const;

  void build_to_file(const std::vector<std::pair<uint64_t, uint64_t>>& hash_entries, const std::filesystem::path& path);
  bool load(const uint8_t* ptr, size_t size);
  void set_bloom(const bloom* b) { bloom_ = b; }

 private:
  static constexpr size_t SLOT_SIZE = 2 * sizeof(uint64_t);

  const uint8_t* slots_ = nullptr;
  uint32_t capacity_ = 0;
  const bloom* bloom_ = nullptr;
};
}
