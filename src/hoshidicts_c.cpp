#include "hoshidicts_c.h"

#include <cstdint>
#include <memory>

#include "hoshidicts/deinflector.hpp"
#include "hoshidicts/importer.hpp"
#include "hoshidicts/query.hpp"

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

// query
struct hd_query {
  DictionaryQuery query;
};

struct hd_results {
  std::vector<TermResult> res;
  std::vector<hd_term_result> term_results;
  std::vector<hd_glossary_entry> glossary_entries;
  std::vector<hd_frequency_entry> frequency_entries;
  std::vector<hd_pitch_entry> pitch_entries;
  std::vector<hd_frequency> frequencies;
  std::vector<hd_pitch> pitches;
  std::vector<hd_str> transcriptions;
};

hd_query* hd_query_new(void) {
  try {
    return new hd_query;
  } catch (...) {
    return nullptr;
  }
}

void hd_query_free(hd_query* q) { delete q; }

int hd_query_add_term_dict(hd_query* q, const char* path) {
  try {
    q->query.add_term_dict(path);
    return 0;
  } catch (...) {
    return 1;
  }
}

int hd_query_add_freq_dict(hd_query* q, const char* path) {
  try {
    q->query.add_freq_dict(path);
    return 0;
  } catch (...) {
    return 1;
  }
}

int hd_query_add_pitch_dict(hd_query* q, const char* path) {
  try {
    q->query.add_pitch_dict(path);
    return 0;
  } catch (...) {
    return 1;
  }
}

hd_results* hd_query_run(const hd_query* q, const char* expression, const hd_term_result** out_terms,
                         size_t* out_count) {
  try {
    auto r = std::make_unique<hd_results>();
    r->res = q->query.query(expression);

    size_t glossaries_count = 0;
    size_t freq_entry_count = 0;
    size_t freq_count = 0;
    size_t pitch_entry_count = 0;
    size_t pitch_count = 0;
    size_t transcription_count = 0;
    for (const auto& term_result : r->res) {
      glossaries_count += term_result.glossaries.size();
      freq_entry_count += term_result.frequencies.size();
      pitch_entry_count += term_result.pitches.size();
      for (const auto& freq_entry : term_result.frequencies) {
        freq_count += freq_entry.frequencies.size();
      }
      for (const auto& pitch_entry : term_result.pitches) {
        pitch_count += pitch_entry.pitches.size();
        transcription_count += pitch_entry.transcriptions.size();
      }
    }
    r->glossary_entries.reserve(glossaries_count);
    r->frequency_entries.reserve(freq_entry_count);
    r->frequencies.reserve(freq_count);
    r->pitch_entries.reserve(pitch_entry_count);
    r->pitches.reserve(pitch_count);
    r->transcriptions.reserve(transcription_count);

    for (const auto& term_result : r->res) {
      hd_term_result tr;
      tr.expression = hd_str{term_result.expression.c_str(), term_result.expression.size()};
      tr.reading = hd_str{term_result.reading.c_str(), term_result.reading.size()};
      tr.rules = hd_str{term_result.rules.c_str(), term_result.rules.size()};

      size_t glossaries_start = r->glossary_entries.size();
      for (const auto& gloss_entry : term_result.glossaries) {
        hd_glossary_entry gls;
        gls.dict_name = hd_str{gloss_entry.dict_name.c_str(), gloss_entry.dict_name.size()};
        gls.glossary = hd_str{gloss_entry.glossary.c_str(), gloss_entry.glossary.size()};
        gls.definition_tags = hd_str{gloss_entry.definition_tags.c_str(), gloss_entry.definition_tags.size()};
        gls.term_tags = hd_str{gloss_entry.term_tags.c_str(), gloss_entry.term_tags.size()};
        r->glossary_entries.push_back(gls);
      }
      tr.glossaries = r->glossary_entries.data() + glossaries_start;
      tr.glossaries_count = term_result.glossaries.size();

      size_t frequency_entry_start = r->frequency_entries.size();
      for (const auto& freq_entry : term_result.frequencies) {
        hd_frequency_entry frq;
        frq.dict_name = hd_str{freq_entry.dict_name.c_str(), freq_entry.dict_name.size()};

        size_t frequencies_start = r->frequencies.size();
        for (const auto& freq : freq_entry.frequencies) {
          r->frequencies.push_back(
              hd_frequency{freq.value, hd_str{freq.display_value.c_str(), freq.display_value.size()}});
        }
        frq.frequencies = r->frequencies.data() + frequencies_start;
        frq.frequencies_count = freq_entry.frequencies.size();

        r->frequency_entries.push_back(frq);
      }
      tr.frequencies = r->frequency_entries.data() + frequency_entry_start;
      tr.frequencies_count = term_result.frequencies.size();

      size_t pitch_entry_start = r->pitch_entries.size();
      for (const auto& pitch_entry : term_result.pitches) {
        hd_pitch_entry p;
        p.dict_name = hd_str{pitch_entry.dict_name.c_str(), pitch_entry.dict_name.size()};

        size_t pitches_start = r->pitches.size();
        for (const auto& pitch : pitch_entry.pitches) {
          r->pitches.push_back(hd_pitch{pitch.position, hd_str{pitch.pattern.c_str(), pitch.pattern.size()},
                                        pitch.nasal.data(), pitch.nasal.size(), pitch.devoice.data(),
                                        pitch.devoice.size()});
        }
        p.pitches = r->pitches.data() + pitches_start;
        p.pitches_count = pitch_entry.pitches.size();

        size_t transcription_start = r->transcriptions.size();
        for (const auto& transcription : pitch_entry.transcriptions) {
          r->transcriptions.push_back(hd_str{transcription.c_str(), transcription.size()});
        }
        p.transcriptions = r->transcriptions.data() + transcription_start;
        p.transcriptions_count = pitch_entry.transcriptions.size();

        r->pitch_entries.push_back(p);
      }
      tr.pitches = r->pitch_entries.data() + pitch_entry_start;
      tr.pitches_count = term_result.pitches.size();

      r->term_results.push_back(tr);
    }

    *out_terms = r->term_results.data();
    *out_count = r->term_results.size();
    return r.release();
  } catch (...) {
    return nullptr;
  }
}

void hd_results_free(hd_results* r) { delete r; }
