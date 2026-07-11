// Copyright (c) 2025 YiZhonghua<zhyi@dpai.com>. All rights reserved.
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

#include "toka/PAL_Checker.h"
#include <algorithm>

namespace toka {

bool PALChecker::recordBorrow(const AccessPath &path, bool isMutable,
                              SourceLocation originLoc) {
  if (!IsEnabled) return true;
  LastConflict.reset();

  for (auto it = LedgerStack.rbegin(); it != LedgerStack.rend(); ++it) {
    for (const auto& [regPath, entry] : it->Map) {
       if (!pathsOverlap(regPath, path))
         continue;
       if (entry.State == PathState::BorrowedMut || isMutable) {
          LastConflict = PALConflict{regPath, entry.State, entry.OriginLoc};
          return false; // Error: Already mutably borrowed, or want mutable and already borrowed
       }
    }
  }
  
  auto& map = LedgerStack.back().Map;
  map[path] = {isMutable ? PathState::BorrowedMut
                         : PathState::BorrowedShared,
               originLoc};
  TransientBorrows.push_back(path);
  return true;
}

bool PALChecker::upgradeBorrow(const AccessPath &path) {
  if (!IsEnabled) return true;
  
  if (!LedgerStack.empty()) {
      auto& map = LedgerStack.back().Map;
      if (map.count(path) &&
          map[path].State == PathState::BorrowedShared) {
          map[path].State = PathState::BorrowedMut;
          return true;
      }
  }
  return false;
}

std::optional<PALConflict>
PALChecker::verifyMutation(const AccessPath &path) {
  return verifyExclusiveMutation(path);
}

std::optional<PALConflict>
PALChecker::verifyExclusiveMutation(const AccessPath &path) {
  if (!IsEnabled) return std::nullopt;

  for (auto it = LedgerStack.rbegin(); it != LedgerStack.rend(); ++it) {
    for (const auto& [regPath, entry] : it->Map) {
      if (entry.State == PathState::BorrowedMut ||
          entry.State == PathState::BorrowedShared) {
        if (pathsOverlap(regPath, path)) {
          return PALConflict{regPath, entry.State, entry.OriginLoc};
        }
      }
    }
  }
  return std::nullopt;
}

std::optional<PALConflict>
PALChecker::verifyAccess(const AccessPath &path) {
  if (!IsEnabled) return std::nullopt;

  for (auto it = LedgerStack.rbegin(); it != LedgerStack.rend(); ++it) {
    for (const auto& [regPath, entry] : it->Map) {
      if (entry.State == PathState::BorrowedMut) {
        if (pathsOverlap(regPath, path)) {
          return PALConflict{regPath, entry.State, entry.OriginLoc};
        }
      }
    }
  }
  return std::nullopt;
}

std::optional<PALConflict>
PALChecker::verifyPayloadWrite(const AccessPath &path) {
  if (!IsEnabled) return std::nullopt;

  for (auto it = LedgerStack.rbegin(); it != LedgerStack.rend(); ++it) {
    for (const auto& [regPath, entry] : it->Map) {
      if (entry.State == PathState::BorrowedMut) {
        if (pathsOverlap(regPath, path)) {
          return PALConflict{regPath, entry.State, entry.OriginLoc};
        }
      }
      if (entry.State == PathState::BorrowedShared && !(regPath == path) &&
          accessPathIsLegacyPrefix(path, regPath)) {
        return PALConflict{regPath, entry.State, entry.OriginLoc};
      }
    }
  }
  return std::nullopt;
}

std::optional<PALConflict>
PALChecker::verifyInvalidation(const AccessPath &path) {
  return verifyExclusiveMutation(path);
}

bool PALChecker::operationRequiresExclusive(PALOperationClass op) const {
  return op == PALOperationClass::ExclusivePayloadBorrow ||
         op == PALOperationClass::ExclusiveMutation ||
         op == PALOperationClass::Invalidation;
}

bool PALChecker::operationsConflict(PALOperationClass lhs,
                                    PALOperationClass rhs) const {
  return operationRequiresExclusive(lhs) || operationRequiresExclusive(rhs);
}

AccessPathOverlap PALChecker::classifyOverlap(const AccessPath &lhs,
                                              const AccessPath &rhs) const {
  return classifyAccessPathOverlap(lhs, rhs);
}

bool PALChecker::pathsOverlap(const AccessPath &lhs,
                              const AccessPath &rhs) const {
  return accessPathsMayOverlap(lhs, rhs);
}

std::optional<PALConflict>
PALChecker::verifyOperation(const AccessPath &path, PALOperationClass op) {
  if (op == PALOperationClass::PayloadWrite)
    return verifyPayloadWrite(path);
  if (op == PALOperationClass::Invalidation)
    return verifyInvalidation(path);
  if (operationRequiresExclusive(op))
    return verifyExclusiveMutation(path);
  return verifyAccess(path);
}

PathState PALChecker::getState(const AccessPath &path) {
  if (!IsEnabled) return PathState::Free;
  for (auto it = LedgerStack.rbegin(); it != LedgerStack.rend(); ++it) {
    if (it->Map.count(path)) {
      return it->Map[path].State;
    }
  }
  return PathState::Free;
}

void PALChecker::commitTransient(const AccessPath &path) {
  auto it = std::find(TransientBorrows.begin(), TransientBorrows.end(), path);
  if (it != TransientBorrows.end()) {
      TransientBorrows.erase(it);
  }
}

void PALChecker::clearTransient() {
  if (LedgerStack.empty()) return;
  auto& map = LedgerStack.back().Map;
  for (const auto& path : TransientBorrows) {
      map.erase(path);
  }
  TransientBorrows.clear();
}

bool PALChecker::revokeRoot(const AccessPath &root) {
  if (!IsEnabled) return false;

  bool revoked = false;
  // If the root identifier is reassigned/dropped, ALL paths dependent on it must be killed.
  for (auto it = LedgerStack.rbegin(); it != LedgerStack.rend(); ++it) {
    auto& map = it->Map;
    // Collect paths to erase, though typically if we hit this, it's an error.
    for (auto mapIt = map.begin(); mapIt != map.end(); ) {
      if ((mapIt->second.State == PathState::BorrowedMut ||
           mapIt->second.State == PathState::BorrowedShared) &&
          accessPathIsLegacyPrefix(root, mapIt->first)) {
        revoked = true;
        // In a strict static analyzer, this triggers ERR_MOVE_BORROWED.
        // We will just report the conflict via our verifyMutation, or the caller can throw.
        mapIt = map.erase(mapIt);
      } else {
        ++mapIt;
      }
    }
  }
  return revoked;
}

void PALChecker::markMoved(const AccessPath &path) {
  if (!IsEnabled) return;

  for (auto it = LedgerStack.rbegin(); it != LedgerStack.rend(); ++it) {
    auto& map = it->Map;
    for (auto mapIt = map.begin(); mapIt != map.end(); ) {
      if (accessPathIsLegacyPrefix(path, mapIt->first)) {
         mapIt = map.erase(mapIt);
      } else {
        ++mapIt;
      }
    }
  }
}

PALChecker PALChecker::snapshot() const {
  return *this;
}

void PALChecker::restore(const PALChecker& snapshot) {
  *this = snapshot;
}

void PALChecker::mergeBranches(const PALChecker& base,
                               const PALChecker& first,
                               bool firstReachable,
                               const PALChecker& second,
                               bool secondReachable) {
  if (!firstReachable && !secondReachable) {
    restore(base);
    return;
  }
  if (firstReachable && !secondReachable) {
    restore(first);
    return;
  }
  if (!firstReachable && secondReachable) {
    restore(second);
    return;
  }

  restore(first);

  auto mergeState = [](PathState lhs, PathState rhs) {
    if (lhs == PathState::BorrowedMut || rhs == PathState::BorrowedMut)
      return PathState::BorrowedMut;
    if (lhs == PathState::BorrowedShared || rhs == PathState::BorrowedShared)
      return PathState::BorrowedShared;
    return PathState::Free;
  };

  if (LedgerStack.size() < second.LedgerStack.size()) {
    LedgerStack.resize(second.LedgerStack.size());
  }

  for (size_t i = 0; i < second.LedgerStack.size(); ++i) {
    auto& dst = LedgerStack[i].Map;
    for (const auto& [path, entry] : second.LedgerStack[i].Map) {
      auto found = dst.find(path);
      if (found == dst.end()) {
        dst[path] = entry;
      } else {
        PathState merged = mergeState(found->second.State, entry.State);
        if (merged == entry.State && merged != found->second.State)
          found->second.OriginLoc = entry.OriginLoc;
        found->second.State = merged;
      }
    }
  }

  for (const auto& path : second.TransientBorrows) {
    if (std::find(TransientBorrows.begin(), TransientBorrows.end(), path) ==
        TransientBorrows.end()) {
      TransientBorrows.push_back(path);
    }
  }
}

} // namespace toka
