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
#include "toka/AST.h"
#include "toka/DiagnosticEngine.h"
#include "toka/HandleSurfaceStats.h"
#include "toka/SemanticEvidence.h"
#include "toka/Sema.h"
#include "toka/SourceManager.h"
#include "toka/Type.h"
#include <algorithm>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace toka {

static SourceLocation getLoc(ASTNode *Node) { return Node->Loc; }

std::shared_ptr<toka::Type> Sema::checkUnaryExpr(UnaryExpr *Unary) {

  if (auto *todo = dynamic_cast<TodoExpr *>(Unary->RHS.get())) {
    SemanticEvidence::recordTodoGoal(todo->TodoId, TodoGoalStatus::Unsupported,
                                     false, "", "", "", false, false, false,
                                     {}, Unary->Loc);
    error(Unary, DiagID::ERR_TYPED_TODO_UNSUPPORTED_CONTEXT);
    return m_ExpectedType ? m_ExpectedType : toka::Type::fromString("unknown");
  }


  // [FIX] Enforce Hat-on-Member rule (Chain Restrict)
  // `~m.a` translates to Unary(~, MemberExpr(m, a)). This is strictly banned.
  if (Unary->Op == TokenType::Caret || Unary->Op == TokenType::Tilde) {

    if (dynamic_cast<MemberExpr *>(Unary->RHS.get())) {
      error(Unary, DiagID::ERR_SEMA_MORPHOLOGY_SYMBOLS_CANNOT_PREFIX_A_MEMBER);
      return toka::Type::fromString("unknown");
    }
  }

  // [Ch 5] Single Hat Principle: Intermediate paths MUST NOT have
  // morphology sigils
  if (m_InIntermediatePath) {
    if (Unary->Op == TokenType::Star || Unary->Op == TokenType::Caret ||
        Unary->Op == TokenType::Tilde || Unary->Op == TokenType::Ampersand) {
      error(Unary, DiagID::ERR_SEMA_MORPHOLOGY_SYMBOLS_ARE_ONLY_ALLOWED_AT_TH);
    }
    if (Unary->IsRebindable || Unary->HasNull) {
      if (!m_IsMemberBase) {
        error(Unary, DiagID::ERR_SEMA_PERMISSION_SYMBOLS_ARE_ONLY_ALLOWED_AT_TH);
      }
    }
  }

  // [Fix] Disable soul collapse for pointer hats. Identity should be
  // seen.
  bool savedDisable = m_DisableSoulCollapse;
  if (Unary->Op == TokenType::Star || Unary->Op == TokenType::Caret ||
      Unary->Op == TokenType::Tilde || Unary->Op == TokenType::Ampersand) {
    if (dynamic_cast<VariableExpr *>(Unary->RHS.get())) {
      m_DisableSoulCollapse = true;
    }
  }
  bool savedNegativeLiteral = m_CheckingNegativeIntegerLiteral;
  if (Unary->Op == TokenType::Minus &&
      dynamic_cast<NumberExpr *>(Unary->RHS.get())) {
    m_CheckingNegativeIntegerLiteral = true;
  }
  auto rhsType = checkExpr(Unary->RHS.get());
  m_CheckingNegativeIntegerLiteral = savedNegativeLiteral;
  m_DisableSoulCollapse = savedDisable;

  // Assuming checkExpr returns object now.
  if (!rhsType || rhsType->isUnknown())
    return toka::Type::fromString("unknown");

  std::string rhsInfo = rhsType->toString();
  bool isHandleUnary =
      Unary->Op == TokenType::Star || Unary->Op == TokenType::Caret ||
      Unary->Op == TokenType::Ampersand ||
      (Unary->Op == TokenType::Tilde && !rhsType->isInteger());
  recordHandleSurfaceUnaryExpr(*Unary, isHandleUnary);

  // [Toka 1.3] Bitwise NOT (~) and Logical NOT (!) support
  if (Unary->Op == TokenType::Tilde && rhsType->isInteger()) {
      return rhsType; // Bitwise NOT on integer
  }

  if (Unary->Op == TokenType::Bang) {
    if (!rhsType->isBoolean()) {
      error(Unary, DiagID::ERR_SEMA_OPERAND_OF_MUST_BE_BOOL_GOT, rhsInfo);
    }
    return toka::Type::fromString("bool");
  } else if (Unary->Op == TokenType::Minus) {
    bool isNum = rhsType->isInteger() || rhsType->isFloatingPoint();
    if (!isNum) {
      error(Unary, DiagID::ERR_SEMA_OPERAND_OF_MUST_BE_NUMERIC_GOT, rhsInfo);
    }
    return rhsType; // Return object directly
  }

  if (auto *Var = dynamic_cast<VariableExpr *>(Unary->RHS.get())) {
    SymbolInfo *Info = nullptr;
    std::string actualName = Var->Name;
    if (!CurrentScope->findSymbol(actualName, Info)) {
      if (CurrentScope->findSymbol("&" + actualName, Info)) { actualName = "&" + actualName; }
      else if (CurrentScope->findSymbol("*" + actualName, Info)) { actualName = "*" + actualName; }
      else if (CurrentScope->findSymbol("^" + actualName, Info)) { actualName = "^" + actualName; }
      else if (CurrentScope->findSymbol("~" + actualName, Info)) { actualName = "~" + actualName; }
    }

    if (CurrentScope->findSymbol(actualName, Info)) {
      if (Unary->Op == TokenType::Star || Unary->Op == TokenType::Caret ||
          Unary->Op == TokenType::Tilde || Unary->Op == TokenType::Ampersand) {
        Info->HasHandleBeenUsed = true;
        if (m_InLHS || m_IsAssignmentTarget) {
          // A handle rebind is a real handle use, even though it does not
          // imply payload access.
          Info->HasBeenUsed = true;
        }
      }

      // [Fix] Trace to Source for Borrow Registration/Check
      SymbolInfo *EffectiveInfo = Info;
      std::string EffectiveName = actualName;
      std::shared_ptr<toka::Type> physType = Info->TypeObj;
      EffectiveInfo = resolveBorrowSource(EffectiveInfo, EffectiveName);

      // In an assignment target, a handle view must obtain its writability
      // from the bound declaration.  A use-site # only requests that view;
      // it cannot create the capability.
      bool handleViewWritable =
          m_InLHS ? Info->IsHandleRebindable()
                  : (Unary->IsRebindable ||
                     (m_IsAssignmentTarget && Info->IsHandleRebindable()));

      if (Unary->Op == TokenType::Ampersand) {
        auto innerType = rhsType;
        bool handleMutable = handleViewWritable;
        bool soulMutable = rhsType->IsWritable;

        auto refType = std::make_shared<toka::ReferenceType>(innerType);
        refType->IsNullable = Unary->HasNull;
        refType->IsWritable = handleMutable;

        // [Borrow Rule] Exclusive borrow logic refinement:
        // Rebinding (&!/&#) is ALWAYS exclusive.
        // If soul is mutable, it's exclusive ONLY for unique-ownership
        // or value-types. External manage shared pointers (~#) are
        // SHARED borrows of the handle if handleMutable is false.
        bool isExclusive = handleMutable;
        if (soulMutable) {
          // Rule: Shared pointers (~), Raw pointers (*), and References
          // (&) provide Internal Mutability (Aliasing). They are NOT
          // exclusive unless explicitly rebindable (#).
          if (physType &&
              !(physType->isSharedPtr() || physType->isRawPointer() ||
                physType->isReference())) {
            isExclusive = true;
          }
        }
        
        // [Contextual Borrow] Downgrade to shared if context expects read-only
        if (isExclusive && !handleMutable) {
            if (!m_ExpectedWritability) {
                isExclusive = false;
            }
        }

        if (!m_InLHS) {
          std::string pathToBorrow = getPathString(Unary->RHS.get());
          if (!pathToBorrow.empty()) {
             AccessPath sourcePath = makeAccessPath(Unary->RHS.get());
             AccessPath canonicalSourcePath =
                 canonicalizeAccessPath(sourcePath);
             // Toka Path-Anchored Check
             if (!PALCheckerState.recordBorrow(
                     canonicalSourcePath,
                     isExclusive, Unary->Loc)) {
                const auto &conflict = PALCheckerState.lastConflict();
                // Reborrowing an already-held reference is permitted only
                // for that reference's own recorded source path.  It is
                // needed for returning a view bound by a mutable pattern;
                // unrelated overlapping borrows remain rejected.
                if (!conflict ||
                    !isBorrowAccessAuthorized(sourcePath, conflict->Path)) {
                  error(Unary, DiagID::ERR_BORROW_MUT, pathToBorrow);
                }
                if (conflict &&
                    !isBorrowAccessAuthorized(sourcePath, conflict->Path)) {
                  recordPALConflict(
                      Unary,
                      isExclusive
                          ? PALOperationClass::ExclusivePayloadBorrow
                          : PALOperationClass::SharedPayloadBorrow,
                      canonicalSourcePath, *conflict);
                }
             }
             m_LastBorrowSource = pathToBorrow; // keep this so RHS knows what it borrowed
          }
        }

        return refType;
      }

      if (Unary->Op == TokenType::Star) {
        if (physType && physType->isRawPointer()) {
          return physType->withAttributes(
              handleViewWritable,
              Unary->HasNull || physType->IsNullable);
        }
        // [New] Array-to-Pointer Decay for Variable Elevation
        if (physType && physType->isArray()) {
          auto arr = std::dynamic_pointer_cast<toka::ArrayType>(physType);
          auto res = std::make_shared<toka::RawPointerType>(arr->ElementType);
          res->IsWritable = Unary->IsRebindable;
          res->IsNullable = Unary->HasNull;
          return res;
        }
        auto res = std::make_shared<toka::RawPointerType>(rhsType);
        res->IsWritable = handleViewWritable;
        res->IsNullable = Unary->HasNull;
        return res;
      }

      if (Unary->Op == TokenType::Caret) {
        if (!m_InLHS) {
          std::string pathToBorrow = getPathString(Unary->RHS.get());
          if (!pathToBorrow.empty()) {
             auto conflict = PALCheckerState.verifyInvalidation(
                 canonicalizeAccessPath(makeAccessPath(Unary->RHS.get())));
             if (conflict) {
                 error(Unary, DiagID::ERR_MOVE_BORROWED,
                       conflict->displayPath());
                 recordPALConflict(
                     Unary, PALOperationClass::Invalidation,
                     canonicalizeAccessPath(makeAccessPath(Unary->RHS.get())),
                     *conflict);
             }
          }
        }
        if (physType && physType->isUniquePtr()) {
          return physType->withAttributes(
              handleViewWritable,
              Unary->HasNull || physType->IsNullable);
        }
        auto res = std::make_shared<toka::UniquePointerType>(rhsType);
        res->IsWritable = handleViewWritable;
        res->IsNullable = Unary->HasNull;
        return res;
      }

      if (Unary->Op == TokenType::Tilde) {
        if (!m_InLHS) {
          std::string pathToBorrow = getPathString(Unary->RHS.get());
          if (!pathToBorrow.empty()) {
             auto conflict = PALCheckerState.verifyAccess(
                 canonicalizeAccessPath(makeAccessPath(Unary->RHS.get())));
             if (conflict) {
                 error(Unary, DiagID::ERR_BORROW_MUT,
                       conflict->displayPath());
                 recordPALConflict(
                     Unary, PALOperationClass::SharedPayloadBorrow,
                     canonicalizeAccessPath(makeAccessPath(Unary->RHS.get())),
                     *conflict);
             }
          }
        }
        if (physType && physType->isSharedPtr()) {
          return physType->withAttributes(
              handleViewWritable,
              Unary->HasNull || physType->IsNullable);
        }
        auto res = std::make_shared<toka::SharedPointerType>(rhsType);
        res->IsWritable = handleViewWritable;
        res->IsNullable = Unary->HasNull;
        return res;
      }
    }
  }

  // Fallback for non-variable expressions or other op types
  std::shared_ptr<toka::Type> inner = rhsType;
  if (Unary->Op == TokenType::Star) {
    // Identity (*)
    if (rhsType->isArray()) {
      // Decay Array to Pointer-to-Element
      auto arr = std::dynamic_pointer_cast<toka::ArrayType>(rhsType);
      inner = arr->ElementType;
    }
    if (!inner)
      inner = rhsType;
  }
  if (Unary->Op == TokenType::Ampersand) {
    auto refType = std::make_shared<toka::ReferenceType>(inner);
    refType->IsNullable = Unary->HasNull;
    refType->IsWritable = Unary->IsRebindable;
    
    bool isExclusive = Unary->IsRebindable;
    if (inner->IsWritable && !(inner->isSharedPtr() || inner->isRawPointer() || inner->isReference())) {
      if (m_ExpectedWritability) {
          isExclusive = true;
      }
    }

    // remove debug

    if (!m_InLHS) {
      std::string pathToBorrow = getPathString(Unary->RHS.get());
      if (!pathToBorrow.empty()) {
         if (!PALCheckerState.recordBorrow(
                 canonicalizeAccessPath(makeAccessPath(Unary->RHS.get())),
                 isExclusive, Unary->Loc)) {
             error(Unary, DiagID::ERR_BORROW_MUT, pathToBorrow);
             if (PALCheckerState.lastConflict()) {
               recordPALConflict(
                   Unary,
                   isExclusive
                       ? PALOperationClass::ExclusivePayloadBorrow
                       : PALOperationClass::SharedPayloadBorrow,
                   canonicalizeAccessPath(makeAccessPath(Unary->RHS.get())),
                   *PALCheckerState.lastConflict());
             }
         }
         m_LastBorrowSource = pathToBorrow;
      }
    }
    return refType;
  }

  if (Unary->Op == TokenType::Caret) {
    if (!m_InLHS) {
      std::string pathToBorrow = getPathString(Unary->RHS.get());
      if (!pathToBorrow.empty()) {
         auto conflict = PALCheckerState.verifyInvalidation(
             canonicalizeAccessPath(makeAccessPath(Unary->RHS.get())));
         if (conflict) {
             error(Unary, DiagID::ERR_MOVE_BORROWED,
                   conflict->displayPath());
             recordPALConflict(
                 Unary, PALOperationClass::Invalidation,
                 canonicalizeAccessPath(makeAccessPath(Unary->RHS.get())),
                 *conflict);
         }
      }
    }
    auto res = std::make_shared<toka::UniquePointerType>(inner);
    res->IsWritable = Unary->IsRebindable || (m_IsAssignmentTarget && inner->IsWritable);
    res->IsNullable = Unary->HasNull;
    return res;
  }

  if (Unary->Op == TokenType::Tilde) {
    if (!m_InLHS) {
      std::string pathToBorrow = getPathString(Unary->RHS.get());
      if (!pathToBorrow.empty()) {
         auto conflict = PALCheckerState.verifyAccess(
             canonicalizeAccessPath(makeAccessPath(Unary->RHS.get())));
         if (conflict) {
             error(Unary, DiagID::ERR_BORROW_MUT,
                   conflict->displayPath());
             recordPALConflict(
                 Unary, PALOperationClass::SharedPayloadBorrow,
                 canonicalizeAccessPath(makeAccessPath(Unary->RHS.get())),
                 *conflict);
         }
      }
    }
    auto res = std::make_shared<toka::SharedPointerType>(inner);
    res->IsWritable = Unary->IsRebindable || (m_IsAssignmentTarget && inner->IsWritable);
    res->IsNullable = Unary->HasNull;
    return res;
  }

  if (Unary->Op == TokenType::Star) {
    auto rawPtr = std::make_shared<toka::RawPointerType>(inner);
    rawPtr->IsNullable = Unary->HasNull;
    rawPtr->IsWritable = Unary->IsRebindable || (m_IsAssignmentTarget && inner->IsWritable);
    return rawPtr;
  }

  if (Unary->Op == TokenType::PlusPlus || Unary->Op == TokenType::MinusMinus) {
    if (!rhsType->isInteger()) {
      error(Unary, DiagID::ERR_OPERAND_TYPE_MISMATCH, "++/--", "integer",
            rhsInfo);
    }
    if (auto *Var = dynamic_cast<VariableExpr *>(Unary->RHS.get())) {
      SymbolInfo *Info = nullptr;
      if (CurrentScope->findSymbol(Var->Name, Info)) {
        std::string pathToBorrow = getPathString(Unary->RHS.get());
        if (!pathToBorrow.empty()) {
           auto conflict = PALCheckerState.verifyPayloadWrite(
               canonicalizeAccessPath(makeAccessPath(Unary->RHS.get())));
           if (conflict) {
               error(Unary, DiagID::ERR_BORROW_MUT,
                     conflict->displayPath());
               recordPALConflict(
                   Unary, PALOperationClass::PayloadWrite,
                   canonicalizeAccessPath(makeAccessPath(Unary->RHS.get())),
                   *conflict);
           }
        }
      }
    }
    return rhsType;
  }
  return rhsType;
}

} // namespace toka
