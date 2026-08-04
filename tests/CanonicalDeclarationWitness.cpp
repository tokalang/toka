#include "toka/CanonicalDeclarationWitness.h"

#include <cassert>
#include <string>
#include <utility>

using toka::CanonicalDeclarationWitnessEncoder;
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

} // namespace

int main() {
  auto input = sample();
  auto first = CanonicalDeclarationWitnessEncoder::encodeOutcomeTransition(input);
  assert(first);
  assert(first->rfind("toka.declaration-witness\0\0\1\0\0\0\1", 0) == 0);

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
  return 0;
}
