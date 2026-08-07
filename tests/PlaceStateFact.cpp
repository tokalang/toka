// Copyright (c) 2026 YiZhonghua<zhyi@dpai.com>. All rights reserved.

#include "toka/PlaceState.h"

#include <cassert>

using toka::PlaceState;
using toka::PlaceStateFact;

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
  return 0;
}
