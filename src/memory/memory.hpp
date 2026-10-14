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

mapped_file map_rd(const std::filesystem::path& path, uint64_t offset = 0, size_t size = 0);
mapped_file map_rw(const std::filesystem::path& path, size_t file_size);
void unmap(mapped_file mapping);

intptr_t open_rd(const std::filesystem::path& path);
void read_at(intptr_t file, uint64_t offset, void* out, size_t size);
void close_rd(intptr_t file);
}
