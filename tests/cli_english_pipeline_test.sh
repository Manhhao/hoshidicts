#!/usr/bin/env bash
set -u

CLI="${1:-./build/hoshidicts-cli}"
TOTAL=0
FAILED=0

if [[ ! -x "$CLI" ]]; then
  echo "FAIL: CLI executable not found: $CLI" >&2
  exit 1
fi

assert_public_language_api() {
  local tmp_dir label source_file
  label="public API: explicit language lookup compiles"
  tmp_dir="$(mktemp -d)"
  source_file="${tmp_dir}/language_api.cpp"
  cat >"$source_file" <<'CPP'
#include "hoshidicts/language.hpp"
#include "hoshidicts/lookup.hpp"
#include "hoshidicts/query.hpp"

#include <string>

int main() {
  DictionaryQuery query;
  const auto& language = language::get("ja");
  Lookup lookup(query, language);
  const auto candidates = language.deinflect(std::string("食べた"));
  return candidates.empty() ? 1 : 0;
}
CPP

  if "${CXX:-c++}" -std=c++23 -Iinclude -c "$source_file" -o "${tmp_dir}/language_api.o" 2>"${tmp_dir}/stderr"; then
    record_pass "$label"
  else
    record_fail "$label" "$(cat "${tmp_dir}/stderr")"
  fi
  rm -rf "$tmp_dir"
}

condition_flags() {
  case "$1" in
    v | v_phr)
      echo 1
      ;;
    np)
      echo 2
      ;;
    ns)
      echo 4
      ;;
    n)
      echo 6
      ;;
    adj)
      echo 8
      ;;
    adv)
      echo 16
      ;;
    *)
      echo "unknown rule: $1" >&2
      return 1
      ;;
  esac
}

condition_matches() {
  local current="$1"
  local expected="$2"
  [[ "$current" -eq 0 || $((current & expected)) -ne 0 ]]
}

has_deinflection_candidate() {
  local output="$1"
  local term="$2"
  local rule="$3"
  local expected_trace="$4"
  local expected_flags
  expected_flags="$(condition_flags "$rule")" || return 1

  local line prefix rest conditions after actual_trace
  prefix="${term} (conditions: "
  while IFS= read -r line; do
    [[ "$line" == "$prefix"* ]] || continue

    rest="${line#"$prefix"}"
    conditions="${rest%%)*}"
    [[ "$conditions" =~ ^[0-9]+$ ]] || continue

    after="${rest#*)}"
    actual_trace=""
    if [[ "$after" == "  "* ]]; then
      actual_trace="${after#  }"
    fi

    if condition_matches "$conditions" "$expected_flags" && [[ "$actual_trace" == "$expected_trace" ]]; then
      return 0
    fi
  done <<<"$output"

  return 1
}

record_pass() {
  local label="$1"
  TOTAL=$((TOTAL + 1))
  printf 'ok   %s\n' "$label"
}

record_fail() {
  local label="$1"
  local details="${2:-}"
  TOTAL=$((TOTAL + 1))
  FAILED=$((FAILED + 1))
  printf 'FAIL %s\n' "$label" >&2
  if [[ -n "$details" ]]; then
    printf '%s\n' "$details" >&2
  fi
}

assert_deinflection() {
  local expectation="$1"
  local category="$2"
  local source="$3"
  local term="$4"
  local rule="$5"
  local trace="$6"
  local label="${category}: ${source} => ${term} [${rule}] ${trace}"
  local output

  if ! output="$("$CLI" deinflect en "$source" 2>&1)"; then
    record_fail "$label" "$output"
    return
  fi

  if has_deinflection_candidate "$output" "$term" "$rule" "$trace"; then
    if [[ "$expectation" == "valid" ]]; then
      record_pass "$label"
    else
      record_fail "$label" "unexpected candidate was present"
    fi
  else
    if [[ "$expectation" == "valid" ]]; then
      record_fail "$label" "$output"
    else
      record_pass "$label"
    fi
  fi
}

line_count() {
  local output="$1"
  local needle="$2"
  local count=0
  local line
  while IFS= read -r line; do
    if [[ "$line" == "$needle" ]]; then
      count=$((count + 1))
    fi
  done <<<"$output"
  echo "$count"
}

assert_preprocess_variant() {
  local source="$1"
  local expected="$2"
  local output label count
  label="preprocess: ${source} contains ${expected}"

  if ! output="$("$CLI" preprocess en "$source" 2>&1)"; then
    record_fail "$label" "$output"
    return
  fi

  count="$(line_count "$output" "$expected")"
  if [[ "$count" -eq 1 ]]; then
    record_pass "$label"
  else
    record_fail "$label" "expected one exact line for ${expected}, found ${count}\n${output}"
  fi
}

