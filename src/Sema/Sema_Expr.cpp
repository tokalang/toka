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
#include "toka/MemberAccess.h"
#include "toka/Sema.h"
#include "toka/SourceManager.h"
#include "toka/Type.h"
#include "toka/ASTEvaluator.h"
#include "toka/ComptimeValue.h"
#include <algorithm>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace toka {

static SourceLocation getLoc(ASTNode *Node) { return Node->Loc; }

static std::map<std::string, bool> captureVisibleUniqueMoved(Scope *ScopePtr) {
  std::map<std::string, bool> moved;
  for (Scope *S = ScopePtr; S; S = S->Parent) {
    for (const auto &pair : S->Symbols) {
      if (!moved.count(pair.first) && pair.second.IsUnique())
        moved[pair.first] = pair.second.Moved;
    }
  }
  return moved;
}

static std::map<std::string, uint64_t> captureVisibleInitMasks(Scope *ScopePtr) {
  std::map<std::string, uint64_t> masks;
  for (Scope *S = ScopePtr; S; S = S->Parent) {
    for (const auto &pair : S->Symbols) {
      if (!masks.count(pair.first))
        masks[pair.first] = pair.second.InitMask;
    }
  }
  return masks;
}

static std::map<std::string, bool> captureVisibleMoved(Scope *ScopePtr) {
  std::map<std::string, bool> moved;
  for (Scope *S = ScopePtr; S; S = S->Parent) {
    for (const auto &pair : S->Symbols) {
      if (!moved.count(pair.first))
        moved[pair.first] = pair.second.Moved;
    }
  }
  return moved;
}

static void restoreVisibleAnalysisState(
    Scope *ScopePtr, const std::map<std::string, uint64_t> &masks,
    const std::map<std::string, bool> &moved) {
  for (const auto &pair : masks) {
    SymbolInfo *info = nullptr;
    if (ScopePtr->findSymbol(pair.first, info) && info)
      info->InitMask = pair.second;
  }
  for (const auto &pair : moved) {
    SymbolInfo *info = nullptr;
    if (ScopePtr->findSymbol(pair.first, info) && info)
      info->Moved = pair.second;
  }
}

Sema::AnalysisState Sema::captureAnalysisState() {
  AnalysisState state;
  state.InitMasks = captureVisibleInitMasks(CurrentScope);
  state.Moved = captureVisibleMoved(CurrentScope);
  state.PAL = PALCheckerState.snapshot();
  return state;
}

void Sema::mergeAnalysisStates(const std::vector<AnalysisState> &states,
                               const PALChecker &palBase) {
  if (states.empty())
    return;

  std::map<std::string, uint64_t> mergedMasks = states.front().InitMasks;
  std::map<std::string, bool> mergedMoved = states.front().Moved;
  PALChecker mergedPAL = states.front().PAL;

  for (size_t i = 1; i < states.size(); ++i) {
    const auto &state = states[i];

    for (const auto &pair : state.InitMasks) {
      if (!mergedMasks.count(pair.first))
        mergedMasks[pair.first] = 0;
    }
    for (auto &pair : mergedMasks) {
      uint64_t mask =
          state.InitMasks.count(pair.first) ? state.InitMasks.at(pair.first) : 0;
      pair.second &= mask;
    }

    for (const auto &pair : state.Moved) {
      if (!mergedMoved.count(pair.first))
        mergedMoved[pair.first] = false;
    }
    for (auto &pair : mergedMoved) {
      bool moved = state.Moved.count(pair.first) ? state.Moved.at(pair.first)
                                                 : false;
      pair.second = pair.second || moved;
    }

    PALCheckerState.restore(mergedPAL);
    PALCheckerState.mergeBranches(palBase, mergedPAL, true, state.PAL, true);
    mergedPAL = PALCheckerState.snapshot();
  }

  restoreVisibleAnalysisState(CurrentScope, mergedMasks, mergedMoved);
  PALCheckerState.restore(mergedPAL);
}

static std::vector<std::string> collectLoopEscapingMoves(
    Scope *ScopePtr, const std::map<std::string, bool> &before) {
  std::vector<std::string> names;
  for (const auto &pair : before) {
    if (pair.second)
      continue;
    SymbolInfo *info = nullptr;
    if (ScopePtr->findSymbol(pair.first, info) && info && info->IsUnique() &&
        info->Moved) {
      names.push_back(pair.first);
    }
  }
  return names;
}

static std::string getDisplayVariableName(std::string name) {
  while (!name.empty() &&
         (name[0] == '&' || name[0] == '*' || name[0] == '^' ||
          name[0] == '~')) {
    name.erase(name.begin());
  }
  return name;
}

static void collectVariables(ASTNode *Node, std::set<std::string> &Vars) {
  if (!Node) return;
  if (auto *VE = dynamic_cast<VariableExpr *>(Node)) {
    Vars.insert(VE->Name);
    return;
  }
  if (auto *Bin = dynamic_cast<BinaryExpr *>(Node)) {
    collectVariables(Bin->LHS.get(), Vars);
    collectVariables(Bin->RHS.get(), Vars);
  } else if (auto *Un = dynamic_cast<UnaryExpr *>(Node)) {
    collectVariables(Un->RHS.get(), Vars);
  } else if (auto *Call = dynamic_cast<CallExpr *>(Node)) {
    for (auto &Arg : Call->Args) {
      collectVariables(Arg.get(), Vars);
    }
  } else if (auto *Met = dynamic_cast<MethodCallExpr *>(Node)) {
    collectVariables(Met->Object.get(), Vars);
    for (auto &Arg : Met->Args) {
      collectVariables(Arg.get(), Vars);
    }
  } else if (auto *Cast = dynamic_cast<CastExpr *>(Node)) {
    collectVariables(Cast->Expression.get(), Vars);
  } else if (auto *Addr = dynamic_cast<AddressOfExpr *>(Node)) {
    collectVariables(Addr->Expression.get(), Vars);
  } else if (auto *Idx = dynamic_cast<ArrayIndexExpr *>(Node)) {
    collectVariables(Idx->Array.get(), Vars);
    for (auto &IndexExpr : Idx->Indices) {
      collectVariables(IndexExpr.get(), Vars);
    }
  } else if (auto *Memb = dynamic_cast<MemberExpr *>(Node)) {
    collectVariables(Memb->Object.get(), Vars);
  }
}

static bool isLValueRef(ASTNode *Node, const std::string &VarName) {
  if (!Node) return false;
  if (auto *VE = dynamic_cast<VariableExpr *>(Node)) {
    return VE->Name == VarName;
  }
  if (auto *Memb = dynamic_cast<MemberExpr *>(Node)) {
    return isLValueRef(Memb->Object.get(), VarName);
  }
  if (auto *Idx = dynamic_cast<ArrayIndexExpr *>(Node)) {
    return isLValueRef(Idx->Array.get(), VarName);
  }
  if (auto *Cast = dynamic_cast<CastExpr *>(Node)) {
    return isLValueRef(Cast->Expression.get(), VarName);
  }
  if (auto *Unary = dynamic_cast<UnaryExpr *>(Node)) {
    return isLValueRef(Unary->RHS.get(), VarName);
  }
  return false;
}

static bool isVariableMutated(ASTNode *Node, const std::string &VarName) {
  if (!Node) return false;

  if (auto *Bin = dynamic_cast<BinaryExpr *>(Node)) {
    if (Bin->Op == "=" || Bin->Op == "+=" || Bin->Op == "-=" ||
        Bin->Op == "*=" || Bin->Op == "/=" || Bin->Op == "%=" ||
        Bin->Op == "&=" || Bin->Op == "|=" || Bin->Op == "^=" ||
        Bin->Op == "<<=" || Bin->Op == ">>=") {
      if (isLValueRef(Bin->LHS.get(), VarName)) return true;
    }
    return isVariableMutated(Bin->LHS.get(), VarName) || isVariableMutated(Bin->RHS.get(), VarName);
  }

  if (auto *Unary = dynamic_cast<UnaryExpr *>(Node)) {
    if (Unary->Op == TokenType::PlusPlus || Unary->Op == TokenType::MinusMinus ||
        Unary->Op == TokenType::Caret || Unary->Op == TokenType::Ampersand ||
        Unary->Op == TokenType::Star || Unary->Op == TokenType::Tilde) {
      if (isLValueRef(Unary->RHS.get(), VarName)) return true;
    }
    return isVariableMutated(Unary->RHS.get(), VarName);
  }

  if (auto *Call = dynamic_cast<CallExpr *>(Node)) {
    for (auto &Arg : Call->Args) {
      if (isVariableMutated(Arg.get(), VarName)) return true;
    }
    return false;
  }

  if (auto *Met = dynamic_cast<MethodCallExpr *>(Node)) {
    if (auto *VE = dynamic_cast<VariableExpr *>(Met->Object.get())) {
      if (VE->Name == VarName) return true;
    }
    for (auto &Arg : Met->Args) {
      if (isVariableMutated(Arg.get(), VarName)) return true;
    }
    return isVariableMutated(Met->Object.get(), VarName);
  }

  if (auto *Block = dynamic_cast<BlockStmt *>(Node)) {
    for (auto &Stmt : Block->Statements) {
      if (isVariableMutated(Stmt.get(), VarName)) return true;
    }
    return false;
  }

  if (auto *ExprS = dynamic_cast<ExprStmt *>(Node)) {
    return isVariableMutated(ExprS->Expression.get(), VarName);
  }

  if (auto *If = dynamic_cast<IfExpr *>(Node)) {
    return isVariableMutated(If->Condition.get(), VarName) ||
           isVariableMutated(If->Then.get(), VarName) ||
           isVariableMutated(If->Else.get(), VarName);
  }

  if (auto *Loop = dynamic_cast<LoopExpr *>(Node)) {
    return isVariableMutated(Loop->Condition.get(), VarName) ||
           isVariableMutated(Loop->Body.get(), VarName);
  }

  if (auto *fe = dynamic_cast<ForExpr *>(Node)) {
    return isVariableMutated(fe->Collection.get(), VarName) ||
           isVariableMutated(fe->Body.get(), VarName) ||
           isVariableMutated(fe->ElseBody.get(), VarName);
  }

  if (auto *VarD = dynamic_cast<VariableDecl *>(Node)) {
    return isVariableMutated(VarD->Init.get(), VarName);
  }

  if (auto *Match = dynamic_cast<MatchExpr *>(Node)) {
    if (isVariableMutated(Match->Target.get(), VarName)) return true;
    for (auto &Arm : Match->Arms) {
      if (isVariableMutated(Arm->Body.get(), VarName)) return true;
      if (Arm->Guard && isVariableMutated(Arm->Guard.get(), VarName)) return true;
    }
    return false;
  }

  if (auto *Ret = dynamic_cast<ReturnStmt *>(Node)) {
    return Ret->ReturnValue && isVariableMutated(Ret->ReturnValue.get(), VarName);
  }

  return false;
}

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
  return "";
}

bool Sema::isLValue(const Expr *expr) {
  if (dynamic_cast<const VariableExpr *>(expr))
    return true;
  if (auto *me = dynamic_cast<const MemberExpr *>(expr))
    return isLValue(me->Object.get());
  if (auto *ae = dynamic_cast<const ArrayIndexExpr *>(expr))
    return isLValue(ae->Array.get());
  if (auto *ue = dynamic_cast<const UnaryExpr *>(expr)) {
    if (ue->Op == TokenType::Star)
      return true;
  }
  if (auto *pe = dynamic_cast<const PostfixExpr *>(expr)) {
    if (pe->Op == TokenType::TokenWrite)
      return isLValue(pe->LHS.get());
  }
  return false;
}

std::string Sema::checkUnaryExprStr(UnaryExpr *Unary) {
  return checkUnaryExpr(Unary)->toString();
}

std::unique_ptr<Expr> Sema::foldGenericConstant(std::unique_ptr<Expr> E) {
  return ASTEvaluator::foldExpression(std::move(E), CurrentScope, this);
}

std::shared_ptr<toka::Type> Sema::checkExpr(Expr *E) {
  if (!E)
    return toka::Type::fromString("void");
  ActiveNodeRAII Active(E);
  m_LastInitMask = ~0ULL; // Default to fully set
  auto T = checkExprImpl(E);
  // [Fix] Monomorphize type before assigning it to the node
  T = resolveType(T);
  E->ResolvedType = T;

  if (!dynamic_cast<UnsetExpr *>(E) && !dynamic_cast<InitStructExpr *>(E) &&
      !dynamic_cast<ArrayInitExpr *>(E)) {
    m_LastInitMask = ~0ULL;
  }
  bool hasRefs = false;
  if (T) {
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
      hasRefs = checkType(T);
  }

  if (!hasRefs) {
      m_LastLifeDependencies.clear();
      m_LastBorrowSource = "";
  }

  return T;
}

std::shared_ptr<toka::Type>
Sema::checkExpr(Expr *E, std::shared_ptr<toka::Type> expected) {
  auto oldExpected = m_ExpectedType;
  m_ExpectedType = expected;
  auto T = checkExpr(E);
  m_ExpectedType = oldExpected;
  return T;
}

// -----------------------------------------------------------------------------
// Type & Morphology Helpers
// -----------------------------------------------------------------------------

Sema::MorphKind Sema::getSyntacticMorphology(Expr *E) {
  if (!E)
    return MorphKind::None;

  // Unary Ops: ^, *, ~, &
  if (auto *U = dynamic_cast<UnaryExpr *>(E)) {


    switch (U->Op) {
    case TokenType::Star:
      return MorphKind::Raw;
    case TokenType::Caret:
      return MorphKind::Unique;
    case TokenType::Tilde:
      // [Toka 1.3] Bitwise NOT (~) on integer is Morph-Exempt (Value)
      if (U->RHS && U->RHS->ResolvedType && U->RHS->ResolvedType->isInteger())
          return MorphKind::None;
      return MorphKind::Shared;
    case TokenType::Ampersand:
      return MorphKind::Ref;
    default:
      return MorphKind::None;
    }
  }




  // Binary Expressions: Pointer Arithmetic or Computed Values
  // These produce RValues, which do not need sigils (the strictly-typed
  // checking handles validation).
  if (dynamic_cast<BinaryExpr *>(E)) {
    return MorphKind::Valid;
  }

  // Cast: Check target type string
  if (auto *C = dynamic_cast<CastExpr *>(E))
    return morphKindFromTypeString(C->TargetType);

  if (auto *Aw = dynamic_cast<AwaitExpr *>(E)) {
    return getSyntacticMorphology(Aw->Expression.get());
  }

  if (auto *Wt = dynamic_cast<WaitExpr *>(E)) {
    return getSyntacticMorphology(Wt->Expression.get());
  }

  if (auto *Ce = dynamic_cast<CedeExpr *>(E)) {
    return getSyntacticMorphology(Ce->Value.get());
  }

  if (auto *Post = dynamic_cast<PostfixExpr *>(E)) {
    if (Post->Op == TokenType::DoubleQuestion)
      return MorphKind::None;
    return getSyntacticMorphology(Post->LHS.get());
  }

  if (auto *Unwrap = dynamic_cast<UnwrapPropagationExpr *>(E)) {
    return MorphKind::None;
  }

  // Member Access: Check if Member string carries pointer sigil (e.g. .*name)
  if (auto *M = dynamic_cast<MemberExpr *>(E)) {
    if (!M->Member.empty()) {
      MemberAccessIntent access = parseMemberAccess(M->Member);
      if (access.IsIdentityAssertion) {
        return MorphKind::Valid; // Assertion bypasses strict checking
      }
      if (access.IsMorphicIdentity && M->IsMorphicExempt && M->ResolvedType) {
        return morphKindFromType(M->ResolvedType);
      }
      char c = M->Member[0];
      if (c == '*')
        return MorphKind::Raw;
      if (c == '^')
        return MorphKind::Unique;
      if (c == '~')
        return MorphKind::Shared;
      if (c == '&')
        return MorphKind::Ref;
    }
    return MorphKind::None;
  }

  // Safe Constructors (Exceptions)
  if (dynamic_cast<CallExpr *>(E) || dynamic_cast<MethodCallExpr *>(E) ||
      dynamic_cast<NewExpr *>(E) || dynamic_cast<AllocExpr *>(E) ||
      dynamic_cast<NullExpr *>(E) || dynamic_cast<UnsetExpr *>(E) ||
      dynamic_cast<StringExpr *>(E) ||
      dynamic_cast<ArrayInitExpr *>(E)) {
    return MorphKind::Valid;
  }

  // Unsafe: Recurse
  if (auto *U = dynamic_cast<UnsafeExpr *>(E)) {
    return getSyntacticMorphology(U->Expression.get());
  }
  return MorphKind::None;
}

