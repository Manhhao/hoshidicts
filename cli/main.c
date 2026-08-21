#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#endif

#include "hoshidicts_c.h"

static double monotonic_milliseconds(void) {
#ifdef _WIN32
  LARGE_INTEGER frequency;
  LARGE_INTEGER counter;
  if (!QueryPerformanceFrequency(&frequency) || !QueryPerformanceCounter(&counter)) {
    return 0.0;
  }
  return (double)counter.QuadPart * 1000.0 / (double)frequency.QuadPart;
#else
  struct timespec value;
  if (clock_gettime(CLOCK_MONOTONIC, &value) != 0) {
    return 0.0;
  }
  return (double)value.tv_sec * 1000.0 + (double)value.tv_nsec / 1e6;
#endif
}

static void print_usage(const char* program) {
  printf("Usage:\n");
  printf("%s import <path/to/dictionary.zip>\n", program);
  printf("%s query <path/to/dictionary> <word>\n", program);
  printf("%s lookup <path/to/dictionary> <lookup_string>\n", program);
  printf("%s kanji <path/to/dictionary> <kanji>\n", program);
}

static size_t utf8_length(const char* text) {
  size_t length = 0;
  for (const char* c = text; *c != '\0'; c++) {
    if ((*c & 0xC0) != 0x80) {
      length++;
    }
  }
  return length;
}

