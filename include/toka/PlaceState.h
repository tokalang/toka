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

} // namespace toka
