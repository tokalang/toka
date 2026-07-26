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

#include "toka/AccessPath.h"
#include "toka/AST.h"
#include "toka/DiagnosticEngine.h"
#include <string>
#include <map>
#include <optional>
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
  HandleRebind,
  ExclusiveMutation,
  Invalidation
};

struct PALConflict {
  AccessPath Path;
  PathState State = PathState::Free;
  SourceLocation OriginLoc;

  std::string displayPath() const { return Path.toLegacyString(); }
};

/// Toka's PAL (Path-Anchored Ledger) System
/// 
/// PAL is the official identifier for Toka's Borrow Checker mechanism. 
/// It enforces memory safety and resource aliasing rules at compile time
/// using structured access paths in a transient lexical ledger stack.
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
  bool recordBorrow(const AccessPath &path, bool isMutable = false,
                    SourceLocation originLoc = {});

  // Verifies if a path can be exclusively mutated
  std::optional<PALConflict> verifyMutation(const AccessPath &path);
  std::optional<PALConflict> verifyPayloadWrite(const AccessPath &path);
  std::optional<PALConflict> verifyExclusiveMutation(const AccessPath &path);
  std::optional<PALConflict> verifyInvalidation(const AccessPath &path);

  // Verifies if a path can be accessed (read)
  std::optional<PALConflict> verifyAccess(const AccessPath &path);

  // Verifies whether an operation class can be applied at a path.
  std::optional<PALConflict> verifyOperation(const AccessPath &path,
                                             PALOperationClass op);
  bool operationRequiresExclusive(PALOperationClass op) const;
  bool operationsConflict(PALOperationClass lhs, PALOperationClass rhs) const;
  AccessPathOverlap classifyOverlap(const AccessPath &lhs,
                                    const AccessPath &rhs) const;
  bool pathsOverlap(const AccessPath &lhs, const AccessPath &rhs) const;
  const std::optional<PALConflict> &lastConflict() const {
    return LastConflict;
  }

  // Registers an upgrade of a previously shared borrow to mutable
  bool upgradeBorrow(const AccessPath &path);

  // Gets the exact state of a path
  PathState getState(const AccessPath &path);

  // Commits a specific transient borrow so it persists until the scope ends
  void commitTransient(const AccessPath &path);

  // Releases an exact borrow introduced by a compiler-managed lexical value.
  void releaseBorrow(const AccessPath &path);

  // Clears all uncommitted transient borrows (called at statement boundaries)
  void clearTransient();

  // Revoke all borrows derived from a specific root identifier (e.g. `buf` is reassigned)
  // Returns true if an active borrow was revoked (which usually indicates an error should be thrown)
  bool revokeRoot(const AccessPath &root);

  // Clear tracking for a variable that has moved
  void markMoved(const AccessPath &path);

  // Snapshot helpers for local control-flow analysis.
  PALChecker snapshot() const;
  void restore(const PALChecker& snapshot);
  void mergeBranches(const PALChecker& base,
                     const PALChecker& first,
                     bool firstReachable,
                     const PALChecker& second,
                     bool secondReachable);

private:
  struct LedgerEntry {
    PathState State = PathState::Free;
    SourceLocation OriginLoc;
  };

  struct LedgerScope {
    std::map<AccessPath, LedgerEntry> Map;
  };
  std::vector<LedgerScope> LedgerStack;
  std::vector<AccessPath> TransientBorrows;
  std::optional<PALConflict> LastConflict;
};

} // namespace toka
