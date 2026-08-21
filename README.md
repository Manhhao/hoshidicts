# hoshidicts

This library implements a dictionary backend that works similarly to [Yomitan](https://github.com/yomidevs/yomitan). This was made for [Hoshi Reader](https://github.com/Manhhao/Hoshi-Reader). Japanese and English language processing are supported through explicit language pipelines; additional languages can add their own preprocessing, deinflection, postprocessing, and part-of-speech condition mapping.

A MIT version of the library is available on the [main-mit](https://github.com/Manhhao/hoshidicts/tree/main-mit) branch.

## Build

The library is built by default. Enable the optional command-line tools with `-DHOSHIDICTS_CLI=ON` and the import/lookup benchmarks with `-DHOSHIDICTS_BENCHMARK=ON`; both options default to `OFF`.

## Reference

### importer
```cpp
ImportResult dictionary_importer::import(const std::string& zip_path, const std::string& output_dir, bool low_ram = false)
```
Imports a Yomitan `.zip` dictionary file into a custom format. The resulting folder is stored in `output_dir/<dict_title>`. Glossaries are compressed using zstd, optionally with a trained compression dictionary. Term, frequency, pitch, kanji, media, and tag banks are supported. Setting `low_ram` to `true` reduces import concurrency.

### query
```cpp
void DictionaryQuery::add_term_dict(const std::string& path)
```
Adds an imported term dictionary to the query.

```cpp
void DictionaryQuery::add_freq_dict(const std::string& path)
```
Adds an imported frequency dictionary to the query.

```cpp
void DictionaryQuery::add_pitch_dict(const std::string& path)
```
Adds an imported pitch dictionary to the query.

```cpp
void DictionaryQuery::add_kanji_dict(const std::string& path)
```
Adds an imported kanji dictionary to the query. Use `query_kanji` to retrieve its entries.

```cpp
std::vector<TermResult> DictionaryQuery::query(const std::string& expression) const
```
Queries all added dictionaries for the given expression. TermResult includes glossary, frequency and pitch data in the order dictionaries were added. Glossaries are decompressed.

```cpp
std::vector<DictionaryStyle> DictionaryQuery::get_styles() const
```
Returns CSS styles for all dictionaries, if present.

```cpp
std::vector<char> DictionaryQuery::get_media_file(const std::string& dict_name, const std::string& media_path) const
```
Returns raw bytes for file originally stored at `media_path` in term dictionary `dict_name` or an empty vector if the file does not exist.

### language
```cpp
const LanguageProcessor& language::get(std::string_view id)
```
Returns the language pipeline for a language identifier. Currently supported identifiers are `ja` and `en`.

```cpp
std::vector<TextVariant> LanguageProcessor::preprocess(const std::string& text) const
```
Returns lookup variants generated before deinflection, with a step count used for result ranking.

```cpp
std::vector<DeinflectionResult> LanguageProcessor::deinflect(const std::string& text) const
```
Returns deinflection candidates using the selected language's rules. As this doesn't use any dictionary data, the result may include invalid deinflections.

```cpp
std::vector<TextVariant> LanguageProcessor::postprocess(const std::string& text) const
```
Returns variants generated after deinflection. Languages without postprocessors return the original text.

```cpp
uint32_t LanguageProcessor::pos_to_conditions(const std::vector<std::string>& part_of_speech) const
```
Converts dictionary part-of-speech tags into the bitmask used for language-specific deinflection filtering.

### lookup
```cpp
Lookup::Lookup(DictionaryQuery& query, const LanguageProcessor& language)
```
Creates a Lookup object using a given query with dictionaries added and a language pipeline.

```cpp
std::vector<LookupResult> Lookup::lookup(const std::string& lookup_string, int max_results = 16, size_t scan_length = 16, const LookupOptions& options = {}) const
```
Follows a parsing strategy similar to Yomitan. Substrings of `lookup_string` are tested from length `scan_length` down to 1. Each substring is preprocessed, deinflected, postprocessed, then queried using the query object.

Results are filtered by language-specific part-of-speech rules. Ranking considers the requested primary reading, matched length, the best preprocessing/deinflection trace, configurable frequency ordering, Yomitan score, and reading/expression equality.

## Acknowledgements

- [Yomitan](https://github.com/yomidevs/yomitan): Dictionary format, Japanese and English deinflection rules and descriptions, text processors | GPLv3
- [glaze](https://github.com/stephenberry/glaze): MIT
- [libdeflate](https://github.com/ebiggers/libdeflate.git): MIT
- [xxHash](https://github.com/Cyan4973/xxHash): BSD-2-Clause
- [zstd](https://github.com/facebook/zstd): BSD
- [utfcpp](https://github.com/nemtrif/utfcpp): BSL-1.0
- [unordered_dense](https://github.com/martinus/unordered_dense.git): MIT
- [utf8proc](https://github.com/JuliaStrings/utf8proc): MIT
- [kanji-processor](https://github.com/yomidevs/kanji-processor): MIT

## License
hoshidicts (main) is licensed under the GNU General Public License v3.0. See [LICENSE](LICENSE) for details.
