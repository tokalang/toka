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
#include "toka/Parser.h"
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

static bool requiresPayloadWrite(const std::shared_ptr<toka::Type> &Type) {
  if (!Type)
    return false;
  if (Type->isPointer() || Type->isSmartPointer() || Type->isReference()) {
    auto pointee = Type->getPointeeType();
    return pointee && pointee->IsWritable;
  }
  return Type->IsWritable;
}

static std::string ownershipSourceLabel(const Expr *expression) {
  if (!expression)
    return "value";
  if (auto *variable = dynamic_cast<const VariableExpr *>(expression))
    return Type::stripMorphology(variable->Name);
  if (auto *member = dynamic_cast<const MemberExpr *>(expression))
    return ownershipSourceLabel(member->Object.get()) + "." + member->Member;
  if (auto *index = dynamic_cast<const ArrayIndexExpr *>(expression))
    return ownershipSourceLabel(index->Array.get()) + "[...]";
  return expression->toString();
}

// A field initializer creates a new storage slot. Its H permission belongs
// to the field declaration, not to the source handle value. This does not
// grant payload write authority: pointee compatibility and the direct-source
// flow check below still enforce P. Argument and assignment compatibility for
// an existing binding remain unchanged.
static bool isFieldInitializerCompatible(
    const std::shared_ptr<toka::Type> &destination,
    const std::shared_ptr<toka::Type> &source) {
  if (!destination || !source || !destination->isPointer() ||
      !source->isPointer() || destination->typeKind != source->typeKind)
    return false;
  auto destinationPointee = destination->getPointeeType();
  auto sourcePointee = source->getPointeeType();
  return destinationPointee && sourcePointee &&
         (!source->IsNullable || destination->IsNullable) &&
         sourcePointee->isCompatibleWith(*destinationPointee);
}

static AccessCapability
restrictPatternCapability(AccessCapability capability,
                          const std::string &targetType) {
  auto target = toka::Type::fromString(targetType);
  if (!target ||
      !(target->isPointer() || target->isSmartPointer() ||
        target->isReference()))
    return capability;

  // A pointer/reference payload extracted from an aggregate is itself a
  // shared view, even when the aggregate value was independently owned.
  // A nested handle is a direct source in its own right.  Its declared
  // payload permission is the ceiling for a fresh aggregate; if the
  // aggregate itself already arrived through a shared view, preserve that
  // earlier ceiling as well.  In particular, an Option<&T#> must not inherit
  // the unrelated outer Option payload bit.
  bool targetPayloadWritable = requiresPayloadWrite(target);
  capability.PayloadWritable = capability.PayloadFlowRestricted
                                   ? capability.PayloadWritable &&
                                         targetPayloadWritable
                                   : targetPayloadWritable;
  capability.PayloadFlowRestricted = true;
  return capability;
}

