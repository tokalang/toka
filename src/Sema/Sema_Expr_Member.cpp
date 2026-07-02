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
#include "toka/HandleSurfaceStats.h"
#include "toka/MemberAccess.h"
#include "toka/PathUtils.h"
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

static std::string stripModuleExtension(std::string path) {
  if (path.size() > 4 && path.substr(path.size() - 3) == ".tk") {
    path.resize(path.size() - 3);
  } else if (path.size() > 5 && path.substr(path.size() - 4) == ".tki") {
    path.resize(path.size() - 4);
  }
  return path;
}

static bool modulePathMatchesTarget(const std::string &moduleFile,
                                    const std::string &targetPath) {
  if (targetPath.empty())
    return false;

  std::string target = toka::PathUtils::normalize(targetPath);
  std::replace(target.begin(), target.end(), '\\', '/');
  target = stripModuleExtension(target);
  while (!target.empty() && target.front() == '/') {
    target.erase(target.begin());
  }
  while (!target.empty() && target.back() == '/') {
    target.pop_back();
  }
  if (target.empty())
    return false;

  std::string module = toka::PathUtils::canonicalize(moduleFile);
  module = toka::PathUtils::normalize(module);
  std::replace(module.begin(), module.end(), '\\', '/');
  module = stripModuleExtension(module);

  auto segmentMatch = [&](const std::string &candidate) {
    if (candidate == target)
      return true;
    if (candidate.rfind(target + "/", 0) == 0)
      return true;
    if (candidate.size() > target.size() &&
        candidate.compare(candidate.size() - target.size(), target.size(),
                          target) == 0 &&
        candidate[candidate.size() - target.size() - 1] == '/') {
      return true;
    }
    return candidate.find("/" + target + "/") != std::string::npos;
  };

  if (segmentMatch(module))
    return true;

  size_t slash = module.find_last_of('/');
  std::string base = slash == std::string::npos ? module : module.substr(slash + 1);
  if (segmentMatch(base))
    return true;

  size_t libPos = module.find("/lib/");
  if (libPos != std::string::npos && segmentMatch(module.substr(libPos + 5)))
    return true;

  size_t testsPos = module.find("/tests/");
  if (testsPos != std::string::npos &&
      segmentMatch("tests/" + module.substr(testsPos + 7))) {
    return true;
  }

  return false;
}

