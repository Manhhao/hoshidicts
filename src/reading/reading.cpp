#include "reading.hpp"

namespace reading {
// Compared exactly, as Yomitan does. A kana only entry needs no fallback to the
// expression here because the importer already stored the expression as its
// reading.
bool matches_primary(const TermResult& term, std::string_view primary_reading) {
  return term.reading == primary_reading;
}
}