assert_lookup_contains() {
  local query="$1"
  local expected_line="$2"
  local label="lookup: ${query} contains ${expected_line}"
  local output count

  if [[ ! -d OALDPE10 ]]; then
    printf 'skip %s (OALDPE10 directory not present)\n' "$label"
    return
  fi

  if ! output="$("$CLI" lookup en OALDPE10 "$query" 2>&1)"; then
    record_fail "$label" "$output"
    return
  fi

  count="$(line_count "$output" "$expected_line")"
  if [[ "$count" -ge 1 ]]; then
    record_pass "$label"
  else
    record_fail "$label" "$output"
  fi
}

assert_lookup_filters_merged_pos_entries() {
  local tmp_dir zip_path dict_path output import_output label
  label="lookup: POS filter applies before merging same expression/reading entries"
  tmp_dir="$(mktemp -d)"
  zip_path="${tmp_dir}/fixture.zip"
  dict_path="${tmp_dir}/EnglishPosFixture"

  python3 - "$zip_path" <<'PY'
import json
import sys
import zipfile

zip_path = sys.argv[1]
index = {
    "title": "EnglishPosFixture",
    "format": 3,
    "revision": "test",
    "isUpdatable": False,
    "indexUrl": "",
    "downloadUrl": "",
}
terms = [
    ["close", "close", "", "adj", 0, "ADJ_ONLY_GLOSSARY", 0, ""],
    ["close", "close", "", "v", 0, "VERB_ONLY_GLOSSARY", 0, ""],
]
with zipfile.ZipFile(zip_path, "w") as archive:
    archive.writestr("index.json", json.dumps(index, separators=(",", ":")))
    archive.writestr("term_bank_1.json", json.dumps(terms, separators=(",", ":")))
PY

  if ! import_output="$("$CLI" import "$zip_path" 2>&1)"; then
    record_fail "$label" "$import_output"
    rm -rf "$tmp_dir"
    return
  fi

  if [[ ! -d "$dict_path" ]]; then
    record_fail "$label" "import did not create ${dict_path}\n${import_output}"
    rm -rf "$tmp_dir"
    return
  fi

  if ! output="$("$CLI" lookup en "$dict_path" closed 2>&1)"; then
    record_fail "$label" "$output"
    rm -rf "$tmp_dir"
    return
  fi

  if [[ "$output" == *"VERB_ONLY_GLOSSARY"* && "$output" != *"ADJ_ONLY_GLOSSARY"* ]]; then
    record_pass "$label"
  else
    record_fail "$label" "$output"
  fi
  rm -rf "$tmp_dir"
}

assert_lookup_rejects_empty_rules_without_redirect() {
  local tmp_dir zip_path dict_path output import_output label
  label="lookup: empty rules do not allow algorithmic English deinflection without redirect"
  tmp_dir="$(mktemp -d)"
  zip_path="${tmp_dir}/fixture.zip"
  dict_path="${tmp_dir}/EnglishNoRedirectFixture"

  python3 - "$zip_path" <<'PY'
import json
import sys
import zipfile

zip_path = sys.argv[1]
index = {
    "title": "EnglishNoRedirectFixture",
    "format": 3,
    "revision": "test",
    "isUpdatable": False,
    "indexUrl": "",
    "downloadUrl": "",
}
terms = [
    ["wolf", "wolf", "", "", 0, "WOLF_EMPTY_RULE_GLOSSARY", 0, ""],
]
with zipfile.ZipFile(zip_path, "w") as archive:
    archive.writestr("index.json", json.dumps(index, separators=(",", ":")))
    archive.writestr("term_bank_1.json", json.dumps(terms, separators=(",", ":")))
PY

  if ! import_output="$("$CLI" import "$zip_path" 2>&1)"; then
    record_fail "$label" "$import_output"
    rm -rf "$tmp_dir"
    return
  fi

  if [[ ! -d "$dict_path" ]]; then
    record_fail "$label" "import did not create ${dict_path}\n${import_output}"
    rm -rf "$tmp_dir"
    return
  fi

  if ! output="$("$CLI" lookup en "$dict_path" wolves 2>&1)"; then
    record_fail "$label" "$output"
    rm -rf "$tmp_dir"
    return
  fi

  if [[ "$output" != *"WOLF_EMPTY_RULE_GLOSSARY"* ]]; then
    record_pass "$label"
  else
    record_fail "$label" "$output"
  fi
  rm -rf "$tmp_dir"
}

