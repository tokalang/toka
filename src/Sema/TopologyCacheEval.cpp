// Copyright (c) 2025 YiZhonghua<zhyi@dpai.com>. All rights reserved.
#include "toka/TopologyCacheEval.h"
#include "toka/Parser.h"
#include "toka/Sema.h"
#include "toka/ModuleResolver.h"
#include "toka/AssignmentStats.h"
#include "toka/DiagnosticEngine.h"
#include "toka/SourceManager.h"
#include "toka/PathUtils.h"
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
static std::shared_ptr<Type> resolveTypeB1(
    const Expr *E,
    const std::map<std::string, ShapeDecl *> &shapeMap,
    int &nodesVisited,
    uint64_t &fallbacks,
    bool countFallbacks) {
  if (!E) return nullptr;
  nodesVisited++;

  if (auto *V = dynamic_cast<const VariableExpr *>(E)) {
    return V->ResolvedType;
  }
  if (auto *M = dynamic_cast<const MemberExpr *>(E)) {
    auto baseTy = resolveTypeB1(M->Object.get(), shapeMap, nodesVisited, fallbacks, countFallbacks);
    if (!baseTy) {
      if (countFallbacks) fallbacks++;
      return M->ResolvedType;
    }

    auto soulTy = baseTy->getSoulType();
    if (!soulTy) {
      if (countFallbacks) fallbacks++;
      return M->ResolvedType;
    }

    std::string baseName = soulTy->getSoulName();
    size_t angle = baseName.find('<');
    if (angle != std::string::npos) {
      baseName = baseName.substr(0, angle);
    }

    auto it = shapeMap.find(baseName);
    if (it != shapeMap.end()) {
      ShapeDecl *SD = it->second;
      for (const auto &member : SD->Members) {
        if (member.Name == M->Member) {
          return member.ResolvedType;
        }
      }
    }
    if (countFallbacks) fallbacks++;
    return M->ResolvedType;
  }
  if (auto *Idx = dynamic_cast<const ArrayIndexExpr *>(E)) {
    auto arrayTy = resolveTypeB1(Idx->Array.get(), shapeMap, nodesVisited, fallbacks, countFallbacks);
    for (const auto &idx : Idx->Indices) {
      int dummy = 0;
      resolveTypeB1(idx.get(), shapeMap, dummy, fallbacks, countFallbacks);
      nodesVisited += dummy;
    }
    if (arrayTy) {
      auto soulTy = arrayTy->getSoulType();
      if (soulTy && soulTy->isArray()) {
        return soulTy->getArrayElementType();
      }
    }
    if (countFallbacks) fallbacks++;
    return Idx->ResolvedType;
  }
  if (auto *Deref = dynamic_cast<const DereferenceExpr *>(E)) {
    auto childTy = resolveTypeB1(Deref->Expression.get(), shapeMap, nodesVisited, fallbacks, countFallbacks);
    if (childTy) {
      auto soulTy = childTy->getSoulType();
      if (soulTy && (soulTy->isPointer() || soulTy->isRawPointer() || soulTy->isSmartPointer() || soulTy->isReference())) {
        return soulTy->getPointeeType();
      }
    }
    if (countFallbacks) fallbacks++;
    return Deref->ResolvedType;
  }
  if (auto *Un = dynamic_cast<const UnaryExpr *>(E)) {
    auto childTy = resolveTypeB1(Un->RHS.get(), shapeMap, nodesVisited, fallbacks, countFallbacks);
    if (Un->Op == TokenType::Star) {
      if (childTy) {
        auto soulTy = childTy->getSoulType();
        if (soulTy && (soulTy->isPointer() || soulTy->isRawPointer() || soulTy->isSmartPointer() || soulTy->isReference())) {
          return soulTy->getPointeeType();
        }
      }
      if (countFallbacks) fallbacks++;
    }
    return Un->ResolvedType;
  }
  if (auto *C = dynamic_cast<const CastExpr *>(E)) {
    resolveTypeB1(C->Expression.get(), shapeMap, nodesVisited, fallbacks, countFallbacks);
    return C->ResolvedType;
  }
  return E->ResolvedType;
}

class TopologyCacheSimulator {
public:
  TopologyCacheMetrics &metrics;
  int baselineNum; // 0, 1, 2, or 3
  bool countingEnabled = true;
  bool cacheValid = false;
  const std::map<std::string, ShapeDecl *> &shapeMap;

  TopologyCacheSimulator(
      TopologyCacheMetrics &m,
      int baseline,
      const std::map<std::string, ShapeDecl *> &shapes)
      : metrics(m), baselineNum(baseline), shapeMap(shapes) {}