void Sema::checkPattern(MatchArm::Pattern *Pat, const std::string &TargetType,
                        AccessCapability SourceCapability,
                        const std::string &TargetPath,
                        const AccessPath &TargetAccessPath) {
  if (!Pat)
    return;

  SourceCapability =
      restrictPatternCapability(SourceCapability, TargetType);

  std::string T = resolveType(TargetType);
  while (!T.empty() && (T.back() == '#' || T.back() == '?' || T.back() == '!')) {
      T.pop_back();
  }

  switch (Pat->PatternKind) {
  case MatchArm::Pattern::Elision: {
    break;
  }

  case MatchArm::Pattern::Range: {
    if (Pat->SubPatterns.size() != 2) {
      error(Pat, DiagID::ERR_SEMA_INVALID_RANGE_PATTERN_STRUCTURE);
      break;
    }
    checkPattern(Pat->SubPatterns[0].get(), TargetType, SourceCapability, TargetPath);
    checkPattern(Pat->SubPatterns[1].get(), TargetType, SourceCapability, TargetPath);

    std::string resolvedT = resolveType(T, true);
    auto targetObj = toka::Type::fromString(resolvedT);
    if (!targetObj || !targetObj->isInteger()) {
      error(Pat, DiagID::ERR_SEMA_RANGE_PATTERN_IS_ONLY_ALLOWED_FOR_INTEGER, T);
      break;
    }

    uint64_t startVal = Pat->SubPatterns[0]->LiteralVal;
    uint64_t endVal = Pat->SubPatterns[1]->LiteralVal;
    if (startVal > endVal) {
      error(Pat, DiagID::ERR_SEMA_RANGE_START_VALUE_CANNOT_BE_GREATER_THAN, std::to_string(startVal), std::to_string(endVal));
    } else if (!Pat->IsInclusive && startVal == endVal) {
      error(Pat, DiagID::ERR_SEMA_EXCLUSIVE_RANGE_START_VALUE_CANNOT_BE_EQU, std::to_string(startVal), std::to_string(endVal));
    }
    break;
  }

  case MatchArm::Pattern::Literal: {
    // Literal patterns don't bind variables, but we must check types.
    // Pat->Name contains the raw text of the literal (Integer, String, true, false)
    std::shared_ptr<toka::Type> litType;
    if (Pat->Name == "true" || Pat->Name == "false") {
      litType = toka::Type::fromString("bool");
    } else if (!Pat->Name.empty() && Pat->Name[0] == '"') {
      if (T == "*char" || T == "str" || T == "cstr") {
        litType = toka::Type::fromString(T);
      } else {
        litType = toka::Type::fromString("string");
      }
    } else if (!Pat->Name.empty() && Pat->Name[0] == '\'') {
      if (T == "Char16" || resolveType(T, true) == "u16") {
        litType = toka::Type::fromString("Char16");
      } else {
        litType = toka::Type::fromString("char");
      }
    } else {
      // Assume integer for now (Parser sets LiteralVal for these)
      // We'll use the target type to disambiguate if it's an integer type
      auto targetObj = toka::Type::fromString(T);
      if (targetObj && targetObj->isInteger()) {
        litType = targetObj;
      } else {
        litType = toka::Type::fromString("i32");
      }
    }

    auto targetObj = toka::Type::fromString(T);
    if (targetObj && litType && !isTypeCompatible(targetObj, litType)) {
      error(Pat, DiagID::ERR_TYPE_MISMATCH, litType->toString(), T);
    }

    // Trait Check: If it's not a primitive, it must implement @PartialEq
    auto resolvedTargetObj = toka::Type::fromString(resolveType(T, true));
    if (targetObj && resolvedTargetObj && !resolvedTargetObj->isInteger() && !resolvedTargetObj->isBoolean()) {
        std::string resolvedTarget = resolveType(T);
        if (!targetObj->isPointer()) {
            std::string implKey = resolvedTarget + "@PartialEq";
            if (ImplMap.find(implKey) == ImplMap.end()) {
                error(Pat, DiagID::ERR_TRAIT_NOT_FOUND, "@PartialEq", T);
            }
        }
    }
    break;
  }

  case MatchArm::Pattern::Or: {
    if (Pat->SubPatterns.empty())
      break;

    Scope* originalScope = CurrentScope;
    std::vector<std::map<std::string, SymbolInfo>> branchSymbols;
    
    for (auto &sub : Pat->SubPatterns) {
      CurrentScope = new Scope(originalScope);
      checkPattern(sub.get(), TargetType, SourceCapability, TargetPath);
      branchSymbols.push_back(CurrentScope->Symbols);
      Scope* temp = CurrentScope;
      CurrentScope = originalScope;
      delete temp;
    }
    
    auto &firstBranch = branchSymbols[0];
    bool matches = true;
    for (size_t i = 1; i < branchSymbols.size(); ++i) {
      if (branchSymbols[i].size() != firstBranch.size()) {
        matches = false;
        break;
      }
      for (auto &kv : firstBranch) {
        if (branchSymbols[i].count(kv.first) == 0) {
          matches = false;
          break;
        }
        auto &sym1 = kv.second;
        auto &sym2 = branchSymbols[i][kv.first];
        
        std::string t1 = sym1.TypeObj ? sym1.TypeObj->toString() : "";
        std::string t2 = sym2.TypeObj ? sym2.TypeObj->toString() : "";
        if (t1 != t2 ||
            sym1.IsSoulMutable() != sym2.IsSoulMutable() ||
            sym1.IsHandleRebindable() != sym2.IsHandleRebindable() ||
            sym1.IsReference() != sym2.IsReference() ||
            sym1.IsMorphicExempt != sym2.IsMorphicExempt) {
          matches = false;
          break;
        }
      }
      if (!matches) break;
    }
    
    if (!matches) {
      error(Pat, DiagID::ERR_SEMA_ALL_BRANCHES_OF_AN_OR_PATTERN_MUST_BIND_T);
      break;
    }
    
    for (auto &kv : firstBranch) {
      CurrentScope->define(kv.first, kv.second);
    }
    break;
  }

  case MatchArm::Pattern::Wildcard:
    break;

  case MatchArm::Pattern::Variable: {
    // Check if Pattern->Name is actually a zero-payload variant of target T
    bool isVariant = false;
    std::string baseShapeName = T;
    size_t scopePos = Pat->Name.find("::");
    std::string patName = Pat->Name;
    if (scopePos != std::string::npos) {
      baseShapeName = resolveType(patName.substr(0, scopePos));
      patName = patName.substr(scopePos + 2);
    }
    if (ShapeMap.count(baseShapeName)) {
      ShapeDecl *SD = ShapeMap[baseShapeName];
      for (auto &Memb : SD->Members) {
        bool noPayload = Memb.Type.empty() || Memb.Type == "void";
        if (Memb.Name == patName && noPayload && Memb.SubMembers.empty()) {
          isVariant = true;
          break;
        } else if (Memb.Name == patName) {
          DiagnosticEngine::report(getLoc(Pat), DiagID::ERR_VARIANT_NO_PAYLOAD, patName);
          HasError = true;
          isVariant = true;
          break;
        }
      }
    }

    if (isVariant) {
      // It's a zero-payload variant, do not bind as a variable
      break;
    }

    if (Pat->Binding == MatchArm::Pattern::BindingOrigin::Existing) {
      const bool hasBindingDecorator =
          Pat->IsReference || Pat->IsValueMutable || Pat->IsValueBlocked ||
          Pat->Permission.Morphology != BindingMorphology::None ||
          Pat->Permission.IdentityRebindable ||
          Pat->Permission.IdentityNullable || Pat->Permission.IdentityBlocked;
      SymbolInfo *existing = nullptr;
      if (hasBindingDecorator || !CurrentScope->findSymbol(Pat->Name, existing) ||
          !existing || !existing->IsDeclaredVariable || !existing->TypeObj) {
        error(Pat, DiagID::ERR_PATTERN_IDENTIFIER_MUST_REFER_TO_EXISTING,
              Pat->Name, Pat->Name);
        break;
      }

      auto matchedType = toka::Type::fromString(T);
      if (!matchedType ||
          (!isTypeCompatible(matchedType, existing->TypeObj) &&
           !isTypeCompatible(existing->TypeObj, matchedType))) {
        error(Pat, DiagID::ERR_INVALID_OP, "==", T,
              existing->TypeObj->toString());
        break;
      }

      auto resolvedMatched = resolveType(matchedType, true);
      if (resolvedMatched && resolvedMatched->isShape()) {
        const std::string shapeName = resolvedMatched->getSoulName();
        if (MethodMap.count(shapeName) && MethodMap[shapeName].count("eq")) {
          Pat->EqualityMethod = "eq";
        } else {
          error(Pat, DiagID::ERR_INVALID_OP, "==", T,
                existing->TypeObj->toString());
          break;
        }
      } else if (!resolvedMatched ||
                 (!resolvedMatched->isInteger() &&
                  !resolvedMatched->isFloatingPoint() &&
                  !resolvedMatched->isBoolean() &&
                  !resolvedMatched->isPointer())) {
        error(Pat, DiagID::ERR_INVALID_OP, "==", T,
              existing->TypeObj->toString());
        break;
      }

      existing->HasBeenUsed = true;
      Pat->MatchedValueType = matchedType;
      Pat->ExistingBindingType = existing->TypeObj;
      break;
    }

    auto expectedTypeObj = toka::Type::fromString(T);
    bool isMorphicExempt = (!Pat->Name.empty() && Pat->Name[0] == '\'');
    if (expectedTypeObj->isReference() && !Pat->IsReference && !isMorphicExempt) {
        DiagnosticEngine::report(getLoc(Pat), DiagID::ERR_SEMA_CANNOT_BIND_REFERENCE_MEMBER_TO_A_NON_R_2, Pat->Name);
        HasError = true;
    }

    SymbolInfo Info;
    // Type Migration Stage 1: Coexistence
    // Construct type string to parse object. Pattern bindings infer
    // type T. If Reference, it is &T.
    std::string fullType = "";
    if (Pat->IsReference)
      fullType = "&";
    fullType += T;

    if (!Pat->Name.empty() && Pat->Name[0] == '\'') {
        Info.IsMorphicExempt = true;
    }

    bool bindingPayloadWritable = Pat->IsValueMutable;
    if (isMorphicExempt) {
      // A generic pattern binder transports the instantiated type itself.
      // It is not a request to downgrade (or upgrade) that type's payload
      // capability; the flow ceiling below still prevents shared promotion.
      bindingPayloadWritable =
          bindingPayloadWritable ||
          requiresPayloadWrite(toka::Type::fromString(TargetType));
    }
    if (bindingPayloadWritable)
      fullType += "#";
    Info.TypeObj = toka::Type::fromString(fullType);

    if (Info.TypeObj) {
        // [Safety Gate] Prevent implicit destructure copying of Resources
        if (!Pat->IsReference && !Info.IsMorphicExempt) {
            std::string soulName = Info.TypeObj->getSoulName();
            if (!soulName.empty() && ShapeMap.count(soulName)) {
                if (!ShapeMap[soulName]->MangledDestructorName.empty()) {
                    DiagnosticEngine::report(getLoc(Pat), DiagID::ERR_ILLEGAL_RESOURCE_COPY, soulName, Pat->Name);
                    HasError = true;
                    SourceLocation originLoc = ShapeMap[soulName]->Loc;
                    recordDecision(
                        Pat, SemanticRuleID::OwnResource001,
                        SemanticOperation::ResourceCopy,
                        SemanticDecision::Reject,
                        SemanticReason::ResourceCopyForbidden, Pat->Name,
                        soulName, originLoc);
                    if (originLoc.isValid())
                      DiagnosticEngine::report(
                          originLoc, DiagID::NOTE_GENERIC,
                          "resource type declared here");
                }
            }
        }
    }

    if (Info.TypeObj) {
      switch (Info.TypeObj->typeKind) {
      case toka::Type::UniquePtr:
        Info.Permission.Morphology = BindingMorphology::Unique;
        break;
      case toka::Type::SharedPtr:
        Info.Permission.Morphology = BindingMorphology::Shared;
        break;
      case toka::Type::Reference:
        Info.Permission.Morphology = BindingMorphology::Reference;
        break;
      case toka::Type::RawPtr:
        Info.Permission.Morphology = BindingMorphology::Raw;
        break;
      default:
        break;
      }
    }
    Info.Permission.SoulWritable = bindingPayloadWritable;
    if (SourceCapability.PayloadFlowRestricted) {
      Info.PayloadFlowWritable = SourceCapability.PayloadWritable;
      Info.HasPayloadFlowCeiling = true;
    }

    if (Pat->IsReference && !TargetPath.empty()) {
      Info.BorrowedFrom = TargetPath;
      Info.BorrowedPath = TargetAccessPath
                              ? TargetAccessPath
                              : canonicalizeAccessPath(makeAccessPath(TargetPath));
      Info.LifeDependencySet.insert(TargetPath);

      // Also inherit any transitive dependencies if the target path is a known symbol
      SymbolInfo *targetInfo = nullptr;
      if (CurrentScope->findSymbol(TargetPath, targetInfo)) {
        Info.LifeDependencySet.insert(targetInfo->LifeDependencySet.begin(),
                                      targetInfo->LifeDependencySet.end());
      }

      // A direct match/guard reference binder is an active lexical borrow.
      // Recursive struct patterns extend TargetAccessPath with their exact
      // field projection. Enum payloads retain their enclosing target path
      // conservatively because payload projections have no source spelling.
      if (TargetAccessPath &&
          !PALCheckerState.recordBorrow(TargetAccessPath,
                                        bindingPayloadWritable, Pat->Loc)) {
        error(Pat, DiagID::ERR_BORROW_MUT,
              PALCheckerState.lastConflict()->displayPath());
        recordPALConflict(Pat,
                          bindingPayloadWritable
                              ? PALOperationClass::ExclusivePayloadBorrow
                              : PALOperationClass::SharedPayloadBorrow,
                          TargetAccessPath, *PALCheckerState.lastConflict());
      }
    }

    // Pattern bindings are declarations too.  Preserve their payload-side
    // capability rather than relying on a later use-site # to recreate it.
    Info.IsDeclaredMutable = bindingPayloadWritable;
    Info.IsDeclaredVariable = true;

    CurrentScope->define(Pat->Name, Info);
    if (Pat->IsReference && TargetAccessPath)
      PALCheckerState.commitTransient(TargetAccessPath);
    break;
  }

  case MatchArm::Pattern::Decons: {
    // Pat->Name might be "Ok" or "Result::Ok"
    std::string variantName = Pat->Name;
    std::string shapeName = T;

    size_t pos = variantName.find("::");
    if (pos != std::string::npos) {
      std::string requestedShapeCore = variantName.substr(0, pos);
      size_t ltReq = requestedShapeCore.find("<");
      if (ltReq != std::string::npos) requestedShapeCore = requestedShapeCore.substr(0, ltReq);
      variantName = variantName.substr(pos + 2);
      while (TypeAliasMap.count(requestedShapeCore) && !TypeAliasMap[requestedShapeCore].IsStrong) {
          requestedShapeCore = TypeAliasMap[requestedShapeCore].Target;
          size_t lt = requestedShapeCore.find("<");
          if (lt != std::string::npos) requestedShapeCore = requestedShapeCore.substr(0, lt);
      }
      std::string requestedShapeIdentity = requestedShapeCore;
      if (ShapeDecl *requestedShape =
              findVisibleShapeDecl(requestedShapeCore, getLoc(Pat))) {
        requestedShapeIdentity = genericImplKey(requestedShape);
      }
      
      std::string T_base = T;
      size_t ltT = T.find("_M"); 
      if (ltT == std::string::npos) ltT = T.find("<");
      if (ltT != std::string::npos) T_base = T.substr(0, ltT);
      
      if (TypeAliasMap.count(T_base) && !TypeAliasMap[T_base].IsStrong) {
          T_base = TypeAliasMap[T_base].Target;
          size_t lt = T_base.find("<");
          if (lt != std::string::npos) T_base = T_base.substr(0, lt);
      }
      
      bool isMatch = (T_base == requestedShapeIdentity);
      
      if (!isMatch) {
          DiagnosticEngine::report(getLoc(Pat), DiagID::ERR_UNKNOWN_SHAPE_IN_PAT, requestedShapeCore);
          HasError = true;
      }
      shapeName = T;
    } else {
      shapeName = T;
    }

    if (ShapeMap.count(shapeName)) {
      ShapeDecl *SD = ShapeMap[shapeName];
      if (SD->Kind == ShapeKind::Struct || SD->Kind == ShapeKind::Tuple) {
        std::string varBase = variantName;
        size_t ltV = varBase.find("<");
        if (ltV != std::string::npos) varBase = varBase.substr(0, ltV);
        
        std::string sdBase = SD->Name;
        size_t ltS = sdBase.find("<");
        if (ltS != std::string::npos) sdBase = sdBase.substr(0, ltS);
        
        if (!variantName.empty() && varBase != sdBase) {
          DiagnosticEngine::report(getLoc(Pat), DiagID::ERR_UNKNOWN_SHAPE_IN_PAT, variantName);
          HasError = true;
        } else {
          auto getMorphFromString = [](const std::string &str) -> MorphKind {
            if (str.find('^') != std::string::npos) return MorphKind::Unique;
            if (str.find('~') != std::string::npos) return MorphKind::Shared;
            if (str.find('&') != std::string::npos) return MorphKind::Ref;
            if (str.find('*') != std::string::npos) return MorphKind::Raw;
            return MorphKind::None;
          };

          bool isNamed = false;
          for (const auto& name : Pat->SubPatternNames) {
            if (!name.empty() && name != "..") {
              isNamed = true;
              break;
            }
          }

          if (SD->Kind == ShapeKind::Struct) {
            isNamed = true;
            if (Pat->SubPatternNames.size() < Pat->SubPatterns.size()) {
              Pat->SubPatternNames.resize(Pat->SubPatterns.size(), "");
            }
            for (size_t i = 0; i < Pat->SubPatterns.size(); ++i) {
              if (Pat->SubPatterns[i]->PatternKind == MatchArm::Pattern::Elision) {
                Pat->SubPatternNames[i] = "..";
                continue;
              }
              if (Pat->SubPatternNames[i].empty() || Pat->SubPatternNames[i] == "..") {
                if (Pat->SubPatterns[i]->PatternKind == MatchArm::Pattern::Variable) {
                  std::string inferredName = Pat->SubPatterns[i]->Name;
                  if (Pat->SubPatterns[i]->IsReference) {
                    inferredName = "&" + inferredName;
                  }
                  Pat->SubPatternNames[i] = inferredName;
                } else {
                  DiagnosticEngine::report(getLoc(Pat), DiagID::ERR_SEMA_POSITIONAL_DESTRUCTURING_ELEMENT_IS_PROHI, SD->Name);
                  HasError = true;
                }
              }
            }
          }

          std::vector<size_t> memberIndices(Pat->SubPatterns.size(), -1);

          if (isNamed) {
            if (SD->Kind != ShapeKind::Struct) {
              DiagnosticEngine::report(getLoc(Pat), DiagID::ERR_NOT_A_STRUCT, "pattern matching", SD->Name);
              HasError = true;
            } else {
              // 1. Verify duplicates
              std::set<std::string> seenFields;
              for (const auto &name : Pat->SubPatternNames) {
                if (name.empty() || name == "..") continue;
                std::string cleanName = toka::Type::stripMorphology(name);
                if (seenFields.count(cleanName)) {
                  DiagnosticEngine::report(getLoc(Pat), DiagID::ERR_DUPLICATE_FIELD, name);
                  HasError = true;
                }
                seenFields.insert(cleanName);
              }

              // 2. Verify existence
              std::set<std::string> sdMembers;
              for (const auto &m : SD->Members) {
                sdMembers.insert(m.Name);
              }
              for (const auto &name : Pat->SubPatternNames) {
                if (name.empty() || name == "..") continue;
                if (!sdMembers.count(toka::Type::stripMorphology(name))) {
                  DiagnosticEngine::report(getLoc(Pat), DiagID::ERR_NO_SUCH_MEMBER, SD->Name, name);
                  HasError = true;
                }
              }

              // 3. Verify completeness
              bool hasElision = false;
              for (const auto &name : Pat->SubPatternNames) {
                if (name == "..") {
                  hasElision = true;
                  break;
                }
              }
              for (const auto &sub : Pat->SubPatterns) {
                if (sub->PatternKind == MatchArm::Pattern::Elision) {
                  hasElision = true;
                  break;
                }
              }
              if (!hasElision) {
                for (const auto &defField : SD->Members) {
                  if (!seenFields.count(defField.Name)) {
                    if (!defField.DefaultValue) {
                      DiagnosticEngine::report(getLoc(Pat), DiagID::ERR_MISSING_DEFAULT_FOR_ELIDED, defField.Name, SD->Name);
                      HasError = true;
                    }
                  }
                }
              }

              // 4. Map subpatterns
              for (size_t i = 0; i < Pat->SubPatterns.size(); ++i) {
                if (Pat->SubPatterns[i]->PatternKind == MatchArm::Pattern::Elision) continue;
                std::string fieldName = Pat->SubPatternNames[i];
                for (size_t m = 0; m < SD->Members.size(); ++m) {
                  auto cleanDef = SD->Members[m].Name;
                  while (!cleanDef.empty() &&
                         (cleanDef.back() == '#' || cleanDef.back() == '!' ||
                          cleanDef.back() == '?'))
                    cleanDef.pop_back();

                  auto cleanProv = fieldName;
                  while (!cleanProv.empty() &&
                         (cleanProv.back() == '#' || cleanProv.back() == '!' ||
                          cleanProv.back() == '?'))
                    cleanProv.pop_back();

                  if (cleanDef == cleanProv ||
                      toka::Type::stripMorphology(cleanDef) == toka::Type::stripMorphology(cleanProv)) {
                    memberIndices[i] = m;
                    break;
                  }
                }
              }
            }
          } else {
            // Positional mapping
            size_t elisionIndex = -1;
            size_t elisionCount = 0;
            for (size_t i = 0; i < Pat->SubPatterns.size(); ++i) {
              if (Pat->SubPatterns[i]->PatternKind == MatchArm::Pattern::Elision) {
                elisionIndex = i;
                elisionCount++;
              }
            }

            if (elisionCount > 1) {
              DiagnosticEngine::report(getLoc(Pat), DiagID::ERR_MULTIPLE_ELISION);
              HasError = true;
            } else if (elisionCount == 1) {
              size_t expectedSize = SD->Members.size();
              size_t subPatsWithoutElision = Pat->SubPatterns.size() - 1;
              if (subPatsWithoutElision > expectedSize) {
                DiagnosticEngine::report(getLoc(Pat), DiagID::ERR_VARIANT_ARG_MISMATCH, variantName, expectedSize, Pat->SubPatterns.size());
                HasError = true;
              } else {
                size_t elidedFields = expectedSize - subPatsWithoutElision;
                for (size_t i = 0; i < Pat->SubPatterns.size(); ++i) {
                  if (i == elisionIndex) continue;
                  memberIndices[i] = (i < elisionIndex) ? i : (i + elidedFields - 1);
                }
              }
            } else {
              if (Pat->SubPatterns.size() != SD->Members.size()) {
                DiagnosticEngine::report(getLoc(Pat), DiagID::ERR_VARIANT_ARG_MISMATCH, variantName, SD->Members.size(), Pat->SubPatterns.size());
                HasError = true;
              } else {
                for (size_t i = 0; i < Pat->SubPatterns.size(); ++i) {
                  memberIndices[i] = i;
                }
              }
            }
          }

          // Unified morphological validation and checking
          for (size_t i = 0; i < Pat->SubPatterns.size(); ++i) {
            if (Pat->SubPatterns[i]->PatternKind == MatchArm::Pattern::Elision) continue;
            size_t memberIndex = memberIndices[i];
            if (memberIndex != (size_t)-1) {
              const std::string encapField = toka::Type::stripMorphology(
                  SD->Members[memberIndex].Name);
              if (!canNameEncapField(SD, encapField, Pat->Loc)) {
                error(Pat, DiagID::ERR_MEMBER_PRIVATE, encapField,
                      SD->Name);
              }
              if (!SD->Members[memberIndex].IsMorphicExempt) {
                auto memberTypeObj = getPhysicalType(SD->Members[memberIndex]);
                MorphKind expectedMorph = morphKindFromType(memberTypeObj);

                bool subIsMorphicExempt = false;
                MorphKind subMorph = MorphKind::None;
                if (Pat->SubPatterns[i]->PatternKind == MatchArm::Pattern::Wildcard) {
                  subIsMorphicExempt = true;
                } else if (Pat->SubPatterns[i]->PatternKind == MatchArm::Pattern::Variable) {
                  if (!Pat->SubPatterns[i]->Name.empty() && Pat->SubPatterns[i]->Name[0] == '\'') {
                    subIsMorphicExempt = true;
                  } else {
                    if (Pat->SubPatterns[i]->IsReference) subMorph = MorphKind::Ref;
                    else {
                      subMorph = getMorphFromString(Pat->SubPatterns[i]->Name);
                    }
                  }
                }

                if (!subIsMorphicExempt) {
                  if (isNamed && i < Pat->SubPatternNames.size() &&
                      !Pat->SubPatternNames[i].empty() && Pat->SubPatternNames[i] != "..") {
                    MorphKind fieldMorph = getMorphFromString(Pat->SubPatternNames[i]);
                    if (subMorph != fieldMorph) {
                      auto morphToString = [](MorphKind m) -> std::string {
                        switch (m) {
                          case MorphKind::None: return "plain value (None)";
                          case MorphKind::Raw: return "raw pointer (*)";
                          case MorphKind::Unique: return "unique pointer (^)";
                          case MorphKind::Shared: return "shared pointer (~)";
                          case MorphKind::Ref: return "reference (&)";
                          default: return "unknown";
                        }
                      };
                      std::string subName = Pat->SubPatterns[i]->Name;
                      if (subName.empty()) subName = "_";
                      DiagnosticEngine::report(getLoc(Pat), DiagID::ERR_SEMA_MISMATCHED_MORPHOLOGY_IN_NAMED_PATTERN_MA, subName, morphToString(subMorph), Pat->SubPatternNames[i], morphToString(fieldMorph));
                      HasError = true;
                    }
                  }

                  if (!(expectedMorph == MorphKind::None && subMorph == MorphKind::Ref)) {
                    checkStrictMorphology(Pat, expectedMorph, subMorph, SD->Members[memberIndex].Name);
                  }
                }
              }

              // A nested binder borrows this member, not the enclosing
              // aggregate.  Preserve the structural field projection so PAL
              // can reject a move of the same field while retaining the
              // existing disjoint-field behavior.
              const std::string memberName =
                  toka::Type::stripMorphology(SD->Members[memberIndex].Name);
              std::string memberPath = TargetPath;
              if (!memberPath.empty())
                memberPath += "." + memberName;
              AccessPath memberAccessPath = TargetAccessPath;
              if (memberAccessPath) {
                memberAccessPath.Projections.push_back(
                    AccessProjection::field(memberName,
                                            Pat->SubPatterns[i]->Loc));
              }
              checkPattern(Pat->SubPatterns[i].get(),
                           getPhysicalTypeName(SD->Members[memberIndex]),
                           SourceCapability, memberPath, memberAccessPath);
            }
          }
        }
      } else {
        ShapeMember *foundMemb = nullptr;
        for (auto &Memb : SD->Members) {
          if (Memb.Name == variantName) {
            foundMemb = &Memb;
            break;
          }
        }

        if (foundMemb) {
          if (Pat->SubPatterns.size() > 0) {
            bool noPayload = foundMemb->Type.empty() || foundMemb->Type == "void";
            if (noPayload && foundMemb->SubMembers.empty()) {
              DiagnosticEngine::report(
                  getLoc(Pat), DiagID::ERR_VARIANT_NO_PAYLOAD, variantName);
              HasError = true;
            } else {
              if (!foundMemb->SubMembers.empty()) {
                // Multi-field enum payload variant
                size_t elisionIndex = -1;
                size_t elisionCount = 0;
                for (size_t i = 0; i < Pat->SubPatterns.size(); ++i) {
                  if (Pat->SubPatterns[i]->PatternKind == MatchArm::Pattern::Elision) {
                    elisionIndex = i;
                    elisionCount++;
                  }
                }

                if (elisionCount > 1) {
                  DiagnosticEngine::report(getLoc(Pat), DiagID::ERR_MULTIPLE_ELISION);
                  HasError = true;
                } else if (elisionCount == 1) {
                  size_t expectedSize = foundMemb->SubMembers.size();
                  size_t subPatsWithoutElision = Pat->SubPatterns.size() - 1;
                  if (subPatsWithoutElision > expectedSize) {
                    DiagnosticEngine::report(getLoc(Pat), DiagID::ERR_VARIANT_ARG_MISMATCH, variantName, expectedSize, Pat->SubPatterns.size());
                    HasError = true;
                  } else {
                    size_t elidedFields = expectedSize - subPatsWithoutElision;
                    for (size_t i = 0; i < Pat->SubPatterns.size(); ++i) {
                      if (i == elisionIndex) continue;
                      size_t memberIndex = (i < elisionIndex) ? i : (i + elidedFields - 1);
                      checkPattern(Pat->SubPatterns[i].get(), getPhysicalTypeName(foundMemb->SubMembers[memberIndex]), SourceCapability, TargetPath, TargetAccessPath);
                    }
                  }
                } else {
                  if (Pat->SubPatterns.size() != foundMemb->SubMembers.size()) {
                    DiagnosticEngine::report(
                        getLoc(Pat), DiagID::ERR_VARIANT_ARG_MISMATCH, variantName,
                        foundMemb->SubMembers.size(), Pat->SubPatterns.size());
                    HasError = true;
                  } else {
                    for (size_t i = 0; i < Pat->SubPatterns.size(); ++i) {
                      checkPattern(Pat->SubPatterns[i].get(),
                                   getPhysicalTypeName(foundMemb->SubMembers[i]), SourceCapability, TargetPath, TargetAccessPath);
                    }
                  }
                }
              } else {
                // Legacy single-field variant
                size_t elisionIndex = -1;
                size_t elisionCount = 0;
                for (size_t i = 0; i < Pat->SubPatterns.size(); ++i) {
                  if (Pat->SubPatterns[i]->PatternKind == MatchArm::Pattern::Elision) {
                    elisionIndex = i;
                    elisionCount++;
                  }
                }

                if (elisionCount > 1) {
                  DiagnosticEngine::report(getLoc(Pat), DiagID::ERR_MULTIPLE_ELISION);
                  HasError = true;
                } else if (elisionCount == 1) {
                  // Some(..) is fully exhaustive and matches anything
                } else {
                  if (Pat->SubPatterns.size() != 1) {
                    DiagnosticEngine::report(
                        getLoc(Pat), DiagID::ERR_VARIANT_ARG_MISMATCH, variantName,
                        1, Pat->SubPatterns.size());
                    HasError = true;
                  } else {
                    checkPattern(Pat->SubPatterns[0].get(), foundMemb->Type,
                                 SourceCapability, TargetPath, TargetAccessPath);
                  }
                }
              }
            }
          }
        } else {
          DiagnosticEngine::report(
              getLoc(Pat), DiagID::ERR_UNKNOWN_SHAPE_IN_PAT,
              shapeName); // Actually variant not found in shape
          HasError = true;
        }
      }
    } else {
      DiagnosticEngine::report(getLoc(Pat), DiagID::ERR_UNKNOWN_SHAPE_IN_PAT,
                               shapeName);
      HasError = true;
    }
    break;
  }
  }
}