static std::string getStringifyPath(Expr *E) {
  if (!E)
    return "";
  if (auto *ve = dynamic_cast<VariableExpr *>(E)) {
    return ve->Name;
  }
  if (auto *me = dynamic_cast<MemberExpr *>(E)) {
    std::string member =
        toka::Type::stripMorphology(parseMemberAccess(me->Member).StrippedName);
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
  return "";
}

std::shared_ptr<toka::Type> Sema::checkMemberExpr(MemberExpr *Memb) {
  recordHandleSurfaceMemberExpr(*Memb);

  MemberAccessIntent intent = parseMemberAccess(Memb->Member);
  bool hasInvalidMix = false;

  // 1. 空断言排他性 (Null Assertion Exclusivity)
  if (intent.IsIdentityAssertion || intent.IsIdentityOperator) {
    if (intent.AccessHats > 0 || intent.HasRebindMarker) {
      hasInvalidMix = true;
    }
    if (!intent.MemberName.empty() && intent.MemberName[0] != '\'' && isMemberAccessPrefixChar(intent.MemberName[0])) {
      hasInvalidMix = true;
    }
  }

  // 2. 重绑定去重 (Rebind Suffix/Prefix Uniqueness)
  int hashCount = 0;
  for (char c : Memb->Member) {
    if (c == '#') {
      hashCount++;
    }
  }
  if (hashCount > 1) {
    hasInvalidMix = true;
  }

  // 3. 顺序与连续性合法性 (Prefix Ordering & Hats Adjacency)
  bool seenHash = false;
  for (char c : intent.Prefix) {
    if (c == '#') {
      seenHash = true;
    } else if (isMemberAccessHatChar(c)) {
      if (seenHash) {
        hasInvalidMix = true; // Hat after Hash is invalid
      }
    }
  }

  // 5. Morphic Identity 排他性 (Morphic Identity Exclusivity)
  if (intent.IsMorphicIdentity) {
    if (intent.HasExplicitPrefix || intent.HasRebindMarker) {
      hasInvalidMix = true;
    }
  }

  if (hasInvalidMix) {
    DiagnosticEngine::report(getLoc(Memb), DiagID::ERR_GENERIC_SEMA, "Invalid member access modifier combination: '" + Memb->Member + "'");
    HasError = true;
    return nullptr;
  }

  std::string path = getStringifyPath(Memb);
  bool isNarrowed = m_NarrowedPaths.count(path);

  // [Ch 5] Single Hat Principle & Terminal Marking Trace
  bool oldIntermediate = m_InIntermediatePath;
  m_InIntermediatePath = true; // Sub-indices are intermediate

  // [FIX] Peeling for Semantic Access (Internal)
  std::shared_ptr<toka::Type> objTypeObj;

  // Rule: Intermediate paths MUST NOT have explicit pointer sigils or
  // write sigils Check if current Memb is intermediate or terminal
  bool isTerminal = m_IsAssignmentTarget; // Only terminals can have # or ~^*&

  bool savedDisable = m_DisableSoulCollapse;
  m_DisableSoulCollapse = true;
  bool savedMemberBase = m_IsMemberBase;
  m_IsMemberBase = true;
  Memb->Object = foldGenericConstant(std::move(Memb->Object)); // [Phase 2]
  objTypeObj = checkExpr(Memb->Object.get());
  m_IsMemberBase = savedMemberBase;
  m_DisableSoulCollapse = savedDisable;
  m_InIntermediatePath = oldIntermediate;

  // Unset Check for Member Access
  if (!m_InLHS) {
    if (auto *objVar = dynamic_cast<VariableExpr *>(Memb->Object.get())) {
      SymbolInfo *Info = nullptr;
      std::string actualObjName = objVar->Name;
      if (CurrentScope->findVariableWithDeref(objVar->Name, Info, actualObjName)) {
        // [Rule] Borrowing check for Member Access
        if (!m_InIntermediatePath && !path.empty()) {
           std::string conflictPath = PALCheckerState.verifyAccess(path);
           if (!conflictPath.empty()) {
               DiagnosticEngine::report(getLoc(Memb), DiagID::ERR_BORROW_MUT, conflictPath);
               HasError = true;
           }
        }

        if (Info->TypeObj && Info->TypeObj->isShape()) {
          // Determine which mask to check: InitMask (for values) or
          // DirtyReferentMask (for references)
          uint64_t maskToCheck = Info->InitMask;
          if (Info->IsReference()) {
            maskToCheck = Info->DirtyReferentMask;
          }

          std::string soul = Info->TypeObj->getSoulName();
          if (ShapeMap.count(soul)) {
            ShapeDecl *SD = ShapeMap[soul];
            for (int i = 0; i < (int)SD->Members.size(); ++i) {
              if (SD->Members[i].Name == Memb->Member) {
                if (i < 64 && !(maskToCheck & (1ULL << i))) {
                  DiagnosticEngine::report(getLoc(Memb), DiagID::ERR_USE_UNSET,
                                           objVar->Name + "." + Memb->Member);
                  HasError = true;
                }
                break;
              }
            }
          }
        }
      }
    }
  }

  std::string ObjTypeFull = objTypeObj->toString();

  // Visibility Check for Private Member (Robust check using EncapMap
  // exists later in loop)

  if (ObjTypeFull == "module") {
    // It's a module access
    if (auto *objVar = dynamic_cast<VariableExpr *>(Memb->Object.get())) {
      SymbolInfo *modSpecPtr = nullptr;
      std::string actualModName = objVar->Name;
      if (CurrentScope->findVariableWithDeref(objVar->Name, modSpecPtr, actualModName) &&
          modSpecPtr->ReferencedModule) {
        SymbolInfo modSpec = *modSpecPtr;
        modSpecPtr->HasBeenUsed = true;
        if (modSpecPtr->ImportingDecl) {
          const_cast<ImportDecl*>(modSpecPtr->ImportingDecl)->HasBeenUsed = true;
        }
        ModuleScope *target = (ModuleScope *)modSpec.ReferencedModule;
        if (target->Functions.count(Memb->Member)) {
          return toka::Type::fromString("fn");
        }
        if (target->Globals.count(Memb->Member)) {
          return toka::Type::fromString(
              resolveType(target->Globals[Memb->Member]->TypeName));
        }
      }
    }
  }

  // std::cerr << "DEBUG: checkMemberExpr Object=" <<
  // objTypeObj->toString()
  //           << " IsNullable=" << objTypeObj->IsNullable
  //           << " isNarrowed=" << isNarrowed << "\n";

  // [Ch 6.2] LHS Write Exemption
  bool isLHSTarget = m_InLHS && m_IsAssignmentTarget;
  if (objTypeObj->IsNullable && !isLHSTarget) {
    DiagnosticEngine::report(getLoc(Memb), DiagID::ERR_NULL_ACCESS,
                             objTypeObj->toString());
    HasError = true;
  }

  // [Ch 5.5] Implicit Dereference for soul access
  // If Object is a pointer but we use '.', we must treat it as
  // accessing the soul. Exception: Identity properties or methods.
  if (objTypeObj->isPointer() || objTypeObj->isSmartPointer()) {
    objTypeObj = objTypeObj->getSoulType();
  }

  std::string ObjType =
      toka::Type::stripMorphology(resolveType(objTypeObj, true)->toString());

  if (ShapeMap.count(ObjType)) {
    ShapeDecl *SD = ShapeMap[ObjType];
    MemberAccessIntent access = parseMemberAccess(Memb->Member);
    std::string requestedMember = access.MemberName;
    std::string requestedPrefix = access.Prefix;
    bool requestedMorphicIdentity = access.IsMorphicIdentity;

    // [Ch 5] Single Hat & Terminal Audit: Except for ?? assertion
    if (m_InIntermediatePath && !requestedPrefix.empty() &&
        requestedPrefix != "??") {
      error(Memb, DiagID::ERR_SEMA_POINTER_MORPHOLOGY_AND_PERMISSION_SYMBOLS, requestedPrefix);
    }

    for (int i = 0; i < (int)SD->Members.size(); ++i) {
      const auto &Field = SD->Members[i];
      if (toka::Type::stripMorphology(Field.Name) == requestedMember) {
        if (requestedMorphicIdentity && !Field.IsMorphicExempt) {
          error(Memb,
                DiagID::ERR_SEMA_MORPHIC_MEMBER_ACCESS_REQUIRES_MORPHIC_FIELD,
                requestedMember, requestedMember, ObjType, requestedMember);
          return toka::Type::fromString("unknown");
        }
        Memb->Index = i; // [FIX] Set index for CodeGen
        Memb->IsMorphicExempt = Field.IsMorphicExempt; // [NEW]
        // Visibility Check: God-eye view (same file)
        std::string membFile =
            DiagnosticEngine::SrcMgr->getFullSourceLoc(Memb->Loc).FileName;
        std::string sdFile =
            DiagnosticEngine::SrcMgr->getFullSourceLoc(SD->Loc).FileName;

        if (membFile != sdFile) {
          // Check EncapMap
          std::string baseObjType = ObjType;
          if (baseObjType.find("_M_") != std::string::npos) {
              baseObjType = baseObjType.substr(0, baseObjType.find("_M_"));
          }
          std::string accessType = EncapMap.count(ObjType) ? ObjType : (EncapMap.count(baseObjType) ? baseObjType : "");

          if (!accessType.empty()) {
            bool accessible = false;
            for (const auto &entry : EncapMap[accessType]) {
              bool fieldMatches = false;
              if (entry.IsExclusion) {
                fieldMatches = true;
                for (const auto &f : entry.Fields) {
                  if (f == requestedMember) {
                    fieldMatches = false;
                    break;
                  }
                }
              } else {
                for (const auto &f : entry.Fields) {
                  if (f == requestedMember) {
                    fieldMatches = true;
                    break;
                  }
                }
              }

              if (fieldMatches) {
                if (entry.Level == EncapEntry::Global) {
                  accessible = true;
                } else if (entry.Level == EncapEntry::Crate) {
                  accessible = true;
                } else if (entry.Level == EncapEntry::Path) {
                  if (modulePathMatchesTarget(membFile, entry.TargetPath)) {
                    accessible = true;
                  }
                }
              }
              if (accessible)
                break;
            }

            if (!accessible) {
              error(Memb, DiagID::ERR_MEMBER_PRIVATE, requestedMember, ObjType);
            }
          }
        }

        // Return type based on Toka 1.3 Pointer-Value Duality
        std::shared_ptr<toka::Type> fieldType;
        if (requestedMorphicIdentity && Field.ResolvedType) {
          fieldType = Field.ResolvedType;
        } else {
          std::string fullType = Sema::synthesizePhysicalType(Field);
          fieldType = toka::Type::fromString(fullType);
        }

        // [Ch 5.4] Insulation: Pointers physically break permission
        // inheritance
        bool isSoulInsulated = fieldType->isPointer() ||
                               fieldType->isSmartPointer() ||
                               fieldType->isReference();

        // 1. Determine Soul Writability
        bool finalSoulWritable = false;
        if (Field.IsValueMutable) {
          finalSoulWritable = true;
        } else if (Field.IsValueBlocked) {
          finalSoulWritable = false;
        } else {
          // [Toka 1.3] Inheritance: Pointers usually block.
          // EXCEPTION: If we are on the LHS, or if the usage explicitly
          // showed # (handled via Postfix wrapper) Since
          // checkMemberExpr doesn't see the Postfix wrapper easily, we
          // rely on m_InLHS or m_IsAssignmentTarget.
          bool permitInheritance = !isSoulInsulated;
          finalSoulWritable =
              permitInheritance ? objTypeObj->IsWritable : false;
        }

        // [Toka 1.3] Unit Variant Support: Allow omission of
        // parentheses
        if (SD->Kind == ShapeKind::Enum || SD->Kind == ShapeKind::Union) {
          bool isUnit = (Field.Type == "void" || Field.Type.empty());
          if (isUnit) {
            Memb->IsStatic =
                true; // Mark as static for CodeGen to generate constant
            // Use fieldType directly? No, fieldType is void.
            // We return the Object Type (The Enum Type) as the value.
            // Ensure no writability or nullability permissions are
            // blindly inherited for value.
            return objTypeObj->withAttributes(false, false);
          }
        }

        // Apply soul writing to the fieldType itself if it's a pointer
        if (finalSoulWritable) {
          if (auto pt = fieldType->getPointeeType())
            pt->IsWritable = true;
          else
            fieldType->IsWritable = true;
        }

        if (requestedPrefix.empty() && !requestedMorphicIdentity &&
            !m_DisableSoulCollapse) {
          // obj.field (Hat-Off) -> Soul Collapse.
          return fieldType->getSoulType()->withAttributes(
              finalSoulWritable, isNarrowed ? false : fieldType->IsNullable);
        } else {
          // Hatted access, morphic identity access, or disabled soul collapse.
          // Use fieldType directly as the base (preserving its
          // morphologies)
          std::shared_ptr<toka::Type> result = fieldType;

          // [Toka 1.3] Handle Inheritance:
          // The Identity pointer (Handle) inherits its own writable
          // status from self#
          if (!result->IsBlocked) {
            result->IsWritable = result->IsWritable || objTypeObj->IsWritable;
          }

          // Parse intent from requestedPrefix
          bool intentWritable =
              requestedPrefix.find('#') != std::string::npos ||
              requestedPrefix.find('!') != std::string::npos;
          bool intentNullable = false;
          if (requestedPrefix != "??") {
            intentNullable = requestedPrefix.find('?') != std::string::npos ||
                             requestedPrefix.find('!') != std::string::npos;
          }

          // If requested prefix exists, ensure morphology matches or
          // wrap it
          if (!requestedPrefix.empty() && requestedPrefix != "??") {
            std::string baseMorph =
                toka::Type::stripMorphology(requestedPrefix);
            // If requestedPrefix was just attributes (like '??' or
            // '#'), baseMorph is empty. But here requestedPrefix is
            // like '*#' or
            // '^'. Actually, stripMorphology(requestedPrefix) where
            // requestedPrefix is '*#' returns empty string? Let's check
            // Type.cpp stripMorphology. It strips all @$#!?*^~&. Wait,
            // if requestedPrefix is '*#', stripMorphology returns "".
            // That's not helpful. I need the base sigil.

            char sigil = 0;
            for (char c : requestedPrefix) {
              if (c == '*' || c == '^' || c == '~' || c == '&') {
                sigil = c;
                break;
              }
            }

            if (sigil != 0) {
              bool matches = false;
              if (sigil == '&' && fieldType->isReference())
                matches = true;
              else if (sigil == '^' && fieldType->isUniquePtr())
                matches = true;
              else if (sigil == '~' && fieldType->isSharedPtr())
                matches = true;
              else if (sigil == '*' && fieldType->isRawPointer())
                matches = true;

              if (!matches) {
                // [Constitution 1.3] Smart Pointer Soul Borrowing
                // If the user requests '&' but the field is a unique/shared pointer,
                // and they didn't write '&^' or '&~', they want a reference to the SOUL.
                if (sigil == '&' && requestedPrefix == "&" && 
                    (fieldType->isUniquePtr() || fieldType->isSharedPtr())) {
                     result = std::make_shared<ReferenceType>(fieldType->getSoulType());
                } else {
                  // Shield/Wrap the type with the requested morphology
                  if (sigil == '&')
                    result = std::make_shared<ReferenceType>(fieldType);
                  else if (sigil == '^')
                    result = std::make_shared<UniquePointerType>(fieldType);
                  else if (sigil == '~')
                    result = std::make_shared<SharedPointerType>(fieldType);
                  else if (sigil == '*')
                    result = std::make_shared<RawPointerType>(fieldType);
                }
              }
            }
          }

          if (requestedPrefix == "??") {
            // Identity Assertion (Ch 6.1)
            if (!fieldType->isPointer() && !fieldType->isSmartPointer()) {
              error(Memb, DiagID::ERR_SEMA_IDENTITY_ASSERTION_CAN_ONLY_BE_APPLIED_TO, fieldType->toString());
            }
            result = fieldType->withAttributes(fieldType->IsWritable, false);
          }

          // 2. Determine Handle Writability
          bool finalHandleWritable = false;
          if (Field.IsRebindBlocked) {
            finalHandleWritable = false;
          } else if (intentWritable) {
            // Explicitly requested #/! -> check authorization
            finalHandleWritable = objTypeObj->IsWritable;
          } else if (Field.IsRebindable) {
            finalHandleWritable = result->IsWritable;
          } else {
            // Default: Inherit Handle Writability from Object Soul
            // (Bloodline)
            finalHandleWritable = objTypeObj->IsWritable;
          }

          return result->withAttributes(
              finalHandleWritable, intentNullable ? true : result->IsNullable);
        }
      }
    }
    error(Memb, DiagID::ERR_NO_SUCH_MEMBER, ObjType, Memb->Member);

    return toka::Type::fromString("unknown");
  } else {
    if (ObjType != "unknown") {
      error(Memb, DiagID::ERR_NOT_A_STRUCT, Memb->Member, ObjType);
    }
  }
  return toka::Type::fromString("unknown");
}

std::shared_ptr<toka::Type> Sema::checkIndexExpr(ArrayIndexExpr *Idx) {
  // 1. Validate Indices (must be integer loops)
  for (auto &idxExpr : Idx->Indices) {
    idxExpr = foldGenericConstant(std::move(idxExpr)); // [FIX]
    auto idxType = checkExpr(idxExpr.get());
    if (!idxType->isInteger()) {
      error(Idx, DiagID::ERR_SEMA_ARRAY_INDEX_MUST_BE_INTEGER_GOT, idxType->toString());
    }
  }

  // 2. Resolve Base Type
  std::shared_ptr<toka::Type> baseType = nullptr;

  // [Constitution] Indexing targets Identity (Pointer/Array), not Soul.
  if (auto *Var = dynamic_cast<VariableExpr *>(Idx->Array.get())) {
    SymbolInfo *Info = nullptr;
    std::string actualName = Var->Name;
    if (CurrentScope->findVariableWithDeref(Var->Name, Info, actualName)) {
      baseType = Info->TypeObj;
      if (Info->IsMutable()) {
        baseType = baseType->withAttributes(true, baseType->IsNullable);
      }
      if (Info->TypeObj &&
          (Info->TypeObj->isPointer() || Info->TypeObj->isSmartPointer() ||
           Info->TypeObj->isReference())) {
        Info->HasHandleBeenUsed = true;
      }
      if (m_InLHS) {
        Info->HasBeenMutated = true;
      }
      Info->HasBeenUsed = true;
    } else {
      baseType = checkExpr(Idx->Array.get());
    }
  } else {
    bool old = m_DisableSoulCollapse;
    m_DisableSoulCollapse = true;
    baseType = checkExpr(Idx->Array.get());
    m_DisableSoulCollapse = old;
  }

  if (!baseType || baseType->isUnknown())
    return toka::Type::fromString("unknown");

  std::shared_ptr<toka::Type> resultType = nullptr;
  baseType = resolveType(baseType, true);

  std::string typeStr = toka::Type::stripMorphology(baseType->toString());
  if (typeStr == "str" || typeStr == "String" || typeStr == "string") {
    error(Idx, DiagID::ERR_SEMA_ERROR_E0490_SUBSCRIPT_INDEXING_IS_DISABLE, typeStr);
    return toka::Type::fromString("unknown");
  }

  if (baseType->isArray()) {
    resultType = baseType->getArrayElementType();
  } else if (baseType->isPointer()) {
    // Pointer indexing dispatch:
    // 1. Hatted Base (*p[i] or self.*buf[i]) -> Handle Arithmetic (Stride) ->
    // Returns Pointer
    // 2. Unhatted Base (p[i] or self.buf[i]) -> Soul Access (Deref) -> Returns
    // Value

    auto ptrMorph = baseType->getMorphology();
    auto pointee = baseType->getPointeeType();
    bool isSafeSlice = false;
    if (pointee) {
        if (std::dynamic_pointer_cast<toka::SliceType>(resolveType(pointee, true))) {
             if (ptrMorph == toka::Type::Morphology::Unique || ptrMorph == toka::Type::Morphology::Shared) {
                 isSafeSlice = true;
             }
        }
    }

    if (!m_InUnsafeContext && !isSafeSlice) {
      error(Idx, DiagID::ERR_SEMA_RAW_POINTER_INDEXING_REQUIRES_UNSAFE_CONT);
    }

    MorphKind morph = getSyntacticMorphology(Idx->Array.get());
    bool isHatted = (morph == MorphKind::Raw || morph == MorphKind::Unique ||
                     morph == MorphKind::Shared);

    if (isHatted) {
      // [Fix] Handle Indexing (Pointer Arithmetic)
      if (Idx->Indices.size() != 1) {
        error(Idx, DiagID::ERR_SEMA_POINTER_HANDLE_INDEXING_SUPPORTS_ONLY_ONE);
      }
      resultType = baseType;
    } else {
      // [Default] Soul Indexing (Value Access)
      auto pointee = baseType->getPointeeType();
      if (pointee) {
        auto resolvedPointee = resolveType(pointee, true);
        if (auto slice = std::dynamic_pointer_cast<toka::SliceType>(resolvedPointee)) {
          // [Safety Pillar 3] Uninit subscript ban
          if (slice->ElementType->isUninit() && !m_InUnsafeContext) {
             error(Idx, DiagID::ERR_SEMA_CANNOT_SAFELY_SUBSCRIPT_INTO_AN_UNINITIAL);
          }
          resultType = slice->ElementType->withAttributes(baseType->IsWritable || slice->IsWritable || slice->ElementType->IsWritable, slice->ElementType->IsNullable);
        } else if (auto arr = std::dynamic_pointer_cast<toka::ArrayType>(resolvedPointee)) {
          resultType = arr->ElementType->withAttributes(baseType->IsWritable || arr->IsWritable || arr->ElementType->IsWritable, arr->ElementType->IsNullable);
        } else {
          error(Idx, DiagID::ERR_SEMA_ARRAY_INDEXING_IS_ONLY_PERMITTED_ON_ARRAY, baseType->toString());
          std::cerr << "DEBUG: E0406 generated for node type " << Idx->toString() << "\n";
          resultType = pointee;
        }
      }
    }
  } else {
    error(Idx, DiagID::ERR_SEMA_TYPE_IS_NOT_INDEXABLE, baseType->toString());
    return toka::Type::fromString("unknown");
  }

  if (!resultType)
    return toka::Type::fromString("unknown");

  // [Ch 5.4] Permission Inheritance:
  // Arrays inherit writability from their handle.
  // Pointers do NOT inherit from handle; they use their own Pointee
  // attributes (Insulation).
  if (baseType->isArray()) {
    bool isBaseWritable = baseType->IsWritable;
    resultType =
        resultType->withAttributes(isBaseWritable, resultType->IsNullable);
  }

  return resultType;
}

} // namespace toka
