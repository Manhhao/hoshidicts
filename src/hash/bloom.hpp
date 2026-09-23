#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <vector>

namespace hash {
class bloom {
 public:
  static void build_to_file(const std::vector<uint64_t>& hashes, const std::filesystem::path& path);
  bool load(const uint8_t* ptr, size_t size);

  bool contains(uint64_t h) const {
    auto h1 = static_cast<uint32_t>(h);
    auto h2 = static_cast<uint32_t>(h >> 32);
    for (uint64_t k = 0; k < num_hashes_; k++) {
      uint64_t bit = (h1 + k * h2) & mask_;
      if ((bits_[bit >> 3] & (1U << (bit & 7))) == 0) {
        return false;
      }
    }
    return true;
  }

 private:
  uint64_t mask_ = 0;
  uint64_t num_hashes_ = 0;
  const uint8_t* bits_ = nullptr;
};
}
