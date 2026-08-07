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
#pragma once

#include <cstdint>

namespace toka {

// A concrete exact-place state. `Never` and `Moved` are distinct absent
// states: only `Never` may satisfy an initialization precondition.
enum class PlaceState : uint8_t {
  Never = 1 << 0,
  Live = 1 << 1,
  Moved = 1 << 2,
};

constexpr uint8_t placeStateMask(PlaceState state) {
  return static_cast<uint8_t>(state);
}

// A static exact-place fact is the set of concrete states reachable at a CFG
// point. Reachable program points must retain at least one state; the empty
// set is reserved for internal unreachable-flow handling.
class PlaceStateFact {
public:
  constexpr PlaceStateFact() = default;
  constexpr PlaceStateFact(PlaceState state) : m_States(placeStateMask(state)) {}

  static constexpr PlaceStateFact bottom() {
    return PlaceStateFact(0, RawBits{});
  }

  constexpr PlaceStateFact &operator=(PlaceState state) {
    m_States = placeStateMask(state);
    return *this;
  }

  constexpr bool empty() const { return m_States == 0; }
  constexpr bool contains(PlaceState state) const {
    return (m_States & placeStateMask(state)) != 0;
  }
  constexpr bool isExactly(PlaceState state) const {
    return m_States == placeStateMask(state);
  }
  constexpr PlaceStateFact join(PlaceStateFact other) const {
    return PlaceStateFact(static_cast<uint8_t>(m_States | other.m_States),
                          RawBits{});
  }

  constexpr PlaceStateFact &operator|=(PlaceStateFact other) {
    m_States = static_cast<uint8_t>(m_States | other.m_States);
    return *this;
  }

private:
  struct RawBits {};

  constexpr PlaceStateFact(uint8_t states, RawBits)
      : m_States(states & kAllStates) {}

  static constexpr uint8_t kAllStates = placeStateMask(PlaceState::Never) |
                                        placeStateMask(PlaceState::Live) |
                                        placeStateMask(PlaceState::Moved);
  uint8_t m_States = placeStateMask(PlaceState::Live);
};

constexpr PlaceStateFact operator|(PlaceStateFact lhs, PlaceStateFact rhs) {
  return lhs.join(rhs);
}

constexpr bool hasPlaceState(PlaceStateFact states, PlaceState state) {
  return states.contains(state);
}

constexpr bool hasExactlyPlaceState(PlaceStateFact states, PlaceState state) {
  return states.isExactly(state);
}

// The bounded synchronous partial-move capability is an elaborated plan, not
// an alternate source-language permission.  Sema creates it once for an
// eligible local binding; AST/CodeGen carry the same plan to install and
// update the corresponding cleanup mask.
enum class PartialMoveProjectionKind : uint8_t {
  None,
  DirectField,
  FixedArrayElement,
};

class PartialMovePlan {
public:
  constexpr PartialMovePlan() = default;

  static constexpr PartialMovePlan directFields(uint64_t eligibleMask) {
    return make(PartialMoveProjectionKind::DirectField, eligibleMask);
  }

  static constexpr PartialMovePlan fixedArrayElements(uint64_t eligibleMask) {
    return make(PartialMoveProjectionKind::FixedArrayElement, eligibleMask);
  }

  constexpr bool isAdmitted() const {
    return m_Kind != PartialMoveProjectionKind::None && m_EligibleMask != 0;
  }
  constexpr PartialMoveProjectionKind kind() const { return m_Kind; }
  constexpr uint64_t eligibleMask() const { return m_EligibleMask; }
  constexpr bool admits(PartialMoveProjectionKind kind, uint64_t bit) const {
    return bit < 64 && m_Kind == kind &&
           (m_EligibleMask & (1ULL << bit)) != 0;
  }

private:
  constexpr PartialMovePlan(PartialMoveProjectionKind kind,
                            uint64_t eligibleMask)
      : m_Kind(kind), m_EligibleMask(eligibleMask) {}

