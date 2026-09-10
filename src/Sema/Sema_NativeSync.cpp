#include "toka/Sema.h"
#include <functional>
#include <set>

namespace toka {
void Sema::snapshotNativeSyncAllocation(NewExpr *allocation) {
  if (!allocation || !CurrentFunction || !CurrentFunction->ResolvedReturnType) return;
  auto result = CurrentFunction->ResolvedReturnType;
  if (!result->isUniquePtr() && !result->isSharedPtr()) return;
  auto owner = std::dynamic_pointer_cast<ShapeType>(result->getPointeeType());
  auto *module = getLexicalModule(CurrentFunction->Loc);
  if (!owner || !owner->Decl || !module || !module->SourceModule ||
      module->SourceModule->IsInterface || !module->IsTrustedSystemModule ||
      !module->ShadowCoordinateKnown || module->ShadowLogicalModulePath != "std/sync") return;
  bool nativeOwner = false;
  for (const char *name : {"Mutex", "RwMutex", "CondVar"}) {
    auto found = module->Shapes.find(name);
    nativeOwner |= found != module->Shapes.end() &&
        owner->Decl->InstantiationTemplate == found->second;
  }
  if (nativeOwner)
    m_NativeSyncAllocationSnapshots[allocation] = {CurrentFunction, captureAnalysisState()};
}

bool Sema::prepareNativeSyncAllocation(const NewExpr *allocation, const VariableDecl *binding,
                                     Expr *source, const NativeSyncOwnerCandidatePtr &recipe) {
  auto fail = [&](const char *reason) {
    error(source, DiagID::ERR_GENERIC_SEMA, std::string("native sync owner allocation: ") + reason);
    return false;
  };
  if (!allocation || !binding || !recipe || !recipe->Factory) return fail("MissingOwnerRecipe");
  auto snapshot = m_NativeSyncAllocationSnapshots.find(allocation);
  if (snapshot == m_NativeSyncAllocationSnapshots.end() || snapshot->second.Definition != CurrentFunction)
    return fail("MissingPreAllocationSnapshot");
  auto *transfer = dynamic_cast<CedeExpr *>(source);
  if (!transfer || !transfer->Value || !source->ResolvedType ||
      !recipe->Factory->OwnerType || !source->ResolvedType->equals(*recipe->Factory->OwnerType))
    return fail("PreparedOwnerTransferRequired");
  auto path = canonicalizeAccessPath(makeAccessPath(transfer->Value.get()));
  if (!path || !path.RootID || !path.Projections.empty()) return fail("WholePreparedOwnerRequired");
  const auto &before = snapshot->second.State;
  auto initialized = before.ExactPlaces.find(path.RootName);
  auto prior = before.NativeSyncOwnerRecipes.find(path.RootID);
  if (initialized == before.ExactPlaces.end() ||
      !initialized->second.whole().isExactly(PlaceState::Live) ||
      prior == before.NativeSyncOwnerRecipes.end() || prior->second != recipe)
    return fail("PreparedOwnerNotLiveAtAllocation");
  auto pal = before.PAL;
  if (pal.verifyInvalidation(path)) return fail("PreparedOwnerBorrowConflict");
  if (allocation->ArraySize || !allocation->Initializer || !allocation->Initializer->ResolvedType ||
      !allocation->Initializer->ResolvedType->equals(*recipe->Factory->OwnerType))
    return fail("OwnerAllocationTypeMismatch");
  // The checked SDK wrapper allocates only an empty carrier. If allocation of
  // its refcount then fails, freeing that carrier cannot discard a live native
  // object: all native responsibility is still in the prepared source above.
  std::map<std::string, Expr *> fields;
  if (auto *init = dynamic_cast<InitStructExpr *>(allocation->Initializer.get())) {
    for (auto &field : init->Members)
      if (!fields.emplace(field.first, field.second.get()).second) return fail("DuplicateEmptyField");
  } else if (auto *init = dynamic_cast<CallExpr *>(allocation->Initializer.get())) {
    for (auto &arg : init->Args) {
      auto *named = dynamic_cast<BinaryExpr *>(arg.get());
      auto *name = named ? dynamic_cast<VariableExpr *>(named->LHS.get()) : nullptr;
      if (!named || named->Op != "=" || !name || !fields.emplace(name->Name, named->RHS.get()).second)
        return fail("NamedEmptyCarrierRequired");
    }
  } else return fail("EmptyCarrierConstructionRequired");
  const bool nativeOnly = recipe->Factory->Kind == NativeSyncFactoryKind::CondVar;
  if (fields.size() != (nativeOnly ? 1u : 2u)) return fail("EmptyCarrierSchemaMismatch");
  for (auto &[name, expression] : fields) {
    if ((name != "handle" && (nativeOnly || name != "data_ptr")) ||
        !expression || !expression->ResolvedType || !expression->ResolvedType->isAddrType())
      return fail("EmptyCarrierSchemaMismatch");
    while (auto *cast = dynamic_cast<CastExpr *>(expression)) {
      if (cast->Kind != CastKind::Ascription) {
        error(source, DiagID::ERR_GENERIC_SEMA,
              "native sync owner allocation: EmptyCarrierLiteralRequired: " + expression->toString());
        return false;
      }
      expression = cast->Expression.get();
    }
    auto *zero = dynamic_cast<NumberExpr *>(expression);
    if (!zero || zero->Value != 0) {
      error(source, DiagID::ERR_GENERIC_SEMA,
            "native sync owner allocation: EmptyCarrierLiteralRequired: " + expression->toString());
      return false;
    }
  }
  auto plan = std::shared_ptr<NativeSyncAllocationPlan>(new NativeSyncAllocationPlan);
  plan->Allocation = allocation;
  plan->Binding = binding;
  plan->Definition = CurrentFunction;
  plan->PreparedOwner = transfer->Value.get();
  plan->OwnerType = source->ResolvedType;
  plan->ManagedType = binding->ResolvedType;
  auto *site = const_cast<NewExpr *>(allocation);
  site->NativeSyncAllocationRequired = true;
  site->NativeSyncAllocationSource = plan;
  m_PendingNativeSyncAllocations.push_back(plan);
  return true;
}

NativeSyncOwnerCandidatePtr Sema::collectNativeSyncOwnerRecipe(Expr *source) {
  if (!source || !source->ResolvedType) return {};
  NativeSyncOwnerCandidatePtr recipe;
  FunctionDecl *callee = nullptr;
  if (auto *call = dynamic_cast<CallExpr *>(source)) {
    if (call->NativeSyncFactorySource) {
      auto prepared = std::shared_ptr<NativeSyncOwnerCandidate>(new NativeSyncOwnerCandidate);
      prepared->Factory = call->NativeSyncFactorySource;
      prepared->OwnerEdge = source;
      prepared->Provider = CurrentFunction;
      prepared->ValueType = source->ResolvedType;
      recipe = std::move(prepared);
    } else callee = call->ResolvedFn;
  } else if (auto *method = dynamic_cast<MethodCallExpr *>(source)) callee = method->ResolvedFn;
  else if (auto *cede = dynamic_cast<CedeExpr *>(source)) recipe = cede->Value->NativeSyncOwnerRecipe;
  else if (auto *unsafe = dynamic_cast<UnsafeExpr *>(source)) recipe = unsafe->Expression->NativeSyncOwnerRecipe;
  else if (auto *ascription = dynamic_cast<CastExpr *>(source);
           ascription && ascription->Kind == CastKind::Ascription)
    recipe = ascription->Expression->NativeSyncOwnerRecipe;
  else if (auto *postfix = dynamic_cast<PostfixExpr *>(source)) recipe = postfix->LHS->NativeSyncOwnerRecipe;
  else if (auto *unary = dynamic_cast<UnaryExpr *>(source);
           unary && (source->ResolvedType->isUniquePtr() || source->ResolvedType->isSharedPtr()))
    recipe = unary->RHS->NativeSyncOwnerRecipe;
  else if (dynamic_cast<VariableExpr *>(source)) {
    auto path = canonicalizeAccessPath(makeAccessPath(source));
    if (path && path.Projections.empty()) {
      auto found = m_NativeSyncOwnerRecipes.find(path.RootID);
      if (found != m_NativeSyncOwnerRecipes.end()) recipe = found->second;
    }
  }
  if (callee) {
    const auto returned = m_NativeSyncOwnerReturns.find(callee);
    const auto checked = m_RawAddressReturns.find(callee);
    if (returned == m_NativeSyncOwnerReturns.end() || returned->second.size() != 1 ||
        !returned->second.front() || checked == m_RawAddressReturns.end() ||
        !checked->second.Checked || !checked->second.Valid) return {};
    if (callee->TemplateOrigin) {
      auto cached = InstantiationCache.find(callee->Name);
      if (cached == InstantiationCache.end() || !cached->second ||
          cached->second->Instance != callee ||
          cached->second->Validation != GenericSpecializationValidationState::Valid) return {};
    }
    auto returnedRecipe = returned->second.front();
    if (m_InvalidNativeSyncOwnerRecipes.count(returnedRecipe) || !callee->ResolvedReturnType ||
        !source->ResolvedType->equals(*callee->ResolvedReturnType)) return {};
    auto rebased = std::shared_ptr<NativeSyncOwnerCandidate>(new NativeSyncOwnerCandidate(*returnedRecipe));
    rebased->Parent = returnedRecipe;
    rebased->OwnerEdge = source;
    rebased->Provider = callee;
    rebased->ValueType = source->ResolvedType;
    recipe = std::move(rebased);
  }
  if (!recipe || !recipe->Factory || m_InvalidNativeSyncOwnerRecipes.count(recipe)) return {};
  auto valueType = source->ResolvedType;
  if (valueType->isUniquePtr() || valueType->isSharedPtr()) valueType = valueType->getPointeeType();
  auto shape = std::dynamic_pointer_cast<ShapeType>(valueType);
  const auto &factory = recipe->Factory;
  if (!shape || !shape->Decl || !factory->OwnerTemplate || !factory->ElementType ||
      shape->Decl->InstantiationTemplate != factory->OwnerTemplate ||
      shape->Decl->InstantiationArgs.size() != 1 || !shape->Decl->InstantiationArgs[0] ||
      !shape->Decl->InstantiationArgs[0]->equals(*factory->ElementType)) return {};
  // Pending recipes are deliberately separate from NativeSyncFactoryOrigin.
  // This path neither sets Validated nor supplies environment/CodeGen authority.
  return recipe;
}

void Sema::recordNativeSyncOwnerRecipe(const AccessPath &rawPlace, Expr *source, bool initialization) {
  auto place = canonicalizeAccessPath(rawPlace);
  if (!place) return;
  auto old = m_NativeSyncOwnerRecipes.find(place.RootID);
  if (!place.Projections.empty()) {
    if (old != m_NativeSyncOwnerRecipes.end() && old->second)
      m_InvalidNativeSyncOwnerRecipes.insert(old->second);
    return;
  }
  auto recipe = source ? source->NativeSyncOwnerRecipe : NativeSyncOwnerCandidatePtr{};
  SymbolInfo *binding = nullptr;
  if (!CurrentScope->findSymbolByID(place.RootID, binding) || !binding || !binding->TypeObj) return;
  if (!recipe && !initialization &&
      (binding->TypeObj->isUniquePtr() || binding->TypeObj->isSharedPtr())) {
    auto *decl = binding->ASTPtr ? dynamic_cast<VariableDecl *>(static_cast<ASTNode *>(binding->ASTPtr)) : nullptr;
    auto *allocation = decl && decl->Init ? dynamic_cast<NewExpr *>(decl->Init.get()) : nullptr;
    if (allocation && m_NativeSyncAllocationSnapshots.count(allocation)) {
      // This exact private owner-initialization edge cannot silently fall
      // back to an unchecked assignment if an earlier rejected specialization
      // prevented preparation of its source recipe. The entry rollback also
      // restores this failure, just like an explicitly rejected recipe.
      error(source, DiagID::ERR_GENERIC_SEMA,
            "native sync owner allocation: MissingPreparedOwnerRecipe");
      m_NativeSyncOwnerRecipes[place.RootID].reset();
      return;
    }
  }
  if (recipe && !initialization &&
      (binding->TypeObj->isUniquePtr() || binding->TypeObj->isSharedPtr())) {
    auto *decl = binding->ASTPtr ? dynamic_cast<VariableDecl *>(static_cast<ASTNode *>(binding->ASTPtr)) : nullptr;
    const auto allocation = decl && decl->Init ? dynamic_cast<NewExpr *>(decl->Init.get()) : nullptr;
    auto pointee = binding->TypeObj->getPointeeType();
    if (!allocation || allocation->ArraySize || !pointee || !source->ResolvedType ||
        !pointee->withAttributes(false, false)->equals(*source->ResolvedType->withAttributes(false, false)))
      recipe.reset();
    else {
      if (!m_NativeSyncAllocationSnapshots.count(allocation)) {
        // Ordinary user allocations stay on their existing local semantics;
        // missing the private contract must not manufacture a native witness.
        m_NativeSyncOwnerRecipes[place.RootID].reset();
        return;
      }
      if (!prepareNativeSyncAllocation(allocation, decl, source, recipe)) {
        m_NativeSyncOwnerRecipes[place.RootID].reset();
        return;
      }
      auto owned = std::shared_ptr<NativeSyncOwnerCandidate>(new NativeSyncOwnerCandidate(*recipe));
      owned->Parent = recipe;
      owned->Allocation = allocation;
      owned->OwnerEdge = allocation;
      owned->ValueType = binding->TypeObj;
      owned->Provider = CurrentFunction;
      recipe = std::move(owned);
    }
  }
  m_NativeSyncOwnerRecipes[place.RootID] = std::move(recipe);
}

void Sema::recordNativeSyncOwnerReturn(ReturnStmt *statement) {
  if (!CurrentFunction || !statement || !statement->ReturnValue) return;
  auto recipe = statement->ReturnValue->NativeSyncOwnerRecipe;
  if (recipe && m_InvalidNativeSyncOwnerRecipes.count(recipe)) recipe.reset();
  // Keep every return, including unknown ones; a partial summary cannot be
  // mistaken for a complete factory by silently dropping its other branches.
  m_NativeSyncOwnerReturns[CurrentFunction].push_back(std::move(recipe));
}

bool Sema::qualifyNativeSyncFactory(CallExpr *call, size_t diagnosticStart) {
  call->NativeSyncFactorySource.reset();
  const auto &records = DiagnosticEngine::records();
  if (std::any_of(records.begin() + std::min(diagnosticStart, records.size()), records.end(),
                  [](const auto &record) { return record.Level == DiagLevel::Error; })) return false;
  // Preserve the speculative publication barrier. A private body may prepare
  // facts, but its plan stays UNVALIDATED and cannot enter binding provenance.
  // Finalization below only seals a still-owned AST edge after both the exact
  // factory and its enclosing specialization have completed validation.
  const bool deferPublication = m_IsPrecomputingCaptures || m_D3SpeculativeCallDepth != 0;
  auto reject = [&](const char *why) {
    error(call, DiagID::ERR_GENERIC_SEMA, std::string("native sync factory: ") + why);
    return false;
  };
  auto *fn = call->ResolvedFn;
  auto *declaration = fn && fn->TemplateOrigin ? fn->TemplateOrigin : fn;
  if (!fn || !declaration || !CurrentFunction || !fn->Body || !declaration->Body ||
      fn->NativeSyncFactory == NativeSyncFactoryKind::None ||
      fn->NativeSyncFactory != declaration->NativeSyncFactory)
    return reject("MissingSourceContract");
  auto moduleIt = DeclarationLexicalScopes.find(declaration);
  if (moduleIt == DeclarationLexicalScopes.end() || !moduleIt->second ||
      !moduleIt->second->SourceModule || moduleIt->second->SourceModule->IsInterface ||
      !moduleIt->second->IsTrustedSystemModule ||
      moduleIt->second->ShadowLogicalModulePath != "std/sync")
    return reject("UntrustedDeclaration");
  auto &module = *moduleIt->second;
  auto selected = module.Functions.find(declaration->Name);
  auto overloads = module.FunctionOverloads.find(declaration->Name);
  if (selected == module.Functions.end() || selected->second != declaration ||
      overloads == module.FunctionOverloads.end() || overloads->second.size() != 1 ||
      overloads->second.front() != declaration)
    return reject("AmbiguousDeclaration");
  if (!fn->GenericParams.empty() || !fn->TemplateOrigin)
    return reject("InstantiationRequired");
  auto cached = InstantiationCache.find(fn->Name);
  if (cached == InstantiationCache.end() || !cached->second || cached->second->Instance != fn ||
      cached->second->Validation != GenericSpecializationValidationState::Valid)
    return reject("InvalidSpecialization");
  const bool nativeOnly = fn->NativeSyncFactory == NativeSyncFactoryKind::CondVar;
  const size_t arity = nativeOnly ? 0 : 1;
  if (declaration->GenericParams.size() != 1 || !declaration->GenericParams[0].IsMorphic ||
      declaration->GenericParams[0].IsConst || declaration->IsVariadic ||
      declaration->Effect != EffectKind::None || declaration->Args.size() != arity ||
      fn->Args.size() != arity || call->Args.size() != arity)
    return reject("DeclarationSchemaMismatch");
  if (!nativeOnly) {
    const auto &a = declaration->Args.front();
    if (!a.IsCeded || !a.IsMorphicExempt) return reject("InputRequiresCededMorphicDeclaration");
    // Parser retains the apostrophe on the generic binder, but records the
    // argument's morphology separately from its bare type name.
    if ("'" + a.Type != declaration->GenericParams[0].Name) {
      error(call, DiagID::ERR_GENERIC_SEMA, "native sync factory: InputTypeSchemaMismatch (" +
            a.Type + " versus " + declaration->GenericParams[0].Name + ")");
      return false;
    }
    if (a.IsInit || a.DefaultValue ||
        a.IsRawPointer || a.IsUnique || a.IsShared || a.IsReference ||
        a.IsRebindable || a.IsValueMutable || a.IsPointerNullable || a.IsValueNullable ||
        a.IsRebindBlocked || a.IsValueBlocked)
      return reject("InputContractMismatch");
  }
  const char *ownerName = nativeOnly ? "CondVar" :
      fn->NativeSyncFactory == NativeSyncFactoryKind::Mutex ? "Mutex" : "RwMutex";
  auto nominal = module.Shapes.find(ownerName);
  auto ownerType = std::dynamic_pointer_cast<ShapeType>(call->ResolvedType);
  if (nominal == module.Shapes.end() || !nominal->second || !nominal->second->NominalId ||
      !ownerType || !ownerType->Decl || !ownerType->Decl->GenericParams.empty() ||
      ownerType->Decl->InstantiationTemplate != nominal->second ||
      ownerType->Decl->InstantiationArgs.size() != 1)
    return reject("OwnerNominalMismatch");
  auto element = ownerType->Decl->InstantiationArgs.front();
  if (!element || element->isUnknown() || element->isUninit())
    return reject("UnresolvedElement");
  if (!nativeOnly && (!call->Args.front()->ResolvedType ||
      !element->equals(*call->Args.front()->ResolvedType)))
    return reject("ElementMorphologyMismatch");
  // The exact nominal schema is part of the private contract, not an arbitrary
  // user shape with a familiar short name or an Addr-shaped field.
  const auto &members = ownerType->Decl->Members;
  if (members.size() != (nativeOnly ? 1u : 2u) || members[0].Name != "handle" ||
      !members[0].ResolvedType || !members[0].ResolvedType->isAddrType() ||
      (!nativeOnly && (members[1].Name != "data_ptr" || !members[1].ResolvedType ||
                       !members[1].ResolvedType->isAddrType())))
    return reject("OwnerStorageSchemaMismatch");
  auto plan = std::shared_ptr<NativeSyncFactoryPlan>(new NativeSyncFactoryPlan);
  plan->Site = call;
  plan->Declaration = fn;
  plan->OwnerDefinition = CurrentFunction;
  plan->OwnerTemplate = nominal->second;
  plan->Input = nativeOnly ? nullptr : call->Args.front().get();
  plan->OwnerType = call->ResolvedType;
  plan->ElementType = element;
  plan->Kind = fn->NativeSyncFactory;
  plan->ReplacementClosed = nativeOnly || checkNativeSyncClosedPayload(element).closed();
  plan->Validated = !deferPublication;
  if (deferPublication) m_PendingNativeSyncFactories.push_back(plan);
  call->NativeSyncFactorySource = std::move(plan);
  return true;
}

bool Sema::finalizeNativeSyncFactoryPlans() {
  if (HasError || DiagnosticEngine::hasErrors()) return false;
  for (auto &weak : m_PendingNativeSyncAllocations) {
    auto pending = weak.lock();
    if (!pending) continue;
    const auto checked = m_RawAddressReturns.find(const_cast<FunctionDecl *>(pending->Definition));
    const auto *owner = pending->OwnerType ? dynamic_cast<const ShapeType *>(pending->OwnerType.get()) : nullptr;
    if (!pending->Allocation || !pending->Binding || !pending->Definition ||
        pending->Allocation->NativeSyncAllocationSource != pending ||
        !owner || !owner->Decl || !owner->Decl->HasExplicitDrop || owner->Decl->MangledDestructorName.empty() ||
        checked == m_RawAddressReturns.end() || !checked->second.Checked || !checked->second.Valid) {
      error(const_cast<NewExpr *>(pending->Allocation), DiagID::ERR_GENERIC_SEMA,
            "native sync owner allocation: IncompleteDefinition");
      return false;
    }
    if (pending->Definition->TemplateOrigin) {
      auto cached = InstantiationCache.find(pending->Definition->Name);
      if (cached == InstantiationCache.end() || !cached->second ||
          cached->second->Validation != GenericSpecializationValidationState::Valid) return false;
    }
    auto sealed = std::shared_ptr<NativeSyncAllocationPlan>(new NativeSyncAllocationPlan(*pending));
    sealed->Complete = true;
    const_cast<NewExpr *>(pending->Allocation)->NativeSyncAllocationSource = std::move(sealed);
  }
  m_PendingNativeSyncAllocations.clear();
  for (const auto &weak : m_PendingNativeSyncFactories) {
    auto pending = weak.lock();
    if (!pending) continue; // discarded candidate AST; nothing to publish
    const auto *site = pending->Site;
    auto validInstance = [&](const FunctionDecl *fn) {
      if (!fn || !fn->Body) return false;
      if (!fn->TemplateOrigin) return fn->GenericParams.empty();
      auto found = InstantiationCache.find(fn->Name);
      return found != InstantiationCache.end() && found->second &&
          found->second->Instance == fn &&
          found->second->Validation == GenericSpecializationValidationState::Valid;
    };
    if (!site || site->NativeSyncFactorySource != pending || pending->Validated ||
        pending->Declaration != site->ResolvedFn || !site->ResolvedType ||
        !pending->OwnerType || !pending->OwnerType->equals(*site->ResolvedType) ||
        !validInstance(pending->Declaration) || !validInstance(pending->OwnerDefinition)) {
      if (site) error(const_cast<CallExpr *>(site), DiagID::ERR_GENERIC_SEMA,
                      "native sync factory: PendingValidationIncomplete");
      return false;
    }
    // Replace, rather than mutate, the private carrier. No speculative symbol
    // relation is revived here and no source/ownership facts are reconstructed.
    auto sealed = std::shared_ptr<NativeSyncFactoryPlan>(new NativeSyncFactoryPlan(*pending));
    sealed->Validated = true;
    const_cast<CallExpr *>(site)->NativeSyncFactorySource = std::move(sealed);
  }
  m_PendingNativeSyncFactories.clear();
  return !HasError;
}

bool NativeSyncFactoryPlan::matchesOwnerView(const std::shared_ptr<Type> &view) const {
  const auto *shape = view ? dynamic_cast<const ShapeType *>(view.get()) : nullptr;
  if (!Validated || !OwnerType || !view ||
      !dynamic_cast<const ShapeType *>(OwnerType.get()) ||
      !shape || !shape->Decl || !OwnerTemplate || !ElementType ||
      shape->Decl->InstantiationTemplate != OwnerTemplate ||
      shape->Decl->InstantiationArgs.size() != 1 ||
      !shape->Decl->InstantiationArgs[0] ||
      !ElementType->equals(*shape->Decl->InstantiationArgs[0])) return false;
  return OwnerType->withAttributes(view->IsWritable, OwnerType->IsNullable,
                                    OwnerType->IsBlocked)->equals(*view);
}

NativeSyncFactoryPtr Sema::collectNativeSyncFactoryOrigin(Expr *expression) {
  if (!expression) return {};
  NativeSyncFactoryPtr result;
  if (auto *call = dynamic_cast<CallExpr *>(expression)) result = call->NativeSyncFactorySource;
  else if (auto *cede = dynamic_cast<CedeExpr *>(expression))
    result = cede->Value->NativeSyncFactoryOrigin;
  else if (auto *unsafe = dynamic_cast<UnsafeExpr *>(expression))
    result = unsafe->Expression->NativeSyncFactoryOrigin;
  else if (auto *ascription = dynamic_cast<CastExpr *>(expression);
           ascription && ascription->Kind == CastKind::Ascription)
    result = ascription->Expression->NativeSyncFactoryOrigin;
  else if (dynamic_cast<VariableExpr *>(expression)) {
    auto path = canonicalizeAccessPath(makeAccessPath(expression));
    if (path && path.Projections.empty()) {
      auto found = m_NativeSyncBindings.find(path.RootID);
      if (found != m_NativeSyncBindings.end()) result = found->second;
    }
  }
  if (!result || !result->Validated || m_InvalidNativeSyncOrigins.count(result)) return {};
  // Do not infer ownership or accept raw/reference reinterpretations. A view
  // selector/new-allocation transition needs its own admitted transfer edge.
  if (!result->matchesOwnerView(expression->ResolvedType)) return {};
  // Only the already-checked view's top-level write qualifier may differ.
  // This records storage identity, never grants that qualifier or changes the
  // element's full morphology, nullable state or permission ceiling.
  return result;
}

void Sema::recordNativeSyncBinding(const AccessPath &rawPlace, Expr *source,
                                  bool initialization) {
  auto place = canonicalizeAccessPath(rawPlace);
  if (!place) return;
  auto old = m_NativeSyncBindings.find(place.RootID);
  if (!place.Projections.empty()) {
    // Mutation of storage/native identity poisons all aliases, not just the
    // spelling used for the write. Rejected writes never reach this hook.
    if (old != m_NativeSyncBindings.end()) m_InvalidNativeSyncOrigins.insert(old->second);
    return;
  }
  auto incoming = source ? source->NativeSyncFactoryOrigin : NativeSyncFactoryPtr{};
  if (incoming && m_InvalidNativeSyncOrigins.count(incoming)) incoming.reset();
  // Rebinding changes this binding's relation. It does not retire the old
  // relation still retained by another shared owner. Its runtime cleanup is
  // governed by the existing validated assignment plan, never by this map.
  m_NativeSyncBindings.erase(place.RootID);
  if (incoming) m_NativeSyncBindings[place.RootID] = std::move(incoming);
  (void)initialization;
}

NativeClosedPayloadResult
Sema::checkNativeSyncClosedPayload(const std::shared_ptr<Type> &root) const {
  using State = NativeClosedPayloadState;
  std::set<const ShapeDecl *> active;
  std::function<NativeClosedPayloadResult(const std::shared_ptr<Type> &, std::string)> visit;
  visit = [&](const std::shared_ptr<Type> &type, std::string path) -> NativeClosedPayloadResult {
    auto incomplete = [&](const char *reason) { return NativeClosedPayloadResult{State::Incomplete, path, reason}; };
    if (!type || type->isUnknown() || type->isUninit()) return incomplete("UnresolvedPayload");
    if (type->isAddrType() || type->isOAddrType() || type->isRawPointer() ||
        type->isReference() || type->isSlice() || type->isFunction() || type->isDynFn())
      return {State::ExternalDependency, path, "ReplacementMayCarryExternalDependency"};
    if (type->isInteger() || type->isFloatingPoint() || type->isBoolean() || type->isUnit())
      return {State::Closed, {}, {}};
    if (hasCanonicalOwningStringStorage(type)) return {State::Closed, {}, {}};
    if (type->isUniquePtr() || type->isSharedPtr())
      return visit(type->getPointeeType(), path + ".pointee");
    if (auto array = std::dynamic_pointer_cast<ArrayType>(type)) {
      if (!array->SymbolicSize.empty()) return incomplete("UnresolvedArrayExtent");
      return visit(array->ElementType, path + "[]");
    }
    auto shape = std::dynamic_pointer_cast<ShapeType>(type);
    if (!shape || !shape->Decl) return incomplete("MissingPayloadDeclaration");
    const auto *decl = shape->Decl;
    const auto *identity = decl->InstantiationTemplate ? decl->InstantiationTemplate : decl;
    if (!identity->NominalId) return incomplete("MissingNominalIdentity");
    // Only complete instantiated field types are evidence. Never substitute a
    // template recipe here and accidentally strip an argument's morphology.
    if (!decl->GenericParams.empty()) return incomplete("InstantiationRequired");
    if (decl->Kind != ShapeKind::Struct && decl->Kind != ShapeKind::Tuple &&
        decl->Kind != ShapeKind::Enum) return incomplete("UnsupportedPayloadKind");
    if (!active.insert(decl).second) return incomplete("RecursiveProofUnclosed");
    struct ActiveScope {
      std::set<const ShapeDecl *> &Active;
      const ShapeDecl *Decl;
      ~ActiveScope() { Active.erase(Decl); }
    } scope{active, decl};
    if (decl->InstantiationTemplate) {
      if (identity->GenericParams.size() != decl->InstantiationArgs.size())
        return incomplete("IncompleteInstantiationArguments");
      // First batch does not prove unused/phantom argument irrelevance. An
      // external or unknown type argument cannot borrow a closed field recipe
      // from another instantiation of the same template.
      for (size_t i = 0; i < decl->InstantiationArgs.size(); ++i) {
        if (identity->GenericParams[i].IsConst) continue;
        auto argument = visit(decl->InstantiationArgs[i], path + ".type_argument[" + std::to_string(i) + "]");
        if (!argument.closed()) return argument;
      }
    }
    NativeClosedPayloadResult result{State::Closed, {}, {}};
    for (const auto &member : decl->Members) {
      if (decl->Kind == ShapeKind::Enum) {
        if (member.SubMembers.empty() && !member.Type.empty()) {
          result = {State::Incomplete, path + "." + member.Name, "IncompleteVariantPayload"};
          break;
        }
        for (const auto &payload : member.SubMembers) {
          if (!payload.ResolvedType) {
            result = {State::Incomplete, path + "." + member.Name, "UnresolvedField"};
            break;
          }
          result = visit(getPhysicalType(payload), path + "." + member.Name + "." + payload.Name);
          if (!result.closed()) break;
        }
      } else {
        if (!member.ResolvedType) {
          result = {State::Incomplete, path + "." + member.Name, "UnresolvedField"};
          break;
        }
        result = visit(getPhysicalType(member), path + "." + member.Name);
      }
      if (!result.closed()) break;
    }
    return result;
  };
  return visit(root, "payload");
}
}
