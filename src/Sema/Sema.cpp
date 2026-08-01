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
#include "toka/Sema.h"
#include "toka/DiagnosticEngine.h"
#include "toka/HandleSurfaceStats.h"
#include "toka/SourceManager.h"
#include "toka/Type.h"
#include "toka/PathUtils.h"
#include "toka/Parser.h"
#include <algorithm>
#include <cassert>
#include <cctype>
#include <filesystem>
#include <functional> // [NEW] Added for std::function
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace toka {

#ifdef TOKA_DEBUG_BINDING_PERMISSION
template <typename NodeT>
static void debugCheckBindingPermission(const NodeT &node) {
  assert(node.Permission.matchesLegacy(
      node.IsRawPointer, node.IsUnique, node.IsShared, node.IsReference,
      node.IsRebindable, node.IsPointerNullable, node.IsRebindBlocked,
      node.IsValueMutable, node.IsValueNullable, node.IsValueBlocked,
      node.IsMorphicExempt));
}

static const char *debugMorphologyName(BindingMorphology morphology) {
  switch (morphology) {
  case BindingMorphology::Raw:
    return "raw";
  case BindingMorphology::Unique:
    return "unique";
  case BindingMorphology::Shared:
    return "shared";
  case BindingMorphology::Reference:
    return "reference";
  case BindingMorphology::None:
    return "none";
  }
  return "none";
}

static bool debugTypeStringHasOuterMorphology(const std::string &typeName) {
  if (typeName.empty())
    return false;
  auto parsed = toka::Type::fromString(typeName);
  return parsed && (parsed->isRawPointer() || parsed->isUniquePtr() ||
                    parsed->isSharedPtr() || parsed->isReference());
}

static void debugPrintLocation(SourceLocation loc) {
  if (!DiagnosticEngine::SrcMgr || loc.isInvalid())
    return;

  auto fullLoc = DiagnosticEngine::SrcMgr->getFullSourceLoc(loc);
  if (!fullLoc.isValid())
    return;

  std::cerr << " at " << fullLoc.FileName << ":" << fullLoc.Line << ":"
            << fullLoc.Column;
}

static void debugCheckBindingTypeString(const char *nodeKind,
                                        const std::string &name,
                                        const std::string &typeName,
                                        const BindingPermission &permission,
                                        SourceLocation loc) {
  if (typeName.empty())
    return;

  bool typeHasMorphology = debugTypeStringHasOuterMorphology(typeName);
  bool typeHasIdentityNull = typeName.rfind("nul ", 0) == 0;
  if (!typeHasMorphology && !typeHasIdentityNull)
    return;
  if (permission.MorphicExempt)
    return;

  bool permissionHasHandle =
      permission.Morphology != BindingMorphology::None ||
      permission.IdentityNullable || permission.IdentityRebindable ||
      permission.IdentityBlocked;

  std::cerr << "[TOKA_DEBUG_BINDING_PERMISSION] " << nodeKind << " '"
            << name << "'";
  debugPrintLocation(loc);
  std::cerr << (permissionHasHandle
                    ? " has both binding permission and type-string "
                      "handle morphology"
                    : " has type-string handle morphology without binding "
                      "permission")
            << ": type='" << typeName
            << "', morphology=" << debugMorphologyName(permission.Morphology)
            << ", identityNullable=" << permission.IdentityNullable
            << ", identityRebindable=" << permission.IdentityRebindable
            << ", identityBlocked=" << permission.IdentityBlocked << "\n";
}

static void debugCheckShapeMemberPermissions(const ShapeMember &member,
                                             SourceLocation loc) {
  debugCheckBindingPermission(member);
  debugCheckBindingTypeString("shape member", member.Name, member.Type,
                              member.Permission, loc);
  for (const auto &subMember : member.SubMembers) {
    debugCheckShapeMemberPermissions(subMember, loc);
  }
}
#else
template <typename NodeT>
static void debugCheckBindingPermission(const NodeT &) {}
static void debugCheckBindingTypeString(const char *, const std::string &,
                                        const std::string &,
                                        const BindingPermission &,
                                        SourceLocation) {}
static void debugCheckShapeMemberPermissions(const ShapeMember &,
                                             SourceLocation) {}
#endif

static bool isModuleScopedLibFunction(const Module &M, const std::string &name) {
  if (name == "main") {
    return false;
  }
  if (name.rfind("__toka_", 0) == 0) {
    return false;
  }
  std::string path = M.SourcePath.empty() ? M.ResolvedPath : M.SourcePath;
  if (path.empty()) {
    return false;
  }
  path = toka::PathUtils::canonicalize(path);
  return path.find("/lib/") != std::string::npos;
}

static uint64_t fnv1a64(const std::string &text) {
  uint64_t hash = 14695981039346656037ULL;
  for (unsigned char c : text) {
    hash ^= c;
    hash *= 1099511628211ULL;
  }
  return hash;
}

static std::string sanitizeSymbolPart(const std::string &text) {
  std::string out;
  out.reserve(text.size());
  for (unsigned char c : text) {
    out += (std::isalnum(c) || c == '_') ? static_cast<char>(c) : '_';
  }
  return out.empty() ? "_" : out;
}

static std::string moduleScopedCodegenName(const Module &M,
                                           const std::string &name) {
  if (!isModuleScopedLibFunction(M, name)) {
    return name;
  }
  std::string path = M.SourcePath.empty() ? M.ResolvedPath : M.SourcePath;
  path = toka::PathUtils::canonicalize(path);
  return "__toka_mod_" + std::to_string(fnv1a64(path)) + "_" +
         sanitizeSymbolPart(name);
}

static std::string shapeCodegenName(const Module &M,
                                    const std::string &name) {
  std::string path = M.SourcePath.empty() ? M.ResolvedPath : M.SourcePath;
  if (path.empty())
    return name;
  path = toka::PathUtils::canonicalize(path);
  return "__toka_shape_" + std::to_string(fnv1a64(path)) + "_" +
         sanitizeSymbolPart(name);
}

static std::string shapeCodegenName(const ShapeDecl &shape) {
  if (!shape.Loc.isValid() || !DiagnosticEngine::SrcMgr)
    return shape.Name;
  std::string path = DiagnosticEngine::SrcMgr->getFullSourceLoc(shape.Loc).FileName;
  path = toka::PathUtils::canonicalize(path);
  return "__toka_shape_" + std::to_string(fnv1a64(path)) + "_" +
         sanitizeSymbolPart(shape.Name);
}

static std::string defaultModuleNameForImport(const std::string &importPath) {
  std::string path = toka::PathUtils::normalize(importPath);
  while (path.size() > 1 && path.back() == '/') {
    path.pop_back();
  }

  size_t slash = path.find_last_of('/');
  std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
  const std::string suffixes[] = {".tk_lib", ".tki", ".tk"};
  for (const auto &suffix : suffixes) {
    if (name.size() > suffix.size() &&
        name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0) {
      name.resize(name.size() - suffix.size());
      break;
    }
  }
  return name.empty() ? importPath : name;
}

static bool isUnsafeType(const std::shared_ptr<toka::Type>& T) {
  if (!T) return false;
  if (T->typeKind == toka::Type::RawPtr) return true;
  
  if (T->isFunction()) {
    auto FT = std::dynamic_pointer_cast<toka::FunctionType>(T);
    if (FT) {
      if (isUnsafeType(FT->ReturnType)) return true;
      for (const auto& P : FT->ParamTypes) {
        if (isUnsafeType(P)) return true;
      }
    }
  }
  if (T->isDynFn()) {
    auto DFT = std::dynamic_pointer_cast<toka::DynFnType>(T);
    if (DFT) {
      if (isUnsafeType(DFT->ReturnType)) return true;
      for (const auto& P : DFT->ParamTypes) {
        if (isUnsafeType(P)) return true;
      }
    }
  }
  if (T->getPointeeType()) {
    if (isUnsafeType(T->getPointeeType())) return true;
  }
  if (T->getArrayElementType()) {
    if (isUnsafeType(T->getArrayElementType())) return true;
  }
  if (T->isUninit()) {
    auto UT = std::dynamic_pointer_cast<toka::UninitType>(T);
    if (UT && isUnsafeType(UT->InnerType)) return true;
  }
  return false;
}

static bool isUnsafePublicAPIExempt(const Module *module,
                                    SourceLocation loc) {
  if (module && module->IsInterface) {
    return module->IsTrustedSystemModule;
  }

  std::string path;
  if (loc.isValid()) {
    path = DiagnosticEngine::SrcMgr->getFullSourceLoc(loc).FileName;
  }
  if (path.empty() && module) {
    path = module->SourcePath;
  }
  return path.find("build.tk") != std::string::npos ||
         path.find("prelude") != std::string::npos ||
         path.find("tests/pass/") != std::string::npos ||
         path.find("lib/") != std::string::npos;
}

void Sema::checkUnsafePublicFunctionBoundary(FunctionDecl *Fn) {
  bool trustedDeclaration = false;
  auto owner = DeclarationLexicalScopes.find(Fn);
  if (owner != DeclarationLexicalScopes.end() && owner->second) {
    trustedDeclaration = owner->second->IsTrustedSystemModule;
  }
  if (isUnsafePublicAPIExempt(CurrentModule, Fn->Loc) || trustedDeclaration ||
      !Fn->IsPub ||
      Fn->Name.rfind("unsafe_", 0) == 0 ||
      Fn->Name.rfind("raw_", 0) == 0 || Fn->Name.rfind("__", 0) == 0) {
    return;
  }

  for (auto &Arg : Fn->Args) {
    std::shared_ptr<toka::Type> type = Arg.ResolvedType;
    if (!type) {
      type = resolveType(Sema::synthesizePhysicalTypeObject(Arg, false));
    }
    if (isUnsafeType(type)) {
      DiagnosticEngine::report(Fn->Loc, DiagID::ERR_EXPOSED_UNSAFE_TYPE,
                               Fn->Name, type->toString(), Arg.Name);
      HasError = true;
    }
  }

  if (Fn->ReturnType != "void") {
    std::shared_ptr<toka::Type> type = Fn->ResolvedReturnType;
    if (!type) {
      type = resolveType(Fn->ReturnTypeSyntax
                             ? toka::Type::fromSyntax(Fn->ReturnTypeSyntax)
                             : toka::Type::fromString(Fn->ReturnType));
    }
    if (isUnsafeType(type)) {
      DiagnosticEngine::report(Fn->Loc, DiagID::ERR_EXPOSED_UNSAFE_RET,
                               Fn->Name, type->toString());
      HasError = true;
    }
  }
}

void Sema::checkUnsafePublicShapeBoundary(ShapeDecl *Shape) {
  bool trustedDeclaration = false;
  auto owner = DeclarationLexicalScopes.find(Shape);
  if (owner != DeclarationLexicalScopes.end() && owner->second) {
    trustedDeclaration = owner->second->IsTrustedSystemModule;
  }
  if (isUnsafePublicAPIExempt(CurrentModule, Shape->Loc) ||
      trustedDeclaration || !Shape->IsPub ||
      Shape->Name == "cstr" || Shape->Name.rfind("Unsafe", 0) == 0 ||
      Shape->Name.rfind("Raw", 0) == 0) {
    return;
  }

  auto checkMember = [&](ShapeMember &member) {
    std::shared_ptr<toka::Type> type = member.ResolvedType;
    if (!type) {
      type = resolveType(Sema::synthesizePhysicalTypeObject(member));
    }
    if (isUnsafeType(type)) {
      DiagnosticEngine::report(Shape->Loc,
                               DiagID::ERR_EXPOSED_UNSAFE_FIELD, Shape->Name,
                               member.Name, type->toString());
      HasError = true;
    }
  };

  for (auto &member : Shape->Members) {
    checkMember(member);
    for (auto &subMember : member.SubMembers) {
      checkMember(subMember);
    }
  }
}

static bool isTypeNameBoundary(char c) {
  return !std::isalnum(static_cast<unsigned char>(c)) && c != '_' && c != '\'';
}

static void replaceTypeNameToken(std::string &text, const std::string &from,
                                 const std::string &to) {
  if (from.empty() || from == to)
    return;

  size_t pos = 0;
  while ((pos = text.find(from, pos)) != std::string::npos) {
    bool startOk = pos == 0 || isTypeNameBoundary(text[pos - 1]);
    size_t end = pos + from.size();
    bool endOk = end == text.size() || isTypeNameBoundary(text[end]);
    if (startOk && endOk) {
      text.replace(pos, from.size(), to);
      pos += to.size();
    } else {
      pos += from.size();
    }
  }
}

// Source-derived type spellings stay paired with TypeSyntax through Sema.
static void substituteSourceTypeSyntax(TypeSyntaxPtr &syntax,
                                      std::string &spelling,
                                      const std::string &from,
                                      const std::string &to) {
  if (!syntax) {
    replaceTypeNameToken(spelling, from, to);
    return;
  }
  if (from == to) {
    spelling = syntax->toCanonicalString();
    return;
  }

  std::map<std::string, TypeSyntaxPtr> replacements;
  replacements.emplace(from, TypeSyntax::named(to, syntax->Begin, syntax->End));
  syntax = syntax->substitute(replacements);
  spelling = syntax->toCanonicalString();
}

static void substituteSourceTypeSyntax(
    TypeSyntaxPtr &syntax, std::string &spelling, const std::string &from,
    const std::shared_ptr<toka::Type> &to) {
  if (!to) {
    substituteSourceTypeSyntax(syntax, spelling, from, "unknown");
    return;
  }
  if (!syntax) {
    replaceTypeNameToken(spelling, from, to->toString());
    return;
  }

  std::map<std::string, TypeSyntaxPtr> replacements;
  replacements.emplace(from, to->toSyntax(syntax->Begin, syntax->End));
  syntax = syntax->substitute(replacements);
  spelling = syntax->toCanonicalString();
}

static size_t findTopLevelChar(const std::string &s, char target,
                               size_t start = 0) {
  int balance = 0;
  for (size_t i = start; i < s.size(); ++i) {
    char c = s[i];
    if (balance == 0 && c == target) {
      return i;
    }
    if (c == '<' || c == '(' || c == '[') {
      balance++;
    } else if (c == '>' || c == ')' || c == ']') {
      if (balance > 0)
        balance--;
    }
  }
  return std::string::npos;
}

static size_t findTopLevelDoubleColon(const std::string &s,
                                      size_t start = 0) {
  int balance = 0;
  for (size_t i = start; i + 1 < s.size(); ++i) {
    char c = s[i];
    if (c == '<' || c == '(' || c == '[') {
      balance++;
    } else if (c == '>' || c == ')' || c == ']') {
      if (balance > 0)
        balance--;
    } else if (balance == 0 && c == ':' && s[i + 1] == ':') {
      return i;
    }
  }
  return std::string::npos;
}

static std::string trimTypeString(const std::string &s) {
  size_t first = s.find_first_not_of(" \t\r\n");
  if (first == std::string::npos)
    return "";
  size_t last = s.find_last_not_of(" \t\r\n");
  return s.substr(first, last - first + 1);
}

std::string Sema::getTraitFamilyName(const std::string &traitName) const {
  std::string clean = trimTypeString(traitName);
  if (!clean.empty() && clean[0] == '@') {
    clean = clean.substr(1);
  }
  size_t lt = findTopLevelChar(clean, '<');
  if (lt != std::string::npos) {
    clean = clean.substr(0, lt);
  }
  return clean;
}

std::string Sema::canonicalTraitName(const std::string &traitName,
                                     const TraitDecl *trait) const {
  std::string clean = trimTypeString(traitName);
  if (!clean.empty() && clean[0] == '@')
    clean.erase(0, 1);
  if (!trait)
    return clean;

  size_t generic = findTopLevelChar(clean, '<');
  return trait->Name +
         (generic == std::string::npos ? "" : clean.substr(generic));
}

TraitDecl *Sema::findTraitDecl(const std::string &traitName) const {
  std::string family = getTraitFamilyName(traitName);
  auto it = TraitMap.find(family);
  if (it == TraitMap.end())
    return nullptr;
  return it->second;
}

TraitDecl *Sema::findVisibleTraitDecl(const std::string &traitName,
                                      SourceLocation loc) {
  std::string family = getTraitFamilyName(traitName);
  auto findInModule = [&](ModuleScope *module) -> TraitDecl * {
    if (!module)
      return nullptr;
    auto symbol = module->LexicalTypes.find(family);
    if (symbol == module->LexicalTypes.end())
      return nullptr;
    if (!symbol->second.IsTraitName || !symbol->second.ASTPtr)
      return nullptr;
    auto *trait = static_cast<TraitDecl *>(symbol->second.ASTPtr);
    symbol->second.HasBeenUsed = true;
    if (symbol->second.ImportingDecl) {
      const_cast<ImportDecl *>(symbol->second.ImportingDecl)->HasBeenUsed =
          true;
    }
    return trait;
  };

  if (loc.isValid()) {
    if (TraitDecl *trait = findInModule(getLexicalModule(loc)))
      return trait;
  }

  if (CurrentFunction) {
    auto owner = DeclarationLexicalScopes.find(CurrentFunction);
    if (owner != DeclarationLexicalScopes.end()) {
      if (TraitDecl *trait = findInModule(owner->second))
        return trait;
    }
  }

  if (CurrentModule) {
    ModuleScope *module = nullptr;
    if (!CurrentModule->ResolvedPath.empty())
      module = getModule(CurrentModule->ResolvedPath);
    if (!module && CurrentModule->Loc.isValid())
      module = getLexicalModule(CurrentModule->Loc);
    if (!module && !CurrentModule->SourcePath.empty())
      module = getModule(CurrentModule->SourcePath);
    if (TraitDecl *trait = findInModule(module))
      return trait;
  }

  if (CurrentFunction) {
    auto context = InstantiationLexicalScopes.find(CurrentFunction);
    if (context != InstantiationLexicalScopes.end()) {
      if (TraitDecl *trait = findInModule(context->second))
        return trait;
    }
  }

  return nullptr;
}

ShapeDecl *Sema::findVisibleShapeDecl(const std::string &shapeName,
                                      SourceLocation loc) {
  auto findInModule = [&](ModuleScope *module,
                          const std::string &name) -> ShapeDecl * {
    if (!module)
      return nullptr;
    auto symbol = module->LexicalTypes.find(name);
    if (symbol == module->LexicalTypes.end() || !symbol->second.IsTypeName ||
        symbol->second.IsTraitName || !symbol->second.ASTPtr)
      return nullptr;

    auto findRegisteredShape = [](ModuleScope *scope,
                                  void *declaration) -> ShapeDecl * {
      if (!scope)
        return nullptr;
      for (const auto &[_, shape] : scope->Shapes) {
        if (shape == declaration)
          return shape;
      }
      return nullptr;
    };
    ShapeDecl *shape = findRegisteredShape(module, symbol->second.ASTPtr);
    if (!shape) {
      shape = findRegisteredShape(
          static_cast<ModuleScope *>(symbol->second.ReferencedModule),
          symbol->second.ASTPtr);
    }
    if (!shape)
      return nullptr;

    symbol->second.HasBeenUsed = true;
    if (symbol->second.ImportingDecl) {
      const_cast<ImportDecl *>(symbol->second.ImportingDecl)->HasBeenUsed =
          true;
    }
    return shape;
  };

  size_t scope = shapeName.find("::");
  if (scope != std::string::npos && CurrentScope) {
    std::string moduleName = shapeName.substr(0, scope);
    std::string targetName = shapeName.substr(scope + 2);
    SymbolInfo *moduleInfo = nullptr;
    std::string actualName = moduleName;
    if (CurrentScope->findVariableWithDeref(moduleName, moduleInfo,
                                            actualName) &&
        moduleInfo && moduleInfo->ReferencedModule) {
      return findInModule(
          static_cast<ModuleScope *>(moduleInfo->ReferencedModule),
          targetName);
    }
  }

  if (loc.isValid()) {
    if (ShapeDecl *shape = findInModule(getLexicalModule(loc), shapeName))
      return shape;
  }

  if (CurrentFunction) {
    auto owner = DeclarationLexicalScopes.find(CurrentFunction);
    if (owner != DeclarationLexicalScopes.end()) {
      if (ShapeDecl *shape = findInModule(owner->second, shapeName))
        return shape;
    }
  }

  if (CurrentModule) {
    ModuleScope *module = nullptr;
    if (!CurrentModule->ResolvedPath.empty())
      module = getModule(CurrentModule->ResolvedPath);
    if (!module && CurrentModule->Loc.isValid())
      module = getLexicalModule(CurrentModule->Loc);
    if (!module && !CurrentModule->SourcePath.empty())
      module = getModule(CurrentModule->SourcePath);
    if (ShapeDecl *shape = findInModule(module, shapeName))
      return shape;
  }

  if (CurrentFunction) {
    auto context = InstantiationLexicalScopes.find(CurrentFunction);
    if (context != InstantiationLexicalScopes.end()) {
      if (ShapeDecl *shape = findInModule(context->second, shapeName))
        return shape;
    }
  }

  auto fallback = ShapeMap.find(shapeName);
  return fallback == ShapeMap.end() ? nullptr : fallback->second;
}

std::string Sema::genericImplKey(const ShapeDecl *shape) {
  if (!shape)
    return {};
  return shape->CodegenName.empty() ? shape->Name : shape->CodegenName;
}

std::string Sema::genericImplKey(const std::string &typeName,
                                 SourceLocation loc) {
  std::string baseName = typeName;
  size_t lt = baseName.find('<');
  if (lt != std::string::npos)
    baseName = baseName.substr(0, lt);
  if (ShapeDecl *shape = findVisibleShapeDecl(baseName, loc))
    return genericImplKey(shape);
  return baseName;
}

std::string Sema::canonicalTypeFactKey(const std::string &typeName,
                                       SourceLocation loc) {
  std::string baseName = typeName;
  size_t generic = baseName.find('<');
  if (generic != std::string::npos)
    baseName.resize(generic);

  ShapeDecl *shape = findVisibleShapeDecl(baseName, loc);
  std::string definition = "unresolved:" + baseName;
  if (shape) {
    auto scope = DeclarationLexicalScopes.find(shape);
    if (scope != DeclarationLexicalScopes.end() && scope->second) {
      const ModuleScope *owner = scope->second;
      if (owner->ShadowCoordinateKnown) {
        definition = "crate:" + owner->ShadowCrateId + ";module:" +
                     owner->ShadowLogicalModulePath + ";shape:" +
                     shape->Name;
      } else {
        definition = "module:" + owner->Name + ";shape:" + shape->Name;
      }
    } else {
      definition = "shape:" +
                   (shape->CodegenName.empty() ? shape->Name : shape->CodegenName);
    }
  }

  return definition + ";concrete:" + resolveType(typeName);
}

std::string Sema::canonicalImplDefinitionId(const ImplDecl *impl) const {
  if (!impl)
    return "impl:<null>";

  auto recorded = Slice1ImplDefinitionIds.find(impl);
  if (recorded != Slice1ImplDefinitionIds.end())
    return recorded->second;

  std::string owner = "module:<unknown>";
  auto scope = DeclarationLexicalScopes.find(impl);
  if (scope != DeclarationLexicalScopes.end() && scope->second) {
    const ModuleScope *module = scope->second;
    if (module->ShadowCoordinateKnown) {
      owner = "crate:" + module->ShadowCrateId + ";module:" +
              module->ShadowLogicalModulePath;
    } else {
      owner = "module:" + module->Name;
    }
  }
  return owner + ";impl:" + impl->TypeName + "@" + impl->TraitName +
         ";loc:" + std::to_string(impl->Loc.getRawEncoding());
}

void Sema::recordSlice1ImplFact(ImplDecl *impl,
                                const std::string &resolvedTypeName,
                                const std::string &canonicalTrait) {
  if (!impl)
    return;

  const std::string key = impl->GenericParams.empty()
      ? canonicalTypeFactKey(
            resolvedTypeName.empty() ? impl->TypeName : resolvedTypeName,
            impl->Loc)
      : canonicalImplDefinitionId(impl) + ";template";
  const std::string family = getTraitFamilyName(canonicalTrait);

  ResourceContractMap.try_emplace(key, Slice1ResourceContract::None);
  PartialMovePlanMap.try_emplace(key, Slice1PartialMovePlan{});
  CopyProofMap.try_emplace(key, Slice1CopyProof::Unknown);
  CopyWitnessMap.try_emplace(key, Slice1CopyWitness::None);
  DupProviderMap.try_emplace(key, Slice1DupProvider::None);
  DropPlanMap.try_emplace(key, Slice1DropPlan::Unknown);

  if (family == "Encap") {
    PolicyMap[key] = { !impl->IsStructuralDrop, impl->IsStructuralDrop };
    if (impl->IsStructuralDrop) {
      DropPlanMap[key] = Slice1DropPlan::LegacyStructural;
    } else {
      for (const auto &method : impl->Methods) {
        if (method->Name == "drop") {
          DropPlanMap[key] = Slice1DropPlan::LegacyCustom;
          break;
        }
      }
    }
  } else if (family == "Copy") {
    CopyWitnessMap[key] = Slice1CopyWitness::ExplicitRequest;
  } else if (family == "Dup") {
    const bool valid = impl->Methods.size() == 1 &&
        impl->Methods.front()->Name == "dup" &&
        impl->Methods.front()->IsPub &&
        impl->Methods.front()->Args.size() == 1 &&
        impl->Methods.front()->Args.front().Name == "self" &&
        !impl->Methods.front()->Args.front().IsCeded &&
        !impl->Methods.front()->Args.front().IsUnique &&
        (impl->Methods.front()->ReturnType == "Self" ||
         resolveType(impl->Methods.front()->ReturnType) == resolvedTypeName);
    DupProviderMap[key] = valid ? Slice1DupProvider::UserCandidate
                                : Slice1DupProvider::InvalidCandidate;
  }
}

void Sema::dumpEncapSlice1FactsJSON(std::ostream &out) const {
  size_t invalidDupCandidates = 0;
  for (const auto &[_, provider] : DupProviderMap) {
    if (provider == Slice1DupProvider::InvalidCandidate)
      ++invalidDupCandidates;
  }
  out << "{\"schema\":\"toka.encap-slice1\",\"version\":1"
      << ",\"policy_fact_count\":" << PolicyMap.size()
      << ",\"resource_contract_fact_count\":" << ResourceContractMap.size()
      << ",\"drop_plan_fact_count\":" << DropPlanMap.size()
      << ",\"partial_move_plan_fact_count\":" << PartialMovePlanMap.size()
      << ",\"copy_proof_fact_count\":" << CopyProofMap.size()
      << ",\"copy_witness_fact_count\":" << CopyWitnessMap.size()
      << ",\"dup_provider_fact_count\":" << DupProviderMap.size()
      << ",\"dup_invalid_candidate_count\":" << invalidDupCandidates
      << ",\"generic_impl_instance_count\":" << GenericImplInstanceMap.size()
      << "}";
}

void Sema::registerSlice2Policy(ImplDecl *impl) {
  if (!impl || impl->IsStructuralDrop ||
      getTraitFamilyName(impl->TraitName) != "Encap")
    return;

  std::string base = impl->TypeName;
  size_t generic = base.find('<');
  if (generic != std::string::npos)
    base.resize(generic);
  ShapeDecl *shape = findVisibleShapeDecl(base, impl->Loc);
  auto implOwner = DeclarationLexicalScopes.find(impl);
  auto shapeOwner = shape ? DeclarationLexicalScopes.find(shape)
                          : DeclarationLexicalScopes.end();
  if (!shape || implOwner == DeclarationLexicalScopes.end() ||
      shapeOwner == DeclarationLexicalScopes.end() ||
      implOwner->second != shapeOwner->second) {
    DiagnosticEngine::report(impl->Loc, DiagID::ERR_GENERIC_SEMA,
                             "@Encap v2 policy must be declared in the nominal type's defining module");
    HasError = true;
    return;
  }
  unsigned dropHooks = 0;
  for (const auto &method : impl->Methods) {
    if (method->Name == "drop")
      ++dropHooks;
    else {
      DiagnosticEngine::report(impl->Loc, DiagID::ERR_GENERIC_SEMA,
                               "@Encap v2 accepts only exact field grants and one drop hook");
      HasError = true;
      return;
    }
  }
  if (dropHooks > 1) {
    DiagnosticEngine::report(impl->Loc, DiagID::ERR_GENERIC_SEMA,
                             "@Encap v2 accepts at most one drop hook");
    HasError = true;
    return;
  }
  if (dropHooks == 1) {
    const FunctionDecl *hook = nullptr;
    for (const auto &method : impl->Methods)
      if (method->Name == "drop") hook = method.get();
    const bool validHook = hook && !hook->IsPub &&
        hook->GenericParams.empty() && hook->Effect == EffectKind::None &&
        hook->ReturnType == "void" && hook->Args.size() == 1 &&
        Type::stripMorphology(hook->Args[0].Name) == "self" &&
        hook->Args[0].IsValueMutable;
    if (!validHook) {
      DiagnosticEngine::report(impl->Loc, DiagID::ERR_GENERIC_SEMA,
                               "@Encap v3 drop hook must be private fn drop(self#) -> void");
      HasError = true;
      return;
    }
  }
  for (const auto &[registeredShape, registered] : Slice2PolicyMap) {
    (void)registeredShape;
    std::string registeredBase = registered.Impl ? registered.Impl->TypeName : "";
    if (size_t registeredGeneric = registeredBase.find('<');
        registeredGeneric != std::string::npos)
      registeredBase.resize(registeredGeneric);
    if (registered.Impl != impl && registered.Owner == implOwner->second &&
        registeredBase == base) {
      DiagnosticEngine::report(impl->Loc, DiagID::ERR_GENERIC_SEMA,
                               "@Encap v2 permits exactly one policy declaration per nominal type");
      HasError = true;
      return;
    }
  }
  if (Slice2PolicyMap.count(shape) && Slice2PolicyMap[shape].Impl != impl) {
    DiagnosticEngine::report(impl->Loc, DiagID::ERR_GENERIC_SEMA,
                             "@Encap v2 permits exactly one policy declaration per nominal type");
    HasError = true;
    return;
  }
  for (const auto &parameter : impl->GenericParams) {
    if (!parameter.TraitBounds.empty()) {
      DiagnosticEngine::report(impl->Loc, DiagID::ERR_GENERIC_SEMA,
                               "@Encap v2 generic policies cannot have trait bounds or where constraints");
      HasError = true;
      return;
    }
  }
  if (!shape->GenericParams.empty() &&
      !genericImplAppliesToWholeShape(shape, impl)) {
    DiagnosticEngine::report(impl->Loc, DiagID::ERR_GENERIC_SEMA,
                             "@Encap v2 generic policies must apply uniformly to the nominal type's complete generic domain");
    HasError = true;
    return;
  }

  Slice2Policy policy;
  policy.Impl = impl;
  policy.Owner = implOwner->second;
  policy.Entries = impl->EncapEntries;
  Slice2PolicyMap[shape] = std::move(policy);
}

bool Sema::canNameEncapField(const ShapeDecl *shape, const std::string &field,
                             SourceLocation useLoc) {
  auto policyIt = Slice2PolicyMap.find(shape);
  const Slice2Policy *policyPtr =
      policyIt == Slice2PolicyMap.end() ? nullptr : &policyIt->second;
  // Imports can surface an equivalent declaration pointer through ShapeMap.
  // Recover the policy by its nominal name and defining module identity; never
  // fall back to spelling or physical path alone.
  if (!policyPtr) {
    auto shapeOwner = DeclarationLexicalScopes.find(shape);
    ModuleScope *nominalOwner =
        shapeOwner == DeclarationLexicalScopes.end()
            ? getLexicalModule(shape->Loc)
            : shapeOwner->second;
    for (const auto &[_, candidate] : Slice2PolicyMap) {
      std::string candidateName = candidate.Impl ? candidate.Impl->TypeName : "";
      if (size_t generic = candidateName.find('<'); generic != std::string::npos)
        candidateName.resize(generic);
      std::string shapeName = shape ? shape->Name : "";
      if (size_t generic = shapeName.find('<'); generic != std::string::npos)
        shapeName.resize(generic);
      if (!candidate.Owner || !candidate.Impl ||
          candidateName != shapeName ||
          !nominalOwner || candidate.Owner != nominalOwner)
        continue;
      policyPtr = &candidate;
      break;
    }
  }
  if (!policyPtr)
    return true;
  const Slice2Policy &policy = *policyPtr;
  ModuleScope *requester = getLexicalModule(useLoc);
  if (requester == policy.Owner)
    return true;

  for (const auto &entry : policy.Entries) {
    if (std::find(entry.Fields.begin(), entry.Fields.end(), field) ==
        entry.Fields.end())
      continue;
    return true;
  }
  return false;
}

void Sema::registerSlice4Impl(ImplDecl *impl) {
  if (!impl)
    return;
  auto owner = DeclarationLexicalScopes.find(impl);

  const std::string family = getTraitFamilyName(impl->TraitName);
  if (family == "Clone" || family == "Drop") {
    DiagnosticEngine::report(impl->Loc, DiagID::ERR_GENERIC_SEMA,
                             "@Encap v4 removes the @Clone and @Drop facets");
    HasError = true;
    return;
  }
  if (family != "Copy" && family != "Dup")
    return;

  std::string base = impl->TypeName;
  if (size_t generic = base.find('<'); generic != std::string::npos)
    base.resize(generic);
  ShapeDecl *shape = findVisibleShapeDecl(base, impl->Loc);
  auto shapeOwner = shape ? DeclarationLexicalScopes.find(shape)
                          : DeclarationLexicalScopes.end();
  if (!shape || owner == DeclarationLexicalScopes.end() ||
      shapeOwner == DeclarationLexicalScopes.end() ||
      owner->second != shapeOwner->second) {
    DiagnosticEngine::report(impl->Loc, DiagID::ERR_GENERIC_SEMA,
                             "@Copy and @Dup must be declared in the nominal type's defining module");
    HasError = true;
    return;
  }
  if (family == "Copy") {
    if (!impl->Methods.empty() || Slice4CopyRequests.count(shape)) {
      DiagnosticEngine::report(impl->Loc, DiagID::ERR_GENERIC_SEMA,
                               "@Copy is a unique empty compiler marker");
      HasError = true;
      return;
    }
    Slice4CopyRequests[shape] = impl;
    return;
  }

  const bool validDup = impl->Methods.size() == 1 &&
      impl->Methods.front()->Name == "dup" && impl->Methods.front()->IsPub &&
      impl->Methods.front()->Effect == EffectKind::None &&
      impl->Methods.front()->GenericParams.empty() &&
      impl->Methods.front()->Args.size() == 1 &&
      Type::stripMorphology(impl->Methods.front()->Args.front().Name) == "self" &&
      !impl->Methods.front()->Args.front().IsCeded &&
      !impl->Methods.front()->Args.front().IsValueMutable &&
      impl->Methods.front()->LifeDependencies.empty() &&
      Type::stripMorphology(impl->Methods.front()->ReturnType) ==
          impl->Methods.front()->ReturnType &&
      (impl->Methods.front()->ReturnType == "Self" ||
       resolveType(impl->Methods.front()->ReturnType) == resolveType(impl->TypeName));
  if (!validDup || Slice4DupProviders.count(shape)) {
    DiagnosticEngine::report(impl->Loc, DiagID::ERR_GENERIC_SEMA,
                             "@Dup requires one public fn dup(self) -> Self implementation");
    HasError = true;
    return;
  }
  Slice4DupProviders[shape] = impl;
}

bool Sema::proveSlice4CopyType(std::shared_ptr<toka::Type> type) {
  if (!type || type->isUnknown() || type->isUniquePtr() || type->isSharedPtr())
    return false;
  if (type->isRawPointer() || type->isReference() || type->isFunction() ||
      type->isDynFn() || type->isVoid() || type->isBoolean() ||
      type->isInteger() || type->isFloatingPoint())
    return true;
  if (type->isArray())
    return proveSlice4CopyType(type->getArrayElementType());
  if (!type->isShape())
    return false;
  const std::string name = type->getSoulName();
  auto shape = ShapeMap.find(name);
  return shape != ShapeMap.end() && proveSlice4Copy(shape->second);
}

bool Sema::proveSlice4Copy(const ShapeDecl *shape) {
  if (!shape)
    return false;
  auto cached = Slice4CopyProofs.find(shape);
  if (cached != Slice4CopyProofs.end())
    return cached->second == Slice1CopyProof::ProvenCopy;
  if (!Slice4CopyProofInProgress.insert(shape).second) {
    Slice4CopyProofs[shape] = Slice1CopyProof::ProvenNonCopy;
    return false;
  }

  bool copyable = !shape->HasExplicitDrop;
  const bool governed = Slice2PolicyMap.count(shape) != 0;
  if (governed && !Slice4CopyRequests.count(shape))
    copyable = false;
  for (const auto &member : shape->Members) {
    if (!copyable)
      break;
    copyable = proveSlice4CopyType(getPhysicalType(member));
    for (const auto &submember : member.SubMembers) {
      if (!copyable)
        break;
      copyable = proveSlice4CopyType(getPhysicalType(submember));
    }
  }
  Slice4CopyProofInProgress.erase(shape);
  Slice4CopyProofs[shape] = copyable ? Slice1CopyProof::ProvenCopy
                                      : Slice1CopyProof::ProvenNonCopy;
  CopyProofMap[canonicalTypeFactKey(shape->Name, shape->Loc)] =
      Slice4CopyProofs[shape];
  return copyable;
}

Sema::Slice4CopyRecipe
Sema::deriveSlice4CopyRecipeType(std::shared_ptr<toka::Type> type,
                                 const ShapeDecl *context) {
  auto always = [] {
    return Slice4CopyRecipe{Slice4CopyRecipeKind::Always, {}, ""};
  };
  auto never = [](const std::string &detail) {
    return Slice4CopyRecipe{Slice4CopyRecipeKind::Never, {}, detail};
  };
  auto dependent = [](const std::string &detail) {
    return Slice4CopyRecipe{Slice4CopyRecipeKind::Dependent, {}, detail};
  };
  auto normalizeParam = [](const std::string &name) {
    return !name.empty() && name.front() == '\'' ? name.substr(1) : name;
  };
  auto combine = [&](Slice4CopyRecipe &into, const Slice4CopyRecipe &next) {
    if (into.Kind == Slice4CopyRecipeKind::Never ||
        into.Kind == Slice4CopyRecipeKind::Dependent)
      return;
    if (next.Kind == Slice4CopyRecipeKind::Never ||
        next.Kind == Slice4CopyRecipeKind::Dependent) {
      into = next;
      return;
    }
    if (next.Kind == Slice4CopyRecipeKind::All) {
      if (into.Kind == Slice4CopyRecipeKind::Always)
        into.Kind = Slice4CopyRecipeKind::All;
      into.Requirements.insert(into.Requirements.end(),
                               next.Requirements.begin(),
                               next.Requirements.end());
    }
  };

  if (!type || type->isUnknown())
    return dependent("unresolved field type");
  if (type->isUniquePtr() || type->isSharedPtr())
    return never("ownership-bearing field");
  if (type->isRawPointer() || type->isReference() || type->isFunction() ||
      type->isDynFn() || type->isVoid() || type->isBoolean() ||
      type->isInteger() || type->isFloatingPoint())
    return always();
  if (type->isArray())
    return deriveSlice4CopyRecipeType(type->getArrayElementType(), context);
  if (!type->isShape())
    return dependent("unsupported field type");

  const std::string typeName = normalizeParam(type->getSoulName());
  if (context) {
    for (const auto &parameter : context->GenericParams) {
      if (normalizeParam(parameter.Name) == typeName) {
        return Slice4CopyRecipe{Slice4CopyRecipeKind::All, {typeName}, ""};
      }
    }
  }

  auto shapeType = std::dynamic_pointer_cast<toka::ShapeType>(type);
  ShapeDecl *nested = findVisibleShapeDecl(
      typeName, context ? context->Loc : SourceLocation());
  if (!nested)
    return dependent("unresolved field type " + typeName);
  if (nested->GenericParams.empty())
    return proveSlice4Copy(nested) ? always()
                                   : never("non-Copy field " + typeName);
  if (!shapeType ||
      shapeType->GenericArgs.size() != nested->GenericParams.size())
    return dependent("incomplete generic field " + typeName);

  Slice4CopyRecipe nestedRecipe = deriveSlice4CopyRecipe(nested);
  if (nestedRecipe.Kind == Slice4CopyRecipeKind::Never ||
      nestedRecipe.Kind == Slice4CopyRecipeKind::Dependent)
    return nestedRecipe;
  Slice4CopyRecipe result = always();
  for (const auto &requirement : nestedRecipe.Requirements) {
    size_t index = 0;
    while (index < nested->GenericParams.size() &&
           normalizeParam(nested->GenericParams[index].Name) != requirement)
      ++index;
    if (index == nested->GenericParams.size())
      return dependent("invalid nested Copy recipe for " + typeName);
    combine(result, deriveSlice4CopyRecipeType(shapeType->GenericArgs[index],
                                                context));
  }
  return result;
}

Sema::Slice4CopyRecipe Sema::deriveSlice4CopyRecipe(const ShapeDecl *shape) {
  auto cached = Slice4CopyRecipes.find(shape);
  if (cached != Slice4CopyRecipes.end())
    return cached->second;
  if (!shape)
    return {Slice4CopyRecipeKind::Dependent, {}, "missing shape"};
  if (!Slice4CopyRecipeInProgress.insert(shape).second)
    return {Slice4CopyRecipeKind::Dependent, {}, "recursive field graph"};

  Slice4CopyRecipe result{Slice4CopyRecipeKind::Always, {}, ""};
  if (shape->GenericParams.empty()) {
    result = proveSlice4Copy(shape)
        ? Slice4CopyRecipe{Slice4CopyRecipeKind::Always, {}, ""}
        : Slice4CopyRecipe{Slice4CopyRecipeKind::Never, {},
                           "non-Copy concrete shape"};
  } else if (shape->HasExplicitDrop) {
    result = {Slice4CopyRecipeKind::Never, {}, "custom drop"};
  } else if (Slice2PolicyMap.count(shape) && !Slice4CopyRequests.count(shape)) {
    result = {Slice4CopyRecipeKind::Never, {},
              "governed shape without @Copy"};
  } else {
    auto combine = [&](const Slice4CopyRecipe &next) {
      if (result.Kind == Slice4CopyRecipeKind::Never ||
          result.Kind == Slice4CopyRecipeKind::Dependent)
        return;
      if (next.Kind == Slice4CopyRecipeKind::Never ||
          next.Kind == Slice4CopyRecipeKind::Dependent) {
        result = next;
        return;
      }
      if (next.Kind == Slice4CopyRecipeKind::All) {
        if (result.Kind == Slice4CopyRecipeKind::Always)
          result.Kind = Slice4CopyRecipeKind::All;
        result.Requirements.insert(result.Requirements.end(),
                                   next.Requirements.begin(),
                                   next.Requirements.end());
      }
    };
    std::function<void(const ShapeMember &)> collect =
        [&](const ShapeMember &member) {
          combine(deriveSlice4CopyRecipeType(
              toka::Type::fromString(synthesizePhysicalType(member)), shape));
          for (const auto &submember : member.SubMembers)
            collect(submember);
        };
    for (const auto &member : shape->Members)
      collect(member);
  }
  if (result.Kind == Slice4CopyRecipeKind::All) {
    std::sort(result.Requirements.begin(), result.Requirements.end());
    result.Requirements.erase(
        std::unique(result.Requirements.begin(), result.Requirements.end()),
        result.Requirements.end());
  }
  Slice4CopyRecipeInProgress.erase(shape);
  Slice4CopyRecipes[shape] = result;
  return result;
}

bool Sema::slice4CopyRequirementIsProven(const ShapeDecl *shape,
                                         const ImplDecl *impl,
                                         const std::string &requirement) {
  auto normalizeParam = [](const std::string &name) {
    return !name.empty() && name.front() == '\'' ? name.substr(1) : name;
  };
  auto hasCopyBound = [&](const std::vector<GenericParam> &parameters,
                          const std::string &name) {
    for (const auto &parameter : parameters) {
      if (normalizeParam(parameter.Name) != name)
        continue;
      return std::any_of(parameter.TraitBounds.begin(),
                         parameter.TraitBounds.end(), [&](const std::string &bound) {
        return getTraitFamilyName(bound) == "Copy";
      });
    }
    return false;
  };
  if (hasCopyBound(shape->GenericParams, requirement))
    return true;

  auto implType = toka::Type::fromString(impl->TypeName);
  auto shapeType = std::dynamic_pointer_cast<toka::ShapeType>(implType);
  if (!shapeType || shapeType->GenericArgs.size() != shape->GenericParams.size())
    return false;
  size_t index = 0;
  while (index < shape->GenericParams.size() &&
         normalizeParam(shape->GenericParams[index].Name) != requirement)
    ++index;
  if (index == shape->GenericParams.size())
    return false;

  const auto &argument = shapeType->GenericArgs[index];
  const std::string argumentName =
      argument ? normalizeParam(argument->getSoulName()) : "";
  if (hasCopyBound(impl->GenericParams, argumentName))
    return true;
  return argument && proveSlice4CopyType(argument);
}

bool Sema::genericImplAppliesToWholeShape(const ShapeDecl *shape,
                                          const ImplDecl *impl) const {
  auto normalizeParam = [](const std::string &name) {
    return !name.empty() && name.front() == '\'' ? name.substr(1) : name;
  };
  auto type = toka::Type::fromString(impl->TypeName);
  auto shapeType = std::dynamic_pointer_cast<toka::ShapeType>(type);
  if (!shapeType || shapeType->Name != shape->Name ||
      shapeType->GenericArgs.size() != shape->GenericParams.size() ||
      impl->GenericParams.size() != shape->GenericParams.size())
    return false;
  for (size_t i = 0; i < shapeType->GenericArgs.size(); ++i) {
    if (!shapeType->GenericArgs[i] ||
        normalizeParam(shapeType->GenericArgs[i]->getSoulName()) !=
            normalizeParam(impl->GenericParams[i].Name))
      return false;
  }
  return true;
}

std::string
Sema::describeSlice4CopyRecipe(const Slice4CopyRecipe &recipe) const {
  switch (recipe.Kind) {
  case Slice4CopyRecipeKind::Always:
    return "always";
  case Slice4CopyRecipeKind::Never:
    return "never(" + recipe.Detail + ")";
  case Slice4CopyRecipeKind::All: {
    std::string result = "all(";
    for (size_t i = 0; i < recipe.Requirements.size(); ++i) {
      if (i)
        result += ",";
      result += recipe.Requirements[i] + ":@Copy";
    }
    return result + ")";
  }
  case Slice4CopyRecipeKind::Dependent:
    return "dependent(" + recipe.Detail + ")";
  }
  return "dependent(unknown)";
}

void Sema::validateSlice4CopyAndDup(Module &M) {
  for (const auto &shape : M.Shapes) {
    auto copyRequest = Slice4CopyRequests.find(shape.get());
    auto dup = Slice4DupProviders.find(shape.get());
    if (!shape->GenericParams.empty()) {
      const Slice4CopyRecipe recipe = deriveSlice4CopyRecipe(shape.get());
      bool verifiedCopyDomain = false;
      if (copyRequest != Slice4CopyRequests.end()) {
        const bool appliesToWholeDomain = genericImplAppliesToWholeShape(
            shape.get(), copyRequest->second);
        verifiedCopyDomain = appliesToWholeDomain &&
            (recipe.Kind == Slice4CopyRecipeKind::Always ||
             (recipe.Kind == Slice4CopyRecipeKind::All &&
              std::all_of(recipe.Requirements.begin(), recipe.Requirements.end(),
                          [&](const std::string &requirement) {
                return slice4CopyRequirementIsProven(shape.get(),
                                                     copyRequest->second,
                                                     requirement);
              })));
        if (!Slice2PolicyMap.count(shape.get()) || !verifiedCopyDomain) {
          DiagnosticEngine::report(
              copyRequest->second->Loc, DiagID::ERR_GENERIC_SEMA,
              "@Copy requires a trivial governed shape whose complete field graph is Copy for its entire generic domain");
          HasError = true;
        }
      }

      if (dup != Slice4DupProviders.end()) {
        const bool validDupDomain = genericImplAppliesToWholeShape(
            shape.get(), dup->second);
        if (!validDupDomain) {
          DiagnosticEngine::report(
              dup->second->Loc, DiagID::ERR_GENERIC_SEMA,
              "generic @Dup providers must apply uniformly to the nominal type's complete generic domain");
          HasError = true;
        }
        bool hasIntrinsicCopyDomain = false;
        bool mayOverlap = false;
        if (validDupDomain && copyRequest != Slice4CopyRequests.end() &&
            verifiedCopyDomain) {
          hasIntrinsicCopyDomain = true;
          mayOverlap = true;
        } else if (validDupDomain && !Slice2PolicyMap.count(shape.get()) &&
                   (recipe.Kind == Slice4CopyRecipeKind::Always ||
                    recipe.Kind == Slice4CopyRecipeKind::All)) {
          hasIntrinsicCopyDomain = true;
          mayOverlap = true;
          auto dupType = toka::Type::fromString(dup->second->TypeName);
          auto dupShape = std::dynamic_pointer_cast<toka::ShapeType>(dupType);
          if (recipe.Kind == Slice4CopyRecipeKind::All && dupShape &&
              dupShape->GenericArgs.size() == shape->GenericParams.size()) {
            mayOverlap = false;
            for (const auto &requirement : recipe.Requirements) {
              size_t index = 0;
              while (index < shape->GenericParams.size() &&
                     shape->GenericParams[index].Name != requirement)
                ++index;
              if (index == shape->GenericParams.size()) {
                mayOverlap = true;
                break;
              }
              const auto &argument = dupShape->GenericArgs[index];
              const std::string argumentName =
                  argument ? argument->getSoulName() : "";
              bool isProviderParameter = std::any_of(
                  dup->second->GenericParams.begin(),
                  dup->second->GenericParams.end(),
                  [&](const GenericParam &parameter) {
                    return parameter.Name == argumentName ||
                        (!parameter.Name.empty() && parameter.Name.front() == '\'' &&
                         parameter.Name.substr(1) == argumentName);
                  });
              if (isProviderParameter ||
                  (argument && proveSlice4CopyType(argument))) {
                mayOverlap = true;
                break;
              }
            }
          }
        }
        if (hasIntrinsicCopyDomain && mayOverlap) {
          DiagnosticEngine::report(
              dup->second->Loc, DiagID::ERR_GENERIC_SEMA,
              "a generic user @Dup implementation overlaps, or cannot be proved disjoint from, the intrinsic Dup witness of @Copy");
          HasError = true;
        }
      }
      continue;
    }
    if (copyRequest != Slice4CopyRequests.end()) {
      if (!Slice2PolicyMap.count(shape.get()) || !proveSlice4Copy(shape.get())) {
        DiagnosticEngine::report(copyRequest->second->Loc, DiagID::ERR_GENERIC_SEMA,
                                 "@Copy requires a trivial governed shape whose complete field graph is Copy");
        HasError = true;
      }
    }
    if (dup != Slice4DupProviders.end() && proveSlice4Copy(shape.get())) {
      DiagnosticEngine::report(dup->second->Loc, DiagID::ERR_GENERIC_SEMA,
                               "a user @Dup implementation overlaps the intrinsic Dup witness of @Copy");
      HasError = true;
    }
  }
}

void Sema::recordSlice5InterfaceFacts(Module &M) {
  M.InterfaceV2Facts.clear();
  for (const auto &shape : M.Shapes) {
    const std::string typeName = shape->Name;
    std::function<void(const ShapeMember &, const std::string &)> recordField;
    recordField = [&](const ShapeMember &member, const std::string &path) {
      M.InterfaceV2Facts.push_back("field_graph: " + typeName + "." +
                                   path + " = " +
                                   synthesizePhysicalType(member));
      for (size_t index = 0; index < member.SubMembers.size(); ++index) {
        const auto &submember = member.SubMembers[index];
        const std::string child = submember.Name.empty()
            ? std::to_string(index)
            : submember.Name;
        recordField(submember, path + "." + child);
      }
    };
    for (size_t index = 0; index < shape->Members.size(); ++index) {
      const auto &member = shape->Members[index];
      const std::string name = member.Name.empty() ? std::to_string(index)
                                                    : member.Name;
      recordField(member, name);
    }

    auto policy = Slice2PolicyMap.find(shape.get());
    if (policy != Slice2PolicyMap.end()) {
      std::vector<std::string> grants;
      for (const auto &entry : policy->second.Entries) {
        for (const auto &field : entry.Fields)
          grants.push_back("global:" + field);
      }
      std::sort(grants.begin(), grants.end());
      std::string record = "policy: " + typeName + " =";
      for (const auto &grant : grants)
        record += " " + grant;
      M.InterfaceV2Facts.push_back(std::move(record));
    }

    const bool provenCopy = proveSlice4Copy(shape.get());
    const bool explicitCopy = Slice4CopyRequests.count(shape.get()) != 0;
    if (!shape->GenericParams.empty()) {
      M.InterfaceV2Facts.push_back("copy_recipe: " + typeName + " = " +
                                   describeSlice4CopyRecipe(
                                       deriveSlice4CopyRecipe(shape.get())));
    }
    M.InterfaceV2Facts.push_back(
        "copy_proof: " + typeName + " = " +
        (provenCopy ? "proven-copy" : "proven-noncopy"));
    if (provenCopy) {
      M.InterfaceV2Facts.push_back(
          "copy_witness: " + typeName + " = " +
          (explicitCopy ? "explicit-verified" : "auto-structural"));
      M.InterfaceV2Facts.push_back("dup_provider: " + typeName +
                                   " = intrinsic-copy");
    } else if (Slice4DupProviders.count(shape.get())) {
      M.InterfaceV2Facts.push_back("dup_provider: " + typeName +
                                   " = user");
    }
    if (shape->HasExplicitDrop) {
      M.InterfaceV2Facts.push_back("custom_drop: " + typeName + " = " +
                                   shape->MangledDestructorName);
    }
  }
  std::sort(M.InterfaceV2Facts.begin(), M.InterfaceV2Facts.end());
}

static bool typeMentionsSelf(const std::string &typeName) {
  std::string type = trimTypeString(typeName);
  size_t pos = 0;
  while ((pos = type.find("Self", pos)) != std::string::npos) {
    bool startOk = pos == 0 ||
                   (!std::isalnum(static_cast<unsigned char>(type[pos - 1])) &&
                    type[pos - 1] != '_');
    size_t end = pos + 4;
    bool endOk =
        end >= type.size() ||
        (!std::isalnum(static_cast<unsigned char>(type[end])) &&
         type[end] != '_');
    if (startOk && endOk)
      return true;
    pos = end;
  }
  return false;
}

std::string Sema::getDynTraitName(const std::string &typeName) const {
  return getDynTraitName(toka::Type::fromString(typeName));
}

std::string Sema::getDynTraitName(std::shared_ptr<toka::Type> type) const {
  if (!type)
    return "";
  auto shape = std::dynamic_pointer_cast<toka::ShapeType>(type);
  if (!shape)
    return "";

  const std::string &name = shape->Name;
  if (name.rfind("dyn @", 0) == 0)
    return getTraitFamilyName(name.substr(5));
  if (name.rfind("dyn@", 0) == 0)
    return getTraitFamilyName(name.substr(4));
  return "";
}

bool Sema::validateDynTraitObjectSafety(const std::string &traitName,
                                        SourceLocation loc) {
  const std::string family = getTraitFamilyName(traitName);
  if (family == "Encap" || family == "Copy") {
    DiagnosticEngine::report(
        loc, DiagID::ERR_DYN_TRAIT_NOT_OBJECT_SAFE, family, family,
        "compiler-known policy and Copy markers have no dynamic method dictionary");
    HasError = true;
    return false;
  }

  TraitDecl *trait = findVisibleTraitDecl(traitName, loc);
  if (!trait)
    return true;

  auto fail = [&](const std::string &reason) {
    SourceLocation reportLoc = loc.isValid() ? loc : trait->Loc;
    DiagnosticEngine::report(reportLoc, DiagID::ERR_DYN_TRAIT_NOT_OBJECT_SAFE,
                             trait->Name, trait->Name, reason);
    HasError = true;
    return false;
  };

  if (!trait->GenericParams.empty())
    return fail("generic traits are not yet object-safe");
  if (!trait->AssociatedTypes.empty())
    return fail("associated types are not yet bindable on dyn trait objects");

  for (const auto &method : trait->Methods) {
    if (!method->GenericParams.empty())
      return fail("method '" + method->Name + "' is generic");
    if (typeMentionsSelf(method->ReturnType))
      return fail("method '" + method->Name +
                  "' mentions Self in its return type");
    for (const auto &arg : method->Args) {
      if (arg.Name == "self")
        continue;
      if (typeMentionsSelf(arg.Type))
        return fail("method '" + method->Name +
                    "' mentions Self outside the receiver");
    }
  }

  return true;
}

bool Sema::validateDynTraitObjectSafetyInType(const std::string &typeName,
                                              SourceLocation loc) {
  return validateDynTraitObjectSafetyInType(toka::Type::fromString(typeName),
                                            loc);
}

bool Sema::validateDynTraitObjectSafetyInType(std::shared_ptr<toka::Type> type,
                                              SourceLocation loc) {
  if (!type)
    return true;

  bool ok = true;
  if (std::string dynTrait = getDynTraitName(type); !dynTrait.empty()) {
    ok = validateDynTraitObjectSafety(dynTrait, loc) && ok;
  }

  if (type->isPointer()) {
    ok = validateDynTraitObjectSafetyInType(type->getPointeeType(), loc) && ok;
  }

  if (auto arr = std::dynamic_pointer_cast<toka::ArrayType>(type)) {
    ok = validateDynTraitObjectSafetyInType(arr->ElementType, loc) && ok;
  }

  if (auto fn = std::dynamic_pointer_cast<toka::FunctionType>(type)) {
    for (const auto &param : fn->ParamTypes) {
      ok = validateDynTraitObjectSafetyInType(param, loc) && ok;
    }
    ok = validateDynTraitObjectSafetyInType(fn->ReturnType, loc) && ok;
  }

  if (auto dynFn = std::dynamic_pointer_cast<toka::DynFnType>(type)) {
    for (const auto &param : dynFn->ParamTypes) {
      ok = validateDynTraitObjectSafetyInType(param, loc) && ok;
    }
    ok = validateDynTraitObjectSafetyInType(dynFn->ReturnType, loc) && ok;
  }

  if (auto shape = std::dynamic_pointer_cast<toka::ShapeType>(type)) {
    for (const auto &arg : shape->GenericArgs) {
      ok = validateDynTraitObjectSafetyInType(arg, loc) && ok;
    }
  }

  return ok;
}

bool Sema::isTypeNameVisible(const std::string &typeName, SourceLocation loc) {
  std::string name = Type::stripMorphology(typeName);
  if (name.rfind("cede ", 0) == 0)
    name = name.substr(5);
  if (name.rfind("async ", 0) == 0)
    name = name.substr(6);

  size_t genericPos = name.find('<');
  if (genericPos != std::string::npos)
    name = name.substr(0, genericPos);

  if (name.empty() || name.rfind("__Toka_", 0) == 0 ||
      name.rfind("__Closure_", 0) == 0)
    return true;

  size_t scopePos = name.find("::");
  if (scopePos != std::string::npos) {
    std::string moduleName = name.substr(0, scopePos);
    std::string targetName = name.substr(scopePos + 2);
    SymbolInfo *moduleInfo = nullptr;
    std::string actualName = moduleName;
    if (CurrentScope) {
      CurrentScope->findVariableWithDeref(moduleName, moduleInfo, actualName);
    }
    if ((!moduleInfo || !moduleInfo->ReferencedModule) && loc.isValid()) {
      if (ModuleScope *lexical = getLexicalModule(loc)) {
        auto symbol = lexical->LexicalSymbols.find(moduleName);
        if (symbol != lexical->LexicalSymbols.end())
          moduleInfo = &symbol->second;
      }
    }
    if (!moduleInfo || !moduleInfo->ReferencedModule) {
      return false;
    }
    auto *target = static_cast<ModuleScope *>(moduleInfo->ReferencedModule);
    bool visible = target->Shapes.count(targetName) ||
                   target->TypeAliases.count(targetName) ||
                   target->Traits.count(targetName);
    if (visible) {
      moduleInfo->HasBeenUsed = true;
      if (moduleInfo->ImportingDecl) {
        const_cast<ImportDecl *>(moduleInfo->ImportingDecl)->HasBeenUsed = true;
      }
    }
    return visible;
  }

  SymbolInfo *info = nullptr;
  if (CurrentScope && CurrentScope->findSymbol(name, info) && info &&
      (info->IsTypeName || info->IsTypeAlias)) {
    info->HasBeenUsed = true;
    if (info->ImportingDecl) {
      const_cast<ImportDecl *>(info->ImportingDecl)->HasBeenUsed = true;
    }
    return true;
  }

  ModuleScope *currentModuleScope = nullptr;
  if (CurrentModule) {
    if (!CurrentModule->ResolvedPath.empty())
      currentModuleScope = getModule(CurrentModule->ResolvedPath);
    if (!currentModuleScope && CurrentModule->Loc.isValid())
      currentModuleScope = getLexicalModule(CurrentModule->Loc);
    if (!currentModuleScope && !CurrentModule->SourcePath.empty())
      currentModuleScope = getModule(CurrentModule->SourcePath);
  }
  if (currentModuleScope) {
    auto symbol = currentModuleScope->LexicalTypes.find(name);
    if (symbol != currentModuleScope->LexicalTypes.end()) {
      symbol->second.HasBeenUsed = true;
      if (symbol->second.ImportingDecl) {
        const_cast<ImportDecl *>(symbol->second.ImportingDecl)->HasBeenUsed =
            true;
      }
      return true;
    }
  }

  if (loc.isValid()) {
    if (ModuleScope *lexical = getLexicalModule(loc)) {
      auto symbol = lexical->LexicalTypes.find(name);
      if (symbol != lexical->LexicalTypes.end()) {
        symbol->second.HasBeenUsed = true;
        if (symbol->second.ImportingDecl) {
          const_cast<ImportDecl *>(symbol->second.ImportingDecl)->HasBeenUsed =
              true;
        }
        return true;
      }
    }
  }

  if (CurrentFunction) {
    auto owner = DeclarationLexicalScopes.find(CurrentFunction);
    if (owner != DeclarationLexicalScopes.end() && owner->second) {
      auto symbol = owner->second->LexicalTypes.find(name);
      if (symbol != owner->second->LexicalTypes.end()) {
        symbol->second.HasBeenUsed = true;
        if (symbol->second.ImportingDecl) {
          const_cast<ImportDecl *>(symbol->second.ImportingDecl)->HasBeenUsed =
              true;
        }
        return true;
      }
    }

    auto visibleTypes = InstantiationTypeNames.find(CurrentFunction);
    if (visibleTypes != InstantiationTypeNames.end() &&
        visibleTypes->second.count(name)) {
      return true;
    }

    auto context = InstantiationLexicalScopes.find(CurrentFunction);
    if (context != InstantiationLexicalScopes.end() && context->second) {
      auto symbol = context->second->LexicalTypes.find(name);
      if (symbol != context->second->LexicalTypes.end()) {
        symbol->second.HasBeenUsed = true;
        if (symbol->second.ImportingDecl) {
          const_cast<ImportDecl *>(symbol->second.ImportingDecl)->HasBeenUsed =
              true;
        }
        return true;
      }
    }
  }

  size_t instancePos = name.find("_M_");
  if (instancePos != std::string::npos)
    return isTypeNameVisible(name.substr(0, instancePos), loc);

  return false;
}

void Sema::recordInstantiationType(FunctionDecl *function,
                                   std::shared_ptr<toka::Type> type) {
  if (!function || !type)
    return;

  if (type->isPointer()) {
    recordInstantiationType(function, type->getPointeeType());
    return;
  }
  if (auto *uninit = dynamic_cast<UninitType *>(type.get())) {
    recordInstantiationType(function, uninit->InnerType);
    return;
  }
  if (auto element = type->getArrayElementType()) {
    recordInstantiationType(function, element);
    return;
  }
  if (auto *fn = dynamic_cast<FunctionType *>(type.get())) {
    for (const auto &param : fn->ParamTypes)
      recordInstantiationType(function, param);
    recordInstantiationType(function, fn->ReturnType);
    return;
  }
  if (auto *fn = dynamic_cast<DynFnType *>(type.get())) {
    for (const auto &param : fn->ParamTypes)
      recordInstantiationType(function, param);
    recordInstantiationType(function, fn->ReturnType);
    return;
  }
  if (auto *shape = dynamic_cast<ShapeType *>(type.get())) {
    InstantiationTypeNames[function].insert(
        Type::stripMorphology(shape->Name));
    for (const auto &arg : shape->GenericArgs)
      recordInstantiationType(function, arg);
  }
}

bool Sema::validateTypeVisibilityInType(const std::string &typeName,
                                        SourceLocation loc) {
  std::string trimmed = trimTypeString(typeName);
  if (trimmed == "()")
    return true;

  std::string associated = resolveAssociatedTypeProjection(trimmed, false);
  if (!associated.empty()) {
    size_t at = findTopLevelChar(trimmed, '@');
    size_t scope = at == std::string::npos
                       ? std::string::npos
                       : findTopLevelDoubleColon(trimmed, at + 1);
    bool ok = true;
    if (at != std::string::npos && scope != std::string::npos) {
      ok = validateTypeVisibilityInType(trimmed.substr(0, at), loc) && ok;
      std::string traitName =
          trimTypeString(trimmed.substr(at + 1, scope - at - 1));
      if (!isTypeNameVisible(traitName, loc)) {
        DiagnosticEngine::report(loc, DiagID::ERR_UNDEFINED_TYPE, traitName);
        HasError = true;
        ok = false;
      }
    }
    return validateTypeVisibilityInType(associated, loc) && ok;
  }

  size_t open = trimmed.find('[');
  if (open != std::string::npos) {
    std::string prefix = trimTypeString(trimmed.substr(0, open));
    bool legacyPrefix = true;
    for (char c : prefix) {
      if (std::isalnum(static_cast<unsigned char>(c)) &&
          prefix != "cede" && prefix != "async" && prefix != "nul") {
        legacyPrefix = false;
        break;
      }
    }
    size_t close = trimmed.find(']', open + 1);
    if (legacyPrefix && close != std::string::npos &&
        close + 1 < trimmed.size() &&
        trimmed.substr(open + 1, close - open - 1).find(';') ==
            std::string::npos) {
      return validateTypeVisibilityInType(trimmed.substr(close + 1), loc);
    }
  }

  return validateTypeVisibilityInType(toka::Type::fromString(typeName), loc);
}

bool Sema::validateTypeVisibilityInType(std::shared_ptr<toka::Type> type,
                                        SourceLocation loc) {
  if (!type)
    return true;

  bool ok = true;
  if (type->isPointer())
    return validateTypeVisibilityInType(type->getPointeeType(), loc);

  if (auto *uninit = dynamic_cast<UninitType *>(type.get()))
    return validateTypeVisibilityInType(uninit->InnerType, loc);

  if (auto element = type->getArrayElementType())
    ok = validateTypeVisibilityInType(element, loc) && ok;

  if (auto *fn = dynamic_cast<FunctionType *>(type.get())) {
    for (const auto &param : fn->ParamTypes)
      ok = validateTypeVisibilityInType(param, loc) && ok;
    ok = validateTypeVisibilityInType(fn->ReturnType, loc) && ok;
  }
  if (auto *fn = dynamic_cast<DynFnType *>(type.get())) {
    for (const auto &param : fn->ParamTypes)
      ok = validateTypeVisibilityInType(param, loc) && ok;
    ok = validateTypeVisibilityInType(fn->ReturnType, loc) && ok;
  }

  if (auto *shape = dynamic_cast<ShapeType *>(type.get())) {
    if (shape->Name == "()")
      return ok;

    if (!shape->Name.empty() && shape->Name.front() == '(' &&
        shape->Name.back() == ')') {
      auto resolved = resolveType(type, false);
      auto record = std::dynamic_pointer_cast<ShapeType>(resolved);
      if (record && record->Decl) {
        for (const auto &member : record->Decl->Members) {
          ok = validateTypeVisibilityInType(member.Type, loc) && ok;
        }
      }
      return ok;
    }

    std::string visibleName = shape->Name;
    if (std::string dynTrait = getDynTraitName(type); !dynTrait.empty())
      visibleName = dynTrait;

    if (!isTypeNameVisible(visibleName, loc)) {
      DiagnosticEngine::report(loc, DiagID::ERR_UNDEFINED_TYPE, visibleName);
      HasError = true;
      ok = false;
    }
    ShapeDecl *genericDecl = shape->Decl;
    if (!genericDecl) {
      auto declaration = ShapeMap.find(shape->Name);
      if (declaration != ShapeMap.end())
        genericDecl = declaration->second;
    }
    for (size_t i = 0; i < shape->GenericArgs.size(); ++i) {
      if (genericDecl && i < genericDecl->GenericParams.size() &&
          genericDecl->GenericParams[i].IsConst) {
        continue;
      }
      auto *constant =
          dynamic_cast<ShapeType *>(shape->GenericArgs[i].get());
      if (constant && !constant->Name.empty() &&
          std::all_of(constant->Name.begin(), constant->Name.end(),
                      [](unsigned char c) { return std::isdigit(c); })) {
        continue;
      }
      ok = validateTypeVisibilityInType(shape->GenericArgs[i], loc) && ok;
    }
  }

  return ok;
}

void Sema::validateTraitAssociatedTypes(TraitDecl *Trait) {
  if (!Trait || CheckedAssociatedTypeTraits.count(Trait))
    return;
  CheckedAssociatedTypeTraits.insert(Trait);

  // Ordinary `fn -> async T` is part of the frozen 1.0 async surface. A trait
  // declaration adds interface, receiver, dispatch, and source-less replay
  // obligations that are deferred to the async-interface RFC. Reject it before
  // it can enter MethodMap or a `.tki`.
  for (const auto &method : Trait->Methods) {
    if (method->Effect == EffectKind::Async) {
      DiagnosticEngine::report(
          method->Loc, DiagID::ERR_TRAIT_ASYNC_METHOD_OUTSIDE_1_0,
          method->Name, Trait->Name);
      HasError = true;
    }
  }

  std::set<std::string> genericNames;
  for (const auto &gp : Trait->GenericParams) {
    genericNames.insert(gp.Name);
    if (!gp.Name.empty() && gp.Name[0] == '\'') {
      genericNames.insert(gp.Name.substr(1));
    }
  }

  std::set<std::string> seen;
  for (const auto &assoc : Trait->AssociatedTypes) {
    if (seen.count(assoc.Name)) {
      DiagnosticEngine::report(assoc.Loc, DiagID::ERR_ASSOC_TYPE_DUPLICATE,
                               assoc.Name);
      HasError = true;
      continue;
    }
    seen.insert(assoc.Name);

    if (genericNames.count(assoc.Name)) {
      DiagnosticEngine::report(
          assoc.Loc, DiagID::ERR_ASSOC_TYPE_NAME_CONFLICTS_WITH_GENERIC,
          assoc.Name, Trait->Name);
      HasError = true;
    }
  }
}

std::map<std::string, std::shared_ptr<toka::Type>>
Sema::registerAssociatedTypes(ImplDecl *Impl, TraitDecl *Trait,
                              const std::string &resolvedTypeName) {
  std::map<std::string, std::shared_ptr<toka::Type>> replacements;
  if (!Impl)
    return replacements;

  if (Impl->TraitName.empty()) {
    for (const auto &assoc : Impl->AssociatedTypes) {
      DiagnosticEngine::report(assoc.Loc, DiagID::ERR_ASSOC_TYPE_IN_INHERENT_IMPL,
                               assoc.Name);
      HasError = true;
    }
    return replacements;
  }

  if (!Trait)
    return replacements;

  validateTraitAssociatedTypes(Trait);

  std::map<std::string, const AssociatedTypeDecl *> required;
  for (const auto &assoc : Trait->AssociatedTypes) {
    required[assoc.Name] = &assoc;
  }

  // `@Callable::Output` is not a second declaration that users must keep in
  // sync with `call`.  It is the return type already declared by `call`, made
  // available to generic adapters such as owned `Map<I, F>`.
  const bool derivesCallableOutput =
      getTraitFamilyName(Trait->Name) == "Callable";
  if (derivesCallableOutput && required.count("Output")) {
    bool hasOutput = false;
    for (const auto &assoc : Impl->AssociatedTypes) {
      if (assoc.Name == "Output") {
        hasOutput = true;
        break;
      }
    }
    if (!hasOutput) {
      for (const auto &method : Impl->Methods) {
        if (method && method->Name == "call") {
          Impl->AssociatedTypes.push_back(
              AssociatedTypeDecl{"Output", method->ReturnType,
                                 method->ReturnTypeSyntax, false, method->Loc});
          break;
        }
      }
    }
  }

  std::map<std::string, const AssociatedTypeDecl *> provided;
  for (const auto &assoc : Impl->AssociatedTypes) {
    if (provided.count(assoc.Name)) {
      DiagnosticEngine::report(assoc.Loc, DiagID::ERR_ASSOC_TYPE_DUPLICATE,
                               assoc.Name);
      HasError = true;
      continue;
    }
    provided[assoc.Name] = &assoc;
  }

  for (const auto &[name, traitAssoc] : required) {
    if (!provided.count(name)) {
      DiagnosticEngine::report(Impl->Loc, DiagID::ERR_ASSOC_TYPE_MISSING,
                               getTraitFamilyName(Impl->TraitName),
                               Impl->TypeName, name);
      HasError = true;
    }
  }

  for (const auto &[name, implAssoc] : provided) {
    if (!required.count(name)) {
      DiagnosticEngine::report(implAssoc->Loc, DiagID::ERR_ASSOC_TYPE_EXTRA,
                               name, getTraitFamilyName(Impl->TraitName));
      HasError = true;
      continue;
    }

    const AssociatedTypeDecl *traitAssoc = required[name];
    if (traitAssoc->IsPer != implAssoc->IsPer) {
      DiagnosticEngine::report(
          implAssoc->Loc, DiagID::ERR_ASSOC_TYPE_MODE_MISMATCH, name,
          traitAssoc->IsPer ? "per type" : "type");
      HasError = true;
      continue;
    }

    TypeSyntaxPtr resolvedAssocSyntax = implAssoc->TypeSyntax;
    std::string resolvedAssocType = implAssoc->Type;
    std::shared_ptr<toka::Type> resolvedSelf =
        Impl->HeaderSyntax.Type
            ? resolveType(toka::Type::fromSyntax(Impl->HeaderSyntax.Type))
            : std::make_shared<toka::ShapeType>(resolvedTypeName);
    substituteSourceTypeSyntax(resolvedAssocSyntax, resolvedAssocType, "Self",
                               resolvedSelf);
    for (const auto &[knownName, knownType] : replacements) {
      substituteSourceTypeSyntax(resolvedAssocSyntax, resolvedAssocType,
                                 knownName, knownType);
    }
    std::shared_ptr<toka::Type> resolvedAssoc =
        resolvedAssocSyntax ? toka::Type::fromSyntax(resolvedAssocSyntax)
                           : toka::Type::fromString(resolvedAssocType);
    resolvedAssoc = resolveType(resolvedAssoc);
    resolvedAssocType = resolvedAssoc ? resolvedAssoc->toString() : "unknown";
    replacements[name] = resolvedAssoc;

    std::string canonical = canonicalTraitName(Impl->TraitName, Trait);
    std::string traitKey =
        implAssoc->IsPer ? canonical : getTraitFamilyName(canonical);
    std::string assocKey =
        resolvedTypeName + "@" + traitKey + "::" + name;

    auto existing = AssociatedTypeMap.find(assocKey);
    if (existing != AssociatedTypeMap.end() &&
        existing->second.Type != resolvedAssocType) {
      DiagnosticEngine::report(implAssoc->Loc, DiagID::ERR_ASSOC_TYPE_CONFLICT,
                               resolvedTypeName, traitKey, name,
                               existing->second.Type, resolvedAssocType);
      HasError = true;
      continue;
    }

    AssociatedTypeMap[assocKey] = AssociatedTypeBinding{
        resolvedAssocType, resolvedAssoc, implAssoc->IsPer, implAssoc->Loc};
  }

  return replacements;
}

void Sema::applyAssociatedTypeSubstitutions(
    ImplDecl *Impl,
    const std::map<std::string, std::shared_ptr<toka::Type>> &substitutions) {
  if (!Impl || substitutions.empty())
    return;

  for (auto &Method : Impl->Methods) {
    for (const auto &[name, ty] : substitutions) {
      substituteSourceTypeSyntax(Method->ReturnTypeSyntax, Method->ReturnType,
                                  name, ty);
      Method->syncReturnContractTypeCache();
      Method->ResolvedReturnType = nullptr;
      for (auto &Arg : Method->Args) {
        substituteSourceTypeSyntax(Arg.TypeSyntax, Arg.Type, name, ty);
        Arg.ResolvedType = nullptr;
      }
    }
  }
}

std::string Sema::resolveAssociatedTypeProjection(const std::string &typeName,
                                                  bool force) {
  std::string type = trimTypeString(typeName);
  size_t at = findTopLevelChar(type, '@');
  if (at == std::string::npos) {
    return "";
  }

  size_t scope = findTopLevelDoubleColon(type, at + 1);
  if (scope == std::string::npos) {
    return "";
  }

  std::string selfType = trimTypeString(type.substr(0, at));
  std::string traitName = trimTypeString(type.substr(at + 1, scope - at - 1));
  std::string assocName = trimTypeString(type.substr(scope + 2));
  if (selfType.empty() || traitName.empty() || assocName.empty()) {
    return "";
  }

  std::string resolvedSelf = resolveType(selfType, force);
  std::string exactTrait = traitName;
  if (!exactTrait.empty() && exactTrait[0] == '@') {
    exactTrait = exactTrait.substr(1);
  }
  if (TraitDecl *trait = findVisibleTraitDecl(exactTrait, SourceLocation()))
    exactTrait = canonicalTraitName(exactTrait, trait);

  std::string exactKey = resolvedSelf + "@" + exactTrait + "::" + assocName;
  auto exact = AssociatedTypeMap.find(exactKey);
  if (exact != AssociatedTypeMap.end()) {
    return exact->second.Type;
  }

  std::string family = getTraitFamilyName(exactTrait);
  std::string familyKey = resolvedSelf + "@" + family + "::" + assocName;
  auto fam = AssociatedTypeMap.find(familyKey);
  if (fam != AssociatedTypeMap.end()) {
    return fam->second.Type;
  }

  // Closures synthesize their `@Callable` implementation directly instead of
  // constructing a source-level ImplDecl.  Derive the same Output projection
  // from their generated call method (and from explicit callable shapes when
  // their metadata is available through MethodMap).
  if (getTraitFamilyName(exactTrait) == "Callable" && assocName == "Output") {
    auto selfTy = toka::Type::fromString(resolvedSelf);
    if (auto fnTy = std::dynamic_pointer_cast<toka::FunctionType>(selfTy))
      return fnTy->ReturnType ? fnTy->ReturnType->toString() : "";
    if (auto dynFnTy = std::dynamic_pointer_cast<toka::DynFnType>(selfTy))
      return dynFnTy->ReturnType ? dynFnTy->ReturnType->toString() : "";

    auto impl = ImplMap.find(resolvedSelf + "@Callable");
    if (impl != ImplMap.end()) {
      auto call = impl->second.find("call");
      if (call != impl->second.end() && call->second)
        return resolveType(call->second->ReturnType, force);
    }
    auto methods = MethodMap.find(resolvedSelf);
    if (methods != MethodMap.end()) {
      auto call = methods->second.find("call");
      if (call != methods->second.end())
        return resolveType(call->second, force);
    }
  }

  return "";
}

std::shared_ptr<toka::Type>
Sema::resolveAssociatedTypeProjection(const TypeSyntaxPtr &syntax,
                                      bool force) {
  if (!syntax || syntax->NodeKind != TypeSyntax::Kind::AssociatedProjection ||
      !syntax->Subject || syntax->Text.empty() || syntax->MemberName.empty())
    return nullptr;

  auto selfType = resolveType(toka::Type::fromSyntax(syntax->Subject), force);
  if (!selfType)
    return nullptr;
  const std::string resolvedSelf = selfType->toString();

  std::string exactTrait = syntax->Text;
  if (!exactTrait.empty() && exactTrait.front() == '@')
    exactTrait.erase(exactTrait.begin());
  if (TraitDecl *trait = findVisibleTraitDecl(exactTrait, syntax->Begin))
    exactTrait = canonicalTraitName(exactTrait, trait);

  const std::string exactKey =
      resolvedSelf + "@" + exactTrait + "::" + syntax->MemberName;
  if (auto exact = AssociatedTypeMap.find(exactKey);
      exact != AssociatedTypeMap.end())
    return exact->second.ResolvedType
               ? resolveType(exact->second.ResolvedType, force)
               : nullptr;

  const std::string family = getTraitFamilyName(exactTrait);
  const std::string familyKey =
      resolvedSelf + "@" + family + "::" + syntax->MemberName;
  if (auto associated = AssociatedTypeMap.find(familyKey);
      associated != AssociatedTypeMap.end())
    return associated->second.ResolvedType
               ? resolveType(associated->second.ResolvedType, force)
               : nullptr;

  if (family == "Callable" && syntax->MemberName == "Output") {
    if (auto fn = std::dynamic_pointer_cast<toka::FunctionType>(selfType))
      return fn->ReturnType ? resolveType(fn->ReturnType, force) : nullptr;
    if (auto dynFn = std::dynamic_pointer_cast<toka::DynFnType>(selfType))
      return dynFn->ReturnType ? resolveType(dynFn->ReturnType, force) : nullptr;

    auto impl = ImplMap.find(resolvedSelf + "@Callable");
    if (impl != ImplMap.end()) {
      auto call = impl->second.find("call");
      if (call != impl->second.end() && call->second) {
        if (call->second->ReturnTypeSyntax)
          return resolveType(
              toka::Type::fromSyntax(call->second->ReturnTypeSyntax), force);
        return resolveType(toka::Type::fromString(call->second->ReturnType),
                           force);
      }
    }
  }

  return nullptr;
}

bool Sema::checkModule(Module &M) {

  enterScope();       // Module-level global scope
  CurrentModule = &M; // Set context
  // 1. Register all globals (Functions, Structs, etc.)
  registerGlobals(M);
  // 2. Shape Analysis Pass (Safety Enforcement)
  analyzeShapes(M);

  // 2b. Check function bodies (reordered)

  for (size_t i = 0; i < M.Functions.size(); ++i) {
    if (!M.Functions[i]->GenericParams.empty()) {
      checkUnsafePublicFunctionBoundary(M.Functions[i].get());
      continue; // [NEW] Skip Generic Templates
    }
    checkFunction(M.Functions[i].get());
  }

  // 2c. Check Impl blocks (NEW: Proper Self Injection)
  for (size_t i = 0; i < M.Impls.size(); ++i) {
    if (!M.Impls[i]->GenericParams.empty()) {
      for (auto &Method : M.Impls[i]->Methods) {
        checkUnsafePublicFunctionBoundary(Method.get());
      }
      continue; // Skip templates, they are checked upon instantiation
    }
    checkImpl(M.Impls[i].get());
  }
  // ...

  // Transfer ownership of synthetic anonymous record shapes to the Module
  // so CodeGen can see them as regular structs.
  for (auto &S : SyntheticShapes) {
    M.Shapes.push_back(std::move(S));
  }
  SyntheticShapes.clear();

  CurrentModule = nullptr;

  bool isWarningExempt = false;
  if (!M.Imports.empty() && M.Imports[0]->Loc.isValid()) {
    std::string path = DiagnosticEngine::SrcMgr->getFullSourceLoc(M.Imports[0]->Loc).FileName;
    if (path.find("tests/") != std::string::npos ||
        path.find("build.tk") != std::string::npos ||
        path.find("prelude") != std::string::npos ||
        path.find("lib/") != std::string::npos) {
      isWarningExempt = true;
    }
  }
  if (!isWarningExempt) {
    for (auto &Imp : M.Imports) {
      if (!Imp->IsImplicit && !Imp->IsPub && !Imp->HasBeenUsed) {
        DiagnosticEngine::report(Imp->Loc, DiagID::WARN_UNUSED_IMPORT, Imp->PhysicalPath);
      }
    }
  }

  exitScope();
  return !HasError;
}

static SourceLocation getLoc(ASTNode *Node) { return Node->Loc; }



void Sema::enterScope() { 
  CurrentScope = new Scope(CurrentScope); 
  PALCheckerState.pushScope();
}

void Sema::exitScope() {
  Scope *Old = CurrentScope;
  CurrentScope = CurrentScope->Parent;
  PALCheckerState.popScope();
  delete Old;
}
void Sema::declareGlobals(Module &M) {
  recordHandleSurfaceModule(M);

  std::string fileName = !M.ResolvedPath.empty()
      ? toka::PathUtils::canonicalize(M.ResolvedPath)
      : toka::PathUtils::canonicalize(
            DiagnosticEngine::SrcMgr->getFullSourceLoc(M.Loc).FileName);
  ModuleScope &ms = ModuleMap[fileName];
  ms.IsTrustedSystemModule =
      ms.IsTrustedSystemModule || M.IsTrustedSystemModule;
  ms.ShadowCoordinateKnown = M.ShadowCoordinateKnown;
  ms.ShadowCrateId = M.ShadowCrateId;
  ms.ShadowLogicalModulePath = M.ShadowLogicalModulePath;
  ModulePathAliases[fileName] = &ms;
  if (!M.ResolvedPath.empty())
    ModulePathAliases[toka::PathUtils::canonicalize(M.ResolvedPath)] = &ms;
  if (!M.SourcePath.empty())
    ModulePathAliases[toka::PathUtils::canonicalize(M.SourcePath)] = &ms;
  if (M.Loc.isValid()) {
    ModulePathAliases[toka::PathUtils::canonicalize(
        DiagnosticEngine::SrcMgr->getFullSourceLoc(M.Loc).FileName)] = &ms;
  }
  ms.Name = fileName;
  size_t lastSlash = ms.Name.find_last_of('/');
  if (lastSlash != std::string::npos) {
    ms.Name = ms.Name.substr(lastSlash + 1);
  }
  size_t dot = ms.Name.find_last_of('.');
  if (dot != std::string::npos) {
    ms.Name = ms.Name.substr(0, dot);
  }

  // 1. Register local Functions
  for (auto &Fn : M.Functions) {
    DeclarationLexicalScopes[Fn.get()] = &ms;
    Fn->CodegenName = moduleScopedCodegenName(M, Fn->Name);
    for (const auto &Arg : Fn->Args) {
      debugCheckBindingPermission(Arg);
      debugCheckBindingTypeString("function argument", Arg.Name, Arg.Type,
                                  Arg.Permission, Fn->Loc);
    }
    ms.Functions[Fn->Name] = Fn.get();
    auto &overloads = ms.FunctionOverloads[Fn->Name];
    if (std::find(overloads.begin(), overloads.end(), Fn.get()) ==
        overloads.end()) {
      overloads.push_back(Fn.get());
    }
    if (std::find(GlobalFunctions.begin(), GlobalFunctions.end(), Fn.get()) == GlobalFunctions.end()) {
      GlobalFunctions.push_back(Fn.get());
    }
    SymbolInfo info;
    info.TypeObj = toka::Type::fromString("fn");
    info.ASTPtr = Fn.get();
    ms.LexicalSymbols[Fn->Name] = info;
  }
  // 2. Register Externs
  for (auto &Ext : M.Externs) {
    DeclarationLexicalScopes[Ext.get()] = &ms;
    for (auto &Arg : Ext->Args) {
      debugCheckBindingPermission(Arg);
      debugCheckBindingTypeString("extern argument", Arg.Name, Arg.Type,
                                  Arg.Permission, Ext->Loc);
      if (!Arg.ResolvedType) {
        Arg.ResolvedType =
            resolveType(Sema::synthesizePhysicalTypeObject(Arg));
      }
    }
    ms.Externs[Ext->Name] = Ext.get();
    ExternMap[Ext->Name] = Ext.get();
    SymbolInfo info;
    info.TypeObj = toka::Type::fromString("extern");
    info.ASTPtr = Ext.get();
    ms.LexicalSymbols[Ext->Name] = info;
  }
  // 3. Register Shapes
  for (auto &St : M.Shapes) {
    DeclarationLexicalScopes[St.get()] = &ms;
    if (St->CodegenName.empty())
      St->CodegenName = St->Name;
    auto existingShape = ShapeMap.find(St->Name);
    if (existingShape != ShapeMap.end() && existingShape->second != St.get()) {
      existingShape->second->CodegenName =
          shapeCodegenName(*existingShape->second);
      St->CodegenName = shapeCodegenName(M, St->Name);
    }
    ms.Shapes[St->Name] = St.get();
    ShapeMap[St->Name] = St.get();
    SymbolInfo info;
    info.TypeObj = toka::Type::fromString(St->Name);
    info.IsTypeName = true;
    info.ASTPtr = St.get();
    ms.LexicalTypes[St->Name] = info;
  }
  // 4. Register TypeAliases
  for (auto &Alias : M.TypeAliases) {
    DeclarationLexicalScopes[Alias.get()] = &ms;
    std::string target = Alias->TargetTypeSyntax
                             ? Alias->TargetTypeSyntax->toCanonicalString()
                             : Alias->TargetType;
    TypeSyntaxPtr targetSyntax = Alias->TargetTypeSyntax;
    if (!toka::Parser::TargetTriple.empty()) {
      std::string triple = toka::Parser::TargetTriple;
      bool is32 = (triple.find("wasm32") != std::string::npos ||
                   triple.find("i386") != std::string::npos ||
                   triple.find("i686") != std::string::npos ||
                   (triple.find("arm") != std::string::npos && triple.find("64") == std::string::npos && triple.find("armv8") == std::string::npos));
      if (is32) {
        if (Alias->Name == "usize" || Alias->Name == "Addr" || Alias->Name == "OAddr") {
          target = "u32";
        } else if (Alias->Name == "isize") {
          target = "i32";
        }
      }
    }
    if (!targetSyntax || targetSyntax->toCanonicalString() != target)
      targetSyntax = TypeSyntax::named(target, Alias->Loc, Alias->Loc);
    ms.TypeAliases[Alias->Name] = {target, targetSyntax, Alias->IsStrong,
                                   Alias->GenericParams};
    TypeAliasMap[Alias->Name] = {target, targetSyntax, Alias->IsStrong,
                                 Alias->GenericParams};
    SymbolInfo info;
    info.TypeObj = toka::Type::fromString(Alias->Name);
    info.IsTypeName = true;
    info.ASTPtr = Alias.get();
    ms.LexicalTypes[Alias->Name] = info;
  }
  // 5. Register Traits
  for (auto &Trait : M.Traits) {
    DeclarationLexicalScopes[Trait.get()] = &ms;
    if (Trait->Name == "Clone" || Trait->Name == "Drop") {
      DiagnosticEngine::report(getLoc(Trait.get()), DiagID::ERR_GENERIC_SEMA,
                               "the legacy @Clone and @Drop facets are removed");
      HasError = true;
    }
    ms.Traits[Trait->Name] = Trait.get();
    TraitMap[Trait->Name] = Trait.get();
    SymbolInfo info;
    info.TypeObj = toka::Type::fromString(Trait->Name);
    info.IsTypeName = true;
    info.IsTraitName = true;
    info.ASTPtr = Trait.get();
    ms.LexicalTypes[Trait->Name] = info;
    validateTraitAssociatedTypes(Trait.get());

    std::string traitKey = "@" + Trait->Name;
    for (auto &Method : Trait->Methods) {
      DeclarationLexicalScopes[Method.get()] = &ms;
      MethodMap[traitKey][Method->Name] = Method->ReturnType;
      MethodDecls[traitKey][Method->Name] = Method.get();
    }
  }
  for (auto &Alias : M.TypeAliases) {
    validateDynTraitObjectSafetyInType(
        Alias->TargetTypeSyntax ? Alias->TargetTypeSyntax->toCanonicalString()
                                : Alias->TargetType,
        getLoc(Alias.get()));
  }
  for (auto &St : M.Shapes) {
    if (St->CodegenName.empty())
      St->CodegenName = St->Name;
    for (const auto &Member : St->Members) {
      validateDynTraitObjectSafetyInType(
          Sema::synthesizePhysicalType(Member), getLoc(St.get()));
    }
  }
  // 6. Register Globals
  for (auto &G : M.Globals) {
    if (auto *v = dynamic_cast<VariableDecl *>(G.get())) {
      DeclarationLexicalScopes[v] = &ms;
      ms.Globals[v->Name] = v;
      SymbolInfo info;
      info.TypeObj = v->TypeName.empty()
                         ? toka::Type::fromString("unknown")
                         : toka::Type::fromString(synthesizePhysicalType(*v));
      info.IsRebindable = v->IsRebindable;
      info.CodegenName = v->Name;
      info.ASTPtr = v;
      ms.LexicalSymbols[v->Name] = info;
    }
  }

  // Build a diagnostic-free lexical preview for generic instantiations that
  // can occur during the declaration pass. Full conflict and visibility
  // diagnostics remain in registerGlobals().
  for (auto &Imp : M.Imports) {
    ModuleScope *target = nullptr;
    if (!Imp->ResolvedPath.empty()) {
      auto resolved = ModuleMap.find(
          toka::PathUtils::canonicalize(Imp->ResolvedPath));
      if (resolved != ModuleMap.end())
        target = &resolved->second;
    }
    if (!target)
      target = getModule(Imp->PhysicalPath);
    if (!target)
      continue;

    auto bindValue = [&](const std::string &name,
                         std::shared_ptr<toka::Type> type, ASTNode *node) {
      if (!ms.LexicalSymbols.count(name)) {
        SymbolInfo info;
        info.TypeObj = std::move(type);
        info.ASTPtr = node;
        info.ImportingDecl = Imp.get();
        info.ReferencedModule = target;
        ms.LexicalSymbols[name] = info;
      }
    };
    auto bindType = [&](const std::string &name, ASTNode *node) {
      if (!ms.LexicalTypes.count(name)) {
        SymbolInfo info;
        info.TypeObj = toka::Type::fromString(name);
        info.IsTypeName = true;
        info.ASTPtr = node;
        info.ImportingDecl = Imp.get();
        info.ReferencedModule = target;
        ms.LexicalTypes[name] = info;
      }
    };
    auto bindFunction = [&](const std::string &name, FunctionDecl *fn) {
      bindValue(name, toka::Type::fromString("fn"), fn);
      if (Imp->IsPub)
        ms.Functions[name] = fn;
    };
    auto bindExtern = [&](const std::string &name, ExternDecl *ext) {
      bindValue(name, toka::Type::fromString("extern"), ext);
      if (Imp->IsPub)
        ms.Externs[name] = ext;
    };
    auto bindShape = [&](const std::string &name, ShapeDecl *shape) {
      bindType(name, shape);
      if (Imp->IsPub)
        ms.Shapes[name] = shape;
    };
    auto bindAlias = [&](const std::string &name,
                         const AliasInfo &alias) {
      bindType(name, nullptr);
      if (Imp->IsPub)
        ms.TypeAliases[name] = alias;
    };
    auto bindTrait = [&](const std::string &name, TraitDecl *trait) {
      bindType(name, trait);
      auto symbol = ms.LexicalTypes.find(name);
      if (symbol != ms.LexicalTypes.end() && symbol->second.ASTPtr == trait)
        symbol->second.IsTraitName = true;
      if (Imp->IsPub)
        ms.Traits[name] = trait;
    };
    auto bindGlobal = [&](const std::string &name, VariableDecl *global) {
      bindValue(name,
                toka::Type::fromString(global->TypeName.empty()
                                           ? "unknown"
                                           : synthesizePhysicalType(*global)),
                global);
      auto symbol = ms.LexicalSymbols.find(name);
      if (symbol != ms.LexicalSymbols.end() && symbol->second.ASTPtr == global)
        symbol->second.CodegenName = global->Name;
      if (Imp->IsPub)
        ms.Globals[name] = global;
    };

    if (Imp->Items.empty()) {
      std::string name = Imp->Alias.empty()
                             ? defaultModuleNameForImport(Imp->PhysicalPath)
                             : Imp->Alias;
      bindValue(name, toka::Type::fromString("module"), nullptr);
      ms.LexicalSymbols[name].ReferencedModule = target;
      continue;
    }

    for (const auto &item : Imp->Items) {
      if (item.Symbol == "*") {
        for (const auto &[name, fn] : target->Functions)
          bindFunction(name, fn);
        for (const auto &[name, ext] : target->Externs)
          bindExtern(name, ext);
        for (const auto &[name, shape] : target->Shapes)
          bindShape(name, shape);
        for (const auto &[name, alias] : target->TypeAliases)
          bindAlias(name, alias);
        for (const auto &[name, trait] : target->Traits)
          bindTrait(name, trait);
        for (const auto &[name, global] : target->Globals)
          bindGlobal(name, global);
        continue;
      }

      std::string visible =
          item.Alias.empty() ? item.Symbol : item.Alias;
      std::string traitName = item.Symbol;
      if (!traitName.empty() && traitName.front() == '@')
        traitName.erase(traitName.begin());
      if (target->Functions.count(item.Symbol))
        bindFunction(visible, target->Functions[item.Symbol]);
      else if (target->Externs.count(item.Symbol))
        bindExtern(visible, target->Externs[item.Symbol]);
      else if (target->Shapes.count(item.Symbol))
        bindShape(visible, target->Shapes[item.Symbol]);
      else if (target->TypeAliases.count(item.Symbol))
        bindAlias(visible, target->TypeAliases[item.Symbol]);
      else if (target->Traits.count(traitName))
        bindTrait(visible, target->Traits[traitName]);
      else if (target->Globals.count(item.Symbol))
        bindGlobal(visible, target->Globals[item.Symbol]);
    }
  }

  // 7. Register Impls
  size_t implIndex = 0;
  for (auto &Impl : M.Impls) {
    DeclarationLexicalScopes[Impl.get()] = &ms;
    std::string owner = ms.ShadowCoordinateKnown
        ? "crate:" + ms.ShadowCrateId + ";module:" + ms.ShadowLogicalModulePath
        : "module:" + ms.Name;
    Slice1ImplDefinitionIds[Impl.get()] =
        owner + ";impl-index:" + std::to_string(implIndex++);
    for (auto &Method : Impl->Methods)
      DeclarationLexicalScopes[Method.get()] = &ms;
    declareImpl(Impl.get());
  }
}

void Sema::registerGlobals(Module &M) {
  // Initialize ModuleScope

  std::string fileName = !M.ResolvedPath.empty()
      ? toka::PathUtils::canonicalize(M.ResolvedPath)
      : toka::PathUtils::canonicalize(
            DiagnosticEngine::SrcMgr->getFullSourceLoc(M.Loc).FileName);
  ModuleScope &ms = ModuleMap[fileName];
  ms.ShadowCoordinateKnown = M.ShadowCoordinateKnown;
  ms.ShadowCrateId = M.ShadowCrateId;
  ms.ShadowLogicalModulePath = M.ShadowLogicalModulePath;
  ModulePathAliases[fileName] = &ms;
  if (!M.ResolvedPath.empty())
    ModulePathAliases[toka::PathUtils::canonicalize(M.ResolvedPath)] = &ms;
  if (!M.SourcePath.empty())
    ModulePathAliases[toka::PathUtils::canonicalize(M.SourcePath)] = &ms;
  if (M.Loc.isValid()) {
    ModulePathAliases[toka::PathUtils::canonicalize(
        DiagnosticEngine::SrcMgr->getFullSourceLoc(M.Loc).FileName)] = &ms;
  }
  ms.Name = fileName;
  // Simple name extraction (e.g. std/io.tk -> io)
  size_t lastSlash = ms.Name.find_last_of('/');
  if (lastSlash != std::string::npos) {
    ms.Name = ms.Name.substr(lastSlash + 1);
  }
  size_t dot = ms.Name.find_last_of('.');
  if (dot != std::string::npos) {
    ms.Name = ms.Name.substr(0, dot);
  }

  for (auto &Fn : M.Functions)
    DeclarationLexicalScopes[Fn.get()] = &ms;
  for (auto &Ext : M.Externs)
    DeclarationLexicalScopes[Ext.get()] = &ms;
  for (auto &St : M.Shapes)
    DeclarationLexicalScopes[St.get()] = &ms;
  for (auto &Alias : M.TypeAliases)
    DeclarationLexicalScopes[Alias.get()] = &ms;
  for (auto &Trait : M.Traits) {
    DeclarationLexicalScopes[Trait.get()] = &ms;
    for (auto &Method : Trait->Methods)
      DeclarationLexicalScopes[Method.get()] = &ms;
  }
  for (auto &G : M.Globals)
    DeclarationLexicalScopes[G.get()] = &ms;
  for (auto &Impl : M.Impls) {
    DeclarationLexicalScopes[Impl.get()] = &ms;
    for (auto &Method : Impl->Methods)
      DeclarationLexicalScopes[Method.get()] = &ms;
  }

  // Case A: Register local symbols in the ModuleScope
  for (auto &Fn : M.Functions) {
    Fn->CodegenName = moduleScopedCodegenName(M, Fn->Name);
    ms.Functions[Fn->Name] = Fn.get();
    auto &overloads = ms.FunctionOverloads[Fn->Name];
    if (std::find(overloads.begin(), overloads.end(), Fn.get()) ==
        overloads.end()) {
      overloads.push_back(Fn.get());
    }
    if (std::find(GlobalFunctions.begin(), GlobalFunctions.end(), Fn.get()) == GlobalFunctions.end()) {
      GlobalFunctions.push_back(Fn.get()); // Still keep global map for flat-checks
    }
    // [NEW] Define locally in scope for explicit lookup
    SymbolInfo fnInfo;
    fnInfo.TypeObj = toka::Type::fromString("fn");
    fnInfo.ASTPtr = Fn.get();
    CurrentScope->define(Fn->Name, fnInfo);
  }
  for (auto &Ext : M.Externs) {
    ms.Externs[Ext->Name] = Ext.get();
    ExternMap[Ext->Name] = Ext.get();
    // [NEW] Define locally in scope
    SymbolInfo extInfo;
    extInfo.TypeObj = toka::Type::fromString("extern");
    extInfo.ASTPtr = Ext.get();
    CurrentScope->define(Ext->Name, extInfo);
  }
  for (auto &St : M.Shapes) {
    for (const auto &Member : St->Members) {
      debugCheckShapeMemberPermissions(Member, St->Loc);
    }
    if (!St->GenericParams.empty()) {
      // [NEW] Generic Template Registration
      // Do NOT generate TypeLayout or simple ShapeMap entry yet.
      // We might need a separate GenericShapeMap or flag it.
      // For now, put in ShapeMap but the key distinction is St->GenericParams
      // is not empty. The Type system will see "Box" in ShapeMap, but when it
      // resolves, it sees GenericParams.
      ms.Shapes[St->Name] = St.get();
      ShapeMap[St->Name] = St.get();
    } else {
      ms.Shapes[St->Name] = St.get();
      ShapeMap[St->Name] = St.get();
    }
    SymbolInfo typeInfo;
    typeInfo.TypeObj = toka::Type::fromString(St->Name);
    typeInfo.IsTypeName = true;
    typeInfo.ASTPtr = St.get();
    ms.LexicalTypes[St->Name] = typeInfo;
  }
  for (auto &Alias : M.TypeAliases) {
    std::string target = Alias->TargetTypeSyntax
                             ? Alias->TargetTypeSyntax->toCanonicalString()
                             : Alias->TargetType;
    TypeSyntaxPtr targetSyntax = Alias->TargetTypeSyntax;
    if (!toka::Parser::TargetTriple.empty()) {
      std::string triple = toka::Parser::TargetTriple;
      bool is32 = (triple.find("wasm32") != std::string::npos ||
                   triple.find("i386") != std::string::npos ||
                   triple.find("i686") != std::string::npos ||
                   (triple.find("arm") != std::string::npos && triple.find("64") == std::string::npos && triple.find("armv8") == std::string::npos));
      if (is32) {
        if (Alias->Name == "usize" || Alias->Name == "Addr" || Alias->Name == "OAddr") {
          target = "u32";
        } else if (Alias->Name == "isize") {
          target = "i32";
        }
      }
    }
    if (!targetSyntax || targetSyntax->toCanonicalString() != target)
      targetSyntax = TypeSyntax::named(target, Alias->Loc, Alias->Loc);
    ms.TypeAliases[Alias->Name] = {target, targetSyntax, Alias->IsStrong,
                                   Alias->GenericParams};
    TypeAliasMap[Alias->Name] = {target, targetSyntax, Alias->IsStrong,
                                 Alias->GenericParams};

    SymbolInfo aliasInfo;
    aliasInfo.TypeObj = toka::Type::fromString(Alias->Name);
    aliasInfo.IsTypeName = true;
    aliasInfo.ASTPtr = Alias.get();
    ms.LexicalTypes[Alias->Name] = aliasInfo;
  }
  for (auto &Trait : M.Traits) {
    ms.Traits[Trait->Name] = Trait.get();
    TraitMap[Trait->Name] = Trait.get();
    validateTraitAssociatedTypes(Trait.get());

    // Register Trait methods for 'dyn' dispatch checks
    std::string traitKey = "@" + Trait->Name;
    for (auto &Method : Trait->Methods) {
      MethodMap[traitKey][Method->Name] = Method->ReturnType;
      MethodDecls[traitKey][Method->Name] = Method.get();
    }
    SymbolInfo traitInfo;
    traitInfo.TypeObj = toka::Type::fromString(Trait->Name);
    traitInfo.IsTypeName = true;
    traitInfo.IsTraitName = true;
    traitInfo.ASTPtr = Trait.get();
    ms.LexicalTypes[Trait->Name] = traitInfo;
  }
  for (auto &Alias : M.TypeAliases) {
    validateDynTraitObjectSafetyInType(
        Alias->TargetTypeSyntax ? Alias->TargetTypeSyntax->toCanonicalString()
                                : Alias->TargetType,
        getLoc(Alias.get()));
  }
  for (auto &St : M.Shapes) {
    for (const auto &Member : St->Members) {
      validateDynTraitObjectSafetyInType(
          Sema::synthesizePhysicalType(Member), getLoc(St.get()));
    }
  }
  for (auto &G : M.Globals) {
    if (auto *v = dynamic_cast<VariableDecl *>(G.get())) {
      debugCheckBindingPermission(*v);

      ms.Globals[v->Name] = v;
      SymbolInfo globalInfo;
      globalInfo.TypeObj = v->TypeName.empty()
                               ? toka::Type::fromString("unknown")
                               : toka::Type::fromString(
                                     synthesizePhysicalType(*v));
      globalInfo.IsRebindable = v->IsRebindable;
      globalInfo.Permission = BindingPermission::fromLegacy(
          v->IsRawPointer, v->IsUnique, v->IsShared, v->IsReference,
          v->IsRebindable, v->IsPointerNullable, v->IsRebindBlocked,
          v->IsValueMutable, v->IsValueNullable, v->IsValueBlocked,
          v->IsMorphicExempt);
      globalInfo.CodegenName = v->Name;
      globalInfo.ASTPtr = v;
      CurrentScope->define(v->Name, globalInfo);
    }
  }

  // Case B: Handle Imports
  std::string currentModuleKey = !M.ResolvedPath.empty()
      ? toka::PathUtils::canonicalize(M.ResolvedPath)
      : toka::PathUtils::canonicalize(M.SourcePath);
  for (auto &Imp : M.Imports) {
    ModuleScope *target = nullptr;
    
    // 0. Try ResolvedPath first (canonical absolute physical path resolved during parseSource)
    if (!Imp->ResolvedPath.empty()) {
      auto it = ModuleMap.find(Imp->ResolvedPath);
      if (it != ModuleMap.end()) {
        target = &it->second;
      }
    }
    
    // 1. Try absolute normalized path matching (for relative imports)
    if (!target) {
      std::string importPath = Imp->PhysicalPath;
      std::replace(importPath.begin(), importPath.end(), '\\', '/');
      if (importPath.rfind("./", 0) == 0 || importPath.rfind("../", 0) == 0) {
          size_t lastSlash = M.SourcePath.find_last_of('/');
          std::string parentDir = (lastSlash == std::string::npos) ? "." : M.SourcePath.substr(0, lastSlash);
          importPath = parentDir + "/" + importPath;
      }
      std::string normImport = toka::PathUtils::normalize(importPath);
      std::vector<std::string> normTries = {
          normImport,
          normImport + ".tk",
          normImport + ".tki",
          normImport + "/mod.tk",
          normImport + "/mod.tki"
      };
      
      for (auto &[path, scope] : ModuleMap) {
        if (path == currentModuleKey) continue;
        for (const auto &tryPath : normTries) {
          if (path == tryPath) {
            target = &scope;
            break;
          }
        }
        if (target) break;
      }
    }
    
    // 2. Fallback to suffix matching (for global library imports)
    if (!target) {
      for (auto &[path, scope] : ModuleMap) {
        if (path == currentModuleKey) continue;
        
        if (path == Imp->PhysicalPath) {
          target = &scope;
          break;
        }
        
        std::string p = Imp->PhysicalPath;
        std::vector<std::string> suffixes = {
          "/" + p,
          "/" + p + ".tk",
          "/" + p + ".tki",
          "/" + p + "/mod.tk",
          "/" + p + "/mod.tki",
          p,
          p + ".tk",
          p + ".tki",
          p + "/mod.tk",
          p + "/mod.tki"
        };
        
        bool matched = false;
        for (const auto &suffix : suffixes) {
          if (path.length() >= suffix.length() &&
              path.compare(path.length() - suffix.length(), suffix.length(), suffix) == 0) {
            target = &scope;
            matched = true;
            break;
          }
        }
        if (matched) break;
      }
    }

    if (!target) {
      DiagnosticEngine::report(getLoc(Imp.get()), DiagID::ERR_MODULE_NOT_FOUND,
                               Imp->PhysicalPath);
      HasError = true;
      continue;
    }

    auto importTypeName = [&](const std::string &visibleName,
                              ASTNode *declaration) {
      auto existing = ms.LexicalTypes.find(visibleName);
      if (existing != ms.LexicalTypes.end()) {
        bool sameDeclaration =
            declaration && existing->second.ASTPtr == declaration;
        bool samePreviewAlias =
            !declaration && !existing->second.ASTPtr &&
            existing->second.ImportingDecl == Imp.get() &&
            existing->second.ReferencedModule == target;
        return existing->second.IsTypeName &&
               (sameDeclaration || samePreviewAlias);
      }

      SymbolInfo typeInfo;
      typeInfo.TypeObj = toka::Type::fromString(visibleName);
      typeInfo.IsTypeName = true;
      typeInfo.ASTPtr = declaration;
      typeInfo.ImportingDecl = Imp.get();
      typeInfo.ReferencedModule = target;
      ms.LexicalTypes[visibleName] = typeInfo;
      return true;
    };

    if (Imp->Items.empty()) {
      // 1. Simple Import: import std/io
      // [Fix] Check for conflict using lookup to catch prelude clashes
      SymbolInfo info;
      info.TypeObj = toka::Type::fromString("module");
      info.ReferencedModule = target;
      info.ImportingDecl = Imp.get();
      std::string modName = Imp->Alias.empty()
                                ? defaultModuleNameForImport(Imp->PhysicalPath)
                                : Imp->Alias;

      SymbolInfo existing;
      if (CurrentScope->lookup(modName, existing)) {
        // Allow if it's the exact same module? No, duplicate import is usually
        // redundant. But for strictness, we report redefined.
        DiagnosticEngine::report(getLoc(Imp.get()),
                                 DiagID::ERR_SYMBOL_REDEFINED, modName);
        if (Imp->IsImplicit) {
          DiagnosticEngine::report(getLoc(Imp.get()),
                                   DiagID::NOTE_IMPLICIT_PRELUDE_CONFLICT,
                                   modName);
        }
        HasError = true;
      } else {
        CurrentScope->define(modName, info);
      }
    } else {
      // 2. Specific Import: import std/io::println
      for (auto &item : Imp->Items) {
        if (item.Symbol == "*") {
          // Import all functions
          for (auto const &[name, fn] : target->Functions) {
            std::string actualName = item.Alias.empty() ? name : item.Alias;
            if (CurrentScope->Symbols.count(actualName)) {
              if (CurrentScope->Symbols[actualName].ASTPtr != fn) {
                std::string existingType = CurrentScope->Symbols[actualName].TypeObj->toString();
                if (existingType == "extern") {
                  // Allow matching extern declaration with local definition!
                } else {
                  DiagnosticEngine::report(getLoc(Imp.get()),
                                         DiagID::ERR_SYMBOL_REDEFINED,
                                         actualName);
                  if (Imp->IsImplicit) {
                    DiagnosticEngine::report(
                        getLoc(Imp.get()), DiagID::NOTE_IMPLICIT_PRELUDE_CONFLICT,
                        actualName);
                  }
                  HasError = true;
                }
              }
            } else {
              SymbolInfo fnInfo;
              fnInfo.TypeObj = toka::Type::fromString("fn");
              fnInfo.ReferencedModule = target;
              fnInfo.ImportingDecl = Imp.get();
              fnInfo.ASTPtr = fn;
              CurrentScope->define(actualName, fnInfo);
              if (Imp->IsPub) {
                ms.Functions[actualName] = fn;
              }
            }
          }
          // Import all shapes
          for (auto const &[name, sh] : target->Shapes) {
            if (!importTypeName(name, sh))
              continue;
            ShapeMap[name] =
                sh; // Still needs to be in global maps for resolution
            ShapeImportMap[name] = Imp.get(); // [NEW]
            if (Imp->IsPub) {
              ms.Shapes[name] = sh;
            }
          }
          // Import all aliases
          for (auto const &[name, ai] : target->TypeAliases) {
            if (!importTypeName(name, nullptr))
              continue;
            TypeAliasMap[name] = ai;
            ShapeImportMap[name] = Imp.get(); // [NEW]
            if (Imp->IsPub) {
              ms.TypeAliases[name] = ai;
            }
          }
          // Import all traits
          for (auto const &[name, trait] : target->Traits) {
            if (!importTypeName(name, trait))
              continue;
            ms.LexicalTypes[name].IsTraitName = true;
            TraitMap[name] = trait;
            ShapeImportMap[name] = Imp.get(); // [NEW]
            if (Imp->IsPub) {
              ms.Traits[name] = trait;
            }
          }
          // Import all externs
          for (auto const &[name, ext] : target->Externs) {
            ExternMap[name] = ext;
            if (CurrentScope->Symbols.count(name)) {
              if (CurrentScope->Symbols[name].ASTPtr != ext) {
                std::string existingType = CurrentScope->Symbols[name].TypeObj->toString();
                if (existingType == "fn") {
                  // Allow matching local definition with extern declaration!
                } else {
                  DiagnosticEngine::report(getLoc(Imp.get()),
                                           DiagID::ERR_SYMBOL_REDEFINED, name);
                  if (Imp->IsImplicit) {
                    DiagnosticEngine::report(getLoc(Imp.get()),
                                             DiagID::NOTE_IMPLICIT_PRELUDE_CONFLICT,
                                             name);
                  }
                  HasError = true;
                }
              }
            } else {
              SymbolInfo extInfo;
              extInfo.TypeObj = toka::Type::fromString("extern");
              extInfo.ReferencedModule = target;
              extInfo.ImportingDecl = Imp.get();
              extInfo.ASTPtr = ext;
              CurrentScope->define(name, extInfo);
              if (Imp->IsPub) {
                ms.Externs[name] = ext;
              }
            }
          }
          // Import all globals (constants)
          for (auto const &[name, v] : target->Globals) {
            BindingPermission Permission = v->Permission;
            // Keep imported globals behavior-identical to the previous path:
            // existing TypeName prefixes are preserved and blocked markers were
            // not part of this import signature synthesis.
            Permission.IdentityBlocked = false;
            Permission.SoulBlocked = false;
            std::string fullType =
                Sema::synthesizePhysicalType(Permission, v->TypeName, false);

            SymbolInfo globalInfo;
            globalInfo.TypeObj = toka::Type::fromString(fullType);
            globalInfo.IsRebindable = v->IsRebindable;
            globalInfo.CodegenName = v->Name;
            globalInfo.ReferencedModule = target;
            globalInfo.ImportingDecl = Imp.get();
            globalInfo.ASTPtr = v;
            std::string actualName = item.Alias.empty() ? name : item.Alias;
            if (CurrentScope->Symbols.count(actualName)) {
              if (CurrentScope->Symbols[actualName].ASTPtr != v) {
                DiagnosticEngine::report(getLoc(Imp.get()),
                                       DiagID::ERR_SYMBOL_REDEFINED,
                                       actualName);
                if (Imp->IsImplicit) {
                  DiagnosticEngine::report(
                      getLoc(Imp.get()), DiagID::NOTE_IMPLICIT_PRELUDE_CONFLICT,
                      actualName);
                }
                HasError = true;
              }
            } else {
              CurrentScope->define(actualName, globalInfo);
              if (Imp->IsPub) {
                ms.Globals[actualName] = v;
              }
            }
          }
        } else {
          // Import specific
          std::string name = item.Alias.empty() ? item.Symbol : item.Alias;
          bool found = false;
          // Trait name lookup hack: if symbol is @Trait, look for Trait
          std::string lookupSym = item.Symbol;
          if (lookupSym.size() > 1 && lookupSym[0] == '@') {
            lookupSym = lookupSym.substr(1);
          }

          if (target->Functions.count(item.Symbol)) {
            auto *fn = target->Functions[item.Symbol];
            if (CurrentScope->Symbols.count(name)) {
              if (CurrentScope->Symbols[name].ASTPtr != fn) {
                std::string existingType = CurrentScope->Symbols[name].TypeObj->toString();
                if (existingType == "extern") {
                  // Allow matching extern declaration with local definition!
                } else {
                  DiagnosticEngine::report(getLoc(Imp.get()),
                                           DiagID::ERR_SYMBOL_REDEFINED, name);
                  if (Imp->IsImplicit) {
                    DiagnosticEngine::report(getLoc(Imp.get()),
                                             DiagID::NOTE_IMPLICIT_PRELUDE_CONFLICT,
                                             name);
                  }
                  HasError = true;
                }
              }
            } else {
              SymbolInfo fnInfo;
              fnInfo.TypeObj = toka::Type::fromString("fn");
              fnInfo.ReferencedModule = target;
              fnInfo.ImportingDecl = Imp.get();
              fnInfo.ASTPtr = fn;
              CurrentScope->define(name, fnInfo);
              if (Imp->IsPub) {
                ms.Functions[name] = fn;
              }
            }
            found = true;
          } else if (target->Shapes.count(item.Symbol)) {
            if (!importTypeName(name, target->Shapes[item.Symbol]))
              continue;
            ShapeMap[name] = target->Shapes[item.Symbol];
            ShapeImportMap[name] = Imp.get(); // [NEW]
            if (Imp->IsPub) {
              ms.Shapes[name] = target->Shapes[item.Symbol];
            }
            found = true;
          } else if (target->TypeAliases.count(item.Symbol)) {
            if (!importTypeName(name, nullptr))
              continue;
            TypeAliasMap[name] = target->TypeAliases[item.Symbol];
            ShapeImportMap[name] = Imp.get(); // [NEW]
            if (Imp->IsPub) {
              ms.TypeAliases[name] = target->TypeAliases[item.Symbol];
            }
            found = true;
          } else if (target->Traits.count(lookupSym)) {
            if (!importTypeName(name, target->Traits[lookupSym]))
              continue;
            ms.LexicalTypes[name].IsTraitName = true;
            TraitMap[name] = target->Traits[lookupSym];
            ShapeImportMap[name] = Imp.get(); // [NEW]
            if (Imp->IsPub) {
              ms.Traits[name] = target->Traits[lookupSym];
            }
            found = true;
          } else if (target->Externs.count(item.Symbol)) {
            auto *ext = target->Externs[item.Symbol];
            ExternMap[name] = ext;
            if (CurrentScope->Symbols.count(name)) {
              if (CurrentScope->Symbols[name].ASTPtr != ext) {
                std::string existingType = CurrentScope->Symbols[name].TypeObj->toString();
                if (existingType == "fn") {
                  // Allow matching local definition with extern declaration!
                } else {
                  DiagnosticEngine::report(getLoc(Imp.get()),
                                           DiagID::ERR_SYMBOL_REDEFINED, name);
                  if (Imp->IsImplicit) {
                    DiagnosticEngine::report(getLoc(Imp.get()),
                                             DiagID::NOTE_IMPLICIT_PRELUDE_CONFLICT,
                                             name);
                  }
                  HasError = true;
                }
              }
            } else {
              SymbolInfo extInfo;
              extInfo.TypeObj = toka::Type::fromString("extern");
              extInfo.ReferencedModule = target;
              extInfo.ImportingDecl = Imp.get();
              extInfo.ASTPtr = ext;
              CurrentScope->define(name, extInfo);
              if (Imp->IsPub) {
                ms.Externs[name] = ext;
              }
            }
            found = true;
          } else if (target->Globals.count(item.Symbol)) {
            auto *v = target->Globals[item.Symbol];
            BindingPermission Permission = v->Permission;
            // Keep imported globals behavior-identical to the previous path:
            // existing TypeName prefixes are preserved and blocked markers were
            // not part of this import signature synthesis.
            Permission.IdentityBlocked = false;
            Permission.SoulBlocked = false;
            std::string fullType =
                Sema::synthesizePhysicalType(Permission, v->TypeName, false);

            SymbolInfo globalInfo;
            globalInfo.TypeObj = toka::Type::fromString(fullType);
            globalInfo.IsRebindable = v->IsRebindable;
            globalInfo.CodegenName = v->Name;
            globalInfo.ReferencedModule = target;
            globalInfo.ImportingDecl = Imp.get();
            globalInfo.ASTPtr = v;
            if (CurrentScope->Symbols.count(name)) {
              if (CurrentScope->Symbols[name].ASTPtr != v) {
                DiagnosticEngine::report(getLoc(Imp.get()),
                                         DiagID::ERR_SYMBOL_REDEFINED, name);
                if (Imp->IsImplicit) {
                  DiagnosticEngine::report(getLoc(Imp.get()),
                                           DiagID::NOTE_IMPLICIT_PRELUDE_CONFLICT,
                                           name);
                }
                HasError = true;
              }
            } else {
              CurrentScope->define(name, globalInfo);
              if (Imp->IsPub) {
                ms.Globals[name] = v;
              }
            }
            found = true;
          }

          if (!found) {
            DiagnosticEngine::report(getLoc(Imp.get()),
                                     DiagID::ERR_SYMBOL_NOT_FOUND, item.Symbol,
                                     Imp->PhysicalPath);
            HasError = true;
          }
        }
      }
    }
  }

  // Imports must be visible before global initializers are inferred. Globals
  // were predeclared above so imports still diagnose name conflicts.
  for (auto &G : M.Globals) {
    auto *v = dynamic_cast<VariableDecl *>(G.get());
    if (!v)
      continue;

    if (v->TypeName.empty() && v->Init) {
      if (auto *cast = dynamic_cast<CastExpr *>(v->Init.get())) {
        v->TypeName = cast->TargetType;
      } else if (dynamic_cast<NumberExpr *>(v->Init.get())) {
        v->TypeName = "i64";
      } else if (dynamic_cast<BoolExpr *>(v->Init.get())) {
        v->TypeName = "bool";
      } else if (dynamic_cast<StringExpr *>(v->Init.get())) {
        v->TypeName = "str";
      } else {
        std::shared_ptr<toka::Type> inferredType = checkExpr(v->Init.get());
        if (!inferredType->isUnknown() && !inferredType->isVoid())
          v->TypeName = inferredType->toString();
      }
    }

    if (!v->TypeName.empty())
      validateTypeVisibilityInType(v->TypeName, getLoc(v));
    debugCheckBindingTypeString("global variable", v->Name, v->TypeName,
                                v->Permission, v->Loc);

    auto symbol = CurrentScope->Symbols.find(v->Name);
    if (symbol != CurrentScope->Symbols.end()) {
      symbol->second.TypeObj = toka::Type::fromString(
          v->TypeName.empty() ? "unknown" : synthesizePhysicalType(*v));
      symbol->second.IsRebindable = v->IsRebindable;
      symbol->second.ASTPtr = v;
    }
  }

  ms.LexicalSymbols = CurrentScope->Symbols;

  for (auto &Impl : M.Impls) {
    // [NEW] Generic Impl Registration (Lazy)
    // If impl has generic params, OR it points to a generic shape, it's a
    // template.
    bool typeIsGeneric = false;
    std::string baseShapeName = Impl->TypeName;
    size_t lt_check = baseShapeName.find('<');
    if (lt_check != std::string::npos)
      baseShapeName = baseShapeName.substr(0, lt_check);

    ShapeDecl *implShape = findVisibleShapeDecl(baseShapeName, Impl->Loc);
    if (implShape && !implShape->GenericParams.empty()) {
      typeIsGeneric = true;
    }

    if (!Impl->GenericParams.empty() || typeIsGeneric) {
      if (Impl->GenericParams.empty()) {
        // [Check] Validate that we aren't using undefined types as generic
        // args e.g. impl Vec<T> {} -> Error "T is undefined"
        auto typeObj = toka::Type::fromString(Impl->TypeName);
        if (auto st = std::dynamic_pointer_cast<ShapeType>(typeObj)) {
          for (auto &Arg : st->GenericArgs) {
            // Peel pointer/ref to get base name
            std::string name = Arg->getSoulName();
            // Check if known
            bool known = false;
            if (toka::Type::fromString(name)->typeKind == toka::Type::Primitive)
              known = true;
            else if (ShapeMap.count(name))
              known = true;
            else if (TypeAliasMap.count(name))
              known = true;
            else if (ExternMap.count(name))
              known = true; // External?
            else {
              // Consult Sema lookup (CurrentScope)
              // Since we are in registerGlobals, imports might be in scope or
              // Extern declarations? But usually simple generic params are
              // just T, U, etc.
              SymbolInfo info;
              if (CurrentScope && CurrentScope->lookup(name, info))
                known = true;
            }

            if (!known) {
              // Heuristic: If name is short (1-2 chars) or clearly looks like
              // a placeholder
              DiagnosticEngine::report(Impl->Loc, DiagID::ERR_UNDEFINED_TYPE,
                                       name);
              DiagnosticEngine::report(Impl->Loc,
                                       DiagID::NOTE_GENERIC_IMPL_HINT, name);
              HasError = true;
            }
          }
        }
      }

      std::string baseName = genericImplKey(Impl->TypeName, Impl->Loc);
      GenericImplMap[baseName].push_back(Impl.get());
      
      if (Impl->TraitName == "Encap" && !Impl->IsStructuralDrop) {
        EncapMap[baseName] = Impl->EncapEntries;
      }

      continue; // Skip standard registration for templates
    }

    if (Impl->TraitName == "Encap" && !Impl->IsStructuralDrop) {
      std::string encapBaseName = Impl->TypeName;
      size_t lt_encap = encapBaseName.find('<');
      if (lt_encap != std::string::npos) encapBaseName = encapBaseName.substr(0, lt_encap);
      EncapMap[encapBaseName] = Impl->EncapEntries;
      // removed continue to allow method registration (hybrid trait)
    }
    registerImpl(Impl.get());
  }

  ms.LexicalSymbols = CurrentScope->Symbols;
}

void Sema::registerImpl(ImplDecl *Impl) {
  std::string resolvedTypeName = resolveType(Impl->TypeName);
  TraitDecl *traitDecl =
      Impl->TraitName.empty()
          ? nullptr
          : Impl->Loc.isValid()
                ? findVisibleTraitDecl(Impl->TraitName, getLoc(Impl))
                : findTraitDecl(Impl->TraitName);
  std::string canonicalTrait = canonicalTraitName(Impl->TraitName, traitDecl);
  recordSlice1ImplFact(Impl, resolvedTypeName, canonicalTrait);

  if (getTraitFamilyName(canonicalTrait) == "ErrorInto") {
    size_t begin = canonicalTrait.find('<');
    size_t end = canonicalTrait.rfind('>');
    std::string target =
        begin != std::string::npos && end > begin
            ? canonicalTrait.substr(begin + 1, end - begin - 1)
            : "unknown";
    for (const auto &method : Impl->Methods) {
      if (method->Name != "into_error")
        continue;
      bool validSelf =
          method->Args.size() == 1 &&
          Type::stripMorphology(method->Args[0].Name) == "self" &&
          method->Args[0].IsCeded;
      auto actualReturn = resolveType(toka::Type::fromString(method->ReturnType),
                                      false);
      auto targetType = resolveType(toka::Type::fromString(target), false);
      bool validReturn = actualReturn && targetType &&
                         actualReturn->equals(*targetType);
      if (!method->IsPub ||
          method->Effect != EffectKind::None || !validSelf || !validReturn) {
        DiagnosticEngine::report(getLoc(method.get()),
                                 DiagID::ERR_SEMA_ERROR_CONVERSION_SIGNATURE,
                                 target, target);
        HasError = true;
      }
    }
  }

  std::map<std::string, std::shared_ptr<toka::Type>> associatedTypeSubstitutions =
      registerAssociatedTypes(Impl, traitDecl, resolvedTypeName);
  AssociatedTypeSubstitutionCache[Impl] = associatedTypeSubstitutions;

  // [New] Resolve 'Self' in Method Signatures for External Callers
  // We must replace 'Self' with the concrete (or generic) TypeName
  // so that callers (like main) typically don't fail to resolve 'Self'.
  std::string selfTy = Impl->TypeName;
  for (auto &Method : Impl->Methods) {
    substituteSourceTypeSyntax(Method->ReturnTypeSyntax, Method->ReturnType,
                                "Self", selfTy);
    Method->syncReturnContractTypeCache();
    Method->ResolvedReturnType = nullptr;
    for (auto &Arg : Method->Args) {
      substituteSourceTypeSyntax(Arg.TypeSyntax, Arg.Type, "Self", selfTy);
      Arg.ResolvedType = nullptr;
    }
  }
  applyAssociatedTypeSubstitutions(Impl, associatedTypeSubstitutions);

  auto requireSelfDependency = [&](const char *methodName) {
    for (const auto &method : Impl->Methods) {
      if (method->Name != methodName)
        continue;
      bool found = false;
      for (const auto &dep : method->LifeDependencies) {
        if (Type::stripMorphology(dep) == "self") {
          found = true;
          break;
        }
      }
      if (!found) {
        DiagnosticEngine::report(
            getLoc(method.get()),
            DiagID::ERR_SEMA_ITERATOR_DEPENDENCY_REQUIRED,
            canonicalTrait + "::" + methodName,
            resolvedTypeName);
        HasError = true;
      }
    }
  };
  if (getTraitFamilyName(canonicalTrait) == "Iterable")
    requireSelfDependency("iter");
  if (getTraitFamilyName(canonicalTrait) == "BorrowIterator")
    requireSelfDependency("next_ref");

  std::set<std::string> implemented;
  for (auto &Method : Impl->Methods) {
    if (!Method->IsClosureInvoke) {
      Method->CodegenName =
          Impl->TraitName.empty()
              ? resolvedTypeName + "_" + Method->Name
              : canonicalTrait + "_" + resolvedTypeName + "_" + Method->Name;
    }
    const bool isDropHook = getTraitFamilyName(canonicalTrait) == "Encap" &&
                            Method->Name == "drop";
    if (!isDropHook) {
      MethodMap[resolvedTypeName][Method->Name] = Method->ReturnType;
      MethodDecls[resolvedTypeName][Method->Name] = Method.get();
    }
    implemented.insert(Method->Name);
  }

  // Populate ImplMap
  if (!Impl->TraitName.empty() &&
      getTraitFamilyName(canonicalTrait) != "Encap" &&
      getTraitFamilyName(canonicalTrait) != "Copy") {
    std::string implKey = resolvedTypeName + "@" + canonicalTrait;
    ImplMap[implKey]; // Ensure the key exists even for empty traits
    for (auto &Method : Impl->Methods) {
      ImplMap[implKey][Method->Name] = Method.get();
    }
  }

  // Slice 2 makes @Encap an authority declaration, not the legacy trait
  // contract that required clone and drop methods.
  if (!Impl->TraitName.empty() &&
      getTraitFamilyName(canonicalTrait) != "Encap" &&
      getTraitFamilyName(canonicalTrait) != "Copy") {
    if (traitDecl) {
      TraitDecl *TD = traitDecl;
      std::string traitFamily = getTraitFamilyName(canonicalTrait);
      if (ShapeImportMap.count(traitFamily)) {
        const_cast<ImportDecl*>(ShapeImportMap[traitFamily])->HasBeenUsed = true;
      }
      for (auto &Method : TD->Methods) {
        if (implemented.count(Method->Name)) {
          // Verify Signature Match (Pub/Priv)
          FunctionDecl *ImplMethod = nullptr;
          for (auto &m : Impl->Methods) {
            if (m->Name == Method->Name) {
              ImplMethod = m.get();
              break;
            }
          }
          if (ImplMethod) {
            if (ImplMethod->IsPub != Method->IsPub) {
              std::string traitVis = Method->IsPub ? "pub" : "private";
              std::string implVis = ImplMethod->IsPub ? "pub" : "private";
              DiagnosticEngine::report(getLoc(ImplMethod),
                                       DiagID::ERR_SIGNATURE_MISMATCH,
                                       Method->Name, traitVis, implVis);
              HasError = true;
            }
          }
          continue;
        }
        if (Method->Body) {
          // Trait provides a default implementation
          TypeSyntaxPtr defaultReturnSyntax = Method->ReturnTypeSyntax;
          std::string defaultReturnType = Method->ReturnType;
          for (const auto &[name, ty] : associatedTypeSubstitutions) {
            substituteSourceTypeSyntax(defaultReturnSyntax, defaultReturnType,
                                       name, ty);
          }
          MethodMap[resolvedTypeName][Method->Name] = defaultReturnType;
          MethodDecls[resolvedTypeName][Method->Name] = Method.get();
        } else {
          // [Fix] Optional methods for intrinsic interfaces
          if (getTraitFamilyName(canonicalTrait) == "Delegate") {
            continue;
          }

          DiagnosticEngine::report(getLoc(Impl), DiagID::ERR_MISSING_IMPL,
                                   Impl->TraitName, Method->Name);
          HasError = true;
        }
      }
    } else {
      DiagnosticEngine::report(getLoc(Impl), DiagID::ERR_TRAIT_NOT_FOUND,
                               getTraitFamilyName(Impl->TraitName),
                               Impl->TypeName);
      HasError = true;
    }
  }

  // [Toka] Resource Management: Mark type as having drop if @Encap is
  // implemented
  if (getTraitFamilyName(canonicalTrait) == "Encap") {
    if (implemented.count("drop")) {
      m_ShapeProps[resolvedTypeName].HasDrop = true;
      // [Single Source of Truth] Store the authoritative mangled name
      if (ShapeMap.count(resolvedTypeName)) {
        ShapeMap[resolvedTypeName]->MangledDestructorName =
            "Encap_" + resolvedTypeName + "_drop";
      }
    }
  }

  // [Toka] Sync Trait: Mark type as IsSync
  if (getTraitFamilyName(canonicalTrait) == "sync" ||
      getTraitFamilyName(canonicalTrait) == "Sync") {
    if (ShapeMap.count(resolvedTypeName)) {
      ShapeMap[resolvedTypeName]->IsSync = true;
    }
  }
}

void Sema::declareImpl(ImplDecl *Impl) {
  registerSlice2Policy(Impl);
  registerSlice4Impl(Impl);
  std::string baseName = Impl->TypeName;
  size_t lt = baseName.find('<');
  if (lt != std::string::npos) {
    recordSlice1ImplFact(Impl, Impl->TypeName, Impl->TraitName);
    baseName = genericImplKey(baseName, Impl->Loc);
    if (std::find(GenericImplMap[baseName].begin(), GenericImplMap[baseName].end(), Impl) == GenericImplMap[baseName].end()) {
      GenericImplMap[baseName].push_back(Impl);
    }
    return; // Skip standard registration for templates
  }

  std::string selfTy = Impl->TypeName;
  for (auto &Method : Impl->Methods) {
    substituteSourceTypeSyntax(Method->ReturnTypeSyntax, Method->ReturnType,
                                "Self", selfTy);
    Method->syncReturnContractTypeCache();
    Method->ResolvedReturnType = nullptr;
    for (auto &Arg : Method->Args) {
      substituteSourceTypeSyntax(Arg.TypeSyntax, Arg.Type, "Self", selfTy);
      Arg.ResolvedType = nullptr;
    }
  }

  std::set<std::string> implemented;
  std::string resolvedTypeName = resolveType(Impl->TypeName);
  TraitDecl *traitDecl =
      Impl->TraitName.empty()
          ? nullptr
          : findVisibleTraitDecl(Impl->TraitName, getLoc(Impl));
  std::string canonicalTrait = canonicalTraitName(Impl->TraitName, traitDecl);
  auto requireSelfDependency = [&](const char *methodName) {
    for (const auto &method : Impl->Methods) {
      if (method->Name != methodName)
        continue;
      bool found = false;
      for (const auto &dep : method->LifeDependencies) {
        if (Type::stripMorphology(dep) == "self") {
          found = true;
          break;
        }
      }
      if (!found) {
        DiagnosticEngine::report(
            getLoc(method.get()),
            DiagID::ERR_SEMA_ITERATOR_DEPENDENCY_REQUIRED,
            canonicalTrait + "::" + methodName,
            resolvedTypeName);
        HasError = true;
      }
    }
  };
  if (getTraitFamilyName(canonicalTrait) == "Iterable")
    requireSelfDependency("iter");
  if (getTraitFamilyName(canonicalTrait) == "BorrowIterator")
    requireSelfDependency("next_ref");
  for (auto &Method : Impl->Methods) {
    if (!Method->IsClosureInvoke) {
      Method->CodegenName =
          Impl->TraitName.empty()
              ? resolvedTypeName + "_" + Method->Name
              : canonicalTrait + "_" + resolvedTypeName + "_" + Method->Name;
    }
    const bool isDropHook = getTraitFamilyName(canonicalTrait) == "Encap" &&
                            Method->Name == "drop";
    if (!isDropHook) {
      MethodMap[resolvedTypeName][Method->Name] = Method->ReturnType;
      MethodDecls[resolvedTypeName][Method->Name] = Method.get();
    }
    implemented.insert(Method->Name);
  }

  if (!Impl->TraitName.empty() &&
      getTraitFamilyName(canonicalTrait) != "Encap" &&
      getTraitFamilyName(canonicalTrait) != "Copy") {
    std::string implKey = resolvedTypeName + "@" + canonicalTrait;
    ImplMap[implKey];
    for (auto &Method : Impl->Methods) {
      ImplMap[implKey][Method->Name] = Method.get();
    }
  }

  if (getTraitFamilyName(canonicalTrait) == "Encap") {
    if (implemented.count("drop")) {
      m_ShapeProps[resolvedTypeName].HasDrop = true;
      if (ShapeMap.count(resolvedTypeName)) {
        ShapeMap[resolvedTypeName]->MangledDestructorName =
            "Encap_" + resolvedTypeName + "_drop";
      }
    }
  }

  if (getTraitFamilyName(canonicalTrait) == "sync" ||
      getTraitFamilyName(canonicalTrait) == "Sync") {
    if (ShapeMap.count(resolvedTypeName)) {
      ShapeMap[resolvedTypeName]->IsSync = true;
    }
  }
}

void Sema::checkFunction(FunctionDecl *Fn) {
  // [NEW] Skip Generic Templates
  // We cannot check them until they are instantiated with concrete types.
  if (!Fn->GenericParams.empty())
    return;

  ActiveNodeRAII Active(Fn);

  std::string savedRet =
      CurrentFunctionReturnType; // [FIX] Save state for recursion
  FunctionDecl *savedFn = CurrentFunction;
  std::string savedBorrowSource = m_LastBorrowSource;
  auto savedLifeDependencies = m_LastLifeDependencies;
  auto savedFieldDependencies = m_LastFieldDependencies;
  CurrentFunction = Fn;
  CurrentFunctionReturnType = Fn->ReturnType;
  m_LastBorrowSource.clear();
  m_LastLifeDependencies.clear();
  m_LastFieldDependencies.clear();

  // [New] Annotated AST: Resolve Return Type Object
  if (Fn->ReturnType != "void") {
    validateTypeVisibilityInType(Fn->ReturnType, getLoc(Fn));
    validateDynTraitObjectSafetyInType(Fn->ReturnType, getLoc(Fn));
    Fn->ResolvedReturnType = resolveType(
        Fn->ReturnTypeSyntax ? toka::Type::fromSyntax(Fn->ReturnTypeSyntax)
                             : toka::Type::fromString(Fn->ReturnType));
  } else {
    Fn->ResolvedReturnType = toka::Type::fromString("void");
  }

  if (Fn->Name == "main" && Fn->ResolvedReturnType) {
    auto mainRet = resolveType(Fn->ResolvedReturnType, false);
    std::string mainRetName = mainRet ? mainRet->getSoulName() : "unknown";
    if (mainRetName != "i32" && mainRetName != "void") {
      DiagnosticEngine::report(getLoc(Fn),
                               DiagID::ERR_SEMA_MAIN_RETURN_CONTRACT);
      HasError = true;
    }
  }

  enterScope(); // Function scope

  // Register arguments
  for (auto &Arg : Fn->Args) {
    debugCheckBindingPermission(Arg);
    debugCheckBindingTypeString("function argument", Arg.Name, Arg.Type,
                                Arg.Permission, Fn->Loc);
    SourceLocation argLoc = Arg.Loc.isValid() ? Arg.Loc : getLoc(Fn);
    if (Arg.IsValueBlocked || Arg.IsRebindBlocked) {
      DiagnosticEngine::report(argLoc, DiagID::ERR_REDUNDANT_BLOCK,
                               Arg.Name);
      HasError = true;
    }
    validateTypeVisibilityInType(Arg.Type, argLoc);
    validateDynTraitObjectSafetyInType(Arg.Type, argLoc);

    if (Arg.IsReference && !Arg.IsRebindable &&
        Type::stripMorphology(Arg.Name) != "self") {
      DiagnosticEngine::report(argLoc, DiagID::ERR_REDUNDANT_PARAM_BORROW);
      HasError = true;
    }

    SymbolInfo Info;
    // Preserve full generic-substituted Arg.Type values like "&i32".
    // [Fix] Preserve pre-resolved Types (e.g. Synthetic Closures)
    if (Arg.ResolvedType) {
      Info.TypeObj = Arg.ResolvedType;
    } else {
      Info.TypeObj =
          resolveType(Sema::synthesizePhysicalTypeObject(Arg, false));

      // Assign to AST Node for CodeGen
      Arg.ResolvedType = Info.TypeObj;
    }

    bool morphicPayloadWritable = false;
    if (Arg.IsMorphicExempt && Info.TypeObj) {
      auto payloadType = Info.TypeObj;
      if (payloadType->isPointer() || payloadType->isSmartPointer() ||
          payloadType->isReference())
        payloadType = payloadType->getPointeeType();
      morphicPayloadWritable = payloadType && payloadType->IsWritable;
    }
    bool declaredPayloadWritable =
        Arg.IsValueMutable || morphicPayloadWritable;

    Info.IsRebindable = Arg.IsRebindable;
    Info.Permission = BindingPermission::fromLegacy(
        Arg.IsRawPointer, Arg.IsUnique, Arg.IsShared, Arg.IsReference,
        Arg.IsRebindable, Arg.IsPointerNullable, Arg.IsRebindBlocked,
        declaredPayloadWritable, Arg.IsValueNullable, Arg.IsValueBlocked,
        Arg.IsMorphicExempt);
    Info.IsMorphicExempt = Arg.IsMorphicExempt; // [NEW]
    Info.IsDeclaredMutable = declaredPayloadWritable;
    Info.IsDeclaredVariable = true;
    Info.DeclLoc = argLoc;
    Info.IsCeded = Arg.IsCeded;
    Info.IsFunctionParameter = true;
    if (Info.TypeObj && (Info.TypeObj->isFunction() || Info.TypeObj->isDynFn()))
      Info.CallableReceiver = getCallableReceiverMode(*Info.TypeObj);


    if (!Arg.Type.empty() && Arg.Type[0] == '\'') {
      Info.IsMorphicExempt = true;
    }
    CurrentScope->define(Arg.Name, Info);

  }

  // --- Sema: Safety Redline Boundaries ---
  checkUnsafePublicFunctionBoundary(Fn);

  if (Fn->Body) {
    checkStmt(Fn->Body.get());

    for (auto &Arg : Fn->Args) {
      if (!Arg.IsCeded)
        continue;

      SymbolInfo *Info = nullptr;
      SourceLocation argLoc = Arg.Loc.isValid() ? Arg.Loc : getLoc(Fn);
      if (CurrentScope->findSymbol(Arg.Name, Info) && Info && !Info->Moved) {
        DiagnosticEngine::report(argLoc, DiagID::ERR_CEDE_PARAMETER_NOT_CONSUMED,
                                 Arg.Name);
        HasError = true;
        recordDecision(Fn, SemanticRuleID::OwnCede001,
                       SemanticOperation::CedeObligation,
                       SemanticDecision::Reject,
                       SemanticReason::UnconsumedCede, Arg.Name,
                       Fn->Name, argLoc);
        SemanticEvidence::recordCedeObligation(
            CedeObligationStage::CalleeConsumption,
            CedeObligationStatus::Violated, SemanticReason::UnconsumedCede,
            Arg.Name, Fn->Name, argLoc, argLoc);
      } else if (Info && Info->Moved) {
        SemanticEvidence::recordCedeObligation(
            CedeObligationStage::CalleeConsumption,
            CedeObligationStatus::Fulfilled, SemanticReason::CedeConsumed,
            Arg.Name, Fn->Name,
            Info->MoveLoc.isValid() ? Info->MoveLoc : argLoc, argLoc);
      }
    }

    // Check if all paths return if return type is not void
    if (Fn->ReturnType != "void") {
      if (!allPathsReturn(Fn->Body.get())) {
        DiagnosticEngine::report(getLoc(Fn), DiagID::ERR_CONTROL_REACHES_END,
                                 Fn->Name);
        HasError = true;
      }
    }
  }

  bool isWarningExempt = false;
  if (Fn->Loc.isValid()) {
    std::string path = DiagnosticEngine::SrcMgr->getFullSourceLoc(Fn->Loc).FileName;
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
            DiagnosticEngine::report(info.DeclLoc.isValid() ? info.DeclLoc : Fn->Loc, DiagID::WARN_MUTABLE_VAR_NEVER_MUTATED, name);
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
            DiagnosticEngine::report(info.DeclLoc.isValid() ? info.DeclLoc : Fn->Loc, DiagID::WARN_UNUSED_VARIABLE, name);
          }
        }
      }
      if (info.IsFunctionParameter && info.TypeObj &&
          (info.TypeObj->isPointer() || info.TypeObj->isSmartPointer() ||
           info.TypeObj->isReference()) &&
          !info.HasHandleBeenUsed && Type::stripMorphology(name) != "self") {
        std::string stripped = name;
        size_t idx = 0;
        while (idx < stripped.size() &&
               (stripped[idx] == '*' || stripped[idx] == '&' ||
                stripped[idx] == '^' || stripped[idx] == '~' ||
                stripped[idx] == '#')) {
          idx++;
        }
        if (stripped.empty() || idx >= stripped.size() || stripped[idx] != '_') {
          DiagnosticEngine::report(info.DeclLoc.isValid() ? info.DeclLoc : Fn->Loc,
                                   DiagID::WARN_HATTED_PARAM_HANDLE_UNUSED, name);
        }
      }
    }
  }

  exitScope();
  CurrentFunctionReturnType = savedRet; // [FIX] Restore state
  CurrentFunction = savedFn;
  if (savedFn) {
    m_LastBorrowSource = std::move(savedBorrowSource);
    m_LastLifeDependencies = std::move(savedLifeDependencies);
    m_LastFieldDependencies = std::move(savedFieldDependencies);
  } else {
    m_LastBorrowSource.clear();
    m_LastLifeDependencies.clear();
    m_LastFieldDependencies.clear();
  }
}

