# hoshidicts

This library implements a dictionary backend that works similarly to [Yomitan](https://github.com/yomidevs/yomitan). This was made for [Hoshi Reader](https://github.com/Manhhao/Hoshi-Reader) and was only tested with Japanese. Other languages might need their own deinflector or adjustments to the lookup strategy.

A MIT version of the library is available on the [main-mit](https://github.com/Manhhao/hoshidicts/tree/main-mit) branch.

## Reference

### importer
```cpp
ImportResult dictionary_importer::import(const std::string& zip_path, const std::string& output_dir, bool low_ram = false)
```
Imports a Yomitan `.zip` dictionary file into a custom format. The resulting folder is stored in `output_dir/<dict_title>`. Large term dictionaries train and store a per-import zstd dictionary in `glossary.dict` (format v4) to make repeated structured glossaries smaller and faster to decode; small inputs fall back to ordinary zstd frames and format v3. Readers remain compatible with formats v1-v3. Term, frequency and pitch dictionaries are generally supported, but only a small part of the pitch accent spec was implemented. Setting `low_ram` to `true` can reduce memory usage significantly at the cost of slightly lower import speed.

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

### deinflector
```cpp
std::vector<DeinflectionResult> Deinflector::deinflect(const std::string& text) const
```
Deinflects a given Japanese string using rules from the Yomitan deinflector. As this doesn't use any dictionary data, the result may include invalid deinflections.

```cpp
static uint32_t Deinflector::pos_to_conditions(const std::vector<std::string>& part_of_speech)
```
Converts a vector of part-of-speech tags into a bitmask used for deinflection filtering.

### lookup
```cpp
Lookup::Lookup(DictionaryQuery& query, Deinflector& deinflector)
```
Creates a Lookup object using a given query with dictionaries added and a deinflector.

```cpp
std::vector<LookupResult> Lookup::lookup(const std::string& lookup_string, int max_results = 16, size_t scan_length = 16) const
```
Follows a parsing strategy similar to Yomitan. Substrings of `lookup_string` are tested from length `scan_length` down to 1. Each substring is preprocessed, deinflected then queried using the query object.

Results are filtered by part-of-speech tags defined in dictionaries, or added directly if none are present. The results are sorted by matched length first, then by preprocessing steps, then deinflection trace length and finally by frequency.

## Acknowledgements

- [Yomitan](https://github.com/yomidevs/yomitan): Dictionary format, Japanese deinflection rules and descriptions, Japanese preprocessor | GPL-3.0
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
