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

bool PALChecker::isPrefixOf(const std::string& prefix, const std::string& full) const {
  if (prefix == full) return true;
  if (full.length() > prefix.length() && 
      full.substr(0, prefix.length()) == prefix && 
      full[prefix.length()] == '.') {
    return true;
  }
  return false;
}

bool PALChecker::isSuffixDerived(const std::string& base, const std::string& derived) const {
  // Same logic as prefix overlap in our path model: derived is a sub-path of base
  return isPrefixOf(base, derived);
}

bool PALChecker::recordBorrow(const std::string& path, bool isMutable) {
  if (!IsEnabled) return true;

  auto& map = LedgerStack.back().Map;
  
  if (map.count(path)) {
     if (map[path] == PathState::BorrowedMut || isMutable) {
        return false; // Error: Already mutably borrowed, or want mutable and already borrowed
     }
  }
  
  map[path] = isMutable ? PathState::BorrowedMut : PathState::BorrowedShared;
  TransientBorrows.push_back(path);
  return true;
}

bool PALChecker::upgradeBorrow(const std::string& path) {
  if (!IsEnabled) return true;
  
  if (!LedgerStack.empty()) {
      auto& map = LedgerStack.back().Map;
      if (map.count(path) && map[path] == PathState::BorrowedShared) {
          map[path] = PathState::BorrowedMut;
          return true;
      }
  }
  return false;
}

std::string PALChecker::verifyMutation(const std::string& path) {
  if (!IsEnabled) return "";

  for (auto it = LedgerStack.rbegin(); it != LedgerStack.rend(); ++it) {
    for (const auto& [regPath, state] : it->Map) {
      if (state == PathState::BorrowedMut || state == PathState::BorrowedShared) {
        if (isPrefixOf(regPath, path) || isPrefixOf(path, regPath)) {
          return regPath;
        }
      }
    }
  }
  return "";
}

std::string PALChecker::verifyAccess(const std::string& path) {
  if (!IsEnabled) return "";

  for (auto it = LedgerStack.rbegin(); it != LedgerStack.rend(); ++it) {
    for (const auto& [regPath, state] : it->Map) {
      if (state == PathState::BorrowedMut) {
        if (isPrefixOf(regPath, path) || isPrefixOf(path, regPath)) {
          return regPath;
        }
      }
    }
  }
  return "";
}

bool PALChecker::operationRequiresExclusive(PALOperationClass op) const {
  return op == PALOperationClass::ExclusivePayloadBorrow ||
         op == PALOperationClass::Invalidation;
}

bool PALChecker::operationsConflict(PALOperationClass lhs,
                                    PALOperationClass rhs) const {
  return operationRequiresExclusive(lhs) || operationRequiresExclusive(rhs);
}

bool PALChecker::pathsOverlap(const std::string& lhs,
                              const std::string& rhs) const {
  return isPrefixOf(lhs, rhs) || isPrefixOf(rhs, lhs);
}

std::string PALChecker::verifyOperation(const std::string& path,
                                        PALOperationClass op) {
  return operationRequiresExclusive(op) ? verifyMutation(path)
                                        : verifyAccess(path);
}

PathState PALChecker::getState(const std::string& path) {
  if (!IsEnabled) return PathState::Free;
  for (auto it = LedgerStack.rbegin(); it != LedgerStack.rend(); ++it) {
    if (it->Map.count(path)) {
      return it->Map[path];
    }
  }
  return PathState::Free;
}

void PALChecker::commitTransient(const std::string& path) {
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

bool PALChecker::revokeRoot(const std::string& rootIdentifier) {
  if (!IsEnabled) return false;

  bool revoked = false;
  // If the root identifier is reassigned/dropped, ALL paths dependent on it must be killed.
  for (auto it = LedgerStack.rbegin(); it != LedgerStack.rend(); ++it) {
    auto& map = it->Map;
    // Collect paths to erase, though typically if we hit this, it's an error.
    for (auto mapIt = map.begin(); mapIt != map.end(); ) {
      if ((mapIt->second == PathState::BorrowedMut || mapIt->second == PathState::BorrowedShared) && isPrefixOf(rootIdentifier, mapIt->first)) {
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

void PALChecker::markMoved(const std::string& path) {
  if (!IsEnabled) return;

  for (auto it = LedgerStack.rbegin(); it != LedgerStack.rend(); ++it) {
    auto& map = it->Map;
    for (auto mapIt = map.begin(); mapIt != map.end(); ) {
      if (isPrefixOf(path, mapIt->first)) {
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
    for (const auto& [path, state] : second.LedgerStack[i].Map) {
      auto found = dst.find(path);
      if (found == dst.end()) {
        dst[path] = state;
      } else {
        found->second = mergeState(found->second, state);
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