void Sema::checkImpl(ImplDecl *Impl) {
  // [NEW] Skip Generic Templates until Instantiation
  if (!Impl->GenericParams.empty()) {
      for (auto &Method : Impl->Methods) {
        checkUnsafePublicFunctionBoundary(Method.get());
      }
      return;
  }
  // (Assuming Impl<T> is handled similarly to Functions, but for now we focus
  // on non-generic Impl or instantiated ones)
  
  // [CRITICAL FIX] Skip synthetic Closure implementations! 
  // They have already been meticulously verified by checkClosureExpr with correctly mapped captured variables.
  // Re-evaluating them here strips the closure captures and breaks explicit cede variables.
  if (Impl->TypeName.find("__Closure_") == 0) {
      return;
  }

  if (!Impl->TraitName.empty()) {
    TraitDecl *TD = Impl->Loc.isValid()
                        ? findVisibleTraitDecl(Impl->TraitName, getLoc(Impl))
                        : findTraitDecl(Impl->TraitName);
    if (TD) {
      std::string resolvedTypeName = resolveType(Impl->TypeName);
      for (const auto &bound : TD->SelfTraitBounds) {
        TraitDecl *boundDecl = findVisibleTraitDecl(bound, TD->Loc);
        std::string canonicalBound = canonicalTraitName(bound, boundDecl);
        std::string implKey = resolvedTypeName + "@" + canonicalBound;
        bool satisfied = ImplMap.count(implKey);

        if (!satisfied && getTraitFamilyName(canonicalBound) == "Send") {
          auto typeObj = toka::Type::fromString(resolvedTypeName);
          satisfied = typeObj && typeObj->isSend(this);
        } else if (!satisfied &&
                   getTraitFamilyName(canonicalBound) == "Sync") {
          auto typeObj = toka::Type::fromString(resolvedTypeName);
          satisfied = typeObj && typeObj->isSync(this);
        }

        if (!satisfied) {
          DiagnosticEngine::report(getLoc(Impl),
                                   DiagID::ERR_TRAIT_PREREQUISITE_UNSATISFIED,
                                   Impl->TypeName, Impl->TraitName, bound);
          HasError = true;
        }
      }
    }
  }

  enterScope(); // Helper Scope for Self Injection

  // 1. Resolve Target Type (The "Self")
  std::shared_ptr<toka::Type> SelfType = nullptr;

  // Resolve the type name. Note: resolveType handles "Box<T>" if
  // instantiated, or "Box" if we are inside a generic context (which we
  // aren't yet for global impls). For now, let's assume we are checking
  // concrete impls OR we are just setting up the scope for "Self" to alias to
  // "TypeName".

  // Create a Type Object for the Impl's Target
  // We use Type::fromString but we might want to resolve aliases.
  SelfType = toka::Type::fromString(Impl->TypeName);

  // If we can resolve it deeper (e.g. valid shape), do so.
  SelfType = resolveType(SelfType);

  // 2. Define "Self" in the Scope
  if (SelfType) {
    SymbolInfo Sym;
    // Sym.Name = "Self"; // SymbolInfo doesn't store Name, Key does.
    Sym.IsTypeAlias = true;
    Sym.TypeObj = SelfType;
    CurrentScope->define("Self", Sym);
  } else {
    // Should we error?
  }

  auto assocIt = AssociatedTypeSubstitutionCache.find(Impl);
  if (assocIt != AssociatedTypeSubstitutionCache.end()) {
    for (const auto &[name, type] : assocIt->second) {
      SymbolInfo Sym;
      Sym.IsTypeAlias = true;
      Sym.TypeObj = resolveType(type);
      CurrentScope->define(name, Sym);
    }
  }

  // 3. Check all methods
  for (auto &Method : Impl->Methods) {
    // Methods inside Impl are FunctionDecls.
    checkFunction(Method.get());
  }

  exitScope();
}

