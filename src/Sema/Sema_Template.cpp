
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
#include "toka/Parser.h"
#include "toka/Sema.h"
#include "toka/Type.h"
#include <cctype>
#include <iostream>
#include <sstream>

namespace toka {

static bool isTypeNameBoundary(char c) {
  return !std::isalnum(static_cast<unsigned char>(c)) && c != '_';
}

// Non-type fields such as a callee name still use text.  Keep their
// substitution token-aware; all actual type positions use TypeSyntax below.
static std::string substituteTextualName(
    const std::string &input,
    const std::map<std::string, std::shared_ptr<toka::Type>> &replacements) {
  std::string result = input;
  for (const auto &[name, replacement] : replacements) {
    if (name.empty() || !replacement)
      continue;
    const std::string value = replacement->toString();
    size_t pos = 0;
    while ((pos = result.find(name, pos)) != std::string::npos) {
      const bool startOk = pos == 0 || isTypeNameBoundary(result[pos - 1]);
      const size_t end = pos + name.size();
      const bool endOk = end == result.size() || isTypeNameBoundary(result[end]);
      if (startOk && endOk) {
        result.replace(pos, name.size(), value);
        pos += value.size();
      } else {
        pos += name.size();
      }
    }
  }
  return result;
}

static TypeSyntaxPtr substituteTypeSyntax(
    const TypeSyntaxPtr &input,
    const std::map<std::string, std::shared_ptr<toka::Type>> &replacements) {
  if (!input)
    return nullptr;

  // `F@Callable::Output` must lower before F is substituted by a function
  // type, otherwise the projection would bind to the function spelling.
  if (input->NodeKind == TypeSyntax::Kind::AssociatedProjection &&
      input->Text == "Callable" && input->MemberName == "Output" &&
      input->Subject && input->Subject->NodeKind == TypeSyntax::Kind::Named) {
    auto it = replacements.find(input->Subject->Text);
    if (it != replacements.end()) {
      const auto &callable = it->second;
      if (auto fn = std::dynamic_pointer_cast<toka::FunctionType>(callable)) {
        if (fn->ReturnType)
          return fn->ReturnType->toSyntax(input->Begin, input->End);
      }
      if (auto dynFn = std::dynamic_pointer_cast<toka::DynFnType>(callable)) {
        if (dynFn->ReturnType)
          return dynFn->ReturnType->toSyntax(input->Begin, input->End);
      }
    }
  }

  std::map<std::string, TypeSyntaxPtr> typedReplacements;
  for (const auto &[name, replacement] : replacements) {
    if (replacement)
      typedReplacements.emplace(name,
                                replacement->toSyntax(input->Begin, input->End));
  }
  return input->substitute(typedReplacements);
}

// --- Helper: Generic Instantiator Visitor ---
// Traverses the AST and applies substitution to all Type Strings.
// Since we don't have a central AST Visitor, we implement specific traversals
// here.
class GenericInstantiator {
  const std::map<std::string, std::shared_ptr<toka::Type>> &Replacements;
  const std::map<std::string, std::shared_ptr<toka::Type>> &BodyReplacements;
  const std::set<std::string> &MorphicParams;
  bool InBody = false;

  const std::map<std::string, std::shared_ptr<toka::Type>> &active() const {
    return InBody ? BodyReplacements : Replacements;
  }

public:
  GenericInstantiator(const std::map<std::string,
                                    std::shared_ptr<toka::Type>> &map,
                      const std::map<std::string,
                                     std::shared_ptr<toka::Type>> &bodyMap,
                      const std::set<std::string> &morphicParams)
      : Replacements(map), BodyReplacements(bodyMap),
        MorphicParams(morphicParams) {}

  std::string sub(const std::string &s) {
    if (s.empty())
      return "";
    return substituteTextualName(s, active());
  }

  void sub(TypeSyntaxPtr &syntax, std::string &spelling) {
    if (!syntax) {
      spelling = sub(spelling);
      return;
    }
    syntax = substituteTypeSyntax(syntax, active());
    spelling = syntax->toCanonicalString();
  }

