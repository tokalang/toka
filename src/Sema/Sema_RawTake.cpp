#include "toka/Sema.h"
#include <algorithm>
#include <functional>

namespace toka {

std::shared_ptr<Type> Sema::checkRawTakeExpr(RawTakeExpr *take) {
  take->Plan.reset();
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
        for (const auto &payload : field.SubMembers) complete &= fieldComplete(payload);
      } else complete &= fieldComplete(field);
    }
    typeStack.erase(shape->Decl);
    return complete;
  };
  if (!dependencyFree(element)) return reject("ElementDependenciesUnproven");
  const auto source = canonicalizeAccessPath(makeAccessPath(index));
  if (!source || !source.RootID) return reject("ExactSourceRequired");
  if (PALCheckerState.verifyInvalidation(source)) return reject("ActiveBorrowConflict");
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
  index->ResolvedType = element;
  // No managed place state is changed: live-slot retirement is an explicit
  // unsafe postcondition. The raw allocation/remainder remains with its owner.
  m_LastInitMask = ~0ULL;
  return element;
}

} // namespace toka
