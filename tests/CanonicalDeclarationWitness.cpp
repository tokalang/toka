#include "toka/CanonicalDeclarationWitness.h"

#include <cassert>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <utility>

using toka::CanonicalDeclarationWitnessEncoder;
using toka::CanonicalDeclarationWitnessDecoder;
using toka::OutcomeDeclarationWitnessInput;

namespace {

constexpr const char kType[] =
    "toka-outcome-type-v1;cede=1:0;writable=1:0;nullable=1:0;"
    "blocked=1:0;kind=9:primitive;name=3:i32;";

OutcomeDeclarationWitnessInput sample() {
  OutcomeDeclarationWitnessInput input;
  input.FunctionCrateId = "workspace-test";
  input.FunctionLogicalModulePath = "pkg/example";
  input.FunctionName = "try_read";
  input.Parameters = {
      {0, true, false, kType},
      {1, false, false, kType},
  };
  input.CanonicalResultType = kType;
  input.OutcomeFormalIndex = 0;
  input.ReturnEnum = {"workspace-test", "pkg/example", "ReadResult", 0};
  input.Cases = {{"Err", 1, false}, {"Ok", 0, true}};
  return input;
}

std::optional<unsigned> hexNibble(char value) {
  if (value >= '0' && value <= '9')
    return value - '0';
  if (value >= 'a' && value <= 'f')
    return value - 'a' + 10;
  return std::nullopt;
}

std::optional<std::string> decodeHex(const std::string &hex) {
  if (hex.empty() || hex.size() % 2 != 0)
    return std::nullopt;
  std::string bytes;
  bytes.reserve(hex.size() / 2);
  for (size_t index = 0; index < hex.size(); index += 2) {
    auto high = hexNibble(hex[index]);
    auto low = hexNibble(hex[index + 1]);
    if (!high || !low)
      return std::nullopt;
    bytes.push_back(static_cast<char>((*high << 4) | *low));
  }
  return bytes;
}

int verifyHexFile(const char *path) {
  std::ifstream file(path);
  std::string hex;
  if (!std::getline(file, hex))
    return 1;
  auto bytes = decodeHex(hex);
  if (!bytes)
    return 1;
  auto decoded =
      CanonicalDeclarationWitnessDecoder::decodeOutcomeTransition(*bytes);
  if (!decoded)
    return 1;
  auto canonical =
      CanonicalDeclarationWitnessEncoder::encodeOutcomeTransition(*decoded);
  if (!canonical)
    return 1;
  std::cout << CanonicalDeclarationWitnessEncoder::hexEncode(*canonical)
            << '\n';
  return 0;
}

} // namespace

int main(int argc, char **argv) {
  if (argc == 3 && std::string(argv[1]) == "--hex-file")
    return verifyHexFile(argv[2]);
  assert(argc == 1);

  auto input = sample();
  auto first = CanonicalDeclarationWitnessEncoder::encodeOutcomeTransition(input);
  assert(first);
  assert(first->rfind("toka.declaration-witness\0\0\1\0\0\0\1", 0) == 0);
  auto decoded =
      CanonicalDeclarationWitnessDecoder::decodeOutcomeTransition(*first);
  assert(decoded);
  auto reencoded =
      CanonicalDeclarationWitnessEncoder::encodeOutcomeTransition(*decoded);
  assert(reencoded && *reencoded == *first);

  std::swap(input.Cases[0], input.Cases[1]);
  auto reordered =
      CanonicalDeclarationWitnessEncoder::encodeOutcomeTransition(input);
  assert(reordered && *reordered == *first);

  input.Cases.push_back({"Ok", 0, true});
  assert(!CanonicalDeclarationWitnessEncoder::encodeOutcomeTransition(input));

  input = sample();
  input.Parameters[0].CanonicalPhysicalType = "i32";
  assert(!CanonicalDeclarationWitnessEncoder::encodeOutcomeTransition(input));

  const std::string hex = CanonicalDeclarationWitnessEncoder::hexEncode(*first);
  assert(hex.rfind("746f6b612e6465636c61726174696f6e2d7769746e65737300", 0) ==
         0);

  constexpr size_t kHeaderSize =
      sizeof("toka.declaration-witness\0") - 1 + 2 + 4 + 4;
  std::string truncated = *first;
  truncated.pop_back();
  assert(!CanonicalDeclarationWitnessDecoder::decodeOutcomeTransition(truncated));

  std::string trailing = *first;
  trailing.push_back('\0');
  assert(!CanonicalDeclarationWitnessDecoder::decodeOutcomeTransition(trailing));

  std::string unsupportedVersion = *first;
  unsupportedVersion[sizeof("toka.declaration-witness\0") - 1] = '\0';
  unsupportedVersion[sizeof("toka.declaration-witness\0")] = '\2';
  assert(!CanonicalDeclarationWitnessDecoder::decodeOutcomeTransition(
      unsupportedVersion));

  std::string unknownField = *first;
  unknownField[kHeaderSize + 2] = '\0';
  unknownField[kHeaderSize + 3] = '\0';
  assert(!CanonicalDeclarationWitnessDecoder::decodeOutcomeTransition(
      unknownField));

  std::string reorderedField = *first;
  const size_t secondTag =
      kHeaderSize + 2 + 2 + 4 + std::string("outcome-transition").size();
  reorderedField[kHeaderSize + 2] = '\0';
  reorderedField[kHeaderSize + 3] = '\2';
  reorderedField[secondTag] = '\0';
  reorderedField[secondTag + 1] = '\1';
  assert(!CanonicalDeclarationWitnessDecoder::decodeOutcomeTransition(
      reorderedField));

  input = sample();
  input.Cases = {{"Err", 0, false}, {"Foo", 0, true}};
  auto distinctCases =
      CanonicalDeclarationWitnessEncoder::encodeOutcomeTransition(input);
  assert(distinctCases);
  std::string duplicateVariant = *distinctCases;
  const size_t foo = duplicateVariant.find("Foo");
  assert(foo != std::string::npos);
  duplicateVariant.replace(foo, 3, "Err");
  assert(!CanonicalDeclarationWitnessDecoder::decodeOutcomeTransition(
      duplicateVariant));
  return 0;
}
