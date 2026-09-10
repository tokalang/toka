#include "toka/Sema.h"
#include "toka/InterfaceVersion.h"
#include "toka/SourceManager.h"
#include <functional>

namespace toka {
namespace {
Expr *surface(Expr *expression) {
  while (expression) {
    if (auto *cede = dynamic_cast<CedeExpr *>(expression)) expression = cede->Value.get();
    else if (auto *unsafe = dynamic_cast<UnsafeExpr *>(expression)) expression = unsafe->Expression.get();
    else if (auto *cast = dynamic_cast<CastExpr *>(expression); cast && cast->Kind == CastKind::Ascription) expression = cast->Expression.get();
    else break;
  }
  return expression;
}
bool hasCede(Expr *e) {
  while (e) {
    if (auto *u = dynamic_cast<UnsafeExpr *>(e)) e = u->Expression.get();
    else if (auto *c = dynamic_cast<CastExpr *>(e); c && c->Kind == CastKind::Ascription) e = c->Expression.get();
    else break;
  }
  return dynamic_cast<CedeExpr *>(e) != nullptr;
}
}

const FunctionDecl *Sema::findThreadInvoke(Expr *argument) {
  std::set<uint64_t> originBindings;
  std::function<const FunctionDecl *(Expr *)> actualInvoke = [&](Expr *expression) -> const FunctionDecl * {
    expression = surface(expression);
    if (auto *closure = dynamic_cast<ClosureExpr *>(expression)) {
      auto methods = MethodDecls.find(closure->SynthesizedShapeName);
      if (!closure->HasBoundaryCaptureSummary || methods == MethodDecls.end()) return nullptr;
      auto found = methods->second.find("__invoke");
      if (found == methods->second.end() || !found->second || !found->second->IsClosureInvoke ||
          !found->second->Body || !found->second->GenericParams.empty() ||
          found->second->Effect != EffectKind::None) return nullptr;
      return found->second;
    }
    auto *variable = dynamic_cast<VariableExpr *>(expression);
    SymbolInfo *binding = nullptr;
    std::string bindingName;
    if (!variable || !CurrentScope->findVariableWithDeref(variable->Name, binding, bindingName) ||
        !binding || binding->IsFunctionParameter || binding->IsPlaceAlias || binding->HasBeenMutated ||
        m_ReturnSourceInvalidatedRoots.count(binding->SymbolID) ||
        m_ReturnSourceUnknownRoots.count(binding->SymbolID) || !originBindings.insert(binding->SymbolID).second)
      return nullptr;
    auto *declaration = binding->ASTPtr
        ? dynamic_cast<VariableDecl *>(static_cast<ASTNode *>(binding->ASTPtr)) : nullptr;
    auto owner = m_LocalVariableOwners.find(declaration);
    if (!declaration || !declaration->Init || owner == m_LocalVariableOwners.end() ||
        owner->second != CurrentFunction || !declaration->Loc.isValid() || !variable->Loc.isValid())
      return nullptr;
    // An initializer use must not resolve to a subsequently shadowing binding.
    const auto declaredAt = DiagnosticEngine::SrcMgr->getFullSourceLoc(declaration->Loc);
    const auto usedAt = DiagnosticEngine::SrcMgr->getFullSourceLoc(variable->Loc);
    if (declaredAt.FileName != usedAt.FileName || declaredAt.Line > usedAt.Line ||
        (declaredAt.Line == usedAt.Line && declaredAt.Column >= usedAt.Column)) return nullptr;
    return actualInvoke(declaration->Init.get());
  };
  return actualInvoke(argument);
}

bool Sema::inspectThreadValue(std::shared_ptr<Type> value, std::string &identity, bool &needsDrop) {
  std::set<const ShapeDecl *> visiting;
  std::function<bool(std::shared_ptr<Type>, std::string &, bool &)> resultFacts =
      [&](std::shared_ptr<Type> type, std::string &identity, bool &needsDrop) {
    type = resolveExplicitCedeStage0TypeReadOnly(type);
    if (!type || type->isUnknown() || type->isRawPointer() || type->isReference() ||
        type->isFunction() || type->isDynFn() || type->isSlice() ||
        type->isAddrType() || type->isOAddrType() || type->isVoid() || type->isNever()) return false;
    identity += type->toString() + ";";
    if (hasCanonicalOwningStringStorage(type)) {
      needsDrop = true;
      auto shape = std::dynamic_pointer_cast<ShapeType>(type);
      if (!shape || !shape->Decl || !shape->Decl->NominalId) return false;
      identity += shape->Decl->NominalId->canonical();
      for (const auto &field : shape->Decl->Members) {
        auto physical = getPhysicalType(field);
        if (!physical) return false;
        identity += field.Name + ":" + physical->toString() + ";";
      }
      return true;
    }
    if (type->isUniquePtr() || type->isSharedPtr()) {
      needsDrop = true;
      return resultFacts(type->getPointeeType(), identity, needsDrop);
    }
    if (type->isArray()) {
      auto array = std::static_pointer_cast<ArrayType>(type);
      return array->SymbolicSize.empty() && resultFacts(array->ElementType, identity, needsDrop);
    }
    if (type->typeKind == Type::Primitive || type->isUnit()) return true;
    auto shape = std::dynamic_pointer_cast<ShapeType>(type);
    const ShapeDecl *definition = shape && shape->Decl && shape->Decl->InstantiationTemplate
        ? shape->Decl->InstantiationTemplate : shape ? shape->Decl : nullptr;
    if (!shape || !shape->Decl || !definition || !definition->NominalId ||
        !visiting.insert(shape->Decl).second) return false;
    identity += definition->NominalId->canonical();
    needsDrop |= shape->Decl->HasExplicitDrop;
    std::map<std::string, std::shared_ptr<Type>> substitutions;
    if (!shape->Decl->GenericParams.empty()) {
      if (shape->Decl->GenericParams.size() != shape->GenericArgs.size()) return false;
      for (size_t i = 0; i < shape->GenericArgs.size(); ++i)
        substitutions[shape->Decl->GenericParams[i].Name] = shape->GenericArgs[i];
    }
    auto memberFacts = [&](const ShapeMember &field) {
      auto fieldType = getPhysicalType(field);
      if (fieldType && !substitutions.empty()) fieldType = fieldType->substitute(substitutions);
      identity += field.Name + ":";
      return fieldType && resultFacts(fieldType, identity, needsDrop);
    };
    bool complete = true;
    for (const auto &field : shape->Decl->Members) {
      if (shape->Decl->Kind == ShapeKind::Enum) {
        identity += "variant:" + field.Name + ";";
        for (const auto &payload : field.SubMembers) complete &= memberFacts(payload);
      } else complete &= memberFacts(field);
    }
    visiting.erase(shape->Decl);
    return complete;
  };
  return resultFacts(value, identity, needsDrop);
}

bool Sema::qualifyPublicThread(CallExpr *call, const AnalysisState &before, size_t start) {
  call->PublicThreadSource.reset();
  auto reject = [&](const char *reason) {
    error(call, DiagID::ERR_GENERIC_SEMA, std::string("public thread: ") + reason);
    return false;
  };
  const auto &diagnostics = DiagnosticEngine::records();
  for (size_t i = start; i < diagnostics.size(); ++i)
    if (diagnostics[i].Level == DiagLevel::Error) return false;
  auto *fn = call->ResolvedFn;
  if (!fn || fn->PublicThread == PublicThreadKind::None || !call->ResolvedType ||
      fn->Effect != EffectKind::None || fn->IsVariadic || !PALCheckerState.IsEnabled)
    return reject("IncompleteCallContract");
  const bool handleRecipe = fn->PublicThread == PublicThreadKind::Join ||
      fn->PublicThread == PublicThreadKind::Detach || fn->PublicThread == PublicThreadKind::Drop;
  if (m_IsPrecomputingCaptures || (m_D3SpeculativeCallDepth && !handleRecipe)) return false;
  if (fn->TemplateOrigin) {
    auto cached = InstantiationCache.find(fn->Name);
    if (cached == InstantiationCache.end() || !cached->second ||
        cached->second->Instance != fn ||
        cached->second->Validation != GenericSpecializationValidationState::Valid)
      return reject("UnqualifiedSpecialization");
  }
  auto p = std::shared_ptr<PublicThreadPlan>(new PublicThreadPlan);
  p->Site = call; p->Declaration = fn; p->Kind = fn->PublicThread;
  p->OwnerDefinition = CurrentFunction;
  p->OutputType = call->ResolvedType;
  const bool spawn = p->Kind == PublicThreadKind::Spawn || p->Kind == PublicThreadKind::SpawnState;
  const bool stateful = p->Kind == PublicThreadKind::SpawnState;
  if (call->Args.size() != (stateful ? 2u : 1u) || fn->Args.size() != call->Args.size())
    return reject("ArityMismatch");
  std::shared_ptr<Type> okType;
  if (p->Kind != PublicThreadKind::Drop) {
    auto output = std::dynamic_pointer_cast<ShapeType>(p->OutputType);
    if (!output || !output->Decl || output->Decl->Kind != ShapeKind::Enum ||
        output->Decl->Members.size() != 2) return reject("ResultSchemaMismatch");
    for (size_t i = 0; i < output->Decl->Members.size(); ++i) {
      const auto &variant = output->Decl->Members[i];
      if (variant.SubMembers.size() != 1) return reject("ResultPayloadIncomplete");
      auto type = getPhysicalType(variant.SubMembers[0]);
      if (!type || type->isUnknown()) return reject("ResultPayloadIncomplete");
      const auto tag = variant.TagValue < 0 ? static_cast<int64_t>(i) : variant.TagValue;
      if (variant.Name == "Ok") { okType = type; p->OkTag = tag; }
      else if (variant.Name == "Err") { p->ErrorType = type; p->ErrTag = tag; }
      else return reject("ResultSchemaMismatch");
    }
    auto errorType = std::dynamic_pointer_cast<ShapeType>(p->ErrorType);
    if (!okType || p->OkTag == p->ErrTag || !errorType || !errorType->Decl ||
        errorType->Decl->Members.size() != 2 || errorType->Decl->HasExplicitDrop)
      return reject("ErrorSchemaMismatch");
    for (size_t i = 0; i < 2; ++i) {
      const auto &field = errorType->Decl->Members[i];
      auto type = getPhysicalType(field);
      if (field.Name != (i ? "native_code" : "kind") || !type ||
          type->toString() != "i32") return reject("ErrorSchemaMismatch");
    }
  } else if (!p->OutputType->isUnit()) return reject("DropResultMismatch");
  p->HandleType = resolveType(spawn ? okType : call->Args[0]->ResolvedType, true);
  auto handle = std::dynamic_pointer_cast<ShapeType>(p->HandleType);
  if (!handle || !handle->Decl || !handle->Decl->InstantiationTemplate ||
      !handle->Decl->HasExplicitDrop || handle->Decl->Members.size() != 2 ||
      handle->Decl->InstantiationArgs.size() != 1) return reject("OpaqueHandleIncomplete");
  const auto *definition = handle->Decl->InstantiationTemplate;
  auto owner = DeclarationLexicalScopes.find(definition);
  if (!definition->NominalId || definition->Name != "JoinHandle" ||
      owner == DeclarationLexicalScopes.end() || !owner->second ||
      !owner->second->IsTrustedSystemModule || !owner->second->ShadowCoordinateKnown ||
      !owner->second->SourceModule ||
      owner->second->SourceModule->ShadowCoordinateOrigin != "toolchain" ||
      owner->second->ShadowLogicalModulePath != "std/thread")
    return reject("HandleDeclarationIdentityMismatch");
  for (size_t i = 0; i < 2; ++i) {
    const auto &field = handle->Decl->Members[i];
    auto type = getPhysicalType(field);
    if (field.Name != (i ? "join_adapter" : "control") || !type || !type->isAddrType())
      return reject("OpaqueHandleSchemaMismatch");
  }
  if (!spawn) {
    if (fn->Args[0].IsCeded || !fn->Args[0].IsValueMutable)
      return reject("ExclusiveHandleRequired");
    p->Handle = call->Args[0].get();
    p->ResultType = okType;
    if (p->Kind == PublicThreadKind::Detach && (!okType || !okType->isUnit()))
      return reject("DetachResultMismatch");
  } else {
    auto sourceReady = [&](Expr *edge, bool destructive) {
      auto path = canonicalizeAccessPath(makeAccessPath(surface(edge)));
      if (!path) return !hasCede(edge);
      if (!destructive) return true;
      if (!hasCede(edge) || !path.Projections.empty()) return false;
      auto place = before.ExactPlaces.find(path.RootName);
      if (place == before.ExactPlaces.end() || !place->second.whole().isExactly(PlaceState::Live)) return false;
      auto pal = before.PAL;
      return !pal.verifyInvalidation(path);
    };
    p->Callable = call->Args[stateful ? 1 : 0].get();
    if (!sourceReady(const_cast<Expr *>(p->Callable), !stateful))
      return reject("SourceTransferUnproven");
    const auto *invoke = findThreadInvoke(const_cast<Expr *>(p->Callable));
    if (!invoke || invoke->Args.size() != (stateful ? 2u : 1u) ||
        invoke->Effect != EffectKind::None || !invoke->Args[0].ResolvedType)
      return reject("ActualInvokeUnproven");
    p->Invoke = invoke;
    p->EnvironmentType = invoke->Args[0].ResolvedType->withAttributes(false, false);
    auto environmentShape = std::dynamic_pointer_cast<ShapeType>(p->EnvironmentType);
    if (!environmentShape || !environmentShape->Decl || !environmentShape->Decl->IsCompilerSynthesized)
      return reject("EnvironmentLayoutUnproven");
    p->CallableType = p->Callable->ResolvedType;
    auto dyn = std::dynamic_pointer_cast<DynFnType>(p->CallableType);
    auto thin = std::dynamic_pointer_cast<FunctionType>(p->CallableType);
    if ((dyn && dyn->ParamTypes.size() != (stateful ? 1u : 0u)) ||
        (thin && thin->ParamTypes.size() != (stateful ? 1u : 0u)))
      return reject("ActualInvokeArityMismatch");
    p->EnvironmentConstruction = dynamic_cast<ClosureExpr *>(surface(const_cast<Expr *>(p->Callable)));
    p->Dynamic = dyn != nullptr && !p->EnvironmentConstruction;
    if ((dyn || thin) && getCallableReceiverMode(*p->CallableType) != invoke->ClosureReceiver)
      return reject("ActualInvokeModeMismatch");
    if (thin && !environmentShape->Decl->Members.empty() &&
        (invoke->ClosureReceiver != CallableReceiverMode::Consuming ||
         lookupD3CopyProof(p->CallableType) != D3CopyProof::ProvenNonCopy))
      return reject("ThinEnvironmentTransferUnqualified");
    p->Consuming = invoke->ClosureReceiver == CallableReceiverMode::Consuming;
    // Existing dynamic environments may have retained aliases. Mutable mode
    // cannot cross this boundary until uniqueness has actually been proved.
    if (dyn && invoke->ClosureReceiver == CallableReceiverMode::Mutable && !p->EnvironmentConstruction)
      return reject("MutableDynamicUniquenessUnproven");
    if (!prepareCallableReturnEnvironment(const_cast<Expr *>(p->Callable)))
      return reject("EnvironmentPreparationFailed");
    auto envFacts = collectStage1CallableEnvironment(const_cast<Expr *>(p->Callable));
    p->NativeOwners = envFacts.NativeOwners;
    p->NativeOwnerCount = envFacts.NativeOwners.size();
    bool independentOwnedEnvironment = false;
    auto ownedEnvironment = std::dynamic_pointer_cast<ShapeType>(p->CallableType);
    if (ownedEnvironment && ownedEnvironment->Decl == environmentShape->Decl) {
      // An unerased closure value is the checked capture record itself,
      // not a thin borrowed identity. Prove every actual physical field.
      independentOwnedEnvironment = true;
      for (const auto &field : environmentShape->Decl->Members) {
        std::string identity; bool drop = false;
        independentOwnedEnvironment &= inspectThreadValue(getPhysicalType(field), identity, drop);
      }
    }
    if (!independentOwnedEnvironment &&
        (!envFacts.Complete || !envFacts.Referents.empty() || !envFacts.LocalBounds.empty()))
      return reject("EnvironmentLifetimeUnproven");
    if (stateful && (!thin || !environmentShape->Decl->Members.empty() || p->Consuming))
      return reject("StateEntryContractMismatch");
    p->ResultType = invoke->ResolvedReturnType;
    auto facadeResult = dyn ? dyn->ReturnType : thin ? thin->ReturnType : p->ResultType;
    if (!p->ResultType || !facadeResult || !p->ResultType->equals(*facadeResult))
      return reject("ActualInvokeResultMismatch");
    if (!p->ResultType->equals(*handle->Decl->InstantiationArgs[0]))
      return reject("HandleResultMismatch");
    p->ResultIdentity = std::string("thread-result;") + TOKA_COMPILER_INTERFACE_VERSION + ";";
    if (!inspectThreadValue(p->ResultType, p->ResultIdentity, p->ResultHasDrop) ||
        !p->ResultType->isSend(this)) return reject("ResultDependenciesUnproven");
    p->ResultIdentity += p->ResultHasDrop ? ";cleanup:typed" : ";cleanup:none";
    if (stateful) {
      p->State = call->Args[0].get(); p->StateType = p->State->ResolvedType;
      std::string identity;
      if (!sourceReady(const_cast<Expr *>(p->State), true) || !p->StateType ||
          !inspectThreadValue(p->StateType, identity, p->StateHasDrop) ||
          !p->StateType->isSend(this)) return reject("StateTransferUnproven");
      auto formal = invoke->Args[1].ResolvedType;
      if (invoke->Args[1].IsCeded || !formal ||
          !formal->withAttributes(false, false)->equals(*p->StateType->withAttributes(false, false)))
        return reject("StateEntryArgumentMismatch");
    }
  }
  if (handleRecipe) {
    // This is not an authority carrier. In particular a cache-warming probe
    // must never set Complete or attach it to the executable AST edge.
    m_PendingPublicHandlePlans[call] = std::move(p);
    return true;
  }
  if (m_D3SpeculativeCallDepth) return false;
  p->Complete = true;
  call->PublicThreadSource = std::move(p);
  return true;
}

bool Sema::finalizePublicThreadPlans() {
  // The only promotion point runs after all ordinary Sema, outside probes.
  if (HasError || m_D3SpeculativeCallDepth || m_IsPrecomputingCaptures) return false;
  for (const auto &entry : m_PendingPublicHandlePlans) {
    const auto *site = entry.first;
    const auto &pending = entry.second;
    auto fail = [&] {
      error(const_cast<CallExpr *>(site), DiagID::ERR_GENERIC_SEMA,
            "public thread: HandleRecipeNotFinallyValidated");
    };
    const auto *owner = pending->OwnerDefinition;
    auto body = m_RawAddressReturns.find(const_cast<FunctionDecl *>(owner));
    if (!owner || !owner->Body || owner->Body->Statements.size() != 1 ||
        body == m_RawAddressReturns.end() || !body->second.Checked || !body->second.Valid ||
        owner->Args.size() != 1 || !owner->Args[0].IsValueMutable ||
        !owner->Args[0].ResolvedType || !pending->HandleType ||
        !owner->Args[0].ResolvedType->withAttributes(false, false)
             ->equals(*pending->HandleType->withAttributes(false, false))) {
      fail(); continue;
    }
    // Limit promotion to the complete checked library wrapper body, not an
    // arbitrary user function or a speculative partial body containing it.
    const Expr *bodyExpression = nullptr;
    if (auto *returned = dynamic_cast<ReturnStmt *>(owner->Body->Statements[0].get()))
      bodyExpression = returned->ReturnValue.get();
    else if (auto *statement = dynamic_cast<ExprStmt *>(owner->Body->Statements[0].get()))
      bodyExpression = statement->Expression.get();
    auto lexical = getLexicalModule(owner->Loc);
    auto *source = dynamic_cast<VariableExpr *>(surface(const_cast<Expr *>(pending->Handle)));
    if (bodyExpression != site || !lexical || !lexical->SourceModule ||
        !lexical->IsTrustedSystemModule || !lexical->ShadowCoordinateKnown ||
        lexical->ShadowLogicalModulePath != "std/thread" ||
        lexical->SourceModule->ShadowCoordinateOrigin != "toolchain" ||
        !source || Type::stripMorphology(source->Name) != "self" || hasCede(const_cast<Expr *>(pending->Handle)) ||
        pending->Complete || pending->Site != site || !site->ResolvedFn ||
        pending->Declaration != site->ResolvedFn || site->Args.size() != 1 ||
        pending->Handle != site->Args[0].get() || !site->ResolvedType ||
        !pending->OutputType->equals(*site->ResolvedType)) {
      fail(); continue;
    }
    auto instance = InstantiationCache.find(site->ResolvedFn->Name);
    if (instance == InstantiationCache.end() || !instance->second ||
        instance->second->Instance != pending->Declaration ||
        instance->second->Validation != GenericSpecializationValidationState::Valid) {
      fail(); continue;
    }
    auto ready = std::shared_ptr<PublicThreadPlan>(new PublicThreadPlan(*pending));
    ready->Complete = true;
    const_cast<CallExpr *>(site)->PublicThreadSource = std::move(ready);
  }
  m_PendingPublicHandlePlans.clear();
  return !HasError;
}
} // namespace toka
