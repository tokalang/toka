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
#include "toka/SourceManager.h"
#include "toka/Sema.h"
#include "toka/Type.h"
#include <algorithm>
#include <functional>
#include <iostream>
#include <set>

namespace toka {

static SourceLocation getLoc(ASTNode *Node) { return Node->Loc; }

static bool isReadOnlyReferenceViewInitializer(ASTNode *Node,
                                               Scope *CurrentScope) {
  if (!Node || !CurrentScope)
    return false;

  if (auto *Cast = dynamic_cast<CastExpr *>(Node))
    return isReadOnlyReferenceViewInitializer(Cast->Expression.get(),
                                              CurrentScope);
  if (auto *Post = dynamic_cast<PostfixExpr *>(Node))
    return isReadOnlyReferenceViewInitializer(Post->LHS.get(), CurrentScope);
  if (auto *Unsafe = dynamic_cast<UnsafeExpr *>(Node))
    return isReadOnlyReferenceViewInitializer(Unsafe->Expression.get(),
                                              CurrentScope);

  if (auto *VE = dynamic_cast<VariableExpr *>(Node)) {
    std::string actualName = VE->Name;
    SymbolInfo *Info = nullptr;
    if (CurrentScope->findVariableWithDeref(VE->Name, Info, actualName))
      return Info && Info->IsReference() && !Info->IsDeclaredMutable;
  }

  return false;
}

static bool isReadOnlyReferenceType(const std::shared_ptr<toka::Type> &Type) {
  if (!Type || !Type->isReference())
    return false;
  auto pointee = Type->getPointeeType();
  return !pointee || !pointee->IsWritable;
}

bool Sema::allPathsReturn(Stmt *S) {
  if (!S)
    return false;
  if (dynamic_cast<ReturnStmt *>(S) || dynamic_cast<UnreachableStmt *>(S))
    return true;
  if (auto *B = dynamic_cast<BlockStmt *>(S)) {
    for (const auto &Sub : B->Statements) {
      if (allPathsReturn(Sub.get()))
        return true;
    }
    return false;
  }
  if (auto *Unsafe = dynamic_cast<UnsafeStmt *>(S)) {
    return allPathsReturn(Unsafe->Statement.get());
  }
  // Expressions wrapped in Stmt
  if (auto *ES = dynamic_cast<ExprStmt *>(S)) {
    Expr *E = ES->Expression.get();
    if (auto *If = dynamic_cast<IfExpr *>(E)) {
      if (If->Else && allPathsReturn(If->Then.get()) &&
          allPathsReturn(If->Else.get()))
        return true;
      return false;
    }
    if (auto *Match = dynamic_cast<MatchExpr *>(E)) {
      for (const auto &Arm : Match->Arms) {
        if (!allPathsReturn(Arm->Body.get()))
          return false;
      }
      return true;
    }
    if (auto *Loop = dynamic_cast<LoopExpr *>(E)) {
      if (allPathsReturn(Loop->Body.get()))
        return true;
      return false;
    }
  }
  return false;
}

void Sema::mergeFlowExits(FlowSummary &dst, const FlowSummary &src) {
  dst.HasReturnLikeExit = dst.HasReturnLikeExit || src.HasReturnLikeExit;
  dst.BreakLabels.insert(src.BreakLabels.begin(), src.BreakLabels.end());
  dst.ContinueLabels.insert(src.ContinueLabels.begin(),
                            src.ContinueLabels.end());
}

Sema::FlowSummary Sema::summarizeFlow(Stmt *S) {
  FlowSummary result;
  if (!S)
    return result;

  if (dynamic_cast<ReturnStmt *>(S) || dynamic_cast<UnreachableStmt *>(S)) {
    result.CanFallThrough = false;
    result.HasReturnLikeExit = true;
    return result;
  }

  if (auto *B = dynamic_cast<BlockStmt *>(S)) {
    bool canReachNext = true;
    result.CanFallThrough = true;
    for (const auto &Sub : B->Statements) {
      if (!canReachNext)
        break;
      FlowSummary sub = summarizeFlow(Sub.get());
      mergeFlowExits(result, sub);
      canReachNext = sub.CanFallThrough;
    }
    result.CanFallThrough = canReachNext;
    return result;
  }

  if (auto *Unsafe = dynamic_cast<UnsafeStmt *>(S)) {
    return summarizeFlow(Unsafe->Statement.get());
  }

  if (auto *ES = dynamic_cast<ExprStmt *>(S)) {
    return summarizeFlowExpr(ES->Expression.get());
  }

  return result;
}

Sema::FlowSummary Sema::summarizeFlowExpr(Expr *E) {
  FlowSummary result;
  if (!E)
    return result;

  if (auto *Break = dynamic_cast<BreakExpr *>(E)) {
    result.CanFallThrough = false;
    result.BreakLabels.insert(Break->TargetLabel);
    return result;
  }

  if (auto *Continue = dynamic_cast<ContinueExpr *>(E)) {
    result.CanFallThrough = false;
    result.ContinueLabels.insert(Continue->TargetLabel);
    return result;
  }

  if (auto *If = dynamic_cast<IfExpr *>(E)) {
    FlowSummary thenFlow = summarizeFlow(If->Then.get());
    mergeFlowExits(result, thenFlow);
    if (If->Else) {
      FlowSummary elseFlow = summarizeFlow(If->Else.get());
      mergeFlowExits(result, elseFlow);
      result.CanFallThrough =
          thenFlow.CanFallThrough || elseFlow.CanFallThrough;
    } else {
      result.CanFallThrough = true;
    }
    return result;
  }

  if (auto *Guard = dynamic_cast<GuardExpr *>(E)) {
    FlowSummary thenFlow = summarizeFlow(Guard->Then.get());
    mergeFlowExits(result, thenFlow);
    if (Guard->Else) {
      FlowSummary elseFlow = summarizeFlow(Guard->Else.get());
      mergeFlowExits(result, elseFlow);
      result.CanFallThrough =
          thenFlow.CanFallThrough || elseFlow.CanFallThrough;
    } else {
      result.CanFallThrough = true;
    }
    return result;
  }

  if (auto *Match = dynamic_cast<MatchExpr *>(E)) {
    result.CanFallThrough = false;
    for (const auto &Arm : Match->Arms) {
      FlowSummary armFlow = summarizeFlow(Arm->Body.get());
      mergeFlowExits(result, armFlow);
      result.CanFallThrough = result.CanFallThrough || armFlow.CanFallThrough;
    }
    return result;
  }

  if (auto *Loop = dynamic_cast<LoopExpr *>(E)) {
    FlowSummary bodyFlow = summarizeFlow(Loop->Body.get());
    result.HasReturnLikeExit = bodyFlow.HasReturnLikeExit;

    bool hasLocalBreak = bodyFlow.BreakLabels.count("") > 0;
    result.CanFallThrough = (Loop->Condition != nullptr) || hasLocalBreak;

    for (const auto &label : bodyFlow.BreakLabels) {
      if (!label.empty())
        result.BreakLabels.insert(label);
    }
    for (const auto &label : bodyFlow.ContinueLabels) {
      if (!label.empty())
        result.ContinueLabels.insert(label);
    }
    return result;
  }

  if (auto *For = dynamic_cast<ForExpr *>(E)) {
    FlowSummary bodyFlow = summarizeFlow(For->Body.get());
    FlowSummary elseFlow;
    if (For->ElseBody)
      elseFlow = summarizeFlow(For->ElseBody.get());

    result.HasReturnLikeExit =
        bodyFlow.HasReturnLikeExit || elseFlow.HasReturnLikeExit;

    bool hasLocalBreak = bodyFlow.BreakLabels.count("") > 0;
    result.CanFallThrough =
        hasLocalBreak || (For->ElseBody ? elseFlow.CanFallThrough : true);

    for (const auto &label : bodyFlow.BreakLabels) {
      if (!label.empty())
        result.BreakLabels.insert(label);
    }
    for (const auto &label : bodyFlow.ContinueLabels) {
      if (!label.empty())
        result.ContinueLabels.insert(label);
    }
    result.BreakLabels.insert(elseFlow.BreakLabels.begin(),
                              elseFlow.BreakLabels.end());
    result.ContinueLabels.insert(elseFlow.ContinueLabels.begin(),
                                 elseFlow.ContinueLabels.end());
    return result;
  }

  return result;
}

bool Sema::allPathsJump(Stmt *S) {
  return !summarizeFlow(S).CanFallThrough;
}

void Sema::checkStmt(Stmt *S) {
  if (!S)
    return;

  ActiveNodeRAII Active(S);

  if (auto *Block = dynamic_cast<BlockStmt *>(S)) {
    enterScope();
    bool hasDiverged = false;
    for (auto &SubStmt : Block->Statements) {
      if (hasDiverged) {
        bool isWarningExempt = false;
        if (SubStmt->Loc.isValid()) {
          std::string path = DiagnosticEngine::SrcMgr->getFullSourceLoc(SubStmt->Loc).FileName;
          if (path.find("tests/") != std::string::npos ||
              path.find("build.tk") != std::string::npos ||
              path.find("prelude") != std::string::npos ||
              path.find("lib/") != std::string::npos) {
            isWarningExempt = true;
          }
        }
        if (!isWarningExempt) {
          DiagnosticEngine::report(SubStmt->Loc, DiagID::WARN_UNREACHABLE_CODE);
        }
        break; // Avoid applying effects from unreachable statements.
      }
      checkStmt(SubStmt.get());
      if (!summarizeFlow(SubStmt.get()).CanFallThrough) {
        hasDiverged = true;
      }
    }

    // SCOPE GUARD: Hot Potato Check
    // Iterate over symbols in the current scope before exiting.
    // If any symbol is a Reference with DirtyReferentMask != Full,
    // we must ensure its Referent (BorrowedFrom) is now Clean.
    // However, the Referent might be in a parent scope. We need to check the
    // Referent's CURRENT state. Wait, simpler model first: If the Ref is marked
    // Dirty, it means it TOOK responsibility. We check if the Ref *itself*
    // thinks it's done? No, the Ref doesn't update its own DirtyMask
    // automatically unless we implement flow sensitive updates to
    // DirtyReferentMask on assignment. BETTER APPROACH based on plan: "Check if
    // the referent (Source) has been fully initialized (Cleaned) within this
    // scope."

    // We need to iterate the Symbols in CurrentScope.
    for (auto const &[name, info] : CurrentScope->Symbols) {
      if (info.IsReference() && info.DirtyReferentMask != ~0ULL) {
        // It was a dirty reference.
        // Check if it's still dirty?
        // Actually, we should check the SOURCE variable's current InitMask.
        SymbolInfo *sourceInfo = nullptr;
        if (!info.BorrowedFrom.empty() &&
            CurrentScope->findSymbol(info.BorrowedFrom, sourceInfo)) {
          bool referentIsShape =
              (sourceInfo->TypeObj && sourceInfo->TypeObj->isShape());
          uint64_t signature = ~0ULL;
          if (referentIsShape) {
            std::string soul = sourceInfo->TypeObj->getSoulName();
            if (ShapeMap.count(soul)) {
              ShapeDecl *SD = ShapeMap[soul];
              signature = (1ULL << SD->Members.size()) - 1;
              if (SD->Members.size() >= 64)
                signature = ~0ULL;
            }
          }

          // Check if Source is now fully initialized
          if ((sourceInfo->InitMask & signature) != signature) {
            DiagnosticEngine::report(getLoc(Block),
                                     DiagID::ERR_DIRTY_REF_ESCAPE, name,
                                     info.BorrowedFrom);
            HasError = true;
          }
        }
      }
    }

    bool isWarningExempt = false;
    if (Block->Loc.isValid()) {
      std::string path = DiagnosticEngine::SrcMgr->getFullSourceLoc(Block->Loc).FileName;
      if (path.find("tests/") != std::string::npos ||
          path.find("build.tk") != std::string::npos ||
          path.find("prelude") != std::string::npos ||
          path.find("lib/") != std::string::npos) {
        isWarningExempt = true;
      }
    }
    if (!isWarningExempt) {
      for (auto const &[name, info] : CurrentScope->Symbols) {
        if (info.IsDeclaredMutable && !info.HasBeenMutated) {
          if (Type::stripMorphology(name) != "self") {
            std::string stripped = name;
            size_t idx = 0;
            while (idx < stripped.size() && (stripped[idx] == '*' || stripped[idx] == '&' || stripped[idx] == '^' || stripped[idx] == '~' || stripped[idx] == '#')) {
              idx++;
            }
            if (stripped.empty() || idx >= stripped.size() || stripped[idx] != '_') {
              DiagnosticEngine::report(info.DeclLoc.isValid() ? info.DeclLoc : Block->Loc, DiagID::WARN_MUTABLE_VAR_NEVER_MUTATED, name);
            }
          }
        }
        if (info.IsDeclaredVariable && !info.HasBeenUsed) {
          if (Type::stripMorphology(name) != "self") {
            std::string stripped = name;
            size_t idx = 0;
            while (idx < stripped.size() && (stripped[idx] == '*' || stripped[idx] == '&' || stripped[idx] == '^' || stripped[idx] == '~' || stripped[idx] == '#')) {
              idx++;
            }
            if (stripped.empty() || idx >= stripped.size() || stripped[idx] != '_') {
              DiagnosticEngine::report(info.DeclLoc.isValid() ? info.DeclLoc : Block->Loc, DiagID::WARN_UNUSED_VARIABLE, name);
            }
          }
        }
      }
    }

    exitScope();
  } else if (auto *Ret = dynamic_cast<ReturnStmt *>(S)) {
    std::string ExprType = "void";
    std::shared_ptr<toka::Type> ExprTypeObj = toka::Type::fromString("void");
    if (Ret->ReturnValue) {
      Ret->ReturnValue = foldGenericConstant(std::move(Ret->ReturnValue));
      m_ControlFlowStack.push_back(
          {"", CurrentFunctionReturnType, nullptr, false, true});
      auto RetTypeObj = checkExpr(Ret->ReturnValue.get(), toka::Type::fromString(CurrentFunctionReturnType));
      ExprTypeObj = RetTypeObj;
      ExprType = RetTypeObj->toString();
      m_ControlFlowStack.pop_back();

      // Escape Blockade: Check for Dirty Reference
      if (auto *Var = dynamic_cast<VariableExpr *>(Ret->ReturnValue.get())) {
        SymbolInfo *info = nullptr;
        if (CurrentScope->findSymbol(Var->Name, info)) {
          if (info->IsReference() && info->DirtyReferentMask != ~0ULL) {
            DiagnosticEngine::report(getLoc(Ret), DiagID::ERR_ESCAPE_UNSET,
                                     Var->Name);
            HasError = true;
          }
        }
      }

      // [NEW] Unified Lifetime Check logic
      std::shared_ptr<toka::Type> expectedRetObj = nullptr;
      if (CurrentFunction && CurrentFunction->ResolvedReturnType && CurrentFunctionReturnType == CurrentFunction->ReturnType) {
          expectedRetObj = CurrentFunction->ResolvedReturnType;
      } else {
          expectedRetObj = resolveType(toka::Type::fromString(CurrentFunctionReturnType));
      }

      std::set<std::string> visitedBorrowLikeTypes;
      std::function<bool(std::shared_ptr<toka::Type>)> isBorrowLikeType =
          [&](std::shared_ptr<toka::Type> t) -> bool {
        if (!t)
          return false;
        if (t->isReference())
          return true;
        if (auto *shape = dynamic_cast<ShapeType *>(t.get())) {
          for (const auto &arg : shape->GenericArgs) {
            if (isBorrowLikeType(arg))
              return true;
          }
          std::string name = t->getSoulName();
          if (name == "str" || name == "bytes")
            return true;
          if (visitedBorrowLikeTypes.count(name) == 0) {
            visitedBorrowLikeTypes.insert(name);
            if (ShapeMap.count(name)) {
              ShapeDecl *SD = ShapeMap[name];
              for (const auto &member : SD->Members) {
                if (isBorrowLikeType(getPhysicalType(member)))
                  return true;
              }
            }
          }
        }
        return false;
      };

      std::function<bool(Expr *)> returnsBorrowExpr = [&](Expr *E) -> bool {
        if (!E)
          return false;
        if (auto *Addr = dynamic_cast<UnaryExpr *>(E))
          return Addr->Op == TokenType::Ampersand || returnsBorrowExpr(Addr->RHS.get());
        if (dynamic_cast<AddressOfExpr *>(E))
          return true;
        if (auto *Cast = dynamic_cast<CastExpr *>(E))
          return returnsBorrowExpr(Cast->Expression.get());
        return false;
      };

      std::function<bool(Expr *)> carriesLifeDependencyExpr = [&](Expr *E) -> bool {
        if (!E)
          return false;
        if (auto *Cast = dynamic_cast<CastExpr *>(E))
          return carriesLifeDependencyExpr(Cast->Expression.get());
        if (auto *Var = dynamic_cast<VariableExpr *>(E)) {
          SymbolInfo info;
          if (CurrentScope->lookup(Var->Name, info)) {
            return !info.BorrowedFrom.empty() || !info.LifeDependencySet.empty();
          }
        }
        if (auto *Clo = dynamic_cast<ClosureExpr *>(E)) {
          return !Clo->ImplicitCaptures.empty();
        }
        if (auto *Method = dynamic_cast<MethodCallExpr *>(E)) {
          return Method->ResolvedFn &&
                 !Method->ResolvedFn->LifeDependencies.empty();
        }
        if (auto *Init = dynamic_cast<InitStructExpr *>(E)) {
          for (auto &Mem : Init->Members) {
            if (carriesLifeDependencyExpr(Mem.second.get()))
              return true;
          }
        } else if (auto *Anon = dynamic_cast<AnonymousRecordExpr *>(E)) {
          for (auto &Field : Anon->Fields) {
            if (carriesLifeDependencyExpr(Field.second.get()))
              return true;
          }
        } else if (auto *Bin = dynamic_cast<BinaryExpr *>(E)) {
          if (Bin->Op == "=")
            return carriesLifeDependencyExpr(Bin->RHS.get());
        }
        return false;
      };

      bool isTrackedRet =
          isBorrowLikeType(expectedRetObj) || isBorrowLikeType(ExprTypeObj) ||
          returnsBorrowExpr(Ret->ReturnValue.get()) ||
          carriesLifeDependencyExpr(Ret->ReturnValue.get());

      if (isTrackedRet) {
          std::set<std::string> returnedDeps;

          auto recordDependencyPathTo = [&](std::set<std::string> &out,
                                            const std::string &dep) {
            if (dep.empty())
              return;
            std::string baseName = dep;
            size_t dotPos = baseName.find('.');
            if (dotPos != std::string::npos)
              baseName = baseName.substr(0, dotPos);

            bool isParam = false;
            if (CurrentFunction) {
              for (const auto &Arg : CurrentFunction->Args) {
                if (Arg.Name == baseName) {
                  isParam = true;
                  break;
                }
              }
            }

            SymbolInfo depInfo;
            if (CurrentScope->lookup(baseName, depInfo)) {
              bool contributedDeps = false;
              if (!depInfo.BorrowedFrom.empty()) {
                out.insert(depInfo.BorrowedFrom);
                contributedDeps = true;
              }
              size_t depCountBefore = out.size();
              out.insert(depInfo.LifeDependencySet.begin(),
                         depInfo.LifeDependencySet.end());
              if (out.size() != depCountBefore)
                contributedDeps = true;
              if (!contributedDeps && isParam)
                out.insert(dep);
              return;
            }

            if (isParam)
              out.insert(dep);
            else
              out.insert(dep);
          };

          auto recordDependencyPath = [&](const std::string &dep) {
            recordDependencyPathTo(returnedDeps, dep);
          };

          auto getPath = [&](Expr *E) {
            AccessPath path = makeAccessPath(E);
            for (const auto &projection : path.Projections) {
              if (projection.Kind != AccessProjectionKind::Field)
                return std::string{};
            }
            return path.toLegacyString();
          };

          // Helper to collect dependencies from the returned expression
          std::function<void(Expr *, std::set<std::string> &)> collectDepsInto =
              [&](Expr *E, std::set<std::string> &out) {
            if (!E)
              return;

            // Case 1: Taking address `&var` or `&var.field` via UnaryExpr
            if (auto *Addr = dynamic_cast<UnaryExpr *>(E)) {
              if (Addr->Op == TokenType::Ampersand) {
                std::string path = getPath(Addr->RHS.get());
                if (!path.empty()) {
                    out.insert(path);
                }
              }
            }
            // Case 1b: AddressOfExpr Borrow (implicit/explicit borrow alignment)
            else if (auto *AddrOf = dynamic_cast<AddressOfExpr *>(E)) {
                std::string path = getPath(AddrOf->Expression.get());
                if (!path.empty()) {
                    out.insert(path);
                }
            }
            // Case 2: Returning existing reference variable `x`
            else if (auto *Var = dynamic_cast<VariableExpr *>(E)) {
              SymbolInfo info;
              if (CurrentScope->lookup(Var->Name, info)) {
                if (!info.BorrowedFrom.empty()) {
                  out.insert(info.BorrowedFrom);
                }
                out.insert(info.LifeDependencySet.begin(),
                           info.LifeDependencySet.end());
                if (info.IsReference() || isBorrowLikeType(info.TypeObj)) {
                  bool contributedDeps = false;
                  // It depends on whatever 'info' borrowed from
                  if (!info.BorrowedFrom.empty()) {
                    out.insert(info.BorrowedFrom);
                    contributedDeps = true;
                  }
                  // Also merge its transitive dependencies if we track them
                  size_t depCountBefore = out.size();
                  out.insert(info.LifeDependencySet.begin(),
                             info.LifeDependencySet.end());
                  if (out.size() != depCountBefore)
                    contributedDeps = true;
                  if (!contributedDeps && CurrentFunction) {
                    std::string baseName = Var->Name;
                    size_t dotPos = baseName.find('.');
                    if (dotPos != std::string::npos)
                      baseName = baseName.substr(0, dotPos);
                    for (const auto &Arg : CurrentFunction->Args) {
                      if (Arg.Name == baseName) {
                        out.insert(baseName);
                        break;
                      }
                    }
                  }
                }
              }
            }
            // Case 3: Dependency Transform Operator (CastExpr)
            else if (auto *Cast = dynamic_cast<CastExpr *>(E)) {
              auto srcType = Cast->Expression ? Cast->Expression->ResolvedType : nullptr;
              auto targetType = Cast->ResolvedType;
              if (srcType && targetType) {
                bool targetIsTracked = targetType->isReference();
                bool srcIsUntraced = srcType->isRawPointer() || srcType->isAddrType() ||
                                     srcType->toString() == "Addr" || srcType->toString() == "*void" || srcType->toString() == "*byte";
                
                if (targetIsTracked && srcIsUntraced) {
                  // Rule 3: Reinterpret Cast -> Chain Fracture -> Evaporation Event
                  out.insert("__untraced_escape");
                } else {
                  // Rule 2: Reference-Preserving Cast -> Continuity Preservation
                  collectDepsInto(Cast->Expression.get(), out);
                }
              }
            }
            // Case 4: CallExpr
            else if (auto *Call = dynamic_cast<CallExpr *>(E)) {
                for (auto &Arg : Call->Args) {
                    collectDepsInto(Arg.get(), out);
                }
            }
            else if (auto *Method = dynamic_cast<MethodCallExpr *>(E)) {
                if (Method->ResolvedFn) {
                  for (const auto &dep : Method->ResolvedFn->LifeDependencies) {
                    if (Type::stripMorphology(dep) == "self") {
                      std::string path = getPath(Method->Object.get());
                      if (!path.empty())
                        out.insert(path);
                      continue;
                    }
                    for (size_t i = 1;
                         i < Method->ResolvedFn->Args.size(); ++i) {
                      if (Method->ResolvedFn->Args[i].Name != dep ||
                          i - 1 >= Method->Args.size())
                        continue;
                      std::string path = getPath(Method->Args[i - 1].get());
                      if (!path.empty())
                        out.insert(path);
                      break;
                    }
                  }
                }
            }
            // Case 5: InitStructExpr / AnonymousRecordExpr
            else if (auto *Init = dynamic_cast<InitStructExpr *>(E)) {
                for (auto &Mem : Init->Members) {
                    collectDepsInto(Mem.second.get(), out);
                }
            } else if (auto *Anon = dynamic_cast<AnonymousRecordExpr *>(E)) {
                for (auto &Field : Anon->Fields) {
                    collectDepsInto(Field.second.get(), out);
                }
            }
            // Case 6: Fallback for BinaryExpr named arg init if kept as CallExpr
            else if (auto *Bin = dynamic_cast<BinaryExpr *>(E)) {
                if (Bin->Op == "=") {
                    collectDepsInto(Bin->RHS.get(), out);
                }
            }
            // Case 6b: Closure expression with implicit borrow captures
            else if (auto *Clo = dynamic_cast<ClosureExpr *>(E)) {
                for (const auto &capture : Clo->ImplicitCaptures) {
                    recordDependencyPathTo(out, capture);
                }
            }
            // Case 7: MemberExpr (e.g., e.&val)
            else if (auto *Memb = dynamic_cast<MemberExpr *>(E)) {
                bool isRef = false;
                bool isAddressOf = Memb->Member.find('&') != std::string::npos;
                if (isAddressOf || Memb->Member.find('^') != std::string::npos || Memb->Member.find('~') != std::string::npos) {
                    isRef = true;
                }
                if (isRef) {
                    std::string path = getPath(Memb);
                    if (!path.empty()) {
                        out.insert(path);
                    }
                }
            }
          };

          auto collectDeps = [&](Expr *E) {
            collectDepsInto(E, returnedDeps);
          };

          auto cleanFieldName = [](std::string name) {
            while (!name.empty() &&
                   (name[0] == '&' || name[0] == '*' || name[0] == '^' ||
                    name[0] == '~')) {
              name.erase(name.begin());
            }
            while (!name.empty() &&
                   (name.back() == '#' || name.back() == '?' ||
                    name.back() == '$' || name.back() == '!')) {
              name.pop_back();
            }
            return toka::Type::stripMorphology(name);
          };

          std::map<std::string, std::set<std::string>> returnedMemberDeps;
          std::function<void(Expr *)> collectMemberDeps = [&](Expr *E) {
            if (!E)
              return;
            if (auto *Cast = dynamic_cast<CastExpr *>(E)) {
              collectMemberDeps(Cast->Expression.get());
            } else if (auto *Bin = dynamic_cast<BinaryExpr *>(E)) {
              if (Bin->Op == "=")
                collectMemberDeps(Bin->RHS.get());
            } else if (auto *Init = dynamic_cast<InitStructExpr *>(E)) {
              for (auto &Mem : Init->Members) {
                std::string field = cleanFieldName(Mem.first);
                if (field.empty() || field == ".." || field == "*")
                  continue;
                collectDepsInto(Mem.second.get(), returnedMemberDeps[field]);
              }
            } else if (auto *Anon = dynamic_cast<AnonymousRecordExpr *>(E)) {
              for (auto &Field : Anon->Fields) {
                std::string field = cleanFieldName(Field.first);
                if (field.empty())
                  continue;
                collectDepsInto(Field.second.get(), returnedMemberDeps[field]);
              }
            } else if (auto *Var = dynamic_cast<VariableExpr *>(E)) {
              SymbolInfo info;
              if (CurrentScope->lookup(Var->Name, info)) {
                for (const auto &pair : info.FieldDependencySet) {
                  returnedMemberDeps[pair.first].insert(pair.second.begin(),
                                                        pair.second.end());
                }
              }
            }
          };

          collectDeps(Ret->ReturnValue.get());
          collectMemberDeps(Ret->ReturnValue.get());
          for (const auto &dep : m_LastLifeDependencies)
            recordDependencyPath(dep);
          if (!m_LastBorrowSource.empty())
            recordDependencyPath(m_LastBorrowSource);

          // Validate dependencies against declared LifeDependencies
          if (CurrentFunction) {
            // [NEW] TCB boundary enforcement for untraced escape signals
            if (returnedDeps.count("__untraced_escape")) {
              std::string fnName = CurrentFunction->Name;
              bool isUnsafeFn = (fnName.rfind("unsafe_", 0) == 0 ||
                                 fnName.rfind("raw_", 0) == 0 ||
                                 fnName.rfind("__", 0) == 0);
              if (!isUnsafeFn) {
                // Safe TCB boundary: trigger株连 or immediate 枪决
                if (CurrentFunction->Args.empty()) {
                  DiagnosticEngine::report(getLoc(Ret), DiagID::ERR_ESCAPE_LOCAL, "untraced unsafe cast");
                  HasError = true;
                  recordDecision(
                      Ret, SemanticRuleID::EffRet001,
                      SemanticOperation::EscapingDependency,
                      SemanticDecision::ConservativeReject,
                      SemanticReason::UnknownProvenance,
                      "untraced unsafe cast");
                } else {
                  for (const auto &Arg : CurrentFunction->Args) {
                    // Only implicate parameters that can carry lifetimes (i.e. not pure value types)
                    bool isValueType = false;
                    if (Arg.ResolvedType && (Arg.ResolvedType->isInteger() || Arg.ResolvedType->isFloatingPoint() || Arg.ResolvedType->isBoolean())) {
                      isValueType = true;
                    }
                    if (!isValueType) {
                      returnedDeps.insert(Arg.Name);
                    }
                  }
                }
              }
              returnedDeps.erase("__untraced_escape");
            }

            auto isDepMatch = [](const std::string &d, const std::string &a) -> bool {
              if (d == a) return true;
              // a is sub-path of d (e.g., d is self.buf, a is self)
              if (d.size() > a.size() && d.substr(0, a.size() + 1) == a + ".") return true;
              // d is sub-path of a (e.g., d is self, a is self.buf)
              if (a.size() > d.size() && a.substr(0, d.size() + 1) == d + ".") return true;
              return false;
            };

            for (const auto &fieldPair : returnedMemberDeps) {
              auto declaredIt = CurrentFunction->MemberDependencies.find(fieldPair.first);
              if (declaredIt == CurrentFunction->MemberDependencies.end())
                continue;

              for (const auto &actualDep : fieldPair.second) {
                if (actualDep == "__untraced_escape")
                  continue;
                bool allowedForField = false;
                for (const auto &allowedDep : declaredIt->second) {
                  if (isDepMatch(actualDep, allowedDep)) {
                    allowedForField = true;
                    break;
                  }
                }

                if (!allowedForField) {
                  DiagnosticEngine::report(getLoc(Ret),
                                           DiagID::ERR_LIFETIME_UNION_REQUIRED,
                                           actualDep, actualDep);
                  HasError = true;
                  SourceLocation originLoc = findPathDeclaration(actualDep);
                  recordDecision(
                      Ret, SemanticRuleID::EffMember001,
                      SemanticOperation::MemberDependency,
                      SemanticDecision::Reject,
                      SemanticReason::MemberDependencyMismatch, actualDep,
                      fieldPair.first, originLoc);
                  if (originLoc.isValid())
                    DiagnosticEngine::report(
                        originLoc, DiagID::NOTE_GENERIC,
                        "returned dependency originates here");
                }
              }
            }

            bool hasLocalDependency = false;
            for (const auto &dep : returnedDeps) {
              std::string baseDep = dep.substr(0, dep.find('.'));
              bool isParam = false;
              for (const auto &Arg : CurrentFunction->Args) {
                if (Arg.Name == baseDep) {
                  isParam = true;
                  break;
                }
              }
              if (!isParam) {
                hasLocalDependency = true;
                break;
              }
            }

            for (const auto &dep : returnedDeps) {
              // 1. Is it a parameter that can outlive the function?
              bool isParam = false;
              std::string baseDep = dep;
              size_t dotPos = baseDep.find('.');
              if (dotPos != std::string::npos) baseDep = baseDep.substr(0, dotPos);

              for (const auto &Arg : CurrentFunction->Args) {
                if (Arg.Name == baseDep) {
                    isParam = true;
                    break;
                }
              }

              if (!isParam) {
                DiagnosticEngine::report(getLoc(Ret), DiagID::ERR_ESCAPE_LOCAL, dep);
                HasError = true;
                SourceLocation originLoc = findPathDeclaration(dep);
                recordDecision(
                    Ret, SemanticRuleID::EffRet001,
                    SemanticOperation::EscapingDependency,
                    SemanticDecision::Reject, SemanticReason::LocalEscape,
                    dep, dep, originLoc);
                if (originLoc.isValid())
                  DiagnosticEngine::report(originLoc, DiagID::NOTE_GENERIC,
                                           "escaping local declared here");
                continue;
              }

              // A direct local escape is the primary fault. Transitive
              // parameter provenance is retained internally but would only
              // produce a secondary, misleading declaration suggestion.
              if (hasLocalDependency)
                continue;

              // 2. Is it allowed via effects?
              bool allowed = false;
              for (const auto &allowedDep : CurrentFunction->LifeDependencies) {
                if (isDepMatch(dep, allowedDep)) {
                  allowed = true;
                  break;
                }
              }
              if (!allowed) {
                for (const auto &pair : CurrentFunction->MemberDependencies) {
                   for (const auto &allowedDep : pair.second) {
                     if (isDepMatch(dep, allowedDep)) {
                       allowed = true;
                       break;
                     }
                   }
                   if (allowed) break;
                }
              }

              if (!allowed) {
                DiagnosticEngine::report(getLoc(Ret), DiagID::ERR_LIFETIME_UNION_REQUIRED, dep, dep);
                HasError = true;
                SourceLocation originLoc = findPathDeclaration(dep);
                recordDecision(
                    Ret, SemanticRuleID::EffRet001,
                    SemanticOperation::EscapingDependency,
                    SemanticDecision::Reject,
                    SemanticReason::MissingReturnDependency, dep,
                    CurrentFunction ? CurrentFunction->Name : "", originLoc);
                if (originLoc.isValid())
                  DiagnosticEngine::report(
                      originLoc, DiagID::NOTE_GENERIC,
                      "undeclared return dependency originates here");
              }
            }
          }
      }
    }


    std::shared_ptr<toka::Type> expectedRetObj = nullptr;
    if (CurrentFunction && CurrentFunction->ResolvedReturnType && CurrentFunctionReturnType == CurrentFunction->ReturnType) {
        expectedRetObj = CurrentFunction->ResolvedReturnType;
    } else {
        expectedRetObj = resolveType(toka::Type::fromString(CurrentFunctionReturnType));
    }

    bool bypassNullRet = false;
    if (m_InUnsafeContext && expectedRetObj && expectedRetObj->isRawPointer() && ExprTypeObj && ExprTypeObj->isNullType()) {
        bypassNullRet = true;
    }

    // Strict Ownership/Morphology Check for Return
    if (expectedRetObj && expectedRetObj->IsCede) {
      if (Ret->ReturnValue && !dynamic_cast<CedeExpr*>(Ret->ReturnValue.get())) {
        DiagnosticEngine::report(getLoc(Ret), DiagID::ERR_EXPECTED_CEDE_RETURN, CurrentFunctionReturnType);
        HasError = true;
        SourceLocation originLoc = CurrentFunction ? CurrentFunction->Loc
                                                   : SourceLocation{};
        recordDecision(Ret, SemanticRuleID::OwnCede002,
                       SemanticOperation::OwnershipTransfer,
                       SemanticDecision::Reject,
                       SemanticReason::MissingCedeReturn,
                       CurrentFunctionReturnType,
                       CurrentFunction ? CurrentFunction->Name : "",
                       originLoc);
        if (originLoc.isValid())
          DiagnosticEngine::report(originLoc, DiagID::NOTE_GENERIC,
                                   "cede return declared here");
      } else if (Ret->ReturnValue) {
        recordDecision(Ret, SemanticRuleID::OwnCede002,
                       SemanticOperation::OwnershipTransfer,
                       SemanticDecision::Allow, SemanticReason::CedeConsumed,
                       CurrentFunctionReturnType,
                       CurrentFunction ? CurrentFunction->Name : "",
                       CurrentFunction ? CurrentFunction->Loc
                                       : SourceLocation{});
      }
    }

    if (!bypassNullRet && !HasError && !isTypeCompatible(expectedRetObj, ExprTypeObj)) {
      bool handled = false;
      if (expectedRetObj && expectedRetObj->isReference()) {
        if (auto *ve = dynamic_cast<VariableExpr *>(Ret->ReturnValue.get())) {
          SymbolInfo info;
          if (CurrentScope->lookup(ve->Name, info)) {
            auto expectedPtr = std::static_pointer_cast<toka::PointerType>(expectedRetObj);
            if (isTypeCompatible(expectedPtr->PointeeType, info.TypeObj)) {
               DiagnosticEngine::report(getLoc(Ret), DiagID::ERR_MISSING_AMPERSAND_RETURN, 
                 ExprType, CurrentFunctionReturnType, ve->Name);
               HasError = true;
               handled = true;
            }
          }
        }
      }
      if (!handled) {
        DiagnosticEngine::report(getLoc(Ret), DiagID::ERR_TYPE_MISMATCH, ExprType,
                                 CurrentFunctionReturnType);
        HasError = true;
      }
    } else if (!HasError) {

      MorphKind targetMorph =
          morphKindFromTypeString(CurrentFunctionReturnType);
      MorphKind sourceMorph = getSyntacticMorphology(Ret->ReturnValue.get());
      
      bool exempt = false;
      Expr *e = Ret->ReturnValue.get();
      while (e) {
          if (e->IsMorphicExempt) { exempt = true; break; }
          if (auto *un = dynamic_cast<UnaryExpr *>(e)) e = un->RHS.get();
          else break;
      }
      
      if (!exempt) {
          checkStrictMorphology(Ret, targetMorph, sourceMorph, "return value");
      }
    }
    m_LastBorrowSource.clear();
    m_LastLifeDependencies.clear();
    m_LastFieldDependencies.clear();
  } else if (auto *Free = dynamic_cast<FreeStmt *>(S)) {
    Free->Expression = foldGenericConstant(std::move(Free->Expression));
    auto FreeTypeObj = checkExpr(Free->Expression.get());
    if (!FreeTypeObj->isRawPointer()) {
      std::string ExprType = FreeTypeObj->toString();
      if (FreeTypeObj->isSmartPointer()) {
        DiagnosticEngine::report(getLoc(Free), DiagID::ERR_FREE_SMART,
                                 ExprType);
        HasError = true;
      } else {
        DiagnosticEngine::report(getLoc(Free), DiagID::ERR_FREE_NON_PTR,
                                 ExprType);
        HasError = true;
      }
    }
  } else if (auto *Unsafe = dynamic_cast<UnsafeStmt *>(S)) {
    bool oldUnsafe = m_InUnsafeContext;
    m_InUnsafeContext = true;
    checkStmt(Unsafe->Statement.get());
    m_InUnsafeContext = oldUnsafe;
  } else if (auto *ExprS = dynamic_cast<ExprStmt *>(S)) {
    // Standalone expressions are NOT receivers
    m_ControlFlowStack.push_back({"", "void", nullptr, false, false});
    ExprS->Expression = foldGenericConstant(std::move(ExprS->Expression));
    auto exprType = checkExpr(ExprS->Expression.get());
    m_ControlFlowStack.pop_back();

    if (exprType) {
      std::string soul = exprType->getSoulName();
      if (soul == "Result" || (soul.size() > 7 && soul.substr(0, 7) == "Result<")) {
        bool isWarningExempt = false;
        if (ExprS->Loc.isValid()) {
          std::string path = DiagnosticEngine::SrcMgr->getFullSourceLoc(ExprS->Loc).FileName;
          if (path.find("tests/") != std::string::npos ||
              path.find("build.tk") != std::string::npos ||
              path.find("prelude") != std::string::npos ||
              path.find("lib/") != std::string::npos) {
            isWarningExempt = true;
          }
        }
        if (!isWarningExempt) {
          DiagnosticEngine::report(ExprS->Loc, DiagID::WARN_UNUSED_RESULT, soul);
        }
      }
    }

  } else if (auto *Var = dynamic_cast<VariableDecl *>(S)) {
    recordHandleSurfaceVariableDecl(*Var);

    // [Constitutional 1.3] Adversarial Principle: $ is only for contesting
    // inheritance.
    if (Var->IsValueBlocked || Var->IsRebindBlocked) {
      DiagnosticEngine::report(getLoc(Var), DiagID::ERR_REDUNDANT_BLOCK,
                               Var->Name);
      HasError = true;
    }
    if (!Var->ResolvedType && !Var->TypeName.empty() &&
        Var->TypeName != "auto") {
      validateTypeVisibilityInType(Var->TypeName, getLoc(Var));
      validateDynTraitObjectSafetyInType(Var->TypeName, getLoc(Var));
    }

    std::string InitType = "";
    std::shared_ptr<toka::Type> InitTypeObj = nullptr;
    if (Var->Init) {
      Var->Init = foldGenericConstant(std::move(Var->Init));
      // [Auto-Clone]
      if (!Var->IsReference) {
        tryInjectAutoClone(Var->Init);
      }
      if (Var->IsReference)
        m_AllowUnsetUsage = true;
      m_ControlFlowStack.push_back({Var->Name, "void", nullptr, false, true});
      std::shared_ptr<toka::Type> declTargetTy = nullptr;
      if (!Var->TypeName.empty() && Var->TypeName != "auto") {
        declTargetTy = resolveType(toka::Type::fromString(Var->TypeName), false);
        if (declTargetTy && (declTargetTy->typeKind == toka::Type::Function || declTargetTy->typeKind == toka::Type::DynFn)) {
           if (auto clo = dynamic_cast<ClosureExpr*>(Var->Init.get())) {
              std::vector<std::shared_ptr<Type>> paramTypes;
              std::shared_ptr<Type> returnType;
              if (declTargetTy->typeKind == toka::Type::DynFn) {
                  auto fnTy = std::static_pointer_cast<toka::DynFnType>(declTargetTy);
                  paramTypes = fnTy->ParamTypes;
                  returnType = fnTy->ReturnType;
              } else {
                  auto fnTy = std::static_pointer_cast<toka::FunctionType>(declTargetTy);
                  paramTypes = fnTy->ParamTypes;
                  returnType = fnTy->ReturnType;
              }
              
              clo->InjectedParamTypes = paramTypes;
              if ((clo->ReturnType.empty() || clo->ReturnType == "unknown") && returnType) {
                  clo->ReturnType = returnType->toString();
              }
           }
        }
      }
      
      bool oldExpectedWritability = m_ExpectedWritability;
      if (Var->IsReference) {
          m_ExpectedWritability = Var->IsValueMutable;
      }
      m_LastBorrowSource = ""; // [NEW] Clear stale borrow source
      InitTypeObj = checkExpr(Var->Init.get(), declTargetTy);
      m_ExpectedWritability = oldExpectedWritability;
      InitType = InitTypeObj->toString();
      
      if (Var->IsReference && Var->Init) {
        Expr *initExpr = Var->Init.get();
        while (true) {
          if (auto *cast = dynamic_cast<CastExpr *>(initExpr)) {
            initExpr = cast->Expression.get();
          } else if (auto *unsafe = dynamic_cast<UnsafeExpr *>(initExpr)) {
            initExpr = unsafe->Expression.get();
          } else {
            break;
          }
        }
        if (auto *memb = dynamic_cast<MemberExpr *>(initExpr)) {
          auto objType = memb->Object->ResolvedType;
          if (objType) {
            std::shared_ptr<Type> soulType = objType->getSoulType();
            std::string soulName = Type::stripMorphology(resolveType(soulType, true)->toString());
            if (ShapeMap.count(soulName)) {
              ShapeDecl *SD = ShapeMap[soulName];
              if (memb->Index >= 0 && memb->Index < (int)SD->Members.size()) {
                const auto &field = SD->Members[memb->Index];
                bool fieldIsPointer = field.IsReference || field.IsUnique || field.IsShared || field.IsRawPointer;
                if (fieldIsPointer) {
                  bool hasPrefix = !memb->Member.empty() && 
                                   (memb->Member[0] == '&' || memb->Member[0] == '^' || 
                                    memb->Member[0] == '~' || memb->Member[0] == '*');
                  if (!hasPrefix) {
                    DiagnosticEngine::report(Var->Loc, DiagID::ERR_MORPHOLOGY_MISMATCH,
                                             "&", "value");
                    HasError = true;
                  }
                }
              }
            }
          }
        }
      }
      
      m_AllowUnsetUsage = false;
    }

    bool initIsExplicitReferenceView =
        Var->Init && getSyntacticMorphology(Var->Init.get()) == MorphKind::Ref;
    if (Var->IsReference && Var->IsValueMutable && Var->Init &&
        (isReadOnlyReferenceViewInitializer(Var->Init.get(), CurrentScope) ||
         (isReadOnlyReferenceType(InitTypeObj) &&
          !initIsExplicitReferenceView))) {
      DiagnosticEngine::report(getLoc(Var),
                               DiagID::ERR_SEMA_COVENANT_VIOLATION_CANNOT_ELEVATE_WRITE_P);
      HasError = true;
    }

    // 4. If type not specified, infer from init
    if (Var->TypeName.empty() || Var->TypeName == "auto") {
      if (InitType.empty() || InitType == "void") {
        DiagnosticEngine::report(getLoc(Var), DiagID::ERR_TYPE_REQUIRED,
                                 Var->Name);
        HasError = true;
        Var->TypeName = "unknown";
      } else {
        std::string Inferred = InitType;
        // A cede return marks transfer across the call edge. Binding the
        // result accepts that transfer, so the local variable has the
        // underlying owned type rather than a persistent cede-qualified type.
        if (Inferred.rfind("cede ", 0) == 0) {
          Inferred = Inferred.substr(5);
        }
        if (Inferred == "null") {
          DiagnosticEngine::report(getLoc(Var), DiagID::ERR_INFER_NULLPTR);
          HasError = true;
          Var->TypeName = "unknown";
          return;
        }

        // If variable declares morphology (auto ^p = ...), strip matching
        // morphology from inferred soul
        if (Var->IsRawPointer || Var->IsUnique || Var->IsShared ||
            Var->IsReference) {
          if (Inferred.find("nul ") == 0) Inferred = Inferred.substr(4);
          if (!Inferred.empty() && (Inferred[0] == '*' || Inferred[0] == '^' ||
                                    Inferred[0] == '~' || Inferred[0] == '&')) {
            Inferred = Inferred.substr(1);
            if (!Inferred.empty() && (Inferred[0] == '?' ||
                                      Inferred[0] == '!' || Inferred[0] == '#'))
              Inferred = Inferred.substr(1);
          } else {
            // [NEW] Strict Explicit Memory Allocation. 
            // Implicit boxing like `auto ^p = shape` is structurally prohibited.
            if (Var->IsShared || Var->IsUnique) {
               DiagnosticEngine::report(getLoc(Var), DiagID::ERR_IMPLICIT_BOX_PROHIBITED, Inferred);
               HasError = true;
               Var->TypeName = "unknown";
               return;
            } else if (Var->IsRawPointer) {
               DiagnosticEngine::report(getLoc(Var), DiagID::ERR_INIT_TYPE_MISMATCH, "*(Raw Pointer)", Inferred);
               HasError = true;
               Var->TypeName = "unknown";
               return;
            }
          }
        } else {
          if (!Var->IsMorphicExempt && !Inferred.empty() &&
              (Inferred[0] == '*' || Inferred[0] == '^' || Inferred[0] == '~' ||
               Inferred[0] == '&')) {
            std::string sigilStr = std::string(1, Inferred[0]);
            DiagnosticEngine::report(getLoc(Var), (int)Var->Name.length(),
                                     DiagID::ERR_POINTER_SIGIL_MISSING,
                                     Var->Name, Inferred, sigilStr, Var->Name);
            HasError = true;
            Var->TypeName = "unknown";
            return;
          }
        }
        

        
        // [New] Decay inherently mutable auto-inferred types to ReadOnly if var doesn't declare it.
        if (!Var->IsValueMutable && !Inferred.empty() && Inferred.back() == '#') {
            Inferred.pop_back();
        }

        Var->TypeName = Inferred;
      }
    } else {
      // Compatibility Check
      std::string DeclFullTy = Var->TypeName;
      std::string Morph = "";
      if (Var->IsRawPointer)
        Morph = "*";
      else if (Var->IsUnique)
        Morph = "^";
      else if (Var->IsShared)
        Morph = "~";
      else if (Var->IsReference)
        Morph = "&";
      if (!Morph.empty()) {
        if (Var->IsRebindable)
          Morph += "#";
        if (Var->IsPointerNullable)
          Morph = "nul " + Morph;
        DeclFullTy = Morph + DeclFullTy;
      }
      if (Var->IsValueMutable) {
        DeclFullTy += "#";
      }

      if (!InitType.empty() && !isTypeCompatible(toka::Type::fromString(resolveType(DeclFullTy)), InitTypeObj)) {
        std::string boxedType = (Var->IsShared ? "~" : (Var->IsUnique ? "^" : ""));
        if (!boxedType.empty()) {
           DiagnosticEngine::report(getLoc(Var), DiagID::ERR_IMPLICIT_BOX_PROHIBITED, InitType);
           HasError = true;
           Var->TypeName = "unknown";
           return;
        } else {
           DiagnosticEngine::report(getLoc(Var), DiagID::ERR_INIT_TYPE_MISMATCH, DeclFullTy, InitType);
           HasError = true;
        }
      } else if (!InitType.empty() && InitTypeObj) {
         auto declTargetTy = toka::Type::fromString(resolveType(DeclFullTy));
         bool isPrimitiveWidening = declTargetTy->typeKind == toka::Type::Primitive && InitTypeObj->typeKind == toka::Type::Primitive;
         if (!declTargetTy->equals(*InitTypeObj) && isPrimitiveWidening && !Var->IsShared && !Var->IsUnique) {
             auto origLoc = Var->Init->Loc;
             Var->Init = std::make_unique<CastExpr>(std::move(Var->Init), declTargetTy->toString());
             Var->Init->Loc = origLoc;
             Var->Init->ResolvedType = declTargetTy;
             InitTypeObj = declTargetTy;
             InitType = declTargetTy->toString();
         }
      }
    }

    // 5. Strict Morphology Check
    if (Var->Init &&
        Var->Permission.Morphology != BindingMorphology::Reference) {
      MorphKind lhsMorph = morphKindFromPermission(Var->Permission);
      MorphKind rhsMorph = getSyntacticMorphology(Var->Init.get());

      checkStrictMorphology(Var, lhsMorph, rhsMorph, Var->Name);
    }

    SymbolInfo Info;
    std::string morph = "";
    if (Var->IsRawPointer)
      morph = "*";
    else if (Var->IsUnique)
      morph = "^";
    else if (Var->IsShared)
      morph = "~";
    else if (Var->IsReference)
      morph = "&";

    std::string baseType = Var->TypeName;
    bool hadNul = false;
    if (baseType.size() > 4 && baseType.substr(0, 4) == "nul ") {
      hadNul = true;
      baseType = baseType.substr(4);
    }
    
    // Strip redundant sigil from baseType if it matches morph
    if (baseType.size() > 1 && (baseType[0] == '^' || baseType[0] == '~' ||
                                baseType[0] == '*' || baseType[0] == '&')) {
      if (morph.empty()) {
        morph = std::string(1, baseType[0]);
        baseType = baseType.substr(1);
      } else if (morph[0] == baseType[0]) {
        baseType = baseType.substr(1);
      }
    }
    
    if (baseType.size() > 1 && baseType[0] == '#') {
       baseType = baseType.substr(1);
    }

    if (!morph.empty()) {
      if (Var->IsRebindable && morph.find('#') == std::string::npos)
        morph += "#";
      if ((Var->IsPointerNullable || hadNul) && morph.find("nul") == std::string::npos)
        morph = "nul " + morph;
    }
    std::set<std::string> depsToCommitAsBorrow;
    if (!m_LastLifeDependencies.empty()) {
      for (const auto &dep : m_LastLifeDependencies) {
        Info.LifeDependencySet.insert(dep);
        depsToCommitAsBorrow.insert(dep);
        SymbolInfo *depInfo = nullptr;
        if (CurrentScope->findSymbol(dep, depInfo)) {
            Info.LifeDependencySet.insert(depInfo->LifeDependencySet.begin(), depInfo->LifeDependencySet.end());
            depsToCommitAsBorrow.insert(depInfo->LifeDependencySet.begin(),
                                        depInfo->LifeDependencySet.end());
        }

        int srcDepth = getScopeDepth(dep);
        int myDepth = CurrentScope->Depth;
        if (myDepth < srcDepth) {
          DiagnosticEngine::report(getLoc(Var), DiagID::ERR_BORROW_LIFETIME,
                                   Var->Name, dep);
          HasError = true;
          SourceLocation originLoc = findPathDeclaration(dep);
          recordDecision(Var, SemanticRuleID::EffRet001,
                         SemanticOperation::EscapingDependency,
                         SemanticDecision::Reject,
                         SemanticReason::LifetimeDepthViolation, Var->Name,
                         dep, originLoc);
          if (originLoc.isValid())
            DiagnosticEngine::report(originLoc, DiagID::NOTE_GENERIC,
                                     "shorter-lived dependency declared here");
        }
      }
      m_LastLifeDependencies.clear();
    }

    if (auto *clo = dynamic_cast<ClosureExpr *>(Var->Init.get())) {
      for (const auto &dep : clo->ImplicitCaptures) {
        Info.LifeDependencySet.insert(dep);
        SymbolInfo *depInfo = nullptr;
        if (CurrentScope->findSymbol(dep, depInfo)) {
          Info.LifeDependencySet.insert(depInfo->LifeDependencySet.begin(),
                                        depInfo->LifeDependencySet.end());
        }
      }
    }

    if (morph == "&" && !m_LastBorrowSource.empty()) {
      Info.BorrowedFrom = m_LastBorrowSource;
      Info.LifeDependencySet.insert(m_LastBorrowSource);

      SymbolInfo *srcPtr = nullptr;
      if (CurrentScope->findSymbol(m_LastBorrowSource, srcPtr)) {
        Info.LifeDependencySet.insert(srcPtr->LifeDependencySet.begin(), srcPtr->LifeDependencySet.end());

        // [NEW] Lifetime check: Depth(Me) >= Depth(Src)
        int srcDepth = getScopeDepth(m_LastBorrowSource);
        int myDepth = CurrentScope->Depth;
        if (myDepth < srcDepth) {
          DiagnosticEngine::report(getLoc(Var), DiagID::ERR_BORROW_LIFETIME,
                                   Var->Name, m_LastBorrowSource);
          HasError = true;
          SourceLocation originLoc = findPathDeclaration(m_LastBorrowSource);
          recordDecision(Var, SemanticRuleID::EffRet001,
                         SemanticOperation::EscapingDependency,
                         SemanticDecision::Reject,
                         SemanticReason::LifetimeDepthViolation, Var->Name,
                         m_LastBorrowSource, originLoc);
          if (originLoc.isValid())
            DiagnosticEngine::report(originLoc, DiagID::NOTE_GENERIC,
                                     "shorter-lived dependency declared here");
        }

        // [Hot Potato] Propagate InitMask from Source to Reference
        uint64_t fullMask = ~0ULL;
        if (srcPtr->TypeObj && srcPtr->TypeObj->isShape()) {
          std::string soul = srcPtr->TypeObj->getSoulName();
          if (ShapeMap.count(soul)) {
            ShapeDecl *SD = ShapeMap[soul];
            uint64_t bits = (1ULL << SD->Members.size()) - 1;
            if (SD->Members.size() >= 64)
              bits = ~0ULL;
            fullMask = bits;
          }
        }

        if ((srcPtr->InitMask & fullMask) != fullMask) {
          // It's Dirty!
          Info.DirtyReferentMask = srcPtr->InitMask;
        } else {
          Info.DirtyReferentMask = ~0ULL; // Clean
        }
      }

      Info.BorrowedFrom = m_LastBorrowSource;
      if (!m_LastBorrowSource.empty()) {
          PALCheckerState.commitTransient(
              canonicalizeAccessPath(makeAccessPath(m_LastBorrowSource)));
      }
      for (const auto &dep : Info.LifeDependencySet) {
          PALCheckerState.commitTransient(
              canonicalizeAccessPath(makeAccessPath(dep)));
      }
    }
    
    if (!m_LastFieldDependencies.empty()) {
      for (const auto &pair : m_LastFieldDependencies) {
        for (const auto &dep : pair.second) {
          std::string actualDep = dep;
          if (actualDep.rfind("self.", 0) == 0)
            actualDep = Var->Name + actualDep.substr(4);
          Info.FieldDependencySet[pair.first].insert(actualDep);
          Info.LifeDependencySet.insert(actualDep);
          depsToCommitAsBorrow.insert(actualDep);
        }
      }
      m_LastFieldDependencies.clear();
    }

    m_LastBorrowSource = ""; // Clear for next var

    // [Constitution 1.3] Dual-Attribute Synthesis
    BindingPermission LocalPermission;
    if (!morph.empty()) {
      std::string normalizedMorph = morph;
      if (normalizedMorph.rfind("nul ", 0) == 0)
        normalizedMorph = normalizedMorph.substr(4);

      switch (normalizedMorph.empty() ? '\0' : normalizedMorph[0]) {
      case '^':
        LocalPermission.Morphology = BindingMorphology::Unique;
        break;
      case '~':
        LocalPermission.Morphology = BindingMorphology::Shared;
        break;
      case '&':
        LocalPermission.Morphology = BindingMorphology::Reference;
        break;
      case '*':
        LocalPermission.Morphology = BindingMorphology::Raw;
        break;
      default:
        break;
      }

      LocalPermission.IdentityRebindable = Var->IsRebindable;
      LocalPermission.IdentityNullable = Var->IsPointerNullable || hadNul;
    }
    // Preserve the legacy local rule: without a handle morphology, # acts on
    // the soul/value rather than as a rebindable identity marker.
    LocalPermission.SoulWritable =
        Var->IsValueMutable || (morph.empty() && Var->IsRebindable);
    LocalPermission.SoulNullable = Var->IsValueNullable;

    std::string fullType =
        Sema::synthesizePhysicalType(LocalPermission, baseType, false);

    Info.TypeObj = resolveType(toka::Type::fromString(fullType), false);
    if (!Info.TypeObj) {
      Info.TypeObj = toka::Type::fromString(fullType);
    }

    std::string dependencySoul =
        Info.TypeObj ? Info.TypeObj->getSoulName() : "";
    bool isBorrowedViewValue =
        dependencySoul == "str" || dependencySoul == "bytes";
    bool isBorrowLikeStoredValue = isBorrowLikeType(Info.TypeObj);
    if (!depsToCommitAsBorrow.empty() &&
        (Var->IsReference || morph == "&" || isBorrowedViewValue ||
         isBorrowLikeStoredValue || !Info.FieldDependencySet.empty())) {
      for (const auto &dep : depsToCommitAsBorrow) {
        if (dep.empty())
          continue;
        AccessPath dependencyPath =
            canonicalizeAccessPath(makeAccessPath(dep));
        if (!PALCheckerState.recordBorrow(dependencyPath, false,
                                          getLoc(Var))) {
          DiagnosticEngine::report(getLoc(Var), DiagID::ERR_BORROW_MUT, dep);
          HasError = true;
          if (PALCheckerState.lastConflict()) {
            recordPALConflict(
                Var, PALOperationClass::SharedPayloadBorrow, dependencyPath,
                *PALCheckerState.lastConflict());
          }
        }
        PALCheckerState.commitTransient(dependencyPath);
      }
    }

    Info.IsRebindable = Var->IsRebindable;
    Info.IsMorphicExempt = Var->IsMorphicExempt; // [NEW]
    Info.IsDeclaredMutable = Var->IsValueMutable;
    Info.DeclLoc = Var->Loc;
    Var->ResolvedType = Info.TypeObj;
    if (Info.TypeObj && (Info.TypeObj->isFunction() || Info.TypeObj->isDynFn()))
      Info.CallableReceiver = getCallableReceiverMode(*Info.TypeObj);
    if (auto *closure = dynamic_cast<ClosureExpr *>(Var->Init.get())) {
      if (!Info.TypeObj ||
          (!Info.TypeObj->isFunction() && !Info.TypeObj->isDynFn()))
        Info.CallableReceiver = closure->CallableReceiver;
    }

    if (Var->Init) {
      Info.InitMask = m_LastInitMask;
    } else {
      Info.InitMask = 0;
    }

    // Rule: Numeric Substitution (Constant variables)
    if (Var->Init && Var->TypeName == "i32" && !Var->IsValueMutable) {
      if (auto *Num = dynamic_cast<NumberExpr *>(Var->Init.get())) {
        Info.HasConstValue = true;
        Info.ConstValue = Num->Value;
        Info.ConstValObj = ComptimeValue(Num->Value);
      }
      // Or if initialized with ANOTHER const variable (like N = M)
      else if (auto *RefVar = dynamic_cast<VariableExpr *>(Var->Init.get())) {
        SymbolInfo RefInfo;
        if (CurrentScope->lookup(RefVar->Name, RefInfo) &&
            RefInfo.HasConstValue) {
          Info.HasConstValue = true;
          Info.ConstValue = RefInfo.ConstValue;
          Info.ConstValObj = RefInfo.ConstValObj;
        }
      }
    }

    Info.IsDeclaredVariable = true;
    CurrentScope->define(Var->Name, Info);

    // Move Logic: If initializing from a Unique Variable, move it.
    if (Var->Init && Info.IsUnique()) {
      bool moveCheckedByExpression =
          dynamic_cast<UnaryExpr *>(Var->Init.get()) != nullptr ||
          dynamic_cast<CedeExpr *>(Var->Init.get()) != nullptr;
      Expr *InitExpr = Var->Init.get();
      // Unwrap unary ^ or ~ or * if it matches
      if (auto *Unary = dynamic_cast<UnaryExpr *>(InitExpr)) {
        InitExpr = Unary->RHS.get();
      }

      if (auto *RHSVar = dynamic_cast<VariableExpr *>(InitExpr)) {
        SymbolInfo *SourceInfoPtr = nullptr;
        std::string actName;
        if (CurrentScope->findVariableWithDeref(RHSVar->Name, SourceInfoPtr, actName)) {
          if (SourceInfoPtr->IsUnique()) {
            if (!SourceInfoPtr->Moved) {
              if (!moveCheckedByExpression) {
                auto conflict = PALCheckerState.verifyInvalidation(
                    canonicalizeAccessPath(makeAccessPath(actName)));
                if (conflict) {
                  DiagnosticEngine::report(getLoc(Var),
                                           DiagID::ERR_MOVE_BORROWED,
                                           conflict->displayPath());
                  HasError = true;
                  recordPALConflict(
                      Var, PALOperationClass::Invalidation,
                      canonicalizeAccessPath(makeAccessPath(actName)),
                      *conflict);
                }
              }
              CurrentScope->markMoved(actName, getLoc(Var));
            }
          }
        }
      }
      
      Expr *InitScan = InitExpr;
      while (true) {
          if (auto *un = dynamic_cast<UnaryExpr *>(InitScan)) {
              InitScan = un->RHS.get();
          } else if (auto *ce = dynamic_cast<CedeExpr *>(InitScan)) {
              InitScan = ce->Value.get();
          } else {
              break;
          }
      }

      if (auto *Memb = dynamic_cast<MemberExpr *>(InitScan)) {
        // [Move Restriction Rule] Prohibit moving member out of shape that
        // has drop() Rule applies if we are moving any resource
        bool memberIsResource = InitTypeObj->isUniquePtr();
        if (!memberIsResource && InitTypeObj->isShape()) {
            std::string rhsSoul = toka::Type::stripMorphology(InitTypeObj->getSoulName());
            if (hasDrop(rhsSoul)) {
                memberIsResource = true;
            }
        }

        if (memberIsResource) {
          auto objType = checkExpr(Memb->Object.get());
          std::shared_ptr<toka::Type> soulType = objType->getSoulType();
          std::string soul = toka::Type::stripMorphology(soulType->getSoulName());
          if (hasDrop(soul)) {
            error(Var, DiagID::ERR_MOVE_MEMBER_DROP, Memb->Member, soul);
            HasError = true;
          }
        }
      }
    }

    if (Var->Init) {
      m_ControlFlowStack.pop_back();
    }
  } else if (auto *Destruct = dynamic_cast<DestructuringDecl *>(S)) {
    auto initType = checkExpr(Destruct->Init.get());
    auto declType = toka::Type::fromString(Destruct->TypeName);

    // Basic check: declType should match initType
    if (!Destruct->TypeName.empty() && !initType->isUnknown() &&
        !isTypeCompatible(declType, initType)) {
      DiagnosticEngine::report(getLoc(Destruct), DiagID::ERR_TYPE_MISMATCH,
                               initType->toString(), declType->toString());
      HasError = true;
    }

    std::string soulName = Type::stripMorphology(Destruct->TypeName);
    if (soulName.empty() && initType && initType->isShape()) {
      soulName = Type::stripMorphology(initType->getSoulName());
    }
    if (!soulName.empty()) {
      soulName = resolveType(soulName, true);
    }

    size_t elisionIndex = -1;
    size_t elisionCount = 0;
    for (size_t i = 0; i < Destruct->Variables.size(); ++i) {
      if (Destruct->Variables[i].Name == "..") {
        elisionIndex = i;
        elisionCount++;
      }
    }

    if (elisionCount > 1) {
      DiagnosticEngine::report(getLoc(Destruct), DiagID::ERR_MULTIPLE_ELISION);
      HasError = true;
    }

    if (!soulName.empty() && ShapeMap.count(soulName)) {
      ShapeDecl *SD = ShapeMap[soulName];
      if (SD->Kind == ShapeKind::Struct) {
        auto getMorphFromString = [](const std::string &str) -> MorphKind {
          if (str.find('^') != std::string::npos) return MorphKind::Unique;
          if (str.find('~') != std::string::npos) return MorphKind::Shared;
          if (str.find('&') != std::string::npos) return MorphKind::Ref;
          if (str.find('*') != std::string::npos) return MorphKind::Raw;
          return MorphKind::None;
        };

        // 1. Preprocess and fill in FieldName for Field Punning and Positional
        std::vector<bool> wasFieldNameEmpty(Destruct->Variables.size(), false);
        size_t elisionIndex = -1;
        size_t elisionCount = 0;
        for (size_t i = 0; i < Destruct->Variables.size(); ++i) {
          if (Destruct->Variables[i].Name == "..") {
            elisionIndex = i;
            elisionCount++;
          }
        }

        size_t expectedSize = SD->Members.size();
        size_t varsWithoutElision = Destruct->Variables.size() - (elisionCount > 0 ? 1 : 0);

        for (size_t i = 0; i < Destruct->Variables.size(); ++i) {
          auto &v = Destruct->Variables[i];
          if (v.Name == "..") continue;
          if (v.FieldName.empty()) {
            wasFieldNameEmpty[i] = true;
            v.FieldName = v.Name;
          }
        }

        // 2. Verify duplicates in deconstruction list
        std::set<std::string> seenFields;
        for (const auto &v : Destruct->Variables) {
          if (v.Name == "..") continue;
          if (v.FieldName == "_") {
            error(Destruct, DiagID::ERR_SEMA_POSITIONAL_PLACEHOLDER_IS_NOT_ALLOWED_IN);
            continue;
          }
          std::string cleanField = toka::Type::stripMorphology(v.FieldName);
          if (seenFields.count(cleanField)) {
            DiagnosticEngine::report(getLoc(Destruct), DiagID::ERR_DUPLICATE_FIELD, v.FieldName);
            HasError = true;
          }
          seenFields.insert(cleanField);
        }

        // 3. Verify all specified fields exist in shape
        std::set<std::string> sdMembers;
        for (const auto &m : SD->Members) {
          sdMembers.insert(m.Name);
        }
        for (const auto &v : Destruct->Variables) {
          if (v.Name == "..") continue;
          if (v.FieldName != "_" && !sdMembers.count(toka::Type::stripMorphology(v.FieldName))) {
            DiagnosticEngine::report(getLoc(Destruct), DiagID::ERR_NO_SUCH_MEMBER, soulName, v.FieldName);
            HasError = true;
          }
        }

        // 4. Verify completeness (missing fields check)
        bool hasElision = false;
        for (const auto &v : Destruct->Variables) {
          if (v.Name == "..") {
            hasElision = true;
            break;
          }
        }
        if (!hasElision) {
          for (const auto &defField : SD->Members) {
            if (!seenFields.count(defField.Name)) {
              if (!defField.DefaultValue) {
                DiagnosticEngine::report(getLoc(Destruct), DiagID::ERR_MISSING_DEFAULT_FOR_ELIDED, defField.Name, soulName);
                HasError = true;
              }
            }
          }
        }

        // 5. Perform bindings
        for (size_t i = 0; i < Destruct->Variables.size(); ++i) {
          if (Destruct->Variables[i].Name == "..") continue;
          if (Destruct->Variables[i].IsWildcard && Destruct->Variables[i].FieldName == "_") {
            continue;
          }

          size_t memberIndex = -1;
          for (size_t m = 0; m < SD->Members.size(); ++m) {
            auto cleanDef = SD->Members[m].Name;
            while (!cleanDef.empty() &&
                   (cleanDef.back() == '#' || cleanDef.back() == '!' ||
                    cleanDef.back() == '?'))
              cleanDef.pop_back();

            auto cleanProv = Destruct->Variables[i].FieldName;
            while (!cleanProv.empty() &&
                   (cleanProv.back() == '#' || cleanProv.back() == '!' ||
                    cleanProv.back() == '?'))
              cleanProv.pop_back();

            if (cleanDef == cleanProv ||
                toka::Type::stripMorphology(cleanDef) == toka::Type::stripMorphology(cleanProv)) {
              memberIndex = m;
              break;
            }
          }
          if (memberIndex == (size_t)-1) continue;

          // Morphic validation
          if (!SD->Members[memberIndex].IsMorphicExempt) {
            auto memberTypeObj = getPhysicalType(SD->Members[memberIndex]);
            MorphKind expectedMorph = morphKindFromType(memberTypeObj);

            bool isMorphicExempt = (!Destruct->Variables[i].Name.empty() && Destruct->Variables[i].Name[0] == '\'');
            if (!isMorphicExempt) {
              MorphKind varMorph = getMorphFromString(Destruct->Variables[i].Name);
              
              if (!wasFieldNameEmpty[i]) {
                MorphKind fieldMorph = getMorphFromString(Destruct->Variables[i].FieldName);
                if (varMorph != fieldMorph) {
                  auto morphToString = [](MorphKind m) -> std::string {
                    switch (m) {
                      case MorphKind::None: return "plain value (None)";
                      case MorphKind::Raw: return "raw pointer (*)";
                      case MorphKind::Unique: return "unique pointer (^)";
                      case MorphKind::Shared: return "shared pointer (~)";
                      case MorphKind::Ref: return "reference (&)";
                      default: return "unknown";
                    }
                  };
                  DiagnosticEngine::report(getLoc(Destruct), DiagID::ERR_SEMA_MISMATCHED_MORPHOLOGY_IN_NAMED_DESTRUCTUR, Destruct->Variables[i].Name, morphToString(varMorph), Destruct->Variables[i].FieldName, morphToString(fieldMorph));
                  HasError = true;
                }
              }

              if (!(expectedMorph == MorphKind::None && varMorph == MorphKind::Ref)) {
                checkStrictMorphology(Destruct, expectedMorph, varMorph, SD->Members[memberIndex].Name);
              }
            }
          }

          {
            auto memberTypeObj = getPhysicalType(SD->Members[memberIndex]);
            bool isMorphicExempt = (!Destruct->Variables[i].Name.empty() && Destruct->Variables[i].Name[0] == '\'');
            if (memberTypeObj->isReference() && !Destruct->Variables[i].IsReference && !isMorphicExempt) {
              DiagnosticEngine::report(getLoc(Destruct), DiagID::ERR_SEMA_CANNOT_BIND_REFERENCE_MEMBER_TO_A_NON_REF, SD->Members[memberIndex].Name, Destruct->Variables[i].Name);
              HasError = true;
            }
          }

          SymbolInfo Info;
          std::string fullType = getPhysicalTypeName(SD->Members[memberIndex]);
          auto baseTypeObj = toka::Type::fromString(fullType);
          auto soulType = baseTypeObj->withAttributes(
              Destruct->Variables[i].IsValueMutable,
              Destruct->Variables[i].IsValueNullable,
              Destruct->Variables[i].IsValueBlocked);

          if (Destruct->Variables[i].IsReference) {
            Info.TypeObj = std::make_shared<toka::ReferenceType>(soulType);
            Info.BorrowedFrom = m_LastBorrowSource;
          } else {
            Info.TypeObj = soulType;
          }

          if (!Destruct->Variables[i].IsReference) {
            std::string sName = Info.TypeObj->getSoulName();
            if (!sName.empty() && ShapeMap.count(sName)) {
              if (!ShapeMap[sName]->MangledDestructorName.empty()) {
                DiagnosticEngine::report(getLoc(Destruct), DiagID::ERR_ILLEGAL_RESOURCE_COPY, sName, Destruct->Variables[i].Name);
                HasError = true;
                recordDecision(
                    Destruct, SemanticRuleID::OwnResource001,
                    SemanticOperation::ResourceCopy,
                    SemanticDecision::Reject,
                    SemanticReason::ResourceCopyForbidden,
                    Destruct->Variables[i].Name, sName, ShapeMap[sName]->Loc);
                if (ShapeMap[sName]->Loc.isValid())
                  DiagnosticEngine::report(ShapeMap[sName]->Loc,
                                           DiagID::NOTE_GENERIC,
                                           "resource type declared here");
              }
            }
          }

          Info.IsDeclaredVariable = true;
          Info.IsDeclaredMutable = Destruct->Variables[i].IsValueMutable;
          Info.DeclLoc = Destruct->Loc;
          CurrentScope->define(Destruct->Variables[i].Name, Info);
        }
      } else {
        DiagnosticEngine::report(getLoc(Destruct), DiagID::ERR_NOT_A_STRUCT, "destructuring", SD->Name);
        HasError = true;
      }
    } else {
      for (const auto &Var : Destruct->Variables) {
        SymbolInfo Info;
        Info.TypeObj = toka::Type::fromString("unknown");
        Info.IsDeclaredVariable = true;
        CurrentScope->define(Var.Name, Info);
      }
    }
  } else if (auto *GuardBind = dynamic_cast<GuardBindStmt *>(S)) {
    auto targetTypeObj = checkExpr(GuardBind->Target.get());
    std::string targetType = targetTypeObj->toString();

    // [New] Temporary Lifetime Extension
    // Signal CodeGen that this target expression should have its lifetime extended
    // to the end of the current scope (block) if it is a temporary value.
    if (GuardBind->Target) {
        GuardBind->Target->ExtendLifetime = true;
    }

    std::string targetPath = getPathString(GuardBind->Target.get());
    // Check Pattern and bind variables into CurrentScope
    checkPattern(GuardBind->Pat.get(), targetType, false, targetPath);

    bool isReceiver = false;
    if (!m_ControlFlowStack.empty()) {
      isReceiver = m_ControlFlowStack.back().IsReceiver;
    }
    m_ControlFlowStack.push_back({"", "void", nullptr, false, isReceiver});
    checkStmt(GuardBind->ElseBody.get());
    m_ControlFlowStack.pop_back();

    if (!allPathsJump(GuardBind->ElseBody.get())) {
      DiagnosticEngine::report(getLoc(GuardBind), DiagID::ERR_GUARD_MUST_DIVERGE);
      HasError = true;
    }
  } else if (auto *Unreachable = dynamic_cast<UnreachableStmt *>(S)) {
    // No-op for now, it's just a marker
  }

  // Clear uncommitted transient borrows created during this statement
  PALCheckerState.clearTransient();
}

} // namespace toka
