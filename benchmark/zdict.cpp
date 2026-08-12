// Trained-zstd-dictionary potential for ~1 KiB glossary blocks, plus the
// serial write/merge bottleneck in the importer. Diagnostic only.
#include <xxh3.h>
#define ZDICT_STATIC_LINKING_ONLY
#include <zdict.h>
#include <zstd.h>

#include <ankerl/unordered_dense.h>

#include <chrono>
#include <format>
#include <iostream>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include "../src/json/yomitan_parser.hpp"
#include "../src/zip/zip.hpp"

using clk = std::chrono::high_resolution_clock;
double ms_since(clk::time_point t) {
  const std::chrono::duration<double, std::milli> e = clk::now() - t;
  return e.count();
}

int main(int argc, char** argv) {
  if (argc < 2) {
    std::cout << std::format("{} <zip_path> [training_blocks] [dictionary_kib]\n", argv[0]);
    return 1;
  }
  Zip zip;
  if (!zip.open(argv[1])) return 1;

  std::vector<int> banks;
  for (int i = 0; i < static_cast<int>(zip.entries.size()); ++i)
    if (zip.entries[i].name.starts_with("term_bank_")) banks.push_back(i);

  // Collect unique glossaries, then shuffle deterministically before a disjoint train/evaluation split.
  ankerl::unordered_dense::set<uint64_t> seen;
  std::vector<std::string> gloss;
  for (int idx : banks) {
    if (gloss.size() >= 100000) break;
    std::string content = zip.read(idx);
    std::vector<Term> terms;
    if (!yomitan_parser::parse_term_bank(content, terms)) continue;
    for (auto& term : terms) {
      const std::string_view glossary = term.glossary.str;
      const uint64_t h = XXH3_64bits(glossary.data(), glossary.size());
      if (seen.insert(h).second) gloss.emplace_back(glossary);
      if (gloss.size() >= 100000) break;
    }
  }

  std::mt19937_64 rng(0x484f534849444943ULL);
  std::shuffle(gloss.begin(), gloss.end(), rng);
  const size_t requested_train_n = argc > 2 ? std::stoull(argv[2]) : 20000;
  const size_t dictionary_kib = argc > 3 ? std::stoull(argv[3]) : 110;
  const size_t train_n = std::min(requested_train_n, gloss.size() / 3);
  std::vector<std::string> train(gloss.begin(), gloss.begin() + train_n);
  std::vector<std::string> eval(gloss.begin() + train_n, gloss.end());
  size_t in_bytes = 0;
  for (const auto& g : eval) in_bytes += g.size();
  std::cout << std::format("train: {} blocks; disjoint eval: {} blocks, {:.1f} MiB, avg {:.0f} B\n\n", train.size(),
                           eval.size(), in_bytes / 1048576.0, static_cast<double>(in_bytes) / eval.size());

  // ---------- Baseline: per-block compress, no trained dict (what ships today) ----------
  auto bench_plain = [&](int level) {
    auto* cctx = ZSTD_createCCtx();
    std::vector<char> buf;
    auto t = clk::now();
    size_t out = 0;
    for (const auto& g : eval) {
      const size_t bound = ZSTD_compressBound(g.size());
      buf.resize(bound);
      const size_t cs = ZSTD_compressCCtx(cctx, buf.data(), bound, g.data(), g.size(), level);
      if (!ZSTD_isError(cs)) out += cs;
    }
    const double ms = ms_since(t);
    ZSTD_freeCCtx(cctx);
    return std::pair{ms, out};
  };

  // ---------- Trained dictionary ----------
  std::vector<char> dict_buf(dictionary_kib * 1024);
  std::vector<size_t> sizes;
  std::string samples;
  {
    for (const auto& g : train) {
      samples += g;
      sizes.push_back(g.size());
    }
  }
  auto t_train = clk::now();
  ZDICT_fastCover_params_t params{};
  params.k = 0;
  params.d = 8;
  params.f = 20;
  params.steps = 4;
  params.nbThreads = std::thread::hardware_concurrency();
  params.splitPoint = 1.0;
  params.accel = 5;
  const size_t dict_size = ZDICT_optimizeTrainFromBuffer_fastCover(
      dict_buf.data(), dict_buf.size(), samples.data(), sizes.data(),
      static_cast<unsigned>(sizes.size()), &params);
  const double train_ms = ms_since(t_train);
  if (ZDICT_isError(dict_size)) {
    std::cout << std::format("dictionary training failed: {}\n", ZDICT_getErrorName(dict_size));
    return 1;
  }
  dict_buf.resize(dict_size);
  std::cout << std::format("trained zstd dict: {:.1f} KiB in {:.0f} ms (one-time, per dictionary import)\n\n",
                           dict_size / 1024.0, train_ms);

  auto bench_trained = [&](int level) {
    auto* cdict = ZSTD_createCDict(dict_buf.data(), dict_buf.size(), level);
    auto* cctx = ZSTD_createCCtx();
    std::vector<char> buf;
    auto t = clk::now();
    size_t out = 0;
    for (const auto& g : eval) {
      const size_t bound = ZSTD_compressBound(g.size());
      buf.resize(bound);
      const size_t cs = ZSTD_compress_usingCDict(cctx, buf.data(), bound, g.data(), g.size(), cdict);
      if (!ZSTD_isError(cs)) out += cs;
    }
    const double ms = ms_since(t);
    ZSTD_freeCCtx(cctx);
    ZSTD_freeCDict(cdict);
    return std::pair{ms, out};
  };

  std::cout << std::format("{:<26}{:>10}{:>12}{:>10}{:>12}\n", "mode", "ms", "MiB out", "ratio", "vs base");
  const auto [base_ms, base_out] = bench_plain(3);
  auto row = [&](const char* name, double ms, size_t out) {
    std::cout << std::format("{:<26}{:>10.1f}{:>12.2f}{:>10.2f}x{:>11.2f}x\n", name, ms, out / 1048576.0,
                             static_cast<double>(in_bytes) / out, static_cast<double>(base_out) / out);
  };
  row("plain level 3 (current)", base_ms, base_out);
  {
    const auto [ms, out] = bench_plain(1);
    row("plain level 1", ms, out);
  }
  {
    const auto [ms, out] = bench_trained(1);
    row("trained dict, level 1", ms, out);
  }
  {
    const auto [ms, out] = bench_trained(3);
    row("trained dict, level 3", ms, out);
  }

  // ---------- Decompression speed (affects every lookup) ----------
  std::cout << "\n=== decompression speed (lookup path) ===\n";
  {
    // Pre-compress with each scheme, then time decompress.
    auto* cctx = ZSTD_createCCtx();
    std::vector<std::vector<char>> plain_blocks, trained_blocks;
    std::vector<char> buf;
    for (const auto& g : eval) {
      const size_t bound = ZSTD_compressBound(g.size());
      buf.resize(bound);
      const size_t cs = ZSTD_compressCCtx(cctx, buf.data(), bound, g.data(), g.size(), 3);
      plain_blocks.emplace_back(buf.begin(), buf.begin() + cs);
    }
    auto* cdict = ZSTD_createCDict(dict_buf.data(), dict_buf.size(), 3);
    for (const auto& g : eval) {
      const size_t bound = ZSTD_compressBound(g.size());
      buf.resize(bound);
      const size_t cs = ZSTD_compress_usingCDict(cctx, buf.data(), bound, g.data(), g.size(), cdict);
      trained_blocks.emplace_back(buf.begin(), buf.begin() + cs);
    }
    ZSTD_freeCCtx(cctx);
    ZSTD_freeCDict(cdict);

    std::vector<char> out(4 * 1024 * 1024);
    auto* dctx = ZSTD_createDCtx();
    auto t = clk::now();
    for (const auto& b : plain_blocks) ZSTD_decompressDCtx(dctx, out.data(), out.size(), b.data(), b.size());
    const double plain_ms = ms_since(t);

    auto* ddict = ZSTD_createDDict(dict_buf.data(), dict_buf.size());
    t = clk::now();
    for (const auto& b : trained_blocks)
      ZSTD_decompress_usingDDict(dctx, out.data(), out.size(), b.data(), b.size(), ddict);
    const double trained_ms = ms_since(t);
    ZSTD_freeDDict(ddict);
    ZSTD_freeDCtx(dctx);

    std::cout << std::format("  plain   : {:.1f} ms for {} blocks ({:.3f} us/block)\n", plain_ms, plain_blocks.size(),
                             plain_ms * 1000.0 / plain_blocks.size());
    std::cout << std::format("  trained : {:.1f} ms for {} blocks ({:.3f} us/block)\n", trained_ms,
                             trained_blocks.size(), trained_ms * 1000.0 / trained_blocks.size());
  }

  return 0;
}
