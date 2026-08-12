cmake_minimum_required(VERSION 3.22.1)
cmake_policy(SET CMP0077 NEW)

project(hoshidicts LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(HOSHIDICTS_CLI "hoshidicts cli" OFF)
option(HOSHIDICTS_BENCHMARK "hoshidicts import and lookup benchmarks" OFF)
option(HOSHIDICTS_TESTS "hoshidicts tests" OFF)

set(ZSTD_BUILD_PROGRAMS OFF CACHE BOOL "")
set(ZSTD_BUILD_TESTS OFF CACHE BOOL "")
set(ZSTD_BUILD_SHARED OFF CACHE BOOL "")
set(ZSTD_BUILD_STATIC ON CACHE BOOL "")

set(LIBDEFLATE_BUILD_SHARED_LIB OFF CACHE BOOL "")
set(LIBDEFLATE_BUILD_GZIP OFF CACHE BOOL "")

set(UTF8PROC_INSTALL OFF CACHE BOOL "")

add_subdirectory(external/glaze)
add_subdirectory(external/zstd)
add_subdirectory(external/unordered_dense)
add_subdirectory(external/libdeflate)
add_subdirectory(external/utf8proc)

add_library(hoshidicts
    src/compression/glossary_codec.cpp
    src/hash/hash.cpp
    src/hash/bloom.cpp
    src/importer.cpp
    src/memory/memory.cpp
    src/zip/zip.cpp
    src/json/yomitan_parser.cpp
    src/text_processor/text_processor.cpp
    src/text_processor/kanji_variants.cpp
    src/deinflector.cpp
    src/query.cpp
    src/lookup.cpp
    src/hoshidicts_c.cpp
)

target_include_directories(hoshidicts PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/external/utfcpp/source
    ${CMAKE_CURRENT_SOURCE_DIR}/external/xxHash
)

target_link_libraries(hoshidicts PRIVATE
    glaze::glaze
    libzstd_static
    libdeflate_static
    unordered_dense
    utf8proc::utf8proc
)

if(HOSHIDICTS_TESTS)
    enable_testing()
    add_executable(glossary-codec-test tests/glossary_codec_test.cpp)
    target_include_directories(glossary-codec-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(glossary-codec-test PRIVATE hoshidicts libzstd_static)
    add_test(NAME glossary-codec-test COMMAND glossary-codec-test)

    add_executable(importer-format-test tests/importer_format_test.cpp)
    target_link_libraries(importer-format-test PRIVATE hoshidicts)
    if(DEFINED HOSHIDICTS_LARGE_TEST_DICTIONARY)
        add_test(NAME importer-format-test COMMAND importer-format-test
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/dictionary.zip
            ${HOSHIDICTS_LARGE_TEST_DICTIONARY}
            ${CMAKE_CURRENT_BINARY_DIR}/importer-format-test-output)
    endif()
endif()

if(MSVC)
    target_compile_options(hoshidicts PRIVATE /utf-8)
endif()

if(HOSHIDICTS_CLI)
    add_executable(hoshidicts-cli
        cli/main.cpp
    )

    add_executable(hoshidicts-cli-c
        cli/main.c
    )

    target_link_libraries(hoshidicts-cli PRIVATE
        hoshidicts
    )

    target_link_libraries(hoshidicts-cli-c PRIVATE
        hoshidicts
    )
endif()

if(HOSHIDICTS_BENCHMARK)
    add_executable(benchmark-import
        benchmark/import.cpp
    )

    target_link_libraries(benchmark-import PRIVATE
        hoshidicts
    )

    add_executable(benchmark-lookup
        benchmark/lookup.cpp
    )

    add_executable(benchmark-zdict
        benchmark/zdict.cpp
    )

    target_link_libraries(benchmark-lookup PRIVATE
        hoshidicts
    )

    target_link_libraries(benchmark-zdict PRIVATE
        hoshidicts
        unordered_dense
        glaze::glaze
        libzstd_static
        libdeflate_static
    )
endif()
