#pragma once

#include <string_view>

#include "hoshidicts/query.hpp"

namespace reading {
// Yomitan's primary_reading search parameter, which its internal links carry so
// that the reading the user followed ranks first.
bool matches_primary(const TermResult& term, std::string_view primary_reading);
}