assert_lookup_uses_dictionary_redirect() {
  local tmp_dir zip_path dict_path output import_output label
  label="lookup: dictionary redirect reaches empty-rule English lemma"
  tmp_dir="$(mktemp -d)"
  zip_path="${tmp_dir}/fixture.zip"
  dict_path="${tmp_dir}/EnglishRedirectFixture"

  python3 - "$zip_path" <<'PY'
import json
import sys
import zipfile

zip_path = sys.argv[1]
index = {
    "title": "EnglishRedirectFixture",
    "format": 3,
    "revision": "test",
    "isUpdatable": False,
    "indexUrl": "",
    "downloadUrl": "",
}
terms = [
    ["wolf", "wolf", "", "", 0, "WOLF_EMPTY_RULE_GLOSSARY", 0, ""],
    ["wolves", "wolves", "", "", 0, [["wolf", ["Redirected from wolves"]]], 0, ""],
]
with zipfile.ZipFile(zip_path, "w") as archive:
    archive.writestr("index.json", json.dumps(index, separators=(",", ":")))
    archive.writestr("term_bank_1.json", json.dumps(terms, separators=(",", ":")))
PY

  if ! import_output="$("$CLI" import "$zip_path" 2>&1)"; then
    record_fail "$label" "$import_output"
    rm -rf "$tmp_dir"
    return
  fi

  if [[ ! -d "$dict_path" ]]; then
    record_fail "$label" "import did not create ${dict_path}\n${import_output}"
    rm -rf "$tmp_dir"
    return
  fi

  if ! output="$("$CLI" lookup en "$dict_path" wolves 2>&1)"; then
    record_fail "$label" "$output"
    rm -rf "$tmp_dir"
    return
  fi

  if [[ "$output" == *"WOLF_EMPTY_RULE_GLOSSARY"* ]] &&
     [[ "$(line_count "$output" "  Redirected from wolves")" -ge 1 ]] &&
     [[ "$(line_count "$output" "wolves wolves")" -eq 0 ]] &&
     [[ "$(line_count "$output" "[[\"wolf\",[\"Redirected from wolves\"]]]")" -eq 0 ]]; then
    record_pass "$label"
  else
    record_fail "$label" "$output"
  fi
  rm -rf "$tmp_dir"
}

assert_public_language_api

while IFS='|' read -r expectation category source term rule trace; do
  [[ -z "${expectation}" || "${expectation}" == \#* ]] && continue
  assert_deinflection "$expectation" "$category" "$source" "$term" "$rule" "$trace"
done <<'YOMITAN_CASES'
# Mirrors ../yomitan/test/language/english-transforms.test.js.
valid|yomitan nouns|cats|cat|ns|plural
valid|yomitan nouns|cat's|cat|ns|possessive
valid|yomitan nouns|cats'|cat|ns|plural -> possessive
valid|yomitan nouns|cats'|cats|ns|possessive
valid|yomitan nouns|dirty|dirt|ns|-y
valid|yomitan nouns|hazy|haze|ns|-y
valid|yomitan nouns|baggy|bag|ns|-y
valid|yomitan nouns|scummy|scum|ns|-y
valid|yomitan nouns|runny|run|ns|-y
valid|yomitan nouns|slippy|slip|ns|-y
valid|yomitan nouns|starry|star|ns|-y
valid|yomitan nouns|gassy|gas|ns|-y
valid|yomitan nouns|witty|wit|ns|-y
valid|yomitan verbs|walked|walk|v|past
valid|yomitan verbs|going to walk|walk|v|going-to future
valid|yomitan verbs|will walk|walk|v|will future
valid|yomitan verbs|don't walk|walk|v|imperative negative
valid|yomitan verbs|do not walk|walk|v|imperative negative
valid|yomitan verbs|hoped|hope|v|past
valid|yomitan verbs|tried|try|v|past
valid|yomitan verbs|frolicked|frolic|v|past
valid|yomitan verbs|rubbed|rub|v|past
valid|yomitan verbs|bidded|bid|v|past
valid|yomitan verbs|rigged|rig|v|past
valid|yomitan verbs|yakked|yak|v|past
valid|yomitan verbs|dialled|dial|v|past
valid|yomitan verbs|skimmed|skim|v|past
valid|yomitan verbs|binned|bin|v|past
valid|yomitan verbs|ripped|rip|v|past
valid|yomitan verbs|starred|star|v|past
valid|yomitan verbs|bussed|bus|v|past
valid|yomitan verbs|pitted|pit|v|past
valid|yomitan verbs|quizzed|quiz|v|past
valid|yomitan verbs|laid|lay|v|past
valid|yomitan verbs|paid|pay|v|past
valid|yomitan verbs|said|say|v|past
valid|yomitan verbs|adorn'd|adorn|v|past -> archaic
valid|yomitan verbs|walking|walk|v|ing
valid|yomitan verbs|driving|drive|v|ing
valid|yomitan verbs|lying|lie|v|ing
valid|yomitan verbs|panicking|panic|v|ing
valid|yomitan verbs|rubbing|rub|v|ing
valid|yomitan verbs|bidding|bid|v|ing
valid|yomitan verbs|rigging|rig|v|ing
valid|yomitan verbs|yakking|yak|v|ing
valid|yomitan verbs|dialling|dial|v|ing
valid|yomitan verbs|skimming|skim|v|ing
valid|yomitan verbs|binning|bin|v|ing
valid|yomitan verbs|ripping|rip|v|ing
valid|yomitan verbs|starring|star|v|ing
valid|yomitan verbs|bussing|bus|v|ing
valid|yomitan verbs|pitting|pit|v|ing
valid|yomitan verbs|quizzing|quiz|v|ing
valid|yomitan verbs|runnin'|run|v|ing -> dropped g
valid|yomitan verbs|walks|walk|v|3rd pers. sing. pres
valid|yomitan verbs|teaches|teach|v|3rd pers. sing. pres
valid|yomitan verbs|tries|try|v|3rd pers. sing. pres
valid|yomitan verbs|pushy|push|v|-y
valid|yomitan verbs|groovy|groove|v|-y
valid|yomitan verbs|saggy|sag|v|-y
valid|yomitan verbs|swimmy|swim|v|-y
valid|yomitan verbs|slippy|slip|v|-y
valid|yomitan verbs|blurry|blur|v|-y
valid|yomitan verbs|chatty|chat|v|-y
valid|yomitan verbs|unlearn|learn|v|un-
valid|yomitan phrasal verbs|look something up|look up|v_phr|interposed object
valid|yomitan phrasal verbs|look it up|look up|v_phr|interposed object
valid|yomitan phrasal verbs|look one up|look up|v_phr|interposed object
valid|yomitan phrasal verbs|looking up|look up|v_phr|ing
valid|yomitan phrasal verbs|looked up|look up|v_phr|past
valid|yomitan phrasal verbs|looks up|look up|v_phr|3rd pers. sing. pres
valid|yomitan phrasal verbs|looked something up|look up|v_phr|past -> interposed object
valid|yomitan adverbs|uninterestingly|interestingly|adj|un-
valid|yomitan adjectives|unfunny|funny|adj|un-
valid|yomitan adjectives|cooler|cool|adj|comparative
valid|yomitan adjectives|subtler|subtle|adj|comparative
valid|yomitan adjectives|funnier|funny|adj|comparative
valid|yomitan adjectives|drabber|drab|adj|comparative
valid|yomitan adjectives|madder|mad|adj|comparative
valid|yomitan adjectives|bigger|big|adj|comparative
valid|yomitan adjectives|dimmer|dim|adj|comparative
valid|yomitan adjectives|tanner|tan|adj|comparative
valid|yomitan adjectives|hotter|hot|adj|comparative
valid|yomitan adjectives|coolest|cool|adj|superlative
valid|yomitan adjectives|subtlest|subtle|adj|superlative
valid|yomitan adjectives|funniest|funny|adj|superlative
valid|yomitan adjectives|drabbest|drab|adj|superlative
valid|yomitan adjectives|maddest|mad|adj|superlative
valid|yomitan adjectives|biggest|big|adj|superlative
valid|yomitan adjectives|dimmest|dim|adj|superlative
valid|yomitan adjectives|tannest|tan|adj|superlative
valid|yomitan adjectives|hottest|hot|adj|superlative
valid|yomitan adjectives|quickly|quick|adj|adverb
valid|yomitan adjectives|happily|happy|adj|adverb
valid|yomitan adjectives|humbly|humble|adj|adverb
invalid|yomitan invalid|bo|boss|ns|plural -> plural
invalid|yomitan invalid|stable|sta|adj|-able
valid|yomitan -able|unforgettable|forget|adj|un- -> -able
valid|yomitan -able|forgettable|forget|adj|-able
valid|yomitan -able|likeable|like|adj|-able
valid|yomitan -able|doable|do|adj|-able
valid|yomitan -able|desirable|desire|adj|-able
valid|yomitan -able|reliable|rely|adj|-able
valid|yomitan -able|movable|move|adj|-able
valid|yomitan -able|adorable|adore|adj|-able
valid|yomitan -able|carriable|carry|adj|-able
YOMITAN_CASES

while IFS='|' read -r expectation category source term rule trace; do
  [[ -z "${expectation}" || "${expectation}" == \#* ]] && continue
  assert_deinflection "$expectation" "$category" "$source" "$term" "$rule" "$trace"
done <<'EXTRA_DEINFLECTION_CASES'
# Additional CLI black-box coverage for supported English transforms.
valid|extra plural|dogs|dog|ns|plural
valid|extra plural|buses|bus|ns|plural
valid|extra plural|wishes|wish|ns|plural
valid|extra plural|boxes|box|ns|plural
valid|extra plural|berries|berry|ns|plural
valid|extra plural|wolves|wolf|ns|plural
valid|extra plural|knives|knife|ns|plural
valid|extra short lemma|as|a|ns|plural
valid|extra short lemma|a's|a|n|possessive
valid|extra possessive|dogs'|dogs|n|possessive
valid|extra possessive|children's|children|n|possessive
valid|extra past|loved|love|v|past
valid|extra past|studied|study|v|past
valid|extra past|hopped|hop|v|past
valid|extra past|planned|plan|v|past
valid|extra past|stopped|stop|v|past
valid|extra ing|making|make|v|ing
valid|extra ing|stopping|stop|v|ing
valid|extra third person|flies|fly|v|3rd pers. sing. pres
valid|extra phrasal verbs|turning off|turn off|v_phr|ing
valid|extra phrasal verbs|turned the light off|turn off|v_phr|past -> interposed object
valid|extra phrasal verbs|turns off|turn off|v_phr|3rd pers. sing. pres
valid|extra phrasal regex parity|looked up!|look up!|v_phr|past
valid|extra phrasal regex parity|looking up!|look up!|v_phr|ing
valid|extra phrasal regex parity|looks up!|look up!|v_phr|3rd pers. sing. pres
valid|extra phrasal regex parity|look it up today|look up today|v_phr|interposed object
valid|extra phrasal regex parity|look it up!|look up!|v_phr|interposed object
invalid|extra phrasal regex parity|look backward-compatible up|look up|v_phr|interposed object
valid|extra adverb|terribly|terrible|adj|adverb
valid|extra comparative|sadder|sad|adj|comparative
valid|extra superlative|saddest|sad|adj|superlative
valid|extra -y|foggy|fog|ns|-y
valid|extra -y|nosey|nose|ns|-y
valid|extra un-|undo|do|v|un-
valid|extra future|going to try|try|v|going-to future
valid|extra future|will try|try|v|will future
valid|extra imperative negative|do not try|try|v|imperative negative
valid|extra -able|usable|use|adj|-able
valid|extra -able|stoppable|stop|adj|-able
invalid|extra invalid|able|a|adj|-able
invalid|extra invalid|un|n|v|un-
EXTRA_DEINFLECTION_CASES

assert_preprocess_variant "Read" "Read"
assert_preprocess_variant "Read" "read"
assert_preprocess_variant "read" "Read"
assert_preprocess_variant "read" "read"
assert_preprocess_variant "DOG" "DOG"
assert_preprocess_variant "DOG" "Dog"
assert_preprocess_variant "DOG" "dog"
assert_preprocess_variant "iPhone" "iPhone"
assert_preprocess_variant "iPhone" "IPhone"
assert_preprocess_variant "iPhone" "Iphone"
assert_preprocess_variant "iPhone" "iphone"
assert_preprocess_variant "CAFÉ" "CAFÉ"
assert_preprocess_variant "CAFÉ" "café"
assert_preprocess_variant "CAFÉ" "Café"
assert_preprocess_variant "über" "über"
assert_preprocess_variant "über" "Über"

assert_lookup_contains "dogs" "  Redirected from dogs"
assert_lookup_contains "dogs" "dog dog"
assert_lookup_contains "driving" "  Redirected from driving"
assert_lookup_contains "driving" "drive drive"
assert_lookup_contains "picked up" "  Redirected from picked up"
assert_lookup_contains "picked up" "pick up pick up"
assert_lookup_contains "pick it up" "  Redirected from pick it up"
assert_lookup_contains "pick it up" "pick pick"
assert_lookup_contains "Read" "read read"
assert_lookup_filters_merged_pos_entries
assert_lookup_rejects_empty_rules_without_redirect
assert_lookup_uses_dictionary_redirect

if [[ "$FAILED" -ne 0 ]]; then
  printf '\n%d/%d CLI checks failed\n' "$FAILED" "$TOTAL" >&2
  exit 1
fi

printf '\n%d CLI checks passed\n' "$TOTAL"