void Sema::checkShapeSovereignty() {
}

void Sema::analyzeShapes(Module &M) {
  // Pass 2: Resolve Member Types (The "Filling" Phase)
  // This must happen after registerGlobals (Pass 1) so that all Shape names
  // are known.
  for (auto &S : M.Shapes) {
    // [NEW] Skip analysis for Generic Templates. They are analyzed only upon
    // Instantiation.
    if (!S->GenericParams.empty()) {
      checkUnsafePublicShapeBoundary(S.get());
      continue;
    }

    // We only resolve members for structs, enum payload records, and legacy bare unions (not enum variants).
    // purely yet? Enums have members too) Actually ShapeMember is used for
    // all.
    for (auto &member : S->Members) {
      auto resolveShapeMemberType = [&](ShapeMember &m) {
        if (m.ResolvedType)
          return;

        const std::string fullTypeStr = Sema::synthesizePhysicalType(m);
        validateTypeVisibilityInType(fullTypeStr, getLoc(S.get()));
        m.ResolvedType = resolveType(Sema::synthesizePhysicalTypeObject(m));

        std::shared_ptr<toka::Type> inner = m.ResolvedType;
        while (inner->isPointer() || inner->isArray()) {
          if (auto p = std::dynamic_pointer_cast<toka::PointerType>(inner))
            inner = p->PointeeType;
          else if (auto a = std::dynamic_pointer_cast<toka::ArrayType>(inner))
            inner = a->ElementType;
          else
            break;
        }
        if (auto *st = dynamic_cast<toka::ShapeType *>(inner.get())) {
          if (ShapeMap.count(st->Name)) {
            st->resolve(ShapeMap[st->Name]);
          }
        }
      };

      // Resolve the member itself (struct field or legacy union variant)
      resolveShapeMemberType(member);
      
      // Resolve SubMembers (mostly payloads for Enum variants)
      for (auto &subMemb : member.SubMembers) {
          resolveShapeMemberType(subMemb);
      }

      // 5. Basic Validation (Optional but good)
      if (member.ResolvedType->isUnknown()) {
        // ... (keep existing comments if any, or just ignore unknown)
      }

      // [Legacy] Bare union type blacklist: check underlying physics
      if (S->Kind == ShapeKind::Union) {
        auto underlying = getDeepestUnderlyingType(member.ResolvedType);
        bool invalid = false;
        std::string reason = "";

        if (underlying->isBoolean()) {
          invalid = true;
          reason = "bool";
        } else if (auto st =
                       std::dynamic_pointer_cast<toka::ShapeType>(underlying)) {
          // Check if it's a Strict Enum
          if (ShapeMap.count(st->Name)) {
            ShapeDecl *SD = ShapeMap[st->Name];
            if (SD->Kind == ShapeKind::Enum) {
              invalid = true;
              reason = "strict enum";
            }
          }
        }

        if (invalid) {
          DiagnosticEngine::report(
              getLoc(S.get()), DiagID::ERR_UNION_INVALID_MEMBER, member.Name,
              member.Type /* Original Name */, reason);
          HasError = true;
        }
      }
    }

    // --- Sema: Safety Redline Boundaries ---
    checkUnsafePublicShapeBoundary(S.get());
  }

  // First pass: Compute properties for all shapes
  for (auto &S : M.Shapes) {
    if (!S->GenericParams.empty())
      continue;
    if (m_ShapeProps[S->Name].Status != ShapeAnalysisStatus::Analyzed) {
      computeShapeProperties(S->Name, M);
    }
  }

  // Second pass: Enforce Rules
  for (auto &S : M.Shapes) {
    if (!S->GenericParams.empty())
      continue;
    auto &props = m_ShapeProps[S->Name];

    // A drop hook is explicit only when it is declared in an @Encap policy.
    bool hasExplicitDrop = false;
    for (auto &I : M.Impls) {
      if (I->TypeName == S->Name &&
          getTraitFamilyName(I->TraitName) == "Encap") {
        for (auto &method : I->Methods) {
          if (method->Name == "drop") {
            hasExplicitDrop = true;
            break;
          }
        }
      }
      if (hasExplicitDrop)
        break;
    }
    S->HasExplicitDrop = hasExplicitDrop;

    // [Legacy] Bare union safety: no resource types (HasDrop)
    if (S->Kind == ShapeKind::Union) {
      for (auto &memb : S->Members) {
        bool isResource = false;
        // 1. Check for ANY pointer morphology (&^~*) on the member itself
        // (Rule: no pointer morphology is allowed in legacy bare union members)
        if (memb.IsUnique || memb.IsShared || memb.IsRawPointer ||
            memb.IsReference) {
          isResource = true;
        }

        // [Rule Update] "but it does not restrict what members of members contain"
        // We no longer perform recursive check for resource types / drop
        // impls within value-type members of a legacy bare union.

        if (isResource) {
          DiagnosticEngine::report(getLoc(S.get()),
                                   DiagID::ERR_UNION_RESOURCE_TYPE, memb.Name,
                                   memb.Type);
          DiagnosticEngine::report(getLoc(S.get()),
                                   DiagID::NOTE_UNION_RESOURCE_TIP, memb.Type);
          HasError = true;
        }
      }
    }
  }
  validateSlice4CopyAndDup(M);
  recordSlice5InterfaceFacts(M);
}

