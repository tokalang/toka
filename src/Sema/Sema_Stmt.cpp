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
#include "toka/Parser.h"
#include "toka/SourceManager.h"
#include "toka/Sema.h"
#include "toka/Type.h"
#include "toka/HandleGrammarAudit.h"
#include <algorithm>
#include <cassert>
#include <functional>
#include <iostream>
#include <set>

namespace toka {

static SourceLocation getLoc(ASTNode *Node) { return Node->Loc; }

static std::string directOutcomeVariantName(const Expr *expr) {
  std::string name;
  if (const auto *init = dynamic_cast<const InitStructExpr *>(expr)) {
    name = init->ShapeName;
  } else if (const auto *call = dynamic_cast<const CallExpr *>(expr)) {
    name = call->Callee;
  } else {
    return {};
  }
  const size_t separator = name.rfind("::");
  if (separator == std::string::npos)
    return {};
  return name.substr(separator + 2);
}

static bool isMayZeroRawCedeSource(const Expr *expr) {
  auto *cede = dynamic_cast<const CedeExpr *>(expr);
  if (!cede || !cede->Value)
    return false;

  const Expr *source = cede->Value.get();
  while (auto *cast = dynamic_cast<const CastExpr *>(source))
    source = cast->Expression.get();
  if (!source || !source->ResolvedType)
    return false;

  auto sourceType = source->ResolvedType;
  return sourceType->isRawPointer() && sourceType->IsNullable;
}

static bool isMayZeroRawCedeDestination(const std::shared_ptr<Type> &type) {
  if (!type)
    return false;
  return type->isRawPointer() && type->IsNullable;
}

// Keep the primary typed-todo diagnostic primary while the surrounding
// declaration is being recovered.  The expression is still rejected; this
// only suppresses derivative morphology noise from a wrapper such as `^todo`.
static bool isTodoWrapper(const Expr *expr) {
  while (expr) {
    if (dynamic_cast<const TodoExpr *>(expr))
      return true;
    if (auto *unary = dynamic_cast<const UnaryExpr *>(expr)) {
      expr = unary->RHS.get();
    } else if (auto *cede = dynamic_cast<const CedeExpr *>(expr)) {
      expr = cede->Value.get();
    } else if (auto *cast = dynamic_cast<const CastExpr *>(expr)) {
      expr = cast->Expression.get();
    } else {
      return false;
    }
  }
  return false;
}

// Conditional todo facts are deliberately narrower than general provenance.
// A direct binding can inherit the requirement set of a direct source binding;
// arbitrary expressions, calls, and control-flow joins require a later dataflow
// slice and must not be guessed here.
static std::set<uint64_t>
directConditionalTodoDependencies(const Expr *expr, Scope *scope) {
  while (expr) {
    if (auto *todo = dynamic_cast<const TodoExpr *>(expr))
      return {todo->TodoId};
    if (auto *cast = dynamic_cast<const CastExpr *>(expr)) {
      expr = cast->Expression.get();
      continue;
    }
    if (auto *pass = dynamic_cast<const PassExpr *>(expr)) {
      expr = pass->Value.get();
      continue;
    }
    auto *variable = dynamic_cast<const VariableExpr *>(expr);
    if (!variable || !scope)
      return {};
    SymbolInfo *info = nullptr;
    std::string actualName;
    if (!scope->findVariableWithDeref(variable->Name, info, actualName) ||
        !info)
      return {};
    return info->ConditionalTodoIds;
  }
  return {};
}

// The conditional-facts protocol has no authority to infer ownership,
// borrowing, or control-flow facts.  It may, however, conservatively carry an
// already-known todo dependency through a pure expression and a resolved call:
// a union means the result can be incomplete on at least one evaluated input.
// Cede is intentionally a hard boundary because a conditional fact cannot
// stand in for a real source invalidation or cleanup obligation.
static void collectConditionalTodoDependenciesFromStmt(
    const Stmt *stmt, Scope *scope, std::set<uint64_t> &dependencies);

static void collectConditionalTodoDependencies(
    const Expr *expr, Scope *scope, std::set<uint64_t> &dependencies) {
  if (!expr)
    return;
  if (auto *todo = dynamic_cast<const TodoExpr *>(expr)) {
    dependencies.insert(todo->TodoId);
    return;
  }
  if (auto *variable = dynamic_cast<const VariableExpr *>(expr)) {
    if (!scope)
      return;
    SymbolInfo *info = nullptr;
    std::string actualName;
    if (scope->findVariableWithDeref(variable->Name, info, actualName) &&
        info) {
      dependencies.insert(info->ConditionalTodoIds.begin(),
                          info->ConditionalTodoIds.end());
    }
    return;
  }
  if (dynamic_cast<const CedeExpr *>(expr))
    return;
  if (auto *binary = dynamic_cast<const BinaryExpr *>(expr)) {
    collectConditionalTodoDependencies(binary->LHS.get(), scope, dependencies);
    collectConditionalTodoDependencies(binary->RHS.get(), scope, dependencies);
  } else if (auto *unary = dynamic_cast<const UnaryExpr *>(expr)) {
    collectConditionalTodoDependencies(unary->RHS.get(), scope, dependencies);
  } else if (auto *postfix = dynamic_cast<const PostfixExpr *>(expr)) {
    collectConditionalTodoDependencies(postfix->LHS.get(), scope, dependencies);
  } else if (auto *cast = dynamic_cast<const CastExpr *>(expr)) {
    collectConditionalTodoDependencies(cast->Expression.get(), scope,
                                       dependencies);
  } else if (auto *pass = dynamic_cast<const PassExpr *>(expr)) {
    collectConditionalTodoDependencies(pass->Value.get(), scope, dependencies);
  } else if (auto *call = dynamic_cast<const CallExpr *>(expr)) {
    for (const auto &argument : call->Args)
      collectConditionalTodoDependencies(argument.get(), scope, dependencies);
  } else if (auto *method = dynamic_cast<const MethodCallExpr *>(expr)) {
    collectConditionalTodoDependencies(method->Object.get(), scope,
                                       dependencies);
    for (const auto &argument : method->Args)
      collectConditionalTodoDependencies(argument.get(), scope, dependencies);
  } else if (auto *member = dynamic_cast<const MemberExpr *>(expr)) {
    collectConditionalTodoDependencies(member->Object.get(), scope,
                                       dependencies);
  } else if (auto *index = dynamic_cast<const ArrayIndexExpr *>(expr)) {
    collectConditionalTodoDependencies(index->Array.get(), scope, dependencies);
    for (const auto &part : index->Indices)
      collectConditionalTodoDependencies(part.get(), scope, dependencies);
  } else if (auto *ifExpr = dynamic_cast<const IfExpr *>(expr)) {
    collectConditionalTodoDependencies(ifExpr->Condition.get(), scope,
                                       dependencies);
    if (ifExpr->IsComptime) {
      if (ifExpr->ComptimeTaken)
        collectConditionalTodoDependenciesFromStmt(ifExpr->Then.get(), scope,
                                                   dependencies);
      else
        collectConditionalTodoDependenciesFromStmt(ifExpr->Else.get(), scope,
                                                   dependencies);
    } else {
      collectConditionalTodoDependenciesFromStmt(ifExpr->Then.get(), scope,
                                                 dependencies);
      collectConditionalTodoDependenciesFromStmt(ifExpr->Else.get(), scope,
                                                 dependencies);
    }
  } else if (auto *match = dynamic_cast<const MatchExpr *>(expr)) {
    collectConditionalTodoDependencies(match->Target.get(), scope,
                                       dependencies);
    for (const auto &arm : match->Arms) {
      collectConditionalTodoDependencies(arm->Guard.get(), scope,
                                         dependencies);
      collectConditionalTodoDependenciesFromStmt(arm->Body.get(), scope,
                                                 dependencies);
    }
  }
}

static void collectConditionalTodoDependenciesFromStmt(
    const Stmt *stmt, Scope *scope, std::set<uint64_t> &dependencies) {
  if (!stmt)
    return;
  if (auto *block = dynamic_cast<const BlockStmt *>(stmt)) {
    for (const auto &item : block->Statements)
      collectConditionalTodoDependenciesFromStmt(item.get(), scope,
                                                 dependencies);
  } else if (auto *exprStmt = dynamic_cast<const ExprStmt *>(stmt)) {
    collectConditionalTodoDependencies(exprStmt->Expression.get(), scope,
                                       dependencies);
  } else if (auto *returnStmt = dynamic_cast<const ReturnStmt *>(stmt)) {
    collectConditionalTodoDependencies(returnStmt->ReturnValue.get(), scope,
                                       dependencies);
  } else if (auto *unsafeStmt = dynamic_cast<const UnsafeStmt *>(stmt)) {
    collectConditionalTodoDependenciesFromStmt(unsafeStmt->Statement.get(),
                                               scope, dependencies);
  }
}

std::set<uint64_t>
Sema::collectConditionalTodoDependencies(const Expr *expr) {
  std::set<uint64_t> dependencies =
      directConditionalTodoDependencies(expr, CurrentScope);
  ::toka::collectConditionalTodoDependencies(expr, CurrentScope,
                                             dependencies);
  return dependencies;
}

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

static bool requiresPayloadWrite(const std::shared_ptr<toka::Type> &Type) {
  if (!Type)
    return false;
  if (Type->isPointer() || Type->isSmartPointer() || Type->isReference()) {
    auto pointee = Type->getPointeeType();
    return pointee && pointee->IsWritable;
  }
  return Type->IsWritable;
}

static AccessCapability deriveDestructureFieldCapability(
    AccessCapability capability, const std::shared_ptr<toka::Type> &fieldType) {
  if (!fieldType ||
      !(fieldType->isPointer() || fieldType->isSmartPointer() ||
        fieldType->isReference()))
    return capability;

  bool fieldPayloadWritable = requiresPayloadWrite(fieldType);
  capability.PayloadWritable = capability.PayloadFlowRestricted
                                   ? capability.PayloadWritable &&
                                         fieldPayloadWritable
                                   : fieldPayloadWritable;
  capability.PayloadFlowRestricted = true;
  return capability;
}

bool Sema::allPathsReturn(Stmt *S) {
  return S && !summarizeFlow(S).CanFallThrough;
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

  if (auto *InitBlock = dynamic_cast<InitBlockStmt *>(S))
    return summarizeFlow(InitBlock->Body.get());

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

  if (E->ResolvedType && E->ResolvedType->isNever()) {
    result.CanFallThrough = false;
    return result;
  }

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

  if (auto *InitBlock = dynamic_cast<InitBlockStmt *>(S)) {
    SymbolInfo *targetInfo = nullptr;
    const bool isWholePlainLocal =
        !InitBlock->IsValueMutable && !InitBlock->IsValueBlocked &&
        CurrentScope->findSymbol(InitBlock->PlaceName, targetInfo) &&
        targetInfo && targetInfo->IsDeclaredVariable &&
        !targetInfo->IsDeclaredMutable;
    const bool hasInitAuthority =
        isWholePlainLocal &&
        hasExactlyPlaceState(targetInfo->placeFact(), PlaceState::Never);
    if (!hasInitAuthority)
      error(InitBlock, DiagID::ERR_INIT_REQUIRES_UNINITIALIZED,
            InitBlock->PlaceName);

    m_InitBlockContexts.push_back(
        {InitBlock->PlaceName, m_ControlFlowStack.size()});
    checkStmt(InitBlock->Body.get());
    m_InitBlockContexts.pop_back();

    if (hasInitAuthority && !allPathsJump(InitBlock->Body.get())) {
      SymbolInfo *postState = nullptr;
      if (!CurrentScope->findSymbol(InitBlock->PlaceName, postState) ||
          !postState ||
          !hasExactlyPlaceState(postState->placeFact(), PlaceState::Live)) {
        error(InitBlock, DiagID::ERR_INIT_BLOCK_UNFULFILLED,
              InitBlock->PlaceName, InitBlock->PlaceName);
      }
    }
  } else if (auto *Block = dynamic_cast<BlockStmt *>(S)) {
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
        if (info.IsDeclaredVariable && !info.HasBeenUsed &&
            !info.HasPayloadBeenUsed) {
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
    if (CurrentFunction) {
      const std::string outcomeVariant =
          directOutcomeVariantName(Ret->ReturnValue.get());
      const FunctionDecl::OutcomeTransition::Case *declaredOutcome =
          CurrentFunction->ResolvedOutcomeTransition
              ? CurrentFunction->ResolvedOutcomeTransition->findVariant(
                    outcomeVariant)
              : nullptr;
      if (CurrentFunction->ResolvedOutcomeTransition && !declaredOutcome) {
        DiagnosticEngine::report(
            getLoc(Ret), DiagID::ERR_OUTCOME_CONTRACT_INVALID,
            CurrentFunction->Name,
            "return must construct one direct declared outcome variant");
        HasError = true;
      }
      for (const auto &Arg : CurrentFunction->Args) {
        if (!Arg.IsInit)
          continue;
        const FunctionDecl::OutcomeTransition::Case *outcome = nullptr;
        if (declaredOutcome) {
          outcome = declaredOutcome;
          if (CurrentFunction->ResolvedOutcomeTransition->Subject != &Arg)
            outcome = nullptr;
        }
        SymbolInfo *Info = nullptr;
        const PlaceState requiredState =
            outcome && outcome->Post == OutcomePostState::Uninit
                ? PlaceState::Never
                : PlaceState::Live;
        if (!CurrentScope->findSymbol(Arg.Name, Info) || !Info ||
            !hasExactlyPlaceState(Info->placeFact(), requiredState)) {
          if (outcome) {
            DiagnosticEngine::report(
                getLoc(Ret), DiagID::ERR_OUTCOME_RETURN_STATE, outcomeVariant,
                Arg.Name,
                requiredState == PlaceState::Live ? "init" : "uninit");
          } else {
            DiagnosticEngine::report(
                getLoc(Ret), DiagID::ERR_INIT_PARAMETER_UNFULFILLED,
                CurrentFunction->Name, Arg.Name);
          }
          HasError = true;
        }
      }
    }
    if (CurrentFunction &&
        CurrentFunction->ReturnContract.ResultKind == ReturnResultKind::Never) {
      DiagnosticEngine::report(getLoc(Ret), DiagID::ERR_NEVER_FUNCTION_RETURN,
                               CurrentFunction->Name);
      HasError = true;
      return;
    }
    std::optional<AnalysisState> returnRollbackState;
    std::optional<Stage0CallSnapshot> returnPlanSnapshot;
    std::optional<ExplicitCedePlan> returnSourcePlan;
    const size_t returnDiagnosticStart = DiagnosticEngine::records().size();
    const bool hadPriorSemanticError = std::any_of(
        DiagnosticEngine::records().begin(),
        DiagnosticEngine::records().end(), [](const auto &record) {
          return record.Level == DiagLevel::Error;
        });
    if (m_EnableStage1ExplicitCallerCede) {
      returnRollbackState = captureAnalysisState();
      returnPlanSnapshot = captureStage0CallSnapshot();
    }
    std::string ExprType = "()";
    std::shared_ptr<toka::Type> ExprTypeObj = toka::Type::fromString("()");
    auto functionOutcome = CurrentFunction
                               ? std::dynamic_pointer_cast<MissOutcomeType>(
                                     CurrentFunction->ResolvedReturnType)
                               : nullptr;
    bool isMissReturn = false;
    bool returnExpressionWasWholeOutcome = false;
    if (functionOutcome && Ret->ReturnValue) {
      if (auto *variable =
              dynamic_cast<VariableExpr *>(Ret->ReturnValue.get())) {
        isMissReturn = variable->Name == "miss";
      }
    }
    if (isMissReturn) {
      Ret->OutcomeKind = ReturnStmt::MissOutcomeKind::Miss;
      ExprTypeObj = functionOutcome;
      ExprType = functionOutcome->toString();
    } else if (Ret->ReturnValue) {
      Ret->ReturnValue = foldGenericConstant(std::move(Ret->ReturnValue));
      std::shared_ptr<Type> returnExpectation = functionOutcome
                                                    ? functionOutcome->PayloadType
                                                    : toka::Type::fromString(
                                                          CurrentFunctionReturnType);
      auto resolvedReturnExpectation = resolveType(returnExpectation);
      returnSourcePlan = recordExplicitCedeStage0NonCallPlan(
          Ret, Ret->ReturnValue.get(), resolvedReturnExpectation,
          TransferDestination::Return, TransferEligibilityContext::Return,
          "return", nullptr,
          returnPlanSnapshot ? &*returnPlanSnapshot : nullptr, {}, {}, 0,
          true);
      bool rejectedAliasReturn = false;
      if (resolvedReturnExpectation &&
          (resolvedReturnExpectation->isUniquePtr() ||
           resolvedReturnExpectation->requiresExplicitOwnershipTransfer(this))) {
        Expr *transferSource = Ret->ReturnValue.get();
        while (auto *cast = dynamic_cast<CastExpr *>(transferSource))
          transferSource = cast->Expression.get();
        while (auto *cede = dynamic_cast<CedeExpr *>(transferSource))
          transferSource = cede->Value.get();
        if (auto *unary = dynamic_cast<UnaryExpr *>(transferSource);
            unary && unary->Op == TokenType::Caret)
          transferSource = unary->RHS.get();
        rejectedAliasReturn = diagnosePlaceAliasOwnershipTransfer(
            Ret->ReturnValue.get(), transferSource);
      }
      bool oldSuppressAliasInvalidation =
          m_SuppressRejectedAliasInvalidation;
      m_SuppressRejectedAliasInvalidation = rejectedAliasReturn;
      m_ControlFlowStack.push_back(
          {"", CurrentFunctionReturnType, nullptr, false, true});
      auto authorityContext =
          beginAuthorityFullExpression(Ret->ReturnValue.get());
      auto RetTypeObj = checkExpr(Ret->ReturnValue.get(), returnExpectation);
      if (!m_StaticReturnStorageFrames.empty() &&
          m_StaticReturnStorageFrames.back().Function == CurrentFunction &&
          m_StaticReturnStorageFrames.back().ClosureDepth == m_CallableReturnClosureDepth)
        prepareStaticReturnStorage(Ret->ReturnValue.get());
      returnExpressionWasWholeOutcome =
          functionOutcome && RetTypeObj && RetTypeObj->isMissOutcome();
      restoreAuthorityFullExpression(std::move(authorityContext));
      m_SuppressRejectedAliasInvalidation = oldSuppressAliasInvalidation;
      ExprTypeObj = RetTypeObj;
      ExprType = RetTypeObj->toString();
      m_ControlFlowStack.pop_back();

      // A hatted unique handle used as a returned value is an intrinsic move.
      // Unlike an ordinary `^param` capture, a cede parameter is authorized to
      // cross this ownership boundary; the direct move also discharges its
      // consumption obligation without a redundant `return cede ^param`.
      if (resolvedReturnExpectation &&
          resolvedReturnExpectation->isUniquePtr()) {
        Expr *moveSource = Ret->ReturnValue.get();
        while (auto *cast = dynamic_cast<CastExpr *>(moveSource))
          moveSource = cast->Expression.get();
        if (auto *unary = dynamic_cast<UnaryExpr *>(moveSource);
            unary && unary->Op == TokenType::Caret) {
          moveSource = unary->RHS.get();
          if (auto *variable = dynamic_cast<VariableExpr *>(moveSource)) {
            SymbolInfo *sourceInfo = nullptr;
            std::string actualName;
            if (CurrentScope->findVariableWithDeref(
                    variable->Name, sourceInfo, actualName) &&
                sourceInfo && sourceInfo->IsUnique()) {
              if (sourceInfo->IsFunctionParameter && !sourceInfo->IsCeded) {
                error(Ret->ReturnValue.get(),
                      DiagID::ERR_SEMA_DIRECT_MOVE_NON_CEDE_PARAMETER,
                      actualName);
              } else {
                CurrentScope->markMoved(actualName, getLoc(Ret));
                PALCheckerState.markMoved(
                    canonicalizeAccessPath(makeAccessPath(actualName)));
              }
            }
          }
        }
      }

      if (functionOutcome) {
        if (RetTypeObj && RetTypeObj->isMissOutcome() &&
            isTypeCompatible(functionOutcome, RetTypeObj)) {
          Ret->OutcomeKind = ReturnStmt::MissOutcomeKind::Forward;
          ExprTypeObj = functionOutcome;
          ExprType = functionOutcome->toString();
        } else if (RetTypeObj && functionOutcome->PayloadType &&
                   isTypeCompatible(functionOutcome->PayloadType,
                                    RetTypeObj)) {
          Ret->OutcomeKind = ReturnStmt::MissOutcomeKind::Hit;
          ExprTypeObj = functionOutcome;
          ExprType = functionOutcome->toString();
        } else {
          DiagnosticEngine::report(
              getLoc(Ret), DiagID::ERR_MISS_OUTCOME_RETURN_MISMATCH,
              RetTypeObj ? RetTypeObj->toString() : "unknown",
              functionOutcome->toString(),
              functionOutcome->PayloadType
                  ? functionOutcome->PayloadType->toString()
                  : "unknown");
          HasError = true;
        }
      }

      // [NEW] Unified Lifetime Check logic
      std::shared_ptr<toka::Type> expectedRetObj = nullptr;
      if (CurrentFunction && CurrentFunction->ResolvedReturnType && CurrentFunctionReturnType == CurrentFunction->ReturnType) {
          expectedRetObj = CurrentFunction->ResolvedReturnType;
      } else {
          expectedRetObj = resolveType(toka::Type::fromString(CurrentFunctionReturnType));
      }

      auto lookupReturnBinding = [&](const VariableExpr *variable, SymbolInfo &info) {
        SymbolInfo *binding = nullptr;
        std::string name;
        if (variable->ResolvedBindingID)
          CurrentScope->findSymbolByID(variable->ResolvedBindingID, binding);
        else
          CurrentScope->findVariableWithDeref(variable->Name, binding, name);
        if (!binding) return false;
        info = *binding;
        return true;
      };
      std::set<std::string> visitedBorrowLikeTypes;
      std::function<bool(std::shared_ptr<toka::Type>)> isBorrowLikeType =
          [&](std::shared_ptr<toka::Type> t) -> bool {
        if (!t)
          return false;
        if (auto outcome =
                std::dynamic_pointer_cast<MissOutcomeType>(t))
          return isBorrowLikeType(outcome->PayloadType);
        if (t->isReference() || t->isFunction() || t->isDynFn())
          return true;
        if (t->isArray())
          return isBorrowLikeType(t->getArrayElementType());
        if (auto *shape = dynamic_cast<ShapeType *>(t.get())) {
          for (const auto &arg : shape->GenericArgs) {
            if (isBorrowLikeType(arg))
              return true;
          }
          if (shape->GenericArgs.empty() && shape->Decl &&
              shape->Decl->InstantiationTemplate) {
            for (const auto &arg : shape->Decl->InstantiationArgs) {
              if (isBorrowLikeType(arg))
                return true;
            }
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
        if (auto *Arr = dynamic_cast<ArrayExpr *>(E)) {
          for (const auto &elem : Arr->Elements) {
            if (returnsBorrowExpr(elem.get())) return true;
          }
        }
        if (auto *Rep = dynamic_cast<RepeatedArrayExpr *>(E)) {
          return returnsBorrowExpr(Rep->Value.get());
        }
        if (auto *Cast = dynamic_cast<CastExpr *>(E))
          return returnsBorrowExpr(Cast->Expression.get());
        return false;
      };

      std::function<bool(Expr *)> carriesLifeDependencyExpr = [&](Expr *E) -> bool {
        if (!E)
          return false;
        if (auto *cede = dynamic_cast<CedeExpr *>(E))
          return carriesLifeDependencyExpr(cede->Value.get());
        if (auto *unsafe = dynamic_cast<UnsafeExpr *>(E))
          return carriesLifeDependencyExpr(unsafe->Expression.get());
        if (auto *selector = dynamic_cast<UnaryExpr *>(E);
            selector && selector->ResolvedType &&
            ((selector->Op == TokenType::Caret && selector->ResolvedType->isUniquePtr()) ||
             (selector->Op == TokenType::Tilde && selector->ResolvedType->isSharedPtr())))
          return carriesLifeDependencyExpr(selector->RHS.get());
        if (auto *Cast = dynamic_cast<CastExpr *>(E))
          return carriesLifeDependencyExpr(Cast->Expression.get());
        if (auto *Var = dynamic_cast<VariableExpr *>(E)) {
          SymbolInfo info;
          if (lookupReturnBinding(Var, info)) {
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
        } else if (auto *Arr = dynamic_cast<ArrayExpr *>(E)) {
          for (const auto &elem : Arr->Elements) {
            if (carriesLifeDependencyExpr(elem.get()))
              return true;
          }
        } else if (auto *Rep = dynamic_cast<RepeatedArrayExpr *>(E)) {
          return carriesLifeDependencyExpr(Rep->Value.get());
        } else if (auto *Bin = dynamic_cast<BinaryExpr *>(E)) {
          if (Bin->Op == "=")
            return carriesLifeDependencyExpr(Bin->RHS.get());
        }
        return false;
      };

      bool isTrackedRet =
          isBorrowLikeType(expectedRetObj) || isBorrowLikeType(ExprTypeObj) ||
          returnsBorrowExpr(Ret->ReturnValue.get()) ||
          (carriesLifeDependencyExpr(Ret->ReturnValue.get()) &&
           (!expectedRetObj || expectedRetObj->isUnknown() ||
            expectedRetObj->isUniquePtr() || expectedRetObj->isSharedPtr() ||
            isBorrowLikeType(expectedRetObj) || isBorrowLikeType(ExprTypeObj)));

      // A named record's already prepared structural dependencies must enter
      // the existing lifetime checker even when legacy type inspection did
      // not recognize its borrowed fields. This adds no source proof and
      // does not classify any new type as borrowed.
      if (m_EnableStage1ExplicitCallerCede && returnSourcePlan &&
          returnSourcePlan->Prepared.SourceCategory == TransferSourceCategory::NamedSourcePlace &&
          ((returnSourcePlan->Prepared.SourceView == TransferSourceView::DirectValue &&
            returnSourcePlan->Prepared.Ownership == TransferOwnershipKind::PlainValue) ||
           (returnSourcePlan->Prepared.SourceView == TransferSourceView::UniqueHandle &&
            returnSourcePlan->Prepared.Ownership == TransferOwnershipKind::UniqueOwner) ||
           (returnSourcePlan->Prepared.SourceView == TransferSourceView::SharedHandle &&
            returnSourcePlan->Prepared.Ownership == TransferOwnershipKind::SharedOwner)) &&
          returnSourcePlan->Prepared.Dependency == TransferDependencyKind::Structural &&
          returnSourcePlan->Prepared.DependencyFactsComplete &&
          !returnSourcePlan->Prepared.DependencyRoots.empty())
        isTrackedRet = true;

      if (isTrackedRet) {
          std::set<std::string> returnedDeps;
          std::set<std::string> addressedStoragePaths;
          std::vector<AccessPath> origins, storageOrigins;
          bool usedCurrentReference = false;
          const bool currentOriginsComplete = collectActualReturnReferents(
              Ret->ReturnValue.get(), origins, nullptr, &storageOrigins,
              &usedCurrentReference);
          if (currentOriginsComplete) {
            for (const auto &origin : storageOrigins)
              addressedStoragePaths.insert(origin.toLegacyString());
          }

          std::set<std::string> resolvingDependencyPaths;
          std::function<void(std::set<std::string> &, const std::string &)>
              recordDependencyPathTo;
          recordDependencyPathTo = [&](std::set<std::string> &out,
                                       const std::string &dep) {
            if (dep.empty())
              return;
            if (addressedStoragePaths.count(dep)) {
              out.insert(dep);
              return;
            }
            if (!resolvingDependencyPaths.insert(dep).second) {
              out.insert(dep);
              return;
            }
            std::string baseName = dep;
            size_t dotPos = baseName.find('.');
            if (dotPos != std::string::npos)
              baseName = baseName.substr(0, dotPos);

            SymbolInfo *depInfo = nullptr;
            std::string actualName;
            if (CurrentScope->findVariableWithDeref(baseName, depInfo,
                                                    actualName) &&
                depInfo) {
              VariableExpr source(actualName);
              source.Loc = depInfo->DeclLoc;
              std::vector<AccessPath> dynamicOrigins;
              std::vector<SourceLocation> staticOrigins;
              if (collectActualReturnReferents(&source, dynamicOrigins,
                                                &staticOrigins) &&
                  dynamicOrigins.empty() && !staticOrigins.empty()) {
                resolvingDependencyPaths.erase(dep);
                return;
              }
              bool contributedDeps = false;
              if (!depInfo->BorrowedFrom.empty() &&
                  depInfo->BorrowedFrom != dep) {
                recordDependencyPathTo(out, depInfo->BorrowedFrom);
                contributedDeps = true;
              }
              for (const auto &source : depInfo->LifeDependencySet) {
                if (source == dep || source == depInfo->BorrowedFrom)
                  continue;
                recordDependencyPathTo(out, source);
                contributedDeps = true;
              }
              if (!contributedDeps)
                out.insert(dep);
            } else {
              out.insert(dep);
            }
            resolvingDependencyPaths.erase(dep);
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

          auto recordAddressDependency = [&](Expr *address, Expr *target,
                                             std::set<std::string> &out) {
            // Preserve the existing collector's supported path boundary;
            // indexed/raw extraction routes retain their existing checks.
            if (getPath(target).empty()) return;
            std::vector<AccessPath> referents, storage;
            if (collectActualReturnReferents(address, referents, nullptr, &storage)) {
              for (const auto &path : storage)
                addressedStoragePaths.insert(path.toLegacyString());
              for (const auto &path : referents)
                recordDependencyPathTo(out, path.toLegacyString());
            } else {
              // Unknown address provenance cannot inherit a value's static
              // exemption. Retain the storage dependency for normal checks.
              auto path = canonicalizeAccessPath(makeAccessPath(target));
              if (path) out.insert(path.toLegacyString());
            }
          };

          // Helper to collect dependencies from the returned expression
          bool collectingCedeSource = false;
          std::function<void(Expr *, std::set<std::string> &)> collectDepsInto =
              [&](Expr *E, std::set<std::string> &out) {
            if (!E)
              return;
            if (auto *cede = dynamic_cast<CedeExpr *>(E);
                m_EnableStage1ExplicitCallerCede && cede) {
              const bool previous = collectingCedeSource;
              collectingCedeSource = true;
              collectDepsInto(cede->Value.get(), out);
              collectingCedeSource = previous;
              return;
            }
            if (auto *unsafe = dynamic_cast<UnsafeExpr *>(E);
                m_EnableStage1ExplicitCallerCede && unsafe) {
              collectDepsInto(unsafe->Expression.get(), out);
              return;
            }

            // Case 1: Taking address `&var` or `&var.field` via UnaryExpr
            if (auto *Addr = dynamic_cast<UnaryExpr *>(E)) {
              if (Addr->Op == TokenType::Ampersand) {
                recordAddressDependency(Addr, Addr->RHS.get(), out);
              } else if (Addr->ResolvedType &&
                         ((Addr->Op == TokenType::Caret && Addr->ResolvedType->isUniquePtr()) ||
                          (Addr->Op == TokenType::Tilde && Addr->ResolvedType->isSharedPtr()))) {
                // Handle selection preserves actual payload dependencies;
                // the integer '~' operation is deliberately excluded.
                collectDepsInto(Addr->RHS.get(), out);
              }
            }
            // Case 1b: AddressOfExpr Borrow (implicit/explicit borrow alignment)
            else if (auto *AddrOf = dynamic_cast<AddressOfExpr *>(E)) {
                recordAddressDependency(AddrOf, AddrOf->Expression.get(), out);
            }
            // Case 2: Returning existing reference variable `x`
            else if (auto *Var = dynamic_cast<VariableExpr *>(E)) {
              SymbolInfo info;
              if (lookupReturnBinding(Var, info)) {
                if (!info.BorrowedFrom.empty()) {
                  recordDependencyPathTo(out, info.BorrowedFrom);
                }
                for (const auto &dependency : info.LifeDependencySet)
                  recordDependencyPathTo(out, dependency);
                if (info.IsReference() || isBorrowLikeType(info.TypeObj)) {
                  bool contributedDeps = !info.LifeDependencySet.empty();
                  // It depends on whatever 'info' borrowed from
                  if (!info.BorrowedFrom.empty()) {
                    recordDependencyPathTo(out, info.BorrowedFrom);
                    contributedDeps = true;
                  }
                  // Also merge its transitive dependencies if we track them
                  size_t depCountBefore = out.size();
                  for (const auto &dependency : info.LifeDependencySet)
                    recordDependencyPathTo(out, dependency);
                  if (out.size() != depCountBefore)
                    contributedDeps = true;
                  auto ownership = collectingCedeSource
                      ? queryExplicitCedeStage0OwnershipReadOnly(info.TypeObj)
                      : std::optional<ValueOwnership>{};
                  const bool transfersOwner = ownership &&
                      (*ownership == ValueOwnership::Owned ||
                       *ownership == ValueOwnership::SharedHandle);
                  const bool transfersCallable = collectingCedeSource &&
                      info.TypeObj && (info.TypeObj->isFunction() ||
                                       info.TypeObj->isDynFn());
                  // Keep every actual dependency above. Moving an owning
                  // container/callable does not additionally borrow its old
                  // binding; callable capture dependencies remain explicit.
                  if (!contributedDeps && CurrentFunction && !transfersOwner &&
                      !transfersCallable) {
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
                std::vector<AccessPath> actualOrigins;
                std::vector<SourceLocation> staticOrigins;
                if (collectActualReturnReferents(Call, actualOrigins, &staticOrigins)) {
                  for (const auto &origin : actualOrigins)
                    recordDependencyPathTo(out, origin.toLegacyString());
                  return;
                }
                for (auto &Arg : Call->Args) {
                    collectDepsInto(Arg.get(), out);
                }
            }
            else if (auto *Method = dynamic_cast<MethodCallExpr *>(E)) {
                std::vector<AccessPath> actualOrigins;
                std::vector<SourceLocation> staticOrigins;
                if (collectActualReturnReferents(Method, actualOrigins, &staticOrigins)) {
                  for (const auto &origin : actualOrigins)
                    recordDependencyPathTo(out, origin.toLegacyString());
                  return;
                }
                if (Method->ResolvedFn) {
                  for (const auto &dep : Method->ResolvedFn->LifeDependencies) {
                    if (Type::stripMorphology(dep) == "self") {
                      std::string path = getPath(Method->Object.get());
                      if (!path.empty())
                        recordDependencyPathTo(out, path);
                      continue;
                    }
                    for (size_t i = 1;
                         i < Method->ResolvedFn->Args.size(); ++i) {
                      if (Method->ResolvedFn->Args[i].Name != dep ||
                          i - 1 >= Method->Args.size())
                        continue;
                      std::string path = getPath(Method->Args[i - 1].get());
                      if (!path.empty())
                        recordDependencyPathTo(out, path);
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
            } else if (auto *Arr = dynamic_cast<ArrayExpr *>(E)) {
                for (auto &Elem : Arr->Elements) {
                    collectDepsInto(Elem.get(), out);
                }
            } else if (auto *Rep = dynamic_cast<RepeatedArrayExpr *>(E)) {
                collectDepsInto(Rep->Value.get(), out);
            }
            // Case 6: Fallback for BinaryExpr named arg init if kept as CallExpr
            else if (auto *Bin = dynamic_cast<BinaryExpr *>(E)) {
                if (Bin->Op == "=") {
                    collectDepsInto(Bin->RHS.get(), out);
                }
            }
            else if (auto *wait = dynamic_cast<WaitExpr *>(E)) {
                std::vector<AccessPath> actualOrigins;
                if (collectActualReturnReferents(wait, actualOrigins)) {
                    for (const auto &origin : actualOrigins)
                        recordDependencyPathTo(out, origin.toLegacyString());
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
                if (Memb->ResolvedType && !Memb->ResolvedType->isRawPointer() &&
                    queryExplicitCedeStage0OwnershipReadOnly(Memb->ResolvedType) ==
                        ValueOwnership::BorrowedView) {
                  std::vector<AccessPath> actualOrigins;
                  if (collectActualReturnReferents(Memb, actualOrigins)) {
                    for (const auto &origin : actualOrigins)
                      recordDependencyPathTo(out, origin.toLegacyString());
                    return;
                  }
                }
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
            if (auto *cede = dynamic_cast<CedeExpr *>(E);
                m_EnableStage1ExplicitCallerCede && cede) {
              collectMemberDeps(cede->Value.get());
            } else if (auto *unsafe = dynamic_cast<UnsafeExpr *>(E);
                       m_EnableStage1ExplicitCallerCede && unsafe) {
              collectMemberDeps(unsafe->Expression.get());
            } else if (auto *Cast = dynamic_cast<CastExpr *>(E)) {
              collectMemberDeps(Cast->Expression.get());
            } else if (auto *selector = dynamic_cast<UnaryExpr *>(E);
                       selector && selector->ResolvedType &&
                       ((selector->Op == TokenType::Caret && selector->ResolvedType->isUniquePtr()) ||
                        (selector->Op == TokenType::Tilde && selector->ResolvedType->isSharedPtr()))) {
              collectMemberDeps(selector->RHS.get());
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
              if (lookupReturnBinding(Var, info)) {
                for (const auto &pair : info.FieldDependencySet) {
                  returnedMemberDeps[pair.first].insert(pair.second.begin(),
                                                        pair.second.end());
                }
              }
            }
          };

          collectDeps(Ret->ReturnValue.get());
          collectMemberDeps(Ret->ReturnValue.get());
          // A checked static enum payload has per-expression provenance. Do
          // not mutate the shared dependency scratch state: only this exact
          // return edge may replace the conservative whole-enum mapping.
          // All independently collected dependencies above remain in force.
          std::vector<SourceLocation> selectedPayloadStorage;
          const bool exactStaticPayload = currentOriginsComplete && origins.empty() &&
              storageOrigins.empty() && collectStaticEnumPayload(
                  Ret->ReturnValue.get(), selectedPayloadStorage);
          if (!exactStaticPayload) {
            for (const auto &dep : m_LastLifeDependencies)
              recordDependencyPath(dep);
            if (!m_LastBorrowSource.empty())
              recordDependencyPath(m_LastBorrowSource);
          }

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

            auto isParamDependency = [&](const std::string &dep) -> bool {
              std::string baseDep = dep.substr(0, dep.find('.'));
              const FunctionDecl::Arg *matchingArg = nullptr;
              for (const auto &Arg : CurrentFunction->Args) {
                if (Arg.Name == baseDep) {
                  matchingArg = &Arg;
                  break;
                }
              }
              if (!matchingArg)
                return false;

              // If origin tracking is complete, verify that an origin exists for baseDep
              // and that all origins matching baseDep are confirmed as the parameter.
              // Check if any actual return referent root matches baseDep.
              // An already identified original referent must take priority;
              // an inner same-named lexical binding cannot override it.
              if (currentOriginsComplete) {
                bool foundOrigin = false;
                for (const auto &origin : origins) {
                  if (origin.RootName == baseDep) {
                    foundOrigin = true;
                    SymbolInfo *originInfo = nullptr;
                    if (origin.RootID && CurrentScope)
                      CurrentScope->findSymbolByID(origin.RootID, originInfo);
                    if (originInfo) {
                      if (!originInfo->IsFunctionParameter ||
                          originInfo->DeclLoc != matchingArg->Loc)
                        return false;
                    } else if (origin.RootLoc.isValid()) {
                      if (origin.RootLoc != matchingArg->Loc)
                        return false;
                    } else {
                      // Missing origin identity: unproven
                      return false;
                    }
                  }
                }
                if (foundOrigin) {
                  return true;
                }
              }

              // Fallback: check lexical scope for baseDep when no typed origin
              // identified it directly (e.g. transitive life dependencies).
              // Missing identity must remain unproven, not be accepted merely
              // because a matching parameter name exists.
              if (CurrentScope) {
                SymbolInfo *depInfo = nullptr;
                std::string actualName;
                if (CurrentScope->findVariableWithDeref(baseDep, depInfo, actualName) && depInfo) {
                  if (depInfo->IsFunctionParameter &&
                      depInfo->DeclLoc == matchingArg->Loc)
                    return true;
                }
              }

              return false;
            };

            bool hasLocalDependency = false;
            for (const auto &dep : returnedDeps) {
              if (!isParamDependency(dep)) {
                hasLocalDependency = true;
                break;
              }
            }

            for (const auto &dep : returnedDeps) {
              // 1. Is it a parameter that can outlive the function?
              bool isParam = isParamDependency(dep);

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

    bool diagnosedNullRet = false;
    if (expectedRetObj && expectedRetObj->isRawPointer() &&
        !expectedRetObj->IsNullable && ExprTypeObj &&
        ExprTypeObj->isNullType()) {
      error(Ret, DiagID::ERR_NONZERO_RAW_NULL_FLOW,
            expectedRetObj->toString());
      diagnosedNullRet = true;
    }

    std::shared_ptr<Type> expectedReturnValueObj = expectedRetObj;
    if (auto outcome =
            std::dynamic_pointer_cast<MissOutcomeType>(expectedRetObj)) {
      if (Ret->OutcomeKind == ReturnStmt::MissOutcomeKind::Hit)
        expectedReturnValueObj = outcome->PayloadType;
    }

    // A return signature is a declaration boundary.  A Shared direct source
    // may not promise more payload authority than it currently carries.
    PermissionFlow returnFlow = isMissReturn
                                    ? PermissionFlow{}
                                    : getPermissionFlow(
                                          Ret->ReturnValue.get());
    if (!m_InUnsafeContext && returnFlow.Kind == PermissionFlowKind::Shared &&
        requiresPayloadWrite(expectedReturnValueObj) &&
        !returnFlow.DirectCapability.PayloadWritable) {
      error(Ret->ReturnValue.get(),
            DiagID::ERR_SEMA_COVENANT_VIOLATION_CANNOT_ELEVATE_WRITE_P);
      HasError = true;
    }

    if (!m_InUnsafeContext) {
      auto targetRecord = std::dynamic_pointer_cast<ShapeType>(expectedRetObj);
      auto *recordValue =
          dynamic_cast<AnonymousRecordExpr *>(Ret->ReturnValue.get());
      if (targetRecord && targetRecord->Decl && recordValue &&
          targetRecord->Name.rfind("__Toka_Anon_Rec_", 0) == 0) {
        for (const auto &fieldValue : recordValue->Fields) {
          const auto target = std::find_if(
              targetRecord->Decl->Members.begin(),
              targetRecord->Decl->Members.end(), [&](const ShapeMember &field) {
                return Type::stripMorphology(field.Name) ==
                       Type::stripMorphology(fieldValue.first);
              });
          if (target == targetRecord->Decl->Members.end())
            continue;
          auto targetType = resolveType(getPhysicalType(*target), false);
          PermissionFlow fieldFlow = getPermissionFlow(fieldValue.second.get());
          if (requiresPayloadWrite(targetType) &&
              fieldFlow.Kind == PermissionFlowKind::Shared &&
              !fieldFlow.DirectCapability.PayloadWritable) {
            error(fieldValue.second.get(),
                  DiagID::ERR_SEMA_COVENANT_VIOLATION_CANNOT_ELEVATE_WRITE_P);
            HasError = true;
          }
        }
      }
    }

    if (!diagnosedNullRet && !HasError &&
        !isTypeCompatible(expectedRetObj, ExprTypeObj)) {
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
    } else if (!HasError && !isMissReturn) {

      // A hit return into `T | miss` is checked against the payload's
      // morphology, not the outcome wrapper (whose root has no hat).
      MorphKind targetMorph = morphKindFromType(expectedReturnValueObj);
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
    auto hasNewReturnError = [&]() {
      const auto &records = DiagnosticEngine::records();
      return std::any_of(
          records.begin() + std::min(returnDiagnosticStart, records.size()),
          records.end(), [](const auto &record) {
            return record.Level == DiagLevel::Error;
          });
    };
    const bool returnsWholeOutcome =
        returnExpressionWasWholeOutcome;
    const bool completesBarePreflight =
        returnSourcePlan &&
        returnSourcePlan->Rejection ==
            TransferPlanRejection::IncompleteFacts &&
        returnSourcePlan->Prepared.SurfaceSpelling ==
            TransferSurfaceSpelling::Bare &&
        (!returnSourcePlan->Prepared.SourcePlace ||
         returnSourcePlan->Prepared.SourceCategory ==
             TransferSourceCategory::NoSourcePlace);
    const bool completesRawConstruction = returnSourcePlan &&
        returnSourcePlan->Rejection == TransferPlanRejection::AccessCapabilityMismatch &&
        hasQualifiedUnsafeRawConstruction(Ret->ReturnValue.get());
    const auto checkedTaskResult = taskResultFact(Ret->ReturnValue.get());
    const bool completesTaskResult = checkedTaskResult && !checkedTaskResult->TaskCarrier;
    if (returnSourcePlan && returnPlanSnapshot && !hasNewReturnError() &&
        (completesBarePreflight || returnsWholeOutcome || completesRawConstruction || completesTaskResult)) {
      returnSourcePlan = recordExplicitCedeStage0NonCallPlan(
          Ret, Ret->ReturnValue.get(),
          returnsWholeOutcome ? expectedRetObj : expectedReturnValueObj,
          TransferDestination::Return, TransferEligibilityContext::Return,
          "return", nullptr, &*returnPlanSnapshot, {}, {}, 0, false, true);
    }
    const bool isUninstantiatedGenericReturn =
        CurrentFunction && !CurrentFunction->GenericParams.empty() &&
        !CurrentFunction->TemplateOrigin;
    const bool isCompilerPlaceOutcomeReturn =
        expectedRetObj && containsInternalPlaceOutcome(expectedRetObj);
    const bool enforceReturnSourcePlan =
        m_EnableStage1ExplicitCallerCede && returnSourcePlan &&
        !hadPriorSemanticError &&
        !isUninstantiatedGenericReturn &&
        !isCompilerPlaceOutcomeReturn;
    if (enforceReturnSourcePlan && !returnSourcePlan->admitted() &&
        !hasNewReturnError()) {
      if (returnSourcePlan->Rejection == TransferPlanRejection::AccessCapabilityMismatch) {
        const auto &facts = returnSourcePlan->Prepared;
        DiagnosticEngine::report(Ret->Loc, DiagID::NOTE_GENERIC,
            "return capability facts: actual=" + facts.ActualTypeKey +
            " P=" + std::to_string(facts.ActualCapabilities.PayloadWritable) +
            "; destination=" + facts.FormalTypeKey +
            " P=" + std::to_string(facts.DestinationCapabilities.PayloadWritable) +
            "; source flow P=" + std::to_string(facts.SourceFlowCeiling.PayloadWritable) +
            "; raw authority=" + facts.RawWriteAuthority);
      }
      Expr *source = Ret->ReturnValue.get();
      while (source) {
        if (auto *cede = dynamic_cast<CedeExpr *>(source))
          source = cede->Value.get();
        else if (auto *cast = dynamic_cast<CastExpr *>(source))
          source = cast->Expression.get();
        else if (auto *unsafeExpr = dynamic_cast<UnsafeExpr *>(source))
          source = unsafeExpr->Expression.get();
        else
          break;
      }
      std::string sourceName = source ? getPathString(source) : "";
      if (sourceName.empty() && source)
        sourceName = source->toString();
      switch (returnSourcePlan->Rejection) {
      case TransferPlanRejection::MissingCedeForNamedSource:
        error(Ret->ReturnValue.get(),
              DiagID::ERR_SEMA_RETURN_NAMED_SOURCE_REQUIRES_CEDE,
              sourceName, sourceName);
        break;
      case TransferPlanRejection::ExplicitCedeRequiresSource:
        error(Ret->ReturnValue.get(),
              DiagID::ERR_SEMA_RETURN_CEDE_REQUIRES_SOURCE);
        break;
      case TransferPlanRejection::RedundantIntrinsicUniqueCede:
        error(Ret->ReturnValue.get(),
              DiagID::ERR_SEMA_RETURN_UNIQUE_CEDE_REDUNDANT, sourceName,
              sourceName);
        break;
      case TransferPlanRejection::SourceTransferUnauthorized:
        error(Ret->ReturnValue.get(),
              DiagID::ERR_SEMA_RETURN_SOURCE_TRANSFER_UNAUTHORIZED,
              sourceName);
        break;
      default:
        error(Ret->ReturnValue.get(),
              DiagID::ERR_SEMA_RETURN_PLAN_INCOMPLETE,
              toString(returnSourcePlan->Rejection));
        break;
      }
    }
    recordEnumReturn(Ret, !hasNewReturnError());
    recordTaskResultReturn(Ret, enforceReturnSourcePlan && !hasNewReturnError() && returnSourcePlan->admitted());
    if (!m_IndependentReturnFrames.empty() &&
        m_IndependentReturnFrames.back().Function == CurrentFunction &&
        m_IndependentReturnFrames.back().ClosureDepth == m_CallableReturnClosureDepth) {
      auto proof = resultIndependence(Ret->ReturnValue.get());
      auto &frame = m_IndependentReturnFrames.back();
      frame.SawReturn = true;
      frame.Complete &= enforceReturnSourcePlan && !hasNewReturnError() &&
                        returnSourcePlan->admitted() && proof != nullptr;
      if (proof) frame.RequiredArguments.insert(proof->RequiredArguments.begin(), proof->RequiredArguments.end());
      if (proof && proof->Bytes) frame.Bytes = proof->Bytes;
    }
    if (!m_StaticReturnStorageFrames.empty() &&
        m_StaticReturnStorageFrames.back().Function == CurrentFunction &&
        m_StaticReturnStorageFrames.back().ClosureDepth == m_CallableReturnClosureDepth) {
      auto &frame = m_StaticReturnStorageFrames.back();
      frame.SawReturn = true;
      const bool staticPlan = !hasNewReturnError() && enforceReturnSourcePlan &&
          returnSourcePlan->admitted() &&
          returnSourcePlan->ValueProduction == TransferValueProduction::CopyIdentity &&
          returnSourcePlan->Drop == TransferDropDisposition::NoLiability &&
          !returnSourcePlan->Prepared.CarriesDropLiability &&
          returnSourcePlan->Prepared.DropLiabilityComplete &&
          returnSourcePlan->Prepared.DependencyFactsComplete &&
          returnSourcePlan->Prepared.Dependency == TransferDependencyKind::None &&
          returnSourcePlan->Prepared.DependencyRoots.empty() &&
          !returnSourcePlan->Prepared.ReferentPlace &&
          returnSourcePlan->Prepared.StructuredReferentPlaces.empty() &&
          std::all_of(returnSourcePlan->Prepared.ResultFieldReferents.begin(),
                      returnSourcePlan->Prepared.ResultFieldReferents.end(),
                      [](const auto &field) { return field.second.empty(); }) &&
          !returnSourcePlan->Prepared.StaticStorageOrigins.empty();
      std::vector<AccessPath> paths, storage;
      std::vector<SourceLocation> origins;
      const bool complete = staticPlan && collectActualReturnReferents(
          Ret->ReturnValue.get(), paths, &origins, &storage) &&
          paths.empty() && storage.empty() && !origins.empty();
      frame.Complete &= complete;
      if (complete) frame.Origins.insert(frame.Origins.end(), origins.begin(), origins.end());
    }
    if (!m_CallableReturnFrames.empty() &&
        m_CallableReturnFrames.back().Function == CurrentFunction &&
        m_CallableReturnFrames.back().ClosureDepth == m_CallableReturnClosureDepth) {
      if (!hasNewReturnError() && !prepareCallableReturnEnvironment(Ret->ReturnValue.get()))
        error(Ret->ReturnValue.get(), DiagID::ERR_SEMA_RETURN_PLAN_INCOMPLETE,
              "CallableReturnEnvironmentUnavailable");
      auto &frame = m_CallableReturnFrames.back();
      frame.SawReturn = true;
      auto environment = collectStage1CallableEnvironment(Ret->ReturnValue.get());
      if (!environment.NativeOwners.empty()) {
        // Native instance witnesses need caller-edge rebasing; do not lose
        // them and accidentally advertise an independent erased result.
        m_CallableReturnFrames.back().Facts.NativeOwners.insert(
            m_CallableReturnFrames.back().Facts.NativeOwners.end(),
            environment.NativeOwners.begin(), environment.NativeOwners.end());
      }
      frame.Facts.Complete &= environment.Complete && !hasNewReturnError() &&
          (!enforceReturnSourcePlan || returnSourcePlan->admitted());
      auto appendParameterOrigins = [&](const auto &origins, auto &destination) {
        for (const auto &origin : origins) {
          SymbolInfo *binding = nullptr;
          if (!origin.RootID || !CurrentScope->findSymbolByID(origin.RootID, binding) ||
              !binding || !binding->IsFunctionParameter ||
              std::none_of(CurrentFunction->Args.begin(), CurrentFunction->Args.end(),
                  [&](const auto &arg) { return Type::stripMorphology(arg.Name) ==
                                               Type::stripMorphology(origin.RootName); })) {
            frame.Facts.Complete = false;
            continue;
          }
          if (std::find(destination.begin(), destination.end(), origin) == destination.end())
            destination.push_back(origin);
        }
      };
      appendParameterOrigins(environment.Referents, frame.Facts.Referents);
      appendParameterOrigins(environment.LocalBounds, frame.Facts.LocalBounds);
    }
    if (returnRollbackState && enforceReturnSourcePlan &&
        (!returnSourcePlan->admitted() || hasNewReturnError()))
      mergeAnalysisStates({*returnRollbackState}, returnRollbackState->PAL);
    if (!hasNewReturnError()) {
      auto *borrow = dynamic_cast<UnaryExpr *>(Ret->ReturnValue.get());
      auto *value = borrow && borrow->Op == TokenType::Ampersand
          ? dynamic_cast<VariableExpr *>(borrow->RHS.get()) : nullptr;
      SymbolInfo *parameter = nullptr;
      if (value && value->IsAbstractWholeValue && value->ResolvedBindingID &&
          CurrentScope->findSymbolByID(value->ResolvedBindingID, parameter) &&
          parameter && parameter->IsFunctionParameter && CurrentFunction) {
        for (size_t index = 0; index < CurrentFunction->Args.size(); ++index) {
          const auto &formal = CurrentFunction->Args[index];
          if (formal.IsAbstractWholeValue && formal.Loc == parameter->DeclLoc &&
              Type::stripMorphology(formal.Name) == Type::stripMorphology(value->Name))
            m_WholeParameterStorageReturns[CurrentFunction].insert(index);
        }
      }
      recordRawAddressReturn(Ret);
      recordNativeSyncOwnerReturn(Ret);
    }
    m_LastBorrowSource.clear();
    m_LastLifeDependencies.clear();
    m_LastFieldDependencies.clear();
  } else if (auto *Free = dynamic_cast<FreeStmt *>(S)) {
    auto rawSlotsBeforeRelease = m_RawSlotDependencies;
    m_RawSlotDependencies.clear();
    Free->RawStorageRelease.reset();
    const size_t releaseDiagnostics = DiagnosticEngine::records().size();
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
    const auto &releaseRecords = DiagnosticEngine::records();
    if (FreeTypeObj->isRawPointer() && std::none_of(
            releaseRecords.begin() + releaseDiagnostics, releaseRecords.end(),
            [](const auto &record) { return record.Level == DiagLevel::Error; })) {
      Expr *source = Free->Expression.get();
      while (auto *selector = dynamic_cast<UnaryExpr *>(source)) {
        if (selector->Op != TokenType::Star) break;
        source = selector->RHS.get();
      }
      auto observation = std::make_shared<RawStorageReleaseObservation>();
      observation->SourceEdge = makeExplicitCedeStage0NonCallGroupIdentity(Free, "raw-storage-release");
      observation->StorageBinding = canonicalizeAccessPath(makeAccessPath(source));
      observation->StorageType = FreeTypeObj;
      observation->DeclaredCount = Free->Count.get();
      if (!observation->SourceEdge.empty() && observation->StorageBinding.RootID)
        Free->RawStorageRelease = std::move(observation);
    } else {
      m_RawSlotDependencies = std::move(rawSlotsBeforeRelease);
    }
  } else if (auto *Unsafe = dynamic_cast<UnsafeStmt *>(S)) {
    bool oldUnsafe = m_InUnsafeContext;
    m_InUnsafeContext = true;
    checkStmt(Unsafe->Statement.get());
    m_InUnsafeContext = oldUnsafe;
  } else if (auto *ExprS = dynamic_cast<ExprStmt *>(S)) {
    // Standalone expressions are NOT receivers
    m_ControlFlowStack.push_back({"", NoProducedValue, nullptr, false, false});
    ExprS->Expression = foldGenericConstant(std::move(ExprS->Expression));
    Expr *statementRoot = ExprS->Expression.get();
    while (statementRoot) {
      if (auto *unsafe = dynamic_cast<UnsafeExpr *>(statementRoot))
        statementRoot = unsafe->Expression.get();
      else if (auto *cast = dynamic_cast<CastExpr *>(statementRoot);
               cast && cast->Kind == CastKind::Ascription)
        statementRoot = cast->Expression.get();
      else
        break;
    }
    auto *standaloneCede = dynamic_cast<CedeExpr *>(statementRoot);
    const bool consumingInvocation = standaloneCede &&
        isConsumingCallableInvocation(dynamic_cast<CallExpr *>(standaloneCede->Value.get()));
    const bool uninstantiatedGeneric = CurrentFunction &&
        !CurrentFunction->GenericParams.empty() && !CurrentFunction->TemplateOrigin;
    const bool activateStandalone = standaloneCede && !consumingInvocation &&
        m_EnableStage1ExplicitCallerCede && !uninstantiatedGeneric &&
        !m_IsPrecomputingCaptures;
    std::optional<AnalysisState> standaloneBefore;
    std::optional<ExplicitCedePlan> standalonePlan;
    const size_t standaloneDiagnosticStart = DiagnosticEngine::records().size();
    if (activateStandalone) standaloneBefore = captureAnalysisState();
    if (standaloneCede && (!consumingInvocation || !m_EnableStage1ExplicitCallerCede)) {
      standalonePlan = recordExplicitCedeStage0NonCallPlan(
          ExprS, ExprS->Expression.get(), nullptr,
          TransferDestination::StatementEndDiscard,
          TransferEligibilityContext::Standalone, "standalone");
      if (activateStandalone && !standalonePlan->admitted()) {
        error(standaloneCede, DiagID::ERR_SEMA_STANDALONE_CEDE_REJECTED,
              toString(standalonePlan->Rejection));
        // Nothing below this point (including a nested call) has been checked
        // or allowed to invalidate the source of a rejected standalone cede.
        m_ControlFlowStack.pop_back();
        return;
      }
    }
    auto authorityContext =
        beginAuthorityFullExpression(ExprS->Expression.get());
    // The admitted shared discard owns a carrier, not the enclosing function's
    // result. An inherited scalar return context must not collapse that carrier
    // to its pointee and send the pointee destructor a two-word handle slot.
    const bool sharedDiscard = activateStandalone && standalonePlan &&
        standalonePlan->Prepared.Ownership == TransferOwnershipKind::SharedOwner;
    auto exprType = sharedDiscard ? checkExpr(ExprS->Expression.get(), nullptr)
                                 : checkExpr(ExprS->Expression.get());
    restoreAuthorityFullExpression(std::move(authorityContext));
    if (activateStandalone) {
      const auto &diagnostics = DiagnosticEngine::records();
      bool failed = std::any_of(
          diagnostics.begin() + std::min(standaloneDiagnosticStart, diagnostics.size()),
          diagnostics.end(), [](const auto &record) { return record.Level == DiagLevel::Error; });
      if (!failed && (!exprType || exprType->isUnknown())) {
        error(standaloneCede, DiagID::ERR_SEMA_STANDALONE_CEDE_REJECTED,
              toString(TransferPlanRejection::IncompleteFacts));
        failed = true;
      }
      if (failed) {
        mergeAnalysisStates({*standaloneBefore}, standaloneBefore->PAL);
        if (ExprS->Stage0Authority) ExprS->Stage0Authority->SemaValidated = false;
      } else {
        Stage0CodeGenAuthority authority;
        authority.Kind = Stage0CodeGenAuthorityKind::NonCallItem;
        authority.RequiresAuthority = requiresStage0CodeGenAuthority(*standalonePlan);
        authority.SemaValidated = true;
        authority.Complete = true;
        authority.DestinationMatching = true;
        authority.SnapshotRevision = standalonePlan->Prepared.SnapshotRevision;
        authority.Destination = TransferDestination::StatementEndDiscard;
        authority.ItemPlan = *standalonePlan;
        ExprS->Stage0Authority = authority;
        standaloneCede->Stage0Authority = std::move(authority);
      }
    }
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
    Stage1BindingTransfer bindingTransfer(*this, Var, Var->Init != nullptr);
    recordHandleSurfaceVariableDecl(*Var);
    const bool inferredManagedConstruction = (Var->IsUnique || Var->IsShared) &&
        dynamic_cast<NewExpr *>(Var->Init.get());
    const bool inferredManagedReference = Var->IsReference && Var->ResolvedType &&
        Var->Permission.HandleLayers.size() == 2 && Var->ResolvedType->isReference() &&
        Var->ResolvedType->getPointeeType() &&
        (Var->ResolvedType->getPointeeType()->isUniquePtr() || Var->ResolvedType->getPointeeType()->isSharedPtr());
    if (!Var->DeclaredTypeSyntax && Var->ResolvedType &&
        (inferredManagedReference || inferredManagedConstruction)) {
      // Capture discovery has already elaborated this inferred two-layer
      // reference or explicit new binding. TypeName is a cached inner type,
      // not a new source annotation. Re-infer instead of wrapping twice or
      // applying the non-construction unique/shared conversion rules to new.
      Var->TypeName.clear();
      Var->ResolvedType.reset();
    }
    const bool inferredType = Var->TypeName.empty() || Var->TypeName == "auto";

    // [Constitutional 1.3] Adversarial Principle: $ is only for contesting
    // inheritance.
    if (Var->IsValueBlocked || Var->IsRebindBlocked) {
      DiagnosticEngine::report(getLoc(Var), DiagID::ERR_REDUNDANT_BLOCK,
                               Var->Name);
      HasError = true;
    }
    if (!Var->TypeName.empty() && Var->TypeName != "auto") {
      auto explicitTy = Var->DeclaredTypeSyntax ? toka::Type::fromSyntax(Var->DeclaredTypeSyntax) : toka::Type::fromString(Var->TypeName);
      validateHandleGrammar(getLoc(Var), explicitTy);
      if (!Var->ResolvedType) {
        validateTypeVisibilityInType(Var->TypeName, getLoc(Var));
        validateDynTraitObjectSafetyInType(Var->TypeName, getLoc(Var));
      }
    }

    if (!validateResultCedeSyntax(Var, Var->DeclaredTypeSyntax))
      return;
    std::string InitType = "";
    std::shared_ptr<toka::Type> InitTypeObj = nullptr;
    m_LastBorrowSource.clear();
    if (Var->Init) {
      Var->Init = foldGenericConstant(std::move(Var->Init));
      // Only the initializer's direct conversion can receive this request.
      // Do not broadcast expected writability into calls, branches or casts'
      // operands. Parentheses are absent from the AST; unsafe is transparent.
      Expr *requestEdge = Var->Init.get();
      while (auto *wrapper = dynamic_cast<UnsafeExpr *>(requestEdge))
        requestEdge = wrapper->Expression.get();
      if (auto *cast = dynamic_cast<CastExpr *>(requestEdge))
        cast->RawWriteRequest = Var->IsRawPointer && Var->IsValueMutable &&
                                       cast->Kind == CastKind::Conversion
                                   ? Var : nullptr;
      m_ControlFlowStack.push_back({Var->Name, NoProducedValue, nullptr, false, true});
      std::shared_ptr<toka::Type> declTargetTy = nullptr;
      if (!Var->TypeName.empty() && Var->TypeName != "auto") {
        declTargetTy = resolveType(
            Var->DeclaredTypeSyntax
                ? toka::Type::fromSyntax(Var->DeclaredTypeSyntax)
                : toka::Type::fromString(Var->TypeName),
            false);
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
      auto stage0DestinationType = declTargetTy;
      if (stage0DestinationType && (Var->IsRawPointer || Var->IsUnique || Var->IsShared || Var->IsReference)) {
        // A capture-discovery pass may already have filled TypeName with the
        // inferred soul. The declared handle layers remain independent facts;
        // do not mistake that cached soul for the physical binding target.
        stage0DestinationType = resolveExplicitCedeStage0TypeReadOnly(
            synthesizePhysicalTypeObject(Var->Permission, Var->DeclaredTypeSyntax, Var->TypeName));
      }
      if (!stage0DestinationType && (Var->IsRawPointer || Var->IsUnique ||
                                     Var->IsShared || Var->IsReference)) {
        auto sourceType =
            queryExplicitCedeStage0NonCallType(Var->Init.get(), nullptr);
        if (sourceType && !sourceType->isUnknown()) {
          std::string spelling;
          if (Var->IsRawPointer)
            spelling = "*";
          else if (Var->IsUnique)
            spelling = "^";
          else if (Var->IsShared)
            spelling = "~";
          else
            spelling = "&";
          if (Var->IsRebindable)
            spelling += "#";
          spelling += Type::stripMorphology(sourceType->toString());
          if (Var->IsValueMutable)
            spelling += "#";
          stage0DestinationType = Type::fromString(spelling);
        }
      }
      if (bindingTransfer.enabled())
        bindingTransfer.prepare(Var->Init.get(), stage0DestinationType);
      else
        recordExplicitCedeStage0NonCallPlan(
            Var, Var->Init.get(), stage0DestinationType,
            TransferDestination::Initialization,
            TransferEligibilityContext::Initialization, "initialization");

      bool oldExpectedWritability = m_ExpectedWritability;
      if (Var->IsReference) {
          m_ExpectedWritability = Var->IsValueMutable;
      }
      m_LastBorrowSource = ""; // [NEW] Clear stale borrow source
      bool oldConsuming = m_IsConsumingEffect;
      m_IsConsumingEffect = true;
      bool rejectedAliasInit = false;
      if (Var->IsUnique || (declTargetTy && declTargetTy->isUniquePtr())) {
        Expr *transferSource = Var->Init.get();
        while (auto *cast = dynamic_cast<CastExpr *>(transferSource))
          transferSource = cast->Expression.get();
        if (auto *unary = dynamic_cast<UnaryExpr *>(transferSource);
            unary && unary->Op == TokenType::Caret)
          rejectedAliasInit =
              diagnosePlaceAliasOwnershipTransfer(Var, unary->RHS.get());
      }
      bool oldSuppressAliasInvalidation =
          m_SuppressRejectedAliasInvalidation;
      m_SuppressRejectedAliasInvalidation = rejectedAliasInit;
      auto authorityContext = beginAuthorityFullExpression(Var->Init.get());
      InitTypeObj = checkExpr(Var->Init.get(), declTargetTy);
      restoreAuthorityFullExpression(std::move(authorityContext));
      m_SuppressRejectedAliasInvalidation = oldSuppressAliasInvalidation;
      m_IsConsumingEffect = oldConsuming;
      m_ExpectedWritability = oldExpectedWritability;
      if (!InitTypeObj) {
        if (!HasError)
          error(Var, DiagID::ERR_GENERIC_SEMA, "Initializer has no valid type");
        m_ControlFlowStack.pop_back();
        return;
      }
      InitType = InitTypeObj->toString();
      if (auto *ascription = dynamic_cast<CastExpr *>(Var->Init.get());
          ascription && ascription->Kind == CastKind::Ascription) {
        // The source ascription is the inferred binding type.  In particular,
        // semantic Type rendering may omit callable `cede` morphology that is
        // still part of the source contract.
        InitType = ascription->TargetType;
      }

      if (!Var->IsMorphicExempt &&
          !dynamic_cast<CedeExpr *>(Var->Init.get()) && InitTypeObj &&
          InitTypeObj->requiresExplicitOwnershipTransfer(this)) {
        Expr *directSource = Var->Init.get();
        while (true) {
          if (auto *cast = dynamic_cast<CastExpr *>(directSource)) {
            directSource = cast->Expression.get();
          } else if (auto *unsafeExpr =
                         dynamic_cast<UnsafeExpr *>(directSource)) {
            directSource = unsafeExpr->Expression.get();
          } else {
            break;
          }
        }
        if (auto *sourceVar = dynamic_cast<VariableExpr *>(directSource);
            sourceVar && makeAccessPath(directSource)) {
          SymbolInfo *sourceInfo = nullptr;
          std::string actualName;
          const bool sourceAlreadyMoved =
              CurrentScope->findVariableWithDeref(
                  sourceVar->Name, sourceInfo, actualName) &&
              sourceInfo &&
              hasPlaceState(sourceInfo->placeFact(), PlaceState::Moved);
          auto sourceType = sourceInfo ? sourceInfo->TypeObj : nullptr;
          if (sourceType)
            sourceType = resolveType(sourceType, false);
          const bool sourceOwnsAggregate =
              InitTypeObj->isShape() && sourceType && sourceType->isShape() &&
              sourceType->requiresExplicitOwnershipTransfer(this);
          if (!sourceAlreadyMoved && sourceOwnsAggregate) {
            std::string sourcePath = getPathString(directSource);
            if (sourcePath.empty())
              sourcePath = directSource->toString();
            error(Var->Init.get(),
                  DiagID::ERR_SEMA_OWNED_VALUE_BINDING_REQUIRES_CEDE,
                  Var->Name, sourcePath, sourcePath);
          }
        } else if (dynamic_cast<MemberExpr *>(directSource) &&
                   makeAccessPath(directSource)) {
          error(Var->Init.get(),
                DiagID::ERR_SEMA_OWNED_PROJECTION_BINDING_REQUIRES_CEDE,
                Var->Name, directSource->toString(), directSource->toString());
        }
      }

      // `cede` does not prove that a may-zero raw source is non-zero.
      if (auto *cede = dynamic_cast<CedeExpr *>(Var->Init.get())) {
        // An inner ascription is the explicit destination contract.
        const auto *destinationAscription =
            dynamic_cast<const CastExpr *>(cede->Value.get());
        const bool ascriptionAllowsZero =
            destinationAscription &&
            destinationAscription->Kind == CastKind::Ascription &&
            isMayZeroRawCedeDestination(destinationAscription->ResolvedType);
        const bool targetAllowsZero =
            (Var->IsRawPointer && Var->IsPointerNullable) ||
            (declTargetTy && declTargetTy->isRawPointer() &&
             declTargetTy->IsNullable) ||
            ascriptionAllowsZero;
        // Any written raw destination must state whether it accepts zero.
        const bool hasDeclaredDestination =
            !inferredType || Var->IsRawPointer || Var->IsUnique ||
            Var->IsShared || Var->IsReference ||
            (destinationAscription &&
             destinationAscription->Kind == CastKind::Ascription);
        const bool sourceMayBeZero = isMayZeroRawCedeSource(cede);
        if (hasDeclaredDestination && !targetAllowsZero && sourceMayBeZero) {
          DiagnosticEngine::report(
              getLoc(Var),
              DiagID::ERR_SEMA_CEDE_MAY_ZERO_RAW_REQUIRES_GUARD);
          HasError = true;
        }
      }
      
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

    if (inferredType && Var->Init && Var->Init->IsAbstractWholeValue &&
        Var->Permission.HandleLayers.empty() && !Var->IsRawPointer &&
        !Var->IsUnique && !Var->IsShared && !Var->IsReference) {
      Var->IsAbstractWholeValue = true;
      Var->IsMorphicExempt = true;
      Var->Permission.MorphicExempt = true;
    }

    // A qualified raw_take returns the complete stored morphology. For an
    // inferred morphic binding, carry its resolved owner view into the normal
    // binding machinery instead of stripping the hat into a payload local.
    // This does not grant any permission absent from the binding declaration.
    if (inferredType && Var->IsMorphicExempt && InitTypeObj) {
      Expr *source = Var->Init.get();
      while (auto *unsafe = dynamic_cast<UnsafeExpr *>(source))
        source = unsafe->Expression.get();
      auto *take = dynamic_cast<RawTakeExpr *>(source);
      if (take && take->Plan && take->Plan->SemaValidated &&
          (InitTypeObj->isUniquePtr() || InitTypeObj->isSharedPtr())) {
        Var->IsUnique = InitTypeObj->isUniquePtr();
        Var->IsShared = InitTypeObj->isSharedPtr();
        Var->Permission = BindingPermission::fromLegacy(
            Var->IsRawPointer, Var->IsUnique, Var->IsShared, Var->IsReference,
            Var->IsRebindable, Var->IsPointerNullable, Var->IsRebindBlocked,
            Var->IsValueMutable, Var->IsValueNullable, Var->IsValueBlocked, true);
      }
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
        // A `cede` expression marks transfer across a call edge. Binding its
        // result accepts that transfer, so the local has the underlying owned
        // type.  A RHS type ascription such as `closure:cede fn()` is instead
        // an explicit callable contract and must retain its receiver mode.
        if (dynamic_cast<CedeExpr *>(Var->Init.get()) &&
            Inferred.rfind("cede ", 0) == 0) {
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
          if (Var->IsRawPointer && !Var->IsPointerNullable && InitTypeObj &&
              InitTypeObj->isRawPointer() && InitTypeObj->IsNullable) {
            auto nonzeroType = InitTypeObj->withAttributes(
                InitTypeObj->IsWritable, false, InitTypeObj->IsBlocked);
            error(Var, DiagID::ERR_NONZERO_RAW_NULL_FLOW,
                  nonzeroType->toString());
            Var->TypeName = "unknown";
            return;
          }
          // Remove the raw may-zero wrapper before matching the binding's
          // explicit `*` morphology.
          if (Inferred.rfind("nul ", 0) == 0) {
            Inferred = Inferred.substr(4);
          } else if (Inferred.rfind("nul", 0) == 0 &&
                     Inferred.size() > 3 && Inferred[3] == '*') {
            Inferred = Inferred.substr(3);
          }
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
    if (Var->Init && !isTodoWrapper(Var->Init.get()) &&
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
    
    // Binding-side morphology wraps the soul selected by the initializer.
    // For inferred declarations, take that soul structurally instead of
    // concatenating textual prefixes: `auto &x = &u` remains `&T`, while
    // `auto &^h = &^u` becomes `&^T` exactly once.
    if (inferredType && !Var->Permission.HandleLayers.empty() && InitTypeObj) {
      baseType = InitTypeObj->getSoulName();
    } else if (baseType.size() > 1 &&
               (baseType[0] == '^' || baseType[0] == '~' ||
                baseType[0] == '*' || baseType[0] == '&')) {
      if (morph.empty()) {
        morph = std::string(1, baseType[0]);
        baseType = baseType.substr(1);
      } else if (morph[0] == baseType[0]) {
        baseType = baseType.substr(1);
      }
    }
    
    // Inferred morphic declarations already selected a full handle above.
    // Keep the physical binding descriptor consistent with that selection;
    // otherwise synthesizePhysicalTypeObject silently rebuilds only its soul.
    // This is target typing, not transfer admission: the complete binding
    // planner below still validates source, dependencies, PAL and permissions.
    if (inferredType && Var->IsMorphicExempt && InitTypeObj &&
        !Var->IsUnique && !Var->IsShared && !Var->IsReference &&
        !Var->IsRawPointer &&
        ((morph == "^" && InitTypeObj->isUniquePtr()) ||
         (morph == "~" && InitTypeObj->isSharedPtr()) ||
         (morph == "*" && InitTypeObj->isRawPointer()) ||
         (morph == "&" && InitTypeObj->isReference()))) {
      Var->IsUnique = InitTypeObj->isUniquePtr();
      Var->IsShared = InitTypeObj->isSharedPtr();
      Var->IsRawPointer = InitTypeObj->isRawPointer();
      Var->IsReference = InitTypeObj->isReference();
      Var->IsPointerNullable = InitTypeObj->IsNullable;
      Var->Permission = BindingPermission::fromLegacy(
          Var->IsRawPointer, Var->IsUnique, Var->IsShared, Var->IsReference,
          Var->IsRebindable, Var->IsPointerNullable, Var->IsRebindBlocked,
          Var->IsValueMutable, Var->IsValueNullable, Var->IsValueBlocked, true);
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

    // [Constitution 1.3] Dual-Attribute Synthesis
    BindingPermission LocalPermission = Var->Permission;
    LocalPermission.SoulWritable =
        Var->IsValueMutable || (morph.empty() && Var->IsRebindable);
    Info.Permission = LocalPermission;

    // An inferred reference constructed over opaque T borrows that complete
    // T, not its eventual terminal soul. Keep the checked inner type; the
    // binding's own permission remains the access ceiling.
    auto initializerContract = Var->Init ? queryGenericValueContract(Var->Init.get()) : nullptr;
    if (inferredType && Var->IsReference && !Var->IsValueMutable &&
        InitTypeObj && InitTypeObj->isReference() &&
        Var->Permission.HandleLayers.size() <= 1) {
      bool preserveInner = false;
      if (initializerContract && initializerContract->Type &&
          initializerContract->Type->NodeKind == TypeSyntax::Kind::Morphology &&
          initializerContract->Type->Text == "&" &&
          !initializerContract->Type->IsPostfix) {
        auto selected = *initializerContract;
        selected.Type = selected.Type->Subject;
        if (selected.isWholeValue())
          preserveInner = true;
      } else if (InitTypeObj->getPointeeType() &&
                 (InitTypeObj->getPointeeType()->isPointer() ||
                  InitTypeObj->getPointeeType()->isSmartPointer())) {
        preserveInner = true;
      }
      if (preserveInner) {
        auto pointee = InitTypeObj->getPointeeType();
        if (pointee && !Var->IsValueMutable && pointee->IsWritable) {
          pointee = pointee->withAttributes(false, pointee->IsNullable, pointee->IsBlocked);
        }
        auto ref = std::make_shared<ReferenceType>(pointee);
        Info.TypeObj = ref->withAttributes(
            Var->IsRebindable, Var->IsPointerNullable, Var->IsRebindBlocked);
      }
    }

    if (!Info.TypeObj) {
      if (inferredType && morph.empty() && InitTypeObj &&
          InitTypeObj->isShape()) {
        Info.TypeObj = resolveType(InitTypeObj->withAttributes(
            LocalPermission.SoulWritable, false), false);
        if (Info.TypeObj) Info.TypeObj->IsCede = false;
      } else {
        Info.TypeObj = resolveType(
            Sema::synthesizePhysicalTypeObject(
                LocalPermission,
                inferredType ? TypeSyntaxPtr{} : Var->DeclaredTypeSyntax,
                baseType, false),
            false);
      }
      if (!Info.TypeObj) {
        Info.TypeObj = Sema::synthesizePhysicalTypeObject(
            LocalPermission,
            inferredType ? TypeSyntaxPtr{} : Var->DeclaredTypeSyntax, baseType,
            false);
      }
    }
    std::set<std::string> depsToCommitAsBorrow;
    if (!m_LastLifeDependencies.empty()) {
      for (const auto &dep : m_LastLifeDependencies) {
        Info.LifeDependencySet.insert(dep);
        depsToCommitAsBorrow.insert(dep);

        SymbolInfo *depInfo = nullptr;
        std::string depRoot = extractPathRoot(dep);
        std::string actualDepName = depRoot;
        if (CurrentScope->findVariableWithDeref(depRoot, depInfo, actualDepName) && depInfo) {
          Info.LifeDependencySet.insert(depInfo->LifeDependencySet.begin(), depInfo->LifeDependencySet.end());
          depsToCommitAsBorrow.insert(depInfo->LifeDependencySet.begin(), depInfo->LifeDependencySet.end());
        }

        if (depInfo && !depInfo->IsReference() && !depInfo->LifeDependencySet.empty() && isBorrowLikeType(depInfo->TypeObj)) {
          for (const auto &transDep : depInfo->LifeDependencySet) {
            int transDepth = getScopeDepth(transDep);
            int myDepth = CurrentScope->Depth;
            if (myDepth < transDepth && isBorrowLikeType(Info.TypeObj)) {
              DiagnosticEngine::report(getLoc(Var), DiagID::ERR_BORROW_LIFETIME,
                                       Var->Name, transDep);
              HasError = true;
              SourceLocation originLoc = findPathDeclaration(transDep);
              recordDecision(Var, SemanticRuleID::EffRet001,
                             SemanticOperation::EscapingDependency,
                             SemanticDecision::Reject,
                             SemanticReason::LifetimeDepthViolation, Var->Name,
                             transDep, originLoc);
              if (originLoc.isValid())
                DiagnosticEngine::report(originLoc, DiagID::NOTE_GENERIC,
                                         "shorter-lived dependency declared here");
            }
          }
        } else {
          int srcDepth = getScopeDepth(dep);
          int myDepth = CurrentScope->Depth;
          if (myDepth < srcDepth && isBorrowLikeType(Info.TypeObj)) {
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
      }
      m_LastLifeDependencies.clear();
    }

    Expr *closureSource = Var->Init.get();
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

    if (auto *clo = dynamic_cast<ClosureExpr *>(closureSource)) {
      for (const auto &dep : clo->ImplicitCaptures) {
        Info.LifeDependencySet.insert(dep);
        depsToCommitAsBorrow.insert(dep);
        SymbolInfo *depInfo = nullptr;
        if (CurrentScope->findSymbol(dep, depInfo)) {
          Info.LifeDependencySet.insert(depInfo->LifeDependencySet.begin(),
                                        depInfo->LifeDependencySet.end());
          depsToCommitAsBorrow.insert(depInfo->LifeDependencySet.begin(),
                                      depInfo->LifeDependencySet.end());
        }
      }
      Info.HasClosureBoundarySummary = clo->HasBoundaryCaptureSummary;
      for (const auto &capture : clo->ExplicitCaptures)
        Info.ClosureExplicitCaptures.insert(
            Type::stripMorphology(capture.Name));
      Info.ClosureImplicitCaptures.insert(
          clo->BoundaryImplicitCaptures.begin(),
          clo->BoundaryImplicitCaptures.end());
      Info.ClosureNonSendCaptures.insert(clo->BoundaryNonSendCaptures.begin(),
                                         clo->BoundaryNonSendCaptures.end());
      Info.ClosureNonSyncCopyCaptures.insert(
          clo->BoundaryNonSyncCopyCaptures.begin(),
          clo->BoundaryNonSyncCopyCaptures.end());
    } else if (auto *source = dynamic_cast<VariableExpr *>(closureSource)) {
      SymbolInfo *sourceInfo = nullptr;
      std::string sourceName;
      if (CurrentScope->findVariableWithDeref(source->Name, sourceInfo,
                                              sourceName) && sourceInfo &&
          sourceInfo->HasClosureBoundarySummary) {
        Info.HasClosureBoundarySummary = true;
        Info.ClosureExplicitCaptures = sourceInfo->ClosureExplicitCaptures;
        Info.ClosureImplicitCaptures = sourceInfo->ClosureImplicitCaptures;
        Info.ClosureNonSendCaptures = sourceInfo->ClosureNonSendCaptures;
        Info.ClosureNonSyncCopyCaptures =
            sourceInfo->ClosureNonSyncCopyCaptures;
      }
    }

    // Handle rebinding (&#) does not remove the reference's persistent loan.
    if (Info.TypeObj && Info.TypeObj->isReference() && !m_LastBorrowSource.empty()) {
      // A rebindable reference retains its loan, but is not a permanent
      // alias of its initializer. CurrentReferenceTargets tracks rebinding.
      if (!Var->IsRebindable) Info.BorrowedFrom = m_LastBorrowSource;
      Info.LifeDependencySet.insert(m_LastBorrowSource);

      SymbolInfo *srcPtr = nullptr;
      std::string srcRoot = extractPathRoot(m_LastBorrowSource);
      std::string actualSrcName = srcRoot;
      if (CurrentScope->findVariableWithDeref(srcRoot, srcPtr, actualSrcName) && srcPtr) {
        Info.LifeDependencySet.insert(srcPtr->LifeDependencySet.begin(), srcPtr->LifeDependencySet.end());
      }

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

      if (!Var->IsRebindable) Info.BorrowedFrom = m_LastBorrowSource;
      if (!m_LastBorrowSource.empty()) {
          AccessPath borrowPath =
              canonicalizeAccessPath(makeAccessPath(m_LastBorrowSource));
          if (auto *borrowExpr =
                  dynamic_cast<UnaryExpr *>(Var->Init.get());
              borrowExpr && borrowExpr->Op == TokenType::Ampersand) {
            borrowPath = canonicalizeAccessPath(
                makeAccessPath(borrowExpr->RHS.get()));
          }
          if (!Var->IsRebindable) Info.BorrowedPath = borrowPath;
          PALCheckerState.commitTransient(borrowPath);
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

    // Retain the actual origins of a checked factory result with borrowed
    // fields. Legacy expression checking may have cleared its transient
    // dependency metadata; never reconstruct it from the enclosing return
    // declaration or merely from the field's type.
    if (m_EnableStage1ExplicitCallerCede && Var->Init && !HasError) {
      Expr *producer = Var->Init.get();
      while (producer) {
        if (auto *cast = dynamic_cast<CastExpr *>(producer))
          producer = cast->Expression.get();
        else if (auto *unsafe = dynamic_cast<UnsafeExpr *>(producer))
          producer = unsafe->Expression.get();
        else
          break;
      }
      auto shape = std::dynamic_pointer_cast<ShapeType>(Info.TypeObj);
      if ((dynamic_cast<CallExpr *>(producer) ||
           dynamic_cast<MethodCallExpr *>(producer)) && shape && shape->Decl) {
        bool hasBorrowedField = false;
        for (const auto &field : shape->Decl->Members) {
          auto type = resolveExplicitCedeStage0TypeReadOnly(getPhysicalType(field));
          auto ownership = queryExplicitCedeStage0OwnershipReadOnly(type);
          hasBorrowedField |= type && !type->isRawPointer() && ownership &&
                              *ownership == ValueOwnership::BorrowedView;
        }
        std::vector<AccessPath> origins;
        std::map<std::string, ActualReturnFieldOrigins> fields;
        if (hasBorrowedField && collectActualReturnReferents(
                producer, origins, nullptr, nullptr, nullptr, &fields)) {
          for (const auto &origin : origins) {
            const auto dependency = origin.toLegacyString();
            Info.LifeDependencySet.insert(dependency);
            depsToCommitAsBorrow.insert(dependency);
          }
          for (const auto &[field, facts] : fields) {
            if (field.empty()) continue; // A whole-result ceiling is not a field mapping.
            for (const auto &origin : facts.Referents)
              Info.FieldDependencySet[field].insert(origin.toLegacyString());
          }
        }
      }
    }

    if (m_EnableStage1ExplicitCallerCede && Var->Init && !HasError &&
        dynamic_cast<WaitExpr *>(Var->Init.get())) {
      std::vector<AccessPath> origins;
      if (collectActualReturnReferents(Var->Init.get(), origins)) {
        for (const auto &origin : origins) {
          const auto dependency = origin.toLegacyString();
          Info.LifeDependencySet.insert(dependency);
          depsToCommitAsBorrow.insert(dependency);
        }
      }
    }

    // Preserve actual borrowed-field origins of an initialized record.  The
    // return planner must not later substitute the function's declared
    // dependency ceiling for missing binding provenance.
    Expr *construction = Var->Init.get();
    while (auto *cast = dynamic_cast<CastExpr *>(construction)) {
      if (cast->Kind != CastKind::Implicit && cast->Kind != CastKind::Ascription) break;
      construction = cast->Expression.get();
    }
    auto *recordInitializer = dynamic_cast<InitStructExpr *>(Var->Init.get());
    auto recordType = Info.TypeObj;
    if (auto *allocation = dynamic_cast<NewExpr *>(construction);
        allocation && !allocation->ArraySize && recordType &&
        (recordType->isUniquePtr() || recordType->isSharedPtr())) {
      // The allocation owns the descriptor, not the field's referent. Reuse
      // the same checked field-origin and PAL registration as a value record.
      auto *initializer = dynamic_cast<InitStructExpr *>(allocation->Initializer.get());
      auto payload = std::dynamic_pointer_cast<ShapeType>(recordType->getPointeeType());
      auto initialized = initializer ? std::dynamic_pointer_cast<ShapeType>(initializer->ResolvedType) : nullptr;
      if (payload && initialized && payload->Decl && payload->Decl == initialized->Decl) {
        recordInitializer = initializer;
        recordType = payload;
      }
    }
    if (auto *init = recordInitializer; init && recordType && !HasError) {
      auto shape = std::dynamic_pointer_cast<ShapeType>(recordType);
      if (shape && shape->Decl) {
        std::map<std::string, std::shared_ptr<Type>> substitutions;
        if (shape->GenericArgs.size() == shape->Decl->GenericParams.size()) {
          for (size_t i = 0; i < shape->GenericArgs.size(); ++i)
            substitutions[shape->Decl->GenericParams[i].Name] = shape->GenericArgs[i];
        }
        for (const auto &initializer : init->Members) {
          const auto fieldName = Type::stripMorphology(initializer.first);
          auto field = std::find_if(shape->Decl->Members.begin(),
              shape->Decl->Members.end(), [&](const ShapeMember &member) {
                return Type::stripMorphology(member.Name) == fieldName;
              });
          if (field == shape->Decl->Members.end()) continue;
          auto fieldType = getPhysicalType(*field);
          if (fieldType && !substitutions.empty())
            fieldType = fieldType->substitute(substitutions);
          fieldType = resolveExplicitCedeStage0TypeReadOnly(fieldType);
          if (!fieldType || fieldType->isRawPointer()) continue;
          auto ownership = queryExplicitCedeStage0OwnershipReadOnly(fieldType);
          if (!ownership || *ownership != ValueOwnership::BorrowedView) continue;
          std::vector<AccessPath> origins;
          if (!collectActualReturnReferents(initializer.second.get(), origins)) continue;
          for (const auto &origin : origins) {
            const auto dependency = origin.toLegacyString();
            Info.FieldDependencySet[fieldName].insert(dependency);
            Info.LifeDependencySet.insert(dependency);
            depsToCommitAsBorrow.insert(dependency);
          }
        }
      }
    }

    if (Var->Init) {
      // Shared flow is checked only against the direct initializer.  Earlier
      // hops have already reduced that initializer's capability, so this
      // local intersection prevents escalation without provenance tracing.
      PermissionFlow flow = getPermissionFlow(Var->Init.get());
      // Raw access is governed by the explicit unsafe boundary, not by a
      // persistent safe-view ceiling.  It therefore remains outside this
      // shared-flow propagation rule.
      if (flow.Kind == PermissionFlowKind::Shared) {
        Info.PayloadFlowWritable = flow.DirectCapability.PayloadWritable;
        Info.HasPayloadFlowCeiling = true;
      }
    }

    if (!Info.TypeObj) {
      if (inferredType && morph.empty() && InitTypeObj &&
          InitTypeObj->isShape()) {
        Info.TypeObj = resolveType(InitTypeObj->withAttributes(
            LocalPermission.SoulWritable, false), false);
        if (Info.TypeObj) Info.TypeObj->IsCede = false;
      } else {
        auto directType = resolveType(
            Sema::synthesizePhysicalTypeObject(
                LocalPermission,
                inferredType ? TypeSyntaxPtr{} : Var->DeclaredTypeSyntax,
                baseType, false),
            false);
        Info.TypeObj = directType;
      }
      if (!Info.TypeObj) {
        Info.TypeObj = Sema::synthesizePhysicalTypeObject(
            LocalPermission,
            inferredType ? TypeSyntaxPtr{} : Var->DeclaredTypeSyntax, baseType,
            false);
      }
    }

    if (Info.TypeObj) {
      if (containsInternalPlaceOutcome(Info.TypeObj)) {
        error(Var, DiagID::ERR_PLACE_OUTCOME_INTERNAL_ONLY,
              Info.TypeObj->toString());
      }
      if (!validateHandleGrammar(getLoc(Var), Info.TypeObj)) {
        HasError = true;
      }
      std::string fnId = CurrentFunction ? (!CurrentFunction->CodegenName.empty() ? CurrentFunction->CodegenName : CurrentFunction->Name) : "";
      bool isGeneric = (CurrentFunction && (CurrentFunction->TemplateOrigin != nullptr || (!CurrentFunction->CodegenName.empty() && CurrentFunction->CodegenName.find("_M_") != std::string::npos)));
      std::vector<FormationPhase> phases;
      if (isGeneric) {
        phases.push_back(FormationPhase::GenericInstance);
      }
      if (inferredType) {
        phases.push_back(FormationPhase::IntermediateLowering);
      } else {
        phases.push_back(FormationPhase::DirectResolution);
      }
      SyntaxOrigin origin = (CurrentModule && CurrentModule->IsInterface) ? SyntaxOrigin::TKIImport : SyntaxOrigin::SourceSurface;
      recordHandleGrammarAudit(Info.TypeObj, origin, phases,
                               CurrentFunction ? CurrentFunction->Name : "", "", Var->Name, Var->Loc, isGeneric, fnId);
    }

    // Only a complete contextual todo may seed a conditional binding.  An
    // inferred `auto value = todo` is underconstrained and must remain
    // unavailable rather than being mislabeled as conditional.  The direct
    // helper preserves the narrow v1 alias contract; the expression collector
    // then extends it through non-transfer expressions and resolved calls.
    if (Var->Init && InitTypeObj && !InitTypeObj->isUnknown()) {
      Info.ConditionalTodoIds =
          collectConditionalTodoDependencies(Var->Init.get());
    }

    // A declared return dependency is itself the caller-side borrow contract.
    // Commit it even when generic type resolution has not yet exposed an
    // embedded reference (for example Option<&T>). Filtering by the inferred
    // storage type made such lending APIs lose their PAL protection.
    if (!depsToCommitAsBorrow.empty()) {
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
    Info.IsAbstractWholeValue = Var->IsAbstractWholeValue;
    Info.GenericContract = Var->Init ? queryGenericValueContract(Var->Init.get()) : nullptr;
    Info.IsDeclaredMutable = Var->IsValueMutable;
    Info.DeclLoc = Var->Loc;
    Var->ResolvedType = Info.TypeObj;
    if (Info.TypeObj && (Info.TypeObj->isFunction() || Info.TypeObj->isDynFn()))
      Info.CallableReceiver = getCallableReceiverMode(*Info.TypeObj);
    if (Info.TypeObj &&
        (Info.TypeObj->isFunction() || Info.TypeObj->isDynFn())) {
      TypeSyntaxPtr callableDeclarationSyntax =
          inferredType ? TypeSyntaxPtr{} : Var->DeclaredTypeSyntax;
      bool hasOwnCallableDeclaration = callableDeclarationSyntax != nullptr;
      Expr *initializer = Var->Init.get();
      while (auto *unsafeExpr = dynamic_cast<UnsafeExpr *>(initializer))
        initializer = unsafeExpr->Expression.get();
      if (auto *ascription = dynamic_cast<CastExpr *>(initializer);
          ascription && ascription->Kind == CastKind::Ascription) {
        callableDeclarationSyntax = ascription->TargetTypeSyntax;
        hasOwnCallableDeclaration = callableDeclarationSyntax != nullptr;
      }
      if (hasOwnCallableDeclaration) {
        populateCallableParameterOrigins(
            Info, callableDeclarationSyntax,
            callableDeclarationGenericNames(CurrentFunction));
      }
      if (inferredType && !hasOwnCallableDeclaration) {
        Expr *source = initializer;
        while (source) {
          if (auto *unsafeExpr = dynamic_cast<UnsafeExpr *>(source)) {
            source = unsafeExpr->Expression.get();
          } else if (auto *cede = dynamic_cast<CedeExpr *>(source)) {
            source = cede->Value.get();
          } else if (auto *cast = dynamic_cast<CastExpr *>(source);
                     cast && cast->Kind != CastKind::Ascription) {
            source = cast->Expression.get();
          } else {
            break;
          }
        }
        if (auto *sourceVariable = dynamic_cast<VariableExpr *>(source)) {
          SymbolInfo *sourceInfo = nullptr;
          std::string sourceName;
          if (CurrentScope->findVariableWithDeref(sourceVariable->Name,
                                                  sourceInfo, sourceName) &&
              sourceInfo && sourceInfo->CallableParameterOriginsComplete) {
            Info.CallableParameterOrigins =
                sourceInfo->CallableParameterOrigins;
            Info.CallableParameterOriginsComplete = true;
          }
        }
      }
    }
    if (Info.TypeObj && Info.TypeObj->isDynFn() && Var->Init) {
      Expr *source = Var->Init.get();
      bool destructive = false;
      while (source) {
        if (auto *unsafeExpr = dynamic_cast<UnsafeExpr *>(source)) {
          source = unsafeExpr->Expression.get();
        } else if (auto *cast = dynamic_cast<CastExpr *>(source)) {
          source = cast->Expression.get();
        } else if (auto *cede = dynamic_cast<CedeExpr *>(source)) {
          destructive = true;
          source = cede->Value.get();
        } else if (auto *postfix = dynamic_cast<PostfixExpr *>(source)) {
          source = postfix->LHS.get();
        } else {
          break;
        }
      }
      const bool copiesNamedEnvironment =
          source && source->ResolvedType && source->ResolvedType->isDynFn() &&
          makeAccessPath(source) && !destructive;
      std::shared_ptr<Type> sourceCallableType =
          source && source->ResolvedType && source->ResolvedType->isDynFn()
              ? source->ResolvedType
              : nullptr;
      if (auto *sourceVariable = dynamic_cast<VariableExpr *>(source)) {
        SymbolInfo *sourceInfo = nullptr;
        std::string sourceName;
        if (CurrentScope->findVariableWithDeref(sourceVariable->Name,
                                                sourceInfo, sourceName) &&
            sourceInfo) {
          Info.CallableReceiver = sourceInfo->CallableReceiver;
          if (sourceInfo->TypeObj && sourceInfo->TypeObj->isDynFn())
            sourceCallableType = sourceInfo->TypeObj;
        }
      } else if (sourceCallableType) {
        Info.CallableReceiver = getCallableReceiverMode(*sourceCallableType);
      }
      if (sourceCallableType &&
          Info.CallableReceiver == CallableReceiverMode::Consuming) {
        auto preserved = std::dynamic_pointer_cast<DynFnType>(
            sourceCallableType->withAttributes(Info.TypeObj->IsWritable,
                                               Info.TypeObj->IsNullable,
                                               Info.TypeObj->IsBlocked));
        if (preserved) {
          preserved->ReceiverMode = CallableReceiverMode::Consuming;
          Info.TypeObj = preserved;
          Var->ResolvedType = preserved;
        }
      }
      if (copiesNamedEnvironment &&
          Info.CallableReceiver == CallableReceiverMode::Consuming) {
        std::string sourceName = getPathString(source);
        if (sourceName.empty())
          sourceName = source->toString();
        error(Var->Init.get(),
              DiagID::ERR_SEMA_CONSUMING_DYN_FN_COPY_REQUIRES_CEDE, sourceName,
              sourceName);
        Var->DynFnEnvironment = DynFnEnvironmentDisposition::None;
      } else {
        Var->DynFnEnvironment = copiesNamedEnvironment
                                    ? DynFnEnvironmentDisposition::Retain
                                    : DynFnEnvironmentDisposition::Transfer;
      }
    }
    if (auto *closure = dynamic_cast<ClosureExpr *>(Var->Init.get())) {
      if (!Info.TypeObj ||
          (!Info.TypeObj->isFunction() && !Info.TypeObj->isDynFn()))
        Info.CallableReceiver = closure->CallableReceiver;
    }

    const auto *cast = dynamic_cast<CastExpr *>(Var->Init.get());
    const bool isAscribedUninit =
        cast && cast->Kind == CastKind::Ascription &&
        dynamic_cast<UnsetExpr *>(cast->Expression.get());
    const bool isUninitializedDecl =
        !Var->Init || dynamic_cast<UnsetExpr *>(Var->Init.get()) || isAscribedUninit;

    if (isUninitializedDecl) {
      Info.InitMask = 0;
      Info.placeFact() = PlaceState::Never;
    } else {
      Info.InitMask = (m_LastInitMask == 0) ? ~0ULL : m_LastInitMask;
      Info.placeFact() = PlaceState::Live;
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
    if (bindingTransfer.enabled() &&
        !bindingTransfer.prepare(Var->Init.get(), Var->ResolvedType, nullptr, true, InitTypeObj)) {
      m_ControlFlowStack.pop_back();
      return;
    }
    Info.ASTPtr = Var;
    if (m_EnableStage1ExplicitCallerCede && Info.TypeObj &&
        Info.TypeObj->isReference()) {
      std::vector<AccessPath> targets;
      if (!Var->Init || HasError ||
          !collectActualReturnReferents(Var->Init.get(), targets))
        targets.clear();
      for (const auto &target : targets)
        Info.LifeDependencySet.insert(target.toLegacyString());
      Info.CurrentReferenceTargets = std::move(targets);
    }
    if ((m_AuthorityFactsSession || m_EnableSignatureDrivenCallCede) &&
        CurrentFunction)
      m_LocalVariableOwners[Var] = CurrentFunction;
    Info.installPartialMovePlan(admittedPartialMovePlan(Info));
    Var->PartialMove = Info.partialMovePlan();
    initializeProjectionFacts(Info);
    CurrentScope->define(Var->Name, Info);
    Var->ResolvedBindingID = makeAccessPath(Var->Name).RootID;
    if (Var->Init) {
      auto path = makeAccessPath(Var->Name);
      if (!HasError) recordEnumBinding(path, Var->Init.get());
      if (!HasError) recordNullStorageBinding(path, Var->Init.get());
      recordRawAddressBinding(path, Var->Init.get());
      if (!HasError) recordNativeSyncBinding(path, Var->Init.get(), true);
      if (!HasError) recordNativeSyncOwnerRecipe(path, Var->Init.get(), true);
      if (!HasError) recordNativeSyncGuardBinding(path, Var->Init.get());
    }
    if (!Info.ConditionalTodoIds.empty()) {
      SemanticEvidence::recordConditionalFact(
          Var->Name, Info.TypeObj ? Info.TypeObj->toString() : Var->TypeName,
          std::vector<uint64_t>(Info.ConditionalTodoIds.begin(),
                                Info.ConditionalTodoIds.end()),
          Var->Loc);
    }

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
            if (!hasPlaceState(SourceInfoPtr->placeFact(),
                               PlaceState::Moved)) {
              if (SourceInfoPtr->IsFunctionParameter &&
                  !SourceInfoPtr->IsCeded) {
                error(Var, DiagID::ERR_SEMA_DIRECT_MOVE_NON_CEDE_PARAMETER,
                      actName);
              } else {
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
    bindingTransfer.complete();
  } else if (auto *Destruct = dynamic_cast<DestructuringDecl *>(S)) {
    auto initType = checkExpr(Destruct->Init.get());
    PermissionFlow initFlow = getPermissionFlow(Destruct->Init.get());
    AccessCapability initCapability = initFlow.DirectCapability;
    if (initFlow.Kind == PermissionFlowKind::Shared)
      initCapability.PayloadFlowRestricted = true;
    const std::string initPath = getPathString(Destruct->Init.get());
    const AccessPath initAccessPath = canonicalizeAccessPath(
        makeAccessPath(Destruct->Init.get()));
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
        bool hasInvalidMember = false;
        for (const auto &v : Destruct->Variables) {
          if (v.Name == "..") continue;
          if (v.FieldName != "_" && !sdMembers.count(toka::Type::stripMorphology(v.FieldName))) {
            hasInvalidMember = true;
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
        if (!hasElision && !hasInvalidMember) {
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

          const std::string encapField = toka::Type::stripMorphology(
              SD->Members[memberIndex].Name);
          if (!canNameEncapField(SD, encapField, getLoc(Destruct))) {
            error(Destruct, DiagID::ERR_MEMBER_PRIVATE, encapField,
                  SD->Name);
          }

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
          // A destructured field's declaration is its authority.  Build the
          // physical type directly from that declaration rather than using a
          // cache as the permission source.
          auto baseTypeObj = getPhysicalType(SD->Members[memberIndex]);
          AccessCapability fieldCapability =
              deriveDestructureFieldCapability(initCapability, baseTypeObj);
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

          if (!Destruct->Variables[i].IsReference &&
              !Info.TypeObj->isPointer() && !Info.TypeObj->isReference() &&
              !Info.TypeObj->isSmartPointer()) {
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
          // The destructured binding has its own declared handle/payload
          // permission.  Later expression checks must consult that
          // declaration, then intersect it with the direct-source flow
          // ceiling below; leaving Permission at its default would silently
          // erase an explicit binding such as `&view#`.
          Info.Permission = Destruct->Variables[i].Permission;
          Info.IsDeclaredMutable = Destruct->Variables[i].IsValueMutable;
          if (fieldCapability.PayloadFlowRestricted) {
            Info.PayloadFlowWritable = fieldCapability.PayloadWritable;
            Info.HasPayloadFlowCeiling = true;
          }
          Info.DeclLoc = Destruct->Loc;

          if (Destruct->Variables[i].IsReference && initAccessPath) {
            const std::string memberName = toka::Type::stripMorphology(
                SD->Members[memberIndex].Name);
            std::string memberPath = initPath;
            if (!memberPath.empty())
              memberPath += "." + memberName;
            AccessPath memberAccessPath = initAccessPath;
            memberAccessPath.Projections.push_back(
                AccessProjection::field(memberName, Destruct->Loc));

            Info.BorrowedFrom = memberPath;
            Info.BorrowedPath = memberAccessPath;
            Info.LifeDependencySet.insert(memberPath);
            if (!PALCheckerState.recordBorrow(
                    memberAccessPath,
                    Destruct->Variables[i].IsValueMutable, Destruct->Loc)) {
              error(Destruct, DiagID::ERR_BORROW_MUT,
                    PALCheckerState.lastConflict()->displayPath());
              recordPALConflict(
                  Destruct,
                  Destruct->Variables[i].IsValueMutable
                      ? PALOperationClass::ExclusivePayloadBorrow
                      : PALOperationClass::SharedPayloadBorrow,
                  memberAccessPath, *PALCheckerState.lastConflict());
            }
            PALCheckerState.commitTransient(memberAccessPath);
          }
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
    AccessPath targetAccessPath =
        canonicalizeAccessPath(makeAccessPath(GuardBind->Target.get()));
    PermissionFlow targetFlow = getPermissionFlow(GuardBind->Target.get());
    AccessCapability targetCapability = targetFlow.DirectCapability;
    if (targetFlow.Kind == PermissionFlowKind::Shared)
      targetCapability.PayloadFlowRestricted = true;
    bool transfersOwnership =
        dynamic_cast<CedeExpr *>(GuardBind->Target.get()) != nullptr;
    // Check Pattern and bind variables into CurrentScope
    checkPattern(GuardBind->Pat.get(), targetType, targetCapability,
                 targetPath, targetAccessPath, transfersOwnership);

    bool isReceiver = false;
    if (!m_ControlFlowStack.empty()) {
      isReceiver = m_ControlFlowStack.back().IsReceiver;
    }
    m_ControlFlowStack.push_back({"", NoProducedValue, nullptr, false, isReceiver});
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
