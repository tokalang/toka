
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
#include "toka/Type.h"
#include <iostream>
#include <sstream>

namespace toka {

// --- Helper: String-based Type Substitution ---
// Replaces generic params (e.g. "T") with concrete types (e.g. "i32") in a type
// string.
static std::string
substituteTypeString(const std::string &Input,
                     const std::map<std::string, std::string> &Map) {
  if (Input.empty()) return "";

  // Preserve the binding of a callable result projection when the substituted
  // callable is a function type.  `fn(A) -> R@Callable::Output` would parse
  // as a projection on R, not on the original generic F.
  for (const auto &[K, V] : Map) {
    if (Input != K + "@Callable::Output")
      continue;
    auto callable = toka::Type::fromString(V);
    if (auto fn = std::dynamic_pointer_cast<toka::FunctionType>(callable)) {
      if (fn->ReturnType)
        return fn->ReturnType->toString();
    }
    if (auto dynFn = std::dynamic_pointer_cast<toka::DynFnType>(callable)) {
      if (dynFn->ReturnType)
        return dynFn->ReturnType->toString();
    }
    // A named callable is resolved through the regular associated-type table
    // after the generic template is instantiated.
    return V + "@Callable::Output";
  }
  
  auto typeObj = toka::Type::fromString(Input);
  if (!typeObj) return Input;
  
  std::map<std::string, std::shared_ptr<toka::Type>> ObjMap;
  for (const auto &[K, V] : Map) {
    ObjMap[K] = toka::Type::fromString(V);
  }
  
  auto subObj = typeObj->substitute(ObjMap);
  std::string Output = subObj->toString();

  if (Input != Output) {
  }
  return Output;
}

// --- Helper: Generic Instantiator Visitor ---
// Traverses the AST and applies substitution to all Type Strings.
// Since we don't have a central AST Visitor, we implement specific traversals
// here.
class GenericInstantiator {
  const std::map<std::string, std::string> &Replacements;
  const std::set<std::string> &MorphicParams;

public:
  GenericInstantiator(const std::map<std::string, std::string> &map,
                      const std::set<std::string> &morphicParams)
      : Replacements(map), MorphicParams(morphicParams) {}

  std::string sub(const std::string &s) {
    if (s.empty())
      return "";
    return substituteTypeString(s, Replacements);
  }

  void visitPattern(MatchArm::Pattern *Pat) {
    if (!Pat)
      return;
    Pat->Name = sub(Pat->Name);
    for (auto &Sub : Pat->SubPatterns) {
      visitPattern(Sub.get());
    }
  }

  void visitFunction(FunctionDecl *Fn) {
    Fn->ReturnType = sub(Fn->ReturnType);
    for (auto &Arg : Fn->Args) {
      if (MorphicParams.count(Arg.Type)) {
        Arg.IsMorphicExempt = true;
        Arg.Permission.MorphicExempt = true;
      }
      Arg.Type = sub(Arg.Type);
      // Reset ResolvedType to allow Sema to re-resolve it
      Arg.ResolvedType = nullptr;
    }
    if (Fn->Body) {
      visitStmt(Fn->Body.get());
    }
    // GenericParams of the function itself?
    // If impl<T> fn foo<U>(), T is substituted, U remains.
    // We only substitute Impl params.
  }

  void visitAssociatedType(AssociatedTypeDecl &Assoc) {
    Assoc.Type = sub(Assoc.Type);
  }

  void visitStmt(Stmt *S) {
    if (!S)
      return;

    if (auto *Block = dynamic_cast<BlockStmt *>(S)) {
      for (auto &Sub : Block->Statements) {
        visitStmt(Sub.get());
      }
    } else if (auto *Ret = dynamic_cast<ReturnStmt *>(S)) {
      visitExpr(Ret->ReturnValue.get());
    } else if (auto *ExprS = dynamic_cast<ExprStmt *>(S)) {
      visitExpr(ExprS->Expression.get());
    } else if (auto *Var = dynamic_cast<VariableDecl *>(S)) {
      if (!Var->TypeName.empty()) {
        Var->TypeName = sub(Var->TypeName);
      }
      Var->ResolvedType = nullptr;
      visitExpr(Var->Init.get());
    } else if (auto *Del = dynamic_cast<DeleteStmt *>(S)) {
      visitExpr(Del->Expression.get());
    } else if (auto *Free = dynamic_cast<FreeStmt *>(S)) {
      visitExpr(Free->Expression.get());
      visitExpr(Free->Count.get());
    } else if (auto *Uns = dynamic_cast<UnsafeStmt *>(S)) {
      if (Uns->Statement)
        visitStmt(Uns->Statement.get());
    }
  }