void Sema::computeShapeProperties(const std::string &shapeName, Module &M) {
  if (m_ShapeProps.count(shapeName)) {
    auto &p = m_ShapeProps[shapeName];
    if (p.Status == ShapeAnalysisStatus::Analyzed)
      return;
  }
  auto &props = m_ShapeProps[shapeName];
  if (props.Status == ShapeAnalysisStatus::Visiting)
    return; // Cycle
  props.Status = ShapeAnalysisStatus::Visiting;

  // Find Shape Decl
  const ShapeDecl *S = nullptr;
  if (ShapeMap.count(shapeName))
    S = ShapeMap[shapeName];
  // Also check ModuleScope if using full path? Assume simplified for now or
  // look in M.Shapes
  if (!S) {
    for (auto &sh : M.Shapes)
      if (sh->Name == shapeName) {
        S = sh.get();
        break;
      }
  }

    if (S) {
    for (auto &member : S->Members) {
      std::string typeStr = member.Type;
      if (member.IsRawPointer) {
        props.HasRawPtr = true;
      }

      // [NEW] Trait auto-derivation
      auto memberTypeObj = toka::Type::fromString(typeStr);
      if (member.IsRawPointer) {
        props.IsSend = false;
        props.IsSync = false;
      } else {
        if (member.IsUnique) memberTypeObj = std::make_shared<toka::UniquePointerType>(memberTypeObj);
        else if (member.IsShared) memberTypeObj = std::make_shared<toka::SharedPointerType>(memberTypeObj);
        else if (member.IsReference) memberTypeObj = std::make_shared<toka::ReferenceType>(memberTypeObj);

        if (memberTypeObj) {
            if (!memberTypeObj->isSend(this)) props.IsSend = false;
            if (!memberTypeObj->isSync(this)) props.IsSync = false;
        }
      }

      if (member.IsUnique || member.IsShared || typeStr.rfind("^", 0) == 0 ||
          typeStr.rfind("~", 0) == 0) {
        props.HasDrop = true;
      }

      // Check if it's an array string "[T; N]"
      if (typeStr.size() > 0 && typeStr.front() == '[') {
        size_t semi = typeStr.rfind(';');
        if (semi != std::string::npos) {
          std::string inner = typeStr.substr(1, semi - 1);
          if (inner.rfind("^", 0) == 0 || inner.rfind("~", 0) == 0) {
            props.HasDrop = true;
          } else {
            computeShapeProperties(inner, M);
            if (hasDrop(inner))
              props.HasDrop = true;
            // Handle IsSync contagion
            if (ShapeMap.count(inner) && ShapeMap[inner]->IsSync) {
                // If member's type is sync, this shape is sync
                const_cast<ShapeDecl*>(S)->IsSync = true;
            }
          }
        }
      } else if (!member.IsRawPointer && !member.IsUnique && !member.IsShared &&
                 !member.IsReference && typeStr.rfind("^", 0) != 0 &&
                 typeStr.rfind("~", 0) != 0) {
        // Value Type T. Check if T is a Shape.
        std::string baseType = member.Type;
        if (ShapeMap.count(baseType)) {
          computeShapeProperties(baseType, M);
          std::string resolvedBase = resolveType(baseType);
          if (hasDrop(baseType)) props.HasDrop = true;
          if ((m_ShapeProps.count(baseType) && m_ShapeProps[baseType].HasManualDrop) ||
              (m_ShapeProps.count(resolvedBase) && m_ShapeProps[resolvedBase].HasManualDrop)) {
            props.HasManualDrop = true;
          }
          // [Toka] IsSync Contagion
          if (ShapeMap[baseType]->IsSync) {
              const_cast<ShapeDecl*>(S)->IsSync = true;
          }
        }

        // Also check if type T has 'drop' method itself
        bool memberTypeHasExplicitDrop = false;
        for (auto &I : M.Impls) {
          if (I->TypeName == baseType) {
            for (auto &M : I->Methods) {
              if (M->Name == "drop") {
                memberTypeHasExplicitDrop = true;
                break;
              }
            }
          }
        }
        std::string resolvedBase = resolveType(baseType);
        if ((MethodMap.count(baseType) && MethodMap[baseType].count("drop")) ||
            (MethodMap.count(resolvedBase) && MethodMap[resolvedBase].count("drop"))) {
          memberTypeHasExplicitDrop = true;
        }
        if (resolvedBase == "str" || resolvedBase == "bytes" || resolvedBase == "cstr" ||
            resolvedBase == "ViewStrSplitIterator" || resolvedBase == "ViewStrLinesIterator" ||
            resolvedBase == "string" || resolvedBase == "SlabID" || resolvedBase == "TimerHeap") {
          memberTypeHasExplicitDrop = false;
        }
        if (memberTypeHasExplicitDrop) {
          props.HasDrop = true;
          props.HasManualDrop = true;
        }
      }
    }
  }

  // Check if THIS shape has an explicit drop impl
  for (auto &I : M.Impls) {
    if (I->TypeName == shapeName) {
      for (auto &Met : I->Methods) {
        if (Met->Name == "drop") {
          props.HasDrop = true;
          props.HasManualDrop = true;
          break;
        }
      }
    }
  }
  std::string resolvedShape = resolveType(shapeName);
  if ((MethodMap.count(shapeName) && MethodMap[shapeName].count("drop")) ||
      (MethodMap.count(resolvedShape) && MethodMap[resolvedShape].count("drop"))) {
    props.HasDrop = true;
    props.HasManualDrop = true;
  }

  if (resolvedShape == "str" || resolvedShape == "bytes" || resolvedShape == "cstr" ||
      resolvedShape == "ViewStrSplitIterator" || resolvedShape == "ViewStrLinesIterator" ||
      resolvedShape == "string" || resolvedShape == "SlabID" || resolvedShape == "TimerHeap") {
    props.HasDrop = false;
    props.HasManualDrop = false;
  }

  props.Status = ShapeAnalysisStatus::Analyzed;
}