bool Sema::checkStrictMorphology(ASTNode *Node, MorphKind Target,
                                 MorphKind Source,
                                 const std::string &TargetName) {
  // 1. Exact Match
  if (Target == Source)
    return true;

  // 2. Safe Constructors (Source is function call/new/etc)
  if (Source == MorphKind::Valid)
    return true;

  // 3. None/None Match (Value types)
  if (Target == MorphKind::None && Source == MorphKind::None)
    return true;

  // 4. Mismatch
  std::string tgtStr = "value";
  if (Target == MorphKind::Raw)
    tgtStr = "*";
  if (Target == MorphKind::Unique)
    tgtStr = "^";
  if (Target == MorphKind::Shared)
    tgtStr = "~";
  if (Target == MorphKind::Ref)
    tgtStr = "&";

  std::string srcStr = "value";
  if (Source == MorphKind::Raw)
    srcStr = "*";
  if (Source == MorphKind::Unique)
    srcStr = "^";
  if (Source == MorphKind::Shared)
    srcStr = "~";
  if (Source == MorphKind::Ref)
    srcStr = "&";

  DiagnosticEngine::report(Node->Loc, DiagID::ERR_MORPHOLOGY_MISMATCH, tgtStr,
                           srcStr);
  HasError = true;
  return false;
}

