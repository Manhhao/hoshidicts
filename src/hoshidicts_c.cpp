#include "hoshidicts_c.h"

#include <cstdint>

#include "hoshidicts/deinflector.hpp"
#include "hoshidicts/importer.hpp"

// importer
struct hd_import_result {
  ImportResult result;
};

hd_import_result* hd_import(const char* zip_path, const char* output_dir, int low_ram) {
  try {
    return new hd_import_result{dictionary_importer::import(zip_path, output_dir, low_ram != 0)};
  } catch (...) {
    return nullptr;
  }
}

void hd_import_result_free(hd_import_result* r) { delete r; }

int hd_import_result_success(const hd_import_result* r) { return static_cast<int>(r->result.success); }

const char* hd_import_result_title(const hd_import_result* r) { return r->result.title.c_str(); }

uint64_t hd_import_result_term_count(const hd_import_result* r) { return r->result.term_count; }

uint64_t hd_import_result_meta_count(const hd_import_result* r) { return r->result.meta_count; }

uint64_t hd_import_result_freq_count(const hd_import_result* r) { return r->result.freq_count; }

uint64_t hd_import_result_pitch_count(const hd_import_result* r) { return r->result.pitch_count; }

uint64_t hd_import_result_media_count(const hd_import_result* r) { return r->result.media_count; }

size_t hd_import_result_error_count(const hd_import_result* r) { return r->result.errors.size(); }

const char* hd_import_result_error(const hd_import_result* r, size_t index) {
  return index < r->result.errors.size() ? r->result.errors[index].c_str() : nullptr;
}

// deinflector
struct hd_deinflector {
  Deinflector deinflector;
};

hd_deinflector* hd_deinflector_new(void) {
  try {
    return new hd_deinflector;
  } catch (...) {
    return nullptr;
  }
}

void hd_deinflector_free(hd_deinflector* d) { delete d; }