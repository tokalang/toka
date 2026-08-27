// Copyright (c) 2026 YiZhonghua<zhyi@dpai.com>. All rights reserved.

#include "toka/SemanticModel.h"

#include <set>
#include <type_traits>
#include <unordered_set>
#include <vector>

using toka::CallableContractId;
using toka::DeclarationId;
using toka::FieldId;
using toka::LangItemId;
using toka::MethodSlotId;
using toka::PlaceId;
using toka::PlaceProjection;
using toka::ResolvedCalleeId;
using toka::ResolvedCalleeKind;
using toka::RootSymbolId;
using toka::SemanticModel;
using toka::SemanticIdentityBuilder;
using toka::SemanticIdentityError;
using toka::SemanticNodeId;
using toka::SubstitutionId;

#define CHECK(condition)                                                       \
  do {                                                                         \
    if (!(condition))                                                          \
      return __LINE__;                                                         \
  } while (false)

static_assert(!std::is_convertible_v<SemanticNodeId, DeclarationId>);
static_assert(!std::is_convertible_v<DeclarationId, SemanticNodeId>);
static_assert(!std::is_constructible_v<SemanticNodeId, DeclarationId>);
static_assert(!std::is_constructible_v<RootSymbolId, FieldId>);
static_assert(std::is_empty_v<SemanticModel>);

