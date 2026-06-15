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
#include "toka/Parser.h"
#include <cctype>
#include <iostream>
#include <set>
#include <string>

namespace toka {

// Helper to get location
static SourceLocation getLoc(ASTNode *Node) { return Node->Loc; }

static bool isAnonymousRecord(const std::shared_ptr<toka::Type> &type) {
  if (!type) return false;
  auto shapeT = std::dynamic_pointer_cast<toka::ShapeType>(type);
  if (!shapeT) return false;
  return shapeT->Name.rfind("__Toka_Anon_Rec_", 0) == 0;
}

bool areStructsStructurallyCompatible(Sema *sema, const std::string &targetName, const std::string &sourceName) {
  auto targetIt = sema->ShapeMap.find(targetName);
  auto sourceIt = sema->ShapeMap.find(sourceName);
  if (targetIt == sema->ShapeMap.end() || sourceIt == sema->ShapeMap.end()) {
    return false;
  }
  ShapeDecl* targetDecl = targetIt->second;
  ShapeDecl* sourceDecl = sourceIt->second;
  if (!targetDecl || !sourceDecl) return false;
  
  if (targetDecl->Kind != ShapeKind::Struct || sourceDecl->Kind != ShapeKind::Struct) {
    return false;
  }
  
  if (targetDecl->Members.size() != sourceDecl->Members.size()) {
    return false;
  }
  
  for (size_t i = 0; i < targetDecl->Members.size(); ++i) {
    const auto &tMem = targetDecl->Members[i];
    const auto &sMem = sourceDecl->Members[i];
    
    // Field names must match exactly under strict order-equality
    if (tMem.Name != sMem.Name) {
      return false;
    }
    
    auto tType = tMem.ResolvedType ? tMem.ResolvedType : sema->resolveType(toka::Type::fromString(tMem.Type));
    auto sType = sMem.ResolvedType ? sMem.ResolvedType : sema->resolveType(toka::Type::fromString(sMem.Type));
    
    if (!sema->isTypeCompatible(tType, sType)) {
      return false;
    }
  }
  return true;
}

std::string Sema::resolveType(const std::string &Type, bool force) {
  // [NEW] Local Type Alias (Generic Parameter) Lookup
  if (CurrentScope) {
    SymbolInfo Sym;
    if (CurrentScope->lookup(Type, Sym)) {
      if (Sym.IsTypeAlias && Sym.TypeObj) {
        return resolveType(Sym.TypeObj->toString(), force);
      }
    }
  }

  size_t scopePos = Type.find("::");
  if (scopePos != std::string::npos) {
    std::string ModName = Type.substr(0, scopePos);
    std::string TargetType = Type.substr(scopePos + 2);

    SymbolInfo *modSpecPtr = nullptr;
    std::string actualModName = ModName;
    if (CurrentScope && CurrentScope->findVariableWithDeref(ModName, modSpecPtr, actualModName) &&
        modSpecPtr->ReferencedModule) {
      SymbolInfo modSpec = *modSpecPtr;
      modSpecPtr->HasBeenUsed = true;
      if (modSpecPtr->ImportingDecl) {
        const_cast<ImportDecl*>(modSpecPtr->ImportingDecl)->HasBeenUsed = true;
      }
      ModuleScope *target = (ModuleScope *)modSpec.ReferencedModule;
      if (target->TypeAliases.count(TargetType)) {
        auto &aliasInfo = target->TypeAliases[TargetType];
        if (!aliasInfo.IsStrong || force) {
          return resolveType(aliasInfo.Target, force);
        }
      }
      return TargetType; // It's a base type or shape in that module or a
                         // strong alias
    }
  }

  // [NEW] Check for Generic Type Alias Instantiation (e.g. AliasNodeG<i32>)
  std::string baseName = Type;
  std::vector<std::string> args;
  size_t lt = Type.find('<');
  if (lt != std::string::npos && Type.back() == '>') {
    baseName = Type.substr(0, lt);
    std::string argsStr = Type.substr(lt + 1, Type.size() - lt - 2);
    // Split args by comma (respecting nested brackets)
    int balance = 0;
    std::string current;
    for (char c : argsStr) {
      if (c == '<' || c == '(' || c == '[')
        balance++;
      else if (c == '>' || c == ')' || c == ']')
        balance--;

      if (c == ',' && balance == 0) {
        // trim
        args.push_back(current);
        current = "";
      } else {
        if (current.empty() && c == ' ')
          continue; // skip leading space
        current += c;
      }
    }
    if (!current.empty())
      args.push_back(current);
  }

  // Check Local Alias (Generic) with args?
  // Check TypeAliasMap
  if (TypeAliasMap.count(baseName)) {
    auto &info = TypeAliasMap[baseName];
    if (!info.GenericParams.empty()) {
      // Substitute!
      std::string result = info.Target;
      for (size_t i = 0; i < info.GenericParams.size(); ++i) {
        std::vector<std::string> paramsToReplace = { info.GenericParams[i].Name };
        if (!info.GenericParams[i].Name.empty() && info.GenericParams[i].Name[0] == '\'') {
          paramsToReplace.push_back(info.GenericParams[i].Name.substr(1));
        }
        std::string val = (i < args.size()) ? args[i] : "unknown";
        for (const std::string &param : paramsToReplace) {
          size_t pos = 0;
          while ((pos = result.find(param, pos)) != std::string::npos) {
            // Check boundaries
            bool startOk =
                (pos == 0) || !isalnum(result[pos - 1]) && result[pos - 1] != '_';
            bool endOk = (pos + param.size() == result.size()) ||
                         !isalnum(result[pos + param.size()]) &&
                             result[pos + param.size()] != '_';

            if (startOk && endOk) {
              result.replace(pos, param.size(), val);
              pos += val.size();
            } else {
              pos += param.size();
            }
          }
        }
      }

      if (info.IsStrong && !force) {
        return Type;
      }
      return resolveType(result, force);
    }
  }

  // Fallback: use the object-based resolver which handles generics
  auto typeObj = toka::Type::fromString(Type);
  return resolveType(typeObj, force)->toString();
}

std::shared_ptr<toka::Type> Sema::resolveType(std::shared_ptr<toka::Type> type,
                                              bool force) {
  if (!type)
    return nullptr;

  if (auto ptr = std::dynamic_pointer_cast<toka::PointerType>(type)) {
    auto inner = resolveType(ptr->getPointeeType());
    if (inner != ptr->getPointeeType()) {
      std::shared_ptr<toka::PointerType> newPtr;
      if (ptr->typeKind == toka::Type::SharedPtr)
        newPtr = std::make_shared<toka::SharedPointerType>(inner);
      else if (ptr->typeKind == toka::Type::UniquePtr)
        newPtr = std::make_shared<toka::UniquePointerType>(inner);
      else if (ptr->typeKind == toka::Type::Reference)
        newPtr = std::make_shared<toka::ReferenceType>(inner);
      else
        newPtr = std::make_shared<toka::RawPointerType>(inner);
      newPtr->IsWritable = ptr->IsWritable;
      newPtr->IsNullable = ptr->IsNullable;
      return newPtr;
    }
  }

  if (auto arr = std::dynamic_pointer_cast<toka::ArrayType>(type)) {
    auto inner = resolveType(arr->ElementType);
    if (inner != arr->ElementType) {
      auto newArr = std::make_shared<toka::ArrayType>(inner, arr->Size,
                                                      arr->SymbolicSize);
      newArr->IsWritable = arr->IsWritable;
      newArr->IsNullable = arr->IsNullable;
      return newArr;
    }
  }

  if (auto fnTy = std::dynamic_pointer_cast<toka::FunctionType>(type)) {
    bool changed = false;
    std::vector<std::shared_ptr<toka::Type>> newParams;
    for (auto &p : fnTy->ParamTypes) {
      auto resolved = resolveType(p);
      newParams.push_back(resolved);
      if (resolved != p) changed = true;
    }
    
    auto newRet = resolveType(fnTy->ReturnType);
    if (newRet != fnTy->ReturnType) changed = true;
    
    if (changed) {
      auto newFn = std::make_shared<toka::FunctionType>(newParams, newRet);
      newFn->IsVariadic = fnTy->IsVariadic;
      newFn->IsWritable = fnTy->IsWritable;
      newFn->IsNullable = fnTy->IsNullable;
      newFn->IsBlocked = fnTy->IsBlocked;
      return newFn;
    }
  }

  if (auto shape = std::dynamic_pointer_cast<toka::ShapeType>(type)) {
    if (ShapeImportMap.count(shape->Name)) {
      const_cast<ImportDecl*>(ShapeImportMap[shape->Name])->HasBeenUsed = true;
    }
    if (!shape->Name.empty() && shape->Name.front() == '(' && shape->Name.back() == ')') {
      std::string nameStr = shape->Name;
      if (ParenthesizedRecordTypes.count(nameStr)) {
        auto cached = ParenthesizedRecordTypes[nameStr];
        return cached->withAttributes(type->IsWritable, type->IsNullable, type->IsBlocked);
      }

      std::string inner = nameStr.substr(1, nameStr.size() - 2);
      std::vector<ShapeMember> members;
      int balance = 0;
      size_t start = 0;
      for (size_t i = 0; i < inner.size(); ++i) {
        if (inner[i] == '<' || inner[i] == '(' || inner[i] == '[') {
          balance++;
        } else if (inner[i] == '>' || inner[i] == ')' || inner[i] == ']') {
          balance--;
        } else if (inner[i] == ',' && balance == 0) {
          std::string field = inner.substr(start, i - start);
          size_t f = field.find_first_not_of(" \t\r\n");
          size_t l = field.find_last_not_of(" \t\r\n");
          if (f != std::string::npos) {
            field = field.substr(f, l - f + 1);
            if (!field.empty()) {
              size_t colon = field.find(':');
              if (colon != std::string::npos) {
                std::string fieldName = field.substr(0, colon);
                std::string fieldType = field.substr(colon + 1);
                size_t fn_f = fieldName.find_first_not_of(" \t\r\n");
                size_t fn_l = fieldName.find_last_not_of(" \t\r\n");
                if (fn_f != std::string::npos) fieldName = fieldName.substr(fn_f, fn_l - fn_f + 1);
                size_t ft_f = fieldType.find_first_not_of(" \t\r\n");
                size_t ft_l = fieldType.find_last_not_of(" \t\r\n");
                if (ft_f != std::string::npos) fieldType = fieldType.substr(ft_f, ft_l - ft_f + 1);
                
                ShapeMember sm;
                sm.Name = fieldName;
                sm.Type = fieldType;
                sm.ResolvedType = resolveType(toka::Type::fromString(fieldType), force);
                members.push_back(sm);
              }
            }
          }
          start = i + 1;
        }
      }
      if (start < inner.size()) {
        std::string field = inner.substr(start);
        size_t f = field.find_first_not_of(" \t\r\n");
        size_t l = field.find_last_not_of(" \t\r\n");
        if (f != std::string::npos) {
          field = field.substr(f, l - f + 1);
          if (!field.empty()) {
            size_t colon = field.find(':');
            if (colon != std::string::npos) {
              std::string fieldName = field.substr(0, colon);
              std::string fieldType = field.substr(colon + 1);
              size_t fn_f = fieldName.find_first_not_of(" \t\r\n");
              size_t fn_l = fieldName.find_last_not_of(" \t\r\n");
              if (fn_f != std::string::npos) fieldName = fieldName.substr(fn_f, fn_l - fn_f + 1);
              size_t ft_f = fieldType.find_first_not_of(" \t\r\n");
              size_t ft_l = fieldType.find_last_not_of(" \t\r\n");
              if (ft_f != std::string::npos) fieldType = fieldType.substr(ft_f, ft_l - ft_f + 1);
              
              ShapeMember sm;
              sm.Name = fieldName;
              sm.Type = fieldType;
              sm.ResolvedType = resolveType(toka::Type::fromString(fieldType), force);
              members.push_back(sm);
            }
          }
        }
      }

      std::string UniqueName = "__Toka_Anon_Rec_" + std::to_string(AnonRecordCounter++);
      auto SyntheticShape = std::make_unique<ShapeDecl>(
          false, UniqueName, std::vector<GenericParam>{}, ShapeKind::Struct,
          members);
      ShapeMap[UniqueName] = SyntheticShape.get();
      SyntheticShapes.push_back(std::move(SyntheticShape));
      
      auto resolvedShapeType = std::make_shared<ShapeType>(UniqueName);
      resolvedShapeType->resolve(ShapeMap[UniqueName]);
      ParenthesizedRecordTypes[nameStr] = resolvedShapeType;
      return resolvedShapeType->withAttributes(type->IsWritable, type->IsNullable, type->IsBlocked);
    }

    // [NEW] Local Scope Alias Lookup (for T -> i32)
    if (CurrentScope) {
      SymbolInfo Sym;
      if (CurrentScope->lookup(shape->Name, Sym)) {
        if (Sym.IsTypeAlias && Sym.TypeObj) {
          // We found T -> i32 (TypeObj).
          auto resolved = resolveType(Sym.TypeObj, force);
          return resolved->withAttributes(type->IsWritable, type->IsNullable);
        }
      }
    }

    // [FIX] Check for Aliases (including Generic Aliases) BEFORE finding
    // Shape Template
    if (TypeAliasMap.count(shape->Name)) {
      const auto &aliasInfo = TypeAliasMap[shape->Name];
      if (!shape->GenericArgs.empty() && !aliasInfo.GenericParams.empty()) {
        // It is a Generic Alias Instantiation (e.g. Vec<i32>)
        // We must perform substitution similarly to string-based resolveType
        std::string result =
            aliasInfo.Target; // string template e.g. "GenericNode<T>"
        // Map params to args
        // Args are Type objects. toString() gives us representation.
        // We should resolve args first?
        // Or trust string substitution handles it?
        // Let's resolve args first.
        for (auto &Arg : shape->GenericArgs) {
          Arg = resolveType(Arg, force);
        }

        std::vector<std::string> argStrings;
        for (auto &Arg : shape->GenericArgs) {
          argStrings.push_back(Arg->toString());
        }

        if (aliasInfo.GenericParams.size() == argStrings.size()) {
          for (size_t i = 0; i < aliasInfo.GenericParams.size(); ++i) {
            std::string param = aliasInfo.GenericParams[i].Name;
            std::string val = argStrings[i];
            size_t pos = 0;
            while ((pos = result.find(param, pos)) != std::string::npos) {
              bool startOk = (pos == 0) || !isalnum(result[pos - 1]) &&
                                               result[pos - 1] != '_';
              bool endOk = (pos + param.size() == result.size()) ||
                           !isalnum(result[pos + param.size()]) &&
                               result[pos + param.size()] != '_';
              if (startOk && endOk) {
                result.replace(pos, param.size(), val);
                pos += val.size();
              } else {
                pos += param.size();
              }
            }
          }

          // Check Strong vs Weak
          if (aliasInfo.IsStrong && !force) {
            // Clone Shape logic for Generic Strong Alias (e.g.
            // StrongNodeG<i32>) We must create a concrete ShapeDecl with a
            // mangled name so CodeGen can find it.

            // 1. Resolve Target (e.g. GenericNode<i32>)
            auto targetTy = resolveType(toka::Type::fromString(result), true);
            auto targetSh = std::dynamic_pointer_cast<ShapeType>(targetTy);

            if (targetSh && targetSh->Decl) {
              // 2. Mangle Name: AliasName_M_Args...
              std::string mangledName = shape->Name + "_M";
              for (auto argStr : argStrings) {
                for (char &c : argStr)
                  if (!isalnum(c) && c != '_')
                    c = '_';
                mangledName += "_" + argStr;
              }

              // 3. Clone and register if not exists
              if (!ShapeMap.count(mangledName)) {
                auto cloned = new ShapeDecl(*targetSh->Decl);
                cloned->Name = mangledName;
                cloned->GenericParams.clear(); // Concretized
                ShapeMap[mangledName] = cloned;
                SyntheticShapes.push_back(std::unique_ptr<ShapeDecl>(cloned));
              }

              auto newShape = std::make_shared<ShapeType>(mangledName);
              newShape->resolve(ShapeMap[mangledName]);

              auto res = std::dynamic_pointer_cast<toka::Type>(newShape);
              return res->withAttributes(type->IsWritable, type->IsNullable);
            }

            return shape;
          }
          return resolveType(toka::Type::fromString(result), force);
        }
      }
    }

    // [NEW] Monomorphization Trigger
    if (!shape->GenericArgs.empty()) {
      // 1. Resolve arguments first
      for (auto &Arg : shape->GenericArgs) {
        Arg = resolveType(Arg, force);
      }
      // 2. Instantiate
      return instantiateGenericShape(shape);
    }

    size_t scopePos = shape->Name.find("::");
    if (scopePos != std::string::npos) {
      std::string ModName = shape->Name.substr(0, scopePos);
      std::string TargetType = shape->Name.substr(scopePos + 2);
      SymbolInfo *modSpecPtr = nullptr;
      std::string actualModName = ModName;
      if (CurrentScope && CurrentScope->findVariableWithDeref(ModName, modSpecPtr, actualModName) &&
          modSpecPtr->ReferencedModule) {
        SymbolInfo modSpec = *modSpecPtr;
        modSpecPtr->HasBeenUsed = true;
        if (modSpecPtr->ImportingDecl) {
          const_cast<ImportDecl*>(modSpecPtr->ImportingDecl)->HasBeenUsed = true;
        }
        ModuleScope *target = (ModuleScope *)modSpec.ReferencedModule;
        if (target->TypeAliases.count(TargetType)) {
          auto &aliasInfo = target->TypeAliases[TargetType];
          if (!aliasInfo.IsStrong || force) {
            auto resolved = toka::Type::fromString(aliasInfo.Target);
            return resolveType(
                resolved->withAttributes(type->IsWritable, type->IsNullable),
                force);
          }
        }
      }
    }

    if (TypeAliasMap.count(shape->Name)) {
      const auto &aliasInfo = TypeAliasMap[shape->Name];
      if (!aliasInfo.IsStrong) {
        // [Weak Alias] Transparent Synonym
        auto resolved = toka::Type::fromString(aliasInfo.Target);
        if (auto resShape = std::dynamic_pointer_cast<ShapeType>(resolved)) {
          if (!shape->GenericArgs.empty())
            resShape->GenericArgs = shape->GenericArgs;
        }
        return resolveType(
            resolved->withAttributes(type->IsWritable, type->IsNullable), true);
      } else {
        // [Strong Alias] Isolated Identity with Cloned Structure
        if (!force && !ShapeMap.count(shape->Name)) {
          auto targetTy =
              resolveType(toka::Type::fromString(aliasInfo.Target), true);
          if (auto targetSh = std::dynamic_pointer_cast<ShapeType>(targetTy)) {
            if (targetSh->Decl) {
              auto cloned = new ShapeDecl(*targetSh->Decl);
              cloned->Name = shape->Name;
              ShapeMap[cloned->Name] = cloned;
              SyntheticShapes.push_back(std::unique_ptr<ShapeDecl>(cloned));
            }
          }
        }
        if (!force) {
          if (ShapeMap.count(shape->Name)) {
            shape->resolve(ShapeMap[shape->Name]);
            return shape;
          }
          return shape;
        }
        return resolveType(toka::Type::fromString(aliasInfo.Target), true);
      }
    }

    if (ShapeMap.count(shape->Name)) {
      shape->resolve(ShapeMap[shape->Name]);
    }
  }

  if (auto prim = std::dynamic_pointer_cast<toka::PrimitiveType>(type)) {
    if (TypeAliasMap.count(prim->Name)) {
      const auto &info = TypeAliasMap[prim->Name];
      if (!info.IsStrong || force) {
        auto resolved = toka::Type::fromString(info.Target);
        return resolveType(
            resolved->withAttributes(type->IsWritable, type->IsNullable),
            force);
      }
      return prim;
    }
  }

  // Primitives can also be aliased potentially? Or just shapes.
  // currently Type::fromString parses unknown as ShapeType so this covers
  // aliases.
  return type;
}

std::shared_ptr<toka::Type>
Sema::instantiateGenericShape(std::shared_ptr<ShapeType> GenericShape) {
  if (!GenericShape)
    return GenericShape;

  // 1. Find the Template
  std::string templateName = GenericShape->Name;
  if (!ShapeMap.count(templateName)) {
    // Maybe alias logic here, but let's assume direct lookup first
    return GenericShape;
  }
  ShapeDecl *Template = ShapeMap[templateName];
  if (Template->GenericParams.empty()) {
    // Not a generic template, why args?
    // Error or ignore? Error.
    // For now, return generic shape (unresolved) or error
    return GenericShape;
  }

  if (Template->GenericParams.size() != GenericShape->GenericArgs.size()) {
    // Error: Arity mismatch
    return GenericShape;
  }

  // [NEW] Check Trait Bounds and Morphic Exemption
  for (size_t i = 0; i < Template->GenericParams.size(); ++i) {
    auto &Param = Template->GenericParams[i];
    auto ArgType = GenericShape->GenericArgs[i];

    // Morphic constraint check
    if (!Param.IsMorphic) {
      if (ArgType->isRawPointer() || ArgType->isUniquePtr() || ArgType->isSharedPtr() || ArgType->isReference()) {
        DiagnosticEngine::report(Template->Loc, DiagID::ERR_MORPHIC_CONSTRAINT, Param.Name, Param.Name);
        return GenericShape;
      }
    }

    if (!Param.TraitBounds.empty()) {
      if (!checkTraitBounds(Template->Loc, Param.Name, Param.TraitBounds, ArgType->toString())) {
        return nullptr;
      }
    }
  }

  // 2. Mangle Name: Name_M_Arg1_Arg2
  // Simple mangling: Name_M + (Arg1.Name or Arg1.Mangling)
  // We need a robust mangler. For Proof of Concept:
  std::string mangledName = templateName + "_M";
  for (auto &Arg : GenericShape->GenericArgs) {
    mangledName += "_" + Arg->getMangledName();
  }

  // 3. Check Cache
  // We use ShapeMap as the primary cache for instantiated decls.
  // We also have a GenericShapeCache if we want to return the same ShapeType
  // object too.
  if (GenericShapeCache.count(mangledName)) {
    return std::dynamic_pointer_cast<ShapeType>(
        GenericShapeCache[mangledName]->withAttributes(
            GenericShape->IsWritable, GenericShape->IsNullable));
  }

  // 4. Instantiate (Cache-First Cycle Breaking)
  // Create partial decl first to allow recursion
  auto NewDecl = std::make_unique<ShapeDecl>(
      Template->IsPub, mangledName, std::vector<GenericParam>{}, Template->Kind,
      std::vector<ShapeMember>{}, Template->IsPacked);
  NewDecl->Loc = Template->Loc;

  ShapeDecl *storedDecl = NewDecl.get();
  // Register IMMEDIATELY in Sema's primary map for resolution
  ShapeMap[mangledName] = storedDecl;

  // Add to CurrentModule to ensure CodeGen visibility
  GenericInstancesModule->Shapes.push_back(std::move(NewDecl));

  auto NewShapeTy = std::make_shared<toka::ShapeType>(mangledName);
  NewShapeTy->Decl = storedDecl;
  GenericShapeCache[mangledName] = NewShapeTy; // Cache base version

  // Now resolve members with recursion enabled using substMap...
  // Wait, we need to return ResultTy but the members are in storedDecl.
  // storedDecl is shared by all attributes versions. Correct.
  std::vector<ShapeMember> newMembers;
  std::map<std::string, std::shared_ptr<toka::Type>> substMap;

  for (size_t i = 0; i < Template->GenericParams.size(); ++i) {
    std::string k = Template->GenericParams[i].Name;
    substMap[k] = GenericShape->GenericArgs[i];
    if (!k.empty() && k[0] == '\'') substMap[k.substr(1)] = GenericShape->GenericArgs[i];
  }

  for (auto &oldMember : Template->Members) {
    ShapeMember newM = oldMember;

    newM.Type = oldMember.Type; // Wait, actually resolveMember parses `m.Type` and we substitute there! We don't need any pre-computation here.
    newMembers.push_back(std::move(newM));
  }

  // Update members of the already-registered decl
  storedDecl->Members = std::move(newMembers);

  // Recursively analyze the new shape (resolve members fully)
  // We manually run the resolution logic that analyzeShapes does
  for (auto &member : storedDecl->Members) {
    auto resolveMember = [&](ShapeMember &m) {
      if (m.ResolvedType)
        return;

      std::string prefix = "";
      if (m.IsShared)
        prefix += "~";
      else if (m.IsUnique)
        prefix += "^";
      else if (m.IsReference)
        prefix += "&";
      else if (m.IsRawPointer)
        prefix += "*";

      std::string fullTypeStr = m.Type;
      bool hasMorphology =
          morphKindFromTypeString(fullTypeStr) != MorphKind::None;
      // Preserve legacy behavior: a leading nul marker means the type string
      // already carries its own outer spelling and should not be prefixed.
      if (!hasMorphology && fullTypeStr.rfind("nul ", 0) == 0)
        hasMorphology = true;
      if (!hasMorphology) {
        fullTypeStr = prefix + m.Type;
      }
      
      // [NEW] Structural Substitution
      auto memberTypeObj = toka::Type::fromString(fullTypeStr);
      auto subObj = memberTypeObj->substitute(substMap);
      std::string newStr = subObj->toString();
      fullTypeStr = newStr;
      
      // Update m.Type to ensure downstream logic (e.g. CodeGen) perceives the substituted template type
      m.Type = fullTypeStr;

      std::string resolvedName = resolveType(fullTypeStr);
      m.ResolvedType = toka::Type::fromString(resolvedName);
    };

    // [NEW] Handle Nested Substitution for SubMembers (Variants)
    for (auto &sub : member.SubMembers) {
      resolveMember(sub);
    }

    resolveMember(member);
  }

  auto instance = std::make_shared<ShapeType>(mangledName);
  instance->resolve(storedDecl);

  // [NEW] Late Validation for Generic Union Instantiation
  // We must re-run the union safety checks ("Latent Blacklist Check")
  // because T might have been substituted with a forbidden type (e.g.
  // Union<bool>).
  if (storedDecl->Kind == ShapeKind::Union) {
    for (const auto &memb : storedDecl->Members) {
      if (!memb.ResolvedType)
        continue;

      // 1. Check for Forbidden Primitive Types (bool, strict enum)
      auto underlying = getDeepestUnderlyingType(memb.ResolvedType);
      bool invalid = false;
      std::string reason = "";

      if (underlying->isBoolean()) {
        invalid = true;
        reason = "bool";
      } else if (auto st =
                     std::dynamic_pointer_cast<toka::ShapeType>(underlying)) {
        // Naive Check for Strict Enum (without full ShapeMap lookup if easy)
        // We can access ShapeMap from Sema
        if (ShapeMap.count(st->Name)) {
          ShapeDecl *SD = ShapeMap[st->Name];
          if (SD->Kind == ShapeKind::Enum && !SD->IsPacked) {
            invalid = true;
            reason = "strict enum";
          }
        }
      }

      if (invalid) {
        DiagnosticEngine::report(getLoc(storedDecl),
                                 DiagID::ERR_UNION_INVALID_MEMBER, memb.Name,
                                 memb.Type, reason);
        HasError = true;
      }

      // 2. Check for Pointer Morphology (&^~*)
      bool isPointer = false;
      if (memb.IsUnique || memb.IsShared || memb.IsRawPointer ||
          memb.IsReference || memb.ResolvedType->isPointer()) {
        isPointer = true;
      }

      if (isPointer) {
        DiagnosticEngine::report(getLoc(storedDecl),
                                 DiagID::ERR_UNION_RESOURCE_TYPE, memb.Name,
                                 memb.Type);
        DiagnosticEngine::report(getLoc(storedDecl),
                                 DiagID::NOTE_UNION_RESOURCE_TIP, memb.Type);
        HasError = true;
      }
    }
  }

  // [NEW] Synchronous Impl Instantiation
  // If this shape has generic impls, instantiate them all now so that
  // m_ShapeProps (HasDrop) and MethodMap are populated before sovereignty
  // checks.
  // [FIX] Moved here to ensure storedDecl->Members is populated first.
  if (GenericImplMap.count(templateName)) {
    for (auto *ImplTemplate : GenericImplMap[templateName]) {
      instantiateGenericImpl(ImplTemplate, mangledName,
                             GenericShape->GenericArgs);
    }
  }
  return std::dynamic_pointer_cast<ShapeType>(NewShapeTy->withAttributes(
      GenericShape->IsWritable, GenericShape->IsNullable));
}

bool Sema::isTypeCompatible(std::shared_ptr<toka::Type> Target,
                            std::shared_ptr<toka::Type> Source) {
  if (!Target || !Source)
    return false;

  if (Target->isUnknown() || Source->isUnknown())
    return true;

  // [CORE] Strong Type Wall: Strict Name Identity (Shapes only)
  bool isTShape =
      Target->isShape() || (Target->isPointer() && Target->getPointeeType() &&
                            Target->getPointeeType()->isShape());
  bool isSShape =
      Source->isShape() || (Source->isPointer() && Source->getPointeeType() &&
                            Source->getPointeeType()->isShape());

  if (isTShape && isSShape) {
    std::string tName = toka::Type::stripMorphology(Target->getSoulName());
    std::string sName = toka::Type::stripMorphology(Source->getSoulName());

    if (TypeAliasMap.count(tName) && TypeAliasMap[tName].IsStrong) {
      if (tName != sName) {
        return false;
      }
    }
    if (TypeAliasMap.count(sName) && TypeAliasMap[sName].IsStrong) {
      if (tName != sName) {
        // <<
        // "\n";
        return false;
      }
    }
  }

  // [NEW] Canonicalize types before comparison
  auto T = resolveType(Target, false);
  auto S = resolveType(Source, false);

  bool isTAnon = isAnonymousRecord(T);
  bool isSAnon = isAnonymousRecord(S);

  if (isTAnon || isSAnon) {
    if (isTAnon && isSAnon) {
      // Rule 1: Both are Anon, structural check (bidirectional/order-equal compatibility)
      std::string tName = T->getSoulName();
      std::string sName = S->getSoulName();
      return areStructsStructurallyCompatible(this, tName, sName);
    }
    if (!isTAnon && isSAnon) {
      // Rule 2: Nominal Target, Anon Source: single-direction coercion
      if (T->isShape()) {
        std::string tName = T->getSoulName();
        std::string sName = S->getSoulName();
        return areStructsStructurallyCompatible(this, tName, sName);
      }
      return false;
    }
    // Rule 3: Anon Target, Nominal Source: ALWAYS FALSE
    return false;
  }

  // Identity: For strong aliases, this is the final authority.
  if (T->equals(*S))
    return true;

  // [Fix] Value Compatibility: T# is compatible with T (and vice versa) for
  // non-indirection types.
  if (T->typeKind == S->typeKind &&
      (T->typeKind == Type::Primitive || T->typeKind == Type::Shape)) {
    if (T->getSoulName() == S->getSoulName())
      return true;
  }

  // Check if one resolved to the other (Weak alias resolution check)
  // If T is a weak alias, resolveType(T, true) will get its target.
  // But wait, resolveType(Target, false) already resolves weak aliases.
  // We only need additional structural checks for non-alias types.

  // [NEW] FunctionType/DynFnType and Closure Compatibility
  if ((T->typeKind == toka::Type::Function || T->typeKind == toka::Type::DynFn) && S->typeKind == toka::Type::Shape) {
    bool isDynFn = T->typeKind == toka::Type::DynFn;
    std::vector<std::shared_ptr<Type>> paramTypes;
    std::shared_ptr<Type> returnType;
    
    if (isDynFn) {
        auto tFn = std::static_pointer_cast<toka::DynFnType>(T);
        paramTypes = tFn->ParamTypes;
        returnType = tFn->ReturnType;
    } else {
        auto tFn = std::static_pointer_cast<toka::FunctionType>(T);
        paramTypes = tFn->ParamTypes;
        returnType = tFn->ReturnType;
    }
    
    auto sSh = std::static_pointer_cast<toka::ShapeType>(S);
    if (sSh->Name.find("__Closure_") == 0) {
      if (MethodDecls.count(sSh->Name) && MethodDecls[sSh->Name].count("__invoke")) {
        auto *invokeFn = MethodDecls[sSh->Name]["__invoke"];
        if (paramTypes.size() == invokeFn->Args.size() - 1) {
          bool ok = true;
          for (size_t i = 0; i < paramTypes.size(); ++i) {
            auto expectedArg = paramTypes[i];
            auto actualArg = resolveType(toka::Type::fromString(invokeFn->Args[i + 1].Type), false);
            if (!isTypeCompatible(expectedArg, actualArg)) {
              ok = false;
              break;
            }
          }
          if (ok) {
            auto sRet = resolveType(invokeFn->ResolvedReturnType ? invokeFn->ResolvedReturnType : toka::Type::fromString(invokeFn->ReturnType), false);
            if (isTypeCompatible(returnType, sRet)) {
              return true;
            }
          }
        }
      }
    }
  }

  // Dynamic Trait Coercion (Unsizing)
  // Check if Target is "dyn @Trait"
  if (auto tShape = std::dynamic_pointer_cast<toka::ShapeType>(T)) {
    std::string tName = tShape->Name;
    if (tName.size() >= 4 && tName.substr(0, 3) == "dyn") {
      std::string traitName = "";
      if (tName.rfind("dyn @", 0) == 0)
        traitName = tName.substr(5);
      else if (tName.rfind("dyn@", 0) == 0)
        traitName = tName.substr(4);

      if (!traitName.empty()) {
        // Get Soul Type Name from Source
        std::string sName = "";
        auto inner = S;
        // Strip pointers to find soul
        while (inner->isPointer()) {
          if (auto ptr = std::dynamic_pointer_cast<toka::PointerType>(inner))
            inner = ptr->getPointeeType();
          else
            break;
        }

        if (auto sPrim = std::dynamic_pointer_cast<toka::PrimitiveType>(inner))
          sName = sPrim->Name;
        else if (auto sShape =
                     std::dynamic_pointer_cast<toka::ShapeType>(inner))
          sName = sShape->Name;

        if (!sName.empty()) {
          std::string implKey = sName + "@" + traitName;
          if (ImplMap.count(implKey))
            return true;
        }
      }
    }
  }

  // Numeric Widening (Lossless)
  auto getNumericBitWidth = [](const std::string& name) -> int {
    if (name == "i8" || name == "u8" || name == "char") return 8;
    if (name == "i16" || name == "u16") return 16;
    if (name == "i32" || name == "u32" || name == "f32") return 32;
    if (name == "i64" || name == "u64" || name == "f64") return 64;
    if (name == "usize" || name == "isize" || name == "Addr" || name == "OAddr") {
      if (!toka::Parser::TargetTriple.empty()) {
        std::string triple = toka::Parser::TargetTriple;
        bool is32 = (triple.find("wasm32") != std::string::npos ||
                     triple.find("i386") != std::string::npos ||
                     triple.find("i686") != std::string::npos ||
                     (triple.find("arm") != std::string::npos && triple.find("64") == std::string::npos && triple.find("armv8") == std::string::npos));
        if (is32) return 32;
      }
      return 64;
    }
    return 0;
  };

  auto primT = std::dynamic_pointer_cast<toka::PrimitiveType>(T);
  auto primS = std::dynamic_pointer_cast<toka::PrimitiveType>(S);
  if (primT && primS) {
    if (primT->isInteger() && primS->isInteger()) {
      int sW = getNumericBitWidth(primS->Name);
      int tW = getNumericBitWidth(primT->Name);
      bool sSigned = primS->isSignedInteger();
      bool tSigned = primT->isSignedInteger();

      if (sW > 0 && tW > 0) {
        // Safe implicit widening conditions:
        // 1. Target width must be strictly larger than Source width (equal width relies on identical Type matching earlier)
        // 2. Cannot cast from Signed to Unsigned implicitly (since negative values wrap to huge positive values)
        if (tW > sW && (!sSigned || tSigned)) {
          return true;
        }
      }
    }
    
    if (primT->isFloatingPoint() && primS->isFloatingPoint()) {
      int sW = getNumericBitWidth(primS->Name);
      int tW = getNumericBitWidth(primT->Name);
      if (sW > 0 && tW > 0 && tW > sW) {
        return true; 
      }
    }

  }

  // String literal (str) to pointer (*char) decay
  if (primS && primS->Name == "str") {
    // T could be *i8 or *u8 or *char
    if (auto ptr = std::dynamic_pointer_cast<toka::PointerType>(T)) {
      if (auto pte = std::dynamic_pointer_cast<toka::PrimitiveType>(
              ptr->getPointeeType())) {
        if (pte->Name == "i8" || pte->Name == "u8" || pte->Name == "char")
          return true;
      }
    }
  }

  // cstr shape to pointer decay (FFI zero-friction)
  if (Source->isShape() && Source->getSoulName() == "cstr") {
    if (auto ptr = std::dynamic_pointer_cast<toka::PointerType>(T)) {
      if (auto pte = std::dynamic_pointer_cast<toka::PrimitiveType>(
              ptr->getPointeeType())) {
        if (pte->Name == "i8" || pte->Name == "u8" || pte->Name == "char")
          return true;
      }
    }
  }

  // 0. [Toka 1.3] Morphology-Based Permission Decay & Nullability Covariance
  // "ReadOnly Target is compatible with Writable Source of same morphology."
  // "Nullable Target is compatible with Non-Nullable Source of same
  // morphology."
  bool bothPointers = Target->isPointer() && Source->isPointer();
  if (bothPointers) {
    auto targetPtr = std::dynamic_pointer_cast<toka::PointerType>(Target);
    auto sourcePtr = std::dynamic_pointer_cast<toka::PointerType>(Source);

    // Rule 1: Morphologies MUST match exactly in Toka 1.3
    if (targetPtr->typeKind == sourcePtr->typeKind) {
      // Rule 2: Pointee Types must be compatible
      if (isTypeCompatible(targetPtr->getPointeeType(),
                           sourcePtr->getPointeeType())) {
        // Rule 3: Permission Decay (Source Writable -> Target ReadOnly)
        bool permissionMatch =
            (sourcePtr->IsWritable || !targetPtr->IsWritable);
        bool nullabilityMatch =
            (targetPtr->IsNullable || !sourcePtr->IsNullable);

        if (permissionMatch && nullabilityMatch)
          return true;
      }
    }
  }


  // 1. Array to Pointer Decay (e.g. [10]i32 -> *i32)
  if (auto ptrT = std::dynamic_pointer_cast<toka::PointerType>(T)) {
    if (auto arrS = std::dynamic_pointer_cast<toka::ArrayType>(S)) {
      if (ptrT->getPointeeType()->isCompatibleWith(
              *arrS->getArrayElementType())) {
        return true;
      }
    }
  }

  // 2. Nullability Covariance: T is compatible with T?
  // (A non-null value can be assigned to a nullable slot)
  // Check if Target is Nullable (Implicitly via name/attribute or
  // Explicitly
  // ?)
  bool targetNullable = Target->IsNullable;
  // Should check specific pointer types too, but let's look at the objects.

  // 3. Implicit Dereference (Reference -> Value)
  // If Source is Reference (&T) and Target is Value (T), allow if T is
  // compat. Note: Sema doesn't strictly track "is copyable" yet, so we
  // allow it generically. CodeGen handles the load.
  if (auto refS = std::dynamic_pointer_cast<toka::ReferenceType>(S)) {
    // Check if Target is NOT a reference
    if (!std::dynamic_pointer_cast<toka::ReferenceType>(T)) {
      // Source &T, Target T. Check compatibility of Inner(S) and T.
      if (isTypeCompatible(Target, refS->getPointeeType())) {
        return true;
      }
    }
  }

  // 4. Writability Stripping (T# compatible with T)
  // Used for passing mutable variables to immutable args.
  // S->withAttributes(false, ...) effectively strips writability logic from
  // comparison.
  // 4. Writability Stripping (T# compatible with T)
  auto cleanT = Target->withAttributes(false, Target->IsNullable);
  auto cleanS = Source->withAttributes(false, Source->IsNullable);
  if (cleanT->equals(*cleanS)) {
      bool isIndirection = Target->isPointer() || Target->isReference() || Target->isSmartPointer();
      if (isIndirection && Target->IsWritable && !Source->IsWritable) {
          // It's technically true they match in base type, but Target demands mutability
          // that Source does not have. This is illegal for pointers/references!
          return false;
      }
      return true;
  }

  // 5. Pointer Nullability Subtyping (*Data compatible with *?Data)
  if (auto ptrT = std::dynamic_pointer_cast<toka::PointerType>(Target)) {
    if (auto ptrS = std::dynamic_pointer_cast<toka::PointerType>(Source)) {
      if (ptrS->getPointeeType()->equals(*ptrT->getPointeeType())) {
        // Same Pointee. Check nullability.
        // We assume subtyping: NonNullable <: Nullable
        // So if Target is Nullable, Source can be anything.
        if (ptrT->IsNullable)
          return true; // *?T accepts *T or *?T
        if (!ptrS->IsNullable)
          return true; // *T accepts *T
        // Fallback: Raw pointers allow *?T -> *T (Unsafe) if
        // Type::isCompatibleWith allows it. But we handle it there.
      }
    }
  }

  // 6. Universal Null Compatibility
  // null is compatible with any pointer or smart pointer
  if (S->isNullType()) {
    if (T->isPointer() || T->isSmartPointer() || T->isReference()) {
      if (T->IsNullable)
        return true;
    }
  }
  if (T->isNullType()) {
    if (S->isPointer() || S->isSmartPointer() || S->isReference())
      return true;
  }

  // [Chapter 6 Extension] Nullable Soul Compatibility (none -> T?)
  if ((S->isVoid() || S->isNullType()) && Target->IsNullable && !Target->isPointer() &&
      !Target->isSmartPointer() && !Target->isReference()) {
    return true;
  }



  // NOTE: Trait coercion (dyn) is omitted for briefness/complexity, will
  // rely upon resolveType logic or add later. The original string logic had
  // it. For Coexistence, we might skip it if not used in current tests, OR
  // add it. Original logic checked string "dyn". `Type::fromString` parses
  // "dyn Shape" as ShapeType("dyn Shape")? No, `dyn @Shape`. `fromString`
  // fallback: ShapeType("dyn @Shape"). So we can check name.

  // Use core compatibility (Source flows to Target)
  return S->isCompatibleWith(*T);
}



std::shared_ptr<toka::Type>
Sema::getDeepestUnderlyingType(std::shared_ptr<toka::Type> type) {
  if (!type)
    return nullptr;

  auto current = type;
  // Limit recursion to avoid infinite loops
  for (int i = 0; i < 20; ++i) {
    if (auto s = std::dynamic_pointer_cast<toka::ShapeType>(current)) {
      if (TypeAliasMap.count(s->Name)) {
        std::string targetStr = TypeAliasMap[s->Name].Target;
        auto targetObj = toka::Type::fromString(resolveType(targetStr));
        if (targetObj) {
          current = targetObj;
          continue;
        }
      }
    }
    break;
  }
  return resolveType(current);
}

uint64_t Sema::getTypeSize(std::shared_ptr<toka::Type> t) {
  if (!t)
    return 0;
  if (t->isBoolean())
    return 1;
    
  uint64_t ptrSize = 8;
  if (!toka::Parser::TargetTriple.empty()) {
    std::string triple = toka::Parser::TargetTriple;
    bool is32 = (triple.find("wasm32") != std::string::npos ||
                 triple.find("i386") != std::string::npos ||
                 triple.find("i686") != std::string::npos ||
                 (triple.find("arm") != std::string::npos && triple.find("64") == std::string::npos && triple.find("armv8") == std::string::npos));
    if (is32) {
      ptrSize = 4;
    }
  }

  if (auto prim = std::dynamic_pointer_cast<toka::PrimitiveType>(t)) {
    if (prim->Name == "u8" || prim->Name == "i8") return 1;
    if (prim->Name == "u16" || prim->Name == "i16") return 2;
    if (prim->Name == "u32" || prim->Name == "i32" || prim->Name == "f32" || prim->Name == "char") return 4;
    if (prim->Name == "u64" || prim->Name == "i64" || prim->Name == "f64") return 8;
    if (prim->Name == "usize" || prim->Name == "isize") return ptrSize;
  }
  if (t->isPointer() || t->isReference())
    return ptrSize;
  if (t->isArray()) {
    auto arr = std::dynamic_pointer_cast<toka::ArrayType>(t);
    return arr->Size * getTypeSize(arr->ElementType);
  }
  if (auto st = std::dynamic_pointer_cast<toka::ShapeType>(t)) {
    ShapeDecl *Decl = st->Decl;
    if (!Decl && ShapeMap.count(st->Name))
      Decl = ShapeMap[st->Name];
    if (Decl) {
      if (Decl->Kind == ShapeKind::Union) {
        uint64_t maxS = 0;
        for (auto &m : Decl->Members) {
          uint64_t s = getTypeSize(
              m.ResolvedType ? m.ResolvedType
                             : toka::Type::fromString(resolveType(m.Type)));
          if (s > maxS)
            maxS = s;
        }
        return maxS;
      } else if (Decl->Kind == ShapeKind::Struct) {
        uint64_t sum = 0;
        for (auto &m : Decl->Members) {
          sum += getTypeSize(m.ResolvedType);
        }
        return sum;
      }
    }
  }
  return 0; // Unknown
}

} // namespace toka
