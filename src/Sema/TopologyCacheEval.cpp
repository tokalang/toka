// Copyright (c) 2025 YiZhonghua<zhyi@dpai.com>. All rights reserved.
#include "toka/TopologyCacheEval.h"
#include "toka/Parser.h"
#include "toka/Sema.h"
#include "toka/ModuleResolver.h"
#include "toka/AssignmentStats.h"
#include "toka/DiagnosticEngine.h"
#include "toka/SourceManager.h"
#include "toka/PathUtils.h"
#include <chrono>
#include <iostream>
#include <filesystem>
#include <algorithm>
#include <iomanip>

namespace fs = std::filesystem;

namespace toka {

// Helper to check if a type is a pointer/handle type
static bool isHandleType(const std::shared_ptr<Type> &type) {
  if (!type) return false;
  return type->isPointer() || type->isRawPointer() || type->isSmartPointer() || type->isReference();
}

// Counts AST nodes recursively in an expression
static int countNodes(const Expr *E) {
  if (!E) return 0;
  int count = 1;
  if (auto *Bin = dynamic_cast<const BinaryExpr *>(E)) {
    count += countNodes(Bin->LHS.get()) + countNodes(Bin->RHS.get());
  } else if (auto *Un = dynamic_cast<const UnaryExpr *>(E)) {
    count += countNodes(Un->RHS.get());
  } else if (auto *Post = dynamic_cast<const PostfixExpr *>(E)) {
    count += countNodes(Post->LHS.get());
  } else if (auto *Prop = dynamic_cast<const UnwrapPropagationExpr *>(E)) {
    count += countNodes(Prop->Base.get());
  } else if (auto *Aw = dynamic_cast<const AwaitExpr *>(E)) {
    count += countNodes(Aw->Expression.get());
  } else if (auto *Wt = dynamic_cast<const WaitExpr *>(E)) {
    count += countNodes(Wt->Expression.get());
  } else if (auto *St = dynamic_cast<const StartExpr *>(E)) {
    count += countNodes(St->Expression.get());
  } else if (auto *C = dynamic_cast<const CastExpr *>(E)) {
    count += countNodes(C->Expression.get());
  } else if (auto *Addr = dynamic_cast<const AddressOfExpr *>(E)) {
    count += countNodes(Addr->Expression.get());
  } else if (auto *Deref = dynamic_cast<const DereferenceExpr *>(E)) {
    count += countNodes(Deref->Expression.get());
  } else if (auto *M = dynamic_cast<const MemberExpr *>(E)) {
    count += countNodes(M->Object.get());
  } else if (auto *Idx = dynamic_cast<const ArrayIndexExpr *>(E)) {
    count += countNodes(Idx->Array.get());
    for (auto &idx : Idx->Indices) count += countNodes(idx.get());
  } else if (auto *Arr = dynamic_cast<const ArrayExpr *>(E)) {
    for (auto &el : Arr->Elements) count += countNodes(el.get());
  } else if (auto *Rep = dynamic_cast<const RepeatedArrayExpr *>(E)) {
    count += countNodes(Rep->Value.get()) + countNodes(Rep->Count.get());
  } else if (auto *Uns = dynamic_cast<const UnsafeExpr *>(E)) {
    count += countNodes(Uns->Expression.get());
  } else if (auto *Alloc = dynamic_cast<const AllocExpr *>(E)) {
    count += countNodes(Alloc->Initializer.get()) + countNodes(Alloc->ArraySize.get());
  } else if (auto *InitSt = dynamic_cast<const InitStructExpr *>(E)) {
    for (auto &m : InitSt->Members) count += countNodes(m.second.get());
  } else if (auto *Anon = dynamic_cast<const AnonymousRecordExpr *>(E)) {
    for (auto &f : Anon->Fields) count += countNodes(f.second.get());
  } else if (auto *Call = dynamic_cast<const CallExpr *>(E)) {
    for (auto &arg : Call->Args) count += countNodes(arg.get());
  } else if (auto *Meth = dynamic_cast<const MethodCallExpr *>(E)) {
    count += countNodes(Meth->Object.get());
    for (auto &arg : Meth->Args) count += countNodes(arg.get());
  } else if (auto *Pass = dynamic_cast<const PassExpr *>(E)) {
    count += countNodes(Pass->Value.get());
  } else if (auto *Cede = dynamic_cast<const CedeExpr *>(E)) {
    count += countNodes(Cede->Value.get());
  } else if (auto *Match = dynamic_cast<const MatchExpr *>(E)) {
    count += countNodes(Match->Target.get());
    count += Match->Arms.size();
  } else if (auto *If = dynamic_cast<const IfExpr *>(E)) {
    count += countNodes(If->Condition.get());
  } else if (auto *Gd = dynamic_cast<const GuardExpr *>(E)) {
    count += countNodes(Gd->Condition.get());
  } else if (auto *Lp = dynamic_cast<const LoopExpr *>(E)) {
    count += countNodes(Lp->Condition.get());
  } else if (auto *For = dynamic_cast<const ForExpr *>(E)) {
    count += countNodes(For->Collection.get());
  }
  return count;
}

// Recovers/resolves type dynamically from the LHS AST tree, matching B1.
// Also counts AST nodes visited during this resolution.
static std::shared_ptr<Type> resolveTypeB1(const Expr *E, int &nodesVisited) {
  if (!E) return nullptr;
  nodesVisited++;

  if (auto *V = dynamic_cast<const VariableExpr *>(E)) {
    // VariableExpr is resolved from its type directly
    return V->ResolvedType;
  }
  if (auto *M = dynamic_cast<const MemberExpr *>(E)) {
    return M->ResolvedType;
  }
  if (auto *Idx = dynamic_cast<const ArrayIndexExpr *>(E)) {
    return Idx->ResolvedType;
  }
  if (auto *Deref = dynamic_cast<const DereferenceExpr *>(E)) {
    return Deref->ResolvedType;
  }
  if (auto *Un = dynamic_cast<const UnaryExpr *>(E)) {
    return Un->ResolvedType;
  }
  if (auto *C = dynamic_cast<const CastExpr *>(E)) {
    return C->ResolvedType;
  }
  return E->ResolvedType;
}

class TopologyCacheSimulator {
public:
  TopologyCacheMetrics &metrics;
  int baselineNum; // 0, 1, 2, or 3
  bool countingEnabled = true;

