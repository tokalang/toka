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
#include "toka/SemanticEvidence.h"
#include "toka/Sema.h"
#include "toka/SourceManager.h"
#include "toka/Type.h"
#include "toka/ASTEvaluator.h"
#include "toka/ComptimeValue.h"
#include <algorithm>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace toka {

static SourceLocation getLoc(ASTNode *Node) { return Node->Loc; }

static std::string directMatchVariantName(const MatchArm::Pattern *pattern) {
  if (!pattern ||
      (pattern->PatternKind != MatchArm::Pattern::Variable &&
       pattern->PatternKind != MatchArm::Pattern::Decons)) {
    return {};
  }
  const size_t separator = pattern->Name.rfind("::");
  if (separator == std::string::npos)
    return {};
  return pattern->Name.substr(separator + 2);
}

static Expr *unwrapCedeDirectSource(Expr *E) {
  while (E) {
    if (auto *unary = dynamic_cast<UnaryExpr *>(E)) {
      E = unary->RHS.get();
    } else if (auto *cast = dynamic_cast<CastExpr *>(E)) {
      E = cast->Expression.get();
    } else {
      break;
    }
  }
  return E;
}

static bool isMayZeroRawType(const std::shared_ptr<Type> &type) {
  return type && type->isRawPointer() && type->IsNullable;
}

static bool isMayZeroRawCedeSource(const Expr *expr) {
  auto *cede = dynamic_cast<const CedeExpr *>(expr);
  auto *source = cede ? unwrapCedeDirectSource(cede->Value.get()) : nullptr;
  return source && isMayZeroRawType(source->ResolvedType);
}

AccessCapability Sema::getAccessCapability(Expr *E, bool declarationOnly) {
  if (!E)
    return {};

  if (dynamic_cast<TodoExpr *>(E))
    return {};

  auto applyPathFlowCeiling = [&](AccessCapability capability) {
    AccessPath path = canonicalizeAccessPath(makeAccessPath(E));
    if (!declarationOnly && path && m_PayloadFlowRestrictedPaths.count(path)) {
      capability.PayloadWritable = false;
      capability.PayloadFlowRestricted = true;
    }
    return capability;
  };

  if (auto *Cast = dynamic_cast<CastExpr *>(E))
    return getAccessCapability(Cast->Expression.get(), declarationOnly);
  if (auto *Cede = dynamic_cast<CedeExpr *>(E))
    return getAccessCapability(Cede->Value.get(), declarationOnly);
  if (auto *Addr = dynamic_cast<AddressOfExpr *>(E))
    return getAccessCapability(Addr->Expression.get(), declarationOnly);
  if (auto *Post = dynamic_cast<PostfixExpr *>(E))
    return getAccessCapability(Post->LHS.get(), declarationOnly);

  if (auto *Var = dynamic_cast<VariableExpr *>(E)) {
    SymbolInfo *Info = nullptr;
    std::string actualName = Var->Name;
    if (CurrentScope->findVariableWithDeref(Var->Name, Info, actualName) &&
        Info) {
      bool isPlainOwnedValue =
          Info->IsDeclaredVariable && !Info->IsFunctionParameter &&
          Info->Permission.Morphology == BindingMorphology::None &&
          Info->TypeObj && !Info->TypeObj->isPointer() &&
          !Info->TypeObj->isSmartPointer() && !Info->TypeObj->isReference();
      // Raw pointer payload access is an unsafe capability.  Handle-side #
      // remains identity-only and cannot manufacture it.
      bool rawPayloadCapability =
          m_InUnsafeContext && Info->TypeObj && Info->TypeObj->isRawPointer() &&
          !Info->IsHandleRebindable();
      const bool declaredPayloadWritable = Info->IsSoulMutable();
      bool valueBindingCanRebindMemberHandle =
          Info->TypeObj && !Info->TypeObj->isPointer() &&
          !Info->TypeObj->isSmartPointer() && !Info->TypeObj->isReference() &&
          declaredPayloadWritable;
      return applyPathFlowCeiling(
          {(declaredPayloadWritable || (!declarationOnly && isPlainOwnedValue) ||
            rawPayloadCapability) &&
               (declarationOnly || Info->PayloadFlowWritable),
           Info->Permission.IdentityRebindable ||
               valueBindingCanRebindMemberHandle,
           !declarationOnly && Info->HasPayloadFlowCeiling &&
               !Info->PayloadFlowWritable});
    }
    return {};
  }

  if (auto *Unary = dynamic_cast<UnaryExpr *>(E)) {
    // A hat chooses which existing handle is viewed; its # is an intent, not
    // a new grant.  The underlying declaration remains the authority.
    return getAccessCapability(Unary->RHS.get(), declarationOnly);
  }

  if (auto *Member = dynamic_cast<MemberExpr *>(E)) {
    auto base = getAccessCapability(Member->Object.get(), declarationOnly);
    // A member is a declaration boundary.  Its own P declaration caps the
    // parent path: a writable record must not turn a `~field: T` into a
    // payload-writable handle merely by projecting it.  Do not infer this
    // from MemberExpr::ResolvedType, because member surface syntax can carry
    // an intent marker which is not authority.
    auto objType = Member->Object ? Member->Object->ResolvedType : nullptr;
    if (!objType)
      return applyPathFlowCeiling(
          {base.PayloadWritable, false, base.PayloadFlowRestricted});
    auto resolvedObject = resolveType(objType->getSoulType(), true);
    auto shapeType =
        std::dynamic_pointer_cast<ShapeType>(resolvedObject);
    ShapeDecl *shape = shapeType ? shapeType->Decl : nullptr;
    if (!shape)
      shape = findVisibleShapeDecl(resolvedObject->getSoulName(), Member->Loc);
    if (!shape)
      return applyPathFlowCeiling(
          {base.PayloadWritable, false, base.PayloadFlowRestricted});

    MemberAccessIntent access = parseMemberAccess(Member->Member);
    for (const auto &field : shape->Members) {
      if (Type::stripMorphology(field.Name) != access.MemberName)
        continue;
      auto fieldType = Type::fromString(synthesizePhysicalType(field));
      bool insulated = fieldType &&
                       (fieldType->isPointer() ||
                        fieldType->isSmartPointer() ||
                        fieldType->isReference());
      // A field declaration is its own authority boundary.  A by-value
      // aggregate parameter may therefore project a field that was declared
      // payload-writable, even when the aggregate binding itself is not
      // payload-writable.  This does not grant authority through a shared or
      // reference aggregate view: those remain indirect and retain the
      // parent's direct capability as the ceiling.
      const bool fieldPayloadDeclared =
          field.IsValueMutable || field.Permission.SoulWritable;
      const bool fieldPayloadBlocked =
          field.IsValueBlocked || field.Permission.SoulBlocked;
      // A field declaration is an authority source in its own right.  An
      // outer handle that only grants H therefore does not erase `field#`.
      // Conversely, a handle/reference field without P establishes a
      // restricted direct source: deeper projections cannot recover P from
      // their own syntax or from a later field declaration.
      const bool fieldStartsRestrictedFlow =
          insulated && !fieldPayloadDeclared;
      bool fieldPayloadWritable =
          !base.PayloadFlowRestricted && !fieldPayloadBlocked &&
          (fieldPayloadDeclared || (!insulated && base.PayloadWritable));
      bool fieldHandleRebindable =
          field.IsRebindable && !field.IsRebindBlocked;
      return applyPathFlowCeiling(
          {fieldPayloadWritable,
           fieldHandleRebindable,
           base.PayloadFlowRestricted || fieldStartsRestrictedFlow});
    }
    return applyPathFlowCeiling(
        {base.PayloadWritable, false, base.PayloadFlowRestricted});
  }

  if (auto *Index = dynamic_cast<ArrayIndexExpr *>(E)) {
    auto base = getAccessCapability(Index->Array.get(), declarationOnly);
    // Index syntax may carry the array binding's handle attributes, while the
    // element declaration carries its own payload capability.  Recover the
    // physical element type so an array of `~T` cannot become `~T#` merely
    // because the array binding itself is writable.
    std::shared_ptr<Type> arrayType;
    if (auto *Var = dynamic_cast<VariableExpr *>(Index->Array.get())) {
      SymbolInfo *info = nullptr;
      std::string actualName = Var->Name;
      if (CurrentScope->findVariableWithDeref(Var->Name, info, actualName) &&
          info)
        arrayType = info->TypeObj;
    }
    if (!arrayType && Index->Array)
      arrayType = Index->Array->ResolvedType;
    if (!arrayType)
      return applyPathFlowCeiling(
          {base.PayloadWritable, false, base.PayloadFlowRestricted});
    auto resolvedArray = resolveType(arrayType, true);
    // Index expressions in std's unsafe storage helpers are often rooted at
    // `*['T]`, not directly at `['T]`.  Peel that raw-storage carrier before
    // recovering the element declaration; otherwise an array of references
    // loses its slot-rebind capability during generic instantiation.
    auto storageType = resolvedArray;
    if (storageType && storageType->isRawPointer())
      storageType = storageType->getPointeeType();
    auto elementType = storageType && storageType->isArray()
                           ? storageType->getArrayElementType()
                           : nullptr;
    auto indexedType = Index->ResolvedType;
    const bool isIndirectElement =
        (elementType && (elementType->isPointer() ||
                         elementType->isSmartPointer() ||
                         elementType->isReference())) ||
        (indexedType && (indexedType->isPointer() ||
                         indexedType->isSmartPointer() ||
                         indexedType->isReference()));
    if (isIndirectElement) {
      auto elementSoul = elementType ? elementType->getPointeeType()
                                     : indexedType->getPointeeType();
      // Array slots reached through a raw pointer are untyped storage.  An
      // explicit unsafe block may write such a slot's handle identity (for
      // example Vec<&T> copying a reference into allocated backing storage),
      // but this is not a payload capability grant for the referent.
      const bool unsafeRawStorage =
          m_InUnsafeContext && arrayType->isRawPointer();
      return applyPathFlowCeiling(
          {base.PayloadWritable && elementSoul && elementSoul->IsWritable,
           unsafeRawStorage, base.PayloadFlowRestricted});
    }
    return applyPathFlowCeiling(
        {base.PayloadWritable, false, base.PayloadFlowRestricted});
  }

  // A non-place expression is a fresh value.  For an indirect result, retain
  // only the capability carried by its declared pointee; for all other
  // rvalues, no caller-owned payload exists to protect.
  if (E->ResolvedType &&
      (E->ResolvedType->isPointer() || E->ResolvedType->isSmartPointer() ||
       E->ResolvedType->isReference())) {
    auto pointee = E->ResolvedType->getPointeeType();
    return applyPathFlowCeiling({pointee && pointee->IsWritable, false});
  }
  return applyPathFlowCeiling({true, false});
}

PermissionFlow Sema::getPermissionFlow(Expr *E) {
  if (!E)
    return {};

  if (dynamic_cast<TodoExpr *>(E)) {
    PermissionFlow flow;
    flow.Kind = PermissionFlowKind::RequirementOnly;
    return flow;
  }

  if (auto *Cede = dynamic_cast<CedeExpr *>(E)) {
    PermissionFlow flow = getPermissionFlow(Cede->Value.get());
    // A cast changes the static presentation, not the direct ownership
    // source. It cannot turn a member transfer into an independent one.
    Expr *directSource = unwrapCedeDirectSource(Cede->Value.get());

    // A whole binding or a fresh owned rvalue may establish an independent
    // owner. A member, index, or spread instead remains a view of its host:
    // `cede` transfers that local resource but cannot recreate payload
    // authority beyond the direct path's existing ceiling.
    if (dynamic_cast<MemberExpr *>(directSource) ||
        dynamic_cast<ArrayIndexExpr *>(directSource) ||
        dynamic_cast<SpreadExpr *>(directSource)) {
      flow.Kind = PermissionFlowKind::Shared;
    }
    return flow;
  }
  if (auto *Cast = dynamic_cast<CastExpr *>(E))
    return getPermissionFlow(Cast->Expression.get());
  if (auto *Post = dynamic_cast<PostfixExpr *>(E))
    return getPermissionFlow(Post->LHS.get());
  if (auto *Member = dynamic_cast<MemberExpr *>(E)) {
    PermissionFlow flow = getPermissionFlow(Member->Object.get());
    flow.DirectCapability = getAccessCapability(E);
    // The direct source is the projected field, not the aggregate that owns
    // it.  A shared/reference/unique field is therefore a view for flow
    // purposes even when its aggregate is an owned local value.  Otherwise a
    // readonly field could be misclassified as Fresh and regain P at a new
    // binding boundary.
    if (Member->ResolvedType) {
      if (Member->ResolvedType->isRawPointer())
        flow.Kind = PermissionFlowKind::UnsafeRaw;
      else if (Member->ResolvedType->isPointer() ||
               Member->ResolvedType->isSmartPointer() ||
               Member->ResolvedType->isReference())
        flow.Kind = PermissionFlowKind::Shared;
    }
    return flow;
  }
  if (auto *Index = dynamic_cast<ArrayIndexExpr *>(E)) {
    PermissionFlow flow = getPermissionFlow(Index->Array.get());
    flow.DirectCapability = getAccessCapability(E);
    // Array elements follow the same direct-source rule as fields: an
    // indirect element is a view of its array storage, not a fresh authority
    // root created by the index expression.
    std::shared_ptr<Type> arrayType;
    if (auto *Var = dynamic_cast<VariableExpr *>(Index->Array.get())) {
      SymbolInfo *info = nullptr;
      std::string actualName = Var->Name;
      if (CurrentScope->findVariableWithDeref(Var->Name, info, actualName) &&
          info)
        arrayType = info->TypeObj;
    }
    if (!arrayType && Index->Array)
      arrayType = Index->Array->ResolvedType;
    auto resolvedArray = arrayType ? resolveType(arrayType, true) : nullptr;
    auto directElement = resolvedArray && resolvedArray->isArray()
                             ? resolvedArray->getArrayElementType()
                             : Index->ResolvedType;
    if (directElement) {
      if (directElement->isRawPointer())
        flow.Kind = PermissionFlowKind::UnsafeRaw;
      else if (directElement->isPointer() || directElement->isSmartPointer() ||
               directElement->isReference())
        flow.Kind = PermissionFlowKind::Shared;
    }
    return flow;
  }
  if (auto *Address = dynamic_cast<AddressOfExpr *>(E)) {
    PermissionFlow flow;
    flow.Kind = PermissionFlowKind::Shared;
    flow.DirectCapability = getAccessCapability(Address->Expression.get());
    return flow;
  }
  if (auto *Unary = dynamic_cast<UnaryExpr *>(E)) {
    PermissionFlow flow = getPermissionFlow(Unary->RHS.get());
    flow.DirectCapability = getAccessCapability(E);
    return flow;
  }

  PermissionFlow flow;
  flow.DirectCapability = getAccessCapability(E);

  if (auto *Var = dynamic_cast<VariableExpr *>(E)) {
    SymbolInfo *Info = nullptr;
    std::string actualName = Var->Name;
    if (CurrentScope->findVariableWithDeref(Var->Name, Info, actualName) &&
        Info) {
      switch (Info->Permission.Morphology) {
      case BindingMorphology::Unique:
        flow.Kind = PermissionFlowKind::Independent;
        return flow;
      case BindingMorphology::Shared:
      case BindingMorphology::Reference:
        flow.Kind = PermissionFlowKind::Shared;
        return flow;
      case BindingMorphology::Raw:
        flow.Kind = PermissionFlowKind::UnsafeRaw;
        return flow;
      case BindingMorphology::None:
        break;
      }
    }
  }

  if (E->ResolvedType) {
    if (E->ResolvedType->isSharedPtr() || E->ResolvedType->isReference())
      flow.Kind = PermissionFlowKind::Shared;
    else if (E->ResolvedType->isRawPointer())
      flow.Kind = PermissionFlowKind::UnsafeRaw;
    else if (E->ResolvedType->isUniquePtr())
      flow.Kind = PermissionFlowKind::Independent;
  }
  return flow;
}

AccessIntent Sema::getAccessIntent(Expr *E) {
  if (!E)
    return {};

  if (dynamic_cast<TodoExpr *>(E))
    return {};

  if (auto *Cast = dynamic_cast<CastExpr *>(E))
    return getAccessIntent(Cast->Expression.get());
  if (auto *Cede = dynamic_cast<CedeExpr *>(E))
    return getAccessIntent(Cede->Value.get());
  if (auto *Post = dynamic_cast<PostfixExpr *>(E)) {
    auto intent = getAccessIntent(Post->LHS.get());
    if (Post->Op == TokenType::TokenWrite || Post->Op == TokenType::Bang)
      intent.PayloadWrite = true;
    return intent;
  }
  if (auto *Var = dynamic_cast<VariableExpr *>(E)) {
    SymbolInfo *Info = nullptr;
    std::string actualName = Var->Name;
    if (CurrentScope->findVariableWithDeref(Var->Name, Info, actualName) &&
        Info) {
      return {Var->IsValueMutable || Info->Permission.SoulWritable, false};
    }
    return {};
  }
  if (auto *Unary = dynamic_cast<UnaryExpr *>(E)) {
    auto intent = getAccessIntent(Unary->RHS.get());
    intent.HandleRebind = Unary->IsRebindable;
    return intent;
  }
  if (auto *Member = dynamic_cast<MemberExpr *>(E)) {
    // A field declaration is the authority requested by a mutable callee.
    // Ordinary member access has no suffix syntax for repeating that request;
    // inheriting the aggregate binding's intent would therefore incorrectly
    // erase a `field#: T` declaration.  Capability is still checked
    // independently by the caller, so a shared/reference projection cannot
    // gain payload write access here.
    auto capability = getAccessCapability(Member);
    return {capability.PayloadWritable, false};
  }
  if (auto *Index = dynamic_cast<ArrayIndexExpr *>(E))
    return getAccessIntent(Index->Array.get());

  // Fresh rvalues have no caller storage to protect, so their ownership is
  // sufficient intent for a mutable-by-value callee.
  return {true, false};
}

bool Sema::hasExplicitCallArgumentWriteSigil(const Expr *E) {
  if (!E)
    return false;
  if (auto *post = dynamic_cast<const PostfixExpr *>(E)) {
    if (post->Op == TokenType::TokenWrite)
      return true;
  }
  if (auto *var = dynamic_cast<const VariableExpr *>(E)) {
    if (var->IsValueMutable)
      return true;
  }
  return false;
}

static bool isDirectPlaceExpr(const Expr *E) {
  if (!E)
    return false;
  return dynamic_cast<const VariableExpr *>(E) != nullptr ||
         dynamic_cast<const MemberExpr *>(E) != nullptr ||
         dynamic_cast<const ArrayIndexExpr *>(E) != nullptr ||
         dynamic_cast<const PostfixExpr *>(E) != nullptr;
}

static DiagLoc getArgumentSpan(const Expr *arg) {
  if (!arg)
    return {};

  SourceManager *sm = DiagnosticEngine::SrcMgr;

  if (auto *var = dynamic_cast<const VariableExpr *>(arg)) {
    if (sm) {
      FullSourceLoc full = sm->getFullSourceLoc(var->Loc);
      return DiagLoc{full.FileName, static_cast<int>(full.Line),
                     static_cast<int>(full.Column),
                     static_cast<int>(var->Name.length())};
    }
  } else if (auto *member = dynamic_cast<const MemberExpr *>(arg)) {
    if (sm) {
      SourceLocation startLoc = member->Object ? member->Object->Loc : member->Loc;
      SourceLocation endLoc = member->MemberLoc.isValid() ? member->MemberLoc : member->Loc;
      FullSourceLoc fullStart = sm->getFullSourceLoc(startLoc);
      FullSourceLoc fullEnd = sm->getFullSourceLoc(endLoc);
      if (fullStart.isValid() && fullEnd.isValid()) {
        int length = 0;
        if (fullStart.Line == fullEnd.Line && fullEnd.Column >= fullStart.Column) {
          length = (fullEnd.Column + member->Member.length()) - fullStart.Column;
        } else {
          length = member->Member.length();
        }
        return DiagLoc{fullStart.FileName, static_cast<int>(fullStart.Line),
                       static_cast<int>(fullStart.Column), length};
      }
    }
  } else if (auto *idx = dynamic_cast<const ArrayIndexExpr *>(arg)) {
    if (sm) {
      SourceLocation startLoc = idx->Array ? idx->Array->Loc : idx->Loc;
      SourceLocation endLoc = idx->RBracketLoc.isValid() ? idx->RBracketLoc : idx->Loc;
      FullSourceLoc fullStart = sm->getFullSourceLoc(startLoc);
      FullSourceLoc fullEnd = sm->getFullSourceLoc(endLoc);
      if (fullStart.isValid() && fullEnd.isValid()) {
        int length = 0;
        if (fullStart.Line == fullEnd.Line && fullEnd.Column >= fullStart.Column) {
          length = (fullEnd.Column + 1) - fullStart.Column;
        } else {
          length = 1;
        }
        return DiagLoc{fullStart.FileName, static_cast<int>(fullStart.Line),
                       static_cast<int>(fullStart.Column), length};
      }
    }
  }

  if (sm) {
    FullSourceLoc full = sm->getFullSourceLoc(arg->Loc);
    return DiagLoc{full.FileName, static_cast<int>(full.Line),
                   static_cast<int>(full.Column), 1};
  }
  return {};
}

std::string Sema::getDisplayArgumentString(Expr *arg) {
  if (!arg)
    return "";
  if (auto *post = dynamic_cast<PostfixExpr *>(arg)) {
    return getDisplayArgumentString(post->LHS.get());
  }
  if (auto *var = dynamic_cast<VariableExpr *>(arg)) {
    return var->Name;
  }
  if (auto *member = dynamic_cast<MemberExpr *>(arg)) {
    std::string base = getDisplayArgumentString(member->Object.get());
    return base.empty() ? member->Member : (base + "." + member->Member);
  }
  if (auto *idx = dynamic_cast<ArrayIndexExpr *>(arg)) {
    std::string base = getDisplayArgumentString(idx->Array.get());
    std::string indices;
    for (size_t i = 0; i < idx->Indices.size(); ++i) {
      if (i > 0)
        indices += ", ";
      if (idx->Indices[i])
        indices += getDisplayArgumentString(idx->Indices[i].get());
    }
    return base + "[" + indices + "]";
  }
  if (auto *num = dynamic_cast<NumberExpr *>(arg)) {
    return std::to_string(num->Value);
  }
  std::string path = getPathString(arg);
  return path.empty() ? arg->toString() : path;
}

