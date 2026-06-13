#include <_stdio.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "hoshidicts_c.h"

static void print_usage(const char* program) {
  printf("Usage:\n");
  printf("%s import <path/to/dictionary.zip>\n", program);
}

static int cmd_import(const char* path) {
  char output_dir[256] = ".";
  const char* parent = strrchr(path, '/');
  if (parent) {
    memcpy(output_dir, path, parent - path);
    output_dir[parent - path] = '\0';
  }

  hd_import_result* ir = hd_import(path, output_dir, false);
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
    printf("media_count: %llu\n", hd_import_result_media_count(ir));
  } else {
    printf("could not import dictionary: %s\n", hd_import_result_error(ir));
  }

  hd_import_result_free(ir);
  return 0;
}

int main(int argc, char* argv[]) {
  if (argc < 2) {
    print_usage(argv[0]);
    return 1;
  }

  struct timespec t0;
  struct timespec t1;
  clock_gettime(CLOCK_MONOTONIC, &t0);
  int ret;

  const char* command = argv[1];
  if (strcmp(command, "import") == 0 && argc >= 3) {
    ret = cmd_import(argv[2]);
  } else {
    print_usage(argv[0]);
    return 1;
  }

  clock_gettime(CLOCK_MONOTONIC, &t1);
  double ms = (double)(t1.tv_sec - t0.tv_sec) * 1000.0 + (double)(t1.tv_nsec - t0.tv_nsec) / 1e6;
  printf("runtime: %.2fms\n", ms);
  return ret;
}