std::shared_ptr<toka::Type> Sema::checkShapeInit(InitStructExpr *Init) {
  std::string OriginalName = Init->ShapeName; // [Fix] Capture original name
  std::map<std::string, uint64_t> memberMasks;
  if (!validateTypeVisibilityInType(OriginalName, getLoc(Init)))
    return toka::Type::fromString("unknown");
  auto resolvedTypeObj =
      resolveType(toka::Type::fromString(Init->ShapeName), true);
  std::string resolvedName = resolvedTypeObj->toString();
  ShapeDecl *resolvedShape = nullptr;
  if (auto shapeType = std::dynamic_pointer_cast<ShapeType>(resolvedTypeObj))
    resolvedShape = shapeType->Decl;
  if (!resolvedShape)
    resolvedShape = findVisibleShapeDecl(resolvedName, getLoc(Init));

  // Helper lambda for inference (copied from original)
  auto performInference = [&](std::string &currentName, ShapeDecl *&SD) {
    if (!SD)
      return;
    // Try inference from m_ExpectedType first
    if (!SD->GenericParams.empty() && m_ExpectedType) {
      auto expShape =
          std::dynamic_pointer_cast<toka::ShapeType>(m_ExpectedType);
      if (expShape && (expShape->Name == SD->Name ||
                       expShape->Name.find(SD->Name + "_M") == 0)) {
        currentName = resolveType(m_ExpectedType->toString());
        SD = ShapeMap[currentName];
      }
    }

    // Inference from fields if still a template
    if (SD && !SD->GenericParams.empty() &&
        (currentName == SD->Name || currentName == SD->Name + "<>")) {
      std::map<std::string, std::string> inferred;
      for (auto const &pair : Init->Members) {
        std::string fieldName = Type::stripMorphology(pair.first);
        const ShapeMember *pM = nullptr;
        for (const auto &m : SD->Members) {
          if (m.Name == fieldName) {
            pM = &m;
            break;
          }
        }
        if (!pM)
          continue;

        auto valType = checkExpr(pair.second.get(), nullptr);
        if (!valType)
          continue;

        if (pM->Type.find("[") == 0) {
          auto arrTy = std::dynamic_pointer_cast<toka::ArrayType>(valType);
          if (arrTy) {
            size_t semi = pM->Type.find(';');
            size_t close = pM->Type.find(']');
            if (semi != std::string::npos && close != std::string::npos) {
              auto trim_inline = [](std::string s) {
                size_t first = s.find_first_not_of(" \t\n\r");
                if (first == std::string::npos)
                  return std::string("");
                size_t last = s.find_last_not_of(" \t\n\r");
                return s.substr(first, (last - first + 1));
              };
              std::string tName = trim_inline(pM->Type.substr(1, semi - 1));
              std::string nName =
                  trim_inline(pM->Type.substr(semi + 1, close - semi - 1));

              for (const auto &gp : SD->GenericParams) {
                if (gp.Name == tName && !gp.IsConst)
                  inferred[tName] = arrTy->ElementType->toString();
                if (gp.Name == nName && gp.IsConst)
                  inferred[nName] = std::to_string(arrTy->Size);
              }
            }
          }
        } else {
          for (const auto &gp : SD->GenericParams) {
            if (gp.Name == pM->Type && !gp.IsConst)
              inferred[gp.Name] = valType->toString();
          }
        }
      }

      if (inferred.size() > 0) {
        std::string fullName = SD->Name + "<";
        for (size_t i = 0; i < SD->GenericParams.size(); ++i) {
          std::string pName = SD->GenericParams[i].Name;
          if (inferred.count(pName))
            fullName += inferred[pName];
          else
            fullName += "unknown";
          if (i < SD->GenericParams.size() - 1)
            fullName += ", ";
        }
        fullName += ">";
        currentName = resolveType(fullName, true);
        SD = ShapeMap[currentName];
      }
    }
  };

  if (resolvedShape) {
    ShapeDecl *SD = resolvedShape;
    performInference(resolvedName, SD);
    Init->ShapeName = resolvedName;

    if (!checkVisibility(Init, SD)) {
      return toka::Type::fromString("unknown");
    }

    std::shared_ptr<toka::Type> ResultType;
    if (SD->Kind == ShapeKind::Union || SD->Kind == ShapeKind::Enum) {
      ResultType = checkVariantInit(Init, SD, resolvedName, memberMasks);
    } else {
      ResultType = checkStructInit(Init, SD, resolvedName, memberMasks);
    }

    // [Fix] Strong Alias Preservation
    std::string BaseName = toka::Type::stripMorphology(OriginalName);
    size_t lt = BaseName.find('<');
    if (lt != std::string::npos) {
      BaseName = BaseName.substr(0, lt);
    }

    if (TypeAliasMap.count(BaseName) && TypeAliasMap[BaseName].IsStrong) {
      return toka::Type::fromString(OriginalName);
    }
    return ResultType;
  }

  DiagnosticEngine::report(getLoc(Init), DiagID::ERR_UNKNOWN_STRUCT,
                           Init->ShapeName);
  HasError = true;
  return toka::Type::fromString("unknown");
}

