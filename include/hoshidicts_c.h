#ifndef HOSHIDICTS_C_H
#define HOSHIDICTS_C_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct hd_str {
  const char* ptr;
  size_t len;
} hd_str;

// importer
typedef struct hd_import_result hd_import_result;

hd_import_result* hd_import(const char* zip_path, const char* output_dir, int low_ram);
void hd_import_result_free(hd_import_result* r);

int hd_import_result_success(const hd_import_result* r);
const char* hd_import_result_title(const hd_import_result* r);
uint64_t hd_import_result_term_count(const hd_import_result* r);
uint64_t hd_import_result_meta_count(const hd_import_result* r);
uint64_t hd_import_result_freq_count(const hd_import_result* r);
uint64_t hd_import_result_pitch_count(const hd_import_result* r);
uint64_t hd_import_result_kanji_count(const hd_import_result* r);
uint64_t hd_import_result_media_count(const hd_import_result* r);

const char* hd_import_result_error(const hd_import_result* r);

// deinflector
typedef struct hd_deinflector hd_deinflector;

hd_deinflector* hd_deinflector_new(void);
void hd_deinflector_free(hd_deinflector* d);

// query
typedef struct hd_query hd_query;
typedef struct hd_results hd_results;
typedef struct hd_kanji_results hd_kanji_results;
typedef struct hd_styles hd_styles;

typedef struct hd_frequency {
  int32_t value;
  hd_str display_value;
} hd_frequency;

typedef struct hd_dictionary_style {
  hd_str dict_name;
  hd_str styles;
} hd_dictionary_style;

typedef struct hd_media_file {
  const uint8_t* data;
  size_t size;
} hd_media_file;

typedef struct hd_glossary_entry {
  hd_str dict_name;
  hd_str glossary;
  hd_str definition_tags;
  hd_str term_tags;
} hd_glossary_entry;

typedef struct hd_frequency_entry {
  hd_str dict_name;
  const hd_frequency* frequencies;
  size_t frequencies_count;
} hd_frequency_entry;

typedef struct hd_pitch {
  int32_t position;
  hd_str pattern;
  const int32_t* nasal;
  size_t nasal_count;
  const int32_t* devoice;
  size_t devoice_count;
} hd_pitch;

typedef struct hd_pitch_entry {
  hd_str dict_name;
  const hd_pitch* pitches;
  size_t pitches_count;
  const hd_str* transcriptions;
  size_t transcriptions_count;
} hd_pitch_entry;

typedef struct hd_term_result {
  hd_str expression;
  hd_str reading;
  hd_str rules;
  int32_t score;
  const hd_glossary_entry* glossaries;
  size_t glossaries_count;
  const hd_frequency_entry* frequencies;
  size_t frequencies_count;
  const hd_pitch_entry* pitches;
  size_t pitches_count;
} hd_term_result;

typedef struct hd_kanji_stat {
  hd_str key;
  hd_str value;
} hd_kanji_stat;

typedef struct hd_kanji_entry {
  hd_str dict_name;
  hd_str onyomi;
  hd_str kunyomi;
  hd_str tags;
  const hd_str* definitions;
  size_t definitions_count;
  const hd_kanji_stat* stats;
  size_t stats_count;
} hd_kanji_entry;

hd_query* hd_query_new(void);
void hd_query_free(hd_query* q);

int hd_query_add_term_dict(hd_query* q, const char* path);
int hd_query_add_freq_dict(hd_query* q, const char* path);
int hd_query_add_pitch_dict(hd_query* q, const char* path);
int hd_query_add_kanji_dict(hd_query* q, const char* path);

hd_results* hd_query_run(const hd_query* q, const char* expression, const hd_term_result** out_terms,
                         size_t* out_count);
void hd_results_free(hd_results* r);

hd_kanji_results* hd_query_run_kanji(const hd_query* q, const char* kanji, const hd_kanji_entry** out_entries,
                                     size_t* out_count);
void hd_kanji_results_free(hd_kanji_results* r);

hd_media_file hd_query_get_media_file(const hd_query* q, const char* dict_name, const char* media_path);

hd_styles* hd_query_get_styles(const hd_query* q, const hd_dictionary_style** out_styles, size_t* out_count);
void hd_styles_free(hd_styles* s);

// lookup
typedef struct hd_lookup hd_lookup;
typedef struct hd_lookup_results hd_lookup_results;

typedef struct hd_transform_group {
  hd_str name;
  hd_str description;
} hd_transform_group;

typedef struct hd_lookup_result {
  hd_str matched;
  hd_str deinflected;
  const hd_transform_group* trace;
  size_t trace_count;
  hd_term_result term;
  int32_t preprocessor_steps;
} hd_lookup_result;

typedef enum hd_lookup_frequency_order {
  HD_LOOKUP_FREQUENCY_ORDER_AUTO = 0,
  HD_LOOKUP_FREQUENCY_ORDER_ASCENDING = 1,
  HD_LOOKUP_FREQUENCY_ORDER_DESCENDING = 2,
  HD_LOOKUP_FREQUENCY_ORDER_DISABLED = 3,
} hd_lookup_frequency_order;

// A null hd_str leaves the corresponding preference unset.
typedef struct hd_lookup_options {
  hd_str frequency_dictionary;
  int32_t frequency_order;
} hd_lookup_options;

hd_lookup* hd_lookup_new(hd_query* q, hd_deinflector* d);
void hd_lookup_free(hd_lookup* l);

hd_lookup_results* hd_lookup_run(const hd_lookup* l, const char* lookup_string, int max_results, size_t scan_length,
                                 const hd_lookup_result** out_results, size_t* out_count);
// As hd_lookup_run, but applies the caller's sort preferences before the
// max_results cap, so the cap keeps the results the caller would have ranked
// highest. Passing a null options pointer is equivalent to hd_lookup_run.
hd_lookup_results* hd_lookup_run_with_options(const hd_lookup* l, const char* lookup_string, int max_results,
                                              size_t scan_length, const hd_lookup_options* options,
                                              const hd_lookup_result** out_results, size_t* out_count);
void hd_lookup_results_free(hd_lookup_results* r);

#ifdef __cplusplus
}
#endif

#endif