  TopologyCacheSimulator(TopologyCacheMetrics &m, int baseline)
      : metrics(m), baselineNum(baseline) {}

  void triggerQuery() {
    if (!countingEnabled) return;
    metrics.topologyQueries++;
    if (metrics.cacheValid) {
      metrics.validCacheHits++;
    } else {
      metrics.cacheRecomputations++;
      metrics.cacheValid = true;
    }
  }

  void handleAssignment(bool isHandle, bool isSilent, int nodesB1) {
    if (baselineNum == 0) {
      // B0: Always invalidate on every assignment
      if (metrics.cacheValid) {
        metrics.cacheValid = false;
        metrics.invalidations++;
        if (!isHandle) {
          metrics.extensionalFalseInvalidations++;
        }
      }
    } else {
      // B1-B3: Invalidate only on handle assignment
      if (isHandle) {
        if (metrics.cacheValid) {
          metrics.cacheValid = false;
          metrics.invalidations++;
          if (isSilent) {
            metrics.extensionalFalseInvalidations++;
          }
        }
      } else {
        // Payload assignment
        if (metrics.cacheValid) {
          metrics.retainedThroughPayload++;
        }
      }
      if (baselineNum == 1 && countingEnabled) {
        metrics.nodesVisited += nodesB1;
      }
    }
  }

