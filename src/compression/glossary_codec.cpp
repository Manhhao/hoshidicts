#include "glossary_codec.hpp"

#define ZDICT_STATIC_LINKING_ONLY
#include <zdict.h>

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <thread>

namespace glossary_codec {
namespace {
void check(size_t result, const char* operation) {
  if (ZSTD_isError(result)) {
    throw std::runtime_error(std::string(operation) + ": " + ZSTD_getErrorName(result));
  }
}
}

void DDictDeleter::operator()(ZSTD_DDict* dictionary) const { ZSTD_freeDDict(dictionary); }

std::vector<char> train_dictionary(const std::vector<std::string>& samples, size_t capacity) {
  if (samples.size() < 8 || capacity == 0) {
    return {};
  }
  std::vector<size_t> sizes;
  std::string data;
  sizes.reserve(samples.size());
  size_t total_size = 0;
  for (const auto& sample : samples) {
    total_size += sample.size();
  }
  data.reserve(total_size);
  for (const auto& sample : samples) {
    data.append(sample);
    sizes.push_back(sample.size());
  }

  std::vector<char> dictionary(capacity);
  ZDICT_fastCover_params_t params{};
  params.k = 0;
  params.d = 8;
  params.f = 20;
  params.steps = 4;
  params.nbThreads = std::max(1U, std::thread::hardware_concurrency());
  params.splitPoint = 1.0;
  params.accel = 5;
  const size_t dictionary_size = ZDICT_optimizeTrainFromBuffer_fastCover(
      dictionary.data(), dictionary.size(), data.data(), sizes.data(), static_cast<unsigned>(sizes.size()), &params);
  if (ZDICT_isError(dictionary_size)) {
    return {};
  }
  dictionary.resize(dictionary_size);
  return dictionary;
}

Decoder make_decoder(const std::vector<char>& dictionary) {
  if (dictionary.empty()) {
    return {};
  }
  return Decoder(ZSTD_createDDict(dictionary.data(), dictionary.size()));
}

Compressor::Compressor() : context_(ZSTD_createCCtx()) {
  if (!context_) {
    throw std::runtime_error("failed to create zstd compression context");
  }
}

Compressor::Compressor(const std::vector<char>& dictionary) : Compressor() {
  if (dictionary.empty()) {
    return;
  }
  dictionary_ = ZSTD_createCDict(dictionary.data(), dictionary.size(), 3);
  if (!dictionary_) {
    throw std::runtime_error("failed to create zstd compression dictionary");
  }
}

Compressor::~Compressor() {
  ZSTD_freeCDict(dictionary_);
  ZSTD_freeCCtx(context_);
}

std::vector<char> Compressor::compress(std::string_view input) {
  std::vector<char> output(ZSTD_compressBound(input.size()));
  const size_t compressed_size = dictionary_
                                     ? ZSTD_compress_usingCDict(context_, output.data(), output.size(), input.data(),
                                                                input.size(), dictionary_)
                                     : ZSTD_compressCCtx(context_, output.data(), output.size(), input.data(), input.size(), 3);
  check(compressed_size, "failed to compress glossary");
  output.resize(compressed_size);
  return output;
}

std::string decompress(const void* data, size_t size, const ZSTD_DDict* dictionary) {
  if (!data || size == 0) {
    return {};
  }
  const unsigned long long decompressed_size = ZSTD_getFrameContentSize(data, size);
  if (decompressed_size == ZSTD_CONTENTSIZE_ERROR || decompressed_size == ZSTD_CONTENTSIZE_UNKNOWN) {
    return {};
  }
  std::string output(static_cast<size_t>(decompressed_size), '\0');
  thread_local std::unique_ptr<ZSTD_DCtx, decltype(&ZSTD_freeDCtx)> context(ZSTD_createDCtx(), ZSTD_freeDCtx);
  if (!context) {
    return {};
  }
  const size_t actual_size = dictionary
                                 ? ZSTD_decompress_usingDDict(context.get(), output.data(), output.size(), data, size,
                                                              dictionary)
                                 : ZSTD_decompressDCtx(context.get(), output.data(), output.size(), data, size);
  if (ZSTD_isError(actual_size)) {
    return {};
  }
  output.resize(actual_size);
  return output;
}
}
