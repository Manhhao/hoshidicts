#include "hoshidicts/language.hpp"

#include <stdexcept>

#include "en/en.hpp"
#include "ja/ja.hpp"

namespace language {

const LanguageProcessor& get(std::string_view id) {
  if (id == "ja") {
    return ja::get();
  }
  if (id == "en") {
    return en::get();
  }
  throw std::invalid_argument("unsupported language: " + std::string(id));
}

}