  void sub(TypeArgumentSyntax &argument, std::string &spelling) {
    if (argument.ArgumentKind == TypeArgumentSyntax::Kind::Type) {
      sub(argument.Type, spelling);
      return;
    }
    const auto &replacements = active();
    auto it = replacements.find(argument.ConstantText);
    if (it != replacements.end())
      argument.ConstantText = it->second ? it->second->toString() : "unknown";
    spelling = argument.toCanonicalString();
  }

  void visitPattern(MatchArm::Pattern *Pat) {
    if (!Pat)
      return;
    // Pattern names are variants or bindings, never type/const syntax. A
    // const-generic replacement here would turn a binding such as `N_` into
    // the identifier `4` rather than a literal expression.
    for (auto &Sub : Pat->SubPatterns) {
      visitPattern(Sub.get());
    }
  }

  void visitFunction(FunctionDecl *Fn) {
    sub(Fn->ReturnTypeSyntax, Fn->ReturnType);
    Fn->syncReturnContractTypeCache();
    for (auto &Arg : Fn->Args) {
      if (MorphicParams.count(Arg.Type)) {
        Arg.IsMorphicExempt = true;
        Arg.Permission.MorphicExempt = true;
      }
      sub(Arg.TypeSyntax, Arg.Type);
      Arg.ResolvedType = nullptr;
    }
    if (Fn->Body) {
      InBody = true;
      visitStmt(Fn->Body.get());
      InBody = false;
    }
    // GenericParams of the function itself?
    // If impl<T> fn foo<U>(), T is substituted, U remains.
    // We only substitute Impl params.
  }

  void visitAssociatedType(AssociatedTypeDecl &Assoc) {
    sub(Assoc.TypeSyntax, Assoc.Type);
  }

