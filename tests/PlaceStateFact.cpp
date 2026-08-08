// Copyright (c) 2026 YiZhonghua<zhyi@dpai.com>. All rights reserved.

#include "toka/PlaceState.h"

#include <cassert>

using toka::PlaceState;
using toka::PlaceStateFact;
using toka::ExactPlaceFacts;
using toka::PartialMovePlan;
using toka::PartialMoveProjectionKind;
using toka::ProjectionPlaceFacts;

#define CHECK(condition)                                                       \
  do {                                                                         \
    if (!(condition))                                                         \
      return __LINE__;                                                        \
  } while (false)

int main() {
  const PlaceStateFact never = PlaceState::Never;
  const PlaceStateFact live = PlaceState::Live;
  const PlaceStateFact moved = PlaceState::Moved;

  assert(never.isExactly(PlaceState::Never));
  assert(!never.contains(PlaceState::Moved));
  assert(live.isExactly(PlaceState::Live));
  assert(moved.isExactly(PlaceState::Moved));

  const PlaceStateFact maybe = never.join(live);
  assert(maybe.contains(PlaceState::Never));
  assert(maybe.contains(PlaceState::Live));
  assert(!maybe.contains(PlaceState::Moved));
  assert(!maybe.isExactly(PlaceState::Never));

  const PlaceStateFact liveOrMoved = live.join(moved);
  assert(liveOrMoved.contains(PlaceState::Live));
  assert(liveOrMoved.contains(PlaceState::Moved));
  assert(!liveOrMoved.contains(PlaceState::Never));

  const PlaceStateFact unreachable = PlaceStateFact::bottom();
  assert(unreachable.empty());
  assert(unreachable.join(live).isExactly(PlaceState::Live));

  auto projections = ProjectionPlaceFacts::fromLegacyInitMask(0x3, 0x3);
  assert(projections.factAt(0).isExactly(PlaceState::Live));
  projections.markMoved(0);
  assert(projections.factAt(0).isExactly(PlaceState::Moved));
  assert(projections.definitelyLiveMask() == 0x2);
  projections.markLive(0);
  assert(projections.factAt(0).isExactly(PlaceState::Live));
  auto movedBranch = projections;
  movedBranch.markMoved(1);
  const auto joined = projections | movedBranch;
  assert(joined.factAt(0).isExactly(PlaceState::Live));
  assert(joined.factAt(1).contains(PlaceState::Live));
  assert(joined.factAt(1).contains(PlaceState::Moved));
  assert(joined.definitelyLiveMask() == 0x1);

  const auto neverProjection =
      ProjectionPlaceFacts::fromLegacyInitMask(0x1, 0);
  assert(neverProjection.factAt(0).isExactly(PlaceState::Never));
  auto movedProjection =
      ProjectionPlaceFacts::fromLegacyInitMask(0x1, 0x1);
  movedProjection.markMoved(0);
  const auto absentJoin = neverProjection | movedProjection;
  assert(absentJoin.factAt(0).contains(PlaceState::Never));
  assert(absentJoin.factAt(0).contains(PlaceState::Moved));
  assert(!absentJoin.factAt(0).contains(PlaceState::Live));

  const auto fields = PartialMovePlan::directFields(0x3);
  assert(fields.isAdmitted());
  assert(fields.admits(PartialMoveProjectionKind::DirectField, 1));
  assert(!fields.admits(PartialMoveProjectionKind::FixedArrayElement, 1));
  assert(!fields.admits(PartialMoveProjectionKind::DirectField, 2));
  const auto elements = PartialMovePlan::fixedArrayElements(0x4);
  assert(elements.admits(PartialMoveProjectionKind::FixedArrayElement, 2));
  assert(!PartialMovePlan::directFields(0).isAdmitted());

  auto exact = ExactPlaceFacts::fromLegacy(
      never, fields, ProjectionPlaceFacts::fromLegacyInitMask(0x3, 0x3));
  CHECK(exact.whole().isExactly(PlaceState::Never));
  CHECK(exact.transitionWhole(PlaceState::Never, PlaceState::Live));
  CHECK(exact.whole().isExactly(PlaceState::Live));
  CHECK(!exact.transitionWhole(PlaceState::Never, PlaceState::Moved));
  CHECK(exact.transitionWhole(PlaceState::Live, PlaceState::Moved));
  CHECK(exact.transitionWhole(PlaceState::Moved, PlaceState::Live));
  CHECK(exact.plan().admits(PartialMoveProjectionKind::DirectField, 1));
  CHECK(exact.transitionProjection(PartialMoveProjectionKind::DirectField, 0,
                                   PlaceState::Live));
  CHECK(exact.projections().factAt(0).isExactly(PlaceState::Live));
  CHECK(!exact.transitionProjection(
      PartialMoveProjectionKind::FixedArrayElement, 0, PlaceState::Moved));

  auto exactMoved = exact;
  CHECK(exactMoved.transitionProjection(PartialMoveProjectionKind::DirectField,
                                        0, PlaceState::Moved));
  const auto exactJoined = exact | exactMoved;
  CHECK(exactJoined.projections().factAt(0).contains(PlaceState::Live));
  CHECK(exactJoined.projections().factAt(0).contains(PlaceState::Moved));
  CHECK(exactJoined.applyToLegacyInitMask(0) == 0x2);

  exactMoved.repopulateAllProjections();
  CHECK(exactMoved.projections().definitelyLiveMask() == 0x3);

  const auto mismatchedPlan = ExactPlaceFacts::fromLegacy(
      live, elements, ProjectionPlaceFacts::fromLegacyInitMask(0x4, 0x4));
  const auto failClosedJoin = exact | mismatchedPlan;
  CHECK(!failClosedJoin.plan().isAdmitted());
  CHECK(!failClosedJoin.projections().isTracking());
  return 0;
}
