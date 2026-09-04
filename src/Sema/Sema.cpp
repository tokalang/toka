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
#include "toka/CanonicalDeclarationWitness.h"
#include "toka/DiagnosticEngine.h"
#include "toka/HandleSurfaceStats.h"
#include "toka/SourceManager.h"
#include "toka/Type.h"
#include "toka/PathUtils.h"
#include "toka/Parser.h"
#include "toka/HandleGrammarAudit.h"
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
#include <tuple>
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
      permission.IdentityMayBeZero || permission.IdentityRebindable ||
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
            << ", identityMayBeZero=" << permission.IdentityMayBeZero
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

static void appendStableIdentityPart(std::string &out,
                                     const std::string &value) {
  out += std::to_string(value.size());
  out += ':';
  out += value;
  out += ';';
}

static std::string exactSymbolEncoding(const std::string &identity) {
  static constexpr char Hex[] = "0123456789abcdef";
  std::string result;
  result.reserve(identity.size() * 2);
  for (unsigned char byte : identity) {
    result += Hex[byte >> 4];
    result += Hex[byte & 0x0f];
  }
  return result;
}

static std::string genericFunctionCodegenName(const Module &M,
                                              const FunctionDecl &fn) {
  std::string identity = "toka.generic-function.v1;";
  if (M.ShadowCoordinateKnown && !M.ShadowCrateId.empty() &&
      !M.ShadowLogicalModulePath.empty()) {
    appendStableIdentityPart(identity, "resolver");
    appendStableIdentityPart(identity, M.ShadowCrateId);
    appendStableIdentityPart(identity, M.ShadowLogicalModulePath);
  } else {
    std::string path = M.SourcePath.empty() ? M.ResolvedPath : M.SourcePath;
    if (path.empty() && M.Loc.isValid() && DiagnosticEngine::SrcMgr) {
      path = DiagnosticEngine::SrcMgr->getFullSourceLoc(M.Loc).FileName;
    }
    appendStableIdentityPart(identity, "source");
    appendStableIdentityPart(identity, toka::PathUtils::canonicalize(path));
  }
  appendStableIdentityPart(identity, fn.Name);
  appendStableIdentityPart(identity, std::to_string(fn.GenericParams.size()));
  for (const auto &param : fn.GenericParams) {
    appendStableIdentityPart(identity, param.Name);
    for (auto bound : param.MorphologyBounds)
      appendStableIdentityPart(identity, morphologyConstraintName(bound));
  }
  return "__toka_gfn_" + exactSymbolEncoding(identity);
}

static std::string functionCodegenName(const Module &M,
                                       const FunctionDecl &fn) {
  if (!fn.GenericParams.empty())
    return genericFunctionCodegenName(M, fn);
  return moduleScopedCodegenName(M, fn.Name);
}

static bool isTrustedAtomicModule(const Module &module) {
  return module.IsTrustedSystemModule && module.ShadowCoordinateKnown &&
         module.ShadowCoordinateOrigin == "toolchain" &&
         module.ShadowLogicalModulePath == "core/intrinsics/atomic";
}

static bool isTrustedStdAtomicModule(const Module &module) {
  return module.IsTrustedSystemModule && module.ShadowCoordinateKnown &&
         module.ShadowCoordinateOrigin == "toolchain" &&
         module.ShadowLogicalModulePath == "std/atomic";
}

static bool isTrustedCoreTraitsModule(const Module &module) {
  return module.IsTrustedSystemModule && module.ShadowCoordinateKnown &&
         module.ShadowCoordinateOrigin == "toolchain" &&
         module.ShadowLogicalModulePath == "core/traits";
}

static bool isAtomicIntrinsicDeclaration(const Module &module,
                                         const FunctionDecl &function) {
  static const std::set<std::string> Names = {
      "__toka_atomic_load",       "__toka_atomic_store",
      "__toka_atomic_fetch_add",  "__toka_atomic_fetch_sub",
      "__toka_atomic_fetch_and",  "__toka_atomic_fetch_or",
      "__toka_atomic_fetch_xor",  "__toka_atomic_swap",
      "__toka_atomic_compare_exchange", "__toka_atomic_fence",
      "__toka_atomic_fence_acquire", "__toka_atomic_fence_release"};
  return isTrustedAtomicModule(module) && Names.count(function.Name) != 0;
}

static bool isAtomicWrapperDeclaration(const Module &module,
                                       const FunctionDecl &function) {
  static const std::set<std::string> Names = {
      "load",      "store",     "fetch_add", "fetch_sub",
      "fetch_and", "fetch_or",  "fetch_xor", "swap",
      "compare_exchange", "fence", "fence_acquire", "fence_release"};
  if (isTrustedAtomicModule(module))
    return Names.count(function.Name) != 0;
  return isTrustedStdAtomicModule(module) && function.Name == "fence";
}

static bool isStdAtomicMethodDeclaration(const Module &module,
                                         const ImplDecl &impl,
                                         const FunctionDecl &function) {
  static const std::set<std::string> Owners = {
      "AtomicI32", "AtomicUsize", "AtomicI64", "AtomicBool"};
  static const std::set<std::string> Methods = {
      "load", "store", "compare_exchange"};
  return isTrustedStdAtomicModule(module) && impl.TraitName.empty() &&
         Owners.count(Type::stripMorphology(impl.TypeName)) != 0 &&
         Methods.count(function.Name) != 0;
}

