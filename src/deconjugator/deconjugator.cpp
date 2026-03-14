// Deconjugator ported from Jiten (https://github.com/Sirush/Jiten/blob/master/Jiten.Parser/Deconjugator.cs)
#include "hoshidicts/deconjugator.hpp"

#include <utf8.h>
#include <xxh3.h>

#include <cstddef>
#include <glaze/glaze.hpp>
#include <optional>
#include <string_view>
#include <cstdint>

#include "deconjugation_rules.hpp"

using DeconjugationRule = Deconjugator::Rule;
using DeconjugationVirtualRule = Deconjugator::VirtualRule;

struct RawRule {
  std::string type;
  std::optional<std::string> contextrule;
  std::vector<std::string> dec_end;
  std::vector<std::string> con_end;
  std::vector<std::string> dec_tag;
  std::vector<std::string> con_tag;
  std::string detail;
};

template <>
struct glz::meta<RawRule> {
  using T = RawRule;
  static constexpr auto value =
      object("type", glz::raw_string<&T::type>, "contextrule", &T::contextrule, "dec_end", &T::dec_end, "con_end",
             &T::con_end, "dec_tag", &T::dec_tag, "con_tag", &T::con_tag, "detail", glz::raw_string<&T::detail>);
};

namespace {
std::vector<DeconjugationRule> parse_rules(std::string_view json) {
  std::vector<RawRule> parsed_rules;
  auto err = glz::read<glz::opts{.error_on_unknown_keys = false}>(parsed_rules, json);
  if (err) {
    return {};
  }

  std::vector<DeconjugationRule> rules;
  for (auto& parsed : parsed_rules) {
    DeconjugationRule rule;
    rule.type = parsed.type;
    rule.context_rule = parsed.contextrule;
    rule.detail = parsed.detail;

    size_t count = parsed.dec_end.size();
    rule.virtual_rules.reserve(count);

    for (size_t i = 0; i < count; i++) {
      DeconjugationVirtualRule virtual_rule;
      virtual_rule.dec_end = parsed.dec_end[i];
      virtual_rule.con_end = i < parsed.con_end.size() ? parsed.con_end[i] : parsed.con_end[0];
      virtual_rule.dec_tag = i < parsed.dec_tag.size() ? parsed.dec_tag[i] : parsed.dec_tag[0];
      virtual_rule.con_tag = i < parsed.con_tag.size() ? parsed.con_tag[i] : parsed.con_tag[0];

      rule.virtual_rules.push_back(std::move(virtual_rule));
    }

    rules.push_back(std::move(rule));
  }
  return rules;
}

struct DeconjugationFormHash {
  size_t operator()(const DeconjugationForm& f) const {
    XXH3_state_t state;
    XXH3_64bits_reset(&state);

    XXH3_64bits_update(&state, f.text.data(), f.text.size());
    XXH3_64bits_update(&state, f.original_text.data(), f.original_text.size());

    for (const auto& t : f.tags) {
      XXH3_64bits_update(&state, t.data(), t.size());
    }
    for (const auto& p : f.process) {
      XXH3_64bits_update(&state, p.data(), p.size());
    }

    uint64_t seen_hash = 0;
    for (const auto& s : f.seen_text) {
      seen_hash ^= XXH3_64bits(s.data(), s.size());
    }
    XXH3_64bits_update(&state, &seen_hash, sizeof(seen_hash));

    return XXH3_64bits_digest(&state);
  }
};

struct DeconjugationFormEquals {
  bool operator()(const DeconjugationForm& a, const DeconjugationForm& b) const {
    return a.text == b.text && a.original_text == b.original_text && a.tags == b.tags && a.process == b.process &&
           a.seen_text == b.seen_text;
  }
};

using DeconjugationFormSet = std::unordered_set<DeconjugationForm, DeconjugationFormHash, DeconjugationFormEquals>;

bool should_skip_form(const DeconjugationForm& form) {
  auto original_text_len = utf8::distance(form.original_text.begin(), form.original_text.end());
  return form.text.empty() || utf8::distance(form.text.begin(), form.text.end()) > original_text_len + 10 ||
         form.tags.size() > original_text_len + 6;
}

DeconjugationForm create_new_form(const DeconjugationForm& form, const std::string& new_text,
                                  const std::string& con_tag, const std::string& dec_tag, const std::string& detail) {
  DeconjugationForm result;
  result.text = new_text;
  result.original_text = form.original_text;

  result.tags = form.tags;
  if (form.tags.empty() && !con_tag.empty()) {
    result.tags.push_back(con_tag);
  }
  if (!dec_tag.empty()) {
    result.tags.push_back(dec_tag);
  }

  result.seen_text = form.seen_text;
  if (result.seen_text.empty()) {
    result.seen_text.insert(form.text);
  }
  result.seen_text.insert(new_text);

  result.process = form.process;
  if (!detail.empty()) {
    result.process.push_back(detail);
  }

  return result;
}

std::optional<DeconjugationForm> std_rule_deconjugate_inner(const DeconjugationForm& form,
                                                            const DeconjugationVirtualRule& rule,
                                                            const std::string& detail) {
  if (!form.text.ends_with(rule.con_end)) {
    return std::nullopt;
  }

  if (!form.tags.empty() && form.tags.back() != rule.con_tag) {
    return std::nullopt;
  }

  std::string new_text = form.text.substr(0, form.text.size() - rule.con_end.size()) + rule.dec_end;
  if (new_text == form.original_text) {
    return std::nullopt;
  }

  return create_new_form(form, new_text, rule.con_tag, rule.dec_tag, detail);
}

void std_rule_deconjugate(const DeconjugationForm& form, const DeconjugationRule& rule,
                          std::vector<DeconjugationForm>& out) {
  if (rule.detail.empty() && form.tags.empty()) {
    return;
  }

  for (const auto& vr : rule.virtual_rules) {
    if (auto result = std_rule_deconjugate_inner(form, vr, rule.detail)) {
      out.push_back(std::move(*result));
    }
  }
}

void rewrite_rule_deconjugate(const DeconjugationForm& form, const DeconjugationRule& rule,
                              std::vector<DeconjugationForm>& out) {
  if (rule.virtual_rules.empty()) {
    return;
  }
  if (form.text != rule.virtual_rules[0].con_end) {
    return;
  }
  std_rule_deconjugate(form, rule, out);
}

void only_final_rule_deconjugate(const DeconjugationForm& form, const DeconjugationRule& rule,
                                 std::vector<DeconjugationForm>& out) {
  if (!form.tags.empty()) {
    return;
  }
  std_rule_deconjugate(form, rule, out);
}

void never_final_rule_deconjugate(const DeconjugationForm& form, const DeconjugationRule& rule,
                                  std::vector<DeconjugationForm>& out) {
  if (form.tags.empty()) {
    return;
  }
  std_rule_deconjugate(form, rule, out);
}

bool v1_inf_trap_check(const DeconjugationForm& form) { return form.tags.size() != 1 || !(form.tags[0] == "stem-ren"); }

bool sa_special_check(const DeconjugationForm& form, const DeconjugationRule& rule) {
  if (form.text.empty()) {
    return false;
  }
  const auto& con_end = rule.virtual_rules[0].con_end;
  if (!form.text.ends_with(con_end)) {
    return false;
  }

  auto prefix_len = form.text.size() - con_end.size();
  std::string_view prefix(form.text.data(), prefix_len);
  return !prefix.ends_with("さ");
}

bool temiru_check(const DeconjugationForm& form, const DeconjugationRule& rule) {
  const auto& con_end = rule.virtual_rules[0].con_end;
  if (!form.text.ends_with(con_end)) {
    return false;
  }
  auto prefix_len = form.text.size() - con_end.size();
  std::string_view prefix(form.text.data(), prefix_len);
  return prefix.ends_with("て") || prefix.ends_with("で");
}

void context_rule_deconjugate(const DeconjugationForm& form, const DeconjugationRule& rule,
                              std::vector<DeconjugationForm>& out) {
  if (rule.context_rule == "v1inftrap" && !v1_inf_trap_check(form)) {
    return;
  }
  if (rule.context_rule == "saspecial" && !sa_special_check(form, rule)) {
    return;
  }
  if (rule.context_rule == "temirurule" && !temiru_check(form, rule)) {
    return;
  }
  std_rule_deconjugate(form, rule, out);
}

void apply_rule(const DeconjugationForm& form, const DeconjugationRule& rule, std::vector<DeconjugationForm>& out) {
  if (rule.type == "stdrule") {
    std_rule_deconjugate(form, rule, out);
  } else if (rule.type == "rewriterule") {
    rewrite_rule_deconjugate(form, rule, out);
  } else if (rule.type == "onlyfinalrule") {
    only_final_rule_deconjugate(form, rule, out);
  } else if (rule.type == "neverfinalrule") {
    never_final_rule_deconjugate(form, rule, out);
  } else if (rule.type == "contextrule") {
    context_rule_deconjugate(form, rule, out);
  }
}
}

