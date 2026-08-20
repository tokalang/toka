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
#include "toka/SemanticEvidence.h"
#include "toka/Sema.h"
#include "toka/SourceManager.h"
#include "toka/Type.h"
#include <algorithm>
#include <array>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace toka {

static SourceLocation getLoc(ASTNode *Node) { return Node->Loc; }

struct CallArgPALFact {
  AccessPath Path;
  PALOperationClass Access;
  Expr *Node = nullptr;
};

static bool isExecutionBoundaryCalleeName(std::string Name) {
  size_t scopePos = Name.rfind("::");
  if (scopePos != std::string::npos)
    Name = Name.substr(scopePos + 2);
  size_t genericPos = Name.find('<');
  if (genericPos != std::string::npos)
    Name = Name.substr(0, genericPos);
  return Name == "thread_spawn";
}

static bool isOwnedStateThreadCalleeName(std::string Name) {
  size_t scopePos = Name.rfind("::");
  if (scopePos != std::string::npos)
    Name = Name.substr(scopePos + 2);
  size_t genericPos = Name.find('<');
  if (genericPos != std::string::npos)
    Name = Name.substr(0, genericPos);
  return Name == "thread_spawn_with_state";
}

static ClosureExpr *findClosureExpr(Expr *E) {
  if (!E)
    return nullptr;
  if (auto *Clo = dynamic_cast<ClosureExpr *>(E))
    return Clo;
  if (auto *Cede = dynamic_cast<CedeExpr *>(E))
    return findClosureExpr(Cede->Value.get());
  if (auto *Pass = dynamic_cast<PassExpr *>(E))
    return findClosureExpr(Pass->Value.get());
  if (auto *Cast = dynamic_cast<CastExpr *>(E))
    return findClosureExpr(Cast->Expression.get());
  if (auto *Unary = dynamic_cast<UnaryExpr *>(E))
    return findClosureExpr(Unary->RHS.get());
  return nullptr;
}

// The execution-boundary check needs to follow a closure through the same
// transparent wrappers accepted by ordinary argument checking.  It must not,
// however, treat a variable as a literal: the binding's summary is the source
// of truth once a closure has escaped its AST node.
static VariableExpr *findVariableExpr(Expr *E) {
  if (!E)
    return nullptr;
  if (auto *Var = dynamic_cast<VariableExpr *>(E))
    return Var;
  if (auto *Cede = dynamic_cast<CedeExpr *>(E))
    return findVariableExpr(Cede->Value.get());
  if (auto *Pass = dynamic_cast<PassExpr *>(E))
    return findVariableExpr(Pass->Value.get());
  if (auto *Cast = dynamic_cast<CastExpr *>(E))
    return findVariableExpr(Cast->Expression.get());
  if (auto *Unary = dynamic_cast<UnaryExpr *>(E))
    return findVariableExpr(Unary->RHS.get());
  return nullptr;
}

static bool isNullableCedeSource(const Expr *expr) {
  auto *cede = dynamic_cast<const CedeExpr *>(expr);
  if (!cede || !cede->Value)
    return false;

  const Expr *source = cede->Value.get();
  while (auto *cast = dynamic_cast<const CastExpr *>(source))
    source = cast->Expression.get();
  if (!source || !source->ResolvedType)
    return false;

  auto sourceType = source->ResolvedType;
  auto sourceSoul = sourceType->getSoulType();
  return sourceType->IsNullable ||
         (sourceSoul && sourceSoul->IsNullable);
}

static bool isNullableCedeDestination(const std::shared_ptr<Type> &type) {
  if (!type)
    return false;
  auto soul = type->getSoulType();
  return type->IsNullable || (soul && soul->IsNullable);
}