static bool supportsAtomicIntrinsicType(
    const std::string &functionName, const std::shared_ptr<Type> &type,
    std::string &expectedDomain) {
  const auto *primitive =
      type ? dynamic_cast<const PrimitiveType *>(type.get()) : nullptr;
  const bool isInteger = primitive && primitive->isInteger();
  const bool isFloating = primitive && primitive->isFloatingPoint();

  if (functionName == "__toka_atomic_fetch_add" ||
      functionName == "__toka_atomic_fetch_sub") {
    expectedDomain = "an integer or floating-point scalar";
    return isInteger || isFloating;
  }
  if (functionName == "__toka_atomic_fetch_and" ||
      functionName == "__toka_atomic_fetch_or" ||
      functionName == "__toka_atomic_fetch_xor") {
    expectedDomain = "an integer scalar";
    return isInteger;
  }
  if (functionName == "__toka_atomic_compare_exchange") {
    expectedDomain = "an integer scalar";
    return isInteger;
  }
  if (functionName == "__toka_atomic_load" ||
      functionName == "__toka_atomic_store" ||
      functionName == "__toka_atomic_swap") {
    expectedDomain = "an integer or floating-point scalar";
    return isInteger || isFloating;
  }
  return true;
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
  if (T->isMissOutcome()) {
    auto outcome = std::dynamic_pointer_cast<toka::MissOutcomeType>(T);
    if (outcome && isUnsafeType(outcome->PayloadType))
      return true;
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

ShapeDecl *Sema::resolveImplOwner(ImplDecl *impl) {
  if (!impl)
    return nullptr;

  std::shared_ptr<toka::Type> surfaceType =
      impl->HeaderSyntax.Type
          ? toka::Type::fromSyntax(impl->HeaderSyntax.Type)
          : toka::Type::fromString(impl->TypeName);
  auto surfaceShape = std::dynamic_pointer_cast<ShapeType>(
      surfaceType ? surfaceType->getSoulType() : nullptr);
  std::string surfaceName =
      surfaceShape ? surfaceShape->Name
                   : Type::stripMorphology(impl->TypeName);
  if (const size_t generic = surfaceName.find('<');
      generic != std::string::npos) {
    surfaceName.resize(generic);
  }

  auto findRegistered = [](ModuleScope *scope,
                           const void *declaration) -> ShapeDecl * {
    if (!scope || !declaration)
      return nullptr;
    for (const auto &[_, shape] : scope->Shapes) {
      if (shape == declaration)
        return shape;
    }
    return nullptr;
  };

  auto findInScope = [&](ModuleScope *scope,
                         const std::string &name) -> ShapeDecl * {
    if (!scope || name.empty())
      return nullptr;
    const size_t qualifier = name.find("::");
    if (qualifier != std::string::npos) {
      const std::string moduleName = name.substr(0, qualifier);
      const std::string targetName = name.substr(qualifier + 2);
      auto moduleSymbol = scope->LexicalSymbols.find(moduleName);
      if (moduleSymbol == scope->LexicalSymbols.end() ||
          !moduleSymbol->second.ReferencedModule)
        return nullptr;
      auto *target = static_cast<ModuleScope *>(
          moduleSymbol->second.ReferencedModule);
      auto shape = target->Shapes.find(targetName);
      return shape == target->Shapes.end() ? nullptr : shape->second;
    }

    auto symbol = scope->LexicalTypes.find(name);
    if (symbol == scope->LexicalTypes.end() || !symbol->second.IsTypeName ||
        symbol->second.IsTraitName || !symbol->second.ASTPtr)
      return nullptr;
    if (ShapeDecl *shape = findRegistered(scope, symbol->second.ASTPtr))
      return shape;
    return findRegistered(
        static_cast<ModuleScope *>(symbol->second.ReferencedModule),
        symbol->second.ASTPtr);
  };

  auto lexical = DeclarationLexicalScopes.find(impl);
  if (lexical != DeclarationLexicalScopes.end()) {
    if (ShapeDecl *owner = findInScope(lexical->second, surfaceName))
      return owner;
  }
  if (impl->Loc.isValid()) {
    if (ShapeDecl *owner = findInScope(getLexicalModule(impl->Loc), surfaceName))
      return owner;
  }

  // Materialized generic impls receive their exact owner directly from the
  // instantiation path; their synthetic concrete spelling is intentionally
  // absent from the source module's lexical table.
  return impl->TemplateOrigin ? impl->ResolvedOwner : nullptr;
}

PartialMovePlan Sema::admittedPartialMovePlan(const SymbolInfo &info) {
  if (!info.IsDeclaredVariable || info.IsFunctionParameter ||
      info.IsReference() || !info.TypeObj)
    return {};

  if (info.TypeObj->isArray()) {
    auto array = std::dynamic_pointer_cast<ArrayType>(info.TypeObj);
    if (!array || array->Size == 0 || array->Size > 64)
      return {};
    const uint64_t mask =
        array->Size == 64 ? ~0ULL : (1ULL << array->Size) - 1ULL;
    return PartialMovePlan::fixedArrayElements(mask);
  }

  if (!info.TypeObj->isShape())
    return {};
  auto shapeType = std::dynamic_pointer_cast<ShapeType>(info.TypeObj);
  ShapeDecl *shape = shapeType ? shapeType->Decl : nullptr;
  if (!shape)
    shape = findVisibleShapeDecl(info.TypeObj->getSoulName(), info.DeclLoc);
  if (!shape || shape->HasExplicitDrop ||
      (shape->Kind != ShapeKind::Struct && shape->Kind != ShapeKind::Tuple) ||
      shape->Members.empty() || shape->Members.size() > 64)
    return {};
  for (const auto &member : shape->Members) {
    if (member.IsShared)
      return {};
  }
  const uint64_t mask = shape->Members.size() == 64
                            ? ~0ULL
                            : (1ULL << shape->Members.size()) - 1ULL;
  return PartialMovePlan::directFields(mask);
}

void Sema::initializeProjectionFacts(SymbolInfo &info) {
  if (info.projectionFacts().isTracking())
    return;
  if (info.partialMovePlan().isAdmitted())
    info.ExactPlace.setPlan(info.partialMovePlan(), info.InitMask);
}

void Sema::syncLegacyProjectionLiveness(SymbolInfo &info) {
  info.InitMask = info.ExactPlace.applyToLegacyInitMask(info.InitMask);
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
  std::string constraints;
  for (const auto &param : impl->GenericParams) {
    for (auto bound : param.MorphologyBounds) {
      constraints += param.Name;
      constraints += ":";
      constraints += morphologyConstraintName(bound);
      constraints += ",";
    }
  }
  return owner + ";impl:" + impl->TypeName + "@" + impl->TraitName +
         ";morphology:" + constraints +
         ";loc:" + std::to_string(impl->Loc.getRawEncoding());
}

static void appendCanonicalIdentityField(std::string &out,
                                         const std::string &name,
                                         const std::string &value) {
  out += name + "=" + std::to_string(value.size()) + ":" + value + ";";
}

static void appendCanonicalOutcomeTypeAttributes(std::string &out,
                                                 const toka::Type &type) {
  appendCanonicalIdentityField(out, "cede", type.IsCede ? "1" : "0");
  appendCanonicalIdentityField(out, "writable",
                               type.IsWritable ? "1" : "0");
  appendCanonicalIdentityField(out, "nullable",
                               type.IsNullable ? "1" : "0");
  appendCanonicalIdentityField(out, "blocked", type.IsBlocked ? "1" : "0");
}

std::string
Sema::canonicalOutcomeModuleIdentity(const ModuleScope *module) const {
  if (!module || !module->ShadowCoordinateKnown)
    return "unbound";
  std::string result;
  appendCanonicalIdentityField(result, "crate", module->ShadowCrateId);
  appendCanonicalIdentityField(result, "module",
                               module->ShadowLogicalModulePath);
  return result;
}

bool Sema::canonicalOutcomeTypeIdentity(
    const std::shared_ptr<toka::Type> &type, std::string &result) const {
  if (!type)
    return false;

  result = "toka-outcome-type-v1;";
  appendCanonicalOutcomeTypeAttributes(result, *type);
  auto appendChild = [&](const std::string &name,
                         const std::shared_ptr<toka::Type> &child) {
    std::string childIdentity;
    if (!canonicalOutcomeTypeIdentity(child, childIdentity))
      return false;
    appendCanonicalIdentityField(result, name, childIdentity);
    return true;
  };

  if (dynamic_cast<const toka::UnitType *>(type.get())) {
    appendCanonicalIdentityField(result, "kind", "unit");
    return true;
  }
  if (dynamic_cast<const toka::VoidType *>(type.get())) {
    appendCanonicalIdentityField(result, "kind", "void");
    return true;
  }
  if (dynamic_cast<const toka::NeverType *>(type.get())) {
    appendCanonicalIdentityField(result, "kind", "never");
    return true;
  }
  if (const auto *primitive =
          dynamic_cast<const toka::PrimitiveType *>(type.get())) {
    appendCanonicalIdentityField(result, "kind", "primitive");
    appendCanonicalIdentityField(result, "name", primitive->Name);
    return true;
  }
  if (const auto *pointer =
          dynamic_cast<const toka::PointerType *>(type.get())) {
    std::string kind;
    switch (pointer->typeKind) {
    case toka::Type::RawPtr:
      kind = "raw-pointer";
      break;
    case toka::Type::UniquePtr:
      kind = "unique-pointer";
      break;
    case toka::Type::SharedPtr:
      kind = "shared-pointer";
      break;
    case toka::Type::Reference:
      kind = "reference";
      break;
    default:
      return false;
    }
    appendCanonicalIdentityField(result, "kind", kind);
    return appendChild("pointee", pointer->PointeeType);
  }
  if (const auto *array = dynamic_cast<const toka::ArrayType *>(type.get())) {
    // Symbolic extents belong to the generic-constant domain, which P1 has
    // not bound canonically yet.
    if (!array->SymbolicSize.empty())
      return false;
    appendCanonicalIdentityField(result, "kind", "array");
    appendCanonicalIdentityField(result, "extent", std::to_string(array->Size));
    return appendChild("element", array->ElementType);
  }
  if (const auto *slice = dynamic_cast<const toka::SliceType *>(type.get())) {
    appendCanonicalIdentityField(result, "kind", "slice");
    return appendChild("element", slice->ElementType);
  }
  if (const auto *shape = dynamic_cast<const toka::ShapeType *>(type.get())) {
    // No generic binders, aliases, anonymous records, projections, or enum
    // constructor suffixes are admitted by the narrow P1 type domain.
    if (!shape->Decl || !shape->GenericArgs.empty() ||
        !shape->VariantSuffix.empty() || !shape->Decl->GenericParams.empty())
      return false;
    auto owner = DeclarationLexicalScopes.find(shape->Decl);
    if (owner == DeclarationLexicalScopes.end() || !owner->second ||
        !owner->second->ShadowCoordinateKnown)
      return false;
    appendCanonicalIdentityField(result, "kind", "nominal");
    appendCanonicalIdentityField(result, "definition",
                                 canonicalOutcomeShapeIdentity(shape->Decl));
    return true;
  }

  // Function, dynamic-function, unresolved, Uninit, and all future type
  // kinds remain outside the concrete first-order P1 domain.
  return false;
}

std::string Sema::canonicalOutcomeFunctionIdentity(FunctionDecl *fn,
                                                   bool &hasCanonicalTypes) {
  hasCanonicalTypes = fn != nullptr;
  std::string result = "toka-outcome-def-v1;";
  const ModuleScope *owner = nullptr;
  auto scope = DeclarationLexicalScopes.find(fn);
  if (scope != DeclarationLexicalScopes.end())
    owner = scope->second;
  appendCanonicalIdentityField(result, "owner",
                               canonicalOutcomeModuleIdentity(owner));
  appendCanonicalIdentityField(result, "kind", "function");
  appendCanonicalIdentityField(result, "name", fn ? fn->Name : "<null>");
  appendCanonicalIdentityField(result, "generic-arity",
                               fn ? std::to_string(fn->GenericParams.size())
                                  : "0");
  appendCanonicalIdentityField(result, "effect",
                               fn ? std::to_string(static_cast<unsigned>(fn->Effect))
                                  : "0");
  if (!fn)
    return result;
  auto appendPhysicalType = [&](const std::string &field,
                                const std::shared_ptr<toka::Type> &type) {
    std::string identity;
    if (!canonicalOutcomeTypeIdentity(type, identity)) {
      hasCanonicalTypes = false;
      identity = "unavailable";
    }
    appendCanonicalIdentityField(result, field, identity);
  };
  for (size_t index = 0; index < fn->Args.size(); ++index) {
    const auto &arg = fn->Args[index];
    const std::string prefix = "arg" + std::to_string(index) + "-";
    appendCanonicalIdentityField(
        result, prefix + "kind", arg.IsInit ? "init" : "ordinary");
    appendCanonicalIdentityField(
        result, prefix + "cede", arg.IsCeded ? "1" : "0");
    appendPhysicalType(prefix + "type", arg.ResolvedType);
  }
  appendPhysicalType("return", fn->ResolvedReturnType);
  return result;
}

std::string
Sema::canonicalOutcomeShapeIdentity(const ShapeDecl *shape) const {
  std::string result = "toka-outcome-def-v1;";
  const ModuleScope *owner = nullptr;
  auto scope = DeclarationLexicalScopes.find(shape);
  if (scope != DeclarationLexicalScopes.end())
    owner = scope->second;
  appendCanonicalIdentityField(result, "owner",
                               canonicalOutcomeModuleIdentity(owner));
  appendCanonicalIdentityField(result, "kind", "shape");
  appendCanonicalIdentityField(result, "name", shape ? shape->Name : "<null>");
  appendCanonicalIdentityField(result, "generic-arity",
                               shape ? std::to_string(shape->GenericParams.size())
                                     : "0");
  return result;
}

std::optional<std::string>
Sema::canonicalOutcomeDeclarationWitness(FunctionDecl *fn) const {
  if (!fn || !fn->ResolvedOutcomeTransition)
    return std::nullopt;

  const auto &transition = *fn->ResolvedOutcomeTransition;
  if (!transition.HasKnownDeclarationCoordinates ||
      !transition.HasCanonicalTypeIdentities || !transition.ReturnEnum ||
      fn->GenericParams.size() != 0 ||
      transition.ReturnEnum->GenericParams.size() != 0)
    return std::nullopt;

  auto functionOwner = DeclarationLexicalScopes.find(fn);
  auto enumOwner = DeclarationLexicalScopes.find(transition.ReturnEnum);
  if (functionOwner == DeclarationLexicalScopes.end() || !functionOwner->second ||
      enumOwner == DeclarationLexicalScopes.end() || !enumOwner->second)
    return std::nullopt;

  OutcomeDeclarationWitnessInput input;
  input.FunctionCrateId = functionOwner->second->ShadowCrateId;
  input.FunctionLogicalModulePath =
      functionOwner->second->ShadowLogicalModulePath;
  input.FunctionName = fn->Name;
  input.FunctionGenericArity = fn->GenericParams.size();
  input.EffectKind = static_cast<unsigned>(fn->Effect);
  input.Parameters.reserve(fn->Args.size());
  for (size_t index = 0; index < fn->Args.size(); ++index) {
    std::string typeIdentity;
    if (!canonicalOutcomeTypeIdentity(fn->Args[index].ResolvedType,
                                      typeIdentity))
      return std::nullopt;
    input.Parameters.push_back({static_cast<uint32_t>(index),
                                fn->Args[index].IsInit,
                                fn->Args[index].IsCeded,
                                std::move(typeIdentity)});
  }
  if (!canonicalOutcomeTypeIdentity(fn->ResolvedReturnType,
                                    input.CanonicalResultType))
    return std::nullopt;

  input.OutcomeFormalIndex = static_cast<uint32_t>(transition.SubjectIndex);
  input.ReturnEnum = {enumOwner->second->ShadowCrateId,
                      enumOwner->second->ShadowLogicalModulePath,
                      transition.ReturnEnum->Name,
                      static_cast<uint32_t>(
                          transition.ReturnEnum->GenericParams.size())};
  input.Cases.reserve(transition.Cases.size());
  for (const auto &entry : transition.Cases) {
    if (!entry.Variant)
      return std::nullopt;
    input.Cases.push_back({entry.Variant->Name,
                           static_cast<uint32_t>(entry.VariantOrdinal),
                           entry.Post == OutcomePostState::Init});
  }
  return CanonicalDeclarationWitnessEncoder::encodeOutcomeTransition(input);
}

void Sema::populateOutcomeTransitionIdentities(FunctionDecl *fn) {
  if (!fn || !fn->ResolvedOutcomeTransition)
    return;

  auto &transition = *fn->ResolvedOutcomeTransition;
  auto hasKnownOwnerCoordinate = [this](const auto *declaration) {
    auto scope = DeclarationLexicalScopes.find(declaration);
    return scope != DeclarationLexicalScopes.end() && scope->second &&
           scope->second->ShadowCoordinateKnown;
  };
  // The Outcome formal is rooted at `fn`; each case is rooted at the direct
  // return enum.  This audit bit is deliberately narrower than a future CDW
  // record and remains non-authoritative until that schema is activated.
  transition.HasKnownDeclarationCoordinates =
      hasKnownOwnerCoordinate(fn) &&
      hasKnownOwnerCoordinate(transition.ReturnEnum);
  transition.FunctionIdentity = canonicalOutcomeFunctionIdentity(
      fn, transition.HasCanonicalTypeIdentities);
  transition.SubjectIdentity = transition.FunctionIdentity + "formal=" +
      std::to_string(transition.SubjectIndex) + ";";
  transition.ReturnEnumIdentity =
      canonicalOutcomeShapeIdentity(transition.ReturnEnum);
  for (auto &entry : transition.Cases) {
    entry.VariantIdentity = transition.ReturnEnumIdentity + "variant-name=" +
        std::to_string(entry.Variant ? entry.Variant->Name.size() : 0) + ":" +
        (entry.Variant ? entry.Variant->Name : "") + ";variant-ordinal=" +
        std::to_string(entry.VariantOrdinal) + ";";
  }
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
        hook->ReturnContract.ResultKind == ReturnResultKind::Unit &&
        !hook->ReturnContract.HasArrow && hook->Args.size() == 1 &&
        Type::stripMorphology(hook->Args[0].Name) == "self" &&
        hook->Args[0].IsValueMutable;
    if (!validHook) {
      DiagnosticEngine::report(impl->Loc, DiagID::ERR_GENERIC_SEMA,
                               "@Encap v3 drop hook must be private fn drop(self#)");
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
  auto shapeType = std::dynamic_pointer_cast<toka::ShapeType>(type);
  if (shapeType && shapeType->Decl &&
      (shapeType->GenericArgs.empty() ||
       shapeType->Decl->InstantiationTemplate)) {
    return proveSlice4Copy(shapeType->Decl);
  }
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
    // Enum members are variant shells, not stored values.  Only their
    // payload fields participate in the by-value Copy graph; unit variants
    // contribute no edge at all.
    if (shape->Kind != ShapeKind::Enum)
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
  ShapeDecl *nested = shapeType && shapeType->Decl
      ? shapeType->Decl
      : findVisibleShapeDecl(typeName,
                             context ? context->Loc : SourceLocation());
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
          if (shape->Kind != ShapeKind::Enum) {
            combine(deriveSlice4CopyRecipeType(
                toka::Type::fromString(synthesizePhysicalType(member)),
                shape));
          }
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
    // Publish one canonical non-generic Copy proof before body checking.
    // Later observation paths are read-only consumers of this established
    // fact; cache warmth or call order must not create a second proof route.
    const bool canonicalCopyProof = proveSlice4Copy(shape.get());
    if (copyRequest != Slice4CopyRequests.end()) {
      if (!Slice2PolicyMap.count(shape.get()) || !canonicalCopyProof) {
        DiagnosticEngine::report(copyRequest->second->Loc, DiagID::ERR_GENERIC_SEMA,
                                 "@Copy requires a trivial governed shape whose complete field graph is Copy");
        HasError = true;
      }
    }
    if (dup != Slice4DupProviders.end() && canonicalCopyProof) {
      DiagnosticEngine::report(dup->second->Loc, DiagID::ERR_GENERIC_SEMA,
                               "a user @Dup implementation overlaps the intrinsic Dup witness of @Copy");
      HasError = true;
    }
  }
}

void Sema::recordSlice5InterfaceFacts(Module &M) {
  M.InterfaceV2Facts.clear();
  M.CanonicalOutcomeDeclarationWitnesses.clear();
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
  for (const auto &fn : M.Functions) {
    if (!fn->ResolvedOutcomeTransition)
      continue;
    const auto &transition = *fn->ResolvedOutcomeTransition;
    if (auto cdw = canonicalOutcomeDeclarationWitness(fn.get())) {
      M.CanonicalOutcomeDeclarationWitnesses.push_back(*cdw);
      M.InterfaceV2Facts.push_back(
          "cdw1: " + CanonicalDeclarationWitnessEncoder::hexEncode(*cdw));
    }
    std::vector<std::string> cases;
    for (const auto &entry : transition.Cases) {
      cases.push_back(entry.VariantIdentity + "post=" +
                      (entry.Post == OutcomePostState::Init ? "init" :
                                                               "uninit") +
                      ";");
    }
    std::sort(cases.begin(), cases.end());
    std::string record = "outcome_transition: " +
        std::string("coordinate=") +
        (transition.HasKnownDeclarationCoordinates ? "known;" : "unbound;") +
        "type-domain=" +
        (transition.HasCanonicalTypeIdentities ? "canonical-v1;" :
                                                "unavailable;") +
        transition.FunctionIdentity + "subject=" + transition.SubjectIdentity +
        "result=" + transition.ReturnEnumIdentity;
    for (const auto &entry : cases)
      record += "case=" + entry;
    M.InterfaceV2Facts.push_back(std::move(record));
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
    const auto &arguments =
        shape->GenericArgs.empty() && shape->Decl &&
                shape->Decl->InstantiationTemplate
            ? shape->Decl->InstantiationArgs
            : shape->GenericArgs;
    for (const auto &arg : arguments)
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
          AssociatedTypeDecl output;
          output.Name = "Output";
          output.Type = method->ReturnType;
          output.TypeSyntax = method->ReturnTypeSyntax;
          output.ResolvedType = method->ResolvedReturnType;
          output.Loc = method->Loc;
          Impl->AssociatedTypes.push_back(std::move(output));
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
        implAssoc->ResolvedType
            ? implAssoc->ResolvedType
            : resolvedAssocSyntax ? toka::Type::fromSyntax(resolvedAssocSyntax)
                                  : toka::Type::fromString(resolvedAssocType);
    for (const auto &[knownName, knownType] : replacements) {
      if (resolvedAssoc)
        resolvedAssoc = resolvedAssoc->substitute({{knownName, knownType}});
    }
    resolvedAssoc = resolveType(resolvedAssoc);
    if (containsInternalPlaceOutcome(resolvedAssoc))
      error(Impl, DiagID::ERR_PLACE_OUTCOME_INTERNAL_ONLY,
            resolvedAssoc->toString());
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
      if (Impl->TemplateOrigin && Method->ResolvedReturnType) {
        Method->ResolvedReturnType =
            Method->ResolvedReturnType->substitute({{name, ty}});
      } else {
        Method->ResolvedReturnType = nullptr;
      }
      for (auto &Arg : Method->Args) {
        substituteSourceTypeSyntax(Arg.TypeSyntax, Arg.Type, name, ty);
        if (Impl->TemplateOrigin && Arg.ResolvedType) {
          Arg.ResolvedType = Arg.ResolvedType->substitute({{name, ty}});
        } else {
          Arg.ResolvedType = nullptr;
        }
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

  auto resolvedSelfType =
      resolveType(toka::Type::fromString(selfType), force);
  std::string resolvedSelf =
      resolvedSelfType ? resolvedSelfType->toString() : selfType;
  if (auto shape = std::dynamic_pointer_cast<ShapeType>(
          resolvedSelfType ? resolvedSelfType->getSoulType() : nullptr)) {
    if (shape->Decl) {
      resolvedSelf = shape->Decl->CodegenName.empty()
                         ? shape->Decl->Name
                         : shape->Decl->CodegenName;
    }
  }
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
  std::string resolvedSelf = selfType->toString();
  if (auto shape = std::dynamic_pointer_cast<ShapeType>(
          selfType->getSoulType())) {
    if (shape->Decl) {
      resolvedSelf = shape->Decl->CodegenName.empty()
                         ? shape->Decl->Name
                         : shape->Decl->CodegenName;
    }
  }

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
        if (call->second->ResolvedReturnType)
          return resolveType(call->second->ResolvedReturnType, force);
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
  if (m_AuthorityFactsSession) {
    auto storeBuildBefore = captureAuthorityParentSentinel();
    auto candidateStore = buildAuthorityCleanupClassStore();
    auto storeBuildAfter = captureAuthorityParentSentinel();
    auto storeBuildDifferences = differingAuthorityParentFields(
        storeBuildBefore, storeBuildAfter);
    auto storePublishBefore = captureAuthorityParentSentinel();
    m_AuthorityCleanupClassStore = std::move(candidateStore);
    m_AuthorityFactsSession->setCleanupStore(m_AuthorityCleanupClassStore);
    auto storePublishAfter = captureAuthorityParentSentinel();
    auto storePublishDifferences = differingAuthorityParentFields(
        storePublishBefore, storePublishAfter);
    storePublishDifferences.erase(
        std::remove(storePublishDifferences.begin(),
                    storePublishDifferences.end(), "cleanup_class_count"),
        storePublishDifferences.end());
    storePublishDifferences.erase(
        std::remove(storePublishDifferences.begin(),
                    storePublishDifferences.end(), "cleanup_class_facts"),
        storePublishDifferences.end());
    m_AuthorityFactsSession->setCleanupStoreQualification(
        storeBuildDifferences.empty(), storePublishDifferences.empty(),
        std::move(storeBuildDifferences),
        std::move(storePublishDifferences));
  }

  // 2b. Check function bodies (reordered)

  for (size_t i = 0; i < M.Functions.size(); ++i) {
    if (!M.Functions[i]->GenericParams.empty()) {
      checkFunction(M.Functions[i].get());
      checkUnsafePublicFunctionBoundary(M.Functions[i].get());
      continue; // [NEW] Skip Generic Templates
    }
    checkFunction(M.Functions[i].get());
  }

  for (const auto &trait : M.Traits) {
    std::set<std::string> allowedNames = {"Self"};
    for (const auto &associated : trait->AssociatedTypes)
      allowedNames.insert(associated.Name);
    for (const auto &method : trait->Methods)
      validateGenericSignatureTypeNames(method.get(), trait->GenericParams,
                                        allowedNames);
  }

  // 2c. Check Impl blocks (NEW: Proper Self Injection)
  for (size_t i = 0; i < M.Impls.size(); ++i) {
    if (!M.Impls[i]->GenericParams.empty()) {
      std::set<std::string> allowedNames = {"Self"};
      for (const auto &associated : M.Impls[i]->AssociatedTypes)
        allowedNames.insert(associated.Name);
      TraitDecl *trait = M.Impls[i]->TraitName.empty()
                             ? nullptr
                             : findVisibleTraitDecl(M.Impls[i]->TraitName,
                                                    M.Impls[i]->Loc);
      if (trait) {
        for (const auto &associated : trait->AssociatedTypes)
          allowedNames.insert(associated.Name);
      }
      for (auto &Method : M.Impls[i]->Methods) {
        validateGenericSignatureTypeNames(Method.get(),
                                          M.Impls[i]->GenericParams,
                                          allowedNames);
        checkUnsafePublicFunctionBoundary(Method.get());
      }
      continue; // Skip templates, they are checked upon instantiation
    }
    checkImpl(M.Impls[i].get());
  }
  recordSlice5InterfaceFacts(M);
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

std::optional<Sema::AuthorityFullExpressionContext>
Sema::beginAuthorityFullExpression(Expr *root) {
  auto previous = m_AuthorityFullExpression;
  if (!m_AuthorityFactsSession || !root || !root->Loc.isValid()) {
    m_AuthorityFullExpression.reset();
    return previous;
  }
  auto before = captureAuthorityParentSentinel();
  auto fullLoc = DiagnosticEngine::SrcMgr->getFullSourceLoc(root->Loc);
  std::string kind = "expr";
  if (dynamic_cast<CallExpr *>(root))
    kind = "call";
  else if (dynamic_cast<BinaryExpr *>(root))
    kind = "binary";
  else if (dynamic_cast<VariableExpr *>(root))
    kind = "variable";
  else if (dynamic_cast<InitStructExpr *>(root))
    kind = "init-struct";
  else if (dynamic_cast<ClosureExpr *>(root))
    kind = "closure";
  const std::string witness = std::to_string(fullLoc.Line) + ":" +
                              std::to_string(fullLoc.Column) + ":" + kind;
  auto rootNode =
      SemanticIdentityBuilder::semanticNode(fullLoc.FileName, witness);
  if (!rootNode) {
    m_AuthorityFullExpression.reset();
    return previous;
  }
  auto expression = SemanticIdentityBuilder::fullExpression(rootNode.value());
  if (!expression) {
    m_AuthorityFullExpression.reset();
    return previous;
  }
  auto after = captureAuthorityParentSentinel();
  auto differences = differingAuthorityParentFields(before, after);
  if (m_AuthorityFactsSession->takeFaultPoint(
          AuthorityFaultPoint::AfterFullExpressionIdentity, fullLoc.FileName)) {
    AuthorityObservationReceipt receipt;
    receipt.File = fullLoc.FileName;
    receipt.Line = fullLoc.Line;
    receipt.Column = fullLoc.Column;
    receipt.FullExpression = expression.value();
    receipt.BuildDifferences = differences;
    receipt.BuildParentUnchanged = differences.empty();
    receipt.PublishParentUnchanged = true;
    receipt.RevisionSizeBefore =
        m_AuthorityFactsSession->revision()
            ? m_AuthorityFactsSession->revision()->size()
            : 0;
    receipt.RevisionSizeAfter = receipt.RevisionSizeBefore;
    m_AuthorityFactsSession->noteError(AuthorityBuildError::MalformedRevision,
                                       std::move(receipt));
    error(root, DiagID::ERR_GENERIC_SEMA,
          "internal M1b.2a full-expression fault");
    m_AuthorityFullExpression.reset();
    return previous;
  }
  m_AuthorityFullExpression = AuthorityFullExpressionContext{
      root, rootNode.value(), expression.value(), differences.empty(),
      std::move(differences)};
  return previous;
}

void Sema::restoreAuthorityFullExpression(
    std::optional<AuthorityFullExpressionContext> previous) {
  m_AuthorityFullExpression = std::move(previous);
}

std::optional<ConcreteTypeId>
Sema::authorityConcreteTypeId(const std::shared_ptr<Type> &type) const {
  if (!type || type->isUnknown())
    return std::nullopt;
  auto shape = std::dynamic_pointer_cast<ShapeType>(type->getSoulType());
  if (!shape || !shape->Decl || !shape->Decl->NominalId ||
      !shape->GenericArgs.empty())
    return std::nullopt;
  std::string origin;
  if (shape->Decl->Loc.isValid())
    origin =
        DiagnosticEngine::SrcMgr->getFullSourceLoc(shape->Decl->Loc).FileName;
  if (origin.empty())
    return std::nullopt;
  auto identity = SemanticIdentityBuilder::concreteType(
      origin, shape->Decl->NominalId->canonical());
  return identity ? std::optional<ConcreteTypeId>(identity.value())
                  : std::nullopt;
}

CleanupClassStore Sema::buildAuthorityCleanupClassStore() {
  std::vector<CleanupClassFact> entries;
  std::set<const ShapeDecl *> emitted;
  std::set<const ShapeDecl *> visiting;
  std::map<const ShapeDecl *, CleanupClassFact> memo;

  std::function<CleanupClassFact(ShapeDecl *)> classify =
      [&](ShapeDecl *shape) -> CleanupClassFact {
    if (auto found = memo.find(shape); found != memo.end())
      return found->second;
    CleanupClassFact fact;
    if (!shape)
      return fact;
    auto shapeType = std::make_shared<ShapeType>(shape->Name);
    shapeType->resolve(shape);
    auto typeId = authorityConcreteTypeId(shapeType);
    if (!typeId) {
      fact.Kind = CleanupClassKind::Indeterminate;
      fact.Reason = CleanupClassIndeterminateReason::MissingConcreteTypeGraph;
      fact.Source = CleanupClassSource::Incomplete;
      return fact;
    }
    fact.Type = *typeId;
    if (shape->Loc.isValid()) {
      const std::string source =
          DiagnosticEngine::SrcMgr->getFullSourceLoc(shape->Loc).FileName;
      if (source.size() >= 4 &&
          source.compare(source.size() - 4, 4, ".tki") == 0) {
        fact.Kind = CleanupClassKind::Indeterminate;
        fact.Reason = CleanupClassIndeterminateReason::GenericOrSourceHidden;
        fact.Source = CleanupClassSource::Incomplete;
        memo[shape] = fact;
        return fact;
      }
    }
    std::string name = Type::stripMorphology(shape->Name);
    if (size_t scope = name.rfind("::"); scope != std::string::npos)
      name = name.substr(scope + 2);
    if (name == "str" || name == "bytes" || name == "cstr" ||
        name == "ViewStrSplitIterator" || name == "ViewStrLinesIterator") {
      fact.Kind = CleanupClassKind::NoCleanup;
      fact.Reason = CleanupClassIndeterminateReason::None;
      fact.Source = CleanupClassSource::BorrowedView;
    } else if (name == "string" || name == "Bytes") {
      fact.Kind = CleanupClassKind::OwnedWholeCleanup;
      fact.Reason = CleanupClassIndeterminateReason::None;
      fact.Source = CleanupClassSource::BuiltinOwnedBuffer;
    } else {
      bool hasEncapDrop = shape->HasExplicitDrop;
      if (auto genericImpls = GenericImplMap.find(shape->Name);
          !hasEncapDrop && genericImpls != GenericImplMap.end()) {
        for (auto *impl : genericImpls->second) {
          if (!impl || getTraitFamilyName(impl->TraitName) != "Encap")
            continue;
          for (const auto &method : impl->Methods)
            if (method->Name == "drop")
              hasEncapDrop = true;
          if (hasEncapDrop)
            break;
        }
      }
      if (hasEncapDrop) {
        fact.Kind = CleanupClassKind::OwnedWholeCleanup;
        fact.Reason = CleanupClassIndeterminateReason::None;
        fact.Source = CleanupClassSource::ExplicitEncapDrop;
      } else if (!shape->GenericParams.empty()) {
        fact.Kind = CleanupClassKind::Indeterminate;
        fact.Reason = CleanupClassIndeterminateReason::GenericOrSourceHidden;
        fact.Source = CleanupClassSource::Incomplete;
      } else if (!visiting.insert(shape).second) {
        fact.Kind = CleanupClassKind::Indeterminate;
        fact.Reason = CleanupClassIndeterminateReason::RecursiveCycle;
        fact.Source = CleanupClassSource::Incomplete;
      } else {
        bool owned = false;
        bool incomplete = false;
        for (const auto &member : shape->Members) {
          if (member.IsUnique || member.IsShared) {
            owned = true;
            break;
          }
          auto memberType = member.ResolvedType;
          if (!memberType) {
            std::string base = Type::stripMorphology(member.Type);
            base.erase(std::remove_if(
                           base.begin(), base.end(),
                           [](unsigned char ch) { return std::isspace(ch); }),
                       base.end());
            if (size_t lt = base.find('<'); lt != std::string::npos)
              base = base.substr(0, lt);
            if (size_t scope = base.rfind("::"); scope != std::string::npos)
              base = base.substr(scope + 2);
            auto declaration = ShapeMap.find(base);
            if (declaration == ShapeMap.end()) {
              declaration = std::find_if(
                  ShapeMap.begin(), ShapeMap.end(), [&](const auto &entry) {
                    return entry.second && entry.second->Name == base;
                  });
            }
            if (declaration == ShapeMap.end()) {
              incomplete = true;
              continue;
            }
            auto nested = classify(declaration->second);
            if (nested.Kind == CleanupClassKind::OwnedWholeCleanup) {
              owned = true;
              break;
            }
            if (nested.Kind == CleanupClassKind::Indeterminate)
              incomplete = true;
            continue;
          }
          if (memberType->isUniquePtr() || memberType->isSharedPtr()) {
            owned = true;
            break;
          }
          auto memberShape =
              std::dynamic_pointer_cast<ShapeType>(memberType->getSoulType());
          if (!memberShape)
            continue;
          std::string base = Type::stripMorphology(memberShape->Name);
          base.erase(
              std::remove_if(base.begin(), base.end(),
                             [](unsigned char ch) { return std::isspace(ch); }),
              base.end());
          if (size_t lt = base.find('<'); lt != std::string::npos)
            base = base.substr(0, lt);
          if (size_t scope = base.rfind("::"); scope != std::string::npos)
            base = base.substr(scope + 2);
          ShapeDecl *memberDeclaration =
              memberShape->Decl && memberShape->Decl->InstantiationTemplate
                  ? memberShape->Decl->InstantiationTemplate
                  : memberShape->Decl;
          if (!memberDeclaration) {
            auto declaration = ShapeMap.find(base);
            if (declaration == ShapeMap.end()) {
              declaration = std::find_if(
                  ShapeMap.begin(), ShapeMap.end(), [&](const auto &entry) {
                    return entry.second && entry.second->Name == base;
                  });
            }
            if (declaration != ShapeMap.end())
              memberDeclaration = declaration->second;
          }
          if (!memberDeclaration) {
            incomplete = true;
            continue;
          }
          auto nested = classify(memberDeclaration);
          if (nested.Kind == CleanupClassKind::OwnedWholeCleanup) {
            owned = true;
            break;
          }
          if (nested.Kind == CleanupClassKind::Indeterminate)
            incomplete = true;
        }
        visiting.erase(shape);
        if (owned) {
          fact.Kind = CleanupClassKind::OwnedWholeCleanup;
          fact.Reason = CleanupClassIndeterminateReason::None;
          fact.Source = CleanupClassSource::StructuralOwnedField;
        } else if (incomplete) {
          fact.Kind = CleanupClassKind::Indeterminate;
          fact.Reason = CleanupClassIndeterminateReason::ColdAnalysis;
          fact.Source = CleanupClassSource::Incomplete;
        } else {
          fact.Kind = CleanupClassKind::NoCleanup;
          fact.Reason = CleanupClassIndeterminateReason::None;
          fact.Source = CleanupClassSource::ProvenNoCleanup;
        }
      }
    }
    memo[shape] = fact;
    return fact;
  };

  for (const auto &entry : ShapeMap) {
    if (entry.second && emitted.insert(entry.second).second) {
      auto fact = classify(entry.second);
      if (fact.Type.valid())
        entries.push_back(std::move(fact));
    }
  }
  auto built = CleanupClassStore::build(std::move(entries));
  return built.second == CleanupClassStoreError::None ? std::move(built.first)
                                                      : CleanupClassStore{};
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

std::optional<NominalShapeId>
Sema::makeDeclaredShapeId(const Module &module,
                          const ShapeDecl &shape) const {
  if (module.ShadowCoordinateKnown) {
    if (module.ShadowCrateId.empty() ||
        module.ShadowLogicalModulePath.empty())
      return std::nullopt;
    return NominalShapeId::fromResolverCoordinate(
        module.ShadowCrateId, module.ShadowLogicalModulePath, shape.Name,
        shape.GenericParams.size());
  }

  std::string sourcePath = module.SourcePath;
  if (sourcePath.empty())
    sourcePath = module.ResolvedPath;
  if (sourcePath.empty() && module.Loc.isValid()) {
    sourcePath =
        DiagnosticEngine::SrcMgr->getFullSourceLoc(module.Loc).FileName;
  }
  if (sourcePath.empty())
    return std::nullopt;
  return NominalShapeId::fromSourcePath(
      toka::PathUtils::canonicalize(sourcePath), shape.Name,
      shape.GenericParams.size());
}

void Sema::declareGlobals(Module &M) {
  recordHandleSurfaceModule(M);
  std::set<std::string> declaredTypeParameters;
  auto rememberTypeParameters = [&](const std::vector<GenericParam> &params) {
    for (const auto &parameter : params) {
      if (parameter.IsConst)
        continue;
      declaredTypeParameters.insert(parameter.Name);
      if (!parameter.Name.empty() && parameter.Name.front() == '\'')
        declaredTypeParameters.insert(parameter.Name.substr(1));
    }
  };
  auto addExactTypeParameters = [](std::set<std::string> &names,
                                   const std::vector<GenericParam> &params) {
    for (const auto &parameter : params) {
      if (parameter.IsConst)
        continue;
      names.insert(parameter.Name);
      if (!parameter.Name.empty() && parameter.Name.front() == '\'')
        names.insert(parameter.Name.substr(1));
    }
  };
  auto annotateStage0FormalDeclarations =
      [&](FunctionDecl &function,
          const std::vector<GenericParam> *enclosingParameters) {
        std::set<std::string> exactGenericNames;
        if (enclosingParameters)
          addExactTypeParameters(exactGenericNames, *enclosingParameters);
        addExactTypeParameters(exactGenericNames, function.GenericParams);
        for (auto &argument : function.Args) {
          argument.Stage0DeclarationProvenanceComplete =
              argument.TypeSyntax != nullptr;
          argument.Stage0GenericValueRole = false;
          argument.Stage0MorphicGenericRole = false;
          if (!argument.Stage0DeclarationProvenanceComplete ||
              argument.IsRawPointer || argument.IsUnique ||
              argument.IsShared || argument.IsReference ||
              argument.TypeSyntax->NodeKind != TypeSyntax::Kind::Named)
            continue;
          const bool namesGeneric =
              exactGenericNames.count(argument.TypeSyntax->Text) != 0;
          argument.Stage0MorphicGenericRole =
              namesGeneric && argument.IsMorphicExempt;
          argument.Stage0GenericValueRole =
              namesGeneric && !argument.IsMorphicExempt;
        }
      };
  for (auto &function : M.Functions)
    annotateStage0FormalDeclarations(*function, nullptr);
  for (auto &trait : M.Traits) {
    for (auto &method : trait->Methods)
      annotateStage0FormalDeclarations(*method, &trait->GenericParams);
  }
  for (auto &impl : M.Impls) {
    for (auto &method : impl->Methods)
      annotateStage0FormalDeclarations(*method, &impl->GenericParams);
  }
  for (const auto &function : M.Functions)
    rememberTypeParameters(function->GenericParams);
  for (const auto &shape : M.Shapes)
    rememberTypeParameters(shape->GenericParams);
  for (const auto &trait : M.Traits)
    rememberTypeParameters(trait->GenericParams);
  for (const auto &impl : M.Impls)
    rememberTypeParameters(impl->GenericParams);
  for (const auto &alias : M.TypeAliases)
    rememberTypeParameters(alias->GenericParams);
  if (std::find(DeclaredModules.begin(), DeclaredModules.end(), &M) ==
      DeclaredModules.end())
    DeclaredModules.push_back(&M);

  std::string fileName = !M.ResolvedPath.empty()
      ? toka::PathUtils::canonicalize(M.ResolvedPath)
      : toka::PathUtils::canonicalize(
            DiagnosticEngine::SrcMgr->getFullSourceLoc(M.Loc).FileName);
  ModuleScope &ms = ModuleMap[fileName];
  ms.GenericTypeParameterNames.insert(declaredTypeParameters.begin(),
                                      declaredTypeParameters.end());
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
    Fn->IsTrustedAtomicIntrinsic = Fn->IsTrustedAtomicIntrinsic ||
                                   isAtomicIntrinsicDeclaration(M, *Fn);
    if (isAtomicWrapperDeclaration(M, *Fn))
      TrustedAtomicWrapperDeclarations.insert(Fn.get());
    Fn->CodegenName = functionCodegenName(M, *Fn);
    SyntaxOrigin fnOrigin = M.IsInterface ? SyntaxOrigin::TKIImport : SyntaxOrigin::SourceSurface;
    auto fnRetTy = Fn->ReturnTypeSyntax ? toka::Type::fromSyntax(Fn->ReturnTypeSyntax) : toka::Type::fromString(Fn->ReturnType);
    if (containsInternalPlaceOutcome(fnRetTy))
      error(Fn.get(), DiagID::ERR_PLACE_OUTCOME_INTERNAL_ONLY,
            fnRetTy->toString());
    validateHandleGrammar(Fn->Loc, fnRetTy);
    recordHandleGrammarAudit(fnRetTy, fnOrigin, {FormationPhase::DirectResolution}, "", "", "return", Fn->Loc, false, functionCodegenName(M, *Fn));
    for (const auto &Arg : Fn->Args) {
      debugCheckBindingPermission(Arg);
      debugCheckBindingTypeString("function argument", Arg.Name, Arg.Type,
                                  Arg.Permission, Fn->Loc);
      auto argTy = Sema::synthesizePhysicalTypeObject(Arg, false);
      if (containsInternalPlaceOutcome(argTy))
        error(Fn.get(), DiagID::ERR_PLACE_OUTCOME_INTERNAL_ONLY,
              argTy->toString());
      validateHandleGrammar(Fn->Loc, argTy);
      recordHandleGrammarAudit(argTy, fnOrigin, {FormationPhase::DirectResolution}, "", "", Arg.Name, Fn->Loc, false, functionCodegenName(M, *Fn));
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
    // Foreign ABI results are never inferred.  Typed extern results remain
    // valid, while no-value ABI must say `-> void` explicitly.
    if (Ext->ReturnContract.ResultKind == ReturnResultKind::Unit) {
      DiagnosticEngine::report(getLoc(Ext.get()),
                               DiagID::ERR_EXTERN_RESULT_CONTRACT, Ext->Name);
      HasError = true;
    }
    SyntaxOrigin extOrigin = M.IsInterface ? SyntaxOrigin::TKIImport : SyntaxOrigin::SourceSurface;
    auto extRetTy = Ext->ReturnTypeSyntax ? toka::Type::fromSyntax(Ext->ReturnTypeSyntax) : toka::Type::fromString(Ext->ReturnType);
    if (containsInternalPlaceOutcome(extRetTy))
      error(Ext.get(), DiagID::ERR_PLACE_OUTCOME_INTERNAL_ONLY,
            extRetTy->toString());
    validateHandleGrammar(Ext->Loc, extRetTy);
    recordHandleGrammarAudit(extRetTy, extOrigin, {FormationPhase::DirectResolution}, "", "", "return", Ext->Loc, false, Ext->Name);
    for (auto &Arg : Ext->Args) {
      debugCheckBindingPermission(Arg);
      debugCheckBindingTypeString("extern argument", Arg.Name, Arg.Type,
                                  Arg.Permission, Ext->Loc);
      if (!Arg.ResolvedType) {
        Arg.ResolvedType =
            resolveType(Sema::synthesizePhysicalTypeObject(Arg, false));
      }
      if (containsInternalPlaceOutcome(Arg.ResolvedType))
        error(Ext.get(), DiagID::ERR_PLACE_OUTCOME_INTERNAL_ONLY,
              Arg.ResolvedType->toString());
      validateParameterHandleChain(Arg.Loc.isValid() ? Arg.Loc : Ext->Loc,
                                   Arg.Name, Arg.Permission, Arg.ResolvedType,
                                   Arg.TypeSyntax,
                                   Arg.HadRejectedTypeSideMorphology);
      recordHandleGrammarAudit(Arg.ResolvedType, extOrigin, {FormationPhase::DirectResolution}, "", "", Arg.Name, Ext->Loc, false, Ext->Name);
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
    if (St->Name == "__PlaceOutcome")
      error(St.get(), DiagID::ERR_PLACE_OUTCOME_INTERNAL_ONLY, St->Name);
    SyntaxOrigin shapeOrigin = M.IsInterface ? SyntaxOrigin::TKIImport : SyntaxOrigin::SourceSurface;
    for (const auto &mem : St->Members) {
      auto memTy = mem.TypeSyntax ? toka::Type::fromSyntax(mem.TypeSyntax) : toka::Type::fromString(mem.Type);
      if (containsInternalPlaceOutcome(memTy))
        error(St.get(), DiagID::ERR_PLACE_OUTCOME_INTERNAL_ONLY,
              memTy->toString());
      validateHandleGrammar(St->Loc, memTy);
      recordHandleGrammarAudit(memTy, shapeOrigin, {FormationPhase::DirectResolution}, St->Name, "", mem.Name, St->Loc);
    }
    if (St->IsCompilerSynthesized) {
      St->NominalId.reset();
    } else {
      St->NominalId = makeDeclaredShapeId(M, *St);
      if (!St->NominalId) {
        DiagnosticEngine::report(
            St->Loc, DiagID::ERR_NOMINAL_SHAPE_IDENTITY_MISSING, St->Name);
        HasError = true;
      } else {
        DeclaredShapeIdentityRecords.push_back({&M, St.get()});
      }
    }
    if (St->CodegenName.empty())
      St->CodegenName = St->Name;
    if (St->OwnerLinkName.empty()) {
      if (!M.IsTrustedSystemModule && !St->IsCompilerSynthesized &&
          St->NominalId) {
        St->OwnerLinkName = "__toka_owner_" + St->NominalId->mangled();
      } else {
        St->OwnerLinkName = St->CodegenName;
      }
    }
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
    if (Alias->Name == "__PlaceOutcome")
      error(Alias.get(), DiagID::ERR_PLACE_OUTCOME_INTERNAL_ONLY,
            Alias->Name);
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
    SyntaxOrigin aliasOrigin = M.IsInterface ? SyntaxOrigin::TKIImport : SyntaxOrigin::SourceSurface;
    auto aliasTypeObj = toka::Type::fromSyntax(targetSyntax);
    if (!aliasTypeObj)
      aliasTypeObj = toka::Type::fromString(target);
    validateAliasTarget(Alias->Loc, Alias->Name, targetSyntax, aliasTypeObj);
    if (containsInternalPlaceOutcome(aliasTypeObj))
      error(Alias.get(), DiagID::ERR_PLACE_OUTCOME_INTERNAL_ONLY,
            aliasTypeObj->toString());
    recordHandleGrammarAudit(aliasTypeObj, aliasOrigin, {FormationPhase::DirectResolution}, Alias->Name, "", "", Alias->Loc);
    SymbolInfo info;
    info.TypeObj = toka::Type::fromString(Alias->Name);
    info.IsTypeName = true;
    info.ASTPtr = Alias.get();
    ms.LexicalTypes[Alias->Name] = info;
  }
  // 5. Register Traits
  for (auto &Trait : M.Traits) {
    DeclarationLexicalScopes[Trait.get()] = &ms;
    if (Trait->Name == "PlaceIterator" && isTrustedCoreTraitsModule(M))
      m_CorePlaceIteratorTrait = Trait.get();
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
      SyntaxOrigin traitOrigin = M.IsInterface ? SyntaxOrigin::TKIImport : SyntaxOrigin::SourceSurface;
      auto methodRetTy = Method->ReturnTypeSyntax ? toka::Type::fromSyntax(Method->ReturnTypeSyntax) : toka::Type::fromString(Method->ReturnType);
      if (containsInternalPlaceOutcome(methodRetTy) &&
          !(Trait.get() == m_CorePlaceIteratorTrait &&
            Method->Name == "next_place"))
        error(Method.get(), DiagID::ERR_PLACE_OUTCOME_INTERNAL_ONLY,
              methodRetTy->toString());
      validateHandleGrammar(Method->Loc, methodRetTy);
      recordHandleGrammarAudit(methodRetTy, traitOrigin, {FormationPhase::DirectResolution}, Trait->Name, "", "return", Method->Loc, false, Method->Name);
      for (auto &Arg : Method->Args) {
        if (!Arg.ResolvedType) {
          Arg.ResolvedType = resolveType(Sema::synthesizePhysicalTypeObject(Arg, false));
        }
        if (containsInternalPlaceOutcome(Arg.ResolvedType))
          error(Method.get(), DiagID::ERR_PLACE_OUTCOME_INTERNAL_ONLY,
                Arg.ResolvedType->toString());
        validateHandleGrammar(Method->Loc, Arg.ResolvedType);
        recordHandleGrammarAudit(Arg.ResolvedType, traitOrigin, {FormationPhase::DirectResolution}, Trait->Name, "", Arg.Name, Method->Loc, false, Method->Name);
      }
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
      auto memTy = toka::Type::fromString(Sema::synthesizePhysicalType(Member));
      validateHandleGrammar(St->Loc, memTy);
      validateDynTraitObjectSafetyInType(memTy, getLoc(St.get()));
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
      validateHandleGrammar(getLoc(v), info.TypeObj);
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
    for (auto &Method : Impl->Methods) {
      DeclarationLexicalScopes[Method.get()] = &ms;
      if (isStdAtomicMethodDeclaration(M, *Impl, *Method))
        TrustedAtomicWrapperDeclarations.insert(Method.get());
    }
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
    for (auto &Method : Impl->Methods) {
      DeclarationLexicalScopes[Method.get()] = &ms;
      if (isStdAtomicMethodDeclaration(M, *Impl, *Method))
        TrustedAtomicWrapperDeclarations.insert(Method.get());
    }
  }

  // Case A: Register local symbols in the ModuleScope
  for (auto &Fn : M.Functions) {
    Fn->IsTrustedAtomicIntrinsic = Fn->IsTrustedAtomicIntrinsic ||
                                   isAtomicIntrinsicDeclaration(M, *Fn);
    if (isAtomicWrapperDeclaration(M, *Fn))
      TrustedAtomicWrapperDeclarations.insert(Fn.get());
    Fn->CodegenName = functionCodegenName(M, *Fn);
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
    auto aliasTypeObj = toka::Type::fromSyntax(targetSyntax);
    if (!aliasTypeObj)
      aliasTypeObj = toka::Type::fromString(target);
    validateAliasTarget(Alias->Loc, Alias->Name, targetSyntax, aliasTypeObj);

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
      auto memTy = toka::Type::fromString(Sema::synthesizePhysicalType(Member));
      validateHandleGrammar(St->Loc, memTy);
      validateDynTraitObjectSafetyInType(memTy, getLoc(St.get()));
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
      validateHandleGrammar(getLoc(v), globalInfo.TypeObj);
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
    auto globalType = v->TypeName.empty()
                          ? toka::Type::fromString("unknown")
                          : toka::Type::fromString(
                                synthesizePhysicalType(*v));
    if (containsInternalPlaceOutcome(globalType))
      error(v, DiagID::ERR_PLACE_OUTCOME_INTERNAL_ONLY,
            globalType->toString());
    debugCheckBindingTypeString("global variable", v->Name, v->TypeName,
                                v->Permission, v->Loc);

    auto symbol = CurrentScope->Symbols.find(v->Name);
    if (symbol != CurrentScope->Symbols.end()) {
      symbol->second.TypeObj = globalType;
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
  if (ShapeDecl *owner = resolveImplOwner(Impl))
    Impl->ResolvedOwner = owner;
  std::shared_ptr<toka::Type> resolvedSelfType;
  if (Impl->ResolvedOwner) {
    auto exactSelf = std::make_shared<ShapeType>(Impl->ResolvedOwner->Name);
    exactSelf->resolve(Impl->ResolvedOwner);
    resolvedSelfType = exactSelf;
  } else {
    resolvedSelfType =
        resolveType(Impl->HeaderSyntax.Type
                        ? toka::Type::fromSyntax(Impl->HeaderSyntax.Type)
                        : toka::Type::fromString(Impl->TypeName));
  }
  if (resolvedSelfType) {
    validateHandleGrammar(getLoc(Impl), resolvedSelfType);
  }
  auto resolvedSelfShape = std::dynamic_pointer_cast<ShapeType>(
      resolvedSelfType ? resolvedSelfType->getSoulType() : nullptr);
  if (!Impl->ResolvedOwner && resolvedSelfShape)
    Impl->ResolvedOwner = resolvedSelfShape->Decl;
  std::string resolvedTypeName =
      Impl->ResolvedOwner
          ? (Impl->ResolvedOwner->CodegenName.empty()
                 ? Impl->ResolvedOwner->Name
                 : Impl->ResolvedOwner->CodegenName)
          : resolvedSelfType ? resolvedSelfType->toString()
                             : resolveType(Impl->TypeName);
  TraitDecl *traitDecl =
      Impl->TraitName.empty()
          ? nullptr
          : Impl->Loc.isValid()
                ? findVisibleTraitDecl(Impl->TraitName, getLoc(Impl))
                : findTraitDecl(Impl->TraitName);
  std::string canonicalTrait = canonicalTraitName(Impl->TraitName, traitDecl);
  auto implOwner = DeclarationLexicalScopes.find(Impl);
  const bool qualifiedPlaceImpl =
      traitDecl && traitDecl == m_CorePlaceIteratorTrait &&
      implOwner != DeclarationLexicalScopes.end() && implOwner->second &&
      implOwner->second->IsTrustedSystemModule &&
      implOwner->second->ShadowCoordinateKnown &&
      implOwner->second->ShadowLogicalModulePath == "std/vec";
  if (qualifiedPlaceImpl) {
    for (const auto &method : Impl->Methods) {
      if (method->Name == "next_place")
        m_QualifiedPlaceIteratorProviders.insert(method.get());
    }
  }
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
    if (!Impl->TemplateOrigin)
      Method->ResolvedReturnType = nullptr;
    for (auto &Arg : Method->Args) {
      substituteSourceTypeSyntax(Arg.TypeSyntax, Arg.Type, "Self", selfTy);
      if (!Impl->TemplateOrigin)
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
  if (getTraitFamilyName(canonicalTrait) == "MutableBorrowIterator")
    requireSelfDependency("next_mut");
  if (getTraitFamilyName(canonicalTrait) == "PlaceIterator")
    requireSelfDependency("next_place");

  const std::string methodOwnerName =
      Impl->ResolvedOwner &&
              Impl->ResolvedOwner->OwnerLinkName.rfind("__toka_owner_", 0) == 0
          ? Impl->ResolvedOwner->OwnerLinkName
          : resolvedTypeName;
  std::set<std::string> implemented;
  for (auto &Method : Impl->Methods) {
    const bool isDropHook = getTraitFamilyName(canonicalTrait) == "Encap" &&
                            Method->Name == "drop";
    if (!Method->IsClosureInvoke) {
      Method->CodegenName =
          Impl->TraitName.empty()
              ? methodOwnerName + "_" + Method->Name
              : canonicalTrait + "_" + methodOwnerName + "_" + Method->Name;
    }
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
      ShapeDecl *owner = Impl->ResolvedOwner;
      if (!owner && ShapeMap.count(resolvedTypeName))
        owner = ShapeMap[resolvedTypeName];
      if (owner) {
        for (const auto &method : Impl->Methods) {
          if (method->Name == "drop") {
            owner->MangledDestructorName = method->CodegenName;
            break;
          }
        }
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
  auto declaredOwnerType =
      Impl->HeaderSyntax.Type
          ? toka::Type::fromSyntax(Impl->HeaderSyntax.Type)
          : toka::Type::fromString(Impl->TypeName);
  if (containsInternalPlaceOutcome(declaredOwnerType))
    error(Impl, DiagID::ERR_PLACE_OUTCOME_INTERNAL_ONLY,
          declaredOwnerType->toString());
  TraitDecl *declaredTrait = Impl->TraitName.empty()
                                 ? nullptr
                                 : findVisibleTraitDecl(Impl->TraitName,
                                                        getLoc(Impl));
  auto implOwner = DeclarationLexicalScopes.find(Impl);
  const bool isPlaceFacet =
      declaredTrait && declaredTrait == m_CorePlaceIteratorTrait &&
      implOwner != DeclarationLexicalScopes.end() && implOwner->second &&
      implOwner->second->IsTrustedSystemModule &&
      implOwner->second->ShadowCoordinateKnown &&
      implOwner->second->ShadowLogicalModulePath == "std/vec";
  for (auto &Method : Impl->Methods) {
    auto returnType = Method->ReturnTypeSyntax
                          ? toka::Type::fromSyntax(Method->ReturnTypeSyntax)
                          : toka::Type::fromString(Method->ReturnType);
    if (containsInternalPlaceOutcome(returnType) &&
        !(isPlaceFacet && Method->Name == "next_place"))
      error(Method.get(), DiagID::ERR_PLACE_OUTCOME_INTERNAL_ONLY,
            returnType->toString());
    for (const auto &Arg : Method->Args) {
      auto argumentType = Sema::synthesizePhysicalTypeObject(Arg, false);
      if (containsInternalPlaceOutcome(argumentType))
        error(Method.get(), DiagID::ERR_PLACE_OUTCOME_INTERNAL_ONLY,
              argumentType->toString());
    }
  }
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
  if (ShapeDecl *owner = resolveImplOwner(Impl))
    Impl->ResolvedOwner = owner;
  std::shared_ptr<toka::Type> resolvedSelfType;
  if (Impl->ResolvedOwner) {
    auto exactSelf = std::make_shared<ShapeType>(Impl->ResolvedOwner->Name);
    exactSelf->resolve(Impl->ResolvedOwner);
    resolvedSelfType = exactSelf;
  } else {
    resolvedSelfType =
        resolveType(Impl->HeaderSyntax.Type
                        ? toka::Type::fromSyntax(Impl->HeaderSyntax.Type)
                        : toka::Type::fromString(Impl->TypeName));
  }
  auto resolvedSelfShape = std::dynamic_pointer_cast<ShapeType>(
      resolvedSelfType ? resolvedSelfType->getSoulType() : nullptr);
  if (!Impl->ResolvedOwner && resolvedSelfShape)
    Impl->ResolvedOwner = resolvedSelfShape->Decl;
  std::string resolvedTypeName =
      Impl->ResolvedOwner
          ? (Impl->ResolvedOwner->CodegenName.empty()
                 ? Impl->ResolvedOwner->Name
                 : Impl->ResolvedOwner->CodegenName)
          : resolvedSelfType ? resolvedSelfType->toString()
                             : resolveType(Impl->TypeName);
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
  if (getTraitFamilyName(canonicalTrait) == "MutableBorrowIterator")
    requireSelfDependency("next_mut");
  if (getTraitFamilyName(canonicalTrait) == "PlaceIterator")
    requireSelfDependency("next_place");
  const std::string methodOwnerName =
      Impl->ResolvedOwner &&
              Impl->ResolvedOwner->OwnerLinkName.rfind("__toka_owner_", 0) == 0
          ? Impl->ResolvedOwner->OwnerLinkName
          : resolvedTypeName;
  for (auto &Method : Impl->Methods) {
    const bool isDropHook = getTraitFamilyName(canonicalTrait) == "Encap" &&
                            Method->Name == "drop";
    if (!Method->IsClosureInvoke) {
      Method->CodegenName =
          Impl->TraitName.empty()
              ? methodOwnerName + "_" + Method->Name
              : canonicalTrait + "_" + methodOwnerName + "_" + Method->Name;
    }
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
      ShapeDecl *owner = Impl->ResolvedOwner;
      if (!owner && ShapeMap.count(resolvedTypeName))
        owner = ShapeMap[resolvedTypeName];
      if (owner) {
        for (const auto &method : Impl->Methods) {
          if (method->Name == "drop") {
            owner->MangledDestructorName = method->CodegenName;
            break;
          }
        }
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

void Sema::validateGenericSignatureTypeNames(
    FunctionDecl *Fn, const std::vector<GenericParam> &enclosingParams,
    const std::set<std::string> &enclosingTypeNames) {
  if (!Fn)
    return;

  std::vector<GenericParam> allParams = enclosingParams;
  allParams.insert(allParams.end(), Fn->GenericParams.begin(),
                   Fn->GenericParams.end());
  std::set<std::string> parameterNames;
  std::map<std::string, std::set<std::string>> parameterBounds;
  for (const auto &parameter : allParams) {
    if (parameter.IsConst)
      continue;
    std::string name = parameter.Name;
    if (!name.empty() && name.front() == '\'')
      name.erase(0, 1);
    parameterNames.insert(name);
    for (const auto &bound : parameter.TraitBounds)
      parameterBounds[name].insert(getTraitFamilyName(bound));
  }
  parameterNames.insert(enclosingTypeNames.begin(), enclosingTypeNames.end());

  std::set<const Type *> visited;
  std::function<void(const std::shared_ptr<Type> &)> validateNames =
      [&](const std::shared_ptr<Type> &type) {
        if (!type || !visited.insert(type.get()).second)
          return;
        if (type->isPointer()) {
          validateNames(type->getPointeeType());
          return;
        }
        if (auto array = std::dynamic_pointer_cast<ArrayType>(type)) {
          validateNames(array->ElementType);
          return;
        }
        if (auto slice = std::dynamic_pointer_cast<SliceType>(type)) {
          validateNames(slice->ElementType);
          return;
        }
        if (auto uninit = std::dynamic_pointer_cast<UninitType>(type)) {
          validateNames(uninit->InnerType);
          return;
        }
        if (auto outcome = std::dynamic_pointer_cast<MissOutcomeType>(type)) {
          validateNames(outcome->PayloadType);
          return;
        }
        if (auto place = std::dynamic_pointer_cast<PlaceOutcomeType>(type)) {
          validateNames(place->ItemType);
          return;
        }
        if (auto function = std::dynamic_pointer_cast<FunctionType>(type)) {
          for (const auto &parameter : function->ParamTypes)
            validateNames(parameter);
          validateNames(function->ReturnType);
          return;
        }
        if (auto function = std::dynamic_pointer_cast<DynFnType>(type)) {
          for (const auto &parameter : function->ParamTypes)
            validateNames(parameter);
          validateNames(function->ReturnType);
          return;
        }
        auto shape = std::dynamic_pointer_cast<ShapeType>(type);
        if (!shape)
          return;

        if (!shape->Name.empty() && shape->Name.front() == '(' &&
            shape->Name.back() == ')') {
          if (shape->SourceSyntax && shape->SourceSyntax->NodeKind ==
                                         TypeSyntax::Kind::AnonymousRecord) {
            for (const auto &field : shape->SourceSyntax->Fields)
              validateNames(Type::fromSyntax(field.Type));
          }
          return;
        }

        std::string name = shape->Name;
        if (!name.empty() && name.front() == '\'')
          name.erase(0, 1);
        bool admitted = parameterNames.count(name) != 0;
        size_t projectionAt = findTopLevelChar(name, '@');
        size_t projectionScope =
            projectionAt == std::string::npos
                ? std::string::npos
                : findTopLevelDoubleColon(name, projectionAt + 1);
        if (!admitted && projectionAt != std::string::npos &&
            projectionScope != std::string::npos) {
          const std::string base = trimTypeString(name.substr(0, projectionAt));
          const std::string trait =
              getTraitFamilyName(trimTypeString(name.substr(
                  projectionAt + 1, projectionScope - projectionAt - 1)));
          admitted =
              parameterBounds.count(base) && parameterBounds[base].count(trait);
        }
        if (!admitted && !isTypeNameVisible(shape->Name, Fn->Loc)) {
          DiagnosticEngine::report(Fn->Loc, DiagID::ERR_UNDEFINED_TYPE,
                                   shape->Name);
          HasError = true;
        }

        ShapeDecl *genericDecl = shape->Decl;
        if (!genericDecl) {
          auto declaration = ShapeMap.find(shape->Name);
          if (declaration != ShapeMap.end())
            genericDecl = declaration->second;
        }
        for (size_t i = 0; i < shape->GenericArgs.size(); ++i) {
          if (genericDecl && i < genericDecl->GenericParams.size() &&
              genericDecl->GenericParams[i].IsConst)
            continue;
          validateNames(shape->GenericArgs[i]);
        }
      };

  validateNames(Fn->ReturnTypeSyntax ? Type::fromSyntax(Fn->ReturnTypeSyntax)
                                     : Type::fromString(Fn->ReturnType));
  for (const auto &argument : Fn->Args)
    validateNames(Sema::synthesizePhysicalTypeObject(argument, false));
}

void Sema::checkFunction(FunctionDecl *Fn) {
  // Generic templates do not execute a body check until instantiation, but
  // their signatures must still reject unknown names at the declaration
  // point without resolving or instantiating later CodeGen state.
  if (!Fn->GenericParams.empty()) {
    validateGenericSignatureTypeNames(Fn);
    return;
  }

  ActiveNodeRAII Active(Fn);

  std::string savedRet =
      CurrentFunctionReturnType; // [FIX] Save state for recursion
  FunctionDecl *savedFn = CurrentFunction;
  std::string savedBorrowSource = m_LastBorrowSource;
  auto savedLifeDependencies = m_LastLifeDependencies;
  auto savedFieldDependencies = m_LastFieldDependencies;
  auto savedOutcomePendingCalls = std::move(m_OutcomePendingCalls);
  m_OutcomePendingCalls.clear();
  CurrentFunction = Fn;
  CurrentFunctionReturnType = Fn->ReturnType;
  m_LastBorrowSource.clear();
  m_LastLifeDependencies.clear();
  m_LastFieldDependencies.clear();

  const ReturnResultKind resultKind = Fn->ReturnContract.ResultKind;
  if (resultKind == ReturnResultKind::AbiVoid) {
    DiagnosticEngine::report(getLoc(Fn),
                             DiagID::ERR_ORDINARY_VOID_RETURN_CONTRACT,
                             Fn->Name);
    HasError = true;
  } else if (resultKind == ReturnResultKind::Unit &&
             Fn->ReturnContract.HasExplicitResultType) {
    DiagnosticEngine::report(getLoc(Fn),
                             DiagID::ERR_EXPLICIT_UNIT_RETURN_CONTRACT,
                             Fn->Name);
    HasError = true;
  }
  if (resultKind == ReturnResultKind::Never &&
      (Fn->Effect != EffectKind::None || !Fn->ReturnContract.BindingName.empty() ||
       !Fn->ReturnContract.Routes.empty() || Fn->Name == "main")) {
    DiagnosticEngine::report(getLoc(Fn), DiagID::ERR_NEVER_RETURN_CONTRACT);
    HasError = true;
  }

  // Resolve ordinary Unit, ABI void, and bottom independently.  The legacy
  // string cache remains only a bridge into the existing semantic layers.
  if (resultKind != ReturnResultKind::AbiVoid) {
    validateTypeVisibilityInType(Fn->ReturnType, getLoc(Fn));
    validateDynTraitObjectSafetyInType(Fn->ReturnType, getLoc(Fn));
    Fn->ResolvedReturnType =
        resolveType(Fn->ResolvedReturnType
                        ? Fn->ResolvedReturnType
                        : Fn->ReturnTypeSyntax
                              ? toka::Type::fromSyntax(Fn->ReturnTypeSyntax)
                              : toka::Type::fromString(Fn->ReturnType));
  } else {
    Fn->ResolvedReturnType = toka::Type::fromString("void");
  }

  if (Fn->ResolvedReturnType) {
    validateHandleGrammar(getLoc(Fn), Fn->ResolvedReturnType);
    std::string fnId = !Fn->CodegenName.empty() ? Fn->CodegenName : Fn->Name;
    bool isGeneric = (Fn->TemplateOrigin != nullptr || (!Fn->CodegenName.empty() && Fn->CodegenName.find("_M_") != std::string::npos));
    SyntaxOrigin origin = (CurrentModule && CurrentModule->IsInterface) ? SyntaxOrigin::TKIImport : SyntaxOrigin::SourceSurface;
    recordHandleGrammarAudit(Fn->ResolvedReturnType, origin,
                             {isGeneric ? FormationPhase::GenericInstance : FormationPhase::DirectResolution},
                             Fn->Name, "", "return", getLoc(Fn), isGeneric, fnId);
  }

  Fn->ResolvedOutcomeTransition.reset();
  if (!Fn->OutcomeContract.empty()) {
    auto invalidOutcome = [&](const std::string &detail) {
      DiagnosticEngine::report(getLoc(Fn), DiagID::ERR_OUTCOME_CONTRACT_INVALID,
                               Fn->Name, detail);
      HasError = true;
    };

    ShapeDecl *returnEnum = nullptr;
    if (auto shape = std::dynamic_pointer_cast<ShapeType>(
            Fn->ResolvedReturnType)) {
      returnEnum = shape->Decl;
    }
    if (!returnEnum) {
      std::string base = Type::stripMorphology(Fn->ReturnType);
      const size_t generic = base.find('<');
      if (generic != std::string::npos)
        base = base.substr(0, generic);
      auto found = ShapeMap.find(base);
      if (found != ShapeMap.end())
        returnEnum = found->second;
    }
    if (!returnEnum || returnEnum->Kind != ShapeKind::Enum) {
      invalidOutcome("return type must be a direct nominal enum");
    } else {
      FunctionDecl::OutcomeTransition resolved;
      resolved.ReturnEnum = returnEnum;
      const FunctionDecl::Arg *outcomeFormal = nullptr;
      std::set<std::string> declaredVariants;
      bool valid = true;
      size_t initFormalCount = 0;
      for (const auto &arg : Fn->Args) {
        if (arg.IsInit)
          ++initFormalCount;
      }
      if (initFormalCount != 1) {
        invalidOutcome("the first slice requires exactly one init formal");
        valid = false;
      }
      for (const auto &member : returnEnum->Members)
        declaredVariants.insert(member.Name);
      if (declaredVariants.empty()) {
        invalidOutcome("return enum has no direct variants");
        valid = false;
      }

      std::set<std::string> coveredVariants;
      for (const auto &transition : Fn->OutcomeContract.Transitions) {
        const FunctionDecl::Arg *subject = nullptr;
        size_t subjectIndex = 0;
        for (size_t index = 0; index < Fn->Args.size(); ++index) {
          if (Fn->Args[index].Name == transition.Subject) {
            subject = &Fn->Args[index];
            subjectIndex = index;
            break;
          }
        }
        if (!subject || !subject->IsInit) {
          invalidOutcome("each entry must name the same init formal");
          valid = false;
          continue;
        }
        if (outcomeFormal && outcomeFormal != subject) {
          invalidOutcome("only one outcome-governed init formal is allowed");
          valid = false;
        }
        outcomeFormal = subject;
        const ShapeMember *variant = nullptr;
        size_t variantOrdinal = 0;
        for (size_t index = 0; index < returnEnum->Members.size(); ++index) {
          if (returnEnum->Members[index].Name == transition.Variant) {
            variant = &returnEnum->Members[index];
            variantOrdinal = index;
            break;
          }
        }
        if (!variant) {
          invalidOutcome("entry names a variant absent from the return enum");
          valid = false;
          continue;
        }
        if (!coveredVariants.insert(transition.Variant).second) {
          invalidOutcome("each return variant may appear only once");
          valid = false;
          continue;
        }
        if (resolved.Subject && resolved.Subject != subject) {
          valid = false;
          continue;
        }
        resolved.Subject = subject;
        resolved.SubjectIndex = subjectIndex;
        resolved.Cases.push_back(
            {variant, variantOrdinal, {}, transition.Post});
      }
      if (!outcomeFormal) {
        invalidOutcome("entries must name one init formal");
        valid = false;
      }
      if (coveredVariants != declaredVariants) {
        invalidOutcome("entries must cover every direct return variant exactly once");
        valid = false;
      }
      if (valid)
        Fn->ResolvedOutcomeTransition = std::move(resolved);
    }
  }

  if (Fn->Name == "main" && Fn->ResolvedReturnType) {
    auto mainRet = resolveType(Fn->ResolvedReturnType, false);
    std::string mainRetName = mainRet ? mainRet->getSoulName() : "unknown";
    if (mainRetName != "i32" && !Fn->ResolvedReturnType->isUnit()) {
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

    SymbolInfo Info;
    // Preserve full generic-substituted Arg.Type values like "&i32".
    // [Fix] Preserve pre-resolved Types (e.g. Synthetic Closures)
    bool paramOk = true;
    if (Arg.ResolvedType) {
      Info.TypeObj = resolveType(Arg.ResolvedType);
      Arg.ResolvedType = Info.TypeObj;
    } else {
      Info.TypeObj =
          resolveType(Sema::synthesizePhysicalTypeObject(Arg, false));

      // Assign to AST Node for CodeGen
      Arg.ResolvedType = Info.TypeObj;
    }

    if (Arg.ResolvedType) {
      paramOk = validateParameterHandleChain(
          argLoc, Arg.Name, Arg.Permission, Arg.ResolvedType, Arg.TypeSyntax,
          Arg.HadRejectedTypeSideMorphology);
      std::string fnId = !Fn->CodegenName.empty() ? Fn->CodegenName : Fn->Name;
      bool isGeneric = (Fn->TemplateOrigin != nullptr || (!Fn->CodegenName.empty() && Fn->CodegenName.find("_M_") != std::string::npos));
      SyntaxOrigin origin = (CurrentModule && CurrentModule->IsInterface) ? SyntaxOrigin::TKIImport : SyntaxOrigin::SourceSurface;
      recordHandleGrammarAudit(Arg.ResolvedType, origin,
                               {isGeneric ? FormationPhase::GenericInstance : FormationPhase::DirectResolution},
                               Fn->Name, "", Arg.Name, argLoc, isGeneric, fnId);
    }

    if (paramOk && Arg.IsReference && !Arg.IsRebindable &&
        Type::stripMorphology(Arg.Name) != "self") {
      if (!Arg.ResolvedType || !Arg.ResolvedType->isReference() ||
          !Arg.ResolvedType->getPointeeType() ||
          !Arg.ResolvedType->getPointeeType()->isRawPointer()) {
        DiagnosticEngine::report(argLoc, DiagID::ERR_REDUNDANT_PARAM_BORROW);
        HasError = true;
      }
    }

    if (Arg.DefaultValue && Info.TypeObj && Info.TypeObj->isMissOutcome()) {
      DiagnosticEngine::report(argLoc,
                               DiagID::ERR_MISS_OUTCOME_DEFAULT_FORBIDDEN,
                               Info.TypeObj->toString());
      HasError = true;
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
    if (Arg.IsInit) {
      const bool isPlainValue = !Arg.IsCeded && !Arg.IsRawPointer &&
                                !Arg.IsUnique && !Arg.IsShared &&
                                !Arg.IsReference && !Arg.IsRebindable &&
                                !Arg.IsValueMutable &&
                                !Arg.IsPointerNullable &&
                                !Arg.IsValueBlocked && Arg.Name != "self" &&
                                Fn->Effect == EffectKind::None &&
                                Fn->ReturnContract.ResultKind !=
                                    ReturnResultKind::Never;
      if (!isPlainValue) {
        DiagnosticEngine::report(argLoc, DiagID::ERR_INIT_PARAMETER_INVALID,
                                 Arg.Name);
        HasError = true;
      }
      // An init formal aliases caller storage.  It begins unavailable to
      // ordinary reads and becomes Live only through `init param = ...`.
      Info.InitMask = 0;
      Info.ExactPlace.setWhole(PlaceState::Never);
      // The contract itself consumes the formal's place authority; this also
      // keeps bodyless TKI declarations from reporting a spurious unused
      // value warning for a parameter that cannot be read.
      Info.HasBeenUsed = true;
    }
    if (Info.TypeObj && (Info.TypeObj->isFunction() || Info.TypeObj->isDynFn()))
      Info.CallableReceiver = getCallableReceiverMode(*Info.TypeObj);


    if (!Arg.Type.empty() && Arg.Type[0] == '\'') {
      Info.IsMorphicExempt = true;
    }
    CurrentScope->define(Arg.Name, Info);

  }

  populateOutcomeTransitionIdentities(Fn);

  // --- Sema: Safety Redline Boundaries ---
  checkUnsafePublicFunctionBoundary(Fn);

  if (Fn->Body) {
    checkStmt(Fn->Body.get());

    // Explicit returns check their own path at the return expression.  Only
    // a reachable normal fallthrough remains to be discharged here.
    if (!allPathsJump(Fn->Body.get())) {
      if (Fn->ResolvedOutcomeTransition) {
        DiagnosticEngine::report(
            getLoc(Fn), DiagID::ERR_OUTCOME_CONTRACT_INVALID, Fn->Name,
            "function can fall through without selecting an outcome variant");
        HasError = true;
      }
      for (auto &Arg : Fn->Args) {
        if (!Arg.IsInit)
          continue;
        SymbolInfo *Info = nullptr;
        SourceLocation argLoc = Arg.Loc.isValid() ? Arg.Loc : getLoc(Fn);
        if (CurrentScope->findSymbol(Arg.Name, Info) && Info &&
            !hasExactlyPlaceState(Info->placeFact(), PlaceState::Live)) {
          DiagnosticEngine::report(
              argLoc, DiagID::ERR_INIT_PARAMETER_UNFULFILLED, Fn->Name,
              Arg.Name);
          HasError = true;
        }
      }
    }

    for (CallExpr *call : m_OutcomePendingCalls) {
      if (!call || call->OutcomeMatchConsumed)
        continue;
      DiagnosticEngine::report(getLoc(call), DiagID::ERR_OUTCOME_MATCH_REQUIRED,
                               call->Callee);
      HasError = true;
    }

    for (auto &Arg : Fn->Args) {
      if (!Arg.IsCeded)
        continue;

      SymbolInfo *Info = nullptr;
      SourceLocation argLoc = Arg.Loc.isValid() ? Arg.Loc : getLoc(Fn);
      if (CurrentScope->findSymbol(Arg.Name, Info) && Info &&
          !hasPlaceState(Info->placeFact(), PlaceState::Moved)) {
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
      } else if (Info &&
                 hasPlaceState(Info->placeFact(), PlaceState::Moved)) {
        SemanticEvidence::recordCedeObligation(
            CedeObligationStage::CalleeConsumption,
            CedeObligationStatus::Fulfilled, SemanticReason::CedeConsumed,
            Arg.Name, Fn->Name,
            Info->MoveLoc.isValid() ? Info->MoveLoc : argLoc, argLoc);
      }
    }

    if (resultKind == ReturnResultKind::Never) {
      if (!allPathsJump(Fn->Body.get())) {
        DiagnosticEngine::report(getLoc(Fn),
                                 DiagID::ERR_NEVER_FUNCTION_COMPLETES,
                                 Fn->Name);
        HasError = true;
      }
    } else if (resultKind == ReturnResultKind::Typed) {
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
  m_OutcomePendingCalls = std::move(savedOutcomePendingCalls);
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
      if (Impl->ResolvedOwner) {
        resolvedTypeName = Impl->ResolvedOwner->CodegenName.empty()
                               ? Impl->ResolvedOwner->Name
                               : Impl->ResolvedOwner->CodegenName;
      }
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
  if (Impl->ResolvedOwner) {
    auto exactSelf = std::make_shared<ShapeType>(Impl->ResolvedOwner->Name);
    exactSelf->resolve(Impl->ResolvedOwner);
    SelfType = exactSelf;
  } else {
    SelfType = resolveType(toka::Type::fromString(Impl->TypeName));
  }

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
  std::map<NominalShapeId, const ShapeDecl *> owners;
  std::map<const ShapeDecl *, const Module *> declarationOwners;
  std::set<const ShapeDecl *> visited;
  auto reportViolation = [&](SourceLocation loc, const std::string &message) {
    DiagnosticEngine::report(loc, DiagID::ERR_SHAPE_SOVEREIGNTY_VIOLATION,
                             message);
    HasError = true;
  };
  for (const auto &record : DeclaredShapeIdentityRecords) {
    if (!record.Owner || !record.Decl) {
      reportViolation(SourceLocation{},
                      "Invalid declared shape identity record");
      continue;
    }

    const auto member = std::find_if(
        record.Owner->Shapes.begin(), record.Owner->Shapes.end(),
        [&](const std::unique_ptr<ShapeDecl> &candidate) {
          return candidate.get() == record.Decl;
        });
    if (member == record.Owner->Shapes.end()) {
      reportViolation(
          record.Owner->Loc,
          "A declared shape identity no longer belongs to its module");
      continue;
    }

    auto [ownerIt, ownerInserted] =
        declarationOwners.emplace(record.Decl, record.Owner);
    if (!ownerInserted && ownerIt->second != record.Owner) {
      reportViolation(record.Decl->Loc,
                      "A shape declaration is owned by more than one module");
      continue;
    }
    if (!visited.insert(record.Decl).second)
      continue;

    const ShapeDecl *shape = record.Decl;
    const auto expected = makeDeclaredShapeId(*record.Owner, *shape);
    std::string violation;
    if (!shape->NominalId) {
      violation = "Shape '" + shape->Name +
                  "' is missing its declared nominal identity";
    } else if (!expected || *shape->NominalId != *expected) {
      violation = "Shape '" + shape->Name +
                  "' changed nominal identity after declaration";
    } else if (!DeclarationLexicalScopes.count(shape)) {
      violation = "Shape '" + shape->Name +
                  "' has no declaration lexical scope";
    } else {
      ModuleScope *ownerScope = nullptr;
      if (!record.Owner->ResolvedPath.empty())
        ownerScope = getModule(record.Owner->ResolvedPath);
      if (!ownerScope && !record.Owner->SourcePath.empty())
        ownerScope = getModule(record.Owner->SourcePath);
      if (!ownerScope && record.Owner->Loc.isValid())
        ownerScope = getLexicalModule(record.Owner->Loc);
      if (!ownerScope || DeclarationLexicalScopes.at(shape) != ownerScope) {
        violation = "Shape '" + shape->Name +
                    "' has a declaration scope outside its owning module";
      }
    }

    if (violation.empty()) {
      auto [it, inserted] = owners.emplace(*shape->NominalId, shape);
      if (!inserted && it->second != shape) {
        violation = "Distinct shape declarations share nominal identity '" +
                    shape->NominalId->canonical() + "'";
      }
    }

    if (!violation.empty()) {
      reportViolation(shape->Loc, violation);
    }
  }

  auto registeredShape = [](ModuleScope *scope,
                            const SymbolInfo &symbol) -> ShapeDecl * {
    auto find = [&](ModuleScope *candidate) -> ShapeDecl * {
      if (!candidate || !symbol.ASTPtr)
        return nullptr;
      for (const auto &[_, shape] : candidate->Shapes) {
        if (shape == symbol.ASTPtr)
          return shape;
      }
      return nullptr;
    };
    if (ShapeDecl *shape = find(scope))
      return shape;
    return find(static_cast<ModuleScope *>(symbol.ReferencedModule));
  };

  auto visibleShape = [&](ModuleScope *scope,
                          const std::string &name) -> ShapeDecl * {
    if (!scope)
      return nullptr;
    ModuleScope *target = scope;
    std::string localName = name;
    const size_t separator = name.find("::");
    if (separator != std::string::npos) {
      auto module = scope->LexicalSymbols.find(name.substr(0, separator));
      if (module == scope->LexicalSymbols.end() ||
          !module->second.ReferencedModule)
        return nullptr;
      target = static_cast<ModuleScope *>(module->second.ReferencedModule);
      localName = name.substr(separator + 2);
    }

    auto symbol = target->LexicalTypes.find(localName);
    if (symbol == target->LexicalTypes.end() ||
        !symbol->second.IsTypeName || symbol->second.IsTraitName)
      return nullptr;
    return registeredShape(target, symbol->second);
  };

  std::set<const ShapeDecl *> declaredShapes;
  for (const auto &record : DeclaredShapeIdentityRecords) {
    if (record.Decl)
      declaredShapes.insert(record.Decl);
  }

  using AuditedTypeKey =
      std::tuple<const Type *, const TypeSyntax *, const ModuleScope *>;
  std::set<AuditedTypeKey> auditedTypes;
  std::set<std::pair<const ShapeDecl *, const ModuleScope *>>
      auditedSyntheticShapes;
  std::function<void(const ShapeDecl *, ModuleScope *, SourceLocation)>
      auditSyntheticShape;
  std::function<void(const std::shared_ptr<Type> &, const TypeSyntaxPtr &,
                     ModuleScope *, SourceLocation)>
      auditType;
  auditType = [&](const std::shared_ptr<Type> &type,
                  const TypeSyntaxPtr &syntax, ModuleScope *scope,
                  SourceLocation loc) {
    if (!type)
      return;
    if (!auditedTypes.emplace(type.get(), syntax.get(), scope).second)
      return;

    TypeSyntaxPtr soulSyntax = syntax;
    while (soulSyntax &&
           soulSyntax->NodeKind == TypeSyntax::Kind::Morphology)
      soulSyntax = soulSyntax->Subject;

    if (auto pointer = std::dynamic_pointer_cast<PointerType>(type)) {
      auditType(pointer->PointeeType, soulSyntax, scope, loc);
      return;
    }
    if (auto array = std::dynamic_pointer_cast<ArrayType>(type)) {
      TypeSyntaxPtr element =
          soulSyntax && soulSyntax->NodeKind == TypeSyntax::Kind::Array
              ? soulSyntax->Subject
              : nullptr;
      auditType(array->ElementType, element, scope, loc);
      return;
    }
    if (auto slice = std::dynamic_pointer_cast<SliceType>(type)) {
      TypeSyntaxPtr element =
          soulSyntax && soulSyntax->NodeKind == TypeSyntax::Kind::Slice
              ? soulSyntax->Subject
              : nullptr;
      auditType(slice->ElementType, element, scope, loc);
      return;
    }
    if (auto uninit = std::dynamic_pointer_cast<UninitType>(type)) {
      TypeSyntaxPtr inner;
      if (soulSyntax &&
          soulSyntax->NodeKind == TypeSyntax::Kind::GenericApplication &&
          !soulSyntax->Arguments.empty() &&
          soulSyntax->Arguments[0].ArgumentKind ==
              TypeArgumentSyntax::Kind::Type)
        inner = soulSyntax->Arguments[0].Type;
      auditType(uninit->InnerType, inner, scope, loc);
      return;
    }
    if (auto function = std::dynamic_pointer_cast<FunctionType>(type)) {
      for (size_t i = 0; i < function->ParamTypes.size(); ++i) {
        TypeSyntaxPtr parameter;
        if (soulSyntax &&
            soulSyntax->NodeKind == TypeSyntax::Kind::Function &&
            i < soulSyntax->Elements.size())
          parameter = soulSyntax->Elements[i];
        auditType(function->ParamTypes[i], parameter, scope, loc);
      }
      TypeSyntaxPtr result =
          soulSyntax && soulSyntax->NodeKind == TypeSyntax::Kind::Function
              ? soulSyntax->Result
              : nullptr;
      auditType(function->ReturnType, result, scope, loc);
      return;
    }
    if (auto function = std::dynamic_pointer_cast<DynFnType>(type)) {
      for (size_t i = 0; i < function->ParamTypes.size(); ++i) {
        TypeSyntaxPtr parameter;
        if (soulSyntax &&
            soulSyntax->NodeKind == TypeSyntax::Kind::Function &&
            i < soulSyntax->Elements.size())
          parameter = soulSyntax->Elements[i];
        auditType(function->ParamTypes[i], parameter, scope, loc);
      }
      TypeSyntaxPtr result =
          soulSyntax && soulSyntax->NodeKind == TypeSyntax::Kind::Function
              ? soulSyntax->Result
              : nullptr;
      auditType(function->ReturnType, result, scope, loc);
      return;
    }

    auto shapeType = std::dynamic_pointer_cast<ShapeType>(type);
    if (!shapeType)
      return;
    for (size_t i = 0; i < shapeType->GenericArgs.size(); ++i) {
      TypeSyntaxPtr argumentSyntax;
      if (soulSyntax &&
          soulSyntax->NodeKind == TypeSyntax::Kind::GenericApplication &&
          i < soulSyntax->Arguments.size() &&
          soulSyntax->Arguments[i].ArgumentKind ==
              TypeArgumentSyntax::Kind::Type)
        argumentSyntax = soulSyntax->Arguments[i].Type;
      auditType(shapeType->GenericArgs[i], argumentSyntax, scope, loc);
    }

    std::string surfaceName;
    if (soulSyntax && soulSyntax->NodeKind == TypeSyntax::Kind::Named) {
      surfaceName = soulSyntax->Text;
    } else if (soulSyntax &&
               soulSyntax->NodeKind == TypeSyntax::Kind::GenericApplication &&
               soulSyntax->Subject &&
               soulSyntax->Subject->NodeKind == TypeSyntax::Kind::Named) {
      surfaceName = soulSyntax->Subject->Text;
    }

    if (!shapeType->Decl) {
      if (!surfaceName.empty() && visibleShape(scope, surfaceName)) {
        reportViolation(loc, "Shape type '" + surfaceName +
                                 "' lost its nominal declaration binding");
      }
      return;
    }
    if (shapeType->Decl->IsCompilerSynthesized) {
      auditSyntheticShape(shapeType->Decl, scope, loc);
      return;
    }
    if (!declaredShapes.count(shapeType->Decl) ||
        !shapeType->Decl->NominalId) {
      reportViolation(loc, "Shape type '" + shapeType->Name +
                               "' refers to an unregistered nominal "
                               "declaration");
      return;
    }

    ShapeDecl *expected =
        surfaceName.empty() ? nullptr : visibleShape(scope, surfaceName);
    if (expected && expected != shapeType->Decl) {
      reportViolation(loc, "Shape type '" + surfaceName +
                               "' is bound to the wrong nominal declaration");
    }
  };

  std::function<void(const ShapeMember &, ModuleScope *, SourceLocation)>
      auditMember;
  auditMember = [&](const ShapeMember &member, ModuleScope *scope,
                    SourceLocation fallbackLoc) {
    const SourceLocation memberLoc =
        member.Loc.isValid() ? member.Loc : fallbackLoc;
    if (member.TypeSyntax && !member.ResolvedType) {
      reportViolation(memberLoc,
                      "A concrete shape member has no resolved semantic type");
    } else {
      auditType(member.ResolvedType, member.TypeSyntax, scope, memberLoc);
    }
    for (const auto &subMember : member.SubMembers)
      auditMember(subMember, scope, memberLoc);
  };

  auditSyntheticShape = [&](const ShapeDecl *shape, ModuleScope *scope,
                            SourceLocation loc) {
    if (!shape || !auditedSyntheticShapes.emplace(shape, scope).second)
      return;
    for (const auto &member : shape->Members)
      auditMember(member, scope, loc);
  };

  std::set<const ShapeDecl *> auditedShapes;
  for (const auto &record : DeclaredShapeIdentityRecords) {
    if (!record.Decl || !auditedShapes.insert(record.Decl).second ||
        record.Decl->IsCompilerSynthesized ||
        !record.Decl->GenericParams.empty())
      continue;
    auto scope = DeclarationLexicalScopes.find(record.Decl);
    if (scope == DeclarationLexicalScopes.end())
      continue;
    for (const auto &member : record.Decl->Members)
      auditMember(member, scope->second, record.Decl->Loc);
  }

  for (const Module *module : DeclaredModules) {
    if (!module)
      continue;
    ModuleScope *ownerScope = nullptr;
    if (!module->ResolvedPath.empty())
      ownerScope = getModule(module->ResolvedPath);
    if (!ownerScope && !module->SourcePath.empty())
      ownerScope = getModule(module->SourcePath);
    if (!ownerScope && module->Loc.isValid())
      ownerScope = getLexicalModule(module->Loc);
    for (const auto &shape : module->Shapes) {
      if (!shape->IsCompilerSynthesized)
        continue;
      auto lexicalScope = DeclarationLexicalScopes.find(shape.get());
      auditSyntheticShape(
          shape.get(), lexicalScope == DeclarationLexicalScopes.end()
                           ? ownerScope
                           : lexicalScope->second,
          shape->Loc);
    }
  }

  if (GenericInstancesModule) {
    for (const auto &shape : GenericInstancesModule->Shapes) {
      if (!shape->IsCompilerSynthesized)
        continue;
      auto lexicalScope = DeclarationLexicalScopes.find(shape.get());
      auditSyntheticShape(
          shape.get(), lexicalScope == DeclarationLexicalScopes.end()
                           ? nullptr
                           : lexicalScope->second,
          shape->Loc);
    }
  }
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
        if (m.ResolvedType) {
          SyntaxOrigin origin = (CurrentModule && CurrentModule->IsInterface) ? SyntaxOrigin::TKIImport : SyntaxOrigin::SourceSurface;
          recordHandleGrammarAudit(m.ResolvedType, origin,
                                   {FormationPhase::DirectResolution},
                                   S->Name, "", m.Name, m.Loc.isValid() ? m.Loc : getLoc(S.get()));
        }

        if (m.DefaultValue && m.ResolvedType &&
            m.ResolvedType->isMissOutcome()) {
          DiagnosticEngine::report(
              m.Loc.isValid() ? m.Loc : getLoc(S.get()),
              DiagID::ERR_MISS_OUTCOME_DEFAULT_FORBIDDEN,
              m.ResolvedType->toString());
          HasError = true;
        }

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

      // A unit enum variant has an explicit semantic payload state, not an
      // empty spelling that should be lowered as a type.
      if (!(S->Kind == ShapeKind::Enum && member.IsUnitVariant))
        resolveShapeMemberType(member);
      
      // Resolve SubMembers (mostly payloads for Enum variants)
      for (auto &subMemb : member.SubMembers) {
          resolveShapeMemberType(subMemb);
      }

      // 5. Basic Validation (Optional but good)
      if (member.ResolvedType && member.ResolvedType->isUnknown()) {
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
  std::string resolvedName = resolveType(shapeName);
  if (!S && ShapeMap.count(resolvedName))
    S = ShapeMap[resolvedName];
  if (!S) {
    for (auto &sh : M.Shapes)
      if (sh->Name == shapeName || sh->Name == resolvedName) {
        S = sh.get();
        break;
      }
  }
  if (!S) {
    std::string suffix = "::" + shapeName;
    for (auto &pair : ShapeMap) {
      if (pair.first.length() >= suffix.length() &&
          pair.first.compare(pair.first.length() - suffix.length(), suffix.length(), suffix) == 0) {
        S = pair.second;
        break;
      }
    }
  }
  if (!S) {
    size_t instancePos = shapeName.find("_M_");
    if (instancePos == std::string::npos)
      instancePos = shapeName.find('<');
    if (instancePos != std::string::npos) {
      std::string baseName = shapeName.substr(0, instancePos);
      std::string resolvedBase = resolveType(baseName);
      if (ShapeMap.count(baseName))
        S = ShapeMap[baseName];
      else if (ShapeMap.count(resolvedBase))
        S = ShapeMap[resolvedBase];
      if (!S) {
        for (auto &sh : M.Shapes)
          if (sh->Name == baseName || sh->Name == resolvedBase) {
            S = sh.get();
            break;
          }
      }
      if (!S) {
        std::string suffix = "::" + baseName;
        for (auto &pair : ShapeMap) {
          if (pair.first.length() >= suffix.length() &&
              pair.first.compare(pair.first.length() - suffix.length(), suffix.length(), suffix) == 0) {
            S = pair.second;
            break;
          }
        }
      }
    }
  }

  if (S) {
    for (auto &member : S->Members) {
      std::string typeStr = member.Type;
      if (member.IsRawPointer) {
        props.HasRawPtr = true;
      }

      // [NEW] Trait auto-derivation
      if (typeStr.size() > 0 && typeStr.front() == '\'') {
        // Generic type parameter - satisfied by bounds at instantiation
      } else if (member.IsRawPointer) {
        props.IsSend = false;
        props.IsSync = false;
      } else {
        auto memberTypeObj = toka::Type::fromString(typeStr);
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
      if (auto outcome = std::dynamic_pointer_cast<MissOutcomeType>(
              member.ResolvedType)) {
        if (outcome->PayloadType &&
            outcome->PayloadType->requiresExplicitOwnershipTransfer(this))
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

    auto typeId = authorityConcreteTypeId(Ty);
    if (!typeId) {
      auto fallback = SemanticIdentityBuilder::concreteType(
          "legacy-policy", Ty->canonicalIdentity());
      if (fallback) typeId = fallback.value();
    }
    if (!typeId) return false;
    std::string soul = Type::stripMorphology(resolved);
    if (size_t scope = soul.rfind("::"); scope != std::string::npos)
      soul = soul.substr(scope + 2);
    const bool borrowed =
        soul == "str" || soul == "bytes" || soul == "cstr" ||
        soul == "ViewStrSplitIterator" || soul == "ViewStrLinesIterator";
    RawLegacyCedePolicyInput raw(
        *typeId, soul,
        borrowed ? LegacyPolicyTypeCategory::BorrowedView
                 : LegacyPolicyTypeCategory::Shape,
        LegacyPolicyDropFact::Indeterminate);
    auto requirement = classifyLegacyCedeRequirement(raw);
    if (requirement != LegacyCedeRequirement::Indeterminate)
      return requirement == LegacyCedeRequirement::ImplicitExempt;

    raw = RawLegacyCedePolicyInput(
        *typeId, soul, LegacyPolicyTypeCategory::Shape,
        hasDrop(sName) || hasDrop(resolved) ? LegacyPolicyDropFact::HasDrop
                                            : LegacyPolicyDropFact::NoDrop);
    requirement = classifyLegacyCedeRequirement(raw);
    return requirement == LegacyCedeRequirement::ImplicitExempt;
  }

  return false;
}

bool Sema::canPreserveBareSignatureCede(
    std::shared_ptr<toka::Type> Ty) {
  return m_EnableSignatureDrivenCallCede && Ty &&
         Ty->valueOwnership(this) == ValueOwnership::BorrowedView;
}

bool Sema::consumeMissingCallTransferFault(ASTNode *node) {
  if (!m_InjectMissingCallTransferElaboration ||
      m_MissingCallTransferFaultConsumed || !node || !node->Loc.isValid() ||
      !DiagnosticEngine::SrcMgr)
    return false;
  const auto location = DiagnosticEngine::SrcMgr->getFullSourceLoc(node->Loc);
  if (!location.isValid() ||
      std::string(location.FileName).find("codegen_missing_call_transfer.tk") ==
          std::string::npos)
    return false;
  m_MissingCallTransferFaultConsumed = true;
  return true;
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
  std::string clean = toka::Type::stripMorphology(shapeName);
  if (ImplMap.count(clean + "@Send")) {
    return true;
  }
  std::string resolved = resolveType(clean);
  std::string cleanResolved = toka::Type::stripMorphology(resolved);
  if (ImplMap.count(cleanResolved + "@Send")) {
    return true;
  }

  std::string sendSuffix = "::" + clean + "@Send";
  std::string resolvedSendSuffix = "::" + cleanResolved + "@Send";
  for (const auto &pair : ImplMap) {
    if ((pair.first.length() >= sendSuffix.length() &&
         pair.first.compare(pair.first.length() - sendSuffix.length(), sendSuffix.length(), sendSuffix) == 0) ||
        (pair.first.length() >= resolvedSendSuffix.length() &&
         pair.first.compare(pair.first.length() - resolvedSendSuffix.length(), resolvedSendSuffix.length(), resolvedSendSuffix) == 0)) {
      return true;
    }
  }

  if ((!m_ShapeProps.count(clean) || m_ShapeProps[clean].Status != ShapeAnalysisStatus::Analyzed) && CurrentModule) {
    computeShapeProperties(clean, *CurrentModule);
  }
  if ((!m_ShapeProps.count(cleanResolved) || m_ShapeProps[cleanResolved].Status != ShapeAnalysisStatus::Analyzed) && CurrentModule) {
    computeShapeProperties(cleanResolved, *CurrentModule);
  }

  bool res = true;
  if (m_ShapeProps.count(clean))
    res = m_ShapeProps[clean].IsSend;
  else if (m_ShapeProps.count(cleanResolved))
    res = m_ShapeProps[cleanResolved].IsSend;
  return res;
}

bool Sema::isShapeSync(const std::string &shapeName) {
  std::string clean = toka::Type::stripMorphology(shapeName);
  if (ImplMap.count(clean + "@Sync") || ImplMap.count(clean + "@sync")) return true;
  std::string resolved = resolveType(clean);
  std::string cleanResolved = toka::Type::stripMorphology(resolved);
  if (ImplMap.count(cleanResolved + "@Sync") || ImplMap.count(cleanResolved + "@sync")) return true;

  std::string syncSuffix = "::" + clean + "@Sync";
  std::string resolvedSyncSuffix = "::" + cleanResolved + "@Sync";
  for (const auto &pair : ImplMap) {
    if ((pair.first.length() >= syncSuffix.length() &&
         pair.first.compare(pair.first.length() - syncSuffix.length(), syncSuffix.length(), syncSuffix) == 0) ||
        (pair.first.length() >= resolvedSyncSuffix.length() &&
         pair.first.compare(pair.first.length() - resolvedSyncSuffix.length(), resolvedSyncSuffix.length(), resolvedSyncSuffix) == 0)) {
      return true;
    }
  }

  if ((!m_ShapeProps.count(clean) || m_ShapeProps[clean].Status != ShapeAnalysisStatus::Analyzed) && CurrentModule) {
    computeShapeProperties(clean, *CurrentModule);
  }
  if ((!m_ShapeProps.count(cleanResolved) || m_ShapeProps[cleanResolved].Status != ShapeAnalysisStatus::Analyzed) && CurrentModule) {
    computeShapeProperties(cleanResolved, *CurrentModule);
  }
  if (m_ShapeProps.count(clean))
    return m_ShapeProps[clean].IsSync;
  if (m_ShapeProps.count(cleanResolved))
    return m_ShapeProps[cleanResolved].IsSync;
  return true;
}

Sema::GenericFunctionInstantiationResult Sema::instantiateGenericFunction(
    FunctionDecl *Template,
    const std::vector<std::shared_ptr<toka::Type>> &Args, CallExpr *CallSite,
    bool prepareStage0BodyCalls, bool promoteStage0BodyCalls) {

  std::string qualificationParentIdentity;
  if (prepareStage0BodyCalls && CurrentFunction &&
      !m_Stage0GenericBodyQualificationPromotionScopes.empty()) {
    auto parent = InstantiationSemanticKeys.find(CurrentFunction);
    if (parent != InstantiationSemanticKeys.end())
      qualificationParentIdentity = parent->second;
  }

  if (Template->GenericParams.size() != Args.size()) {
    DiagnosticEngine::report(getLoc(CallSite), DiagID::NOTE_GENERIC, Template->Name, Template->GenericParams.size(), Args.size());
    HasError = true;
    return {nullptr, GenericSpecializationValidationState::Invalid};
  }

  if (Template->IsTrustedAtomicIntrinsic && !Args.empty()) {
    auto valueType = resolveType(Args.front());
    std::string expectedDomain;
    if (!supportsAtomicIntrinsicType(Template->Name, valueType,
                                     expectedDomain)) {
      error(CallSite ? static_cast<ASTNode *>(CallSite)
                     : static_cast<ASTNode *>(Template),
            DiagID::ERR_ATOMIC_INTRINSIC_TYPE_DOMAIN, Template->Name,
            valueType ? valueType->toString() : "unknown", expectedDomain);
      return {nullptr, GenericSpecializationValidationState::Invalid};
    }
  }

  // [NEW] Check Trait Bounds
  for (size_t i = 0; i < Template->GenericParams.size(); ++i) {
    if (!checkMorphologyBounds(
            CallSite ? getLoc(CallSite) : Template->Loc,
            Template->GenericParams[i], Args[i], false)) {
      return {nullptr, GenericSpecializationValidationState::Invalid};
    }
    if (!Template->GenericParams[i].TraitBounds.empty()) {
      auto bounds = substituteTraitBounds(
          Template->GenericParams[i].TraitBounds, Template->GenericParams,
          Args);
      if (!checkTraitBounds(CallSite ? getLoc(CallSite) : Template->Loc, 
                            Template->GenericParams[i].Name, 
                            bounds,
                            Args[i], false, Template->Loc)) {
        return {nullptr, GenericSpecializationValidationState::Invalid};
      }
    }
  }

  // Semantic cache identity and object linkage are deliberately separate.
  // Both use the template's module-scoped codegen name, while the cache key
  // additionally uses the exact structural identity of each argument.
  const std::string templateIdentity = Template->CodegenName.empty()
                                           ? Template->Name
                                           : Template->CodegenName;
  std::string cacheKey = templateIdentity + "_M";
  std::string mangledName = templateIdentity + "_M";
  for (auto &Arg : Args) {
    if (!Arg)
      continue;
    auto resolvedArg = resolveType(Arg);
    auto semanticArg = resolvedArg ? resolvedArg : Arg;
    cacheKey += "_" + semanticArg->canonicalMangledName();
    mangledName += "_" + semanticArg->getMangledName();
  }

  // Recursion Guard
  if (RecursionDepth > 100) {
    DiagnosticEngine::report(getLoc(CallSite), DiagID::NOTE_GENERIC, Template->Name);
    HasError = true;
    return {nullptr, GenericSpecializationValidationState::Invalid};
  }

  // Check Cache
  if (auto cached = InstantiationCache.find(cacheKey);
      cached != InstantiationCache.end()) {
    auto entry = cached->second;
    const auto validation =
        entry ? entry->Validation
              : GenericSpecializationValidationState::Unchecked;
    if (validation != GenericSpecializationValidationState::Valid &&
        !m_GenericValidationFrames.empty())
      m_GenericValidationFrames.back().HasInvalidDependency = true;
    if (!entry ||
        validation == GenericSpecializationValidationState::Unchecked) {
      if (entry) {
        entry->Validation = GenericSpecializationValidationState::Invalid;
        entry->BodyQualification = GenericBodyQualificationState::Invalid;
        SemanticEvidence::rollbackGenericBodyCallQualification(
            entry->SpecializationIdentity);
      }
      error(CallSite ? static_cast<ASTNode *>(CallSite)
                     : static_cast<ASTNode *>(Template),
            DiagID::ERR_GENERIC_SEMA,
            "recursive generic specialization is not supported: " +
                mangledName);
      return {nullptr, GenericSpecializationValidationState::Invalid};
    }
    if (prepareStage0BodyCalls &&
        validation == GenericSpecializationValidationState::Valid) {
      SemanticEvidence::recordGenericBodyCallQualificationDependency(
          qualificationParentIdentity, entry->SpecializationIdentity);
      if (promoteStage0BodyCalls &&
          entry->BodyQualification == GenericBodyQualificationState::Prepared)
        promoteStage0GenericBodyQualification(entry);
    }
    return {entry->Instance, validation, entry->BodyQualification};
  }

  const size_t validationDiagnosticStart = DiagnosticEngine::records().size();

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
      bool hasValue = false;
      try {
        size_t consumed = 0;
        val = std::stoull(SubstVal, &consumed);
        hasValue = consumed == SubstVal.size();
      } catch (...) {
      }
      constInfo.HasConstValue = hasValue;
      if (hasValue) {
        constInfo.ConstValue = val;
        constInfo.ConstValObj = ComptimeValue(val);
      }

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
  }
  if (Instance->ReturnContract.BindingBorrowsSoul &&
      Instance->ReturnTypeSyntax) {
    TypeSyntaxPtr root = Instance->ReturnTypeSyntax;
    if (root->NodeKind == TypeSyntax::Kind::Morphology &&
        !root->IsPostfix && root->Text == "&") {
      root = root->Subject;
      while (root && root->NodeKind == TypeSyntax::Kind::Morphology &&
             root->IsPostfix)
        root = root->Subject;
    } else {
      root = nullptr;
    }

    auto returnSubstMap = substMap;
    if (root && root->NodeKind == TypeSyntax::Kind::Named) {
      auto replacement = returnSubstMap.find(root->Text);
      if (replacement != returnSubstMap.end() && replacement->second)
        replacement->second = replacement->second->getSoulType();
    }
    std::map<std::string, TypeSyntaxPtr> typedReturnSubst;
    for (const auto &[name, replacement] : returnSubstMap) {
      if (replacement)
        typedReturnSubst.emplace(
            name, replacement->toSyntax(Instance->ReturnTypeSyntax->Begin,
                                        Instance->ReturnTypeSyntax->End));
    }
    Instance->ReturnTypeSyntax =
        Instance->ReturnTypeSyntax->substitute(typedReturnSubst);
    Instance->ReturnType = Instance->ReturnTypeSyntax->toCanonicalString();
  } else {
    applyTypeSyntaxSubst(Instance->ReturnTypeSyntax, Instance->ReturnType);
  }
  Instance->syncReturnContractTypeCache();

  // Source syntax is retained for diagnostics and TKI export, but a bound
  // nominal argument cannot survive a toSyntax()/toString() round-trip.  The
  // instantiated signature therefore keeps semantic Types substituted from
  // the original template declaration.
  for (size_t i = 0; i < Template->Args.size() && i < Instance->Args.size();
       ++i) {
    auto semanticArgument = Template->Args[i].ResolvedType
                                ? Template->Args[i].ResolvedType
                                : Sema::synthesizePhysicalTypeObject(
                                      Template->Args[i], false);
    Instance->Args[i].ResolvedType =
        semanticArgument ? semanticArgument->substitute(substMap) : nullptr;
  }
  auto semanticReturn =
      Template->ResolvedReturnType
          ? Template->ResolvedReturnType
          : Template->ReturnTypeSyntax
                ? toka::Type::fromSyntax(Template->ReturnTypeSyntax)
                : toka::Type::fromString(Template->ReturnType);
  Instance->ResolvedReturnType =
      semanticReturn ? semanticReturn->substitute(substMap) : nullptr;
  if (Instance->ReturnContract.BindingBorrowsSoul &&
      Instance->ResolvedReturnType) {
    auto reference = std::dynamic_pointer_cast<toka::ReferenceType>(
        Instance->ResolvedReturnType);
    if (reference && reference->PointeeType) {
      auto soul = reference->PointeeType->getSoulType();
      if (soul) {
        soul = soul->withAttributes(
            soul->IsWritable ||
                Instance->ReturnContract.BindingSoulWritable,
            soul->IsNullable, soul->IsBlocked);
        auto projected = std::make_shared<toka::ReferenceType>(soul);
        projected->IsWritable = reference->IsWritable;
        projected->IsNullable = reference->IsNullable;
        projected->IsBlocked = reference->IsBlocked;
        projected->IsCede = reference->IsCede;
        Instance->ResolvedReturnType = projected;
      }
    }
  }

  // Body type parameters remain written as T/Vec<T>.  They are resolved
  // through the exact Type aliases installed in the surrounding
  // instantiation scope below.  Replacing them with Type::toSyntax() would
  // erase a bound shape's Decl and let a same-named declaration in the
  // template module capture the body.  Const parameters still require
  // structural source substitution for array extents and sizeof.
  std::map<std::string, std::shared_ptr<toka::Type>> bodySubstMap;
  for (size_t i = 0; i < Template->GenericParams.size(); ++i) {
    const auto &parameter = Template->GenericParams[i];
    if (!parameter.IsConst)
      continue;
    auto replacement = resolveType(Args[i]);
    bodySubstMap[parameter.Name] = replacement;
    if (parameter.IsMorphic && !parameter.Name.empty() &&
        parameter.Name.front() == '\'') {
      bodySubstMap[parameter.Name.substr(1)] = replacement;
    }
  }

  auto applyBodySubst = [&](std::string &spelling) {
    for (const auto &[name, replacement] : bodySubstMap)
      replaceTypeNameToken(spelling, name,
                           replacement ? replacement->toString() : "unknown");
  };
  auto applyBodyTypeSyntaxSubst = [&](TypeSyntaxPtr &syntax,
                                      std::string &spelling) {
    if (!syntax) {
      applyBodySubst(spelling);
      return;
    }
    std::map<std::string, TypeSyntaxPtr> typed;
    for (const auto &[name, replacement] : bodySubstMap) {
      if (replacement)
        typed.emplace(name,
                      replacement->toSyntax(syntax->Begin, syntax->End));
    }
    syntax = syntax->substitute(typed);
    spelling = syntax->toCanonicalString();
  };
  auto applyBodyTypeArgumentSubst = [&](TypeArgumentSyntax &argument,
                                        std::string &spelling) {
    if (argument.ArgumentKind == TypeArgumentSyntax::Kind::Type) {
      applyBodyTypeSyntaxSubst(argument.Type, spelling);
      return;
    }
    auto replacement = bodySubstMap.find(argument.ConstantText);
    if (replacement != bodySubstMap.end())
      argument.ConstantText = replacement->second
                                  ? replacement->second->toString()
                                  : "unknown";
    spelling = argument.toCanonicalString();
  };

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
        applyBodyTypeSyntaxSubst(ne->TypeSyntax, ne->Type);
        if (ne->Initializer)
          visitExpr(ne->Initializer.get());
      } else if (auto *ae = dynamic_cast<AllocExpr *>(e)) {
        applyBodyTypeSyntaxSubst(ae->TypeSyntax, ae->TypeName);
        if (ae->Initializer)
          visitExpr(ae->Initializer.get());
        if (ae->ArraySize)
          visitExpr(ae->ArraySize.get());
      } else if (auto *ce = dynamic_cast<CastExpr *>(e)) {
        applyBodyTypeSyntaxSubst(ce->TargetTypeSyntax, ce->TargetType);
        if (ce->Expression)
          visitExpr(ce->Expression.get());
      } else if (auto *se = dynamic_cast<SizeOfExpr *>(e)) {
        // Const generic extents also occur in type-level expressions such as
        // `sizeof([u8; N_])`.  Leaving this spelling symbolic makes CodeGen
        // materialize a zero-length array even though the function instance
        // itself was specialized (for example bytes_from_array<32>).
        applyBodyTypeSyntaxSubst(se->TypeSyntax, se->TypeStr);
      } else if (auto *ise = dynamic_cast<InitStructExpr *>(e)) {
        for (auto &m : ise->Members)
          visitExpr(m.second.get());
      } else if (auto *call = dynamic_cast<CallExpr *>(e)) {
        // Function Name (if it has generics embedded?) - Usually handled by
        // Parser logic putting generics in name? If so, applySubst on Callee.
        applyBodySubst(call->Callee);
        for (size_t i = 0; i < call->GenericArgs.size(); ++i) {
          if (i < call->GenericArgSyntax.size())
            applyBodyTypeArgumentSubst(call->GenericArgSyntax[i],
                                       call->GenericArgs[i]);
          else
            applyBodySubst(call->GenericArgs[i]);
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
        applyBodySubst(are->AssignedTypeName);
        for (auto &m : are->Fields)
          if (m.second)
            visitExpr(m.second.get());
      } else if (auto *arrayInit = dynamic_cast<ArrayInitExpr *>(e)) {
        applyBodyTypeSyntaxSubst(arrayInit->TypeSyntax, arrayInit->Type);
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
        applyBodySubst(clo->ReturnType);
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
      } else if (auto *initBlock = dynamic_cast<InitBlockStmt *>(s)) {
        visitStmt(initBlock->Body.get());
      } else if (auto *vd = dynamic_cast<VariableDecl *>(s)) {
        applyBodyTypeSyntaxSubst(vd->DeclaredTypeSyntax, vd->TypeName);
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
        applyBodySubst(dd->TypeName);
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
  std::string semanticOwner = "crate:external;module:source";
  if (ModuleScope *module = getLexicalModule(Template->Loc);
      module && !module->ShadowCrateId.empty() &&
      !module->ShadowLogicalModulePath.empty())
    semanticOwner = "crate:" + module->ShadowCrateId +
                    ";module:" + module->ShadowLogicalModulePath;
  auto templateLocation =
      DiagnosticEngine::SrcMgr
          ? DiagnosticEngine::SrcMgr->getFullSourceLoc(Template->Loc)
          : FullSourceLoc{};
  std::string semanticInstantiation =
      semanticOwner + ";template:" + Template->Name;
  if (templateLocation.isValid())
    semanticInstantiation +=
        ";declaration:" + std::to_string(templateLocation.Line) + ":" +
        std::to_string(templateLocation.Column);
  semanticInstantiation += ";arguments:";
  std::function<std::string(const std::shared_ptr<toka::Type> &)>
      stableInstantiationType =
          [&](const std::shared_ptr<toka::Type> &type) -> std::string {
    if (!type)
      return "indeterminate";
    std::string canonical;
    if (canonicalOutcomeTypeIdentity(type, canonical))
      return canonical;
    std::string result = "toka-stage0-instantiation-type-v1;";
    appendCanonicalOutcomeTypeAttributes(result, *type);
    appendCanonicalIdentityField(
        result, "kind", std::to_string(static_cast<unsigned>(type->typeKind)));
    auto appendChild = [&](const std::string &name,
                           const std::shared_ptr<toka::Type> &child) {
      appendCanonicalIdentityField(result, name,
                                   stableInstantiationType(child));
    };
    if (auto pointer = std::dynamic_pointer_cast<toka::PointerType>(type)) {
      appendChild("pointee", pointer->PointeeType);
    } else if (auto array = std::dynamic_pointer_cast<toka::ArrayType>(type)) {
      appendCanonicalIdentityField(result, "extent",
                                   std::to_string(array->Size));
      appendCanonicalIdentityField(result, "symbolic-extent",
                                   array->SymbolicSize);
      appendChild("element", array->ElementType);
    } else if (auto slice = std::dynamic_pointer_cast<toka::SliceType>(type)) {
      appendChild("element", slice->ElementType);
    } else if (auto shape = std::dynamic_pointer_cast<toka::ShapeType>(type)) {
      appendCanonicalIdentityField(
          result, "definition",
          shape->Decl ? canonicalOutcomeShapeIdentity(shape->Decl)
                      : "unresolved:" + shape->Name);
      appendCanonicalIdentityField(result, "variant", shape->VariantSuffix);
      for (size_t index = 0; index < shape->GenericArgs.size(); ++index)
        appendChild("argument-" + std::to_string(index),
                    shape->GenericArgs[index]);
    } else if (auto function =
                   std::dynamic_pointer_cast<toka::FunctionType>(type)) {
      appendCanonicalIdentityField(
          result, "receiver",
          std::to_string(static_cast<unsigned>(function->ReceiverMode)));
      appendCanonicalIdentityField(result, "variadic",
                                   function->IsVariadic ? "1" : "0");
      for (size_t index = 0; index < function->ParamTypes.size(); ++index)
        appendChild("parameter-" + std::to_string(index),
                    function->ParamTypes[index]);
      appendChild("return", function->ReturnType);
    } else if (auto function =
                   std::dynamic_pointer_cast<toka::DynFnType>(type)) {
      appendCanonicalIdentityField(
          result, "receiver",
          std::to_string(static_cast<unsigned>(function->ReceiverMode)));
      for (size_t index = 0; index < function->ParamTypes.size(); ++index)
        appendChild("parameter-" + std::to_string(index),
                    function->ParamTypes[index]);
      appendChild("return", function->ReturnType);
    } else if (auto outcome =
                   std::dynamic_pointer_cast<toka::MissOutcomeType>(type)) {
      appendChild("payload", outcome->PayloadType);
    } else if (auto uninit =
                   std::dynamic_pointer_cast<toka::UninitType>(type)) {
      appendChild("inner", uninit->InnerType);
    } else {
      appendCanonicalIdentityField(result, "spelling", type->toString());
    }
    return result;
  };
  for (size_t index = 0; index < Args.size(); ++index) {
    if (index != 0)
      semanticInstantiation += ",";
    auto argument = resolveType(Args[index], false);
    semanticInstantiation += stableInstantiationType(argument);
  }
  InstantiationSemanticKeys[Instance] = semanticInstantiation;

  auto cacheEntry = std::make_shared<GenericSpecializationCacheEntry>();
  cacheEntry->Instance = Instance;
  cacheEntry->Validation = GenericSpecializationValidationState::Unchecked;
  cacheEntry->BodyQualification = GenericBodyQualificationState::Unseen;
  cacheEntry->SpecializationIdentity = std::move(semanticInstantiation);
  Instance->Stage0BodyQualificationRequired =
      SemanticEvidence::isCodeGenAuthorityEnabled();
  Instance->Stage0BodySpecializationIdentity =
      cacheEntry->SpecializationIdentity;
  InstantiationCache[cacheKey] = cacheEntry;
  InstantiationCache[mangledName] = cacheEntry;

  // [NEW] Inject Const Generic Variables into Body
  // Removed dirty hack (VariableDecl injection).
  // Const values are now handled by SymbolInfo resolution in CodeGen.

  // 4. Semantic Check (Recursion)
  m_GenericValidationFrames.push_back({});
  if (prepareStage0BodyCalls) {
    m_Stage0GenericBodyQualificationPermitDepths.push_back(
        m_D3SpeculativeCallDepth);
    m_Stage0GenericBodyQualificationPromotionScopes.push_back(
        promoteStage0BodyCalls);
  }
  checkFunction(Instance);
  if (prepareStage0BodyCalls) {
    m_Stage0GenericBodyQualificationPromotionScopes.pop_back();
    m_Stage0GenericBodyQualificationPermitDepths.pop_back();
  }
  const bool invalidDependency =
      m_GenericValidationFrames.back().HasInvalidDependency;
  m_GenericValidationFrames.pop_back();

  exitScope();
  RecursionDepth--;

  const auto &validationDiagnostics = DiagnosticEngine::records();
  const bool validationFailed =
      invalidDependency ||
      std::any_of(
          validationDiagnostics.begin() +
              std::min(validationDiagnosticStart, validationDiagnostics.size()),
          validationDiagnostics.end(),
          [](const auto &record) { return record.Level == DiagLevel::Error; });
  const auto validation = validationFailed
                              ? GenericSpecializationValidationState::Invalid
                              : GenericSpecializationValidationState::Valid;
  cacheEntry->Validation = validation;
  if (prepareStage0BodyCalls) {
    if (validation == GenericSpecializationValidationState::Valid) {
      cacheEntry->BodyQualification = GenericBodyQualificationState::Prepared;
      SemanticEvidence::recordGenericBodyCallQualificationDependency(
          qualificationParentIdentity, cacheEntry->SpecializationIdentity);
      if (promoteStage0BodyCalls)
        promoteStage0GenericBodyQualification(cacheEntry);
    } else {
      cacheEntry->BodyQualification = GenericBodyQualificationState::Invalid;
      SemanticEvidence::rollbackGenericBodyCallQualification(
          cacheEntry->SpecializationIdentity);
    }
  }
  if (validation != GenericSpecializationValidationState::Valid &&
      !m_GenericValidationFrames.empty())
    m_GenericValidationFrames.back().HasInvalidDependency = true;
  return {Instance, validation, cacheEntry->BodyQualification};
}

void Sema::promoteStage0GenericBodyQualification(
    const std::shared_ptr<GenericSpecializationCacheEntry> &entry) {
  if (!entry || entry->SpecializationIdentity.empty())
    return;
  const auto promoted = SemanticEvidence::promoteGenericBodyCallQualification(
      entry->SpecializationIdentity);
  if (promoted.empty())
    return;
  const std::set<std::string> promotedSet(promoted.begin(), promoted.end());
  for (auto &[key, cached] : InstantiationCache) {
    (void)key;
    if (cached && promotedSet.count(cached->SpecializationIdentity))
      cached->BodyQualification = GenericBodyQualificationState::Complete;
    if (cached && cached->Instance &&
        promotedSet.count(cached->SpecializationIdentity))
      cached->Instance->Stage0BodyQualificationComplete = true;
  }
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

std::string Sema::extractPathRoot(const std::string &Path) {
  if (Path.empty())
    return {};
  const size_t delim = Path.find_first_of(".[");
  std::string root = (delim == std::string::npos) ? Path : Path.substr(0, delim);
  return Type::stripMorphology(root);
}

int Sema::getScopeDepth(const std::string &Name) {
  if (Name.empty() || !CurrentScope)
    return 0;

  std::string root = extractPathRoot(Name);
  SymbolInfo *info = nullptr;
  std::string actualName = root;
  Scope *ownerScope = nullptr;

  if (CurrentScope->findVariableWithDerefScope(root, info, actualName, ownerScope) && ownerScope) {
    return ownerScope->Depth;
  }

  // Symbol not found in any scope. Fail-closed to avoid treating unresolvable paths as global.
  return FAIL_CLOSED_SCOPE_DEPTH;
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