  void triggerQuery() {
    if (!countingEnabled) return;
    metrics.topologyQueries++;
    if (cacheValid) {
      metrics.validCacheHits++;
    } else {
      metrics.cacheRecomputations++;
      cacheValid = true;
    }
  }

  void handleAssignment(
      bool isHandle,
      bool isB0PayloadInval = false,
      bool isB13SelfRebind = false,
      bool isB3Unknown = false) {
    if (!countingEnabled) {
      if (baselineNum == 0 || isHandle) {
        cacheValid = false;
      }
      return;
    }

    metrics.assignmentTransferVisits++;
    if (baselineNum == 0) {
      // B0: Always invalidate on every assignment
      if (cacheValid) {
        cacheValid = false;
        metrics.invalidations++;
        if (isB0PayloadInval) {
          metrics.payloadConservativeInvalidations++;
        }
      }
    } else {
      // B1-B3: Invalidate only on handle assignment
      if (isHandle) {
        if (cacheValid) {
          cacheValid = false;
          metrics.invalidations++;
          if (isB13SelfRebind) {
            metrics.syntacticSelfRebindings++;
          }
          if (isB3Unknown) {
            metrics.unknownConservativeInvalidations++;
          }
        }
      } else {
        // Payload assignment
        if (cacheValid) {
          metrics.retainedThroughPayload++;
        }
      }
    }
  }

