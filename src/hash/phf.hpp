#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

namespace hash {
class phf {
 public:
  struct location {
    uint64_t offset;
    bool single;
  };

  static void build_to_file(const std::vector<std::pair<uint64_t, location>>& hash_entries,
                            const std::filesystem::path& path);
  static bool migrate(const std::filesystem::path& dir);
  bool load(const uint8_t* ptr, size_t size);
  std::optional<location> operator()(std::string_view key) const;

 private:
  std::vector<std::array<uint32_t, 8>> filter_;
  uint64_t num_partitions_ = 0;
  const uint8_t* partitions_ = nullptr;
};
}