bool Sema::hasDrop(const std::string &shapeName) {
  std::string resolved = resolveType(shapeName);
  if (resolved == "str" || resolved == "bytes" || resolved == "cstr" ||
      resolved == "ViewStrSplitIterator" || resolved == "ViewStrLinesIterator" ||
      resolved == "string" || resolved == "SlabID" || resolved == "TimerHeap") {
    return false;
  }
  if (!m_ShapeProps.count(shapeName) && CurrentModule) {
    computeShapeProperties(shapeName, *CurrentModule);
  }
  if (!m_ShapeProps.count(resolved) && CurrentModule) {
    computeShapeProperties(resolved, *CurrentModule);
  }
  if (m_ShapeProps.count(shapeName) && m_ShapeProps[shapeName].HasDrop) return true;
  if (m_ShapeProps.count(resolved) && m_ShapeProps[resolved].HasDrop) return true;
  return false;
}

bool Sema::canImplicitlyPassToCede(std::shared_ptr<toka::Type> Ty) {
  if (!Ty) return false;

  // 1. Primitive, RawPtr, Function (普通函数指针), Reference (引用) 直接豁免
  if (Ty->typeKind == toka::Type::Primitive ||
      Ty->typeKind == toka::Type::RawPtr ||
      Ty->typeKind == toka::Type::Function ||
      Ty->typeKind == toka::Type::Reference) {
    return true;
  }

  // 2. Shape 类型判断
  if (Ty->typeKind == toka::Type::Shape) {
    std::string sName = Ty->toString();
    std::string resolved = resolveType(sName);

    // 如果是闭包合成类型，检查其捕获成员
    if (resolved.rfind("__Closure_", 0) == 0 || sName.rfind("__Closure_", 0) == 0) {
      std::string closureName = (resolved.rfind("__Closure_", 0) == 0) ? resolved : sName;
      if (ShapeMap.count(closureName)) {
        ShapeDecl *SD = ShapeMap[closureName];
        for (const auto &memb : SD->Members) {
          // 引用和借用本身不包含所有权转移，豁免
          if (memb.IsReference || memb.Type.rfind("&", 0) == 0) {
             continue;
          }
          std::shared_ptr<toka::Type> membTy = memb.ResolvedType;
          if (!membTy) {
             membTy = toka::Type::fromString(memb.Type);
          }
          if (!canImplicitlyPassToCede(membTy)) {
             return false; // 捕获了带 Drop / 资源类型的变量，不能豁免
          }
        }
        return true; // 所有捕获成员都是无所有权或无 Drop 的，豁免
      }
      return false; // 没找到该闭包定义，安全起见不豁免
    }

    // 内置/标准库中的有资源类型不予豁免，强制 cede
    if (resolved == "str" || resolved == "bytes" || resolved == "cstr" ||
        resolved == "ViewStrSplitIterator" || resolved == "ViewStrLinesIterator" ||
        resolved == "string" || resolved == "TimerHeap") {
      return false;
    }

    if (resolved == "SlabID") {
      return true; // 豁免 SlabID
    }

    // 其它的 Shape 检查是否包含显式 drop
    if (hasDrop(sName) || hasDrop(resolved)) {
      return false;
    }

    return true; // 默认无 drop 的 Shape 豁免
  }

  return false;
}

