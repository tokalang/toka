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
#include "toka/PathUtils.h"
#include "toka/Parser.h"
#include "toka/Sema.h"
#include "toka/SourceManager.h"
#include "toka/Type.h"
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace toka {

[[maybe_unused]] static SourceLocation getLoc(ASTNode *Node) { return Node->Loc; }

namespace {

uint64_t closureIdentityHash(const std::string &identity) {
  uint64_t hash = 14695981039346656037ULL;
  for (unsigned char c : identity) {
    hash ^= c;
    hash *= 1099511628211ULL;
  }
  return hash;
}

std::string closureModulePath(const ClosureExpr *closure,
                              const Module *currentModule) {
  if (closure && closure->Loc.isValid() && DiagnosticEngine::SrcMgr) {
    auto fullLoc = DiagnosticEngine::SrcMgr->getFullSourceLoc(closure->Loc);
    if (fullLoc.isValid())
      return PathUtils::canonicalize(fullLoc.FileName);
  }

  if (currentModule) {
    std::string path = currentModule->SourcePath.empty()
                           ? currentModule->ResolvedPath
                           : currentModule->SourcePath;
    if (!path.empty())
      return PathUtils::canonicalize(path);
  }
  return "root";
}

class ClosureCapabilityAnalyzer {
public:
  explicit ClosureCapabilityAnalyzer(std::set<std::string> captures)
      : Captures(std::move(captures)) {}

  CallableReceiverMode analyze(BlockStmt *body) {
    visitStmt(body);
    return Mode;
  }

private:
  std::set<std::string> Captures;
  CallableReceiverMode Mode = CallableReceiverMode::Shared;

  void require(CallableReceiverMode requested) {
    if (static_cast<int>(requested) > static_cast<int>(Mode))
      Mode = requested;
  }

  bool isCaptureRoot(Expr *expr) {
    if (!expr)
      return false;
    std::string path = pathString(expr);
    if (path.empty())
      return false;
    size_t dot = path.find('.');
    std::string root = Type::stripMorphology(path.substr(0, dot));
    return Captures.count(root) != 0;
  }

  static std::string pathString(Expr *expr) {
    if (!expr)
      return {};
    if (auto *variable = dynamic_cast<VariableExpr *>(expr))
      return variable->Name;
    if (auto *member = dynamic_cast<MemberExpr *>(expr)) {
      std::string base = pathString(member->Object.get());
      return base.empty() ? std::string() : base + "." + member->Member;
    }
    if (auto *index = dynamic_cast<ArrayIndexExpr *>(expr))
      return pathString(index->Array.get());
    if (auto *postfix = dynamic_cast<PostfixExpr *>(expr))
      return pathString(postfix->LHS.get());
    if (auto *unary = dynamic_cast<UnaryExpr *>(expr))
      return pathString(unary->RHS.get());
    if (auto *address = dynamic_cast<AddressOfExpr *>(expr))
      return pathString(address->Expression.get());
    if (auto *deref = dynamic_cast<DereferenceExpr *>(expr))
      return pathString(deref->Expression.get());
    if (auto *cede = dynamic_cast<CedeExpr *>(expr))
      return pathString(cede->Value.get());
    return {};
  }

  static bool isAssignment(const std::string &op) {
    return op == "=" || op == "+=" || op == "-=" || op == "*=" ||
           op == "/=" || op == "%=" || op == "&=" || op == "|=" ||
           op == "^=" || op == "<<=" || op == ">>=";
  }

  void visitStmt(Stmt *stmt) {
    if (!stmt)
      return;
    if (auto *expr = dynamic_cast<Expr *>(stmt)) {
      visitExpr(expr);
    } else if (auto *block = dynamic_cast<BlockStmt *>(stmt)) {
      for (auto &child : block->Statements)
        visitStmt(child.get());
    } else if (auto *initBlock = dynamic_cast<InitBlockStmt *>(stmt)) {
      visitStmt(initBlock->Body.get());
    } else if (auto *ret = dynamic_cast<ReturnStmt *>(stmt)) {
      visitExpr(ret->ReturnValue.get());
    } else if (auto *exprStmt = dynamic_cast<ExprStmt *>(stmt)) {
      visitExpr(exprStmt->Expression.get());
    } else if (auto *var = dynamic_cast<VariableDecl *>(stmt)) {
      visitExpr(var->Init.get());
    } else if (auto *del = dynamic_cast<DeleteStmt *>(stmt)) {
      visitExpr(del->Expression.get());
    } else if (auto *fre = dynamic_cast<FreeStmt *>(stmt)) {
      visitExpr(fre->Expression.get());
      visitExpr(fre->Count.get());
    } else if (auto *unsafeStmt = dynamic_cast<UnsafeStmt *>(stmt)) {
      visitStmt(unsafeStmt->Statement.get());
    }
  }

  void visitExpr(Expr *expr) {
    if (!expr)
      return;
    if (dynamic_cast<ClosureExpr *>(expr))
      return;
    if (auto *cede = dynamic_cast<CedeExpr *>(expr)) {
      if (isCaptureRoot(cede->Value.get()))
        require(CallableReceiverMode::Consuming);
      visitExpr(cede->Value.get());
      return;
    }
    if (auto *binary = dynamic_cast<BinaryExpr *>(expr)) {
      if (isAssignment(binary->Op) && isCaptureRoot(binary->LHS.get()))
        require(CallableReceiverMode::Mutable);
      visitExpr(binary->LHS.get());
      visitExpr(binary->RHS.get());
      return;
    }
    if (auto *method = dynamic_cast<MethodCallExpr *>(expr)) {
      if (isCaptureRoot(method->Object.get()) && method->ResolvedFn &&
          !method->ResolvedFn->Args.empty()) {
        const auto &receiver = method->ResolvedFn->Args[0];
        if (receiver.IsCeded)
          require(CallableReceiverMode::Consuming);
        else if (receiver.IsValueMutable)
          require(CallableReceiverMode::Mutable);
      }
      visitExpr(method->Object.get());
      for (auto &arg : method->Args)
        visitExpr(arg.get());
      return;
    }
    if (auto *call = dynamic_cast<CallExpr *>(expr)) {
      std::string callee = Type::stripMorphology(call->Callee);
      if (Captures.count(callee)) {
        if (call->CallableReceiver == CallableReceiverMode::Consuming)
          require(CallableReceiverMode::Consuming);
        else if (call->CallableReceiver == CallableReceiverMode::Mutable)
          require(CallableReceiverMode::Mutable);
      }
      size_t offset = call->ResolvedFn && call->ResolvedFn->IsClosureInvoke ? 1 : 0;
      for (size_t i = offset; i < call->Args.size(); ++i) {
        Expr *arg = call->Args[i].get();
        size_t param = i;
        if (call->ResolvedFn && param < call->ResolvedFn->Args.size() &&
            isCaptureRoot(arg)) {
          const auto &formal = call->ResolvedFn->Args[param];
          if (formal.IsCeded)
            require(CallableReceiverMode::Consuming);
          else if (formal.IsValueMutable)
            require(CallableReceiverMode::Mutable);
        }
        visitExpr(arg);
      }
      return;
    }
    if (auto *variable = dynamic_cast<VariableExpr *>(expr)) {
      if (variable->IsValueMutable && isCaptureRoot(variable))
        require(CallableReceiverMode::Mutable);
      return;
    }
    if (auto *member = dynamic_cast<MemberExpr *>(expr))
      visitExpr(member->Object.get());
    else if (auto *index = dynamic_cast<ArrayIndexExpr *>(expr)) {
      visitExpr(index->Array.get());
      for (auto &item : index->Indices)
        visitExpr(item.get());
    } else if (auto *value = dynamic_cast<DereferenceExpr *>(expr))
      visitExpr(value->Expression.get());
    else if (auto *value = dynamic_cast<AddressOfExpr *>(expr))
      visitExpr(value->Expression.get());
    else if (auto *value = dynamic_cast<UnaryExpr *>(expr)) {
      if ((value->Op == TokenType::PlusPlus || value->Op == TokenType::MinusMinus) &&
          isCaptureRoot(value->RHS.get()))
        require(CallableReceiverMode::Mutable);
      visitExpr(value->RHS.get());
    } else if (auto *value = dynamic_cast<PostfixExpr *>(expr)) {
      if (value->Op == TokenType::TokenWrite && isCaptureRoot(value->LHS.get()))
        require(CallableReceiverMode::Mutable);
      visitExpr(value->LHS.get());
    } else if (auto *value = dynamic_cast<CastExpr *>(expr))
      visitExpr(value->Expression.get());
    else if (auto *value = dynamic_cast<UnsafeExpr *>(expr))
      visitExpr(value->Expression.get());
    else if (auto *value = dynamic_cast<StartExpr *>(expr))
      visitExpr(value->Expression.get());
    else if (auto *value = dynamic_cast<AwaitExpr *>(expr))
      visitExpr(value->Expression.get());
    else if (auto *value = dynamic_cast<WaitExpr *>(expr))
      visitExpr(value->Expression.get());
    else if (auto *value = dynamic_cast<IfExpr *>(expr)) {
      visitExpr(value->Condition.get());
      visitStmt(value->Then.get());
      visitStmt(value->Else.get());
    } else if (auto *value = dynamic_cast<GuardExpr *>(expr)) {
      visitExpr(value->Condition.get());
      visitStmt(value->Then.get());
      visitStmt(value->Else.get());
    } else if (auto *value = dynamic_cast<LoopExpr *>(expr)) {
      visitExpr(value->Condition.get());
      visitStmt(value->Body.get());
    } else if (auto *value = dynamic_cast<ForExpr *>(expr)) {
      visitExpr(value->Collection.get());
      visitStmt(value->Body.get());
      visitStmt(value->ElseBody.get());
    } else if (auto *value = dynamic_cast<MatchExpr *>(expr)) {
      visitExpr(value->Target.get());
      for (auto &arm : value->Arms) {
        visitExpr(arm->Guard.get());
        visitStmt(arm->Body.get());
      }
    } else if (auto *value = dynamic_cast<PassExpr *>(expr))
      visitExpr(value->Value.get());
    else if (auto *value = dynamic_cast<BreakExpr *>(expr))
      visitExpr(value->Value.get());
    else if (auto *value = dynamic_cast<ArrayExpr *>(expr))
      for (auto &item : value->Elements)
        visitExpr(item.get());
    else if (auto *value = dynamic_cast<RepeatedArrayExpr *>(expr)) {
      visitExpr(value->Value.get());
      visitExpr(value->Count.get());
    } else if (auto *value = dynamic_cast<InitStructExpr *>(expr))
      for (auto &field : value->Members)
        visitExpr(field.second.get());
    else if (auto *value = dynamic_cast<AnonymousRecordExpr *>(expr))
      for (auto &field : value->Fields)
        visitExpr(field.second.get());
  }
};

} // namespace

std::shared_ptr<toka::Type> Sema::checkClosureExpr(ClosureExpr *Clo) {
  if (!Clo->SynthesizedShapeName.empty()) {
      return toka::Type::fromString(Clo->SynthesizedShapeName);
  }

  auto fullLoc = DiagnosticEngine::SrcMgr && Clo->Loc.isValid()
                     ? DiagnosticEngine::SrcMgr->getFullSourceLoc(Clo->Loc)
                     : FullSourceLoc{};
  std::string modulePath = closureModulePath(Clo, CurrentModule);
  std::string functionName = "<global>";
  if (CurrentFunction) {
    functionName = CurrentFunction->CodegenName.empty()
                       ? CurrentFunction->Name
                       : CurrentFunction->CodegenName;
  }
  std::string closureIdentity =
      modulePath + "\n" + functionName + "\n" +
      std::to_string(fullLoc.Line) + ":" + std::to_string(fullLoc.Column);
  std::string UniqueName =
      "__Closure_" + std::to_string(closureIdentityHash(closureIdentity));
  Clo->SynthesizedShapeName = UniqueName;

  auto oldAccessed = m_AccessedVariables;
  m_AccessedVariables.clear();
  auto *oldClosureCaptureRootScope = m_ClosureCaptureRootScope;
  
  auto oldFuncRetType = CurrentFunctionReturnType;
  CurrentFunctionReturnType = Clo->ReturnType;

  enterScope();
  m_ClosureCaptureRootScope = CurrentScope;
  
  // Synthesize Params and Generic Parameters for the `__invoke` method
  std::vector<GenericParam> invokeGenerics;
  std::vector<FunctionDecl::Arg> closureParams;
  
  if (Clo->HasExplicitArgs) {
      for (size_t i = 0; i < Clo->ArgNames.size(); ++i) {
          std::string tName;
          if (i < Clo->InjectedParamTypes.size()) {
             tName = Clo->InjectedParamTypes[i]->getSoulName();
          } else {
             tName = "T" + std::to_string(i);
             invokeGenerics.push_back({tName});
          }
          
          FunctionDecl::Arg arg;
          arg.Name = Clo->ArgNames[i];
          arg.Type = tName;
          closureParams.push_back(std::move(arg));
      }
  } else if (Clo->MaxImplicitArgIndex >= 0) {
      for (int i = 0; i <= Clo->MaxImplicitArgIndex; ++i) {
          std::string tName;
          if (i < Clo->InjectedParamTypes.size()) {
             tName = Clo->InjectedParamTypes[i]->getSoulName();
          } else {
             tName = "T" + std::to_string(i);
             invokeGenerics.push_back({tName});
          }
          
          FunctionDecl::Arg arg;
          arg.Name = "_arg" + std::to_string(i); // Matches VariableExpr generated by parser
          arg.Type = tName;
          closureParams.push_back(std::move(arg));
      }
  }

  // Define params in scope
  for (auto &p : closureParams) {
    p.ResolvedType = toka::Type::fromString(p.Type); // Dynamic (fallback to T0 if generic)
    SymbolInfo info;
    info.TypeObj = p.ResolvedType;
    CurrentScope->define(p.Name, info);
  }

  // Check Body (this will recursively call checkExpr which populates m_AccessedVariables)
  if (Clo->Body) {
      // This pass discovers captures only.  It must not leave ownership or
      // borrow transitions on the outer declarations: the invoke-body pass
      // below replays those transitions against the fresh capture bindings.
      const AnalysisState precomputeState = captureAnalysisState();
      bool oldPrecompute = m_IsPrecomputingCaptures;
      m_IsPrecomputingCaptures = true;
      checkStmt(Clo->Body.get());
      m_IsPrecomputingCaptures = oldPrecompute;
      mergeAnalysisStates({precomputeState}, precomputeState.PAL);
  }

  // Determine Captures
  std::vector<ShapeMember> members;
  std::map<std::string, SymbolInfo> captureBindings;
  Clo->ImplicitCaptures.clear();
  Clo->BoundaryImplicitCaptures.clear();
  Clo->BoundaryNonSendCaptures.clear();
  Clo->BoundaryNonSyncCopyCaptures.clear();
  Clo->HasBoundaryCaptureSummary = false;
  bool completeBoundarySummary = true;
  for (const auto &capture : Clo->ExplicitCaptures) {
    std::string name = Type::stripMorphology(capture.Name);
    if (!name.empty() && name != "*")
      m_AccessedVariables.insert(name);
  }
  for (const auto& varName : m_AccessedVariables) {
     SymbolInfo *infoPtr = nullptr;
     std::string actualName;
     // Only names resolved from an outer scope are captures. Parameters and
     // locals declared inside the closure body belong to the closure scope.
     bool isClosureLocal =
         CurrentScope->Symbols.count(varName) ||
         CurrentScope->Symbols.count("&" + varName) ||
         CurrentScope->Symbols.count("*" + varName) ||
         CurrentScope->Symbols.count("^" + varName) ||
         CurrentScope->Symbols.count("~" + varName);
     if (isClosureLocal) {
         continue;
     }

     if (CurrentScope->findVariableWithDeref(varName, infoPtr, actualName)) {
        
        bool isExplicit = false;
        CaptureMode explicitMode = CaptureMode::ImplicitBorrow;
        
        for (auto& cap : Clo->ExplicitCaptures) {
           std::string rawName = cap.Name;
           while(!rawName.empty() && (rawName[0]=='~' || rawName[0]=='^' || rawName[0]=='*' || rawName[0]=='&' || rawName[0]=='?' || rawName[0]=='#')) {
              rawName = rawName.substr(1);
           }
           if (rawName == varName || cap.Name == "*") {
              isExplicit = true;
              explicitMode = cap.Mode;
              break;
           }
        }
        
        if (!infoPtr->IsDeclaredVariable) {
           continue;
        }

        if (!isExplicit) {
           Clo->ImplicitCaptures.push_back(varName);
           Clo->BoundaryImplicitCaptures.push_back(varName);
        } else if (infoPtr->TypeObj &&
                   (infoPtr->TypeObj->typeKind == Type::Function ||
                    infoPtr->TypeObj->typeKind == Type::DynFn)) {
           if (!infoPtr->HasClosureBoundarySummary) {
             completeBoundarySummary = false;
           } else {
             Clo->BoundaryImplicitCaptures.insert(
                 Clo->BoundaryImplicitCaptures.end(),
                 infoPtr->ClosureImplicitCaptures.begin(),
                 infoPtr->ClosureImplicitCaptures.end());
             Clo->BoundaryNonSendCaptures.insert(
                 Clo->BoundaryNonSendCaptures.end(),
                 infoPtr->ClosureNonSendCaptures.begin(),
                 infoPtr->ClosureNonSendCaptures.end());
             Clo->BoundaryNonSyncCopyCaptures.insert(
                 Clo->BoundaryNonSyncCopyCaptures.end(),
                 infoPtr->ClosureNonSyncCopyCaptures.begin(),
                 infoPtr->ClosureNonSyncCopyCaptures.end());
           }
        } else if (!infoPtr->TypeObj || !infoPtr->TypeObj->isSend(this)) {
           Clo->BoundaryNonSendCaptures.push_back(varName);
        } else if ((explicitMode == CaptureMode::ExplicitCopy ||
                    explicitMode == CaptureMode::ExplicitDup) &&
                   !infoPtr->TypeObj->isSync(this)) {
           Clo->BoundaryNonSyncCopyCaptures.push_back(varName);
        }

        ShapeMember sm;
        sm.Name = varName;
        
        if (isExplicit &&
            (explicitMode == CaptureMode::ExplicitCede ||
             explicitMode == CaptureMode::ExplicitCopy ||
             explicitMode == CaptureMode::ExplicitDup)) {
            if (explicitMode == CaptureMode::ExplicitCopy) {
                // Copy capture uses the same structural proof as every other
                // copy operation.
                bool isResourceCapture = !proveSlice4CopyType(infoPtr->TypeObj);
                if (isResourceCapture) {
                    error(Clo, DiagID::ERR_SEMA_CLOSURE_COPY_CAPTURE_RESOURCE,
                          varName, infoPtr->TypeObj->toString(), varName);
                    SourceLocation originLoc = infoPtr->DeclLoc;
                    recordDecision(
                        Clo, SemanticRuleID::OwnResource001,
                        SemanticOperation::ResourceCopy,
                        SemanticDecision::Reject,
                        SemanticReason::ResourceCopyForbidden, varName,
                        infoPtr->TypeObj->toString(), originLoc);
                    if (originLoc.isValid())
                      DiagnosticEngine::report(
                          originLoc, DiagID::NOTE_GENERIC,
                          "resource value declared here");
                    continue;
                }
            }
            if (explicitMode == CaptureMode::ExplicitDup) {
                bool hasDup = false;
                if (infoPtr->TypeObj) {
                    hasDup = proveSlice4CopyType(infoPtr->TypeObj);
                    if (!hasDup && infoPtr->TypeObj->isShape()) {
                        std::string soul = toka::Type::stripMorphology(
                            infoPtr->TypeObj->getSoulName());
                        auto shape = ShapeMap.find(soul);
                        hasDup = shape != ShapeMap.end() &&
                                 Slice4DupProviders.count(shape->second);
                    }
                }
                if (!hasDup) {
                    DiagnosticEngine::report(
                        Clo->Loc, DiagID::ERR_GENERIC_SEMA,
                        "[dup ...] capture requires a proven @Copy type or an explicit @Dup provider");
                    HasError = true;
                    continue;
                }
            }
            sm.Type = infoPtr->TypeObj->toString(); 
            sm.ResolvedType = infoPtr->TypeObj; // [Fix] Pre-resolve
            // An explicit capture is a new binding, but it carries the
            // captured declaration's authority and any direct-flow ceiling.
            // The capture-list sigils select the transfer; they cannot erase
            // a real payload capability and force later body checks to treat
            // a writable captured field as read-only.
            SymbolInfo captureInfo = *infoPtr;
            captureInfo.SymbolID = 0;
            captureInfo.Moved = false;
            captureInfo.MoveLoc = SourceLocation{};
            captureInfo.IsFunctionParameter = false;
            captureInfo.IsDeclaredVariable = true;
            captureInfo.TypeObj = sm.ResolvedType;
            captureBindings[sm.Name] = std::move(captureInfo);
            if (explicitMode == CaptureMode::ExplicitCede) {
                // Mark original variable as Consumed/Moved in the parent scope!
                CurrentScope->Parent->markMoved(varName, Clo->Loc);
            }
        } else {
            // Implicit capture means Borrow (Reference).  It is a new view,
            // not a new authority root: retain the directly captured payload
            // capability and its existing flow ceiling, but never retain
            // handle rebinding permission.
            sm.Type = "&" + infoPtr->TypeObj->toString();
            sm.ResolvedType = std::make_shared<toka::ReferenceType>(infoPtr->TypeObj); // [Fix] Pre-resolve reference

            SymbolInfo captureInfo = *infoPtr;
            captureInfo.SymbolID = 0;
            captureInfo.Moved = false;
            captureInfo.MoveLoc = SourceLocation{};
            captureInfo.IsFunctionParameter = false;
            captureInfo.IsDeclaredVariable = true;
            captureInfo.Permission.Morphology = BindingMorphology::Reference;
            captureInfo.Permission.IdentityRebindable = false;
            captureInfo.Permission.IdentityNullable = false;
            captureInfo.Permission.IdentityBlocked = false;
            captureInfo.TypeObj = sm.ResolvedType;
            captureBindings[sm.Name] = std::move(captureInfo);
        }

        members.push_back(sm);
     }
  }

  if (m_ExpectedType && m_ExpectedType->typeKind == toka::Type::DynFn &&
      !Clo->ImplicitCaptures.empty()) {
    for (const auto &name : Clo->ImplicitCaptures) {
      DiagnosticEngine::report(
          Clo->Loc, DiagID::ERR_SEMA_DYN_FN_IMPLICIT_CAPTURE, name, name,
          name);
      HasError = true;
    }
  }

  for (const auto &name : Clo->ImplicitCaptures) {
    m_LastLifeDependencies.insert(name);
  }
  Clo->HasBoundaryCaptureSummary = completeBoundarySummary;

  std::set<std::string> captureNames;
  for (const auto &member : members)
    captureNames.insert(Type::stripMorphology(member.Name));
  Clo->CallableReceiver =
      ClosureCapabilityAnalyzer(std::move(captureNames))
          .analyze(Clo->Body.get());

  exitScope();
  m_ClosureCaptureRootScope = oldClosureCaptureRootScope;
  m_AccessedVariables = oldAccessed;
  CurrentFunctionReturnType = oldFuncRetType;

  // Construct synthetic ShapeDecl
  auto SyntheticShape = std::make_unique<ShapeDecl>(
      false, UniqueName, std::vector<GenericParam>{}, ShapeKind::Struct, members);
  SyntheticShape->Loc = Clo->Loc;
  
  auto retTy = std::make_shared<toka::ShapeType>(UniqueName);
  retTy->resolve(SyntheticShape.get());

  ShapeMap[UniqueName] = SyntheticShape.get();
  // MOVED DOWN: SyntheticShapes.push_back(std::move(SyntheticShape));

  std::vector<FunctionDecl::Arg> invokeArgs;
  FunctionDecl::Arg selfArg;
  selfArg.Name = "self";
  selfArg.Type = UniqueName;
  selfArg.ResolvedType = retTy;
  selfArg.IsValueMutable =
      Clo->CallableReceiver == CallableReceiverMode::Mutable;
  selfArg.IsCeded =
      Clo->CallableReceiver == CallableReceiverMode::Consuming;
  selfArg.Permission = BindingPermission::fromLegacy(
      false, false, false, false, false, false, false,
      selfArg.IsValueMutable, false, false);
  invokeArgs.push_back(std::move(selfArg));

  for (const auto& p : closureParams) {
     invokeArgs.push_back(p.clone());
  }

  std::string invokeRetType = Clo->ReturnType;
  if (invokeRetType.empty() || invokeRetType == "unknown") {
      invokeRetType = CurrentFunctionReturnType; 
  }
  auto invokeFunc = std::make_unique<FunctionDecl>(
      true, "call", std::move(invokeArgs), std::move(Clo->Body),
      invokeRetType);
  invokeFunc->GenericParams = invokeGenerics; // [NEW] Attach generic parameters
  invokeFunc->ResolvedReturnType = toka::Type::fromString(invokeRetType);
  invokeFunc->IsClosureInvoke = true;
  invokeFunc->ClosureReceiver = Clo->CallableReceiver;
  invokeFunc->CodegenName = UniqueName + "___invoke";

  // [Fix] Closure Body Semantic Checking
  // We must type-check the body here so all AST nodes receive ResolvedType.
  // We create a temporary scope to inject 'self', parameters, and captured fields
  // so that accessing a captured field doesn't trigger "Use of moved value" from the outer scope.
  enterScope();
  
  // 1. Inject 'self' and original params
  for (auto &arg : invokeFunc->Args) {
    SymbolInfo Info;
    Info.TypeObj = arg.ResolvedType ? arg.ResolvedType : toka::Type::fromString(arg.Type);
    CurrentScope->define(arg.Name, Info);
  }
  
  // 2. Inject captured variables as perfectly valid locals
  for (auto &memb : SyntheticShape->Members) {
    if (memb.ResolvedType) {
       SymbolInfo Info;
       auto captureIt = captureBindings.find(memb.Name);
       if (captureIt != captureBindings.end()) {
         Info = captureIt->second;
       }
       Info.TypeObj = memb.ResolvedType; // Pre-resolved!
       Info.IsDeclaredVariable = true;
       // If it's a reference capture, the user writes `x`, but it's a reference under the hood. 
       // We want it to be considered as the exact physical type.
       CurrentScope->define(memb.Name, Info);
     }
  }

  // 3. Check the body
  if (invokeFunc->Body) {
      std::string savedRet = CurrentFunctionReturnType;
      FunctionDecl *savedFn = CurrentFunction;
      CurrentFunction = invokeFunc.get();
      CurrentFunctionReturnType = invokeRetType;

      checkStmt(invokeFunc->Body.get());

      CurrentFunctionReturnType = savedRet;
      CurrentFunction = savedFn;
  }
  
  exitScope();

  // Now we can safely move SyntheticShape to permanent storage
  ShapeMap[UniqueName] = SyntheticShape.get();
  SyntheticShapes.push_back(std::move(SyntheticShape));

  ImplMap[UniqueName]["call"] = invokeFunc.get();
  ImplMap[UniqueName + "@Callable"]["call"] = invokeFunc.get();
  MethodDecls[UniqueName]["call"] = invokeFunc.get();
  MethodDecls[UniqueName]["__invoke"] = invokeFunc.get();
  MethodMap[UniqueName]["call"] = invokeRetType;
  MethodMap[UniqueName]["__invoke"] = invokeRetType;

  std::vector<std::unique_ptr<FunctionDecl>> implMethods;
  implMethods.push_back(std::move(invokeFunc));
  auto implDecl = std::make_unique<ImplDecl>(UniqueName, std::move(implMethods),
                                             "Callable");

  if (GenericInstancesModule) {
      GenericInstancesModule->Impls.push_back(std::move(implDecl));
  }

  return toka::Type::fromString(UniqueName);
}

} // namespace toka
