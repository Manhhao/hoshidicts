#include "bloom.hpp"

#include <algorithm>
#include <bit>
#include <cstdint>
#include <cstring>
#include <stdexcept>

#include "../memory/memory.hpp"

namespace hash {
namespace {
constexpr uint64_t num_hashes = 7;
}

bool bloom::load(const uint8_t* ptr, size_t size) {
  if (size < 2 * sizeof(uint64_t)) {
    return false;
  }
  uint64_t num_bits;
  std::memcpy(&num_bits, ptr, sizeof(num_bits));
  if (num_bits == 0 || !std::has_single_bit(num_bits) || num_bits % 8 != 0) {
    return false;
  }
  if (size - 2 * sizeof(uint64_t) != num_bits / 8) {
    return false;
  }
  std::memcpy(&num_hashes_, ptr + sizeof(uint64_t), sizeof(num_hashes_));
  mask_ = num_bits - 1;
  bits_ = ptr + 2 * sizeof(uint64_t);
  return true;
}

void bloom::build_to_file(const std::vector<uint64_t>& hashes, const std::filesystem::path& path) {
  uint64_t num_bits = std::bit_ceil(std::max<uint64_t>(hashes.size() * 10, 64));
  uint64_t mask = num_bits - 1;

  size_t bits_size = num_bits / 8;
  auto out = memory::map_rw(path, 2 * sizeof(uint64_t) + bits_size);
  if (!out) {
    throw std::runtime_error("failed to create bloom filter");
  }

  std::memcpy(out.data, &num_bits, sizeof(uint64_t));
  std::memcpy(out.data + sizeof(uint64_t), &num_hashes, sizeof(uint64_t));
  uint8_t* bits = out.data + 2 * sizeof(uint64_t);
  std::memset(bits, 0, bits_size);

  for (uint64_t h : hashes) {
    auto h1 = static_cast<uint32_t>(h);
    auto h2 = static_cast<uint32_t>(h >> 32);
    for (uint64_t k = 0; k < num_hashes; k++) {
      uint64_t bit = (h1 + k * h2) & mask;
      bits[bit >> 3] |= 1U << (bit & 7);
    }
  }

  memory::unmap(out);
}
}
