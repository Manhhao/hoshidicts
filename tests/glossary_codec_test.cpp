#include "compression/glossary_codec.hpp"

#include <stdexcept>
#include <string>
#include <vector>

namespace {
void require(bool condition, const char* message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}
}

int main() {
  std::vector<std::string> samples;
  samples.reserve(2000);
  for (int i = 0; i < 2000; ++i) {
    samples.push_back("[{\"type\":\"structured-content\",\"content\":[{\"tag\":\"div\",\"content\":\"definition-" +
                      std::to_string(i) + "\"}]}]");
  }

  const auto dictionary = glossary_codec::train_dictionary(samples, 64 * 1024);
  require(!dictionary.empty(), "dictionary training failed");

  glossary_codec::Compressor plain;
  glossary_codec::Compressor trained(dictionary);
  const std::string input =
      "[{\"type\":\"structured-content\",\"content\":[{\"tag\":\"div\",\"content\":\"definition-new\"}]}]";
  const auto plain_block = plain.compress(input);
  const auto trained_block = trained.compress(input);

  require(!plain_block.empty(), "plain compression failed");
  require(!trained_block.empty(), "trained compression failed");
  require(trained_block.size() < plain_block.size(), "trained block was not smaller");
  require(glossary_codec::decompress(plain_block.data(), plain_block.size(), nullptr) == input,
          "plain round trip failed");

  const auto decoder = glossary_codec::make_decoder(dictionary);
  require(decoder != nullptr, "decoder creation failed");
  require(glossary_codec::decompress(trained_block.data(), trained_block.size(), decoder.get()) == input,
          "trained round trip failed");
  return 0;
}
