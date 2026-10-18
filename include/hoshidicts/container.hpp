#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace dictionary_container {
enum SectionType : uint32_t {
  INDEX = 1,
  BLOBS = 2,
  HASH_TABLE = 3,
  BLOOM_FILTER = 4,
  ZSTD_DICT = 5,
  MEDIA = 6,
  MEDIA_INDEX = 7,
};

struct Section {
  uint32_t type = 0;
  uint32_t flags = 0;
  uint64_t offset = 0;
  uint64_t length = 0;
  uint64_t checksum = 0;
};

struct Container {
  uint32_t payload_version = 0;
  uint64_t size = 0;
  std::vector<Section> sections;

  const Section* find(uint32_t type) const;
};

struct OpenResult {
  bool ok = false;
  Container container;
  std::string error;
};

struct PackResult {
  bool ok = false;
  uint64_t bytes = 0;
  std::string error;
};

struct VerifyResult {
  bool ok = false;
  Container container;
  std::string error;
};

struct IndexResult {
  bool ok = false;
  std::string json;
  std::string error;
};

OpenResult open(const std::string& path_utf8);

PackResult pack(const std::string& dictionary_dir_utf8, const std::string& output_path_utf8);

VerifyResult verify(const std::string& path_utf8);

IndexResult read_index(const std::string& path_utf8);
}
