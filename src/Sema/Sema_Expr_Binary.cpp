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
#include "toka/AssignmentStats.h"
#include "toka/DiagnosticEngine.h"
#include "toka/Sema.h"
#include "toka/SourceManager.h"
#include "toka/Type.h"
#include <algorithm>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace toka {

static SourceLocation getLoc(ASTNode *Node) { return Node->Loc; }

static bool containsMemberExpr(const Expr *E) {
  if (!E)
    return false;
  if (auto *M = dynamic_cast<const MemberExpr *>(E))
    return true;
  if (auto *U = dynamic_cast<const UnaryExpr *>(E))
    return containsMemberExpr(U->RHS.get());
  if (auto *A = dynamic_cast<const AddressOfExpr *>(E))
    return containsMemberExpr(A->Expression.get());
  if (auto *A = dynamic_cast<const ArrayIndexExpr *>(E)) {
    if (containsMemberExpr(A->Array.get()))
      return true;
    for (const auto &Index : A->Indices) {
      if (containsMemberExpr(Index.get()))
        return true;
    }
    return false;
  }
  if (auto *C = dynamic_cast<const CastExpr *>(E))
    return containsMemberExpr(C->Expression.get());
  if (auto *P = dynamic_cast<const PostfixExpr *>(E))
    return containsMemberExpr(P->LHS.get());
  return false;
}

static const MemberExpr *getTerminalMemberExpr(const Expr *E) {
  if (!E)
    return nullptr;
  if (auto *M = dynamic_cast<const MemberExpr *>(E))
    return M;
  if (auto *U = dynamic_cast<const UnaryExpr *>(E))
    return getTerminalMemberExpr(U->RHS.get());
  if (auto *A = dynamic_cast<const AddressOfExpr *>(E))
    return getTerminalMemberExpr(A->Expression.get());
  if (auto *C = dynamic_cast<const CastExpr *>(E))
    return getTerminalMemberExpr(C->Expression.get());
  if (auto *P = dynamic_cast<const PostfixExpr *>(E))
    return getTerminalMemberExpr(P->LHS.get());
  return nullptr;
}

static bool terminalMemberHasMorphology(const Expr *E) {
  const MemberExpr *M = getTerminalMemberExpr(E);
  return M && toka::Type::stripMorphology(M->Member) != M->Member;
}

// An explicit ^, ~, or & on an assignment target selects the binding
// identity.  It must not be silently reinterpreted as an implicit payload
// assignment merely because the RHS happens to match the pointee type.
static bool selectsHandleIdentity(const Expr *E) {
  while (E) {
    if (auto *C = dynamic_cast<const CastExpr *>(E)) {
      E = C->Expression.get();
    } else if (auto *P = dynamic_cast<const PostfixExpr *>(E)) {
      E = P->LHS.get();
    } else {
      break;
    }
  }

  if (auto *M = dynamic_cast<const MemberExpr *>(E))
    return Type::stripMorphology(M->Member) != M->Member;

  auto *U = dynamic_cast<const UnaryExpr *>(E);
  return U && (U->Op == TokenType::Caret || U->Op == TokenType::Tilde ||
               U->Op == TokenType::Ampersand);
}

// Conditional todo state is associated only with a whole local binding.
// A member, index, or raw dereference needs a path-sensitive fact model and
// remains outside this first assignment-flow slice.
static const VariableExpr *directBindingAssignmentTarget(const Expr *expr) {
  while (expr) {
    if (auto *cast = dynamic_cast<const CastExpr *>(expr)) {
      expr = cast->Expression.get();
    } else if (auto *postfix = dynamic_cast<const PostfixExpr *>(expr)) {
      expr = postfix->LHS.get();
    } else if (auto *unary = dynamic_cast<const UnaryExpr *>(expr)) {
      if (unary->Op != TokenType::Caret && unary->Op != TokenType::Tilde &&
          unary->Op != TokenType::Ampersand)
        return nullptr;
      expr = unary->RHS.get();
    } else {
      break;
    }
  }
  return dynamic_cast<const VariableExpr *>(expr);
}

// An unsafe container implementation may state its own loop-bound invariant
// for an affine dynamic index.  Keep the recognized form deliberately narrow:
// anything outside `index`, `index + N`, or `index - N` remains unknown and
// therefore overlaps for a cede transfer.
static bool getSimpleAffineIndex(const Expr *expr, std::string &base,
                                 int64_t &offset) {
  if (auto *variable = dynamic_cast<const VariableExpr *>(expr)) {
    base = Type::stripMorphology(variable->Name);
    offset = 0;
    return true;
  }

  auto *binary = dynamic_cast<const BinaryExpr *>(expr);
  if (!binary || (binary->Op != "+" && binary->Op != "-"))
    return false;

  auto *variable = dynamic_cast<const VariableExpr *>(binary->LHS.get());
  auto *number = dynamic_cast<const NumberExpr *>(binary->RHS.get());
  if (!variable || !number ||
      number->Value > static_cast<uint64_t>(std::numeric_limits<int64_t>::max()))
    return false;

  base = Type::stripMorphology(variable->Name);
  offset = static_cast<int64_t>(number->Value);
  if (binary->Op == "-")
    offset = -offset;
  return true;
}

static bool proveDistinctArrayElements(const ArrayIndexExpr *destination,
                                       const ArrayIndexExpr *source) {
  if (!destination || !source ||
      destination->Indices.size() != source->Indices.size())
    return false;

  for (size_t i = 0; i < destination->Indices.size(); ++i) {
    std::string destinationBase;
    std::string sourceBase;
    int64_t destinationOffset = 0;
    int64_t sourceOffset = 0;
    if (getSimpleAffineIndex(destination->Indices[i].get(), destinationBase,
                             destinationOffset) &&
        getSimpleAffineIndex(source->Indices[i].get(), sourceBase,
                             sourceOffset) &&
        destinationBase == sourceBase &&
        destinationOffset != sourceOffset)
      return true;
  }
  return false;
}

