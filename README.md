# hoshidicts

This library implements a dictionary backend that works similarly to [Yomitan](https://github.com/yomidevs/yomitan). This was made for [Hoshi Reader](https://github.com/Manhhao/Hoshi-Reader). Japanese, English, Chinese, and Korean language processing are supported through explicit language pipelines; additional languages can add their own preprocessing, deinflection, postprocessing, reading normalization, and part-of-speech condition mapping.

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
Returns the language pipeline for a language identifier. Currently supported identifiers are `ja`, `en`, `zh`, and `ko`.

```cpp
std::vector<TextVariant> LanguageProcessor::preprocess(const std::string& text) const
```
Returns lookup variants generated before deinflection, with a step count used for result ranking.

```cpp
std::vector<DeinflectionResult> LanguageProcessor::deinflect(const std::string& text) const
```
Returns deinflection candidates using the selected language's rules. This is a pipeline stage: pass preprocessed text and postprocess its candidates before querying dictionaries. Korean operates on decomposed compatibility jamo at this stage. As this doesn't use any dictionary data, the result may include invalid deinflections.

```cpp
std::vector<TextVariant> LanguageProcessor::postprocess(const std::string& text) const
```
Returns variants generated after deinflection. Languages without postprocessors return the original text.

```cpp
uint32_t LanguageProcessor::pos_to_conditions(const std::vector<std::string>& part_of_speech) const
```
Converts dictionary part-of-speech tags into the bitmask used for language-specific deinflection filtering.

```cpp
std::string LanguageProcessor::normalize_reading(const std::string& reading) const
```
Returns the reading used to group lookup results. Chinese applies NFC, Unicode lowercase, and Yomitan's pinyin separator removal while preserving tone marks. Japanese, English, and Korean return the reading unchanged. Dictionary readings remain unchanged for display, metadata matching, and `primary_reading` ranking.

```cpp
bool LanguageProcessor::is_text_lookup_worthy(const std::string& text) const
```
Provides a hint for text scanners. Chinese checks Yomitan's Chinese character ranges, including CJK ideographs, Bopomofo, and selected punctuation. Other languages currently return `true`. This does not restrict explicit lookups, including pinyin searches.

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

### Chinese

Chinese processing follows [Yomitan at `0273cbb3`](https://github.com/yomidevs/yomitan/tree/0273cbb33d774861d2470be62623dde56c8059d6/ext/js/language). It generates radical-normalized lookup variants (for example, `⼀` → `一`) and merges entries with the same expression and normalized pinyin, such as `Rì wén` and `rìwén`. The first matching dictionary's original reading is retained. Chinese has no algorithmic deinflection rules. Simplified/traditional conversion, numeric-tone conversion, and Cantonese are not included.

Normalization happens during lookup grouping. Frequency and pronunciation metadata are matched against the retained reading's original spelling. Existing imported dictionaries work without reimporting; storage and direct `DictionaryQuery::query` behavior are unchanged. Pinyin lookup still uses the dictionary's original reading spelling.

```cpp
Lookup lookup(query, language::get("zh"));
auto results = lookup.lookup("日文");
```

The C API selects Chinese with `hd_deinflector_new_for_language("zh")`. The scanner hint is available through `hd_deinflector_is_text_lookup_worthy(d, text)`, returning `0` on null input or an error. `hd_deinflector_new()` still defaults to Japanese.

```sh
hoshidicts-cli lookup zh /path/to/dictionary 日文
hoshidicts-cli preprocess zh ⼀
hoshidicts-cli-c lookup --language zh /path/to/dictionary 日文
```

Both lookup commands accept multiple dictionary paths. The C CLI defaults to Japanese when `--language` is omitted; the option accepts `ja`, `en`, `zh`, and `ko`.

### Korean

Korean processing follows Yomitan at `0273cbb3`, including all 450 transform groups and 2,682 suffix rules. Native Hangul processing reproduces the bundled hangul-js 0.2.6 behavior: preprocessing disassembles syllables into compatibility jamo, deinflection applies the rules, and postprocessing reassembles the candidates. Each preprocessing or postprocessing stage adds one step when it changes the text. This is distinct from Unicode NFD/NFC normalization; conjoining, archaic, and halfwidth jamo retain the upstream behavior.

```cpp
Lookup lookup(query, language::get("ko"));
auto results = lookup.lookup("읽었어요"); // Can match 읽다 in a dictionary tagged with v.
```

Only `v`, `adj`, and `ida` dictionary rules map to Korean part-of-speech conditions. Inflected matches require compatible conditions, using the existing cross-dictionary condition aggregation; exact matches remain available without POS tags. Korean readings are not normalized, and the scanner hint retains the default `true` behavior. The rule engine is initialized only when Korean deinflection is used.

The upstream `-로라` group contains an empty-suffix rule that generates `다` with the `ida` condition even from unrelated input. This behavior, including the same-text candidate for direct `deinflect("다")`, is retained for compatibility. Dictionary lookup still applies POS filtering.

```sh
hoshidicts-cli lookup ko /path/to/dictionary 읽었어요
hoshidicts-cli preprocess ko 읽었어요
hoshidicts-cli-c lookup --language ko /path/to/dictionary 읽었어요
```

The C API selects Korean with `hd_deinflector_new_for_language("ko")`. Existing imported dictionaries work without reimporting. All processing is native; building and using the library requires neither JavaScript nor a Yomitan checkout.

## Acknowledgements

- [Yomitan](https://github.com/yomidevs/yomitan): Dictionary format, Japanese, English, and Korean deinflection rules and descriptions, text processors, Chinese character ranges and pinyin normalization | GPLv3
- [hangul-js](https://github.com/e-/Hangul.js): Korean disassembly and assembly behavior, ported from 0.2.6 | MIT
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