  void visitExpr(Expr *E) {
    if (!E)
      return;

    if (auto *If = dynamic_cast<IfExpr *>(E)) {
      visitExpr(If->Condition.get());
      visitStmt(If->Then.get());
      visitStmt(If->Else.get());
    } else if (auto *For = dynamic_cast<ForExpr *>(E)) {
      visitExpr(For->Collection.get());
      visitStmt(For->Body.get());
      visitStmt(For->ElseBody.get());
    } else if (auto *Loop = dynamic_cast<LoopExpr *>(E)) {
      if (Loop->Condition)
        visitExpr(Loop->Condition.get());
      visitStmt(Loop->Body.get());
    } else if (auto *Bin = dynamic_cast<BinaryExpr *>(E)) {
      visitExpr(Bin->LHS.get());
      visitExpr(Bin->RHS.get());
    } else if (auto *Un = dynamic_cast<UnaryExpr *>(E)) {
      visitExpr(Un->RHS.get());
    } else if (auto *Post = dynamic_cast<PostfixExpr *>(E)) {
      visitExpr(Post->LHS.get());
    } else if (auto *Cast = dynamic_cast<CastExpr *>(E)) {
      Cast->TargetType = sub(Cast->TargetType);
      visitExpr(Cast->Expression.get());
    } else if (auto *Closure = dynamic_cast<ClosureExpr *>(E)) {
      Closure->ReturnType = sub(Closure->ReturnType);
      visitStmt(Closure->Body.get());
    } else if (auto *SE = dynamic_cast<SizeOfExpr *>(E)) {
      SE->TypeStr = sub(SE->TypeStr);
    } else if (auto *Addr = dynamic_cast<AddressOfExpr *>(E)) {
      visitExpr(Addr->Expression.get());
    } else if (auto *Mem = dynamic_cast<MemberExpr *>(E)) {
      visitExpr(Mem->Object.get());
    } else if (auto *VarE = dynamic_cast<VariableExpr *>(E)) {
      VarE->Name = sub(VarE->Name);
    } else if (auto *Call = dynamic_cast<CallExpr *>(E)) {
      // Call->Callee could rely on T? e.g. T::new() -> i32::new()
      // T::new is parsed as "T::new" string in Callee.
      Call->Callee = sub(Call->Callee);
      for (auto &Arg : Call->Args)
        visitExpr(Arg.get());
      for (auto &G : Call->GenericArgs)
        G = sub(G);
    } else if (auto *New = dynamic_cast<NewExpr *>(E)) {
      New->Type = sub(New->Type);
      visitExpr(New->Initializer.get());
    } else if (auto *Alloc = dynamic_cast<AllocExpr *>(E)) {
      Alloc->TypeName = sub(Alloc->TypeName); 
      visitExpr(Alloc->Initializer.get());
      visitExpr(Alloc->ArraySize.get());
    } else if (auto *Arr = dynamic_cast<ArrayExpr *>(E)) {
      for (auto &El : Arr->Elements)
        visitExpr(El.get());
    } else if (auto *Idx = dynamic_cast<ArrayIndexExpr *>(E)) {
      visitExpr(Idx->Array.get());
      for (auto &I : Idx->Indices)
        visitExpr(I.get());
    } else if (auto *Rec = dynamic_cast<AnonymousRecordExpr *>(E)) {
      for (auto &F : Rec->Fields)
        visitExpr(F.second.get());
      if (!Rec->AssignedTypeName.empty())
        Rec->AssignedTypeName = sub(Rec->AssignedTypeName);
    } else if (auto *MetCall = dynamic_cast<MethodCallExpr *>(E)) {
      visitExpr(MetCall->Object.get());
      for (auto &Arg : MetCall->Args)
        visitExpr(Arg.get());
    } else if (auto *Init = dynamic_cast<InitStructExpr *>(E)) {
      Init->ShapeName = sub(Init->ShapeName);
      for (auto &F : Init->Members)
        visitExpr(F.second.get());
    } else if (auto *Rep = dynamic_cast<RepeatedArrayExpr *>(E)) {
      visitExpr(Rep->Value.get());
      visitExpr(Rep->Count.get());
    } else if (auto *Match = dynamic_cast<MatchExpr *>(E)) {
      visitExpr(Match->Target.get());
      for (auto &Arm : Match->Arms) {
        visitPattern(Arm->Pat.get());
        visitStmt(Arm->Body.get());
        if (Arm->Guard)
          visitExpr(Arm->Guard.get());
      }
    } else if (auto *Pass = dynamic_cast<PassExpr *>(E)) {
      visitExpr(Pass->Value.get());
    } else if (auto *UnsE = dynamic_cast<UnsafeExpr *>(E)) {
      visitExpr(UnsE->Expression.get());
    } else if (auto *Brk = dynamic_cast<BreakExpr *>(E)) {
      if (Brk->Value)
        visitExpr(Brk->Value.get());
    } else if (auto *Cont = dynamic_cast<ContinueExpr *>(E)) {
      // Nothing to substitute in labels usually
    }

    // Reset ResolvedType
    E->ResolvedType = nullptr;
  }
};

void Sema::instantiateGenericImpl(
    ImplDecl *Template, const std::string &ConcreteTypeName,
    const std::vector<std::shared_ptr<toka::Type>> &GenericArgs) {
  // 1. Verify generic args count
  if (GenericArgs.size() != Template->GenericParams.size()) {
    return;
  }

  // [NEW] Check Trait Bounds (SFINAE)
  for (size_t i = 0; i < Template->GenericParams.size(); ++i) {
    if (!Template->GenericParams[i].TraitBounds.empty()) {
      auto bounds = substituteTraitBounds(
          Template->GenericParams[i].TraitBounds, Template->GenericParams,
          GenericArgs);
      if (!checkTraitBounds(Template->Loc, Template->GenericParams[i].Name,
                            bounds, GenericArgs[i]->toString(),
                            true /* isSilent */)) {
        return; // SFINAE: Silently filter out this impl block
      }
    }
  }

  std::string MangledName = resolveType(ConcreteTypeName);
  const std::string instanceKey = canonicalImplDefinitionId(Template) +
                                  ";concrete:" + MangledName;
  const bool firstInstance = GenericImplInstanceMap.insert(instanceKey).second;
  // Legacy non-empty impl registration has observable downstream effects.
  // Slice 1 only needs the identity cache to make empty marker impls stable;
  // retain the historical path for callable impls until their later rewrite.
  if (!firstInstance && Template->Methods.empty())
    return;
  if (MethodDecls.count(MangledName) && !Template->Methods.empty()) {
    const std::string &firstMethod = Template->Methods.front()->Name;
    if (MethodDecls[MangledName].count(firstMethod))
      return;
  }

  // 2. Build Substitution Map
  std::map<std::string, std::string> Replacements;
  std::set<std::string> MorphicParams;
  for (size_t i = 0; i < Template->GenericParams.size(); ++i) {
    std::string k = Template->GenericParams[i].Name;
    std::string v = GenericArgs[i]->toString();
    Replacements[k] = v;
    if (Template->GenericParams[i].IsMorphic) {
      MorphicParams.insert(k);
      if (!k.empty() && k[0] == '\'') {
        MorphicParams.insert(k.substr(1));
      }
    }
    if (!k.empty() && k[0] == '\'') Replacements[k.substr(1)] = v;
  }

  // 3. Clone and Substitute
  GenericInstantiator Instantiator(Replacements, MorphicParams);
  SourceLocation instantiationLoc =
      CurrentFunction && CurrentFunction->Loc.isValid()
          ? CurrentFunction->Loc
          : CurrentModule && CurrentModule->Loc.isValid()
                ? CurrentModule->Loc
                : Template->Loc;
  ModuleScope *instantiationScope = getLexicalModule(instantiationLoc);

  std::vector<std::unique_ptr<FunctionDecl>> NewMethods;
  for (auto &Method : Template->Methods) {
    // Clone Method
    // We can use the clone() method from ASTNode if FunctionDecl supports it
    // correctly? FunctionDecl::clone() exists.
    auto ClonedAST = Method->clone();
    // clone() returns unique_ptr<ASTNode>. cast to FunctionDecl.
    // The AST clone implementation returns base unique_ptr.
    // We need to static_cast via release/reset or dynamic_cast.

    // Wait, FunctionDecl::clone() returns unique_ptr<ASTNode>.
    std::unique_ptr<FunctionDecl> ClonedFn(
        static_cast<FunctionDecl *>(ClonedAST.release()));

    // Apply Substitution
    Instantiator.visitFunction(ClonedFn.get());
    InstantiationLexicalScopes[ClonedFn.get()] = instantiationScope;
    auto definition = DeclarationLexicalScopes.find(Method.get());
    if (definition != DeclarationLexicalScopes.end())
      DeclarationLexicalScopes[ClonedFn.get()] = definition->second;
    for (const auto &arg : GenericArgs)
      recordInstantiationType(ClonedFn.get(), resolveType(arg));
    recordInstantiationType(
        ClonedFn.get(), resolveType(toka::Type::fromString(ConcreteTypeName)));

    NewMethods.push_back(std::move(ClonedFn));
  }

  std::vector<AssociatedTypeDecl> NewAssociatedTypes =
      Template->AssociatedTypes;
  for (auto &Assoc : NewAssociatedTypes) {
    Instantiator.visitAssociatedType(Assoc);
  }

  // 4. Create New ImplDecl
  // TypeName MUST be the mangled name (e.g. "Box_M_i32") for CodeGen lookup
  auto NewImpl = std::make_unique<ImplDecl>(
      MangledName, std::move(NewMethods), Template->TraitName
      // No GenericParams for the instance!
  );

  // Copy encapsulation entries if any (generics might affect them?)
  NewImpl->EncapEntries = Template->EncapEntries;
  NewImpl->AssociatedTypes = std::move(NewAssociatedTypes);
  NewImpl->Loc = Template->Loc; // rough loc

  // 5. Register and Check
  ImplDecl *RawPtr = NewImpl.get();

  // We must add to M.Impls to own it.
  GenericInstancesModule->Impls.push_back(std::move(NewImpl));

  // Now Register it!
  registerImpl(RawPtr);

  // Now Check it!
  // This will check method bodies
  checkImpl(RawPtr);

  // Done.
}

std::vector<std::string> Sema::substituteTraitBounds(
    const std::vector<std::string> &Bounds,
    const std::vector<GenericParam> &Params,
    const std::vector<std::shared_ptr<toka::Type>> &Args) {
  std::map<std::string, std::shared_ptr<toka::Type>> replacements;
  for (size_t i = 0; i < Params.size() && i < Args.size(); ++i) {
    replacements[Params[i].Name] = Args[i];
    if (!Params[i].Name.empty() && Params[i].Name[0] == '\'')
      replacements[Params[i].Name.substr(1)] = Args[i];
  }

  std::vector<std::string> result;
  result.reserve(Bounds.size());
  for (const auto &bound : Bounds) {
    auto type = toka::Type::fromString(bound);
    result.push_back(type ? type->substitute(replacements)->toString() : bound);
  }
  return result;
}

bool Sema::checkTraitBounds(SourceLocation Loc, const std::string &ParamName, 
                            const std::vector<std::string> &TraitBounds, 
                            const std::string &ConcreteType, bool isSilent,
                            SourceLocation BoundLoc) {
  bool success = true;
  std::string resolvedConcreteType = resolveType(ConcreteType);

  for (const auto &bound : TraitBounds) {
    SourceLocation visibilityLoc = BoundLoc.isValid() ? BoundLoc : Loc;
    TraitDecl *trait = findVisibleTraitDecl(bound, visibilityLoc);
    std::string canonicalBound = canonicalTraitName(bound, trait);
    std::string implKey = resolvedConcreteType + "@" + canonicalBound;
    if (ImplMap.count(implKey)) continue;

    // [NEW] Fallback for Auto Traits
    if (getTraitFamilyName(canonicalBound) == "Callable") {
      auto typeObj = toka::Type::fromString(resolvedConcreteType);
      if (typeObj && (typeObj->isFunction() || typeObj->isDynFn()))
        continue;
    } else if (canonicalBound == "Send") {
      auto typeObj = toka::Type::fromString(resolvedConcreteType);
      if (typeObj && typeObj->isSend(this)) continue;
    } else if (canonicalBound == "Sync") {
      auto typeObj = toka::Type::fromString(resolvedConcreteType);
      if (typeObj && typeObj->isSync(this)) continue;
    } else if (canonicalBound == "Clone") {
      // Toka permits ordinary value copies unless a type explicitly deletes
      // clone. Treat that language rule as the implicit @Clone contract so a
      // bounded generic implementation is omitted only for move-only types.
      auto methods = MethodDecls.find(resolvedConcreteType);
      if (methods == MethodDecls.end() || !methods->second.count("clone") ||
          !MethodDecls[resolvedConcreteType]["clone"]->IsDeleted)
        continue;
    }

    if (!isSilent) {
      DiagnosticEngine::report(Loc, DiagID::ERR_TRAIT_BOUND_UNSATISFIED, ConcreteType, bound, ParamName);
      HasError = true;
    }
    success = false;
  }
  return success;
}

} // namespace toka