// Stage 5: Object-Oriented Binary Expression Check
std::shared_ptr<toka::Type> Sema::checkBinaryExpr(BinaryExpr *Bin) {
  struct SuffixGuard {
    bool &flag;
    bool oldVal;
    SuffixGuard(bool &f, bool newVal) : flag(f), oldVal(f) { flag = newVal; }
    ~SuffixGuard() { flag = oldVal; }
  } suffixGuard(m_AllowPermissionSuffix, false);

  bool isAssign = (Bin->Op == "=" || Bin->Op == "+=" || Bin->Op == "-=" ||
                   Bin->Op == "*=" || Bin->Op == "/=" || Bin->Op == "%=");
  if (Bin->Op == "is" && dynamic_cast<UnsetExpr *>(Bin->RHS.get())) {
    auto *target = dynamic_cast<VariableExpr *>(Bin->LHS.get());
    SymbolInfo *targetInfo = nullptr;
    const bool isOwningMaybeTarget =
        m_ExpectedInitStatePredicate == Bin && target &&
        !target->IsRawPointer && !target->IsUnique && !target->IsShared &&
        !target->IsValueMutable && !target->IsValueNullable &&
        !target->IsValueBlocked && !m_InitBlockContexts.empty() &&
        m_InitBlockContexts.back().PlaceName == target->Name &&
        CurrentScope->findSymbol(target->Name, targetInfo) && targetInfo &&
        targetInfo->IsDeclaredVariable &&
        hasPlaceState(targetInfo->placeFact(), PlaceState::Never) &&
        hasPlaceState(targetInfo->placeFact(), PlaceState::Live) &&
        !hasPlaceState(targetInfo->placeFact(), PlaceState::Moved);
    if (!isOwningMaybeTarget) {
      error(Bin, DiagID::ERR_INIT_STATE_PREDICATE,
            target ? target->Name : "<place>",
            !m_InitBlockContexts.empty()
                ? m_InitBlockContexts.back().PlaceName
                : "<place>");
    } else {
      Bin->IsInitStatePredicate = true;
    }
    return toka::Type::fromString("bool");
  }
  // Normal assignment preserves RHS-first analysis because the RHS can carry
  // a borrow or transfer.  A todo carries neither, so it is the one case
  // where we may first inspect the LHS solely to supply its complete target
  // contract.
  const bool rhsIsTodo = isAssign &&
                         dynamic_cast<TodoExpr *>(Bin->RHS.get()) != nullptr;

  // 1. Resolve Operands using New API
  // [Toka 1.3] Evaluation Order: Check RHS first to avoid LHS
  // borrows/moves blocking RHS usage (e.g. &#cursor = cursor.&next)
  Bin->RHS = foldGenericConstant(std::move(Bin->RHS));

  // A source-invalidating cede or direct unique transfer into existing storage
  // must establish canonical disjointness before either side is checked:
  // checking the RHS first would mark the source moved before discovering a self- or prefix
  // overlap. Fresh bindings are handled by declaration checking and do not
  // enter this assignment path.
  if (isAssign) {
    Expr *transferSource = nullptr;
    if (auto *cede = dynamic_cast<CedeExpr *>(Bin->RHS.get())) {
      transferSource = cede->Value.get();
    } else if (auto *unary = dynamic_cast<UnaryExpr *>(Bin->RHS.get());
               unary && unary->Op == TokenType::Caret) {
      transferSource = unary->RHS.get();
    }
    if (transferSource) {
      const AccessPath destination =
          canonicalizeAccessPath(makeAccessPath(Bin->LHS.get()));
      const AccessPath source =
          canonicalizeAccessPath(makeAccessPath(transferSource));
      const auto *destinationIndex =
          dynamic_cast<const ArrayIndexExpr *>(Bin->LHS.get());
      const auto *sourceIndex =
          dynamic_cast<const ArrayIndexExpr *>(transferSource);
      const AccessPath destinationIndexedBase = destinationIndex
          ? canonicalizeAccessPath(makeAccessPath(destinationIndex->Array.get()))
          : AccessPath{};
      const AccessPath sourceIndexedBase = sourceIndex
          ? canonicalizeAccessPath(makeAccessPath(sourceIndex->Array.get()))
          : AccessPath{};
      const bool hasSameIndexedBase =
          destinationIndex && sourceIndex &&
          destinationIndexedBase && sourceIndexedBase &&
          destinationIndexedBase == sourceIndexedBase;
      // Integer overflow and slice bounds are runtime concerns, so this is
      // not a safe-language alias proof.  It is only the narrow handoff
      // admitted within an explicit unsafe container invariant (for example
      // Vec's bounded shift loop).
      const bool unsafeAffineHandoff =
          m_InUnsafeContext && hasSameIndexedBase &&
          proveDistinctArrayElements(destinationIndex, sourceIndex);
      if (destination && source && !unsafeAffineHandoff &&
          classifyAccessPathOverlap(destination, source) !=
              AccessPathOverlap::NoOverlap) {
        error(Bin, DiagID::ERR_SEMA_CEDE_OVERLAPPING_DESTINATION,
              source.toDebugString(), destination.toDebugString());
        return toka::Type::fromString("unknown");
      }
    }
  }
  m_LastBorrowSource = ""; // [NEW] Clear stale borrow source
  std::shared_ptr<toka::Type> rhsType;
  if (!rhsIsTodo)
    rhsType = checkExpr(Bin->RHS.get());
  std::string rhsBorrowSource = ""; 
  if (!rhsIsTodo && !getPathString(Bin->RHS.get()).empty()) {
      rhsBorrowSource = m_LastBorrowSource;
  }

  bool oldLHS = m_InLHS;
  m_InLHS = isAssign;

  if (isAssign) {
    // [Toka 1.3] Identify potential borrower name (LHS)
    std::string lhsName = "";
    Expr *curr = Bin->LHS.get();
    while (auto *C = dynamic_cast<CastExpr *>(curr))
      curr = C->Expression.get();
    if (auto *V = dynamic_cast<VariableExpr *>(curr)) {
      lhsName = V->Name;
    } else if (auto *U = dynamic_cast<UnaryExpr *>(curr)) {
      if (U->Op == TokenType::Ampersand) { // &cursor = ...
        if (auto *RV = dynamic_cast<VariableExpr *>(U->RHS.get()))
          lhsName = RV->Name;
      }
    }
    m_ControlFlowStack.push_back({lhsName, NoProducedValue, nullptr, false, false});
  }

  if (!isAssign)
    Bin->LHS = foldGenericConstant(std::move(Bin->LHS));

  bool oldTarget = m_IsAssignmentTarget;
  if (isAssign) {
    m_IsAssignmentTarget = true;
  }

  auto lhsType = checkExpr(Bin->LHS.get());
  if (isAssign)
    m_ControlFlowStack.pop_back();
  m_InLHS = oldLHS;
  m_IsAssignmentTarget = oldTarget;

  if (rhsIsTodo)
    rhsType = checkExpr(Bin->RHS.get(), lhsType);

  if (isAssign && lhsType && lhsType->IsWritable && !rhsBorrowSource.empty()) {
      if (!PALCheckerState.upgradeBorrow(
              canonicalizeAccessPath(makeAccessPath(rhsBorrowSource)))) {
          error(Bin, DiagID::ERR_BORROW_MUT, rhsBorrowSource);
      }
  }

  if (!lhsType || !rhsType)
    return toka::Type::fromString("unknown");

  std::string LHS = lhsType->toString(); // For error messages
  std::string RHS = rhsType->toString();

  if (LHS == "unknown" || RHS == "unknown" || 
      LHS.find("Unresolved") == 0 || RHS.find("Unresolved") == 0) {
    return toka::Type::fromString("unknown");
  }

  // [Optimization] Literal Adaptation
  // Allow mixed comparison like (i64 < 2) by auto-casting the literal
  // to the explicit type.
  Expr *lhsExpr = Bin->LHS.get();
  Expr *rhsExpr = Bin->RHS.get();

  auto integerLiteral = [](Expr *expr) -> NumberExpr * {
    if (auto *number = dynamic_cast<NumberExpr *>(expr))
      return number;
    if (auto *unary = dynamic_cast<UnaryExpr *>(expr)) {
      if (unary->Op == TokenType::Minus)
        return dynamic_cast<NumberExpr *>(unary->RHS.get());
    }
    return nullptr;
  };
  auto adaptIntegerLiteral = [&](Expr *expr, NumberExpr *number,
                                 const std::shared_ptr<toka::Type> &type) {
    auto *unary = dynamic_cast<UnaryExpr *>(expr);
    const bool isNegative = unary && unary->Op == TokenType::Minus;
    validateIntegerLiteralRange(expr, number, type, isNegative);
    expr->ResolvedType = type;
    number->ResolvedType = type;
  };

  auto *lhsNum = integerLiteral(lhsExpr);
  auto *rhsNum = integerLiteral(rhsExpr);

  if (resolveType(lhsType, true)->isInteger() && rhsNum && !lhsNum) {
    // Left is Strong Integer, Right is Literal -> Adapt Right
    adaptIntegerLiteral(Bin->RHS.get(), rhsNum, lhsType);
    rhsType = lhsType;
    RHS = rhsType->toString();
  } else if (resolveType(rhsType, true)->isInteger() && lhsNum && !rhsNum) {
    // Right is Strong Integer, Left is Literal -> Adapt Left
    adaptIntegerLiteral(Bin->LHS.get(), lhsNum, rhsType);
    lhsType = rhsType;
    LHS = lhsType->toString();
  }

  // Generic Implicit Dereference for Smart Pointers (Soul Interaction)
  // If one side is Smart Pointer and other side matches its Pointee,
  // decay Smart Pointer.
  const bool explicitHandleTarget =
      isAssign && selectsHandleIdentity(Bin->LHS.get());
  bool assignmentLHSWasSmartPointerPayloadDecay = false;
  if (!explicitHandleTarget &&
      (lhsType->isUniquePtr() || lhsType->isSharedPtr())) {
    if (auto inner = lhsType->getPointeeType()) {
      if (isTypeCompatible(inner, rhsType)) {
        assignmentLHSWasSmartPointerPayloadDecay = isAssign;
        lhsType = inner;
        Bin->LHS->ResolvedType = lhsType; // PERSIST for CodeGen
        LHS = lhsType->toString();
      }
    }
  } else if (rhsType->isUniquePtr() || rhsType->isSharedPtr()) {
    if (auto inner = rhsType->getPointeeType()) {
      if (isTypeCompatible(lhsType, inner)) {
        rhsType = inner;
        Bin->RHS->ResolvedType = rhsType; // PERSIST for CodeGen
        RHS = rhsType->toString();
      }
    }
  }
  bool isRefAssign = false;
  bool isUnsetInit = false;
  const bool isExplicitInit = Bin->IsInitialization;
  auto *initTarget = dynamic_cast<VariableExpr *>(Bin->LHS.get());
  SymbolInfo *initTargetInfo = nullptr;
  const bool isWholePlainLocal =
      initTarget && !initTarget->IsRawPointer && !initTarget->IsUnique &&
      !initTarget->IsShared && !initTarget->IsValueMutable &&
      !initTarget->IsValueNullable && !initTarget->IsValueBlocked &&
      CurrentScope->findSymbol(initTarget->Name, initTargetInfo) &&
      initTargetInfo && initTargetInfo->IsDeclaredVariable &&
      !initTargetInfo->IsDeclaredMutable;
  if (isExplicitInit) {
    isUnsetInit = true;
    if (!isWholePlainLocal ||
        !hasExactlyPlaceState(initTargetInfo->placeFact(),
                              PlaceState::Never)) {
      error(Bin, DiagID::ERR_INIT_REQUIRES_UNINITIALIZED,
            initTarget ? initTarget->Name : "<place>");
    } else {
      // Construction consumes the place authority even though it is not an
      // ordinary value read; it should satisfy the binding-use diagnostic.
      initTargetInfo->HasBeenUsed = true;
    }
  } else if (isAssign) {
    VariableExpr *rootLHSVar = nullptr;
    Expr *currLHS = Bin->LHS.get();
    bool isProjectionLHS = false;
    while (currLHS) {
      if (auto *V = dynamic_cast<VariableExpr *>(currLHS)) {
        rootLHSVar = V;
        break;
      } else if (auto *M = dynamic_cast<MemberExpr *>(currLHS)) {
        isProjectionLHS = true;
        currLHS = M->Object.get();
      } else if (auto *I = dynamic_cast<ArrayIndexExpr *>(currLHS)) {
        isProjectionLHS = true;
        currLHS = I->Array.get();
      } else if (auto *U = dynamic_cast<UnaryExpr *>(currLHS)) {
        currLHS = U->RHS.get();
      } else if (auto *C = dynamic_cast<CastExpr *>(currLHS)) {
        currLHS = C->Expression.get();
      } else {
        break;
      }
    }
    SymbolInfo *rootLHSInfo = nullptr;
    std::string actualRootLHSName;
    if (rootLHSVar) {
      CurrentScope->findVariableWithDeref(rootLHSVar->Name, rootLHSInfo, actualRootLHSName);
    }
    if (rootLHSInfo && !rootLHSInfo->IsReference()) {
      if (hasPlaceState(rootLHSInfo->placeFact(), PlaceState::Never)) {
        error(Bin, DiagID::ERR_INIT_REQUIRES_EXPLICIT, rootLHSVar->Name,
              rootLHSVar->Name);
        isUnsetInit = true;
      } else if (isProjectionLHS && hasExactlyPlaceState(rootLHSInfo->placeFact(), PlaceState::Moved)) {
        error(Bin, DiagID::ERR_INIT_REQUIRES_UNINITIALIZED, rootLHSVar->Name);
        isUnsetInit = true;
      }
    }
  }
  if (m_IsUnsetInitCall) {
    isRefAssign = true;
    isUnsetInit = true;
    m_IsUnsetInitCall = false;
  }
  if (isAssign && !explicitHandleTarget &&
      !assignmentLHSWasSmartPointerPayloadDecay) {
    Expr *NakedLHS = Bin->LHS.get();
    while (auto *C = dynamic_cast<CastExpr *>(NakedLHS))
      NakedLHS = C->Expression.get();
    if (auto *V = dynamic_cast<VariableExpr *>(NakedLHS)) {
      if (!V->IsRawPointer && !V->IsUnique && !V->IsShared) {
        SymbolInfo *InfoPtr = nullptr;
        std::string actualName = V->Name;
        if (CurrentScope->findVariableWithDeref(V->Name, InfoPtr,
                                                actualName) &&
            InfoPtr && InfoPtr->TypeObj && InfoPtr->TypeObj->isSmartPointer()) {
          auto inner = InfoPtr->TypeObj->getPointeeType();
          if (inner && isTypeCompatible(inner, lhsType) &&
              isTypeCompatible(inner, rhsType)) {
            assignmentLHSWasSmartPointerPayloadDecay = true;
          }
        }
      }
    }
  }

  // Rebinding Logic: Unwrap &ref on LHS
  if (Bin->Op == "=") {
    if (auto *UnLHS = dynamic_cast<UnaryExpr *>(Bin->LHS.get())) {
      if (UnLHS->Op == TokenType::Ampersand) {
        if (lhsType->isReference() && lhsType->getPointeeType() &&
            lhsType->getPointeeType()->isReference()) {
          lhsType = lhsType->getPointeeType();
        }
      }
    }
  }

  // Assignment Logic
  if (Bin->Op == "=") {
    // Refresh the cached right-hand-side type before ownership analysis.
    rhsType = Bin->RHS->ResolvedType;
    rhsExpr = Bin->RHS.get();
    RHS = rhsType->toString();

    // Move Logic
    Expr *RHSExpr = Bin->RHS.get();
    if (auto *Unary = dynamic_cast<UnaryExpr *>(RHSExpr)) {
      if (Unary->Op == TokenType::Caret)
        RHSExpr = Unary->RHS.get();
    }

    if (auto *RHSVar = dynamic_cast<VariableExpr *>(RHSExpr)) {
      SymbolInfo *RHSInfoPtr = nullptr;
      std::string actualRHSName = RHSVar->Name;
      if (CurrentScope->findVariableWithDeref(RHSVar->Name, RHSInfoPtr, actualRHSName) &&
          RHSInfoPtr->IsUnique()) {
        auto conflict = PALCheckerState.verifyInvalidation(
            canonicalizeAccessPath(makeAccessPath(actualRHSName)));
        if (conflict) {
            error(Bin, DiagID::ERR_MOVE_BORROWED, conflict->displayPath());
            recordPALConflict(
                Bin, PALOperationClass::Invalidation,
                canonicalizeAccessPath(makeAccessPath(actualRHSName)),
                *conflict);
        }
        CurrentScope->markMoved(actualRHSName, Bin->Loc);
      }
    }
    
    Expr *RHSScan = RHSExpr;
    while (true) {
        if (auto *un = dynamic_cast<UnaryExpr *>(RHSScan)) {
            RHSScan = un->RHS.get();
        } else if (auto *ce = dynamic_cast<CedeExpr *>(RHSScan)) {
            RHSScan = ce->Value.get();
        } else {
            break;
        }
    }

    // A cede expression validates direct field transfer itself, including
    // the user-drop restriction.  Do not report that same restriction again
    // from the enclosing assignment.
    if (!dynamic_cast<CedeExpr *>(RHSExpr) &&
        dynamic_cast<MemberExpr *>(RHSScan)) {
      auto *Memb = static_cast<MemberExpr *>(RHSScan);
      // [Move Restriction Rule] Prohibit moving member out of shape
      // that has drop() Rule applies if we are moving any resource
      bool memberIsResource = rhsType->isUniquePtr();
      if (!memberIsResource && rhsType->isShape()) {
          std::string rhsSoul = toka::Type::stripMorphology(rhsType->getSoulName());
          if (hasDrop(rhsSoul)) {
              memberIsResource = true;
          }
      }

      if (memberIsResource) {
        auto objType = checkExpr(Memb->Object.get());
        std::shared_ptr<toka::Type> soulType = objType->getSoulType();
        std::string soul = toka::Type::stripMorphology(soulType->getSoulName());
        if (hasDrop(soul)) {
          error(Bin, DiagID::ERR_MOVE_MEMBER_DROP, Memb->Member, soul);
          HasError = true;
        }
      }
    }

    // [New] Assign to VariableExpr: Clear Moved State
    Expr *LHSScan = Bin->LHS.get();
    while (auto *un = dynamic_cast<UnaryExpr *>(LHSScan)) {
      LHSScan = un->RHS.get();
    }
    if (auto *LHSVar = dynamic_cast<VariableExpr *>(LHSScan)) {
      std::string actualLHSName = LHSVar->Name;
      SymbolInfo *LHSInfoPtr = nullptr;
      CurrentScope->findVariableWithDeref(LHSVar->Name, LHSInfoPtr, actualLHSName);
      CurrentScope->resetMoved(actualLHSName);
    }

    // Reference Assignment
    // [Constitution] Reference Rebinding Detection
    // If the LHS resolved to a Reference type (due to explicit hatted
    // syntax
    // &#z), then it's a rebinding. If it collapsed to the Soul
    // (Hat-Off), it's Soul modification.
    if (lhsType->isReference()) {
      isRefAssign = true;
    }
  }

  if (isAssign) {
    // Smart Pointer NewExpr Special Case
    bool isSmartNew = false;
    bool isImplicitDerefAssign = assignmentLHSWasSmartPointerPayloadDecay;

    if (dynamic_cast<NewExpr *>(Bin->RHS.get())) {
      if (lhsType->isUniquePtr() || lhsType->isSharedPtr()) {
        auto inner = lhsType->getPointeeType();
        auto rhsInner = rhsType->getPointeeType(); // new returns ^T
        if (inner && rhsInner && isTypeCompatible(inner, rhsInner)) {
          isSmartNew = true;
        }
      }
    }

    // Implicit Dereference Assignment Logic (Soul Mutation)
    if (!isImplicitDerefAssign && !explicitHandleTarget && !isSmartNew &&
        !isRefAssign &&
        (lhsType->isUniquePtr() || lhsType->isSharedPtr())) {
      auto inner = lhsType->getPointeeType();
      // If RHS matches Inner, we are assigning to the Soul (implicit *s
      // = val)
      if (inner && isTypeCompatible(inner, rhsType)) {
        lhsType = inner; // Decay to Pointee Type for Writability Check
        isImplicitDerefAssign = true;
      }
    }

    // [Constitution 1.3] Covenant-based Writability Check
    bool isLHSWritable = false;
    bool isRebind = false;

    // Detect Rebind: Assigning to the pointer variable/handle itself
    // e.g. "p = ..." where p is smart pointer, or "~#p = ..."
    // Constitution 1.3: Reference rebinding is ALSO a Reseat operation.
    if (!isImplicitDerefAssign) {
      if (explicitHandleTarget || lhsType->isPointer() ||
          lhsType->isSmartPointer() || isRefAssign) {
        isRebind = true;
      }
    }

    AssignmentSemanticKind assignmentKind =
        AssignmentSemanticKind::Unclassified;
    if (!isImplicitDerefAssign && !isRebind && !isRefAssign) {
      assignmentKind = AssignmentSemanticKind::Payload;
    }
    if (isImplicitDerefAssign) {
      assignmentKind = AssignmentSemanticKind::Payload;
    }
    if (isRebind || isRefAssign) {
      assignmentKind = AssignmentSemanticKind::Handle;
    }
    if (Bin->Op != "=") {
      assignmentKind = AssignmentSemanticKind::ResidualCompound;
    }
    Bin->AssignmentKind = assignmentKind;

    if (assignmentStatsEnabled()) {
      AssignmentStats &stats = assignmentStats();
      bool isCompound = Bin->Op != "=";
      bool isMemberLHS = containsMemberExpr(Bin->LHS.get());
      AssignmentFrontendEvidence evidence =
          AssignmentFrontendEvidence::Unclassified;
      stats.TotalAssignmentSites++;
      if (!isCompound && !isImplicitDerefAssign &&
          assignmentKind == AssignmentSemanticKind::Payload) {
        stats.PlainPayloadAssignments++;
        evidence = AssignmentFrontendEvidence::Payload;
      }
      if (isImplicitDerefAssign) {
        stats.ImplicitDerefPayloadAssignments++;
        evidence = AssignmentFrontendEvidence::Payload;
      }
      if (assignmentKind == AssignmentSemanticKind::Handle && !isRefAssign) {
        stats.HandleRebindings++;
        evidence = AssignmentFrontendEvidence::Handle;
      }
      if (isRefAssign) {
        stats.ReferenceRebindings++;
        evidence = AssignmentFrontendEvidence::Handle;
      }
      if (isMemberLHS)
        stats.MemberLHSAssignments++;
      if (terminalMemberHasMorphology(Bin->LHS.get()))
        stats.TerminalMemberMorphologyAssignments++;
      if (isCompound) {
        stats.CompoundAssignments++;
        evidence = AssignmentFrontendEvidence::ResidualCompound;
      }
      recordAssignmentFrontendEvidence(Bin, evidence);
    }

    Expr *Traverse = Bin->LHS.get();
    while (true) {
      if (auto *M = dynamic_cast<MemberExpr *>(Traverse)) {
        Traverse = M->Object.get();
      } else if (auto *Idx = dynamic_cast<ArrayIndexExpr *>(Traverse)) {
        Traverse = Idx->Array.get();
      } else if (auto *Un = dynamic_cast<UnaryExpr *>(Traverse)) {
        Traverse = Un->RHS.get();
      } else {
        break;
      }
    }

    if (auto *Var = dynamic_cast<VariableExpr *>(Traverse)) {
      SymbolInfo *InfoPtr = nullptr;
      std::string actualName = Var->Name;
      if (CurrentScope->findVariableWithDeref(Var->Name, InfoPtr, actualName)) {
        AccessPath lhsPath =
            canonicalizeAccessPath(makeAccessPath(Bin->LHS.get()));
        if (!isUnsetInit && lhsPath) {
            PALOperationClass lhsOperation =
                (isRebind || isRefAssign) ? PALOperationClass::HandleRebind
                                          : PALOperationClass::PayloadWrite;
            auto conflict = PALCheckerState.verifyOperation(lhsPath, lhsOperation);
            bool authorized = false;
            
            if (conflict) {
               const std::string conflictPath = conflict->displayPath();
               SymbolInfo info;
               std::string borrower = "";
               if (!m_ControlFlowStack.empty())
                 borrower = m_ControlFlowStack.back().Label;
                 
               if (CurrentScope->lookup(actualName, info) &&
                   ((!info.BorrowedPath.empty() &&
                     canonicalizeAccessPath(info.BorrowedPath) ==
                         canonicalizeAccessPath(conflict->Path)) ||
                    info.BorrowedFrom == conflictPath)) {
                  authorized = true;
               } else if (!borrower.empty()) {
                   SymbolInfo borrowerInfo;
                   if (CurrentScope->lookup(borrower, borrowerInfo) &&
                       ((!borrowerInfo.BorrowedPath.empty() &&
                         canonicalizeAccessPath(borrowerInfo.BorrowedPath) ==
                             canonicalizeAccessPath(conflict->Path)) ||
                        borrowerInfo.BorrowedFrom == conflictPath)) {
                     authorized = true;
                   } else if (actualName == borrower) {
                      authorized = true;
                   }
               }
            }
            
            if (conflict && !authorized) {
                if (conflict->State == toka::PathState::BorrowedShared) {
                    error(Bin, DiagID::ERR_BORROW_IMMUT,
                          conflict->displayPath());
                } else {
                    error(Bin, DiagID::ERR_BORROW_MUT,
                          conflict->displayPath());
                }
                recordPALConflict(Bin, lhsOperation, lhsPath, *conflict);
            }
        }

        bool isWholeLHS = false;
        Expr *NakedLHS = Bin->LHS.get();
        while (auto *C = dynamic_cast<CastExpr *>(NakedLHS))
          NakedLHS = C->Expression.get();
        if (dynamic_cast<VariableExpr *>(NakedLHS))
          isWholeLHS = true;

        if (InfoPtr->IsReference()) {
          if (InfoPtr->DirtyReferentMask != ~0ULL)
            isLHSWritable = true;
        } else if (isWholeLHS) {
          if (hasExactlyPlaceState(InfoPtr->placeFact(), PlaceState::Moved))
            isLHSWritable = true;
        } else if (auto *M = dynamic_cast<MemberExpr *>(NakedLHS)) {
          if (hasExactlyPlaceState(InfoPtr->placeFact(), PlaceState::Live) &&
              InfoPtr->TypeObj && InfoPtr->TypeObj->isShape()) {
            auto shapeType = std::dynamic_pointer_cast<ShapeType>(InfoPtr->TypeObj);
            ShapeDecl *SD = shapeType ? shapeType->Decl : nullptr;
            if (!SD)
              SD = findVisibleShapeDecl(InfoPtr->TypeObj->getSoulName(), getLoc(M));
            if (SD) {
              for (int i = 0; i < (int)SD->Members.size(); ++i) {
                if (toka::Type::stripMorphology(SD->Members[i].Name) ==
                    toka::Type::stripMorphology(M->Member)) {
                  if (InfoPtr->partialMovePlan().admits(PartialMoveProjectionKind::DirectField, i)) {
                    if (hasExactlyPlaceState(
                            InfoPtr->ExactPlace.projectionFact(
                                PartialMoveProjectionKind::DirectField, i),
                            PlaceState::Moved)) {
                      isLHSWritable = true;
                    }
                  }
                  break;
                }
              }
            }
          }
        } else if (auto *Idx = dynamic_cast<ArrayIndexExpr *>(NakedLHS)) {
          if (hasExactlyPlaceState(InfoPtr->placeFact(), PlaceState::Live) &&
              Idx->Indices.size() == 1) {
            if (auto *constant = dynamic_cast<NumberExpr *>(Idx->Indices[0].get())) {
              if (InfoPtr->partialMovePlan().admits(
                      PartialMoveProjectionKind::FixedArrayElement, constant->Value)) {
                if (hasExactlyPlaceState(
                        InfoPtr->ExactPlace.projectionFact(
                            PartialMoveProjectionKind::FixedArrayElement, constant->Value),
                        PlaceState::Moved)) {
                  isLHSWritable = true;
                }
              }
            }
          }
        }
        
        // This assignment reaches a payload path.  Handle rebindability
        // (for example ^#p) is not payload write permission.
        if (InfoPtr->IsSoulMutable()) {
            isLHSWritable = true;
        }
      }
    }

    // [Fix] Global Permission Check: If Sema says it's writable, it's
    // writable
    if (lhsType->IsWritable) {
      isLHSWritable = true;
    } else if (auto *Post = dynamic_cast<PostfixExpr *>(Traverse)) {
      if (Post->Op == TokenType::TokenWrite || Post->Op == TokenType::Bang) {
        isLHSWritable = true;
      }
    } else if (auto *Un = dynamic_cast<UnaryExpr *>(Traverse)) {
      // [Constitution] Explicit Rebind/Access check
      if (Un->Op == TokenType::Star) { // *p
        isLHSWritable = true; // Pointer deref allows mutation if target
                              // type allows (checked below via Soul)
      } else if (Un->Op == TokenType::Caret || Un->Op == TokenType::Tilde ||
                 Un->Op == TokenType::Ampersand ||
                 Un->Op == TokenType::Star) { // ^p, ~p, &p, *p
        // These are Rebindable handles or access handles.
        // If they are on the LHS without a deref, they are rebinds.
        if (isRebind) {
          // Check if the UnaryExpr itself carries the rebind intent (#
          // or
          // !)
          if (Un->IsRebindable) {
            isLHSWritable = true;
          }
        } else {
          isLHSWritable = true; // Soul view
        }
      }
    }

    if (isUnsetInit)
      isLHSWritable = true;

    // A declaration may request payload writability, but a shared view may
    // only preserve or restrict the capability of its direct source.  This
    // is independent of the physical type's writable marker, which is still
    // needed for ABI and overload compatibility.
    AccessCapability lhsCapability = getAccessCapability(Bin->LHS.get());
    if (isRebind && lhsCapability.HandleRebindable)
      isLHSWritable = true;
    bool payloadCapabilityDenied =
        assignmentKind == AssignmentSemanticKind::Payload &&
        !lhsCapability.PayloadWritable && !isUnsetInit;
    bool handleCapabilityDenied =
        assignmentKind == AssignmentSemanticKind::Handle &&
        !lhsCapability.HandleRebindable;
    bool payloadFlowDenied =
        assignmentKind == AssignmentSemanticKind::Payload &&
        lhsCapability.PayloadFlowRestricted &&
        !lhsCapability.PayloadWritable && !isUnsetInit;
    if (payloadFlowDenied) {
      error(Bin->LHS.get(),
            DiagID::ERR_SEMA_COVENANT_VIOLATION_CANNOT_ELEVATE_WRITE_P);
      HasError = true;
    }

    if (!isLHSWritable && !payloadFlowDenied && !handleCapabilityDenied) {
      error(Bin, DiagID::ERR_SEMA_CANNOT_ASSIGN_TO_IMMUTABLE_ENTITY_MISSING);
      HasError = true;
    }

    // [Constitution] Soul Permission Elevation Audit
    // RHS soul must not exceed LHS soul's permissions if they share
    // objects (Shared/Ref)
    if (!isRebind && (lhsType->isSharedPtr() || lhsType->isReference())) {
      auto lhsSoul = lhsType->getPointeeType();
      auto rhsSoul = rhsType->getPointeeType();
      if (lhsSoul && rhsSoul) {
        // Check suffix: if RHS soul is Immutable ($/?) and LHS soul is
        // Writable
        // (#/!) -> Error
        if (!rhsSoul->IsWritable && lhsSoul->IsWritable) {
          // [Identity Exemption] Fresh allocations from 'new' can
          // satisfy writable souls.
          if (dynamic_cast<NewExpr *>(Bin->RHS.get())) {
            // OK: Freshly baked bread is always warm.
          } else {
            error(Bin, DiagID::ERR_SEMA_COVENANT_VIOLATION_CANNOT_ELEVATE_WRITE_P);
            HasError = true;
          }
        }
      }
    }

    // [FIX] Unset Safety: Allow writing to immutable fields if they are
    // uninitialized [FIX] Uninit Safety: Allow writing to immutable fields if
    // they are uninitialized
    bool isLHSUnset = false;
    if (auto *M = dynamic_cast<MemberExpr *>(Bin->LHS.get())) {
      Expr *Traverse = M->Object.get();
      while (auto *InnerM = dynamic_cast<MemberExpr *>(Traverse))
        Traverse = InnerM->Object.get();

      if (auto *ObjVar = dynamic_cast<VariableExpr *>(Traverse)) {
        SymbolInfo *ObjInfo = nullptr;
        if (CurrentScope->findSymbol(ObjVar->Name, ObjInfo)) {
          // Follow BorrowedFrom chain to find the actual shape instance
          SymbolInfo *EffectiveInfo = ObjInfo;
          int depth = 0;
          while (!EffectiveInfo->BorrowedFrom.empty() && depth < 10) {
            SymbolInfo *Next = nullptr;
            if (CurrentScope->findSymbol(EffectiveInfo->BorrowedFrom, Next))
              EffectiveInfo = Next;
            else
              break;
            depth++;
          }

          std::shared_ptr<toka::Type> actualType = EffectiveInfo->TypeObj;
          while (actualType &&
                 (actualType->isReference() || actualType->isPointer())) {
            actualType = actualType->getPointeeType();
          }

          if (actualType && actualType->isShape()) {
            std::string sName = actualType->getSoulName();
            if (ShapeMap.count(sName)) {
              ShapeDecl *SD = ShapeMap[sName];
              for (int i = 0; i < (int)SD->Members.size(); ++i) {
                std::string sanDef =
                    toka::Type::stripMorphology(SD->Members[i].Name);
                std::string sanMemb = toka::Type::stripMorphology(M->Member);
                if (sanDef == sanMemb) {
                  uint64_t bit = (1ULL << i);
                  bool isUnset = false;
                  if (!EffectiveInfo->IsReference() &&
                      EffectiveInfo->partialMovePlan().admits(
                          PartialMoveProjectionKind::DirectField, i)) {
                    isUnset = hasExactlyPlaceState(
                        EffectiveInfo->ExactPlace.projectionFact(
                            PartialMoveProjectionKind::DirectField, i),
                        PlaceState::Moved);
                  } else {
                    isUnset = !(EffectiveInfo->InitMask & bit) &&
                              !(EffectiveInfo->DirtyReferentMask & bit);
                  }

                  if (isUnset) {
                    isLHSUnset = true;
                  }
                  break;
                }
              }
            }
          }
        }
      }
    }

    // Constitution 1.3: Only elevate soul permission if it's a
    // Mutation, not a Reseat (Rebind).
    if (!isRebind && (isLHSWritable || isLHSUnset))
      lhsType =
          lhsType->withAttributes(true, lhsType->IsNullable); // Valid Mutation

    // A writable outer aggregate is not an authority source for a nested
    // field.  Once the field path has been resolved, its declaration-derived
    // capability is decisive unless this is the one permitted initialization
    // of an uninitialized slot.  In particular, `node#.shared_field.payload = ...`
    // cannot manufacture P for a `~field: T` declaration.
    if (payloadCapabilityDenied && !payloadFlowDenied && !isLHSUnset) {
      error(Bin->LHS.get(),
            DiagID::ERR_SEMA_COVENANT_VIOLATION_CANNOT_ELEVATE_WRITE_P);
      if (auto *member = dynamic_cast<MemberExpr *>(Bin->LHS.get())) {
        DiagnosticEngine::report(
            getLoc(member), DiagID::NOTE_GENERIC,
            "payload-write authority is local to the target field declaration; a shared aggregate view cannot make an ordinary sibling field writable");
      }
      HasError = true;
    }

    // Like payload P, H comes only from the declaration or parameter
    // signature.  A use-site `#` records an attempted rebind; it must not
    // turn a payload-only `^p#`, `*p#`, or `&p#` into a rebindable handle.
    if (handleCapabilityDenied && !isLHSUnset) {
      error(Bin->LHS.get(),
            DiagID::ERR_SEMA_CANNOT_ASSIGN_TO_IMMUTABLE_ENTITY_MISSING);
      HasError = true;
    }

    if (!isRebind && !lhsType->IsWritable && !isRefAssign) {
      error(Bin->LHS.get(), DiagID::ERR_IMMUTABLE_MOD, LHS);
    }

    auto lhsCompatType = lhsType->withAttributes(false, lhsType->IsNullable);

    // [FIX] Reference Rebinding Morphology Mirror
    // [NEW] Lifetime Safety Check: Scope(LHS_Object) >=
    // Scope(RHS_Dependency)
    std::string targetObjName = "";
    Expr *lhsObj = Bin->LHS.get();
    while (true) {
        if (auto *me = dynamic_cast<MemberExpr *>(lhsObj)) {
            lhsObj = me->Object.get();
        } else if (auto *un = dynamic_cast<UnaryExpr *>(lhsObj)) {
            lhsObj = un->RHS.get();
        } else if (auto *ce = dynamic_cast<CastExpr *>(lhsObj)) {
            lhsObj = ce->Expression.get();
        } else {
            break;
        }
    }
    if (auto *ve = dynamic_cast<VariableExpr *>(lhsObj)) {
      targetObjName = ve->Name;
    }

    if (!targetObjName.empty()) {
      SymbolInfo *targetInfo = nullptr;
      std::string lookupName = targetObjName;
      if (!CurrentScope->findSymbol(lookupName, targetInfo)) {
          if (CurrentScope->findSymbol("&" + lookupName, targetInfo)) { lookupName = "&" + lookupName; }
          else if (CurrentScope->findSymbol("*" + lookupName, targetInfo)) { lookupName = "*" + lookupName; }
          else if (CurrentScope->findSymbol("^" + lookupName, targetInfo)) { lookupName = "^" + lookupName; }
          else if (CurrentScope->findSymbol("~" + lookupName, targetInfo)) { lookupName = "~" + lookupName; }
      }

      if (targetInfo) {
        std::set<std::string> rhsDeps = m_LastLifeDependencies;
        if (!m_LastBorrowSource.empty())
          rhsDeps.insert(m_LastBorrowSource);
        
        if (auto *rv = dynamic_cast<VariableExpr *>(Bin->RHS.get())) {
          SymbolInfo *ri = nullptr;
          std::string rhsName = rv->Name;
          if (!CurrentScope->findSymbol(rhsName, ri)) {
              if (CurrentScope->findSymbol("&" + rhsName, ri)) { rhsName = "&" + rhsName; }
              else if (CurrentScope->findSymbol("*" + rhsName, ri)) { rhsName = "*" + rhsName; }
              else if (CurrentScope->findSymbol("^" + rhsName, ri)) { rhsName = "^" + rhsName; }
              else if (CurrentScope->findSymbol("~" + rhsName, ri)) { rhsName = "~" + rhsName; }
          }
          if (ri) {
            rhsDeps.insert(ri->LifeDependencySet.begin(), ri->LifeDependencySet.end());
          }
        }

        std::set<std::string> mergedDeps;
        for (const auto &dep : rhsDeps) {
            mergedDeps.insert(dep);
            SymbolInfo *depInfo = nullptr;
            std::string depName = dep;
            if (!CurrentScope->findSymbol(depName, depInfo)) {
                if (CurrentScope->findSymbol("&" + depName, depInfo)) { depName = "&" + depName; }
                else if (CurrentScope->findSymbol("*" + depName, depInfo)) { depName = "*" + depName; }
                else if (CurrentScope->findSymbol("^" + depName, depInfo)) { depName = "^" + depName; }
                else if (CurrentScope->findSymbol("~" + depName, depInfo)) { depName = "~" + depName; }
            }
            if (depInfo) {
                mergedDeps.insert(depInfo->LifeDependencySet.begin(), depInfo->LifeDependencySet.end());
            }
        }

        int targetDepth = getScopeDepth(lookupName);
        std::set<std::string> visited;
        std::function<bool(std::shared_ptr<toka::Type>)> checkType = [&](std::shared_ptr<toka::Type> t) -> bool {
            if (!t) return false;
            if (t->isReference()) return true;
            if (auto *st = dynamic_cast<ShapeType *>(t.get())) {
                for (const auto &arg : st->GenericArgs) {
                    if (checkType(arg)) return true;
                }
                std::string sName = t->getSoulName();
                if (visited.count(sName) == 0) {
                    visited.insert(sName);
                    if (ShapeMap.count(sName)) {
                        ShapeDecl *SD = ShapeMap[sName];
                        for (const auto &m : SD->Members) {
                            auto mT = getPhysicalType(m);
                            if (checkType(mT)) return true;
                        }
                    }
                }
            }
            return false;
        };

        for (const auto &dep : mergedDeps) {
          SymbolInfo *depInfo = nullptr;
          if (CurrentScope->findSymbol(dep, depInfo) && !depInfo->IsReference() && checkType(depInfo->TypeObj)) {
              for (const auto &transDep : depInfo->LifeDependencySet) {
                  int depDepth = getScopeDepth(transDep);
                  if (targetDepth < depDepth) {
                      error(Bin, DiagID::ERR_BORROW_LIFETIME, targetObjName, transDep);
                      SourceLocation originLoc = findPathDeclaration(transDep);
                      recordDecision(
                          Bin, SemanticRuleID::EffRet001,
                          SemanticOperation::EscapingDependency,
                          SemanticDecision::Reject,
                          SemanticReason::LifetimeDepthViolation,
                          targetObjName, transDep, originLoc);
                      if (originLoc.isValid())
                        DiagnosticEngine::report(
                            originLoc, DiagID::NOTE_GENERIC,
                            "shorter-lived dependency declared here");
                  }
                  targetInfo->LifeDependencySet.insert(transDep);
              }
          } else {
              int depDepth = getScopeDepth(dep);
              if (targetDepth < depDepth) {
                error(Bin, DiagID::ERR_BORROW_LIFETIME, targetObjName, dep);
                SourceLocation originLoc = findPathDeclaration(dep);
                recordDecision(Bin, SemanticRuleID::EffRet001,
                               SemanticOperation::EscapingDependency,
                               SemanticDecision::Reject,
                               SemanticReason::LifetimeDepthViolation,
                               targetObjName, dep, originLoc);
                if (originLoc.isValid())
                  DiagnosticEngine::report(
                      originLoc, DiagID::NOTE_GENERIC,
                      "shorter-lived dependency declared here");
              }
              targetInfo->LifeDependencySet.insert(dep);
          }
        }

        if (!m_LastFieldDependencies.empty()) {
          targetInfo->FieldDependencySet.clear();
          for (const auto &pair : m_LastFieldDependencies) {
            for (const auto &dep : pair.second) {
              std::string actualDep = dep;
              if (actualDep.rfind("self.", 0) == 0)
                actualDep = targetObjName + actualDep.substr(4);
              targetInfo->FieldDependencySet[pair.first].insert(actualDep);
            }
          }
        }

        // An assignment to a closure binding must preserve the same boundary
        // facts as its source.  Otherwise `f = borrowed_closure` would hide
        // an implicit capture before `thread_spawn(f)` observes it.
        if (Bin->Op == "=" && !containsMemberExpr(Bin->LHS.get())) {
          auto clearClosureSummary = [&]() {
            targetInfo->HasClosureBoundarySummary = false;
            targetInfo->ClosureExplicitCaptures.clear();
            targetInfo->ClosureImplicitCaptures.clear();
            targetInfo->ClosureNonSendCaptures.clear();
            targetInfo->ClosureNonSyncCopyCaptures.clear();
          };
          Expr *closureSource = Bin->RHS.get();
          while (closureSource) {
            if (auto *cede = dynamic_cast<CedeExpr *>(closureSource)) {
              closureSource = cede->Value.get();
            } else if (auto *pass = dynamic_cast<PassExpr *>(closureSource)) {
              closureSource = pass->Value.get();
            } else if (auto *cast = dynamic_cast<CastExpr *>(closureSource)) {
              closureSource = cast->Expression.get();
            } else if (auto *unary = dynamic_cast<UnaryExpr *>(closureSource)) {
              closureSource = unary->RHS.get();
            } else {
              break;
            }
          }
          if (auto *closure = dynamic_cast<ClosureExpr *>(closureSource)) {
            targetInfo->HasClosureBoundarySummary =
                closure->HasBoundaryCaptureSummary;
            for (const auto &capture : closure->ExplicitCaptures)
              targetInfo->ClosureExplicitCaptures.insert(
                  Type::stripMorphology(capture.Name));
            targetInfo->ClosureImplicitCaptures = std::set<std::string>(
                closure->BoundaryImplicitCaptures.begin(),
                closure->BoundaryImplicitCaptures.end());
            targetInfo->ClosureNonSendCaptures = std::set<std::string>(
                closure->BoundaryNonSendCaptures.begin(),
                closure->BoundaryNonSendCaptures.end());
            targetInfo->ClosureNonSyncCopyCaptures = std::set<std::string>(
                closure->BoundaryNonSyncCopyCaptures.begin(),
                closure->BoundaryNonSyncCopyCaptures.end());
          } else if (auto *source =
                         dynamic_cast<VariableExpr *>(closureSource)) {
            SymbolInfo *sourceInfo = nullptr;
            std::string sourceName;
            if (CurrentScope->findVariableWithDeref(source->Name, sourceInfo,
                                                    sourceName) && sourceInfo &&
                sourceInfo->HasClosureBoundarySummary) {
              targetInfo->HasClosureBoundarySummary = true;
              targetInfo->ClosureExplicitCaptures =
                  sourceInfo->ClosureExplicitCaptures;
              targetInfo->ClosureImplicitCaptures =
                  sourceInfo->ClosureImplicitCaptures;
              targetInfo->ClosureNonSendCaptures =
                  sourceInfo->ClosureNonSendCaptures;
              targetInfo->ClosureNonSyncCopyCaptures =
                  sourceInfo->ClosureNonSyncCopyCaptures;
            } else {
              clearClosureSummary();
            }
          } else {
            clearClosureSummary();
          }
        }
      }
    }
    m_LastBorrowSource = "";
    m_LastLifeDependencies.clear();
    m_LastFieldDependencies.clear();

    // A handle rebind replaces the direct source that bounds the target's
    // effective payload authority.  Reference rebinds return early below,
    // so keep the update in one helper shared by both reference and ordinary
    // handle assignments.
    auto updateHandleFlowCeiling = [&]() {
      if (isUnsetInit)
        return;
      AccessPath targetPath =
          canonicalizeAccessPath(makeAccessPath(Bin->LHS.get()));
      if (!targetPath)
        return;
      PermissionFlow flow = getPermissionFlow(Bin->RHS.get());
      if ((flow.Kind == PermissionFlowKind::Shared ||
           flow.Kind == PermissionFlowKind::UnsafeRaw) &&
          !flow.DirectCapability.PayloadWritable) {
        m_PayloadFlowRestrictedPaths.insert(targetPath);
      } else {
        m_PayloadFlowRestrictedPaths.erase(targetPath);
      }
    };

    if (isRefAssign && !isUnsetInit) {
      // If LHS is Ref (&#), RHS must be Ref (&)
      if (!rhsType->isReference()) {
        error(Bin->RHS.get(), DiagID::ERR_MORPHOLOGY_MISMATCH, "&",
              rhsType->toString());
      }
      // Compare types after stripping one layer of Reference
      auto target = lhsType->getPointeeType();
      auto source = rhsType->getPointeeType();
      if (target && source && isTypeCompatible(target, source)) {
        // OK
      } else {
        error(Bin, DiagID::ERR_TYPE_MISMATCH, RHS + " (ref)", LHS);
      }
      updateHandleFlowCeiling();
      return lhsType;
    }

    // Strict Pointer Morphology Check
    if (!isRefAssign) {
      // Skip check if LHS is explicit dereference (*p = val) which
      // targets Value, not Pointer Identity.
      bool isDerefAssign = false;
      if (auto *Un = dynamic_cast<UnaryExpr *>(Bin->LHS.get())) {
        if (Un->Op == TokenType::Star)
          isDerefAssign = true;
      }

      if (!isDerefAssign) {
        // Determine Target Morphology (LHS)
        MorphKind targetMorph = MorphKind::None;
        // We need to look at the LHS expression structure
        // If LHS is *p or ^p etc.
        targetMorph = getSyntacticMorphology(Bin->LHS.get());

        // If LHS is a variable declaration, we don't handle it here
        // (handled in checkVariableDecl). But this is assignment to
        // existing variable. If LHS is 'p' (VariableExpr) and p is a
        // pointer type, Morph is None (hidden). If p is pointer,
        // targetMorph=None. SourceMorph check... User rule: "auto ^p =
        // x" (Invalid). "auto p = ^x" (Invalid). "p = q" (Hidden =
        // Hidden)? "Strict explicit morphology matching". If LHS has no
        // sigil, but is a pointer type? "auto p = ^x". p is pointer.
        // LHS sigil None. RHS sigil Unique. Mismatch. Correct. So
        // `getSyntacticMorphology` returning None for VariableExpr is
        // correct.

        // Determine if either side is morphic exempt.
        auto isExempt = [](Expr *E) {
            if (!E) return false;
            while (E) {
                if (E->IsMorphicExempt) return true;
                if (auto *Un = dynamic_cast<UnaryExpr *>(E)) { E = Un->RHS.get(); }
                else if (auto *Idx = dynamic_cast<ArrayIndexExpr *>(E)) { E = Idx->Array.get(); }
                else { break; }
            }
            return false;
        };

        MorphKind sourceMorph = getSyntacticMorphology(Bin->RHS.get());
        if (!isExempt(Bin->LHS.get()) && !isExempt(Bin->RHS.get())) {
          checkStrictMorphology(Bin, targetMorph, sourceMorph, LHS);
        }
      }
    }

    bool diagnosedNullAssign = false;
    if (lhsCompatType && lhsCompatType->isRawPointer() &&
        !lhsCompatType->IsNullable && rhsType && rhsType->isNullType()) {
      error(Bin, DiagID::ERR_NONZERO_RAW_NULL_FLOW,
            lhsCompatType->toString());
      diagnosedNullAssign = true;
    }

    if (!diagnosedNullAssign && !isRefAssign && !isSmartNew &&
        !isTypeCompatible(lhsCompatType, rhsType) && LHS != "unknown" &&
        RHS != "unknown") {
      if (auto outcome =
              std::dynamic_pointer_cast<MissOutcomeType>(lhsCompatType)) {
        DiagnosticEngine::report(
            getLoc(Bin), DiagID::ERR_MISS_OUTCOME_RETURN_MISMATCH,
            rhsType ? rhsType->toString() : RHS, outcome->toString(),
            outcome->PayloadType ? outcome->PayloadType->toString()
                                 : "unknown");
        HasError = true;
      } else {
        error(Bin, DiagID::ERR_TYPE_MISMATCH, RHS + " (assign)", LHS);
      }
    }

    // Rebinding an existing indirect binding preserves its declaration but
    // replaces the direct source that limits its effective payload authority.
    // A shared/reference/raw RHS therefore installs its one-hop ceiling on
    // the exact target path; an independent or fresh source leaves only the
    // target's declared ceiling.  This is a flow update, never a declaration
    // rewrite.
    if (assignmentKind == AssignmentSemanticKind::Handle)
      updateHandleFlowCeiling();

    // [Fix] Update InitMask logic for uninitialized variables
    Expr *LHSExpr = Bin->LHS.get();

    // Helper lambda for back-propagation
    auto propagateInit = [&](std::string startVar, uint64_t updateBits,
                             bool isPartial) {
      std::string current = startVar;
      // Limit depth to avoid infinite loops in circular refs (though
      // illegal in Toka)
      int depth = 0;
      while (!current.empty() && depth < 20) {
        SymbolInfo *Sym = nullptr;
        std::string actualName;
        if (!CurrentScope->findVariableWithDeref(current, Sym, actualName))
          break;

        // Update the symbol itself (if it's the source or a ref in
        // chain)
        if (Sym->IsReference()) {
          if (isPartial)
            Sym->DirtyReferentMask |= updateBits;
          else
            Sym->DirtyReferentMask = ~0ULL;
        } else {
          Sym->InitMask |= updateBits;
        }

        // Move to next upstream source
        if (Sym->IsReference()) {
          current = Sym->BorrowedFrom;
        } else {
          break; // Reached root
        }
        depth++;
      }
    };

    if (auto *Var = dynamic_cast<VariableExpr *>(LHSExpr)) {
      SymbolInfo *Info = nullptr;
      std::string actualName;
      if (CurrentScope->findVariableWithDeref(Var->Name, Info, actualName)) {
        // Full Assignment to Variable (or Reference)
        // If it's a reference, we propagate Cleanliness to Source
        if (Info->IsReference()) {
          Info->DirtyReferentMask = ~0ULL;
          if (!Info->BorrowedFrom.empty()) {
            propagateInit(Info->BorrowedFrom, ~0ULL, false);
          }
        } else {
          Info->InitMask = ~0ULL;
          if (isExplicitInit && Info == initTargetInfo)
            Info->ExactPlace.transitionWhole(PlaceState::Never,
                                             PlaceState::Live);
          Info->ExactPlace.repopulateAllProjections();
        }
      }
    } else if (auto *Memb = dynamic_cast<MemberExpr *>(LHSExpr)) {
      // Partial Initialization via Member
      Expr *Obj = Memb->Object.get();
      if (auto *Var = dynamic_cast<VariableExpr *>(Obj)) {
        SymbolInfo *Info = nullptr;
        std::string actualName;
        if (CurrentScope->findVariableWithDeref(Var->Name, Info, actualName)) {
          std::shared_ptr<toka::Type> actualType = Info->TypeObj;
          // If reference, peel to find Shape
          if (actualType && actualType->isReference()) {
            actualType = actualType->getPointeeType();
          }

          if (actualType && actualType->isShape()) {
            std::string sName = actualType->getSoulName();
            if (ShapeMap.count(sName)) {
              ShapeDecl *SD = ShapeMap[sName];

              // Find which bit to set
              uint64_t bitsToSet = 0;
              uint64_t projectionIndex = 0;
              for (int i = 0; i < (int)SD->Members.size(); ++i) {
                if (SD->Members[i].Name == Memb->Member) {
                  bitsToSet = (1ULL << i);
                  projectionIndex = static_cast<uint64_t>(i);
                  break;
                }
              }

              if (bitsToSet != 0) {
                // Apply locally
                if (Info->IsReference()) {
                  Info->DirtyReferentMask |= bitsToSet;
                  // Propagate Up
                  if (!Info->BorrowedFrom.empty()) {
                    propagateInit(Info->BorrowedFrom, bitsToSet, true);
                  }
                } else {
                  initializeProjectionFacts(*Info);
                  if (Info->ExactPlace.transitionProjection(
                          PartialMoveProjectionKind::DirectField,
                          projectionIndex, PlaceState::Live)) {
                    syncLegacyProjectionLiveness(*Info);
                  } else {
                    Info->InitMask |= bitsToSet;
                  }
                }
              }
            }
          }
        }
      }
    } else if (auto *Index = dynamic_cast<ArrayIndexExpr *>(LHSExpr)) {
      auto *Root = dynamic_cast<VariableExpr *>(Index->Array.get());
      auto *constant = Index->Indices.size() == 1
                           ? dynamic_cast<NumberExpr *>(Index->Indices[0].get())
                           : nullptr;
      if (Root && constant && constant->Value < 64) {
        SymbolInfo *Info = nullptr;
        std::string actualName;
        if (CurrentScope->findVariableWithDeref(Root->Name, Info, actualName) &&
            Info && Info->TypeObj && Info->TypeObj->isArray()) {
          auto array = std::dynamic_pointer_cast<ArrayType>(Info->TypeObj);
          if (array && constant->Value < array->Size) {
            initializeProjectionFacts(*Info);
            if (Info->ExactPlace.transitionProjection(
                    PartialMoveProjectionKind::FixedArrayElement,
                    constant->Value, PlaceState::Live)) {
              syncLegacyProjectionLiveness(*Info);
            } else {
              Info->InitMask |= (1ULL << constant->Value);
            }
          }
        }
      }
    }

    // A simple local assignment replaces the direct value source of that
    // binding.  Carry the incomplete-input dependency for later declarations,
    // but deliberately do not emit an assignment event in protocol v1: its
    // records are declaration facts and have no temporal overwrite field.
    if (Bin->Op == "=") {
      if (const auto *target =
              directBindingAssignmentTarget(Bin->LHS.get())) {
        SymbolInfo *info = nullptr;
        std::string actualName;
        if (CurrentScope->findVariableWithDeref(target->Name, info,
                                                actualName) &&
            info) {
          info->ConditionalTodoIds =
              collectConditionalTodoDependencies(Bin->RHS.get());
        }
      }
    }

    return lhsType;
  }

  // General Binary Ops
  if (Bin->Op == "&&" || Bin->Op == "||") {
    if (!lhsType->isBoolean() || !rhsType->isBoolean()) {
      error(Bin, DiagID::ERR_INVALID_OP, Bin->Op, lhsType->toString(),
            rhsType->toString());
    }
    return toka::Type::fromString("bool");
  }

  if (lhsType->isPointer() && (Bin->Op == "+" || Bin->Op == "-")) {
    if (!m_InUnsafeContext) {
      error(Bin,
            DiagID::ERR_UNSAFE_ALLOC_CTX); // Reuse for ptr arithmetic
    }
    auto ptrType = std::dynamic_pointer_cast<toka::PointerType>(resolveType(lhsType, true));
    std::string elemType = "unknown";
    if (ptrType) {
      auto pointee = resolveType(ptrType->getPointeeType(), true);
      if (auto slice = std::dynamic_pointer_cast<toka::SliceType>(pointee)) {
        elemType = slice->ElementType->toString();
      } else if (auto arr = std::dynamic_pointer_cast<toka::ArrayType>(pointee)) {
        elemType = arr->ElementType->toString();
      } else {
        elemType = pointee->toString();
      }
    }
    return toka::Type::fromString("*[" + elemType + "]");
  }

  if (Bin->Op == "==" || Bin->Op == "!=" || Bin->Op == "<" || Bin->Op == ">" ||
      Bin->Op == "<=" || Bin->Op == ">=") {
    bool diagnosedNullCmp = false;
    if (lhsType && lhsType->isRawPointer() && rhsType && rhsType->isNullType()) {
      if (!lhsType->IsNullable) {
        error(Bin, DiagID::ERR_NONZERO_RAW_NULL_FLOW,
              lhsType->toString());
        diagnosedNullCmp = true;
      }
    }
    if (rhsType && rhsType->isRawPointer() && lhsType && lhsType->isNullType()) {
      if (!rhsType->IsNullable) {
        error(Bin, DiagID::ERR_NONZERO_RAW_NULL_FLOW,
              rhsType->toString());
        diagnosedNullCmp = true;
      }
    }

    // [Phase 2] Syntactic Sugar / Operator Overloading for == and !=
    if ((Bin->Op == "==" || Bin->Op == "!=") && !diagnosedNullCmp) {
      auto projectOwnedStringViewForEquality =
          [&](std::unique_ptr<Expr> &operand,
              std::shared_ptr<toka::Type> &operandType,
              const std::shared_ptr<toka::Type> &otherType) -> bool {
        auto resolvedOperand = resolveType(operandType, true);
        if (!resolvedOperand || !resolvedOperand->isShape())
          return false;

        const std::string soul = resolvedOperand->getSoulName();
        if (soul != "string" && soul != "String")
          return false;
        if (!MethodMap.count(soul) || !MethodMap[soul].count("as_str"))
          return false;
        auto targetType = resolveType(
            toka::Type::fromString(MethodMap[soul]["as_str"]), true);
        if (!targetType ||
            (!isTypeCompatible(targetType, otherType) &&
             !isTypeCompatible(otherType, targetType)))
          return false;

        const std::string targetSoul = targetType->getSoulName();
        if (targetSoul != "str" || !MethodMap.count(targetSoul) ||
            !MethodMap[targetSoul].count("eq"))
          return false;

        auto targetCall = std::make_unique<MethodCallExpr>(
            std::move(operand), "as_str",
            std::vector<std::unique_ptr<Expr>>{});
        targetCall->Loc = Bin->Loc;
        operand = std::move(targetCall);
        operandType = checkExpr(operand.get());
        return operandType && operandType->toString() != "unknown";
      };

      if (!isTypeCompatible(lhsType, rhsType) &&
          !isTypeCompatible(rhsType, lhsType)) {
        if (projectOwnedStringViewForEquality(Bin->LHS, lhsType, rhsType)) {
          LHS = lhsType->toString();
        } else if (projectOwnedStringViewForEquality(Bin->RHS, rhsType,
                                                     lhsType)) {
          RHS = rhsType->toString();
        }
      }

      auto shapeLRes = resolveType(lhsType, true);
      if (shapeLRes->isShape()) {
        std::string sName = shapeLRes->getSoulName();
        if (MethodMap.count(sName) && MethodMap[sName].count("eq")) {
          if (isTypeCompatible(lhsType, rhsType) || isTypeCompatible(rhsType, lhsType)) {
            Bin->OverloadedMethod = "eq";
            return toka::Type::fromString("bool");
          }
        }
      }
    }

    if (!diagnosedNullCmp && !isTypeCompatible(lhsType, rhsType) &&
        !isTypeCompatible(rhsType, lhsType)) {
      error(Bin, DiagID::ERR_INVALID_OP, Bin->Op, LHS, RHS);
    }
    // Strict Integer Check
    auto lRes = resolveType(lhsType);
    auto rRes = resolveType(rhsType);
    if (!lRes->withAttributes(false, false)
             ->equals(*rRes->withAttributes(false, false))) {
      if (lhsType->isInteger() && rhsType->isInteger()) {
        error(Bin, DiagID::ERR_SEMA_COMPARISON_OPERANDS_MUST_HAVE_EXACT_SAME, LHS, RHS);
      }
    }
    return toka::Type::fromString("bool");
  }

  if (Bin->Op == "+" || Bin->Op == "-" || Bin->Op == "*" || Bin->Op == "/" ||
      Bin->Op == "%") {
    bool isValid = false;
    auto lRes = resolveType(lhsType, true);
    if (lRes->isInteger() || lRes->isFloatingPoint()) {
      if (Bin->Op == "%" && lRes->isFloatingPoint()) {
        error(Bin, DiagID::ERR_SEMA_OPERAND_OF_MUST_BE_INTEGER_GOT_FLOAT);
      }
      isValid = true;
    }

    if (!isValid) {
      error(Bin, DiagID::ERR_SEMA_OPERANDS_OF_MUST_BE_NUMERIC_GOT, Bin->Op, LHS);
    }
    return lhsType->withAttributes(false, lhsType->IsNullable);
  }

  if (Bin->Op == "&" || Bin->Op == "|" || Bin->Op == "^" ||
      Bin->Op == "<<" || Bin->Op == ">>") {
    if (!resolveType(lhsType, true)->isInteger() ||
        !resolveType(rhsType, true)->isInteger()) {
      error(Bin, DiagID::ERR_SEMA_OPERANDS_OF_MUST_BE_INTEGERS, Bin->Op);
    }
    return lhsType->withAttributes(false, lhsType->IsNullable);
  }

  if (Bin->Op == "is" || Bin->Op == "is!") {
    // [Fix] Disable soul collapse for the LHS of 'is' so we can check
    // the the pointer/handle itself.
    bool oldDisable = m_DisableSoulCollapse;
    m_DisableSoulCollapse = true;
    auto lhsType = checkExpr(Bin->LHS.get());
    m_DisableSoulCollapse = oldDisable;

    auto rhsType = checkExpr(Bin->RHS.get());
    // Basic validation for 'is' / 'is!'
    if (auto *rhsVar = dynamic_cast<VariableExpr *>(Bin->RHS.get())) {
      // If RHS is just a Shape name, it's NOT a valid pattern (should
      // be a variable or variant)
      if (ShapeMap.count(rhsVar->Name)) {
        error(Bin->RHS.get(), DiagID::ERR_SEMA_IS_A_SHAPE_NOT_A_VALID_PATTERN_FOR_IS, rhsVar->Name);
      }
    }
    return toka::Type::fromString("bool");
  }

  return toka::Type::fromString("unknown");
}

} // namespace toka