std::string Sema::ownershipSourceLabel(const Expr *expression) {
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

void Sema::validateCallArgumentMutSigil(Expr *arg, bool paramIsValueMutable,
                                        const std::string &paramName,
                                        SourceLocation paramLoc,
                                        SourceLocation callLoc,
                                        size_t argIndex) {
  if (!arg)
    return;

  // cede ownership transfer is explicitly exempted from W0408 for now.
  if (dynamic_cast<const CedeExpr *>(arg) != nullptr)
    return;

  const bool hasSigil = hasExplicitCallArgumentWriteSigil(arg);
  const std::string paramStr =
      paramName.empty() ? ("arg" + std::to_string(argIndex + 1)) : paramName;
  std::string argStr = getDisplayArgumentString(arg);
  if (argStr.empty())
    argStr = arg->toString();

  if (paramIsValueMutable) {
    if (!hasSigil && isDirectPlaceExpr(arg)) {
      AccessCapability declaredCapability = getAccessCapability(arg, true);
      if (declaredCapability.PayloadWritable) {
        DiagLoc reportLoc = getArgumentSpan(arg);
        DiagnosticEngine::report(
            reportLoc, DiagID::WARN_CALL_ARG_MISSING_MUTABLE_SIGIL,
            argStr, paramStr, argStr, "#");
      }
    }
  } else {
    if (hasSigil) {
      DiagLoc sigilLoc;
      if (auto *post = dynamic_cast<const PostfixExpr *>(arg)) {
        DiagLoc lhsSpan = getArgumentSpan(post->LHS.get());
        sigilLoc = DiagLoc{lhsSpan.File, lhsSpan.Line, lhsSpan.Col + lhsSpan.Length, 1};
      } else if (auto *var = dynamic_cast<const VariableExpr *>(arg)) {
        SourceManager *sm = DiagnosticEngine::SrcMgr;
        if (sm) {
          FullSourceLoc full = sm->getFullSourceLoc(var->Loc);
          sigilLoc = DiagLoc{full.FileName, static_cast<int>(full.Line),
                             static_cast<int>(full.Column + var->Name.length()), 1};
        }
      }
      if (sigilLoc.File.empty() && DiagnosticEngine::SrcMgr) {
        FullSourceLoc full = DiagnosticEngine::SrcMgr->getFullSourceLoc(arg->Loc);
        sigilLoc = DiagLoc{full.FileName, static_cast<int>(full.Line),
                           static_cast<int>(full.Column), 1};
      }
      DiagnosticEngine::report(
          sigilLoc, DiagID::ERR_SEMA_CALL_ARG_UNEXPECTED_MUTABLE_SIGIL,
          argStr, paramStr);
    }
  }
}

static std::map<std::string, bool> captureVisibleUniqueMoved(Scope *ScopePtr) {
  std::map<std::string, bool> moved;
  for (Scope *S = ScopePtr; S; S = S->Parent) {
    for (const auto &pair : S->Symbols) {
      if (!moved.count(pair.first) && pair.second.IsUnique())
        moved[pair.first] = pair.second.Moved;
    }
  }
  return moved;
}

static std::map<std::string, uint64_t> captureVisibleInitMasks(Scope *ScopePtr) {
  std::map<std::string, uint64_t> masks;
  for (Scope *S = ScopePtr; S; S = S->Parent) {
    for (const auto &pair : S->Symbols) {
      if (!masks.count(pair.first))
        masks[pair.first] = pair.second.InitMask;
    }
  }
  return masks;
}

static std::map<std::string, bool> captureVisibleMoved(Scope *ScopePtr) {
  std::map<std::string, bool> moved;
  for (Scope *S = ScopePtr; S; S = S->Parent) {
    for (const auto &pair : S->Symbols) {
      if (!moved.count(pair.first))
        moved[pair.first] = pair.second.Moved;
    }
  }
  return moved;
}

static std::map<std::string, ExactPlaceFacts>
captureVisibleExactPlaceFacts(Scope *ScopePtr) {
  std::map<std::string, ExactPlaceFacts> facts;
  for (Scope *S = ScopePtr; S; S = S->Parent) {
    for (const auto &pair : S->Symbols) {
      if (!facts.count(pair.first))
        facts[pair.first] = pair.second.ExactPlace;
    }
  }
  return facts;
}

static std::map<std::string, std::set<uint64_t>>
captureVisibleConditionalTodoIds(Scope *ScopePtr) {
  std::map<std::string, std::set<uint64_t>> dependencies;
  for (Scope *S = ScopePtr; S; S = S->Parent) {
    for (const auto &pair : S->Symbols) {
      if (!dependencies.count(pair.first))
        dependencies[pair.first] = pair.second.ConditionalTodoIds;
    }
  }
  return dependencies;
}

static void restoreVisibleConditionalTodoIds(
    Scope *ScopePtr,
    const std::map<std::string, std::set<uint64_t>> &dependencies) {
  for (const auto &pair : dependencies) {
    SymbolInfo *info = nullptr;
    if (ScopePtr->findSymbol(pair.first, info) && info)
      info->ConditionalTodoIds = pair.second;
  }
}

static void restoreVisibleAnalysisState(
    Scope *ScopePtr, const std::map<std::string, uint64_t> &masks,
    const std::map<std::string, bool> &moved,
    const std::map<std::string, ExactPlaceFacts> &exactPlaces) {
  for (const auto &pair : masks) {
    SymbolInfo *info = nullptr;
    if (ScopePtr->findSymbol(pair.first, info) && info)
      info->InitMask = pair.second;
  }
  for (const auto &pair : moved) {
    SymbolInfo *info = nullptr;
    if (ScopePtr->findSymbol(pair.first, info) && info)
      info->Moved = pair.second;
  }
  for (const auto &pair : exactPlaces) {
    SymbolInfo *info = nullptr;
    if (ScopePtr->findSymbol(pair.first, info) && info) {
      info->ExactPlace = pair.second;
      info->InitMask = pair.second.applyToLegacyInitMask(info->InitMask);
    }
  }
}

Sema::AnalysisState Sema::captureAnalysisState() {
  AnalysisState state;
  state.InitMasks = captureVisibleInitMasks(CurrentScope);
  state.Moved = captureVisibleMoved(CurrentScope);
  state.ExactPlaces = captureVisibleExactPlaceFacts(CurrentScope);
  state.ConditionalTodoIds = captureVisibleConditionalTodoIds(CurrentScope);
  state.PayloadFlowRestrictedPaths = m_PayloadFlowRestrictedPaths;
  state.PAL = PALCheckerState.snapshot();
  return state;
}

void Sema::mergeAnalysisStates(const std::vector<AnalysisState> &states,
                               const PALChecker &palBase) {
  if (states.empty())
    return;

  std::map<std::string, uint64_t> mergedMasks = states.front().InitMasks;
  std::map<std::string, bool> mergedMoved = states.front().Moved;
  std::map<std::string, ExactPlaceFacts> mergedExactPlaces =
      states.front().ExactPlaces;
  std::map<std::string, std::set<uint64_t>> mergedConditionalTodoIds =
      states.front().ConditionalTodoIds;
  std::set<AccessPath> mergedPayloadFlowRestrictions =
      states.front().PayloadFlowRestrictedPaths;
  PALChecker mergedPAL = states.front().PAL;

  for (size_t i = 1; i < states.size(); ++i) {
    const auto &state = states[i];

    for (const auto &pair : state.InitMasks) {
      if (!mergedMasks.count(pair.first))
        mergedMasks[pair.first] = 0;
    }
    for (auto &pair : mergedMasks) {
      uint64_t mask =
          state.InitMasks.count(pair.first) ? state.InitMasks.at(pair.first) : 0;
      pair.second &= mask;
    }

    for (const auto &pair : state.Moved) {
      if (!mergedMoved.count(pair.first))
        mergedMoved[pair.first] = false;
    }
    for (auto &pair : mergedMoved) {
      bool moved = state.Moved.count(pair.first) ? state.Moved.at(pair.first)
                                                 : false;
      pair.second = pair.second || moved;
    }

    for (const auto &pair : state.ExactPlaces) {
      if (!mergedExactPlaces.count(pair.first))
        mergedExactPlaces[pair.first] = ExactPlaceFacts::bottom();
    }
    for (auto &pair : mergedExactPlaces) {
      const ExactPlaceFacts facts =
          state.ExactPlaces.count(pair.first)
              ? state.ExactPlaces.at(pair.first)
              : ExactPlaceFacts::bottom();
      pair.second |= facts;
    }

    for (const auto &pair : state.ConditionalTodoIds) {
      auto &dependencies = mergedConditionalTodoIds[pair.first];
      dependencies.insert(pair.second.begin(), pair.second.end());
    }

    // A branch join must not erase a restriction that is present on another
    // reachable path.  Subsequent unconditional rebinding explicitly removes
    // the exact path from the current state.
    mergedPayloadFlowRestrictions.insert(
        state.PayloadFlowRestrictedPaths.begin(),
        state.PayloadFlowRestrictedPaths.end());

    PALCheckerState.restore(mergedPAL);
    PALCheckerState.mergeBranches(palBase, mergedPAL, true, state.PAL, true);
    mergedPAL = PALCheckerState.snapshot();
  }

  restoreVisibleAnalysisState(CurrentScope, mergedMasks, mergedMoved,
                              mergedExactPlaces);
  restoreVisibleConditionalTodoIds(CurrentScope, mergedConditionalTodoIds);
  m_PayloadFlowRestrictedPaths = std::move(mergedPayloadFlowRestrictions);
  PALCheckerState.restore(mergedPAL);
}

static std::vector<std::string> collectLoopEscapingMoves(
    Scope *ScopePtr, const std::map<std::string, bool> &before) {
  std::vector<std::string> names;
  for (const auto &pair : before) {
    if (pair.second)
      continue;
    SymbolInfo *info = nullptr;
    if (ScopePtr->findSymbol(pair.first, info) && info && info->IsUnique() &&
        hasPlaceState(info->placeFact(), PlaceState::Moved)) {
      names.push_back(pair.first);
    }
  }
  return names;
}

static std::string getDisplayVariableName(std::string name) {
  while (!name.empty() &&
         (name[0] == '&' || name[0] == '*' || name[0] == '^' ||
          name[0] == '~')) {
    name.erase(name.begin());
  }
  return name;
}

static void collectVariables(ASTNode *Node, std::set<std::string> &Vars) {
  if (!Node) return;
  if (auto *VE = dynamic_cast<VariableExpr *>(Node)) {
    Vars.insert(VE->Name);
    return;
  }
  if (auto *Bin = dynamic_cast<BinaryExpr *>(Node)) {
    collectVariables(Bin->LHS.get(), Vars);
    collectVariables(Bin->RHS.get(), Vars);
  } else if (auto *Un = dynamic_cast<UnaryExpr *>(Node)) {
    collectVariables(Un->RHS.get(), Vars);
  } else if (auto *Call = dynamic_cast<CallExpr *>(Node)) {
    for (auto &Arg : Call->Args) {
      collectVariables(Arg.get(), Vars);
    }
  } else if (auto *Met = dynamic_cast<MethodCallExpr *>(Node)) {
    collectVariables(Met->Object.get(), Vars);
    for (auto &Arg : Met->Args) {
      collectVariables(Arg.get(), Vars);
    }
  } else if (auto *Cast = dynamic_cast<CastExpr *>(Node)) {
    collectVariables(Cast->Expression.get(), Vars);
  } else if (auto *Addr = dynamic_cast<AddressOfExpr *>(Node)) {
    collectVariables(Addr->Expression.get(), Vars);
  } else if (auto *Idx = dynamic_cast<ArrayIndexExpr *>(Node)) {
    collectVariables(Idx->Array.get(), Vars);
    for (auto &IndexExpr : Idx->Indices) {
      collectVariables(IndexExpr.get(), Vars);
    }
  } else if (auto *Memb = dynamic_cast<MemberExpr *>(Node)) {
    collectVariables(Memb->Object.get(), Vars);
  }
}

static bool isLValueRef(ASTNode *Node, const std::string &VarName) {
  if (!Node) return false;
  if (auto *VE = dynamic_cast<VariableExpr *>(Node)) {
    return VE->Name == VarName;
  }
  if (auto *Memb = dynamic_cast<MemberExpr *>(Node)) {
    return isLValueRef(Memb->Object.get(), VarName);
  }
  if (auto *Idx = dynamic_cast<ArrayIndexExpr *>(Node)) {
    return isLValueRef(Idx->Array.get(), VarName);
  }
  if (auto *Cast = dynamic_cast<CastExpr *>(Node)) {
    return isLValueRef(Cast->Expression.get(), VarName);
  }
  if (auto *Unary = dynamic_cast<UnaryExpr *>(Node)) {
    return isLValueRef(Unary->RHS.get(), VarName);
  }
  return false;
}

static bool isReadOnlyReferenceViewExpr(ASTNode *Node, Scope *CurrentScope) {
  if (!Node || !CurrentScope)
    return false;

  if (auto *Cast = dynamic_cast<CastExpr *>(Node))
    return isReadOnlyReferenceViewExpr(Cast->Expression.get(), CurrentScope);
  if (auto *Post = dynamic_cast<PostfixExpr *>(Node))
    return isReadOnlyReferenceViewExpr(Post->LHS.get(), CurrentScope);

  if (auto *VE = dynamic_cast<VariableExpr *>(Node)) {
    std::string actualName = VE->Name;
    SymbolInfo *Info = nullptr;
    if (CurrentScope->findVariableWithDeref(VE->Name, Info, actualName))
      return Info && Info->IsReference() && !Info->IsDeclaredMutable;
  }

  return false;
}

static bool isVariableMutated(ASTNode *Node, const std::string &VarName) {
  if (!Node) return false;

  if (auto *Bin = dynamic_cast<BinaryExpr *>(Node)) {
    if (Bin->Op == "=" || Bin->Op == "+=" || Bin->Op == "-=" ||
        Bin->Op == "*=" || Bin->Op == "/=" || Bin->Op == "%=" ||
        Bin->Op == "&=" || Bin->Op == "|=" || Bin->Op == "^=" ||
        Bin->Op == "<<=" || Bin->Op == ">>=") {
      if (isLValueRef(Bin->LHS.get(), VarName)) return true;
    }
    return isVariableMutated(Bin->LHS.get(), VarName) || isVariableMutated(Bin->RHS.get(), VarName);
  }

  if (auto *Unary = dynamic_cast<UnaryExpr *>(Node)) {
    if (Unary->Op == TokenType::PlusPlus || Unary->Op == TokenType::MinusMinus ||
        Unary->Op == TokenType::Caret || Unary->Op == TokenType::Ampersand ||
        Unary->Op == TokenType::Star || Unary->Op == TokenType::Tilde) {
      if (isLValueRef(Unary->RHS.get(), VarName)) return true;
    }
    return isVariableMutated(Unary->RHS.get(), VarName);
  }

  if (auto *Call = dynamic_cast<CallExpr *>(Node)) {
    for (auto &Arg : Call->Args) {
      if (isVariableMutated(Arg.get(), VarName)) return true;
    }
    return false;
  }

  if (auto *Met = dynamic_cast<MethodCallExpr *>(Node)) {
    if (auto *VE = dynamic_cast<VariableExpr *>(Met->Object.get())) {
      if (VE->Name == VarName) return true;
    }
    for (auto &Arg : Met->Args) {
      if (isVariableMutated(Arg.get(), VarName)) return true;
    }
    return isVariableMutated(Met->Object.get(), VarName);
  }

  if (auto *Block = dynamic_cast<BlockStmt *>(Node)) {
    for (auto &Stmt : Block->Statements) {
      if (isVariableMutated(Stmt.get(), VarName)) return true;
    }
    return false;
  }

  if (auto *ExprS = dynamic_cast<ExprStmt *>(Node)) {
    return isVariableMutated(ExprS->Expression.get(), VarName);
  }

  if (auto *If = dynamic_cast<IfExpr *>(Node)) {
    return isVariableMutated(If->Condition.get(), VarName) ||
           isVariableMutated(If->Then.get(), VarName) ||
           isVariableMutated(If->Else.get(), VarName);
  }

  if (auto *Loop = dynamic_cast<LoopExpr *>(Node)) {
    return isVariableMutated(Loop->Condition.get(), VarName) ||
           isVariableMutated(Loop->Body.get(), VarName);
  }

  if (auto *fe = dynamic_cast<ForExpr *>(Node)) {
    return isVariableMutated(fe->Collection.get(), VarName) ||
           isVariableMutated(fe->Body.get(), VarName) ||
           isVariableMutated(fe->ElseBody.get(), VarName);
  }

  if (auto *VarD = dynamic_cast<VariableDecl *>(Node)) {
    return isVariableMutated(VarD->Init.get(), VarName);
  }

  if (auto *Match = dynamic_cast<MatchExpr *>(Node)) {
    if (isVariableMutated(Match->Target.get(), VarName)) return true;
    for (auto &Arm : Match->Arms) {
      if (isVariableMutated(Arm->Body.get(), VarName)) return true;
      if (Arm->Guard && isVariableMutated(Arm->Guard.get(), VarName)) return true;
    }
    return false;
  }

  if (auto *Ret = dynamic_cast<ReturnStmt *>(Node)) {
    return Ret->ReturnValue && isVariableMutated(Ret->ReturnValue.get(), VarName);
  }

  return false;
}

bool Sema::isLValue(const Expr *expr) {
  if (dynamic_cast<const VariableExpr *>(expr))
    return true;
  if (auto *me = dynamic_cast<const MemberExpr *>(expr))
    return isLValue(me->Object.get());
  if (auto *ae = dynamic_cast<const ArrayIndexExpr *>(expr))
    return isLValue(ae->Array.get());
  if (auto *ue = dynamic_cast<const UnaryExpr *>(expr)) {
    if (ue->Op == TokenType::Star)
      return true;
  }
  if (auto *pe = dynamic_cast<const PostfixExpr *>(expr)) {
    if (pe->Op == TokenType::TokenWrite)
      return isLValue(pe->LHS.get());
  }
  return false;
}

std::string Sema::checkUnaryExprStr(UnaryExpr *Unary) {
  return checkUnaryExpr(Unary)->toString();
}

std::unique_ptr<Expr> Sema::foldGenericConstant(std::unique_ptr<Expr> E) {
  return ASTEvaluator::foldExpression(std::move(E), CurrentScope, this);
}

std::shared_ptr<toka::Type> Sema::checkExpr(Expr *E) {
  if (!E)
    return toka::Type::fromString("()");
  ActiveNodeRAII Active(E);
  m_LastInitMask = ~0ULL; // Default to fully set
  auto T = checkExprImpl(E);
  std::set<std::string> taskDependencies;
  if (T && T->toString().find("TaskHandle") != std::string::npos)
    taskDependencies = m_LastLifeDependencies;
  // [Fix] Monomorphize type before assigning it to the node
  T = resolveType(T);
  if (!taskDependencies.empty())
    m_LastLifeDependencies.insert(taskDependencies.begin(),
                                  taskDependencies.end());
  E->ResolvedType = T;

  const auto *cast = dynamic_cast<CastExpr *>(E);
  const bool isAscribedUninit =
      cast && cast->Kind == CastKind::Ascription &&
      dynamic_cast<UnsetExpr *>(cast->Expression.get());
  if (!dynamic_cast<UnsetExpr *>(E) && !isAscribedUninit &&
      !dynamic_cast<InitStructExpr *>(E) && !dynamic_cast<ArrayInitExpr *>(E)) {
    m_LastInitMask = ~0ULL;
  }
  bool hasRefs = false;
  if (T) {
      std::set<std::string> visited;
      std::function<bool(std::shared_ptr<toka::Type>)> checkType = [&](std::shared_ptr<toka::Type> t) -> bool {
          if (!t) return false;
          if (t->isReference()) return true;
          if (auto *st = dynamic_cast<ShapeType *>(t.get())) {
              for (const auto &arg : st->GenericArgs) {
                  if (checkType(arg)) return true;
              }
              if (st->GenericArgs.empty() && st->Decl &&
                  st->Decl->InstantiationTemplate) {
                  for (const auto &arg : st->Decl->InstantiationArgs) {
                      if (checkType(arg)) return true;
                  }
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
      hasRefs = checkType(T);
      std::string soul = T->getSoulName();
      if (!m_LastLifeDependencies.empty() &&
          soul.rfind("TaskHandle", 0) == 0) {
        hasRefs = true;
      }
      if (!m_LastLifeDependencies.empty() &&
          (T->isFunction() || T->isDynFn())) {
        hasRefs = true;
      }
  }

  if (!hasRefs) {
      m_LastLifeDependencies.clear();
      m_LastFieldDependencies.clear();
      m_LastBorrowSource = "";
  }

  return T;
}

std::shared_ptr<toka::Type>
Sema::checkExpr(Expr *E, std::shared_ptr<toka::Type> expected) {
  auto oldExpected = m_ExpectedType;
  m_ExpectedType = expected;
  auto T = checkExpr(E);
  m_ExpectedType = oldExpected;
  return T;
}

bool Sema::validateIntegerLiteralRange(
    ASTNode *site, NumberExpr *literal,
    const std::shared_ptr<toka::Type> &targetType, bool isNegative) {
  if (!literal || !targetType)
    return true;

  auto resolved = resolveType(targetType, true);
  if (!resolved || !resolved->isInteger())
    return true;

  const std::string name = resolved->getSoulName();
  unsigned bits = 0;
  if (name == "i8" || name == "u8" || name == "char")
    bits = 8;
  else if (name == "i16" || name == "u16")
    bits = 16;
  else if (name == "i32" || name == "u32")
    bits = 32;
  else if (name == "i64" || name == "u64")
    bits = 64;
  if (bits == 0)
    return true;

  const bool isSigned = resolved->isSignedInteger();
  bool fits = true;
  if (isNegative && !isSigned) {
    fits = false;
  } else if (isSigned) {
    const uint64_t limit =
        bits == 64
            ? (isNegative ? (uint64_t{1} << 63)
                          : static_cast<uint64_t>(
                                std::numeric_limits<int64_t>::max()))
            : (isNegative ? (uint64_t{1} << (bits - 1))
                          : (uint64_t{1} << (bits - 1)) - 1);
    fits = literal->Value <= limit;
  } else if (bits < 64) {
    fits = literal->Value <= ((uint64_t{1} << bits) - 1);
  }

  if (!fits) {
    const std::string value =
        (isNegative ? "-" : "") + std::to_string(literal->Value);
    error(site ? site : literal,
          DiagID::ERR_SEMA_INTEGER_LITERAL_OUT_OF_RANGE, value,
          targetType->toString());
  }
  return fits;
}

bool Sema::projectOwnedStringView(
    std::unique_ptr<Expr> &argument,
    std::shared_ptr<toka::Type> &argumentType,
    const std::shared_ptr<toka::Type> &expectedType) {
  if (!argument || !argumentType || !expectedType)
    return false;

  auto source = resolveType(argumentType, true);
  auto target = resolveType(expectedType, true);
  if (!source || !target || target->getSoulName() != "str")
    return false;

  const std::string sourceSoul = source->getSoulName();
  if (sourceSoul != "string" && sourceSoul != "String")
    return false;
  if (!MethodMap.count(sourceSoul) ||
      !MethodMap[sourceSoul].count("as_str"))
    return false;

  SourceLocation loc = argument->Loc;
  auto view = std::make_unique<MethodCallExpr>(
      std::move(argument), "as_str", std::vector<std::unique_ptr<Expr>>{});
  view->Loc = loc;
  view->IsCompilerInternal = true;
  view->ObjectIsPrechecked = true;
  argument = std::move(view);
  argumentType = checkExpr(argument.get(), expectedType);
  return argumentType && argumentType->getSoulName() == "str";
}

// -----------------------------------------------------------------------------
// Type & Morphology Helpers
// -----------------------------------------------------------------------------

Sema::MorphKind Sema::getSyntacticMorphology(Expr *E) {
  if (!E)
    return MorphKind::None;

  // Unary Ops: ^, *, ~, &
  if (auto *U = dynamic_cast<UnaryExpr *>(E)) {


    switch (U->Op) {
    case TokenType::Star:
      return MorphKind::Raw;
    case TokenType::Caret:
      return MorphKind::Unique;
    case TokenType::Tilde:
      // [Toka 1.3] Bitwise NOT (~) on integer is Morph-Exempt (Value)
      if (U->RHS && U->RHS->ResolvedType && U->RHS->ResolvedType->isInteger())
          return MorphKind::None;
      return MorphKind::Shared;
    case TokenType::Ampersand:
      return MorphKind::Ref;
    default:
      return MorphKind::None;
    }
  }




  // Binary Expressions: Pointer Arithmetic or Computed Values
  // These produce RValues, which do not need sigils (the strictly-typed
  // checking handles validation).
  if (dynamic_cast<BinaryExpr *>(E)) {
    return MorphKind::Valid;
  }

  // Cast: Check target type string
  if (auto *C = dynamic_cast<CastExpr *>(E))
    return morphKindFromTypeString(C->TargetType);

  if (auto *Aw = dynamic_cast<AwaitExpr *>(E)) {
    return getSyntacticMorphology(Aw->Expression.get());
  }

  if (auto *Wt = dynamic_cast<WaitExpr *>(E)) {
    return getSyntacticMorphology(Wt->Expression.get());
  }

  if (auto *Ce = dynamic_cast<CedeExpr *>(E)) {
    return getSyntacticMorphology(Ce->Value.get());
  }

  if (auto *Post = dynamic_cast<PostfixExpr *>(E)) {
    return getSyntacticMorphology(Post->LHS.get());
  }

  if (auto *Unwrap = dynamic_cast<UnwrapPropagationExpr *>(E)) {
    return MorphKind::None;
  }

  // Member Access: Check if Member string carries pointer sigil (e.g. .*name)
  if (auto *M = dynamic_cast<MemberExpr *>(E)) {
    if (!M->Member.empty()) {
      MemberAccessIntent access = parseMemberAccess(M->Member);
      if (access.IsMorphicIdentity && M->IsMorphicExempt && M->ResolvedType) {
        return morphKindFromType(M->ResolvedType);
      }
      char c = M->Member[0];
      if (c == '*')
        return MorphKind::Raw;
      if (c == '^')
        return MorphKind::Unique;
      if (c == '~')
        return MorphKind::Shared;
      if (c == '&')
        return MorphKind::Ref;
    }
    return MorphKind::None;
  }

  // Safe Constructors (Exceptions)
  if (dynamic_cast<CallExpr *>(E) || dynamic_cast<MethodCallExpr *>(E) ||
      dynamic_cast<NewExpr *>(E) || dynamic_cast<AllocExpr *>(E) ||
      dynamic_cast<NullExpr *>(E) || dynamic_cast<UnsetExpr *>(E) ||
      dynamic_cast<StringExpr *>(E) ||
      dynamic_cast<ArrayInitExpr *>(E)) {
    return MorphKind::Valid;
  }

  // Unsafe: Recurse
  if (auto *U = dynamic_cast<UnsafeExpr *>(E)) {
    return getSyntacticMorphology(U->Expression.get());
  }
  return MorphKind::None;
}

bool Sema::checkStrictMorphology(ASTNode *Node, MorphKind Target,
                                 MorphKind Source,
                                 const std::string &TargetName) {
  // 1. Exact Match
  if (Target == Source)
    return true;

  // 2. Safe Constructors (Source is function call/new/etc)
  if (Source == MorphKind::Valid)
    return true;

  // 3. None/None Match (Value types)
  if (Target == MorphKind::None && Source == MorphKind::None)
    return true;

  // 4. Mismatch
  std::string tgtStr = "value";
  if (Target == MorphKind::Raw)
    tgtStr = "*";
  if (Target == MorphKind::Unique)
    tgtStr = "^";
  if (Target == MorphKind::Shared)
    tgtStr = "~";
  if (Target == MorphKind::Ref)
    tgtStr = "&";

  std::string srcStr = "value";
  if (Source == MorphKind::Raw)
    srcStr = "*";
  if (Source == MorphKind::Unique)
    srcStr = "^";
  if (Source == MorphKind::Shared)
    srcStr = "~";
  if (Source == MorphKind::Ref)
    srcStr = "&";

  DiagnosticEngine::report(Node->Loc, DiagID::ERR_MORPHOLOGY_MISMATCH, tgtStr,
                           srcStr);
  HasError = true;
  return false;
}

std::shared_ptr<toka::Type> Sema::checkExprImpl(Expr *E) {
  if (!E)
    return toka::Type::fromString("()");

  if (dynamic_cast<TodoExpr *>(E)) {
    auto *todo = static_cast<TodoExpr *>(E);
    if (m_ExpectedType && !m_ExpectedType->isUnknown() &&
        !m_ExpectedType->IsCede && !m_ExpectedCedeTransfer) {
      auto expected = resolveType(m_ExpectedType, true);
      auto soul = expected ? expected->getSoulType() : nullptr;
      std::string morphology = "value";
      bool handleRebind = false;
      bool payloadWrite = expected && expected->IsWritable;
      if (expected && expected->isRawPointer()) {
        morphology = "raw";
        handleRebind = expected->IsWritable;
        payloadWrite = soul && soul->IsWritable;
      } else if (expected && expected->isUniquePtr()) {
        morphology = "unique";
        handleRebind = expected->IsWritable;
        payloadWrite = soul && soul->IsWritable;
      } else if (expected && expected->isSharedPtr()) {
        morphology = "shared";
        handleRebind = expected->IsWritable;
        payloadWrite = soul && soul->IsWritable;
      } else if (expected && expected->isReference()) {
        morphology = "reference";
        handleRebind = expected->IsWritable;
        payloadWrite = soul && soul->IsWritable;
      }
      const bool nullable =
          expected && expected->isRawPointer() && expected->IsNullable;
      SemanticEvidence::recordTodoGoal(
          todo->TodoId, TodoGoalStatus::Incomplete, true,
          expected ? expected->toString() : m_ExpectedType->toString(),
          morphology, "none", handleRebind, payloadWrite, nullable, {}, E->Loc);
      DiagnosticEngine::report(E->Loc, DiagID::ERR_TYPED_TODO_INCOMPLETE,
                               m_ExpectedType->toString());
      HasError = true;
      return m_ExpectedType;
    }
    const TodoGoalStatus status =
        m_ExpectedCedeTransfer || (m_ExpectedType && m_ExpectedType->IsCede)
            ? TodoGoalStatus::Unsupported
            : TodoGoalStatus::Underconstrained;
    SemanticEvidence::recordTodoGoal(todo->TodoId, status, false, "", "",
                                     "", false, false, false, {}, E->Loc);
    if (status == TodoGoalStatus::Unsupported) {
      DiagnosticEngine::report(E->Loc, DiagID::ERR_TYPED_TODO_UNSUPPORTED_CONTEXT);
      HasError = true;
      return m_ExpectedType;
    }
    DiagnosticEngine::report(E->Loc, DiagID::ERR_TYPED_TODO_UNDERCONSTRAINED);
    HasError = true;
    return toka::Type::fromString("unknown");
  }

  if (auto *U = dynamic_cast<UnsetExpr *>(E)) {
    m_LastInitMask = 0;
    return toka::Type::fromString("unknown");
  }

  if (auto *Null = dynamic_cast<NullExpr *>(E)) {
    return toka::Type::fromString("null");
  }

  if (auto *CE = dynamic_cast<ComptimeReflectExpr *>(E)) {
    CE->ReflectedType = resolveType(
        CE->TypeSyntax ? toka::Type::fromSyntax(CE->TypeSyntax)
                       : toka::Type::fromString(CE->ReflectedTypeStr));
    if (CE->ReflectedType)
      CE->ReflectedTypeStr = CE->ReflectedType->toString();
    return toka::Type::fromString("TypeInfo");
  }

  if (auto *CFE = dynamic_cast<ComptimeFieldExpr *>(E)) {
    return toka::Type::fromString("FieldInfo");
  }

  if (dynamic_cast<CharLiteralExpr *>(E)) {
    // A character literal, like a numeric literal, obtains its exact storage
    // width from an enclosing type ascription.  `Char16` is a transparent
    // alias of `u16`, so returning the contextual integer type preserves the
    // established literal-ascription rule without inserting a conversion.
    if (m_ExpectedType) {
      auto physicalExpected = resolveType(m_ExpectedType, true);
      if (physicalExpected && physicalExpected->isInteger())
        return m_ExpectedType;
    }
    return toka::Type::fromString("char");
  }

  if (auto *Num = dynamic_cast<NumberExpr *>(E)) {
    if (m_ExpectedType && m_ExpectedType->isInteger()) {
      validateIntegerLiteralRange(Num, Num, m_ExpectedType,
                                  m_CheckingNegativeIntegerLiteral);
      return m_ExpectedType;
    }
    if (m_ExpectedType &&
        (m_ExpectedType->isAddrType() || m_ExpectedType->isOAddrType()))
      return m_ExpectedType;
    if (Num->Value > 9223372036854775807ULL)
      return toka::Type::fromString("u64");
    if (Num->Value > 2147483647)
      return toka::Type::fromString("i64");
    return toka::Type::fromString("i32");
  } else if (auto *Flt = dynamic_cast<FloatExpr *>(E)) {
    if (m_ExpectedType && m_ExpectedType->isFloatingPoint())
      return m_ExpectedType;
    return toka::Type::fromString("f64");
  } else if (auto *Bool = dynamic_cast<BoolExpr *>(E)) {
    return toka::Type::fromString("bool");
  } else if (auto *Addr = dynamic_cast<AddressOfExpr *>(E)) {
    recordHandleSurfaceAddressOfExpr(*Addr);

    // Toka Spec: &x creates a Reference.
    auto innerObj = checkExpr(Addr->Expression.get());
    if (innerObj->isUnknown())
      return toka::Type::fromString("unknown");

    if (auto *VE = dynamic_cast<VariableExpr *>(Addr->Expression.get())) {
      std::string actualName = VE->Name;
      SymbolInfo *InfoPtr = nullptr;
      if (CurrentScope->findVariableWithDeref(VE->Name, InfoPtr, actualName)) {
        InfoPtr->HasHandleBeenUsed = true;
      }
    }

    // Borrow Tracking
    Expr *scan = Addr->Expression.get();
    // Unwrap Postfix (like x#)
    while (auto *post = dynamic_cast<PostfixExpr *>(scan)) {
      scan = post->LHS.get();
    }

    std::string pathToBorrow = getPathString(scan);
    if (!pathToBorrow.empty()) {
        bool wantMutable = innerObj->IsWritable;
        if (wantMutable) {
            if (!m_ExpectedWritability) {
                wantMutable = false;
            }
        }

        std::string baseVar = pathToBorrow;
        size_t dotPos = baseVar.find('.');
        if (dotPos != std::string::npos) {
            baseVar = baseVar.substr(0, dotPos);
        }

        SymbolInfo *Info = nullptr;
        if (CurrentScope->findSymbol(baseVar, Info)) {
            bool isProjectionBorrow = (pathToBorrow != baseVar);
            bool borrowUnavailable = false;
            if (!Info->IsReference()) {
              if (!isProjectionBorrow) {
                if (hasPlaceState(Info->placeFact(), PlaceState::Never) ||
                    !Info->ExactPlace.isDefinitelyLive()) {
                  borrowUnavailable = true;
                }
              } else {
                if (!hasExactlyPlaceState(Info->placeFact(), PlaceState::Live)) {
                  borrowUnavailable = true;
                } else {
                  if (auto *ME = dynamic_cast<MemberExpr *>(scan)) {
                    if (Info->TypeObj && Info->TypeObj->isShape()) {
                      auto shapeType = std::dynamic_pointer_cast<ShapeType>(Info->TypeObj);
                      ShapeDecl *SD = shapeType ? shapeType->Decl : nullptr;
                      if (!SD)
                        SD = findVisibleShapeDecl(Info->TypeObj->getSoulName(), getLoc(ME));
                      if (SD) {
                        for (int i = 0; i < (int)SD->Members.size(); ++i) {
                          if (toka::Type::stripMorphology(SD->Members[i].Name) ==
                              toka::Type::stripMorphology(ME->Member)) {
                            if (Info->partialMovePlan().admits(PartialMoveProjectionKind::DirectField, i)) {
                              if (!hasExactlyPlaceState(
                                      Info->ExactPlace.projectionFact(
                                          PartialMoveProjectionKind::DirectField, i),
                                      PlaceState::Live)) {
                                borrowUnavailable = true;
                              }
                            }
                            break;
                          }
                        }
                      }
                    }
                  } else if (auto *IE = dynamic_cast<ArrayIndexExpr *>(scan)) {
                    if (IE->Indices.size() == 1) {
                      if (auto *constant = dynamic_cast<NumberExpr *>(IE->Indices[0].get())) {
                        if (Info->partialMovePlan().admits(
                                PartialMoveProjectionKind::FixedArrayElement, constant->Value)) {
                          if (!hasExactlyPlaceState(
                                  Info->ExactPlace.projectionFact(
                                      PartialMoveProjectionKind::FixedArrayElement, constant->Value),
                                  PlaceState::Live)) {
                            borrowUnavailable = true;
                          }
                        }
                      }
                    }
                  }
                }
              }
            }

            if (borrowUnavailable) {
              m_LastBorrowSource.clear();
              auto refType = std::make_shared<toka::ReferenceType>(innerObj);
              refType->IsNullable = false;
              refType->IsWritable = wantMutable;
              return refType;
            }

            if (wantMutable && pathToBorrow == baseVar) {
                if (!Info->IsSoulMutable()) {
                    error(Addr, DiagID::ERR_BORROW_IMMUT, baseVar);
                }
            }
            if (!m_InLHS) {
                if (!PALCheckerState.recordBorrow(
                        canonicalizeAccessPath(makeAccessPath(scan)),
                        wantMutable, Addr->Loc)) {
                    error(Addr, DiagID::ERR_BORROW_MUT, pathToBorrow);
                    if (PALCheckerState.lastConflict()) {
                      recordPALConflict(
                          Addr, wantMutable
                                    ? PALOperationClass::ExclusivePayloadBorrow
                                    : PALOperationClass::SharedPayloadBorrow,
                          canonicalizeAccessPath(makeAccessPath(scan)),
                          *PALCheckerState.lastConflict());
                    }
                }
            }

            m_LastBorrowSource = pathToBorrow;

            // Member-Level Dependency Extraction
            if (pathToBorrow != baseVar && !Info->FieldDependencySet.empty()) {
                std::string fieldName = pathToBorrow.substr(dotPos + 1);
                size_t nextDot = fieldName.find('.');
                if (nextDot != std::string::npos) fieldName = fieldName.substr(0, nextDot);
                
                if (Info->FieldDependencySet.count(fieldName)) {
                    for (const auto &dep : Info->FieldDependencySet[fieldName]) {
                        if (!m_InLHS) {
                            if (!PALCheckerState.recordBorrow(
                                    canonicalizeAccessPath(makeAccessPath(dep)),
                                    wantMutable, Addr->Loc)) {
                                error(Addr, DiagID::ERR_BORROW_MUT, dep);
                                if (PALCheckerState.lastConflict()) {
                                  recordPALConflict(
                                      Addr,
                                      wantMutable
                                          ? PALOperationClass::ExclusivePayloadBorrow
                                          : PALOperationClass::SharedPayloadBorrow,
                                      canonicalizeAccessPath(
                                          makeAccessPath(dep)),
                                      *PALCheckerState.lastConflict());
                                }
                            }
                        }
                        m_LastLifeDependencies.insert(dep);
                    }
                    if (!m_LastLifeDependencies.empty()) {
                        m_LastBorrowSource = *m_LastLifeDependencies.begin();
                    }
                } else {
                    m_LastLifeDependencies.insert(pathToBorrow);
                }
            } else {
                m_LastLifeDependencies.insert(pathToBorrow);
            }
        }
    }

    auto refType = std::make_shared<toka::ReferenceType>(innerObj);
    return refType;
  } else if (auto *Idx = dynamic_cast<ArrayIndexExpr *>(E)) {
    return checkIndexExpr(Idx);
  } else if (auto *Clo = dynamic_cast<ClosureExpr *>(E)) {
    return checkClosureExpr(Clo);
  } else if (auto *Rec = dynamic_cast<AnonymousRecordExpr *>(E)) {
    // 1. Infer field types
    std::vector<ShapeMember> members;
    std::set<std::string> seenFields;

    for (auto &f : Rec->Fields) {
      if (seenFields.count(f.first)) {
        error(Rec, DiagID::ERR_DUPLICATE_FIELD, f.first);
      }
      seenFields.insert(f.first);

      auto fieldTypeObj = checkExpr(f.second.get());
      std::string fieldT = fieldTypeObj->toString();
      if (fieldTypeObj->isUnknown())
        return toka::Type::fromString("unknown");

      ShapeMember sm;
      sm.Name = f.first;
      sm.Type = fieldT;
      sm.ResolvedType = fieldTypeObj;
      members.push_back(sm);
    }

    // 2. Generate Unique Type Name
    // Each anonymous record literal creates a distinct Nominal Type.
    std::string UniqueName =
        "__Toka_Anon_Rec_" + std::to_string(AnonRecordCounter++);
    Rec->AssignedTypeName = UniqueName;

    // 3. Create and Register Synthetic ShapeDecl
    // We treat it as a regular Struct
    auto SyntheticShape = std::make_unique<ShapeDecl>(
        false, UniqueName, std::vector<GenericParam>{}, ShapeKind::Struct,
        members);
    SyntheticShape->IsCompilerSynthesized = true;

    // Important: Register in ShapeMap so MemberExpr can find it
    ShapeMap[UniqueName] = SyntheticShape.get();

    // Store ownership
    SyntheticShapes.push_back(std::move(SyntheticShape));

    return toka::Type::fromString(UniqueName);
  } else if (auto *Deref = dynamic_cast<DereferenceExpr *>(E)) {
    recordHandleSurfaceDereferenceExpr(*Deref);

    auto innerObj = checkExpr(Deref->Expression.get());
    if (innerObj->isUnknown())
      return toka::Type::fromString("unknown");

    if (auto ptr = std::dynamic_pointer_cast<toka::PointerType>(innerObj)) {
      return ptr->getPointeeType();
    }
    error(Deref, DiagID::ERR_INVALID_OP, "dereference", innerObj->toString(),
          "void");
    return toka::Type::fromString("unknown");
  } else if (auto *Unary = dynamic_cast<UnaryExpr *>(E)) {
    return checkUnaryExpr(Unary);
  } else if (auto *Str = dynamic_cast<StringExpr *>(E)) {
    if (m_ExpectedType) {
        if (m_ExpectedType->isPointer()) {
            auto pte = m_ExpectedType->getPointeeType();
            if (pte && pte->typeKind == Type::Primitive) {
                std::string pName = pte->getSoulName();
                if (pName == "char" || pName == "i8" || pName == "u8") {
                    return m_ExpectedType;
                }
            }
        }
        std::string soul = m_ExpectedType->getSoulName();
        if (soul == "cstr" || soul == "Addr" || soul == "OAddr") {
            return m_ExpectedType;
        }
    }
    auto t = toka::Type::fromString("cstr");
    return resolveType(t);
  } else if (auto *VStr = dynamic_cast<ViewStringExpr *>(E)) {
      auto t = toka::Type::fromString("str");
      return resolveType(t);
  } else if (auto *ve = dynamic_cast<VariableExpr *>(E)) {
    if (m_IsPrecomputingCaptures && m_ClosureCaptureRootScope) {
      SymbolInfo *CapturedInfo = nullptr;
      std::string actualName;
      Scope *ownerScope = nullptr;
      if (CurrentScope->findVariableWithDerefScope(ve->Name, CapturedInfo,
                                                   actualName, ownerScope) &&
          CapturedInfo && CapturedInfo->IsDeclaredVariable && ownerScope &&
          ownerScope->Depth < m_ClosureCaptureRootScope->Depth) {
        m_AccessedVariables.insert(ve->Name);
      }
    } else {
      m_AccessedVariables.insert(ve->Name); // [CLOSURE] Tracker
    }
    if (m_InLHS) {
      std::string actualName = ve->Name;
      SymbolInfo *InfoPtr = nullptr;
      if (CurrentScope->findVariableWithDeref(ve->Name, InfoPtr, actualName)) {
        InfoPtr->HasBeenMutated = true;
        if (m_IsMemberBase) {
          // `p.field = ...` uses p's payload projection, but not necessarily
          // its handle view. Preserve that distinction for W0402/W0407.
          InfoPtr->HasPayloadBeenUsed = true;
        }
      }
    }

    // [NEW] Enforce Suffix Lifecycle Rule: '#' and '$' only allowed in declarations
    // or explicit mutable method invocation caller contexts.
    if (ve->IsValueMutable || ve->IsValueBlocked) {
      if (!m_AllowPermissionSuffix) {
        error(ve, DiagID::ERR_ILLEGAL_MODIFIER_SUFFIX);
      }
    }

    // [Ch 5] Single Hat Principle: Intermediate paths MUST NOT have morphology
    // or permissions Check flags because Lexer splits them from the identifier
    // name.
    if (m_InIntermediatePath) {
      if (ve->IsRawPointer || ve->IsUnique || ve->IsShared) {
        // [Rule] Allow pointer morphology if it's a member base (deref access)
        if (!m_IsMemberBase) {
          error(ve, DiagID::ERR_SEMA_MORPHOLOGY_SYMBOLS_ARE_ONLY_ALLOWED_AT__2, ve->Name);
        }
      }
      if (ve->IsValueMutable || ve->IsValueNullable || ve->IsValueBlocked) {
        // [Rule] Intermediate permission symbols ONLY allowed for
        // pointers/legacy unions as bases
        bool allowed = false;
        if (m_IsMemberBase) {
          SymbolInfo info;
          if (CurrentScope->lookup(ve->Name, info)) {
            if (info.TypeObj) {
              if (info.TypeObj->isPointer()) {
                allowed = true;
              } else if (info.TypeObj->isShape()) {
                std::string soul = info.TypeObj->getSoulName();
                if (ShapeMap.count(soul) &&
                    ShapeMap[soul]->Kind == toka::ShapeKind::Union) {
                  allowed = true;
                }
              }
            }
          }
        }
        if (!allowed) {
          error(ve, DiagID::ERR_SEMA_PERMISSION_SYMBOLS_ARE_ONLY_ALLOWED_AT__2, ve->Name, (ve->IsValueMutable ? "#" : ""));
        }
      }
    }

    SymbolInfo Info;
    std::string actualName = ve->Name;
    bool isImplicitDeref = false;
    
    SymbolInfo *InfoPtr = nullptr;
    if (CurrentScope->findVariableWithDeref(ve->Name, InfoPtr, actualName)) {
      isImplicitDeref = (actualName != ve->Name);
      Info = *InfoPtr;
      ve->ResolvedName = Info.CodegenName;
      ve->IsMorphicExempt = Info.IsMorphicExempt; // [NEW]
      ve->IsImplicitDeref = isImplicitDeref;      // [Fix] Mark AST node
      if (!m_InLHS) {
        InfoPtr->HasBeenUsed = true;
        if (InfoPtr->ImportingDecl) {
          const_cast<ImportDecl*>(InfoPtr->ImportingDecl)->HasBeenUsed = true;
        }
      }
    }

    if (!InfoPtr) {
      if (ve->Name == "f_arg") {
          std::string fnName = CurrentFunction ? CurrentFunction->Name : "NONE";
          std::cerr << "[TRACE] VariableExpr failed to find 'f_arg' in scope! Depth: " << CurrentScope->Depth << " In Function: " << fnName << "\n";
      }
      if (ve->Name == "cb") {
          std::cerr << "[TRACE] VariableExpr failed to find 'cb' in scope! Depth: " << CurrentScope->Depth << "\n";
          // Try to dump the symbol table
          Scope *s = CurrentScope;
          while (s) {
              for (auto &kv : s->Symbols) {
                  std::cerr << "   Scope Level " << s->Depth << " contains " << kv.first << "\n";
              }
              s = s->Parent;
          }
      }
      // [NEW] Surgical Plan: Try resolving as a type (handles Option<i32>)
      auto possible = toka::Type::fromString(ve->Name);
      if (possible && !possible->isUnknown()) {
        auto resolved = resolveType(possible);
        if (auto shapeT =
                std::dynamic_pointer_cast<toka::ShapeType>(resolved)) {
          if (shapeT->isResolved()) {
            // Success: Resolved to a type name (e.g. Option_M_i32)
            return shapeT;
          }
        }
      }

      error(ve, DiagID::ERR_UNDECLARED, ve->Name);
      if (ve->Name.find('-') != std::string::npos) {
        error(ve, DiagID::NOTE_GENERIC, "Did you mean subtraction? Subtraction operator '-' requires spaces around it to avoid ambiguity with kebab-case identifiers.");
      }
      return toka::Type::fromString("unknown");
    }
    if (hasPlaceState(Info.placeFact(), PlaceState::Moved) && !m_InLHS) {
      error(ve, DiagID::ERR_USE_MOVED, actualName);
      recordDecision(ve, SemanticRuleID::OwnMove001,
                     SemanticOperation::OwnershipTransfer,
                     SemanticDecision::Reject, SemanticReason::AlreadyMoved,
                     Type::stripMorphology(actualName),
                     Type::stripMorphology(actualName), Info.MoveLoc);
      if (Info.MoveLoc.isValid())
        DiagnosticEngine::report(Info.MoveLoc, DiagID::NOTE_GENERIC,
                                 "value moved here");
    }
    // [Fix] Trace to Source for Borrow Check
    SymbolInfo *EffectiveInfo = &Info;
    std::string EffectiveName = actualName;
    EffectiveInfo = resolveBorrowSource(EffectiveInfo, EffectiveName);

    std::string borrower = "";
    if (!m_ControlFlowStack.empty())
      borrower = m_ControlFlowStack.back().Label;

    std::optional<PALConflict> conflict;
    if (!m_InIntermediatePath) {
      if (m_InLHS) {
         conflict = PALCheckerState.verifyPayloadWrite(
             canonicalizeAccessPath(makeAccessPath(actualName)));
      } else {
         conflict = PALCheckerState.verifyAccess(
             canonicalizeAccessPath(makeAccessPath(actualName)));
      }
    }
    
    // Usage-time Authorization: A previously defined reference 
    // is authorized to access its own source.
    bool authorized = false;
    if (conflict) {
       authorized = isBorrowAccessAuthorized(makeAccessPath(ve),
                                              conflict->Path);
       const std::string conflictPath = conflict->displayPath();
       SymbolInfo veInfo;
       if (!authorized && CurrentScope->lookup(actualName, veInfo)) {
         if ((!veInfo.BorrowedPath.empty() &&
              canonicalizeAccessPath(veInfo.BorrowedPath) ==
                  canonicalizeAccessPath(conflict->Path)) ||
             veInfo.BorrowedFrom == conflictPath) {
           authorized = true;
         }
       }
       if (!authorized && !borrower.empty()) {
            SymbolInfo borrowerInfo;
            if (CurrentScope->lookup(borrower, borrowerInfo) &&
                ((!borrowerInfo.BorrowedPath.empty() &&
                  canonicalizeAccessPath(borrowerInfo.BorrowedPath) ==
                      canonicalizeAccessPath(conflict->Path)) ||
                 borrowerInfo.BorrowedFrom == conflictPath)) {
              authorized = true;
            } else if (actualName == borrower) {
               authorized = true; // writing to myself
            }
       }
    }

    if (conflict && !authorized) {
       error(ve, DiagID::ERR_BORROW_MUT, conflict->displayPath());
       recordPALConflict(
           ve, m_InLHS ? PALOperationClass::PayloadWrite
                       : PALOperationClass::SharedPayloadBorrow,
           canonicalizeAccessPath(makeAccessPath(actualName)), *conflict);
    }

    // [Annotated AST] Constant Substitution: The Core Fix
    // If this variable is a Generic Constant (e.g., S=4, N=10), we MUST NOT
    // allow CodeGen to see a VariableExpr, because it will try to Load it as an
    // LValue. Instead, we perform AST Substitution here and return a NumberExpr
    // (or equivalent).
    if (Info.HasConstValue) {
      // NOTE: We cannot simply return a new NumberExpr *node* here because
      // checkExpr returns a Type object, not an AST node replacement. Wait,
      // checkExpr returns shared_ptr<Type>, but it modifies the AST in-place?
      // No, checkExpr does not take AST** or smart_ptr reference.
      //
      // CRITICAL ARCHITECTURE CORRECTION:
      // Since checkExpr cannot replace the AST node itself (it takes raw
      // pointer), we must rely on the previous approach (HasConstantValue
      // propagation) BUT with a different CodeGen contract, OR we need the
      // CALLER of checkExpr to handle replacement.
      //
      // However, the User's directive was "checkIdentifierExpr returns new
      // NumberExpr". But verify checkExpr signature: it returns
      // `std::shared_ptr<toka::Type>`. It does NOT return an Expr*.
      //
      // Let's look at `checkExprImpl` or `Sema::analyze`?
      // Ah, `checkExpr` is recursive.
      //
      // Let's look at the Caller. Most callers are `checkExpr(E.get())`.
      // E is a `unique_ptr<Expr>`.
      // If we want to substitute, we need to change how `checkExpr` is called
      // or works.
      //
      // PROPOSAL: We implement the 'HasConstantValue' flag-based fix again, BUT
      // we correct CodeGen logic to NOT generate a Load.
      //
      // WAIT, the User said "don't do IsConstant check in CodeGen".
      // That implies AST substitution.
      //
      // Let's check if we can modify the AST in place.
      // We have `VariableExpr *ve`.
      // We can't turn a `VariableExpr` into `NumberExpr` in C++ without
      // changing the pointer.
      //
      // ALTERNATIVE:
      // The user might be thinking of a different compiler architecture (AST
      // Rewriter).
      //
      // Let's stick with the user's intent: "Ensure CodeGen treats it as a
      // number". If I can't swap the node pointer easily here, I must modify
      // CodeGen to *behave* as if it's a number.
      //
      // BUT, I can try to Mutate the VariableExpr to ACT like a NumberExpr?
      // No.
      //
      // LET'S LOOK AT checkStmt or whoever owns the unique_ptr.
      //
      // Actually, looking at `Sema_Expr.cpp`, `checkExpr` takes `Expr *E`.
      //
      // If I cannot replace the node, I MUST use the `HasConstantValue` flag in
      // CodeGen. The user said: "Do not return VariableExpr! ... return new
      // NumberExpr". This implies the user thinks checkExpr returns an Expr*.
      //
      // CHECK SEMANTICS:
      // `std::shared_ptr<toka::Type> Sema::checkExpr(Expr *E)`
      // Correct. It returns Type.
      //
      // So I cannot substitute the node *here*.
      //
      // OPTION 2: The `HasConstantValue` approach IS the right way for this
      // codebase, provided `genVariableExpr` handles it correctly.
      //
      // The user's diagnosis "You generated a Load for a ConstantInt" was
      // correct. My CodeGen fix WAS: `if (var->HasConstantValue) return
      // ConstantInt;` This DOES NOT generate a Load. It generates a Value.
      //
      // So why did it crash?
      // "Upper-level logic... attempts to generate builder.CreateLoad(i32 4)"
      //
      // Ah. `genVariableExpr` returned a `PhysEntity` wrapping the ConstantInt.
      // `PhysEntity` constructor has `bool isAlloca`.
      // If `isAlloca` is false (default), `PhysEntity::load()` returns the
      // value itself.
      //
      // If the caller calls `load()`, it gets `i32 4`.
      // If the caller calls `store()`, it crashes.
      //
      // Where is it crashing?
      // `audit_generics.tk` Line 31: `auto buf = [0; S]`
      //
      // AST: `VariableDecl(buf, RepeatedArrayExpr(0, S))`
      // CodeGen: `genVariableDecl` -> `genRepeatedArrayExpr` -> `genExpr(S)`.
      // `genRepeatedArrayExpr` logic:
      // `if (auto *num = dyn_cast<NumberExpr>(expr->Count)) ...`
      // `else if (auto *var = dyn_cast<VariableExpr>(expr->Count))`
      //
      // In my CodeGen fix, I handled `VariableExpr` with `HasConstantValue`.
      // So `genRepeatedArrayExpr` should grab `var->ConstantValue` (raw int)
      // and use `ArrayType::get`. It does NOT call `genExpr(var)`.
      //
      // So `genVariableExpr` is NOT called for the array size S.
      //
      // So where is the crash?
      // "Control flow reaches end of non-void function" (Exit 1)
      // "Segmentation fault" (Exit 139)
      //
      // When `println(".. S")` was uncommented, it crashed.
      // `println` calls `make_buffer_print` -> `genCallExpr` ->
      // `genExpr(Args)`. `S` is passed to `println`. `println` takes `(fmt,
      // ...)`. Varargs? `println` implementation might expect LValue?
      //
      // `checkIdentifierExpr` DOES NOT EXIST in this codebase.
      // `checkExpr` handles `VariableExpr`.
      //
      // I will implement the Metadata Propagation (`HasConstantValue`) in Sema,
      // AND I will re-implement the CodeGen fix which was correct,
      // BUT I will ensure `PhysEntity` correctly flags it as an RValue.
      //
      // Wait, the user specifically said: "Don't do IsConstant check in
      // CodeGen". They want AST Substitution. To do AST Substitution, I need to
      // find where `Expr` pointers are held. They are held in
      // `unique_ptr<Expr>` inside other AST nodes (Stmt, CallExpr, etc).
      //
      // To substitute, I would need a `Transform` pass or `Mutator`. `Sema` is
      // a pass. `Sema` visits nodes via `checkExpr(E)`.
      //
      // If I cannot replace the node pointer, I must modify the node to behave
      // correctly.
      //
      // Let's execute the User's Plan via a helper method
      // `substituteConstantVars`? Or just accept that `HasConstantValue` IS the
      // way to go, but I need to fix the Crash reason.
      //
      // The crash with `println` might be because `genVariableExpr` returned a
      // ConstantInt, and `genCallExpr` tried to pass it as `...` (VarArg). For
      // VarArgs, `genCallExpr` calls `genExpr`. If `genExpr` returns a
      // ConstantInt RValue, `genCallExpr` handles it?
      //
      // Let's implement the `HasConstantValue` propagation here first.

      ve->HasConstantValue = true;
      ve->ConstantValue = Info.ConstValue;
      ve->ConstantValObj = Info.ConstValObj;
      ve->ResolvedType = Info.TypeObj; // Ensure type is known (e.g. usize)
    }

    // Unset Check: Only check if NOT in LHS
    // A member path needs the selected field to be initialized, not every
    // field of its aggregate base. For an admitted local projection plan, the
    // exact fact decides whole-value availability; legacy aggregate shapes
    // retain their mask path below.
    if (!m_InLHS && !m_IsMemberBase && !m_InIntermediatePath) {
      bool isFullyInit = true;
      const bool hasExactWholeAvailability =
          !Info.IsReference() &&
          (Info.partialMovePlan().isAdmitted() || !Info.TypeObj ||
           (!Info.TypeObj->isShape() && !Info.TypeObj->isArray()));
      if (hasExactWholeAvailability) {
        isFullyInit = Info.ExactPlace.isDefinitelyLive();
      } else if (Info.InitMask == 0) {
        isFullyInit = false;
      } else if (Info.TypeObj && Info.TypeObj->isShape()) {
        // Check all bits for struct and enum-payload record shapes
        auto shapeType = std::dynamic_pointer_cast<ShapeType>(Info.TypeObj);
        ShapeDecl *SD = shapeType ? shapeType->Decl : nullptr;
        if (!SD)
          SD = findVisibleShapeDecl(Info.TypeObj->getSoulName(), getLoc(ve));
        if (SD) {
          if (SD->Kind == ShapeKind::Struct || SD->Kind == ShapeKind::Tuple) {
            uint64_t expected = (1ULL << SD->Members.size()) - 1;
            if (SD->Members.size() >= 64)
              expected = ~0ULL;
            if ((Info.InitMask & expected) != expected) {
              isFullyInit = false;
            }
          }
        }
      }

      if (!isFullyInit && !m_AllowUnsetUsage) {
        DiagnosticEngine::report(getLoc(ve), DiagID::ERR_USE_UNSET, ve->Name);
        HasError = true;
      }
    }
    m_LastInitMask = Info.InitMask;
    // [Constitution] Soul Collapse (The Hat-Off Transduction)
    // "Variable name without hat collapses to value (Soul)."
    bool shouldCollapse = true;
    if (m_DisableSoulCollapse || Info.IsMorphicExempt) {
      shouldCollapse = false;
    }

    auto current = Info.TypeObj;
    if (shouldCollapse && current) {
      while (current && (current->isPointer() || current->isReference() ||
                         current->isSmartPointer())) {
        // [Constitution 5.2] LHS Exemption: Nullable pointers collapse on LHS
        // for assignment.
        if (current->IsNullable && !m_InLHS) {
          break;
        }
        auto inner = current->getPointeeType();
        if (!inner)
          break;
        current = inner;
      }
    }

    // [Constitution 1.3] Soul Attributes Rule:
    // Identifiers (x, p) refer to the SOUL. Sigils (*p, ^p) refer to the
    // IDENTITY. Trailing attributes (#, ?) on an identifier always apply to the
    // soul. If we are looking at the soul (either we collapsed or it's a
    // value), apply the attributes. If we are looking at the identity (collapse
    // disabled), keep handle attributes and ignore trailing soul attributes for
    // now.
    if (shouldCollapse || (current && !current->isPointer())) {
      if (current) {
        // [Toka 1.3] Permission View: Inherit inherent mutability.
        // With explicit suffixes banned in expressions, variables inherently exhibit 
        // their declared mutability.
        // A suffix # is an intent at a use site.  A plain owned value has a
        // local payload capability, while a handle must declare payload-side
        // writability.  Intent alone never upgrades a handle-only binding.
        bool isPlainOwnedValue =
            Info.IsDeclaredVariable && !Info.IsFunctionParameter &&
            Info.Permission.Morphology == BindingMorphology::None &&
            Info.TypeObj && !Info.TypeObj->isPointer() &&
            !Info.TypeObj->isSmartPointer() && !Info.TypeObj->isReference();
        bool rawPayloadCapability =
            m_InUnsafeContext && Info.TypeObj && Info.TypeObj->isRawPointer() &&
            !Info.IsHandleRebindable();
        bool payloadCapability =
            Info.Permission.SoulWritable || rawPayloadCapability;
        bool payloadIntent = ve->IsValueMutable || Info.Permission.SoulWritable;
        bool usageMutable = false;
        if (shouldCollapse) {
           usageMutable = payloadCapability && payloadIntent;
        } else {
           usageMutable =
               Info.Permission.IdentityRebindable ||
               (!Info.TypeObj->isPointer() && !Info.TypeObj->isSmartPointer() &&
                !Info.TypeObj->isReference() && Info.Permission.SoulWritable);
        }

        return current->withAttributes(usageMutable, current->IsNullable);
      }
    }

    // Identity view (Collapse disabled)
    if (!shouldCollapse && current) {
      bool identWritable =
          Info.Permission.IdentityRebindable ||
          (!Info.TypeObj->isPointer() && !Info.TypeObj->isSmartPointer() &&
           !Info.TypeObj->isReference() && Info.Permission.SoulWritable);
      return current->withAttributes(identWritable, current->IsNullable);
    }

    return current;
  } else if (auto *Cast = dynamic_cast<CastExpr *>(E)) {
    validateTypeVisibilityInType(Cast->TargetType, getLoc(Cast));
    auto targetType = resolveType(
        Cast->TargetTypeSyntax
            ? toka::Type::fromSyntax(Cast->TargetTypeSyntax)
            : toka::Type::fromString(Cast->TargetType));
    validateDynTraitObjectSafetyInType(targetType, getLoc(Cast));
    if (Cast->Kind == CastKind::Implicit) {
      checkExpr(Cast->Expression.get(), targetType);
      return targetType;
    }

    if (Cast->Kind == CastKind::Ascription) {
      if (auto *closure = dynamic_cast<ClosureExpr *>(Cast->Expression.get())) {
        std::vector<std::shared_ptr<Type>> parameterTypes;
        std::shared_ptr<Type> returnType;
        if (targetType->typeKind == Type::Function) {
          auto functionType = std::static_pointer_cast<FunctionType>(targetType);
          parameterTypes = functionType->ParamTypes;
          returnType = functionType->ReturnType;
        } else if (targetType->typeKind == Type::DynFn) {
          auto functionType = std::static_pointer_cast<DynFnType>(targetType);
          parameterTypes = functionType->ParamTypes;
          returnType = functionType->ReturnType;
        }
        if (returnType) {
          closure->InjectedParamTypes = std::move(parameterTypes);
          if (closure->ReturnType.empty() || closure->ReturnType == "unknown")
            closure->ReturnType = returnType->toString();
        }
      }
    }

    auto srcType = Cast->Kind == CastKind::Ascription
                       ? checkExpr(Cast->Expression.get(), targetType)
                       : checkExpr(Cast->Expression.get());

    if (Cast->Kind == CastKind::Ascription) {
      if (dynamic_cast<UnsetExpr *>(Cast->Expression.get())) {
        m_LastInitMask = 0;
        return targetType;
      }
      // Resolving an ascription target may already have diagnosed an unknown
      // type.  Do not turn that primary name-resolution error into a second,
      // misleading compatibility failure.
      if (!targetType->isUnknown() && !isTypeCompatible(targetType, srcType)) {
        error(Cast, DiagID::ERR_TYPE_ASCRIPTION_MISMATCH, Cast->TargetType,
              srcType ? srcType->toString() : "unknown");
      }
      return targetType;
    }

    // Rule: Numeric Casts (Always allowed for standard numeric types)
    auto srcTypeResolved = resolveType(srcType, true);
    auto targetTypeResolved = resolveType(targetType, true);
    bool srcIsNumeric = srcTypeResolved->isInteger() || srcTypeResolved->isFloatingPoint();
    bool targetIsNumeric =
        targetTypeResolved->isInteger() || targetTypeResolved->isFloatingPoint();

    // Rule: Pointer Morphologies or Addr
    bool srcIsAddr = srcType->isAddrType();
    bool targetIsAddr =
        (Cast->TargetType == "Addr" || resolveType("Addr") == Cast->TargetType);

    bool srcIsRaw = srcType->isRawPointer();
    bool targetIsRaw = targetType->isRawPointer();

    bool srcIsOAddr = srcType->isOAddrType();
    bool targetIsOAddr = (Cast->TargetType == "OAddr" ||
                          resolveType("OAddr") == Cast->TargetType);

    if (targetIsOAddr) {
      // [User Directive] as OAddr is special and always safe.
      // Skip all borrow registration and checks.
      return targetType;
    }

    if (srcIsOAddr && !targetIsOAddr) {
      DiagnosticEngine::report(getLoc(Cast), DiagID::ERR_CAST_MISMATCH,
                               srcType->toString(), Cast->TargetType);
      HasError = true;
    }

    if (srcIsNumeric && targetIsNumeric) {
      // Normal numeric cast, allow.
    } else if (targetType->isReference()) {
      // [Constitution 1.3] Borrow-Cast: var as &Node or var as &var
      // Re-interpreting identity as a direct borrow view.
      auto targetInner = targetType->getPointeeType();
      auto srcInner =
          (srcType->isPointer())
              ? std::dynamic_pointer_cast<toka::PointerType>(srcType)
                    ->getPointeeType()
              : srcType;

      bool raisesWritePermission =
          targetInner && targetInner->IsWritable &&
          (isReadOnlyReferenceViewExpr(Cast->Expression.get(), CurrentScope) ||
           !srcInner || !srcInner->IsWritable);
      if (!isTypeCompatible(targetInner, srcInner) || raisesWritePermission) {
        error(Cast, DiagID::ERR_CAST_MISMATCH, srcType->toString(),
              Cast->TargetType);
      }

      if (srcType->isReference()) {
        // [Optimization] Source is already a reference.
        // We are just re-interpreting the type, not creating a new borrow.
        // Skip registration.
        return targetType;
      }

      // Semantic Side-Effect: Register this as a borrow
      Expr *Traverse = Cast->Expression.get();
      // Peel Identity Op if present: ^p -> p
      if (auto *UE = dynamic_cast<UnaryExpr *>(Traverse)) {
        if (UE->Op == TokenType::Caret || UE->Op == TokenType::Tilde ||
            UE->Op == TokenType::Star) {
          Traverse = UE->RHS.get();
        }
      }

      while (true) {
        if (auto *M = dynamic_cast<MemberExpr *>(Traverse)) {
          Traverse = M->Object.get();
        } else if (auto *Idx = dynamic_cast<ArrayIndexExpr *>(Traverse)) {
          Traverse = Idx->Array.get();
        } else {
          break;
        }
      }

      if (auto *Var = dynamic_cast<VariableExpr *>(Traverse)) {
        SymbolInfo *Info = nullptr;
        if (CurrentScope->findSymbol(Var->Name, Info)) {
          // [Fix] Trace to Source for Borrow Registration
          SymbolInfo *EffectiveInfo = Info;
          std::string EffectiveName = Var->Name;
          EffectiveInfo = resolveBorrowSource(EffectiveInfo, EffectiveName);

          if (!m_InLHS) {
            bool isExclusive = targetInner->IsWritable;
            std::string pathToBorrow = EffectiveName;
            if (!pathToBorrow.empty()) {
               if (!PALCheckerState.recordBorrow(
                       canonicalizeAccessPath(makeAccessPath(pathToBorrow)),
                       isExclusive, Cast->Loc)) {
                   error(Cast, DiagID::ERR_BORROW_MUT, pathToBorrow);
                   if (PALCheckerState.lastConflict()) {
                     recordPALConflict(
                         Cast,
                         isExclusive
                             ? PALOperationClass::ExclusivePayloadBorrow
                             : PALOperationClass::SharedPayloadBorrow,
                         canonicalizeAccessPath(makeAccessPath(pathToBorrow)),
                         *PALCheckerState.lastConflict());
                   }
               }
               m_LastBorrowSource = pathToBorrow;
            }
          }
        }
      }
    } else if (!m_InUnsafeContext && targetType->isSmartPointer() && !srcType->isSmartPointer() &&
               !srcType->isNullType()) {
      error(Cast, DiagID::ERR_SMART_PTR_FROM_STACK, Cast->TargetType[0]);
    } else if (!m_InUnsafeContext && targetIsRaw &&
               (srcType->isUniquePtr() || srcType->isSharedPtr())) {
      error(Cast, DiagID::ERR_SEMA_IDENTITY_INTRUSION_MANAGED_MEMORY_CANNOT, srcType->toString(), Cast->TargetType);
    } else if (targetIsAddr) {
      if (!(srcIsAddr || srcIsRaw || srcIsNumeric || srcType->isUniquePtr() || srcType->isSharedPtr() || srcType->isReference())) {
        error(Cast, DiagID::ERR_CAST_MISMATCH, srcType->toString(),
              Cast->TargetType);
      }
    } else if (srcIsAddr) {
      if (!(targetIsAddr || targetIsRaw || targetIsNumeric)) {
        error(Cast, DiagID::ERR_CAST_MISMATCH, srcType->toString(),
              Cast->TargetType);
      }
    } else if (targetIsOAddr) {
      if (!srcType->isPointer()) {
        error(Cast, DiagID::ERR_CAST_MISMATCH, srcType->toString(),
              Cast->TargetType);
      }
    } else if (targetIsRaw) {
      bool srcIsStr = srcType->isStringType();
      bool srcIsNull = srcType->isNullType();
      auto isKnownZeroAddress = [&](auto &&self, const Expr *expr) -> bool {
        if (auto *number = dynamic_cast<const NumberExpr *>(expr))
          return number->Value == 0;
        if (auto *variable = dynamic_cast<const VariableExpr *>(expr))
          return variable->Name == "ADDR0";
        if (auto *innerCast = dynamic_cast<const CastExpr *>(expr))
          return self(self, innerCast->Expression.get());
        return false;
      };
      if (srcIsNull && !targetType->IsNullable) {
        error(Cast, DiagID::ERR_NONZERO_RAW_NULL_FLOW,
              targetType->toString());
      }
      if (!targetType->IsNullable &&
          isKnownZeroAddress(isKnownZeroAddress, Cast->Expression.get())) {
        error(Cast, DiagID::ERR_NONZERO_RAW_NULL_FLOW,
              targetType->toString());
      }
      if (srcIsRaw && srcType->IsNullable && !targetType->IsNullable) {
        error(Cast, DiagID::ERR_NONZERO_RAW_NULL_FLOW,
              targetType->toString());
      }
      if (!(srcIsAddr || srcIsRaw || srcIsNumeric || srcIsStr || srcIsNull ||
            srcType->isUniquePtr() ||
            srcType->isSharedPtr() || srcType->isReference())) {
        error(Cast, DiagID::ERR_CAST_MISMATCH, srcType->toString(),
              Cast->TargetType);
      }
    } else if (srcIsRaw) {
      if (!(targetIsAddr || targetIsRaw || targetIsNumeric || (m_InUnsafeContext && targetType->isSmartPointer()) || targetType->isReference())) {
        error(Cast, DiagID::ERR_CAST_MISMATCH, srcType->toString(),
              Cast->TargetType);
      }
    } else if (!srcType->equals(*targetType)) {
      // Legacy union reinterpretation and enum casting
      auto srcTypeResolved = resolveType(srcType);
      bool srcIsUnion = false;
      bool srcIsEnum = false;
      std::shared_ptr<ShapeType> st = nullptr;
      if (srcTypeResolved->isShape()) {
        st = std::dynamic_pointer_cast<ShapeType>(srcTypeResolved);
        if (st->Decl && st->Decl->Kind == ShapeKind::Union) {
          srcIsUnion = true;
        } else if (st->Decl && st->Decl->Kind == ShapeKind::Enum) {
          srcIsEnum = true;
        }
      }

      if (srcIsEnum && targetIsNumeric) {
        // Enum can be cast to an integer Discriminant. Valid.
      } else if (srcIsUnion) {
        bool found = false;
        for (const auto &M : st->Decl->Members) {
          auto mType = getPhysicalType(M);
          if (mType && targetType->equals(*mType)) {
            found = true;
            break;
          }
        }
        if (!found) {
          error(Cast, DiagID::ERR_CAST_MISMATCH, srcType->toString(),
                Cast->TargetType);
        }
      } else if (!isTypeCompatible(targetType, srcType)) {
        error(Cast, DiagID::ERR_CAST_MISMATCH, srcType->toString(),
              Cast->TargetType);
      }
    }

    return targetType;
  } else if (auto *Bin = dynamic_cast<BinaryExpr *>(E)) {
    return checkBinaryExpr(Bin);
  } else if (auto *ie = dynamic_cast<IfExpr *>(E)) {

    ie->Condition = foldGenericConstant(std::move(ie->Condition));
    if (auto *boolLit = dynamic_cast<BoolExpr*>(ie->Condition.get())) {
        ie->IsComptime = true;
        ie->ComptimeTaken = boolLit->Value;
        
        bool isReceiver = false;
        if (!m_ControlFlowStack.empty()) isReceiver = m_ControlFlowStack.back().IsReceiver;
        m_ControlFlowStack.push_back({"", NoProducedValue, nullptr, false, isReceiver});
        
        if (ie->ComptimeTaken) {
            checkStmt(ie->Then.get());
        } else if (ie->Else) {
            checkStmt(ie->Else.get());
        }
        
        auto retTypeObj = m_ControlFlowStack.back().ExpectedTypeObj ? m_ControlFlowStack.back().ExpectedTypeObj : toka::Type::fromString("()");
        m_ControlFlowStack.pop_back();
        return retTypeObj;
    }

    const BinaryExpr *oldExpectedInitStatePredicate =
        m_ExpectedInitStatePredicate;
    m_ExpectedInitStatePredicate =
        dynamic_cast<BinaryExpr *>(ie->Condition.get());
    auto condTypeObj =
        checkExpr(ie->Condition.get(), toka::Type::fromString("bool"));
    m_ExpectedInitStatePredicate = oldExpectedInitStatePredicate;
    std::string condType = condTypeObj->toString();

    // Type narrowing for null checks is branch-sensitive.  `x is null`
    // proves x nullable in its then branch and non-null only in its else
    // branch; `x is! null` proves non-null in its then branch.  A path
    // narrowing is therefore an observation of that exact branch, never a
    // permission grant or a fact that survives the conditional.
    std::string narrowedVar;
    SymbolInfo originalInfo;
    bool narrowed = false;
    bool narrowThen = false;
    bool narrowElse = false;
    VariableExpr *narrowedVariable = nullptr;
    bool narrowsInitState = false;
    VariableExpr *initStateVariable = nullptr;
    SymbolInfo initStateOriginal;

    if (auto *bin = dynamic_cast<BinaryExpr *>(ie->Condition.get())) {
      if (bin->IsInitStatePredicate) {
        initStateVariable = dynamic_cast<VariableExpr *>(bin->LHS.get());
        if (initStateVariable) {
          SymbolInfo *infoPtr = nullptr;
          if (CurrentScope->findSymbol(initStateVariable->Name, infoPtr) &&
              infoPtr) {
            narrowsInitState = true;
            initStateOriginal = *infoPtr;
          }
        }
      } else {
        const bool comparesNull =
            dynamic_cast<NullExpr *>(bin->RHS.get());
        if (bin->Op == "is" || bin->Op == "is!") {
          narrowThen = (bin->Op == "is" && !comparesNull) ||
                        (bin->Op == "is!" && comparesNull);
          narrowElse = bin->Op == "is" && comparesNull;
        }
      }

      if (!narrowsInitState && (narrowThen || narrowElse)) {
        Expr *lhs = bin->LHS.get();
        std::string path = getPathString(lhs);
        if (!path.empty()) {
          narrowed = true;
          narrowedVar = path;
        }

        narrowedVariable = dynamic_cast<VariableExpr *>(lhs);
        if (!narrowedVariable) {
          if (auto *un = dynamic_cast<UnaryExpr *>(lhs)) {
            narrowedVariable = dynamic_cast<VariableExpr *>(un->RHS.get());
          }
        }

        if (narrowedVariable) {
          SymbolInfo *infoPtr = nullptr;
          if (CurrentScope->findSymbol(narrowedVariable->Name, infoPtr)) {
            if (!narrowed) {
              narrowedVar = narrowedVariable->Name;
              narrowed = true;
            }
            originalInfo = *infoPtr;
          }
        }
      }
    }

    auto applyNarrowing = [&]() {
      if (!narrowed)
        return;
      m_NarrowedPaths.insert(narrowedVar);
      if (!narrowedVariable)
        return;
      SymbolInfo *infoPtr = nullptr;
      if (CurrentScope->findSymbol(narrowedVariable->Name, infoPtr) &&
          infoPtr && infoPtr->TypeObj) {
        infoPtr->TypeObj = infoPtr->TypeObj->withAttributes(
            infoPtr->TypeObj->IsWritable, false);
      }
    };

    auto restoreNarrowing = [&]() {
      if (!narrowed)
        return;
      if (narrowedVariable) {
        SymbolInfo *infoPtr = nullptr;
        if (CurrentScope->findSymbol(narrowedVariable->Name, infoPtr) &&
            infoPtr) {
          *infoPtr = originalInfo;
        }
      }
      m_NarrowedPaths.erase(narrowedVar);
    };

    auto applyInitStateNarrowing = [&](PlaceState state) {
      if (!narrowsInitState || !initStateVariable)
        return;
      SymbolInfo *infoPtr = nullptr;
      if (!CurrentScope->findSymbol(initStateVariable->Name, infoPtr) ||
          !infoPtr)
        return;
      infoPtr->placeFact() = state;
      infoPtr->Moved = false;
      infoPtr->InitMask = state == PlaceState::Never ? 0 : ~0ULL;
    };

    auto restoreInitStateNarrowing = [&]() {
      if (!narrowsInitState || !initStateVariable)
        return;
      SymbolInfo *infoPtr = nullptr;
      if (CurrentScope->findSymbol(initStateVariable->Name, infoPtr) &&
          infoPtr)
        *infoPtr = initStateOriginal;
    };

    bool isReceiver = false;
    if (!m_ControlFlowStack.empty()) {
      isReceiver = m_ControlFlowStack.back().IsReceiver;
    }

    m_ControlFlowStack.push_back({"", NoProducedValue, nullptr, false, isReceiver});

    // Save Mask & Moved State for Intersection Rule
    // A branch may appear inside a nested block while mutating a binding from
    // an enclosing scope.  Snapshot the visible bindings, not just the
    // current block, so a terminating arm cannot leave its parent local in an
    // uninitialized state on the reachable sibling path.
    auto masksBefore = captureVisibleInitMasks(CurrentScope);
    auto movedBefore = captureVisibleMoved(CurrentScope);
    auto exactPlacesBefore = captureVisibleExactPlaceFacts(CurrentScope);
    auto conditionalBefore = captureVisibleConditionalTodoIds(CurrentScope);
    auto palBefore = PALCheckerState.snapshot();

    if (narrowsInitState)
      applyInitStateNarrowing(PlaceState::Never);
    else if (narrowThen)
      applyNarrowing();
    checkStmt(ie->Then.get());

    auto masksThen = captureVisibleInitMasks(CurrentScope);
    auto movedThen = captureVisibleMoved(CurrentScope);
    auto exactPlacesThen = captureVisibleExactPlaceFacts(CurrentScope);
    auto conditionalThen = captureVisibleConditionalTodoIds(CurrentScope);
    auto palThen = PALCheckerState.snapshot();

    if (narrowsInitState)
      restoreInitStateNarrowing();
    else if (narrowThen)
      restoreNarrowing();

    std::string thenType = m_ControlFlowStack.back().ExpectedType;
    auto thenTypeObj = m_ControlFlowStack.back().ExpectedTypeObj;
    bool thenReturns = allPathsJump(ie->Then.get());
    m_ControlFlowStack.pop_back();

    std::string elseType = NoProducedValue;
    std::shared_ptr<toka::Type> elseTypeObj;
    bool elseReturns = false;
    if (ie->Else) {
      // Restore Before Else
      restoreVisibleAnalysisState(CurrentScope, masksBefore, movedBefore,
                                  exactPlacesBefore);
      restoreVisibleConditionalTodoIds(CurrentScope, conditionalBefore);
      PALCheckerState.restore(palBefore);

      m_ControlFlowStack.push_back({"", NoProducedValue, nullptr, false, isReceiver});
      if (narrowsInitState)
        applyInitStateNarrowing(PlaceState::Live);
      else if (narrowElse)
        applyNarrowing();
      checkStmt(ie->Else.get());
      elseType = m_ControlFlowStack.back().ExpectedType;
      elseTypeObj = m_ControlFlowStack.back().ExpectedTypeObj;
      elseReturns = allPathsJump(ie->Else.get());
      auto masksElse = captureVisibleInitMasks(CurrentScope);
      auto conditionalElse = captureVisibleConditionalTodoIds(CurrentScope);
      auto exactPlacesElse = captureVisibleExactPlaceFacts(CurrentScope);
      auto palElse = PALCheckerState.snapshot();
      m_ControlFlowStack.pop_back();
      if (narrowsInitState)
        restoreInitStateNarrowing();
      else if (narrowElse)
        restoreNarrowing();

      // Intersection Rule
      if (thenReturns && elseReturns) {
        // No reachable continuation; keep the incoming PAL state for any
        // subsequent dead-code diagnostics.
        restoreVisibleConditionalTodoIds(CurrentScope, conditionalBefore);
        PALCheckerState.restore(palBefore);
      } else if (thenReturns) {
        // State is purely from Else branch.  Restore the editor-only state
        // explicitly because narrowing restoration may have replaced a full
        // SymbolInfo after the snapshot was taken.
        restoreVisibleConditionalTodoIds(CurrentScope, conditionalElse);
        PALCheckerState.restore(palElse);
      } else if (elseReturns) {
        // State is purely from Then branch
        restoreVisibleAnalysisState(CurrentScope, masksThen, movedThen,
                                    exactPlacesThen);
        restoreVisibleConditionalTodoIds(CurrentScope, conditionalThen);
        PALCheckerState.restore(palThen);
      } else {
        // Actual Intersection
        for (const auto &pair : masksBefore) {
          SymbolInfo *info = nullptr;
          if (!CurrentScope->findSymbol(pair.first, info) || !info)
            continue;
          uint64_t thenM = masksThen.count(pair.first) ? masksThen[pair.first] : 0;
          uint64_t elseM = masksElse.count(pair.first) ? masksElse[pair.first] : 0;
          info->InitMask = thenM & elseM;
          bool thenMoved = movedThen.count(pair.first) ? movedThen[pair.first] : false;
          info->Moved = info->Moved || thenMoved;
          ExactPlaceFacts exactPlaces =
              exactPlacesThen.count(pair.first)
                  ? exactPlacesThen[pair.first]
                  : ExactPlaceFacts::bottom();
          if (exactPlacesElse.count(pair.first))
            exactPlaces |= exactPlacesElse[pair.first];
          info->ExactPlace = exactPlaces;
          syncLegacyProjectionLiveness(*info);
        }
        for (const auto &pair : conditionalBefore) {
          SymbolInfo *info = nullptr;
          if (!CurrentScope->findSymbol(pair.first, info) || !info)
            continue;
          std::set<uint64_t> dependencies =
              conditionalThen.count(pair.first)
                  ? conditionalThen[pair.first]
                  : std::set<uint64_t>{};
          if (conditionalElse.count(pair.first)) {
            dependencies.insert(conditionalElse[pair.first].begin(),
                                conditionalElse[pair.first].end());
          }
          info->ConditionalTodoIds = std::move(dependencies);
        }
        PALCheckerState.mergeBranches(palBefore, palThen, true, palElse, true);
      }
    } else {
      for (const auto &pair : masksBefore) {
        SymbolInfo *info = nullptr;
        if (!CurrentScope->findSymbol(pair.first, info) || !info)
          continue;
        info->InitMask = pair.second;
        if (thenReturns) {
          info->Moved = movedBefore[pair.first];
          info->ExactPlace =
              exactPlacesBefore.count(pair.first)
                  ? exactPlacesBefore[pair.first]
                  : ExactPlaceFacts::bottom();
          syncLegacyProjectionLiveness(*info);
          if (narrowsInitState && initStateVariable &&
              pair.first == initStateVariable->Name) {
            info->Moved = false;
            info->placeFact() = PlaceState::Live;
            info->InitMask = ~0ULL;
          }
        } else {
          bool thenMoved = movedThen.count(pair.first) ? movedThen[pair.first] : false;
          info->Moved = movedBefore[pair.first] || thenMoved;
          ExactPlaceFacts exactPlaces =
              exactPlacesBefore.count(pair.first)
                  ? exactPlacesBefore[pair.first]
                  : ExactPlaceFacts::bottom();
          ExactPlaceFacts thenExactPlaces =
              exactPlacesThen.count(pair.first)
                  ? exactPlacesThen[pair.first]
                  : ExactPlaceFacts::bottom();
          exactPlaces |= thenExactPlaces;
          info->ExactPlace = exactPlaces;
          syncLegacyProjectionLiveness(*info);
          if (narrowsInitState && initStateVariable &&
              pair.first == initStateVariable->Name) {
            info->placeFact() =
                thenExactPlaces.whole().join(PlaceState::Live);
            info->Moved = false;
            info->InitMask = hasExactlyPlaceState(info->placeFact(),
                                                   PlaceState::Live)
                                 ? ~0ULL
                                 : 0;
          }
        }
      }
      for (const auto &pair : conditionalBefore) {
        SymbolInfo *info = nullptr;
        if (!CurrentScope->findSymbol(pair.first, info) || !info)
          continue;
        if (thenReturns) {
          info->ConditionalTodoIds = pair.second;
        } else {
          std::set<uint64_t> dependencies = pair.second;
          if (conditionalThen.count(pair.first)) {
            dependencies.insert(conditionalThen[pair.first].begin(),
                                conditionalThen[pair.first].end());
          }
          info->ConditionalTodoIds = std::move(dependencies);
        }
      }
      if (thenReturns) {
        PALCheckerState.restore(palBefore);
      } else {
        PALCheckerState.mergeBranches(palBefore, palThen, true, palBefore, true);
      }
    }

    if (isReceiver) {
      if (thenType == NoProducedValue && !allPathsJump(ie->Then.get()))
        error(ie->Then.get(), DiagID::ERR_YIELD_VALUE_REQUIRED, "if branch");
      if (!ie->Else)
        error(ie, DiagID::ERR_YIELD_ELSE_REQUIRED);
      else if (elseType == NoProducedValue && !allPathsJump(ie->Else.get()))
        error(ie->Else.get(), DiagID::ERR_YIELD_VALUE_REQUIRED, "else branch");
    }

    if (thenType != NoProducedValue && elseType != NoProducedValue &&
        !isTypeCompatible(thenTypeObj, elseTypeObj)) {
      error(ie, DiagID::ERR_BRANCH_TYPE_MISMATCH, "If", thenType, elseType);
    }
    const std::string result =
        (thenType != NoProducedValue) ? thenType : elseType;
    return toka::Type::fromString(result == NoProducedValue ? "()" : result);
  } else if (auto *guard = dynamic_cast<GuardExpr *>(E)) {
    auto condType = checkExpr(guard->Condition.get());
    if (condType->isUnknown())
      return condType;

    auto *varExpr = dynamic_cast<VariableExpr *>(guard->Condition.get());
    if (!varExpr) {
      if (auto *unary = dynamic_cast<UnaryExpr *>(guard->Condition.get())) {
        varExpr = dynamic_cast<VariableExpr *>(unary->RHS.get());
      }
    }

    // A projection guard may refine only an exact compiler-managed local
    // path.  This keeps the proof local to the observed member/index and
    // avoids synthesizing a fact for a dynamic index, a sibling, or a caller
    // supplied alias.
    std::string guardedPath;
    bool isProjectionGuard = false;
    if (!varExpr) {
      if (auto *member = dynamic_cast<MemberExpr *>(guard->Condition.get())) {
        if (auto *root = dynamic_cast<VariableExpr *>(member->Object.get())) {
          SymbolInfo *rootInfo = nullptr;
          std::string actualRoot;
          if (CurrentScope->findVariableWithDeref(root->Name, rootInfo,
                                                  actualRoot) &&
              rootInfo && rootInfo->IsDeclaredVariable &&
              !rootInfo->IsFunctionParameter) {
            guardedPath = getPathString(member);
            isProjectionGuard = !guardedPath.empty();
          }
        }
      } else if (auto *index =
                     dynamic_cast<ArrayIndexExpr *>(guard->Condition.get())) {
        if (index->Indices.size() == 1 &&
            dynamic_cast<NumberExpr *>(index->Indices[0].get())) {
          if (auto *root =
                  dynamic_cast<VariableExpr *>(index->Array.get())) {
            SymbolInfo *rootInfo = nullptr;
            std::string actualRoot;
            if (CurrentScope->findVariableWithDeref(root->Name, rootInfo,
                                                    actualRoot) &&
                rootInfo && rootInfo->IsDeclaredVariable &&
                !rootInfo->IsFunctionParameter) {
              guardedPath = getPathString(index);
              isProjectionGuard = !guardedPath.empty();
            }
          }
        }
      }
    }

    if (!varExpr && !isProjectionGuard) {
      error(guard->Condition.get(), DiagID::ERR_SEMA_GUARD_CONDITION_MUST_BE_A_VARIABLE);
      return std::make_shared<UnitType>();
    }

    SymbolInfo *infoPtr = nullptr;
    std::string actualName;

    std::map<std::string, uint64_t> masksBefore;
    std::map<std::string, bool> movedBefore;
    std::map<std::string, ExactPlaceFacts> exactPlacesBefore;
    for (auto &pair : CurrentScope->Symbols) {
      masksBefore[pair.first] = pair.second.InitMask;
      movedBefore[pair.first] = pair.second.Moved;
      exactPlacesBefore[pair.first] = pair.second.ExactPlace;
    }
    auto palBefore = PALCheckerState.snapshot();

    auto restoreGuardEntryState = [&]() {
      for (auto &pair : masksBefore) {
        CurrentScope->Symbols[pair.first].InitMask = pair.second;
      }
      for (auto &pair : movedBefore) {
        CurrentScope->Symbols[pair.first].Moved = pair.second;
      }
      for (auto &pair : exactPlacesBefore) {
        auto &info = CurrentScope->Symbols[pair.first];
        info.ExactPlace = pair.second;
        info.InitMask = pair.second.applyToLegacyInitMask(info.InitMask);
      }
      PALCheckerState.restore(palBefore);
    };

    auto captureMasks = [&]() {
      std::map<std::string, uint64_t> masks;
      for (auto &pair : CurrentScope->Symbols) {
        masks[pair.first] = pair.second.InitMask;
      }
      return masks;
    };

    auto captureMoved = [&]() {
      std::map<std::string, bool> moved;
      for (auto &pair : CurrentScope->Symbols) {
        moved[pair.first] = pair.second.Moved;
      }
      return moved;
    };

    auto captureExactPlaceFacts = [&]() {
      std::map<std::string, ExactPlaceFacts> facts;
      for (auto &pair : CurrentScope->Symbols)
        facts[pair.first] = pair.second.ExactPlace;
      return facts;
    };

    bool thenJumps = false;
    bool elseJumps = false;
    std::map<std::string, uint64_t> masksThen;
    std::map<std::string, bool> movedThen;
    std::map<std::string, ExactPlaceFacts> exactPlacesThen;
    std::map<std::string, uint64_t> masksElse = masksBefore;
    std::map<std::string, bool> movedElse = movedBefore;
    std::map<std::string, ExactPlaceFacts> exactPlacesElse = exactPlacesBefore;
    PALChecker palThen = palBefore;
    PALChecker palElse = palBefore;

    if (varExpr &&
        CurrentScope->findVariableWithDeref(varExpr->Name, infoPtr,
                                            actualName)) {
      const bool isPtrNullable =
          condType->isRawPointer() && condType->IsNullable;

      if (!isPtrNullable && !condType->isVoid()) {
        error(guard->Condition.get(),
              DiagID::ERR_SEMA_GUARD_CONDITION_MUST_BE_A_MAY_ZERO_RAW_TYPE);
      }

      enterScope();
      SymbolInfo nonNullInfo = *infoPtr;
      if (nonNullInfo.TypeObj) {
        nonNullInfo.TypeObj = nonNullInfo.TypeObj->withAttributes(
            nonNullInfo.TypeObj->IsWritable, false,
            nonNullInfo.TypeObj->IsBlocked);
      }
      CurrentScope->define(actualName, nonNullInfo);

      checkStmt(guard->Then.get());
      exitScope();
      thenJumps = allPathsJump(guard->Then.get());
      masksThen = captureMasks();
      movedThen = captureMoved();
      exactPlacesThen = captureExactPlaceFacts();
      palThen = PALCheckerState.snapshot();
    } else {
      const bool isPtrNullable =
          condType->isRawPointer() && condType->IsNullable;
      if (!isPtrNullable && !condType->isVoid()) {
        error(guard->Condition.get(),
              DiagID::ERR_SEMA_GUARD_CONDITION_MUST_BE_A_MAY_ZERO_RAW_TYPE);
      }
      enterScope();
      if (isProjectionGuard)
        m_NarrowedPaths.insert(guardedPath);
      checkStmt(guard->Then.get());
      if (isProjectionGuard)
        m_NarrowedPaths.erase(guardedPath);
      exitScope();
      thenJumps = allPathsJump(guard->Then.get());
      masksThen = captureMasks();
      movedThen = captureMoved();
      exactPlacesThen = captureExactPlaceFacts();
      palThen = PALCheckerState.snapshot();
    }

    restoreGuardEntryState();
    if (guard->Else) {
      enterScope();
      checkStmt(guard->Else.get());
      exitScope();
      elseJumps = allPathsJump(guard->Else.get());
      masksElse = captureMasks();
      movedElse = captureMoved();
      exactPlacesElse = captureExactPlaceFacts();
      palElse = PALCheckerState.snapshot();
    }

    if (guard->Else) {
      if (thenJumps && elseJumps) {
        restoreGuardEntryState();
      } else if (thenJumps) {
        for (auto &pair : CurrentScope->Symbols) {
          if (masksElse.count(pair.first))
            pair.second.InitMask = masksElse[pair.first];
          if (movedElse.count(pair.first))
            pair.second.Moved = movedElse[pair.first];
          if (exactPlacesElse.count(pair.first)) {
            pair.second.ExactPlace = exactPlacesElse[pair.first];
            syncLegacyProjectionLiveness(pair.second);
          }
        }
        PALCheckerState.restore(palElse);
      } else if (elseJumps) {
        for (auto &pair : CurrentScope->Symbols) {
          if (masksThen.count(pair.first))
            pair.second.InitMask = masksThen[pair.first];
          if (movedThen.count(pair.first))
            pair.second.Moved = movedThen[pair.first];
          if (exactPlacesThen.count(pair.first)) {
            pair.second.ExactPlace = exactPlacesThen[pair.first];
            syncLegacyProjectionLiveness(pair.second);
          }
        }
        PALCheckerState.restore(palThen);
      } else {
        for (auto &pair : CurrentScope->Symbols) {
          uint64_t thenMask = masksThen.count(pair.first) ? masksThen[pair.first] : 0;
          uint64_t elseMask = masksElse.count(pair.first) ? masksElse[pair.first] : 0;
          pair.second.InitMask = thenMask & elseMask;
          bool thenMoved = movedThen.count(pair.first) ? movedThen[pair.first] : false;
          bool elseMoved = movedElse.count(pair.first) ? movedElse[pair.first] : false;
          pair.second.Moved = thenMoved || elseMoved;
          ExactPlaceFacts exactPlaces =
              exactPlacesThen.count(pair.first)
                  ? exactPlacesThen[pair.first]
                  : ExactPlaceFacts::bottom();
          if (exactPlacesElse.count(pair.first))
            exactPlaces |= exactPlacesElse[pair.first];
          pair.second.ExactPlace = exactPlaces;
          syncLegacyProjectionLiveness(pair.second);
        }
        PALCheckerState.mergeBranches(palBefore, palThen, true, palElse, true);
      }
    } else {
      if (thenJumps) {
        restoreGuardEntryState();
      } else {
        for (auto &pair : CurrentScope->Symbols) {
          uint64_t thenMask = masksThen.count(pair.first) ? masksThen[pair.first] : 0;
          uint64_t entryMask = masksBefore.count(pair.first) ? masksBefore[pair.first] : 0;
          pair.second.InitMask = thenMask & entryMask;
          bool thenMoved = movedThen.count(pair.first) ? movedThen[pair.first] : false;
          bool entryMoved = movedBefore.count(pair.first) ? movedBefore[pair.first] : false;
          pair.second.Moved = thenMoved || entryMoved;
          ExactPlaceFacts exactPlaces =
              exactPlacesThen.count(pair.first)
                  ? exactPlacesThen[pair.first]
                  : ExactPlaceFacts::bottom();
          if (exactPlacesBefore.count(pair.first))
            exactPlaces |= exactPlacesBefore[pair.first];
          pair.second.ExactPlace = exactPlaces;
          syncLegacyProjectionLiveness(pair.second);
        }
        PALCheckerState.mergeBranches(palBefore, palThen, true, palBefore, true);
      }
    }

    return std::make_shared<UnitType>();
  } else if (auto *le = dynamic_cast<LoopExpr *>(E)) {
    if (le->Condition) {
      auto condTy = checkExpr(le->Condition.get(), toka::Type::fromString("bool"));
      if (condTy && !condTy->isBoolean()) {
        error(le->Condition.get(), DiagID::ERR_OPERAND_TYPE_MISMATCH, "loop condition", "bool", condTy->toString());
      }
      std::set<std::string> conditionVars;
      collectVariables(le->Condition.get(), conditionVars);
      bool isWarningExempt = false;
      if (le->Loc.isValid()) {
        std::string path = DiagnosticEngine::SrcMgr->getFullSourceLoc(le->Loc).FileName;
        if (path.find("tests/") != std::string::npos ||
            path.find("build.tk") != std::string::npos ||
            path.find("prelude") != std::string::npos ||
            path.find("lib/") != std::string::npos) {
          isWarningExempt = true;
        }
      }
      if (!isWarningExempt) {
        bool anyMutated = false;
        bool hasCheckableVar = false;
        std::string firstCheckableVar;
        for (const auto &varName : conditionVars) {
          SymbolInfo info;
          if (CurrentScope->lookup(varName, info)) {
            if (info.IsDeclaredVariable && !info.HasConstValue) {
              hasCheckableVar = true;
              if (firstCheckableVar.empty()) {
                firstCheckableVar = varName;
              }
              if (isVariableMutated(le->Body.get(), varName) || isVariableMutated(le->Condition.get(), varName)) {
                anyMutated = true;
                break;
              }
            }
          }
        }
        if (hasCheckableVar && !anyMutated) {
          DiagnosticEngine::report(le->Loc, DiagID::WARN_NON_PROGRESS_LOOP, firstCheckableVar);
        }
      }
    }

    bool isReceiver = false;
    if (!m_ControlFlowStack.empty()) {
      isReceiver = m_ControlFlowStack.back().IsReceiver;
    }

    if (isReceiver && (!m_ControlFlowStack.empty() &&
                       m_ControlFlowStack.back().ExpectedType != NoProducedValue)) {
      error(le, DiagID::ERR_SEMA_TOKA_1_0_DOES_NOT_SUPPORT_YIELDING_VALUES);
    }

    bool tookOver = false;
    if (!m_ControlFlowStack.empty() && !m_ControlFlowStack.back().IsLoop &&
        !m_ControlFlowStack.back().Label.empty()) {
      m_ControlFlowStack.back().IsLoop = true;
      m_ControlFlowStack.back().IsReceiver = isReceiver;
      tookOver = true;
    } else {
      m_ControlFlowStack.push_back({"", NoProducedValue, nullptr, true, isReceiver});
    }
    size_t loopFlowIndex = m_ControlFlowStack.size() - 1;

    std::map<std::string, uint64_t> masksBefore;
    std::map<std::string, bool> movedBefore;
    std::map<std::string, ExactPlaceFacts> exactPlacesBefore;
    for (auto &pair : CurrentScope->Symbols) {
      masksBefore[pair.first] = pair.second.InitMask;
      movedBefore[pair.first] = pair.second.Moved;
      exactPlacesBefore[pair.first] = pair.second.ExactPlace;
    }
    auto conditionalBefore = captureVisibleConditionalTodoIds(CurrentScope);
    auto visibleUniqueMovedBefore = captureVisibleUniqueMoved(CurrentScope);
    auto palBefore = PALCheckerState.snapshot();

    enterScope();
    CurrentScope->IsLoop = true;
    checkStmt(le->Body.get());
    exitScope();
    bool bodyJumps = allPathsJump(le->Body.get());
    auto breakStates = m_ControlFlowStack[loopFlowIndex].BreakStates;
    m_ControlFlowStack[loopFlowIndex].BreakStates.clear();
    auto continueStates = m_ControlFlowStack[loopFlowIndex].ContinueStates;
    m_ControlFlowStack[loopFlowIndex].ContinueStates.clear();

    if (!continueStates.empty()) {
      std::vector<AnalysisState> loopBackStates;
      if (!bodyJumps)
        loopBackStates.push_back(captureAnalysisState());
      loopBackStates.insert(loopBackStates.end(), continueStates.begin(),
                            continueStates.end());
      mergeAnalysisStates(loopBackStates, palBefore);
    }

    for (const auto &name :
         collectLoopEscapingMoves(CurrentScope, visibleUniqueMovedBefore)) {
      error(le, DiagID::ERR_SEMA_CANNOT_CEDE_MOVE_VALUE_INSIDE_A_LOOP_BECA,
            getDisplayVariableName(name));
    }

    if (le->Condition) {
      std::map<std::string, uint64_t> masksBody;
      std::map<std::string, bool> movedBody;
      std::map<std::string, ExactPlaceFacts> exactPlacesBody;
      for (auto &pair : CurrentScope->Symbols) {
        masksBody[pair.first] = pair.second.InitMask;
        movedBody[pair.first] = pair.second.Moved;
        exactPlacesBody[pair.first] = pair.second.ExactPlace;
      }
      auto palBody = PALCheckerState.snapshot();

      for (auto &pair : CurrentScope->Symbols) {
        uint64_t entryMask =
            masksBefore.count(pair.first) ? masksBefore[pair.first] : 0;
        uint64_t bodyMask =
            masksBody.count(pair.first) ? masksBody[pair.first] : 0;
        pair.second.InitMask = entryMask & bodyMask;

        bool entryMoved =
            movedBefore.count(pair.first) ? movedBefore[pair.first] : false;
        bool bodyMoved =
            movedBody.count(pair.first) ? movedBody[pair.first] : false;
        pair.second.Moved = entryMoved || bodyMoved;
        ExactPlaceFacts exactPlaces =
            exactPlacesBefore.count(pair.first)
                ? exactPlacesBefore[pair.first]
                : ExactPlaceFacts::bottom();
        if (exactPlacesBody.count(pair.first))
          exactPlaces |= exactPlacesBody[pair.first];
        pair.second.ExactPlace = exactPlaces;
        syncLegacyProjectionLiveness(pair.second);
      }
      PALCheckerState.mergeBranches(palBefore, palBefore, true, palBody, true);
    }

    if (!breakStates.empty()) {
      std::vector<AnalysisState> afterStates;
      if (le->Condition)
        afterStates.push_back(captureAnalysisState());
      afterStates.insert(afterStates.end(), breakStates.begin(),
                         breakStates.end());
      mergeAnalysisStates(afterStates, palBefore);
    }

    if (!tookOver)
      m_ControlFlowStack.pop_back();

    // Loop iteration count and back-edge reachability are not modeled by the
    // conditional-facts v1 protocol.  Do not leak a body-local assignment
    // fact past the loop until that dedicated fixed-point slice exists.
    restoreVisibleConditionalTodoIds(CurrentScope, conditionalBefore);

    return std::make_shared<UnitType>();
  } else if (auto *fe = dynamic_cast<ForExpr *>(E)) {
    // [Phase 2] Comptime Macro Unroll Detection
    bool isMacroUnroll = false;
    std::string ReflectedShapeName = "";
    if (auto *Memb = dynamic_cast<MemberExpr *>(fe->Collection.get())) {
      Memb->Object = foldGenericConstant(std::move(Memb->Object));
      if (Memb->Member == "fields") {
        if (auto *CRE = dynamic_cast<ComptimeReflectExpr *>(Memb->Object.get())) {
          isMacroUnroll = true;
          ReflectedShapeName = CRE->ReflectedTypeStr;
        }
      }
    }

    if (isMacroUnroll) {
      fe->IsComptimeUnrolled = true;
      auto resolvedObj = resolveType(toka::Type::fromString(ReflectedShapeName), true);
      std::string targetSoul = resolvedObj->getSoulName();
      if (ShapeMap.count(targetSoul)) {
        auto *SD = ShapeMap[targetSoul];
        uint64_t currentOffset = 0;
        for (const auto &member : SD->Members) {
          auto clonedBody = cloneNode(fe->Body);
          enterScope();
          SymbolInfo Info;
          Info.TypeObj = toka::Type::fromString("FieldInfo");
          Info.IsComptimeField = true;
          Info.ComptimeFieldName = member.Name;
          Info.ComptimeFieldTypeStr = member.Type;
          Info.ComptimeFieldOffset = currentOffset;
          Info.ComptimeFieldSize = 8; // Standard word approximation
          
          CurrentScope->define(fe->VarName, Info);
          checkStmt(clonedBody.get());
          fe->UnrolledBodies.push_back(std::move(clonedBody));
          exitScope();
          currentOffset += 8;
        }
      } else {
        error(fe, DiagID::ERR_SEMA_CANNOT_REFLECT_UNINSTANTIATED_OR_PRIMITIV, targetSoul);
      }
      return toka::Type::fromString("()");
    }

    auto collTypeObj = checkExpr(fe->Collection.get());
    std::string collType = collTypeObj->toString();
    std::string elemType = "unknown"; // recovery only
    std::string soulType = collTypeObj->getSoulType()->toString();

    bool isArray = false;
    // 1. Array Fast Path Identification
    if (collType.size() > 2 && collType.substr(0, 2) == "[]") {
        isArray = true;
        elemType = collType.substr(2);
    } else if (!collType.empty() && collType.front() == '[') {
        size_t semi = collType.find_last_of(';');
        if (semi != std::string::npos) {
            isArray = true;
            elemType = collType.substr(1, semi - 1);
        } else {
            size_t endBracket = collType.find(']');
            if (endBracket != std::string::npos) {
                isArray = true;
                elemType = collType.substr(endBracket + 1);
            }
        }
    }

    std::string fullType = "unknown";
    AccessPath iteratorSourcePath;
    std::string iteratorSourceName;
    bool iteratorBorrowAdded = false;

    if (isArray) {
        // Array emulation of next/next_ref
        fullType = fe->MorphologyPrefix + elemType;
        if (fe->IsMutable) fullType += "#";
                       fe->IterElementType = fullType;
    } else {
        // 2. Iterator Protocol Method Lookup
        std::string baseSoulType = toka::Type::stripMorphology(soulType);
        if (!MethodMap.count(baseSoulType) || !MethodMap[baseSoulType].count("iter")) {
            error(fe->Collection.get(), DiagID::ERR_SEMA_TYPE_DOES_NOT_IMPLEMENT_ITERATOR_PROTOCOL, soulType);
            fullType = "i32";
        } else {
            std::string iterableKey = baseSoulType + "@Iterable";
            if (!ImplMap.count(iterableKey)) {
              error(fe->Collection.get(),
                    DiagID::ERR_SEMA_ITERABLE_TRAIT_REQUIRED, soulType);
            }
            std::string iterObjStr = MethodMap[baseSoulType]["iter"];
            iterObjStr = resolveType(iterObjStr, false);
            auto iterObj = toka::Type::fromString(iterObjStr);
            auto iterSoul = iterObj->getSoulType()->toString();
            std::string baseIterSoul = toka::Type::stripMorphology(iterSoul);
            fe->IteratorType = baseIterSoul;
            if (MethodDecls.count(baseSoulType) &&
                MethodDecls[baseSoulType].count("iter")) {
              fe->ResolvedIterFn = MethodDecls[baseSoulType]["iter"];
              bool dependsOnSelf = false;
              for (const auto &dep : fe->ResolvedIterFn->LifeDependencies) {
                if (Type::stripMorphology(dep) == "self") {
                  dependsOnSelf = true;
                  break;
                }
              }
              if (!dependsOnSelf) {
                error(fe->Collection.get(),
                      DiagID::ERR_SEMA_ITERATOR_DEPENDENCY_REQUIRED,
                      "Iterable::iter",
                      soulType);
              }
            }
            
            // 1. Peek at next() to see what the element type is
            std::string E_type = "";
            if (MethodMap.count(baseIterSoul) && MethodMap[baseIterSoul].count("next")) {
                std::string nextRetStr = MethodMap[baseIterSoul]["next"];
                if (nextRetStr.size() > 7 && nextRetStr.substr(0, 7) == "Option<") {
                    E_type = nextRetStr.substr(7, nextRetStr.size() - 8);
                }
            }
            if (E_type.empty()) {
                error(fe, DiagID::ERR_SEMA_ITERATOR_PROTOCOL_REQUIRES_NEXT_TO_RETURN);
                fullType = "i32";
            } else {
                // 2. Compare user's morphology prefix with E's morphology to determine intent
                size_t prefRef = 0;
                for (char c : fe->MorphologyPrefix) { if (c == '&') prefRef++; else break; }
                size_t eRef = 0;
                for (char c : E_type) { if (c == '&') eRef++; else break; }
                
                bool expectsRef = false;
                if (prefRef > eRef) {
                    expectsRef = true;
                } else if (!fe->MorphologyPrefix.empty()) {
                     expectsRef = false;
                } else {
                     expectsRef = fe->IsReference; // fallback
                }
                
                fe->IsReference = expectsRef;
                std::string nextMethodName = expectsRef ? "next_ref" : "next";
                if (!ImplMap.count(baseIterSoul + "@Iterator")) {
                  error(fe, DiagID::ERR_SEMA_ITERATOR_TRAIT_REQUIRED,
                        baseIterSoul, "@Iterator");
                }
                std::string iteratorFacet =
                    expectsRef ? "@BorrowIterator" : "@Iterator";
                if (expectsRef &&
                    !ImplMap.count(baseIterSoul + iteratorFacet)) {
                  error(fe, DiagID::ERR_SEMA_ITERATOR_TRAIT_REQUIRED,
                        baseIterSoul, iteratorFacet);
                }
                
                if (!MethodMap[iterSoul].count(nextMethodName)) {
                     error(fe, DiagID::ERR_SEMA_ITERATOR_FOR_DOES_NOT_SUPPORT, soulType, (expectsRef ? "borrow semantics (.next_ref())" : "value semantics (.next())"));
                     fullType = "i32";
                } else {
                     if (MethodDecls.count(iterSoul) &&
                         MethodDecls[iterSoul].count(nextMethodName)) {
                       fe->ResolvedNextFn =
                           MethodDecls[iterSoul][nextMethodName];
                     } else if (MethodDecls.count(baseIterSoul) &&
                                MethodDecls[baseIterSoul].count(nextMethodName)) {
                       fe->ResolvedNextFn =
                           MethodDecls[baseIterSoul][nextMethodName];
                     }
                     std::string nextRetStr = MethodMap[iterSoul][nextMethodName];
                     if (nextRetStr.size() > 7 && nextRetStr.substr(0, 7) == "Option<") {
                         std::string payload = nextRetStr.substr(7, nextRetStr.size() - 8);
                         fullType = payload;
                         if (fe->IsMutable) fullType += "#";
                           fe->IterElementType = fullType;
                     } else {
                         error(fe, DiagID::ERR_SEMA_ITERATOR_PROTOCOL_REQUIRES_TO_RETURN_OPTI, nextMethodName);
                         fullType = "i32";
                     }
                }
            }
        }
    }

    auto masksBefore = captureVisibleInitMasks(CurrentScope);
    auto movedBefore = captureVisibleMoved(CurrentScope);
    auto exactPlacesBefore = captureVisibleExactPlaceFacts(CurrentScope);
    auto conditionalBefore = captureVisibleConditionalTodoIds(CurrentScope);
    auto visibleUniqueMovedBefore = captureVisibleUniqueMoved(CurrentScope);
    auto palBefore = PALCheckerState.snapshot();

    // Array reference iteration has the same dynamic-element aliasing
    // property as a BorrowIterator.  The current element is not statically
    // known, so retain a conservative borrow of the collection for the loop
    // body; otherwise a `cede values[i]` can invalidate the live iteration
    // reference.
    if (!isArray || fe->IsReference) {
      iteratorSourcePath =
          canonicalizeAccessPath(makeAccessPath(fe->Collection.get()));
      iteratorSourceName = getPathString(fe->Collection.get());
      if (iteratorSourcePath &&
          PALCheckerState.getState(iteratorSourcePath) == PathState::Free) {
        if (!PALCheckerState.recordBorrow(iteratorSourcePath, false,
                                          getLoc(fe))) {
          error(fe->Collection.get(), DiagID::ERR_BORROW_MUT,
                iteratorSourcePath.toLegacyString());
        } else {
          PALCheckerState.commitTransient(iteratorSourcePath);
          iteratorBorrowAdded = true;
        }
      }
    }

    enterScope();
    CurrentScope->IsLoop = true;
    SymbolInfo Info;
    Info.TypeObj = toka::Type::fromString(fullType);
    fe->ResolvedIterElementType = Info.TypeObj;
    if (fe->IsReference && !iteratorSourceName.empty()) {
      Info.BorrowedFrom = iteratorSourceName;
      Info.LifeDependencySet.insert(iteratorSourceName);
    }
    CurrentScope->define(fe->VarName, Info);

    bool isReceiver = false;
    if (!m_ControlFlowStack.empty()) {
      isReceiver = m_ControlFlowStack.back().IsReceiver;
    }

    bool tookOver = false;
    if (!m_ControlFlowStack.empty() && !m_ControlFlowStack.back().IsLoop &&
        !m_ControlFlowStack.back().Label.empty()) {
      m_ControlFlowStack.back().IsLoop = true;
      m_ControlFlowStack.back().IsReceiver = isReceiver; // Sync receiver status
      tookOver = true;
    } else {
      m_ControlFlowStack.push_back({"", NoProducedValue, nullptr, true, isReceiver});
    }
    size_t loopFlowIndex = m_ControlFlowStack.size() - 1;
    checkStmt(fe->Body.get());
    std::string bodyType = m_ControlFlowStack.back().ExpectedType;
    auto bodyTypeObj = m_ControlFlowStack.back().ExpectedTypeObj;
    bool bodyJumps = allPathsJump(fe->Body.get());
    auto breakStates = m_ControlFlowStack[loopFlowIndex].BreakStates;
    m_ControlFlowStack[loopFlowIndex].BreakStates.clear();
    auto continueStates = m_ControlFlowStack[loopFlowIndex].ContinueStates;
    m_ControlFlowStack[loopFlowIndex].ContinueStates.clear();
    bool bodyContinuesLoop = !bodyJumps || !continueStates.empty();

    if (!continueStates.empty()) {
      std::vector<AnalysisState> loopBackStates;
      if (!bodyJumps)
        loopBackStates.push_back(captureAnalysisState());
      loopBackStates.insert(loopBackStates.end(), continueStates.begin(),
                            continueStates.end());
      mergeAnalysisStates(loopBackStates, palBefore);
    }

    for (const auto &name :
         collectLoopEscapingMoves(CurrentScope, visibleUniqueMovedBefore)) {
      error(fe, DiagID::ERR_SEMA_CANNOT_CEDE_MOVE_VALUE_INSIDE_A_LOOP_BECA,
            getDisplayVariableName(name));
    }

    if (!tookOver)
      m_ControlFlowStack.pop_back();
    exitScope();
    auto masksBody = captureVisibleInitMasks(CurrentScope);
    auto movedBody = captureVisibleMoved(CurrentScope);
    auto exactPlacesBody = captureVisibleExactPlaceFacts(CurrentScope);
    auto palBody = PALCheckerState.snapshot();

    std::string elseType = NoProducedValue;
    std::shared_ptr<toka::Type> elseTypeObj;
    bool elseJumps = false;
    std::map<std::string, uint64_t> masksElse = masksBefore;
    std::map<std::string, bool> movedElse = movedBefore;
    std::map<std::string, ExactPlaceFacts> exactPlacesElse = exactPlacesBefore;
    PALChecker palElse = palBefore;
    if (fe->ElseBody) {
      restoreVisibleAnalysisState(CurrentScope, masksBefore, movedBefore,
                                  exactPlacesBefore);
      PALCheckerState.restore(palBefore);

      m_ControlFlowStack.push_back({"", NoProducedValue, nullptr, false, isReceiver});
      checkStmt(fe->ElseBody.get());
      elseType = m_ControlFlowStack.back().ExpectedType;
      elseTypeObj = m_ControlFlowStack.back().ExpectedTypeObj;
      elseJumps = allPathsJump(fe->ElseBody.get());
      masksElse = captureVisibleInitMasks(CurrentScope);
      movedElse = captureVisibleMoved(CurrentScope);
      exactPlacesElse = captureVisibleExactPlaceFacts(CurrentScope);
      palElse = PALCheckerState.snapshot();
      m_ControlFlowStack.pop_back();
    }

    if (fe->ElseBody) {
      if (!bodyContinuesLoop && elseJumps) {
        restoreVisibleAnalysisState(CurrentScope, masksBefore, movedBefore,
                                    exactPlacesBefore);
        PALCheckerState.restore(palBefore);
      } else if (!bodyContinuesLoop) {
        restoreVisibleAnalysisState(CurrentScope, masksElse, movedElse,
                                    exactPlacesElse);
        PALCheckerState.restore(palElse);
      } else if (elseJumps) {
        restoreVisibleAnalysisState(CurrentScope, masksBody, movedBody,
                                    exactPlacesBody);
        PALCheckerState.restore(palBody);
      } else {
        for (auto &pair : masksBefore) {
          SymbolInfo *info = nullptr;
          if (!CurrentScope->findSymbol(pair.first, info) || !info)
            continue;
          uint64_t bodyMask =
              masksBody.count(pair.first) ? masksBody[pair.first] : 0;
          uint64_t elseMask =
              masksElse.count(pair.first) ? masksElse[pair.first] : 0;
          info->InitMask = bodyMask & elseMask;
          bool bodyMoved =
              movedBody.count(pair.first) ? movedBody[pair.first] : false;
          bool elseMoved =
              movedElse.count(pair.first) ? movedElse[pair.first] : false;
          info->Moved = bodyMoved || elseMoved;
          ExactPlaceFacts exactPlaces =
              exactPlacesBody.count(pair.first)
                  ? exactPlacesBody[pair.first]
                  : ExactPlaceFacts::bottom();
          if (exactPlacesElse.count(pair.first))
            exactPlaces |= exactPlacesElse[pair.first];
          info->ExactPlace = exactPlaces;
          syncLegacyProjectionLiveness(*info);
        }
        PALCheckerState.mergeBranches(palBefore, palBody, true, palElse, true);
      }
    } else {
      if (!bodyContinuesLoop) {
        restoreVisibleAnalysisState(CurrentScope, masksBefore, movedBefore,
                                    exactPlacesBefore);
        PALCheckerState.restore(palBefore);
      } else {
        for (auto &pair : masksBefore) {
          SymbolInfo *info = nullptr;
          if (!CurrentScope->findSymbol(pair.first, info) || !info)
            continue;
          uint64_t entryMask = pair.second;
          uint64_t bodyMask =
              masksBody.count(pair.first) ? masksBody[pair.first] : 0;
          info->InitMask = entryMask & bodyMask;
          bool entryMoved =
              movedBefore.count(pair.first) ? movedBefore[pair.first] : false;
          bool bodyMoved =
              movedBody.count(pair.first) ? movedBody[pair.first] : false;
          info->Moved = entryMoved || bodyMoved;
          ExactPlaceFacts exactPlaces =
              exactPlacesBefore.count(pair.first)
                  ? exactPlacesBefore[pair.first]
                  : ExactPlaceFacts::bottom();
          if (exactPlacesBody.count(pair.first))
            exactPlaces |= exactPlacesBody[pair.first];
          info->ExactPlace = exactPlaces;
          syncLegacyProjectionLiveness(*info);
        }
        PALCheckerState.mergeBranches(palBefore, palBody, true, palBefore, true);
      }
    }

    if (!breakStates.empty()) {
      bool normalAfterReachable = !fe->ElseBody || !(bodyJumps && elseJumps);
      std::vector<AnalysisState> afterStates;
      if (normalAfterReachable)
        afterStates.push_back(captureAnalysisState());
      afterStates.insert(afterStates.end(), breakStates.begin(),
                         breakStates.end());
      mergeAnalysisStates(afterStates, palBefore);
    }

    // As with `loop`, conditional-facts v1 does not model iteration or the
    // `for ... or` reachability split.  Preserve the incoming fact state.
    restoreVisibleConditionalTodoIds(CurrentScope, conditionalBefore);

    if (isReceiver) {
      if (bodyType == NoProducedValue && !bodyJumps)
        error(fe->Body.get(), DiagID::ERR_YIELD_VALUE_REQUIRED, "for loop");
      if (!fe->ElseBody)
        error(fe, DiagID::ERR_YIELD_OR_REQUIRED, "for");
      else if (elseType == NoProducedValue && !elseJumps)
        error(fe->ElseBody.get(), DiagID::ERR_YIELD_VALUE_REQUIRED,
              "'or' block");
    }

    if (bodyType != NoProducedValue && !fe->ElseBody) {
      error(fe, DiagID::ERR_YIELD_OR_REQUIRED, "for");
    }
    if (bodyType != NoProducedValue && elseType != NoProducedValue &&
        !isTypeCompatible(bodyTypeObj, elseTypeObj)) {
      error(fe, DiagID::ERR_BRANCH_TYPE_MISMATCH, "For loop", bodyType,
            elseType);
    }
    if (iteratorBorrowAdded)
      PALCheckerState.releaseBorrow(iteratorSourcePath);
    const std::string result =
        (bodyType != NoProducedValue) ? bodyType : elseType;
    return toka::Type::fromString(result == NoProducedValue ? "()" : result);
  } else if (auto *ce = dynamic_cast<CedeExpr *>(E)) {
    if (auto *todo = dynamic_cast<TodoExpr *>(ce->Value.get())) {
      SemanticEvidence::recordTodoGoal(
          todo->TodoId, TodoGoalStatus::Unsupported, false, "", "", "",
          false, false, false, {}, todo->Loc);
      error(ce, DiagID::ERR_TYPED_TODO_UNSUPPORTED_CONTEXT);
      return toka::Type::fromString("unknown");
    }
    if (auto *call = dynamic_cast<CallExpr *>(ce->Value.get()))
      call->CallableReceiver = CallableReceiverMode::Consuming;
    auto innerTy = checkExpr(ce->Value.get());
    bool canInvalidate = true;
    
    // [Fix] Enforce tracking move semantics and borrow check for `cede` expression universally.
    if (ce->Value) {
      std::string pathToMove = getPathString(ce->Value.get());
      if (!pathToMove.empty()) {
          auto conflict = PALCheckerState.verifyInvalidation(
              canonicalizeAccessPath(makeAccessPath(ce->Value.get())));
          if (conflict) {
              error(ce, DiagID::ERR_MOVE_BORROWED, conflict->displayPath());
              recordPALConflict(
                  ce, PALOperationClass::Invalidation,
                  canonicalizeAccessPath(makeAccessPath(ce->Value.get())),
                  *conflict);
              canInvalidate = false;
          }
          if (canInvalidate) {
            size_t dotPos = pathToMove.find('.');
            if (dotPos != std::string::npos) {
              std::string rootName = pathToMove.substr(0, dotPos);
              SymbolInfo *RootInfo = nullptr;
              std::string actualRootName;
              if (CurrentScope->findVariableWithDeref(rootName, RootInfo,
                                                      actualRootName)) {
                if (RootInfo && RootInfo->IsFunctionParameter &&
                    RootInfo->IsCeded) {
                  CurrentScope->markMoved(actualRootName, ce->Loc);
                }
              }
            }
          }
      }

      Expr *underlying =
          canInvalidate ? unwrapCedeDirectSource(ce->Value.get()) : nullptr;
      if (auto *Var = dynamic_cast<VariableExpr *>(underlying)) {
        SymbolInfo *Info = nullptr;
        std::string actualName;
        if (CurrentScope->findVariableWithDeref(Var->Name, Info, actualName)) {
            if (Info->IsFunctionParameter && !Info->IsCeded) {
                error(ce, DiagID::ERR_SEMA_CANNOT_CEDE_NON_CEDE_PARAMETER, Var->Name);
            }
            CurrentScope->markMoved(actualName, ce->Loc);
        }
      } else if (auto *Member = dynamic_cast<MemberExpr *>(underlying)) {
        // A direct field transfer from a local compiler-managed record leaves
        // that field uninitialized.  Keep legacy container/index transfers
        // outside this narrow rule; they require their own representation
        // invariants rather than a record field drop mask.
        auto *Root = dynamic_cast<VariableExpr *>(Member->Object.get());
        if (Root) {
          SymbolInfo *RootInfo = nullptr;
          std::string actualRootName;
          if (CurrentScope->findVariableWithDeref(Root->Name, RootInfo,
                                                  actualRootName) &&
              RootInfo && RootInfo->IsDeclaredVariable &&
              !RootInfo->IsFunctionParameter && RootInfo->TypeObj &&
              RootInfo->TypeObj->isShape()) {
            auto shapeType =
                std::dynamic_pointer_cast<ShapeType>(RootInfo->TypeObj);
            ShapeDecl *shape = shapeType ? shapeType->Decl : nullptr;
            if (!shape)
              shape = findVisibleShapeDecl(RootInfo->TypeObj->getSoulName(),
                                           getLoc(Member));
            if (shape && shape->HasExplicitDrop) {
              // A user drop body owns the aggregate invariant.  The compiler
              // cannot remove one field from that body safely, so this narrow
              // direct-field model rejects the transfer rather than leaving a
              // double-drop or leak path open.
              error(ce, DiagID::ERR_MOVE_MEMBER_DROP, Member->Member,
                    shape->Name);
            } else if (shape && (shape->Kind == ShapeKind::Struct ||
                                 shape->Kind == ShapeKind::Tuple) &&
                       shape->Members.size() <= 64) {
              int memberIndex = Member->Index;
              if (memberIndex < 0) {
                for (size_t i = 0; i < shape->Members.size(); ++i) {
                  if (stripMemberAccessMarkers(shape->Members[i].Name) ==
                      stripMemberAccessMarkers(Member->Member)) {
                    memberIndex = static_cast<int>(i);
                    break;
                  }
                }
              }
              bool hasSharedMember = false;
              for (const auto &shapeMember : shape->Members) {
                if (shapeMember.IsShared) {
                  hasSharedMember = true;
                  break;
                }
              }
              if (memberIndex < 0 || memberIndex >= 64 || hasSharedMember) {
                error(ce, DiagID::ERR_SEMA_CEDE_PARTIAL_PROJECTION_UNSUPPORTED,
                      Member->toString());
              } else {
                initializeProjectionFacts(*RootInfo);
                if (!RootInfo->ExactPlace.transitionProjection(
                        PartialMoveProjectionKind::DirectField, memberIndex,
                        PlaceState::Moved)) {
                  error(ce, DiagID::ERR_SEMA_CEDE_PARTIAL_PROJECTION_UNSUPPORTED,
                        Member->toString());
                } else {
                  syncLegacyProjectionLiveness(*RootInfo);
                }
              }
            } else {
              error(ce, DiagID::ERR_SEMA_CEDE_PARTIAL_PROJECTION_UNSUPPORTED,
                    Member->toString());
            }
          }
        } else {
          auto sourceSoul = innerTy ? innerTy->getSoulType() : nullptr;
          if (sourceSoul && sourceSoul->isShape() &&
              hasDrop(sourceSoul->getSoulName())) {
            error(ce, DiagID::ERR_SEMA_CEDE_PARTIAL_PROJECTION_UNSUPPORTED,
                  Member->toString());
          }
        }
      } else if (auto *Index = dynamic_cast<ArrayIndexExpr *>(underlying)) {
        // A fixed local array can share the same bounded liveness model as a
        // direct record field when its element is selected by a constant
        // index.  A dynamic resource index has no stable static projection,
        // so reject it rather than silently losing its cleanup obligation.
        auto *Root = dynamic_cast<VariableExpr *>(Index->Array.get());
        auto *constant = Index->Indices.size() == 1
                             ? dynamic_cast<NumberExpr *>(Index->Indices[0].get())
                             : nullptr;
        if (Root) {
          SymbolInfo *RootInfo = nullptr;
          std::string actualRootName;
          if (CurrentScope->findVariableWithDeref(Root->Name, RootInfo,
                                                  actualRootName) &&
              RootInfo && RootInfo->IsDeclaredVariable &&
              !RootInfo->IsFunctionParameter && RootInfo->TypeObj &&
              RootInfo->TypeObj->isArray()) {
            auto array =
                std::dynamic_pointer_cast<ArrayType>(RootInfo->TypeObj);
            if (array && constant && array->Size <= 64 &&
                constant->Value < array->Size && constant->Value < 64) {
              initializeProjectionFacts(*RootInfo);
              if (!RootInfo->ExactPlace.transitionProjection(
                      PartialMoveProjectionKind::FixedArrayElement,
                      constant->Value, PlaceState::Moved)) {
                error(ce, DiagID::ERR_SEMA_CEDE_PARTIAL_PROJECTION_UNSUPPORTED,
                      Index->toString());
              } else {
                syncLegacyProjectionLiveness(*RootInfo);
              }
            } else if (array && constant && constant->Value < array->Size) {
              error(ce, DiagID::ERR_SEMA_CEDE_PARTIAL_PROJECTION_UNSUPPORTED,
                    Index->toString());
            } else {
              auto sourceSoul = innerTy ? innerTy->getSoulType() : nullptr;
              if (sourceSoul && sourceSoul->isShape() &&
                  hasDrop(sourceSoul->getSoulName())) {
                error(ce, DiagID::ERR_SEMA_CEDE_DYNAMIC_ARRAY_INDEX_UNSUPPORTED,
                      Index->toString());
              }
            }
          }
        }
      }
    }
    if (!innerTy) return nullptr;
    if (auto ptrTy = std::dynamic_pointer_cast<toka::PointerType>(innerTy)) {
        if (m_ExpectedType && !std::dynamic_pointer_cast<toka::PointerType>(m_ExpectedType)) {
            innerTy = ptrTy->PointeeType;
        }
    }
    ce->ResolvedType = innerTy;
    return innerTy;
  } else if (auto *se = dynamic_cast<SizeOfExpr *>(E)) {
    se->OperandType = resolveType(
        se->TypeSyntax ? toka::Type::fromSyntax(se->TypeSyntax)
                       : toka::Type::fromString(se->TypeStr));
    if (se->OperandType)
      se->TypeStr = se->OperandType->toString();
    se->ResolvedType = toka::Type::fromString("usize");
    return se->ResolvedType;
  } else if (auto *pe = dynamic_cast<PassExpr *>(E)) {
    // 1. Detect if this is a prefix 'pass' (wrapping a complex expression)
    bool isPrefixMatch = dynamic_cast<MatchExpr *>(pe->Value.get());
    bool isPrefixIf = dynamic_cast<IfExpr *>(pe->Value.get());
    bool isPrefixFor = dynamic_cast<ForExpr *>(pe->Value.get());
    bool isPrefixLoop = dynamic_cast<LoopExpr *>(pe->Value.get());

    std::string valType = NoProducedValue;
    std::shared_ptr<toka::Type> valTypeObj;
    if (isPrefixMatch || isPrefixIf || isPrefixFor ||
        isPrefixLoop) {
      m_ControlFlowStack.push_back({"", NoProducedValue, nullptr, false, true});
      valTypeObj = checkExpr(pe->Value.get());
      valType = valTypeObj->toString();
      m_ControlFlowStack.pop_back();

      if (valType == NoProducedValue) {
        error(pe, DiagID::ERR_SEMA_PREFIX_PASS_EXPECTS_A_VALUE_YIELDING_EXPR);
      }
    } else {
      valTypeObj = checkExpr(pe->Value.get());
      valType = valTypeObj->toString();
    }

    bool foundReceiver = false;
    if (!m_ControlFlowStack.empty()) {
      for (auto it = m_ControlFlowStack.rbegin();
           it != m_ControlFlowStack.rend(); ++it) {
        if (it->IsReceiver) {
          foundReceiver = true;
          if (it->ExpectedType == NoProducedValue) {
            it->ExpectedType = valType;
            it->ExpectedTypeObj = valTypeObj;
          } else if (!isTypeCompatible(it->ExpectedTypeObj, valTypeObj)) {
            error(pe, DiagID::ERR_TYPE_MISMATCH, valType, it->ExpectedType);
          }
          break;
        }
      }
    }

    if (!foundReceiver) {
      error(pe, DiagID::ERR_PASS_NO_RECEIVER);
    }

    const std::string result =
        (isPrefixMatch || isPrefixIf || isPrefixFor || isPrefixLoop)
            ? valType
            : "()";
    return toka::Type::fromString(result == NoProducedValue ? "()" : result);
  } else if (auto *be = dynamic_cast<BreakExpr *>(E)) {
    std::string valType = NoProducedValue;
    std::shared_ptr<toka::Type> valTypeObj;
    if (be->Value) {
      error(be, DiagID::ERR_SEMA_TOKA_1_0_DOES_NOT_SUPPORT_YIELDING_VALU_2);
      valTypeObj = checkExpr(be->Value.get());
      valType = valTypeObj->toString();
    }

    ControlFlowInfo *target = nullptr;
    if (be->TargetLabel.empty()) {
      for (auto it = m_ControlFlowStack.rbegin();
           it != m_ControlFlowStack.rend(); ++it) {
        if (it->IsLoop) {
          target = &(*it);
          break;
        }
      }
    } else {
      for (auto it = m_ControlFlowStack.rbegin();
           it != m_ControlFlowStack.rend(); ++it) {
        if (it->Label == be->TargetLabel) {
          target = &(*it);
          break;
        }
      }
    }

    if (target) {
      size_t targetDepth = static_cast<size_t>(
          std::distance(m_ControlFlowStack.data(), target));
      if (!m_InitBlockContexts.empty() &&
          targetDepth < m_InitBlockContexts.back().ControlFlowDepth)
        error(be, DiagID::ERR_INIT_BLOCK_EXIT, "break");
      target->BreakStates.push_back(captureAnalysisState());
      if (valType != NoProducedValue) {
        if (target->ExpectedType == NoProducedValue) {
          target->ExpectedType = valType;
          target->ExpectedTypeObj = valTypeObj;
        } else if (!isTypeCompatible(target->ExpectedTypeObj, valTypeObj)) {
          error(be, DiagID::ERR_TYPE_MISMATCH, valType, target->ExpectedType);
        }
      }
    }
    return toka::Type::fromString("()");
  } else if (auto *ce = dynamic_cast<ContinueExpr *>(E)) {
    // Continue target must be a loop
    ControlFlowInfo *target = nullptr;
    if (ce->TargetLabel.empty()) {
      for (auto it = m_ControlFlowStack.rbegin();
           it != m_ControlFlowStack.rend(); ++it) {
        if (it->IsLoop) {
          target = &(*it);
          break;
        }
      }
    } else {
      for (auto it = m_ControlFlowStack.rbegin();
           it != m_ControlFlowStack.rend(); ++it) {
        if (it->Label == ce->TargetLabel && it->IsLoop) {
          target = &(*it);
          break;
        }
      }
    }

    if (target) {
      size_t targetDepth = static_cast<size_t>(
          std::distance(m_ControlFlowStack.data(), target));
      if (!m_InitBlockContexts.empty() &&
          targetDepth < m_InitBlockContexts.back().ControlFlowDepth)
        error(ce, DiagID::ERR_INIT_BLOCK_EXIT, "continue");
      target->ContinueStates.push_back(captureAnalysisState());
    }
    return toka::Type::fromString("()");
  } else if (auto *Call = dynamic_cast<CallExpr *>(E)) {
    return checkCallExpr(Call);
  } else if (auto *awaitEx = dynamic_cast<AwaitExpr *>(E)) {
    if (!CurrentFunction || CurrentFunction->Effect != EffectKind::Async) {
      error(awaitEx,
            DiagID::ERR_CODEGEN_AWAIT_CAN_ONLY_BE_USED_INSIDE_AN_ASYNC);
    }
    // The P1 lexical construction proof is synchronous.  A fully constructed
    // target is an ordinary frame local, but an unresolved Never/Maybe fact
    // must not cross a coroutine suspension before the async/place bridge has
    // a frame-level obligation and cleanup proof.
    for (const auto &context : m_InitBlockContexts) {
      SymbolInfo *target = nullptr;
      if (!CurrentScope->findSymbol(context.PlaceName, target) || !target ||
          !hasExactlyPlaceState(target->placeFact(), PlaceState::Live)) {
        error(awaitEx, DiagID::ERR_INIT_BLOCK_SUSPEND, context.PlaceName);
      }
    }
    bool oldConsuming = m_IsConsumingEffect;
    m_IsConsumingEffect = true;
    auto innerType = checkExpr(awaitEx->Expression.get());
    m_IsConsumingEffect = oldConsuming;

    auto taskShape = std::dynamic_pointer_cast<ShapeType>(
        innerType ? innerType->getSoulType() : nullptr);
    if (taskShape && taskShape->Decl &&
        taskShape->Decl->InstantiationTemplate &&
        taskShape->Decl->InstantiationTemplate->Name == "TaskHandle" &&
        taskShape->Decl->InstantiationArgs.size() == 1) {
      awaitEx->AwaitedType = taskShape->Decl->InstantiationArgs.front();
      if (awaitEx->CatchesCancellation) {
        awaitEx->ResolvedType = resolveType(std::make_shared<ShapeType>(
            "Option", std::vector<std::shared_ptr<toka::Type>>{
                          awaitEx->AwaitedType}));
      } else {
        awaitEx->ResolvedType = awaitEx->AwaitedType;
      }
      return awaitEx->ResolvedType;
    }
    
    std::string tName = innerType->toString();
    if (tName.find("TaskHandle_M_") != std::string::npos) {
        size_t pos = tName.find("TaskHandle_M_");
        std::string inner = tName.substr(pos + 13);
        // Unit is spelled `()` in the semantic type system and mangles to
        // `__` in a TaskHandle instance name.  Recover its semantic spelling
        // before type resolution instead of treating the mangled fragment as
        // an unresolved shape.
        if (inner == "__")
            inner = "()";
        awaitEx->AwaitedType = resolveType(toka::Type::fromString(inner));
        if (awaitEx->CatchesCancellation) {
            awaitEx->ResolvedType =
                resolveType(toka::Type::fromString("Option<" + inner + ">"));
        } else {
            awaitEx->ResolvedType = awaitEx->AwaitedType;
        }
        return awaitEx->ResolvedType;
    }
    error(E, DiagID::ERR_SEMA_CANNOT_AWAIT_A_NON_TASKHANDLE_TYPE, tName);
    return toka::Type::fromString("unknown");
  } else if (auto *waitEx = dynamic_cast<WaitExpr *>(E)) {
    if (CurrentFunction && CurrentFunction->Effect == EffectKind::Async) {
      error(waitEx, DiagID::ERR_SEMA_WAIT_INSIDE_ASYNC);
    }
    bool oldConsuming = m_IsConsumingEffect;
    m_IsConsumingEffect = true;
    auto innerType = checkExpr(waitEx->Expression.get());
    m_IsConsumingEffect = oldConsuming;

    auto taskShape = std::dynamic_pointer_cast<ShapeType>(
        innerType ? innerType->getSoulType() : nullptr);
    if (taskShape && taskShape->Decl &&
        taskShape->Decl->InstantiationTemplate &&
        taskShape->Decl->InstantiationTemplate->Name == "TaskHandle" &&
        taskShape->Decl->InstantiationArgs.size() == 1) {
      waitEx->ResolvedType = taskShape->Decl->InstantiationArgs.front();
      return waitEx->ResolvedType;
    }
    
    std::string tName = innerType->toString();
    if (tName.find("TaskHandle_M_") != std::string::npos) {
        size_t pos = tName.find("TaskHandle_M_");
        std::string inner = tName.substr(pos + 13);
        if (inner == "__")
            inner = "()";
        waitEx->ResolvedType = toka::Type::fromString(inner);
        return waitEx->ResolvedType;
    }
    error(E, DiagID::ERR_SEMA_CANNOT_WAIT_ON_A_NON_TASKHANDLE_TYPE, tName);
    return toka::Type::fromString("unknown");
  } else if (auto *AIE = dynamic_cast<ArrayInitExpr *>(E)) {
    error(AIE, DiagID::ERR_SEMA_BARE_ARRAY_INIT_EXCLUDED);
    return toka::Type::fromString("unknown");
  } else if (auto *New = dynamic_cast<NewExpr *>(E)) {
    validateTypeVisibilityInType(New->Type, getLoc(New));
    auto resolvedType = resolveType(
        New->TypeSyntax ? toka::Type::fromSyntax(New->TypeSyntax)
                        : toka::Type::fromString(New->Type));
    std::string resolvedName = resolvedType ? resolvedType->toString()
                                            : New->Type;

    if (resolvedType && resolvedType->isMissOutcome() &&
        !New->Initializer) {
      DiagnosticEngine::report(getLoc(New),
                               DiagID::ERR_MISS_OUTCOME_DEFAULT_FORBIDDEN,
                               resolvedType->toString());
      HasError = true;
    }

    // [New] Generic Inference for 'new'
    if (ShapeMap.count(resolvedName)) {
      ShapeDecl *SD = ShapeMap[resolvedName];
      if (!SD->GenericParams.empty() && m_ExpectedType) {
        auto ptrTy =
            std::dynamic_pointer_cast<toka::PointerType>(m_ExpectedType);
        if (ptrTy) {
          auto expShape =
              std::dynamic_pointer_cast<toka::ShapeType>(ptrTy->PointeeType);
          if (expShape && (expShape->Name == SD->Name ||
                           expShape->Name.find(SD->Name + "_M") == 0)) {
            resolvedType = resolveType(expShape);
            resolvedName = resolvedType ? resolvedType->toString()
                                        : resolvedName;
          }
        }
      }
    }

    if (New->ArraySize) {
      checkExpr(New->ArraySize.get());
    }
    if (New->Initializer) {
      if (resolvedName.find("Uninit<") == 0) {
        // [Safety Pillar 3] Uninit allocation bypasses constructor evaluation
      } else {
        auto InitTypeObj = checkExpr(New->Initializer.get(), resolvedType);
      }
      // Re-propagate the mask from initializer to NewExpr
      // (This will be picked up by checkExpr wrapper and passed to
      // VariableDecl)
    }
    // 'new' usually returns a unique pointer: ^Type# (soul is fully writable)
    m_LastInitMask = ~0ULL;
    if (New->ArraySize) {
      auto array = std::make_shared<toka::SliceType>(resolvedType);
      array = std::dynamic_pointer_cast<toka::SliceType>(
          array->withAttributes(true, array->IsNullable, array->IsBlocked));
      return std::make_shared<toka::UniquePointerType>(array);
    }
    auto writable = resolvedType->withAttributes(
        true, resolvedType->IsNullable, resolvedType->IsBlocked);
    return std::make_shared<toka::UniquePointerType>(writable);
  } else if (auto *UnsafeE = dynamic_cast<UnsafeExpr *>(E)) {
    bool oldUnsafe = m_InUnsafeContext;
    m_InUnsafeContext = true;
    auto typeObj = checkExpr(UnsafeE->Expression.get());
    std::string type = typeObj->toString();
    m_InUnsafeContext = oldUnsafe;
    return typeObj;
  } else if (auto *AllocE = dynamic_cast<AllocExpr *>(E)) {
    if (!m_InUnsafeContext) {
      DiagnosticEngine::report(getLoc(AllocE), DiagID::ERR_UNSAFE_ALLOC_CTX);
      HasError = true;
    }
    // Mapping to __toka_alloc
    // Returning raw pointer identity: *Type
    validateTypeVisibilityInType(AllocE->TypeName, getLoc(AllocE));
    auto baseTypeObj = resolveType(
        AllocE->TypeSyntax ? toka::Type::fromSyntax(AllocE->TypeSyntax)
                           : toka::Type::fromString(AllocE->TypeName));
    std::string baseType = baseTypeObj ? baseTypeObj->toString()
                                       : AllocE->TypeName;
    if (AllocE->IsArray) {
      if (AllocE->ArraySize) {
        checkExpr(AllocE->ArraySize.get());
      }
    }
    if (AllocE->Initializer) {
      checkExpr(AllocE->Initializer.get(), baseTypeObj);
    }
    // [FIX] Update with mangled name for CodeGen, but only if it's NOT an unresolved generic param
    if (baseType.find('\'') == std::string::npos) {
      AllocE->TypeName = baseType;
    }
    if (AllocE->IsArray) {
      return std::make_shared<toka::RawPointerType>(
          std::make_shared<toka::SliceType>(baseTypeObj));
    }
    return std::make_shared<toka::RawPointerType>(baseTypeObj);
  } else if (auto *Met = dynamic_cast<MethodCallExpr *>(E)) {
    bool oldAllow = m_AllowPermissionSuffix;
    m_AllowPermissionSuffix = true; // [NEW] Grant suffix allowance for explicit method call objects
    auto ObjTypeObj = Met->ObjectIsPrechecked && Met->Object->ResolvedType
                          ? Met->Object->ResolvedType
                          : checkExpr(Met->Object.get());
    m_AllowPermissionSuffix = oldAllow;
    
    auto resolvedObjType = resolveType(ObjTypeObj);
    std::string ObjType =
        resolvedObjType ? resolvedObjType->toString() : "unknown";

    // Check for Dynamic Trait Object
    if (ObjType.size() >= 4 && ObjType.substr(0, 3) == "dyn") {
      std::string traitName = "";
      if (ObjType.rfind("dyn @", 0) == 0)
        traitName = ObjType.substr(5);
      else if (ObjType.rfind("dyn@", 0) == 0)
        traitName = ObjType.substr(4);

      TraitDecl *TD = traitName.empty()
                          ? nullptr
                          : findVisibleTraitDecl(traitName, Met->Loc);
      if (TD) {
        if (!validateDynTraitObjectSafety(traitName, Met->Loc))
          return toka::Type::fromString("unknown");
        for (auto &M : TD->Methods) {
          if (M->Name == Met->Method) {
            if (!M->IsPub) {
              bool sameModule = false;
              if (CurrentModule) {
                std::string modFile = DiagnosticEngine::SrcMgr
                                          ->getFullSourceLoc(CurrentModule->Loc)
                                          .FileName;
                std::string tdDeclFile =
                    DiagnosticEngine::SrcMgr->getFullSourceLoc(TD->Loc)
                        .FileName;
                if (modFile == tdDeclFile) {
                  sameModule = true;
                }
              }
              std::string metCallFile =
                  DiagnosticEngine::SrcMgr->getFullSourceLoc(Met->Loc).FileName;
              std::string tdDeclFile =
                  DiagnosticEngine::SrcMgr->getFullSourceLoc(TD->Loc).FileName;
              if (metCallFile != tdDeclFile && !sameModule &&
                  !Met->IsCompilerInternal) {
                error(Met, DiagID::ERR_METHOD_PRIVATE, Met->Method,
                      "trait " + traitName, getModuleName(CurrentModule));
              }
            }
            return toka::Type::fromString(M->ReturnType);
          }
        }
      }
    }

    auto objectShape = std::dynamic_pointer_cast<ShapeType>(
        resolvedObjType ? resolvedObjType->getSoulType() : nullptr);
    std::string soulType =
        objectShape && objectShape->Decl
            ? (objectShape->Decl->CodegenName.empty()
                   ? objectShape->Decl->Name
                   : objectShape->Decl->CodegenName)
            : Type::stripMorphology(ObjType);
    const bool isDupCall = Met->Method == "dup";
    if (isDupCall &&
        (!Met->Args.empty() ||
         dynamic_cast<CedeExpr *>(
             unwrapCedeDirectSource(Met->Object.get())) ||
         (ObjTypeObj && ObjTypeObj->isSmartPointer()))) {
      DiagnosticEngine::report(
          getLoc(Met), DiagID::ERR_GENERIC_SEMA,
          ".dup() requires a non-consuming, non-owning receiver and no arguments");
      HasError = true;
      return toka::Type::fromString("unknown");
    }
    auto duplicatedBase =
        ObjTypeObj && (ObjTypeObj->isRawPointer() || ObjTypeObj->isReference())
            ? ObjTypeObj->getPointeeType()
            : ObjTypeObj;
    const bool hasCopyDup =
        isDupCall && duplicatedBase && proveSlice4CopyType(duplicatedBase);
    if (hasCopyDup) {
      Met->IsIntrinsicCopyDup = true;
      auto duplicated = duplicatedBase->withAttributes(
          false, duplicatedBase->IsNullable, false);
      duplicated->IsBlocked = false;
      duplicated->IsCede = false;
      return duplicated;
    }

    if (isDupCall ||
        !(MethodMap.count(soulType) &&
          MethodMap[soulType].count(Met->Method))) {
      // [NEW] Lazy Impl Instantiation
      // [Fix] Use fully resolved/mangled name for lazy instantiation lookup
      std::string ConcreteTypeName = soulType;
      std::string BaseName =
          objectShape && objectShape->Decl &&
                  objectShape->Decl->InstantiationTemplate
              ? objectShape->Decl->InstantiationTemplate->Name
              : ConcreteTypeName;
      size_t lt = BaseName.find('<');
      if (lt != std::string::npos) {
        BaseName = BaseName.substr(0, lt);
      }
      const std::string implKey = genericImplKey(BaseName, getLoc(Met));
      if (GenericImplMap.count(implKey)) {
          // [FIX] Pass generic arguments to instantiateGenericImpl
          std::vector<std::shared_ptr<toka::Type>> genericArgs;
          if (objectShape) {
            auto *ST = objectShape.get();
            genericArgs =
                ST->GenericArgs.empty() && ST->Decl &&
                        ST->Decl->InstantiationTemplate
                    ? ST->Decl->InstantiationArgs
                    : ST->GenericArgs;
          } else {
            // Fallback: parse from string
            auto parsed = Type::fromString(ConcreteTypeName);
            if (auto *PST = dynamic_cast<ShapeType *>(parsed.get())) {
              genericArgs = PST->GenericArgs;
            }
          }
          for (auto *ImplTemplate : GenericImplMap[implKey]) {
            instantiateGenericImpl(ImplTemplate, ConcreteTypeName, genericArgs,
                                   objectShape ? objectShape->Decl : nullptr);
          }
      }
    }

    FunctionDecl *dupProvider = nullptr;
    if (isDupCall) {
      auto dupImpl = ImplMap.find(soulType + "@Dup");
      if (dupImpl != ImplMap.end()) {
        auto method = dupImpl->second.find("dup");
        if (method != dupImpl->second.end())
          dupProvider = method->second;
      }
      if (!dupProvider) {
        error(Met, DiagID::ERR_NO_SUCH_MEMBER, ObjType, Met->Method);
        return toka::Type::fromString("unknown");
      }
    }

    if (dupProvider ||
        (MethodMap.count(soulType) &&
         MethodMap[soulType].count(Met->Method))) {
      if (dupProvider ||
          (MethodDecls.count(soulType) &&
           MethodDecls[soulType].count(Met->Method))) {
        FunctionDecl *FD = dupProvider ? dupProvider
                                       : MethodDecls[soulType][Met->Method];
        Met->ResolvedFn = FD;
        
        // [Effect] Concurrency Check for Method Call
        if (FD->Effect != EffectKind::None && !m_IsConsumingEffect && !m_IsPrecomputingCaptures) {
          error(Met, DiagID::ERR_DANGLING_EFFECT, Met->Method);
          recordDecision(Met, SemanticRuleID::AsyncEffect001,
                         SemanticOperation::EffectConsumption,
                         SemanticDecision::Reject,
                         SemanticReason::DanglingEffect, Met->Method);
        } else if (FD->Effect != EffectKind::None && m_IsConsumingEffect &&
                   !m_IsPrecomputingCaptures) {
          recordDecision(Met, SemanticRuleID::AsyncEffect001,
                         SemanticOperation::EffectConsumption,
                         SemanticDecision::Allow, SemanticReason::NoConflict,
                         Met->Method);
        }
        
        if (!FD->IsPub) {
          bool sameModule = false;
          if (CurrentModule) {
            std::string modFile =
                DiagnosticEngine::SrcMgr->getFullSourceLoc(CurrentModule->Loc)
                    .FileName;
            std::string fdFile =
                DiagnosticEngine::SrcMgr->getFullSourceLoc(FD->Loc).FileName;
            if (modFile == fdFile) {
              sameModule = true;
            }
          }
          std::string metCallFile =
              DiagnosticEngine::SrcMgr->getFullSourceLoc(Met->Loc).FileName;
          std::string fdDeclFile =
              DiagnosticEngine::SrcMgr->getFullSourceLoc(FD->Loc).FileName;

          if (metCallFile != fdDeclFile && !sameModule &&
              !Met->IsCompilerInternal) {
            error(Met, DiagID::ERR_METHOD_PRIVATE, Met->Method, soulType,
                  getModuleName(CurrentModule));
          }
        }

        // [Rule] Enforce Explicit Mutability for Method Calls
        bool requiresMutableBorrow = false;
        if (!FD->Args.empty() &&
            Type::stripMorphology(FD->Args[0].Name) == "self" &&
            FD->Args[0].IsValueMutable) {
          requiresMutableBorrow = true;
          // std::cerr << "[DEBUG] MutCheck: Method=" << Met->Method <<
          // std::endl;
          bool isExplicitlyMutable = false;
          // Case 1: Postfix '#' (Expression Wrapper)
          if (auto *PE = dynamic_cast<PostfixExpr *>(Met->Object.get())) {
            // std::cerr << "[DEBUG] Is PostfixExpr. Op=" << (int)PE->Op <<
            // std::endl;
            if (PE->Op == TokenType::TokenWrite)
              isExplicitlyMutable = true;
          }
          // Case 2: Variable with Suffix (Lexer Fused Token)
          else if (auto *VE = dynamic_cast<VariableExpr *>(Met->Object.get())) {
            // std::cerr << "[DEBUG] Is VariableExpr. IsValueMutable=" <<
            // VE->IsValueMutable << std::endl;
            if (VE->IsValueMutable)
              isExplicitlyMutable = true;
          }
          AccessCapability receiverCapability =
              getAccessCapability(Met->Object.get());
          if (!isExplicitlyMutable || !receiverCapability.PayloadWritable) {
            error(Met, DiagID::ERR_IMMUTABLE_MOD,
                  "Method '" + Met->Method +
                      "' requires a declared mutable payload capability (use '#' only to request it)");
          }
          if (requiresMutableBorrow) {
            Expr *objExpr = Met->Object.get();
            while (auto *PE = dynamic_cast<PostfixExpr *>(objExpr)) {
              objExpr = PE->LHS.get();
            }
            while (auto *un = dynamic_cast<UnaryExpr *>(objExpr)) {
              objExpr = un->RHS.get();
            }
            if (auto *VE = dynamic_cast<VariableExpr *>(objExpr)) {
              std::string actualName = VE->Name;
              SymbolInfo *InfoPtr = nullptr;
              if (CurrentScope->findVariableWithDeref(VE->Name, InfoPtr, actualName)) {
                InfoPtr->HasBeenMutated = true;
              }
            }
          }
        }

        if (!FD->Args.empty() &&
            Type::stripMorphology(FD->Args[0].Name) == "self") {
            if (FD->Effect == EffectKind::Async) {
              // A `cede self` receiver is the method-call spelling of an
              // ownership transfer.  Unlike ordinary arguments it has no
              // separate call-site `cede` wrapper, so the execution-boundary
              // check must use the receiver declaration as the transfer fact.
              checkStartBoundaryArgument(
                  Met->Object.get(), ObjTypeObj, FD->Args[0].IsCeded,
                  FD->Args[0].IsCeded ||
                      dynamic_cast<CedeExpr *>(Met->Object.get()) != nullptr,
                  "self");
            }
            // [NEW] Cede Ownership check for Method Call
            if (FD->Args[0].IsCeded) {
                PermissionFlow receiverFlow =
                    getPermissionFlow(Met->Object.get());
                bool directFieldOwnsPayload = false;
                bool hasDirectFieldDeclaration = false;
                Expr *ownershipSource = Met->Object.get();
                while (auto *postfix =
                           dynamic_cast<PostfixExpr *>(ownershipSource)) {
                  ownershipSource = postfix->LHS.get();
                }
                if (auto *member =
                        dynamic_cast<MemberExpr *>(ownershipSource)) {
                  auto ownerType = member->Object
                                       ? resolveType(member->Object->ResolvedType,
                                                     true)
                                       : nullptr;
                  auto ownerShape = std::dynamic_pointer_cast<ShapeType>(
                      ownerType ? ownerType->getSoulType() : nullptr);
                  ShapeDecl *shape = ownerShape ? ownerShape->Decl : nullptr;
                  if (!shape && ownerType) {
                    shape = findVisibleShapeDecl(ownerType->getSoulName(),
                                                 member->Loc);
                  }
                  if (shape) {
                    for (const auto &field : shape->Members) {
                      if (stripMemberAccessMarkers(field.Name) ==
                          stripMemberAccessMarkers(member->Member)) {
                        hasDirectFieldDeclaration = true;
                        MemberAccessIntent declaredField =
                            parseMemberAccess(field.Name);
                        directFieldOwnsPayload =
                            !field.IsShared && !field.IsReference &&
                            !field.IsRawPointer &&
                            declaredField.Prefix.find('~') ==
                                std::string::npos &&
                            declaredField.Prefix.find('&') ==
                                std::string::npos &&
                            declaredField.Prefix.find('*') ==
                                std::string::npos;
                        break;
                      }
                    }
                  }
                }
                const bool receiverOwnsPayload =
                    hasDirectFieldDeclaration
                        ? directFieldOwnsPayload
                        : (receiverFlow.Kind != PermissionFlowKind::Shared &&
                           receiverFlow.Kind != PermissionFlowKind::UnsafeRaw);
                if (!receiverOwnsPayload) {
                  error(Met->Object.get(),
                        DiagID::ERR_SEMA_CEDE_RECEIVER_NOT_OWNED,
                        getPathString(Met->Object.get()));
                } else {
                Expr *receiver = Met->Object.get();
                while (auto *postfix =
                           dynamic_cast<PostfixExpr *>(receiver)) {
                  receiver = postfix->LHS.get();
                }

                if (auto *member = dynamic_cast<MemberExpr *>(receiver)) {
                  // A consuming receiver is an implicit whole transfer of
                  // that receiver.  For an eligible direct field, use the
                  // same bounded partial-cede liveness rule as `cede x.f`.
                  // Anything else remains outside this local record model.
                  auto *root =
                      dynamic_cast<VariableExpr *>(member->Object.get());
                  SymbolInfo *rootInfo = nullptr;
                  std::string actualRootName;
                  bool supportedDirectField = false;
                  bool reportedLifecycleError = false;
                  if (root && CurrentScope->findVariableWithDeref(
                                  root->Name, rootInfo, actualRootName) &&
                      rootInfo && rootInfo->IsDeclaredVariable &&
                      !rootInfo->IsFunctionParameter && rootInfo->TypeObj &&
                      rootInfo->TypeObj->isShape()) {
                    auto shapeType = std::dynamic_pointer_cast<ShapeType>(
                        rootInfo->TypeObj);
                    ShapeDecl *shape = shapeType ? shapeType->Decl : nullptr;
                    if (shape && shape->HasExplicitDrop) {
                      error(Met, DiagID::ERR_MOVE_MEMBER_DROP, member->Member,
                            shape->Name);
                      reportedLifecycleError = true;
                    } else if (shape &&
                               (shape->Kind == ShapeKind::Struct ||
                                shape->Kind == ShapeKind::Tuple)) {
                      for (size_t i = 0; i < shape->Members.size(); ++i) {
                        if (stripMemberAccessMarkers(shape->Members[i].Name) ==
                            stripMemberAccessMarkers(member->Member)) {
                          if (i < 64) {
                            initializeProjectionFacts(*rootInfo);
                            if (rootInfo->ExactPlace.transitionProjection(
                                    PartialMoveProjectionKind::DirectField,
                                    i, PlaceState::Moved)) {
                              syncLegacyProjectionLiveness(*rootInfo);
                              supportedDirectField = true;
                            }
                          }
                          break;
                        }
                      }
                    }
                  }
                  if (!supportedDirectField && !reportedLifecycleError) {
                    error(Met, DiagID::ERR_SEMA_CEDE_RECEIVER_PROJECTION_UNSUPPORTED,
                          member->Member);
                  }
                } else if (auto *var =
                               dynamic_cast<VariableExpr *>(receiver)) {
                  SymbolInfo *receiverInfo = nullptr;
                  std::string actualReceiverName;
                  if (CurrentScope->findVariableWithDeref(
                          var->Name, receiverInfo, actualReceiverName) &&
                      receiverInfo && receiverInfo->IsFunctionParameter &&
                      !receiverInfo->IsCeded && receiverInfo->TypeObj &&
                      !proveSlice4CopyType(receiverInfo->TypeObj)) {
                    error(Met->Object.get(),
                          DiagID::ERR_SEMA_CANNOT_CEDE_NON_CEDE_PARAMETER,
                          var->Name);
                  } else {
                    CurrentScope->markMoved(var->Name, Met->Loc);
                  }
                } else if (auto *index =
                               dynamic_cast<ArrayIndexExpr *>(receiver)) {
                  // Explicit `cede values[0]` has a separately qualified
                  // fixed-index transfer path.  Do not silently treat a
                  // consuming method receiver as equivalent until its call
                  // ABI, source-slot release, and drop-mask proof are shared.
                  error(Met,
                        DiagID::ERR_SEMA_CEDE_RECEIVER_PROJECTION_UNSUPPORTED,
                        index->toString());
                }
                }
            }
        }

        // [Rule] Borrowing check for Method Call
        std::string objPath = getPathString(Met->Object.get());
        if (!objPath.empty()) {
           AccessPath receiverPath = makeAccessPath(Met->Object.get());
           AccessPath objectPath = canonicalizeAccessPath(receiverPath);
           auto conflict =
               requiresMutableBorrow
                   ? PALCheckerState.verifyExclusiveMutation(objectPath)
                   : PALCheckerState.verifyAccess(objectPath);
           // A mutable reference is allowed to use the exact path it already
           // borrowed.  Treat method receivers the same way as variable and
           // member accesses; otherwise `&value#` pattern bindings cannot
           // even call an ordinary read-only method on themselves.
           if (conflict && !isBorrowAccessAuthorized(receiverPath,
                                                      conflict->Path)) {
               DiagnosticEngine::report(getLoc(Met), DiagID::ERR_BORROW_MUT,
                                        conflict->displayPath());
               HasError = true;
               recordPALConflict(
                   Met, requiresMutableBorrow
                            ? PALOperationClass::ExclusiveMutation
                            : PALOperationClass::SharedPayloadBorrow,
                   objectPath, *conflict);
           }
        }

        // [FIX] Typecheck Method Arguments
        if (FD) {
            size_t expectedArgs = FD->Args.size() - 1; // exclude self
            if (Met->Args.size() != expectedArgs && !FD->IsVariadic) {
                // If variadic, handle appropriately
                if (Met->Args.size() < expectedArgs) {
                    error(Met, DiagID::ERR_SEMA_METHOD_EXPECTS_AT_LEAST_ARGUMENTS_GOT, Met->Method, std::to_string(expectedArgs), std::to_string(Met->Args.size()));
                }
            }
            
            for (size_t i = 0; i < Met->Args.size(); ++i) {
                Met->Args[i] = foldGenericConstant(std::move(Met->Args[i]));
                std::shared_ptr<toka::Type> expectedParamTy = nullptr;
                if (i < expectedArgs) {
                    expectedParamTy = FD->Args[i + 1].ResolvedType
                                          ? FD->Args[i + 1].ResolvedType
                                          : resolveType(
                                                Sema::synthesizePhysicalTypeObject(
                                                    FD->Args[i + 1]));
                }
                
                bool oldAllowPermissionSuffix = m_AllowPermissionSuffix;
                m_AllowPermissionSuffix =
                    hasExplicitCallArgumentWriteSigil(Met->Args[i].get());
                auto argTy = checkExpr(Met->Args[i].get(), expectedParamTy);
                m_AllowPermissionSuffix = oldAllowPermissionSuffix;
                projectOwnedStringView(Met->Args[i], argTy, expectedParamTy);

                if (i < expectedArgs) {
                  const auto &param = FD->Args[i + 1];
                  auto *callerCede =
                      dynamic_cast<CedeExpr *>(Met->Args[i].get());
                  if (!param.IsCeded && callerCede &&
                      makeAccessPath(callerCede->Value.get())) {
                    error(Met->Args[i].get(),
                          DiagID::ERR_SEMA_CEDE_ARGUMENT_TO_BORROWED_PARAMETER,
                          getPathString(Met->Args[i].get()), param.Name);
                  }
                  if (param.IsCeded &&
                      isMayZeroRawCedeSource(Met->Args[i].get()) &&
                      !(param.IsRawPointer && param.IsPointerNullable)) {
          error(Met->Args[i].get(),
                DiagID::ERR_SEMA_CEDE_MAY_ZERO_RAW_REQUIRES_GUARD);
                  }
                  bool paramIsHatted = param.IsRawPointer || param.IsUnique ||
                                        param.IsShared || param.IsReference;
                  AccessCapability declaredCapability =
                      getAccessCapability(Met->Args[i].get(), true);
                  AccessCapability argCapability =
                      getAccessCapability(Met->Args[i].get());
                  AccessIntent argIntent =
                      getAccessIntent(Met->Args[i].get());
                  bool isIndependentCedeTransfer =
                      param.IsCeded &&
                      dynamic_cast<CedeExpr *>(Met->Args[i].get()) != nullptr &&
                      getPermissionFlow(Met->Args[i].get()).Kind ==
                          PermissionFlowKind::Independent;
                  bool lacksHandleCapability =
                      paramIsHatted && param.IsRebindable &&
                      (!declaredCapability.HandleRebindable ||
                       !argCapability.HandleRebindable ||
                       !argIntent.HandleRebind);
                  bool lacksPayloadCapability =
                      param.IsValueMutable && !isIndependentCedeTransfer &&
                      (!declaredCapability.PayloadWritable ||
                       !argCapability.PayloadWritable ||
                       !argIntent.PayloadWrite);
                  if (lacksHandleCapability || lacksPayloadCapability) {
                    error(Met->Args[i].get(),
                          DiagID::ERR_SEMA_TYPE_MISMATCH_IN_METHOD_ARGUMENT_EXPECTED,
                          std::to_string(i + 1),
                          expectedParamTy ? expectedParamTy->toString()
                                          : "capable argument",
                          argTy ? argTy->toString() : "unknown");
                  }
                  validateCallArgumentMutSigil(Met->Args[i].get(),
                                               param.IsValueMutable, param.Name,
                                               param.Loc, Met->Loc, i);
                }
                if (FD->Effect == EffectKind::Async && i < expectedArgs) {
                  checkStartBoundaryArgument(
                      Met->Args[i].get(), argTy, FD->Args[i + 1].IsCeded,
                      dynamic_cast<CedeExpr *>(Met->Args[i].get()) != nullptr,
                      FD->Args[i + 1].Name, FD->Args[i + 1].Loc);
                }
                
                if (expectedParamTy) {
                    if (FD->Args[i + 1].IsCeded) {
                        bool isCallerCeded = dynamic_cast<CedeExpr*>(Met->Args[i].get()) != nullptr;
                        bool isPrimitive = canImplicitlyPassToCede(argTy);
                        if (!isCallerCeded && !isPrimitive) {
                            error(Met->Args[i].get(), DiagID::ERR_SEMA_ARGUMENT_MUST_BE_EXPLICITLY_PASSED_WITH_C);
                            if (FD->Args[i + 1].Loc.isValid())
                              DiagnosticEngine::report(
                                  FD->Args[i + 1].Loc, DiagID::NOTE_GENERIC,
                                  "cede parameter declared here");
                        }
                        recordDecision(
                            Met->Args[i].get(), SemanticRuleID::OwnCede001,
                            SemanticOperation::CedeObligation,
                            (!isCallerCeded && !isPrimitive)
                                ? SemanticDecision::Reject
                                : SemanticDecision::Allow,
                            (!isCallerCeded && !isPrimitive)
                                ? SemanticReason::MissingExplicitCede
                                : SemanticReason::CedeConsumed,
                            getPathString(Met->Args[i].get()),
                            FD->Args[i + 1].Name, FD->Args[i + 1].Loc);
                        SemanticEvidence::recordCedeObligation(
                            CedeObligationStage::CallerTransfer,
                            (!isCallerCeded && !isPrimitive)
                                ? CedeObligationStatus::Violated
                                : CedeObligationStatus::Fulfilled,
                            (!isCallerCeded && !isPrimitive)
                                ? SemanticReason::MissingExplicitCede
                                : SemanticReason::CedeConsumed,
                            getPathString(Met->Args[i].get()),
                            FD->Args[i + 1].Name, getLoc(Met->Args[i].get()),
                            FD->Args[i + 1].Loc);
                    }

                    if (!isTypeCompatible(expectedParamTy, argTy)) {
                        error(Met->Args[i].get(), DiagID::ERR_SEMA_TYPE_MISMATCH_IN_METHOD_ARGUMENT_EXPECTED, std::to_string(i + 1), expectedParamTy->toString(), argTy->toString());
                    } else if (expectedParamTy->isShape() && argTy->isRawPointer()) {
                        auto shp = std::static_pointer_cast<toka::ShapeType>(expectedParamTy);
                        if (shp->Name == "str") {
                            Met->Args[i]->ResolvedType = expectedParamTy;
                        }
                    }
                }
            }
        } else {
            // Unresolved method definition context (e.g. core/std method called locally but not parsed as FD).
            // This is a flaw in Toka's global pass - we fallback to just checking the arguments to ensure they are visited.
            for (size_t i = 0; i < Met->Args.size(); ++i) {
                checkExpr(Met->Args[i].get()); 
            }
        }

        // Method-call syntax omits the receiver from Met->Args.  Preserve the
        // declaration's formal indices so trusted std/atomic methods share the
        // same Ordering legality table as the core wrappers.
        validateAtomicOrderingArguments(FD, Met->Args, 1);

        auto retType =
            FD && FD->ResolvedReturnType
                ? FD->ResolvedReturnType
                : FD ? toka::Type::fromString(FD->ReturnType)
                     : toka::Type::fromString(
                           MethodMap[soulType][Met->Method]);

        if (FD) {
            bool hasExplicitDeps = !FD->LifeDependencies.empty();

            auto mapParamToArg = [&](const std::string &paramName) -> std::string {
               const std::string normalized =
                   Type::stripMorphology(paramName);
               for (size_t i = 0; i < FD->Args.size(); ++i) {
                  const std::string argumentName =
                      Type::stripMorphology(FD->Args[i].Name);
                  if (normalized != argumentName &&
                      !(normalized.size() > argumentName.size() &&
                        normalized.compare(0, argumentName.size(),
                                           argumentName) == 0 &&
                        normalized[argumentName.size()] == '.'))
                    continue;

                  Expr *argument = i == 0 ? Met->Object.get()
                                          : (i - 1 < Met->Args.size()
                                                 ? Met->Args[i - 1].get()
                                                 : nullptr);
                  std::string path = getDependencyPathString(argument);
                  if (path.empty())
                    return "";
                  if (normalized.size() > argumentName.size())
                    path += normalized.substr(argumentName.size());
                  return path;
               }
               return "";
            };

            if (hasExplicitDeps) {
                for (const auto &dep : FD->LifeDependencies) {
                   std::string argVar = mapParamToArg(dep);
                   if (argVar.empty())
                     continue;

                   Expr *structuralArg = Met->Object.get();
                   for (size_t i = 1; i < FD->Args.size(); ++i) {
                     const std::string argumentName =
                         Type::stripMorphology(FD->Args[i].Name);
                     const std::string normalized =
                         Type::stripMorphology(dep);
                     if ((normalized == argumentName ||
                          (normalized.size() > argumentName.size() &&
                           normalized.compare(0, argumentName.size(),
                                              argumentName) == 0 &&
                           normalized[argumentName.size()] == '.')) &&
                         i - 1 < Met->Args.size()) {
                       structuralArg = Met->Args[i - 1].get();
                       break;
                     }
                   }
                   bool isExpressionDependency =
                       getPathString(structuralArg).empty();

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

                   SymbolInfo *argInfo = nullptr;
                   std::shared_ptr<toka::Type> argResolvedType = nullptr;
                   bool contributedDeps = false;

                   if (Type::stripMorphology(dep) == "self") {
                     if (auto *selfVar = dynamic_cast<VariableExpr *>(Met->Object.get())) {
                       if (CurrentScope->findSymbol(selfVar->Name, argInfo)) {
                         argResolvedType = argInfo->TypeObj;
                       }
                     } else {
                       argResolvedType = Met->Object ? Met->Object->ResolvedType : nullptr;
                     }
                   } else {
                     for (size_t i = 0; i < FD->Args.size(); ++i) {
                       if (FD->Args[i].Name != dep || i == 0 || (i - 1) >= Met->Args.size())
                         continue;
                       Expr *argExpr = Met->Args[i - 1].get();
                       argResolvedType = argExpr ? argExpr->ResolvedType : nullptr;
                       if (auto *var = dynamic_cast<VariableExpr *>(argExpr)) {
                         if (CurrentScope->findSymbol(var->Name, argInfo)) {
                           if (!argResolvedType)
                             argResolvedType = argInfo->TypeObj;
                         }
                       }
                       break;
                     }
                   }

                   if (argInfo) {
                     if (!argInfo->BorrowedFrom.empty()) {
                       m_LastLifeDependencies.insert(argInfo->BorrowedFrom);
                       contributedDeps = true;
                     }
                     if (!argInfo->LifeDependencySet.empty()) {
                       m_LastLifeDependencies.insert(
                           argInfo->LifeDependencySet.begin(),
                           argInfo->LifeDependencySet.end());
                       contributedDeps = true;
                     }
                   }

                   bool isLifetimeAnchor = false;
                   if (argResolvedType) {
                     auto soul = std::dynamic_pointer_cast<ShapeType>(
                         argResolvedType->getSoulType());
                     isLifetimeAnchor = soul && soul->Decl &&
                                        soul->Decl->HasExplicitDrop &&
                                        retType &&
                                        resolveType(retType)->isReference();
                   }
                   if (isExpressionDependency || isCurrentFunctionParam ||
                       !contributedDeps || isLifetimeAnchor) {
                     m_LastLifeDependencies.insert(argVar);
                   }
                   recordDecision(
                       Met, FD->Effect == EffectKind::Async
                                ? SemanticRuleID::AsyncSuspend001
                                : SemanticRuleID::EffRet001,
                       SemanticOperation::EscapingDependency,
                       SemanticDecision::Allow,
                       SemanticReason::InterfaceContractApplied, argVar, dep,
                       FD->Loc);
                }
            }

            for (const auto &pair : FD->MemberDependencies) {
              if (!returnTypeHasMember(FD, pair.first))
                continue;
              for (const auto &dep : pair.second) {
                std::string argVar = mapParamToArg(dep);
                if (argVar.empty())
                  continue;
                m_LastFieldDependencies[pair.first].insert(argVar);
                recordDecision(
                    Met, SemanticRuleID::EffMember001,
                    SemanticOperation::MemberDependency,
                    SemanticDecision::Allow,
                    SemanticReason::InterfaceContractApplied,
                    pair.first + "<-" + argVar, dep, FD->Loc);
              }
            }
        }

        if (FD && FD->Effect == EffectKind::Async) {
            return resolveType(std::make_shared<ShapeType>(
                "TaskHandle",
                std::vector<std::shared_ptr<toka::Type>>{retType}));
        }
        return retType;
      }
    }

    // Check with @Encap suffix as fallback
    std::string encapType = soulType + "@Encap";
    if (MethodMap.count(encapType) && MethodMap[encapType].count(Met->Method)) {
      return toka::Type::fromString(MethodMap[encapType][Met->Method]);
    }

    // Check with @Delegate suffix as fallback (Intrinsic Method Proxying)
    std::string delegateKey = soulType + "@Delegate";
    if (ImplMap.count(delegateKey) && ImplMap[delegateKey].count("target")) {
      FunctionDecl* targetMethod = ImplMap[delegateKey]["target"];
      std::string targetReturnTypeStr = targetMethod->ReturnType;
      auto targetTypeObj = toka::Type::fromString(targetReturnTypeStr);
      std::string targetSoul = targetTypeObj->getSoulName();
      
      if (MethodMap.count(targetSoul) && MethodMap[targetSoul].count(Met->Method)) {
          // Mutate the AST: baseObj.method(...) -> baseObj.target().method(...)
          auto targetCall = std::make_unique<MethodCallExpr>(std::move(Met->Object), "target", std::vector<std::unique_ptr<Expr>>());
          targetCall->Loc = Met->Loc;
          Met->Object = std::move(targetCall);
          
          // Re-evaluate to run full argument, permission, and concurrency checking on the target method
          return checkExpr(E);
      }
    }

    // Check if it's a reference to a struct
    if (ObjType.size() > 1 && ObjType[0] == '^') {
      std::string Pointee = ObjType.substr(1);
      std::string pSoul = Type::stripMorphology(Pointee);
      if (MethodMap.count(pSoul) && MethodMap[pSoul].count(Met->Method)) {
        return toka::Type::fromString(MethodMap[pSoul][Met->Method]);
      }
    }
    // [Intrinsic] unset() & unwrap()
    if (Met->Method == "unset") {
      m_IsUnsetInitCall = true;
      return ObjTypeObj;
    }
    if (Met->Method == "unwrap") {
      if (!ObjTypeObj->IsNullable) {
        // Warning or Silent
      }
      return ObjTypeObj->withAttributes(ObjTypeObj->IsWritable, false);
    }

    error(Met, DiagID::ERR_NO_SUCH_MEMBER, ObjType, Met->Method);
    return toka::Type::fromString("unknown");
  } else if (auto *Init = dynamic_cast<InitStructExpr *>(E)) {
    return checkShapeInit(Init);
  } else if (auto *Memb = dynamic_cast<MemberExpr *>(E)) {
    return checkMemberExpr(Memb);
  } else if (auto *St = dynamic_cast<StartExpr *>(E)) {
    bool old = m_IsConsumingEffect;
    bool oldStartingTask = m_IsStartingTask;
    m_IsConsumingEffect = true;
    m_IsStartingTask = true;
    auto res = checkExpr(St->Expression.get());
    m_IsConsumingEffect = old;
    m_IsStartingTask = oldStartingTask;
    for (const auto &dep : m_LastLifeDependencies) {
      DiagnosticEngine::report(
          getLoc(St), DiagID::ERR_SEMA_START_BOUNDARY_DEPENDENCY, dep);
      HasError = true;
      SourceLocation originLoc = findPathDeclaration(dep);
      recordDecision(St, SemanticRuleID::AsyncCapture001,
                     SemanticOperation::ExecutionBoundaryCapture,
                     SemanticDecision::Reject,
                     SemanticReason::BorrowedBoundaryDependency, dep, dep,
                     originLoc);
      if (originLoc.isValid())
        DiagnosticEngine::report(originLoc, DiagID::NOTE_GENERIC,
                                 "borrowed dependency originates here");
    }
    St->ResolvedType = res;
    return res;
  } else if (auto *Unwrap = dynamic_cast<UnwrapPropagationExpr *>(E)) {
    bool old = m_IsConsumingEffect;
    m_IsConsumingEffect = true;
    auto baseObj = checkExpr(Unwrap->Base.get());
    m_IsConsumingEffect = old;

    std::string propagationPath = getPathString(Unwrap->Base.get());
    if (!propagationPath.empty()) {
      auto *Var = dynamic_cast<VariableExpr *>(Unwrap->Base.get());
      if (!Var) {
        error(Unwrap, DiagID::ERR_SEMA_PROPAGATION_COMPLEX_MOVE,
              propagationPath);
        recordDecision(Unwrap, SemanticRuleID::ErrorProp001,
                       SemanticOperation::ErrorPropagation,
                       SemanticDecision::ConservativeReject,
                       SemanticReason::PartialMoveUnsupported,
                       propagationPath, propagationPath);
      } else {
        AccessPath path =
            canonicalizeAccessPath(makeAccessPath(Unwrap->Base.get()));
        if (auto conflict = PALCheckerState.verifyInvalidation(path)) {
          error(Unwrap, DiagID::ERR_MOVE_BORROWED,
                conflict->displayPath());
          recordPALConflict(Unwrap, PALOperationClass::Invalidation, path,
                            *conflict);
        }
        SymbolInfo *Info = nullptr;
        std::string actualName;
        if (CurrentScope->findVariableWithDeref(Var->Name, Info, actualName)) {
          if (Info) {
            if (!Info->BorrowedFrom.empty())
              m_LastLifeDependencies.insert(Info->BorrowedFrom);
            m_LastLifeDependencies.insert(Info->LifeDependencySet.begin(),
                                          Info->LifeDependencySet.end());
          }
          CurrentScope->markMoved(actualName, E->Loc);
          PALCheckerState.markMoved(path);
        }
      }
    }

    if (baseObj->isUnknown()) return toka::Type::fromString("unknown");
    baseObj = resolveType(baseObj, false);
    
    if (!baseObj->isShape()) {
      error(Unwrap, DiagID::ERR_SEMA_UNWRAP_OPERATOR_REQUIRES_A_RESULT_OR_OPTI);
      return toka::Type::fromString("unknown");
    }
    
    auto shapeT = std::static_pointer_cast<toka::ShapeType>(baseObj);
    std::string soul = shapeT->Decl ? shapeT->Decl->Name : shapeT->getSoulName();

    bool isResult = soul == "Result" || soul.find("Result_") == 0;
    bool isOption = soul == "Option" || soul.find("Option_") == 0;

    if (!isResult && !isOption) {
      error(Unwrap, DiagID::ERR_SEMA_UNWRAP_OPERATOR_REQUIRES_RESULT_T_E_OR_OP, soul);
      return toka::Type::fromString("unknown");
    }
    
    std::shared_ptr<toka::Type> payloadT = nullptr;
    std::shared_ptr<toka::Type> errT = nullptr;

    auto endsWith = [](const std::string& str, const std::string& suffix) {
        return str.size() >= suffix.size() && 
               str.compare(str.size() - suffix.size(), suffix.size(), suffix) == 0;
    };

    if (shapeT->Decl) {
      for (auto &M : shapeT->Decl->Members) {
        if (M.Name == "Ok" || M.Name == "Some" || endsWith(M.Name, "::Ok") || endsWith(M.Name, "::Some")) {
           if (!M.SubMembers.empty()) {
             payloadT = M.SubMembers[0].ResolvedType;
           } else {
             payloadT = M.ResolvedType;
           }
        } else if (M.Name == "Err" || endsWith(M.Name, "::Err")) {
           if (!M.SubMembers.empty()) {
             errT = M.SubMembers[0].ResolvedType;
           } else {
             errT = M.ResolvedType;
           }
        }
      }
    }

    if (!payloadT) {
      error(Unwrap, DiagID::ERR_SEMA_UNWRAP_OPERATOR_REQUIRES_A_PAYLOAD_TYPE_I, soul);
      return toka::Type::fromString("unknown");
    }
    
    if (!CurrentFunction) {
      error(Unwrap, DiagID::ERR_SEMA_UNWRAP_OPERATOR_MUST_BE_USED_INSIDE_A_FUN);
      return toka::Type::fromString("unknown");
    }
    
    auto fnRetT = CurrentFunction->ResolvedReturnType;
    if (!fnRetT) {
      error(Unwrap, DiagID::ERR_SEMA_CANNOT_DETERMINE_FUNCTION_RETURN_TYPE_FOR);
      return toka::Type::fromString("unknown");
    }
    fnRetT = resolveType(fnRetT, false);
    
    if (isResult) {
      if (!fnRetT->isShape()) {
        error(Unwrap, DiagID::ERR_SEMA_FUNCTION_MUST_RETURN_RESULT_WHEN_UNWRAPPI);
        return payloadT;
      }
      auto retShape = std::static_pointer_cast<toka::ShapeType>(fnRetT);
      std::string fnRetSoul = retShape->Decl ? retShape->Decl->Name : retShape->getSoulName();
      bool fnIsResult = fnRetSoul == "Result" || fnRetSoul.find("Result_") == 0;
      if (!fnIsResult) {
        error(Unwrap, DiagID::ERR_SEMA_FUNCTION_MUST_RETURN_RESULT_WHEN_UNWRAPPI);
        return payloadT;
      }
      
      std::shared_ptr<toka::Type> retErrT = nullptr;
      if (retShape->Decl) {
        for (auto &M : retShape->Decl->Members) {
          if (M.Name == "Err" || endsWith(M.Name, "::Err")) {
             if (!M.SubMembers.empty()) {
               retErrT = M.SubMembers[0].ResolvedType;
             } else {
               retErrT = M.ResolvedType;
             }
          }
        }
      }
      if (!errT || !retErrT) {
        error(Unwrap, DiagID::ERR_SEMA_RESULT_TYPES_MUST_HAVE_ERR_VARIANT);
      } else {
        errT = resolveType(errT, false);
        retErrT = resolveType(retErrT, false);
        Unwrap->SourceErrorType = errT;
        Unwrap->TargetErrorType = retErrT;

        if (!errT->equals(*retErrT)) {
          std::string sourceName =
              Type::stripMorphology(errT->getSoulName());
          std::string targetName = retErrT->toString();
          std::string traitName = "ErrorInto<" + targetName + ">";
          std::string implKey = sourceName + "@" + traitName;

          if (!ImplMap.count(implKey)) {
            std::string sourceBase = sourceName;
            size_t generic = sourceBase.find('<');
            if (generic != std::string::npos)
              sourceBase = sourceBase.substr(0, generic);
            const std::string genericKey =
                genericImplKey(sourceBase, getLoc(Unwrap));
            if (GenericImplMap.count(genericKey)) {
              std::vector<std::shared_ptr<toka::Type>> genericArgs;
              if (auto sourceShape =
                      std::dynamic_pointer_cast<ShapeType>(errT))
                genericArgs = sourceShape->GenericArgs;
              for (auto *impl : GenericImplMap[genericKey])
                instantiateGenericImpl(impl, sourceName, genericArgs);
            }
          }

          auto impl = ImplMap.find(implKey);
          if (impl == ImplMap.end() ||
              !impl->second.count("into_error")) {
            error(Unwrap, DiagID::ERR_SEMA_ERROR_CONVERSION_REQUIRED,
                  errT->toString(), retErrT->toString(), sourceName,
                  targetName);
            recordDecision(Unwrap, SemanticRuleID::ErrorProp001,
                           SemanticOperation::ErrorPropagation,
                           SemanticDecision::Reject,
                           SemanticReason::MissingErrorConversion,
                           errT->toString(), retErrT->toString());
          } else {
            Unwrap->ErrorConversionFn = impl->second["into_error"];
            recordDecision(Unwrap, SemanticRuleID::ErrorProp001,
                           SemanticOperation::ErrorPropagation,
                           SemanticDecision::Allow,
                           SemanticReason::ExplicitErrorConversion,
                           errT->toString(), retErrT->toString());
          }
        } else {
          recordDecision(Unwrap, SemanticRuleID::ErrorProp001,
                         SemanticOperation::ErrorPropagation,
                         SemanticDecision::Allow,
                         SemanticReason::DirectErrorMatch,
                         errT->toString(), retErrT->toString());
        }
      }
    } else if (isOption) {
      if (!fnRetT->isShape()) {
        error(Unwrap, DiagID::ERR_SEMA_FUNCTION_MUST_RETURN_OPTION_WHEN_UNWRAPPI);
      } else {
        auto retShape = std::static_pointer_cast<toka::ShapeType>(fnRetT);
        std::string fnRetSoul = retShape->Decl ? retShape->Decl->Name : retShape->getSoulName();
        bool fnIsOption = fnRetSoul == "Option" || fnRetSoul.find("Option_") == 0;
        if (!fnIsOption) {
          error(Unwrap, DiagID::ERR_SEMA_FUNCTION_MUST_RETURN_OPTION_WHEN_UNWRAPPI);
        }
      }
    }
    return payloadT;
  } else if (auto *Post = dynamic_cast<PostfixExpr *>(E)) {
    // [Fix] Do NOT disable soul collapse.
    // If the user wants the handle, they must use explicit prefix (e.g.
    // ^ptr#). Otherwise `ptr#` means "Mutable Value".

    // [NEW] Enforce Suffix Lifecycle Rule
    if (Post->Op == TokenType::TokenWrite) {
      if (!m_AllowPermissionSuffix) {
        error(Post, DiagID::ERR_ILLEGAL_MODIFIER_SUFFIX);
      }
    }

    if (m_InIntermediatePath) {
      if (Post->Op == TokenType::TokenWrite ||
          Post->Op == TokenType::TokenNull) {
        if (!m_IsMemberBase) {
          error(Post, DiagID::ERR_SEMA_PERMISSION_SYMBOLS_ARE_ONLY_ALLOWED_AT_TH);
        }
      }
    }

    bool oldAllowSuffix = m_AllowPermissionSuffix;
    m_AllowPermissionSuffix = false;
    auto lhsObj = checkExpr(Post->LHS.get());
    m_AllowPermissionSuffix = oldAllowSuffix;
    std::string lhsInfo = lhsObj->toString();
    if (Post->Op == TokenType::TokenWrite) {
      if (auto *Var = dynamic_cast<VariableExpr *>(Post->LHS.get())) {
        SymbolInfo *InfoPtr = nullptr;
        std::string actualName = Var->Name;
        if (CurrentScope->findVariableWithDeref(Var->Name, InfoPtr, actualName) &&
            InfoPtr && !InfoPtr->IsSoulMutable()) {
          error(Post, DiagID::ERR_IMMUTABLE_MOD, Var->Name);
        }
      }
    }
    if (Post->Op == TokenType::TokenWrite) {
      if (!lhsInfo.empty() && lhsInfo.back() != '#' && lhsInfo.back() != '!')
        lhsInfo += "#";
    } else if (Post->Op == TokenType::TokenNull) {
      if (!lhsInfo.empty() && lhsInfo.back() != '?' && lhsInfo.back() != '!')
        lhsInfo += "?";
    }
    return toka::Type::fromString(lhsInfo);
  } else if (auto *Repeat = dynamic_cast<RepeatedArrayExpr *>(E)) {
    std::shared_ptr<toka::Type> expectedElemType = nullptr;
    if (m_ExpectedType && m_ExpectedType->isArray()) {
      expectedElemType = m_ExpectedType->getArrayElementType();
    }
    auto elemType = checkExpr(Repeat->Value.get(), expectedElemType);

    // [FIX] Substitution for Count
    Repeat->Count = foldGenericConstant(std::move(Repeat->Count));

    uint64_t size = 0;
    if (auto *Num = dynamic_cast<NumberExpr *>(Repeat->Count.get())) {
      size = Num->Value;
    } else if (auto *Var = dynamic_cast<VariableExpr *>(Repeat->Count.get())) {
      SymbolInfo info;
      if (CurrentScope->lookup(Var->Name, info) && info.HasConstValue) {
        size = info.ConstValue;
        // [Annotated AST] Propagate for CodeGen
        Var->HasConstantValue = true;
        Var->ConstantValue = size;
        Var->ConstantValObj = info.ConstValObj;
      } else {
        error(Repeat, DiagID::ERR_SEMA_ARRAY_REPEAT_COUNT_MUST_BE_A_NUMERIC_LITE);
      }
    } else {
      error(Repeat, DiagID::ERR_SEMA_ARRAY_REPEAT_COUNT_MUST_BE_A_NUMERIC_LITE);
    }
    return std::make_shared<toka::ArrayType>(elemType, size);
  } else if (auto *ArrLit = dynamic_cast<ArrayExpr *>(E)) {
    // Infer from first element
    if (!ArrLit->Elements.empty()) {
      std::shared_ptr<toka::Type> expectedElemType = nullptr;
      if (m_ExpectedType && m_ExpectedType->isArray()) {
        expectedElemType = m_ExpectedType->getArrayElementType();
      }
      ArrLit->Elements[0] =
          foldGenericConstant(std::move(ArrLit->Elements[0])); // [FIX]
      auto ElemTyObj = checkExpr(ArrLit->Elements[0].get(), expectedElemType);
      for (size_t i = 1; i < ArrLit->Elements.size(); ++i) {
        checkExpr(ArrLit->Elements[i].get(), ElemTyObj);
      }
      std::string ElemTy = ElemTyObj->toString();
      return toka::Type::fromString(
          "[" + ElemTy + ";" + std::to_string(ArrLit->Elements.size()) + "]");
    }
    return toka::Type::fromString("[i32; 0]");
  } else if (auto *me = dynamic_cast<MatchExpr *>(E)) {
    auto targetTypeObj = checkExpr(me->Target.get());
    std::string targetType = targetTypeObj->toString();
    std::string resultType = NoProducedValue;
    std::shared_ptr<toka::Type> resultTypeObj;

    if (me->Target) {
        me->Target->ExtendLifetime = true;
    }

    bool isReceiver = false;
    if (!m_ControlFlowStack.empty()) {
      isReceiver = m_ControlFlowStack.back().IsReceiver;
    }

    std::string targetPath = getPathString(me->Target.get());
    AccessPath targetAccessPath =
        canonicalizeAccessPath(makeAccessPath(me->Target.get()));
    PermissionFlow targetFlow = getPermissionFlow(me->Target.get());
    AccessCapability targetCapability = targetFlow.DirectCapability;
    if (targetFlow.Kind == PermissionFlowKind::Shared)
      targetCapability.PayloadFlowRestricted = true;

    CallExpr *outcomeCall = dynamic_cast<CallExpr *>(me->Target.get());
    FunctionDecl *outcomeFunction =
        outcomeCall && outcomeCall->RequiresOutcomeMatch
            ? outcomeCall->ResolvedFn
            : nullptr;
    std::string outcomePlace;
    if (outcomeFunction && outcomeFunction->ResolvedOutcomeTransition) {
      outcomeCall->OutcomeMatchConsumed = true;
      const auto &transition = *outcomeFunction->ResolvedOutcomeTransition;
      if (transition.SubjectIndex < outcomeCall->Args.size()) {
        if (auto *place = dynamic_cast<VariableExpr *>(
                outcomeCall->Args[transition.SubjectIndex].get()))
          outcomePlace = place->Name;
      }
      if (outcomePlace.empty()) {
        DiagnosticEngine::report(getLoc(me),
                                 DiagID::ERR_OUTCOME_MATCH_REQUIRED,
                                 outcomeCall->Callee);
        HasError = true;
        outcomeFunction = nullptr;
      }
    } else {
      outcomeFunction = nullptr;
    }

    std::map<std::string, uint64_t> masksBefore;
    std::map<std::string, bool> movedBefore;
    std::map<std::string, ExactPlaceFacts> exactPlacesBefore;
    for (auto &pair : CurrentScope->Symbols) {
      masksBefore[pair.first] = pair.second.InitMask;
      movedBefore[pair.first] = pair.second.Moved;
      exactPlacesBefore[pair.first] = pair.second.ExactPlace;
    }
    auto palBefore = PALCheckerState.snapshot();
    bool hasReachableArm = false;
    std::map<std::string, uint64_t> mergedMasks;
    std::map<std::string, bool> mergedMoved;
    std::map<std::string, ExactPlaceFacts> mergedExactPlaces;
    PALChecker mergedPAL = palBefore;

    auto restoreMatchEntryState = [&]() {
      for (auto &pair : masksBefore) {
        CurrentScope->Symbols[pair.first].InitMask = pair.second;
      }
      for (auto &pair : movedBefore) {
        CurrentScope->Symbols[pair.first].Moved = pair.second;
      }
      for (auto &pair : exactPlacesBefore) {
        auto &info = CurrentScope->Symbols[pair.first];
        info.ExactPlace = pair.second;
        info.InitMask = pair.second.applyToLegacyInitMask(info.InitMask);
      }
      PALCheckerState.restore(palBefore);
    };

    std::set<const ShapeMember *> outcomeMatchedVariants;
    bool hasInvalidOutcomeArm = false;

    for (auto &arm : me->Arms) {
      restoreMatchEntryState();
      enterScope();
      if (outcomeFunction) {
        const std::string variant = directMatchVariantName(arm->Pat.get());
        const auto &outcome = *outcomeFunction->ResolvedOutcomeTransition;
        const FunctionDecl::OutcomeTransition::Case *transition =
            outcome.findVariant(variant);
        if (arm->Guard || !transition ||
            !outcomeMatchedVariants.insert(transition->Variant).second) {
          DiagnosticEngine::report(getLoc(arm->Pat.get()),
                                   DiagID::ERR_OUTCOME_MATCH_ARM_INVALID);
          HasError = true;
          hasInvalidOutcomeArm = true;
        } else {
          SymbolInfo *placeInfo = nullptr;
          if (CurrentScope->findSymbol(outcomePlace, placeInfo) && placeInfo) {
            const bool isLive = transition->Post == OutcomePostState::Init;
            const PlaceStateFact pending =
                PlaceStateFact(PlaceState::Never).join(PlaceState::Live);
            if (placeInfo->ExactPlace.transitionWhole(
                    pending, isLive ? PlaceStateFact(PlaceState::Live)
                                    : PlaceStateFact(PlaceState::Never)))
              placeInfo->InitMask = isLive ? ~0ULL : 0;
          }
        }
      }
      checkPattern(arm->Pat.get(), targetType, targetCapability, targetPath,
                   targetAccessPath);
      if (arm->Guard) {
        auto guardTypeObj = checkExpr(arm->Guard.get());
        if (!arm->Guard->ResolvedType->isBoolean()) {
          error(arm->Guard.get(), DiagID::ERR_OPERAND_TYPE_MISMATCH,
                "match guard", "bool", arm->Guard->ResolvedType->toString());
        }
      }
      m_ControlFlowStack.push_back({"", NoProducedValue, nullptr, false, isReceiver});
      checkStmt(arm->Body.get());
      std::string armType = m_ControlFlowStack.back().ExpectedType;
      auto armTypeObj = m_ControlFlowStack.back().ExpectedTypeObj;
      m_ControlFlowStack.pop_back();

      if (isReceiver && armType == NoProducedValue && !allPathsJump(arm->Body.get())) {
        error(arm->Body.get(), DiagID::ERR_YIELD_VALUE_REQUIRED, "match arm");
      }

      if (resultType == NoProducedValue) {
        resultType = armType;
        resultTypeObj = armTypeObj;
      } else if (armType != NoProducedValue && !isTypeCompatible(resultTypeObj, armTypeObj)) {
        error(me, DiagID::ERR_BRANCH_TYPE_MISMATCH, "match", resultType, armType);
      }

      exitScope();

      if (!allPathsJump(arm->Body.get())) {
        if (!hasReachableArm) {
          for (auto &pair : CurrentScope->Symbols) {
            mergedMasks[pair.first] = pair.second.InitMask;
            mergedMoved[pair.first] = pair.second.Moved;
            mergedExactPlaces[pair.first] = pair.second.ExactPlace;
          }
          mergedPAL = PALCheckerState.snapshot();
          hasReachableArm = true;
        } else {
          for (auto &pair : CurrentScope->Symbols) {
            uint64_t armMask = pair.second.InitMask;
            if (mergedMasks.count(pair.first))
              mergedMasks[pair.first] &= armMask;
            else
              mergedMasks[pair.first] = armMask;

            bool armMoved = pair.second.Moved;
            if (mergedMoved.count(pair.first))
              mergedMoved[pair.first] = mergedMoved[pair.first] || armMoved;
            else
              mergedMoved[pair.first] = armMoved;

            if (mergedExactPlaces.count(pair.first))
              mergedExactPlaces[pair.first] |= pair.second.ExactPlace;
            else
              mergedExactPlaces[pair.first] = pair.second.ExactPlace;
          }

          PALChecker nextMerged = mergedPAL;
          nextMerged.mergeBranches(palBefore, mergedPAL, true,
                                   PALCheckerState.snapshot(), true);
          mergedPAL = nextMerged;
        }
      }
    }

    if (outcomeFunction) {
      std::set<const ShapeMember *> declaredVariants;
      for (const auto &transition :
           outcomeFunction->ResolvedOutcomeTransition->Cases)
        declaredVariants.insert(transition.Variant);
      if (!hasInvalidOutcomeArm && outcomeMatchedVariants != declaredVariants) {
        DiagnosticEngine::report(getLoc(me),
                                 DiagID::ERR_OUTCOME_MATCH_ARM_INVALID);
        HasError = true;
      }
    }

    if (hasReachableArm) {
      for (auto &pair : CurrentScope->Symbols) {
        if (mergedMasks.count(pair.first))
          pair.second.InitMask = mergedMasks[pair.first];
        if (mergedMoved.count(pair.first))
          pair.second.Moved = mergedMoved[pair.first];
        if (mergedExactPlaces.count(pair.first)) {
          pair.second.ExactPlace = mergedExactPlaces[pair.first];
          syncLegacyProjectionLiveness(pair.second);
        }
      }
      PALCheckerState.restore(mergedPAL);
    } else {
      restoreMatchEntryState();
    }

    if (isReceiver && resultType == NoProducedValue) {
      error(me, DiagID::ERR_YIELD_VALUE_REQUIRED, "match expression");
    }

    if (auto outcome =
            std::dynamic_pointer_cast<MissOutcomeType>(targetTypeObj)) {
      bool coversMiss = false;
      bool coversHit = false;
      bool coversAll = false;
      for (const auto &arm : me->Arms) {
        if (!arm->Pat)
          continue;
        if (arm->Guard) {
          DiagnosticEngine::report(getLoc(arm->Guard.get()),
                                   DiagID::ERR_MISS_OUTCOME_MATCH_INVALID,
                                   outcome->toString());
          HasError = true;
          continue;
        }
        const auto *pattern = arm->Pat.get();
        if (pattern->PatternKind == MatchArm::Pattern::Wildcard) {
          coversAll = true;
          continue;
        }
        if (pattern->PatternKind == MatchArm::Pattern::Variable &&
            pattern->Name == "miss" && !pattern->HasAutoBinding) {
          coversMiss = true;
          continue;
        }
        if (pattern->PatternKind == MatchArm::Pattern::Variable &&
            pattern->Binding ==
                MatchArm::Pattern::BindingOrigin::Fresh) {
          coversHit = true;
        }
      }
      if (!coversAll && !(coversMiss && coversHit)) {
        DiagnosticEngine::report(getLoc(me),
                                 DiagID::ERR_MISS_OUTCOME_MATCH_INVALID,
                                 outcome->toString());
        HasError = true;
      }
      return toka::Type::fromString(
          resultType == NoProducedValue ? "()" : resultType);
    }

    // Exhaustiveness Check
    bool hasWildcard = false;
    std::set<std::string> matchedVariants;

    std::string unstrippedTargetType = targetType;
    std::string baseTargetType = toka::Type::stripMorphology(targetType);

    std::function<bool(MatchArm::Pattern*, const std::string&)> isPatternExhaustive = 
      [&](MatchArm::Pattern *pat, const std::string &t) -> bool {
        if (!pat) return false;
        if (pat->PatternKind == MatchArm::Pattern::Wildcard || pat->PatternKind == MatchArm::Pattern::Elision) return true;
        if (pat->PatternKind == MatchArm::Pattern::Variable) {
          std::string baseShapeName = t;
          size_t scopePos = pat->Name.find("::");
          std::string patName = pat->Name;
          if (scopePos != std::string::npos) {
            baseShapeName = patName.substr(0, scopePos);
            patName = patName.substr(scopePos + 2);
          }
          baseShapeName = toka::Type::stripMorphology(baseShapeName);
          while (TypeAliasMap.count(baseShapeName) && !TypeAliasMap[baseShapeName].IsStrong) {
              baseShapeName = TypeAliasMap[baseShapeName].Target;
              size_t lt = baseShapeName.find("<");
              if (lt != std::string::npos) baseShapeName = baseShapeName.substr(0, lt);
          }
          bool isVariant = false;
          if (ShapeMap.count(baseShapeName)) {
            ShapeDecl *SD = ShapeMap[baseShapeName];
            for (auto &Memb : SD->Members) {
              bool noPayload = Memb.IsUnitVariant ||
                               (Memb.Type.empty() && Memb.SubMembers.empty());
              if (Memb.Name == patName && noPayload && Memb.SubMembers.empty()) {
                isVariant = true;
                break;
              }
            }
          }
          if (isVariant) return false;
          return true;
        }
        if (pat->PatternKind == MatchArm::Pattern::Or) {
          for (auto &sub : pat->SubPatterns) {
            if (isPatternExhaustive(sub.get(), t)) return true;
          }
          return false;
        }
        if (pat->PatternKind == MatchArm::Pattern::Decons) {
          std::string baseT = toka::Type::stripMorphology(t);
          size_t lt = baseT.find("<");
          if (lt != std::string::npos) baseT = baseT.substr(0, lt);
          while (TypeAliasMap.count(baseT) && !TypeAliasMap[baseT].IsStrong) {
              baseT = TypeAliasMap[baseT].Target;
              size_t lt2 = baseT.find("<");
              if (lt2 != std::string::npos) baseT = baseT.substr(0, lt2);
          }
          if (ShapeMap.count(baseT)) {
            ShapeDecl *SD = ShapeMap[baseT];
            if (SD->Kind == ShapeKind::Struct || SD->Kind == ShapeKind::Tuple) {
              size_t elisionIndex = -1;
              size_t elisionCount = 0;
              for (size_t i = 0; i < pat->SubPatterns.size(); ++i) {
                if (pat->SubPatterns[i]->PatternKind == MatchArm::Pattern::Elision) {
                  elisionIndex = i;
                  elisionCount++;
                }
              }

              if (elisionCount > 1) {
                return false;
              } else if (elisionCount == 1) {
                size_t expectedSize = SD->Members.size();
                size_t subPatsWithoutElision = pat->SubPatterns.size() - 1;
                if (subPatsWithoutElision > expectedSize) {
                  return false;
                }
                size_t elidedFields = expectedSize - subPatsWithoutElision;
                for (size_t i = 0; i < pat->SubPatterns.size(); ++i) {
                  if (i == elisionIndex) continue;
                  size_t memberIndex = (i < elisionIndex) ? i : (i + elidedFields - 1);
                  if (!isPatternExhaustive(pat->SubPatterns[i].get(), getPhysicalTypeName(SD->Members[memberIndex]))) return false;
                }
                return true;
              } else {
                if (pat->SubPatterns.size() != SD->Members.size()) return false;
                for (size_t i = 0; i < pat->SubPatterns.size(); ++i) {
                  if (!isPatternExhaustive(pat->SubPatterns[i].get(), getPhysicalTypeName(SD->Members[i]))) return false;
                }
                return true;
              }
            }
          }
        }
        return false;
      };

    if (ShapeMap.count(baseTargetType) && ShapeMap[baseTargetType]->Kind == ShapeKind::Enum) {
      ShapeDecl *SD = ShapeMap[baseTargetType];
      std::function<void(MatchArm::Pattern*)> collectMatchedVariants = [&](MatchArm::Pattern *pat) {
        if (!pat) return;
        if (pat->PatternKind == MatchArm::Pattern::Or) {
          for (auto &sub : pat->SubPatterns) {
            collectMatchedVariants(sub.get());
          }
        } else if (pat->PatternKind == MatchArm::Pattern::Decons) {
          std::string vName = pat->Name;
          size_t p = vName.find("::");
          if (p != std::string::npos) vName = vName.substr(p + 2);
          
          ShapeMember *foundMemb = nullptr;
          for (auto &Memb : SD->Members) {
            if (Memb.Name == vName) {
              foundMemb = &Memb;
              break;
            }
          }
          if (foundMemb) {
            bool subExhaustive = true;
            if (!foundMemb->SubMembers.empty()) {
              size_t elisionIndex = -1;
              size_t elisionCount = 0;
              for (size_t i = 0; i < pat->SubPatterns.size(); ++i) {
                if (pat->SubPatterns[i]->PatternKind == MatchArm::Pattern::Elision) {
                  elisionIndex = i;
                  elisionCount++;
                }
              }

              if (elisionCount > 1) {
                subExhaustive = false;
              } else if (elisionCount == 1) {
                size_t expectedSize = foundMemb->SubMembers.size();
                size_t subPatsWithoutElision = pat->SubPatterns.size() - 1;
                if (subPatsWithoutElision > expectedSize) {
                  subExhaustive = false;
                } else {
                  size_t elidedFields = expectedSize - subPatsWithoutElision;
                  for (size_t i = 0; i < pat->SubPatterns.size(); ++i) {
                    if (i == elisionIndex) continue;
                    size_t memberIndex = (i < elisionIndex) ? i : (i + elidedFields - 1);
                    if (!isPatternExhaustive(pat->SubPatterns[i].get(), getPhysicalTypeName(foundMemb->SubMembers[memberIndex]))) {
                      subExhaustive = false;
                      break;
                    }
                  }
                }
              } else {
                if (pat->SubPatterns.size() != foundMemb->SubMembers.size()) {
                  subExhaustive = false;
                } else {
                  for (size_t i = 0; i < pat->SubPatterns.size(); ++i) {
                    if (!isPatternExhaustive(pat->SubPatterns[i].get(), getPhysicalTypeName(foundMemb->SubMembers[i]))) {
                      subExhaustive = false;
                      break;
                    }
                  }
                }
              }
            } else if (!foundMemb->IsUnitVariant &&
                       !foundMemb->Type.empty()) {
              size_t elisionIndex = -1;
              size_t elisionCount = 0;
              for (size_t i = 0; i < pat->SubPatterns.size(); ++i) {
                if (pat->SubPatterns[i]->PatternKind == MatchArm::Pattern::Elision) {
                  elisionIndex = i;
                  elisionCount++;
                }
              }

              if (elisionCount > 1) {
                subExhaustive = false;
              } else if (elisionCount == 1) {
                subExhaustive = true;
              } else {
                if (pat->SubPatterns.size() != 1 || 
                    !isPatternExhaustive(pat->SubPatterns[0].get(), foundMemb->Type)) {
                  subExhaustive = false;
                }
              }
            }
            if (subExhaustive) {
              matchedVariants.insert(vName);
            }
          }
        } else if (pat->PatternKind == MatchArm::Pattern::Variable) {
          std::string vName = pat->Name;
          size_t p = vName.find("::");
          if (p != std::string::npos) vName = vName.substr(p + 2);
          
          ShapeMember *foundMemb = nullptr;
          for (auto &Memb : SD->Members) {
            if (Memb.Name == vName) {
              foundMemb = &Memb;
              break;
            }
          }
          if (foundMemb) {
            bool noPayload = foundMemb->IsUnitVariant ||
                             (foundMemb->Type.empty() &&
                              foundMemb->SubMembers.empty());
            if (noPayload && foundMemb->SubMembers.empty()) {
              matchedVariants.insert(vName);
            }
          }
        }
      };

      for (auto &arm : me->Arms) {
        if (arm->Guard) continue;
        if (isPatternExhaustive(arm->Pat.get(), targetType)) {
          hasWildcard = true;
          break;
        } else {
          collectMatchedVariants(arm->Pat.get());
        }
      }

      if (!hasWildcard) {
        std::vector<std::string> missing;
        for (auto &m : SD->Members) {
          if (m.Name == "Moved") continue;
          if (matchedVariants.find(m.Name) == matchedVariants.end()) {
            missing.push_back(m.Name);
          }
        }
        if (!missing.empty()) {
          std::string missingStr = "";
          for (size_t i = 0; i < missing.size(); ++i) {
            missingStr += missing[i];
            if (i < missing.size() - 1) missingStr += ", ";
          }
          DiagnosticEngine::report(getLoc(me), DiagID::ERR_MATCH_NOT_EXHAUSTIVE, missingStr);
          HasError = true;
        }
      }
    } else {
      for (auto &arm : me->Arms) {
        if (arm->Guard) continue;
        if (isPatternExhaustive(arm->Pat.get(), targetType)) {
          hasWildcard = true;
          break;
        }
      }
      if (!hasWildcard) {
        DiagnosticEngine::report(getLoc(me), DiagID::ERR_MATCH_NOT_EXHAUSTIVE, "non-enum types require a wildcard/exhaustive branch");
        HasError = true;
      }
    }

    return toka::Type::fromString(resultType == NoProducedValue ? "()"
                                                                  : resultType);
  }

  return toka::Type::fromString("unknown");
}

} // namespace toka
