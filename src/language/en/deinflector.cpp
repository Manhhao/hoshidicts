#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#include "../transform_engine.hpp"
#include "conditions.hpp"
#include "en.hpp"

namespace {

using language::en::ADJ;
using language::en::ADV;
using language::en::N;
using language::en::NP;
using language::en::NS;
using language::en::V;
using language::en::V_PHR;
using language::internal::TransformEngine;

struct SuffixSpec {
  std::string from;
  std::string to;
  uint32_t conditions_in;
  uint32_t conditions_out;
};

const std::vector<std::string> phrasal_verb_particles = {
    "aboard",   "about",     "above",     "across",  "ahead",    "alongside",  "apart",    "around",  "aside",
    "astray",   "away",      "back",      "before",  "behind",   "below",      "beneath",  "besides", "between",
    "beyond",   "by",        "close",     "down",    "east",     "west",       "north",    "south",   "eastward",
    "westward", "northward", "southward", "forward", "backward", "backwards",  "forwards", "home",    "in",
    "inside",   "instead",   "near",      "off",     "on",       "opposite",   "out",      "outside", "over",
    "overhead", "past",      "round",     "since",   "through",  "throughout", "together", "under",   "underneath",
    "up",       "within",    "without"};

const std::vector<std::string> phrasal_verb_prepositions = {
    "aback",  "about",    "above",  "across",  "after", "against", "ahead",   "along",  "among", "apart",
    "around", "as",       "aside",  "at",      "away",  "back",    "before",  "behind", "below", "between",
    "beyond", "by",       "down",   "even",    "for",   "forth",   "forward", "from",   "in",    "into",
    "of",     "off",      "on",     "onto",    "open",  "out",     "over",    "past",   "round", "through",
    "to",     "together", "toward", "towards", "under", "up",      "upon",    "way",    "with",  "without"};

std::unordered_set<std::string> make_word_set() {
  std::unordered_set<std::string> result;
  result.insert(phrasal_verb_particles.begin(), phrasal_verb_particles.end());
  result.insert(phrasal_verb_prepositions.begin(), phrasal_verb_prepositions.end());
  return result;
}

const std::unordered_set<std::string>& phrasal_verb_word_set() {
  static const auto result = make_word_set();
  return result;
}

std::vector<SuffixSpec> doubled_consonant_inflections(std::string_view consonants, std::string_view suffix,
                                                      uint32_t conditions_in, uint32_t conditions_out) {
  std::vector<SuffixSpec> result;
  for (char consonant : consonants) {
    result.push_back({.from = std::string{consonant} + consonant + std::string(suffix),
                      .to = std::string{consonant},
                      .conditions_in = conditions_in,
                      .conditions_out = conditions_out});
  }
  return result;
}

void append(std::vector<SuffixSpec>& target, std::vector<SuffixSpec> source) {
  target.insert(target.end(), std::make_move_iterator(source.begin()), std::make_move_iterator(source.end()));
}

bool is_js_word_char(char c) {
  const auto ch = static_cast<unsigned char>(c);
  return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '_';
}

bool is_js_word_boundary(std::string_view text, size_t offset) {
  const bool before = offset > 0 && is_js_word_char(text[offset - 1]);
  const bool after = offset < text.size() && is_js_word_char(text[offset]);
  return before != after;
}

bool starts_with_at(std::string_view text, size_t offset, std::string_view prefix) {
  return offset + prefix.size() <= text.size() && text.substr(offset, prefix.size()) == prefix;
}

template <typename Words>
bool has_word_prefix(std::string_view text, size_t offset, const Words& words) {
  return std::ranges::any_of(words, [&](std::string_view word) { return starts_with_at(text, offset, word); });
}

bool has_phrasal_word_boundary_at(std::string_view text, size_t offset) {
  if (!is_js_word_boundary(text, offset)) {
    return false;
  }

  return std::ranges::any_of(phrasal_verb_word_set(), [&](const std::string& word) {
    return starts_with_at(text, offset, word) && is_js_word_boundary(text, offset + word.size());
  });
}

bool contains_phrasal_word_with_boundaries(std::string_view text, size_t begin, size_t end) {
  for (size_t offset = begin; offset < end; ++offset) {
    if (has_phrasal_word_boundary_at(text, offset)) {
      return true;
    }
  }
  return false;
}

std::optional<std::string> phrasal_verb_suffix_inflection(const std::string& text, std::string_view inflected,
                                                          std::string_view deinflected) {
  for (size_t offset = 0; offset + inflected.size() < text.size(); ++offset) {
    if (offset > 0 && !is_js_word_char(text[offset - 1])) {
      break;
    }
    if (!starts_with_at(text, offset, inflected)) {
      continue;
    }

    const size_t space_offset = offset + inflected.size();
    if (space_offset >= text.size() || text[space_offset] != ' ') {
      continue;
    }
    if (!has_word_prefix(text, space_offset + 1, phrasal_verb_word_set())) {
      continue;
    }

    std::string result;
    result.reserve(text.size() - inflected.size() + deinflected.size());
    result.append(text, 0, offset);
    result.append(deinflected);
    result.append(text, space_offset, std::string::npos);
    return result;
  }

  return std::nullopt;
}

std::optional<std::string> phrasal_verb_interposed_object(const std::string& text) {
  size_t first_space = 0;
  while (first_space < text.size() && is_js_word_char(text[first_space])) {
    ++first_space;
  }
  if (first_space >= text.size() || text[first_space] != ' ' || first_space == 0) {
    return std::nullopt;
  }

  for (size_t particle_space = first_space + 2; particle_space < text.size(); ++particle_space) {
    if (text[particle_space] != ' ') {
      continue;
    }
    if (!has_word_prefix(text, particle_space + 1, phrasal_verb_particles)) {
      continue;
    }
    if (contains_phrasal_word_with_boundaries(text, first_space + 1, particle_space)) {
      continue;
    }

    std::string result;
    result.reserve(text.size() - (particle_space - first_space));
    result.append(text, 0, first_space);
    result.push_back(' ');
    result.append(text, particle_space + 1, std::string::npos);
    return result;
  }

  return std::nullopt;
}

class EnglishDeinflector final {
 public:
  EnglishDeinflector() : engine_(TransformEngine::TraceOrder::Prepend) { init_transforms(); }