int main() {
  const auto nodeResult =
      SemanticIdentityBuilder::semanticNode("unit:a", "node:7");
  const auto sameNodeResult =
      SemanticIdentityBuilder::semanticNode("unit:a", "node:7");
  const auto otherNodeResult =
      SemanticIdentityBuilder::semanticNode("unit:a", "node:8");
  CHECK(nodeResult && sameNodeResult && otherNodeResult);
  const auto node = nodeResult.value();
  const auto sameNode = sameNodeResult.value();
  const auto otherNode = otherNodeResult.value();
  CHECK(node.valid());
  CHECK(node == sameNode);
  CHECK(node != otherNode);
  CHECK(node < otherNode);
  CHECK(!SemanticNodeId{}.valid());

  std::unordered_set<SemanticNodeId> nodes;
  nodes.insert(node);
  nodes.insert(sameNode);
  nodes.insert(otherNode);
  CHECK(nodes.size() == 2);

  const auto declarationResult = SemanticIdentityBuilder::declaration(
      "crate:std;module:thread", "fn:spawn");
  const auto traitResult = SemanticIdentityBuilder::declaration(
      "crate:core", "trait:Callable");
  CHECK(declarationResult && traitResult);
  const auto declaration = declarationResult.value();
  const auto trait = traitResult.value();
  const auto substitutionResult =
      SemanticIdentityBuilder::substitution(declaration, "T=i32");
  const auto slotResult = SemanticIdentityBuilder::methodSlot(trait, "call");
  const auto contractResult = SemanticIdentityBuilder::callableContract(
      "crate:core", "fn(i32)->i32");
  const auto langItemResult =
      SemanticIdentityBuilder::langItem("crate:core", "thread_handoff");
  CHECK(substitutionResult && slotResult && contractResult && langItemResult);
  const auto substitution = substitutionResult.value();
  const auto slot = slotResult.value();
  const auto contract = contractResult.value();
  const auto langItem = langItemResult.value();

  const auto direct = ResolvedCalleeId::direct(declaration);
  const auto directAgain = ResolvedCalleeId::direct(declaration);
  const auto generic =
      ResolvedCalleeId::genericInstance(declaration, substitution);
  const auto traitSlot = ResolvedCalleeId::traitSlot(trait, slot);
  const auto indirect = ResolvedCalleeId::indirectFunction(contract);
  const auto indirectDyn = ResolvedCalleeId::indirectDynFunction(contract);
  const auto external = ResolvedCalleeId::externDeclaration(declaration);
  const auto item = ResolvedCalleeId::langItem(langItem, declaration);

  CHECK(direct.valid());
  CHECK(direct == directAgain);
  CHECK(direct.kind() == ResolvedCalleeKind::DirectDeclaration);
  CHECK(generic.kind() == ResolvedCalleeKind::GenericInstance);
  CHECK(traitSlot.kind() == ResolvedCalleeKind::TraitSlot);
  CHECK(indirect.kind() == ResolvedCalleeKind::IndirectFunction);
  CHECK(indirectDyn.kind() == ResolvedCalleeKind::IndirectDynFunction);
  CHECK(external.kind() == ResolvedCalleeKind::ExternDeclaration);
  CHECK(item.kind() == ResolvedCalleeKind::LangItem);
  CHECK(direct != external);
  CHECK(indirect != indirectDyn);
  CHECK(!ResolvedCalleeId{}.valid());
  CHECK(ResolvedCalleeId{}.kind() == ResolvedCalleeKind::Invalid);
  CHECK(!ResolvedCalleeId::direct(DeclarationId{}).valid());
  CHECK(!ResolvedCalleeId::genericInstance(declaration, SubstitutionId{})
             .valid());

  std::unordered_set<ResolvedCalleeId> callees = {
      direct, generic, traitSlot, indirect, indirectDyn, external, item};
  CHECK(callees.size() == 7);
  std::set<ResolvedCalleeId> orderedCallees(callees.begin(), callees.end());
  CHECK(orderedCallees.size() == callees.size());

  const auto rootResult =
      SemanticIdentityBuilder::rootSymbol(declaration, "binding:value");
  const auto fieldResult =
      SemanticIdentityBuilder::field(declaration, "shape:Pair;field:right");
  CHECK(rootResult && fieldResult);
  const auto root = rootResult.value();
  const auto field = fieldResult.value();
  const auto fieldProjection = PlaceProjection::field(field);
  const auto constantProjection = PlaceProjection::constantIndex(3);
  const auto dynamicProjection = PlaceProjection::dynamicIndex(node);
  const auto dereference = PlaceProjection::dereference();
  const auto unknown = PlaceProjection::unknown();
  CHECK(fieldProjection != constantProjection);
  CHECK(constantProjection != dynamicProjection);
  CHECK(dereference != unknown);
  CHECK(!PlaceProjection::field(FieldId{}).valid());
  CHECK(!PlaceProjection::dynamicIndex(SemanticNodeId{}).valid());

  const PlaceId whole(root);
  const PlaceId projected(root, {fieldProjection, constantProjection});
  const PlaceId sameProjected(root, {fieldProjection, constantProjection});
  const PlaceId dynamic(root, {dynamicProjection});
  CHECK(whole.valid());
  CHECK(projected == sameProjected);
  CHECK(projected != whole);
  CHECK(projected != dynamic);
  CHECK(projected.projections().size() == 2);
  CHECK(!PlaceId(root, {PlaceProjection::field(FieldId{})}).valid());

  std::unordered_set<PlaceId> places = {whole, projected, sameProjected,
                                        dynamic};
  CHECK(places.size() == 3);

  const auto emptyOrigin =
      SemanticIdentityBuilder::semanticNode("", "node:1");
  const auto emptyWitness =
      SemanticIdentityBuilder::semanticNode("unit:a", "");
  const auto invalidParent =
      SemanticIdentityBuilder::rootSymbol(DeclarationId{}, "binding:value");
  CHECK(!emptyOrigin &&
        emptyOrigin.error() == SemanticIdentityError::EmptyOrigin);
  CHECK(!emptyWitness &&
        emptyWitness.error() == SemanticIdentityError::EmptyWitness);
  CHECK(!invalidParent &&
        invalidParent.error() == SemanticIdentityError::InvalidParent);

  constexpr SemanticModel model;
  static_assert(model.empty());
  static_assert(model.size() == 0);
  CHECK(model.empty());
  CHECK(model.size() == 0);
  return 0;
}
