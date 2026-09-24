#include "toka/Sema.h"
#include <functional>
#include <set>

namespace toka {
namespace {
bool nativeCompositePrimitive(const std::shared_ptr<Type> &type) {
  return type && !type->IsNullable && !type->IsBlocked &&
      (type->isBoolean() || type->isInteger() || type->isFloatingPoint());
}

bool nativeSameValueView(const std::shared_ptr<Type> &a, const std::shared_ptr<Type> &b) {
  return a && b && a->withAttributes(b->IsWritable, a->IsNullable, a->IsBlocked)->equals(*b);
}

bool nativeInitializerFields(Expr *expression, std::map<std::string, Expr *> &fields) {
  while (auto *cast = dynamic_cast<CastExpr *>(expression)) {
    if (cast->Kind == CastKind::Conversion || !dynamic_cast<ShapeType *>(cast->ResolvedType.get()) ||
        !nativeSameValueView(cast->Expression->ResolvedType, cast->ResolvedType)) return false;
    expression = cast->Expression.get();
  }
  if (auto *init = dynamic_cast<InitStructExpr *>(expression)) {
    for (auto &field : init->Members)
      if (!fields.emplace(field.first, field.second.get()).second) return false;
    return true;
  }
  auto *call = dynamic_cast<CallExpr *>(expression);
  if (!call || !call->ResolvedShape) return false;
  for (auto &arg : call->Args) {
    auto *named = dynamic_cast<BinaryExpr *>(arg.get());
    auto *name = named ? dynamic_cast<VariableExpr *>(named->LHS.get()) : nullptr;
    if (!named || named->Op != "=" || !name || !fields.emplace(name->Name, named->RHS.get()).second)
      return false;
  }
  return true;
}

bool nativeZeroLiteral(Expr *expression) {
  while (auto *cast = dynamic_cast<CastExpr *>(expression)) {
    if (cast->Kind != CastKind::Ascription &&
        !(cast->Kind == CastKind::Implicit && cast->ResolvedType &&
          (nativeCompositePrimitive(cast->ResolvedType) || cast->ResolvedType->isAddrType()) &&
          nativeSameValueView(cast->Expression->ResolvedType, cast->ResolvedType))) return false;
    expression = cast->Expression.get();
  }
  if (auto *zero = dynamic_cast<NumberExpr *>(expression)) return zero->Value == 0;
  if (auto *boolean = dynamic_cast<BoolExpr *>(expression)) return !boolean->Value;
  return false;
}
}
void Sema::collectNativeSyncGuardFlow(Expr *expression) {
  if (auto *method = dynamic_cast<MethodCallExpr *>(expression)) {
    if (method->NativeSyncAccess) {
      auto origin = std::shared_ptr<NativeSyncGuardOrigin>(new NativeSyncGuardOrigin);
      origin->Owner = method->NativeSyncAccess;
      origin->AcquireSite = method;
      origin->Writable = method->ResolvedFn == origin->Owner->Acquire;
      origin->Access = origin->Writable ? origin->Owner->GuardAccess : origin->Owner->ReadGuardAccess;
      if (!origin->Access) return; // native-only operations produce no guard
      expression->NativeSyncGuardOrigin = std::move(origin);
      if (m_NativeSyncTemporaryGuards && m_NativeSyncTemporaryGuards->Definition == CurrentFunction &&
          !m_IsPrecomputingCaptures && isStage0CallTransferObservationAllowed())
        m_NativeSyncTemporaryGuards->Guards.push_back(expression->NativeSyncGuardOrigin);
    } else if (auto origin = method->Object->NativeSyncGuardOrigin) {
      if (!nativeSyncOwnerLive(origin->Owner) || !nativeSyncDefinitionReady(method->ResolvedFn)) return;
      if (origin->Outcome && method->Method == "unwrap" && method->Args.empty()) {
        auto *module = getLexicalModule(method->ResolvedFn->Loc);
        if (!module || !module->IsTrustedSystemModule || !module->ShadowCoordinateKnown ||
            module->ShadowLogicalModulePath != "core/result" || method->ResolvedFn->Args.empty() ||
            !method->ResolvedFn->Args[0].IsCeded || !method->ResolvedType ||
            !origin->Access || !origin->Access->Args[0].ResolvedType ||
            !method->ResolvedType->withAttributes(false, false)->equals(
                *origin->Access->Args[0].ResolvedType->withAttributes(false, false))) return;
        auto guard = std::shared_ptr<NativeSyncGuardOrigin>(new NativeSyncGuardOrigin(*origin));
        guard->Outcome = false;
        expression->NativeSyncGuardOrigin = std::move(guard);
      } else if (!origin->Outcome && method->ResolvedFn == origin->Access) {
        expression->NativeSyncSlotOrigin = origin;
      }
    }
  } else if (auto *variable = dynamic_cast<VariableExpr *>(expression)) {
    auto path = makeAccessPath(variable);
    auto guard = m_NativeSyncGuards.find(path.RootID);
    if (guard != m_NativeSyncGuards.end()) expression->NativeSyncGuardOrigin = guard->second;
    auto slot = m_NativeSyncSlots.find(path.RootID);
    if (slot != m_NativeSyncSlots.end()) expression->NativeSyncSlotOrigin = slot->second;
  } else {
    Expr *child = nullptr;
    if (auto *cast = dynamic_cast<CastExpr *>(expression); cast && cast->Kind == CastKind::Ascription)
      child = cast->Expression.get();
    else if (auto *postfix = dynamic_cast<PostfixExpr *>(expression)) child = postfix->LHS.get();
    else if (auto *cede = dynamic_cast<CedeExpr *>(expression)) child = cede->Value.get();
    else if (auto *selected = dynamic_cast<UnaryExpr *>(expression);
             selected && selected->NativeSyncManagedSlotTarget) child = selected->RHS.get();
    if (child) {
      expression->NativeSyncGuardOrigin = child->NativeSyncGuardOrigin;
      expression->NativeSyncSlotOrigin = child->NativeSyncSlotOrigin;
    }
  }
}

void Sema::recordNativeSyncGuardBinding(const AccessPath &place, Expr *source) {
  if (!place || !place.Projections.empty() || !source) return;
  m_NativeSyncGuards.erase(place.RootID);
  m_NativeSyncSlots.erase(place.RootID);
  if (source->NativeSyncGuardOrigin) {
    m_NativeSyncGuards[place.RootID] = source->NativeSyncGuardOrigin;
    if (m_NativeSyncTemporaryGuards && m_NativeSyncTemporaryGuards->Definition == CurrentFunction) {
      auto &pending = m_NativeSyncTemporaryGuards->Guards;
      pending.erase(std::remove_if(pending.begin(), pending.end(), [&](const auto &guard) {
        return guard->AcquireSite == source->NativeSyncGuardOrigin->AcquireSite;
      }), pending.end());
    }
  }
  if (source->NativeSyncSlotOrigin) m_NativeSyncSlots[place.RootID] = source->NativeSyncSlotOrigin;
}

std::shared_ptr<Type> Sema::queryNativeSyncManagedSlotTarget(UnaryExpr *target) {
  if (!target || (target->Op != TokenType::Caret && target->Op != TokenType::Tilde)) return {};
  auto *variable = dynamic_cast<VariableExpr *>(target->RHS.get());
  if (!variable) return {};
  auto path = makeAccessPath(variable);
  auto found = m_NativeSyncSlots.find(path.RootID);
  SymbolInfo *binding = nullptr;
  if (found == m_NativeSyncSlots.end() || !found->second || found->second->Outcome ||
      !nativeSyncOwnerLive(found->second->Owner) ||
      !CurrentScope->findSymbolByID(path.RootID, binding) || !binding || binding->IsPlaceAlias ||
      !binding->TypeObj || !binding->TypeObj->isReference()) return {};
  auto element = found->second->Owner->ElementType;
  auto referenced = binding->TypeObj->getPointeeType();
  if (!element || !referenced || !nativeSameValueView(element, referenced) ||
      (target->Op == TokenType::Caret ? !element->isUniquePtr() : !element->isSharedPtr())) return {};
  // Reference P authorizes replacing the complete slot. It does not authorize
  // modifying the managed pointee, nor reseating the reference binding itself.
  // PayloadFlowRestricted describes the value stored in this slot, not the
  // guard/reference's right to replace that value. Keep that ceiling for P
  // checks; deriving H from it would make a readonly shared value one-shot.
  const bool writable = found->second->Writable && referenced->IsWritable;
  return element->withAttributes(writable, element->IsNullable, element->IsBlocked);
}

void Sema::prepareNativeSyncReplacement(BinaryExpr *assignment) {
  auto path = makeAccessPath(assignment->LHS.get());
  if (!path || !path.Projections.empty()) return;
  auto slot = m_NativeSyncSlots.find(path.RootID);
  if (slot == m_NativeSyncSlots.end()) return;
  Expr *lhs = assignment->LHS.get();
  while (auto *postfix = dynamic_cast<PostfixExpr *>(lhs)) lhs = postfix->LHS.get();
  auto *selected = dynamic_cast<UnaryExpr *>(lhs);
  const bool managedHandle = selected && selected->NativeSyncManagedSlotTarget;
  if (!dynamic_cast<VariableExpr *>(lhs) && !managedHandle) {
    // Rebinding a reference is not replacing the initialized native element.
    m_NativeSyncSlots.erase(slot);
    return;
  }
  assignment->NativeSyncReplacementRequired = true;
  assignment->NativeSyncReplacement.reset();
  auto guard = slot->second;
  auto fail = [&](const char *reason) { error(assignment, DiagID::ERR_GENERIC_SEMA,
      std::string("native sync replacement: ") + reason); };
  auto *authority = assignment->Stage0Authority ? &*assignment->Stage0Authority : nullptr;
  if (!guard || guard->Outcome || !guard->Writable || !nativeSyncOwnerLive(guard->Owner) ||
      !authority || !authority->SemaValidated || !authority->Complete || !authority->ItemPlan ||
      !authority->ItemPlan->admitted() || assignment->IsInitialization ||
      dynamic_cast<UnsetExpr *>(assignment->RHS.get()) || !assignment->RHS->ResolvedType) {
    fail("IncompleteSlotPlan"); return;
  }
  auto element = guard->Owner->ElementType;
  const bool validatedManagedFlow = managedHandle && element &&
      nativeSameValueView(element, assignment->LHS->ResolvedType) &&
      assignment->RHS->ResolvedType->typeKind == element->typeKind &&
      authority->ItemPlan->Prepared.TypeCompatibility == TransferTypeCompatibility::Compatible;
  if (!element || !checkNativeSyncClosedPayload(element).closed() ||
      (!validatedManagedFlow &&
       !element->withAttributes(false, false)->equals(*assignment->RHS->ResolvedType->withAttributes(false, false)))) {
    error(assignment, DiagID::ERR_GENERIC_SEMA, "native sync replacement: ElementTypeMismatch: expected " +
        (element ? element->toString() : "unknown") + ", got " + assignment->RHS->ResolvedType->toString());
    return;
  }
  auto plan = std::shared_ptr<NativeSyncReplacementPlan>(new NativeSyncReplacementPlan);
  plan->Site = assignment;
  plan->Destination = assignment->LHS.get();
  plan->Source = assignment->RHS.get();
  plan->Guard = guard;
  plan->ElementType = element;
  if (managedHandle) {
    if (!queryNativeSyncManagedSlotTarget(selected)) { fail("ManagedSlotTargetMismatch"); return; }
    SymbolInfo *binding = nullptr;
    if (!CurrentScope->findSymbolByID(path.RootID, binding) || !binding || !binding->TypeObj) {
      fail("MissingReferenceBinding"); return;
    }
    plan->ManagedHandle = true;
    plan->ReferenceBinding = selected->RHS.get();
    plan->ReferenceType = binding->TypeObj;
  }
  assignment->NativeSyncReplacement = std::move(plan);
}

bool Sema::nativeSyncDefinitionReady(const FunctionDecl *function) const {
  if (!function || !function->Body || !function->GenericParams.empty()) return false;
  auto checked = m_RawAddressReturns.find(const_cast<FunctionDecl *>(function));
  if (checked == m_RawAddressReturns.end() || !checked->second.Checked || !checked->second.Valid) return false;
  if (!function->TemplateOrigin) return true;
  auto cached = InstantiationCache.find(function->Name);
  return cached != InstantiationCache.end() && cached->second && cached->second->Instance == function &&
      cached->second->Validation == GenericSpecializationValidationState::Valid;
}

bool Sema::nativeSyncOwnerLive(const NativeSyncOwnerWitnessPtr &witness) const {
  if (witness && witness->DataFile) return dataFileLeaseLive(witness);
  if (!witness || !witness->Origin) return false;
  std::set<const NativeSyncOwnerCandidate *> seen;
  for (auto node = witness->Origin; node; node = node->Parent) {
    if (!seen.insert(node.get()).second || m_InvalidNativeSyncOwnerRecipes.count(node) ||
        !nativeSyncDefinitionReady(node->Provider)) return false;
  }
  for (const auto &[name, child] : witness->Children)
    if (!nativeSyncOwnerLive(child)) return false;
  return true;
}

NativeSyncOwnerWitnessPtr Sema::qualifyNativeSyncOwner(const NativeSyncOwnerCandidatePtr &recipe,
                                                     const std::shared_ptr<Type> &actualType) {
  if (recipe && recipe->DataFile) return qualifyDataFileLease(recipe, actualType);
  if (recipe && recipe->Channel) return qualifyChannelStorage(recipe, actualType);
  if (!recipe || !actualType || !recipe->ValueType ||
      !actualType->equals(*recipe->ValueType) ||
      (!actualType->isSharedPtr() && !actualType->isUniquePtr() && !actualType->isShape())) return {};
  auto cached = m_NativeSyncOwnerWitnesses.find(recipe);
  if (cached != m_NativeSyncOwnerWitnesses.end())
    return nativeSyncOwnerLive(cached->second) ? cached->second : NativeSyncOwnerWitnessPtr{};
  if (!recipe->Factory) {
    // Composition is admitted from actual child instances, never a nominal
    // exemption. First-batch primitive fields cannot carry hidden cleanup.
    const bool managed = actualType->isUniquePtr() || actualType->isSharedPtr();
    auto owner = std::dynamic_pointer_cast<ShapeType>(managed ? actualType->getPointeeType() : actualType);
    if (!owner || !owner->Decl || owner->Decl->HasExplicitDrop || owner->Decl->Kind != ShapeKind::Struct ||
        !owner->Decl->GenericParams.empty() || recipe->Children.empty()) return {};
    auto *module = getLexicalModule(owner->Decl->Loc);
    if (!module || !module->SourceModule || module->SourceModule->IsInterface ||
        !module->IsTrustedSystemModule || !module->ShadowCoordinateKnown ||
        module->ShadowLogicalModulePath != "std/sync") return {};
    auto witness = std::shared_ptr<NativeSyncOwnerWitness>(new NativeSyncOwnerWitness);
    witness->Origin = recipe; witness->ValueType = actualType; witness->OwnerType = owner;
    witness->AllocationSite = dynamic_cast<const NewExpr *>(recipe->Allocation);
    if (managed && (!witness->AllocationSite || !witness->AllocationSite->NativeSyncAllocationRequired ||
        !witness->AllocationSite->NativeSyncAllocationSource ||
        witness->AllocationSite->NativeSyncAllocationSource->CompositeDeclaration != owner->Decl ||
        !nativeSyncDefinitionReady(witness->AllocationSite->NativeSyncAllocationSource->Definition))) return {};
    for (const auto &field : owner->Decl->Members) {
      if (!field.ResolvedType) return {};
      auto child = recipe->Children.find(field.Name);
      if (child == recipe->Children.end()) {
        if (!nativeCompositePrimitive(getPhysicalType(field)) ||
            !checkNativeSyncClosedPayload(getPhysicalType(field)).closed()) return {};
        continue;
      }
      if (!child->second || !child->second->Factory ||
          !nativeSameValueView(getPhysicalType(field), child->second->ValueType)) return {};
      auto qualified = qualifyNativeSyncOwner(child->second, child->second->ValueType);
      if (!qualified) return {};
      witness->Children[field.Name] = std::move(qualified);
    }
    if (witness->Children.size() != recipe->Children.size()) return {};
    // Only single-call forwarding to the exact source-owned private operation
    // is exposed. The child witnesses above remain necessary independently.
    auto methods = MethodDecls.find(owner->Decl->Name);
    if (methods == MethodDecls.end()) return {};
    for (const auto &[name, function] : methods->second) {
      if (!nativeSyncDefinitionReady(function) || function->Args.empty() || function->Args[0].IsCeded ||
          function->Effect != EffectKind::None || function->Body->Statements.size() != 1) continue;
      auto *stmt = dynamic_cast<ExprStmt *>(function->Body->Statements.front().get());
      auto *call = stmt ? dynamic_cast<CallExpr *>(stmt->Expression.get()) : nullptr;
      if (!call || !nativeSyncDefinitionReady(call->ResolvedFn) ||
          call->Args.size() != function->Args.size()) continue;
      bool operation = false;
      for (const char *helper : {"__sync_once_call", "__sync_waitgroup_add", "__sync_waitgroup_done", "__sync_waitgroup_wait"}) {
        auto declared = module->Functions.find(helper);
        operation |= declared != module->Functions.end() && declared->second == call->ResolvedFn;
      }
      if (!operation || call->ResolvedFn->Args.size() != function->Args.size() ||
          !nativeSameValueView(call->ResolvedFn->Args.front().ResolvedType, owner)) continue;
      bool exact = true;
      for (size_t i = 0; i < call->Args.size(); ++i) {
        Expr *argument = call->Args[i].get();
        if (auto *postfix = dynamic_cast<PostfixExpr *>(argument)) argument = postfix->LHS.get();
        auto *variable = dynamic_cast<VariableExpr *>(argument);
        exact &= variable && variable->Name == function->Args[i].Name &&
            !call->ResolvedFn->Args[i].IsCeded &&
            nativeSameValueView(call->ResolvedFn->Args[i].ResolvedType, function->Args[i].ResolvedType);
      }
      if (exact) witness->CompositeOperations.push_back(function);
    }
    if (witness->CompositeOperations.empty() || !nativeSyncOwnerLive(witness)) return {};
    m_NativeSyncOwnerWitnesses[recipe] = witness;
    return witness;
  }
  const auto &factory = recipe->Factory;
  const bool nativeOnly = factory->Kind == NativeSyncFactoryKind::CondVar;
  const bool rw = factory->Kind == NativeSyncFactoryKind::RwMutex;
  if (factory->Kind == NativeSyncFactoryKind::None || !factory->Site || !factory->OwnerType ||
      !factory->ElementType || (!nativeOnly && !checkNativeSyncClosedPayload(factory->ElementType).closed()) ||
      !nativeSyncDefinitionReady(factory->Declaration) || !nativeSyncDefinitionReady(factory->OwnerDefinition)) return {};
  auto *module = getLexicalModule(factory->Declaration->Loc);
  if (!module || !module->SourceModule || module->SourceModule->IsInterface ||
      !module->IsTrustedSystemModule || !module->ShadowCoordinateKnown ||
      module->ShadowLogicalModulePath != "std/sync") return {};
  auto owner = std::dynamic_pointer_cast<ShapeType>(factory->OwnerType);
  const bool managed = actualType->isSharedPtr() || actualType->isUniquePtr();
  auto captureOwner = std::dynamic_pointer_cast<ShapeType>(managed ? actualType->getPointeeType() : actualType);
  if (!owner || !owner->Decl || !captureOwner || captureOwner->Decl != owner->Decl) return {};
  auto allocation = dynamic_cast<const NewExpr *>(recipe->Allocation);
  if (managed && (!allocation || !allocation->NativeSyncAllocationRequired || !allocation->NativeSyncAllocationSource ||
      !nativeSyncDefinitionReady(allocation->NativeSyncAllocationSource->Definition))) return {};
  auto drop = m_NativeSyncDropDeclarations.find(owner->Decl);
  if (drop == m_NativeSyncDropDeclarations.end() || !nativeSyncDefinitionReady(drop->second) ||
      drop->second->CodegenName != owner->Decl->MangledDestructorName) return {};
  auto method = [&](const ShapeDecl *type, const char *name) -> const FunctionDecl * {
    auto found = MethodDecls.find(type->Name);
    if (found == MethodDecls.end()) found = MethodDecls.find(type->CodegenName);
    if (found == MethodDecls.end()) return nullptr;
    auto item = found->second.find(name);
    return item == found->second.end() ? nullptr : item->second;
  };
  auto forwarder = [&](const FunctionDecl *function, const char *helper, bool returns, size_t arity = 1) {
    if (!nativeSyncDefinitionReady(function) || function->Effect != EffectKind::None ||
        function->Args.size() != arity || function->Args[0].IsCeded ||
        function->Body->Statements.size() != 1) return false;
    Expr *body = nullptr;
    if (returns) {
      auto *ret = dynamic_cast<ReturnStmt *>(function->Body->Statements[0].get());
      if (ret) body = ret->ReturnValue.get();
    } else {
      auto *statement = dynamic_cast<ExprStmt *>(function->Body->Statements[0].get());
      if (statement) body = statement->Expression.get();
    }
    auto *call = dynamic_cast<CallExpr *>(body);
    if (!call || !nativeSyncDefinitionReady(call->ResolvedFn) || call->Args.size() != arity) return false;
    auto declared = module->Functions.find(helper);
    auto origin = call->ResolvedFn->TemplateOrigin ? call->ResolvedFn->TemplateOrigin : call->ResolvedFn;
    if (declared == module->Functions.end() || declared->second != origin) return false;
    auto *argument = call->Args[0].get();
    if (auto *postfix = dynamic_cast<PostfixExpr *>(argument)) argument = postfix->LHS.get();
    auto *self = dynamic_cast<VariableExpr *>(argument);
    if (!self || Type::stripMorphology(self->Name) != "self") return false;
    for (size_t i = 1; i < arity; ++i) {
      auto *actual = dynamic_cast<VariableExpr *>(call->Args[i].get());
      if (!actual || actual->Name != function->Args[i].Name) return false;
    }
    return true;
  };
  auto witness = std::shared_ptr<NativeSyncOwnerWitness>(new NativeSyncOwnerWitness);
  witness->Origin = recipe;
  witness->ValueType = actualType;
  witness->OwnerType = factory->OwnerType;
  witness->ElementType = factory->ElementType;
  witness->FactorySite = factory->Site;
  witness->AllocationSite = allocation;
  witness->OwnerDrop = drop->second;
  witness->Kind = factory->Kind;
  if (nativeOnly) {
    witness->NotifyOne = method(owner->Decl, "notify_one");
    witness->NotifyAll = method(owner->Decl, "notify_all");
    witness->Wait = method(owner->Decl, "wait_cond");
    if (!forwarder(drop->second, "__sync_cond_drop", false) ||
        !forwarder(witness->NotifyOne, "__sync_cond_signal", false) ||
        !forwarder(witness->NotifyAll, "__sync_cond_broadcast", false) ||
        !forwarder(witness->Wait, "__sync_cond_wait", false, 2) || !nativeSyncOwnerLive(witness)) return {};
    m_NativeSyncOwnerWitnesses[recipe] = witness;
    return witness;
  }
  auto acquire = method(owner->Decl, rw ? "write_lock" : "lock");
  if (!forwarder(drop->second, rw ? "__sync_rw_drop" : "__sync_mutex_drop", false) ||
      !forwarder(acquire, rw ? "__sync_rw_write" : "__sync_mutex_acquire", true)) return {};
  auto qualifyGuard = [&](const FunctionDecl *acquisition, const char *guardName,
                          const char *releaseName, const char *accessName,
                          const FunctionDecl *&dropOut, const FunctionDecl *&accessOut) {
  if (!acquisition) return false;
  auto outcome = std::dynamic_pointer_cast<ShapeType>(acquisition->ResolvedReturnType);
  if (!outcome || !outcome->Decl || outcome->Decl->Kind != ShapeKind::Enum) return false;
  std::shared_ptr<ShapeType> guard;
  for (const auto &variant : outcome->Decl->Members)
    if (variant.Name == "Ok" && variant.SubMembers.size() == 1)
      guard = std::dynamic_pointer_cast<ShapeType>(getPhysicalType(variant.SubMembers[0]));
  auto guardTemplate = module->Shapes.find(guardName);
  if (!guard || !guard->Decl || guardTemplate == module->Shapes.end() ||
      guard->Decl->InstantiationTemplate != guardTemplate->second || guard->Decl->InstantiationArgs.size() != 1 ||
      !guard->Decl->InstantiationArgs[0]->equals(*factory->ElementType)) return false;
  auto guardDrop = m_NativeSyncDropDeclarations.find(guard->Decl);
  auto access = method(guard->Decl, accessName);
  if (guardDrop == m_NativeSyncDropDeclarations.end() ||
      !forwarder(guardDrop->second, releaseName, false) ||
      !nativeSyncDefinitionReady(access) || access->Body->Statements.size() != 1 ||
      access->Args.size() != 1 || access->Args[0].IsCeded) return false;
  auto *ret = dynamic_cast<ReturnStmt *>(access->Body->Statements[0].get());
  auto *view = ret ? dynamic_cast<MemberExpr *>(ret->ReturnValue.get()) : nullptr;
  auto *self = view ? dynamic_cast<VariableExpr *>(view->Object.get()) : nullptr;
  if (!view || !self || self->Name != "self" || Type::stripMorphology(view->Member) != "data" ||
      !view->ResolvedType || !access->ResolvedReturnType || !access->ResolvedReturnType->isReference() ||
      !view->ResolvedType->equals(*access->ResolvedReturnType)) return false;
  dropOut = guardDrop->second; accessOut = access;
  return true;
  };
  witness->Acquire = acquire;
  if (!qualifyGuard(acquire, rw ? "RwWriteLock" : "MutexLock", rw ? "__sync_rw_write_release" : "__sync_mutex_release",
                    "borrow_mut", witness->GuardDrop, witness->GuardAccess)) return {};
  if (rw) {
    witness->ReadAcquire = method(owner->Decl, "read_lock");
    if (!forwarder(witness->ReadAcquire, "__sync_rw_read", true) ||
        !qualifyGuard(witness->ReadAcquire, "RwReadLock", "__sync_rw_read_release", "borrow",
                      witness->ReadGuardDrop, witness->ReadGuardAccess)) return {};
  }
  if (!nativeSyncOwnerLive(witness)) return {};
  m_NativeSyncOwnerWitnesses[recipe] = witness;
  return witness;
}

bool Sema::rejectNativeSyncUnlock(Expr *expression) {
  const FunctionDecl *callee = nullptr;
  Expr *receiver = nullptr;
  if (auto *method = dynamic_cast<MethodCallExpr *>(expression)) {
    callee = method->ResolvedFn;
    receiver = method->Object.get();
  } else if (auto *call = dynamic_cast<CallExpr *>(expression); call && !call->Args.empty()) {
    callee = call->ResolvedFn;
    receiver = call->Args.front().get();
  }
  if (!callee || !receiver) return false;
  auto recipe = receiver->NativeSyncOwnerRecipe;
  if (!recipe) {
    // An earlier exposure can revoke positive qualification without releasing
    // the native lock. Retain its identity for this rejection-only check.
    auto path = makeAccessPath(receiver);
    auto found = m_NativeSyncOwnerRecipes.find(path.RootID);
    if (path && path.Projections.empty() && found != m_NativeSyncOwnerRecipes.end()) recipe = found->second;
  }
  if (!recipe) return false;
  auto conflicts = [&](const NativeSyncGuardOriginPtr &guard) {
    if (!guard || !guard->Owner ||
        (guard->Owner->Kind != NativeSyncFactoryKind::Mutex && guard->Owner->Kind != NativeSyncFactoryKind::RwMutex) ||
        guard->Owner->Origin != recipe) return false;
    auto owner = std::dynamic_pointer_cast<ShapeType>(guard->Owner->OwnerType);
    if (!owner || !owner->Decl) return false;
    auto methods = MethodDecls.find(owner->Decl->Name);
    if (methods == MethodDecls.end()) methods = MethodDecls.find(owner->Decl->CodegenName);
    if (methods == MethodDecls.end()) return false;
    auto unlock = methods->second.find("unlock");
    if (unlock == methods->second.end() || unlock->second != callee) return false;
    // A live Result may still contain a guard; a moved or out-of-scope binding
    // does not. Compare exact owner instances, not their common factory parent.
    error(expression, DiagID::ERR_GENERIC_SEMA, "native sync unlock: ActiveGuardOwnsUnlock");
    if (guard->AcquireSite)
      DiagnosticEngine::report(guard->AcquireSite->Loc, DiagID::NOTE_GENERIC,
                               "this owner's guard retains the unlock responsibility");
    return true;
  };
  for (const auto &[id, guard] : m_NativeSyncGuards) {
    SymbolInfo *binding = nullptr;
    if (CurrentScope->findSymbolByID(id, binding) && binding &&
        hasPlaceState(binding->placeFact(), PlaceState::Live) && conflicts(guard)) return true;
  }
  if (m_NativeSyncTemporaryGuards && m_NativeSyncTemporaryGuards->Definition == CurrentFunction)
    for (const auto &guard : m_NativeSyncTemporaryGuards->Guards)
      if (conflicts(guard)) return true;
  return false;
}

void Sema::checkNativeSyncOwnerExposure(Expr *expression) {
  if (!expression) return;
  auto invalidate = [&](const NativeSyncOwnerCandidatePtr &recipe) {
    if (recipe) m_InvalidNativeSyncOwnerRecipes.insert(recipe);
  };
  if (auto *address = dynamic_cast<AddressOfExpr *>(expression)) {
    auto recipe = address->Expression->NativeSyncOwnerRecipe;
    if (recipe && recipe->DataFile) invalidate(recipe);
  } else if (auto *unary = dynamic_cast<UnaryExpr *>(expression);
             unary && unary->Op == TokenType::Ampersand) {
    auto recipe = unary->RHS->NativeSyncOwnerRecipe;
    if (recipe && recipe->DataFile) invalidate(recipe);
  } else if (auto *member = dynamic_cast<MemberExpr *>(expression)) {
    auto recipe = member->Object->NativeSyncOwnerRecipe;
    if (recipe && recipe->Channel) {
      auto pair = std::dynamic_pointer_cast<ShapeType>(member->Object->ResolvedType);
      if (pair && pair->Decl == recipe->Channel->Pair && member->Index >= 0 &&
          static_cast<size_t>(member->Index) < pair->Decl->Members.size()) return;
    }
    invalidate(member->Object->NativeSyncOwnerRecipe);
  } else if (auto *method = dynamic_cast<MethodCallExpr *>(expression)) {
    method->NativeSyncAccessRequired = false;
    method->NativeSyncAccess.reset();
    method->NativeSyncWaitGuard.reset();
    auto recipe = method->Object->NativeSyncOwnerRecipe;
    if (!recipe) return;
    if (recipe->DataFile) {
      const auto *resultOrigin = method->ResolvedFn &&
                                 method->ResolvedFn->TemplateOrigin
                                     ? method->ResolvedFn->TemplateOrigin
                                     : method->ResolvedFn;
      auto *resultScope = resultOrigin
                              ? getLexicalModule(resultOrigin->Loc) : nullptr;
      const bool trustedResult = resultOrigin && resultOrigin->Body &&
          resultScope && resultScope->SourceModule &&
          !resultScope->SourceModule->IsInterface &&
          resultScope->IsTrustedSystemModule &&
          resultScope->ShadowCoordinateKnown &&
          resultScope->ShadowLogicalModulePath == "core/result" &&
          resultScope->SourceModule->ShadowCoordinateOrigin == "toolchain";
      if (recipe->DataFilePhase == DataFileLeasePhase::PendingOpen &&
          (method->Method == "unwrap" || method->Method == "is_ok" ||
           method->Method == "is_err") && trustedResult &&
          method->Args.empty() &&
          method->Object->ResolvedType &&
          method->Object->ResolvedType->equals(
              *recipe->DataFile->Open->ResolvedReturnType)) return;
      auto lease = qualifyDataFileLease(recipe, method->Object->ResolvedType);
      if (!lease || (method->ResolvedFn != recipe->DataFile->Clone &&
                     method->ResolvedFn != recipe->DataFile->ReadAt)) {
        invalidate(recipe);
        return;
      }
      method->NativeSyncAccessRequired = true;
      method->NativeSyncAccess = std::move(lease);
      return;
    }
    auto type = recipe->ValueType;
    auto witness = qualifyNativeSyncOwner(recipe, type);
    const bool allowed = witness && (method->ResolvedFn == witness->Acquire || method->ResolvedFn == witness->ReadAcquire ||
        method->ResolvedFn == witness->NotifyOne || method->ResolvedFn == witness->NotifyAll || method->ResolvedFn == witness->Wait ||
        std::find(witness->CompositeOperations.begin(), witness->CompositeOperations.end(), method->ResolvedFn) !=
            witness->CompositeOperations.end());
    if (!allowed) invalidate(recipe);
    else {
      if (method->ResolvedFn == witness->Wait) {
        auto guard = method->Args.size() == 1 ? method->Args[0]->NativeSyncGuardOrigin : nullptr;
        auto path = method->Args.size() == 1 ? canonicalizeAccessPath(makeAccessPath(method->Args[0].get())) : AccessPath{};
        auto conflict = path ? PALCheckerState.verifyInvalidation(path) : std::nullopt;
        if (!guard || guard->Outcome || guard->Owner->Kind != NativeSyncFactoryKind::Mutex ||
            !nativeSyncOwnerLive(guard->Owner) ||
            !guard->Owner->ElementType->equals(*witness->ElementType) || !path || conflict) {
          error(method, DiagID::ERR_GENERIC_SEMA, "native sync wait: LiveUnborrowedMutexGuardRequired");
          return;
        }
        method->NativeSyncWaitGuard = guard;
      }
      method->NativeSyncAccessRequired = true;
      method->NativeSyncAccess = witness;
    }
  } else if (auto *call = dynamic_cast<CallExpr *>(expression)) {
    // An empty checked observer cannot escape or mutate a value. Other calls
    // need an explicit native contract before their owner recipe can survive.
    const bool observer = nativeSyncDefinitionReady(call->ResolvedFn) && call->ResolvedFn->Body->Statements.empty();
    if (!observer)
      for (const auto &argument : call->Args) invalidate(argument->NativeSyncOwnerRecipe);
  }
}

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
  // A snapshot alone grants nothing. Select composites by an actual complete
  // local recipe, not by Once/WaitGroup spelling or a Drop-query fallback.
  if (!nativeOwner && !owner->Decl->HasExplicitDrop) {
    for (const auto &[id, recipe] : m_NativeSyncOwnerRecipes) {
      SymbolInfo *symbol = nullptr;
      const bool local = CurrentScope->findSymbolByID(id, symbol) && symbol && CurrentFunction->Body &&
          std::any_of(CurrentFunction->Body->Statements.begin(), CurrentFunction->Body->Statements.end(),
              [&](const auto &statement) {
                return dynamic_cast<VariableDecl *>(statement.get()) && symbol->ASTPtr == statement.get();
              });
      if (local && recipe && !recipe->Children.empty() &&
          !m_InvalidNativeSyncOwnerRecipes.count(recipe) &&
          nativeSameValueView(recipe->ValueType, owner)) {
        nativeOwner = true;
        break;
      }
    }
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
  if (!allocation || !binding || !recipe || (!recipe->Factory && recipe->Children.empty()))
    return fail("MissingOwnerRecipe");
  const bool composite = !recipe->Factory;
  auto ownerType = composite ? recipe->ValueType : recipe->Factory->OwnerType;
  auto snapshot = m_NativeSyncAllocationSnapshots.find(allocation);
  if (snapshot == m_NativeSyncAllocationSnapshots.end() || snapshot->second.Definition != CurrentFunction)
    return fail("MissingPreAllocationSnapshot");
  auto *transfer = dynamic_cast<CedeExpr *>(source);
  if (!transfer || !transfer->Value || !source->ResolvedType ||
      !ownerType || !source->ResolvedType->equals(*ownerType))
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
      !allocation->Initializer->ResolvedType->equals(*ownerType))
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
  auto plan = std::shared_ptr<NativeSyncAllocationPlan>(new NativeSyncAllocationPlan);
  if (composite) {
    auto owner = std::dynamic_pointer_cast<ShapeType>(ownerType);
    if (!owner || !owner->Decl || owner->Decl->Kind != ShapeKind::Struct ||
        owner->Decl->HasExplicitDrop || !owner->Decl->GenericParams.empty() ||
        fields.size() != owner->Decl->Members.size()) return fail("UnqualifiedCompositeDeclaration");
    plan->CompositeDeclaration = owner->Decl;
    size_t nativeCount = 0;
    for (size_t i = 0; i < owner->Decl->Members.size(); ++i) {
      const auto &member = owner->Decl->Members[i];
      auto type = getPhysicalType(member);
      auto value = fields.find(member.Name);
      if (!member.ResolvedType || value == fields.end() ||
          !nativeSameValueView(type, value->second->ResolvedType)) return fail("CompositeFieldMismatch");
      NativeSyncAllocationPlan::FieldCleanup field;
      field.Index = i; field.Name = member.Name; field.Type = type; field.DeclaredType = member.ResolvedType;
      auto child = recipe->Children.find(member.Name);
      if (child != recipe->Children.end()) {
        field.Recipe = child->second;
        auto factory = field.Recipe ? field.Recipe->Factory : nullptr;
        if (!factory || !nativeSameValueView(type, factory->OwnerType) ||
            !nativeSameValueView(type, field.Recipe->ValueType) ||
            m_InvalidNativeSyncOwnerRecipes.count(field.Recipe)) return fail("UnqualifiedNativeChild");
        std::map<std::string, Expr *> empty;
        if (!nativeInitializerFields(value->second, empty)) return fail("EmptyNativeChildRequired");
        const bool cond = factory->Kind == NativeSyncFactoryKind::CondVar;
        if (factory->Kind == NativeSyncFactoryKind::None || empty.size() != (cond ? 1u : 2u))
          return fail("EmptyNativeChildSchemaMismatch");
        for (const auto &[name, expression] : empty)
          if ((name != "handle" && (cond || name != "data_ptr")) || !expression->ResolvedType ||
              !expression->ResolvedType->isAddrType() || !nativeZeroLiteral(expression))
            return fail("EmptyNativeChildRequired");
        ++nativeCount;
      } else if (!nativeCompositePrimitive(type) || !checkNativeSyncClosedPayload(type).closed() ||
                 !nativeZeroLiteral(value->second)) return fail("ClosedPrimitiveFieldRequired");
      plan->Fields.push_back(std::move(field));
    }
    if (nativeCount != recipe->Children.size()) return fail("UnmatchedNativeChild");
  } else {
    const bool nativeOnly = recipe->Factory->Kind == NativeSyncFactoryKind::CondVar;
    if (fields.size() != (nativeOnly ? 1u : 2u)) return fail("EmptyCarrierSchemaMismatch");
    for (auto &[name, expression] : fields) {
    if ((name != "handle" && (nativeOnly || name != "data_ptr")) ||
        !expression || !expression->ResolvedType || !expression->ResolvedType->isAddrType())
      return fail("EmptyCarrierSchemaMismatch");
    if (!nativeZeroLiteral(expression)) {
      error(source, DiagID::ERR_GENERIC_SEMA,
            "native sync owner allocation: EmptyCarrierLiteralRequired: " + expression->toString());
      return false;
    }
    }
  }
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
  if (auto lease = collectDataFileLeaseRecipe(source)) return lease;
  if (auto channel = collectChannelStorageRecipe(source)) return channel;
  NativeSyncOwnerCandidatePtr recipe;
  FunctionDecl *callee = nullptr;
  auto aggregate = std::dynamic_pointer_cast<ShapeType>(source->ResolvedType);
  std::map<std::string, Expr *> fields;
  if (auto *init = dynamic_cast<InitStructExpr *>(source)) {
    for (auto &field : init->Members) fields[field.first] = field.second.get();
  } else if (auto *call = dynamic_cast<CallExpr *>(source); call && call->ResolvedShape) {
    for (auto &argument : call->Args) {
      auto *named = dynamic_cast<BinaryExpr *>(argument.get());
      auto *name = named ? dynamic_cast<VariableExpr *>(named->LHS.get()) : nullptr;
      if (!named || named->Op != "=" || !name) { fields.clear(); break; }
      fields[name->Name] = named->RHS.get();
    }
  }
  if (aggregate && aggregate->Decl && !fields.empty() &&
      fields.size() == aggregate->Decl->Members.size()) {
    auto prepared = std::shared_ptr<NativeSyncOwnerCandidate>(new NativeSyncOwnerCandidate);
    prepared->OwnerEdge = source;
    prepared->Provider = CurrentFunction;
    prepared->ValueType = source->ResolvedType;
    bool complete = true;
    for (const auto &field : aggregate->Decl->Members) {
      auto value = fields.find(field.Name);
      if (value == fields.end()) { complete = false; break; }
      // Normal aggregate validation may insert a no-op field-view cast after
      // the initializer was checked. Preserve only its already validated exact
      // direct shape identity; do not rerun Sema or grant cast permissions.
      auto *actual = value->second;
      while (auto *cast = dynamic_cast<CastExpr *>(actual)) {
        if (cast->Kind == CastKind::Conversion || !cast->Expression->ResolvedType ||
            !dynamic_cast<ShapeType *>(cast->ResolvedType.get()) ||
            !nativeSameValueView(cast->Expression->ResolvedType, cast->ResolvedType)) break;
        actual = cast->Expression.get();
      }
      if (actual->NativeSyncOwnerRecipe &&
          nativeSameValueView(actual->NativeSyncOwnerRecipe->ValueType, getPhysicalType(field)))
        prepared->Children[field.Name] = actual->NativeSyncOwnerRecipe;
      else if (!nativeCompositePrimitive(getPhysicalType(field)) ||
               !checkNativeSyncClosedPayload(getPhysicalType(field)).closed()) { complete = false; break; }
    }
    if (complete && !prepared->Children.empty()) return prepared;
  }
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
  if (!recipe || m_InvalidNativeSyncOwnerRecipes.count(recipe)) return {};
  if (recipe->DataFile)
    return recipe->ValueType &&
                   recipe->ValueType->withAttributes(false, false,
                       recipe->ValueType->IsBlocked)->equals(
                       *source->ResolvedType->withAttributes(false, false,
                           source->ResolvedType->IsBlocked))
               ? recipe : NativeSyncOwnerCandidatePtr{};
  if (recipe->Channel) {
    auto shape = std::dynamic_pointer_cast<ShapeType>(source->ResolvedType);
    const auto &p = recipe->Channel;
    if (!shape || (shape->Decl != p->Pair && shape->Decl != p->Sender && shape->Decl != p->Receiver)) return {};
    if (recipe->ValueType && recipe->ValueType->equals(*source->ResolvedType)) return recipe;
    auto rebased = std::shared_ptr<NativeSyncOwnerCandidate>(new NativeSyncOwnerCandidate(*recipe));
    rebased->Parent = recipe;
    rebased->ValueType = source->ResolvedType;
    return rebased;
  }
  auto valueType = source->ResolvedType;
  if (valueType->isReference()) valueType = valueType->getPointeeType();
  if (valueType->isUniquePtr() || valueType->isSharedPtr()) valueType = valueType->getPointeeType();
  auto shape = std::dynamic_pointer_cast<ShapeType>(valueType);
  if (!recipe->Factory) {
    auto expected = recipe->ValueType;
    if (expected && (expected->isUniquePtr() || expected->isSharedPtr())) expected = expected->getPointeeType();
    auto original = std::dynamic_pointer_cast<ShapeType>(expected);
    return !recipe->Children.empty() && shape && original && shape->Decl == original->Decl ? recipe : nullptr;
  }
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
  if (recipe && recipe->DataFile) {
    if (!recipe->ValueType ||
        !recipe->ValueType->withAttributes(false, false,
            recipe->ValueType->IsBlocked)->equals(
            *binding->TypeObj->withAttributes(false, false,
                binding->TypeObj->IsBlocked))) recipe.reset();
    else if (dynamic_cast<CedeExpr *>(source)) {
      auto moved = std::shared_ptr<NativeSyncOwnerCandidate>(
          new NativeSyncOwnerCandidate(*recipe));
      moved->Parent = recipe;
      moved->OwnerEdge = source;
      moved->ValueType = binding->TypeObj;
      recipe = std::move(moved);
    }
  }
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
  if (recipe && recipe->Channel && recipe->ValueType && binding->TypeObj->isShape() &&
      !recipe->ValueType->equals(*binding->TypeObj)) {
    if (!recipe->ValueType->withAttributes(false, false, recipe->ValueType->IsBlocked)->equals(
            *binding->TypeObj->withAttributes(false, false, binding->TypeObj->IsBlocked))) recipe.reset();
    else {
      auto view = std::shared_ptr<NativeSyncOwnerCandidate>(new NativeSyncOwnerCandidate(*recipe));
      view->Parent = recipe;
      view->ValueType = binding->TypeObj;
      recipe = std::move(view);
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
  if (declaration->GenericParams.size() != 1 ||
      declaration->GenericParams[0].IsConst || declaration->IsVariadic ||
      declaration->Effect != EffectKind::None || declaration->Args.size() != arity ||
      fn->Args.size() != arity || call->Args.size() != arity)
    return reject("DeclarationSchemaMismatch");
  if (!nativeOnly) {
    const auto &a = declaration->Args.front();
    if (!a.IsCeded || !a.IsAbstractWholeValue) return reject("InputRequiresCededWholeValueDeclaration");
    if (!a.TypeSyntax || a.TypeSyntax->NodeKind != TypeSyntax::Kind::Named ||
        a.TypeSyntax->Text != declaration->GenericParams[0].Name) {
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
        !owner || !owner->Decl ||
        (pending->CompositeDeclaration ?
          (pending->CompositeDeclaration != owner->Decl || owner->Decl->HasExplicitDrop ||
           pending->Fields.size() != owner->Decl->Members.size()) :
          (!owner->Decl->HasExplicitDrop || owner->Decl->MangledDestructorName.empty())) ||
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
    for (auto &field : sealed->Fields) {
      if (field.Index >= owner->Decl->Members.size()) return false;
      const auto &member = owner->Decl->Members[field.Index];
      if (member.Name != field.Name || !field.Type || !field.Type->equals(*getPhysicalType(member))) return false;
      if (field.Recipe) {
        field.Witness = qualifyNativeSyncOwner(field.Recipe, field.Recipe->ValueType);
        if (!field.Witness || !nativeSameValueView(field.Type, field.Witness->OwnerType)) {
          error(const_cast<NewExpr *>(pending->Allocation), DiagID::ERR_GENERIC_SEMA,
                "native sync owner allocation: UnqualifiedChildCleanup: " + field.Name);
          return false;
        }
      } else if (!nativeCompositePrimitive(field.Type) || !checkNativeSyncClosedPayload(field.Type).closed()) return false;
    }
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