std::shared_ptr<toka::Type>
Sema::checkStructInit(InitStructExpr *Init, ShapeDecl *SD,
                      const std::string &resolvedName,
                      std::map<std::string, uint64_t> &memberMasks) {
  m_LastLifeDependencies.clear();
  std::set<std::string> providedFields;
  bool hasElision = false;

  int elisionCount = 0;
  for (size_t i = 0; i < Init->Members.size(); ++i) {
    if (Init->Members[i].first == "..") {
      elisionCount++;
    }
  }

  for (const auto &member : Init->Members) {
    if (member.first == ".." || member.first == "*")
      continue;
    const std::string field = toka::Type::stripMorphology(member.first);
    if (!canNameEncapField(SD, field, Init->Loc))
      error(Init, DiagID::ERR_MEMBER_PRIVATE, field, SD->Name);
  }
  if (elisionCount > 1) {
    error(Init, DiagID::ERR_MULTIPLE_ELISION);
  }

  Expr* spreadExprObj = nullptr;
  bool hasSpread = false;
  bool isSpreadCede = false;
  size_t spreadIdx = -1;
  std::string baseVarActualName = "";

  for (size_t i = 0; i < Init->Members.size(); ++i) {
    if (Init->Members[i].first == "*") {
      if (hasSpread) {
        error(Init, DiagID::ERR_SEMA_MULTIPLE_SPREAD_OPERATORS_ARE_NOT_ALLOWED);
      }
      hasSpread = true;
      spreadExprObj = Init->Members[i].second.get();
      spreadIdx = i;
    }
  }

  SpreadExpr* spreadNode = nullptr;
  if (hasSpread) {
    if (auto* cedeNode = dynamic_cast<CedeExpr*>(spreadExprObj)) {
      isSpreadCede = true;
      spreadNode = dynamic_cast<SpreadExpr*>(cedeNode->Value.get());
    } else {
      spreadNode = dynamic_cast<SpreadExpr*>(spreadExprObj);
    }

    if (!spreadNode) {
      error(Init, DiagID::ERR_SEMA_INVALID_SPREAD_OPERATOR);
    } else {
      auto baseType = checkExpr(spreadNode->Base.get());
      std::string targetNominalName = resolveType(resolvedName);
      std::string baseNominalName = resolveType(baseType->getSoulName());

      if (targetNominalName != baseNominalName) {
        error(spreadExprObj, DiagID::ERR_SEMA_TYPE_MISMATCH_FOR_SPREAD_NOMINAL_TYPE_OF, baseType->getSoulName(), resolvedName);
      }

      if (isSpreadCede) {
        if (auto *Var = dynamic_cast<VariableExpr *>(spreadNode->Base.get())) {
          SymbolInfo *Info = nullptr;
          std::string actualName;
          if (CurrentScope->findVariableWithDeref(Var->Name, Info, actualName)) {
            baseVarActualName = actualName;
            Init->CededBases.push_back(actualName);
          }
        }
      }

      std::set<std::string> explicitlyProvided;
      for (const auto& member : Init->Members) {
        if (member.first != ".." && member.first != "*") {
          explicitlyProvided.insert(member.first);
        }
      }

      for (const auto &defField : SD->Members) {
        if (!explicitlyProvided.count(defField.Name) &&
            !explicitlyProvided.count("^" + defField.Name) &&
            !explicitlyProvided.count("*" + defField.Name) &&
            !explicitlyProvided.count("~" + defField.Name) &&
            !explicitlyProvided.count("&" + defField.Name) &&
            !explicitlyProvided.count("^?" + defField.Name)) {
          if (!canNameEncapField(SD,
                                 toka::Type::stripMorphology(defField.Name),
                                 spreadExprObj->Loc)) {
            error(spreadExprObj, DiagID::ERR_MEMBER_PRIVATE,
                  toka::Type::stripMorphology(defField.Name), SD->Name);
          }
          
          auto memberTypeObj = getPhysicalType(defField);

          std::string prefix = "";
          if (memberTypeObj->isUniquePtr()) prefix = "^";
          else if (memberTypeObj->isSharedPtr()) prefix = "~";
          else if (memberTypeObj->isRawPointer()) prefix = "*";
          else if (memberTypeObj->isReference()) prefix = "&";

          auto memberAccess = std::make_unique<MemberExpr>(cloneNode(spreadNode->Base), prefix + defField.Name);
          memberAccess->Loc = spreadExprObj->Loc;

          const bool isResource =
              memberTypeObj->requiresExplicitOwnershipTransfer(this);

          if (isResource) {
            bool isBaseTemporary = dynamic_cast<MethodCallExpr*>(spreadNode->Base.get()) != nullptr || 
                                   dynamic_cast<CallExpr*>(spreadNode->Base.get()) != nullptr;
                                   
            if (!isSpreadCede && !isBaseTemporary) {
              error(spreadExprObj, DiagID::ERR_SEMA_FIELD_OF_TYPE_IS_NON_COPYABLE_YOU_MUST_EX, defField.Name, memberTypeObj->toString());
              recordDecision(
                  spreadExprObj, SemanticRuleID::OwnResource001,
                  SemanticOperation::ResourceCopy,
                  SemanticDecision::Reject,
                  SemanticReason::ResourceCopyForbidden, defField.Name,
                  resolvedName, SD->Loc);
              if (SD->Loc.isValid())
                DiagnosticEngine::report(SD->Loc, DiagID::NOTE_GENERIC,
                                         "resource-bearing type declared here");
            }
          }

          std::unique_ptr<Expr> memberInitializer = std::move(memberAccess);
          // `cede source.*` transfers every resource-bearing member that the
          // spread synthesizes.  Preserve that transfer on each synthetic
          // member expression so the ordinary named-field ownership check and
          // CodeGen see the same explicit boundary as the source spread.
          if (isSpreadCede && isResource) {
            auto cededMember = std::make_unique<CedeExpr>(std::move(memberInitializer));
            cededMember->Loc = spreadExprObj->Loc;
            memberInitializer = std::move(cededMember);
          }
          Init->Members.push_back({prefix + defField.Name, std::move(memberInitializer)});
        }
      }

      // Remove the spread operator placeholder from Members
      Init->Members.erase(std::remove_if(Init->Members.begin(), Init->Members.end(),
          [](const auto& pair) { return pair.first == "*"; }), Init->Members.end());
    }
  }

  for (size_t i = 0; i < Init->Members.size(); ++i) {
    auto &pair = Init->Members[i];
    if (pair.first == "..") {
      hasElision = true;
      continue;
    }

    if (providedFields.count(pair.first)) {
      DiagnosticEngine::report(getLoc(Init), DiagID::ERR_DUPLICATE_FIELD,
                               pair.first);
      HasError = true;
    }
    providedFields.insert(pair.first);

    bool fieldFound = false;
    const ShapeMember *pDefMember = nullptr;
    for (const auto &defField : SD->Members) {
      auto cleanDef = defField.Name;
      while (!cleanDef.empty() &&
             (cleanDef.back() == '#' || cleanDef.back() == '!' ||
              cleanDef.back() == '?'))
        cleanDef.pop_back();

      auto cleanProv = pair.first;
      while (!cleanProv.empty() &&
             (cleanProv.back() == '#' || cleanProv.back() == '!' ||
              cleanProv.back() == '?'))
        cleanProv.pop_back();

      if (cleanDef == cleanProv || toka::Type::stripMorphology(cleanDef) ==
                                       toka::Type::stripMorphology(cleanProv)) {
        fieldFound = true;
        pDefMember = &defField;
        break;
      }
    }

    if (!fieldFound) {
      error(Init, DiagID::ERR_NO_SUCH_MEMBER, resolvedName, pair.first);
      continue;
    }

    auto memberTypeObj = pDefMember->ResolvedType;
    if (!memberTypeObj)
      memberTypeObj = toka::Type::fromString(pDefMember->Type);

    // [New] Morphic Exemption: Validating Caller Transparency
    if (!pDefMember->IsMorphicExempt) {
      MorphKind providedMorph = MorphKind::None;
      if (pair.first.find('^') != std::string::npos) providedMorph = MorphKind::Unique;
      else if (pair.first.find('~') != std::string::npos) providedMorph = MorphKind::Shared;
      else if (pair.first.find('&') != std::string::npos) providedMorph = MorphKind::Ref;
      else if (pair.first.find('*') != std::string::npos) providedMorph = MorphKind::Raw;

      MorphKind expectedMorph = morphKindFromType(memberTypeObj);

      checkStrictMorphology(Init, expectedMorph, providedMorph, pDefMember->Name);
    }

    std::shared_ptr<toka::Type> exprTypeObj =
        checkExpr(pair.second.get(), memberTypeObj);
    memberMasks[pair.first] = m_LastInitMask;

    // Named field initialization is an ownership boundary just like a `cede`
    // parameter. An owned lvalue cannot be bitwise-copied into the new
    // aggregate: that would leave two cleanup owners for the same value.
    // Aliases (`~`, `&`, raw) retain their existing, non-consuming paths.
    const bool fieldReceivesOwnedValue =
        memberTypeObj &&
        memberTypeObj->requiresExplicitOwnershipTransfer(this);
    if (fieldReceivesOwnedValue &&
        !dynamic_cast<CedeExpr *>(pair.second.get())) {
      Expr *directSource = pair.second.get();
      while (auto *cast = dynamic_cast<CastExpr *>(directSource))
        directSource = cast->Expression.get();
      // An access path denotes existing storage (a local, projection, or
      // index), unlike a fresh call/constructor result.  Any owned value read
      // from that storage needs an explicit transfer at this new field
      // boundary.
      const bool isOwnedLValueSource =
          static_cast<bool>(makeAccessPath(directSource));
      if (isOwnedLValueSource) {
        const std::string sourceName = ownershipSourceLabel(directSource);
        error(pair.second.get(),
              DiagID::ERR_SEMA_FIELD_INITIALIZER_MUST_BE_EXPLICITLY_CEDED,
              pair.first, sourceName, sourceName);
      }
    }

    // A field declaration is a declaration boundary.  It cannot turn a
    // Shared direct source into a payload-writable view for later readers.
    PermissionFlow memberFlow = getPermissionFlow(pair.second.get());
    if (memberFlow.Kind == PermissionFlowKind::Shared &&
        requiresPayloadWrite(memberTypeObj) &&
        !memberFlow.DirectCapability.PayloadWritable) {
      error(pair.second.get(),
            DiagID::ERR_SEMA_COVENANT_VIOLATION_CANNOT_ELEVATE_WRITE_P);
      HasError = true;
    }

    if ((isTypeCompatible(memberTypeObj, exprTypeObj) ||
         isFieldInitializerCompatible(memberTypeObj, exprTypeObj)) &&
        !memberTypeObj->equals(*exprTypeObj)) {
      auto origLoc = pair.second->Loc;
      pair.second = std::make_unique<CastExpr>(std::move(pair.second),
                                               memberTypeObj->toString());
      pair.second->Loc = origLoc;
      pair.second->ResolvedType = memberTypeObj;
      exprTypeObj = memberTypeObj;
    }

    bool bypassNullStruct = false;
    if (m_InUnsafeContext && memberTypeObj && memberTypeObj->isRawPointer() && exprTypeObj && exprTypeObj->isNullType()) {
        bypassNullStruct = true;
    }

    if (!bypassNullStruct &&
        !(isTypeCompatible(memberTypeObj, exprTypeObj) ||
          isFieldInitializerCompatible(memberTypeObj, exprTypeObj))) {
      error(Init, DiagID::ERR_MEMBER_TYPE_MISMATCH, pair.first,
            memberTypeObj->toString(), exprTypeObj->toString());
    }

    // Borrow-like fields carry dependencies directly from their initializer.
    if (memberTypeObj &&
        (memberTypeObj->isReference() || memberTypeObj->isPointer() ||
         memberTypeObj->isSmartPointer())) {
        if (!m_LastBorrowSource.empty()) {
          m_LastLifeDependencies.insert(m_LastBorrowSource);
        }
    }
  }

  if (hasElision && providedFields.size() == SD->Members.size()) {
    DiagnosticEngine::report(getLoc(Init), DiagID::ERR_REDUNDANT_ELISION);
    HasError = true;
  }

  // Missing fields check for struct and enum-payload records
  for (const auto &defField : SD->Members) {
    if (!providedFields.count(defField.Name) &&
        !providedFields.count("^" + defField.Name) &&
        !providedFields.count("*" + defField.Name) &&
        !providedFields.count("~" + defField.Name) &&
        !providedFields.count("&" + defField.Name) &&
        !providedFields.count("^?" + defField.Name)) {
      
      if (!hasElision) {
        if (defField.DefaultValue) {
           error(Init, DiagID::ERR_SEMA_MISSING_FIELD_IN_CONSTRUCTOR_FOR_USE_TO_E, defField.Name, Init->ShapeName);
        } else {
           error(Init, DiagID::ERR_SEMA_MISSING_FIELD_IN_CONSTRUCTOR_FOR_NO_DEFAU, defField.Name, Init->ShapeName);
        }
        continue;
      }
      
      if (!defField.DefaultValue) {
        error(Init, DiagID::ERR_MISSING_DEFAULT_FOR_ELIDED, defField.Name, Init->ShapeName);
        continue;
      }

      // Inject default value
      auto cloned = std::unique_ptr<Expr>(
          static_cast<Expr *>(defField.DefaultValue->clone().release()));

      auto memberTypeObj = defField.ResolvedType;
      if (!memberTypeObj)
          memberTypeObj = getPhysicalType(defField);

        std::shared_ptr<toka::Type> exprTypeObj =
            checkExpr(cloned.get(), memberTypeObj);
        memberMasks[defField.Name] = m_LastInitMask;

        PermissionFlow memberFlow = getPermissionFlow(cloned.get());
        if (memberFlow.Kind == PermissionFlowKind::Shared &&
            requiresPayloadWrite(memberTypeObj) &&
            !memberFlow.DirectCapability.PayloadWritable) {
          error(cloned.get(),
                DiagID::ERR_SEMA_COVENANT_VIOLATION_CANNOT_ELEVATE_WRITE_P);
          HasError = true;
        }

        if ((isTypeCompatible(memberTypeObj, exprTypeObj) ||
             isFieldInitializerCompatible(memberTypeObj, exprTypeObj)) &&
            !memberTypeObj->equals(*exprTypeObj)) {
          auto origLoc = cloned->Loc;
          cloned = std::make_unique<CastExpr>(std::move(cloned),
                                              memberTypeObj->toString());
          cloned->Loc = origLoc;
          cloned->ResolvedType = memberTypeObj;
          exprTypeObj = memberTypeObj;
        }

        bool bypassNullStruct = false;
        if (m_InUnsafeContext && memberTypeObj && memberTypeObj->isRawPointer() && exprTypeObj && exprTypeObj->isNullType()) {
            bypassNullStruct = true;
        }

        if (!bypassNullStruct &&
            !(isTypeCompatible(memberTypeObj, exprTypeObj) ||
              isFieldInitializerCompatible(memberTypeObj, exprTypeObj))) {
          error(Init, DiagID::ERR_MEMBER_TYPE_MISMATCH, defField.Name,
                memberTypeObj->toString(), exprTypeObj->toString());
        }
        providedFields.insert(defField.Name);
        Init->Members.push_back({defField.Name, std::move(cloned)});
    }
  }

  Init->Members.erase(std::remove_if(Init->Members.begin(), Init->Members.end(),
      [](const auto& pair) { return pair.first == ".."; }), Init->Members.end());

  // Mask Calculation
  uint64_t mask = 0;
  for (int i = 0; i < (int)SD->Members.size(); ++i) {
    std::string memName = SD->Members[i].Name;
    for (const auto &pair : Init->Members) {
      if (toka::Type::stripMorphology(pair.first) ==
          toka::Type::stripMorphology(memName)) {
        m_LastInitMask = memberMasks[pair.first];
        std::shared_ptr<Type> memTypeObj = getPhysicalType(SD->Members[i]);
        uint64_t expected = 1;
        if (memTypeObj->isShape()) {
          std::string sName = memTypeObj->getSoulName();
          if (ShapeMap.count(sName)) {
            size_t sz = ShapeMap[sName]->Members.size();
            expected = (sz >= 64) ? ~0ULL : (1ULL << sz) - 1;
          }
        }
        if ((m_LastInitMask & expected) == expected) {
          if (i < 64)
            mask |= (1ULL << i);
        }
        break;
      }
    }
  }
  m_LastInitMask = mask;

  if (!baseVarActualName.empty()) {
    CurrentScope->markMoved(baseVarActualName, Init->Loc);
  }

  auto resultType = std::make_shared<toka::ShapeType>(resolvedName);
  resultType->resolve(SD);
  return resultType;
}

