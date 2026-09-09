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
    else if (auto *cast = dynamic_cast<CastExpr *>(expression);
             cast && cast->Kind == CastKind::Ascription) expression = cast->Expression.get();
    else break;
  }
  return expression;
}
bool explicitCede(Expr *expression) {
  while (expression) {
    if (auto *unsafe = dynamic_cast<UnsafeExpr *>(expression)) expression = unsafe->Expression.get();
    else if (auto *cast = dynamic_cast<CastExpr *>(expression); cast && cast->Kind == CastKind::Ascription)
      expression = cast->Expression.get();
    else break;
  }
  return dynamic_cast<CedeExpr *>(expression) != nullptr;
}
}

std::shared_ptr<Type> Sema::checkCallWithThreadHandoff(CallExpr *call) {
  SymbolInfo *symbol = nullptr;
  std::string name;
  FunctionDecl *function = nullptr;
  if (CurrentScope->findVariableWithDeref(call->Callee, symbol, name) && symbol &&
      symbol->TypeObj && symbol->TypeObj->toString() == "fn" && symbol->ASTPtr)
    function = dynamic_cast<FunctionDecl *>(static_cast<ASTNode *>(symbol->ASTPtr));
  // Private library helpers are resolved through the declaration's lexical
  // module, not necessarily through an imported local SymbolInfo. This lookup
  // only selects the pre-call snapshot path; normal resolution below remains
  // authoritative, and qualification checks its resolved declaration again.
  if (!function && !symbol) {
    ModuleScope *lexical = nullptr;
    if (CurrentFunction) {
      auto owner = DeclarationLexicalScopes.find(CurrentFunction);
      if (owner != DeclarationLexicalScopes.end()) lexical = owner->second;
    }
    if (!lexical) lexical = getLexicalModule(call->Loc);
    if (lexical) {
      auto found = lexical->Functions.find(call->Callee);
      if (found != lexical->Functions.end()) function = found->second;
    }
    if (!function) {
      auto instance = InstantiationCache.find(call->Callee);
      if (instance != InstantiationCache.end() && instance->second)
        function = instance->second->Instance;
    }
  }
  if (!function) return checkCallExpr(call);
  if (function->NativeSyncFactory != NativeSyncFactoryKind::None) {
    const size_t start = DiagnosticEngine::records().size();
    CallArgumentRollbackGuard rollback(*this, call->Args, true);
    call->NativeSyncFactorySource.reset();
    auto result = checkCallExpr(call);
    call->ResolvedType = result;
    if (!qualifyNativeSyncFactory(call, start)) rollback.reject();
    else m_LastInitMask = ~0ULL;
    return result;
  }
  if (function->PublicThread != PublicThreadKind::None) {
    auto before = captureAnalysisState();
    const size_t start = DiagnosticEngine::records().size();
    CallArgumentRollbackGuard rollback(*this, call->Args, true);
    auto result = checkCallExpr(call);
    call->ResolvedType = result;
    if (!qualifyPublicThread(call, before, start)) rollback.reject();
    else m_LastInitMask = ~0ULL;
    return result;
  }
  if (function->ThreadProbe == ThreadProbeKind::None) return checkCallExpr(call);
  if (m_IsPrecomputingCaptures || m_D3SpeculativeCallDepth != 0 || !PALCheckerState.IsEnabled) {
    error(call, DiagID::ERR_GENERIC_SEMA, "thread handoff: FinalCheckedContextRequired");
    return Type::fromString("unknown");
  }
  // Dedicated intrinsic checking: never execute/instantiate its placeholder
  // body. Real argument expressions, source state and capture bodies still go
  // through normal Sema exactly once before the immutable plan can be sealed.
  auto before = captureAnalysisState();
  const size_t diagnosticStart = DiagnosticEngine::records().size();
  CallArgumentRollbackGuard rollback(*this, call->Args, true);
  call->ThreadHandoffSource.reset();
  call->ResolvedFn = function;
  if (symbol) {
    symbol->HasBeenUsed = true;
    if (symbol->ImportingDecl) const_cast<ImportDecl *>(symbol->ImportingDecl)->HasBeenUsed = true;
  }
  if (call->Args.size() != 1 || call->GenericArgs.size() != 2 || call->isInitArgument(0)) {
    error(call, DiagID::ERR_GENERIC_SEMA, "thread handoff: InvalidProbeArity");
    rollback.reject(); return Type::fromString("unknown");
  }
  checkExpr(call->Args[0].get());
  if (!qualifyThreadHandoffSource(call, before, diagnosticStart)) {
    rollback.reject(); return Type::fromString("unknown");
  }
  m_LastInitMask = ~0ULL; // a completed handoff produces a complete result, not the argument's mask
  return function->ThreadProbe == ThreadProbeKind::Run
      ? call->ThreadHandoffSource->ResultType : Type::fromString("()");
}