  void visitStmt(Stmt *S) {
    if (!S)
      return;

    if (auto *Block = dynamic_cast<BlockStmt *>(S)) {
      for (auto &Sub : Block->Statements) {
        visitStmt(Sub.get());
      }
    } else if (auto *InitBlock = dynamic_cast<InitBlockStmt *>(S)) {
      visitStmt(InitBlock->Body.get());
    } else if (auto *Ret = dynamic_cast<ReturnStmt *>(S)) {
      visitExpr(Ret->ReturnValue.get());
    } else if (auto *ExprS = dynamic_cast<ExprStmt *>(S)) {
      visitExpr(ExprS->Expression.get());
    } else if (auto *Var = dynamic_cast<VariableDecl *>(S)) {
      if (!Var->TypeName.empty())
        sub(Var->DeclaredTypeSyntax, Var->TypeName);
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
      sub(Cast->TargetTypeSyntax, Cast->TargetType);
      visitExpr(Cast->Expression.get());
    } else if (auto *Cede = dynamic_cast<CedeExpr *>(E)) {
      visitExpr(Cede->Value.get());
    } else if (auto *Closure = dynamic_cast<ClosureExpr *>(E)) {
      Closure->ReturnType = sub(Closure->ReturnType);
      visitStmt(Closure->Body.get());
    } else if (auto *SE = dynamic_cast<SizeOfExpr *>(E)) {
      sub(SE->TypeSyntax, SE->TypeStr);
    } else if (auto *Addr = dynamic_cast<AddressOfExpr *>(E)) {
      visitExpr(Addr->Expression.get());
    } else if (auto *Mem = dynamic_cast<MemberExpr *>(E)) {
      visitExpr(Mem->Object.get());
    } else if (dynamic_cast<VariableExpr *>(E)) {
      // Keep value identifiers intact. Const parameters are exact compile-time
      // symbols in the instance scope; spelling substitution would create a
      // VariableExpr named "4" instead of a numeric literal.
    } else if (auto *Call = dynamic_cast<CallExpr *>(E)) {
      // Call->Callee could rely on T? e.g. T::new() -> i32::new()
      // T::new is parsed as "T::new" string in Callee.
      Call->Callee = sub(Call->Callee);
      for (auto &Arg : Call->Args)
        visitExpr(Arg.get());
      for (size_t i = 0; i < Call->GenericArgs.size(); ++i) {
        if (i < Call->GenericArgSyntax.size())
          sub(Call->GenericArgSyntax[i], Call->GenericArgs[i]);
        else
          Call->GenericArgs[i] = sub(Call->GenericArgs[i]);
      }
    } else if (auto *New = dynamic_cast<NewExpr *>(E)) {
      sub(New->TypeSyntax, New->Type);
      visitExpr(New->Initializer.get());
    } else if (auto *Alloc = dynamic_cast<AllocExpr *>(E)) {
      sub(Alloc->TypeSyntax, Alloc->TypeName);
      visitExpr(Alloc->Initializer.get());
      visitExpr(Alloc->ArraySize.get());
    } else if (auto *ArrayInit = dynamic_cast<ArrayInitExpr *>(E)) {
      sub(ArrayInit->TypeSyntax, ArrayInit->Type);
      visitExpr(ArrayInit->Initializer.get());
      visitExpr(ArrayInit->ArraySize.get());
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
      if (auto *count = dynamic_cast<VariableExpr *>(Rep->Count.get())) {
        auto replacement = active().find(count->Name);
        if (replacement != active().end() && replacement->second) {
          const std::string spelling = replacement->second->toString();
          try {
            size_t consumed = 0;
            uint64_t value = std::stoull(spelling, &consumed);
            if (consumed == spelling.size()) {
              auto literal = std::make_unique<NumberExpr>(value);
              literal->Loc = count->Loc;
              Rep->Count = std::move(literal);
            }
          } catch (...) {
          }
        }
      }
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
    const std::vector<std::shared_ptr<toka::Type>> &GenericArgs,
    ShapeDecl *ConcreteOwner) {
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
                            bounds, GenericArgs[i],
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
  std::map<std::string, std::shared_ptr<toka::Type>> Replacements;
  std::set<std::string> MorphicParams;
  for (size_t i = 0; i < Template->GenericParams.size(); ++i) {
    std::string k = Template->GenericParams[i].Name;
    Replacements[k] = GenericArgs[i];
    if (Template->GenericParams[i].IsMorphic) {
      MorphicParams.insert(k);
      if (!k.empty() && k[0] == '\'') {
        MorphicParams.insert(k.substr(1));
      }
    }
    if (!k.empty() && k[0] == '\'')
      Replacements[k.substr(1)] = GenericArgs[i];
  }
  std::shared_ptr<toka::Type> concreteSelf;
  if (ConcreteOwner) {
    auto exactSelf = std::make_shared<toka::ShapeType>(ConcreteOwner->Name);
    exactSelf->resolve(ConcreteOwner);
    concreteSelf = exactSelf;
  } else {
    concreteSelf = resolveType(toka::Type::fromString(ConcreteTypeName));
  }
  if (concreteSelf)
    Replacements["Self"] = concreteSelf;

  // The instantiated body is checked with exact aliases for generic type
  // parameters.  Keep those names in its source syntax so a bound nominal
  // Type never passes through the lossy Type::toSyntax() representation.
  // Self is already a concrete materialized shape and const parameters must
  // still be substituted into array extents and other type-level constants.
  std::map<std::string, std::shared_ptr<toka::Type>> BodyReplacements;
  if (concreteSelf)
    BodyReplacements["Self"] = concreteSelf;
  for (size_t i = 0; i < Template->GenericParams.size(); ++i) {
    const auto &parameter = Template->GenericParams[i];
    if (!parameter.IsConst)
      continue;
    BodyReplacements[parameter.Name] = GenericArgs[i];
    if (!parameter.Name.empty() && parameter.Name.front() == '\'')
      BodyReplacements[parameter.Name.substr(1)] = GenericArgs[i];
  }

  // 3. Clone and Substitute
  GenericInstantiator Instantiator(Replacements, BodyReplacements,
                                   MorphicParams);
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
    auto semanticReturn =
        Method->ReturnTypeSyntax
            ? toka::Type::fromSyntax(Method->ReturnTypeSyntax)
            : toka::Type::fromString(Method->ReturnType);
    ClonedFn->ResolvedReturnType =
        semanticReturn ? semanticReturn->substitute(Replacements) : nullptr;
    for (size_t i = 0; i < Method->Args.size() &&
                       i < ClonedFn->Args.size();
         ++i) {
      auto semanticArgument =
          Sema::synthesizePhysicalTypeObject(Method->Args[i], false);
      ClonedFn->Args[i].ResolvedType =
          semanticArgument ? semanticArgument->substitute(Replacements)
                           : nullptr;
    }
    InstantiationLexicalScopes[ClonedFn.get()] = instantiationScope;
    auto definition = DeclarationLexicalScopes.find(Method.get());
    if (definition != DeclarationLexicalScopes.end())
      DeclarationLexicalScopes[ClonedFn.get()] = definition->second;
    for (const auto &arg : GenericArgs)
      recordInstantiationType(ClonedFn.get(), resolveType(arg));
    recordInstantiationType(
        ClonedFn.get(), resolveType(std::make_shared<toka::ShapeType>(
                            ConcreteTypeName)));

    NewMethods.push_back(std::move(ClonedFn));
  }

  std::vector<AssociatedTypeDecl> NewAssociatedTypes =
      Template->AssociatedTypes;
  for (size_t i = 0; i < NewAssociatedTypes.size(); ++i) {
    auto &Assoc = NewAssociatedTypes[i];
    const auto &TemplateAssoc = Template->AssociatedTypes[i];
    auto semanticType =
        TemplateAssoc.ResolvedType
            ? TemplateAssoc.ResolvedType
            : TemplateAssoc.TypeSyntax
                  ? toka::Type::fromSyntax(TemplateAssoc.TypeSyntax)
                  : toka::Type::fromString(TemplateAssoc.Type);
    Assoc.ResolvedType =
        semanticType ? semanticType->substitute(Replacements) : nullptr;
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
  NewImpl->TemplateOrigin = Template;
  NewImpl->ResolvedOwner = ConcreteOwner;
  if (!NewImpl->ResolvedOwner) {
    if (auto concreteShape =
            std::dynamic_pointer_cast<ShapeType>(concreteSelf))
      NewImpl->ResolvedOwner = concreteShape->Decl;
  }
  NewImpl->Loc = Template->Loc; // rough loc

  // 5. Register and Check
  ImplDecl *RawPtr = NewImpl.get();

  // We must add to M.Impls to own it.
  GenericInstancesModule->Impls.push_back(std::move(NewImpl));

  auto templateOwner = DeclarationLexicalScopes.find(Template);
  if (templateOwner != DeclarationLexicalScopes.end())
    DeclarationLexicalScopes[RawPtr] = templateOwner->second;

  const std::string family = getTraitFamilyName(Template->TraitName);
  if (family == "Encap") {
    std::string base = Template->TypeName;
    if (size_t generic = base.find('<'); generic != std::string::npos)
      base.resize(generic);
    ShapeDecl *templateShape = findVisibleShapeDecl(base, Template->Loc);
    ShapeDecl *instanceShape =
        findVisibleShapeDecl(ConcreteTypeName, RawPtr->Loc);
    auto policy = templateShape ? Slice2PolicyMap.find(templateShape)
                                : Slice2PolicyMap.end();
    if (instanceShape && policy != Slice2PolicyMap.end()) {
      Slice2PolicyMap[instanceShape] =
          Slice2Policy{RawPtr, policy->second.Owner, RawPtr->EncapEntries};
    }
  }
  registerSlice4Impl(RawPtr);

  enterScope();
  for (size_t i = 0; i < Template->GenericParams.size(); ++i) {
    const auto &parameter = Template->GenericParams[i];
    const auto &argument = GenericArgs[i];
    if (parameter.IsConst) {
      SymbolInfo constInfo;
      constInfo.TypeObj = toka::Type::fromString(
          parameter.Type.empty() ? "usize" : parameter.Type);
      uint64_t value = 0;
      bool hasValue = false;
      try {
        const std::string spelling = argument ? argument->toString() : "";
        size_t consumed = 0;
        value = std::stoull(spelling, &consumed);
        hasValue = consumed == spelling.size();
      } catch (...) {
      }
      constInfo.HasConstValue = hasValue;
      if (hasValue) {
        constInfo.ConstValue = value;
        constInfo.ConstValObj = ComptimeValue(value);
      }
      CurrentScope->define(parameter.Name, constInfo);
      if (!parameter.Name.empty() && parameter.Name.front() == '\'')
        CurrentScope->define(parameter.Name.substr(1), constInfo);
    } else {
      SymbolInfo aliasInfo;
      aliasInfo.TypeObj = argument;
      aliasInfo.IsTypeAlias = true;
      CurrentScope->define(parameter.Name, aliasInfo);
      if (!parameter.Name.empty() && parameter.Name.front() == '\'')
        CurrentScope->define(parameter.Name.substr(1), aliasInfo);
    }
  }
  if (concreteSelf) {
    SymbolInfo selfInfo;
    selfInfo.TypeObj = concreteSelf;
    selfInfo.IsTypeAlias = true;
    CurrentScope->define("Self", selfInfo);
  }

  // Now Register it!
  registerImpl(RawPtr);

  // Now Check it!
  // This will check method bodies
  checkImpl(RawPtr);
  exitScope();

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
  for (const auto &bound : Bounds)
    result.push_back(substituteTextualName(bound, replacements));
  return result;
}

bool Sema::checkTraitBounds(SourceLocation Loc, const std::string &ParamName, 
                            const std::vector<std::string> &TraitBounds, 
                            const std::shared_ptr<toka::Type> &ConcreteType,
                            bool isSilent,
                            SourceLocation BoundLoc) {
  bool success = true;
  auto resolvedConcreteType = resolveType(ConcreteType);
  const std::string concreteTypeName =
      resolvedConcreteType ? resolvedConcreteType->toString() : "unknown";
  // A morphic argument can carry its value-mutable suffix through template
  // substitution (for example `TaskPtr#`). Trait facts are nominal and are
  // registered on `TaskPtr`, so normalize only the lookup/proof key.
  std::string nominalConcreteType =
      toka::Type::stripMorphology(concreteTypeName);
  if (auto shape = std::dynamic_pointer_cast<ShapeType>(
          resolvedConcreteType ? resolvedConcreteType->getSoulType()
                               : nullptr)) {
    if (shape->Decl) {
      nominalConcreteType = shape->Decl->CodegenName.empty()
                                ? shape->Decl->Name
                                : shape->Decl->CodegenName;
    }
  }

  for (const auto &bound : TraitBounds) {
    SourceLocation visibilityLoc = BoundLoc.isValid() ? BoundLoc : Loc;
    TraitDecl *trait = findVisibleTraitDecl(bound, visibilityLoc);
    std::string canonicalBound = canonicalTraitName(bound, trait);
    std::string implKey = nominalConcreteType + "@" + canonicalBound;
    if (ImplMap.count(implKey)) continue;

    if (getTraitFamilyName(canonicalBound) == "Copy") {
      if (proveSlice4CopyType(resolvedConcreteType))
        continue;
    }
    if (getTraitFamilyName(canonicalBound) == "Dup") {
      if (proveSlice4CopyType(resolvedConcreteType))
        continue;
    }

    // [NEW] Fallback for Auto Traits
    if (getTraitFamilyName(canonicalBound) == "Callable") {
      if (resolvedConcreteType &&
          (resolvedConcreteType->isFunction() ||
           resolvedConcreteType->isDynFn()))
        continue;
    } else if (canonicalBound == "Send") {
      if (resolvedConcreteType && resolvedConcreteType->isSend(this)) continue;
    } else if (canonicalBound == "Sync") {
      if (resolvedConcreteType && resolvedConcreteType->isSync(this)) continue;
    }

    if (!isSilent) {
      DiagnosticEngine::report(Loc, DiagID::ERR_TRAIT_BOUND_UNSATISFIED,
                               concreteTypeName, bound, ParamName);
      HasError = true;
    }
    success = false;
  }
  return success;
}

} // namespace toka
