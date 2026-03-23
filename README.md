# hoshidicts

This library implements a dictionary backend that works similarly to [Yomitan](https://github.com/yomidevs/yomitan). This was made for [Hoshi Reader](https://github.com/Manhhao/Hoshi-Reader) and was only tested with Japanese. Other languages might need their own deinflector or adjustments to the lookup strategy.

## Reference

### importer
```cpp
ImportResult dictionary_importer::import(const std::string& zip_path, const std::string& output_dir, bool low_ram = false)
```
Imports a Yomitan `.zip` dictionary file into a custom format. The resulting folder is stored in `output_dir/<dict_title>`. Glossaries are compressed using zstd. Term, frequency and pitch dictionaries are generally supported, but only a small part of the pitch accent spec was implemented. Setting `low_ram` to `true` can reduce memory usage significantly at the cost of slightly lower import speed.

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

### deconjugator
```cpp
std::vector<DeconjugationForm> Deconjugator::deconjugate(const std::string& text) const
```
Deconjugates a given Japanese string using a port of Jiten's deconjugator. As this doesn't use any dictionary data, the result may include invalid deconjugations. The result may also include duplicate forms with different processing steps.

### lookup
```cpp
Lookup::Lookup(DictionaryQuery& query, Deconjugator& deconjugator)
```
Creates a Lookup object using a given query with dictionaries added and a deconjugator.

```cpp
std::vector<LookupResult> Lookup::lookup(const std::string& lookup_string, int max_results = 16, size_t scan_length = 16) const
```
Follows a parsing strategy similar to Yomitan. Substrings of `lookup_string` are tested from length `scan_length` down to 1. Each substring is processed using hiragana/katakana conversion, deconjugated then queried using the query object. 

Results are filtered by part-of-speech tags defined in dictionaries, or added directly if none are present. The results are sorted by matched length first, then by processing steps, then deconjugation step count and finally by frequency.

## Acknowledgements

- [Yomitan](https://github.com/yomidevs/yomitan): Dictionary format | GPLv3
- [Jiten](https://github.com/Sirush/Jiten): Deconjugator | Apache-2.0
- [glaze](https://github.com/stephenberry/glaze): MIT
- [libdeflate](https://github.com/ebiggers/libdeflate.git): MIT
- [xxHash](https://github.com/Cyan4973/xxHash): BSD-2-Clause
- [zstd](https://github.com/facebook/zstd): BSD
- [utfcpp](https://github.com/nemtrif/utfcpp): BSL-1.0
- [unordered_dense](https://github.com/martinus/unordered_dense.git): MIT

## License
hoshidicts (main-mit) is licensed under the MIT license. See [LICENSE](LICENSE) for details.