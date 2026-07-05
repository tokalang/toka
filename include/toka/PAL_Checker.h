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
#pragma once

#include "toka/AST.h"
#include "toka/DiagnosticEngine.h"
#include <string>
#include <map>
#include <vector>

namespace toka {

// Represents the state of a path in the Lexical Ledger
enum class PathState {
  Free,
  BorrowedShared,  // Immutable borrow (&)
  BorrowedMut      // Mutable/Exclusive borrow (&#)
};

enum class PALOperationClass {
  PayloadWrite,
  SharedPayloadBorrow,
  ExclusivePayloadBorrow,
  HandleViewBorrow,
  ExclusiveMutation,
  Invalidation
};

/// Toka's PAL (Path-Anchored Ledger) System
/// 
/// PAL is the official identifier for Toka's Borrow Checker mechanism. 
/// It enforces memory safety and resource aliasing rules at compile time 
/// uniformly utilizing an AST path-string based transient-lexical ledger stack.
class PALChecker {
public:
  bool IsEnabled = true;

  PALChecker() {
    // Top level global scope
    pushScope();
  }

  void pushScope() {
    LedgerStack.push_back({});
  }

  void popScope() {
    if (!LedgerStack.empty()) {
      LedgerStack.pop_back();
    }
  }

  // Marks a specific path as degraded
  // isMutable parameter determines exclusivity
  bool recordBorrow(const std::string& path, bool isMutable = false);

  // Verifies if a path can be exclusively mutated
  std::string verifyMutation(const std::string& path);
  std::string verifyPayloadWrite(const std::string& path);
  std::string verifyExclusiveMutation(const std::string& path);
  std::string verifyInvalidation(const std::string& path);

  // Verifies if a path can be accessed (read)
  std::string verifyAccess(const std::string& path);

  // Verifies whether an operation class can be applied at a path.
  std::string verifyOperation(const std::string& path, PALOperationClass op);
  bool operationRequiresExclusive(PALOperationClass op) const;
  bool operationsConflict(PALOperationClass lhs, PALOperationClass rhs) const;
  bool pathsOverlap(const std::string& lhs, const std::string& rhs) const;

  // Registers an upgrade of a previously shared borrow to mutable
  bool upgradeBorrow(const std::string& path);

  // Gets the exact state of a path
  PathState getState(const std::string& path);

  // Commits a specific transient borrow so it persists until the scope ends
  void commitTransient(const std::string& path);

  // Clears all uncommitted transient borrows (called at statement boundaries)
  void clearTransient();

  // Revoke all borrows derived from a specific root identifier (e.g. `buf` is reassigned)
  // Returns true if an active borrow was revoked (which usually indicates an error should be thrown)
  bool revokeRoot(const std::string& rootIdentifier);

  // Clear tracking for a variable that has moved
  void markMoved(const std::string& path);

  // Snapshot helpers for local control-flow analysis.
  PALChecker snapshot() const;
  void restore(const PALChecker& snapshot);
  void mergeBranches(const PALChecker& base,
                     const PALChecker& first,
                     bool firstReachable,
                     const PALChecker& second,
                     bool secondReachable);

private:
  struct LedgerScope {
    std::map<std::string, PathState> Map;
  };
  std::vector<LedgerScope> LedgerStack;
  std::vector<std::string> TransientBorrows;

  // Helper to check prefix overlap
  bool isPrefixOf(const std::string& prefix, const std::string& full) const;
  bool isSuffixDerived(const std::string& base, const std::string& derived) const;
};

} // namespace toka