bool Sema::isStartBoundaryScalar(std::shared_ptr<toka::Type> Ty) const {
  if (!Ty || Ty->typeKind != toka::Type::Primitive)
    return false;
  const std::string name = Ty->getSoulName();
  return name != "str" && name != "bytes" && name != "cstr";
}

void Sema::checkStartBoundaryArgument(ASTNode *Node,
                                      std::shared_ptr<toka::Type> Ty,
                                      bool ParamIsCeded, bool ArgIsCeded,
                                      const std::string &Name,
                                      SourceLocation ParamLoc) {
  if (!m_IsStartingTask)
    return;
  if (isStartBoundaryScalar(Ty)) {
    recordDecision(Node, SemanticRuleID::AsyncCapture001,
                   SemanticOperation::ExecutionBoundaryArgument,
                   SemanticDecision::Allow, SemanticReason::NoConflict, Name,
                   Name, ParamLoc);
    return;
  }
  const std::string soul = Ty ? Ty->getSoulName() : "";
  bool isNonTransferableBorrow =
      Ty && (Ty->isReference() || Ty->isRawPointer() || soul == "str" ||
             soul == "bytes" || soul == "cstr");
  if (!isNonTransferableBorrow && ParamIsCeded && ArgIsCeded) {
    recordDecision(Node, SemanticRuleID::AsyncCapture001,
                   SemanticOperation::ExecutionBoundaryArgument,
                   SemanticDecision::Allow, SemanticReason::CedeConsumed,
                   Name, Name, ParamLoc);
    return;
  }
  DiagnosticEngine::report(getLoc(Node),
                           DiagID::ERR_SEMA_START_BOUNDARY_ARGUMENT, Name,
                           Ty ? Ty->toString() : "unknown");
  HasError = true;
  recordDecision(Node, SemanticRuleID::AsyncCapture001,
                 SemanticOperation::ExecutionBoundaryArgument,
                 SemanticDecision::Reject,
                 SemanticReason::BorrowedBoundaryArgument, Name, Name,
                 ParamLoc);
  if (ParamLoc.isValid())
    DiagnosticEngine::report(ParamLoc, DiagID::NOTE_GENERIC,
                             "execution-boundary parameter declared here");
}

