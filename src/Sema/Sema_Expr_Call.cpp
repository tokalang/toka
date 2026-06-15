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
#include "toka/SourceManager.h"
#include "toka/Type.h"
#include <algorithm>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace toka {

static SourceLocation getLoc(ASTNode *Node) { return Node->Loc; }

static std::string getStringifyPath(Expr *E) {
  if (!E)
    return "";
  if (auto *ve = dynamic_cast<VariableExpr *>(E)) {
    return ve->Name;
  }
  if (auto *me = dynamic_cast<MemberExpr *>(E)) {
    std::string member = toka::Type::stripMorphology(me->Member);
    return getStringifyPath(me->Object.get()) + "." + member;
  }
  if (auto *ue = dynamic_cast<UnaryExpr *>(E)) {
    return getStringifyPath(ue->RHS.get());
  }
  if (auto *ae = dynamic_cast<AddressOfExpr *>(E)) {
    return getStringifyPath(ae->Expression.get());
  }
  if (auto *ae = dynamic_cast<ArrayIndexExpr *>(E)) {
    return getStringifyPath(ae->Array.get());
  }
  if (auto *ce = dynamic_cast<CastExpr *>(E)) {
    return getStringifyPath(ce->Expression.get());
  }
  return "";
}

// Stage 5c: Object-Oriented Call Expression Check
std::shared_ptr<toka::Type> Sema::checkCallExpr(CallExpr *Call) {

  std::string CallName = Call->Callee;
  std::string OriginalName = CallName;

  struct EffectRestorer {
      bool &ref;
      bool oldVal;
      EffectRestorer(bool &r) : ref(r), oldVal(r) {}
      ~EffectRestorer() { ref = oldVal; }
  } _restorer(m_IsConsumingEffect);
  if (CallName == "block_on" || CallName == "std/task::block_on") {
      m_IsConsumingEffect = true;
  }

  // 1. Primitives (Constructors/Casts) e.g. i32(42)
  if (CallName == "i32" || CallName == "u32" || CallName == "i64" ||
      CallName == "u64" || CallName == "f32" || CallName == "f64" ||
      CallName == "i16" || CallName == "u16" || CallName == "i8" ||
      CallName == "u8" || CallName == "usize" || CallName == "isize" ||
      CallName == "bool") {
    for (auto &Arg : Call->Args) {
      Arg = foldGenericConstant(std::move(Arg)); // [FIX]
      checkExpr(Arg.get());
    }
    return toka::Type::fromString(CallName);
  }

  // 1b. Comptime compile_error
  if (CallName == "core/comptime::compile_error" || CallName == "compile_error") {
      if (Call->Args.size() == 1) {
          Call->Args[0] = foldGenericConstant(std::move(Call->Args[0]));
          std::string errStr;
          bool hasStr = false;
          if (auto constStr = dynamic_cast<StringExpr*>(Call->Args[0].get())) {
              errStr = constStr->Value;
              hasStr = true;
          } else if (auto constVStr = dynamic_cast<ViewStringExpr*>(Call->Args[0].get())) {
              errStr = constVStr->Value;
              hasStr = true;
          }
          if (hasStr) {
              error(Call, DiagID::ERR_SEMA_COMPILE_TIME_ERROR, errStr);
              return toka::Type::fromString("void");
          }
      }
      error(Call, DiagID::ERR_SEMA_COMPILE_ERROR_REQUIRES_A_STRING_LITERAL);
      return toka::Type::fromString("void");
  }

  // 1c. core/mem::bit_cast intrinsic
  if (CallName == "core/mem::bit_cast" || CallName == "bit_cast") {
      if (Call->Args.size() != 1) {
          error(Call, DiagID::ERR_SEMA_BIT_CAST_REQUIRES_EXACTLY_1_ARGUMENT);
          return toka::Type::fromString("unknown");
      }
      if (Call->GenericArgs.empty()) {
          error(Call, DiagID::ERR_SEMA_BIT_CAST_REQUIRES_THE_TARGET_TYPE_AS_A_GE);
          return toka::Type::fromString("unknown");
      }
      
      auto fromTy = checkExpr(Call->Args[0].get());
      if (!fromTy || fromTy->isUnknown()) {
          return toka::Type::fromString("unknown");
      }
      
      std::string toStr = Call->GenericArgs[0];
      auto toTy = resolveType(toka::Type::fromString(toStr));
      if (!toTy || toTy->isUnknown()) {
          error(Call, DiagID::ERR_SEMA_UNKNOWN_TARGET_TYPE_IN_BIT_CAST, toStr);
          return toka::Type::fromString("unknown");
      }
      
      uint64_t fromSize = getTypeSize(fromTy);
      uint64_t toSize = getTypeSize(toTy);
      if (fromSize != toSize) {
          DiagnosticEngine::report(getLoc(Call), DiagID::ERR_BITCAST_SIZE_MISMATCH,
                                   fromTy->toString(), fromSize, toTy->toString(), toSize);
          HasError = true;
          return toTy;
      }
      
      return toTy;
  }

  // 2. Intrinsics (println, print, String::fmt)
  bool isPrintlnLegacy = (CallName == "println_legacy" || CallName == "std::io::println_legacy" || CallName == "print_legacy" || CallName == "std::io::print_legacy");
  bool isPrintln = (CallName == "println" || CallName == "std::io::println" || CallName == "print" || CallName == "std::io::print");
  bool isStringFmt = (CallName == "String::fmt" || CallName == "std::string::String::fmt" || CallName == "string::fmt" || CallName == "std::string::string::fmt" || CallName == "fmt" || CallName == "std::string::fmt");

  // New non-magical zero-overhead println/print Sema validation
  if (isPrintln) {
      bool visible = false;
      if (CallName == "std::io::println" || CallName == "std::io::print") {
          visible = true;
      } else {
          SymbolInfo *symPtr = nullptr;
          std::string actualCallName = CallName;
          if (CurrentScope->findVariableWithDeref(CallName, symPtr, actualCallName)) {
              visible = true;
              symPtr->HasBeenUsed = true;
              if (symPtr->ImportingDecl) {
                  const_cast<ImportDecl*>(symPtr->ImportingDecl)->HasBeenUsed = true;
              }
          }
      }

      if (!visible) {
          DiagnosticEngine::report(getLoc(Call), DiagID::ERR_UNDECLARED, CallName);
          HasError = true;
          return toka::Type::fromString("unknown");
      }

      if (Call->Args.empty()) {
          error(Call, DiagID::ERR_SEMA_REQUIRES_AT_LEAST_A_FORMAT_STRING_ARGUMEN, CallName);
          return toka::Type::fromString("void");
      }

      Call->Args[0] = foldGenericConstant(std::move(Call->Args[0]));
      checkExpr(Call->Args[0].get());

      std::string fmt = "";
      if (auto *SE = dynamic_cast<StringExpr*>(Call->Args[0].get())) {
          fmt = SE->Value;
      } else if (auto *VSE = dynamic_cast<ViewStringExpr*>(Call->Args[0].get())) {
          fmt = VSE->Value;
      } else {
          error(Call->Args[0].get(), DiagID::ERR_SEMA_FORMAT_ARGUMENT_MUST_BE_A_STRING_LITERAL, CallName);
          return toka::Type::fromString("void");
      }

      std::vector<std::string> formatSpecifiers;
      size_t lastPos = 0;
      while (lastPos < fmt.size()) {
          size_t startPos = fmt.find('{', lastPos);
          if (startPos == std::string::npos) break;
          if (startPos + 1 < fmt.size() && fmt[startPos + 1] == '{') {
              lastPos = startPos + 2;
              continue;
          }
          size_t endPos = fmt.find('}', startPos + 1);
          if (endPos == std::string::npos) break;

          std::string specifier = fmt.substr(startPos + 1, endPos - startPos - 1);
          formatSpecifiers.push_back(specifier);
          lastPos = endPos + 1;
      }

      size_t expectedArgs = formatSpecifiers.size();
      size_t providedArgs = Call->Args.size() - 1;
      if (expectedArgs != providedArgs) {
          error(Call, DiagID::ERR_SEMA_FORMAT_STRING_PLACEHOLDER_COUNT_DOES_NOT, std::to_string(expectedArgs), std::to_string(providedArgs));
          return toka::Type::fromString("void");
      }

      for (size_t i = 1; i < Call->Args.size(); i++) {
          Call->Args[i] = foldGenericConstant(std::move(Call->Args[i]));
          auto argTyObj = checkExpr(Call->Args[i].get());
          if (!argTyObj) continue;

          if (argTyObj->isPointer()) {
              continue;
          }

          std::string argTy = argTyObj->getSoulName();
          auto soulTy = Type::stripMorphology(argTy);

          bool isFmt = false;
          std::string spec = formatSpecifiers[i - 1];
          if (!spec.empty() && spec[0] == ':') {
              isFmt = true;
              spec = spec.substr(1);
          }

          if (soulTy == "String" || soulTy == "string" || soulTy == "str") {
              if (isFmt) {
                  error(Call->Args[i].get(), DiagID::ERR_SEMA_FORMATTED_PRINTING_IS_NOT_YET_SUPPORTED_F);
              }
              continue;
          }

          bool isBuiltinPrintable = (soulTy == "i32" || soulTy == "f64" || soulTy == "bool" || soulTy == "char" || soulTy == "i64" || soulTy == "u32" || soulTy == "u64" || soulTy == "usize" || soulTy == "f32");
          if (isBuiltinPrintable && !isFmt) {
              continue;
          }

          std::string requiredMethod = isFmt ? "to_string_fmt" : "to_string";
          std::string requiredTrait = isFmt ? "@ToFormat" : "@ToString";

          if (!MethodMap.count(soulTy) || !MethodMap[soulTy].count(requiredMethod)) {
              error(Call->Args[i].get(), DiagID::ERR_SEMA_TYPE_DOES_NOT_IMPLEMENT_TRAIT_OR_METHOD, soulTy, requiredTrait, requiredMethod);
              return toka::Type::fromString("void");
          }
      }

      return toka::Type::fromString("void");
  }

  bool treatAsIntrinsic = false;
  if ((isPrintlnLegacy || isStringFmt) && !Call->Args.empty()) {
      if (dynamic_cast<StringExpr*>(Call->Args[0].get()) || dynamic_cast<ViewStringExpr*>(Call->Args[0].get())) {
          treatAsIntrinsic = true;
      }
  }

  if (treatAsIntrinsic) {
    bool visible = true;
    if (isPrintlnLegacy) {
      visible = (CallName == "std::io::println_legacy" || CallName == "std::io::print_legacy" || 
                 CallName == "println_legacy" || CallName == "print_legacy");
      if (!visible) {
          SymbolInfo *symPtr = nullptr;
          std::string actualCallName = CallName;
          if (CurrentScope->findVariableWithDeref(CallName, symPtr, actualCallName)) {
              visible = true;
              symPtr->HasBeenUsed = true;
              if (symPtr->ImportingDecl) {
                  const_cast<ImportDecl*>(symPtr->ImportingDecl)->HasBeenUsed = true;
              }
          }
      }
    }
    if (!visible) {
      error(Call, DiagID::ERR_SEMA_REQUIRES_AT_LEAST_A_FORMAT_STRING, CallName);
      return toka::Type::fromString("void");
    }
    for (auto &Arg : Call->Args) {
      Arg = foldGenericConstant(std::move(Arg)); // [FIX]
      checkExpr(Arg.get());
    }
    if (isStringFmt || isPrintlnLegacy) {
      std::vector<std::string> formatSpecifiers;
      if (auto *SE = dynamic_cast<StringExpr*>(Call->Args[0].get())) {
          std::string fmt = SE->Value;
          size_t lastPos = 0;
          while (lastPos < fmt.size()) {
              size_t startPos = fmt.find('{', lastPos);
              if (startPos == std::string::npos) break;
              if (startPos + 1 < fmt.size() && fmt[startPos + 1] == '{') {
                  lastPos = startPos + 2;
                  continue;
              }
              size_t endPos = fmt.find('}', startPos + 1);
              if (endPos == std::string::npos) break;
              
              std::string specifier = fmt.substr(startPos + 1, endPos - startPos - 1);
              formatSpecifiers.push_back(specifier);
              lastPos = endPos + 1;
          }
      }

      for (size_t i = 1; i < Call->Args.size(); i++) {
        auto argTyObj = Call->Args[i]->ResolvedType;
        std::string argTy = argTyObj ? argTyObj->getSoulName() : "";
        auto soulTy = Type::stripMorphology(argTy);
        
        bool isFmt = false;
        if (i - 1 < formatSpecifiers.size()) {
            std::string spec = formatSpecifiers[i - 1];
            if (!spec.empty() && spec[0] == ':') isFmt = true;
        }

        // [P3] Zero-copy println: Bypass to_string check for string types
        if (soulTy == "String" || soulTy == "string" || soulTy == "str") {
            if (isFmt) {
                error(Call->Args[i].get(), DiagID::ERR_SEMA_FORMATTED_PRINTING_IS_NOT_YET_SUPPORTED_F);
            }
            continue;
        }

        std::string requiredMethod = isFmt ? "to_string_fmt" : "to_string";
        std::string requiredTrait = isFmt ? "@ToFormat" : "@ToString";

        if (!MethodMap.count(soulTy) || !MethodMap[soulTy].count(requiredMethod)) {
            error(Call->Args[i].get(), DiagID::ERR_SEMA_TYPE_DOES_NOT_IMPLEMENT_TRAIT_OR_METHOD, soulTy, requiredTrait, requiredMethod);
            return toka::Type::fromString("void");
        }
      }
      auto strTy = resolveType(toka::Type::fromString("string"));
      if (!strTy) strTy = resolveType(toka::Type::fromString("String"));
      if (!strTy) strTy = resolveType(toka::Type::fromString("std::string::String"));
      if (!strTy) strTy = toka::Type::fromString("string");
      return strTy;
    }
    return toka::Type::fromString("void");
  }

  std::shared_ptr<toka::FunctionType> funcType = nullptr;

  // 3. Resolve Static Methods / Enum Variants
  size_t pos = CallName.find("::");
  if (pos != std::string::npos) {
    std::string RawPrefix = CallName.substr(0, pos);
    std::string ShapeName = resolveType(RawPrefix);
    // [FIX] resolveType might return a type string "SharedPtr_M_i32" or
    // similar. If it contains sigils, strip them for Map lookup.
    ShapeName = Type::stripMorphology(ShapeName);

    std::string VariantName = CallName.substr(pos + 2);

    if (ShapeMap.count(ShapeName)) {
      // Update CallName and Callee for subsequent lookup and CodeGen
      CallName = ShapeName + "::" + VariantName;
      Call->Callee = CallName;

      // Static Method
      if (MethodMap.count(ShapeName) &&
          MethodMap[ShapeName].count(VariantName)) {
        FunctionDecl *MetAST = nullptr;
        if (MethodDecls.count(ShapeName) && MethodDecls[ShapeName].count(VariantName)) {
            MetAST = MethodDecls[ShapeName][VariantName];
        }

        if (MetAST) {
            size_t expectedArgs = MetAST->Args.size();
            if (Call->Args.size() != expectedArgs && !MetAST->IsVariadic) {
                if (Call->Args.size() < expectedArgs) {
                    DiagnosticEngine::report(getLoc(Call), DiagID::ERR_SEMA_STATIC_METHOD_EXPECTS_AT_LEAST_ARGUMENTS, MetAST->Name, std::to_string(expectedArgs), std::to_string(Call->Args.size()));
                    HasError = true;
                }
            }
        }

        for (size_t i = 0; i < Call->Args.size(); ++i) {
          Call->Args[i] = foldGenericConstant(std::move(Call->Args[i])); // [FIX]
          std::shared_ptr<toka::Type> expectedTy = nullptr;
          if (MetAST && i < MetAST->Args.size()) {
              std::string tyStr = MetAST->Args[i].Type;
              if (MetAST->Args[i].IsRawPointer) tyStr = "*" + tyStr;
              else if (MetAST->Args[i].IsUnique) tyStr = "^" + tyStr;
              else if (MetAST->Args[i].IsShared) tyStr = "~" + tyStr;
              else if (MetAST->Args[i].IsReference) tyStr = "&" + tyStr;
              expectedTy = resolveType(toka::Type::fromString(tyStr), false);
          }
          auto argTy = checkExpr(Call->Args[i].get(), expectedTy);
          if (expectedTy && !isTypeCompatible(expectedTy, argTy)) {
              DiagnosticEngine::report(getLoc(Call->Args[i].get()), DiagID::ERR_TYPE_MISMATCH,
                                       "Argument " + std::to_string(i + 1) + " (actual: " + argTy->getSoulName() + ")", expectedTy->getSoulName(), argTy->getSoulName());
              HasError = true;
          }
        }
        auto retObj = toka::Type::fromString(MethodMap[ShapeName][VariantName]);
        auto resolvedRet = resolveType(retObj);
        
        // [FIX] Check if Static Method is async and wrap in TaskHandle
        if (MetAST && MetAST->Effect == EffectKind::Async) {
            std::string tName = "TaskHandle<" + resolvedRet->toString() + ">";
            return toka::Type::fromString(tName);
        }
        return resolvedRet;
      } else {
        // [NEW] Lazy Impl Instantiation
        std::string BaseName = RawPrefix;
        size_t lt = BaseName.find('<');
        if (lt != std::string::npos) {
          BaseName = BaseName.substr(0, lt);
          if (GenericImplMap.count(BaseName)) {
            // [FIX] Pass generic arguments to instantiateGenericImpl
            std::vector<std::shared_ptr<toka::Type>> genericArgs;
            auto parsed = Type::fromString(RawPrefix);
            if (auto *ST = dynamic_cast<ShapeType *>(parsed.get())) {
              genericArgs = ST->GenericArgs;
            }
            for (auto *ImplTemplate : GenericImplMap[BaseName]) {
              instantiateGenericImpl(ImplTemplate, RawPrefix, genericArgs);
            }
            // Retry lookup
            if (MethodMap.count(ShapeName) &&
                MethodMap[ShapeName].count(VariantName)) {
              FunctionDecl *MetAST = nullptr;
              if (MethodDecls.count(ShapeName) && MethodDecls[ShapeName].count(VariantName)) {
                  MetAST = MethodDecls[ShapeName][VariantName];
              }

              for (size_t i = 0; i < Call->Args.size(); ++i) {
                Call->Args[i] = foldGenericConstant(std::move(Call->Args[i]));
                std::shared_ptr<toka::Type> expectedTy = nullptr;
                if (MetAST && i < MetAST->Args.size()) {
                    std::string tyStr = MetAST->Args[i].Type;
                    if (MetAST->Args[i].IsRawPointer) tyStr = "*" + tyStr;
                    else if (MetAST->Args[i].IsUnique) tyStr = "^" + tyStr;
                    else if (MetAST->Args[i].IsShared) tyStr = "~" + tyStr;
                    else if (MetAST->Args[i].IsReference) tyStr = "&" + tyStr;
                    expectedTy = resolveType(toka::Type::fromString(tyStr), false);
                }
                auto argTy = checkExpr(Call->Args[i].get(), expectedTy);
                if (expectedTy && !isTypeCompatible(expectedTy, argTy)) {
                    DiagnosticEngine::report(getLoc(Call->Args[i].get()), DiagID::ERR_TYPE_MISMATCH,
                                             "Argument " + std::to_string(i + 1) + " (actual: " + argTy->getSoulName() + ")", expectedTy->getSoulName(), argTy->getSoulName());
                    HasError = true;
                }
              }
              auto retObj =
                  toka::Type::fromString(MethodMap[ShapeName][VariantName]);
              auto resolvedRet = resolveType(retObj);
              
              // [FIX] Check if Static Method is async and wrap in TaskHandle
              if (MetAST && MetAST->Effect == EffectKind::Async) {
                  std::string tName = "TaskHandle<" + resolvedRet->toString() + ">";
                  return toka::Type::fromString(tName);
              }
              return resolvedRet;
            }
          }
        }
      }
      // Enum Variant Constructor
      ShapeDecl *SD = ShapeMap[ShapeName];
      if (SD->Kind == ShapeKind::Enum) {
        for (auto &Memb : SD->Members) {
          if (Memb.Name == VariantName) {
            // Enum Variant Constructor: Variant(Args...) -> ShapeName
            for (auto &Arg : Call->Args) {
              Arg = foldGenericConstant(std::move(Arg)); // [FIX]
              checkExpr(Arg.get());
            }

            // Set ResolvedShape for CodeGen
            Call->ResolvedShape = SD;

            return toka::Type::fromString(ShapeName);
          }
        }
      }
      // If we are here, it means we found the Shape but not the
      // Method/Variant
      error(Call, DiagID::ERR_SEMA_STATIC_METHOD_OR_VARIANT_NOT_FOUND_IN_SHA, VariantName, ShapeName);
      return toka::Type::fromString("unknown");
    }
  }
  // 4. Regular Function Lookup
  FunctionDecl *Fn = nullptr;
  ExternDecl *Ext = nullptr;
  ShapeDecl *Sh = nullptr; // Constructor

  // [NEW] Generic Constructor Pre-Check
  // If CallName looks like a generic type "Box<i32>", try to resolve it
  // as a Type. This triggers monomorphization in resolveType.
  if (CallName.find('<') != std::string::npos) {
    auto possibleType = toka::Type::fromString(CallName);
    if (possibleType && !possibleType->isUnknown()) {
      auto resolved = resolveType(possibleType);
      if (auto shapeT = std::dynamic_pointer_cast<toka::ShapeType>(resolved)) {
        if (shapeT->Decl) {
          Sh = shapeT->Decl;
          Call->Callee = shapeT->Name; // Update Call Name to Mangled
                                       // Name for CodeGen!
          CallName = shapeT->Name;     // Update local var for verification
                                       // logic below

          // [NEW] Union Constructor: Check member name if provided
          if (Sh->Kind == ShapeKind::Union) {
            if (Call->Args.size() == 1) {
              // Check if the argument is a named argument
              // Actually the Parser produces named args as BinaryExpr
              // with
              // "="? Wait, Toka syntax for named args is `Func(name =
              // val)`. In CallExpr::Args, this is parsed as
              // BinaryExpr("=", Var(name), Val). But checkCallExpr
              // usually iterates args and checks them. We need to peek
              // at the arg structure.

              // Moved logic to "4. Regular Function Lookup" section
              // below because Sh might be found there too.
            }
          }
        }
      }
    }
  }

  size_t scopePos = CallName.find("::");
  if (!Sh && scopePos != std::string::npos) {
    std::string ModName = CallName.substr(0, scopePos);
    std::string FuncName = CallName.substr(scopePos + 2);
    SymbolInfo *modSpecPtr = nullptr;
    std::string actualModName = ModName;
    if (CurrentScope->findVariableWithDeref(ModName, modSpecPtr, actualModName)) {
      SymbolInfo modSpec = *modSpecPtr;
      modSpecPtr->HasBeenUsed = true;
      if (modSpecPtr->ImportingDecl) {
        const_cast<ImportDecl*>(modSpecPtr->ImportingDecl)->HasBeenUsed = true;
      }
      if (modSpec.ReferencedModule) {
        ModuleScope *target = (ModuleScope *)modSpec.ReferencedModule;
        if (target->Functions.count(FuncName))
          Fn = target->Functions[FuncName];
        else if (target->Externs.count(FuncName))
          Ext = target->Externs[FuncName];
        else if (target->Shapes.count(FuncName))
          Sh = target->Shapes[FuncName];

        if (!Fn && !Ext && !Sh) {
          // No debug prints
        }
      } else {
        error(Call, DiagID::ERR_SEMA_MODULE_NOT_FOUND_OR_NOT_IMPORTED, ModName);
        return toka::Type::fromString("unknown");
      }
    } else {
      error(Call, DiagID::ERR_SEMA_MODULE_NOT_FOUND_OR_NOT_IMPORTED, ModName);
      return toka::Type::fromString("unknown");
    }
  } else if (!Sh) {
    if (ExternMap.count(CallName))
      Ext = ExternMap[CallName];
    else if (ShapeMap.count(CallName))
      Sh = ShapeMap[CallName];

    // Fallback: Check if it's a type alias to a shape
    if (!Fn && !Ext && !Sh && TypeAliasMap.count(CallName)) {
      std::string target = TypeAliasMap[CallName].Target;
      // [FIX] Generic Alias Resolution: Resolve the type string first
      // This handles 'alias Node = GenericNode<i32>' by triggering
      // instantiation and returning the mangled ShapeType.
      auto potentialType = toka::Type::fromString(target);
      if (potentialType && !potentialType->isUnknown()) {
        auto resolved = resolveType(potentialType);
        if (auto shapeT =
                std::dynamic_pointer_cast<toka::ShapeType>(resolved)) {
          if (shapeT->Decl) {
            Sh = shapeT->Decl;
            // Update Callee to the concrete mangled name (e.g.
            // Generic_M_i32) This ensures CodeGen calls the correct
            // function.
            Call->Callee = shapeT->Name;
          }
        }
      }

      // Legacy/Simple Fallback (if resolveType didn't yield a ShapeDecl
      // logic above covers most)
      if (!Sh && ShapeMap.count(target)) {
        Sh = ShapeMap[target];
        // If simple alias, we might also want to update Callee?
        // Typically code expects Callee to be the Shape Name.
        // For 'alias P = Point', target='Point'.
        Call->Callee = target;
      }
    }
  }
  // Local Scope Lookup (Local, Imported, or Shadowed)
  SymbolInfo sym;
  SymbolInfo *symPtr = nullptr;
  std::string actualCallName = CallName;
  if (CurrentScope->findVariableWithDeref(CallName, symPtr, actualCallName)) {
    sym = *symPtr;
    symPtr->HasBeenUsed = true;
    if (symPtr->ImportingDecl) {
      const_cast<ImportDecl*>(symPtr->ImportingDecl)->HasBeenUsed = true;
    }
    // Find implementation based on lookup
    for (auto *GF : GlobalFunctions) {
      if (GF->Name == CallName) {
        Fn = GF;
        break;
      }
    }
    if (!Fn) {
      for (auto &pair : ExternMap) {
        if (pair.second->Name == CallName) {
          Ext = pair.second;
          break;
        }
      }
    }
    std::string soulName = Type::stripMorphology(CallName);
    if (!Fn && !Ext && ShapeMap.count(soulName)) {
      Sh = ShapeMap[soulName];
    }

    // [New] Closure Invocation Intercept
    if (!Fn && !Ext && !Sh && sym.TypeObj) {
      if (auto sTy = std::dynamic_pointer_cast<ShapeType>(sym.TypeObj)) {
        std::string shapeName = sTy->getSoulName();
        if (MethodMap.count(shapeName) && MethodMap[shapeName].count("__invoke")) {
          FunctionDecl *invokeFn = (FunctionDecl*)MethodDecls[shapeName]["__invoke"];
          
          if (Call->Args.size() != invokeFn->Args.size() - 1) {
             error(Call, DiagID::ERR_SEMA_CLOSURE_EXPECTS_ARGUMENTS_BUT_GOT, std::to_string(invokeFn->Args.size() - 1), std::to_string(Call->Args.size()));
          } else {
             for (size_t i = 0; i < Call->Args.size(); ++i) {
                Call->Args[i] = foldGenericConstant(std::move(Call->Args[i]));
                auto expectedTy = toka::Type::fromString(invokeFn->Args[i + 1].Type);
                auto argTy = checkExpr(Call->Args[i].get(), expectedTy);
                std::string expectedBase = Type::stripMorphology(invokeFn->Args[i + 1].Type);
                std::string actualBase = argTy->getSoulName();
                if (expectedBase != actualBase && expectedBase != "unknown" && actualBase != "unknown") {
                    if (!isTypeCompatible(toka::Type::fromString(resolveType(actualBase)), toka::Type::fromString(resolveType(expectedBase)))) {
                        DiagnosticEngine::report(getLoc(Call->Args[i].get()), DiagID::ERR_TYPE_MISMATCH,
                                                 "Argument " + std::to_string(i + 1), expectedBase, actualBase);
                        HasError = true;
                    }
                }
             }
          }
          Call->ResolvedFn = invokeFn;
          
          // [Fix] Inject `self` as the first argument!
          // We construct `&f1` (AddressOfExpr(VariableExpr("f1"))) and insert it at index 0.
          auto varE = std::make_unique<VariableExpr>(CallName);
          auto refE = std::make_unique<AddressOfExpr>(std::move(varE));
          checkExpr(refE.get()); // Populate ResolvedType and morphology
          Call->Args.insert(Call->Args.begin(), std::move(refE));
          
          return invokeFn->ResolvedReturnType ? invokeFn->ResolvedReturnType : toka::Type::fromString(MethodMap[shapeName]["__invoke"]);
        }
      }
    }
  }

  // [Fix] Fallback for implicitly instantiated generics that are in GlobalFunctions but not in CurrentScope
  if (!Fn && !Ext && !Sh) {
    for (auto *GF : GlobalFunctions) {
      if (GF->Name == CallName) {
        Fn = GF;
        break;
      }
    }
  }

  if (!Fn && !Ext && !Sh) {
    if (sym.TypeObj && sym.TypeObj->typeKind == toka::Type::Function) {
      auto fnTy = std::dynamic_pointer_cast<toka::FunctionType>(sym.TypeObj);
      if (Call->Args.size() != fnTy->ParamTypes.size()) {
         error(Call, DiagID::ERR_SEMA_CLOSURE_EXPECTS_ARGUMENTS_BUT_GOT, std::to_string(fnTy->ParamTypes.size()), std::to_string(Call->Args.size()));
      } else {
         for (size_t i = 0; i < Call->Args.size(); ++i) {
            Call->Args[i] = foldGenericConstant(std::move(Call->Args[i]));
            auto expectedTy = resolveType(fnTy->ParamTypes[i], false);
            auto argTy = checkExpr(Call->Args[i].get(), expectedTy);
            if (!isTypeCompatible(fnTy->ParamTypes[i], argTy)) {
                DiagnosticEngine::report(getLoc(Call->Args[i].get()), DiagID::ERR_TYPE_MISMATCH,
                                         "Argument " + std::to_string(i + 1), fnTy->ParamTypes[i]->getSoulName(), argTy->getSoulName());
                HasError = true;
            }
         }
      }
      return resolveType(fnTy->ReturnType, false);
    } else if (sym.TypeObj && sym.TypeObj->typeKind == toka::Type::DynFn) {
      auto fnTy = std::dynamic_pointer_cast<toka::DynFnType>(sym.TypeObj);
      if (Call->Args.size() != fnTy->ParamTypes.size()) {
         error(Call, DiagID::ERR_SEMA_CLOSURE_EXPECTS_ARGUMENTS_BUT_GOT, std::to_string(fnTy->ParamTypes.size()), std::to_string(Call->Args.size()));
      } else {
         for (size_t i = 0; i < Call->Args.size(); ++i) {
            Call->Args[i] = foldGenericConstant(std::move(Call->Args[i]));
            auto expectedTy = resolveType(fnTy->ParamTypes[i], false);
            auto argTy = checkExpr(Call->Args[i].get(), expectedTy);
            if (!isTypeCompatible(fnTy->ParamTypes[i], argTy)) {
                DiagnosticEngine::report(getLoc(Call->Args[i].get()), DiagID::ERR_TYPE_MISMATCH,
                                         "Argument " + std::to_string(i + 1), fnTy->ParamTypes[i]->getSoulName(), argTy->getSoulName());
                HasError = true;
            }
         }
      }
      return resolveType(fnTy->ReturnType, false);
    }

    if (CallName != "unknown") {
      DiagnosticEngine::report(getLoc(Call), DiagID::ERR_UNDECLARED, CallName);
      HasError = true;
    }
    return toka::Type::fromString("unknown");
  }

  // 5. Synthesize FunctionType
  // ParamTypes, ReturnType
  std::vector<std::shared_ptr<toka::Type>> ParamTypes;
  std::shared_ptr<toka::Type> ReturnType;
  bool IsVariadic = false;

  // [NEW] Generic Instantiation
  if (Fn && !Fn->GenericParams.empty()) {
    std::vector<std::shared_ptr<toka::Type>> TypeArgs;
    bool deductionFailed = false;

    if (!Call->GenericArgs.empty()) {
      // Explicit Instantiation
      if (Call->GenericArgs.size() != Fn->GenericParams.size()) {
        DiagnosticEngine::report(getLoc(Call), DiagID::NOTE_GENERIC, Fn->Name, Fn->GenericParams.size(), Call->GenericArgs.size());
        HasError = true;
        return toka::Type::fromString("unknown");
      }
      for (size_t i = 0; i < Call->GenericArgs.size(); ++i) {
        std::string argStr = Call->GenericArgs[i];
        if (Fn->GenericParams[i].IsConst) {
          // Pass the literal value directly as the "Type" name for
          // mangling
          TypeArgs.push_back(toka::Type::fromString(argStr));
        } else {
          TypeArgs.push_back(toka::Type::fromString(resolveType(argStr)));
        }
      }
    } else {
      // Type Deduction
      std::map<std::string, std::shared_ptr<toka::Type>> Deduced;

      for (size_t i = 0; i < Call->Args.size() && i < Fn->Args.size(); ++i) {
        Call->Args[i] = foldGenericConstant(std::move(Call->Args[i])); // [FIX]
        auto argType = checkExpr(Call->Args[i].get());
        if (!argType || argType->isUnknown())
          continue;

        const auto &Param = Fn->Args[i];
        std::string PType = Param.Type;
        // [FIX] Parse Sigils from PType string (since Parser might
        // leave them in string)
        bool locIsRawPointer = Param.IsRawPointer;
        bool locIsUnique = Param.IsUnique;
        bool locIsShared = Param.IsShared;
        bool locIsReference = Param.IsReference;

        while (PType.size() > 1 && (PType[0] == '*' || PType[0] == '^' ||
                                    PType[0] == '~' || PType[0] == '&')) {
          if (PType[0] == '*')
            locIsRawPointer = true;
          else if (PType[0] == '^')
            locIsUnique = true;
          else if (PType[0] == '~')
            locIsShared = true;
          else if (PType[0] == '&')
            locIsReference = true;
          PType = PType.substr(1);
        }

        // Check if PType is a generic param. Morphic params keep their
        // source name as 'T, but canonical binding positions use T.
        bool isGeneric = false;
        std::string genericKey;
        for (const auto &gp : Fn->GenericParams) {
          if (gp.Name == PType ||
              (gp.IsMorphic && !gp.Name.empty() && gp.Name[0] == '\'' &&
               gp.Name.substr(1) == PType)) {
            isGeneric = true;
            genericKey = gp.Name;
            break;
          }
        }

        if (isGeneric) {
          // Strict Match: T matches ArgType
          // Need to account for morphology stripping on Param/Arg side?
          // If Param is `x: T`, and Arg is `i32`, T=i32.
          // If Param is `x: ^T`, and Arg is `^i32`?
          //   Param.IsUnique is true. Param.Type is T.
          //   ArgType is UniquePointer(i32).
          //   We must strip ArgType morphology to find T.

          std::shared_ptr<toka::Type> candidate = argType;

          // Strip Param Morphology from Candidate
          if (locIsRawPointer) {
            if (candidate->isRawPointer())
              candidate = candidate->getPointeeType();
            else if (candidate->isReference()) // Allow &T -> *T decay
                                               // for deduction
              candidate = candidate->getPointeeType();
            else
              continue; // Mismatch handled later
          }
          if (locIsUnique) {
            if (candidate->isUniquePtr())
              candidate = candidate->getPointeeType();
            else
              continue;
          }
          if (locIsShared) {
            if (candidate->isSharedPtr())
              candidate = candidate->getPointeeType();
            else
              continue;
          }
          if (locIsReference) {
            if (candidate->isReference())
              candidate = candidate->getPointeeType();
          }

          // Generic Decay: strip interior writability flags for T
          candidate = candidate->withAttributes(false, candidate->IsNullable, candidate->IsBlocked);

          // Deduce
          if (Deduced.count(genericKey)) {
            if (!Deduced[genericKey]->equals(*candidate)) {
              error(Call, DiagID::ERR_SEMA_TYPE_DEDUCTION_CONFLICT_FOR_VS, genericKey, Deduced[genericKey]->toString(), candidate->toString());
              deductionFailed = true;
            }
          } else {
            Deduced[genericKey] = candidate;
          }
        }
      }

      if (!deductionFailed) {
        for (const auto &gp : Fn->GenericParams) {
          if (!Deduced.count(gp.Name)) {
            error(Call, DiagID::ERR_SEMA_FAILED_TO_DEDUCE_TYPE_FOR_GENERIC_PARAMET, gp.Name);
            deductionFailed = true;
          } else {
            TypeArgs.push_back(Deduced[gp.Name]);
          }
        }
      }
    }

    if (!deductionFailed) {
      Fn = instantiateGenericFunction(Fn, TypeArgs, Call);
      if (!Fn)
        return toka::Type::fromString("unknown");

      // [FIX] Update the Call AST to point to the mangled instance name
      // otherwise CodeGen will attempt to call the generic template
      // name
      Call->Callee = Fn->Name;
    } else {
      HasError = true;
      return toka::Type::fromString("unknown");
    }
  }

  if (Fn) {
    if (Fn->IsDeleted) {
      error(Call, DiagID::ERR_SEMA_CANNOT_CALL_EXPLICITLY_DELETED_FUNCTION, Fn->Name);
      HasError = true;
      return toka::Type::fromString("unknown");
    }

    Call->ResolvedFn = Fn;
    for (auto &arg : Fn->Args) {
      ParamTypes.push_back(
          toka::Type::fromString(Sema::synthesizePhysicalType(arg)));
    }
    ReturnType = toka::Type::fromString(Fn->ReturnType);
    IsVariadic = Fn->IsVariadic;

    // [Effect] Concurrency Check for Function Call
    if (Fn->Effect != EffectKind::None && !m_IsConsumingEffect && !m_IsPrecomputingCaptures) {
      error(Call, DiagID::ERR_DANGLING_EFFECT, Fn->Name);
    }
  } else if (Ext) {
    Call->ResolvedExtern = Ext;
    for (auto &arg : Ext->Args) {
      ParamTypes.push_back(
          toka::Type::fromString(Sema::synthesizePhysicalType(arg)));
    }
    ReturnType = toka::Type::fromString(Ext->ReturnType);
    IsVariadic = Ext->IsVariadic;

    // [Effect] Concurrency Check for Extern Call
    if (Ext->Effect != EffectKind::None && !m_IsConsumingEffect && !m_IsPrecomputingCaptures) {
      error(Call, DiagID::ERR_DANGLING_EFFECT, Ext->Name);
    }
  } else if (Sh) {
    if (!checkVisibility(Call, Sh)) {
      return toka::Type::fromString("unknown");
    }

    // [NEW] Instantiate Generic Shape Constructor
    if (!Sh->GenericParams.empty() && !Call->GenericArgs.empty()) {
      std::vector<std::shared_ptr<toka::Type>> typeArgs;
      for (const auto &s : Call->GenericArgs) {
        typeArgs.push_back(toka::Type::fromString(resolveType(s)));
      }
      auto genericShape = std::make_shared<toka::ShapeType>(Sh->Name, typeArgs);
      auto resolved = resolveType(genericShape);
      if (auto rs = std::dynamic_pointer_cast<toka::ShapeType>(resolved)) {
        if (rs->Decl) {
          Sh = rs->Decl;
          Call->ResolvedShape = Sh;
          Call->Callee = rs->Name; // Update for CodeGen lookup
        }
      }
    }

    // Constructor: Params = Members, Return = ShapeName
    if (Sh->Kind == ShapeKind::Struct) {
      Call->ResolvedShape = Sh;

      // [NEW] Single Argument Isomorphic Copy / Move Constructor intercept
      if (Call->Args.size() == 1) {
          auto *argExpr = Call->Args[0].get();
          auto argType = checkExpr(argExpr);
          if (argType) {
              std::string expectedBase = Sh->Name;
              std::string actualBase = argType->getSoulName();
              if (actualBase == expectedBase && actualBase != "unknown") {
                  tryInjectAutoClone(Call->Args[0]);
                  Call->IsIsomorphicCopy = true;
                  return toka::Type::fromString(resolveType(Sh->Name));
              }
          }
      }

      // [Sema Defense] Enforce 100% Named Struct Initialization (Positional struct init is strictly prohibited)
      bool allowed = false;
      if (Sh->Members.empty()) {
        if (Call->Args.empty()) {
          allowed = true;
        }
      } else {
        if (Call->Args.size() == 1) {
          if (dynamic_cast<ElisionExpr *>(Call->Args[0].get())) {
            allowed = true;
          }
        }
      }

      if (!allowed) {
        for (size_t i = 0; i < Call->Args.size(); ++i) {
          auto *argExpr = Call->Args[i].get();
          bool isNamed = false;
          if (auto *bin = dynamic_cast<BinaryExpr *>(argExpr)) {
            if (bin->Op == "=") {
              if (dynamic_cast<VariableExpr *>(bin->LHS.get())) {
                isNamed = true;
              }
            }
          } else if (dynamic_cast<ElisionExpr *>(argExpr)) {
            isNamed = true;
          }

          if (!isNamed) {
            DiagnosticEngine::report(getLoc(argExpr), DiagID::ERR_STRUCT_POSITIONAL_INIT_PROHIBITED, Sh->Name);
            HasError = true;
            return toka::Type::fromString("unknown");
          }
        }
      }
      std::set<std::string> providedFields;
      int elisionIndex = -1;
      bool hasNamed = false;
      bool hasPositional = false;

      for (size_t i = 0; i < Call->Args.size(); ++i) {
        auto *argExpr = Call->Args[i].get();
        if (auto *elExpr = dynamic_cast<ElisionExpr *>(argExpr)) {
          if (elisionIndex != -1) {
            error(argExpr, DiagID::ERR_MULTIPLE_ELISION);
          }
          elisionIndex = (int)i;
          hasPositional = true;
          continue;
        }

        bool isNamed = false;
        if (auto *bin = dynamic_cast<BinaryExpr *>(argExpr)) {
          if (bin->Op == "=") isNamed = true;
        }
        if (isNamed) hasNamed = true;
        else hasPositional = true;
      }

      if (hasNamed) {
        error(Call, DiagID::ERR_STRUCT_POSITIONAL_INIT_PROHIBITED, Sh->Name);
      }

      int normalArgsCount = Call->Args.size() - (elisionIndex != -1 ? 1 : 0);
      int elisionSkipCount = 0;
      if (elisionIndex != -1) {
        elisionSkipCount = (int)Sh->Members.size() - normalArgsCount;
        if (elisionSkipCount < 0) {
          error(Call->Args[elisionIndex].get(), DiagID::ERR_SEMA_TOO_MANY_ARGUMENTS_PROVIDED_CANNOT_ELIDE);
        } else if (elisionSkipCount == 0) {
          error(Call->Args[elisionIndex].get(), DiagID::ERR_REDUNDANT_ELISION);
        }
      } else {
        if (normalArgsCount > (int)Sh->Members.size()) {
          error(Call, DiagID::ERR_SEMA_TOO_MANY_ARGUMENTS_FOR_STRUCT, Sh->Name);
        }
      }

      size_t memberIdx = 0;
      for (size_t i = 0; i < Call->Args.size(); ++i) {
        if ((int)i == elisionIndex) {
          for (int k = 0; k < elisionSkipCount; ++k) {
            auto &M = Sh->Members[memberIdx];
            if (!M.DefaultValue) {
              error(Call->Args[i].get(), DiagID::ERR_MISSING_DEFAULT_FOR_ELIDED, M.Name, Sh->Name);
            }
            memberIdx++;
          }
          continue;
        }

        auto &arg = Call->Args[i];
        if (memberIdx < Sh->Members.size()) {
          auto &M = Sh->Members[memberIdx];
          providedFields.insert(M.Name);

          auto expectedType = getPhysicalType(M);
          auto valType = checkExpr(arg.get());
          if (!isTypeCompatible(expectedType, valType)) {
            error(arg.get(), DiagID::ERR_SEMA_TYPE_MISMATCH_FOR_FIELD_EXPECTED_GOT, M.Name, expectedType->toString(), valType->toString());
          }
          memberIdx++;
        }
      }

      // Inject missing defaults or elided defaults
      std::vector<std::unique_ptr<Expr>> resolvedArgs;
      for (const auto &M : Sh->Members) {
        if (!providedFields.count(M.Name)) {
          if (elisionIndex != -1 && M.DefaultValue) {
            auto cloned = std::unique_ptr<Expr>(static_cast<Expr *>(M.DefaultValue->clone().release()));
            auto expectedType = getPhysicalType(M);
            auto valType = checkExpr(cloned.get(), expectedType);

            if (isTypeCompatible(expectedType, valType) && !expectedType->equals(*valType)) {
              auto origLoc = cloned->Loc;
              cloned = std::make_unique<CastExpr>(std::move(cloned), expectedType->toString());
              cloned->Loc = origLoc;
              cloned->ResolvedType = expectedType;
              valType = expectedType;
            }

            bool bypassNullStruct = false;
            if (m_InUnsafeContext && expectedType && expectedType->isRawPointer() && valType && valType->isNullType()) {
                bypassNullStruct = true;
            }

            if (!bypassNullStruct && !isTypeCompatible(expectedType, valType)) {
              error(Call, DiagID::ERR_MEMBER_TYPE_MISMATCH, M.Name,
                    expectedType->toString(), valType->toString());
            }

            auto nameVar = std::make_unique<VariableExpr>(M.Name);
            auto bin = std::make_unique<BinaryExpr>("=", std::move(nameVar), std::move(cloned));
            resolvedArgs.push_back(std::move(bin));
          } else {
            if (elisionIndex == -1) {
              error(Call, DiagID::ERR_SEMA_MISSING_FIELD_IN_CONSTRUCTOR_FOR_USE_TO_E, M.Name, Sh->Name);
            }
          }
        } else {
          // Find the parameter in original Args that was mapped to this field
          // It was mapped by position, skipping `elisionIndex`.
          // We can find it by traversing Call->Args again.
          bool found = false;
          size_t mIdx = 0;
          for (size_t i = 0; i < Call->Args.size(); ++i) {
            if ((int)i == elisionIndex) {
              mIdx += elisionSkipCount;
              continue;
            }
            if (Sh->Members[mIdx].Name == M.Name) {
              // Wrap it in BinaryExpr to match CodeGen expectations for named args injection
              auto nameVar = std::make_unique<VariableExpr>(M.Name);
              auto bin = std::make_unique<BinaryExpr>("=", std::move(nameVar), std::move(Call->Args[i]));
              resolvedArgs.push_back(std::move(bin));
              found = true;
              break;
            }
            mIdx++;
          }
        }
      }
      Call->Args = std::move(resolvedArgs);

      auto res = toka::Type::fromString(Sh->Name);

      if (TypeAliasMap.count(OriginalName) &&
          TypeAliasMap[OriginalName].IsStrong) {
        res = toka::Type::fromString(OriginalName);
      }
      return res;
    } else if (Sh->Kind == ShapeKind::Union) {

      if (Call->Args.size() != 1) {
        error(Call, DiagID::ERR_SEMA_UNION_REQUIRES_EXACTLY_ONE_ARGUMENT, CallName);
        return toka::Type::fromString("void");
      }
      Expr *argExpr = Call->Args[0].get();
      Expr *valExpr = argExpr;
      std::string fieldName = "";
      bool isNamed = false;

      // Detect Named Arg: variant = value
      if (auto *bin = dynamic_cast<BinaryExpr *>(argExpr)) {
        if (bin->Op == "=") {
          if (auto *var = dynamic_cast<VariableExpr *>(bin->LHS.get())) {
            fieldName = var->Name;
            valExpr = bin->RHS.get();
            isNamed = true;
          }
        }
      }

      int matchedIdx = -1;
      std::shared_ptr<toka::Type> argType = checkExpr(valExpr);

      if (isNamed) {
        for (int i = 0; i < (int)Sh->Members.size(); ++i) {
          if (Sh->Members[i].Name == fieldName) {
            matchedIdx = i;
            break;
          }
        }
        if (matchedIdx == -1) {
          error(argExpr, DiagID::ERR_SEMA_UNION_HAS_NO_VARIANT_NAMED, Sh->Name, fieldName);
          return toka::Type::fromString("unknown");
        }

        auto memType = getPhysicalType(Sh->Members[matchedIdx]);
        if (!isTypeCompatible(memType, argType)) {
          error(valExpr, DiagID::ERR_SEMA_TYPE_MISMATCH_FOR_UNION_VARIANT_EXPECTED, fieldName, memType->toString(), argType->toString());
        }

        // 1. Calculate Union Size
        uint64_t unionSize = 0;
        for (auto &m : Sh->Members) {
          auto mT = getPhysicalType(m);
          uint64_t s = getTypeSize(mT);
          if (s > unionSize)
            unionSize = s;
        }

        // 2. Calculate Initialized Variant Size
        // memType is the type of the variant we are initializing
        uint64_t variantSize = getTypeSize(memType);

        if (variantSize < unionSize) {
          DiagnosticEngine::report(getLoc(Call), DiagID::ERR_UNION_PARTIAL_INIT,
                                   Sh->Name, fieldName, variantSize, unionSize);
          HasError = true;
        }

        Call->MatchedMemberIdx = matchedIdx;
      } else {
        // Heuristic matching for positional arg
        int exactMatchIdx = -1;
        int exactMatchCount = 0;
        int fitMatchIdx = -1;
        int fitMatchCount = 0;

        for (int i = 0; i < (int)Sh->Members.size(); ++i) {
          auto memType = getPhysicalType(Sh->Members[i]);
          if (!memType)
            continue;

          if (argType->equals(*memType)) {
            exactMatchCount++;
            if (exactMatchIdx == -1)
              exactMatchIdx = i;
          } else if (isTypeCompatible(memType, argType)) {
            fitMatchCount++;
            if (fitMatchIdx == -1)
              fitMatchIdx = i;
          }
        }

        if (exactMatchCount == 1) {
          Call->MatchedMemberIdx = exactMatchIdx;
        } else if (exactMatchCount > 1) {
          error(Call, DiagID::ERR_SEMA_AMBIGUOUS_UNION_CONSTRUCTOR_MULTIPLE_EXAC, argType->toString());
          HasError = true;
          return toka::Type::fromString("unknown");
        } else if (fitMatchCount == 1) {
          Call->MatchedMemberIdx = fitMatchIdx;
        } else if (fitMatchCount > 1) {
          error(Call, DiagID::ERR_SEMA_AMBIGUOUS_UNION_CONSTRUCTOR_MULTIPLE_SAFE, argType->toString());
          HasError = true;
          return toka::Type::fromString("unknown");
        } else {
          error(Call, DiagID::ERR_SEMA_NO_MATCHING_MEMBER_FOUND_IN_UNION_FOR_TYP, Sh->Name, argType->toString());
          HasError = true;
          return toka::Type::fromString("unknown");
        }
      }

      return toka::Type::fromString(Sh->Name);
    } else {
      return toka::Type::fromString(Sh->Name);
    }
  }

  // Generic Function/Extern Matching
  funcType =
      std::make_shared<toka::FunctionType>(ParamTypes, ReturnType, IsVariadic);

  // 6. Argument Matching and Default Argument Injection
  bool hasFunctionElision = false;
  if (!Call->Args.empty()) {
      if (dynamic_cast<ElisionExpr*>(Call->Args.back().get())) {
          hasFunctionElision = true;
          Call->Args.pop_back();
      }
      // Check for illegal elisions in the middle
      for (const auto &arg : Call->Args) {
          if (dynamic_cast<ElisionExpr*>(arg.get())) {
              error(arg.get(), DiagID::ERR_SEMA_FUNCTION_CALL_ELISION_MUST_STRICTLY_BE_TH);
          }
      }
  }

  size_t providedCount = Call->Args.size();
  size_t paramCount = ParamTypes.size();

  if (hasFunctionElision && providedCount >= paramCount) {
      error(Call, DiagID::ERR_SEMA_ELISION_PROVIDED_BUT_NO_DEFAULT_ARGUMENTS, CallName);
  }

  if (providedCount < paramCount) {
    if (!hasFunctionElision && !IsVariadic) {
        error(Call, DiagID::ERR_SEMA_MISSING_ARGUMENT_IN_FUNCTION_CALL_USE_TO, std::to_string(providedCount + 1), CallName);
    }
    for (size_t i = providedCount; i < paramCount; ++i) {
      std::unique_ptr<Expr> injected = nullptr;
      const ASTNode *defValNode = nullptr;
      if (Fn)
        defValNode = Fn->Args[i].DefaultValue.get();
      else if (Ext)
        defValNode = Ext->Args[i].DefaultValue.get();

      if (defValNode) {
        const Expr *defVal = static_cast<const Expr *>(defValNode);
        if (auto *magic = dynamic_cast<const MagicExpr *>(defVal)) {
          auto fullloc = DiagnosticEngine::SrcMgr->getFullSourceLoc(Call->Loc);
          if (magic->Kind == TokenType::KwFile) {
            injected = std::make_unique<ViewStringExpr>(fullloc.FileName);
          } else if (magic->Kind == TokenType::KwLine) {
            injected = std::make_unique<NumberExpr>(fullloc.Line);
          } else if (magic->Kind == TokenType::KwLoc) {
            // shape SourceLoc(file: str, line: i32)
            std::vector<std::pair<std::string, std::unique_ptr<Expr>>> fields;
            fields.push_back(
                {"file", std::make_unique<ViewStringExpr>(fullloc.FileName)});
            fields.push_back(
                {"line", std::make_unique<NumberExpr>(fullloc.Line)});
            injected = std::make_unique<InitStructExpr>("SourceLoc",
                                                        std::move(fields));
          }
          if (injected)
            injected->Loc = Call->Loc;
        } else {
          injected = std::unique_ptr<Expr>(
              static_cast<Expr *>(defVal->clone().release()));
        }
      }

      if (injected) {
        Call->Args.push_back(std::move(injected));
      } else {
        if (!IsVariadic) {
          error(Call, DiagID::ERR_SEMA_ARGUMENT_COUNT_MISMATCH_FOR_EXPECTED_GOT, CallName, std::to_string(paramCount), std::to_string(providedCount));
          return ReturnType;
        }
      }
    }
  } else if (!IsVariadic && providedCount > paramCount) {
    error(Call, DiagID::ERR_SEMA_ARGUMENT_COUNT_MISMATCH_FOR_EXPECTED_GOT, CallName, std::to_string(paramCount), std::to_string(providedCount));
    return ReturnType;
  }

  for (size_t i = 0; i < Call->Args.size(); ++i) {
    Call->Args[i] = foldGenericConstant(std::move(Call->Args[i])); // [FIX]

    auto paramType = (i < ParamTypes.size()) ? ParamTypes[i] : nullptr;

    // [NEW] Top-Down Closure Type Injection
    if (paramType) {
       auto canonicalParam = resolveType(paramType, false);
       if (canonicalParam && canonicalParam->typeKind == toka::Type::Function) {
          if (auto clo = dynamic_cast<ClosureExpr*>(Call->Args[i].get())) {
             auto fnTy = std::static_pointer_cast<toka::FunctionType>(canonicalParam);
             clo->InjectedParamTypes = fnTy->ParamTypes;
             if ((clo->ReturnType.empty() || clo->ReturnType == "unknown") && fnTy->ReturnType) {
                 clo->ReturnType = fnTy->ReturnType->toString();
              }
          }
       }
    }

    auto argType = checkExpr(Call->Args[i].get(), paramType);
    
    if (paramType && paramType->IsWritable) {
      Expr *argExpr = Call->Args[i].get();
      while (auto *un = dynamic_cast<UnaryExpr *>(argExpr)) {
        argExpr = un->RHS.get();
      }
      if (auto *VE = dynamic_cast<VariableExpr *>(argExpr)) {
        std::string actualName = VE->Name;
        SymbolInfo *InfoPtr = nullptr;
        if (CurrentScope->findVariableWithDeref(VE->Name, InfoPtr, actualName)) {
          InfoPtr->HasBeenMutated = true;
        }
      }
    }
    
    // [NEW] Enforce explicit cede for normal function calls
    bool isCededParam = false;
    if (Fn && i < Fn->Args.size()) {
        isCededParam = Fn->Args[i].IsCeded;
    } else if (Ext && i < Ext->Args.size()) {
        isCededParam = Ext->Args[i].IsCeded;
    }

    if (isCededParam) {
        bool isCallerCeded = dynamic_cast<CedeExpr*>(Call->Args[i].get()) != nullptr;
        if (!isCallerCeded) {
            error(Call->Args[i].get(), DiagID::ERR_SEMA_ARGUMENT_MUST_BE_EXPLICITLY_PASSED_WITH_2);
        }
    }

    if (IsVariadic && i >= ParamTypes.size())
      continue;
    if (i >= ParamTypes.size())
      break;

    // Morphology Check for Argument
    bool paramIsMorphicExempt = false;
    if (Fn && i < Fn->Args.size()) {
      paramIsMorphicExempt = Fn->Args[i].IsMorphicExempt;
    } else if (Ext && i < Ext->Args.size()) {
      paramIsMorphicExempt = Ext->Args[i].IsMorphicExempt;
    }
    if (!paramIsMorphicExempt) {
      MorphKind targetMorph = morphKindFromType(paramType);
      MorphKind sourceMorph = getSyntacticMorphology(Call->Args[i].get());
      std::string ctx = "arg " + std::to_string(i + 1);
      checkStrictMorphology(Call->Args[i].get(), targetMorph, sourceMorph, ctx);
    }

    bool bypassNull = false;
    if (Ext != nullptr && m_InUnsafeContext && paramType && paramType->isRawPointer() && argType && argType->isNullType()) {
        bypassNull = true;
    }

    if (!bypassNull && !isTypeCompatible(paramType, argType)) {
      error(Call->Args[i].get(), DiagID::ERR_SEMA_TYPE_MISMATCH_FOR_ARGUMENT_EXPECTED_GOT, std::to_string(i + 1), paramType->toString(), argType->toString());
    } else if (paramType && argType && paramType->isShape() && argType->isRawPointer()) {
      auto shp = std::static_pointer_cast<toka::ShapeType>(paramType);
      if (shp->Name == "str") {
        Call->Args[i]->ResolvedType = paramType;
      }
    }
  }
  
  bool isAsync = false;
  if (Fn && Fn->Effect == EffectKind::Async) isAsync = true;
  if (Ext && Ext->Effect == EffectKind::Async) isAsync = true;
  
  if (isAsync) {
      std::string tName = "TaskHandle<" + ReturnType->toString() + ">";
      return toka::Type::fromString(tName);
  }

  // Inject Caller-Side Effect Dependencies
  if (Fn) {
      bool hasExplicitDeps = !Fn->LifeDependencies.empty();
      bool hasImplicitDeps = false;

      std::set<std::string> visited;
      std::function<bool(std::shared_ptr<toka::Type>)> checkType = [&](std::shared_ptr<toka::Type> t) -> bool {
          if (!t) return false;
          if (t->isReference()) return true;
          if (auto *st = dynamic_cast<ShapeType *>(t.get())) {
              for (const auto &arg : st->GenericArgs) {
                  if (checkType(arg)) return true;
              }
              std::string sName = t->getSoulName();
              if (visited.count(sName) == 0) {
                  visited.insert(sName);
                  if (ShapeMap.count(sName)) {
                      ShapeDecl *SD = ShapeMap[sName];
                      for (const auto &m : SD->Members) {
                          auto mT = getPhysicalType(m);
                          if (checkType(mT)) return true;
                      }
                  }
              }
          }
          return false;
      };

      if (!hasExplicitDeps && ReturnType && checkType(ReturnType)) {
          hasImplicitDeps = true;
      }

      auto mapParamToArg = [&](const std::string &paramName) -> std::string {
         for (size_t i = 0; i < Fn->Args.size(); ++i) {
            if (Fn->Args[i].Name == paramName) {
               if (i < Call->Args.size()) {
                   return getStringifyPath(Call->Args[i].get());
               }
            }
         }
         return "";
      };

      if (hasExplicitDeps) {
          for (const auto &dep : Fn->LifeDependencies) {
             std::string argVar = mapParamToArg(dep);
             if (!argVar.empty()) m_LastLifeDependencies.insert(argVar);
          }
      } else if (hasImplicitDeps) {
          for (size_t i = 0; i < Fn->Args.size(); ++i) {
              auto argTypeObj = toka::Type::fromString(Fn->Args[i].Type);
              if (checkType(argTypeObj) && i < Call->Args.size()) {
                  std::string argVar = getStringifyPath(Call->Args[i].get());
                  if (!argVar.empty()) {
                      m_LastLifeDependencies.insert(argVar);
                      break;
                  }
              }
          }
      }

      for (const auto &pair : Fn->MemberDependencies) {
         for (const auto &dep : pair.second) {
             std::string argVar = mapParamToArg(dep);
             if (!argVar.empty()) m_LastFieldDependencies[pair.first].insert(argVar);
          }
      }
  }

  return ReturnType;
}

} // namespace toka