std::shared_ptr<toka::Type>
Sema::checkVariantInit(InitStructExpr *Init, ShapeDecl *SD,
                     const std::string &resolvedName,
                     std::map<std::string, uint64_t> &memberMasks) {
  if (Init->Members.empty()) {
    error(Init, DiagID::ERR_MISSING_MEMBER, "at least one variant",
          Init->ShapeName);
    m_LastInitMask = 0;
    auto resultType = std::make_shared<toka::ShapeType>(resolvedName);
    resultType->resolve(SD);
    return resultType;
  }

  if (Init->Members.size() > 1) {
    error(Init, DiagID::ERR_SEMA_UNION_INITIALIZATION_MUST_SPECIFY_EXACTLY, Init->ShapeName, Init->Members.size());
  }

  auto &pair = Init->Members[0];
  bool fieldFound = false;
  const ShapeMember *pDefMember = nullptr;
  for (const auto &defField : SD->Members) {
    if (toka::Type::stripMorphology(defField.Name) ==
        toka::Type::stripMorphology(pair.first)) {
      fieldFound = true;
      pDefMember = &defField;
      break;
    }
  }

  if (!fieldFound) {
    error(Init, DiagID::ERR_NO_SUCH_MEMBER, resolvedName, pair.first);
  } else {
    auto memberTypeObj = pDefMember->ResolvedType;
    if (!memberTypeObj)
      memberTypeObj = toka::Type::fromString(pDefMember->Type);

    // [New] Morphic Exemption: validate caller transparency for variants
    if (!pDefMember->IsMorphicExempt) {
      MorphKind providedMorph = MorphKind::None;
      if (pair.first.find('^') != std::string::npos) providedMorph = MorphKind::Unique;
      else if (pair.first.find('~') != std::string::npos) providedMorph = MorphKind::Shared;
      else if (pair.first.find('&') != std::string::npos) providedMorph = MorphKind::Ref;
      else if (pair.first.find('*') != std::string::npos) providedMorph = MorphKind::Raw;

      MorphKind expectedMorph = morphKindFromType(memberTypeObj);

      checkStrictMorphology(Init, expectedMorph, providedMorph, pDefMember->Name);
    }

    std::shared_ptr<toka::Type> exprTypeObj =
        checkExpr(pair.second.get(), memberTypeObj);
    m_LastInitMask = ~0ULL; // Variant initializer is complete if one field is set

    PermissionFlow memberFlow = getPermissionFlow(pair.second.get());
    if (memberFlow.Kind == PermissionFlowKind::Shared &&
        requiresPayloadWrite(memberTypeObj) &&
        !memberFlow.DirectCapability.PayloadWritable) {
      error(pair.second.get(),
            DiagID::ERR_SEMA_COVENANT_VIOLATION_CANNOT_ELEVATE_WRITE_P);
      HasError = true;
    }

    bool bypassNullStruct = false;
    if (m_InUnsafeContext && memberTypeObj && memberTypeObj->isRawPointer() && exprTypeObj && exprTypeObj->isNullType()) {
        bypassNullStruct = true;
    }

    if (!bypassNullStruct &&
        !(isTypeCompatible(memberTypeObj, exprTypeObj) ||
          isFieldInitializerCompatible(memberTypeObj, exprTypeObj))) {
      error(Init, DiagID::ERR_MEMBER_TYPE_MISMATCH, pair.first,
            memberTypeObj->toString(), exprTypeObj->toString());
    }

    // [Legacy] Retain historical storage-size validation for variant initializers.
    uint64_t unionSize = 0;
    for (auto &m : SD->Members) {
      auto mT = getPhysicalType(m);
      uint64_t s = getTypeSize(mT);
      if (s > unionSize)
        unionSize = s;
    }
    uint64_t variantSize = getTypeSize(memberTypeObj);
    if (variantSize < unionSize) {
      DiagnosticEngine::report(getLoc(Init), DiagID::ERR_UNION_PARTIAL_INIT,
                               SD->Name, pair.first, variantSize, unionSize);
      HasError = true;
    }
  }

  m_LastInitMask = ~0ULL;
  auto retTy = std::make_shared<toka::ShapeType>(resolvedName);
  retTy->resolve(SD);
  return retTy;
}

} // namespace toka
