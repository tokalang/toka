#include "toka/Sema.h"
#include "toka/SourceManager.h"
#include "llvm/Support/SHA256.h"
#include <algorithm>

namespace toka {
namespace {
bool sameByteValue(const std::shared_ptr<Type> &a, const std::shared_ptr<Type> &b) {
  return a && b && a->withAttributes(false, a->IsNullable, a->IsBlocked)->equals(
      *b->withAttributes(false, b->IsNullable, b->IsBlocked));
}
std::string operationName(FunctionDecl *function) {
  auto *declaration = function->TemplateOrigin ? function->TemplateOrigin : function;
  const auto separator = declaration->Name.rfind("::");
  return separator == std::string::npos ? declaration->Name : declaration->Name.substr(separator + 2);
}
}

std::shared_ptr<const ByteBufferFact> joinByteBufferFacts(
    const std::shared_ptr<const ByteBufferFact> &left,
    const std::shared_ptr<const ByteBufferFact> &right) {
  if (!left || !right || left->Scope != right->Scope || !sameByteValue(left->ValueType, right->ValueType)) return {};
  if (left == right) return left;
  auto result = std::make_shared<ByteBufferFact>(*left);
  result->RequiredArguments.insert(right->RequiredArguments.begin(), right->RequiredArguments.end());
  result->StorageRoots.insert(right->StorageRoots.begin(), right->StorageRoots.end());
  for (auto it = result->Fields.begin(); it != result->Fields.end();) {
    auto other = right->Fields.find(it->first);
    auto field = other == right->Fields.end() ? nullptr : joinByteBufferFacts(it->second, other->second);
    if (!field) it = result->Fields.erase(it);
    else { it->second = std::move(field); ++it; }
  }
  return result;
}

bool Sema::byteBufferSchema(SourceLocation location, const std::string &module) {
  // A deliberately exact implementation seal for the finite unsafe operation
  // contract. This is not a hash supplied by the library being authorized.
  // Changes, including otherwise harmless edits, require refreshing the reviewed
  // contract. No filesystem path or user-chosen type name establishes trust.
  static const std::map<std::string, std::string> schemas = {
      {"std/vec", "225d4425a17bf0b052ad6f5b8325f814e74d0f38ca0f743d4d808769f8bdddcb"},
      {"std/bytes", "5eddfd4abf0b66da5ef09d234a56af884f42361b22d739bf6cc47fb09d4853b4"},
      {"std/net", "c99587738ef2cf5058cf14877bb1e069be079e87cbcdd8b7d5251cb3b5fc114a"},
  };
  const auto expected = schemas.find(module);
  if (expected == schemas.end() || !DiagnosticEngine::SrcMgr || !location.isValid()) return false;
  auto *owner = getLexicalModule(location);
  if (!owner || !owner->IsTrustedSystemModule || !owner->ShadowCoordinateKnown ||
      owner->ShadowLogicalModulePath != module || !owner->SourceModule ||
      owner->SourceModule->IsInterface) return false;
  const auto file = DiagnosticEngine::SrcMgr->getFileID(location);
  if (auto cached = m_ByteBufferSchemas.find(file); cached != m_ByteBufferSchemas.end()) return cached->second;
  const auto content = DiagnosticEngine::SrcMgr->getBufferData(location);
  llvm::SHA256 hasher;
  hasher.update(llvm::StringRef(content.data(), content.size()));
  const auto digest = hasher.final();
  std::string hex;
  for (auto byte : digest) {
    hex += "0123456789abcdef"[(static_cast<unsigned char>(byte) >> 4) & 15];
    hex += "0123456789abcdef"[static_cast<unsigned char>(byte) & 15];
  }
  return m_ByteBufferSchemas[file] = hex == expected->second;
}

bool Sema::isByteBufferType(const std::shared_ptr<Type> &type) {
  auto shape = std::dynamic_pointer_cast<ShapeType>(type);
  auto *instance = shape ? shape->Decl : nullptr;
  auto *definition = instance && instance->InstantiationTemplate ? instance->InstantiationTemplate : instance;
  if (!definition || !definition->NominalId || type->IsNullable || type->IsBlocked) return false;
  auto found = DeclarationLexicalScopes.find(definition);
  auto *owner = found == DeclarationLexicalScopes.end() ? nullptr : found->second;
  if (!owner) return false;
  const bool vector = owner->ShadowLogicalModulePath == "std/vec";
  const auto module = vector ? "std/vec" : "std/bytes";
  const auto name = vector ? "Vec" : "Bytes";
  auto declared = owner->Shapes.find(name);
  if (declared == owner->Shapes.end() || declared->second != definition ||
      *definition->NominalId != NominalShapeId::fromResolverCoordinate(owner->ShadowCrateId, module, name, vector ? 1 : 0) ||
      !byteBufferSchema(definition->Loc, module)) return false;
  const auto &arguments = shape->GenericArgs.empty() ? instance->InstantiationArgs : shape->GenericArgs;
  if (vector && (arguments.size() != 1 || !arguments[0] || !arguments[0]->equals(*Type::fromString("u8")))) return false;
  if (!vector && !arguments.empty()) return false;
  if (!instance->HasExplicitDrop || instance->Members.size() != 3) return false;
  const auto storage = getPhysicalType(instance->Members[0]);
  // The source-visible SDK schema resolves core/types::usize to u64. Do not
  // compare physical fields with an unresolved spelling alias.
  const auto sizeType = Type::fromString("u64");
  return instance->Members[0].Name == "buf" && instance->Members[1].Name == "len" && instance->Members[2].Name == "cap" &&
      storage && storage->isRawPointer() && storage->IsNullable && storage->getPointeeType() &&
      storage->getPointeeType()->isSlice() && storage->getPointeeType()->getArrayElementType() &&
      storage->getPointeeType()->getArrayElementType()->equals(*Type::fromString("u8")) &&
      sizeType && sameByteValue(getPhysicalType(instance->Members[1]), sizeType) &&
      sameByteValue(getPhysicalType(instance->Members[2]), sizeType);
}

bool Sema::containsByteBuffer(const std::shared_ptr<Type> &type) {
  std::set<const ShapeDecl *> active;
  std::function<bool(std::shared_ptr<Type>)> visit = [&](std::shared_ptr<Type> value) {
    if (isByteBufferType(value)) return true;
    auto shape = std::dynamic_pointer_cast<ShapeType>(value);
    if (!shape || !shape->Decl || !active.insert(shape->Decl).second) return false;
    bool found = false;
    for (const auto &member : shape->Decl->Members) {
      if (member.IsUnitVariant) continue;
      if (shape->Decl->Kind == ShapeKind::Enum && !member.SubMembers.empty()) {
        for (const auto &field : member.SubMembers) found |= visit(getPhysicalType(field));
      } else found |= visit(getPhysicalType(member));
    }
    active.erase(shape->Decl);
    return found;
  };
  return visit(type);
}

std::shared_ptr<const ByteBufferFact> Sema::byteBufferFact(Expr *source) {
  if (!source || !source->ResolvedType) return {};
  if (auto *variable = dynamic_cast<VariableExpr *>(source)) {
    const auto id = variable->ResolvedBindingID ? variable->ResolvedBindingID : makeAccessPath(variable).RootID;
    const auto found = m_ByteBuffers.find(id);
    if (found != m_ByteBuffers.end() && found->second->Scope == CurrentFunction &&
        sameByteValue(found->second->ValueType, source->ResolvedType)) return found->second;
    // A variable expression's captured receipt is for its already evaluated
    // value only, never for a later query of that binding.
    return {};
  }
  if (auto *cede = dynamic_cast<CedeExpr *>(source); cede && !source->ByteBuffer) {
    auto child = cede->Value->ByteBuffer;
    return child && child->Scope == CurrentFunction && sameByteValue(child->ValueType, source->ResolvedType) ? child : nullptr;
  }
  if (auto *cast = dynamic_cast<CastExpr *>(source);
      cast && !source->ByteBuffer && sameByteValue(source->ResolvedType, cast->Expression->ResolvedType)) {
    // Normal Sema can insert an attribute-only field conversion after the
    // child was checked. Preserve its value receipt, not target permissions.
    auto child = cast->Expression->ByteBuffer;
    if (!child) child = byteBufferFact(cast->Expression.get());
    return child && child->Scope == CurrentFunction && sameByteValue(child->ValueType, source->ResolvedType) ? child : nullptr;
  }
  auto result = source->ByteBuffer;
  if (!result && source->TaskResult && !source->TaskResult->TaskCarrier) result = source->TaskResult->Bytes;
  return result && result->Scope == CurrentFunction && sameByteValue(result->ValueType, source->ResolvedType) ? result : nullptr;
}

void Sema::invalidateByteBuffer(Expr *source) {
  auto path = canonicalizeAccessPath(makeAccessPath(source));
  SymbolInfo *binding = nullptr;
  if (!path.RootID || (CurrentScope->findSymbolByID(path.RootID, binding) && binding && binding->TypeObj &&
      (binding->TypeObj->isRawPointer() || binding->TypeObj->isReference()))) {
    m_ByteBuffers.clear();
    for (auto it = m_TaskResults.begin(); it != m_TaskResults.end();)
      if (it->second->Bytes) it = m_TaskResults.erase(it); else ++it;
  } else {
    m_ByteBuffers.erase(path.RootID);
    auto task = m_TaskResults.find(path.RootID);
    if (task != m_TaskResults.end() && task->second->Bytes) m_TaskResults.erase(task);
  }
}

std::shared_ptr<const ByteBufferFact> Sema::byteBufferArgumentFact(Expr *source, bool consumes) {
  if (!source || !source->ResolvedType) return {};
  auto saved = [&]() {
    auto value = source->ByteBuffer;
    if (!value && source->TaskResult && !source->TaskResult->TaskCarrier) value = source->TaskResult->Bytes;
    return value && value->Scope == CurrentFunction && sameByteValue(value->ValueType, source->ResolvedType) ? value : nullptr;
  };
  if (auto *cede = dynamic_cast<CedeExpr *>(source)) {
    // The checked cede expression denotes the transferred value, not a live
    // receiver slot. Later arguments may legally reinitialize its old binding.
    if (!cede->SourceCheckSucceeded && !cede->IsImplicitCallTransfer) return {};
    if (auto value = saved()) return value;
    auto value = cede->Value->ByteBuffer;
    return value && value->Scope == CurrentFunction && sameByteValue(value->ValueType, source->ResolvedType) ? value : nullptr;
  }
  Expr *wrapped = nullptr;
  if (auto *unsafe = dynamic_cast<UnsafeExpr *>(source)) wrapped = unsafe->Expression.get();
  if (auto *cast = dynamic_cast<CastExpr *>(source)) wrapped = cast->Expression.get();
  if (auto *postfix = dynamic_cast<PostfixExpr *>(source); postfix && postfix->Op == TokenType::TokenWrite)
    wrapped = postfix->LHS.get();
  if (wrapped) {
    if (!sameByteValue(wrapped->ResolvedType, source->ResolvedType)) return {};
    return byteBufferArgumentFact(wrapped, consumes);
  }
  auto path = makeAccessPath(source);
  // A consuming receiver need not have explicit caller spelling yet. Reuse
  // its evaluated value only if normal Sema has actually retired that place.
  // IsCeded alone must not authorize replay of an invalidated live receipt.
  if (consumes && path.RootID) {
    SymbolInfo *binding = nullptr;
    if (CurrentScope->findSymbolByID(path.RootID, binding) && binding) {
      bool retired = path.Projections.empty() && hasExactlyPlaceState(binding->placeFact(), PlaceState::Moved);
      if (path.Projections.size() == 1 && path.Projections[0].Kind == AccessProjectionKind::Field) {
        auto shape = std::dynamic_pointer_cast<ShapeType>(binding->TypeObj);
        if (shape && shape->Decl) for (size_t i = 0; i < shape->Decl->Members.size(); ++i)
          if (Type::stripMorphology(shape->Decl->Members[i].Name) == path.Projections[0].Name)
            retired = hasExactlyPlaceState(binding->ExactPlace.projectionFact(PartialMoveProjectionKind::DirectField, i), PlaceState::Moved);
      }
      if (retired) return saved();
    }
  }
  if (auto *variable = dynamic_cast<VariableExpr *>(source)) return byteBufferFact(variable);
  if (auto *member = dynamic_cast<MemberExpr *>(source)) {
    auto parent = byteBufferArgumentFact(member->Object.get(), false);
    if (!parent) return {};
    auto field = parent->Fields.find(member->Member);
    if (field != parent->Fields.end() && sameByteValue(field->second->ValueType, source->ResolvedType)) return field->second;
    auto shape = std::dynamic_pointer_cast<ShapeType>(member->Object->ResolvedType);
    if (!shape || !shape->Decl || shape->Decl->Kind == ShapeKind::Enum || member->Index < 0 ||
        static_cast<size_t>(member->Index) >= shape->Decl->Members.size() ||
        !containsByteBuffer(source->ResolvedType) ||
        !sameByteValue(getPhysicalType(shape->Decl->Members[member->Index]), source->ResolvedType)) return {};
    auto value = std::make_shared<ByteBufferFact>(*parent);
    value->ValueType = source->ResolvedType; value->Fields.clear();
    return value;
  }
  // Other places are not qualified by an old expression annotation. Genuine
  // temporaries have already produced their value and have no live place to
  // reread after the remaining arguments are evaluated.
  return path ? nullptr : saved();
}

void Sema::bindByteBuffer(const AccessPath &destination, Expr *source) {
  if (!destination.RootID) return;
  auto proof = source ? source->ByteBuffer : nullptr;
  if (!proof) proof = byteBufferFact(source);
  m_ByteBuffers.erase(destination.RootID);
  if (destination.Projections.empty() && proof) m_ByteBuffers[destination.RootID] = std::move(proof);
}

void Sema::recordByteBufferExpression(Expr *source, bool valid) {
  if (!source || source->ByteBufferRecorded) return;
  source->ByteBufferRecorded = true;
  if (!valid || !m_EnableStage1ExplicitCallerCede || m_IsPrecomputingCaptures || !CurrentFunction || !source->ResolvedType) return;
  auto make = [&](std::shared_ptr<Type> type) {
    auto proof = std::make_shared<ByteBufferFact>();
    proof->Scope = CurrentFunction; proof->ValueType = std::move(type);
    return proof;
  };
  auto inherit = [&](Expr *value) {
    auto proof = value ? value->ByteBuffer : nullptr;
    if (!proof) proof = byteBufferFact(value);
    if (proof && sameByteValue(proof->ValueType, source->ResolvedType)) source->ByteBuffer = proof;
  };
  if (auto *variable = dynamic_cast<VariableExpr *>(source)) { source->ByteBuffer = byteBufferFact(variable); return; }
  if (auto *cede = dynamic_cast<CedeExpr *>(source)) {
    inherit(cede->Value.get());
    if (makeAccessPath(cede->Value.get())) invalidateByteBuffer(cede->Value.get());
    return;
  }
  if (auto *unsafe = dynamic_cast<UnsafeExpr *>(source)) { inherit(unsafe->Expression.get()); return; }
  if (auto *cast = dynamic_cast<CastExpr *>(source)) { inherit(cast->Expression.get()); return; }
  if (auto *postfix = dynamic_cast<PostfixExpr *>(source)) { inherit(postfix->LHS.get()); return; }
  if (auto *member = dynamic_cast<MemberExpr *>(source)) {
    auto parent = member->Object->ByteBuffer;
    if (parent) {
      auto field = parent->Fields.find(member->Member);
      if (field != parent->Fields.end() && sameByteValue(field->second->ValueType, source->ResolvedType)) source->ByteBuffer = field->second;
      else if (isByteBufferType(member->Object->ResolvedType) && Type::stripMorphology(member->Member) == "buf")
        invalidateByteBuffer(member->Object.get());
      else if (containsByteBuffer(source->ResolvedType)) {
        auto shape = std::dynamic_pointer_cast<ShapeType>(member->Object->ResolvedType);
        if (shape && shape->Decl && shape->Decl->Kind != ShapeKind::Enum && member->Index >= 0 &&
            static_cast<size_t>(member->Index) < shape->Decl->Members.size() &&
            sameByteValue(getPhysicalType(shape->Decl->Members[member->Index]), source->ResolvedType)) {
          // An all-fields qualified parameter/return may be projected only
          // through the checked physical field. Its prerequisites remain those
          // of the actual whole input, not a fabricated new allocation.
          auto selected = std::make_shared<ByteBufferFact>(*parent);
          selected->ValueType = source->ResolvedType; selected->Fields.clear();
          source->ByteBuffer = std::move(selected);
        }
      }
    }
    return;
  }
  if (auto *address = dynamic_cast<AddressOfExpr *>(source)) { invalidateByteBuffer(address->Expression.get()); return; }
  if (auto *unary = dynamic_cast<UnaryExpr *>(source); unary && unary->Op == TokenType::Ampersand) {
    invalidateByteBuffer(unary->RHS.get()); return;
  }
  auto *call = dynamic_cast<CallExpr *>(source);
  auto *method = dynamic_cast<MethodCallExpr *>(source);
  auto *function = call ? call->ResolvedFn : method ? method->ResolvedFn : nullptr;
  auto combine = [&](std::shared_ptr<ByteBufferFact> &result, Expr *value, const std::string &field) {
    auto child = value ? value->ByteBuffer : nullptr;
    if (!child) child = byteBufferFact(value);
    if (!child) return value && closedTaskResultType(value->ResolvedType);
    if (child->Scope != CurrentFunction || !sameByteValue(child->ValueType, value->ResolvedType)) return false;
    result->RequiredArguments.insert(child->RequiredArguments.begin(), child->RequiredArguments.end());
    result->StorageRoots.insert(child->StorageRoots.begin(), child->StorageRoots.end());
    result->Fields[field] = child;
    return true;
  };
  if (auto *init = dynamic_cast<InitStructExpr *>(source); init && containsByteBuffer(source->ResolvedType)) {
    auto result = make(source->ResolvedType);
    for (auto &[field, value] : init->Members) if (!combine(result, value.get(), field)) return;
    if (!result->Fields.empty()) source->ByteBuffer = std::move(result);
    return;
  }
  if (call && call->ResolvedShape && call->ResolvedShape->Kind == ShapeKind::Enum &&
      call->MatchedMemberIdx >= 0 && containsByteBuffer(source->ResolvedType)) {
    auto result = make(source->ResolvedType);
    for (size_t i = 0; i < call->Args.size(); ++i)
      if (!combine(result, call->Args[i].get(), std::to_string(i))) return;
    if (!result->Fields.empty()) source->ByteBuffer = std::move(result);
    return;
  }
  if (!function) {
    // Extern and indirect calls have no source-visible FunctionDecl recipe.
    // Absence of that recipe is not a preservation promise for an actual
    // owner argument. Enum constructors were handled above.
    auto expose = [&](Expr *actual) {
      auto task = actual ? taskResultFact(actual) : nullptr;
      if (actual && (actual->ByteBuffer || byteBufferFact(actual) || (task && task->Bytes)))
        invalidateByteBuffer(actual);
    };
    if (call) for (auto &actual : call->Args) expose(actual.get());
    if (method) {
      expose(method->Object.get());
      for (auto &actual : method->Args) expose(actual.get());
    }
    return;
  }
  auto argument = [&](size_t i) -> Expr * {
    return call ? (i < call->Args.size() ? call->Args[i].get() : nullptr)
        : i == 0 ? method->Object.get() : i <= method->Args.size() ? method->Args[i-1].get() : nullptr;
  };
  auto captured = [&](size_t i) -> std::shared_ptr<const ByteBufferFact> {
    return byteBufferArgumentFact(argument(i), i < function->Args.size() && function->Args[i].IsCeded);
  };
  auto *definition = function->TemplateOrigin ? function->TemplateOrigin : function;
  auto *module = getLexicalModule(definition->Loc);
  const auto op = operationName(function);
  const bool trusted = module && byteBufferSchema(definition->Loc, module->ShadowLogicalModulePath) &&
      nativeSyncDefinitionReady(function);
  const bool vector = trusted && module->ShadowLogicalModulePath == "std/vec";
  const bool bytes = trusted && module->ShadowLogicalModulePath == "std/bytes";
  if ((vector || bytes) && isByteBufferType(source->ResolvedType) &&
      (op == "new" || op == "with_capacity") &&
      function->Args.size() == (op == "new" ? 0u : 1u)) {
    auto result = make(source->ResolvedType);
    result->StorageRoots.insert(source);
    source->ByteBuffer = std::move(result);
    return;
  }
  auto input = captured(0);
  const bool exactInput = input && !function->Args.empty() &&
      sameByteValue(input->ValueType, function->Args[0].ResolvedType) && isByteBufferType(input->ValueType);
  if ((vector || bytes) && exactInput) {
    if ((op == "from_vec" && bytes && function->Args[0].IsCeded) || op == "into_vec" || op == "take") {
      if (!isByteBufferType(source->ResolvedType)) return;
      auto result = std::make_shared<ByteBufferFact>(*input);
      result->ValueType = source->ResolvedType; result->Fields.clear();
      source->ByteBuffer = std::move(result);
      invalidateByteBuffer(argument(0));
      if (op != "from_vec") {
        auto empty = make(input->ValueType); empty->StorageRoots.insert(source);
        auto path = makeAccessPath(argument(0));
        if (path.RootID && path.Projections.empty()) m_ByteBuffers[path.RootID] = std::move(empty);
      }
      return;
    }
    if (op == "push" || op == "resize" || op == "grow" || op == "set" ||
        (bytes && op == "append_bytes_to_vec")) {
      auto path = makeAccessPath(argument(0));
      if (path.RootID && path.Projections.empty()) m_ByteBuffers[path.RootID] = std::make_shared<ByteBufferFact>(*input);
      return;
    }
    if (op == "len" || op == "cap" || op == "is_empty" || op == "get" || op == "get_opt" || op == "at" ||
        (bytes && op == "as_slice")) return;
  }
  if (trusted && module->ShadowLogicalModulePath == "std/net" &&
      (op == "read_into_async" || op == "write_from_async") && function->Effect == EffectKind::Async) {
    for (size_t i = 0; i < function->Args.size(); ++i) {
      if (!function->Args[i].IsCeded || !isByteBufferType(function->Args[i].ResolvedType)) continue;
      auto actual = captured(i);
      auto resultType = taskResultType(source->ResolvedType);
      if (!actual || !sameByteValue(actual->ValueType, function->Args[i].ResolvedType) || !containsByteBuffer(resultType)) return;
      auto result = std::make_shared<ByteBufferFact>(*actual);
      result->ValueType = resultType; result->Fields.clear();
      auto task = std::make_shared<TaskResultFact>();
      task->Scope = CurrentFunction; task->ValueType = resultType; task->CarrierType = source->ResolvedType;
      task->TaskCarrier = true; task->Origin = TaskResultFact::Kind::Independent;
      task->IndependentParameters = result->RequiredArguments; task->Bytes = std::move(result);
      source->TaskResult = std::move(task);
      return;
    }
  }
  // Ordinary source-visible factories use the existing checked all-return
  // summary. A contract-free function name or empty dependency set is useless.
  if (containsByteBuffer(source->ResolvedType)) {
    prepareCallableFactory(function);
    const auto summary = m_IndependentReturns.find(function);
    if (summary != m_IndependentReturns.end() && summary->second->Bytes &&
        sameByteValue(summary->second->ValueType, source->ResolvedType) && nativeSyncDefinitionReady(function)) {
      auto result = make(source->ResolvedType);
      result->StorageRoots.insert(source);
      bool complete = true;
      for (auto index : summary->second->RequiredArguments) {
        auto actual = captured(index);
        if (!actual || index >= function->Args.size() || !sameByteValue(actual->ValueType, function->Args[index].ResolvedType)) { complete = false; break; }
        result->RequiredArguments.insert(actual->RequiredArguments.begin(), actual->RequiredArguments.end());
        result->StorageRoots.insert(actual->StorageRoots.begin(), actual->StorageRoots.end());
      }
      if (complete) source->ByteBuffer = std::move(result);
    }
  }
  // Unmatched writes/raw exposures cannot preserve qualification. This does
  // not change permissions, runtime cleanup, or the call's existing diagnostics.
  for (size_t i = 0; i < function->Args.size(); ++i) {
    const auto &formal = function->Args[i];
    if (captured(i) || formal.IsCeded || formal.IsValueMutable || formal.IsRebindable ||
        (source->ResolvedType && (source->ResolvedType->isRawPointer() || source->ResolvedType->isAddrType())))
      if (auto *actual = argument(i)) invalidateByteBuffer(actual);
  }
}
} // namespace toka