std::shared_ptr<toka::Type> Sema::checkExprImpl(Expr *E) {
  if (!E)
    return toka::Type::fromString("void");

  if (auto *U = dynamic_cast<UnsetExpr *>(E)) {
    m_LastInitMask = 0;
    return toka::Type::fromString("unknown");
  }

  if (auto *Null = dynamic_cast<NullExpr *>(E)) {
    return toka::Type::fromString("null");
  }

  if (auto *CE = dynamic_cast<ComptimeReflectExpr *>(E)) {
    return toka::Type::fromString("TypeInfo");
  }

  if (auto *CFE = dynamic_cast<ComptimeFieldExpr *>(E)) {
    return toka::Type::fromString("FieldInfo");
  }

  if (auto *None = dynamic_cast<NoneExpr *>(E)) {
    return toka::Type::fromString("none");
  }

  if (dynamic_cast<CharLiteralExpr *>(E)) {
    return toka::Type::fromString("char");
  }

  if (auto *Num = dynamic_cast<NumberExpr *>(E)) {
    if (m_ExpectedType && m_ExpectedType->isInteger()) {
      return m_ExpectedType;
    }
    if (Num->Value > 9223372036854775807ULL)
      return toka::Type::fromString("u64");
    if (Num->Value > 2147483647)
      return toka::Type::fromString("i64");
    return toka::Type::fromString("i32");
  } else if (auto *Flt = dynamic_cast<FloatExpr *>(E)) {
    return toka::Type::fromString("f64");
  } else if (auto *Bool = dynamic_cast<BoolExpr *>(E)) {
    return toka::Type::fromString("bool");
  } else if (auto *Addr = dynamic_cast<AddressOfExpr *>(E)) {
    recordHandleSurfaceAddressOfExpr(*Addr);

    // Toka Spec: &x creates a Reference.
    auto innerObj = checkExpr(Addr->Expression.get());
    if (innerObj->isUnknown())
      return toka::Type::fromString("unknown");

    if (auto *VE = dynamic_cast<VariableExpr *>(Addr->Expression.get())) {
      std::string actualName = VE->Name;
      SymbolInfo *InfoPtr = nullptr;
      if (CurrentScope->findVariableWithDeref(VE->Name, InfoPtr, actualName)) {
        InfoPtr->HasHandleBeenUsed = true;
      }
    }

    // Borrow Tracking
    Expr *scan = Addr->Expression.get();
    // Unwrap Postfix (like x#)
    while (auto *post = dynamic_cast<PostfixExpr *>(scan)) {
      scan = post->LHS.get();
    }

    std::string pathToBorrow = getStringifyPath(scan);
    if (!pathToBorrow.empty()) {
        bool wantMutable = innerObj->IsWritable;
        if (wantMutable) {
            if (!m_ExpectedWritability) {
                wantMutable = false;
            }
        }

        std::string baseVar = pathToBorrow;
        size_t dotPos = baseVar.find('.');
        if (dotPos != std::string::npos) {
            baseVar = baseVar.substr(0, dotPos);
        }

        SymbolInfo *Info = nullptr;
        if (CurrentScope->findSymbol(baseVar, Info)) {
            if (wantMutable && pathToBorrow == baseVar) {
                if (!Info->IsMutable()) {
                    error(Addr, DiagID::ERR_BORROW_IMMUT, baseVar);
                }
            }
            if (!m_InLHS) {
                if (!PALCheckerState.recordBorrow(pathToBorrow, wantMutable)) {
                    error(Addr, DiagID::ERR_BORROW_MUT, pathToBorrow);
                }
            }

            m_LastBorrowSource = pathToBorrow;

            // Member-Level Dependency Extraction
            if (pathToBorrow != baseVar && !Info->FieldDependencySet.empty()) {
                std::string fieldName = pathToBorrow.substr(dotPos + 1);
                size_t nextDot = fieldName.find('.');
                if (nextDot != std::string::npos) fieldName = fieldName.substr(0, nextDot);
                
                if (Info->FieldDependencySet.count(fieldName)) {
                    for (const auto &dep : Info->FieldDependencySet[fieldName]) {
                        m_LastLifeDependencies.insert(dep);
                    }
                    if (!m_LastLifeDependencies.empty()) {
                        m_LastBorrowSource = *m_LastLifeDependencies.begin();
                    }
                } else {
                    m_LastLifeDependencies.insert(pathToBorrow);
                }
            } else {
                m_LastLifeDependencies.insert(pathToBorrow);
            }
        }
    }

    auto refType = std::make_shared<toka::ReferenceType>(innerObj);
    return refType;
  } else if (auto *Idx = dynamic_cast<ArrayIndexExpr *>(E)) {
    return checkIndexExpr(Idx);
  } else if (auto *Clo = dynamic_cast<ClosureExpr *>(E)) {
    return checkClosureExpr(Clo);
  } else if (auto *Rec = dynamic_cast<AnonymousRecordExpr *>(E)) {
    // 1. Infer field types
    std::vector<ShapeMember> members;
    std::set<std::string> seenFields;

    for (auto &f : Rec->Fields) {
      if (seenFields.count(f.first)) {
        error(Rec, DiagID::ERR_DUPLICATE_FIELD, f.first);
      }
      seenFields.insert(f.first);

      auto fieldTypeObj = checkExpr(f.second.get());
      std::string fieldT = fieldTypeObj->toString();
      if (fieldTypeObj->isUnknown())
        return toka::Type::fromString("unknown");

      ShapeMember sm;
      sm.Name = f.first;
      sm.Type = fieldT;
      sm.ResolvedType = fieldTypeObj;
      members.push_back(sm);
    }

    // 2. Generate Unique Type Name
    // Each anonymous record literal creates a distinct Nominal Type.
    std::string UniqueName =
        "__Toka_Anon_Rec_" + std::to_string(AnonRecordCounter++);
    Rec->AssignedTypeName = UniqueName;

    // 3. Create and Register Synthetic ShapeDecl
    // We treat it as a regular Struct
    auto SyntheticShape = std::make_unique<ShapeDecl>(
        false, UniqueName, std::vector<GenericParam>{}, ShapeKind::Struct,
        members);

    // Important: Register in ShapeMap so MemberExpr can find it
    ShapeMap[UniqueName] = SyntheticShape.get();

    // Store ownership
    SyntheticShapes.push_back(std::move(SyntheticShape));

    return toka::Type::fromString(UniqueName);
  } else if (auto *Deref = dynamic_cast<DereferenceExpr *>(E)) {
    recordHandleSurfaceDereferenceExpr(*Deref);

    auto innerObj = checkExpr(Deref->Expression.get());
    if (innerObj->isUnknown())
      return toka::Type::fromString("unknown");

    if (auto ptr = std::dynamic_pointer_cast<toka::PointerType>(innerObj)) {
      return ptr->getPointeeType();
    }
    error(Deref, DiagID::ERR_INVALID_OP, "dereference", innerObj->toString(),
          "void");
    return toka::Type::fromString("unknown");
  } else if (auto *Unary = dynamic_cast<UnaryExpr *>(E)) {
    return checkUnaryExpr(Unary);
  } else if (auto *Str = dynamic_cast<StringExpr *>(E)) {
    if (m_ExpectedType) {
        if (m_ExpectedType->isPointer()) {
            auto pte = m_ExpectedType->getPointeeType();
            if (pte && pte->typeKind == Type::Primitive) {
                std::string pName = pte->getSoulName();
                if (pName == "char" || pName == "i8" || pName == "u8") {
                    return m_ExpectedType;
                }
            }
        }
        std::string soul = m_ExpectedType->getSoulName();
        if (soul == "cstr" || soul == "Addr" || soul == "OAddr") {
            return m_ExpectedType;
        }
    }
    auto t = toka::Type::fromString("cstr");
    return resolveType(t);
  } else if (auto *VStr = dynamic_cast<ViewStringExpr *>(E)) {
      auto t = toka::Type::fromString("str");
      return resolveType(t);
  } else if (auto *ve = dynamic_cast<VariableExpr *>(E)) {
    m_AccessedVariables.insert(ve->Name); // [CLOSURE] Tracker
    if (m_InLHS) {
      std::string actualName = ve->Name;
      SymbolInfo *InfoPtr = nullptr;
      if (CurrentScope->findVariableWithDeref(ve->Name, InfoPtr, actualName)) {
        InfoPtr->HasBeenMutated = true;
      }
    }

    // [NEW] Enforce Suffix Lifecycle Rule: '#' and '$' only allowed in declarations
    // or explicit mutable method invocation caller contexts.
    if (ve->IsValueMutable || ve->IsValueBlocked) {
      if (!m_AllowPermissionSuffix) {
        error(ve, DiagID::ERR_ILLEGAL_MODIFIER_SUFFIX);
      }
    }

    // [Ch 5] Single Hat Principle: Intermediate paths MUST NOT have morphology
    // or permissions Check flags because Lexer splits them from the identifier
    // name.
    if (m_InIntermediatePath) {
      if (ve->IsRawPointer || ve->IsUnique || ve->IsShared) {
        // [Rule] Allow pointer morphology if it's a member base (deref access)
        if (!m_IsMemberBase) {
          error(ve, DiagID::ERR_SEMA_MORPHOLOGY_SYMBOLS_ARE_ONLY_ALLOWED_AT__2, ve->Name);
        }
      }
      if (ve->IsValueMutable || ve->IsValueNullable || ve->IsValueBlocked) {
        // [Rule] Intermediate permission symbols ONLY allowed for
        // pointers/legacy unions as bases
        bool allowed = false;
        if (m_IsMemberBase) {
          SymbolInfo info;
          if (CurrentScope->lookup(ve->Name, info)) {
            if (info.TypeObj) {
              if (info.TypeObj->isPointer()) {
                allowed = true;
              } else if (info.TypeObj->isShape()) {
                std::string soul = info.TypeObj->getSoulName();
                if (ShapeMap.count(soul) &&
                    ShapeMap[soul]->Kind == toka::ShapeKind::Union) {
                  allowed = true;
                }
              }
            }
          }
        }
        if (!allowed) {
          error(ve, DiagID::ERR_SEMA_PERMISSION_SYMBOLS_ARE_ONLY_ALLOWED_AT__2, ve->Name, (ve->IsValueMutable ? "#" : ""));
        }
      }
    }

    SymbolInfo Info;
    std::string actualName = ve->Name;
    bool isImplicitDeref = false;
    
    SymbolInfo *InfoPtr = nullptr;
    if (CurrentScope->findVariableWithDeref(ve->Name, InfoPtr, actualName)) {
      isImplicitDeref = (actualName != ve->Name);
      Info = *InfoPtr;
      ve->IsMorphicExempt = Info.IsMorphicExempt; // [NEW]
      ve->IsImplicitDeref = isImplicitDeref;      // [Fix] Mark AST node
      if (!m_InLHS) {
        InfoPtr->HasBeenUsed = true;
        if (InfoPtr->ImportingDecl) {
          const_cast<ImportDecl*>(InfoPtr->ImportingDecl)->HasBeenUsed = true;
        }
      }
    }

    if (!InfoPtr) {
      if (ve->Name == "f_arg") {
          std::string fnName = CurrentFunction ? CurrentFunction->Name : "NONE";
          std::cerr << "[TRACE] VariableExpr failed to find 'f_arg' in scope! Depth: " << CurrentScope->Depth << " In Function: " << fnName << "\n";
      }
      if (ve->Name == "cb") {
          std::cerr << "[TRACE] VariableExpr failed to find 'cb' in scope! Depth: " << CurrentScope->Depth << "\n";
          // Try to dump the symbol table
          Scope *s = CurrentScope;
          while (s) {
              for (auto &kv : s->Symbols) {
                  std::cerr << "   Scope Level " << s->Depth << " contains " << kv.first << "\n";
              }
              s = s->Parent;
          }
      }
      // [NEW] Surgical Plan: Try resolving as a type (handles Option<i32>)
      auto possible = toka::Type::fromString(ve->Name);
      if (possible && !possible->isUnknown()) {
        auto resolved = resolveType(possible);
        if (auto shapeT =
                std::dynamic_pointer_cast<toka::ShapeType>(resolved)) {
          if (shapeT->isResolved()) {
            // Success: Resolved to a type name (e.g. Option_M_i32)
            return shapeT;
          }
        }
      }

      error(ve, DiagID::ERR_UNDECLARED, ve->Name);
      if (ve->Name.find('-') != std::string::npos) {
        error(ve, DiagID::NOTE_GENERIC, "Did you mean subtraction? Subtraction operator '-' requires spaces around it to avoid ambiguity with kebab-case identifiers.");
      }
      return toka::Type::fromString("unknown");
    }
    if (Info.Moved && !m_InLHS) {
      error(ve, DiagID::ERR_USE_MOVED, actualName);
    }
    // [Fix] Trace to Source for Borrow Check
    SymbolInfo *EffectiveInfo = &Info;
    std::string EffectiveName = actualName;
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

    std::string borrower = "";
    if (!m_ControlFlowStack.empty())
      borrower = m_ControlFlowStack.back().Label;

    std::string conflictPath = "";
    if (!m_InIntermediatePath) {
      if (m_InLHS) {
         conflictPath = PALCheckerState.verifyMutation(actualName);
      } else {
         conflictPath = PALCheckerState.verifyAccess(actualName);
      }
    }
    
    // Usage-time Authorization: A previously defined reference 
    // is authorized to access its own source.
    bool authorized = false;
    if (!conflictPath.empty()) {
       SymbolInfo veInfo;
       if (CurrentScope->lookup(actualName, veInfo)) {
         if (veInfo.BorrowedFrom == conflictPath) {
            authorized = true;
         }
       }
       if (!authorized && !borrower.empty()) {
            SymbolInfo borrowerInfo;
            if (CurrentScope->lookup(borrower, borrowerInfo) && borrowerInfo.BorrowedFrom == conflictPath) {
               authorized = true;
            } else if (actualName == borrower) {
               authorized = true; // writing to myself
            }
       }
    }

    if (!conflictPath.empty() && !authorized) {
       error(ve, DiagID::ERR_BORROW_MUT, conflictPath);
    }

    // [Annotated AST] Constant Substitution: The Core Fix
    // If this variable is a Generic Constant (e.g., S=4, N=10), we MUST NOT
    // allow CodeGen to see a VariableExpr, because it will try to Load it as an
    // LValue. Instead, we perform AST Substitution here and return a NumberExpr
    // (or equivalent).
    if (Info.HasConstValue) {
      // NOTE: We cannot simply return a new NumberExpr *node* here because
      // checkExpr returns a Type object, not an AST node replacement. Wait,
      // checkExpr returns shared_ptr<Type>, but it modifies the AST in-place?
      // No, checkExpr does not take AST** or smart_ptr reference.
      //
      // CRITICAL ARCHITECTURE CORRECTION:
      // Since checkExpr cannot replace the AST node itself (it takes raw
      // pointer), we must rely on the previous approach (HasConstantValue
      // propagation) BUT with a different CodeGen contract, OR we need the
      // CALLER of checkExpr to handle replacement.
      //
      // However, the User's directive was "checkIdentifierExpr returns new
      // NumberExpr". But verify checkExpr signature: it returns
      // `std::shared_ptr<toka::Type>`. It does NOT return an Expr*.
      //
      // Let's look at `checkExprImpl` or `Sema::analyze`?
      // Ah, `checkExpr` is recursive.
      //
      // Let's look at the Caller. Most callers are `checkExpr(E.get())`.
      // E is a `unique_ptr<Expr>`.
      // If we want to substitute, we need to change how `checkExpr` is called
      // or works.
      //
      // PROPOSAL: We implement the 'HasConstantValue' flag-based fix again, BUT
      // we correct CodeGen logic to NOT generate a Load.
      //
      // WAIT, the User said "don't do IsConstant check in CodeGen".
      // That implies AST substitution.
      //
      // Let's check if we can modify the AST in place.
      // We have `VariableExpr *ve`.
      // We can't turn a `VariableExpr` into `NumberExpr` in C++ without
      // changing the pointer.
      //
      // ALTERNATIVE:
      // The user might be thinking of a different compiler architecture (AST
      // Rewriter).
      //
      // Let's stick with the user's intent: "Ensure CodeGen treats it as a
      // number". If I can't swap the node pointer easily here, I must modify
      // CodeGen to *behave* as if it's a number.
      //
      // BUT, I can try to Mutate the VariableExpr to ACT like a NumberExpr?
      // No.
      //
      // LET'S LOOK AT checkStmt or whoever owns the unique_ptr.
      //
      // Actually, looking at `Sema_Expr.cpp`, `checkExpr` takes `Expr *E`.
      //
      // If I cannot replace the node, I MUST use the `HasConstantValue` flag in
      // CodeGen. The user said: "Do not return VariableExpr! ... return new
      // NumberExpr". This implies the user thinks checkExpr returns an Expr*.
      //
      // CHECK SEMANTICS:
      // `std::shared_ptr<toka::Type> Sema::checkExpr(Expr *E)`
      // Correct. It returns Type.
      //
      // So I cannot substitute the node *here*.
      //
      // OPTION 2: The `HasConstantValue` approach IS the right way for this
      // codebase, provided `genVariableExpr` handles it correctly.
      //
      // The user's diagnosis "You generated a Load for a ConstantInt" was
      // correct. My CodeGen fix WAS: `if (var->HasConstantValue) return
      // ConstantInt;` This DOES NOT generate a Load. It generates a Value.
      //
      // So why did it crash?
      // "Upper-level logic... attempts to generate builder.CreateLoad(i32 4)"
      //
      // Ah. `genVariableExpr` returned a `PhysEntity` wrapping the ConstantInt.
      // `PhysEntity` constructor has `bool isAlloca`.
      // If `isAlloca` is false (default), `PhysEntity::load()` returns the
      // value itself.
      //
      // If the caller calls `load()`, it gets `i32 4`.
      // If the caller calls `store()`, it crashes.
      //
      // Where is it crashing?
      // `audit_generics.tk` Line 31: `auto buf = [0; S]`
      //
      // AST: `VariableDecl(buf, RepeatedArrayExpr(0, S))`
      // CodeGen: `genVariableDecl` -> `genRepeatedArrayExpr` -> `genExpr(S)`.
      // `genRepeatedArrayExpr` logic:
      // `if (auto *num = dyn_cast<NumberExpr>(expr->Count)) ...`
      // `else if (auto *var = dyn_cast<VariableExpr>(expr->Count))`
      //
      // In my CodeGen fix, I handled `VariableExpr` with `HasConstantValue`.
      // So `genRepeatedArrayExpr` should grab `var->ConstantValue` (raw int)
      // and use `ArrayType::get`. It does NOT call `genExpr(var)`.
      //
      // So `genVariableExpr` is NOT called for the array size S.
      //
      // So where is the crash?
      // "Control flow reaches end of non-void function" (Exit 1)
      // "Segmentation fault" (Exit 139)
      //
      // When `println(".. S")` was uncommented, it crashed.
      // `println` calls `make_buffer_print` -> `genCallExpr` ->
      // `genExpr(Args)`. `S` is passed to `println`. `println` takes `(fmt,
      // ...)`. Varargs? `println` implementation might expect LValue?
      //
      // `checkIdentifierExpr` DOES NOT EXIST in this codebase.
      // `checkExpr` handles `VariableExpr`.
      //
      // I will implement the Metadata Propagation (`HasConstantValue`) in Sema,
      // AND I will re-implement the CodeGen fix which was correct,
      // BUT I will ensure `PhysEntity` correctly flags it as an RValue.
      //
      // Wait, the user specifically said: "Don't do IsConstant check in
      // CodeGen". They want AST Substitution. To do AST Substitution, I need to
      // find where `Expr` pointers are held. They are held in
      // `unique_ptr<Expr>` inside other AST nodes (Stmt, CallExpr, etc).
      //
      // To substitute, I would need a `Transform` pass or `Mutator`. `Sema` is
      // a pass. `Sema` visits nodes via `checkExpr(E)`.
      //
      // If I cannot replace the node pointer, I must modify the node to behave
      // correctly.
      //
      // Let's execute the User's Plan via a helper method
      // `substituteConstantVars`? Or just accept that `HasConstantValue` IS the
      // way to go, but I need to fix the Crash reason.
      //
      // The crash with `println` might be because `genVariableExpr` returned a
      // ConstantInt, and `genCallExpr` tried to pass it as `...` (VarArg). For
      // VarArgs, `genCallExpr` calls `genExpr`. If `genExpr` returns a
      // ConstantInt RValue, `genCallExpr` handles it?
      //
      // Let's implement the `HasConstantValue` propagation here first.

      ve->HasConstantValue = true;
      ve->ConstantValue = Info.ConstValue;
      ve->ConstantValObj = Info.ConstValObj;
      ve->ResolvedType = Info.TypeObj; // Ensure type is known (e.g. usize)
    }

    // Unset Check: Only check if NOT in LHS
    if (!m_InLHS) {
      bool isFullyInit = true;
      if (Info.InitMask == 0) {
        isFullyInit = false;
      } else if (Info.TypeObj && Info.TypeObj->isShape()) {
        // Check all bits for struct and enum-payload record shapes
        std::string soul = Info.TypeObj->getSoulName();
        if (ShapeMap.count(soul)) {
          ShapeDecl *SD = ShapeMap[soul];
          if (SD->Kind == ShapeKind::Struct || SD->Kind == ShapeKind::Tuple) {
            uint64_t expected = (1ULL << SD->Members.size()) - 1;
            if (SD->Members.size() >= 64)
              expected = ~0ULL;
            if ((Info.InitMask & expected) != expected) {
              isFullyInit = false;
            }
          }
        }
      }

      if (!isFullyInit && !m_AllowUnsetUsage) {
        DiagnosticEngine::report(getLoc(ve), DiagID::ERR_USE_UNSET, ve->Name);
        HasError = true;
      }
    }
    m_LastInitMask = Info.InitMask;
    // [Constitution] Soul Collapse (The Hat-Off Transduction)
    // "Variable name without hat collapses to value (Soul)."
    bool shouldCollapse = true;
    if (m_DisableSoulCollapse || Info.IsMorphicExempt) {
      shouldCollapse = false;
    }

    auto current = Info.TypeObj;
    if (shouldCollapse && current) {
      while (current && (current->isPointer() || current->isReference() ||
                         current->isSmartPointer())) {
        // [Constitution 5.2] LHS Exemption: Nullable pointers collapse on LHS
        // for assignment.
        if (current->IsNullable && !m_InLHS) {
          break;
        }
        auto inner = current->getPointeeType();
        if (!inner)
          break;
        current = inner;
      }
    }

    // [Constitution 1.3] Soul Attributes Rule:
    // Identifiers (x, p) refer to the SOUL. Sigils (*p, ^p) refer to the
    // IDENTITY. Trailing attributes (#, ?) on an identifier always apply to the
    // soul. If we are looking at the soul (either we collapsed or it's a
    // value), apply the attributes. If we are looking at the identity (collapse
    // disabled), keep handle attributes and ignore trailing soul attributes for
    // now.
    if (shouldCollapse || (current && !current->isPointer())) {
      if (current) {
        // [Toka 1.3] Permission View: Inherit inherent mutability.
        // With explicit suffixes banned in expressions, variables inherently exhibit 
        // their declared mutability.
        bool usageMutable = ve->IsValueMutable;
        if (shouldCollapse) {
           if (Info.IsSoulMutable()) usageMutable = true;
        } else {
           if (Info.IsRebindable || Info.IsMutable()) usageMutable = true;
        }

        bool effectiveNull = current->IsNullable || ve->IsValueNullable;
        return current->withAttributes(usageMutable, effectiveNull);
      }
    }

    // Identity view (Collapse disabled)
    if (!shouldCollapse && current) {
      bool identWritable = ve->IsValueMutable;
      if (Info.IsRebindable || Info.IsMutable()) {
        identWritable = true;
      }
      return current->withAttributes(identWritable, current->IsNullable);
    }

    return current;
  } else if (auto *Cast = dynamic_cast<CastExpr *>(E)) {
    auto targetType = resolveType(toka::Type::fromString(Cast->TargetType));
    validateDynTraitObjectSafetyInType(targetType, getLoc(Cast));
    auto srcType = checkExpr(Cast->Expression.get(), targetType);

    // Rule: Numeric Casts (Always allowed for standard numeric types)
    auto srcTypeResolved = resolveType(srcType, true);
    auto targetTypeResolved = resolveType(targetType, true);
    bool srcIsNumeric = srcTypeResolved->isInteger() || srcTypeResolved->isFloatingPoint();
    bool targetIsNumeric =
        targetTypeResolved->isInteger() || targetTypeResolved->isFloatingPoint();

    // Rule: Pointer Morphologies or Addr
    bool srcIsAddr = srcType->isAddrType();
    bool targetIsAddr =
        (Cast->TargetType == "Addr" || resolveType("Addr") == Cast->TargetType);

    bool srcIsRaw = srcType->isRawPointer();
    bool targetIsRaw = targetType->isRawPointer();

    bool srcIsOAddr = srcType->isOAddrType();
    bool targetIsOAddr = (Cast->TargetType == "OAddr" ||
                          resolveType("OAddr") == Cast->TargetType);

    if (targetIsOAddr) {
      // [User Directive] as OAddr is special and always safe.
      // Skip all borrow registration and checks.
      return targetType;
    }

    if (srcIsOAddr && !targetIsOAddr) {
      DiagnosticEngine::report(getLoc(Cast), DiagID::ERR_CAST_MISMATCH,
                               srcType->toString(), Cast->TargetType);
      HasError = true;
    }

    if (srcIsNumeric && targetIsNumeric) {
      // Normal numeric cast, allow.
    } else if (targetType->isReference()) {
      // [Constitution 1.3] Borrow-Cast: var as &Node or var as &var
      // Re-interpreting identity as a direct borrow view.
      auto targetInner = targetType->getPointeeType();
      auto srcInner =
          (srcType->isPointer())
              ? std::dynamic_pointer_cast<toka::PointerType>(srcType)
                    ->getPointeeType()
              : srcType;

      if (!isTypeCompatible(targetInner, srcInner)) {
        error(Cast, DiagID::ERR_CAST_MISMATCH, srcType->toString(),
              Cast->TargetType);
      }

      if (srcType->isReference()) {
        // [Optimization] Source is already a reference.
        // We are just re-interpreting the type, not creating a new borrow.
        // Skip registration.
        return targetType;
      }

      // Semantic Side-Effect: Register this as a borrow
      Expr *Traverse = Cast->Expression.get();
      // Peel Identity Op if present: ^p -> p
      if (auto *UE = dynamic_cast<UnaryExpr *>(Traverse)) {
        if (UE->Op == TokenType::Caret || UE->Op == TokenType::Tilde ||
            UE->Op == TokenType::Star) {
          Traverse = UE->RHS.get();
        }
      }

      while (true) {
        if (auto *M = dynamic_cast<MemberExpr *>(Traverse)) {
          Traverse = M->Object.get();
        } else if (auto *Idx = dynamic_cast<ArrayIndexExpr *>(Traverse)) {
          Traverse = Idx->Array.get();
        } else {
          break;
        }
      }

      if (auto *Var = dynamic_cast<VariableExpr *>(Traverse)) {
        SymbolInfo *Info = nullptr;
        if (CurrentScope->findSymbol(Var->Name, Info)) {
          // [Fix] Trace to Source for Borrow Registration
          SymbolInfo *EffectiveInfo = Info;
          std::string EffectiveName = Var->Name;
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

          if (!m_InLHS) {
            bool isExclusive = targetInner->IsWritable;
            std::string pathToBorrow = getStringifyPath(Var);
            if (!pathToBorrow.empty()) {
               if (!PALCheckerState.recordBorrow(pathToBorrow, isExclusive)) {
                   error(Cast, DiagID::ERR_BORROW_MUT, pathToBorrow);
               }
               m_LastBorrowSource = pathToBorrow;
            }
          }
        }
      }
    } else if (!m_InUnsafeContext && targetType->isSmartPointer() && !srcType->isSmartPointer() &&
               !srcType->isNullType()) {
      error(Cast, DiagID::ERR_SMART_PTR_FROM_STACK, Cast->TargetType[0]);
    } else if (!m_InUnsafeContext && targetIsRaw &&
               (srcType->isUniquePtr() || srcType->isSharedPtr())) {
      error(Cast, DiagID::ERR_SEMA_IDENTITY_INTRUSION_MANAGED_MEMORY_CANNOT, srcType->toString(), Cast->TargetType);
    } else if (targetIsAddr) {
      if (!(srcIsAddr || srcIsRaw || srcIsNumeric || srcType->isUniquePtr() || srcType->isSharedPtr() || srcType->isReference())) {
        error(Cast, DiagID::ERR_CAST_MISMATCH, srcType->toString(),
              Cast->TargetType);
      }
    } else if (srcIsAddr) {
      if (!(targetIsAddr || targetIsRaw || targetIsNumeric)) {
        error(Cast, DiagID::ERR_CAST_MISMATCH, srcType->toString(),
              Cast->TargetType);
      }
    } else if (targetIsOAddr) {
      if (!srcType->isPointer()) {
        error(Cast, DiagID::ERR_CAST_MISMATCH, srcType->toString(),
              Cast->TargetType);
      }
    } else if (targetIsRaw) {
      bool srcIsStr = srcType->isStringType();
      bool srcIsNull = srcType->isNullType();
      if (!(srcIsAddr || srcIsRaw || srcIsNumeric || srcIsStr || srcIsNull || srcType->isUniquePtr() || srcType->isSharedPtr() || srcType->isReference())) {
        error(Cast, DiagID::ERR_CAST_MISMATCH, srcType->toString(),
              Cast->TargetType);
      }
    } else if (srcIsRaw) {
      if (!(targetIsAddr || targetIsRaw || targetIsNumeric || (m_InUnsafeContext && targetType->isSmartPointer()) || targetType->isReference())) {
        error(Cast, DiagID::ERR_CAST_MISMATCH, srcType->toString(),
              Cast->TargetType);
      }
    } else if (!srcType->equals(*targetType)) {
      // Legacy union reinterpretation and enum casting
      auto srcTypeResolved = resolveType(srcType);
      bool srcIsUnion = false;
      bool srcIsEnum = false;
      std::shared_ptr<ShapeType> st = nullptr;
      if (srcTypeResolved->isShape()) {
        st = std::dynamic_pointer_cast<ShapeType>(srcTypeResolved);
        if (st->Decl && st->Decl->Kind == ShapeKind::Union) {
          srcIsUnion = true;
        } else if (st->Decl && st->Decl->Kind == ShapeKind::Enum) {
          srcIsEnum = true;
        }
      }

      if (srcIsEnum && targetIsNumeric) {
        // Enum can be cast to an integer Discriminant. Valid.
      } else if (srcIsUnion) {
        bool found = false;
        for (const auto &M : st->Decl->Members) {
          auto mType = getPhysicalType(M);
          if (mType && targetType->equals(*mType)) {
            found = true;
            break;
          }
        }
        if (!found) {
          error(Cast, DiagID::ERR_CAST_MISMATCH, srcType->toString(),
                Cast->TargetType);
        }
      } else if (!isTypeCompatible(targetType, srcType)) {
        error(Cast, DiagID::ERR_CAST_MISMATCH, srcType->toString(),
              Cast->TargetType);
      }
    }

    return targetType;
  } else if (auto *Bin = dynamic_cast<BinaryExpr *>(E)) {
    return checkBinaryExpr(Bin);
  } else if (auto *ie = dynamic_cast<IfExpr *>(E)) {

    ie->Condition = foldGenericConstant(std::move(ie->Condition));
    if (auto *boolLit = dynamic_cast<BoolExpr*>(ie->Condition.get())) {
        ie->IsComptime = true;
        ie->ComptimeTaken = boolLit->Value;
        
        bool isReceiver = false;
        if (!m_ControlFlowStack.empty()) isReceiver = m_ControlFlowStack.back().IsReceiver;
        m_ControlFlowStack.push_back({"", "void", nullptr, false, isReceiver});
        
        if (ie->ComptimeTaken) {
            checkStmt(ie->Then.get());
        } else if (ie->Else) {
            checkStmt(ie->Else.get());
        }
        
        auto retTypeObj = m_ControlFlowStack.back().ExpectedTypeObj ? m_ControlFlowStack.back().ExpectedTypeObj : toka::Type::fromString("void");
        m_ControlFlowStack.pop_back();
        return retTypeObj;
    }

    auto condTypeObj = checkExpr(ie->Condition.get());
    std::string condType = condTypeObj->toString();

    // Type Narrowing for 'is' check
    std::string narrowedVar;
    SymbolInfo originalInfo;
    bool narrowed = false;

    if (auto *bin = dynamic_cast<BinaryExpr *>(ie->Condition.get())) {
      if (bin->Op == "is") {
        Expr *lhs = bin->LHS.get();
        std::string path = getStringifyPath(lhs);
        if (!path.empty()) {
          m_NarrowedPaths.insert(path);
          narrowed = true;
          narrowedVar = path;
        }

        VariableExpr *varExpr = dynamic_cast<VariableExpr *>(lhs);
        if (!varExpr) {
          if (auto *un = dynamic_cast<UnaryExpr *>(lhs)) {
            varExpr = dynamic_cast<VariableExpr *>(un->RHS.get());
          }
        }

        if (varExpr) {
          SymbolInfo *infoPtr = nullptr;
          if (CurrentScope->findSymbol(varExpr->Name, infoPtr)) {
            if (!narrowed) {
              narrowedVar = varExpr->Name;
              narrowed = true;
            }
            originalInfo = *infoPtr;
            // Sync TypeObj: Narrow type to non-nullable
            if (infoPtr->TypeObj) {
              infoPtr->TypeObj = infoPtr->TypeObj->withAttributes(
                  infoPtr->TypeObj->IsWritable, false);
            }
          }
        }
      }
    }

    bool isReceiver = false;
    if (!m_ControlFlowStack.empty()) {
      isReceiver = m_ControlFlowStack.back().IsReceiver;
    }

    m_ControlFlowStack.push_back({"", "void", nullptr, false, isReceiver});

    // Save Mask & Moved State for Intersection Rule
    std::map<std::string, uint64_t> masksBefore;
    std::map<std::string, bool> movedBefore;
    for (auto &pair : CurrentScope->Symbols) {
      masksBefore[pair.first] = pair.second.InitMask;
      movedBefore[pair.first] = pair.second.Moved;
    }
    auto palBefore = PALCheckerState.snapshot();

    checkStmt(ie->Then.get());

    std::map<std::string, uint64_t> masksThen;
    std::map<std::string, bool> movedThen;
    for (auto &pair : CurrentScope->Symbols) {
      masksThen[pair.first] = pair.second.InitMask;
      movedThen[pair.first] = pair.second.Moved;
    }
    auto palThen = PALCheckerState.snapshot();

    // Restore if narrowed
    if (narrowed) {
      if (CurrentScope->lookup(narrowedVar, originalInfo)) {
        SymbolInfo *infoPtr = nullptr;
        if (CurrentScope->findSymbol(narrowedVar, infoPtr)) {
          *infoPtr = originalInfo;
        }
      }
      m_NarrowedPaths.erase(narrowedVar);
    }

    std::string thenType = m_ControlFlowStack.back().ExpectedType;
    auto thenTypeObj = m_ControlFlowStack.back().ExpectedTypeObj;
    bool thenReturns = allPathsJump(ie->Then.get());
    m_ControlFlowStack.pop_back();

    std::string elseType = "void";
    std::shared_ptr<toka::Type> elseTypeObj;
    bool elseReturns = false;
    if (ie->Else) {
      // Restore Before Else
      for (auto &pair : masksBefore) {
        CurrentScope->Symbols[pair.first].InitMask = pair.second;
      }
      for (auto &pair : movedBefore) {
        CurrentScope->Symbols[pair.first].Moved = pair.second;
      }
      PALCheckerState.restore(palBefore);

      m_ControlFlowStack.push_back({"", "void", nullptr, false, isReceiver});
      checkStmt(ie->Else.get());
      elseType = m_ControlFlowStack.back().ExpectedType;
      elseTypeObj = m_ControlFlowStack.back().ExpectedTypeObj;
      elseReturns = allPathsJump(ie->Else.get());
      auto palElse = PALCheckerState.snapshot();
      m_ControlFlowStack.pop_back();

      // Intersection Rule
      if (thenReturns && elseReturns) {
        // No reachable continuation; keep the incoming PAL state for any
        // subsequent dead-code diagnostics.
        PALCheckerState.restore(palBefore);
      } else if (thenReturns) {
        // State is purely from Else branch. Leave as is.
        PALCheckerState.restore(palElse);
      } else if (elseReturns) {
        // State is purely from Then branch
        for (auto &pair : CurrentScope->Symbols) {
          if (masksThen.count(pair.first))
            pair.second.InitMask = masksThen[pair.first];
          if (movedThen.count(pair.first))
            pair.second.Moved = movedThen[pair.first];
        }
        PALCheckerState.restore(palThen);
      } else {
        // Actual Intersection
        for (auto &pair : CurrentScope->Symbols) {
          uint64_t thenM = masksThen.count(pair.first) ? masksThen[pair.first] : 0;
          pair.second.InitMask &= thenM;
          bool thenMoved = movedThen.count(pair.first) ? movedThen[pair.first] : false;
          pair.second.Moved = pair.second.Moved || thenMoved;
        }
        PALCheckerState.mergeBranches(palBefore, palThen, true, palElse, true);
      }
    } else {
      for (auto &pair : CurrentScope->Symbols) {
        pair.second.InitMask = masksBefore[pair.first];
        if (thenReturns) {
            pair.second.Moved = movedBefore[pair.first];
        } else {
            bool thenMoved = movedThen.count(pair.first) ? movedThen[pair.first] : false;
            pair.second.Moved = movedBefore[pair.first] || thenMoved;
        }
      }
      if (thenReturns) {
        PALCheckerState.restore(palBefore);
      } else {
        PALCheckerState.mergeBranches(palBefore, palThen, true, palBefore, true);
      }
    }

    if (isReceiver) {
      if (thenType == "void" && !allPathsJump(ie->Then.get()))
        error(ie->Then.get(), DiagID::ERR_YIELD_VALUE_REQUIRED, "if branch");
      if (!ie->Else)
        error(ie, DiagID::ERR_YIELD_ELSE_REQUIRED);
      else if (elseType == "void" && !allPathsJump(ie->Else.get()))
        error(ie->Else.get(), DiagID::ERR_YIELD_VALUE_REQUIRED, "else branch");
    }

    if (thenType != "void" && elseType != "void" &&
        !isTypeCompatible(thenTypeObj, elseTypeObj)) {
      error(ie, DiagID::ERR_BRANCH_TYPE_MISMATCH, "If", thenType, elseType);
    }
    return toka::Type::fromString((thenType != "void") ? thenType : elseType);
  } else if (auto *guard = dynamic_cast<GuardExpr *>(E)) {
    auto condType = checkExpr(guard->Condition.get());
    if (condType->isUnknown())
      return condType;

    auto *varExpr = dynamic_cast<VariableExpr *>(guard->Condition.get());
    if (!varExpr) {
      if (auto *unary = dynamic_cast<UnaryExpr *>(guard->Condition.get())) {
        varExpr = dynamic_cast<VariableExpr *>(unary->RHS.get());
      }
    }

    if (!varExpr) {
      error(guard->Condition.get(), DiagID::ERR_SEMA_GUARD_CONDITION_MUST_BE_A_VARIABLE);
      return std::make_shared<VoidType>();
    }

    SymbolInfo *infoPtr = nullptr;
    std::string actualName;

    std::map<std::string, uint64_t> masksBefore;
    std::map<std::string, bool> movedBefore;
    for (auto &pair : CurrentScope->Symbols) {
      masksBefore[pair.first] = pair.second.InitMask;
      movedBefore[pair.first] = pair.second.Moved;
    }
    auto palBefore = PALCheckerState.snapshot();

    auto restoreGuardEntryState = [&]() {
      for (auto &pair : masksBefore) {
        CurrentScope->Symbols[pair.first].InitMask = pair.second;
      }
      for (auto &pair : movedBefore) {
        CurrentScope->Symbols[pair.first].Moved = pair.second;
      }
      PALCheckerState.restore(palBefore);
    };

    auto captureMasks = [&]() {
      std::map<std::string, uint64_t> masks;
      for (auto &pair : CurrentScope->Symbols) {
        masks[pair.first] = pair.second.InitMask;
      }
      return masks;
    };

    auto captureMoved = [&]() {
      std::map<std::string, bool> moved;
      for (auto &pair : CurrentScope->Symbols) {
        moved[pair.first] = pair.second.Moved;
      }
      return moved;
    };

    bool thenJumps = false;
    bool elseJumps = false;
    std::map<std::string, uint64_t> masksThen;
    std::map<std::string, bool> movedThen;
    std::map<std::string, uint64_t> masksElse = masksBefore;
    std::map<std::string, bool> movedElse = movedBefore;
    PALChecker palThen = palBefore;
    PALChecker palElse = palBefore;

    if (CurrentScope->findVariableWithDeref(varExpr->Name, infoPtr, actualName)) {
      bool isPtrNullable = false;
      bool isSoulNullable = false;
      if (auto ptrT = std::dynamic_pointer_cast<toka::PointerType>(condType)) {
        isPtrNullable = ptrT->IsNullable;
      } else if (condType->IsNullable) {
        isSoulNullable = true;
      }

      if (!isPtrNullable && !isSoulNullable && !condType->isVoid()) {
        error(guard->Condition.get(), DiagID::ERR_SEMA_GUARD_CONDITION_MUST_BE_A_NULLABLE_TYPE);
      }

      enterScope();
      SymbolInfo nonNullInfo = *infoPtr;
      if (nonNullInfo.TypeObj) {
        nonNullInfo.TypeObj = nonNullInfo.TypeObj->withAttributes(nonNullInfo.TypeObj->IsWritable, false, nonNullInfo.TypeObj->IsBlocked);
      }
      CurrentScope->define(actualName, nonNullInfo);

      checkStmt(guard->Then.get());
      exitScope();
      thenJumps = allPathsJump(guard->Then.get());
      masksThen = captureMasks();
      movedThen = captureMoved();
      palThen = PALCheckerState.snapshot();
    } else {
      enterScope();
      checkStmt(guard->Then.get());
      exitScope();
      thenJumps = allPathsJump(guard->Then.get());
      masksThen = captureMasks();
      movedThen = captureMoved();
      palThen = PALCheckerState.snapshot();
    }

    restoreGuardEntryState();
    if (guard->Else) {
      enterScope();
      checkStmt(guard->Else.get());
      exitScope();
      elseJumps = allPathsJump(guard->Else.get());
      masksElse = captureMasks();
      movedElse = captureMoved();
      palElse = PALCheckerState.snapshot();
    }

    if (guard->Else) {
      if (thenJumps && elseJumps) {
        restoreGuardEntryState();
      } else if (thenJumps) {
        for (auto &pair : CurrentScope->Symbols) {
          if (masksElse.count(pair.first))
            pair.second.InitMask = masksElse[pair.first];
          if (movedElse.count(pair.first))
            pair.second.Moved = movedElse[pair.first];
        }
        PALCheckerState.restore(palElse);
      } else if (elseJumps) {
        for (auto &pair : CurrentScope->Symbols) {
          if (masksThen.count(pair.first))
            pair.second.InitMask = masksThen[pair.first];
          if (movedThen.count(pair.first))
            pair.second.Moved = movedThen[pair.first];
        }
        PALCheckerState.restore(palThen);
      } else {
        for (auto &pair : CurrentScope->Symbols) {
          uint64_t thenMask = masksThen.count(pair.first) ? masksThen[pair.first] : 0;
          uint64_t elseMask = masksElse.count(pair.first) ? masksElse[pair.first] : 0;
          pair.second.InitMask = thenMask & elseMask;
          bool thenMoved = movedThen.count(pair.first) ? movedThen[pair.first] : false;
          bool elseMoved = movedElse.count(pair.first) ? movedElse[pair.first] : false;
          pair.second.Moved = thenMoved || elseMoved;
        }
        PALCheckerState.mergeBranches(palBefore, palThen, true, palElse, true);
      }
    } else {
      if (thenJumps) {
        restoreGuardEntryState();
      } else {
        for (auto &pair : CurrentScope->Symbols) {
          uint64_t thenMask = masksThen.count(pair.first) ? masksThen[pair.first] : 0;
          uint64_t entryMask = masksBefore.count(pair.first) ? masksBefore[pair.first] : 0;
          pair.second.InitMask = thenMask & entryMask;
          bool thenMoved = movedThen.count(pair.first) ? movedThen[pair.first] : false;
          bool entryMoved = movedBefore.count(pair.first) ? movedBefore[pair.first] : false;
          pair.second.Moved = thenMoved || entryMoved;
        }
        PALCheckerState.mergeBranches(palBefore, palThen, true, palBefore, true);
      }
    }

    return std::make_shared<VoidType>();
  } else if (auto *le = dynamic_cast<LoopExpr *>(E)) {
    if (le->Condition) {
      auto condTy = checkExpr(le->Condition.get());
      if (condTy && !condTy->isBoolean()) {
        error(le->Condition.get(), DiagID::ERR_OPERAND_TYPE_MISMATCH, "loop condition", "bool", condTy->toString());
      }
      std::set<std::string> conditionVars;
      collectVariables(le->Condition.get(), conditionVars);
      bool isWarningExempt = false;
      if (le->Loc.isValid()) {
        std::string path = DiagnosticEngine::SrcMgr->getFullSourceLoc(le->Loc).FileName;
        if (path.find("tests/") != std::string::npos ||
            path.find("build.tk") != std::string::npos ||
            path.find("prelude") != std::string::npos ||
            path.find("lib/") != std::string::npos) {
          isWarningExempt = true;
        }
      }
      if (!isWarningExempt) {
        bool anyMutated = false;
        bool hasCheckableVar = false;
        std::string firstCheckableVar;
        for (const auto &varName : conditionVars) {
          SymbolInfo info;
          if (CurrentScope->lookup(varName, info)) {
            if (info.IsDeclaredVariable && !info.HasConstValue) {
              hasCheckableVar = true;
              if (firstCheckableVar.empty()) {
                firstCheckableVar = varName;
              }
              if (isVariableMutated(le->Body.get(), varName) || isVariableMutated(le->Condition.get(), varName)) {
                anyMutated = true;
                break;
              }
            }
          }
        }
        if (hasCheckableVar && !anyMutated) {
          DiagnosticEngine::report(le->Loc, DiagID::WARN_NON_PROGRESS_LOOP, firstCheckableVar);
        }
      }
    }

    bool isReceiver = false;
    if (!m_ControlFlowStack.empty()) {
      isReceiver = m_ControlFlowStack.back().IsReceiver;
    }

    if (isReceiver && (!m_ControlFlowStack.empty() && m_ControlFlowStack.back().ExpectedType != "void")) {
      error(le, DiagID::ERR_SEMA_TOKA_1_0_DOES_NOT_SUPPORT_YIELDING_VALUES);
    }

    bool tookOver = false;
    if (!m_ControlFlowStack.empty() && !m_ControlFlowStack.back().IsLoop &&
        !m_ControlFlowStack.back().Label.empty()) {
      m_ControlFlowStack.back().IsLoop = true;
      m_ControlFlowStack.back().IsReceiver = isReceiver;
      tookOver = true;
    } else {
      m_ControlFlowStack.push_back({"", "void", nullptr, true, isReceiver});
    }
    size_t loopFlowIndex = m_ControlFlowStack.size() - 1;

    std::map<std::string, uint64_t> masksBefore;
    std::map<std::string, bool> movedBefore;
    for (auto &pair : CurrentScope->Symbols) {
      masksBefore[pair.first] = pair.second.InitMask;
      movedBefore[pair.first] = pair.second.Moved;
    }
    auto visibleUniqueMovedBefore = captureVisibleUniqueMoved(CurrentScope);
    auto palBefore = PALCheckerState.snapshot();

    enterScope();
    CurrentScope->IsLoop = true;
    checkStmt(le->Body.get());
    exitScope();
    bool bodyJumps = allPathsJump(le->Body.get());
    auto breakStates = m_ControlFlowStack[loopFlowIndex].BreakStates;
    m_ControlFlowStack[loopFlowIndex].BreakStates.clear();
    auto continueStates = m_ControlFlowStack[loopFlowIndex].ContinueStates;
    m_ControlFlowStack[loopFlowIndex].ContinueStates.clear();

    if (!continueStates.empty()) {
      std::vector<AnalysisState> loopBackStates;
      if (!bodyJumps)
        loopBackStates.push_back(captureAnalysisState());
      loopBackStates.insert(loopBackStates.end(), continueStates.begin(),
                            continueStates.end());
      mergeAnalysisStates(loopBackStates, palBefore);
    }

    for (const auto &name :
         collectLoopEscapingMoves(CurrentScope, visibleUniqueMovedBefore)) {
      error(le, DiagID::ERR_SEMA_CANNOT_CEDE_MOVE_VALUE_INSIDE_A_LOOP_BECA,
            getDisplayVariableName(name));
    }

    if (le->Condition) {
      std::map<std::string, uint64_t> masksBody;
      std::map<std::string, bool> movedBody;
      for (auto &pair : CurrentScope->Symbols) {
        masksBody[pair.first] = pair.second.InitMask;
        movedBody[pair.first] = pair.second.Moved;
      }
      auto palBody = PALCheckerState.snapshot();

      for (auto &pair : CurrentScope->Symbols) {
        uint64_t entryMask =
            masksBefore.count(pair.first) ? masksBefore[pair.first] : 0;
        uint64_t bodyMask =
            masksBody.count(pair.first) ? masksBody[pair.first] : 0;
        pair.second.InitMask = entryMask & bodyMask;

        bool entryMoved =
            movedBefore.count(pair.first) ? movedBefore[pair.first] : false;
        bool bodyMoved =
            movedBody.count(pair.first) ? movedBody[pair.first] : false;
        pair.second.Moved = entryMoved || bodyMoved;
      }
      PALCheckerState.mergeBranches(palBefore, palBefore, true, palBody, true);
    }

    if (!breakStates.empty()) {
      std::vector<AnalysisState> afterStates;
      if (le->Condition)
        afterStates.push_back(captureAnalysisState());
      afterStates.insert(afterStates.end(), breakStates.begin(),
                         breakStates.end());
      mergeAnalysisStates(afterStates, palBefore);
    }

    if (!tookOver)
      m_ControlFlowStack.pop_back();

    return std::make_shared<VoidType>();
  } else if (auto *fe = dynamic_cast<ForExpr *>(E)) {
    // [Phase 2] Comptime Macro Unroll Detection
    bool isMacroUnroll = false;
    std::string ReflectedShapeName = "";
    if (auto *Memb = dynamic_cast<MemberExpr *>(fe->Collection.get())) {
      Memb->Object = foldGenericConstant(std::move(Memb->Object));
      if (Memb->Member == "fields") {
        if (auto *CRE = dynamic_cast<ComptimeReflectExpr *>(Memb->Object.get())) {
          isMacroUnroll = true;
          ReflectedShapeName = CRE->ReflectedTypeStr;
        }
      }
    }

    if (isMacroUnroll) {
      fe->IsComptimeUnrolled = true;
      auto resolvedObj = resolveType(toka::Type::fromString(ReflectedShapeName), true);
      std::string targetSoul = resolvedObj->getSoulName();
      if (ShapeMap.count(targetSoul)) {
        auto *SD = ShapeMap[targetSoul];
        uint64_t currentOffset = 0;
        for (const auto &member : SD->Members) {
          auto clonedBody = cloneNode(fe->Body);
          enterScope();
          SymbolInfo Info;
          Info.TypeObj = toka::Type::fromString("FieldInfo");
          Info.IsComptimeField = true;
          Info.ComptimeFieldName = member.Name;
          Info.ComptimeFieldTypeStr = member.Type;
          Info.ComptimeFieldOffset = currentOffset;
          Info.ComptimeFieldSize = 8; // Standard word approximation
          
          CurrentScope->define(fe->VarName, Info);
          checkStmt(clonedBody.get());
          fe->UnrolledBodies.push_back(std::move(clonedBody));
          exitScope();
          currentOffset += 8;
        }
      } else {
        error(fe, DiagID::ERR_SEMA_CANNOT_REFLECT_UNINSTANTIATED_OR_PRIMITIV, targetSoul);
      }
      return toka::Type::fromString("void");
    }

    auto collTypeObj = checkExpr(fe->Collection.get());
    std::string collType = collTypeObj->toString();
    std::string elemType = "void"; // fallback
    std::string soulType = collTypeObj->getSoulType()->toString();

    bool isArray = false;
    // 1. Array Fast Path Identification
    if (collType.size() > 2 && collType.substr(0, 2) == "[]") {
        isArray = true;
        elemType = collType.substr(2);
    } else if (!collType.empty() && collType.front() == '[') {
        size_t semi = collType.find_last_of(';');
        if (semi != std::string::npos) {
            isArray = true;
            elemType = collType.substr(1, semi - 1);
        } else {
            size_t endBracket = collType.find(']');
            if (endBracket != std::string::npos) {
                isArray = true;
                elemType = collType.substr(endBracket + 1);
            }
        }
    }

    std::string fullType = "void";

    if (isArray) {
        // Array emulation of next/next_ref
        fullType = fe->MorphologyPrefix + elemType;
        if (fe->IsMutable) fullType += "#";
                       fe->IterElementType = fullType;
    } else {
        // 2. Iterator Protocol Method Lookup
        std::string baseSoulType = toka::Type::stripMorphology(soulType);
        if (!MethodMap.count(baseSoulType) || !MethodMap[baseSoulType].count("iter")) {
            error(fe->Collection.get(), DiagID::ERR_SEMA_TYPE_DOES_NOT_IMPLEMENT_ITERATOR_PROTOCOL, soulType);
            fullType = "i32";
        } else {
            std::string iterObjStr = MethodMap[baseSoulType]["iter"];
            iterObjStr = resolveType(iterObjStr, false);
              auto iterObj = toka::Type::fromString(iterObjStr);
            auto iterSoul = iterObj->getSoulType()->toString();
            std::string baseIterSoul = toka::Type::stripMorphology(iterSoul);
            
            // 1. Peek at next() to see what the element type is
            std::string E_type = "";
            if (MethodMap.count(baseIterSoul) && MethodMap[baseIterSoul].count("next")) {
                std::string nextRetStr = MethodMap[baseIterSoul]["next"];
                if (nextRetStr.size() > 7 && nextRetStr.substr(0, 7) == "Option<") {
                    E_type = nextRetStr.substr(7, nextRetStr.size() - 8);
                }
            }
            if (E_type.empty()) {
                error(fe, DiagID::ERR_SEMA_ITERATOR_PROTOCOL_REQUIRES_NEXT_TO_RETURN);
                fullType = "i32";
            } else {
                // 2. Compare user's morphology prefix with E's morphology to determine intent
                size_t prefRef = 0;
                for (char c : fe->MorphologyPrefix) { if (c == '&') prefRef++; else break; }
                size_t eRef = 0;
                for (char c : E_type) { if (c == '&') eRef++; else break; }
                
                bool expectsRef = false;
                if (prefRef > eRef) {
                    expectsRef = true;
                } else if (!fe->MorphologyPrefix.empty()) {
                     expectsRef = false;
                } else {
                     expectsRef = fe->IsReference; // fallback
                }
                
                fe->IsReference = expectsRef;
                std::string nextMethodName = expectsRef ? "next_ref" : "next";
                
                if (!MethodMap[iterSoul].count(nextMethodName)) {
                     error(fe, DiagID::ERR_SEMA_ITERATOR_FOR_DOES_NOT_SUPPORT, soulType, (expectsRef ? "borrow semantics (.next_ref())" : "value semantics (.next())"));
                     fullType = "i32";
                } else {
                     std::string nextRetStr = MethodMap[iterSoul][nextMethodName];
                     if (nextRetStr.size() > 7 && nextRetStr.substr(0, 7) == "Option<") {
                         std::string payload = nextRetStr.substr(7, nextRetStr.size() - 8);
                         fullType = payload;
                         if (fe->IsMutable) fullType += "#";
                           fe->IterElementType = fullType;
                     } else {
                         error(fe, DiagID::ERR_SEMA_ITERATOR_PROTOCOL_REQUIRES_TO_RETURN_OPTI, nextMethodName);
                         fullType = "i32";
                     }
                }
            }
        }
    }

    auto masksBefore = captureVisibleInitMasks(CurrentScope);
    auto movedBefore = captureVisibleMoved(CurrentScope);
    auto visibleUniqueMovedBefore = captureVisibleUniqueMoved(CurrentScope);
    auto palBefore = PALCheckerState.snapshot();

    enterScope();
    CurrentScope->IsLoop = true;
    SymbolInfo Info;
    Info.TypeObj = toka::Type::fromString(fullType);
    CurrentScope->define(fe->VarName, Info);

    bool isReceiver = false;
    if (!m_ControlFlowStack.empty()) {
      isReceiver = m_ControlFlowStack.back().IsReceiver;
    }

    bool tookOver = false;
    if (!m_ControlFlowStack.empty() && !m_ControlFlowStack.back().IsLoop &&
        !m_ControlFlowStack.back().Label.empty()) {
      m_ControlFlowStack.back().IsLoop = true;
      m_ControlFlowStack.back().IsReceiver = isReceiver; // Sync receiver status
      tookOver = true;
    } else {
      m_ControlFlowStack.push_back({"", "void", nullptr, true, isReceiver});
    }
    size_t loopFlowIndex = m_ControlFlowStack.size() - 1;
    checkStmt(fe->Body.get());
    std::string bodyType = m_ControlFlowStack.back().ExpectedType;
    auto bodyTypeObj = m_ControlFlowStack.back().ExpectedTypeObj;
    bool bodyJumps = allPathsJump(fe->Body.get());
    auto breakStates = m_ControlFlowStack[loopFlowIndex].BreakStates;
    m_ControlFlowStack[loopFlowIndex].BreakStates.clear();
    auto continueStates = m_ControlFlowStack[loopFlowIndex].ContinueStates;
    m_ControlFlowStack[loopFlowIndex].ContinueStates.clear();
    bool bodyContinuesLoop = !bodyJumps || !continueStates.empty();

    if (!continueStates.empty()) {
      std::vector<AnalysisState> loopBackStates;
      if (!bodyJumps)
        loopBackStates.push_back(captureAnalysisState());
      loopBackStates.insert(loopBackStates.end(), continueStates.begin(),
                            continueStates.end());
      mergeAnalysisStates(loopBackStates, palBefore);
    }

    for (const auto &name :
         collectLoopEscapingMoves(CurrentScope, visibleUniqueMovedBefore)) {
      error(fe, DiagID::ERR_SEMA_CANNOT_CEDE_MOVE_VALUE_INSIDE_A_LOOP_BECA,
            getDisplayVariableName(name));
    }

    if (!tookOver)
      m_ControlFlowStack.pop_back();
    exitScope();
    auto masksBody = captureVisibleInitMasks(CurrentScope);
    auto movedBody = captureVisibleMoved(CurrentScope);
    auto palBody = PALCheckerState.snapshot();

    std::string elseType = "void";
    std::shared_ptr<toka::Type> elseTypeObj;
    bool elseJumps = false;
    std::map<std::string, uint64_t> masksElse = masksBefore;
    std::map<std::string, bool> movedElse = movedBefore;
    PALChecker palElse = palBefore;
    if (fe->ElseBody) {
      restoreVisibleAnalysisState(CurrentScope, masksBefore, movedBefore);
      PALCheckerState.restore(palBefore);

      m_ControlFlowStack.push_back({"", "void", nullptr, false, isReceiver});
      checkStmt(fe->ElseBody.get());
      elseType = m_ControlFlowStack.back().ExpectedType;
      elseTypeObj = m_ControlFlowStack.back().ExpectedTypeObj;
      elseJumps = allPathsJump(fe->ElseBody.get());
      masksElse = captureVisibleInitMasks(CurrentScope);
      movedElse = captureVisibleMoved(CurrentScope);
      palElse = PALCheckerState.snapshot();
      m_ControlFlowStack.pop_back();
    }

    if (fe->ElseBody) {
      if (!bodyContinuesLoop && elseJumps) {
        restoreVisibleAnalysisState(CurrentScope, masksBefore, movedBefore);
        PALCheckerState.restore(palBefore);
      } else if (!bodyContinuesLoop) {
        restoreVisibleAnalysisState(CurrentScope, masksElse, movedElse);
        PALCheckerState.restore(palElse);
      } else if (elseJumps) {
        restoreVisibleAnalysisState(CurrentScope, masksBody, movedBody);
        PALCheckerState.restore(palBody);
      } else {
        for (auto &pair : masksBefore) {
          SymbolInfo *info = nullptr;
          if (!CurrentScope->findSymbol(pair.first, info) || !info)
            continue;
          uint64_t bodyMask =
              masksBody.count(pair.first) ? masksBody[pair.first] : 0;
          uint64_t elseMask =
              masksElse.count(pair.first) ? masksElse[pair.first] : 0;
          info->InitMask = bodyMask & elseMask;
          bool bodyMoved =
              movedBody.count(pair.first) ? movedBody[pair.first] : false;
          bool elseMoved =
              movedElse.count(pair.first) ? movedElse[pair.first] : false;
          info->Moved = bodyMoved || elseMoved;
        }
        PALCheckerState.mergeBranches(palBefore, palBody, true, palElse, true);
      }
    } else {
      if (!bodyContinuesLoop) {
        restoreVisibleAnalysisState(CurrentScope, masksBefore, movedBefore);
        PALCheckerState.restore(palBefore);
      } else {
        for (auto &pair : masksBefore) {
          SymbolInfo *info = nullptr;
          if (!CurrentScope->findSymbol(pair.first, info) || !info)
            continue;
          uint64_t entryMask = pair.second;
          uint64_t bodyMask =
              masksBody.count(pair.first) ? masksBody[pair.first] : 0;
          info->InitMask = entryMask & bodyMask;
          bool entryMoved =
              movedBefore.count(pair.first) ? movedBefore[pair.first] : false;
          bool bodyMoved =
              movedBody.count(pair.first) ? movedBody[pair.first] : false;
          info->Moved = entryMoved || bodyMoved;
        }
        PALCheckerState.mergeBranches(palBefore, palBody, true, palBefore, true);
      }
    }

    if (!breakStates.empty()) {
      bool normalAfterReachable = !fe->ElseBody || !(bodyJumps && elseJumps);
      std::vector<AnalysisState> afterStates;
      if (normalAfterReachable)
        afterStates.push_back(captureAnalysisState());
      afterStates.insert(afterStates.end(), breakStates.begin(),
                         breakStates.end());
      mergeAnalysisStates(afterStates, palBefore);
    }

    if (isReceiver) {
      if (bodyType == "void" && !bodyJumps)
        error(fe->Body.get(), DiagID::ERR_YIELD_VALUE_REQUIRED, "for loop");
      if (!fe->ElseBody)
        error(fe, DiagID::ERR_YIELD_OR_REQUIRED, "for");
      else if (elseType == "void" && !elseJumps)
        error(fe->ElseBody.get(), DiagID::ERR_YIELD_VALUE_REQUIRED,
              "'or' block");
    }

    if (bodyType != "void" && !fe->ElseBody) {
      error(fe, DiagID::ERR_YIELD_OR_REQUIRED, "for");
    }
    if (bodyType != "void" && elseType != "void" &&
        !isTypeCompatible(bodyTypeObj, elseTypeObj)) {
      error(fe, DiagID::ERR_BRANCH_TYPE_MISMATCH, "For loop", bodyType,
            elseType);
    }
    return toka::Type::fromString((bodyType != "void") ? bodyType : elseType);
  } else if (auto *ce = dynamic_cast<CedeExpr *>(E)) {
    auto innerTy = checkExpr(ce->Value.get());
    
    // [Fix] Enforce tracking move semantics and borrow check for `cede` expression universally.
    if (ce->Value) {
      std::string pathToMove = getStringifyPath(ce->Value.get());
      if (!pathToMove.empty()) {
          std::string conflictPath = PALCheckerState.verifyMutation(pathToMove);
          if (!conflictPath.empty()) {
              error(ce, DiagID::ERR_MOVE_BORROWED, conflictPath);
          }
          size_t dotPos = pathToMove.find('.');
          if (dotPos != std::string::npos) {
            std::string rootName = pathToMove.substr(0, dotPos);
            SymbolInfo *RootInfo = nullptr;
            std::string actualRootName;
            if (CurrentScope->findVariableWithDeref(rootName, RootInfo,
                                                    actualRootName)) {
              if (RootInfo && RootInfo->IsFunctionParameter &&
                  RootInfo->IsCeded) {
                CurrentScope->markMoved(actualRootName);
              }
            }
          }
      }

      Expr *underlying = ce->Value.get();
      while (true) {
        if (auto *un = dynamic_cast<UnaryExpr *>(underlying)) {
          underlying = un->RHS.get();
        } else {
          break;
        }
      }
      if (auto *Var = dynamic_cast<VariableExpr *>(underlying)) {
        SymbolInfo *Info = nullptr;
        std::string actualName;
        if (CurrentScope->findVariableWithDeref(Var->Name, Info, actualName)) {
            if (Info->IsFunctionParameter && !Info->IsCeded) {
                error(ce, DiagID::ERR_SEMA_CANNOT_CEDE_NON_CEDE_PARAMETER, Var->Name);
            }
            CurrentScope->markMoved(actualName);
        }
      }
    }
    if (!innerTy) return nullptr;
    if (auto ptrTy = std::dynamic_pointer_cast<toka::PointerType>(innerTy)) {
        if (m_ExpectedType && !std::dynamic_pointer_cast<toka::PointerType>(m_ExpectedType)) {
            innerTy = ptrTy->PointeeType;
        }
    }
    ce->ResolvedType = innerTy;
    return innerTy;
  } else if (auto *se = dynamic_cast<SizeOfExpr *>(E)) {
    se->TypeStr = resolveType(se->TypeStr);
    se->ResolvedType = toka::Type::fromString("usize");
    return se->ResolvedType;
  } else if (auto *pe = dynamic_cast<PassExpr *>(E)) {
    // 1. Detect if this is a prefix 'pass' (wrapping a complex expression)
    bool isPrefixMatch = dynamic_cast<MatchExpr *>(pe->Value.get());
    bool isPrefixIf = dynamic_cast<IfExpr *>(pe->Value.get());
    bool isPrefixFor = dynamic_cast<ForExpr *>(pe->Value.get());
    bool isPrefixLoop = dynamic_cast<LoopExpr *>(pe->Value.get());

    std::string valType = "void";
    std::shared_ptr<toka::Type> valTypeObj;
    if (isPrefixMatch || isPrefixIf || isPrefixFor ||
        isPrefixLoop) {
      m_ControlFlowStack.push_back({"", "void", nullptr, false, true});
      valTypeObj = checkExpr(pe->Value.get());
      valType = valTypeObj->toString();
      m_ControlFlowStack.pop_back();

      if (valType == "void") {
        error(pe, DiagID::ERR_SEMA_PREFIX_PASS_EXPECTS_A_VALUE_YIELDING_EXPR);
      }
    } else {
      valTypeObj = checkExpr(pe->Value.get());
      valType = valTypeObj->toString();
    }

    // Escape Blockade: Check for Dirty Reference
    if (pe->Value) {
      if (auto *Var = dynamic_cast<VariableExpr *>(pe->Value.get())) {
        SymbolInfo *info = nullptr;
        if (CurrentScope->findSymbol(Var->Name, info)) {
          if (info->IsReference() && info->DirtyReferentMask != ~0ULL) {
            DiagnosticEngine::report(getLoc(pe), DiagID::ERR_ESCAPE_UNSET,
                                     Var->Name);
            HasError = true;
          }
        }
      }
    }

    bool foundReceiver = false;
    if (!m_ControlFlowStack.empty()) {
      for (auto it = m_ControlFlowStack.rbegin();
           it != m_ControlFlowStack.rend(); ++it) {
        if (it->IsReceiver) {
          foundReceiver = true;
          if (it->ExpectedType == "void") {
            it->ExpectedType = valType;
            it->ExpectedTypeObj = valTypeObj;
          } else if (!isTypeCompatible(it->ExpectedTypeObj, valTypeObj)) {
            error(pe, DiagID::ERR_TYPE_MISMATCH, valType, it->ExpectedType);
          }
          break;
        }
      }
    }

    if (!foundReceiver) {
      error(pe, DiagID::ERR_PASS_NO_RECEIVER);
    }

    return toka::Type::fromString((isPrefixMatch || isPrefixIf || isPrefixFor ||
                                   isPrefixLoop)
                                      ? valType
                                      : "void");
  } else if (auto *be = dynamic_cast<BreakExpr *>(E)) {
    std::string valType = "void";
    std::shared_ptr<toka::Type> valTypeObj;
    if (be->Value) {
      error(be, DiagID::ERR_SEMA_TOKA_1_0_DOES_NOT_SUPPORT_YIELDING_VALU_2);
      valTypeObj = checkExpr(be->Value.get());
      valType = valTypeObj->toString();

      // Escape Blockade: Check for Dirty Reference
      if (auto *Var = dynamic_cast<VariableExpr *>(be->Value.get())) {
        SymbolInfo *info = nullptr;
        if (CurrentScope->findSymbol(Var->Name, info)) {
          if (info->IsReference() && info->DirtyReferentMask != ~0ULL) {
            DiagnosticEngine::report(getLoc(be), DiagID::ERR_ESCAPE_UNSET,
                                     Var->Name);
            HasError = true;
          }
        }
      }
    }

    ControlFlowInfo *target = nullptr;
    if (be->TargetLabel.empty()) {
      for (auto it = m_ControlFlowStack.rbegin();
           it != m_ControlFlowStack.rend(); ++it) {
        if (it->IsLoop) {
          target = &(*it);
          break;
        }
      }
    } else {
      for (auto it = m_ControlFlowStack.rbegin();
           it != m_ControlFlowStack.rend(); ++it) {
        if (it->Label == be->TargetLabel) {
          target = &(*it);
          break;
        }
      }
    }

    if (target) {
      target->BreakStates.push_back(captureAnalysisState());
      if (valType != "void") {
        if (target->ExpectedType == "void") {
          target->ExpectedType = valType;
          target->ExpectedTypeObj = valTypeObj;
        } else if (!isTypeCompatible(target->ExpectedTypeObj, valTypeObj)) {
          error(be, DiagID::ERR_TYPE_MISMATCH, valType, target->ExpectedType);
        }
      }
    }
    return toka::Type::fromString("void");
  } else if (auto *ce = dynamic_cast<ContinueExpr *>(E)) {
    // Continue target must be a loop
    ControlFlowInfo *target = nullptr;
    if (ce->TargetLabel.empty()) {
      for (auto it = m_ControlFlowStack.rbegin();
           it != m_ControlFlowStack.rend(); ++it) {
        if (it->IsLoop) {
          target = &(*it);
          break;
        }
      }
    } else {
      for (auto it = m_ControlFlowStack.rbegin();
           it != m_ControlFlowStack.rend(); ++it) {
        if (it->Label == ce->TargetLabel && it->IsLoop) {
          target = &(*it);
          break;
        }
      }
    }

    if (target)
      target->ContinueStates.push_back(captureAnalysisState());
    return toka::Type::fromString("void");
  } else if (auto *Call = dynamic_cast<CallExpr *>(E)) {
    return checkCallExpr(Call);
  } else if (auto *awaitEx = dynamic_cast<AwaitExpr *>(E)) {
    bool oldConsuming = m_IsConsumingEffect;
    m_IsConsumingEffect = true;
    auto innerType = checkExpr(awaitEx->Expression.get());
    m_IsConsumingEffect = oldConsuming;
    
    std::string tName = innerType->toString();
    if (tName.find("TaskHandle_M_") != std::string::npos) {
        size_t pos = tName.find("TaskHandle_M_");
        std::string inner = tName.substr(pos + 13);
        awaitEx->ResolvedType = toka::Type::fromString(inner);
        return awaitEx->ResolvedType;
    }
    error(E, DiagID::ERR_SEMA_CANNOT_AWAIT_A_NON_TASKHANDLE_TYPE, tName);
    return toka::Type::fromString("unknown");
  } else if (auto *waitEx = dynamic_cast<WaitExpr *>(E)) {
    bool oldConsuming = m_IsConsumingEffect;
    m_IsConsumingEffect = true;
    auto innerType = checkExpr(waitEx->Expression.get());
    m_IsConsumingEffect = oldConsuming;
    
    std::string tName = innerType->toString();
    if (tName.find("TaskHandle_M_") != std::string::npos) {
        size_t pos = tName.find("TaskHandle_M_");
        std::string inner = tName.substr(pos + 13);
        waitEx->ResolvedType = toka::Type::fromString(inner);
        return waitEx->ResolvedType;
    }
    error(E, DiagID::ERR_SEMA_CANNOT_WAIT_ON_A_NON_TASKHANDLE_TYPE, tName);
    return toka::Type::fromString("unknown");
  } else if (auto *AIE = dynamic_cast<ArrayInitExpr *>(E)) {
    auto expectedType = toka::Type::fromString(resolveType(AIE->Type));
    if (AIE->ArraySize) checkExpr(AIE->ArraySize.get());
    if (AIE->Initializer) checkExpr(AIE->Initializer.get(), expectedType);
    return toka::Type::fromString("[" + resolveType(AIE->Type) + "]");
  } else if (auto *New = dynamic_cast<NewExpr *>(E)) {
    std::string resolvedName = resolveType(New->Type);

    // [New] Generic Inference for 'new'
    if (ShapeMap.count(resolvedName)) {
      ShapeDecl *SD = ShapeMap[resolvedName];
      if (!SD->GenericParams.empty() && m_ExpectedType) {
        auto ptrTy =
            std::dynamic_pointer_cast<toka::PointerType>(m_ExpectedType);
        if (ptrTy) {
          auto expShape =
              std::dynamic_pointer_cast<toka::ShapeType>(ptrTy->PointeeType);
          if (expShape && (expShape->Name == SD->Name ||
                           expShape->Name.find(SD->Name + "_M") == 0)) {
            resolvedName = resolveType(expShape->toString());
          }
        }
      }
    }

    if (New->ArraySize) {
      checkExpr(New->ArraySize.get());
    }
    if (New->Initializer) {
      if (resolvedName.find("Uninit<") == 0) {
        // [Safety Pillar 3] Uninit allocation bypasses constructor evaluation
      } else {
        auto InitTypeObj = checkExpr(New->Initializer.get(),
                                     toka::Type::fromString(resolvedName));
      }
      // Re-propagate the mask from initializer to NewExpr
      // (This will be picked up by checkExpr wrapper and passed to
      // VariableDecl)
    }
    // 'new' usually returns a unique pointer: ^Type# (soul is fully writable)
    m_LastInitMask = ~0ULL;
    if (New->ArraySize) {
      return toka::Type::fromString("^[" + resolvedName + "]#");
    }
    return toka::Type::fromString("^" + resolvedName + "#");
  } else if (auto *UnsafeE = dynamic_cast<UnsafeExpr *>(E)) {
    bool oldUnsafe = m_InUnsafeContext;
    m_InUnsafeContext = true;
    auto typeObj = checkExpr(UnsafeE->Expression.get());
    std::string type = typeObj->toString();
    m_InUnsafeContext = oldUnsafe;
    return typeObj;
  } else if (auto *AllocE = dynamic_cast<AllocExpr *>(E)) {
    if (!m_InUnsafeContext) {
      DiagnosticEngine::report(getLoc(AllocE), DiagID::ERR_UNSAFE_ALLOC_CTX);
      HasError = true;
    }
    // Mapping to __toka_alloc
    // Returning raw pointer identity: *Type
    std::string baseType = resolveType(AllocE->TypeName);
    if (AllocE->IsArray) {
      if (AllocE->ArraySize) {
        checkExpr(AllocE->ArraySize.get());
      }
    }
    if (AllocE->Initializer) {
      checkExpr(AllocE->Initializer.get(), toka::Type::fromString(baseType));
    }
    // [FIX] Update with mangled name for CodeGen, but only if it's NOT an unresolved generic param
    if (baseType.find('\'') == std::string::npos) {
      AllocE->TypeName = baseType;
    }
    if (AllocE->IsArray) {
      return toka::Type::fromString("*[" + baseType + "]");
    }
    return toka::Type::fromString("*" + baseType);
  } else if (auto *Met = dynamic_cast<MethodCallExpr *>(E)) {
    bool oldAllow = m_AllowPermissionSuffix;
    m_AllowPermissionSuffix = true; // [NEW] Grant suffix allowance for explicit method call objects
    auto ObjTypeObj = checkExpr(Met->Object.get());
    m_AllowPermissionSuffix = oldAllow;
    
    std::string ObjType = resolveType(ObjTypeObj->toString());

    // Check for Dynamic Trait Object
    if (ObjType.size() >= 4 && ObjType.substr(0, 3) == "dyn") {
      std::string traitName = "";
      if (ObjType.rfind("dyn @", 0) == 0)
        traitName = ObjType.substr(5);
      else if (ObjType.rfind("dyn@", 0) == 0)
        traitName = ObjType.substr(4);

      if (!traitName.empty() && TraitMap.count(traitName)) {
        if (!validateDynTraitObjectSafety(traitName, Met->Loc))
          return toka::Type::fromString("unknown");
        TraitDecl *TD = TraitMap[traitName];
        for (auto &M : TD->Methods) {
          if (M->Name == Met->Method) {
            if (!M->IsPub) {
              bool sameModule = false;
              if (CurrentModule) {
                std::string modFile = DiagnosticEngine::SrcMgr
                                          ->getFullSourceLoc(CurrentModule->Loc)
                                          .FileName;
                std::string tdDeclFile =
                    DiagnosticEngine::SrcMgr->getFullSourceLoc(TD->Loc)
                        .FileName;
                if (modFile == tdDeclFile) {
                  sameModule = true;
                }
              }
              std::string metCallFile =
                  DiagnosticEngine::SrcMgr->getFullSourceLoc(Met->Loc).FileName;
              std::string tdDeclFile =
                  DiagnosticEngine::SrcMgr->getFullSourceLoc(TD->Loc).FileName;
              if (metCallFile != tdDeclFile && !sameModule &&
                  !Met->IsCompilerInternal) {
                error(Met, DiagID::ERR_METHOD_PRIVATE, Met->Method,
                      "trait " + traitName, getModuleName(CurrentModule));
              }
            }
            return toka::Type::fromString(M->ReturnType);
          }
        }
      }
    }

    std::string soulType = Type::stripMorphology(ObjType);
    if (!(MethodMap.count(soulType) &&
          MethodMap[soulType].count(Met->Method))) {
      // [NEW] Lazy Impl Instantiation
      // [Fix] Use fully resolved/mangled name for lazy instantiation lookup
      std::string resolveName = resolveType(ObjTypeObj->toString());
      std::string ConcreteTypeName = Type::stripMorphology(resolveName);
      std::string BaseName = ConcreteTypeName;
      size_t lt = BaseName.find('<');
      if (lt != std::string::npos) {
        BaseName = BaseName.substr(0, lt);
        if (GenericImplMap.count(BaseName)) {
          // [FIX] Pass generic arguments to instantiateGenericImpl
          std::vector<std::shared_ptr<toka::Type>> genericArgs;
          auto soulType = ObjTypeObj->getSoulType();
          if (auto *ST = dynamic_cast<ShapeType *>(soulType.get())) {
            genericArgs = ST->GenericArgs;
          } else {
            // Fallback: parse from string
            auto parsed = Type::fromString(ConcreteTypeName);
            if (auto *PST = dynamic_cast<ShapeType *>(parsed.get())) {
              genericArgs = PST->GenericArgs;
            }
          }
          for (auto *ImplTemplate : GenericImplMap[BaseName]) {
            instantiateGenericImpl(ImplTemplate, ConcreteTypeName, genericArgs);
          }
        }
      }
    }

    if (MethodMap.count(soulType) && MethodMap[soulType].count(Met->Method)) {
      if (MethodDecls.count(soulType) &&
          MethodDecls[soulType].count(Met->Method)) {
        FunctionDecl *FD = MethodDecls[soulType][Met->Method];
        
        if (FD->IsDeleted) {
          if (Met->IsCompilerInternal && Met->Method == "clone") {
            error(Met, DiagID::ERR_SEMA_CANNOT_IMPLICITLY_COPY_VALUE_OF_TYPE_BECA, soulType);
          } else {
            error(Met, DiagID::ERR_SEMA_CANNOT_CALL_EXPLICITLY_DELETED_METHOD_ON, Met->Method, soulType);
          }
          HasError = true;
          return toka::Type::fromString("unknown");
        }

        // [Effect] Concurrency Check for Method Call
        if (FD->Effect != EffectKind::None && !m_IsConsumingEffect && !m_IsPrecomputingCaptures) {
          error(Met, DiagID::ERR_DANGLING_EFFECT, Met->Method);
        }
        
        if (!FD->IsPub) {
          bool sameModule = false;
          if (CurrentModule) {
            std::string modFile =
                DiagnosticEngine::SrcMgr->getFullSourceLoc(CurrentModule->Loc)
                    .FileName;
            std::string fdFile =
                DiagnosticEngine::SrcMgr->getFullSourceLoc(FD->Loc).FileName;
            if (modFile == fdFile) {
              sameModule = true;
            }
          }
          std::string metCallFile =
              DiagnosticEngine::SrcMgr->getFullSourceLoc(Met->Loc).FileName;
          std::string fdDeclFile =
              DiagnosticEngine::SrcMgr->getFullSourceLoc(FD->Loc).FileName;

          if (metCallFile != fdDeclFile && !sameModule &&
              !Met->IsCompilerInternal) {
            error(Met, DiagID::ERR_METHOD_PRIVATE, Met->Method, soulType,
                  getModuleName(CurrentModule));
          }
        }

        // [Rule] Enforce Explicit Mutability for Method Calls
        bool requiresMutableBorrow = false;
        if (!FD->Args.empty() &&
            Type::stripMorphology(FD->Args[0].Name) == "self" &&
            FD->Args[0].IsValueMutable) {
          requiresMutableBorrow = true;
          // std::cerr << "[DEBUG] MutCheck: Method=" << Met->Method <<
          // std::endl;
          bool isExplicitlyMutable = false;
          // Case 1: Postfix '#' (Expression Wrapper)
          if (auto *PE = dynamic_cast<PostfixExpr *>(Met->Object.get())) {
            // std::cerr << "[DEBUG] Is PostfixExpr. Op=" << (int)PE->Op <<
            // std::endl;
            if (PE->Op == TokenType::TokenWrite)
              isExplicitlyMutable = true;
          }
          // Case 2: Variable with Suffix (Lexer Fused Token)
          else if (auto *VE = dynamic_cast<VariableExpr *>(Met->Object.get())) {
            // std::cerr << "[DEBUG] Is VariableExpr. IsValueMutable=" <<
            // VE->IsValueMutable << std::endl;
            if (VE->IsValueMutable)
              isExplicitlyMutable = true;
          }
          if (!isExplicitlyMutable) {
            error(Met, DiagID::ERR_IMMUTABLE_MOD,
                  "Method '" + Met->Method +
                      "' requires explicit mutable argument (use '#')");
          }
          if (requiresMutableBorrow) {
            Expr *objExpr = Met->Object.get();
            while (auto *PE = dynamic_cast<PostfixExpr *>(objExpr)) {
              objExpr = PE->LHS.get();
            }
            while (auto *un = dynamic_cast<UnaryExpr *>(objExpr)) {
              objExpr = un->RHS.get();
            }
            if (auto *VE = dynamic_cast<VariableExpr *>(objExpr)) {
              std::string actualName = VE->Name;
              SymbolInfo *InfoPtr = nullptr;
              if (CurrentScope->findVariableWithDeref(VE->Name, InfoPtr, actualName)) {
                InfoPtr->HasBeenMutated = true;
              }
            }
          }
        }

        if (!FD->Args.empty() &&
            Type::stripMorphology(FD->Args[0].Name) == "self") {
            // [NEW] Cede Ownership check for Method Call
            if (FD->Args[0].IsCeded) {
                std::string objPath = getStringifyPath(Met->Object.get());
                if (!objPath.empty()) {
                    CurrentScope->markMoved(objPath);
                }
            }
        }

        // [Rule] Borrowing check for Method Call
        std::string objPath = getStringifyPath(Met->Object.get());
        if (!objPath.empty()) {
           std::string conflictPath = requiresMutableBorrow ? PALCheckerState.verifyMutation(objPath) : PALCheckerState.verifyAccess(objPath);
           if (!conflictPath.empty()) {
               DiagnosticEngine::report(getLoc(Met), DiagID::ERR_BORROW_MUT, conflictPath);
               HasError = true;
           }
        }

        // [FIX] Typecheck Method Arguments
        if (FD) {
            size_t expectedArgs = FD->Args.size() - 1; // exclude self
            if (Met->Args.size() != expectedArgs && !FD->IsVariadic) {
                // If variadic, handle appropriately
                if (Met->Args.size() < expectedArgs) {
                    error(Met, DiagID::ERR_SEMA_METHOD_EXPECTS_AT_LEAST_ARGUMENTS_GOT, Met->Method, std::to_string(expectedArgs), std::to_string(Met->Args.size()));
                }
            }
            
            for (size_t i = 0; i < Met->Args.size(); ++i) {
                Met->Args[i] = foldGenericConstant(std::move(Met->Args[i]));
                std::shared_ptr<toka::Type> expectedParamTy = nullptr;
                if (i < expectedArgs) {
                    expectedParamTy = toka::Type::fromString(Sema::synthesizePhysicalType(FD->Args[i + 1]));
                }
                
                auto argTy = checkExpr(Met->Args[i].get(), expectedParamTy);
                
                if (expectedParamTy) {
                    if (FD->Args[i + 1].IsCeded) {
                        bool isCallerCeded = dynamic_cast<CedeExpr*>(Met->Args[i].get()) != nullptr;
                        bool isPrimitive = canImplicitlyPassToCede(argTy);
                        if (!isCallerCeded && !isPrimitive) {
                            error(Met->Args[i].get(), DiagID::ERR_SEMA_ARGUMENT_MUST_BE_EXPLICITLY_PASSED_WITH_C);
                        }
                    }

                    if (!isTypeCompatible(expectedParamTy, argTy)) {
                        error(Met->Args[i].get(), DiagID::ERR_SEMA_TYPE_MISMATCH_IN_METHOD_ARGUMENT_EXPECTED, std::to_string(i + 1), expectedParamTy->toString(), argTy->toString());
                    } else if (expectedParamTy->isShape() && argTy->isRawPointer()) {
                        auto shp = std::static_pointer_cast<toka::ShapeType>(expectedParamTy);
                        if (shp->Name == "str") {
                            Met->Args[i]->ResolvedType = expectedParamTy;
                        }
                    }
                }
            }
        } else {
            // Unresolved method definition context (e.g. core/std method called locally but not parsed as FD).
            // This is a flaw in Toka's global pass - we fallback to just checking the arguments to ensure they are visited.
            for (size_t i = 0; i < Met->Args.size(); ++i) {
                checkExpr(Met->Args[i].get()); 
            }
        }

        auto retType = toka::Type::fromString(MethodMap[soulType][Met->Method]);

        if (FD) {
            bool hasExplicitDeps = !FD->LifeDependencies.empty();

            auto mapParamToArg = [&](const std::string &paramName) -> std::string {
               if (Type::stripMorphology(paramName) == "self") {
                  return getStringifyPath(Met->Object.get());
               }
               for (size_t i = 0; i < FD->Args.size(); ++i) {
                  if (FD->Args[i].Name == paramName) {
                     if (i > 0 && (i - 1) < Met->Args.size()) {
                         if (auto *ve = dynamic_cast<VariableExpr*>(Met->Args[i - 1].get())) return ve->Name;
                         else if (auto *me = dynamic_cast<MemberExpr*>(Met->Args[i - 1].get())) return me->toString();
                     }
                  }
               }
               return "";
            };

            if (hasExplicitDeps) {
                for (const auto &dep : FD->LifeDependencies) {
                   std::string argVar = mapParamToArg(dep);
                   if (!argVar.empty()) m_LastLifeDependencies.insert(argVar);
                }
            }
        }

        if (FD && FD->Effect == EffectKind::Async) {
            std::string tName = "TaskHandle<" + retType->toString() + ">";
            return toka::Type::fromString(tName);
        }
        return retType;
      }
    }

    // Check with @encap suffix as fallback
    std::string encapType = soulType + "@encap";
    if (MethodMap.count(encapType) && MethodMap[encapType].count(Met->Method)) {
      return toka::Type::fromString(MethodMap[encapType][Met->Method]);
    }

    // Check with @delegate suffix as fallback (Intrinsic Method Proxying)
    std::string delegateKey = soulType + "@delegate";
    if (ImplMap.count(delegateKey) && ImplMap[delegateKey].count("target")) {
      FunctionDecl* targetMethod = ImplMap[delegateKey]["target"];
      std::string targetReturnTypeStr = targetMethod->ReturnType;
      auto targetTypeObj = toka::Type::fromString(targetReturnTypeStr);
      std::string targetSoul = targetTypeObj->getSoulName();
      
      if (MethodMap.count(targetSoul) && MethodMap[targetSoul].count(Met->Method)) {
          // Mutate the AST: baseObj.method(...) -> baseObj.target().method(...)
          auto targetCall = std::make_unique<MethodCallExpr>(std::move(Met->Object), "target", std::vector<std::unique_ptr<Expr>>());
          targetCall->Loc = Met->Loc;
          Met->Object = std::move(targetCall);
          
          // Re-evaluate to run full argument, permission, and concurrency checking on the target method
          return checkExpr(E);
      }
    }

    // Check if it's a reference to a struct
    if (ObjType.size() > 1 && ObjType[0] == '^') {
      std::string Pointee = ObjType.substr(1);
      std::string pSoul = Type::stripMorphology(Pointee);
      if (MethodMap.count(pSoul) && MethodMap[pSoul].count(Met->Method)) {
        return toka::Type::fromString(MethodMap[pSoul][Met->Method]);
      }
    }
    // [Intrinsic] unset() & unwrap()
    if (Met->Method == "unset") {
      m_IsUnsetInitCall = true;
      return ObjTypeObj;
    }
    if (Met->Method == "unwrap") {
      if (!ObjTypeObj->IsNullable) {
        // Warning or Silent
      }
      return ObjTypeObj->withAttributes(ObjTypeObj->IsWritable, false);
    }

    error(Met, DiagID::ERR_NO_SUCH_MEMBER, ObjType, Met->Method);
    return toka::Type::fromString("unknown");
  } else if (auto *Init = dynamic_cast<InitStructExpr *>(E)) {
    return checkShapeInit(Init);
  } else if (auto *Memb = dynamic_cast<MemberExpr *>(E)) {
    return checkMemberExpr(Memb);
  } else if (auto *Aw = dynamic_cast<AwaitExpr *>(E)) {
    if (CurrentFunction && CurrentFunction->Effect != EffectKind::Async) {
      CurrentFunction->Effect = EffectKind::Async;
    }
    bool old = m_IsConsumingEffect;
    m_IsConsumingEffect = true;
    auto res = checkExpr(Aw->Expression.get());
    m_IsConsumingEffect = old;
    return res;
  } else if (auto *Wt = dynamic_cast<WaitExpr *>(E)) {
    if (CurrentFunction && CurrentFunction->Effect == EffectKind::Async) {
      error(Wt, DiagID::ERR_CODEGEN, "Cannot use '.wait' inside an 'async' function. This would block the thread pool.");
    }
    if (CurrentFunction && CurrentFunction->Effect == EffectKind::None) {
      CurrentFunction->Effect = EffectKind::Wait;
    }
    bool old = m_IsConsumingEffect;
    m_IsConsumingEffect = true;
    auto res = checkExpr(Wt->Expression.get());
    m_IsConsumingEffect = old;
    return res;
  } else if (auto *St = dynamic_cast<StartExpr *>(E)) {
    bool old = m_IsConsumingEffect;
    m_IsConsumingEffect = true;
    auto res = checkExpr(St->Expression.get());
    m_IsConsumingEffect = old;
    St->ResolvedType = res;
    return res;
  } else if (auto *Unwrap = dynamic_cast<UnwrapPropagationExpr *>(E)) {
    bool old = m_IsConsumingEffect;
    m_IsConsumingEffect = true;
    auto baseObj = checkExpr(Unwrap->Base.get());
    m_IsConsumingEffect = old;

    if (auto *Var = dynamic_cast<VariableExpr *>(Unwrap->Base.get())) {
        SymbolInfo *Info = nullptr;
        std::string actualName;
        if (CurrentScope->findVariableWithDeref(Var->Name, Info, actualName)) {
            CurrentScope->markMoved(actualName);
        }
    }

    if (baseObj->isUnknown()) return toka::Type::fromString("unknown");
    baseObj = resolveType(baseObj, false);
    
    if (!baseObj->isShape()) {
      error(Unwrap, DiagID::ERR_SEMA_UNWRAP_OPERATOR_REQUIRES_A_RESULT_OR_OPTI);
      return toka::Type::fromString("unknown");
    }
    
    auto shapeT = std::static_pointer_cast<toka::ShapeType>(baseObj);
    std::string soul = shapeT->Decl ? shapeT->Decl->Name : shapeT->getSoulName();

    bool isResult = soul == "Result" || soul.find("Result_") == 0;
    bool isOption = soul == "Option" || soul.find("Option_") == 0;

    if (!isResult && !isOption) {
      error(Unwrap, DiagID::ERR_SEMA_UNWRAP_OPERATOR_REQUIRES_RESULT_T_E_OR_OP, soul);
      return toka::Type::fromString("unknown");
    }
    
    std::shared_ptr<toka::Type> payloadT = nullptr;
    std::shared_ptr<toka::Type> errT = nullptr;

    auto endsWith = [](const std::string& str, const std::string& suffix) {
        return str.size() >= suffix.size() && 
               str.compare(str.size() - suffix.size(), suffix.size(), suffix) == 0;
    };

    if (shapeT->Decl) {
      for (auto &M : shapeT->Decl->Members) {
        if (M.Name == "Ok" || M.Name == "Some" || endsWith(M.Name, "::Ok") || endsWith(M.Name, "::Some")) {
           if (!M.SubMembers.empty()) {
             payloadT = M.SubMembers[0].ResolvedType;
           } else {
             payloadT = M.ResolvedType;
           }
        } else if (M.Name == "Err" || endsWith(M.Name, "::Err")) {
           if (!M.SubMembers.empty()) {
             errT = M.SubMembers[0].ResolvedType;
           } else {
             errT = M.ResolvedType;
           }
        }
      }
    }

    if (!payloadT) {
      error(Unwrap, DiagID::ERR_SEMA_UNWRAP_OPERATOR_REQUIRES_A_PAYLOAD_TYPE_I, soul);
      return toka::Type::fromString("unknown");
    }
    
    if (!CurrentFunction) {
      error(Unwrap, DiagID::ERR_SEMA_UNWRAP_OPERATOR_MUST_BE_USED_INSIDE_A_FUN);
      return toka::Type::fromString("unknown");
    }
    
    auto fnRetT = CurrentFunction->ResolvedReturnType;
    if (!fnRetT) {
      error(Unwrap, DiagID::ERR_SEMA_CANNOT_DETERMINE_FUNCTION_RETURN_TYPE_FOR);
      return toka::Type::fromString("unknown");
    }
    fnRetT = resolveType(fnRetT, false);
    
    if (isResult) {
      if (!fnRetT->isShape()) {
        error(Unwrap, DiagID::ERR_SEMA_FUNCTION_MUST_RETURN_RESULT_WHEN_UNWRAPPI);
        return payloadT;
      }
      auto retShape = std::static_pointer_cast<toka::ShapeType>(fnRetT);
      std::string fnRetSoul = retShape->Decl ? retShape->Decl->Name : retShape->getSoulName();
      bool fnIsResult = fnRetSoul == "Result" || fnRetSoul.find("Result_") == 0;
      if (!fnIsResult) {
        error(Unwrap, DiagID::ERR_SEMA_FUNCTION_MUST_RETURN_RESULT_WHEN_UNWRAPPI);
        return payloadT;
      }
      
      std::shared_ptr<toka::Type> retErrT = nullptr;
      if (retShape->Decl) {
        for (auto &M : retShape->Decl->Members) {
          if (M.Name == "Err" || endsWith(M.Name, "::Err")) {
             if (!M.SubMembers.empty()) {
               retErrT = M.SubMembers[0].ResolvedType;
             } else {
               retErrT = M.ResolvedType;
             }
          }
        }
      }
      if (!errT || !retErrT) {
        error(Unwrap, DiagID::ERR_SEMA_RESULT_TYPES_MUST_HAVE_ERR_VARIANT);
      } else {
        if (!isTypeCompatible(retErrT, errT)) {
           error(Unwrap, DiagID::ERR_SEMA_FUNCTION_ERROR_RETURN_TYPE_IS_INCOMPATIBL, retErrT->toString(), errT->toString());
        }
      }
    } else if (isOption) {
      if (!fnRetT->isShape()) {
        error(Unwrap, DiagID::ERR_SEMA_FUNCTION_MUST_RETURN_OPTION_WHEN_UNWRAPPI);
      } else {
        auto retShape = std::static_pointer_cast<toka::ShapeType>(fnRetT);
        std::string fnRetSoul = retShape->Decl ? retShape->Decl->Name : retShape->getSoulName();
        bool fnIsOption = fnRetSoul == "Option" || fnRetSoul.find("Option_") == 0;
        if (!fnIsOption) {
          error(Unwrap, DiagID::ERR_SEMA_FUNCTION_MUST_RETURN_OPTION_WHEN_UNWRAPPI);
        }
      }
    }
    return payloadT;
  } else if (auto *Post = dynamic_cast<PostfixExpr *>(E)) {
    // [Fix] Do NOT disable soul collapse.
    // If the user wants the handle, they must use explicit prefix (e.g.
    // ^ptr#). Otherwise `ptr#` means "Mutable Value".

    // [NEW] Enforce Suffix Lifecycle Rule
    if (Post->Op == TokenType::TokenWrite) {
      if (!m_AllowPermissionSuffix) {
        error(Post, DiagID::ERR_ILLEGAL_MODIFIER_SUFFIX);
      }
    }

    if (m_InIntermediatePath) {
      if (Post->Op == TokenType::TokenWrite ||
          Post->Op == TokenType::TokenNull) {
        if (!m_IsMemberBase) {
          error(Post, DiagID::ERR_SEMA_PERMISSION_SYMBOLS_ARE_ONLY_ALLOWED_AT_TH);
        }
      }
    }

    auto lhsObj = checkExpr(Post->LHS.get());
    std::string lhsInfo = lhsObj->toString();
    if (auto *Var = dynamic_cast<VariableExpr *>(Post->LHS.get())) {
      SymbolInfo Info;
      if (CurrentScope->lookup(Var->Name, Info) && !Info.IsMutable()) {
        error(Post, DiagID::ERR_IMMUTABLE_MOD, Var->Name);
      }
    }
    if (Post->Op == TokenType::TokenWrite) {
      if (!lhsInfo.empty() && lhsInfo.back() != '#' && lhsInfo.back() != '!')
        lhsInfo += "#";
    } else if (Post->Op == TokenType::TokenNull) {
      if (!lhsInfo.empty() && lhsInfo.back() != '?' && lhsInfo.back() != '!')
        lhsInfo += "?";
    } else if (Post->Op == TokenType::DoubleQuestion) {
      // [Ch 6.1] Soul Assertion (Postfix ??)
      // If already non-nullable, it's redundant but valid. Result is
      // non-nullable.
      return lhsObj->withAttributes(lhsObj->IsWritable, false);
    }
    return toka::Type::fromString(lhsInfo);
  } else if (auto *Repeat = dynamic_cast<RepeatedArrayExpr *>(E)) {
    std::shared_ptr<toka::Type> expectedElemType = nullptr;
    if (m_ExpectedType && m_ExpectedType->isArray()) {
      expectedElemType = m_ExpectedType->getArrayElementType();
    }
    auto elemType = checkExpr(Repeat->Value.get(), expectedElemType);

    // [FIX] Substitution for Count
    Repeat->Count = foldGenericConstant(std::move(Repeat->Count));

    uint64_t size = 0;
    if (auto *Num = dynamic_cast<NumberExpr *>(Repeat->Count.get())) {
      size = Num->Value;
    } else if (auto *Var = dynamic_cast<VariableExpr *>(Repeat->Count.get())) {
      SymbolInfo info;
      if (CurrentScope->lookup(Var->Name, info) && info.HasConstValue) {
        size = info.ConstValue;
        // [Annotated AST] Propagate for CodeGen
        Var->HasConstantValue = true;
        Var->ConstantValue = size;
        Var->ConstantValObj = info.ConstValObj;
      } else {
        error(Repeat, DiagID::ERR_SEMA_ARRAY_REPEAT_COUNT_MUST_BE_A_NUMERIC_LITE);
      }
    } else {
      error(Repeat, DiagID::ERR_SEMA_ARRAY_REPEAT_COUNT_MUST_BE_A_NUMERIC_LITE);
    }
    return std::make_shared<toka::ArrayType>(elemType, size);
  } else if (auto *ArrLit = dynamic_cast<ArrayExpr *>(E)) {
    // Infer from first element
    if (!ArrLit->Elements.empty()) {
      std::shared_ptr<toka::Type> expectedElemType = nullptr;
      if (m_ExpectedType && m_ExpectedType->isArray()) {
        expectedElemType = m_ExpectedType->getArrayElementType();
      }
      ArrLit->Elements[0] =
          foldGenericConstant(std::move(ArrLit->Elements[0])); // [FIX]
      auto ElemTyObj = checkExpr(ArrLit->Elements[0].get(), expectedElemType);
      for (size_t i = 1; i < ArrLit->Elements.size(); ++i) {
        checkExpr(ArrLit->Elements[i].get(), ElemTyObj);
      }
      std::string ElemTy = ElemTyObj->toString();
      return toka::Type::fromString(
          "[" + ElemTy + ";" + std::to_string(ArrLit->Elements.size()) + "]");
    }
    return toka::Type::fromString("[i32; 0]");
  } else if (auto *me = dynamic_cast<MatchExpr *>(E)) {
    auto targetTypeObj = checkExpr(me->Target.get());
    std::string targetType = targetTypeObj->toString();
    std::string resultType = "void";
    std::shared_ptr<toka::Type> resultTypeObj;

    if (me->Target) {
        me->Target->ExtendLifetime = true;
    }

    bool isReceiver = false;
    if (!m_ControlFlowStack.empty()) {
      isReceiver = m_ControlFlowStack.back().IsReceiver;
    }

    std::string targetPath = getStringifyPath(me->Target.get());
    std::map<std::string, uint64_t> masksBefore;
    std::map<std::string, bool> movedBefore;
    for (auto &pair : CurrentScope->Symbols) {
      masksBefore[pair.first] = pair.second.InitMask;
      movedBefore[pair.first] = pair.second.Moved;
    }
    auto palBefore = PALCheckerState.snapshot();
    bool hasReachableArm = false;
    std::map<std::string, uint64_t> mergedMasks;
    std::map<std::string, bool> mergedMoved;
    PALChecker mergedPAL = palBefore;

    auto restoreMatchEntryState = [&]() {
      for (auto &pair : masksBefore) {
        CurrentScope->Symbols[pair.first].InitMask = pair.second;
      }
      for (auto &pair : movedBefore) {
        CurrentScope->Symbols[pair.first].Moved = pair.second;
      }
      PALCheckerState.restore(palBefore);
    };

    for (auto &arm : me->Arms) {
      restoreMatchEntryState();
      enterScope();
      checkPattern(arm->Pat.get(), targetType, false, targetPath);
      if (arm->Guard) {
        auto guardTypeObj = checkExpr(arm->Guard.get());
        if (!arm->Guard->ResolvedType->isBoolean()) {
          error(arm->Guard.get(), DiagID::ERR_OPERAND_TYPE_MISMATCH,
                "match guard", "bool", arm->Guard->ResolvedType->toString());
        }
      }
      m_ControlFlowStack.push_back({"", "void", nullptr, false, isReceiver});
      checkStmt(arm->Body.get());
      std::string armType = m_ControlFlowStack.back().ExpectedType;
      auto armTypeObj = m_ControlFlowStack.back().ExpectedTypeObj;
      m_ControlFlowStack.pop_back();

      if (isReceiver && armType == "void" && !allPathsJump(arm->Body.get())) {
        error(arm->Body.get(), DiagID::ERR_YIELD_VALUE_REQUIRED, "match arm");
      }

      if (resultType == "void") {
        resultType = armType;
        resultTypeObj = armTypeObj;
      } else if (armType != "void" && !isTypeCompatible(resultTypeObj, armTypeObj)) {
        error(me, DiagID::ERR_BRANCH_TYPE_MISMATCH, "match", resultType, armType);
      }

      exitScope();

      if (!allPathsJump(arm->Body.get())) {
        if (!hasReachableArm) {
          for (auto &pair : CurrentScope->Symbols) {
            mergedMasks[pair.first] = pair.second.InitMask;
            mergedMoved[pair.first] = pair.second.Moved;
          }
          mergedPAL = PALCheckerState.snapshot();
          hasReachableArm = true;
        } else {
          for (auto &pair : CurrentScope->Symbols) {
            uint64_t armMask = pair.second.InitMask;
            if (mergedMasks.count(pair.first))
              mergedMasks[pair.first] &= armMask;
            else
              mergedMasks[pair.first] = armMask;

            bool armMoved = pair.second.Moved;
            if (mergedMoved.count(pair.first))
              mergedMoved[pair.first] = mergedMoved[pair.first] || armMoved;
            else
              mergedMoved[pair.first] = armMoved;
          }

          PALChecker nextMerged = mergedPAL;
          nextMerged.mergeBranches(palBefore, mergedPAL, true,
                                   PALCheckerState.snapshot(), true);
          mergedPAL = nextMerged;
        }
      }
    }

    if (hasReachableArm) {
      for (auto &pair : CurrentScope->Symbols) {
        if (mergedMasks.count(pair.first))
          pair.second.InitMask = mergedMasks[pair.first];
        if (mergedMoved.count(pair.first))
          pair.second.Moved = mergedMoved[pair.first];
      }
      PALCheckerState.restore(mergedPAL);
    } else {
      restoreMatchEntryState();
    }

    if (isReceiver && resultType == "void") {
      error(me, DiagID::ERR_YIELD_VALUE_REQUIRED, "match expression");
    }

    // Exhaustiveness Check
    bool hasWildcard = false;
    std::set<std::string> matchedVariants;

    std::string unstrippedTargetType = targetType;
    std::string baseTargetType = toka::Type::stripMorphology(targetType);

    std::function<bool(MatchArm::Pattern*, const std::string&)> isPatternExhaustive = 
      [&](MatchArm::Pattern *pat, const std::string &t) -> bool {
        if (!pat) return false;
        if (pat->PatternKind == MatchArm::Pattern::Wildcard || pat->PatternKind == MatchArm::Pattern::Elision) return true;
        if (pat->PatternKind == MatchArm::Pattern::Variable) {
          std::string baseShapeName = t;
          size_t scopePos = pat->Name.find("::");
          std::string patName = pat->Name;
          if (scopePos != std::string::npos) {
            baseShapeName = patName.substr(0, scopePos);
            patName = patName.substr(scopePos + 2);
          }
          baseShapeName = toka::Type::stripMorphology(baseShapeName);
          while (TypeAliasMap.count(baseShapeName) && !TypeAliasMap[baseShapeName].IsStrong) {
              baseShapeName = TypeAliasMap[baseShapeName].Target;
              size_t lt = baseShapeName.find("<");
              if (lt != std::string::npos) baseShapeName = baseShapeName.substr(0, lt);
          }
          bool isVariant = false;
          if (ShapeMap.count(baseShapeName)) {
            ShapeDecl *SD = ShapeMap[baseShapeName];
            for (auto &Memb : SD->Members) {
              bool noPayload = Memb.Type.empty() || Memb.Type == "void";
              if (Memb.Name == patName && noPayload && Memb.SubMembers.empty()) {
                isVariant = true;
                break;
              }
            }
          }
          if (isVariant) return false;
          return true;
        }
        if (pat->PatternKind == MatchArm::Pattern::Or) {
          for (auto &sub : pat->SubPatterns) {
            if (isPatternExhaustive(sub.get(), t)) return true;
          }
          return false;
        }
        if (pat->PatternKind == MatchArm::Pattern::Decons) {
          std::string baseT = toka::Type::stripMorphology(t);
          size_t lt = baseT.find("<");
          if (lt != std::string::npos) baseT = baseT.substr(0, lt);
          while (TypeAliasMap.count(baseT) && !TypeAliasMap[baseT].IsStrong) {
              baseT = TypeAliasMap[baseT].Target;
              size_t lt2 = baseT.find("<");
              if (lt2 != std::string::npos) baseT = baseT.substr(0, lt2);
          }
          if (ShapeMap.count(baseT)) {
            ShapeDecl *SD = ShapeMap[baseT];
            if (SD->Kind == ShapeKind::Struct || SD->Kind == ShapeKind::Tuple) {
              size_t elisionIndex = -1;
              size_t elisionCount = 0;
              for (size_t i = 0; i < pat->SubPatterns.size(); ++i) {
                if (pat->SubPatterns[i]->PatternKind == MatchArm::Pattern::Elision) {
                  elisionIndex = i;
                  elisionCount++;
                }
              }

              if (elisionCount > 1) {
                return false;
              } else if (elisionCount == 1) {
                size_t expectedSize = SD->Members.size();
                size_t subPatsWithoutElision = pat->SubPatterns.size() - 1;
                if (subPatsWithoutElision > expectedSize) {
                  return false;
                }
                size_t elidedFields = expectedSize - subPatsWithoutElision;
                for (size_t i = 0; i < pat->SubPatterns.size(); ++i) {
                  if (i == elisionIndex) continue;
                  size_t memberIndex = (i < elisionIndex) ? i : (i + elidedFields - 1);
                  if (!isPatternExhaustive(pat->SubPatterns[i].get(), getPhysicalTypeName(SD->Members[memberIndex]))) return false;
                }
                return true;
              } else {
                if (pat->SubPatterns.size() != SD->Members.size()) return false;
                for (size_t i = 0; i < pat->SubPatterns.size(); ++i) {
                  if (!isPatternExhaustive(pat->SubPatterns[i].get(), getPhysicalTypeName(SD->Members[i]))) return false;
                }
                return true;
              }
            }
          }
        }
        return false;
      };

    if (ShapeMap.count(baseTargetType) && ShapeMap[baseTargetType]->Kind == ShapeKind::Enum) {
      ShapeDecl *SD = ShapeMap[baseTargetType];
      std::function<void(MatchArm::Pattern*)> collectMatchedVariants = [&](MatchArm::Pattern *pat) {
        if (!pat) return;
        if (pat->PatternKind == MatchArm::Pattern::Or) {
          for (auto &sub : pat->SubPatterns) {
            collectMatchedVariants(sub.get());
          }
        } else if (pat->PatternKind == MatchArm::Pattern::Decons) {
          std::string vName = pat->Name;
          size_t p = vName.find("::");
          if (p != std::string::npos) vName = vName.substr(p + 2);
          
          ShapeMember *foundMemb = nullptr;
          for (auto &Memb : SD->Members) {
            if (Memb.Name == vName) {
              foundMemb = &Memb;
              break;
            }
          }
          if (foundMemb) {
            bool subExhaustive = true;
            if (!foundMemb->SubMembers.empty()) {
              size_t elisionIndex = -1;
              size_t elisionCount = 0;
              for (size_t i = 0; i < pat->SubPatterns.size(); ++i) {
                if (pat->SubPatterns[i]->PatternKind == MatchArm::Pattern::Elision) {
                  elisionIndex = i;
                  elisionCount++;
                }
              }

              if (elisionCount > 1) {
                subExhaustive = false;
              } else if (elisionCount == 1) {
                size_t expectedSize = foundMemb->SubMembers.size();
                size_t subPatsWithoutElision = pat->SubPatterns.size() - 1;
                if (subPatsWithoutElision > expectedSize) {
                  subExhaustive = false;
                } else {
                  size_t elidedFields = expectedSize - subPatsWithoutElision;
                  for (size_t i = 0; i < pat->SubPatterns.size(); ++i) {
                    if (i == elisionIndex) continue;
                    size_t memberIndex = (i < elisionIndex) ? i : (i + elidedFields - 1);
                    if (!isPatternExhaustive(pat->SubPatterns[i].get(), getPhysicalTypeName(foundMemb->SubMembers[memberIndex]))) {
                      subExhaustive = false;
                      break;
                    }
                  }
                }
              } else {
                if (pat->SubPatterns.size() != foundMemb->SubMembers.size()) {
                  subExhaustive = false;
                } else {
                  for (size_t i = 0; i < pat->SubPatterns.size(); ++i) {
                    if (!isPatternExhaustive(pat->SubPatterns[i].get(), getPhysicalTypeName(foundMemb->SubMembers[i]))) {
                      subExhaustive = false;
                      break;
                    }
                  }
                }
              }
            } else if (!foundMemb->Type.empty() && foundMemb->Type != "void") {
              size_t elisionIndex = -1;
              size_t elisionCount = 0;
              for (size_t i = 0; i < pat->SubPatterns.size(); ++i) {
                if (pat->SubPatterns[i]->PatternKind == MatchArm::Pattern::Elision) {
                  elisionIndex = i;
                  elisionCount++;
                }
              }

              if (elisionCount > 1) {
                subExhaustive = false;
              } else if (elisionCount == 1) {
                subExhaustive = true;
              } else {
                if (pat->SubPatterns.size() != 1 || 
                    !isPatternExhaustive(pat->SubPatterns[0].get(), foundMemb->Type)) {
                  subExhaustive = false;
                }
              }
            }
            if (subExhaustive) {
              matchedVariants.insert(vName);
            }
          }
        } else if (pat->PatternKind == MatchArm::Pattern::Variable) {
          std::string vName = pat->Name;
          size_t p = vName.find("::");
          if (p != std::string::npos) vName = vName.substr(p + 2);
          
          ShapeMember *foundMemb = nullptr;
          for (auto &Memb : SD->Members) {
            if (Memb.Name == vName) {
              foundMemb = &Memb;
              break;
            }
          }
          if (foundMemb) {
            bool noPayload = foundMemb->Type.empty() || foundMemb->Type == "void";
            if (noPayload && foundMemb->SubMembers.empty()) {
              matchedVariants.insert(vName);
            }
          }
        }
      };

      for (auto &arm : me->Arms) {
        if (arm->Guard) continue;
        if (isPatternExhaustive(arm->Pat.get(), targetType)) {
          hasWildcard = true;
          break;
        } else {
          collectMatchedVariants(arm->Pat.get());
        }
      }

      if (!hasWildcard) {
        std::vector<std::string> missing;
        for (auto &m : SD->Members) {
          if (m.Name == "Moved") continue;
          if (matchedVariants.find(m.Name) == matchedVariants.end()) {
            missing.push_back(m.Name);
          }
        }
        if (!missing.empty()) {
          std::string missingStr = "";
          for (size_t i = 0; i < missing.size(); ++i) {
            missingStr += missing[i];
            if (i < missing.size() - 1) missingStr += ", ";
          }
          DiagnosticEngine::report(getLoc(me), DiagID::ERR_MATCH_NOT_EXHAUSTIVE, missingStr);
          HasError = true;
        }
      }
    } else {
      for (auto &arm : me->Arms) {
        if (arm->Guard) continue;
        if (isPatternExhaustive(arm->Pat.get(), targetType)) {
          hasWildcard = true;
          break;
        }
      }
      if (!hasWildcard) {
        DiagnosticEngine::report(getLoc(me), DiagID::ERR_MATCH_NOT_EXHAUSTIVE, "non-enum types require a wildcard/exhaustive branch");
        HasError = true;
      }
    }

    return toka::Type::fromString(resultType);
  }

  return toka::Type::fromString("unknown");
}

} // namespace toka
