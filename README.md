# hoshidicts

This library implements a dictionary backend that works similarly to [Yomitan](https://github.com/yomidevs/yomitan). This was made for [Hoshi Reader](https://github.com/Manhhao/Hoshi-Reader) and was only tested with Japanese. Other languages might need their own deinflector or adjustments to the lookup strategy.

A MIT version of the library is available on the [main-mit](https://github.com/Manhhao/hoshidicts/tree/main-mit) branch.

## Reference

### importer
```cpp
ImportResult dictionary_importer::import(const std::string& zip_path, const std::string& output_dir, bool low_ram = false)
```
Imports a Yomitan `.zip` dictionary file into a custom format. The resulting folder is stored in `output_dir/<dict_title>`. Glossaries are compressed using zstd. Term, frequency and pitch dictionaries are generally supported, but only a small part of the pitch accent spec was implemented. Setting `low_ram` to `true` can reduce memory usage significantly at the cost of slightly lower import speed.

### container
```cpp
PackResult dictionary_container::pack(const std::string& dictionary_dir_utf8, const std::string& output_path_utf8)
```
Packs an imported dictionary directory into a single `.hoshi` file. The payload files are copied byte for byte behind a header and a section table, so the container is queried by mapping it, with no extraction step. Memory use is constant in the size of the dictionary. See [Container format](#container-format) for the byte layout.

```cpp
VerifyResult dictionary_container::verify(const std::string& path_utf8)
```
Checks a container against the XXH3-64 checksum of every section and reports its payload version. Intended for a build pipeline or a one-time check after a download; loading a container does not hash it.

### query
```cpp
void DictionaryQuery::add_term_dict(const std::string& path)
```
Adds an imported term dictionary to the query. `path` is either the directory an import produced or a `.hoshi` container; containers are recognised by their magic bytes.

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

## Container format

A `.hoshi` file is a single-file wrapper around the directory an import produces. It concatenates the payload files byte for byte, prefixed by a header and a section table, so a dictionary can be mapped and queried without being extracted first.

No payload byte changes. Every offset inside `blobs.bin`, `media.bin`, `hash.table` and `bloom.filter` is already relative to the start of its own file, so a byte-identical copy placed at an arbitrary offset works unchanged as long as the consumer is handed the right base pointer and the exact length.

All fields are fixed-width little-endian. Structs are never written directly.

### Header — 64 bytes at offset 0

| Off | Type | Meaning |
| --- | --- | --- |
| 0 | `char[8]` | magic `HOSHIDCT` |
| 8 | `u32` | container version, currently 1 |
| 12 | `u32` | header size, 64 |
| 16 | `u32` | payload version, 3 or 4 |
| 20 | `u32` | flags, bit 0 `LITTLE_ENDIAN` must be set |
| 24 | `u32` | section count, 1 to 32 |
| 28 | `u32` | section descriptor size, 32 |
| 32 | `u64` | section table offset, 64 |
| 40 | `u64` | exact container file size |
| 48 | `u64` | reserved, zero |
| 56 | `u64` | reserved, zero |

The payload version is what the `.hoshidicts_N` marker file encodes in a directory. The marker itself is not stored.

### Section descriptor — 32 bytes

| Off | Type | Meaning |
| --- | --- | --- |
| 0 | `u32` | section type |
| 4 | `u32` | flags, bit 0 `REQUIRED` |
| 8 | `u64` | offset, multiple of 64 |
| 16 | `u64` | length, exact payload length without padding |
| 24 | `u64` | XXH3-64 of the section bytes |

Descriptors are ordered by section type. Sections are laid out in the same order and each one starts on a 64-byte boundary; the gap in between is zero-filled. 64 rather than 8 because `bloom::load()` reads a `uint64_t` at section offset 0 and +8 through a `reinterpret_cast`, and `linear::load()` does the same with a `uint32_t`. Padding is never inside a section view: both loaders reject a size that is not exactly the size derived from the bytes.

### Sections

| ID | Name | Source file | Required |
| --- | --- | --- | --- |
| 1 | `INDEX` | `index.json` | yes |
| 2 | `BLOBS` | `blobs.bin` | yes |
| 3 | `HASH_TABLE` | `hash.table` | yes |
| 4 | `BLOOM_FILTER` | `bloom.filter` | yes |
| 5 | `ZSTD_DICT` | `dict.zstd` | payload v4 only |
| 6 | `MEDIA` | `media.bin` | no |
| 7 | `MEDIA_INDEX` | `media.idx` | iff `MEDIA` is present |

Types 8 to 63 are reserved. An unknown type without `REQUIRED` is ignored, an unknown type with `REQUIRED` makes the container unreadable.

Payload versions 1 and 2 are out of scope: they may carry a separate `styles.css`, which has no section, and no v1 or v2 dictionary is redistributed.

### Reading

`open()` validates the header and the section table only: magic, container version, header size, descriptor size and table offset; the `LITTLE_ENDIAN` flag; payload version 3 or 4; section count 1 to 32; every offset a multiple of 64 that clears the header and table; no empty section, no `offset + length` overflow, everything within the declared file size; the declared file size equals the real file size; no duplicate types and no overlapping ranges; `INDEX`, `BLOBS`, `HASH_TABLE` and `BLOOM_FILTER` present; `ZSTD_DICT` present iff the payload version is 4; `MEDIA_INDEX` present iff `MEDIA` is present.

It deliberately does not hash payloads; that would defeat the point of mapping a 100 MB file lazily. `verify()` does, and is meant for CI and for a one-time check after a download. Containers are recognised by their magic bytes, not by their extension.

`read_index()` returns the `INDEX` section verbatim — the summary the import wrote, carrying `title`, `revision` and the entry `counts`. That is enough to identify a container, notice that a newer build exists, and tell a term dictionary from a kanji or IPA one, without mapping the payload or being told what the file is.

### Writing

`pack()` streams each source file through a fixed buffer while folding its XXH3, so its memory use is constant in the size of the dictionary. It writes `<output>.tmp`, backpatches the header and the table, reopens the result, runs the full `verify()`, and only then renames it into place.

Given the same input directory the output is byte for byte identical. The *importer* is not reproducible, though — `index.json` carries an `importDate` — so importing the same zip twice and packing both produces two different containers. Publish checksums of what was actually built rather than promising reproducibility.

The `LITTLE_ENDIAN` flag is validated but the reader still decodes with the host byte order, so a big-endian host would misread a container. That matches the rest of the engine, which already assumes little-endian for `blobs.bin`. The flag exists so a future big-endian reader can reject rather than silently misparse.

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