void Sema::validateAtomicOrderingArguments(
    const FunctionDecl *Fn,
    const std::vector<std::unique_ptr<Expr>> &Arguments,
    size_t omittedLeadingArguments) {
  const FunctionDecl *atomicDecl = Fn;
  while (atomicDecl && atomicDecl->TemplateOrigin)
    atomicDecl = atomicDecl->TemplateOrigin;

  const bool trustedAtomicDeclaration =
      atomicDecl && (atomicDecl->IsTrustedAtomicIntrinsic ||
                     TrustedAtomicWrapperDeclarations.count(atomicDecl));
  if (!trustedAtomicDeclaration)
    return;

  std::string atomicOperation = atomicDecl->Name;
  if (atomicOperation.rfind("__toka_atomic_", 0) == 0)
    atomicOperation.erase(0, 14);

  static const std::set<std::string> AtomicOperations = {
      "load",       "store",      "fetch_add", "fetch_sub",
      "fetch_and",  "fetch_or",   "fetch_xor", "swap",
      "compare_exchange", "fence", "fence_acquire", "fence_release"};
  if (!AtomicOperations.count(atomicOperation))
    return;

  static const std::array<const char *, 5> OrderingNames = {
      "Relaxed", "Release", "Acquire", "AcqRel", "SeqCst"};

  auto argumentIndex = [&](size_t formalIndex) -> std::optional<size_t> {
    if (formalIndex < omittedLeadingArguments)
      return std::nullopt;
    const size_t index = formalIndex - omittedLeadingArguments;
    if (index >= Arguments.size())
      return std::nullopt;
    return index;
  };

  auto literalOrderingTag = [&](size_t formalIndex)
      -> std::optional<unsigned> {
    auto index = argumentIndex(formalIndex);
    if (!index)
      return std::nullopt;
    auto *member = dynamic_cast<MemberExpr *>(Arguments[*index].get());
    if (!member || !member->IsStatic || member->Index < 0 ||
        member->Index >= static_cast<int>(OrderingNames.size()) ||
        member->Member != OrderingNames[member->Index] ||
        !member->ResolvedType)
      return std::nullopt;

    auto shape = std::dynamic_pointer_cast<ShapeType>(
        member->ResolvedType->getSoulType());
    if (!shape || !shape->Decl || shape->Decl->Name != "Ordering")
      return std::nullopt;
    return static_cast<unsigned>(member->Index);
  };

  auto rejectOrdering = [&](size_t formalIndex, unsigned tag,
                            const std::string &reason) {
    auto index = argumentIndex(formalIndex);
    if (!index)
      return;
    error(Arguments[*index].get(), DiagID::ERR_ATOMIC_ORDERING_INVALID,
          atomicOperation, OrderingNames[tag], reason);
  };

  if (atomicOperation == "load") {
    if (auto tag = literalOrderingTag(1); tag && (*tag == 1 || *tag == 3))
      rejectOrdering(1, *tag,
                     "load permits Relaxed, Acquire, or SeqCst");
  } else if (atomicOperation == "store") {
    if (auto tag = literalOrderingTag(2); tag && (*tag == 2 || *tag == 3))
      rejectOrdering(2, *tag,
                     "store permits Relaxed, Release, or SeqCst");
  } else if (atomicOperation == "fence") {
    if (auto tag = literalOrderingTag(0); tag && *tag == 0)
      rejectOrdering(
          0, *tag,
          "fence permits Acquire, Release, AcqRel, or SeqCst");
  } else if (atomicOperation == "compare_exchange") {
    auto success = literalOrderingTag(3);
    auto failure = literalOrderingTag(4);
    if (failure && (*failure == 1 || *failure == 3)) {
      rejectOrdering(
          4, *failure,
          "failure ordering permits Relaxed, Acquire, or SeqCst");
    } else if (success && failure) {
      static const bool ValidFailure[5][5] = {
          {true, false, false, false, false},
          {true, false, false, false, false},
          {true, false, true, false, false},
          {true, false, true, false, false},
          {true, false, true, false, true}};
      if (!ValidFailure[*success][*failure])
        rejectOrdering(
            4, *failure,
            std::string("failure ordering must not be stronger than success ordering '") +
                OrderingNames[*success] + "'");
    }
  }
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

  // Generic bodies keep type-parameter spellings in expression syntax.  If
  // an instantiated parameter names a primitive, normalize the synthetic
  // constructor call (for example the `T(value)` inside `unsafe alloc
  // T(value)`) before the ordinary primitive-constructor path below.
  if (CurrentScope) {
    SymbolInfo typeAlias;
    if (CurrentScope->lookup(CallName, typeAlias) && typeAlias.IsTypeAlias &&
        typeAlias.TypeObj) {
      auto aliasType = resolveType(typeAlias.TypeObj);
      if (auto aliasPrimitive = std::dynamic_pointer_cast<PrimitiveType>(
              aliasType ? aliasType->getSoulType() : nullptr);
          aliasPrimitive &&
          isPrimitiveValueConstructorName(aliasPrimitive->Name)) {
        CallName = aliasPrimitive->Name;
        Call->Callee = CallName;
      }
    }
  }

  // 1. Primitives (Constructors/Casts) e.g. i32(42)
  if (isPrimitiveValueConstructorName(CallName)) {
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
              return toka::Type::fromString("()");
          }
      }
      error(Call, DiagID::ERR_SEMA_COMPILE_ERROR_REQUIRES_A_STRING_LITERAL);
      return toka::Type::fromString("()");
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
          return toka::Type::fromString("()");
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
          return toka::Type::fromString("()");
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
          return toka::Type::fromString("()");
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
              return toka::Type::fromString("()");
          }
      }

      return toka::Type::fromString("()");
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
      return toka::Type::fromString("()");
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
            return toka::Type::fromString("()");
        }
      }
      auto strTy = resolveType(toka::Type::fromString("string"));
      if (!strTy) strTy = resolveType(toka::Type::fromString("String"));
      if (!strTy) strTy = resolveType(toka::Type::fromString("std::string::String"));
      if (!strTy) strTy = toka::Type::fromString("string");
      return strTy;
    }
    return toka::Type::fromString("()");
  }

  std::shared_ptr<toka::FunctionType> funcType = nullptr;

  // Every callable route has the same consuming-parameter contract.  Static
  // methods and callable values do not reach the ordinary function-call loop,
  // so keep the nullable guard and explicit-transfer checks here rather than
  // letting those alternate routes weaken a declared `cede` parameter.
  auto checkCedeArgument =
      [&](Expr *argument, const FunctionDecl::Arg &param,
          const std::shared_ptr<Type> &argumentType,
          const std::shared_ptr<Type> &destinationType) {
        if (!param.IsCeded)
          return;

        const bool callerCeded = dynamic_cast<CedeExpr *>(argument) != nullptr;
        if (callerCeded && isNullableCedeSource(argument) &&
            !isNullableCedeDestination(destinationType)) {
          error(argument, DiagID::ERR_SEMA_CEDE_NULLABLE_REQUIRES_GUARD);
        }

        const bool isExempt = canImplicitlyPassToCede(argumentType);
        if (!callerCeded && !isExempt) {
          error(argument,
                DiagID::ERR_SEMA_ARGUMENT_MUST_BE_EXPLICITLY_PASSED_WITH_2);
          if (param.Loc.isValid())
            DiagnosticEngine::report(param.Loc, DiagID::NOTE_GENERIC,
                                     "cede parameter declared here");
        }
        const std::string subject = getPathString(argument);
        recordDecision(
            argument, SemanticRuleID::OwnCede001,
            SemanticOperation::CedeObligation,
            (!callerCeded && !isExempt) ? SemanticDecision::Reject
                                        : SemanticDecision::Allow,
            (!callerCeded && !isExempt) ? SemanticReason::MissingExplicitCede
                                        : SemanticReason::CedeConsumed,
            subject, param.Name, param.Loc);
        SemanticEvidence::recordCedeObligation(
            CedeObligationStage::CallerTransfer,
            (!callerCeded && !isExempt) ? CedeObligationStatus::Violated
                                         : CedeObligationStatus::Fulfilled,
            (!callerCeded && !isExempt) ? SemanticReason::MissingExplicitCede
                                         : SemanticReason::CedeConsumed,
            subject, param.Name, getLoc(argument), param.Loc);
      };

  auto isIndependentCedeTransfer = [&](Expr *argument,
                                        const FunctionDecl::Arg &param) {
    return param.IsCeded && dynamic_cast<CedeExpr *>(argument) != nullptr &&
           getPermissionFlow(argument).Kind == PermissionFlowKind::Independent;
  };

  // 3. Resolve Static Methods / Enum Variants
  size_t pos = CallName.find("::");
  if (pos != std::string::npos) {
    std::string RawPrefix = CallName.substr(0, pos);
    bool staticTypeVisible = isTypeNameVisible(RawPrefix, getLoc(Call));
    if (!staticTypeVisible &&
        (ShapeMap.count(RawPrefix) || TypeAliasMap.count(RawPrefix))) {
      DiagnosticEngine::report(getLoc(Call), DiagID::ERR_UNDEFINED_TYPE,
                               RawPrefix);
      HasError = true;
      return toka::Type::fromString("unknown");
    }
    std::string ShapeName = resolveType(RawPrefix);
    // [FIX] resolveType might return a type string "SharedPtr_M_i32" or
    // similar. If it contains sigils, strip them for Map lookup.
    ShapeName = Type::stripMorphology(ShapeName);

    std::string VariantName = CallName.substr(pos + 2);

    auto staticType =
        staticTypeVisible
            ? resolveType(toka::Type::fromString(RawPrefix))
            : nullptr;
    auto staticShapeType = std::dynamic_pointer_cast<ShapeType>(
        staticType ? staticType->getSoulType() : nullptr);
    ShapeDecl *staticShape =
        staticShapeType ? staticShapeType->Decl : nullptr;
    if (!staticShape) {
      staticShape = staticTypeVisible
                        ? findVisibleShapeDecl(ShapeName, getLoc(Call))
                        : nullptr;
    }
    if (!staticShape && staticTypeVisible)
      staticShape = findVisibleShapeDecl(RawPrefix, getLoc(Call));
    if (staticShape) {
      const std::string methodKey =
          staticShape->CodegenName.empty() ? staticShape->Name
                                           : staticShape->CodegenName;
      // Update CallName and Callee for subsequent lookup and CodeGen
      CallName = methodKey + "::" + VariantName;
      Call->Callee = CallName;

      // Static Method
      if (MethodMap.count(methodKey) &&
          MethodMap[methodKey].count(VariantName)) {
        FunctionDecl *MetAST = nullptr;
        if (MethodDecls.count(methodKey) &&
            MethodDecls[methodKey].count(VariantName)) {
            MetAST = MethodDecls[methodKey][VariantName];
        }
        Call->ResolvedFn = MetAST;

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
              expectedTy = MetAST->Args[i].ResolvedType;
              if (!expectedTy) {
                std::string tyStr = MetAST->Args[i].Type;
                if (MetAST->Args[i].IsRawPointer) tyStr = "*" + tyStr;
                else if (MetAST->Args[i].IsUnique) tyStr = "^" + tyStr;
                else if (MetAST->Args[i].IsShared) tyStr = "~" + tyStr;
                else if (MetAST->Args[i].IsReference) tyStr = "&" + tyStr;
                expectedTy = resolveType(toka::Type::fromString(tyStr), false);
              }
          }
          bool oldAllowPermissionSuffix = m_AllowPermissionSuffix;
          m_AllowPermissionSuffix =
              hasExplicitCallArgumentWriteSigil(Call->Args[i].get());
          auto argTy = checkExpr(Call->Args[i].get(), expectedTy);
          m_AllowPermissionSuffix = oldAllowPermissionSuffix;
          if (MetAST && i < MetAST->Args.size()) {
            const auto &param = MetAST->Args[i];
            checkCedeArgument(Call->Args[i].get(), param, argTy, expectedTy);
            AccessCapability declaredCapability =
                getAccessCapability(Call->Args[i].get(), true);
            AccessCapability argCapability =
                getAccessCapability(Call->Args[i].get());
            AccessIntent argIntent = getAccessIntent(Call->Args[i].get());
            const bool paramIsHatted =
                param.IsRawPointer || param.IsUnique || param.IsShared ||
                param.IsReference;
            const bool lacksHandleCapability =
                paramIsHatted && param.IsRebindable &&
                (!declaredCapability.HandleRebindable ||
                 !argCapability.HandleRebindable || !argIntent.HandleRebind);
            const bool lacksPayloadCapability =
                param.IsValueMutable &&
                !isIndependentCedeTransfer(Call->Args[i].get(), param) &&
                (!declaredCapability.PayloadWritable ||
                 !argCapability.PayloadWritable || !argIntent.PayloadWrite);
            if (lacksHandleCapability || lacksPayloadCapability) {
              error(Call->Args[i].get(),
                    DiagID::ERR_SEMA_TYPE_MISMATCH_FOR_ARGUMENT_EXPECTED_GOT,
                    std::to_string(i + 1),
                    expectedTy ? expectedTy->getSoulName()
                               : "capable argument",
                    argTy ? argTy->getSoulName() : "unknown");
            }
            validateCallArgumentMutSigil(Call->Args[i].get(),
                                         param.IsValueMutable, param.Name,
                                         param.Loc, Call->Loc, i);
          }
          if (MetAST && MetAST->Effect == EffectKind::Async &&
              i < MetAST->Args.size()) {
            checkStartBoundaryArgument(
                Call->Args[i].get(), argTy, MetAST->Args[i].IsCeded,
                dynamic_cast<CedeExpr *>(Call->Args[i].get()) != nullptr,
                MetAST->Args[i].Name);
          }
          if (expectedTy && !isTypeCompatible(expectedTy, argTy)) {
              DiagnosticEngine::report(getLoc(Call->Args[i].get()), DiagID::ERR_TYPE_MISMATCH,
                                       "Argument " + std::to_string(i + 1) + " (actual: " + argTy->getSoulName() + ")", expectedTy->getSoulName(), argTy->getSoulName());
              HasError = true;
          }
        }
        auto resolvedRet =
            MetAST && MetAST->ResolvedReturnType
                ? resolveType(MetAST->ResolvedReturnType)
                : resolveType(toka::Type::fromString(
                      MethodMap[methodKey][VariantName]));
        
        // [FIX] Check if Static Method is async and wrap in TaskHandle
        if (MetAST && MetAST->Effect == EffectKind::Async) {
            return resolveType(std::make_shared<ShapeType>(
                "TaskHandle",
                std::vector<std::shared_ptr<toka::Type>>{resolvedRet}));
        }
        return resolvedRet;
      } else {
        // [NEW] Lazy Impl Instantiation
        std::string BaseName = RawPrefix;
        size_t lt = BaseName.find('<');
        if (lt != std::string::npos) {
          BaseName = BaseName.substr(0, lt);
          const std::string implKey = genericImplKey(BaseName, getLoc(Call));
          if (GenericImplMap.count(implKey)) {
            // [FIX] Pass generic arguments to instantiateGenericImpl
            std::vector<std::shared_ptr<toka::Type>> genericArgs;
            if (staticShape && !staticShape->InstantiationArgs.empty())
              genericArgs = staticShape->InstantiationArgs;
            else if (auto parsed = Type::fromString(RawPrefix);
                     auto *ST = dynamic_cast<ShapeType *>(parsed.get()))
              genericArgs = ST->GenericArgs;
            for (auto *ImplTemplate : GenericImplMap[implKey]) {
              instantiateGenericImpl(ImplTemplate, ShapeName, genericArgs,
                                     staticShape);
            }
            // Retry lookup
            if (MethodMap.count(methodKey) &&
                MethodMap[methodKey].count(VariantName)) {
              FunctionDecl *MetAST = nullptr;
              if (MethodDecls.count(methodKey) &&
                  MethodDecls[methodKey].count(VariantName)) {
                  MetAST = MethodDecls[methodKey][VariantName];
              }
              Call->ResolvedFn = MetAST;

              for (size_t i = 0; i < Call->Args.size(); ++i) {
                Call->Args[i] = foldGenericConstant(std::move(Call->Args[i]));
                std::shared_ptr<toka::Type> expectedTy = nullptr;
                if (MetAST && i < MetAST->Args.size()) {
                    expectedTy = MetAST->Args[i].ResolvedType;
                    if (!expectedTy) {
                      std::string tyStr = MetAST->Args[i].Type;
                      if (MetAST->Args[i].IsRawPointer) tyStr = "*" + tyStr;
                      else if (MetAST->Args[i].IsUnique) tyStr = "^" + tyStr;
                      else if (MetAST->Args[i].IsShared) tyStr = "~" + tyStr;
                      else if (MetAST->Args[i].IsReference) tyStr = "&" + tyStr;
                      expectedTy = resolveType(toka::Type::fromString(tyStr), false);
                    }
                }
                bool oldAllowPermissionSuffix = m_AllowPermissionSuffix;
                m_AllowPermissionSuffix =
                    hasExplicitCallArgumentWriteSigil(Call->Args[i].get());
                auto argTy = checkExpr(Call->Args[i].get(), expectedTy);
                m_AllowPermissionSuffix = oldAllowPermissionSuffix;
                if (MetAST && i < MetAST->Args.size()) {
                  const auto &param = MetAST->Args[i];
                  checkCedeArgument(Call->Args[i].get(), param, argTy,
                                    expectedTy);
                  AccessCapability declaredCapability =
                      getAccessCapability(Call->Args[i].get(), true);
                  AccessCapability argCapability =
                      getAccessCapability(Call->Args[i].get());
                  AccessIntent argIntent = getAccessIntent(Call->Args[i].get());
                  const bool paramIsHatted =
                      param.IsRawPointer || param.IsUnique || param.IsShared ||
                      param.IsReference;
                  const bool lacksHandleCapability =
                      paramIsHatted && param.IsRebindable &&
                      (!declaredCapability.HandleRebindable ||
                       !argCapability.HandleRebindable ||
                       !argIntent.HandleRebind);
                  const bool lacksPayloadCapability =
                      param.IsValueMutable &&
                      !isIndependentCedeTransfer(Call->Args[i].get(), param) &&
                      (!declaredCapability.PayloadWritable ||
                       !argCapability.PayloadWritable ||
                       !argIntent.PayloadWrite);
                  if (lacksHandleCapability || lacksPayloadCapability) {
                    error(Call->Args[i].get(),
                          DiagID::ERR_SEMA_TYPE_MISMATCH_FOR_ARGUMENT_EXPECTED_GOT,
                          std::to_string(i + 1),
                          expectedTy ? expectedTy->getSoulName()
                                     : "capable argument",
                          argTy ? argTy->getSoulName() : "unknown");
                  }
                  validateCallArgumentMutSigil(Call->Args[i].get(),
                                               param.IsValueMutable, param.Name,
                                               param.Loc, Call->Loc, i);
                }
                if (MetAST && MetAST->Effect == EffectKind::Async &&
                    i < MetAST->Args.size()) {
                  checkStartBoundaryArgument(
                      Call->Args[i].get(), argTy, MetAST->Args[i].IsCeded,
                      dynamic_cast<CedeExpr *>(Call->Args[i].get()) != nullptr,
                      MetAST->Args[i].Name);
                }
                if (expectedTy && !isTypeCompatible(expectedTy, argTy)) {
                    DiagnosticEngine::report(getLoc(Call->Args[i].get()), DiagID::ERR_TYPE_MISMATCH,
                                             "Argument " + std::to_string(i + 1) + " (actual: " + argTy->getSoulName() + ")", expectedTy->getSoulName(), argTy->getSoulName());
                    HasError = true;
                }
              }
              auto resolvedRet =
                  MetAST && MetAST->ResolvedReturnType
                      ? resolveType(MetAST->ResolvedReturnType)
                      : resolveType(toka::Type::fromString(
                            MethodMap[methodKey][VariantName]));
              
              // [FIX] Check if Static Method is async and wrap in TaskHandle
              if (MetAST && MetAST->Effect == EffectKind::Async) {
                  return resolveType(std::make_shared<ShapeType>(
                      "TaskHandle",
                      std::vector<std::shared_ptr<toka::Type>>{resolvedRet}));
              }
              return resolvedRet;
            }
          }
        }
      }
      // Enum Variant Constructor
      ShapeDecl *SD = staticShape;
      if (SD->Kind == ShapeKind::Enum) {
        for (auto &Memb : SD->Members) {
          if (Memb.Name == VariantName) {
            // Enum Variant Constructor: Variant(Args...) -> ShapeName
            bool hasPayload = !Memb.IsUnitVariant &&
                              (!Memb.SubMembers.empty() || !Memb.Type.empty());
            size_t expectedCount = 0;
            if (!Memb.SubMembers.empty()) {
              expectedCount = Memb.SubMembers.size();
            } else if (!Memb.IsUnitVariant && !Memb.Type.empty()) {
              expectedCount = 1;
            }

            if (!hasPayload) {
              if (!Call->Args.empty()) {
                error(Call, DiagID::ERR_VARIANT_NO_PAYLOAD, VariantName);
              }
            } else if (Call->Args.size() != expectedCount) {
              error(Call, DiagID::ERR_VARIANT_ARG_MISMATCH, VariantName,
                    expectedCount, Call->Args.size());
            }

            for (size_t i = 0; i < Call->Args.size(); ++i) {
              auto &Arg = Call->Args[i];
              Arg = foldGenericConstant(std::move(Arg)); // [FIX]
              checkExpr(Arg.get());
            }

            // Set ResolvedShape for CodeGen
            Call->ResolvedShape = SD;
            auto result = std::make_shared<toka::ShapeType>(ShapeName);
            result->resolve(SD);
            return result;
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

  // A generic body retains its type-parameter spelling and resolves it
  // through the exact alias installed by the instantiation scope.  Constructor
  // calls such as `T(..)` must therefore consult that alias before treating
  // the callee as an ordinary value/function name.
  if (CurrentScope) {
    SymbolInfo typeAlias;
    if (CurrentScope->lookup(CallName, typeAlias) && typeAlias.IsTypeAlias &&
        typeAlias.TypeObj) {
      auto aliasType = resolveType(typeAlias.TypeObj);
      if (auto aliasShape = std::dynamic_pointer_cast<ShapeType>(
              aliasType ? aliasType->getSoulType() : nullptr)) {
        if (aliasShape->Decl) {
          Sh = aliasShape->Decl;
          CallName = aliasShape->Decl->CodegenName.empty()
                         ? aliasShape->Decl->Name
                         : aliasShape->Decl->CodegenName;
          Call->Callee = CallName;
        }
      }
    }
  }

  auto functionAcceptsCall = [&](FunctionDecl *Candidate) -> bool {
    if (!Candidate) {
      return false;
    }
    if (!Candidate->GenericParams.empty() || !Call->GenericArgs.empty()) {
      return false;
    }

    size_t provided = Call->Args.size();
    size_t params = Candidate->Args.size();
    size_t required = params;
    while (required > 0 && Candidate->Args[required - 1].DefaultValue) {
      --required;
    }

    if (provided < required) {
      return false;
    }
    if (!Candidate->IsVariadic && provided > params) {
      return false;
    }

    struct FlowSnapshot {
      Scope *ScopePtr;
      std::map<std::string, bool> Moved;
      std::map<std::string, ExactPlaceFacts> ExactPlaces;
    };
    std::vector<FlowSnapshot> flowSnapshot;
    for (Scope *scope = CurrentScope; scope; scope = scope->Parent) {
      std::map<std::string, bool> moved;
      std::map<std::string, ExactPlaceFacts> exactPlaces;
      for (const auto &entry : scope->Symbols) {
        moved[entry.first] = entry.second.Moved;
        exactPlaces[entry.first] = entry.second.ExactPlace;
      }
      flowSnapshot.push_back(
          {scope, std::move(moved), std::move(exactPlaces)});
    }

    auto restoreFlow = [&]() {
      for (auto &entry : flowSnapshot) {
        Scope *scope = entry.ScopePtr;
        for (const auto &moved : entry.Moved) {
          auto it = scope->Symbols.find(moved.first);
          if (it != scope->Symbols.end()) {
            it->second.Moved = moved.second;
          }
        }
        for (const auto &exactPlace : entry.ExactPlaces) {
          auto it = scope->Symbols.find(exactPlace.first);
          if (it != scope->Symbols.end()) {
            it->second.ExactPlace = exactPlace.second;
            it->second.InitMask =
                exactPlace.second.applyToLegacyInitMask(it->second.InitMask);
          }
        }
      }
    };

    bool accepts = true;
    size_t fixedCount = std::min(provided, params);
    for (size_t i = 0; i < fixedCount; ++i) {
      auto expectedType =
          resolveType(Sema::synthesizePhysicalTypeObject(Candidate->Args[i]));
      if (Candidate->Args[i].IsInit) {
        if (!Call->isInitArgument(i) ||
            !dynamic_cast<VariableExpr *>(Call->Args[i].get())) {
          accepts = false;
          break;
        }
        continue;
      }
      if (Call->isInitArgument(i)) {
        accepts = false;
        break;
      }
      bool oldAllowPermissionSuffix = m_AllowPermissionSuffix;
      m_AllowPermissionSuffix =
          hasExplicitCallArgumentWriteSigil(Call->Args[i].get());
      auto argType = checkExpr(Call->Args[i].get());
      m_AllowPermissionSuffix = oldAllowPermissionSuffix;
      if (!expectedType || !argType || expectedType->isUnknown() ||
          argType->isUnknown()) {
        continue;
      }
      if (!isTypeCompatible(expectedType, argType)) {
        accepts = false;
        break;
      }
    }
    restoreFlow();
    return accepts;
  };

  auto pickModuleFunction = [&](ModuleScope *Target, const std::string &Name,
                                FunctionDecl *Fallback) -> FunctionDecl * {
    if (!Target) {
      return Fallback;
    }
    auto it = Target->FunctionOverloads.find(Name);
    if (it == Target->FunctionOverloads.end() || it->second.empty()) {
      return Fallback;
    }

    FunctionDecl *match = nullptr;
    size_t matchCount = 0;
    for (auto *Candidate : it->second) {
      if (functionAcceptsCall(Candidate)) {
        match = Candidate;
        ++matchCount;
      }
    }
    return matchCount == 1 ? match : Fallback;
  };

  auto lexicalModuleForCall = [&]() -> ModuleScope * {
    SourceLocation loc = Call->Loc;
    if (!loc.isValid() && CurrentFunction)
      loc = CurrentFunction->Loc;
    return getLexicalModule(loc);
  };

  // [NEW] Generic Constructor Pre-Check
  // If CallName looks like a generic type "Box<i32>", try to resolve it
  // as a Type. This triggers monomorphization in resolveType.
  if (CallName.find('<') != std::string::npos) {
    std::string genericBase = CallName.substr(0, CallName.find('<'));
    auto possibleType = toka::Type::fromString(CallName);
    if (isTypeNameVisible(genericBase, getLoc(Call)) && possibleType &&
        !possibleType->isUnknown()) {
      auto resolved = resolveType(possibleType);
      if (auto shapeT = std::dynamic_pointer_cast<toka::ShapeType>(resolved)) {
        if (shapeT->Decl) {
          Sh = shapeT->Decl;
          Call->Callee = shapeT->Name; // Update Call Name to Mangled
                                       // Name for CodeGen!
          CallName = shapeT->Name;     // Update local var for verification
                                       // logic below

          // [Legacy] Bare union constructor: check member name if provided
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
          Fn = pickModuleFunction(target, FuncName, target->Functions[FuncName]);
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
    SymbolInfo *visibleSymbol = nullptr;
    std::string visibleName = CallName;
    bool hasVisibleSymbol = CurrentScope->findVariableWithDeref(
        CallName, visibleSymbol, visibleName);

    if (hasVisibleSymbol && visibleSymbol->TypeObj &&
        visibleSymbol->TypeObj->toString() == "extern" &&
        ExternMap.count(CallName))
      Ext = ExternMap[CallName];
    else if (isTypeNameVisible(CallName, getLoc(Call)))
      Sh = findVisibleShapeDecl(CallName, getLoc(Call));

    // Fallback: Check if it's a type alias to a shape
    if (!Fn && !Ext && !Sh && isTypeNameVisible(CallName, getLoc(Call)) &&
        TypeAliasMap.count(CallName)) {
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
    // A callable invocation is still a use of its binding.  It must observe
    // the same cede invalidation state as a field read or ordinary variable
    // expression; otherwise a transferred callback could be invoked again.
    if (hasPlaceState(symPtr->placeFact(), PlaceState::Moved)) {
      error(Call, DiagID::ERR_USE_MOVED, actualCallName);
      recordDecision(Call, SemanticRuleID::OwnMove001,
                     SemanticOperation::OwnershipTransfer,
                     SemanticDecision::Reject, SemanticReason::AlreadyMoved,
                     Type::stripMorphology(actualCallName),
                     Type::stripMorphology(actualCallName), symPtr->MoveLoc);
      if (symPtr->MoveLoc.isValid())
        DiagnosticEngine::report(symPtr->MoveLoc, DiagID::NOTE_GENERIC,
                                 "value moved here");
      return toka::Type::fromString("unknown");
    }
    symPtr->HasBeenUsed = true;
    if (symPtr->ImportingDecl) {
      const_cast<ImportDecl*>(symPtr->ImportingDecl)->HasBeenUsed = true;
    }
    if (sym.TypeObj && sym.TypeObj->toString() == "fn" && sym.ASTPtr) {
      auto *symFn = static_cast<FunctionDecl *>(sym.ASTPtr);
      Fn = pickModuleFunction((ModuleScope *)sym.ReferencedModule, symFn->Name,
                              symFn);
    } else if (sym.TypeObj && sym.TypeObj->toString() == "extern" &&
               sym.ASTPtr) {
      Ext = static_cast<ExternDecl *>(sym.ASTPtr);
    }

    // Formal callable invocation. Closures and user-defined callable values
    // share the same receiver-permission path.
    if (!Fn && !Ext && !Sh && sym.TypeObj) {
      if (auto sTy = std::dynamic_pointer_cast<ShapeType>(sym.TypeObj)) {
        std::string shapeName =
            sTy->Decl ? (sTy->Decl->CodegenName.empty()
                             ? sTy->Decl->Name
                             : sTy->Decl->CodegenName)
                      : sTy->getSoulName();
        bool hasCall = MethodDecls.count(shapeName) &&
                       MethodDecls[shapeName].count("call");
        bool isCallable = ImplMap.count(shapeName + "@Callable") != 0;
        if (hasCall && !isCallable) {
          error(Call, DiagID::ERR_SEMA_CALLABLE_PROTOCOL_REQUIRED, shapeName);
          return toka::Type::fromString("unknown");
        } else if (hasCall && isCallable) {
          FunctionDecl *invokeFn = MethodDecls[shapeName]["call"];
          if (Call->ResolvedFn == invokeFn &&
              Call->Args.size() == invokeFn->Args.size()) {
            return invokeFn->ResolvedReturnType
                       ? invokeFn->ResolvedReturnType
                       : toka::Type::fromString(MethodMap[shapeName]["call"]);
          }
          CallableReceiverMode required = invokeFn->ClosureReceiver;
          if (!invokeFn->IsClosureInvoke && !invokeFn->Args.empty()) {
            if (invokeFn->Args[0].IsCeded)
              required = CallableReceiverMode::Consuming;
            else if (invokeFn->Args[0].IsValueMutable)
              required = CallableReceiverMode::Mutable;
          }

          if (required == CallableReceiverMode::Consuming &&
              Call->CallableReceiver != CallableReceiverMode::Consuming) {
            error(Call, DiagID::ERR_SEMA_CALLABLE_CONSUME_REQUIRED, CallName,
                  CallName);
          } else if (required == CallableReceiverMode::Mutable &&
                     Call->CallableReceiver == CallableReceiverMode::Shared) {
            error(Call, DiagID::ERR_SEMA_CALLABLE_MUTABLE_REQUIRED, CallName,
                  CallName);
          }
          if (required != CallableReceiverMode::Consuming &&
              Call->CallableReceiver == CallableReceiverMode::Consuming) {
            Call->CallableReceiver = required;
          }
          if (required == CallableReceiverMode::Mutable && symPtr &&
              !symPtr->IsDeclaredMutable) {
            error(Call, DiagID::ERR_SEMA_CALLABLE_NOT_WRITABLE, CallName);
          }
          if (required == CallableReceiverMode::Mutable && symPtr) {
            // An exclusive callable invocation may mutate its captured
            // payload.  It consumes mutable capability just like a writable
            // argument or receiver call for warning purposes.
            symPtr->HasBeenMutated = true;
          }

          AccessPath callablePath =
              canonicalizeAccessPath(makeAccessPath(CallName));
          std::optional<PALConflict> conflict;
          if (required == CallableReceiverMode::Consuming)
            conflict = PALCheckerState.verifyInvalidation(callablePath);
          else if (required == CallableReceiverMode::Mutable)
            conflict = PALCheckerState.verifyExclusiveMutation(callablePath);
          else
            conflict = PALCheckerState.verifyAccess(callablePath);
          if (conflict) {
            error(Call, required == CallableReceiverMode::Consuming
                            ? DiagID::ERR_MOVE_BORROWED
                            : DiagID::ERR_BORROW_MUT,
                  conflict->displayPath());
          }
          
          if (Call->Args.size() != invokeFn->Args.size() - 1) {
             error(Call, DiagID::ERR_SEMA_CLOSURE_EXPECTS_ARGUMENTS_BUT_GOT, std::to_string(invokeFn->Args.size() - 1), std::to_string(Call->Args.size()));
          } else {
             for (size_t i = 0; i < Call->Args.size(); ++i) {
                Call->Args[i] = foldGenericConstant(std::move(Call->Args[i]));
                auto expectedTy = invokeFn->Args[i + 1].ResolvedType
                                      ? invokeFn->Args[i + 1].ResolvedType
                                      : resolveType(
                                            Sema::synthesizePhysicalTypeObject(
                                                invokeFn->Args[i + 1]));
                bool oldAllowPermissionSuffix = m_AllowPermissionSuffix;
                m_AllowPermissionSuffix =
                    hasExplicitCallArgumentWriteSigil(Call->Args[i].get());
                auto argTy = checkExpr(Call->Args[i].get(), expectedTy);
                m_AllowPermissionSuffix = oldAllowPermissionSuffix;
                const auto &param = invokeFn->Args[i + 1];
                checkCedeArgument(Call->Args[i].get(), param, argTy,
                                  expectedTy);
                AccessCapability declaredCapability =
                    getAccessCapability(Call->Args[i].get(), true);
                AccessCapability argCapability =
                    getAccessCapability(Call->Args[i].get());
                AccessIntent argIntent = getAccessIntent(Call->Args[i].get());
                const bool paramIsHatted =
                    param.IsRawPointer || param.IsUnique || param.IsShared ||
                    param.IsReference;
                const bool lacksHandleCapability =
                    paramIsHatted && param.IsRebindable &&
                    (!declaredCapability.HandleRebindable ||
                     !argCapability.HandleRebindable ||
                     !argIntent.HandleRebind);
                const bool lacksPayloadCapability =
                    param.IsValueMutable &&
                    !isIndependentCedeTransfer(Call->Args[i].get(), param) &&
                    (!declaredCapability.PayloadWritable ||
                     !argCapability.PayloadWritable ||
                     !argIntent.PayloadWrite);
                if (lacksHandleCapability || lacksPayloadCapability) {
                    error(Call->Args[i].get(),
                          DiagID::ERR_SEMA_TYPE_MISMATCH_FOR_ARGUMENT_EXPECTED_GOT,
                          std::to_string(i + 1), expectedTy->getSoulName(),
                          argTy->getSoulName());
                }
                validateCallArgumentMutSigil(Call->Args[i].get(),
                                             param.IsValueMutable, param.Name,
                                             param.Loc, Call->Loc, i);
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

          auto varE = std::make_unique<VariableExpr>(CallName);
          varE->IsValueMutable = required == CallableReceiverMode::Mutable;
          varE->ResolvedType = sym.TypeObj;
          Call->Args.insert(Call->Args.begin(), std::move(varE));

          if (required == CallableReceiverMode::Consuming) {
            CurrentScope->markMoved(CallName, Call->Loc);
          }
          if (symPtr && !invokeFn->LifeDependencies.empty()) {
            for (const auto &dep : invokeFn->LifeDependencies) {
              if (Type::stripMorphology(dep) == "self") {
                m_LastLifeDependencies.insert(symPtr->LifeDependencySet.begin(),
                                              symPtr->LifeDependencySet.end());
              }
            }
          }

          return invokeFn->ResolvedReturnType
                     ? invokeFn->ResolvedReturnType
                     : toka::Type::fromString(MethodMap[shapeName]["call"]);
        }
      }

      if (sym.TypeObj->isFunction() || sym.TypeObj->isDynFn()) {
        CallableReceiverMode required = symPtr ? symPtr->CallableReceiver
                                               : getCallableReceiverMode(*sym.TypeObj);
        if (required == CallableReceiverMode::Consuming &&
            Call->CallableReceiver != CallableReceiverMode::Consuming) {
          error(Call, DiagID::ERR_SEMA_CALLABLE_CONSUME_REQUIRED, CallName,
                CallName);
        } else if (required == CallableReceiverMode::Mutable &&
                   Call->CallableReceiver == CallableReceiverMode::Shared) {
          error(Call, DiagID::ERR_SEMA_CALLABLE_MUTABLE_REQUIRED, CallName,
                CallName);
        }
        if (required == CallableReceiverMode::Mutable && symPtr &&
            !symPtr->IsDeclaredMutable) {
          error(Call, DiagID::ERR_SEMA_CALLABLE_NOT_WRITABLE, CallName);
        }
        if (required == CallableReceiverMode::Mutable && symPtr) {
          // See the formal-callable path above: an exclusive invocation is a
          // mutable use even if the closure body is opaque at this call site.
          symPtr->HasBeenMutated = true;
        }
        if (required != CallableReceiverMode::Consuming &&
            Call->CallableReceiver == CallableReceiverMode::Consuming)
          Call->CallableReceiver = required;
        if (required == CallableReceiverMode::Consuming)
          CurrentScope->markMoved(CallName, Call->Loc);
      }
    }
  }

  // A generic body is checked at its instantiation site but retains the
  // lexical namespace of its defining module.
  if (!Fn && !Ext && !Sh) {
    auto findLexicalSymbol = [&](ModuleScope *lexical) {
      if (!lexical)
        return;
      auto symbol = lexical->LexicalSymbols.find(CallName);
      if (symbol != lexical->LexicalSymbols.end()) {
        SymbolInfo &lexicalInfo = symbol->second;
        if (lexicalInfo.TypeObj && lexicalInfo.TypeObj->toString() == "fn") {
          Fn = static_cast<FunctionDecl *>(lexicalInfo.ASTPtr);
        } else if (lexicalInfo.TypeObj &&
                   lexicalInfo.TypeObj->toString() == "extern") {
          Ext = static_cast<ExternDecl *>(lexicalInfo.ASTPtr);
        }
        if (Fn || Ext || Sh) {
          lexicalInfo.HasBeenUsed = true;
          if (lexicalInfo.ImportingDecl) {
            const_cast<ImportDecl *>(lexicalInfo.ImportingDecl)->HasBeenUsed =
                true;
          }
        }
      }
    };

    findLexicalSymbol(lexicalModuleForCall());
    if (!Fn && !Ext && !Sh && CurrentFunction) {
      auto owner = DeclarationLexicalScopes.find(CurrentFunction);
      if (owner != DeclarationLexicalScopes.end())
        findLexicalSymbol(owner->second);
    }
    if (!Fn && !Ext && !Sh && CurrentFunction) {
      auto context = InstantiationLexicalScopes.find(CurrentFunction);
      if (context != InstantiationLexicalScopes.end())
        findLexicalSymbol(context->second);
    }
  }

  // Concrete generic instances are compiler-created and are not source-level
  // imports. Keep this internal fallback narrow so GlobalFunctions cannot leak
  // unselected module symbols into the current namespace.
  if (!Fn && !Ext && !Sh) {
    auto instance = InstantiationCache.find(CallName);
    if (instance != InstantiationCache.end())
      Fn = instance->second;
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
            bool oldAllowPermissionSuffix = m_AllowPermissionSuffix;
            m_AllowPermissionSuffix =
                hasExplicitCallArgumentWriteSigil(Call->Args[i].get());
            auto argTy = checkExpr(Call->Args[i].get(), expectedTy);
            m_AllowPermissionSuffix = oldAllowPermissionSuffix;
            AccessCapability declaredCapability =
                getAccessCapability(Call->Args[i].get(), true);
            AccessCapability argCapability =
                getAccessCapability(Call->Args[i].get());
            AccessIntent argIntent = getAccessIntent(Call->Args[i].get());
            const bool paramIsHatted =
                expectedTy->isPointer() || expectedTy->isSmartPointer() ||
                expectedTy->isReference();
            const bool paramNeedsPayload =
                paramIsHatted
                    ? (expectedTy->getPointeeType() &&
                       expectedTy->getPointeeType()->IsWritable)
                    : expectedTy->IsWritable;
            const bool lacksHandleCapability =
                paramIsHatted && expectedTy->IsWritable &&
                (!declaredCapability.HandleRebindable ||
                 !argCapability.HandleRebindable ||
                 !argIntent.HandleRebind);
            const bool lacksPayloadCapability =
                paramNeedsPayload &&
                (!declaredCapability.PayloadWritable ||
                 !argCapability.PayloadWritable || !argIntent.PayloadWrite);
            if (lacksHandleCapability || lacksPayloadCapability) {
                error(Call->Args[i].get(),
                      DiagID::ERR_SEMA_TYPE_MISMATCH_FOR_ARGUMENT_EXPECTED_GOT,
                      std::to_string(i + 1), expectedTy->getSoulName(),
                      argTy->getSoulName());
            }
            validateCallArgumentMutSigil(Call->Args[i].get(), paramNeedsPayload,
                                         "", SourceLocation(), Call->Loc, i);
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
            bool oldAllowPermissionSuffix = m_AllowPermissionSuffix;
            m_AllowPermissionSuffix =
                hasExplicitCallArgumentWriteSigil(Call->Args[i].get());
            auto argTy = checkExpr(Call->Args[i].get(), expectedTy);
            m_AllowPermissionSuffix = oldAllowPermissionSuffix;
            AccessCapability declaredCapability =
                getAccessCapability(Call->Args[i].get(), true);
            AccessCapability argCapability =
                getAccessCapability(Call->Args[i].get());
            AccessIntent argIntent = getAccessIntent(Call->Args[i].get());
            const bool paramIsHatted =
                expectedTy->isPointer() || expectedTy->isSmartPointer() ||
                expectedTy->isReference();
            const bool paramNeedsPayload =
                paramIsHatted
                    ? (expectedTy->getPointeeType() &&
                       expectedTy->getPointeeType()->IsWritable)
                    : expectedTy->IsWritable;
            const bool lacksHandleCapability =
                paramIsHatted && expectedTy->IsWritable &&
                (!declaredCapability.HandleRebindable ||
                 !argCapability.HandleRebindable ||
                 !argIntent.HandleRebind);
            const bool lacksPayloadCapability =
                paramNeedsPayload &&
                (!declaredCapability.PayloadWritable ||
                 !argCapability.PayloadWritable || !argIntent.PayloadWrite);
            if (lacksHandleCapability || lacksPayloadCapability) {
                error(Call->Args[i].get(),
                      DiagID::ERR_SEMA_TYPE_MISMATCH_FOR_ARGUMENT_EXPECTED_GOT,
                      std::to_string(i + 1), expectedTy->getSoulName(),
                      argTy->getSoulName());
            }
            validateCallArgumentMutSigil(Call->Args[i].get(), paramNeedsPayload,
                                         "", SourceLocation(), Call->Loc, i);
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
  std::vector<std::shared_ptr<toka::Type>> precheckedArgTypes(Call->Args.size());

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
          std::shared_ptr<toka::Type> explicitType;
          if (i < Call->GenericArgSyntax.size() &&
              Call->GenericArgSyntax[i].ArgumentKind ==
                  TypeArgumentSyntax::Kind::Type &&
              Call->GenericArgSyntax[i].Type) {
            explicitType =
                toka::Type::fromSyntax(Call->GenericArgSyntax[i].Type);
          } else {
            explicitType = toka::Type::fromString(argStr);
          }
          TypeArgs.push_back(resolveType(explicitType));
        }
      }
    } else {
      // Type Deduction
      std::map<std::string, std::shared_ptr<toka::Type>> Deduced;

      for (size_t i = 0; i < Call->Args.size() && i < Fn->Args.size(); ++i) {
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

        if (!isGeneric) {
          // A generic parameter may be carried by a single-parameter shape
          // such as TaskHandle<'T>.  The ordinary direct-T deduction above
          // deliberately stays simple, but this common wrapper form must not
          // force callers to spell a redundant type argument just to hand a
          // typed handle to a generic library function.
          const size_t open = PType.find('<');
          const size_t close = PType.rfind('>');
          if (open != std::string::npos && close == PType.size() - 1 &&
              PType.find(',', open) == std::string::npos) {
            const std::string outer = PType.substr(0, open);
            const std::string inner = PType.substr(open + 1, close - open - 1);
            std::string matchedGeneric;
            for (const auto &gp : Fn->GenericParams) {
              if (inner == gp.Name ||
                  (gp.IsMorphic && !gp.Name.empty() && gp.Name[0] == '\'' &&
                   inner == gp.Name.substr(1))) {
                matchedGeneric = gp.Name;
                break;
              }
            }

            if (!matchedGeneric.empty()) {
              Call->Args[i] = foldGenericConstant(std::move(Call->Args[i]));
              bool oldAllowPermissionSuffix = m_AllowPermissionSuffix;
              m_AllowPermissionSuffix =
                  hasExplicitCallArgumentWriteSigil(Call->Args[i].get());
              auto argType = checkExpr(Call->Args[i].get());
              m_AllowPermissionSuffix = oldAllowPermissionSuffix;
              precheckedArgTypes[i] = argType;
              std::shared_ptr<toka::Type> candidate;
              auto argShape = std::dynamic_pointer_cast<ShapeType>(
                  argType ? argType->getSoulType() : nullptr);
              if (argShape && argShape->Decl &&
                  argShape->Decl->InstantiationTemplate &&
                  (argShape->Decl->InstantiationTemplate->Name == outer ||
                   argShape->Decl->InstantiationTemplate->CodegenName ==
                       outer) &&
                  argShape->Decl->InstantiationArgs.size() == 1) {
                candidate = argShape->Decl->InstantiationArgs.front();
              } else if (argShape && argShape->Name == outer &&
                         argShape->GenericArgs.size() == 1) {
                candidate = argShape->GenericArgs.front();
              }

              // Compatibility fallback for legacy primitive instances whose
              // physical name remains reversibly encoded as Outer_M_T.
              if (!candidate && argType && !argType->isUnknown()) {
                const std::string argName =
                    Type::stripMorphology(argType->toString());
                const std::string prefix = outer + "_M_";
                if (argName.rfind(prefix, 0) == 0) {
                  candidate = resolveType(toka::Type::fromString(
                      argName.substr(prefix.size())));
                }
              }
              if (candidate) {
                candidate = candidate->withAttributes(
                    false, candidate->IsNullable, candidate->IsBlocked);
                if (Deduced.count(matchedGeneric)) {
                  if (!Deduced[matchedGeneric]->equals(*candidate)) {
                    error(Call, DiagID::ERR_SEMA_TYPE_DEDUCTION_CONFLICT_FOR_VS,
                          matchedGeneric, Deduced[matchedGeneric]->toString(),
                          candidate->toString());
                    deductionFailed = true;
                  }
                } else {
                  Deduced[matchedGeneric] = candidate;
                }
              }
            }
          }
          continue;
        }

        if (auto *todo = dynamic_cast<TodoExpr *>(Call->Args[i].get())) {
          SemanticEvidence::recordTodoGoal(
              todo->TodoId, TodoGoalStatus::Underconstrained, false, "", "",
              "", false, false, false, {}, todo->Loc);
          error(Call->Args[i].get(), DiagID::ERR_TYPED_TODO_UNDERCONSTRAINED);
          deductionFailed = true;
          continue;
        }

        Call->Args[i] = foldGenericConstant(std::move(Call->Args[i])); // [FIX]
        bool oldAllowPermissionSuffix = m_AllowPermissionSuffix;
        m_AllowPermissionSuffix =
            hasExplicitCallArgumentWriteSigil(Call->Args[i].get());
        auto argType = checkExpr(Call->Args[i].get());
        m_AllowPermissionSuffix = oldAllowPermissionSuffix;
        precheckedArgTypes[i] = argType;
        if (!argType || argType->isUnknown())
          continue;

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
    Call->ResolvedFn = Fn;
    for (auto &arg : Fn->Args) {
      ParamTypes.push_back(arg.ResolvedType
                               ? resolveType(arg.ResolvedType)
                               : resolveType(
                                     Sema::synthesizePhysicalTypeObject(arg)));
    }
    ReturnType = Fn->ResolvedReturnType
                     ? Fn->ResolvedReturnType
                     : resolveType(Fn->ReturnTypeSyntax
                                       ? toka::Type::fromSyntax(
                                             Fn->ReturnTypeSyntax)
                                       : toka::Type::fromString(
                                             Fn->ReturnType));
    IsVariadic = Fn->IsVariadic;

    // [Effect] Concurrency Check for Function Call
    if (Fn->Effect != EffectKind::None && !m_IsConsumingEffect && !m_IsPrecomputingCaptures) {
      error(Call, DiagID::ERR_DANGLING_EFFECT, Fn->Name);
      recordDecision(Call, SemanticRuleID::AsyncEffect001,
                     SemanticOperation::EffectConsumption,
                     SemanticDecision::Reject, SemanticReason::DanglingEffect,
                     Fn->Name);
    } else if (Fn->Effect != EffectKind::None && m_IsConsumingEffect &&
               !m_IsPrecomputingCaptures) {
      recordDecision(Call, SemanticRuleID::AsyncEffect001,
                     SemanticOperation::EffectConsumption,
                     SemanticDecision::Allow, SemanticReason::NoConflict,
                     Fn->Name);
    }
  } else if (Ext) {
    Call->ResolvedExtern = Ext;
    for (auto &arg : Ext->Args) {
      ParamTypes.push_back(
          resolveType(Sema::synthesizePhysicalTypeObject(arg)));
    }
    ReturnType = resolveType(Ext->ReturnTypeSyntax
                                 ? toka::Type::fromSyntax(Ext->ReturnTypeSyntax)
                                 : toka::Type::fromString(Ext->ReturnType));
    IsVariadic = Ext->IsVariadic;

    // [Effect] Concurrency Check for Extern Call
    if (Ext->Effect != EffectKind::None && !m_IsConsumingEffect && !m_IsPrecomputingCaptures) {
      error(Call, DiagID::ERR_DANGLING_EFFECT, Ext->Name);
      recordDecision(Call, SemanticRuleID::AsyncEffect001,
                     SemanticOperation::EffectConsumption,
                     SemanticDecision::Reject, SemanticReason::DanglingEffect,
                     Ext->Name);
    } else if (Ext->Effect != EffectKind::None && m_IsConsumingEffect &&
               !m_IsPrecomputingCaptures) {
      recordDecision(Call, SemanticRuleID::AsyncEffect001,
                     SemanticOperation::EffectConsumption,
                     SemanticDecision::Allow, SemanticReason::NoConflict,
                     Ext->Name);
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
                  if (!proveSlice4CopyType(argType)) {
                    error(Call, DiagID::ERR_GENERIC_SEMA,
                          "implicit shape copy requires a proven @Copy witness");
                    return toka::Type::fromString("unknown");
                  }
                  Call->IsIsomorphicCopy = true;
                  return toka::Type::fromString(resolveType(Sh->Name));
              }
          }
      }

      // Check for pun eligibility vs positional prohibition
      // Pure-syntax rule:
      // - 0 arguments: allowed only if Sh->Members.empty()
      // - 1 argument:
      //   - If BinaryExpr "=" or ElisionExpr "..": allowed
      //   - Else: single bare argument is NEVER a pun -> report ERR_STRUCT_POSITIONAL_INIT_PROHIBITED (E042A)
      // - >= 2 arguments:
      //   - Each arg must be either BinaryExpr "=", ElisionExpr "..", or VariableExpr (pun).
      //   - Any other expression (literal, binary op, etc.) -> report ERR_STRUCT_POSITIONAL_INIT_PROHIBITED (E042A).

      bool isSingleArg = (Call->Args.size() == 1);
      if (isSingleArg) {
        auto *firstArg = Call->Args[0].get();
        bool isNamed = false;
        if (auto *bin = dynamic_cast<BinaryExpr *>(firstArg)) {
          if (bin->Op == "=" && dynamic_cast<VariableExpr *>(bin->LHS.get()))
            isNamed = true;
        } else if (dynamic_cast<ElisionExpr *>(firstArg)) {
          isNamed = true;
        }
        if (!isNamed) {
          DiagnosticEngine::report(getLoc(firstArg), DiagID::ERR_STRUCT_POSITIONAL_INIT_PROHIBITED, Sh->Name);
          HasError = true;
          return toka::Type::fromString("unknown");
        }
      }

      std::vector<std::pair<std::string, std::unique_ptr<Expr>>> members;
      for (size_t i = 0; i < Call->Args.size(); ++i) {
        auto *argExpr = Call->Args[i].get();
        if (dynamic_cast<ElisionExpr *>(argExpr)) {
          members.push_back({"..", std::move(Call->Args[i])});
          continue;
        }
        if (auto *bin = dynamic_cast<BinaryExpr *>(argExpr)) {
          if (bin->Op == "=") {
            if (auto *varLHS = dynamic_cast<VariableExpr *>(bin->LHS.get())) {
              members.push_back({varLHS->Name, std::move(bin->RHS)});
              continue;
            }
          }
        }
        if (auto *var = dynamic_cast<VariableExpr *>(argExpr)) {
          members.push_back({var->Name, std::move(Call->Args[i])});
          continue;
        }

        // Positional expression prohibited
        DiagnosticEngine::report(getLoc(argExpr), DiagID::ERR_STRUCT_POSITIONAL_INIT_PROHIBITED, Sh->Name);
        HasError = true;
        return toka::Type::fromString("unknown");
      }

      // Check for non-final elision (.. must be at the very end)
      for (size_t i = 0; i < members.size(); ++i) {
        if (members[i].first == ".." && i != members.size() - 1) {
          error(members[i].second ? members[i].second.get() : Call, DiagID::ERR_ELISION_NOT_AT_END);
        }
      }

      // Route through the exact single-source checkStructInit
      InitStructExpr syntheticInit(Sh->Name, std::move(members));
      syntheticInit.Loc = Call->Loc;
      std::map<std::string, uint64_t> memberMasks;
      auto resultType = checkStructInit(&syntheticInit, Sh, Sh->Name, memberMasks);

      // Reconstruct Call->Args as canonical BinaryExpr("=", var, val)
      std::vector<std::unique_ptr<Expr>> canonicalArgs;
      for (auto &pair : syntheticInit.Members) {
        if (pair.first == "..") continue;
        auto nameVar = std::make_unique<VariableExpr>(pair.first);
        auto bin = std::make_unique<BinaryExpr>("=", std::move(nameVar), std::move(pair.second));
        canonicalArgs.push_back(std::move(bin));
      }
      Call->Args = std::move(canonicalArgs);
      Call->ResolvedShape = Sh;

      if (TypeAliasMap.count(OriginalName) &&
          TypeAliasMap[OriginalName].IsStrong) {
        resultType = toka::Type::fromString(OriginalName);
      }
      return resultType;
    } else if (Sh->Kind == ShapeKind::Union) {

      if (Call->Args.size() != 1) {
        error(Call, DiagID::ERR_SEMA_UNION_REQUIRES_EXACTLY_ONE_ARGUMENT, CallName);
        return toka::Type::fromString("()");
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

        // 1. Calculate legacy union storage size
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

  std::vector<CallArgPALFact> callArgPALFacts;
  std::vector<SymbolInfo *> completedInitPlaces;
  auto isReadOnlyBorrowViewArgument = [&](Expr *expr) -> bool {
    if (!expr)
      return false;
    while (auto *cast = dynamic_cast<CastExpr *>(expr))
      expr = cast->Expression.get();
    if (auto *var = dynamic_cast<VariableExpr *>(expr)) {
      SymbolInfo *info = nullptr;
      std::string actualName = var->Name;
      if (CurrentScope->findVariableWithDeref(var->Name, info, actualName)) {
        return info && info->IsReference() && !info->IsDeclaredMutable;
      }
    }
    if (auto *member = dynamic_cast<MemberExpr *>(expr)) {
      std::string raw = member->Member;
      return raw.find('&') != std::string::npos &&
             raw.find('#') == std::string::npos;
    }
    if (auto *unary = dynamic_cast<UnaryExpr *>(expr)) {
      if (unary->Op == TokenType::Ampersand && !unary->IsValueMutable)
        return true;
    }
    if (auto *addr = dynamic_cast<AddressOfExpr *>(expr)) {
      (void)addr;
      return true;
    }
    return false;
  };

  auto diagnosticTypeName = [&](const std::shared_ptr<toka::Type> &type) {
    if (!type)
      return std::string("unknown");
    auto shape = std::dynamic_pointer_cast<ShapeType>(type->getSoulType());
    if (!shape || !shape->Decl ||
        shape->Decl->CodegenName == shape->Decl->Name)
      return type->toString();
    auto owner = DeclarationLexicalScopes.find(shape->Decl);
    if (owner == DeclarationLexicalScopes.end() || !owner->second)
      return type->toString();
    return owner->second->Name + "::" + type->toString();
  };

  for (size_t i = 0; i < Call->Args.size(); ++i) {
    Call->Args[i] = foldGenericConstant(std::move(Call->Args[i])); // [FIX]

    auto paramType = (i < ParamTypes.size()) ? ParamTypes[i] : nullptr;
    const bool isInitParam = Fn && i < Fn->Args.size() && Fn->Args[i].IsInit;
    const bool isInitArgument = Call->isInitArgument(i);
    const bool expectedCedeTransfer =
        (Fn && i < Fn->Args.size() && Fn->Args[i].IsCeded) ||
        (Ext && i < Ext->Args.size() && Ext->Args[i].IsCeded);

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

    std::shared_ptr<toka::Type> argType;
    if (isInitParam && isInitArgument) {
      auto *place = dynamic_cast<VariableExpr *>(Call->Args[i].get());
      SymbolInfo *placeInfo = nullptr;
      const bool isWholePlainLocal =
          place && !place->IsRawPointer && !place->IsUnique &&
          !place->IsShared && !place->IsValueMutable &&
          !place->IsValueNullable && !place->IsValueBlocked &&
          CurrentScope->findSymbol(place->Name, placeInfo) && placeInfo &&
          placeInfo->IsDeclaredVariable && !placeInfo->IsDeclaredMutable &&
          hasExactlyPlaceState(placeInfo->placeFact(), PlaceState::Never);
      if (!isWholePlainLocal) {
        error(Call->Args[i].get(), DiagID::ERR_INIT_ARGUMENT_INVALID,
              Fn->Args[i].Name);
        argType = toka::Type::fromString("unknown");
      } else {
        argType = placeInfo->TypeObj;
        Call->Args[i]->ResolvedType = argType;
        placeInfo->HasBeenUsed = true;
        completedInitPlaces.push_back(placeInfo);
      }
    } else {
      if (isInitParam && !isInitArgument)
        error(Call->Args[i].get(), DiagID::ERR_INIT_ARGUMENT_REQUIRED,
              Fn->Args[i].Name);
      if (!isInitParam && isInitArgument) {
        const std::string parameter =
            Fn && i < Fn->Args.size() ? Fn->Args[i].Name :
            (Ext && i < Ext->Args.size() ? Ext->Args[i].Name : "<none>");
        error(Call->Args[i].get(), DiagID::ERR_INIT_ARGUMENT_UNEXPECTED,
              std::to_string(i + 1), parameter);
      }
      const bool oldExpectedCedeTransfer = m_ExpectedCedeTransfer;
      m_ExpectedCedeTransfer = expectedCedeTransfer;
      bool oldAllowPermissionSuffix = m_AllowPermissionSuffix;
      m_AllowPermissionSuffix =
          hasExplicitCallArgumentWriteSigil(Call->Args[i].get());
      argType = i < precheckedArgTypes.size() && precheckedArgTypes[i]
                    ? precheckedArgTypes[i]
                    : checkExpr(Call->Args[i].get(), paramType);
      m_AllowPermissionSuffix = oldAllowPermissionSuffix;
      m_ExpectedCedeTransfer = oldExpectedCedeTransfer;
    }
    projectOwnedStringView(Call->Args[i], argType, paramType);

    if (isOwnedStateThreadCalleeName(OriginalName) && i == 1) {
      ClosureExpr *entryClosure = findClosureExpr(Call->Args[i].get());
      if (entryClosure) {
        for (const auto &capture : entryClosure->ExplicitCaptures) {
          DiagnosticEngine::report(
              getLoc(Call->Args[i].get()),
              DiagID::ERR_SEMA_OWNED_THREAD_ENTRY_CAPTURE,
              Type::stripMorphology(capture.Name));
          HasError = true;
        }
        for (const auto &capture : entryClosure->ImplicitCaptures) {
          DiagnosticEngine::report(
              getLoc(Call->Args[i].get()),
              DiagID::ERR_SEMA_OWNED_THREAD_ENTRY_CAPTURE, capture);
          HasError = true;
        }
      } else if (auto *entryVariable =
                     findVariableExpr(Call->Args[i].get())) {
        SymbolInfo *entryInfo = nullptr;
        std::string actualName;
        if (!CurrentScope->findVariableWithDeref(
                entryVariable->Name, entryInfo, actualName) || !entryInfo ||
            !entryInfo->HasClosureBoundarySummary) {
          DiagnosticEngine::report(
              getLoc(Call->Args[i].get()),
              DiagID::ERR_SEMA_EXEC_BOUNDARY_UNKNOWN_CLOSURE,
              entryVariable->Name, OriginalName);
          HasError = true;
        } else {
          for (const auto &capture : entryInfo->ClosureExplicitCaptures) {
            DiagnosticEngine::report(
                getLoc(Call->Args[i].get()),
                DiagID::ERR_SEMA_OWNED_THREAD_ENTRY_CAPTURE, capture);
            HasError = true;
          }
          for (const auto &capture : entryInfo->ClosureImplicitCaptures) {
            DiagnosticEngine::report(
                getLoc(Call->Args[i].get()),
                DiagID::ERR_SEMA_OWNED_THREAD_ENTRY_CAPTURE, capture);
            HasError = true;
          }
        }
      } else {
        DiagnosticEngine::report(
            getLoc(Call->Args[i].get()),
            DiagID::ERR_SEMA_EXEC_BOUNDARY_UNKNOWN_CLOSURE,
            "owned thread entry", OriginalName);
        HasError = true;
      }
    }

    if (isExecutionBoundaryCalleeName(OriginalName)) {
      auto reportSummary = [&](ASTNode *Node, const std::string &valueName,
                               bool hasSummary,
                               const std::set<std::string> &implicit,
                               const std::set<std::string> &nonSend,
                               const std::set<std::string> &copyNonSync) {
        // A closure can cross a detached execution boundary only when the
        // compiler has retained its capture facts.  Accepting an opaque
        // function value here would recreate the original binding bypass.
        if (!hasSummary) {
          DiagnosticEngine::report(
              Node->Loc, DiagID::ERR_SEMA_EXEC_BOUNDARY_UNKNOWN_CLOSURE,
              valueName, OriginalName);
          HasError = true;
          return;
        }
        for (const auto &name : implicit) {
          DiagnosticEngine::report(
              Node->Loc, DiagID::ERR_SEMA_EXEC_BOUNDARY_IMPLICIT_CAPTURE,
              OriginalName, name, name, name);
          HasError = true;
          recordDecision(Node, SemanticRuleID::AsyncCapture001,
                         SemanticOperation::ExecutionBoundaryCapture,
                         SemanticDecision::Reject,
                         SemanticReason::ImplicitBoundaryCapture, name,
                         OriginalName, findPathDeclaration(name));
        }
        for (const auto &name : nonSend) {
          DiagnosticEngine::report(
              Node->Loc, DiagID::ERR_SEMA_EXEC_BOUNDARY_NON_SEND_CAPTURE,
              OriginalName, name);
          HasError = true;
        }
        for (const auto &name : copyNonSync) {
          DiagnosticEngine::report(
              Node->Loc, DiagID::ERR_SEMA_EXEC_BOUNDARY_COPY_NON_SYNC_CAPTURE,
              OriginalName, name);
          HasError = true;
        }
      };

      if (auto *Clo = findClosureExpr(Call->Args[i].get())) {
        std::set<std::string> implicit(Clo->BoundaryImplicitCaptures.begin(),
                                       Clo->BoundaryImplicitCaptures.end());
        std::set<std::string> nonSend(Clo->BoundaryNonSendCaptures.begin(),
                                      Clo->BoundaryNonSendCaptures.end());
        std::set<std::string> copyNonSync(
            Clo->BoundaryNonSyncCopyCaptures.begin(),
            Clo->BoundaryNonSyncCopyCaptures.end());
        reportSummary(Clo, "closure", Clo->HasBoundaryCaptureSummary,
                      implicit, nonSend, copyNonSync);
      } else if (auto *Var = findVariableExpr(Call->Args[i].get())) {
        SymbolInfo *Info = nullptr;
        std::string actualName;
        if (CurrentScope->findVariableWithDeref(Var->Name, Info, actualName) &&
            Info) {
          reportSummary(Var, Var->Name, Info->HasClosureBoundarySummary,
                        Info->ClosureImplicitCaptures,
                        Info->ClosureNonSendCaptures,
                        Info->ClosureNonSyncCopyCaptures);
        } else {
          const std::set<std::string> none;
          reportSummary(Var, Var->Name, false, none, none, none);
        }
      } else {
        const std::set<std::string> none;
        reportSummary(Call->Args[i].get(), "closure expression", false, none,
                      none, none);
      }
    }
    
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
    
    bool paramIsHatted = false;
    bool paramIsRebindable = false;
    bool paramIsValueMutable = false;

    // [NEW] Enforce explicit cede for normal function calls
    bool isCededParam = false;
    SourceLocation cedeParamLoc;
    if (Fn && i < Fn->Args.size()) {
        isCededParam = Fn->Args[i].IsCeded;
        paramIsHatted = Fn->Args[i].IsRawPointer || Fn->Args[i].IsUnique ||
                        Fn->Args[i].IsShared || Fn->Args[i].IsReference;
        paramIsRebindable = Fn->Args[i].IsRebindable;
        paramIsValueMutable = Fn->Args[i].IsValueMutable;
        cedeParamLoc = Fn->Args[i].Loc;
    } else if (Ext && i < Ext->Args.size()) {
        isCededParam = Ext->Args[i].IsCeded;
        paramIsHatted = Ext->Args[i].IsRawPointer || Ext->Args[i].IsUnique ||
                        Ext->Args[i].IsShared || Ext->Args[i].IsReference;
        paramIsRebindable = Ext->Args[i].IsRebindable;
        paramIsValueMutable = Ext->Args[i].IsValueMutable;
        cedeParamLoc = Ext->Args[i].Loc;
    }

    // Callable values have no FunctionDecl/ExternDecl metadata at this point,
    // so recover the same two requirements from their physical parameter type.
    if (!Fn && !Ext && paramType) {
      paramIsHatted = paramType->isPointer() || paramType->isSmartPointer() ||
                       paramType->isReference();
      if (paramIsHatted) {
        paramIsRebindable = paramType->IsWritable;
        auto pointee = paramType->getPointeeType();
        paramIsValueMutable = pointee && pointee->IsWritable;
      } else {
        paramIsValueMutable = paramType->IsWritable;
      }
    }

    // Type compatibility is intentionally permissive for value construction
    // (for example, `auto x# = 1`).  It is not an authority check for a
    // mutable call parameter.  Callee requirements must be a subset of the
    // caller path's declared capabilities.
    bool isCallerCeded =
        dynamic_cast<CedeExpr *>(Call->Args[i].get()) != nullptr;
    auto *callerCede =
        dynamic_cast<CedeExpr *>(Call->Args[i].get());
    const bool cedeMovesExistingPlace =
        callerCede && makeAccessPath(callerCede->Value.get());
    if (cedeMovesExistingPlace && !isCededParam) {
      std::string parameter =
          Fn && i < Fn->Args.size()
              ? Fn->Args[i].Name
              : (Ext && i < Ext->Args.size() ? Ext->Args[i].Name
                                             : "arg" + std::to_string(i + 1));
      error(Call->Args[i].get(),
            DiagID::ERR_SEMA_CEDE_ARGUMENT_TO_BORROWED_PARAMETER,
            getPathString(Call->Args[i].get()), parameter);
    }
    PermissionFlow argFlow = getPermissionFlow(Call->Args[i].get());
    // A cede parameter receives a fresh P root only from a whole independent
    // transfer.  `cede ~view` and every other Shared/raw source still carry
    // their direct payload ceiling into the callee; cede transfers a handle,
    // not referent write authority.
    bool isIndependentCedeTransfer =
        isCededParam && isCallerCeded &&
        argFlow.Kind == PermissionFlowKind::Independent;
    AccessCapability declaredCapability =
        getAccessCapability(Call->Args[i].get(), true);
    AccessCapability argCapability = getAccessCapability(Call->Args[i].get());
    AccessIntent argIntent = getAccessIntent(Call->Args[i].get());
    bool lacksHandleCapability =
        paramIsHatted && paramIsRebindable &&
        (!declaredCapability.HandleRebindable ||
         !argCapability.HandleRebindable || !argIntent.HandleRebind);
    bool lacksPayloadCapability =
        paramIsValueMutable && !isIndependentCedeTransfer &&
        (!declaredCapability.PayloadWritable ||
         !argCapability.PayloadWritable || !argIntent.PayloadWrite);
    if (paramIsRebindable || paramIsValueMutable) {
      const bool requiredHandle = paramIsHatted && paramIsRebindable;
      const bool requiredPayload = paramIsValueMutable;
      std::string parameter = "arg" + std::to_string(i + 1);
      if (Fn && i < Fn->Args.size())
        parameter = Fn->Args[i].Name;
      else if (Ext && i < Ext->Args.size())
        parameter = Ext->Args[i].Name;
      std::string subject = getPathString(Call->Args[i].get());
      if (subject.empty())
        subject = Call->Args[i]->toString();
      SemanticEvidence::recordCapabilityCall(
          Fn ? Fn->Name : (Ext ? Ext->Name : CallName), parameter, subject,
          declaredCapability.HandleRebindable,
          declaredCapability.PayloadWritable,
          argCapability.HandleRebindable, argCapability.PayloadWritable,
          argIntent.HandleRebind, argIntent.PayloadWrite, requiredHandle,
          requiredPayload, requiredHandle && !lacksHandleCapability,
          requiredPayload && !lacksPayloadCapability,
          isIndependentCedeTransfer, getLoc(Call->Args[i].get()), cedeParamLoc);
    }
    if (lacksHandleCapability || lacksPayloadCapability) {
      error(Call->Args[i].get(),
            DiagID::ERR_SEMA_TYPE_MISMATCH_FOR_ARGUMENT_EXPECTED_GOT,
            std::to_string(i + 1),
            paramType ? diagnosticTypeName(paramType) : "capable argument",
            diagnosticTypeName(argType));
      if (lacksPayloadCapability) {
        DiagnosticEngine::report(
            getLoc(Call->Args[i].get()), DiagID::NOTE_GENERIC,
            "payload-write authority comes from the binding or parameter declaration; a use-site '#' can request it but cannot create it");
      } else {
        DiagnosticEngine::report(
            getLoc(Call->Args[i].get()), DiagID::NOTE_GENERIC,
            "handle-rebind authority comes from the binding or parameter declaration; a use-site '#' can request it but cannot create it");
      }
    }

    {
      std::string paramName =
          Fn && i < Fn->Args.size() ? Fn->Args[i].Name
          : (Ext && i < Ext->Args.size() ? Ext->Args[i].Name : "");
      validateCallArgumentMutSigil(Call->Args[i].get(), paramIsValueMutable,
                                   paramName, cedeParamLoc, Call->Loc, i);
    }

    if (paramIsValueMutable && !lacksPayloadCapability) {
      Expr *argExpr = Call->Args[i].get();
      while (auto *cede = dynamic_cast<CedeExpr *>(argExpr))
        argExpr = cede->Value.get();
      while (auto *un = dynamic_cast<UnaryExpr *>(argExpr))
        argExpr = un->RHS.get();
      if (auto *VE = dynamic_cast<VariableExpr *>(argExpr)) {
        std::string actualName = VE->Name;
        SymbolInfo *InfoPtr = nullptr;
        if (CurrentScope->findVariableWithDeref(VE->Name, InfoPtr,
                                                actualName)) {
          // Passing or transferring a binding to a payload-writable parameter
          // is a mutable use, even when the callee performs that mutation
          // later through a retained owner.
          InfoPtr->HasBeenMutated = true;
        }
      }
    }

    if (isCededParam && isCallerCeded &&
        isNullableCedeSource(Call->Args[i].get()) &&
        !isNullableCedeDestination(paramType)) {
      error(Call->Args[i].get(),
            DiagID::ERR_SEMA_CEDE_NULLABLE_REQUIRES_GUARD);
    }
    bool calleeIsAsync =
        (Fn && Fn->Effect == EffectKind::Async) ||
        (Ext && Ext->Effect == EffectKind::Async);
    if (calleeIsAsync) {
      std::string paramName = "arg" + std::to_string(i + 1);
      if (Fn && i < Fn->Args.size())
        paramName = Fn->Args[i].Name;
      else if (Ext && i < Ext->Args.size())
        paramName = Ext->Args[i].Name;
      checkStartBoundaryArgument(Call->Args[i].get(), argType, isCededParam,
                                 isCallerCeded, paramName, cedeParamLoc);
    }
    bool isCedeParamImplicitlyExempt = false;
    if (isCededParam) {
         bool isCedeExempt = canImplicitlyPassToCede(argType);
         isCedeParamImplicitlyExempt = isCedeExempt && !isCallerCeded;
         if (!isCallerCeded && !isCedeExempt) {
            error(Call->Args[i].get(), DiagID::ERR_SEMA_ARGUMENT_MUST_BE_EXPLICITLY_PASSED_WITH_2);
            if (cedeParamLoc.isValid())
              DiagnosticEngine::report(cedeParamLoc, DiagID::NOTE_GENERIC,
                                       "cede parameter declared here");
         }
         std::string subject = getPathString(Call->Args[i].get());
         recordDecision(
             Call->Args[i].get(), SemanticRuleID::OwnCede001,
             SemanticOperation::CedeObligation,
             (!isCallerCeded && !isCedeExempt) ? SemanticDecision::Reject
                                               : SemanticDecision::Allow,
             (!isCallerCeded && !isCedeExempt)
                 ? SemanticReason::MissingExplicitCede
                 : SemanticReason::CedeConsumed,
             subject, Fn && i < Fn->Args.size() ? Fn->Args[i].Name : subject,
             cedeParamLoc);
         SemanticEvidence::recordCedeObligation(
             CedeObligationStage::CallerTransfer,
             (!isCallerCeded && !isCedeExempt)
                 ? CedeObligationStatus::Violated
                 : CedeObligationStatus::Fulfilled,
             (!isCallerCeded && !isCedeExempt)
                 ? SemanticReason::MissingExplicitCede
                 : SemanticReason::CedeConsumed,
             subject, Fn && i < Fn->Args.size() ? Fn->Args[i].Name : subject,
             getLoc(Call->Args[i].get()), cedeParamLoc);
    }

    if (paramIsValueMutable && !paramIsHatted && !isCededParam &&
        isReadOnlyBorrowViewArgument(Call->Args[i].get())) {
      error(Call->Args[i].get(),
            DiagID::ERR_SEMA_TYPE_MISMATCH_FOR_ARGUMENT_EXPECTED_GOT,
            std::to_string(i + 1),
            paramType ? diagnosticTypeName(paramType) : "T#",
            diagnosticTypeName(argType));
    }

    if (paramType && i < ParamTypes.size()) {
      AccessPath argPath =
          canonicalizeAccessPath(makeAccessPath(Call->Args[i].get()));
      if (argPath) {
        PALOperationClass access = PALOperationClass::SharedPayloadBorrow;
        if (isCallerCeded || (isCededParam && !isCedeParamImplicitlyExempt)) {
          access = PALOperationClass::Invalidation;
        } else if (paramIsHatted && paramIsRebindable) {
          access = PALOperationClass::HandleRebind;
        } else if (paramIsValueMutable) {
          access = PALOperationClass::ExclusivePayloadBorrow;
        } else if (paramIsHatted) {
          access = PALOperationClass::HandleViewBorrow;
        }

        auto conflict = PALCheckerState.verifyOperation(argPath, access);
        if (conflict) {
          error(Call->Args[i].get(), DiagID::ERR_CALL_ARGUMENT_ALIAS_CONFLICT,
                argPath.toLegacyString(), conflict->displayPath());
          recordPALConflict(Call->Args[i].get(), access, argPath, *conflict);
        }

        callArgPALFacts.push_back({argPath, access, Call->Args[i].get()});
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

    bool diagnosedNull = false;
    if (paramType && paramType->isRawPointer() && !paramType->IsNullable &&
        argType && argType->isNullType()) {
      error(Call->Args[i].get(), DiagID::ERR_NONZERO_RAW_NULL_FLOW,
            paramType->toString());
      diagnosedNull = true;
    }

    if (!diagnosedNull && !isTypeCompatible(paramType, argType)) {
      error(Call->Args[i].get(),
            DiagID::ERR_SEMA_TYPE_MISMATCH_FOR_ARGUMENT_EXPECTED_GOT,
            std::to_string(i + 1), diagnosticTypeName(paramType),
            diagnosticTypeName(argType));
    } else if (paramType && argType && paramType->isShape() && argType->isRawPointer()) {
      auto shp = std::static_pointer_cast<toka::ShapeType>(paramType);
      if (shp->Name == "str") {
        Call->Args[i]->ResolvedType = paramType;
      }
    }
  }

  // Trusted atomic wrappers preserve Ordering as a runtime enum value, but a
  // direct enum literal can be rejected here with a stable source diagnostic.
  // Dynamic values are validated by the operation-specific CodeGen switch.
  validateAtomicOrderingArguments(Fn, Call->Args);

  // An ordinary synchronous init call establishes Live at the caller
  // boundary.  An Outcome Contract instead leaves the exact place in a
  // private {Never, Live} quarantine until its direct match selects one
  // declared result variant.
  const bool hasOutcomeContract =
      Fn && Fn->ResolvedOutcomeTransition.has_value();
  const bool hasRecheckedOutcomeContract = hasOutcomeContract && Fn->Body;
  const bool hasAttestedOutcomeContract =
      hasOutcomeContract && !Fn->Body &&
      Fn->HasSemanticManifestAttestationCandidate;
  if (hasOutcomeContract && !Fn->Body && !hasAttestedOutcomeContract) {
    DiagnosticEngine::report(getLoc(Call),
                             DiagID::ERR_OUTCOME_BODY_RECHECK_REQUIRED,
                             Call->Callee);
    HasError = true;
  }
  if (hasRecheckedOutcomeContract || hasAttestedOutcomeContract) {
    Call->RequiresOutcomeMatch = true;
    Call->OutcomeMatchConsumed = false;
    m_OutcomePendingCalls.push_back(Call);
  }
  for (SymbolInfo *place : completedInitPlaces) {
    if (hasRecheckedOutcomeContract || hasAttestedOutcomeContract) {
      const PlaceStateFact pending =
          PlaceStateFact(PlaceState::Never).join(PlaceState::Live);
      if (place->ExactPlace.transitionWhole(PlaceState::Never, pending))
        place->InitMask = 0;
    } else {
      if (place->ExactPlace.transitionWhole(PlaceState::Never,
                                            PlaceState::Live))
        place->InitMask = ~0ULL;
    }
  }

  for (size_t i = 0; i < callArgPALFacts.size(); ++i) {
    for (size_t j = i + 1; j < callArgPALFacts.size(); ++j) {
      const auto &lhs = callArgPALFacts[i];
      const auto &rhs = callArgPALFacts[j];
      PALConflict lhsOrigin{lhs.Path, PathState::Free,
                            lhs.Node ? lhs.Node->Loc : SourceLocation{}};
      if (!PALCheckerState.pathsOverlap(lhs.Path, rhs.Path)) {
        recordPALDecision(rhs.Node, SemanticRuleID::PALCall001, rhs.Access,
                          rhs.Path, lhsOrigin, SemanticDecision::Allow,
                          SemanticReason::DisjointPaths);
        continue;
      }
      if (!PALCheckerState.operationsConflict(lhs.Access, rhs.Access)) {
        recordPALDecision(rhs.Node, SemanticRuleID::PALCall001, rhs.Access,
                          rhs.Path, lhsOrigin, SemanticDecision::Allow,
                          SemanticReason::CompatibleSharedAccess);
        continue;
      }

      error(rhs.Node, DiagID::ERR_CALL_ARGUMENT_ALIAS_CONFLICT,
            rhs.Path.toLegacyString(), lhs.Path.toLegacyString());
      recordPALDecision(rhs.Node, SemanticRuleID::PALCall001, rhs.Access,
                        rhs.Path, lhsOrigin, SemanticDecision::Reject,
                        SemanticReason::OverlappingExclusiveAccess, true);
    }
  }
  
  bool isAsync = false;
  if (Fn && Fn->Effect == EffectKind::Async) isAsync = true;
  if (Ext && Ext->Effect == EffectKind::Async) isAsync = true;
  
  // Inject Caller-Side Effect Dependencies
  if (Fn) {
      bool hasExplicitDeps = !Fn->LifeDependencies.empty();

      auto mapParamToArg = [&](const std::string &paramName) -> std::string {
         for (size_t i = 0; i < Fn->Args.size(); ++i) {
            const std::string &argName = Fn->Args[i].Name;
            if (argName == paramName ||
                (paramName.size() > argName.size() &&
                 paramName.compare(0, argName.size(), argName) == 0 &&
                 paramName[argName.size()] == '.')) {
               if (i < Call->Args.size()) {
                   std::string argPath =
                       getDependencyPathString(Call->Args[i].get());
                   if (argPath.empty())
                     return "";
                   if (argName == paramName)
                     return argPath;
                   return argPath + paramName.substr(argName.size());
               }
            }
         }
         return "";
      };

      if (hasExplicitDeps) {
          for (const auto &dep : Fn->LifeDependencies) {
             for (size_t i = 0; i < Fn->Args.size(); ++i) {
                const std::string &argName = Fn->Args[i].Name;
                bool depMatchesArg =
                    argName == dep ||
                    (dep.size() > argName.size() &&
                     dep.compare(0, argName.size(), argName) == 0 &&
                     dep[argName.size()] == '.');
                if (!depMatchesArg)
                   continue;
                if (i >= Call->Args.size())
                   continue;

                std::string structuralArgVar =
                    getPathString(Call->Args[i].get());
                std::string argVar =
                    getDependencyPathString(Call->Args[i].get());
                if (argVar.empty())
                  continue;
                bool isExpressionDependency = structuralArgVar.empty();
                if (argName != dep)
                  argVar += dep.substr(argName.size());

                bool isCurrentFunctionParam = false;
                if (CurrentFunction) {
                  std::string baseArg = argVar;
                  size_t dotPos = baseArg.find('.');
                  if (dotPos != std::string::npos)
                    baseArg = baseArg.substr(0, dotPos);
                  for (const auto &arg : CurrentFunction->Args) {
                    if (arg.Name == baseArg) {
                      isCurrentFunctionParam = true;
                      break;
                    }
                  }
                }

                bool contributedDeps = false;
                SymbolInfo *argInfo = nullptr;
                if (auto *var = dynamic_cast<VariableExpr *>(Call->Args[i].get())) {
                  if (CurrentScope->findSymbol(var->Name, argInfo)) {
                    if (!argInfo->BorrowedFrom.empty()) {
                      m_LastLifeDependencies.insert(argInfo->BorrowedFrom);
                      contributedDeps = true;
                    }
                    size_t before = m_LastLifeDependencies.size();
                    m_LastLifeDependencies.insert(argInfo->LifeDependencySet.begin(),
                                                  argInfo->LifeDependencySet.end());
                    if (m_LastLifeDependencies.size() != before)
                      contributedDeps = true;
                  }
                }

                auto argResolvedType = Call->Args[i]->ResolvedType;
                if (!argResolvedType && argInfo)
                  argResolvedType = argInfo->TypeObj;

                bool isDottedDependency = argName != dep;
                if (isExpressionDependency || isDottedDependency ||
                    contributedDeps ||
                    isCurrentFunctionParam ||
                    !isBorrowLikeType(argResolvedType)) {
                  m_LastLifeDependencies.insert(argVar);
                }
                recordDecision(
                    Call, isAsync ? SemanticRuleID::AsyncSuspend001
                                  : SemanticRuleID::EffRet001,
                    SemanticOperation::EscapingDependency,
                    SemanticDecision::Allow,
                    SemanticReason::InterfaceContractApplied, argVar, dep,
                    Fn->Args[i].Loc);
                break;
             }
          }
      }

      for (const auto &pair : Fn->MemberDependencies) {
         for (const auto &dep : pair.second) {
             std::string argVar = mapParamToArg(dep);
             if (!argVar.empty()) {
               m_LastFieldDependencies[pair.first].insert(argVar);
               recordDecision(
                   Call, SemanticRuleID::EffMember001,
                   SemanticOperation::MemberDependency,
                   SemanticDecision::Allow,
                   SemanticReason::InterfaceContractApplied,
                   pair.first + "<-" + argVar, dep);
             }
          }
      }

      if (ReturnType && ReturnType->IsCede) {
        recordDecision(Call, SemanticRuleID::OwnCede002,
                       SemanticOperation::OwnershipTransfer,
                       SemanticDecision::Allow,
                       SemanticReason::InterfaceContractApplied, Fn->Name,
                       ReturnType->toString(), Fn->Loc);
      }

      if (SemanticEvidence::isEnabled() && ReturnType &&
          ReturnType->isShape()) {
        std::string returnSoul =
            Type::stripMorphology(ReturnType->getSoulName());
        auto shapeIt = ShapeMap.find(returnSoul);
        if (shapeIt != ShapeMap.end() && shapeIt->second &&
            !shapeIt->second->MangledDestructorName.empty()) {
          recordDecision(Call, SemanticRuleID::OwnResource001,
                         SemanticOperation::InterfaceReplay,
                         SemanticDecision::Allow,
                         SemanticReason::InterfaceContractApplied, Fn->Name,
                         returnSoul, Fn->Loc);
        }
      }
  }

  if (isAsync) {
    return resolveType(std::make_shared<ShapeType>(
        "TaskHandle",
        std::vector<std::shared_ptr<toka::Type>>{ReturnType}));
  }
  return ReturnType;
}

} // namespace toka
