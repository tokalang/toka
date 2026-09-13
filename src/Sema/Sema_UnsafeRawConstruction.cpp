#include "toka/Sema.h"
#include <algorithm>
#include <functional>

namespace toka {
namespace {
RawAddressSourcePtr opaqueAddress() { return std::make_shared<RawAddressSource>(); }
bool pointerLike(const std::shared_ptr<Type> &type) {
  return type && (type->isPointer() || type->isSmartPointer() || type->isReference());
}
}

RawAddressSourcePtr Sema::mergeRawAddressSources(RawAddressSourcePtr lhs, RawAddressSourcePtr rhs) {
  if (!lhs) return rhs;
  if (!rhs || lhs == rhs) return lhs;
  auto merged = std::make_shared<RawAddressSource>();
  merged->Tag = RawAddressSource::Kind::Merge;
  merged->Inputs = {std::move(lhs), std::move(rhs)};
  return merged;
}

RawAddressSourcePtr Sema::rawStorageOrigin(Expr *expression, bool readOnly) {
  auto node = std::make_shared<RawAddressSource>();
  node->ReadOnly = readOnly;
  auto path = canonicalizeAccessPath(makeAccessPath(expression));
  SymbolInfo *info = nullptr;
  if (path.RootID && CurrentScope) CurrentScope->findSymbolByID(path.RootID, info);
  if (info && info->IsFunctionParameter && CurrentFunction) {
    for (size_t index = 0; index < CurrentFunction->Args.size(); ++index) {
      if (Type::stripMorphology(CurrentFunction->Args[index].Name) !=
          Type::stripMorphology(path.RootName)) continue;
      auto parameter = std::make_shared<RawAddressSource>();
      parameter->Tag = RawAddressSource::Kind::ParameterStorage;
      parameter->Parameter = index;
      parameter->Origins.push_back(path);
      node->Inputs.push_back(parameter);
      return node;
    }
  }
  if (path && path.RootID) node->Origins.push_back(path);
  return node;
}

RawAddressSourcePtr Sema::collectRawAddressSource(Expr *expression, bool view) {
  if (!expression) return opaqueAddress();
  if (view && expression->RawAddressViewFacts) return expression->RawAddressViewFacts;
  if (!view && expression->RawAddressValueFacts) return expression->RawAddressValueFacts;
  if (auto *cast = dynamic_cast<CastExpr *>(expression)) {
    auto source = cast->AddressSource ? cast->AddressSource : collectRawAddressSource(cast->Expression.get(), view);
    if (view && pointerLike(cast->ResolvedType)) {
      auto target = cast->ResolvedType->getPointeeType();
      auto restriction = std::make_shared<RawAddressSource>();
      restriction->ReadOnly = target && !target->isVoid() && (!target->IsWritable || target->IsBlocked);
      restriction->Inputs = {source};
      return restriction;
    }
    return source;
  }
  if (auto *unsafe = dynamic_cast<UnsafeExpr *>(expression))
    return collectRawAddressSource(unsafe->Expression.get(), view);
  if (auto *cede = dynamic_cast<CedeExpr *>(expression))
    return collectRawAddressSource(cede->Value.get(), view);
  if (auto *postfix = dynamic_cast<PostfixExpr *>(expression))
    return collectRawAddressSource(postfix->LHS.get(), view);
  if (auto *address = dynamic_cast<AddressOfExpr *>(expression)) {
    auto type = expression->ResolvedType;
    const bool readOnly = !type || !type->getPointeeType() || !type->getPointeeType()->IsWritable;
    return mergeRawAddressSources(collectRawAddressSource(address->Expression.get(), false),
                                   rawStorageOrigin(address->Expression.get(), readOnly));
  }
  if (auto *unary = dynamic_cast<UnaryExpr *>(expression)) {
    if (unary->Op == TokenType::Ampersand) {
      auto type = expression->ResolvedType;
      const bool readOnly = !type || !type->getPointeeType() || !type->getPointeeType()->IsWritable;
      return mergeRawAddressSources(collectRawAddressSource(unary->RHS.get(), false),
                                     rawStorageOrigin(unary->RHS.get(), readOnly));
    }
    return collectRawAddressSource(unary->RHS.get(), view);
  }
  RawAddressSourcePtr result;
  bool recognized = true;
  if (auto *variable = dynamic_cast<VariableExpr *>(expression)) {
    SymbolInfo *info = nullptr;
    std::string name;
    if (CurrentScope && CurrentScope->findVariableWithDeref(variable->Name, info, name) && info) {
      auto stored = m_RawAddressBindings.find(info->SymbolID);
      if (stored != m_RawAddressBindings.end()) result = stored->second;
      auto canonical = canonicalizeAccessPath(makeAccessPath(variable));
      if (canonical.RootID && canonical.RootID != info->SymbolID) {
        auto carried = m_RawAddressBindings.find(canonical.RootID);
        if (carried != m_RawAddressBindings.end()) result = mergeRawAddressSources(result, carried->second);
      }
      if (info->CurrentReferenceTargets)
        for (const auto &referent : *info->CurrentReferenceTargets) {
          auto carried = m_RawAddressBindings.find(referent.RootID);
          if (carried != m_RawAddressBindings.end()) result = mergeRawAddressSources(result, carried->second);
        }
      if (!result && info->IsFunctionParameter && CurrentFunction) {
        for (size_t index = 0; index < CurrentFunction->Args.size(); ++index) {
          if (Type::stripMorphology(CurrentFunction->Args[index].Name) != Type::stripMorphology(name)) continue;
          auto parameter = std::make_shared<RawAddressSource>();
          parameter->Tag = RawAddressSource::Kind::ParameterValue;
          parameter->Parameter = index;
          result = parameter;
          break;
        }
      }
      auto visibleType = expression->ResolvedType ? expression->ResolvedType : info->TypeObj;
      if (view && pointerLike(visibleType)) {
        auto pointee = visibleType->getPointeeType();
        auto storage = std::make_shared<RawAddressSource>();
        // An untyped void address has no typed pointee permission. A typed
        // readonly view or an established flow/freeze ceiling is different.
        storage->ReadOnly = (pointee && !pointee->isVoid() &&
                              (!pointee->IsWritable || pointee->IsBlocked || !info->Permission.SoulWritable)) ||
                           info->HasPayloadFlowCeiling && !info->PayloadFlowWritable ||
                           info->Permission.SoulBlocked;
        storage->MayBeNull = visibleType->IsNullable && !m_NarrowedPaths.count(getPathString(expression));
        auto path = canonicalizeAccessPath(makeAccessPath(variable));
        if (path && path.RootID) storage->Origins.push_back(path);
        if (info->IsFunctionParameter && CurrentFunction) {
          for (size_t index = 0; index < CurrentFunction->Args.size(); ++index)
            if (Type::stripMorphology(CurrentFunction->Args[index].Name) == Type::stripMorphology(name)) {
              storage->Tag = RawAddressSource::Kind::ParameterView;
              storage->Parameter = index;
              break;
            }
        }
        result = mergeRawAddressSources(result, storage);
        if (visibleType->IsNullable && m_NarrowedPaths.count(getPathString(expression))) {
          auto nonzero = std::make_shared<RawAddressSource>();
          nonzero->NonNull = true;
          nonzero->Inputs = {result};
          result = nonzero;
        }
      }
    }
  } else if (auto *member = dynamic_cast<MemberExpr *>(expression)) {
    result = collectRawAddressSource(member->Object.get(), false);
    if (view && pointerLike(member->ResolvedType)) {
      auto payload = member->ResolvedType->getPointeeType();
      auto capability = queryExplicitCedeStage0AccessCapabilityReadOnly(member);
      result = mergeRawAddressSources(result, rawStorageOrigin(member,
          capability.PayloadFlowRestricted || (payload && !payload->isVoid() &&
              (!payload->IsWritable || payload->IsBlocked))));
    }
  } else if (auto *index = dynamic_cast<ArrayIndexExpr *>(expression)) {
    result = collectRawAddressSource(index->Array.get(), false);
  } else if (auto *binary = dynamic_cast<BinaryExpr *>(expression)) {
    result = mergeRawAddressSources(collectRawAddressSource(binary->LHS.get()),
                                   collectRawAddressSource(binary->RHS.get()));
  } else if (auto *init = dynamic_cast<InitStructExpr *>(expression)) {
    for (const auto &field : init->Members)
      result = mergeRawAddressSources(result, collectRawAddressSource(field.second.get()));
  } else if (auto *record = dynamic_cast<AnonymousRecordExpr *>(expression)) {
    for (const auto &field : record->Fields)
      result = mergeRawAddressSources(result, collectRawAddressSource(field.second.get()));
  } else if (auto *array = dynamic_cast<ArrayExpr *>(expression)) {
    for (const auto &element : array->Elements)
      result = mergeRawAddressSources(result, collectRawAddressSource(element.get()));
  } else if (auto *repeated = dynamic_cast<RepeatedArrayExpr *>(expression)) {
    result = collectRawAddressSource(repeated->Value.get());
  } else if (auto *allocated = dynamic_cast<NewExpr *>(expression)) {
    result = collectRawAddressSource(allocated->Initializer.get(), false);
  } else if (auto *closure = dynamic_cast<ClosureExpr *>(expression)) {
    for (const auto &capture : closure->ExplicitCaptures) {
      VariableExpr source(Type::stripMorphology(capture.Name));
      source.Loc = capture.Loc;
      result = mergeRawAddressSources(result, collectRawAddressSource(&source));
    }
  } else if (auto *call = dynamic_cast<CallExpr *>(expression)) {
    auto node = std::make_shared<RawAddressSource>();
    node->Tag = RawAddressSource::Kind::Call;
    node->Function = call->ResolvedFn;
    for (const auto &argument : call->Args) {
      node->ArgumentValues.push_back(collectRawAddressSource(argument.get(), false));
      node->ArgumentViews.push_back(collectRawAddressSource(argument.get(), true));
      node->ArgumentStorage.push_back(rawStorageOrigin(argument.get(), false));
    }
    if (!node->Function) {
      VariableExpr callee(call->OriginalCallee.empty() ? call->Callee : call->OriginalCallee);
      result = collectRawAddressSource(&callee, false);
    }
    result = mergeRawAddressSources(result, node);
  } else if (auto *method = dynamic_cast<MethodCallExpr *>(expression)) {
    auto node = std::make_shared<RawAddressSource>();
    node->Tag = RawAddressSource::Kind::Call;
    node->Function = method->ResolvedFn;
    auto append = [&](Expr *argument) {
      node->ArgumentValues.push_back(collectRawAddressSource(argument, false));
      node->ArgumentViews.push_back(collectRawAddressSource(argument, true));
      node->ArgumentStorage.push_back(rawStorageOrigin(argument, false));
    };
    append(method->Object.get());
    for (const auto &argument : method->Args) append(argument.get());
    result = node;
  } else if (auto *branch = dynamic_cast<IfExpr *>(expression)) {
    std::function<RawAddressSourcePtr(Stmt *)> valueOf = [&](Stmt *statement) -> RawAddressSourcePtr {
      if (!statement) return opaqueAddress();
      if (auto *block = dynamic_cast<BlockStmt *>(statement))
        return block->Statements.empty() ? opaqueAddress() : valueOf(block->Statements.back().get());
      if (auto *ret = dynamic_cast<ReturnStmt *>(statement)) return collectRawAddressSource(ret->ReturnValue.get());
      if (auto *expr = dynamic_cast<ExprStmt *>(statement)) return collectRawAddressSource(expr->Expression.get());
      return opaqueAddress();
    };
    result = mergeRawAddressSources(valueOf(branch->Then.get()), valueOf(branch->Else.get()));
  } else if (auto *number = dynamic_cast<NumberExpr *>(expression)) {
    auto literal = std::make_shared<RawAddressSource>();
    literal->MayBeNull = number->Value == 0 && number->ResolvedType && number->ResolvedType->isAddrType();
    result = literal;
  } else if (dynamic_cast<AllocExpr *>(expression) ||
             dynamic_cast<ViewStringExpr *>(expression) || dynamic_cast<StringExpr *>(expression)) {
    result = opaqueAddress();
  } else recognized = false;
  if (!recognized && expression->ResolvedType &&
      (expression->ResolvedType->isAddrType() || pointerLike(expression->ResolvedType))) {
    auto unknown = std::make_shared<RawAddressSource>();
    unknown->Tag = RawAddressSource::Kind::Unresolved;
    result = unknown;
  }
  if (view && pointerLike(expression->ResolvedType) &&
      !dynamic_cast<VariableExpr *>(expression) && !dynamic_cast<MemberExpr *>(expression) &&
      !dynamic_cast<AllocExpr *>(expression) && !dynamic_cast<NewExpr *>(expression)) {
    auto restriction = std::make_shared<RawAddressSource>();
    auto payload = expression->ResolvedType->getPointeeType();
    restriction->ReadOnly = payload && !payload->isVoid() && (!payload->IsWritable || payload->IsBlocked);
    restriction->MayBeNull = expression->ResolvedType->IsNullable;
    restriction->Inputs = {result ? result : opaqueAddress()};
    if (auto *method = dynamic_cast<MethodCallExpr *>(expression);
        method && !method->ResolvedFn && method->Method == "unwrap" && method->Args.empty() &&
        method->Object->ResolvedType && method->Object->ResolvedType->isRawPointer() &&
        method->Object->ResolvedType->IsNullable && expression->ResolvedType->isRawPointer() &&
        !expression->ResolvedType->IsNullable)
      restriction->NonNull = true;
    result = restriction;
  }
  return result ? result : opaqueAddress();
}

void Sema::recordRawAddressBinding(const AccessPath &place, Expr *source) {
  if (!place.RootID || !source) return;
  m_RawAddressBindings[place.RootID] = mergeRawAddressSources(
      m_RawAddressBindings[place.RootID], collectRawAddressSource(source));
}

void Sema::recordRawAddressReturn(ReturnStmt *statement) {
  if (!CurrentFunction || !statement->ReturnValue) return;
  auto &summary = m_RawAddressReturns[CurrentFunction];
  if (!summary.Checking || summary.ClosureDepth != m_CallableReturnClosureDepth) return;
  summary.Returns.push_back(collectRawAddressSource(statement->ReturnValue.get()));
}

void Sema::prepareUnsafeRawConstruction(CastExpr *cast, const std::shared_ptr<Type> &sourceType,
                                        const std::shared_ptr<Type> &targetType, size_t diagnosticStart) {
  if (!cast || !sourceType || !targetType) return;
  cast->AddressSource = collectRawAddressSource(cast->Expression.get());
  if (pointerLike(targetType)) {
    auto constraints = std::make_shared<RawAddressSource>();
    auto pointee = targetType->getPointeeType();
    constraints->ReadOnly = pointee && !pointee->isVoid() && (!pointee->IsWritable || pointee->IsBlocked);
    cast->AddressSource = mergeRawAddressSources(cast->AddressSource, constraints);
  }
  if (cast->Kind == CastKind::Conversion && targetType->isRawPointer() &&
      targetType->getPointeeType() && targetType->getPointeeType()->IsWritable && !sourceType->isAddrType()) {
    const auto &diagnostics = DiagnosticEngine::records();
    if (std::none_of(diagnostics.begin() + diagnosticStart, diagnostics.end(),
                     [](const auto &record) { return record.Level == DiagLevel::Error; })) {
      auto existing = pointerLike(sourceType) ? sourceType->getPointeeType() : nullptr;
      if (!existing || !existing->IsWritable)
        error(cast, DiagID::ERR_SEMA_UNSAFE_RAW_CONSTRUCTION, "ExplicitAddrConstructionRequired");
      else {
        auto restrictions = resolveRawAddressSource(collectRawAddressSource(cast->Expression.get()));
        if (!restrictions.Complete || restrictions.ReadOnly)
          error(cast, DiagID::ERR_SEMA_UNSAFE_RAW_CONSTRUCTION,
                restrictions.ReadOnly ? "KnownReadOnlyOrFrozenSource" : "UnresolvedAddressRestrictions");
      }
    }
    return;
  }
  if (cast->Kind != CastKind::Conversion || !sourceType->isAddrType() ||
      !cast->RawWriteRequest ||
      !targetType->isRawPointer() || !targetType->getPointeeType() ||
      !targetType->getPointeeType()->IsWritable) return;
  cast->RequiresRawConstruction = true;
  auto plan = std::make_shared<UnsafeRawConstructionPlan>();
  plan->WriteRequest = cast->RawWriteRequest;
  cast->RawConstruction = plan;
  plan->Site = cast;
  plan->SourceEdge = cast->Expression.get();
  plan->SourceType = sourceType->toString();
  plan->TargetType = targetType->toString();
  plan->Nullable = targetType->IsNullable;
  plan->Authority = RawWriteAuthority::UnsafeCallerPrecondition;
  const auto &records = DiagnosticEngine::records();
  if (std::any_of(records.begin() + diagnosticStart, records.end(),
                  [](const auto &record) { return record.Level == DiagLevel::Error; })) {
    plan->Rejection = "PriorValidationFailed";
    return;
  }
  if (!m_InUnsafeContext) {
    plan->Rejection = "UnsafeContextRequired";
    error(cast, DiagID::ERR_SEMA_UNSAFE_RAW_CONSTRUCTION, plan->Rejection);
    return;
  }
  std::function<bool(Expr *)> knownZero = [&](Expr *source) {
    if (auto *number = dynamic_cast<NumberExpr *>(source)) return number->Value == 0;
    if (auto *wrapper = dynamic_cast<CastExpr *>(source)) return knownZero(wrapper->Expression.get());
    if (auto *variable = dynamic_cast<VariableExpr *>(source)) return variable->Name == "ADDR0";
    return false;
  };
  if (!plan->Nullable && knownZero(cast->Expression.get())) {
    plan->Rejection = "KnownZeroAddress";
    error(cast, DiagID::ERR_NONZERO_RAW_NULL_FLOW, targetType->toString());
    return;
  }
  plan->Prepared = true;
  auto source = collectRawAddressSource(cast->Expression.get());
  auto facts = resolveRawAddressSource(source);
  plan->RestrictionsComplete = facts.Complete;
  if (!facts.Complete) plan->Rejection = "UnresolvedAddressRestrictions";
  else if (facts.ReadOnly) plan->Rejection = "KnownReadOnlyOrFrozenSource";
  else if (facts.MayBeNull && !plan->Nullable) plan->Rejection = "KnownNullableSourceRequiresGuard";
  else for (const auto &origin : facts.Origins) {
    plan->KnownOrigins.push_back(origin.toLegacyString());
    if (PALCheckerState.verifyExclusiveMutation(origin)) { plan->Rejection = "KnownBorrowConflict"; break; }
  }
  plan->SemaValidated = plan->Rejection.empty();
  if (!plan->SemaValidated) error(cast, DiagID::ERR_SEMA_UNSAFE_RAW_CONSTRUCTION, plan->Rejection);
  auto authority = std::make_shared<RawAddressSource>();
  authority->UnsafeConstruction = true;
  authority->Inputs = {cast->AddressSource};
  cast->AddressSource = authority;
  m_UnsafeRawConstructions.push_back({plan, source, PALCheckerState.snapshot()});
}

Sema::ResolvedRawAddressFacts Sema::resolveRawAddressSource(RawAddressSourcePtr root) {
  using Facts = ResolvedRawAddressFacts;
  struct Context { size_t Identity = 0; std::vector<Facts> Values, Views, Storage; };
  auto merge = [](Facts &target, const Facts &source) {
    target.Complete &= source.Complete;
    target.ReadOnly |= source.ReadOnly;
    target.MayBeNull |= source.MayBeNull;
    target.UnsafeConstruction |= source.UnsafeConstruction;
    for (const auto &path : source.Origins)
      if (std::find(target.Origins.begin(), target.Origins.end(), path) == target.Origins.end())
        target.Origins.push_back(path);
  };
  std::set<FunctionDecl *> evaluating;
  std::map<std::pair<const RawAddressSource *, size_t>, Facts> memo;
  size_t nextContext = 1;
  std::function<Facts(RawAddressSourcePtr, const Context *)> evaluate;
  evaluate = [&](RawAddressSourcePtr source, const Context *context) -> Facts {
    Facts result;
    if (!source) return result;
    const auto key = std::make_pair(source.get(), context ? context->Identity : size_t{0});
    if (auto found = memo.find(key); found != memo.end()) return found->second;
    result.ReadOnly = source->ReadOnly;
    result.MayBeNull = source->MayBeNull;
    result.UnsafeConstruction = source->UnsafeConstruction;
    result.Origins = source->Origins;
    using Kind = RawAddressSource::Kind;
    if (source->Tag == Kind::Unresolved) result.Complete = false;
    if (source->Tag == Kind::ParameterValue || source->Tag == Kind::ParameterView ||
        source->Tag == Kind::ParameterStorage) {
      if (context) {
        result.Origins.clear();
        const auto &values = source->Tag == Kind::ParameterValue ? context->Values :
                             source->Tag == Kind::ParameterView ? context->Views : context->Storage;
        if (source->Parameter >= values.size()) result.Complete = false;
        else merge(result, values[source->Parameter]);
      }
    } else if (source->Tag == Kind::Call) {
      Context arguments;
      arguments.Identity = nextContext++;
      for (const auto &arg : source->ArgumentValues) arguments.Values.push_back(evaluate(arg, context));
      for (const auto &arg : source->ArgumentViews) arguments.Views.push_back(evaluate(arg, context));
      for (const auto &arg : source->ArgumentStorage) arguments.Storage.push_back(evaluate(arg, context));
      auto *function = source->Function;
      if (!function || !function->Body) {
        // Opaque calls cannot launder restrictions already carried by an
        // argument. No return type is used to invent a positive permission.
        for (const auto &arg : arguments.Values) merge(result, arg);
      } else {
        auto found = m_RawAddressReturns.find(function);
        if (found == m_RawAddressReturns.end() || (!found->second.Checked && !found->second.Checking)) {
          m_RawAddressPreparedDefinitions.insert(function);
          (void)prepareCallableFactory(function);
          found = m_RawAddressReturns.find(function);
        }
        if (found == m_RawAddressReturns.end() || !found->second.Checked || !found->second.Valid ||
            !evaluating.insert(function).second) result.Complete = false;
        else {
          for (const auto &returned : found->second.Returns) merge(result, evaluate(returned, &arguments));
          evaluating.erase(function);
        }
      }
    }
    for (const auto &input : source->Inputs) merge(result, evaluate(input, context));
    if (source->NonNull) result.MayBeNull = false;
    memo.emplace(key, result);
    return result;
  };
  return evaluate(root, nullptr);
}

bool Sema::checkConstructedRawFlow(Expr *source, const std::shared_ptr<Type> &target) {
  if (!target || !target->isRawPointer() || !target->getPointeeType() ||
      !target->getPointeeType()->IsWritable) return true;
  auto facts = resolveRawAddressSource(collectRawAddressSource(source));
  if (!facts.UnsafeConstruction) return true;
  if (!facts.Complete || facts.ReadOnly) {
    error(source, DiagID::ERR_SEMA_UNSAFE_RAW_CONSTRUCTION,
          facts.ReadOnly ? "KnownReadOnlyOrFrozenSource" : "UnresolvedAddressRestrictions");
    return false;
  }
  return true;
}

bool Sema::hasQualifiedUnsafeRawConstruction(Expr *source) const {
  while (source) {
    if (auto *unsafe = dynamic_cast<UnsafeExpr *>(source)) source = unsafe->Expression.get();
    else if (auto *cede = dynamic_cast<CedeExpr *>(source)) source = cede->Value.get();
    else if (auto *cast = dynamic_cast<CastExpr *>(source)) {
      if (cast->RawConstruction && cast->RawConstruction->SemaValidated &&
          cast->RawConstruction->Authority == RawWriteAuthority::UnsafeCallerPrecondition) return true;
      source = cast->Expression.get();
    } else break;
  }
  return false;
}

bool Sema::finalizeUnsafeRawConstructions() {
  bool valid = true;
  for (size_t index = 0; index < m_UnsafeRawConstructions.size(); ++index) {
    auto pending = m_UnsafeRawConstructions[index];
    auto plan = pending.Plan.lock();
    if (!plan || !plan->Prepared) continue;
    auto facts = resolveRawAddressSource(pending.Source);
    plan->RestrictionsComplete = facts.Complete;
    if (!facts.Complete) plan->Rejection = "UnresolvedAddressRestrictions";
    else if (facts.ReadOnly) plan->Rejection = "KnownReadOnlyOrFrozenSource";
    else if (facts.MayBeNull && !plan->Nullable) plan->Rejection = "KnownNullableSourceRequiresGuard";
    else {
      for (const auto &origin : facts.Origins) {
        plan->KnownOrigins.push_back(origin.toLegacyString());
        if (pending.PAL.verifyExclusiveMutation(origin)) { plan->Rejection = "KnownBorrowConflict"; break; }
      }
    }
    plan->SemaValidated = plan->Rejection.empty();
    if (!plan->SemaValidated) {
      error(const_cast<CastExpr *>(plan->Site), DiagID::ERR_SEMA_UNSAFE_RAW_CONSTRUCTION, plan->Rejection);
      valid = false;
    }
  }
  return valid;
}

std::vector<const CastExpr *> Sema::getUnsafeRawConstructionSites() const {
  std::vector<const CastExpr *> result;
  for (const auto &pending : m_UnsafeRawConstructions)
    if (auto plan = pending.Plan.lock()) result.push_back(plan->Site);
  return result;
}
}
