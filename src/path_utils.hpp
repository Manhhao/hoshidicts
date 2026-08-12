#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace path_utils {
inline std::filesystem::path from_utf8(std::string_view value) {
  return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(value.data()), value.size()));
}

inline std::string to_utf8(const std::filesystem::path& value) {
  const std::u8string encoded = value.u8string();
  return {reinterpret_cast<const char*>(encoded.data()), encoded.size()};
}
}
