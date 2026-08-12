#pragma once

#include <zstd.h>

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace glossary_codec {
struct DDictDeleter {
  void operator()(ZSTD_DDict* dictionary) const;
};
using Decoder = std::unique_ptr<ZSTD_DDict, DDictDeleter>;

std::vector<char> train_dictionary(const std::vector<std::string>& samples, size_t capacity);
Decoder make_decoder(const std::vector<char>& dictionary);
std::string decompress(const void* data, size_t size, const ZSTD_DDict* dictionary);

class Compressor {
 public:
  Compressor();
  explicit Compressor(const std::vector<char>& dictionary);
  ~Compressor();

  Compressor(const Compressor&) = delete;
  Compressor& operator=(const Compressor&) = delete;

  std::vector<char> compress(std::string_view input);

 private:
  ZSTD_CCtx* context_ = nullptr;
  ZSTD_CDict* dictionary_ = nullptr;
};
}
