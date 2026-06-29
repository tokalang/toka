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

static std::string getStringifyPath(Expr *E) {
  if (!E)
    return "";
  if (auto *ve = dynamic_cast<VariableExpr *>(E)) {
    return ve->Name;
  }
  if (auto *me = dynamic_cast<MemberExpr *>(E)) {
    std::string member = toka::Type::stripMorphology(me->Member);
    return getStringifyPath(me->Object.get()) + "." + member;
  }
  if (auto *ue = dynamic_cast<UnaryExpr *>(E)) {
    return getStringifyPath(ue->RHS.get());
  }
  if (auto *ae = dynamic_cast<AddressOfExpr *>(E)) {
    return getStringifyPath(ae->Expression.get());
  }
  if (auto *ae = dynamic_cast<ArrayIndexExpr *>(E)) {
    return getStringifyPath(ae->Array.get());
  }
  if (auto *ce = dynamic_cast<CastExpr *>(E)) {
    return getStringifyPath(ce->Expression.get());
  }
  return "";
}

std::shared_ptr<toka::Type> Sema::checkUnaryExpr(UnaryExpr *Unary) {


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
  auto rhsType = checkExpr(Unary->RHS.get());
  m_DisableSoulCollapse = savedDisable;

  // Assuming checkExpr returns object now.
  if (!rhsType || rhsType->isUnknown())
    return toka::Type::fromString("unknown");

  std::string rhsInfo = rhsType->toString();

  // [Toka 1.3] Bitwise NOT (~) and Logical NOT (!) support
  if (Unary->Op == TokenType::Tilde && rhsType->isInteger()) {
      return rhsType; // Bitwise NOT on integer
  }
  if (Unary->Op == TokenType::KwBnot) {
      if (!rhsType->isInteger()) {
          error(Unary, DiagID::ERR_OPERAND_TYPE_MISMATCH, "bnot", "integer", rhsInfo);
      }
      return rhsType;
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
      }

      // [Fix] Trace to Source for Borrow Registration/Check
      SymbolInfo *EffectiveInfo = Info;
      std::string EffectiveName = actualName;
      std::shared_ptr<toka::Type> physType = Info->TypeObj;
      int traceDepth = 0;
      while (!EffectiveInfo->BorrowedFrom.empty() && traceDepth < 10) {
        SymbolInfo *Next = nullptr;
        if (CurrentScope->findSymbol(EffectiveInfo->BorrowedFrom, Next)) {
          EffectiveName = EffectiveInfo->BorrowedFrom;
          EffectiveInfo = Next;
        } else
          break;
        traceDepth++;
      }

      if (Unary->Op == TokenType::Ampersand) {
        auto innerType = rhsType;
        bool handleMutable = Unary->IsRebindable;
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
          std::string pathToBorrow = getStringifyPath(Unary->RHS.get());
          if (!pathToBorrow.empty()) {
             // Toka Path-Anchored Check
             if (!PALCheckerState.recordBorrow(pathToBorrow, isExclusive)) {
                error(Unary, DiagID::ERR_BORROW_MUT, pathToBorrow);
             }
             m_LastBorrowSource = pathToBorrow; // keep this so RHS knows what it borrowed
          }
        }

        return refType;
      }

      if (Unary->Op == TokenType::Star) {
        if (physType && physType->isRawPointer()) {
          return physType->withAttributes(
              Unary->IsRebindable ||
                  (m_IsAssignmentTarget && Info->IsRebindable),
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
        res->IsWritable =
            Unary->IsRebindable || (m_IsAssignmentTarget && Info->IsRebindable);
        res->IsNullable = Unary->HasNull;
        return res;
      }

      if (Unary->Op == TokenType::Caret) {
        if (!m_InLHS) {
          std::string pathToBorrow = getStringifyPath(Unary->RHS.get());
          if (!pathToBorrow.empty()) {
             std::string conflictPath = PALCheckerState.verifyMutation(pathToBorrow);
             if (!conflictPath.empty()) {
                 error(Unary, DiagID::ERR_MOVE_BORROWED, conflictPath);
             }
          }
        }
        if (physType && physType->isUniquePtr()) {
          return physType->withAttributes(
              Unary->IsRebindable ||
                  (m_IsAssignmentTarget && Info->IsRebindable),
              Unary->HasNull || physType->IsNullable);
        }
        auto res = std::make_shared<toka::UniquePointerType>(rhsType);
        res->IsWritable =
            Unary->IsRebindable || (m_IsAssignmentTarget && Info->IsRebindable);
        res->IsNullable = Unary->HasNull;
        return res;
      }

      if (Unary->Op == TokenType::Tilde) {
        if (!m_InLHS) {
          std::string pathToBorrow = getStringifyPath(Unary->RHS.get());
          if (!pathToBorrow.empty()) {
             std::string conflictPath = PALCheckerState.verifyAccess(pathToBorrow);
             if (!conflictPath.empty()) {
                 error(Unary, DiagID::ERR_BORROW_MUT, conflictPath);
             }
          }
        }
        if (physType && physType->isSharedPtr()) {
          return physType->withAttributes(
              Unary->IsRebindable ||
                  (m_IsAssignmentTarget && Info->IsRebindable),
              Unary->HasNull || physType->IsNullable);
        }
        auto res = std::make_shared<toka::SharedPointerType>(rhsType);
        res->IsWritable =
            Unary->IsRebindable || (m_IsAssignmentTarget && Info->IsRebindable);
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
      std::string pathToBorrow = getStringifyPath(Unary->RHS.get());
      if (!pathToBorrow.empty()) {
         if (!PALCheckerState.recordBorrow(pathToBorrow, isExclusive)) {
             error(Unary, DiagID::ERR_BORROW_MUT, pathToBorrow);
         }
         m_LastBorrowSource = pathToBorrow;
      }
    }
    return refType;
  }

  if (Unary->Op == TokenType::Caret) {
    if (!m_InLHS) {
      std::string pathToBorrow = getStringifyPath(Unary->RHS.get());
      if (!pathToBorrow.empty()) {
         std::string conflictPath = PALCheckerState.verifyMutation(pathToBorrow);
         if (!conflictPath.empty()) {
             error(Unary, DiagID::ERR_MOVE_BORROWED, conflictPath);
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
      std::string pathToBorrow = getStringifyPath(Unary->RHS.get());
      if (!pathToBorrow.empty()) {
         std::string conflictPath = PALCheckerState.verifyAccess(pathToBorrow);
         if (!conflictPath.empty()) {
             error(Unary, DiagID::ERR_BORROW_MUT, conflictPath);
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
        std::string pathToBorrow = getStringifyPath(Unary->RHS.get());
        if (!pathToBorrow.empty()) {
           std::string conflictPath = PALCheckerState.verifyMutation(pathToBorrow);
           if (!conflictPath.empty()) {
               error(Unary, DiagID::ERR_BORROW_MUT, conflictPath);
           }
        }
      }
    }
    return rhsType;
  }
  return rhsType;
}

} // namespace toka