  static constexpr PartialMovePlan make(PartialMoveProjectionKind kind,
                                        uint64_t eligibleMask) {
    return eligibleMask == 0 ? PartialMovePlan()
                             : PartialMovePlan(kind, eligibleMask);
  }

  PartialMoveProjectionKind m_Kind = PartialMoveProjectionKind::None;
  uint64_t m_EligibleMask = 0;
};

// A bounded ledger for independent direct projections of one stable local.
// It is enabled only for a capability slice that has a fixed <=64 projection
// numbering shared with the legacy liveness and runtime cleanup masks.
class ProjectionPlaceFacts {
public:
  constexpr ProjectionPlaceFacts() = default;

  static constexpr ProjectionPlaceFacts fromLegacyInitMask(uint64_t tracked,
                                                            uint64_t live) {
    return ProjectionPlaceFacts(tracked, tracked & ~live, tracked & live, 0);
  }

  constexpr bool isTracking() const { return m_Tracked != 0; }
  constexpr bool tracks(uint64_t bit) const {
    return bit < 64 && (m_Tracked & (1ULL << bit)) != 0;
  }
  constexpr PlaceStateFact factAt(uint64_t bit) const {
    if (!tracks(bit))
      return PlaceStateFact::bottom();
    const uint64_t mask = 1ULL << bit;
    PlaceStateFact result = PlaceStateFact::bottom();
    if ((m_Never & mask) != 0)
      result |= PlaceStateFact(PlaceState::Never);
    if ((m_Live & mask) != 0)
      result |= PlaceStateFact(PlaceState::Live);
    if ((m_Moved & mask) != 0)
      result |= PlaceStateFact(PlaceState::Moved);
    return result;
  }
  constexpr void markLive(uint64_t bit) { set(bit, PlaceState::Live); }
  constexpr void markMoved(uint64_t bit) { set(bit, PlaceState::Moved); }
  constexpr uint64_t trackedMask() const { return m_Tracked; }
  constexpr uint64_t definitelyLiveMask() const {
    return m_Live & ~(m_Never | m_Moved);
  }
  constexpr uint64_t applyToLegacyInitMask(uint64_t legacy) const {
    return isTracking() ? (legacy & ~m_Tracked) | definitelyLiveMask()
                        : legacy;
  }

  constexpr ProjectionPlaceFacts &operator|=(ProjectionPlaceFacts other) {
    if (!isTracking()) {
      *this = other;
      return *this;
    }
    if (!other.isTracking())
      return *this;
    const uint64_t common = m_Tracked & other.m_Tracked;
    m_Never = (m_Never | other.m_Never) & common;
    m_Live = (m_Live | other.m_Live) & common;
    m_Moved = (m_Moved | other.m_Moved) & common;
    m_Tracked = common;
    return *this;
  }

private:
  constexpr ProjectionPlaceFacts(uint64_t tracked, uint64_t never,
                                 uint64_t live, uint64_t moved)
      : m_Tracked(tracked), m_Never(never), m_Live(live), m_Moved(moved) {}

  constexpr void set(uint64_t bit, PlaceState state) {
    if (!tracks(bit))
      return;
    const uint64_t mask = 1ULL << bit;
    m_Never &= ~mask;
    m_Live &= ~mask;
    m_Moved &= ~mask;
    switch (state) {
    case PlaceState::Never:
      m_Never |= mask;
      break;
    case PlaceState::Live:
      m_Live |= mask;
      break;
    case PlaceState::Moved:
      m_Moved |= mask;
      break;
    }
  }

  uint64_t m_Tracked = 0;
  uint64_t m_Never = 0;
  uint64_t m_Live = 0;
  uint64_t m_Moved = 0;
};

constexpr ProjectionPlaceFacts operator|(ProjectionPlaceFacts lhs,
                                          ProjectionPlaceFacts rhs) {
  lhs |= rhs;
  return lhs;
}

} // namespace toka