static int cmd_import(const char* path) {
  const char* parent = strrchr(path, '/');
#ifdef _WIN32
  const char* backslash = strrchr(path, '\\');
  if (parent == NULL || (backslash != NULL && backslash > parent)) {
    parent = backslash;
  }
#endif
  char* output_dir = NULL;
  if (parent != NULL) {
    size_t length = (size_t)(parent - path);
#ifdef _WIN32
    if (length == 0 || (length == 2 && path[1] == ':')) {
#else
    if (length == 0) {
#endif
      length++;
    }
    output_dir = malloc(length + 1);
    if (output_dir == NULL) {
      printf("failed to allocate output path\n");
      return 1;
    }
    memcpy(output_dir, path, length);
    output_dir[length] = '\0';
  }

  hd_import_result* ir = hd_import(path, output_dir == NULL ? "." : output_dir, false);
  free(output_dir);
  if (ir == NULL) {
    printf("failed to import dictionary\n");
    return 1;
  }

  if (hd_import_result_success(ir)) {
    printf("title: %s\n", hd_import_result_title(ir));
    printf("term_count: %llu\n", hd_import_result_term_count(ir));
    printf("meta_count: %llu\n", hd_import_result_meta_count(ir));
    printf("freq_count: %llu\n", hd_import_result_freq_count(ir));
    printf("pitch_count: %llu\n", hd_import_result_pitch_count(ir));
    printf("kanji_count: %llu\n", hd_import_result_kanji_count(ir));
    printf("media_count: %llu\n", hd_import_result_media_count(ir));
  } else {
    printf("could not import dictionary: %s\n", hd_import_result_error(ir));
  }

  hd_import_result_free(ir);
  return 0;
}

static int cmd_query(const char* db_path, const char* expression) {
  hd_query* q = hd_query_new();
  if (hd_query_add_term_dict(q, db_path) != 0) {
    printf("could not open dictionary: %s\n", db_path);
    hd_query_free(q);
    return 1;
  }

  const hd_term_result* terms = NULL;
  size_t count = 0;
  hd_results* r = hd_query_run(q, expression, &terms, &count);
  if (r == NULL) {
    printf("query failed\n");
    hd_query_free(q);
    return 1;
  }

  printf("query results for: %s length: %zu\n", expression, utf8_length(expression));
  printf("%zu entries\n", count);
  for (size_t i = 0; i < count; i++) {
    printf("---------------------------------------------------------------\n");
    printf("%.*s %.*s %.*s\n", (int)terms[i].expression.len, terms[i].expression.ptr, (int)terms[i].reading.len,
           terms[i].reading.ptr, (int)terms[i].rules.len, terms[i].rules.ptr);
    printf("%zu glossary entries\n", terms[i].glossaries_count);
    for (size_t j = 0; j < terms[i].glossaries_count; j++) {
      printf("------\n");
      printf("%.*s\n", (int)terms[i].glossaries[j].dict_name.len, terms[i].glossaries[j].dict_name.ptr);
      printf("%.*s\n", (int)terms[i].glossaries[j].glossary.len, terms[i].glossaries[j].glossary.ptr);
    }
  }

  hd_results_free(r);
  hd_query_free(q);
  return 0;
}

static int cmd_lookup(const char* const* db_paths, int db_count, const char* lookup_string) {
  const int max_results = 8;
  const size_t scan_length = 16;

  hd_query* q = hd_query_new();
  for (int i = 0; i < db_count; i++) {
    if (hd_query_add_term_dict(q, db_paths[i]) != 0) {
      printf("could not open dictionary: %s\n", db_paths[i]);
      hd_query_free(q);
      return 1;
    }
  }

  hd_deinflector* d = hd_deinflector_new();
  hd_lookup* l = hd_lookup_new(q, d);

  const hd_lookup_result* results = NULL;
  size_t count = 0;
  hd_lookup_results* r = hd_lookup_run(l, lookup_string, max_results, scan_length, &results, &count);
  if (r == NULL) {
    printf("lookup failed\n");
    hd_lookup_free(l);
    hd_deinflector_free(d);
    hd_query_free(q);
    return 1;
  }

  printf("lookup results for: %s max_results: %d scan_length: %zu\n", lookup_string, max_results, scan_length);
  printf("%zu results\n", count);

  for (size_t i = 0; i < count; i++) {
    printf("---------------------------------------------------------------\n");
    printf("%.*s\n", (int)results[i].matched.len, results[i].matched.ptr);
    if (results[i].trace_count > 0) {
      printf("  ");
      for (size_t j = 0; j < results[i].trace_count; j++) {
        printf("%.*s%s", (int)results[i].trace[j].name.len, results[i].trace[j].name.ptr,
               j < results[i].trace_count - 1 ? " -> " : "");
      }
      printf("\n");
    }
    printf("%.*s %.*s\n", (int)results[i].term.expression.len, results[i].term.expression.ptr,
           (int)results[i].term.reading.len, results[i].term.reading.ptr);
    for (size_t j = 0; j < results[i].term.glossaries_count; j++) {
      printf("------\n");
      printf("%.*s\n", (int)results[i].term.glossaries[j].dict_name.len, results[i].term.glossaries[j].dict_name.ptr);
      printf("%.*s\n", (int)results[i].term.glossaries[j].glossary.len, results[i].term.glossaries[j].glossary.ptr);
    }
  }

  const hd_dictionary_style* styles = NULL;
  size_t styles_count = 0;
  hd_styles* s = hd_query_get_styles(q, &styles, &styles_count);
  printf("styles: \n");
  for (size_t i = 0; i < styles_count; i++) {
    printf("%.*s\n", (int)styles[i].dict_name.len, styles[i].dict_name.ptr);
    printf("%.*s\n", (int)styles[i].styles.len, styles[i].styles.ptr);
  }

  hd_styles_free(s);
  hd_lookup_results_free(r);
  hd_lookup_free(l);
  hd_deinflector_free(d);
  hd_query_free(q);
  return 0;
}

static int cmd_kanji(const char* db_path, const char* kanji) {
  hd_query* q = hd_query_new();
  if (hd_query_add_kanji_dict(q, db_path) != 0) {
    printf("could not open dictionary: %s\n", db_path);
    hd_query_free(q);
    return 1;
  }

  const hd_kanji_entry* entries = NULL;
  size_t count = 0;
  hd_kanji_results* r = hd_query_run_kanji(q, kanji, &entries, &count);
  if (r == NULL) {
    printf("kanji query failed\n");
    hd_query_free(q);
    return 1;
  }

  printf("kanji result for: %s\n", kanji);
  printf("%zu entries\n", count);

  for (size_t i = 0; i < count; i++) {
    printf("---------------------------------------------------------------\n");
    printf("dict: %.*s\n", (int)entries[i].dict_name.len, entries[i].dict_name.ptr);
    printf("onyomi: %.*s\n", (int)entries[i].onyomi.len, entries[i].onyomi.ptr);
    printf("kunyomi: %.*s\n", (int)entries[i].kunyomi.len, entries[i].kunyomi.ptr);
    printf("tags: %.*s\n", (int)entries[i].tags.len, entries[i].tags.ptr);
    printf("definitions:\n");
    for (size_t j = 0; j < entries[i].definitions_count; j++) {
      printf("  - %.*s\n", (int)entries[i].definitions[j].len, entries[i].definitions[j].ptr);
    }
    if (entries[i].stats_count > 0) {
      printf("stats:\n");
      for (size_t j = 0; j < entries[i].stats_count; j++) {
        printf("  %.*s: %.*s\n", (int)entries[i].stats[j].key.len, entries[i].stats[j].key.ptr,
               (int)entries[i].stats[j].value.len, entries[i].stats[j].value.ptr);
      }
    }
  }

  hd_kanji_results_free(r);
  hd_query_free(q);
  return 0;
}

int main(int argc, char* argv[]) {
  if (argc < 2) {
    print_usage(argv[0]);
    return 1;
  }

  double start_ms = monotonic_milliseconds();
  int ret;

  const char* command = argv[1];
  if (strcmp(command, "import") == 0 && argc >= 3) {
    ret = cmd_import(argv[2]);
  } else if (strcmp(command, "query") == 0 && argc >= 4) {
    ret = cmd_query(argv[2], argv[3]);
  } else if (strcmp(command, "lookup") == 0 && argc >= 4) {
    ret = cmd_lookup((const char* const*)argv + 2, argc - 3, argv[argc - 1]);
  } else if (strcmp(command, "kanji") == 0 && argc >= 4) {
    ret = cmd_kanji(argv[2], argv[3]);
  } else {
    print_usage(argv[0]);
    return 1;
  }

  double ms = monotonic_milliseconds() - start_ms;
  printf("runtime: %.2fms\n", ms);
  return ret;
}