  void traverseStmt(Stmt *S) {
    if (!S) return;
    if (auto *B = dynamic_cast<BlockStmt *>(S)) {
      for (auto &stmt : B->Statements) {
        traverseStmt(stmt.get());
      }
    } else if (auto *R = dynamic_cast<ReturnStmt *>(S)) {
      traverseExpr(R->ReturnValue.get());
    } else if (auto *E = dynamic_cast<ExprStmt *>(S)) {
      traverseExpr(E->Expression.get());
    } else if (auto *D = dynamic_cast<DeleteStmt *>(S)) {
      traverseExpr(D->Expression.get());
    } else if (auto *U = dynamic_cast<UnsafeStmt *>(S)) {
      traverseStmt(U->Statement.get());
    } else if (auto *F = dynamic_cast<FreeStmt *>(S)) {
      traverseExpr(F->Expression.get());
      if (F->Count) traverseExpr(F->Count.get());
    } else if (auto *Var = dynamic_cast<VariableDecl *>(S)) {
      if (Var->Init) {
        traverseExpr(Var->Init.get());
        bool isHandle = isHandleType(Var->ResolvedType);
        handleAssignment(isHandle, /*isSilent=*/false, /*nodesB1=*/countNodes(Var->Init.get()) + 1);
      }
    } else if (auto *Destruct = dynamic_cast<DestructuringDecl *>(S)) {
      if (Destruct->Init) {
        traverseExpr(Destruct->Init.get());
        bool isHandle = false;
        for (const auto &v : Destruct->Variables) {
          if (v.IsReference || v.Name.find('^') != std::string::npos ||
              v.Name.find('~') != std::string::npos || v.Name.find('&') != std::string::npos ||
              v.Name.find('*') != std::string::npos) {
            isHandle = true;
            break;
          }
        }
        if (!isHandle && Destruct->Init->ResolvedType) {
          isHandle = isHandleType(Destruct->Init->ResolvedType);
        }
        handleAssignment(isHandle, /*isSilent=*/false, /*nodesB1=*/countNodes(Destruct->Init.get()) + 1);
      }
    } else if (auto *Guard = dynamic_cast<GuardBindStmt *>(S)) {
      if (Guard->Target) {
        traverseExpr(Guard->Target.get());
        bool isHandle = isHandleType(Guard->Target->ResolvedType);
        handleAssignment(isHandle, /*isSilent=*/false, /*nodesB1=*/countNodes(Guard->Target.get()) + 1);
      }
      if (Guard->ElseBody) {
        bool saveValid = metrics.cacheValid;
        traverseStmt(Guard->ElseBody.get());
        metrics.cacheValid = saveValid;
      }
    }
  }

