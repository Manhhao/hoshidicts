#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#if defined(__clang__) && defined(__APPLE__)
#define SWIFT_IMPORT_UNSAFE __attribute__((swift_attr("import_unsafe")))
#else
#define SWIFT_IMPORT_UNSAFE
#endif

struct Frequency {
  int value;
  std::string display_value;
};

struct DictionaryStyle {
  std::string dict_name;
  std::string styles;
};

struct MediaFileView {
  const char* data;
  size_t size;
};

struct DictionaryRedirect {
  std::string form_of;
  std::vector<std::string> inflection_rules;
};

struct ZSTD_DDict_s;

struct GlossaryEntry {
  std::string dict_name;
  std::string glossary;
  std::string definition_tags;
  std::string term_tags;
  const uint8_t* compressed_data = nullptr;
  uint32_t compressed_size = 0;
  const ZSTD_DDict_s* zstd_dict = nullptr;
  std::vector<DictionaryRedirect> redirects;
  uint8_t dictionary_format_version = 1;
};

struct FrequencyEntry {
  std::string dict_name;
  std::vector<Frequency> frequencies;
};

struct Pitch {
  int position = 0;
  std::string pattern;
  std::vector<int> nasal;
  std::vector<int> devoice;
};

struct PitchEntry {
  std::string dict_name;
  std::vector<Pitch> pitches;
  std::vector<std::string> transcriptions;
};

struct TermResult {
  std::string expression;
  std::string reading;
  std::string rules;
  int score = 0;
  std::vector<GlossaryEntry> glossaries;
  std::vector<FrequencyEntry> frequencies;
  std::vector<PitchEntry> pitches;
};

struct KanjiEntry {
  std::string dict_name;
  std::string onyomi;
  std::string kunyomi;
  std::string tags;
  std::vector<std::string> definitions;
  std::unordered_map<std::string, std::string> stats;
};

struct KanjiResult {
  std::string character;
  std::vector<KanjiEntry> entries;
};

class DictionaryQuery {
 public:
  DictionaryQuery();
  ~DictionaryQuery();

  DictionaryQuery(const DictionaryQuery&) = delete;
  DictionaryQuery& operator=(const DictionaryQuery&) = delete;

  DictionaryQuery(DictionaryQuery&&) noexcept;
  DictionaryQuery& operator=(DictionaryQuery&&) noexcept;

  void add_term_dict(const std::string& path);
  void add_freq_dict(const std::string& path);
  void add_pitch_dict(const std::string& path);
  void add_kanji_dict(const std::string& path);
  bool try_add_term_dict(const std::string& path);
  bool try_add_freq_dict(const std::string& path);
  bool try_add_pitch_dict(const std::string& path);
  bool try_add_kanji_dict(const std::string& path);

  void query_freq(std::vector<TermResult>& terms) const;
  void query_pitch(std::vector<TermResult>& terms) const;
  KanjiResult query_kanji(const std::string& kanji) const;

  std::vector<TermResult> query(const std::string& expression) const;

  std::vector<char> get_media_file(const std::string& dict_name, const std::string& media_path) const;
  SWIFT_IMPORT_UNSAFE
  MediaFileView get_media_file_view(const std::string& dict_name, const std::string& media_path) const;
  std::vector<DictionaryStyle> get_styles() const;
  std::vector<std::string> get_freq_dict_order() const;

 private:
  friend class Lookup;
  std::vector<TermResult> query_raw(const std::string& expression) const;
  std::vector<TermResult> query_raw_entries(const std::string& expression) const;
  static std::vector<TermResult> merge_term_entries(std::vector<TermResult> terms);
  void order_glossaries(std::vector<GlossaryEntry>& glossaries) const;
  void materialize(TermResult& term) const;

  struct DictionaryData;
  struct Dictionary {
    Dictionary();
    ~Dictionary();

    Dictionary(const Dictionary&) = delete;
    Dictionary& operator=(const Dictionary&) = delete;

    Dictionary(Dictionary&&) noexcept;
    Dictionary& operator=(Dictionary&&) noexcept;

    std::string name;
    std::string styles;
    std::unique_ptr<DictionaryData> data;
  };
  enum DictionaryType : uint8_t { TERM, FREQ, PITCH, KANJI };

  bool add_dict(const std::string& path, DictionaryType);

  static std::string decompress_glossary(const void* data, size_t size, const ZSTD_DDict_s* dict);
  std::vector<Dictionary> term_dicts_;
  std::vector<Dictionary> freq_dicts_;
  std::vector<Dictionary> pitch_dicts_;
  std::vector<Dictionary> kanji_dicts_;
};
