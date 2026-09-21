#include "toka/Sema.h"
#include <algorithm>

namespace toka {
namespace {
bool sameResultType(const std::shared_ptr<Type> &a, const std::shared_ptr<Type> &b) {
  return a && b && a->withAttributes(false, a->IsNullable, a->IsBlocked)->equals(
      *b->withAttributes(false, b->IsNullable, b->IsBlocked));
}
bool mergeResult(TaskResultFact &a, const TaskResultFact &b) {
  using Kind = TaskResultFact::Kind;
  if (!sameResultType(a.ValueType, b.ValueType) || a.Scope != b.Scope) return false;
  if (a.Origin != b.Origin) {
    if ((a.Origin == Kind::Static && b.Origin == Kind::Borrowed) ||
        (a.Origin == Kind::Borrowed && b.Origin == Kind::Static)) a.Origin = Kind::Borrowed;
    else return false;
  }
  a.Referents.insert(a.Referents.end(), b.Referents.begin(), b.Referents.end());
  a.AddressedStorage.insert(a.AddressedStorage.end(), b.AddressedStorage.begin(), b.AddressedStorage.end());
  a.StaticStorage.insert(a.StaticStorage.end(), b.StaticStorage.begin(), b.StaticStorage.end());
  a.TaskParameters.insert(b.TaskParameters.begin(), b.TaskParameters.end());
  a.IndependentParameters.insert(b.IndependentParameters.begin(), b.IndependentParameters.end());
  for (const auto &[field, roots] : b.FieldReferents)
    a.FieldReferents[field].insert(a.FieldReferents[field].end(), roots.begin(), roots.end());
  for (const auto &[field, origins] : b.FieldStaticStorage)
    a.FieldStaticStorage[field].insert(a.FieldStaticStorage[field].end(), origins.begin(), origins.end());
  return true;
}
}

std::shared_ptr<Type> Sema::taskResultType(const std::shared_ptr<Type> &type) {
  auto shape = std::dynamic_pointer_cast<ShapeType>(type);
  auto *instance = shape ? shape->Decl : nullptr;
  auto *definition = instance && instance->InstantiationTemplate ? instance->InstantiationTemplate : instance;
  if (!definition || !definition->NominalId) return {};
  auto owner = DeclarationLexicalScopes.find(definition);
  auto *module = owner == DeclarationLexicalScopes.end() ? nullptr : owner->second;
  if (!module || !module->IsTrustedSystemModule || !module->ShadowCoordinateKnown ||
      module->ShadowLogicalModulePath != "core/task" || module->ShadowCrateId.empty()) return {};
  auto declared = module->Shapes.find("TaskHandle");
  if (declared == module->Shapes.end() || declared->second != definition ||
      *definition->NominalId != NominalShapeId::fromResolverCoordinate(
          module->ShadowCrateId, "core/task", "TaskHandle", 1)) return {};
  const auto &arguments = shape->GenericArgs.empty() ? instance->InstantiationArgs : shape->GenericArgs;
  return arguments.size() == 1 && arguments[0] && !arguments[0]->isUnknown() ? arguments[0] : nullptr;
}

bool Sema::closedTaskResultType(std::shared_ptr<Type> type) {
  std::set<const ShapeDecl *> active;
  std::function<bool(std::shared_ptr<Type>)> visit = [&](std::shared_ptr<Type> value) {
    value = resolveExplicitCedeStage0TypeReadOnly(value);
    if (!value || value->isUnknown() || value->isUninit() || value->isRawPointer() ||
        value->isReference() || value->isAddrType() || value->isOAddrType() ||
        value->isFunction() || value->isDynFn() || value->isSlice()) return false;
    if (value->isUnit() || value->isBoolean() || value->isInteger() || value->isFloatingPoint()) return true;
    if (hasCanonicalOwningStringStorage(value)) return true;
    if (value->isUniquePtr() || value->isSharedPtr()) return visit(value->getPointeeType());
    if (value->isArray()) return visit(value->getArrayElementType());
    auto shape = std::dynamic_pointer_cast<ShapeType>(value);
    if (!shape || !shape->Decl || !active.insert(shape->Decl).second) return false;
    std::map<std::string, std::shared_ptr<Type>> substitutions;
    if (shape->GenericArgs.size() != shape->Decl->GenericParams.size()) {
      if (!shape->Decl->GenericParams.empty()) return false;
    } else for (size_t i = 0; i < shape->GenericArgs.size(); ++i)
      substitutions[shape->Decl->GenericParams[i].Name] = shape->GenericArgs[i];
    auto member = [&](const ShapeMember &field) {
      auto physical = getPhysicalType(field);
      if (physical && !substitutions.empty()) physical = physical->substitute(substitutions);
      return visit(physical);
    };
    bool complete = true;
    for (const auto &field : shape->Decl->Members) {
      if (field.IsUnitVariant) continue;
      if (shape->Decl->Kind == ShapeKind::Enum && !field.SubMembers.empty()) {
        for (const auto &payload : field.SubMembers) complete &= member(payload);
      } else complete &= member(field);
    }
    active.erase(shape->Decl);
    return complete;
  };
  return visit(std::move(type));
}

std::shared_ptr<const TaskResultFact> Sema::taskResultFact(Expr *source) {
  if (!source || !source->ResolvedType) return {};
  if (auto *variable = dynamic_cast<VariableExpr *>(source)) {
    auto path = makeAccessPath(variable);
    const auto id = variable->ResolvedBindingID ? variable->ResolvedBindingID : path.RootID;
    auto found = m_TaskResults.find(id);
    if (found == m_TaskResults.end() || found->second->Scope != CurrentFunction ||
        !sameResultType(source->ResolvedType, found->second->CarrierType)) return {};
    return found->second;
  }
  auto proof = source->TaskResult;
  if (!proof) {
    if (auto *cede = dynamic_cast<CedeExpr *>(source); cede && cede->IsImplicitCallTransfer)
      proof = cede->Value->TaskResult;
  }
  return proof && proof->Scope == CurrentFunction &&
      sameResultType(proof->CarrierType, source->ResolvedType) ? proof : nullptr;
}

void Sema::seedTaskResultParameter(FunctionDecl *function, size_t index, SymbolInfo &binding) {
  auto result = taskResultType(binding.TypeObj);
  if (!result || !function || index >= function->Args.size()) return;
  const auto name = Type::stripMorphology(function->Args[index].Name);
  // The explicit ceiling permits using the projection; it supplies no actual
  // source. Actual task witnesses must still discharge this parameter at calls.
  if (std::none_of(function->LifeDependencies.begin(), function->LifeDependencies.end(),
      [&](const std::string &dependency) { return Type::stripMorphology(dependency) == name; })) return;
  auto proof = std::make_shared<TaskResultFact>();
  proof->Origin = TaskResultFact::Kind::Projection;
  proof->Scope = function; proof->ValueType = result; proof->CarrierType = binding.TypeObj;
  proof->TaskCarrier = true; proof->TaskParameters.insert(index);
  AccessPath symbolic;
  symbolic.RootID = binding.SymbolID; symbolic.RootName = name; symbolic.RootLoc = binding.DeclLoc;
  // This private projection is minted only here. No source field spelling is
  // recognized as a task witness, and it is never used to address a descriptor.
  symbolic.Projections.push_back(AccessProjection::field("__task_result", binding.DeclLoc));
  if (closedTaskResultType(result)) proof->Origin = TaskResultFact::Kind::Independent;
  else proof->Referents.push_back(std::move(symbolic));
  m_TaskResults[binding.SymbolID] = std::move(proof);
}

void Sema::bindTaskResult(const AccessPath &destination, Expr *source) {
  auto proof = taskResultFact(source);
  if (!destination.RootID) return;
  m_TaskResults.erase(destination.RootID);
  if (destination.Projections.empty() && proof) m_TaskResults[destination.RootID] = std::move(proof);
}

void Sema::recordTaskResultExpression(Expr *source, bool valid) {
  if (!source) return;
  source->TaskResult.reset();
  if (!m_EnableStage1ExplicitCallerCede || m_IsPrecomputingCaptures || !valid ||
      !source->ResolvedType || !CurrentFunction) return;
  auto publish = [&](std::shared_ptr<const TaskResultFact> proof) {
    if (!proof->TaskCarrier) {
      if (!m_TaskResultFrames.empty() && m_TaskResultFrames.back().Function == CurrentFunction &&
          m_TaskResultFrames.back().ClosureDepth == m_CallableReturnClosureDepth)
        m_TaskResultFrames.back().RequiredTasks.insert(proof->TaskParameters.begin(), proof->TaskParameters.end());
      // This is the checked result relation, not the task's execution/capture
      // dependency set. PAL loans and task cleanup state are not changed.
      m_LastLifeDependencies.clear();
      m_LastFieldDependencies.clear();
      m_LastBorrowSource.clear();
      for (const auto &root : proof->Referents) m_LastLifeDependencies.insert(root.toLegacyString());
      for (const auto &[field, roots] : proof->FieldReferents)
        for (const auto &root : roots) m_LastFieldDependencies[field].insert(root.toLegacyString());
    }
    source->TaskResult = std::move(proof);
  };
  std::shared_ptr<const TaskResultFact> input;
  if (auto *variable = dynamic_cast<VariableExpr *>(source)) input = taskResultFact(variable);
  else if (auto *start = dynamic_cast<StartExpr *>(source)) input = start->Expression->TaskResult;
  else if (auto *member = dynamic_cast<MemberExpr *>(source); member && member->IsTaskStart)
    input = member->Object->TaskResult;
  else if (auto *wait = dynamic_cast<WaitExpr *>(source)) {
    input = taskResultFact(wait->Expression.get());
    if (!input || !input->TaskCarrier || !sameResultType(input->ValueType, source->ResolvedType)) {
      if (CurrentFunction->GenericParams.empty())
        error(source, DiagID::ERR_SEMA_BINDING_TRANSFER_REJECTED, "TaskResultOriginsUnproven");
      return;
    }
  } else if (auto *cede = dynamic_cast<CedeExpr *>(source)) input = cede->Value->TaskResult;
  else if (auto *unsafe = dynamic_cast<UnsafeExpr *>(source)) input = taskResultFact(unsafe->Expression.get());
  else if (auto *cast = dynamic_cast<CastExpr *>(source);
           cast && cast->Expression->ResolvedType &&
           sameResultType(cast->Expression->ResolvedType, source->ResolvedType))
    input = taskResultFact(cast->Expression.get());
  if (input) {
    auto proof = std::make_shared<TaskResultFact>(*input);
    proof->CarrierType = source->ResolvedType;
    if (dynamic_cast<WaitExpr *>(source)) proof->TaskCarrier = false;
    publish(std::move(proof));
    return;
  }
  auto *call = dynamic_cast<CallExpr *>(source);
  auto *method = dynamic_cast<MethodCallExpr *>(source);
  auto *function = call ? call->ResolvedFn : method ? method->ResolvedFn : nullptr;
  if (!function || !function->Body || function->IsClosureInvoke) return;
  const bool async = function->Effect == EffectKind::Async;
  bool projectsTask = false;
  for (const auto &argument : function->Args) projectsTask |= taskResultType(argument.ResolvedType) != nullptr;
  if (!async && !projectsTask && !taskResultType(source->ResolvedType)) return;
  prepareCallableFactory(function);
  auto found = m_TaskResultSummaries.find(function);
  if (found == m_TaskResultSummaries.end() || !found->second.Valid) return;
  if (function->TemplateOrigin) {
    auto cache = InstantiationCache.find(function->Name);
    if (cache == InstantiationCache.end() || !cache->second || cache->second->Instance != function ||
        cache->second->Validation != GenericSpecializationValidationState::Valid) return;
  }
  const auto &summary = found->second;
  const bool carrier = async || summary.ProducesTask;
  auto result = carrier ? taskResultType(source->ResolvedType) : source->ResolvedType;
  if (!sameResultType(result, summary.ResultType)) return;
  auto argument = [&](size_t index) -> Expr * {
    return call ? (index < call->Args.size() ? call->Args[index].get() : nullptr)
        : index == 0 ? method->Object.get()
        : index - 1 < method->Args.size() ? method->Args[index-1].get() : nullptr;
  };
  auto requirements = summary.RequiredTasks;
  requirements.insert(summary.TaskParameters.begin(), summary.TaskParameters.end());
  for (auto index : requirements) {
    auto actual = taskResultFact(argument(index));
    const auto expected = index < function->Args.size() ? taskResultType(function->Args[index].ResolvedType) : nullptr;
    if (!actual || !actual->TaskCarrier || !sameResultType(actual->ValueType, expected)) {
      error(source, DiagID::ERR_SEMA_BINDING_TRANSFER_REJECTED, "TaskResultOriginsUnproven");
      return;
    }
    if (!m_TaskResultFrames.empty() && m_TaskResultFrames.back().Function == CurrentFunction &&
        m_TaskResultFrames.back().ClosureDepth == m_CallableReturnClosureDepth)
      m_TaskResultFrames.back().RequiredTasks.insert(actual->TaskParameters.begin(), actual->TaskParameters.end());
  }
  auto proof = std::make_shared<TaskResultFact>();
  proof->Scope = CurrentFunction; proof->ValueType = result;
  proof->CarrierType = source->ResolvedType; proof->TaskCarrier = carrier;
  proof->Origin = summary.Origin;
  proof->StaticStorage = summary.StaticStorage;
  proof->FieldStaticStorage = summary.FieldStaticStorage;
  if (!summary.TaskParameters.empty()) {
    bool first = true;
    for (auto index : summary.TaskParameters) {
      auto actual = taskResultFact(argument(index));
      if (!actual || !actual->TaskCarrier || !sameResultType(actual->ValueType, result) ||
          (summary.Origin == TaskResultFact::Kind::Independent &&
           actual->Origin != TaskResultFact::Kind::Independent)) {
        error(source, DiagID::ERR_SEMA_BINDING_TRANSFER_REJECTED, "TaskResultOriginsUnproven");
        return;
      }
      if (first) { *proof = *actual; first = false; }
      else if (!mergeResult(*proof, *actual)) return;
    }
    if (first) return;
    proof->CarrierType = source->ResolvedType; proof->TaskCarrier = carrier;
  } else {
    for (auto index : summary.IndependentParameters) {
      auto actual = resultIndependence(argument(index));
      if (!actual) return;
      proof->IndependentParameters.insert(actual->RequiredArguments.begin(), actual->RequiredArguments.end());
    }
    for (const auto &input : summary.Inputs) {
      auto *value = argument(input.Parameter);
      std::unique_ptr<Expr> selected;
      bool mappedField = false;
      if (value && value->ResolvedType && !input.Projections.empty() &&
          hasBorrowedValueFields(value->ResolvedType)) {
        auto *variable = dynamic_cast<VariableExpr *>(value);
        if (!variable || !variable->ResolvedBindingID) return;
        auto root = std::make_unique<VariableExpr>(variable->Name);
        root->ResolvedBindingID = variable->ResolvedBindingID;
        root->ResolvedType = variable->ResolvedType; root->Loc = variable->Loc;
        selected = std::move(root);
        for (const auto &projection : input.Projections) {
          if (projection.Kind != AccessProjectionKind::Field) return;
          auto field = std::make_unique<MemberExpr>(std::move(selected), projection.Name);
          field->Loc = value->Loc;
          field->ResolvedType = queryExplicitCedeStage0NonCallType(field.get(), nullptr);
          if (!field->ResolvedType || field->ResolvedType->isUnknown()) return;
          selected = std::move(field);
        }
        value = selected.get(); mappedField = true;
      }
      std::vector<AccessPath> roots, storage;
      std::vector<SourceLocation> statics;
      if (!value || !collectActualReturnReferents(value, roots, &statics, &storage)) return;
      // A field projection of an owning input may select its internal storage.
      // Do not append it to an already mapped borrowed/structural referent.
      if (!mappedField && !input.Projections.empty() && (!value->ResolvedType ||
          hasBorrowedValueFields(value->ResolvedType) || !statics.empty())) return;
      for (auto &root : roots) {
        if (!mappedField) root.Projections.insert(root.Projections.end(), input.Projections.begin(), input.Projections.end());
        proof->Referents.push_back(root);
        if (input.Storage) proof->AddressedStorage.push_back(root);
        if (!input.Field.empty()) proof->FieldReferents[input.Field].push_back(root);
      }
      proof->StaticStorage.insert(proof->StaticStorage.end(), statics.begin(), statics.end());
      if (!input.Field.empty())
        proof->FieldStaticStorage[input.Field].insert(proof->FieldStaticStorage[input.Field].end(), statics.begin(), statics.end());
      if (roots.empty() && statics.empty()) return;
    }
    if (proof->Origin == TaskResultFact::Kind::Borrowed && proof->Referents.empty()) {
      if (proof->StaticStorage.empty()) return;
      proof->Origin = TaskResultFact::Kind::Static;
    }
  }
  publish(std::move(proof));
}

void Sema::recordTaskResultReturn(ReturnStmt *statement, bool valid) {
  if (m_TaskResultFrames.empty() || m_TaskResultFrames.back().Function != CurrentFunction ||
      m_TaskResultFrames.back().ClosureDepth != m_CallableReturnClosureDepth) return;
  auto &frame = m_TaskResultFrames.back();
  auto *source = statement->ReturnValue.get();
  const bool firstReturn = !frame.SawReturn;
  frame.SawReturn = true;
  if (!valid || !source || !source->ResolvedType) { frame.Complete = false; return; }
  auto proof = taskResultFact(source);
  if (frame.ProducesTask && (!proof || !proof->TaskCarrier)) { frame.Complete = false; return; }
  if (!proof || (proof->TaskCarrier && !frame.ProducesTask)) {
    auto created = std::make_shared<TaskResultFact>();
    created->Scope = CurrentFunction; created->ValueType = frame.ResultType;
    created->CarrierType = source->ResolvedType;
    if (closedTaskResultType(frame.ResultType)) {
      created->Origin = TaskResultFact::Kind::Independent;
    } else if (auto independent = resultIndependence(source)) {
      created->Origin = TaskResultFact::Kind::Independent;
      created->IndependentParameters = independent->RequiredArguments;
    } else {
      std::vector<AccessPath> storage;
      std::map<std::string, ActualReturnFieldOrigins> fields;
      if (!collectActualReturnReferents(source, created->Referents, &created->StaticStorage, &storage, nullptr, &fields) ||
          (!storage.empty() && !frame.ResultType->isReference()) ||
          (created->Referents.empty() && created->StaticStorage.empty())) {
        frame.Complete = false; return;
      }
      created->Origin = created->Referents.empty() ? TaskResultFact::Kind::Static : TaskResultFact::Kind::Borrowed;
      created->AddressedStorage = std::move(storage);
      if (frame.ResultType->isReference())
        created->AddressedStorage = created->Referents;
      for (const auto &[field, origins] : fields) {
        created->FieldReferents[field] = origins.Referents;
        created->FieldStaticStorage[field] = origins.StaticStorage;
      }
    }
    proof = std::move(created);
  }
  if (!sameResultType(proof->ValueType, frame.ResultType)) { frame.Complete = false; return; }
  if (firstReturn)
    frame.Origin = proof->Origin;
  else if (frame.Origin != proof->Origin) { frame.Complete = false; return; }
  frame.TaskParameters.insert(proof->TaskParameters.begin(), proof->TaskParameters.end());
  frame.IndependentParameters.insert(proof->IndependentParameters.begin(), proof->IndependentParameters.end());
  frame.StaticStorage.insert(frame.StaticStorage.end(), proof->StaticStorage.begin(), proof->StaticStorage.end());
  for (const auto &[field, origins] : proof->FieldStaticStorage)
    frame.FieldStaticStorage[field].insert(frame.FieldStaticStorage[field].end(), origins.begin(), origins.end());
  if (proof->Origin == TaskResultFact::Kind::Projection) return;
  auto append = [&](const AccessPath &root, const std::string &field) {
    SymbolInfo *binding = nullptr;
    if (!root.RootID || !CurrentScope->findSymbolByID(root.RootID, binding) ||
        !binding || !binding->IsFunctionParameter) return false;
    for (size_t i = 0; i < CurrentFunction->Args.size(); ++i) {
      const auto &arg = CurrentFunction->Args[i];
      if (arg.Loc != binding->DeclLoc || Type::stripMorphology(arg.Name) != root.RootName) continue;
      // A result borrowed from a transferred input belongs to the task frame;
      // projecting it cannot extend that frame's lifetime.
      const bool storage = std::find(proof->AddressedStorage.begin(), proof->AddressedStorage.end(), root) !=
                           proof->AddressedStorage.end();
      const auto ownership = queryExplicitCedeStage0OwnershipReadOnly(arg.ResolvedType);
      if ((arg.IsCeded && (!ownership || *ownership != ValueOwnership::BorrowedView)) ||
          (storage && (!arg.ResolvedType || !arg.ResolvedType->isReference()))) {
        error(statement, DiagID::ERR_SEMA_RETURN_PLAN_INCOMPLETE, "TaskFrameResultBorrow");
        return false;
      }
      frame.Inputs.push_back({i, root.Projections, field, storage});
      return true;
    }
    return false;
  };
  for (const auto &root : proof->Referents) frame.Complete &= append(root, "");
  for (const auto &[field, roots] : proof->FieldReferents)
    for (const auto &root : roots) frame.Complete &= append(root, field);
}
} // namespace toka
