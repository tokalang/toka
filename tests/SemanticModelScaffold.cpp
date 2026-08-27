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
using toka::SemanticNodeId;
using toka::StructuralIdentityBuilder;
using toka::SubstitutionId;

#define CHECK(condition)                                                       \
  do {                                                                         \
    if (!(condition))                                                          \
      return __LINE__;                                                         \
  } while (false)

static_assert(!std::is_convertible_v<SemanticNodeId, DeclarationId>);
static_assert(!std::is_convertible_v<DeclarationId, SemanticNodeId>);
static_assert(!std::is_constructible_v<SemanticNodeId, DeclarationId>);
static_assert(!std::is_constructible_v<SemanticNodeId, std::string>);
static_assert(!std::is_constructible_v<RootSymbolId, FieldId>);
static_assert(!std::is_convertible_v<toka::TypeId, toka::TemporaryId>);
static_assert(!std::is_convertible_v<toka::LoanId, toka::RegionId>);
static_assert(std::is_empty_v<SemanticModel>);

int main() {
  const auto source =
      StructuralIdentityBuilder::sourceOrigin("fixture", "unit/a", 0);
  const auto node = StructuralIdentityBuilder::semanticNode(source, 0, 7);
  const auto sameNode = StructuralIdentityBuilder::semanticNode(source, 0, 7);
  const auto otherNode = StructuralIdentityBuilder::semanticNode(source, 0, 8);
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

  const auto declaration =
      StructuralIdentityBuilder::declaration("std", "thread", "fn", "spawn");
  const auto substitution = StructuralIdentityBuilder::substitution("T=i32");
  const auto trait = StructuralIdentityBuilder::declaration(
      "core", "traits", "trait", "Callable");
  const auto slot = StructuralIdentityBuilder::methodSlot(trait, 0);
  const auto contract =
      StructuralIdentityBuilder::callableContract("fn(i32)->i32");
  const auto langItem = StructuralIdentityBuilder::langItem("thread_handoff");

  const auto direct = ResolvedCalleeId::direct(declaration);
  const auto directAgain = ResolvedCalleeId::direct(declaration);
  const auto generic =
      ResolvedCalleeId::genericInstance(declaration, substitution);
  const auto traitSlot = ResolvedCalleeId::traitSlot(trait, slot);
  const auto specializedTraitSlot =
      ResolvedCalleeId::traitSlot(trait, slot, substitution);
  const auto indirect = ResolvedCalleeId::indirectFunction(contract);
  const auto indirectDyn = ResolvedCalleeId::indirectDynFunction(contract);
  const auto external = ResolvedCalleeId::externDeclaration(declaration);
  const auto item = ResolvedCalleeId::langItem(langItem, declaration);

  CHECK(direct.valid());
  CHECK(direct == directAgain);
  CHECK(direct.kind() == ResolvedCalleeKind::DirectDeclaration);
  CHECK(generic.kind() == ResolvedCalleeKind::GenericInstance);
  CHECK(traitSlot.kind() == ResolvedCalleeKind::TraitSlot);
  CHECK(specializedTraitSlot.valid() && specializedTraitSlot != traitSlot);
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
  CHECK(!ResolvedCalleeId::traitSlot(DeclarationId{}, slot).valid());
  CHECK(!ResolvedCalleeId::traitSlot(trait, MethodSlotId{}).valid());
  CHECK(!ResolvedCalleeId::indirectFunction(CallableContractId{}).valid());
  CHECK(!ResolvedCalleeId::indirectDynFunction(CallableContractId{}).valid());
  CHECK(!ResolvedCalleeId::externDeclaration(DeclarationId{}).valid());
  CHECK(!ResolvedCalleeId::langItem(LangItemId{}, declaration).valid());

  std::unordered_set<ResolvedCalleeId> callees = {
      direct, generic, traitSlot, indirect, indirectDyn, external, item};
  CHECK(callees.size() == 7);
  std::set<ResolvedCalleeId> orderedCallees(callees.begin(), callees.end());
  CHECK(orderedCallees.size() == callees.size());

  const auto root = StructuralIdentityBuilder::rootSymbol(node, "value", 0);
  const auto field =
      StructuralIdentityBuilder::field(StructuralIdentityBuilder::declaration(
                                           "fixture", "main", "shape", "Pair"),
                                       "right", 1);
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
  const PlaceId reversed(root, {constantProjection, fieldProjection});
  CHECK(whole.valid());
  CHECK(projected == sameProjected);
  CHECK(projected != whole);
  CHECK(projected != dynamic);
  CHECK(projected != reversed);
  CHECK(projected.hashValue() != reversed.hashValue());
  CHECK(projected.projections().size() == 2);
  CHECK(!PlaceId(root, {PlaceProjection::field(FieldId{})}).valid());
  CHECK(!PlaceId{}.valid());

  std::unordered_set<PlaceId> places = {whole, projected, sameProjected,
                                        dynamic};
  CHECK(places.size() == 3);

  const auto call = StructuralIdentityBuilder::callSite(node);
  const auto temporary = StructuralIdentityBuilder::temporary(node, 0, 0);
  const std::vector<toka::DestinationId> destinations = {
      toka::DestinationId::formalSlot(call, 1),
      toka::DestinationId::returnSlot(direct),
      toka::DestinationId::localInitSlot(root),
      toka::DestinationId::aggregateFieldSlot(node, field),
      toka::DestinationId::captureSlot(node, 0),
      toka::DestinationId::temporarySlot(temporary),
  };
  for (const auto &destination : destinations)
    CHECK(destination.valid());
  CHECK(!toka::DestinationId{}.valid());
  CHECK(!toka::DestinationId::formalSlot(call, 0).valid());
  CHECK(!toka::DestinationId::returnSlot(ResolvedCalleeId{}).valid());

  const auto type = StructuralIdentityBuilder::type("i32");
  const auto plan = StructuralIdentityBuilder::argumentPlan(call, 1, 1);
  const auto edge = StructuralIdentityBuilder::transferEdge(plan, 0);
  const auto cleanup = StructuralIdentityBuilder::cleanup(temporary, 0, 0);
  const auto region = StructuralIdentityBuilder::region(
      node, toka::SemanticRegionKind::CallEvaluation, 3, 0);
  const auto loan = StructuralIdentityBuilder::loan(node, whole, whole, 0);
  CHECK(type.valid() && plan.valid() && edge.valid() && cleanup.valid() &&
        region.valid() && loan.valid());

  constexpr SemanticModel model;
  static_assert(model.empty());
  static_assert(model.size() == 0);
  CHECK(model.empty());
  CHECK(model.size() == 0);
  return 0;
}