  void traverseExpr(Expr *E) {
    if (!E) return;

    // Check assignment binary expression
    if (auto *Bin = dynamic_cast<BinaryExpr *>(E)) {
      bool isAssign = (Bin->Op == "=" || Bin->Op == "+=" || Bin->Op == "-=" ||
                       Bin->Op == "*=" || Bin->Op == "/=" || Bin->Op == "%=");
      if (isAssign) {
        // Evaluate RHS first (evaluated once)
        traverseExpr(Bin->RHS.get());

        // Perform LHS traversal for query triggers on subexpressions
        traverseLHSForQueries(Bin->LHS.get());

        // Invalidation Baselines checking
        bool isHandle = false;
        bool isSilent = false;
        int nodesB1 = 0;

        if (baselineNum == 1) {
          // B1: Dynamically resolve target write location's type and count nodes visited
          auto targetTy = resolveTypeB1(Bin->LHS.get(), nodesB1);
          isHandle = isHandleType(targetTy);
        } else if (baselineNum == 2) {
          // B2: Query target AST node's cached metadata directly
          isHandle = Bin->LHS->ResolvedType && isHandleType(Bin->LHS->ResolvedType);
        } else if (baselineNum == 3) {
          // B3: Read the evidence carrier
          const auto &pres = assignmentStats().EvidencePreservationSites;
          auto it = pres.find(Bin);
          if (it != pres.end() && it->second.Frontend == AssignmentFrontendEvidence::Handle) {
            isHandle = true;
          } else {
            isHandle = Bin->LHS->ResolvedType && isHandleType(Bin->LHS->ResolvedType);
          }
        } else {
          // B0 check
          isHandle = Bin->LHS->ResolvedType && isHandleType(Bin->LHS->ResolvedType);
        }

        // Silent handle assignment: e.g. p = p
        if (isHandle && Bin->LHS->toString() == Bin->RHS->toString()) {
          isSilent = true;
        }

        // Trigger query on the LHS handle if it is a handle write
        if (isHandle) {
          triggerQuery();
        }

        // Perform invalidation/retention
        handleAssignment(isHandle, isSilent, nodesB1 + countNodes(Bin->RHS.get()) + 1);
        return;
      }
    }

    // Traverse MatchExpr
    if (auto *Match = dynamic_cast<MatchExpr *>(E)) {
      if (Match->Target) {
        traverseExpr(Match->Target.get());
      }
      bool isHandleTarget = Match->Target && Match->Target->ResolvedType && isHandleType(Match->Target->ResolvedType);
      bool initValid = metrics.cacheValid;
      bool mergedValid = true;
      bool hasArms = !Match->Arms.empty();

      for (const auto &arm : Match->Arms) {
        metrics.cacheValid = initValid;
        if (isHandleTarget) {
          handleAssignment(true, false, Match->Target ? countNodes(Match->Target.get()) : 1);
        }
        if (arm->Guard) {
          traverseExpr(arm->Guard.get());
        }
        if (arm->Body) {
          traverseStmt(arm->Body.get());
        }
        mergedValid = mergedValid && metrics.cacheValid;
      }

      if (hasArms) {
        metrics.cacheValid = mergedValid;
      } else {
        metrics.cacheValid = initValid;
      }
      return;
    }

    // Traverse IfExpr
    if (auto *If = dynamic_cast<IfExpr *>(E)) {
      if (If->Condition) traverseExpr(If->Condition.get());
      bool saveValid = metrics.cacheValid;

      if (If->Then) traverseStmt(If->Then.get());
      bool thenValid = metrics.cacheValid;

      metrics.cacheValid = saveValid;

      if (If->Else) traverseStmt(If->Else.get());
      bool elseValid = metrics.cacheValid;

      metrics.cacheValid = thenValid && elseValid;
      return;
    }

    // Traverse GuardExpr
    if (auto *Gd = dynamic_cast<GuardExpr *>(E)) {
      if (Gd->Condition) traverseExpr(Gd->Condition.get());
      bool saveValid = metrics.cacheValid;

      if (Gd->Then) traverseStmt(Gd->Then.get());
      bool thenValid = metrics.cacheValid;

      metrics.cacheValid = saveValid;

      if (Gd->Else) traverseStmt(Gd->Else.get());
      bool elseValid = metrics.cacheValid;

      metrics.cacheValid = thenValid && elseValid;
      return;
    }

    // Traverse LoopExpr
    if (auto *Lp = dynamic_cast<LoopExpr *>(E)) {
      traverseLoop(Lp->Condition.get(), Lp->Body.get());
      return;
    }

    // Traverse ForExpr
    if (auto *For = dynamic_cast<ForExpr *>(E)) {
      if (For->Collection) traverseExpr(For->Collection.get());
      traverseLoop(nullptr, For->Body.get(), For->ElseBody.get());
      return;
    }

    // General subexpression traversal
    traverseChildren(E);

    // Check if E is a query trigger point
    bool isQuery = false;
    if (dynamic_cast<DereferenceExpr *>(E)) {
      isQuery = true;
    } else if (auto *V = dynamic_cast<VariableExpr *>(E)) {
      if (V->ResolvedType && isHandleType(V->ResolvedType)) {
        isQuery = true;
      }
    } else if (auto *M = dynamic_cast<MemberExpr *>(E)) {
      if (M->Object && M->Object->ResolvedType && isHandleType(M->Object->ResolvedType)) {
        isQuery = true; // Implicit dereference
      } else if (M->ResolvedType && isHandleType(M->ResolvedType)) {
        isQuery = true; // Handle field read/write
      }
    } else if (auto *Idx = dynamic_cast<ArrayIndexExpr *>(E)) {
      if (Idx->Array && Idx->Array->ResolvedType && isHandleType(Idx->Array->ResolvedType)) {
        isQuery = true; // Implicit dereference
      } else if (Idx->ResolvedType && isHandleType(Idx->ResolvedType)) {
        isQuery = true; // Handle element read
      }
    } else if (auto *U = dynamic_cast<UnaryExpr *>(E)) {
      if (U->Op == TokenType::Star) {
        isQuery = true;
      }
    } else if (dynamic_cast<AddressOfExpr *>(E)) {
      isQuery = true; // Handle creation/address-of
    }

    if (isQuery) {
      triggerQuery();
    }
  }

private:
  void traverseLHSForQueries(Expr *lhs) {
    if (!lhs) return;
    if (auto *V = dynamic_cast<VariableExpr *>(lhs)) {
      // Direct variable write target: no queries triggered on it
    } else if (auto *D = dynamic_cast<DereferenceExpr *>(lhs)) {
      traverseExpr(D->Expression.get());
      triggerQuery();
    } else if (auto *M = dynamic_cast<MemberExpr *>(lhs)) {
      traverseExpr(M->Object.get());
      if (M->Object->ResolvedType && isHandleType(M->Object->ResolvedType)) {
        triggerQuery();
      }
    } else if (auto *Idx = dynamic_cast<ArrayIndexExpr *>(lhs)) {
      traverseExpr(Idx->Array.get());
      for (auto &idx : Idx->Indices) traverseExpr(idx.get());
      if (Idx->Array->ResolvedType && isHandleType(Idx->Array->ResolvedType)) {
        triggerQuery();
      }
    } else if (auto *U = dynamic_cast<UnaryExpr *>(lhs)) {
      if (U->Op == TokenType::Star) {
        traverseExpr(U->RHS.get());
        triggerQuery();
      } else {
        traverseExpr(U->RHS.get());
      }
    } else {
      traverseExpr(lhs);
    }
  }