Deconjugator::Deconjugator() : rules_(parse_rules(deconjugation_rules)) {}

std::vector<DeconjugationForm> Deconjugator::deconjugate(const std::string& text) const {
  if (text.empty()) {
    return {};
  }

  DeconjugationFormSet processed;
  DeconjugationFormSet novel;
  DeconjugationForm start_form{.text = text, .original_text = text};
  novel.insert(start_form);

  std::vector<DeconjugationForm> new_forms;
  while (novel.size() > 0) {
    DeconjugationFormSet new_novel;

    for (const auto& form : novel) {
      if (should_skip_form(form)) {
        continue;
      }

      for (const auto& rule : rules_) {
        new_forms.clear();
        apply_rule(form, rule, new_forms);
        for (auto& f : new_forms) {
          if (!processed.contains(f) && !novel.contains(f) && !new_novel.contains(f)) {
            new_novel.insert(std::move(f));
          }
        }
      }
    }

    processed.merge(novel);
    novel = std::move(new_novel);
  }

  std::vector<DeconjugationForm> result(std::make_move_iterator(processed.begin()),
                                        std::make_move_iterator(processed.end()));
  std::ranges::sort(result, [](const auto& a, const auto& b) {
    if (a.text.size() != b.text.size()) {
      return a.text.size() > b.text.size();
    }
    return a.text < b.text;
  });

  return result;
}
