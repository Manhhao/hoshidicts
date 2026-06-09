#include "text_variants.hpp"

#include <map>
#include <ranges>
#include <utility>

namespace language::internal {

std::vector<TextVariant> process_text_variants(const std::string& src,
                                               const std::vector<TextProcessorDefinition>& processors) {
  std::map<std::string, int> variants = {{src, 0}};

  for (const auto& processor : processors) {
    std::map<std::string, int> next;
    for (const auto& [variant, steps] : variants) {
      for (const auto& processed : processor.process(variant)) {
        const int new_steps = processed == variant ? steps : steps + 1;
        auto [it, inserted] = next.try_emplace(processed, new_steps);
        if (!inserted && new_steps < it->second) {
          it->second = new_steps;
        }
      }
    }
    variants = std::move(next);
  }

  return variants | std::views::transform([](const auto& variant) {
           return TextVariant{.text = variant.first, .steps = variant.second};
         }) |
         std::ranges::to<std::vector>();
}

}