bool Sema::isShapeSend(const std::string &shapeName) {
  // First, explicit manual trait impl ALWAYS takes precedence
  if (ImplMap.count(shapeName + "@Send")) {
    // std::cerr << "DEBUG isShapeSend(" << shapeName << ") -> true (ImplMap)\n";
    return true;
  }
  
  if (!m_ShapeProps.count(shapeName) && CurrentModule) {
    computeShapeProperties(shapeName, *CurrentModule);
  }
  
  bool res = m_ShapeProps[shapeName].IsSend;
  if (!res) {

  }
  return res;
}

bool Sema::isShapeSync(const std::string &shapeName) {
  if (ImplMap.count(shapeName + "@Sync") || ImplMap.count(shapeName + "@sync")) return true;
  if (!m_ShapeProps.count(shapeName) && CurrentModule) {
    computeShapeProperties(shapeName, *CurrentModule);
  }
  return m_ShapeProps[shapeName].IsSync;
}

FunctionDecl *Sema::instantiateGenericFunction(
    FunctionDecl *Template,
    const std::vector<std::shared_ptr<toka::Type>> &Args, CallExpr *CallSite) {

  if (Template->GenericParams.size() != Args.size()) {
    DiagnosticEngine::report(getLoc(CallSite), DiagID::NOTE_GENERIC, Template->Name, Template->GenericParams.size(), Args.size());
    HasError = true;
    return nullptr;
  }

  // [NEW] Check Trait Bounds
  for (size_t i = 0; i < Template->GenericParams.size(); ++i) {
    if (!Template->GenericParams[i].TraitBounds.empty()) {
      auto bounds = substituteTraitBounds(
          Template->GenericParams[i].TraitBounds, Template->GenericParams,
          Args);
      if (!checkTraitBounds(CallSite ? getLoc(CallSite) : Template->Loc, 
                            Template->GenericParams[i].Name, 
                            bounds,
                            Args[i]->toString(), false, Template->Loc)) {
        return nullptr;
      }
    }
  }

  // Mangling: Name_M_Arg1_Arg2
  std::string mangledName = Template->Name + "_M";
  for (auto &Arg : Args) {
    if (!Arg)
      continue;
    auto resolvedArg = resolveType(Arg);
    std::string argStr =
        resolvedArg ? resolvedArg->getMangledName() : Arg->getMangledName();
    mangledName += "_" + argStr;
  }

  // Recursion Guard
  if (RecursionDepth > 100) {
    DiagnosticEngine::report(getLoc(CallSite), DiagID::NOTE_GENERIC, Template->Name);
    HasError = true;
    return nullptr;
  }

  // Check Cache
  if (InstantiationCache.count(mangledName)) {
    return InstantiationCache[mangledName];
  }

  // Instantiate
  RecursionDepth++;

  // 1. Clone
  auto ClonedNode = Template->clone();
  FunctionDecl *Instance = static_cast<FunctionDecl *>(ClonedNode.release());
  std::unique_ptr<FunctionDecl> InstancePtr(Instance);

  Instance->Name = mangledName;
  Instance->CodegenName = mangledName;
  Instance->TemplateOrigin = Template;
  Instance->GenericParams.clear(); // Mark as concrete

  // 2. Scope Injection Setup
  enterScope();
  for (size_t i = 0; i < Template->GenericParams.size(); ++i) {
    const auto &GP = Template->GenericParams[i];
    std::string SubstVal = resolveType(Args[i])->toString(); // "10" or "i32"

    if (GP.IsConst) {
      SymbolInfo constInfo;
      // We assume it's a number (usize/integer).
      // We register it as a variable so checkExpr(VariableExpr(N)) works.
      constInfo.TypeObj =
          toka::Type::fromString(GP.Type.empty() ? "usize" : GP.Type);

      // [NEW] Set Const Value for Expression Evaluation
      uint64_t val = 0;
      try {
        val = std::stoull(SubstVal);
      } catch (...) {
      }
      constInfo.HasConstValue = true;
      constInfo.ConstValue = val;
      constInfo.ConstValObj = ComptimeValue(val);

      // NOTE: We don't set IsTypeAlias. Semantically it's a value.
      // But we need CodeGen to see it.
      // Strategy: Inject a synthetic variable declaration at the start of the
      // body.
      CurrentScope->define(GP.Name, constInfo);
      if (GP.IsMorphic && !GP.Name.empty() && GP.Name[0] == '\'')
        CurrentScope->define(GP.Name.substr(1), constInfo);

      // We will inject `auto N = 10;` into the body later.
    } else {
      SymbolInfo aliasInfo;
      aliasInfo.TypeObj = resolveType(Args[i]);
      aliasInfo.IsTypeAlias = true;
      CurrentScope->define(GP.Name, aliasInfo);
      if (GP.IsMorphic && !GP.Name.empty() && GP.Name[0] == '\'')
        CurrentScope->define(GP.Name.substr(1), aliasInfo);
    }
  }

  // [NEW] 2.5 Substitute Generic Types in Signature
  // We must update Arg types and ReturnType so callers see concrete types
  // (e.g. i32 instead of T)
  std::map<std::string, std::shared_ptr<toka::Type>> substMap;
  for (size_t i = 0; i < Template->GenericParams.size(); ++i) {
    const auto &GP = Template->GenericParams[i];
    auto substValue = resolveType(Args[i]);
    substMap[GP.Name] = substValue;
    if (GP.IsMorphic && !GP.Name.empty() && GP.Name[0] == '\'')
      substMap[GP.Name.substr(1)] = substValue;
  }

  auto applySubst = [&](std::string &s) {
    for (const auto &[K, V] : substMap) {
      if (s != K + "@Callable::Output")
        continue;
      if (auto fn = std::dynamic_pointer_cast<toka::FunctionType>(V)) {
        if (fn->ReturnType) {
          s = fn->ReturnType->toString();
          return;
        }
      }
      if (auto dynFn = std::dynamic_pointer_cast<toka::DynFnType>(V)) {
        if (dynFn->ReturnType) {
          s = dynFn->ReturnType->toString();
          return;
        }
      }
    }
    for (const auto &[K, V] : substMap)
      replaceTypeNameToken(s, K, V ? V->toString() : "unknown");
  };

  auto applyTypeSyntaxSubst = [&](TypeSyntaxPtr &syntax, std::string &spelling) {
    if (!syntax) {
      applySubst(spelling);
      return;
    }
    if (syntax->NodeKind == TypeSyntax::Kind::AssociatedProjection &&
        syntax->Text == "Callable" && syntax->MemberName == "Output" &&
        syntax->Subject &&
        syntax->Subject->NodeKind == TypeSyntax::Kind::Named) {
      auto replacement = substMap.find(syntax->Subject->Text);
      if (replacement != substMap.end()) {
        if (auto fn = std::dynamic_pointer_cast<toka::FunctionType>(
                replacement->second)) {
          if (fn->ReturnType) {
            syntax = fn->ReturnType->toSyntax(syntax->Begin, syntax->End);
            spelling = syntax->toCanonicalString();
            return;
          }
        }
        if (auto dynFn = std::dynamic_pointer_cast<toka::DynFnType>(
                replacement->second)) {
          if (dynFn->ReturnType) {
            syntax = dynFn->ReturnType->toSyntax(syntax->Begin, syntax->End);
            spelling = syntax->toCanonicalString();
            return;
          }
        }
      }
    }
    std::map<std::string, TypeSyntaxPtr> typedSubst;
    for (const auto &[name, replacement] : substMap) {
      if (replacement)
        typedSubst.emplace(name,
                           replacement->toSyntax(syntax->Begin, syntax->End));
    }
    syntax = syntax->substitute(typedSubst);
    spelling = syntax->toCanonicalString();
  };

  auto applyTypeArgumentSubst = [&](TypeArgumentSyntax &argument,
                                    std::string &spelling) {
    if (argument.ArgumentKind == TypeArgumentSyntax::Kind::Type) {
      applyTypeSyntaxSubst(argument.Type, spelling);
      return;
    }
    auto it = substMap.find(argument.ConstantText);
    if (it != substMap.end())
      argument.ConstantText = it->second ? it->second->toString() : "unknown";
    spelling = argument.toCanonicalString();
  };

  auto refersToMorphicParam = [&](const std::string &typeName) {
    for (const auto &GP : Template->GenericParams) {
      if (!GP.IsMorphic)
        continue;
      if (GP.Name == typeName)
        return true;
      if (!GP.Name.empty() && GP.Name[0] == '\'' &&
          GP.Name.substr(1) == typeName)
        return true;
    }
    return false;
  };

  // Substitute types in signature
  for (auto &Arg : Instance->Args) {
    if (refersToMorphicParam(Arg.Type)) {
      Arg.IsMorphicExempt = true;
      Arg.Permission.MorphicExempt = true;
    }
    applyTypeSyntaxSubst(Arg.TypeSyntax, Arg.Type);
    Arg.ResolvedType = nullptr;
  }
  applyTypeSyntaxSubst(Instance->ReturnTypeSyntax, Instance->ReturnType);
  Instance->syncReturnContractTypeCache();

  // [NEW] Substitute types in Body (Recursive Traversal)
  // We need to traverse Stmt and Expr to find nodes that store type strings:
  // VariableDecl: TypeName
  // NewExpr: Type
  // CastExpr: TargetType
  // AllocExpr: TypeName
  // InitStructExpr: ShapeName (if generic)
  // CallExpr: GenericArgs (vector<string>)
  // MethodCallExpr: N/A (args handled by Expr)
  // AnonymousRecordExpr: AssignedTypeName

  if (Instance->Body) {
    // Define Visitors
    std::function<void(Expr *)> visitExpr;
    std::function<void(Stmt *)> visitStmt;

    visitExpr = [&](Expr *e) {
      if (!e)
        return;
      // Clear ResolvedType on ALL expressions to force
      // re-type-check/resolution
      e->ResolvedType = nullptr;

      if (auto *ne = dynamic_cast<NewExpr *>(e)) {
        applyTypeSyntaxSubst(ne->TypeSyntax, ne->Type);
        if (ne->Initializer)
          visitExpr(ne->Initializer.get());
      } else if (auto *ae = dynamic_cast<AllocExpr *>(e)) {
        applyTypeSyntaxSubst(ae->TypeSyntax, ae->TypeName);
        if (ae->Initializer)
          visitExpr(ae->Initializer.get());
        if (ae->ArraySize)
          visitExpr(ae->ArraySize.get());
      } else if (auto *ce = dynamic_cast<CastExpr *>(e)) {
        applyTypeSyntaxSubst(ce->TargetTypeSyntax, ce->TargetType);
        if (ce->Expression)
          visitExpr(ce->Expression.get());
      } else if (auto *se = dynamic_cast<SizeOfExpr *>(e)) {
        // Const generic extents also occur in type-level expressions such as
        // `sizeof([u8; N_])`.  Leaving this spelling symbolic makes CodeGen
        // materialize a zero-length array even though the function instance
        // itself was specialized (for example bytes_from_array<32>).
        applyTypeSyntaxSubst(se->TypeSyntax, se->TypeStr);
      } else if (auto *ise = dynamic_cast<InitStructExpr *>(e)) {
        applySubst(ise->ShapeName);
        for (auto &m : ise->Members)
          visitExpr(m.second.get());
      } else if (auto *call = dynamic_cast<CallExpr *>(e)) {
        // Function Name (if it has generics embedded?) - Usually handled by
        // Parser logic putting generics in name? If so, applySubst on Callee.
        applySubst(call->Callee);
        for (size_t i = 0; i < call->GenericArgs.size(); ++i) {
          if (i < call->GenericArgSyntax.size())
            applyTypeArgumentSubst(call->GenericArgSyntax[i],
                                   call->GenericArgs[i]);
          else
            applySubst(call->GenericArgs[i]);
        }
        for (auto &arg : call->Args)
          visitExpr(arg.get());
      } else if (auto *mc = dynamic_cast<MethodCallExpr *>(e)) {
        // applySubst(mc->Method); // Method name usually doesn't have type
        // unless it's generic method?
        visitExpr(mc->Object.get());
        for (auto &arg : mc->Args)
          visitExpr(arg.get());
      } else if (auto *are = dynamic_cast<AnonymousRecordExpr *>(e)) {
        applySubst(are->AssignedTypeName);
        for (auto &m : are->Fields)
          if (m.second)
            visitExpr(m.second.get());
      } else if (auto *arrayInit = dynamic_cast<ArrayInitExpr *>(e)) {
        applyTypeSyntaxSubst(arrayInit->TypeSyntax, arrayInit->Type);
        if (arrayInit->Initializer)
          visitExpr(arrayInit->Initializer.get());
        if (arrayInit->ArraySize)
          visitExpr(arrayInit->ArraySize.get());
      } else if (auto *bin = dynamic_cast<BinaryExpr *>(e)) {
        visitExpr(bin->LHS.get());
        visitExpr(bin->RHS.get());
      } else if (auto *un = dynamic_cast<UnaryExpr *>(e)) {
        visitExpr(un->RHS.get());
      } else if (auto *fe = dynamic_cast<MemberExpr *>(e)) {
        visitExpr(fe->Object.get());
      } else if (auto *idx = dynamic_cast<ArrayIndexExpr *>(e)) {
        visitExpr(idx->Array.get());
        for (auto &i : idx->Indices)
          visitExpr(i.get());
      } else if (auto *me = dynamic_cast<MatchExpr *>(e)) {
        visitExpr(me->Target.get());
        for (auto &arm : me->Arms) {
          if (arm->Guard)
            visitExpr(arm->Guard.get());
          visitStmt(arm->Body.get());
        }
      } else if (auto *ifE = dynamic_cast<IfExpr *>(e)) {
        visitExpr(ifE->Condition.get());
        visitStmt(ifE->Then.get());
        if (ifE->Else)
          visitStmt(ifE->Else.get());
      } else if (auto *le = dynamic_cast<LoopExpr *>(e)) {
        visitStmt(le->Body.get());
      } else if (auto *fore = dynamic_cast<ForExpr *>(e)) {
        visitExpr(fore->Collection.get());
        visitStmt(fore->Body.get());
      } else if (auto *clo = dynamic_cast<ClosureExpr *>(e)) {
        applySubst(clo->ReturnType);
        if (clo->Body) visitStmt(clo->Body.get());
      } else if (auto *rep = dynamic_cast<RepeatedArrayExpr *>(e)) {
        visitExpr(rep->Value.get());
        visitExpr(rep->Count.get());
        // [FIX] Handle N_ replacement in Count if it was invalid
        if (auto *ve = dynamic_cast<VariableExpr *>(rep->Count.get())) {
          std::string name = Type::stripMorphology(ve->Name);
          if (substMap.count(name)) {
            std::string valStr = substMap[name] ? substMap[name]->toString()
                                                 : "unknown";
            // Check if it's a number
            try {
              uint64_t val = std::stoull(valStr);
              auto oldLoc = ve->Loc;
              rep->Count = std::make_unique<NumberExpr>(val);
              rep->Count->Loc = oldLoc;
            } catch (...) {
            }
          }
        }
      } else if (auto *ve = dynamic_cast<VariableExpr *>(e)) {
        // This is tricky because we can't easily replace 'e' here as it's
        // passed by pointer The caller usually holds 'std::unique_ptr<Expr>&'
        // but we have 'Expr*'. However, RepeatedArrayExpr above handles its
        // specific child case. For general cases (e.g. 'return N'), replacing
        // VariableExpr 'N' with NumberExpr '5' is harder without access to
        // the parent's unique_ptr. BUT: 'instantiateGenericFunction' visitor
        // infrastructure is weak here. Fortunately, for 'return N',
        // 'checkExpr' handles Variable lookup into Scope. The ERROR was
        // specific to RepeatedArrayExpr which demands a Literal/Const. By
        // fixing RepeatedArrayExpr specific logic above, we solve the array
        // size issue. For other cases, Scope Injection (N_ = 5) should
        // suffice.
      }
      // Note: VariableExpr, NumberExpr, etc don't have nested Exprs or Types
      // to substitute
    };

    visitStmt = [&](Stmt *s) {
      if (!s)
        return;
      if (auto *bs = dynamic_cast<BlockStmt *>(s)) {
        for (auto &sub : bs->Statements)
          visitStmt(sub.get());
      } else if (auto *vd = dynamic_cast<VariableDecl *>(s)) {
        applyTypeSyntaxSubst(vd->DeclaredTypeSyntax, vd->TypeName);
        vd->ResolvedType = nullptr; // Clear cache
        if (vd->Init)
          visitExpr(vd->Init.get());
      } else if (auto *rs = dynamic_cast<ReturnStmt *>(s)) {
        if (rs->ReturnValue)
          visitExpr(rs->ReturnValue.get());
      } else if (auto *es = dynamic_cast<ExprStmt *>(s)) {
        visitExpr(es->Expression.get());
      } else if (auto *ds = dynamic_cast<DeleteStmt *>(s)) {
        visitExpr(ds->Expression.get());
      } else if (auto *fs = dynamic_cast<FreeStmt *>(s)) {
        visitExpr(fs->Expression.get());
        if (fs->Count)
          visitExpr(fs->Count.get());
      } else if (auto *dd = dynamic_cast<DestructuringDecl *>(s)) {
        applySubst(dd->TypeName);
        if (dd->Init)
          visitExpr(dd->Init.get());
      } else if (auto *us = dynamic_cast<UnsafeStmt *>(s)) {
        visitStmt(us->Statement.get());
      }
    };

    visitStmt(Instance->Body.get());
  }

  // 3. Register in Module
  if (CurrentModule) {
    GenericInstancesModule->Functions.push_back(std::move(InstancePtr));
    Instance = GenericInstancesModule->Functions.back().get();

    std::string fileName = !CurrentModule->ResolvedPath.empty()
        ? toka::PathUtils::canonicalize(CurrentModule->ResolvedPath)
        : toka::PathUtils::canonicalize(
              DiagnosticEngine::SrcMgr->getFullSourceLoc(CurrentModule->Loc).FileName);
    ModuleMap[fileName].Functions[mangledName] = Instance;

    GlobalFunctions.push_back(Instance);
  } else {
    // Create independent ownership if no module context (shouldn't happen
    // here) For safety, leak it or manage elsewhere. But Sema always has
    // CurrentModule during analysis. If we are called from checkCallExpr,
    // CurrentModule is set.
    InstancePtr.release(); // Leak if no module? No, let's assume CurrentModule.
  }

  SourceLocation instantiationLoc =
      CallSite && CallSite->Loc.isValid() ? CallSite->Loc : Template->Loc;
  InstantiationLexicalScopes[Instance] = getLexicalModule(instantiationLoc);
  auto definition = DeclarationLexicalScopes.find(Template);
  if (definition != DeclarationLexicalScopes.end())
    DeclarationLexicalScopes[Instance] = definition->second;
  for (const auto &arg : Args)
    recordInstantiationType(Instance, resolveType(arg));

  // [NEW] Inject Const Generic Variables into Body
  // Removed dirty hack (VariableDecl injection).
  // Const values are now handled by SymbolInfo resolution in CodeGen.

  // 4. Semantic Check (Recursion)
  checkFunction(Instance);

  exitScope();
  RecursionDepth--;

  InstantiationCache[mangledName] = Instance;
  return Instance;
}