  void traverseChildren(Expr *E) {
    if (auto *Bin = dynamic_cast<BinaryExpr *>(E)) {
      traverseExpr(Bin->LHS.get());
      traverseExpr(Bin->RHS.get());
    } else if (auto *Un = dynamic_cast<UnaryExpr *>(E)) {
      traverseExpr(Un->RHS.get());
    } else if (auto *Post = dynamic_cast<PostfixExpr *>(E)) {
      traverseExpr(Post->LHS.get());
    } else if (auto *Prop = dynamic_cast<UnwrapPropagationExpr *>(E)) {
      traverseExpr(Prop->Base.get());
    } else if (auto *Aw = dynamic_cast<AwaitExpr *>(E)) {
      traverseExpr(Aw->Expression.get());
    } else if (auto *Wt = dynamic_cast<WaitExpr *>(E)) {
      traverseExpr(Wt->Expression.get());
    } else if (auto *St = dynamic_cast<StartExpr *>(E)) {
      traverseExpr(St->Expression.get());
    } else if (auto *C = dynamic_cast<CastExpr *>(E)) {
      traverseExpr(C->Expression.get());
    } else if (auto *Addr = dynamic_cast<AddressOfExpr *>(E)) {
      traverseExpr(Addr->Expression.get());
    } else if (auto *Deref = dynamic_cast<DereferenceExpr *>(E)) {
      traverseExpr(Deref->Expression.get());
    } else if (auto *M = dynamic_cast<MemberExpr *>(E)) {
      traverseExpr(M->Object.get());
    } else if (auto *Idx = dynamic_cast<ArrayIndexExpr *>(E)) {
      traverseExpr(Idx->Array.get());
      for (auto &idx : Idx->Indices) traverseExpr(idx.get());
    } else if (auto *Arr = dynamic_cast<ArrayExpr *>(E)) {
      for (auto &el : Arr->Elements) traverseExpr(el.get());
    } else if (auto *Rep = dynamic_cast<RepeatedArrayExpr *>(E)) {
      traverseExpr(Rep->Value.get());
      traverseExpr(Rep->Count.get());
    } else if (auto *Uns = dynamic_cast<UnsafeExpr *>(E)) {
      traverseExpr(Uns->Expression.get());
    } else if (auto *Alloc = dynamic_cast<AllocExpr *>(E)) {
      if (Alloc->Initializer) traverseExpr(Alloc->Initializer.get());
      if (Alloc->ArraySize) traverseExpr(Alloc->ArraySize.get());
    } else if (auto *InitSt = dynamic_cast<InitStructExpr *>(E)) {
      for (auto &m : InitSt->Members) traverseExpr(m.second.get());
    } else if (auto *Anon = dynamic_cast<AnonymousRecordExpr *>(E)) {
      for (auto &f : Anon->Fields) traverseExpr(f.second.get());
    } else if (auto *Call = dynamic_cast<CallExpr *>(E)) {
      for (auto &arg : Call->Args) traverseExpr(arg.get());
    } else if (auto *Meth = dynamic_cast<MethodCallExpr *>(E)) {
      traverseExpr(Meth->Object.get());
      for (auto &arg : Meth->Args) traverseExpr(arg.get());
    } else if (auto *Pass = dynamic_cast<PassExpr *>(E)) {
      traverseExpr(Pass->Value.get());
    } else if (auto *Cede = dynamic_cast<CedeExpr *>(E)) {
      traverseExpr(Cede->Value.get());
    }
  }

