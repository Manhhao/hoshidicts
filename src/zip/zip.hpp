#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

struct ZipEntry {
  std::string name;
  uint16_t compression_method;
  uint32_t compressed_size;
  uint32_t uncompressed_size;
  size_t data_offset;
};

struct Zip {
  void* data = nullptr;
  size_t size = 0;
  std::vector<ZipEntry> entries;

  ~Zip();
  bool load(const std::string& path);
  int find(const std::string& name) const;
  std::string read(int index) const;

  struct MediaResult {
    std::string path;
    std::vector<char> blob;
  };

  std::optional<MediaResult> read_media(int index) const;

 private:
  bool parse_central_directory();
};