Sema::ModuleScope *Sema::getModule(const std::string &Path) {
  std::string canonicalPath = toka::PathUtils::canonicalize(Path);
  auto alias = ModulePathAliases.find(canonicalPath);
  if (alias != ModulePathAliases.end())
    return alias->second;
  if (ModuleMap.count(canonicalPath))
    return &ModuleMap[canonicalPath];
  return nullptr;
}

Sema::ModuleScope *Sema::getLexicalModule(SourceLocation loc) {
  if (!loc.isValid() || !DiagnosticEngine::SrcMgr)
    return nullptr;

  std::string file =
      DiagnosticEngine::SrcMgr->getFullSourceLoc(loc).FileName;
  std::string canonical = toka::PathUtils::canonicalize(file);
  auto alias = ModulePathAliases.find(canonical);
  if (alias != ModulePathAliases.end())
    return alias->second;
  auto exact = ModuleMap.find(canonical);
  if (exact != ModuleMap.end())
    return &exact->second;

  for (auto &[path, module] : ModuleMap) {
    if (path == file || toka::PathUtils::canonicalize(path) == canonical)
      return &module;
  }
  return nullptr;
}

std::string Sema::getModuleName(Module *M) {
  if (!M)
    return "root";
  std::string fullPath =
      DiagnosticEngine::SrcMgr->getFullSourceLoc(M->Loc).FileName;
  size_t lastSlash = fullPath.find_last_of('/');
  std::string name = (lastSlash == std::string::npos)
                         ? fullPath
                         : fullPath.substr(lastSlash + 1);
  size_t dot = name.find_last_of('.');
  if (dot != std::string::npos)
    name = name.substr(0, dot);
  return name;
}

int Sema::getScopeDepth(const std::string &Name) {
  Scope *S = CurrentScope;
  while (S) {
    if (S->Symbols.count(Name))
      return S->Depth;
    S = S->Parent;
  }
  return 0; // Global or not found (Global is 0)
}

bool Sema::checkVisibility(ASTNode *Node, ShapeDecl *SD) {
  if (m_DisableVisibilityCheck)
    return true;
  if (!SD)
    return true;

  // Visibility Check Logic
  std::string sdFile =
      DiagnosticEngine::SrcMgr->getFullSourceLoc(SD->Loc).FileName;
  std::string nodeFile =
      DiagnosticEngine::SrcMgr->getFullSourceLoc(Node->Loc).FileName;

  if (!SD->IsPub && sdFile != nodeFile) {
    bool sameModule = false;
    if (CurrentModule && !CurrentModule->Shapes.empty()) {
      for (const auto &shapeInModule : CurrentModule->Shapes) {
        if (DiagnosticEngine::SrcMgr->getFullSourceLoc(shapeInModule->Loc)
                .FileName == sdFile) {
          if (CurrentModule) {
            std::string modFile =
                DiagnosticEngine::SrcMgr->getFullSourceLoc(CurrentModule->Loc)
                    .FileName;
            if (modFile == sdFile) { // Simplified same-file/module check
              sameModule = true;
            }
          }
          break;
        }
      }
    }
    if (!sameModule) {
      DiagnosticEngine::report(getLoc(Node), DiagID::ERR_PRIVATE_TYPE, SD->Name,
                               sdFile);
      HasError = true;
      return false;
    }
  }
  return true;
}

bool Sema::isBorrowLikeType(std::shared_ptr<toka::Type> type) const {
  std::set<std::string> visited;
  std::function<bool(std::shared_ptr<toka::Type>)> walk =
      [&](std::shared_ptr<toka::Type> ty) -> bool {
    if (!ty)
      return false;
    if (ty->isReference())
      return true;
    if (auto *shape = dynamic_cast<ShapeType *>(ty.get())) {
      for (const auto &arg : shape->GenericArgs) {
        if (walk(arg))
          return true;
      }
      std::string name = ty->getSoulName();
      if (name == "str" || name == "bytes")
        return true;
      if (visited.count(name) == 0) {
        visited.insert(name);
        auto it = ShapeMap.find(name);
        if (it != ShapeMap.end()) {
          ShapeDecl *SD = it->second;
          for (const auto &member : SD->Members) {
            if (walk(getPhysicalType(member)))
              return true;
          }
        }
      }
    }
    return false;
  };
  return walk(type);
}

} // namespace toka