  void traverseLoop(Expr *cond, Stmt *body, Stmt *elseBody = nullptr) {
    bool saveValid = metrics.cacheValid;

    // 1. Dry run to find stable state
    countingEnabled = false;
    if (cond) traverseExpr(cond);
    if (body) traverseStmt(body);
    if (elseBody) traverseStmt(elseBody);
    countingEnabled = true;

    bool exitValid = metrics.cacheValid;

    // Stable loop header validity
    metrics.cacheValid = saveValid && exitValid;

    // 2. Actual run with counting enabled
    if (cond) traverseExpr(cond);
    if (body) traverseStmt(body);
    if (elseBody) traverseStmt(elseBody);
  }
};

void runTopologyCacheEvaluation(
    const std::vector<std::string> &testFiles,
    const std::vector<std::string> &searchPaths,
    const std::map<std::string, std::string> &pkgMap) {

  std::cout << "Starting Topology Cache Evaluation over " << testFiles.size() << " tests...\n";

  GroupedMetrics total;
  double wallTimeB0 = 0.0;
  double wallTimeB1 = 0.0;
  double wallTimeB2 = 0.0;
  double wallTimeB3 = 0.0;

  uint64_t totalAssignmentSites = 0;

  // Process each root test file
  for (const auto &file : testFiles) {
    toka::DiagnosticEngine::ErrorCount = 0;

    // Parse and type-check the file and its imports
    toka::SourceManager localSM;
    toka::DiagnosticEngine::init(localSM);
    toka::ModuleResolver resolver(localSM, searchPaths, pkgMap, true);
    std::vector<std::unique_ptr<toka::Module>> astModules;
    
    if (!resolver.resolveAndParse(file, astModules, "", false)) {
      continue;
    }

    if (astModules.empty() || toka::DiagnosticEngine::hasErrors()) {
      continue;
    }

    // Run Semantic Analysis
    toka::Sema sema;
    sema.setBorrowCheckEnabled(true);
    toka::enableAssignmentStats(true);

    for (const auto &ast : astModules) {
      sema.declareGlobals(*ast);
    }
    for (const auto &ast : astModules) {
      sema.checkModule(*ast);
    }
    sema.checkShapeSovereignty();

    if (toka::DiagnosticEngine::hasErrors()) {
      continue;
    }

    // Simulate baseline B0
    {
      auto start = std::chrono::steady_clock::now();
      for (const auto &ast : astModules) {
        for (const auto &fn : ast->Functions) {
          if (fn->Body) {
            TopologyCacheSimulator sim(total.B0, 0);
            sim.traverseStmt(fn->Body.get());
          }
        }
        for (const auto &impl : ast->Impls) {
          if (impl->TypeName.find("__Closure_") == 0 || !impl->GenericParams.empty()) {
            continue;
          }
          for (const auto &method : impl->Methods) {
            if (method->Body) {
              TopologyCacheSimulator sim(total.B0, 0);
              sim.traverseStmt(method->Body.get());
            }
          }
        }
      }
      auto end = std::chrono::steady_clock::now();
      std::chrono::duration<double, std::milli> diff = end - start;
      wallTimeB0 += diff.count();
    }

    // Simulate baseline B1
    {
      auto start = std::chrono::steady_clock::now();
      for (const auto &ast : astModules) {
        for (const auto &fn : ast->Functions) {
          if (fn->Body) {
            TopologyCacheSimulator sim(total.B1, 1);
            sim.traverseStmt(fn->Body.get());
          }
        }
        for (const auto &impl : ast->Impls) {
          if (impl->TypeName.find("__Closure_") == 0 || !impl->GenericParams.empty()) {
            continue;
          }
          for (const auto &method : impl->Methods) {
            if (method->Body) {
              TopologyCacheSimulator sim(total.B1, 1);
              sim.traverseStmt(method->Body.get());
            }
          }
        }
      }
      auto end = std::chrono::steady_clock::now();
      std::chrono::duration<double, std::milli> diff = end - start;
      wallTimeB1 += diff.count();
    }

    // Simulate baseline B2
    {
      auto start = std::chrono::steady_clock::now();
      for (const auto &ast : astModules) {
        for (const auto &fn : ast->Functions) {
          if (fn->Body) {
            TopologyCacheSimulator sim(total.B2, 2);
            sim.traverseStmt(fn->Body.get());
          }
        }
        for (const auto &impl : ast->Impls) {
          if (impl->TypeName.find("__Closure_") == 0 || !impl->GenericParams.empty()) {
            continue;
          }
          for (const auto &method : impl->Methods) {
            if (method->Body) {
              TopologyCacheSimulator sim(total.B2, 2);
              sim.traverseStmt(method->Body.get());
            }
          }
        }
      }
      auto end = std::chrono::steady_clock::now();
      std::chrono::duration<double, std::milli> diff = end - start;
      wallTimeB2 += diff.count();
    }

    // Simulate baseline B3
    {
      auto start = std::chrono::steady_clock::now();
      for (const auto &ast : astModules) {
        for (const auto &fn : ast->Functions) {
          if (fn->Body) {
            TopologyCacheSimulator sim(total.B3, 3);
            sim.traverseStmt(fn->Body.get());
          }
        }
        for (const auto &impl : ast->Impls) {
          if (impl->TypeName.find("__Closure_") == 0 || !impl->GenericParams.empty()) {
            continue;
          }
          for (const auto &method : impl->Methods) {
            if (method->Body) {
              TopologyCacheSimulator sim(total.B3, 3);
              sim.traverseStmt(method->Body.get());
            }
          }
        }
      }
      auto end = std::chrono::steady_clock::now();
      std::chrono::duration<double, std::milli> diff = end - start;
      wallTimeB3 += diff.count();
    }

    totalAssignmentSites += toka::assignmentStats().TotalAssignmentSites;
  }

  // Calculate memory metadata overhead
  uint64_t overheadB0 = 0;
  uint64_t overheadB1 = 0;
  uint64_t overheadB2 = totalAssignmentSites * 8;  // 8 bytes per site for type pointer reference
  uint64_t overheadB3 = totalAssignmentSites * 16; // 16 bytes per site for evidence Preserved entries

  std::cout << "\n=========================================================================\n";
  std::cout << "                        Topology Cache Evaluation Results\n";
  std::cout << "=========================================================================\n";
  std::cout << std::left 
            << std::setw(30) << "Metric" 
            << std::setw(12) << "B0" 
            << std::setw(12) << "B1" 
            << std::setw(12) << "B2" 
            << std::setw(12) << "B3" << "\n";
  std::cout << "-------------------------------------------------------------------------\n";
  std::cout << std::setw(30) << "Topology Queries" 
            << std::setw(12) << total.B0.topologyQueries 
            << std::setw(12) << total.B1.topologyQueries 
            << std::setw(12) << total.B2.topologyQueries 
            << std::setw(12) << total.B3.topologyQueries << "\n";
  std::cout << std::setw(30) << "Valid Cache Hits" 
            << std::setw(12) << total.B0.validCacheHits 
            << std::setw(12) << total.B1.validCacheHits 
            << std::setw(12) << total.B2.validCacheHits 
            << std::setw(12) << total.B3.validCacheHits << "\n";
  std::cout << std::setw(30) << "Cache Recomputations" 
            << std::setw(12) << total.B0.cacheRecomputations 
            << std::setw(12) << total.B1.cacheRecomputations 
            << std::setw(12) << total.B2.cacheRecomputations 
            << std::setw(12) << total.B3.cacheRecomputations << "\n";
  std::cout << std::setw(30) << "Invalidations" 
            << std::setw(12) << total.B0.invalidations 
            << std::setw(12) << total.B1.invalidations 
            << std::setw(12) << total.B2.invalidations 
            << std::setw(12) << total.B3.invalidations << "\n";
  std::cout << std::setw(30) << "Retained-through-payload" 
            << std::setw(12) << total.B0.retainedThroughPayload 
            << std::setw(12) << total.B1.retainedThroughPayload 
            << std::setw(12) << total.B2.retainedThroughPayload 
            << std::setw(12) << total.B3.retainedThroughPayload << "\n";
  std::cout << std::setw(30) << "Extensional False Inval." 
            << std::setw(12) << total.B0.extensionalFalseInvalidations 
            << std::setw(12) << total.B1.extensionalFalseInvalidations 
            << std::setw(12) << total.B2.extensionalFalseInvalidations 
            << std::setw(12) << total.B3.extensionalFalseInvalidations << "\n";
  std::cout << std::setw(30) << "AST Nodes Visited" 
            << std::setw(12) << 0 
            << std::setw(12) << total.B1.nodesVisited 
            << std::setw(12) << 0 
            << std::setw(12) << 0 << "\n";
  std::cout << std::fixed << std::setprecision(2);
  std::cout << std::setw(30) << "Analysis Wall Time (ms)" 
            << std::setw(12) << wallTimeB0 
            << std::setw(12) << wallTimeB1 
            << std::setw(12) << wallTimeB2 
            << std::setw(12) << wallTimeB3 << "\n";
  std::cout << std::setw(30) << "Metadata Overhead (bytes)" 
            << std::setw(12) << overheadB0 
            << std::setw(12) << overheadB1 
            << std::setw(12) << overheadB2 
            << std::setw(12) << overheadB3 << "\n";
  std::cout << "=========================================================================\n";
}

} // namespace toka