bool Sema::qualifyThreadHandoffSource(CallExpr *call, const AnalysisState &before,
                                     size_t diagnosticStart) {
  auto reject = [&](const char *reason) {
    error(call, DiagID::ERR_GENERIC_SEMA, std::string("thread handoff: ") + reason);
    return false;
  };
  const auto &diagnostics = DiagnosticEngine::records();
  if (std::any_of(diagnostics.begin() + std::min(diagnosticStart, diagnostics.size()),
                  diagnostics.end(), [](const auto &d) { return d.Level == DiagLevel::Error; }))
    return false;
  auto *function = call->ResolvedFn;
  if (!function || function->ThreadProbe == ThreadProbeKind::None ||
      call->Args.size() != 1 || function->Args.size() != 1 || !function->Args[0].IsCeded ||
      function->Args[0].IsInit || function->Args[0].DefaultValue || function->IsVariadic ||
      function->Effect != EffectKind::None)
    return reject("IncompleteCallContract");
  if (function->TemplateOrigin) {
    auto instance = InstantiationCache.find(function->Name);
    if (instance == InstantiationCache.end() || !instance->second ||
        instance->second->Instance != function ||
        instance->second->Validation != GenericSpecializationValidationState::Valid)
      return reject("UnqualifiedSpecialization");
  }
  Expr *argument = call->Args[0].get();
  auto callable = std::dynamic_pointer_cast<DynFnType>(argument->ResolvedType);
  if (!callable || !callable->ParamTypes.empty() || !callable->ReturnType)
    return reject("OwnedNullaryDynFnRequired");
  if (function->GenericParams.size() != 2 || !function->Args[0].TypeSyntax ||
      function->Args[0].TypeSyntax->NodeKind != TypeSyntax::Kind::Named ||
      function->Args[0].TypeSyntax->Text != function->GenericParams[1].Name)
    return reject("InvalidIntrinsicDeclarationSchema");
  const auto &formal = function->Args[0];
  if (formal.IsRawPointer || formal.IsUnique || formal.IsShared || formal.IsReference ||
      formal.IsMorphicExempt || formal.IsRebindable || formal.IsValueMutable ||
      formal.IsPointerNullable || formal.IsValueNullable || formal.IsRebindBlocked || formal.IsValueBlocked)
    return reject("InvalidIntrinsicDeclarationSchema");
  for (const auto &parameter : function->GenericParams)
    if (parameter.IsConst || parameter.IsMorphic || !parameter.Type.empty() ||
        !parameter.TraitBounds.empty() || !parameter.MorphologyBounds.empty())
      return reject("InvalidIntrinsicDeclarationSchema");
  if (function->ThreadProbe == ThreadProbeKind::Run &&
      (!function->ReturnTypeSyntax || function->ReturnTypeSyntax->NodeKind != TypeSyntax::Kind::Named ||
       function->ReturnTypeSyntax->Text != function->GenericParams[0].Name))
    return reject("InvalidIntrinsicReturnSchema");
  if (function->ThreadProbe != ThreadProbeKind::Run &&
      function->ReturnContract.ResultKind != ReturnResultKind::Unit)
    return reject("InvalidIntrinsicReturnSchema");
  const auto declaredCallable = resolveType(Type::fromString(call->GenericArgs[1]), true);
  if (!declaredCallable || !declaredCallable->equals(*callable))
    return reject("CallableFormalModeMismatch");
  Expr *source = surface(argument);
  const auto path = canonicalizeAccessPath(makeAccessPath(source));
  if (path) {
    if (!explicitCede(argument)) return reject("MissingCedeForNamedSource");
    if (!path.Projections.empty()) return reject("ProjectedSourceUnqualified");
    auto live = before.Moved.find(path.RootName);
    if (live == before.Moved.end() || live->second) return reject("SourceNotLive");
    const auto exact = before.ExactPlaces.find(path.RootName);
    const auto initialized = before.InitMasks.find(path.RootName);
    if (exact == before.ExactPlaces.end() || !exact->second.whole().isExactly(PlaceState::Live) ||
        initialized == before.InitMasks.end() || initialized->second == 0)
      return reject("SourceInitializationUnproven");
    SymbolInfo *symbol = nullptr;
    if (!CurrentScope->findSymbolByID(path.RootID, symbol) || !symbol ||
        symbol->IsPlaceAlias || (symbol->IsFunctionParameter && !symbol->IsCeded))
      return reject("SourceTransferAuthorityMissing");
    if (!symbol->IsFunctionParameter) {
      auto *declaration = symbol->ASTPtr
          ? dynamic_cast<VariableDecl *>(static_cast<ASTNode *>(symbol->ASTPtr)) : nullptr;
      auto owner = m_LocalVariableOwners.find(declaration);
      if (!declaration || owner == m_LocalVariableOwners.end() || owner->second != CurrentFunction)
        return reject("NonLocalSourceUnqualified");
    }
    auto pal = before.PAL;
    if (pal.verifyInvalidation(path)) return reject("SourceBorrowConflict");
  } else if (explicitCede(argument)) return reject("CedeRequiresNamedSource");

  bool send = false;
  if (auto *closure = dynamic_cast<ClosureExpr *>(source)) {
    send = closure->HasBoundaryCaptureSummary && closure->BoundaryImplicitCaptures.empty() &&
           closure->BoundaryNonSendCaptures.empty() && closure->BoundaryNonSyncCopyCaptures.empty();
  } else if (auto *variable = dynamic_cast<VariableExpr *>(source)) {
    SymbolInfo *symbol = nullptr;
    std::string name;
    if (CurrentScope->findVariableWithDeref(variable->Name, symbol, name) && symbol)
      send = symbol->HasClosureBoundarySummary && symbol->ClosureImplicitCaptures.empty() &&
             symbol->ClosureNonSendCaptures.empty() && symbol->ClosureNonSyncCopyCaptures.empty();
  }
  if (!send) return reject("ActualEnvironmentSendUnproven");
  if (!prepareCallableReturnEnvironment(argument)) return reject("EnvironmentPreparationFailed");
  auto environment = collectStage1CallableEnvironment(argument);
  if (!environment.Complete || !environment.Referents.empty() || !environment.LocalBounds.empty())
    return reject("EnvironmentLifetimeUnproven");

  // Ascription describes a requested surface type; it cannot rewrite the
  // actual invoke's capture cleanup contract. Recover a checked declaration
  // from immutable local initializer provenance, never from the annotated type.
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
  const auto *invoke = actualInvoke(argument);
  if (!invoke) return reject("ActualInvokeUnproven");
  if (invoke->ClosureReceiver != getCallableReceiverMode(*callable))
    return reject("ActualInvokeModeMismatch");
  auto invokeResult = resolveType(invoke->ResolvedReturnType, true);
  auto requestedResult = resolveType(callable->ReturnType, true);
  if (invoke->Args.size() != 1 || !invokeResult || !requestedResult ||
      !invokeResult->equals(*requestedResult))
    return reject("ActualInvokeSignatureMismatch");

  // Result independence is a separate closed-world proof, not a consequence
  // of @Send or lack of diagnostics. Opaque/borrowed/callable results wait for
  // a richer actual-return summary; there is no declaration-dependency fallback.
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
    if (!shape || !shape->Decl || !shape->Decl->NominalId ||
        !visiting.insert(shape->Decl).second) return false;
    identity += shape->Decl->NominalId->canonical();
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
  auto result = resolveType(callable->ReturnType, true);
  // Unit storage is i8 but a Unit callable returns ABI void. The frozen core
  // does not yet carry this distinct invoke/result-storage conversion.
  if (result && result->isUnit()) return reject("UnitResultABIUnqualified");
  if (!call->GenericArgs.empty()) {
    auto expected = resolveType(Type::fromString(call->GenericArgs[0]), true);
    if (!expected || !result || !expected->equals(*result)) return reject("ResultTypeArgumentMismatch");
  }
  std::string identity = std::string("thread-result;") + TOKA_COMPILER_INTERFACE_VERSION + ";";
  bool needsDrop = false;
  if (!resultFacts(result, identity, needsDrop)) return reject("ResultDependenciesUnproven");
  if (!result->isSend(this)) return reject("ResultNotSend");
  identity += needsDrop ? ";cleanup:typed" : ";cleanup:none";
  const auto mode = invoke->ClosureReceiver;
  if (mode != CallableReceiverMode::Shared && mode != CallableReceiverMode::Consuming)
    return reject("CallableModeUnqualified");
  if (DiagnosticEngine::records().size() > diagnosticStart &&
      std::any_of(DiagnosticEngine::records().begin() + diagnosticStart,
                  DiagnosticEngine::records().end(), [](const auto &d) { return d.Level == DiagLevel::Error; }))
    return false;
  auto plan = std::shared_ptr<ThreadHandoffSourcePlan>(new ThreadHandoffSourcePlan);
  plan->Site = call;
  plan->Source = argument;
  plan->Declaration = function;
  plan->InvokeDeclaration = invoke;
  plan->CallableType = callable;
  plan->ResultType = result;
  plan->Kind = function->ThreadProbe;
  plan->Consuming = mode == CallableReceiverMode::Consuming;
  plan->ResultHasDrop = needsDrop;
  plan->ResultIdentity = std::move(identity);
  plan->Complete = true;
  call->ThreadHandoffSource = std::move(plan);
  return true;
}
}