  std::vector<DeinflectionResult> deinflect(const std::string& text) const { return engine_.deinflect(text); }

 private:
  int add_group(std::string name, std::string description) {
    return engine_.add_group({.name = std::move(name), .description = std::move(description)});
  }

  void add_suffix_rules(int group_id, const std::vector<SuffixSpec>& rules) {
    for (const auto& rule : rules) {
      engine_.add_suffix_rule(rule.from, rule.to, rule.conditions_in, rule.conditions_out, group_id);
    }
  }

  void add_phrasal_verb_rules(int group_id, const std::vector<SuffixSpec>& rules) {
    for (const auto& rule : rules) {
      engine_.add_custom_rule([from = rule.from, to = rule.to](
                                  const std::string& text) { return phrasal_verb_suffix_inflection(text, from, to); },
                              V, V_PHR, group_id);
    }
  }

  void init_transforms() {
    const std::vector<SuffixSpec> plural_rules = {
        {.from = "s", .to = "", .conditions_in = NP, .conditions_out = NS},
        {.from = "es", .to = "", .conditions_in = NP, .conditions_out = NS},
        {.from = "ies", .to = "y", .conditions_in = NP, .conditions_out = NS},
        {.from = "ves", .to = "fe", .conditions_in = NP, .conditions_out = NS},
        {.from = "ves", .to = "f", .conditions_in = NP, .conditions_out = NS},
    };
    add_suffix_rules(add_group("plural", "Plural form of a noun"), plural_rules);

    const std::vector<SuffixSpec> possessive_rules = {
        {.from = "'s", .to = "", .conditions_in = N, .conditions_out = N},
        {.from = "s'", .to = "s", .conditions_in = N, .conditions_out = N},
    };
    add_suffix_rules(add_group("possessive", "Possessive form of a noun"), possessive_rules);

    std::vector<SuffixSpec> past_rules = {
        {.from = "ed", .to = "", .conditions_in = V, .conditions_out = V},
        {.from = "ed", .to = "e", .conditions_in = V, .conditions_out = V},
        {.from = "ied", .to = "y", .conditions_in = V, .conditions_out = V},
        {.from = "cked", .to = "c", .conditions_in = V, .conditions_out = V},
        {.from = "laid", .to = "lay", .conditions_in = V, .conditions_out = V},
        {.from = "paid", .to = "pay", .conditions_in = V, .conditions_out = V},
        {.from = "said", .to = "say", .conditions_in = V, .conditions_out = V},
    };
    append(past_rules, doubled_consonant_inflections("bdgklmnprstz", "ed", V, V));
    int id = add_group("past", "Simple past tense of a verb");
    add_suffix_rules(id, past_rules);
    add_phrasal_verb_rules(id, past_rules);

    std::vector<SuffixSpec> ing_rules = {
        {.from = "ing", .to = "", .conditions_in = V, .conditions_out = V},
        {.from = "ing", .to = "e", .conditions_in = V, .conditions_out = V},
        {.from = "ying", .to = "ie", .conditions_in = V, .conditions_out = V},
        {.from = "cking", .to = "c", .conditions_in = V, .conditions_out = V},
    };
    append(ing_rules, doubled_consonant_inflections("bdgklmnprstz", "ing", V, V));
    id = add_group("ing", "Present participle of a verb");
    add_suffix_rules(id, ing_rules);
    add_phrasal_verb_rules(id, ing_rules);

    const std::vector<SuffixSpec> third_person_rules = {
        {.from = "s", .to = "", .conditions_in = V, .conditions_out = V},
        {.from = "es", .to = "", .conditions_in = V, .conditions_out = V},
        {.from = "ies", .to = "y", .conditions_in = V, .conditions_out = V},
    };
    id = add_group("3rd pers. sing. pres", "Third person singular present tense of a verb");
    add_suffix_rules(id, third_person_rules);
    add_phrasal_verb_rules(id, third_person_rules);

    id = add_group("interposed object", "Phrasal verb with interposed object");
    engine_.add_custom_rule(phrasal_verb_interposed_object, 0, V_PHR, id);

    add_suffix_rules(add_group("archaic", "Archaic form of a word"),
                     {{.from = "'d", .to = "ed", .conditions_in = V, .conditions_out = V}});

    add_suffix_rules(add_group("adverb", "Adverb form of an adjective"),
                     {{.from = "ly", .to = "", .conditions_in = ADV, .conditions_out = ADJ},
                      {.from = "ily", .to = "y", .conditions_in = ADV, .conditions_out = ADJ},
                      {.from = "ly", .to = "le", .conditions_in = ADV, .conditions_out = ADJ}});

    std::vector<SuffixSpec> comparative_rules = {
        {.from = "er", .to = "", .conditions_in = ADJ, .conditions_out = ADJ},
        {.from = "er", .to = "e", .conditions_in = ADJ, .conditions_out = ADJ},
        {.from = "ier", .to = "y", .conditions_in = ADJ, .conditions_out = ADJ},
    };
    append(comparative_rules, doubled_consonant_inflections("bdgmnt", "er", ADJ, ADJ));
    add_suffix_rules(add_group("comparative", "Comparative form of an adjective"), comparative_rules);

    std::vector<SuffixSpec> superlative_rules = {
        {.from = "est", .to = "", .conditions_in = ADJ, .conditions_out = ADJ},
        {.from = "est", .to = "e", .conditions_in = ADJ, .conditions_out = ADJ},
        {.from = "iest", .to = "y", .conditions_in = ADJ, .conditions_out = ADJ},
    };
    append(superlative_rules, doubled_consonant_inflections("bdgmnt", "est", ADJ, ADJ));
    add_suffix_rules(add_group("superlative", "Superlative form of an adjective"), superlative_rules);

    add_suffix_rules(add_group("dropped g", "Dropped g in -ing form of a verb"),
                     {{.from = "in'", .to = "ing", .conditions_in = V, .conditions_out = V}});

    std::vector<SuffixSpec> y_rules = {
        {.from = "y", .to = "", .conditions_in = ADJ, .conditions_out = N | V},
        {.from = "y", .to = "e", .conditions_in = ADJ, .conditions_out = N | V},
    };
    append(y_rules, doubled_consonant_inflections("glmnprst", "y", 0, N | V));
    add_suffix_rules(add_group("-y", "Adjective formed from a verb or noun"), y_rules);

    id = add_group("un-", "Negative form of an adjective, adverb, or verb");
    engine_.add_prefix_rule("un", "", ADJ | ADV | V, ADJ | ADV | V, id);

    id = add_group("going-to future", "Going-to future tense of a verb");
    engine_.add_prefix_rule("going to ", "", V, V, id);

    id = add_group("will future", "Will-future tense of a verb");
    engine_.add_prefix_rule("will ", "", V, V, id);

    id = add_group("imperative negative", "Negative imperative form of a verb");
    engine_.add_prefix_rule("don't ", "", V, V, id);
    engine_.add_prefix_rule("do not ", "", V, V, id);

    std::vector<SuffixSpec> able_rules = {
        {.from = "able", .to = "", .conditions_in = V, .conditions_out = ADJ},
        {.from = "able", .to = "e", .conditions_in = V, .conditions_out = ADJ},
        {.from = "iable", .to = "y", .conditions_in = V, .conditions_out = ADJ},
    };
    append(able_rules, doubled_consonant_inflections("bdgklmnprstz", "able", V, ADJ));
    add_suffix_rules(add_group("-able", "Adjective formed from a verb"), able_rules);
  }

  TransformEngine engine_;
};

}

namespace language::en {

std::vector<DeinflectionResult> deinflect(const std::string& text) {
  static const EnglishDeinflector deinflector;
  return deinflector.deinflect(text);
}

}
