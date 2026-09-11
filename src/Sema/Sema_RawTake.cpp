#include "toka/Sema.h"
#include <algorithm>
#include <functional>

namespace toka {

namespace {
Expr *slotSource(Expr *expression) {
  while (expression) {
    if (auto *unsafe = dynamic_cast<UnsafeExpr *>(expression)) expression = unsafe->Expression.get();
    else if (auto *cast = dynamic_cast<CastExpr *>(expression);
             cast && (cast->Kind == CastKind::Implicit || cast->Kind == CastKind::Ascription))
      expression = cast->Expression.get();
    else break;
  }
  return expression;
}
}

std::optional<AccessPath> Sema::qualifiedRawSlot(ArrayIndexExpr *slot,
                                              std::string *allocationSource,
                                              const AllocExpr **allocationResult,
                                              uint64_t *indexBinding) {
  if (!slot || slot->Indices.size() != 1 || !CurrentFunction) return {};
  auto *base = dynamic_cast<VariableExpr *>(slot->Array.get());
  auto *index = dynamic_cast<NumberExpr *>(slotSource(slot->Indices[0].get()));
  uint64_t indexID = 0;
  if (!index) {
    auto *variable = dynamic_cast<VariableExpr *>(slotSource(slot->Indices[0].get()));
    auto indexPath = variable ? makeAccessPath(variable) : AccessPath{};
    SymbolInfo *info = nullptr;
    if (!indexPath.RootID || !indexPath.Projections.empty() ||
        !CurrentScope->findSymbolByID(indexPath.RootID, info) || !info ||
        !info->TypeObj || !info->TypeObj->isInteger() || info->TypeObj->IsWritable ||
        info->Permission.SoulWritable || info->Permission.IdentityRebindable ||
        m_ReturnSourceUnknownRoots.count(indexPath.RootID) ||
        info->IsPlaceAlias || info->IsFunctionParameter || info->Moved || !info->ASTPtr)
      return {};
    auto *decl = dynamic_cast<VariableDecl *>(static_cast<ASTNode *>(info->ASTPtr));
    auto owner = decl ? m_LocalVariableOwners.find(decl) : m_LocalVariableOwners.end();
    if (!decl || owner == m_LocalVariableOwners.end() || owner->second != CurrentFunction)
      return {};
    // Deliberately retain the literal-extent and proven in-range requirement.
    // No runtime index, initializer replay by name, or unknown bound is admitted.
    index = dynamic_cast<NumberExpr *>(slotSource(decl->Init.get()));
    indexID = indexPath.RootID;
  }
  if (!base || !index) return {};
  auto path = canonicalizeAccessPath(makeAccessPath(slot));
  if (indexID && path.Projections.size() == 1 &&
      path.Projections.front().Kind == AccessProjectionKind::DynamicIndex)
    path.Projections.front() = AccessProjection::constantIndex(index->Value);
  SymbolInfo *binding = nullptr;
  if (!path.RootID || path.Projections.size() != 1 ||
      path.Projections.front().Kind != AccessProjectionKind::ConstantIndex ||
      !CurrentScope->findSymbolByID(path.RootID, binding) || !binding ||
      binding->IsFunctionParameter || binding->IsPlaceAlias || binding->Permission.IdentityRebindable ||
      !binding->TypeObj || !binding->TypeObj->isRawPointer() || !binding->ASTPtr) return {};
  auto *declaration = dynamic_cast<VariableDecl *>(static_cast<ASTNode *>(binding->ASTPtr));
  auto owner = declaration ? m_LocalVariableOwners.find(declaration) : m_LocalVariableOwners.end();
  if (!declaration || owner == m_LocalVariableOwners.end() || owner->second != CurrentFunction) return {};
  auto *allocation = dynamic_cast<AllocExpr *>(slotSource(declaration->Init.get()));
  auto *extent = allocation ? dynamic_cast<NumberExpr *>(slotSource(allocation->ArraySize.get())) : nullptr;
  if (!allocation || !allocation->IsArray || allocation->Initializer || !extent ||
      index->Value >= extent->Value || !allocation->RawAddressValueFacts ||
      !allocation->RawAddressValueFacts->AllocationAncestry) return {};
  const auto &ancestry = *allocation->RawAddressValueFacts->AllocationAncestry;
  if (ancestry.SourceEdge.empty() || !ancestry.IsArray || ancestry.HasInitializerSyntax) return {};
  if (allocationSource) *allocationSource = ancestry.SourceEdge;
  if (allocationResult) *allocationResult = allocation;
  if (indexBinding) *indexBinding = indexID;
  return path;
}

bool Sema::rawSlotValueHasNoBorrows(const std::shared_ptr<Type> &input) {
  std::set<const ShapeDecl *> active;
  std::function<bool(std::shared_ptr<Type>)> visit = [&](std::shared_ptr<Type> type) {
    type = resolveExplicitCedeStage0TypeReadOnly(type);
    if (!type || type->isUnknown() || type->isUninit() || type->isReference() ||
        type->isFunction() || type->isDynFn()) return false;
    if (hasCanonicalOwningStringStorage(type)) return true;
    // This row is used only alongside a current whole-value certificate
    // establishing that opaque raw fields are null. No raw pointee is read.
    if (type->isRawPointer()) return true;
    if (queryExplicitCedeStage0OwnershipReadOnly(type) == ValueOwnership::BorrowedView) return false;
    if (type->isUniquePtr() || type->isSharedPtr()) return visit(type->getPointeeType());
    if (type->isArray()) return visit(type->getArrayElementType());
    if (!type->isShape()) return type->typeKind == Type::Primitive;
    auto shape = std::dynamic_pointer_cast<ShapeType>(type);
    if (!shape || !shape->Decl || !active.insert(shape->Decl).second) return false;
    std::map<std::string, std::shared_ptr<Type>> substitutions;
    if (!shape->Decl->GenericParams.empty()) {
      if (shape->GenericArgs.size() != shape->Decl->GenericParams.size()) return false;
      for (size_t index = 0; index < shape->GenericArgs.size(); ++index)
        substitutions[shape->Decl->GenericParams[index].Name] = shape->GenericArgs[index];
    }
    auto fieldClosed = [&](const ShapeMember &field) {
      auto fieldType = getPhysicalType(field);
      if (fieldType && !substitutions.empty()) fieldType = fieldType->substitute(substitutions);
      return fieldType && visit(fieldType);
    };
    bool closed = true;
    for (const auto &field : shape->Decl->Members) {
      if (shape->Decl->Kind == ShapeKind::Enum) {
        if (!field.IsUnitVariant && field.SubMembers.empty()) closed &= fieldClosed(field);
        for (const auto &payload : field.SubMembers) closed &= fieldClosed(payload);
      } else closed &= fieldClosed(field);
    }
    active.erase(shape->Decl);
    return closed;
  };
  return visit(input);
}

void Sema::recordRawSlotWrite(BinaryExpr *assignment) {
  if (!assignment || !assignment->RawStorageWrite) return;
  auto *slot = dynamic_cast<ArrayIndexExpr *>(assignment->LHS.get());
  std::string allocation;
  const AllocExpr *allocationExpression = nullptr;
  uint64_t indexBinding = 0;
  auto path = qualifiedRawSlot(slot, &allocation, &allocationExpression, &indexBinding);
  if (!path) return;
  Expr *value = assignment->RHS.get();
  while (value && !value->KnownNullRawStorageType) {
    auto *next = slotSource(value);
    if (next == value) break;
    value = next;
  }
  auto proofType = value ? value->KnownNullRawStorageType : nullptr;
  auto element = assignment->RawStorageWrite->ElementType;
  if (!proofType || !element || !rawSlotValueHasNoBorrows(element) ||
      !proofType->withAttributes(false, proofType->IsNullable, proofType->IsBlocked)->equals(
          *element->withAttributes(false, element->IsNullable, element->IsBlocked))) return;
  auto evidence = std::make_shared<RawSlotDependencyEvidence>();
  evidence->Slot = *path;
  evidence->IndexBinding = indexBinding;
  evidence->ElementType = element;
  evidence->Write = assignment;
  evidence->ValueEdge = value;
  evidence->Allocation = allocationExpression;
  evidence->NoBorrowedValueFields = true;
  evidence->AllocationSourceEdge = std::move(allocation);
  m_RawSlotDependencies[*path] = std::move(evidence);
}

std::map<AccessPath, RawSlotDependencyEvidencePtr> Sema::joinRawSlots(
    const std::map<AccessPath, RawSlotDependencyEvidencePtr> &left,
    const std::map<AccessPath, RawSlotDependencyEvidencePtr> &right) {
  std::map<AccessPath, RawSlotDependencyEvidencePtr> joined;
  for (const auto &[slot, value] : left) {
    auto other = right.find(slot);
    if (other == right.end() || !value || !other->second) continue;
    const auto &rhs = other->second;
    if (value == rhs) { joined[slot] = value; continue; }
    if (!value->NoBorrowedValueFields || !rhs->NoBorrowedValueFields ||
        value->IndexBinding != rhs->IndexBinding ||
        !value->ElementType || !rhs->ElementType || !value->ElementType->equals(*rhs->ElementType) ||
        value->Allocation != rhs->Allocation || value->AllocationSourceEdge != rhs->AllocationSourceEdge) continue;
    auto result = std::make_shared<RawSlotDependencyEvidence>(*value);
    result->Write = nullptr;
    result->ValueEdge = nullptr;
    result->Alternatives.clear();
    std::set<const RawSlotDependencyEvidence *> seen;
    auto append = [&](const RawSlotDependencyEvidencePtr &candidate) {
      if (candidate->Alternatives.empty()) {
        if (seen.insert(candidate.get()).second) result->Alternatives.push_back(candidate);
      } else for (const auto &leaf : candidate->Alternatives)
        if (leaf && seen.insert(leaf.get()).second) result->Alternatives.push_back(leaf);
    };
    append(value);
    append(rhs);
    joined[slot] = std::move(result);
  }
  return joined;
}

std::shared_ptr<Type> Sema::checkRawTakeExpr(RawTakeExpr *take) {
  take->Plan.reset();
  take->RecordedSlotProofRequired = false;
  const std::vector<std::unique_ptr<Expr>> noArguments;
  CallArgumentRollbackGuard rollback(*this, noArguments, true);
  const size_t diagnosticStart = DiagnosticEngine::records().size();
  auto reject = [&](const char *reason) {
    error(take, DiagID::ERR_SEMA_RAW_TAKE_REJECTED, reason);
    rollback.reject();
    return Type::fromString("unknown");
  };
  if (!m_InUnsafeContext) return reject("UnsafeContextRequired");
  // The synchronous morphic shared-parameter lowering is not qualified: its
  // legacy declaration flags can disagree with the resolved two-word handle.
  // Do not admit the new primitive in that body until the separate ABI path
  // is fixed. This guard neither changes that ABI nor activates generic cede.
  if (CurrentFunction && CurrentFunction->TemplateOrigin &&
      CurrentFunction->Effect != EffectKind::Async) {
    for (const auto &arg : CurrentFunction->Args)
      if (arg.IsMorphicExempt && arg.IsCeded && !arg.IsShared &&
          arg.ResolvedType && arg.ResolvedType->isSharedPtr())
        return reject("UnqualifiedSharedGenericParameter");
  }
  auto *index = dynamic_cast<ArrayIndexExpr *>(take->Slot.get());
  if (!index || index->Indices.size() != 1)
    return reject("RawStorageIndexRequired");
  // A hatted base selects handle arithmetic, not the stored element.
  if (getSyntacticMorphology(index->Array.get()) != MorphKind::None)
    return reject("RawElementViewRequired");

  const bool oldCollapse = m_DisableSoulCollapse;
  m_DisableSoulCollapse = true;
  auto storageType = checkExpr(index->Array.get(), nullptr);
  m_DisableSoulCollapse = oldCollapse;
  auto indexType = checkExpr(index->Indices.front().get(), nullptr);
  if (!storageType || !storageType->isRawPointer())
    return reject("RawStorageRequired");
  if (!indexType || !indexType->isInteger())
    return reject("IntegerIndexRequired");
  if (storageType->IsNullable &&
      !m_NarrowedPaths.count(getPathString(index->Array.get())))
    return reject("NonNullStorageRequired");
  auto storage = resolveType(storageType->getPointeeType(), true);
  if (!storage || (!storage->isSlice() && !storage->isArray()))
    return reject("RawArrayStorageRequired");
  auto element = resolveType(storage->getArrayElementType(), true);
  if (!element || element->isUnknown() || element->isUninit() ||
      element->isVoid() || element->isNever())
    return reject("CompleteElementTypeRequired");

  // First-batch provenance: immutable raw bindings from alloc or a raw formal.
  // Neither a cast from managed storage nor an unknown/rebound local address
  // can acquire retirement authority. This is NOT an initialized-extent proof.
  std::set<uint64_t> visiting;
  std::function<bool(Expr *)> rawOrigin = [&](Expr *source) {
    if (auto *unsafe = dynamic_cast<UnsafeExpr *>(source))
      return rawOrigin(unsafe->Expression.get());
    if (auto *unary = dynamic_cast<UnaryExpr *>(source);
        unary && unary->Op == TokenType::Star)
      return rawOrigin(unary->RHS.get());
    if (auto *allocation = dynamic_cast<AllocExpr *>(source))
      return allocation->IsArray && allocation->ResolvedType &&
             allocation->ResolvedType->isRawPointer();
    auto *variable = dynamic_cast<VariableExpr *>(source);
    if (!variable) return false;
    SymbolInfo *info = nullptr;
    std::string name;
    if (!CurrentScope->findVariableWithDeref(variable->Name, info, name) ||
        !info || !info->TypeObj || !info->TypeObj->isRawPointer() ||
        info->IsPlaceAlias || info->Permission.IdentityRebindable ||
        m_ReturnSourceUnknownRoots.count(info->SymbolID) ||
        !visiting.insert(info->SymbolID).second)
      return false;
    if (info->IsFunctionParameter) return info->LifeDependencySet.empty();
    auto *declaration = info->ASTPtr
        ? dynamic_cast<VariableDecl *>(static_cast<ASTNode *>(info->ASTPtr)) : nullptr;
    return declaration && declaration->Init && rawOrigin(declaration->Init.get());
  };
  if (!rawOrigin(index->Array.get()))
    return reject("RawStorageOriginUnprovenOrManaged");

  // Read the full declared element morphology, never a hat-off payload type.
  // Do not infer dependency freedom from absence of diagnostics or from Drop.
  std::set<const ShapeDecl *> typeStack;
  std::function<bool(std::shared_ptr<Type>)> dependencyFree =
      [&](std::shared_ptr<Type> type) -> bool {
    type = resolveType(type, true);
    if (hasCanonicalOwningStringStorage(type)) return true;
    if (!type || type->isUnknown() || type->isUninit() || type->isRawPointer() ||
        type->isReference() || type->isFunction() || type->isDynFn() || type->isSlice())
      return false;
    if (type->isUniquePtr() || type->isSharedPtr())
      return dependencyFree(type->getPointeeType());
    if (type->isArray()) return dependencyFree(type->getArrayElementType());
    if (type->isBoolean() || type->isInteger() || type->isFloatingPoint() || type->isUnit())
      return true;
    auto shape = std::dynamic_pointer_cast<ShapeType>(type);
    if (!shape || !shape->Decl || !typeStack.insert(shape->Decl).second) return false;
    std::map<std::string, std::shared_ptr<Type>> substitutions;
    if (!shape->Decl->GenericParams.empty()) {
      if (shape->GenericArgs.size() != shape->Decl->GenericParams.size()) return false;
      for (size_t i = 0; i < shape->GenericArgs.size(); ++i)
        substitutions[shape->Decl->GenericParams[i].Name] = shape->GenericArgs[i];
    }
    auto fieldComplete = [&](const ShapeMember &field) {
      auto fieldType = getPhysicalType(field);
      if (fieldType && !substitutions.empty()) fieldType = fieldType->substitute(substitutions);
      return fieldType && dependencyFree(fieldType);
    };
    bool complete = true;
    for (const auto &field : shape->Decl->Members) {
      if (shape->Decl->Kind == ShapeKind::Enum) {
        if (!field.IsUnitVariant && field.SubMembers.empty()) complete &= fieldComplete(field);
        for (const auto &payload : field.SubMembers) complete &= fieldComplete(payload);
      } else complete &= fieldComplete(field);
    }
    typeStack.erase(shape->Decl);
    return complete;
  };
  RawSlotDependencyEvidencePtr stored;
  if (!dependencyFree(element)) {
    uint64_t indexBinding = 0;
    auto slot = qualifiedRawSlot(index, nullptr, nullptr, &indexBinding);
    auto found = slot ? m_RawSlotDependencies.find(*slot) : m_RawSlotDependencies.end();
    if (found == m_RawSlotDependencies.end() || !found->second ||
        found->second->IndexBinding != indexBinding ||
        !found->second->ElementType || !found->second->ElementType->equals(*element) ||
        (found->second->Alternatives.empty() &&
         (!found->second->ValueEdge || !found->second->ValueEdge->KnownNullRawStorageType)) ||
        !found->second->NoBorrowedValueFields)
      return reject("ElementDependenciesUnproven");
    stored = found->second;
    for (const auto &leaf : stored->Alternatives) {
      if (!leaf || !leaf->Alternatives.empty() || !leaf->NoBorrowedValueFields ||
          leaf->IndexBinding != stored->IndexBinding ||
          !(leaf->Slot == stored->Slot) || leaf->Allocation != stored->Allocation ||
          leaf->AllocationSourceEdge != stored->AllocationSourceEdge ||
          !leaf->ElementType || !leaf->ElementType->equals(*element) ||
          !leaf->Write || !leaf->ValueEdge || !leaf->ValueEdge->KnownNullRawStorageType)
        return reject("ElementDependenciesUnproven");
    }
  }
  const auto rawSource = canonicalizeAccessPath(makeAccessPath(index));
  const auto source = stored ? stored->Slot : rawSource;
  if (!source || !source.RootID) return reject("ExactSourceRequired");
  if (PALCheckerState.verifyInvalidation(rawSource)) return reject("ActiveBorrowConflict");
  auto capability = queryExplicitCedeStage0AccessCapabilityReadOnly(index);
  auto elementPayload = element->isPointer() ? element->getPointeeType() : element;
  // A read-only shared handle still transfers its existing reference. The
  // flow ceiling forbids gaining payload write authority, not that transfer.
  if (capability.PayloadFlowRestricted &&
      (!elementPayload || elementPayload->IsWritable))
    return reject("AccessCapabilityMismatch");
  const auto ownership = queryExplicitCedeStage0OwnershipReadOnly(element);
  auto copy = queryExplicitCedeStage0CopyProof(element);
  if (!ownership) return reject("ElementCleanupUnproven");
  if (copy == TransferCopyProof::Indeterminate) return reject("ElementCopyProofUnproven");
  const auto &records = DiagnosticEngine::records();
  if (std::any_of(records.begin() + diagnosticStart, records.end(),
                  [](const auto &r) { return r.Level == DiagLevel::Error; })) {
    rollback.reject();
    return Type::fromString("unknown");
  }
  RawElementTakePlan plan;
  if (stored) {
    plan.DependencyProof = RawElementTakePlan::DependencyProofKind::RecordedSlot;
    plan.RecordedSlot = stored;
  }
  plan.SlotEdge = index;
  plan.BaseEdge = index->Array.get();
  plan.IndexEdge = index->Indices.front().get();
  plan.StorageType = storageType;
  plan.ElementType = element;
  plan.IndexType = indexType;
  plan.CopyProof = copy;
  plan.BaseKnownNonNull = true;
  plan.SourceSlot = source;
  plan.EdgeIdentity = makeExplicitCedeStage0NonCallGroupIdentity(take, "raw-take");
  if (plan.EdgeIdentity.empty()) return reject("SourceIdentityIncomplete");
  plan.Production = element->isSharedPtr() ? TransferValueProduction::TransferShared
      : copy == TransferCopyProof::ProvenCopy ? TransferValueProduction::CopyValue
                                            : TransferValueProduction::MoveOwned;
  plan.CarriesDropLiability = *ownership == ValueOwnership::Owned ||
                            *ownership == ValueOwnership::SharedHandle;
  plan.ResultCleanup = plan.CarriesDropLiability
      ? TransferDropDisposition::DestinationAssumesLiability
      : TransferDropDisposition::NoLiability;
  plan.DependencyFree = true;
  plan.UnsafeCallerPreconditions = plan.CallerMaintainsRemainder = true;
  plan.SemaValidated = true;
  take->Plan = std::move(plan);
  take->RecordedSlotProofRequired = stored != nullptr;
  if (stored) {
    // The load transfers the exact recorded value, without user code or a
    // clone. Preserve its existing storage certificate on the new value.
    take->KnownNullRawStorageType = element->withAttributes(
        element->IsWritable, element->IsNullable, element->IsBlocked);
  }
  if (auto exact = qualifiedRawSlot(index)) m_RawSlotDependencies.erase(*exact);
  else m_RawSlotDependencies.clear();
  index->ResolvedType = element;
  // No managed place state is changed: live-slot retirement is an explicit
  // unsafe postcondition. The raw allocation/remainder remains with its owner.
  m_LastInitMask = ~0ULL;
  return element;
}

} // namespace toka
