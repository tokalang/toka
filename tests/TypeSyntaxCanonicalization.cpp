// Copyright (c) 2026 YiZhonghua<zhyi@dpai.com>. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "toka/TypeSyntax.h"
#include "toka/Type.h"
#include <iostream>
#include <map>
#include <string>
#include <utility>
#include <vector>

bool g_JsonDiagnostics = false;

namespace {

using toka::SourceLocation;
using toka::TypeArgumentSyntax;
using toka::TypeSyntax;
using toka::TypeSyntaxPtr;

SourceLocation loc(unsigned value) { return SourceLocation(value); }

TypeSyntaxPtr named(const std::string &name, unsigned begin = 1,
                    unsigned end = 1) {
  return TypeSyntax::named(name, loc(begin), loc(end));
}

bool expectCanonical(const std::string &name, const TypeSyntaxPtr &syntax,
                     const std::string &expected) {
  const std::string actual = syntax->toCanonicalString();
  if (actual == expected)
    return true;
  std::cerr << name << ": expected '" << expected << "', got '" << actual
            << "'\n";
  return false;
}

bool expectLowered(const std::string &name, const TypeSyntaxPtr &syntax) {
  const auto lowered = toka::Type::fromSyntax(syntax);
  const auto legacy = toka::Type::fromString(syntax->toCanonicalString());
  const std::string actual = lowered ? lowered->toString() : "<null>";
  const std::string expected = legacy ? legacy->toString() : "<null>";
  if (actual == expected)
    return true;
  std::cerr << name << ": lowered '" << actual << "', legacy '" << expected
            << "'\n";
  return false;
}

bool expectSemanticRoundTrip(const std::string &name,
                             const TypeSyntaxPtr &syntax) {
  const auto lowered = toka::Type::fromSyntax(syntax);
  const auto reified = lowered ? lowered->toSyntax() : nullptr;
  const auto replayed = reified ? toka::Type::fromSyntax(reified) : nullptr;
  const std::string expected = lowered ? lowered->toString() : "<null>";
  const std::string actual = replayed ? replayed->toString() : "<null>";
  if (actual == expected)
    return true;
  std::cerr << name << ": expected semantic round-trip '" << expected
            << "', got '" << actual << "'\n";
  return false;
}

bool expectRemoved(const std::string &name, const TypeSyntaxPtr &syntax) {
  const auto lowered = toka::Type::fromSyntax(syntax);
  if (lowered && lowered->isUnknown())
    return true;
  std::cerr << name << ": removed type unexpectedly lowered as '"
            << (lowered ? lowered->toString() : "<null>") << "'\n";
  return false;
}

} // namespace