  void traverseStmt(Stmt *S) {
    if (!S) return;
    if (auto *B = dynamic_cast<BlockStmt *>(S)) {
      for (auto &stmt : B->Statements) {
        traverseStmt(stmt.get());
      }
    } else if (auto *InitBlock = dynamic_cast<InitBlockStmt *>(S)) {
      traverseStmt(InitBlock->Body.get());
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
      }
    } else if (auto *Destruct = dynamic_cast<DestructuringDecl *>(S)) {
      if (Destruct->Init) {
        traverseExpr(Destruct->Init.get());
      }
    } else if (auto *Guard = dynamic_cast<GuardBindStmt *>(S)) {
      if (Guard->Target) {
        traverseExpr(Guard->Target.get());
      }
      if (Guard->ElseBody) {
        bool saveValid = cacheValid;
        traverseStmt(Guard->ElseBody.get());
        cacheValid = saveValid;
      }
    }
  }

  void traverseExpr(Expr *E) {
    if (!E) return;

    // Check assignment binary expression
    if (auto *Bin = dynamic_cast<BinaryExpr *>(E)) {
      bool isAssign = Bin->Op == "=";
      if (isAssign) {
        // Evaluate RHS first (evaluated once)
        traverseExpr(Bin->RHS.get());

        // Perform LHS traversal for query triggers on subexpressions
        traverseLHSForQueries(Bin->LHS.get());

        // The fixed query schedule is defined by AST access forms above, not
        // by any baseline's assignment classification. B2 remains an oracle
        // only for validating transfer decisions.
        bool b2_isHandle = Bin->LHS->ResolvedType && isHandleType(Bin->LHS->ResolvedType);

        // Invalidation Baselines checking
        bool isHandle = false;
        bool isUnknown = false;
        int nodesB1 = 0;

        if (baselineNum == 1) {
          // B1: Dynamically resolve target type and count nodes visited
          auto targetTy = resolveTypeB1(
              Bin->LHS.get(), shapeMap, nodesB1, metrics.b1StructuralFallbacks, countingEnabled);
          isHandle = isHandleType(targetTy);
          if (countingEnabled) {
            metrics.nodesVisited += nodesB1;
            if (isHandle != b2_isHandle) {
              metrics.b1VsB2Mismatches++;
              if (cacheValid) {
                metrics.b1MismatchesWhileCacheValid++;
              }
              if (isHandle) {
                metrics.b1FalseHandle++;
              } else {
                metrics.b1FalsePayload++;
              }
            }
          }
        } else if (baselineNum == 2) {
          // B2: Query target AST node's cached metadata directly
          isHandle = b2_isHandle;
        } else if (baselineNum == 3) {
          // B3: Read frontend evidence
          if (countingEnabled) {
            metrics.totalBinaryAssignments++;
          }
          const auto &pres = assignmentStats().EvidencePreservationSites;
          auto it = pres.find(Bin);
          if (it != pres.end() && (it->second.Frontend == AssignmentFrontendEvidence::Payload ||
                                   it->second.Frontend == AssignmentFrontendEvidence::Handle)) {
            if (countingEnabled) {
              metrics.explicitEvidenceCount++;
            }
            isHandle = (it->second.Frontend == AssignmentFrontendEvidence::Handle);
            if (countingEnabled && (isHandle != b2_isHandle)) {
              metrics.b3ExplicitVsB2Mismatches++;
            }
          } else {
            // Missing/Residual evidence: conservatively invalidate
            isHandle = true;
            isUnknown = true;
            if (countingEnabled) {
              if (it == pres.end()) {
                metrics.b3MissingEvidenceSites++;
              } else if (it->second.Frontend == AssignmentFrontendEvidence::ResidualCompound) {
                metrics.b3ResidualCompoundSites++;
              } else {
                metrics.b3UnclassifiedSites++;
              }
              if (b2_isHandle) {
                metrics.b3UnknownHandleSites++;
              } else {
                metrics.b3UnknownPayloadSites++;
                metrics.b3ConservativeVsB2Mismatches++;
              }
            }
          }
        } else {
          // B0 check
          isHandle = b2_isHandle;
        }

        // Syntactic self-rebindings check: LHS name and RHS name are matching strings
        bool isSelfRebind = false;
        if (isHandle && Bin->LHS->toString() == Bin->RHS->toString()) {
          isSelfRebind = true;
        }

        // Perform invalidation/retention
        handleAssignment(isHandle, !isHandle, isSelfRebind, isUnknown);
        return;
      }
    }

    // Traverse MatchExpr
    if (auto *Match = dynamic_cast<MatchExpr *>(E)) {
      if (Match->Target) {
        traverseExpr(Match->Target.get());
      }
      bool initValid = cacheValid;
      bool mergedValid = true;
      bool hasArms = !Match->Arms.empty();

      for (const auto &arm : Match->Arms) {
        cacheValid = initValid;
        if (arm->Guard) {
          traverseExpr(arm->Guard.get());
        }
        if (arm->Body) {
          traverseStmt(arm->Body.get());
        }
        mergedValid = mergedValid && cacheValid;
      }

      if (hasArms) {
        cacheValid = mergedValid;
      } else {
        cacheValid = initValid;
      }
      return;
    }

    // Traverse IfExpr
    if (auto *If = dynamic_cast<IfExpr *>(E)) {
      if (If->Condition) traverseExpr(If->Condition.get());
      bool saveValid = cacheValid;

      if (If->Then) traverseStmt(If->Then.get());
      bool thenValid = cacheValid;

      cacheValid = saveValid;

      if (If->Else) traverseStmt(If->Else.get());
      bool elseValid = cacheValid;

      cacheValid = thenValid && elseValid;
      return;
    }

    // Traverse GuardExpr
    if (auto *Gd = dynamic_cast<GuardExpr *>(E)) {
      if (Gd->Condition) traverseExpr(Gd->Condition.get());
      bool saveValid = cacheValid;

      if (Gd->Then) traverseStmt(Gd->Then.get());
      bool thenValid = cacheValid;

      cacheValid = saveValid;

      if (Gd->Else) traverseStmt(Gd->Else.get());
      bool elseValid = cacheValid;

      cacheValid = thenValid && elseValid;
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
    bool saveValid = cacheValid;

    // 1. Dry run to find stable state
    countingEnabled = false;
    if (cond) traverseExpr(cond);
    if (body) traverseStmt(body);
    if (elseBody) traverseStmt(elseBody);
    countingEnabled = true;

    bool exitValid = cacheValid;

    // Stable loop header validity
    cacheValid = saveValid && exitValid;

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

  std::cout << "Trace-driven Cache-Policy Simulator starting...\n";

  GroupedMetrics total;
  uint64_t processedRoots = 0;
  uint64_t successfulRoots = 0;
  uint64_t skippedRoots = 0;

  // Process each root test file
  for (const auto &file : testFiles) {
    processedRoots++;
    toka::DiagnosticEngine::ErrorCount = 0;

    // Parse and type-check the file and its imports
    toka::SourceManager localSM;
    toka::DiagnosticEngine::init(localSM);
    toka::ModuleResolver resolver(localSM, searchPaths, pkgMap, true);
    std::vector<std::unique_ptr<toka::Module>> astModules;
    
    if (!resolver.resolveAndParse(file, astModules, "", false)) {
      skippedRoots++;
      continue;
    }

    if (astModules.empty() || toka::DiagnosticEngine::hasErrors()) {
      skippedRoots++;
      continue;
    }

    successfulRoots++;

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
      successfulRoots--;
      skippedRoots++;
      continue;
    }

    const auto &shapeMap = sema.getShapeMap();

    // Simulate baseline B0
    {
      for (const auto &ast : astModules) {
        for (const auto &fn : ast->Functions) {
          if (fn->Body) {
            TopologyCacheSimulator sim(total.B0, 0, shapeMap);
            sim.traverseStmt(fn->Body.get());
          }
        }
        for (const auto &impl : ast->Impls) {
          if (impl->TypeName.find("__Closure_") == 0 || !impl->GenericParams.empty()) {
            continue;
          }
          for (const auto &method : impl->Methods) {
            if (method->Body) {
              TopologyCacheSimulator sim(total.B0, 0, shapeMap);
              sim.traverseStmt(method->Body.get());
            }
          }
        }
      }
    }

    // Simulate baseline B1
    {
      for (const auto &ast : astModules) {
        for (const auto &fn : ast->Functions) {
          if (fn->Body) {
            TopologyCacheSimulator sim(total.B1, 1, shapeMap);
            sim.traverseStmt(fn->Body.get());
          }
        }
        for (const auto &impl : ast->Impls) {
          if (impl->TypeName.find("__Closure_") == 0 || !impl->GenericParams.empty()) {
            continue;
          }
          for (const auto &method : impl->Methods) {
            if (method->Body) {
              TopologyCacheSimulator sim(total.B1, 1, shapeMap);
              sim.traverseStmt(method->Body.get());
            }
          }
        }
      }
    }

    // Simulate baseline B2
    {
      for (const auto &ast : astModules) {
        for (const auto &fn : ast->Functions) {
          if (fn->Body) {
            TopologyCacheSimulator sim(total.B2, 2, shapeMap);
            sim.traverseStmt(fn->Body.get());
          }
        }
        for (const auto &impl : ast->Impls) {
          if (impl->TypeName.find("__Closure_") == 0 || !impl->GenericParams.empty()) {
            continue;
          }
          for (const auto &method : impl->Methods) {
            if (method->Body) {
              TopologyCacheSimulator sim(total.B2, 2, shapeMap);
              sim.traverseStmt(method->Body.get());
            }
          }
        }
      }
    }

    // Simulate baseline B3
    {
      for (const auto &ast : astModules) {
        for (const auto &fn : ast->Functions) {
          if (fn->Body) {
            TopologyCacheSimulator sim(total.B3, 3, shapeMap);
            sim.traverseStmt(fn->Body.get());
          }
        }
        for (const auto &impl : ast->Impls) {
          if (impl->TypeName.find("__Closure_") == 0 || !impl->GenericParams.empty()) {
            continue;
          }
          for (const auto &method : impl->Methods) {
            if (method->Body) {
              TopologyCacheSimulator sim(total.B3, 3, shapeMap);
              sim.traverseStmt(method->Body.get());
            }
          }
        }
      }
    }

  }

  std::cout << "Processed roots: " << processedRoots 
            << " (parsed successfully: " << successfulRoots 
            << ", skipped due to errors: " << skippedRoots << ")\n";

  std::cout << "\n=========================================================================\n";
  std::cout << "               Trace-Driven Cache-Policy Simulator Results\n";
  std::cout << "=========================================================================\n";
  std::cout << std::left 
            << std::setw(30) << "Metric" 
            << std::setw(12) << "B0" 
            << std::setw(12) << "B1" 
            << std::setw(12) << "B2" 
            << std::setw(20) << "B3 (Frontend-Evidence)" << "\n";
  std::cout << "-------------------------------------------------------------------------\n";
  std::cout << std::setw(30) << "Assignment transfer visits"
            << std::setw(12) << total.B0.assignmentTransferVisits
            << std::setw(12) << total.B1.assignmentTransferVisits
            << std::setw(12) << total.B2.assignmentTransferVisits
            << std::setw(20) << total.B3.assignmentTransferVisits << "\n";
  std::cout << std::setw(30) << "Topology Queries" 
            << std::setw(12) << total.B0.topologyQueries 
            << std::setw(12) << total.B1.topologyQueries 
            << std::setw(12) << total.B2.topologyQueries 
            << std::setw(20) << total.B3.topologyQueries << "\n";
  std::cout << std::setw(30) << "Valid Cache Hits" 
            << std::setw(12) << total.B0.validCacheHits 
            << std::setw(12) << total.B1.validCacheHits 
            << std::setw(12) << total.B2.validCacheHits 
            << std::setw(20) << total.B3.validCacheHits << "\n";
  std::cout << std::setw(30) << "Cache Recomputations" 
            << std::setw(12) << total.B0.cacheRecomputations 
            << std::setw(12) << total.B1.cacheRecomputations 
            << std::setw(12) << total.B2.cacheRecomputations 
            << std::setw(20) << total.B3.cacheRecomputations << "\n";
  std::cout << std::setw(30) << "Invalidations" 
            << std::setw(12) << total.B0.invalidations 
            << std::setw(12) << total.B1.invalidations 
            << std::setw(12) << total.B2.invalidations 
            << std::setw(20) << total.B3.invalidations << "\n";
  std::cout << std::setw(30) << "Retained-through-payload" 
            << std::setw(12) << total.B0.retainedThroughPayload 
            << std::setw(12) << total.B1.retainedThroughPayload 
            << std::setw(12) << total.B2.retainedThroughPayload 
            << std::setw(20) << total.B3.retainedThroughPayload << "\n";
  std::cout << std::setw(30) << "B0 oracle-P Valid->Invalid"
            << std::setw(12) << total.B0.payloadConservativeInvalidations 
            << std::setw(12) << total.B1.payloadConservativeInvalidations 
            << std::setw(12) << total.B2.payloadConservativeInvalidations 
            << std::setw(20) << total.B3.payloadConservativeInvalidations << "\n";
  std::cout << std::setw(30) << "Unknown Valid->Invalid"
            << std::setw(12) << total.B0.unknownConservativeInvalidations
            << std::setw(12) << total.B1.unknownConservativeInvalidations
            << std::setw(12) << total.B2.unknownConservativeInvalidations
            << std::setw(20) << total.B3.unknownConservativeInvalidations << "\n";
  std::cout << std::setw(30) << "Syntactic self-rebind inval." 
            << std::setw(12) << total.B0.syntacticSelfRebindings 
            << std::setw(12) << total.B1.syntacticSelfRebindings 
            << std::setw(12) << total.B2.syntacticSelfRebindings 
            << std::setw(20) << total.B3.syntacticSelfRebindings << "\n";
  std::cout << std::setw(30) << "AST Nodes Visited" 
            << std::setw(12) << 0 
            << std::setw(12) << total.B1.nodesVisited 
            << std::setw(12) << 0 
            << std::setw(20) << 0 << "\n";
  std::cout << "=========================================================================\n";

  // Report B3 explicit evidence coverage, mismatches, and B1 validation details
  double coveragePct = 0.0;
  if (total.B3.totalBinaryAssignments > 0) {
    coveragePct = (double)total.B3.explicitEvidenceCount * 100.0 / total.B3.totalBinaryAssignments;
  }
  std::cout << std::fixed << std::setprecision(2);
  std::cout << "B3 Evidence Coverage: " << coveragePct << "% (" 
            << total.B3.explicitEvidenceCount << " explicit out of " 
            << total.B3.totalBinaryAssignments << " evaluated simple assignments)\n";
  std::cout << "B1 vs B2 Classification Mismatches: " << total.B1.b1VsB2Mismatches << "\n";
  std::cout << "  B1 False Payload (B2 Handle): " << total.B1.b1FalsePayload << "\n";
  std::cout << "  B1 False Handle (B2 Payload): " << total.B1.b1FalseHandle << "\n";
  std::cout << "  B1 Mismatches While Cache Valid: "
            << total.B1.b1MismatchesWhileCacheValid << "\n";
  std::cout << "B1 Structural Fallbacks to Cached Type: " << total.B1.b1StructuralFallbacks << "\n";
  std::cout << "B3 (Explicit) vs B2 Mismatches: " << total.B3.b3ExplicitVsB2Mismatches << "\n";
  std::cout << "B3 Unknown Sites: "
            << (total.B3.b3UnknownPayloadSites + total.B3.b3UnknownHandleSites) << "\n";
  std::cout << "  B2 Payload: " << total.B3.b3UnknownPayloadSites << "\n";
  std::cout << "  B2 Handle: " << total.B3.b3UnknownHandleSites << "\n";
  std::cout << "  Residual compound: " << total.B3.b3ResidualCompoundSites << "\n";
  std::cout << "  Unclassified: " << total.B3.b3UnclassifiedSites << "\n";
  std::cout << "  Missing site record: " << total.B3.b3MissingEvidenceSites << "\n";
  std::cout << "B3 Unknown vs B2 Transfer Mismatches: "
            << total.B3.b3ConservativeVsB2Mismatches << "\n";
  std::cout << "=========================================================================\n";
}

} // namespace toka
