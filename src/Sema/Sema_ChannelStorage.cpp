#include "toka/Sema.h"

namespace toka {
namespace {
Expr *channelSurface(Expr *value) {
  while (value) {
    if (auto *cast = dynamic_cast<CastExpr *>(value)) {
      if (cast->Kind == CastKind::Conversion) break;
      value = cast->Expression.get();
    } else if (auto *cede = dynamic_cast<CedeExpr *>(value)) value = cede->Value.get();
    else if (auto *post = dynamic_cast<PostfixExpr *>(value)) value = post->LHS.get();
    else break;
  }
  return value;
}
ShapeDecl *channelShape(const std::shared_ptr<Type> &type) {
  auto shape = std::dynamic_pointer_cast<ShapeType>(type);
  return shape ? shape->Decl : nullptr;
}
std::map<std::string, Expr *> channelFields(Expr *value, const ShapeDecl *expected) {
  value = channelSurface(value);
  std::map<std::string, Expr *> fields;
  if (!value || channelShape(value->ResolvedType) != expected) return fields;
  if (auto *init = dynamic_cast<InitStructExpr *>(value)) {
    if (!init->PreExpansionMemberNames || init->PreExpansionMemberNames->size() != expected->Members.size()) return {};
    for (auto &field : init->Members)
      if (!fields.emplace(Type::stripMorphology(field.first), field.second.get()).second) return {};
  } else if (auto *call = dynamic_cast<CallExpr *>(value); call && call->ResolvedShape == expected) {
    for (auto &arg : call->Args) {
      auto *named = dynamic_cast<BinaryExpr *>(arg.get());
      auto *name = named ? dynamic_cast<VariableExpr *>(named->LHS.get()) : nullptr;
      if (!named || named->Op != "=" || !name ||
          !fields.emplace(Type::stripMorphology(name->Name), named->RHS.get()).second) return {};
    }
  }
  if (fields.size() != expected->Members.size()) return {};
  return fields;
}
bool channelValueType(const std::shared_ptr<Type> &left, const std::shared_ptr<Type> &right) {
  return left && right && left->isShape() && right->isShape() &&
      !left->IsNullable && !right->IsNullable &&
      left->withAttributes(false, false, left->IsBlocked)->equals(
          *right->withAttributes(false, false, right->IsBlocked));
}
}

// Private source-instance contract. A nominal endpoint type is never enough:
// creation must be the checked SDK factory and carry the real child plans.
NativeSyncOwnerCandidatePtr Sema::collectChannelStorageRecipe(Expr *source) {
  if (!source || !source->ResolvedType || !source->ResolvedType->isShape()) return {};
  auto derived = [&](NativeSyncOwnerCandidatePtr parent, const FunctionDecl *provider) -> NativeSyncOwnerCandidatePtr {
    if (!parent || !parent->Channel || m_InvalidNativeSyncOwnerRecipes.count(parent)) return {};
    const auto *decl = channelShape(source->ResolvedType);
    auto p = parent->Channel;
    if (decl != p->Pair && decl != p->Sender && decl != p->Receiver) return {};
    auto result = std::shared_ptr<NativeSyncOwnerCandidate>(new NativeSyncOwnerCandidate(*parent));
    result->Parent = parent;
    result->OwnerEdge = source;
    result->Provider = provider ? provider : parent->Provider;
    result->ValueType = source->ResolvedType;
    return result;
  };
  if (auto *member = dynamic_cast<MemberExpr *>(source)) {
    auto parent = member->Object->NativeSyncOwnerRecipe;
    if (!parent || !parent->Channel || channelShape(member->Object->ResolvedType) != parent->Channel->Pair)
      return {};
    const auto *pair = parent->Channel->Pair;
    if (member->Index < 0 || static_cast<size_t>(member->Index) >= pair->Members.size() ||
        !channelValueType(getPhysicalType(pair->Members[member->Index]), source->ResolvedType)) return {};
    return derived(parent, nullptr);
  }
  if (auto *method = dynamic_cast<MethodCallExpr *>(source)) {
    auto parent = method->Object->NativeSyncOwnerRecipe;
    if (!parent || !parent->Channel || method->Method != "clone" || !method->Args.empty() ||
        channelShape(source->ResolvedType) != parent->Channel->Sender ||
        !nativeSyncDefinitionReady(method->ResolvedFn)) return {};
    auto *module = getLexicalModule(method->ResolvedFn->Loc);
    auto *origin = getLexicalModule(parent->Channel->Factory->Loc);
    if (module != origin) return {};
    return derived(parent, method->ResolvedFn);
  }
  auto *call = dynamic_cast<CallExpr *>(source);
  if (!call || !call->ResolvedFn || !nativeSyncDefinitionReady(call->ResolvedFn)) return {};
  auto *function = call->ResolvedFn;
  auto *module = getLexicalModule(function->Loc);
  if (!module || !module->SourceModule || module->SourceModule->IsInterface ||
      !module->IsTrustedSystemModule || !module->ShadowCoordinateKnown ||
      module->ShadowLogicalModulePath != "std/channel") return {};
  auto registered = module->Functions.find("__channel_create");
  if (registered == module->Functions.end() || !registered->second ||
      function->TemplateOrigin != registered->second) return {};
  auto pair = std::dynamic_pointer_cast<ShapeType>(source->ResolvedType);
  auto nominal = module->Shapes.find("ChannelPair");
  if (!pair || !pair->Decl || nominal == module->Shapes.end() ||
      pair->Decl->InstantiationTemplate != nominal->second ||
      pair->Decl->InstantiationArgs.size() != 1 || call->Args.size() != 1 ||
      !call->Args[0]->ResolvedType || call->Args[0]->ResolvedType->toString() != "i32") return {};
  auto element = pair->Decl->InstantiationArgs[0];
  if (!checkNativeSyncClosedPayload(element).closed() || !element->isSend(this)) return {};
  auto p = std::shared_ptr<ChannelStorageProof>(new ChannelStorageProof);
  p->FactorySite = call; p->Factory = function; p->Pair = pair->Decl; p->ElementType = element;
  if (p->Pair->Members.size() != 2) return {};
  p->Sender = channelShape(getPhysicalType(p->Pair->Members[0]));
  p->Receiver = channelShape(getPhysicalType(p->Pair->Members[1]));
  auto endpoint = [&](const ShapeDecl *decl, const char *name) {
    auto expected = module->Shapes.find(name);
    if (!decl || expected == module->Shapes.end() || decl->InstantiationTemplate != expected->second ||
        decl->InstantiationArgs.size() != 1 || !decl->InstantiationArgs[0]->equals(*element) ||
        decl->Members.size() != 1 || !decl->HasExplicitDrop) return false;
    auto policy = Slice2PolicyMap.find(expected->second);
    if (policy == Slice2PolicyMap.end() || policy->second.Owner != module || !policy->second.Entries.empty()) return false;
    auto storage = getPhysicalType(decl->Members[0]);
    if (!storage || !storage->isSharedPtr()) return false;
    auto core = channelShape(storage->getPointeeType());
    if (p->Core && core != p->Core) return false;
    p->Core = core;
    return true;
  };
  if (!endpoint(p->Sender, "Sender") || !endpoint(p->Receiver, "Receiver") || !p->Core ||
      p->Core->Members.size() != 3 || p->Core->HasExplicitDrop) return {};
  auto coreNominal = module->Shapes.find("AsyncChannel");
  if (coreNominal == module->Shapes.end() || p->Core->InstantiationTemplate != coreNominal->second) return {};
  // The private factory has a single allocation followed by the pair result.
  // Default/spread/caller-supplied cores cannot enter this contract.
  if (!function->Body || function->Body->Statements.size() != 2) return {};
  auto *binding = dynamic_cast<VariableDecl *>(function->Body->Statements[0].get());
  auto *ret = dynamic_cast<ReturnStmt *>(function->Body->Statements[1].get());
  auto *allocation = binding ? dynamic_cast<NewExpr *>(channelSurface(binding->Init.get())) : nullptr;
  auto *init = allocation ? dynamic_cast<InitStructExpr *>(allocation->Initializer.get()) : nullptr;
  if (!binding || !binding->ResolvedType || !binding->ResolvedType->isSharedPtr() ||
      channelShape(binding->ResolvedType->getPointeeType()) != p->Core || !ret || !ret->ReturnValue ||
      !channelValueType(ret->ReturnValue->ResolvedType, source->ResolvedType) || !init ||
      !init->PreExpansionMemberNames || init->Members.size() != 3 ||
      init->PreExpansionMemberNames->size() != 3 || allocation->ArraySize) return {};
  p->CoreAllocation = allocation;
  for (size_t i = 0; i < p->Core->Members.size(); ++i) {
    const auto &field = p->Core->Members[i];
    auto supplied = std::find_if(init->Members.begin(), init->Members.end(),
        [&](const auto &value) { return value.first == field.Name; });
    if (supplied == init->Members.end()) return {};
    auto *value = channelSurface(supplied->second.get());
    auto recipe = value ? value->NativeSyncOwnerRecipe : nullptr;
    if (!recipe || !recipe->Factory ||
        !nativeSyncDefinitionReady(recipe->Factory->Declaration) ||
        !nativeSyncDefinitionReady(recipe->Factory->OwnerDefinition) ||
        !recipe->Factory->OwnerType || !getPhysicalType(field)->withAttributes(false, false)->equals(
            *recipe->Factory->OwnerType->withAttributes(false, false)) ||
        recipe->Factory->Kind != (i == 0 ? NativeSyncFactoryKind::Mutex : NativeSyncFactoryKind::CondVar)) return {};
    auto queue = channelShape(recipe->Factory->ElementType);
    if (!queue || (p->Queue && p->Queue != queue)) return {};
    p->Queue = queue;
    p->NativeChildren.push_back(recipe->Factory);
  }
  auto queueNominal = module->Shapes.find("WaitQueue");
  if (queueNominal == module->Shapes.end() || !p->Queue ||
      p->Queue->InstantiationTemplate != queueNominal->second ||
      p->Queue->InstantiationArgs.size() != 1 || !p->Queue->InstantiationArgs[0]->equals(*element)) return {};
  auto endpoints = channelFields(ret->ReturnValue.get(), p->Pair);
  if (!binding->ResolvedBindingID || endpoints.size() != 2) return {};
  for (const auto &[name, endpointDecl] : {std::pair{"tx", p->Sender}, std::pair{"rx", p->Receiver}}) {
    auto found = endpoints.find(name);
    if (found == endpoints.end()) return {};
    auto fields = channelFields(found->second, endpointDecl);
    auto channel = fields.find("channel");
    if (fields.size() != 1 || channel == fields.end()) return {};
    auto *selected = channelSurface(channel->second);
    auto *handle = dynamic_cast<UnaryExpr *>(selected);
    auto *variable = handle ? dynamic_cast<VariableExpr *>(channelSurface(handle->RHS.get()))
                            : dynamic_cast<VariableExpr *>(selected);
    if (!variable || variable->ResolvedBindingID != binding->ResolvedBindingID ||
        !selected->ResolvedType || !selected->ResolvedType->isSharedPtr() ||
        channelShape(selected->ResolvedType->getPointeeType()) != p->Core) return {};
  }
  // Bind the queue's initial state to the actual, checked empty constructors.
  // This finite SDK schema does not prove arbitrary raw-buffer histories.
  auto factoryFields = [&](Expr *value, const ShapeDecl *shape, const char *logical) {
    value = channelSurface(value);
    auto *factoryCall = dynamic_cast<CallExpr *>(value);
    auto *fn = factoryCall ? factoryCall->ResolvedFn : nullptr;
    auto *owner = fn ? getLexicalModule(fn->Loc) : nullptr;
    if (!fn || !nativeSyncDefinitionReady(fn) || !owner || !owner->IsTrustedSystemModule ||
        !owner->SourceModule || owner->SourceModule->IsInterface ||
        !owner->ShadowCoordinateKnown || owner->ShadowLogicalModulePath != logical ||
        !fn->Body || fn->Body->Statements.size() != 1) return std::map<std::string, Expr *>{};
    auto *returned = dynamic_cast<ReturnStmt *>(fn->Body->Statements.front().get());
    if (!returned) return std::map<std::string, Expr *>{};
    p->Operations.push_back(fn);
    return channelFields(returned->ReturnValue.get(), shape);
  };
  auto *stateCall = dynamic_cast<CallExpr *>(channelSurface(init->Members[0].second.get()));
  if (!stateCall || stateCall->Args.size() != 1) return {};
  auto queueFields = factoryFields(stateCall->Args[0].get(), p->Queue, "std/channel");
  auto number = [&](Expr *value, int64_t expected) {
    auto *literal = dynamic_cast<NumberExpr *>(channelSurface(value));
    return literal && literal->Value == expected;
  };
  auto closed = queueFields.find("closed");
  auto *boolean = closed == queueFields.end() ? nullptr : dynamic_cast<BoolExpr *>(channelSurface(closed->second));
  if (queueFields.size() != 5 || !boolean || boolean->Value ||
      !number(queueFields["senders"], 1) || !number(queueFields["receivers"], 1)) return {};
  auto *ringDecl = channelShape(queueFields["queue"] ? queueFields["queue"]->ResolvedType : nullptr);
  if (!ringDecl) return {};
  auto ringFields = factoryFields(queueFields["queue"], ringDecl, "std/ring");
  if (ringFields.size() != 2) return {};
  for (const char *side : {"front", "back"}) {
    auto found = ringFields.find(side);
    if (found == ringFields.end()) return {};
    auto *vecDecl = channelShape(found->second->ResolvedType);
    if (!vecDecl || vecDecl->InstantiationArgs.size() != 1 || !vecDecl->InstantiationArgs[0]->equals(*element)) return {};
    auto fields = factoryFields(found->second, vecDecl, "std/vec");
    if (fields.size() != 3 || !number(fields["len"], 0) || !number(fields["cap"], 0) ||
        !dynamic_cast<NullExpr *>(channelSurface(fields["buf"]))) return {};
  }
  // Endpoint operations are the checked SDK declarations. Their private field
  // access cannot be acquired by user extensions or a same-named shape.
  for (const auto *endpointDecl : {p->Sender, p->Receiver}) {
    auto methods = MethodDecls.find(endpointDecl->Name);
    if (methods == MethodDecls.end()) return {};
    const std::vector<const char *> names = endpointDecl == p->Sender
        ? std::vector<const char *>{"clone", "send"}
        : std::vector<const char *>{"recv", "try_recv"};
    for (auto name : names) {
      auto operation = methods->second.find(name);
      if (operation == methods->second.end() || !operation->second ||
          getLexicalModule(operation->second->Loc) != module) return {};
      if (!nativeSyncDefinitionReady(operation->second)) (void)prepareCallableFactory(operation->second);
      if (!nativeSyncDefinitionReady(operation->second)) return {};
      p->Operations.push_back(operation->second);
    }
    auto drop = m_NativeSyncDropDeclarations.find(endpointDecl);
    if (drop == m_NativeSyncDropDeclarations.end() || !drop->second ||
        getLexicalModule(drop->second->Loc) != module ||
        drop->second->CodegenName != endpointDecl->MangledDestructorName ||
        !nativeSyncDefinitionReady(drop->second)) return {};
    p->Operations.push_back(drop->second);
  }
  p->Complete = true;
  auto result = std::shared_ptr<NativeSyncOwnerCandidate>(new NativeSyncOwnerCandidate);
  result->Channel = p; result->OwnerEdge = source; result->Provider = function;
  result->ValueType = source->ResolvedType;
  return result;
}

NativeSyncOwnerWitnessPtr Sema::qualifyChannelStorage(const NativeSyncOwnerCandidatePtr &recipe,
                                                     const std::shared_ptr<Type> &actualType) {
  if (!recipe || !recipe->Channel || !recipe->Channel->Complete ||
      !channelValueType(recipe->ValueType, actualType) ||
      !nativeSyncDefinitionReady(recipe->Channel->Factory)) return {};
  auto p = recipe->Channel;
  for (auto operation : p->Operations) if (!nativeSyncDefinitionReady(operation)) return {};
  // Native publication is sealed by its existing module finalizer. Require
  // completed semantic definitions here; CodeGen still requires Validated.
  for (auto child : p->NativeChildren)
    if (!child || !nativeSyncDefinitionReady(child->Declaration) ||
        !nativeSyncDefinitionReady(child->OwnerDefinition)) return {};
  auto witness = std::shared_ptr<NativeSyncOwnerWitness>(new NativeSyncOwnerWitness);
  witness->Origin = recipe; witness->Channel = p; witness->ValueType = actualType;
  witness->OwnerType = actualType; witness->ElementType = p->ElementType;
  witness->FactorySite = p->FactorySite; witness->AllocationSite = p->CoreAllocation;
  witness->CompositeOperations = p->Operations;
  return nativeSyncOwnerLive(witness) ? witness : NativeSyncOwnerWitnessPtr{};
}
} // namespace toka
