#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>

namespace memory {
struct mapped_file {
  uint8_t* data = nullptr;
  size_t size = 0;

  explicit operator bool() const { return data != nullptr; }
};

mapped_file map_rd(const std::filesystem::path& path);
mapped_file map_rw(const std::filesystem::path& path, size_t file_size);
void unmap(mapped_file mapping);
}