int main() {
  bool passed = true;
  const auto i32 = named("i32", 10, 12);
  const auto self = named("Self", 13, 16);
  const auto voidType = named("void", 17, 20);

  const auto generic = TypeSyntax::generic(
      named("Buffer", 20, 25),
      {TypeArgumentSyntax::type(i32),
       TypeArgumentSyntax::constant("N_", loc(30), loc(31))},
      loc(20), loc(32));
  passed &= expectCanonical("named/Self", self, "Self");
  passed &= expectSemanticRoundTrip("void semantic reification", voidType);
  passed &= expectCanonical("generic type and const arguments", generic,
                            "Buffer<i32,N_>");
  const bool retainedTypeArgumentRange =
      generic->Arguments[0].Begin == i32->Begin &&
      generic->Arguments[0].End == i32->End;
  passed &= retainedTypeArgumentRange;
  if (!retainedTypeArgumentRange)
    std::cerr << "type argument source range was not retained\n";

  const auto array = TypeSyntax::array(
      generic, TypeArgumentSyntax::constant("4", loc(40), loc(40)), loc(33),
      loc(41));
  const auto slice = TypeSyntax::slice(i32, loc(42), loc(46));
  passed &= expectCanonical("array", array, "[Buffer<i32,N_>;4]");
  passed &= expectCanonical("slice", slice, "[i32]");

  const auto tuple = TypeSyntax::tuple({i32, self}, loc(47), loc(55));
  const auto record = TypeSyntax::anonymousRecord(
      {{"value", i32, loc(56), loc(64)},
       {"next", TypeSyntax::slice(self, loc(65), loc(70)), loc(65), loc(70)}},
      loc(56), loc(71));
  passed &= expectCanonical("tuple", tuple, "(i32,Self)");
  passed &= expectCanonical("anonymous record", record, "(value:i32,next:[Self])");

  const auto function = TypeSyntax::function(
      "dyn fn#", {i32, TypeSyntax::slice(self, loc(72), loc(77))},
      TypeSyntax::morphology("cede ", named("Result", 78, 83), loc(78),
                             loc(83)),
      true, false, loc(72), loc(83));
  const auto consumingFunction = TypeSyntax::morphology(
      "cede ", TypeSyntax::function("fn", {i32}, named("void", 84, 87),
                                          false, false, loc(84), loc(87)),
      loc(84), loc(87));
  const auto legacyPrefixArray = named("*[0]u8", 88, 94);
  const auto dynTrait = TypeSyntax::dynTrait("Readable<i32>", loc(84), loc(97));
  const auto projection = TypeSyntax::projection(
      generic, "Readable<i32>", "Item", loc(98), loc(118));
  const auto writableProjection = TypeSyntax::morphology(
      "#", projection, loc(98), loc(119), true);
  passed &= expectCanonical("function", function,
                            "dyn fn#(i32,[Self])->cede Result");
  passed &= expectCanonical("dyn trait", dynTrait, "dyn @Readable<i32>");
  passed &= expectCanonical("associated projection", projection,
                            "Buffer<i32,N_>@Readable<i32>::Item");
  passed &= expectCanonical("morphic associated projection", writableProjection,
                            "Buffer<i32,N_>@Readable<i32>::Item#");

  const auto morphology = TypeSyntax::morphology(
      "nul", TypeSyntax::morphology("*", i32, loc(120), loc(123)), loc(119),
      loc(123));
  const auto postfix = TypeSyntax::morphology("#", morphology, loc(119),
                                               loc(124), true);
  const auto missOutcome =
      TypeSyntax::missOutcome(i32, loc(125), loc(132));
  const auto removedPayload =
      TypeSyntax::morphology("?", i32, loc(133), loc(137), true);
  const auto removedUnique = TypeSyntax::morphology(
      "nul", TypeSyntax::morphology("^", i32, loc(138), loc(142)),
      loc(138), loc(142));
  passed &= expectCanonical("morphology", postfix, "nul*i32#");
  passed &= expectCanonical("miss outcome", missOutcome, "i32|miss");
  passed &= expectCanonical("invalid recovery",
                            TypeSyntax::invalid("Option<i32", loc(125), loc(134)),
                            "Option<i32");

  passed &= expectLowered("generic direct lowering", generic);
  passed &= expectLowered("array direct lowering", array);
  passed &= expectLowered("function direct lowering", function);
  passed &= expectLowered("consuming function direct lowering",
                          consumingFunction);
  passed &= expectLowered("legacy prefix-array bridge", legacyPrefixArray);
  passed &= expectLowered("morphology direct lowering", postfix);
  passed &= expectLowered("miss outcome direct lowering", missOutcome);
  passed &= expectRemoved("removed nullable payload", removedPayload);
  passed &= expectRemoved("removed nullable unique", removedUnique);
  const auto removedPayloadReplay = toka::Type::fromString("i32?");
  const auto removedUniqueReplay = toka::Type::fromString("nul ^i32");
  passed &= removedPayloadReplay && removedPayloadReplay->isUnknown();
  passed &= removedUniqueReplay && removedUniqueReplay->isUnknown();
  passed &= expectSemanticRoundTrip("generic semantic reification", generic);
  passed &= expectSemanticRoundTrip("array semantic reification", array);
  passed &= expectSemanticRoundTrip("function semantic reification", function);
  passed &= expectSemanticRoundTrip("consuming function semantic reification",
                                    consumingFunction);
  passed &= expectSemanticRoundTrip("dyn trait semantic reification", dynTrait);
  passed &= expectSemanticRoundTrip("projection semantic reification", projection);
  passed &= expectSemanticRoundTrip("morphic projection semantic reification",
                                    writableProjection);
  passed &= expectSemanticRoundTrip("morphology semantic reification", postfix);
  passed &= expectSemanticRoundTrip("miss outcome semantic reification",
                                    missOutcome);
  const auto loweredRecord = toka::Type::fromSyntax(record);
  const auto recordShape = std::dynamic_pointer_cast<toka::ShapeType>(loweredRecord);
  const bool retainedRecordStructure =
      recordShape && recordShape->SourceSyntax == record;
  passed &= retainedRecordStructure;
  if (!retainedRecordStructure)
    std::cerr << "anonymous record source structure was not retained\n";

  const auto substituted = generic->substitute(
      {{"i32", named("u64", 10, 12)}, {"N_", named("8", 30, 31)}});
  passed &= expectCanonical("structural substitution", substituted,
                            "Buffer<u64,8>");
  const auto substitutedOutcome = missOutcome->substitute(
      {{"i32", named("u64", 10, 12)}});
  passed &= expectCanonical("miss outcome substitution", substitutedOutcome,
                            "u64|miss");

  // === Level-2 Handles & Multi-layer Raw Tests (Phase 2 M1) ===
  const auto doubleBorrow = TypeSyntax::morphology(
      "&", TypeSyntax::morphology("&", i32, loc(140), loc(143)), loc(139), loc(143));
  const auto borrowUnique = TypeSyntax::morphology(
      "&", TypeSyntax::morphology("^", i32, loc(140), loc(143)), loc(139), loc(143));
  const auto borrowShared = TypeSyntax::morphology(
      "&", TypeSyntax::morphology("~", i32, loc(140), loc(143)), loc(139), loc(143));
  const auto tripleRaw = TypeSyntax::morphology(
      "*", TypeSyntax::morphology("*", TypeSyntax::morphology("*", i32, loc(142), loc(145)), loc(141), loc(145)), loc(140), loc(145));

  const auto nulOuterRaw = TypeSyntax::morphology(
      "nul", TypeSyntax::morphology("*", TypeSyntax::morphology("*", i32, loc(141), loc(144)), loc(140), loc(144)), loc(139), loc(144));
  const auto nulInnerRaw = TypeSyntax::morphology(
      "*", TypeSyntax::morphology("nul", TypeSyntax::morphology("*", i32, loc(141), loc(144)), loc(140), loc(144)), loc(139), loc(144));
  const auto nulBothRaw = TypeSyntax::morphology(
      "nul", TypeSyntax::morphology("*", TypeSyntax::morphology("nul", TypeSyntax::morphology("*", i32, loc(142), loc(145)), loc(141), loc(145)), loc(140), loc(145)), loc(139), loc(145));

  passed &= expectCanonical("double borrow canonical", doubleBorrow, "&&i32");
  passed &= expectCanonical("borrow unique canonical", borrowUnique, "&^i32");
  passed &= expectCanonical("borrow shared canonical", borrowShared, "&~i32");
  passed &= expectCanonical("triple raw canonical", tripleRaw, "***i32");
  passed &= expectCanonical("nul outer raw canonical", nulOuterRaw, "nul**i32");
  passed &= expectCanonical("nul inner raw canonical", nulInnerRaw, "*nul*i32");
  passed &= expectCanonical("nul both raw canonical", nulBothRaw, "nul*nul*i32");

  passed &= expectLowered("double borrow direct lowering", doubleBorrow);
  passed &= expectLowered("borrow unique direct lowering", borrowUnique);
  passed &= expectLowered("borrow shared direct lowering", borrowShared);
  passed &= expectLowered("triple raw direct lowering", tripleRaw);
  passed &= expectLowered("nul outer raw direct lowering", nulOuterRaw);
  passed &= expectLowered("nul inner raw direct lowering", nulInnerRaw);
  passed &= expectLowered("nul both raw direct lowering", nulBothRaw);

  passed &= expectSemanticRoundTrip("double borrow semantic reification", doubleBorrow);
  passed &= expectSemanticRoundTrip("borrow unique semantic reification", borrowUnique);
  passed &= expectSemanticRoundTrip("borrow shared semantic reification", borrowShared);
  passed &= expectSemanticRoundTrip("triple raw semantic reification", tripleRaw);
  passed &= expectSemanticRoundTrip("nul outer raw semantic reification", nulOuterRaw);
  passed &= expectSemanticRoundTrip("nul inner raw semantic reification", nulInnerRaw);
  passed &= expectSemanticRoundTrip("nul both raw semantic reification", nulBothRaw);

  // Mangling collision check across distinct layered handle / raw types
  std::vector<std::pair<std::string, TypeSyntaxPtr>> allSyntaxes = {
      {"&&i32", doubleBorrow},
      {"&^i32", borrowUnique},
      {"&~i32", borrowShared},
      {"***i32", tripleRaw},
      {"nul**i32", nulOuterRaw},
      {"*nul*i32", nulInnerRaw},
      {"nul*nul*i32", nulBothRaw},
  };
  std::map<std::string, std::string> seenMangles;
  for (const auto &p : allSyntaxes) {
    auto t = toka::Type::fromSyntax(p.second);
    if (!t) {
      std::cerr << p.first << ": failed to lower type for mangling check\n";
      passed = false;
      continue;
    }
    std::string m = t->getMangledName();
    if (seenMangles.count(m)) {
      std::cerr << "Mangle collision between '" << p.first << "' and '"
                << seenMangles[m] << "' on mangled name: " << m << "\n";
      passed = false;
    } else {
      seenMangles[m] = p.first;
    }
  }

  return passed ? 0 : 1;
}
