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
  if (isUnsafePublicAPIExempt(CurrentModule, Fn->Loc) || !Fn->IsPub ||
      Fn->Name.rfind("unsafe_", 0) == 0 ||
      Fn->Name.rfind("raw_", 0) == 0 || Fn->Name.rfind("__", 0) == 0) {
    return;
  }

  for (auto &Arg : Fn->Args) {
    std::shared_ptr<toka::Type> type = Arg.ResolvedType;
    if (!type) {
      type = toka::Type::fromString(
          resolveType(Sema::synthesizePhysicalType(Arg, false)));
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
      type = toka::Type::fromString(resolveType(Fn->ReturnType));
    }
    if (isUnsafeType(type)) {
      DiagnosticEngine::report(Fn->Loc, DiagID::ERR_EXPOSED_UNSAFE_RET,
                               Fn->Name, type->toString());
      HasError = true;
    }
  }
}

void Sema::checkUnsafePublicShapeBoundary(ShapeDecl *Shape) {
  if (isUnsafePublicAPIExempt(CurrentModule, Shape->Loc) || !Shape->IsPub ||
      Shape->Name == "cstr" || Shape->Name.rfind("Unsafe", 0) == 0 ||
      Shape->Name.rfind("Raw", 0) == 0) {
    return;
  }

  auto checkMember = [&](ShapeMember &member) {
    std::shared_ptr<toka::Type> type = member.ResolvedType;
    if (!type) {
      type = toka::Type::fromString(
          resolveType(Sema::synthesizePhysicalType(member)));
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

std::map<std::string, std::string>
Sema::registerAssociatedTypes(ImplDecl *Impl, TraitDecl *Trait,
                              const std::string &resolvedTypeName) {
  std::map<std::string, std::string> replacements;
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

    std::string resolvedAssocType = implAssoc->Type;
    replaceTypeNameToken(resolvedAssocType, "Self", resolvedTypeName);
    for (const auto &[knownName, knownType] : replacements) {
      replaceTypeNameToken(resolvedAssocType, knownName, knownType);
    }
    resolvedAssocType = resolveType(resolvedAssocType);
    replacements[name] = resolvedAssocType;

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

    AssociatedTypeMap[assocKey] =
        AssociatedTypeBinding{resolvedAssocType, implAssoc->IsPer, implAssoc->Loc};
  }

  return replacements;
}

void Sema::applyAssociatedTypeSubstitutions(
    ImplDecl *Impl, const std::map<std::string, std::string> &substitutions) {
  if (!Impl || substitutions.empty())
    return;

  for (auto &Method : Impl->Methods) {
    for (const auto &[name, ty] : substitutions) {
      replaceTypeNameToken(Method->ReturnType, name, ty);
      Method->ResolvedReturnType = nullptr;
      for (auto &Arg : Method->Args) {
        replaceTypeNameToken(Arg.Type, name, ty);
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

  return "";
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
    for (const auto &Arg : Ext->Args) {
      debugCheckBindingPermission(Arg);
      debugCheckBindingTypeString("extern argument", Arg.Name, Arg.Type,
                                  Arg.Permission, Ext->Loc);
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
    std::string target = Alias->TargetType;
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
    ms.TypeAliases[Alias->Name] = {target, Alias->IsStrong,
                                   Alias->GenericParams};
    TypeAliasMap[Alias->Name] = {target, Alias->IsStrong,
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
    validateDynTraitObjectSafetyInType(Alias->TargetType, getLoc(Alias.get()));
  }
  for (auto &St : M.Shapes) {
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
  for (auto &Impl : M.Impls) {
    DeclarationLexicalScopes[Impl.get()] = &ms;
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
    std::string target = Alias->TargetType;
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
    ms.TypeAliases[Alias->Name] = {target, Alias->IsStrong,
                                   Alias->GenericParams};
    TypeAliasMap[Alias->Name] = {target, Alias->IsStrong,
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
    validateDynTraitObjectSafetyInType(Alias->TargetType, getLoc(Alias.get()));
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

    if (ShapeMap.count(baseShapeName) &&
        !ShapeMap[baseShapeName]->GenericParams.empty()) {
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

      std::string baseName = Impl->TypeName;
      size_t lt = baseName.find('<');
      if (lt != std::string::npos)
        baseName = baseName.substr(0, lt);
      GenericImplMap[baseName].push_back(Impl.get());
      
      if (Impl->TraitName == "encap") {
        EncapMap[baseName] = Impl->EncapEntries;
      }

      continue; // Skip standard registration for templates
    }

    if (Impl->TraitName == "encap") {
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
  std::map<std::string, std::string> associatedTypeSubstitutions =
      registerAssociatedTypes(Impl, traitDecl, resolvedTypeName);
  AssociatedTypeSubstitutionCache[Impl] = associatedTypeSubstitutions;

  // [New] Resolve 'Self' in Method Signatures for External Callers
  // We must replace 'Self' with the concrete (or generic) TypeName
  // so that callers (like main) typically don't fail to resolve 'Self'.
  std::string selfTy = Impl->TypeName;
  for (auto &Method : Impl->Methods) {
    replaceTypeNameToken(Method->ReturnType, "Self", selfTy);
    Method->ResolvedReturnType = nullptr;
    for (auto &Arg : Method->Args) {
      replaceTypeNameToken(Arg.Type, "Self", selfTy);
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
    MethodMap[resolvedTypeName][Method->Name] = Method->ReturnType;
    MethodDecls[resolvedTypeName][Method->Name] = Method.get();
    implemented.insert(Method->Name);
  }

  // Populate ImplMap
  if (!Impl->TraitName.empty()) {
    std::string implKey = resolvedTypeName + "@" + canonicalTrait;
    ImplMap[implKey]; // Ensure the key exists even for empty traits
    for (auto &Method : Impl->Methods) {
      ImplMap[implKey][Method->Name] = Method.get();
    }
  }

  // Handle Trait Defaults
  if (!Impl->TraitName.empty()) {
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
          std::string defaultReturnType = Method->ReturnType;
          for (const auto &[name, ty] : associatedTypeSubstitutions) {
            replaceTypeNameToken(defaultReturnType, name, ty);
          }
          MethodMap[resolvedTypeName][Method->Name] = defaultReturnType;
          MethodDecls[resolvedTypeName][Method->Name] = Method.get();
        } else {
          // [Fix] Optional methods for intrinsic interfaces
          if (getTraitFamilyName(canonicalTrait) == "delegate") {
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

  // [Toka] Resource Management: Mark type as having drop if @encap is
  // implemented
  if (getTraitFamilyName(canonicalTrait) == "encap") {
    if (implemented.count("drop")) {
      m_ShapeProps[resolvedTypeName].HasDrop = true;
      // [Single Source of Truth] Store the authoritative mangled name
      if (ShapeMap.count(resolvedTypeName)) {
        ShapeMap[resolvedTypeName]->MangledDestructorName =
            "encap_" + resolvedTypeName + "_drop";
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
  std::string baseName = Impl->TypeName;
  size_t lt = baseName.find('<');
  if (lt != std::string::npos) {
    baseName = baseName.substr(0, lt);
    if (std::find(GenericImplMap[baseName].begin(), GenericImplMap[baseName].end(), Impl) == GenericImplMap[baseName].end()) {
      GenericImplMap[baseName].push_back(Impl);
    }
    return; // Skip standard registration for templates
  }

  std::string selfTy = Impl->TypeName;
  for (auto &Method : Impl->Methods) {
    replaceTypeNameToken(Method->ReturnType, "Self", selfTy);
    Method->ResolvedReturnType = nullptr;
    for (auto &Arg : Method->Args) {
      replaceTypeNameToken(Arg.Type, "Self", selfTy);
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
    MethodMap[resolvedTypeName][Method->Name] = Method->ReturnType;
    MethodDecls[resolvedTypeName][Method->Name] = Method.get();
    implemented.insert(Method->Name);
  }

  if (!Impl->TraitName.empty()) {
    std::string implKey = resolvedTypeName + "@" + canonicalTrait;
    ImplMap[implKey];
    for (auto &Method : Impl->Methods) {
      ImplMap[implKey][Method->Name] = Method.get();
    }
  }

  if (getTraitFamilyName(canonicalTrait) == "encap") {
    if (implemented.count("drop")) {
      m_ShapeProps[resolvedTypeName].HasDrop = true;
      if (ShapeMap.count(resolvedTypeName)) {
        ShapeMap[resolvedTypeName]->MangledDestructorName =
            "encap_" + resolvedTypeName + "_drop";
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
    std::string resolvedRetStr = resolveType(Fn->ReturnType);
    Fn->ResolvedReturnType = toka::Type::fromString(resolvedRetStr);
  } else {
    Fn->ResolvedReturnType = toka::Type::fromString("void");
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
    std::string fullType = Sema::synthesizePhysicalType(Arg, false);

    // [Fix] Preserve pre-resolved Types (e.g. Synthetic Closures)
    if (Arg.ResolvedType) {
      Info.TypeObj = Arg.ResolvedType;
    } else {
      // [New] Annotated AST: Use resolveType (string version) to handle
      // aliases/Self, then parse
      std::string resolvedStr = resolveType(fullType);
      Info.TypeObj = toka::Type::fromString(resolvedStr);

      // Assign to AST Node for CodeGen
      Arg.ResolvedType = Info.TypeObj;
    }

    Info.IsRebindable = Arg.IsRebindable;
    Info.IsMorphicExempt = Arg.IsMorphicExempt; // [NEW]
    Info.IsDeclaredMutable = Arg.IsValueMutable;
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
      if (CurrentScope->findSymbol(Arg.Name, Info) && Info && !Info->Moved) {
        SourceLocation argLoc = Arg.Loc.isValid() ? Arg.Loc : getLoc(Fn);
        DiagnosticEngine::report(argLoc, DiagID::ERR_CEDE_PARAMETER_NOT_CONSUMED,
                                 Arg.Name);
        HasError = true;
        recordDecision(Fn, SemanticRuleID::OwnCede001,
                       SemanticOperation::CedeObligation,
                       SemanticDecision::Reject,
                       SemanticReason::UnconsumedCede, Arg.Name,
                       Fn->Name, argLoc);
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
      if (info.IsDeclaredVariable && !info.HasBeenUsed) {
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
    for (const auto &[name, typeName] : assocIt->second) {
      SymbolInfo Sym;
      Sym.IsTypeAlias = true;
      Sym.TypeObj = resolveType(toka::Type::fromString(typeName));
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
  for (auto const &[name, decl] : ShapeMap) {
    if (!decl->GenericParams.empty())
      continue;
    if (GenericShapeCache.count(name))
      continue;

    if (decl->Kind == ShapeKind::Struct) {
      bool needsDrop = false;

      // Check if Shape manages resources
      for (auto &memb : decl->Members) {
        // 1. Raw Pointers (*T) - Force drop for safety
        if (memb.IsRawPointer) {
          needsDrop = true;
          break;
        }
        // [New Rule] Unique/Shared pointers and members with drop are handled
        // automatically by CodeGen, so they don't force parent to implement
        // 'drop'.
      }

      if (needsDrop) {
        // Must have 'drop' method in MethodMap
        // Check MethodMap[name]["drop"]
        bool hasDropImpl = false;
        std::string resolvedName = resolveType(name);
        if ((MethodMap.count(name) && MethodMap[name].count("drop")) ||
            (MethodMap.count(resolvedName) && MethodMap[resolvedName].count("drop"))) {
          hasDropImpl = true;
        }

        if (!hasDropImpl) {
          DiagnosticEngine::report(getLoc(decl), DiagID::ERR_SHAPE_NO_DROP,
                                   name);
          HasError = true;
        }

        // [New] Must have 'clone' method as well (Auto-Clone Enforcement)
        bool hasCloneImpl = false;
        if ((MethodMap.count(name) && MethodMap[name].count("clone")) ||
            (MethodMap.count(resolvedName) && MethodMap[resolvedName].count("clone"))) {
          hasCloneImpl = true;
        }

        if (!hasCloneImpl) {
          DiagnosticEngine::report(getLoc(decl), DiagID::ERR_SHAPE_NO_CLONE,
                                   name);
          HasError = true;
        }
      }
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

        std::string fullTypeStr = Sema::synthesizePhysicalType(m);
        validateTypeVisibilityInType(fullTypeStr, getLoc(S.get()));
        std::string resolvedName = resolveType(fullTypeStr);
        m.ResolvedType = toka::Type::fromString(resolvedName);

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
            if (SD->Kind == ShapeKind::Enum &&
                !SD->IsPacked) { // Packed is C-enum
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

    // Check if Shape has explicit drop
    bool hasExplicitDrop = false;
    // Look in Impl blocks for "drop"
    for (auto &I : M.Impls) {
      if (I->TypeName == S->Name) {
        for (auto &M : I->Methods) {
          if (M->Name == "drop") {
            hasExplicitDrop = true;
            break;
          }
        }
      }
      if (hasExplicitDrop)
        break;
    }

    if (props.HasRawPtr && !hasExplicitDrop) {
      DiagnosticEngine::report(getLoc(S.get()), DiagID::ERR_UNSAFE_RAW_PTR,
                               S->Name);
      HasError = true;
    }

    if (props.HasDrop && !hasExplicitDrop) {
      // [Ch 7] Synthesize default drop impl for resource-managing shapes
      std::vector<FunctionDecl::Arg> args;
      FunctionDecl::Arg dropArg;
      dropArg.Name = "self";
      dropArg.Type = "Self";
      dropArg.IsValueMutable = true;
      dropArg.Permission = BindingPermission::fromLegacy(
          dropArg.IsRawPointer, dropArg.IsUnique, dropArg.IsShared,
          dropArg.IsReference, dropArg.IsRebindable,
          dropArg.IsPointerNullable, dropArg.IsRebindBlocked,
          dropArg.IsValueMutable, dropArg.IsValueNullable,
          dropArg.IsValueBlocked, dropArg.IsMorphicExempt);
      args.push_back(std::move(dropArg));
      auto dropFn =
          std::make_unique<FunctionDecl>(false, "drop", std::move(args),
                                         std::make_unique<BlockStmt>(), "void");

      std::vector<FunctionDecl::Arg> cloneArgs;
      FunctionDecl::Arg cloneArg;
      cloneArg.Name = "self";
      cloneArg.Type = "Self";
      cloneArgs.push_back(std::move(cloneArg));
      auto cloneFn = 
          std::make_unique<FunctionDecl>(true, "clone", std::move(cloneArgs),
                                         nullptr, "Self");
      cloneFn->IsDeleted = true;

      std::vector<std::unique_ptr<FunctionDecl>> methods;
      methods.push_back(std::move(dropFn));
      methods.push_back(std::move(cloneFn));

      auto impl =
          std::make_unique<ImplDecl>(S->Name, std::move(methods), "encap");

      // Register and Add to Module
      registerImpl(impl.get());
      M.Impls.push_back(std::move(impl));

      // Authorize destructor for CodeGen
      S->MangledDestructorName = "encap_" + S->Name + "_drop";
      m_ShapeProps[S->Name].HasDrop = true;
    }

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
      if (!checkTraitBounds(CallSite ? getLoc(CallSite) : Template->Loc, 
                            Template->GenericParams[i].Name, 
                            Template->GenericParams[i].TraitBounds, 
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
  std::map<std::string, std::string> substMap;
  for (size_t i = 0; i < Template->GenericParams.size(); ++i) {
    const auto &GP = Template->GenericParams[i];
    std::string substValue = resolveType(Args[i])->toString();
    substMap[GP.Name] = substValue;
    if (GP.IsMorphic && !GP.Name.empty() && GP.Name[0] == '\'')
      substMap[GP.Name.substr(1)] = substValue;
  }

  auto applySubst = [&](std::string &s) {
    for (auto const &[K, V] : substMap) {
      size_t pos = 0;
      while ((pos = s.find(K, pos)) != std::string::npos) {
        auto isWordChar = [](char c) { return std::isalnum(c) || c == '_'; };
        bool startOk = (pos == 0) || !isWordChar(s[pos - 1]);
        bool endOk =
            (pos + K.size() == s.size()) || !isWordChar(s[pos + K.size()]);
        if (startOk && endOk) {
          s.replace(pos, K.size(), V);
          pos += V.size();
        } else {
          pos += K.size();
        }
      }
    }
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
    applySubst(Arg.Type);
    Arg.ResolvedType = nullptr;
  }
  applySubst(Instance->ReturnType);

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
        applySubst(ne->Type);
        if (ne->Initializer)
          visitExpr(ne->Initializer.get());
      } else if (auto *ae = dynamic_cast<AllocExpr *>(e)) {
        applySubst(ae->TypeName);
        if (ae->Initializer)
          visitExpr(ae->Initializer.get());
        if (ae->ArraySize)
          visitExpr(ae->ArraySize.get());
      } else if (auto *ce = dynamic_cast<CastExpr *>(e)) {
        applySubst(ce->TargetType);
        if (ce->Expression)
          visitExpr(ce->Expression.get());
      } else if (auto *ise = dynamic_cast<InitStructExpr *>(e)) {
        applySubst(ise->ShapeName);
        for (auto &m : ise->Members)
          visitExpr(m.second.get());
      } else if (auto *call = dynamic_cast<CallExpr *>(e)) {
        // Function Name (if it has generics embedded?) - Usually handled by
        // Parser logic putting generics in name? If so, applySubst on Callee.
        applySubst(call->Callee);
        for (auto &s : call->GenericArgs)
          applySubst(s);
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
            std::string valStr = substMap[name];
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
        applySubst(vd->TypeName);
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
