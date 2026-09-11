#include "toka/Sema.h"
#include <algorithm>

namespace toka {
namespace {
Expr *transparentEnumSource(Expr *value) {
  if (auto *cede = dynamic_cast<CedeExpr *>(value)) return cede->Value.get();
  if (auto *unsafe = dynamic_cast<UnsafeExpr *>(value)) return unsafe->Expression.get();
  if (auto *cast = dynamic_cast<CastExpr *>(value);
      cast && cast->Kind == CastKind::Ascription && cast->ResolvedType &&
      cast->Expression->ResolvedType && cast->ResolvedType->equals(*cast->Expression->ResolvedType))
    return cast->Expression.get();
  return nullptr;
}
}

void Sema::recordEnumBinding(const AccessPath &destination, Expr *source) {
  if (!destination.RootID) return;
  auto result = m_EnumExpressionResults.find(source);
  auto selection = m_EnumExpressionSelections.find(source);
  // Read before erasing: a successful self-copy need not invent a new origin.
  auto value = result == m_EnumExpressionResults.end() ? nullptr : result->second;
  auto payload = selection == m_EnumExpressionSelections.end()
      ? std::optional<EnumPayloadSelection>{} : selection->second;
  m_EnumResults.erase(destination.RootID);
  m_EnumSelections.erase(destination.RootID);
  if (!destination.Projections.empty()) return;
  if (value) m_EnumResults[destination.RootID] = std::move(value);
  if (payload) m_EnumSelections[destination.RootID] = std::move(*payload);
}

void Sema::recordEnumPattern(MatchArm::Pattern *pattern, ShapeDecl *shape,
                            size_t variant, size_t slot, const AccessPath &source, bool consuming) {
  if (!m_EnableStage1ExplicitCallerCede || !CurrentFunction || !pattern ||
      pattern->PatternKind != MatchArm::Pattern::Variable || pattern->IsReference ||
      !consuming || !source.RootID || !source.Projections.empty() ||
      !shape || shape->Kind != ShapeKind::Enum) return;
  SymbolInfo *input = nullptr, *binding = nullptr;
  CurrentScope->findSymbolByID(source.RootID, input);
  if (!input || !CurrentScope->findSymbol(pattern->Name, binding) || !binding) return;
  auto type = std::dynamic_pointer_cast<ShapeType>(input->TypeObj);
  if (!type || type->Decl != shape) return;
  EnumPayloadSelection selected;
  selected.Function = CurrentFunction;
  selected.Declaration = shape;
  selected.Variant = variant;
  selected.Slot = slot;
  if (auto actual = m_EnumResults.find(source.RootID); actual != m_EnumResults.end())
    selected.Source = actual->second;
  else {
    if (!input->IsFunctionParameter || !input->IsCeded) return;
    auto formal = std::find_if(CurrentFunction->Args.begin(), CurrentFunction->Args.end(),
        [&](const auto &arg) { return Type::stripMorphology(arg.Name) == Type::stripMorphology(source.RootName); });
    if (formal == CurrentFunction->Args.end()) return;
    selected.Parameter = formal - CurrentFunction->Args.begin();
  }
  m_EnumSelections[binding->SymbolID] = std::move(selected);
}

bool Sema::collectStaticEnumPayload(Expr *expression, std::vector<SourceLocation> &origins) {
  if (!m_EnableStage1ExplicitCallerCede || !expression || !expression->ResolvedType ||
      expression->ResolvedType->isReference() || expression->ResolvedType->isRawPointer() ||
      queryExplicitCedeStage0OwnershipReadOnly(expression->ResolvedType) != ValueOwnership::BorrowedView)
    return false;
  auto selected = m_EnumExpressionSelections.find(expression);
  if (selected == m_EnumExpressionSelections.end() || !selected->second.Source) return false;
  const auto &selection = selected->second;
  const auto &source = *selection.Source;
  if (!source.Producer || !source.Edge || source.Declaration != selection.Declaration) return false;
  if (selection.Variant >= source.Declaration->Members.size()) return false;
  const auto &variant = source.Declaration->Members[selection.Variant];
  if (variant.IsUnitVariant || (!variant.SubMembers.empty() && selection.Slot >= variant.SubMembers.size()) ||
      (variant.SubMembers.empty() && selection.Slot != 0)) return false;
  auto payloadType = resolveExplicitCedeStage0TypeReadOnly(getPhysicalType(
      variant.SubMembers.empty() ? variant : variant.SubMembers[selection.Slot]));
  if (!payloadType || !payloadType->equals(*expression->ResolvedType)) return false;
  auto found = source.StaticSlots.find({selection.Variant, selection.Slot});
  if (found == source.StaticSlots.end() || found->second.empty()) return false;
  origins.insert(origins.end(), found->second.begin(), found->second.end());
  return true;
}

void Sema::recordEnumExpression(Expr *expression, bool valid) {
  if (!valid || !m_EnableStage1ExplicitCallerCede || m_IsPrecomputingCaptures || !expression) return;
  if (auto *assignment = dynamic_cast<BinaryExpr *>(expression);
      assignment && (assignment->Op == "=" || assignment->Op == "+=" || assignment->Op == "-=")) {
    recordEnumBinding(canonicalizeAccessPath(makeAccessPath(assignment->LHS.get())),
                      assignment->Op == "=" ? assignment->RHS.get() : nullptr);
    return;
  }
  if (auto *underlying = transparentEnumSource(expression)) {
    if (auto value = m_EnumExpressionResults.find(underlying); value != m_EnumExpressionResults.end())
      m_EnumExpressionResults[expression] = value->second;
    if (auto value = m_EnumExpressionSelections.find(underlying); value != m_EnumExpressionSelections.end())
      m_EnumExpressionSelections[expression] = value->second;
    return;
  }
  if (auto *cast = dynamic_cast<CastExpr *>(expression);
      cast && cast->ResolvedType && (cast->ResolvedType->isRawPointer() ||
      cast->ResolvedType->isAddrType() || cast->ResolvedType->isOAddrType())) {
    // Exposure of storage through an untracked raw address forfeits this
    // result witness, even if later writes do not retain a named alias.
    recordEnumBinding(canonicalizeAccessPath(makeAccessPath(cast->Expression.get())), nullptr);
    return;
  }
  if (auto *variable = dynamic_cast<VariableExpr *>(expression)) {
    const auto path = canonicalizeAccessPath(makeAccessPath(variable));
    if (!path || !path.Projections.empty()) return;
    if (auto value = m_EnumResults.find(path.RootID); value != m_EnumResults.end())
      m_EnumExpressionResults[expression] = value->second;
    if (auto value = m_EnumSelections.find(path.RootID); value != m_EnumSelections.end())
      m_EnumExpressionSelections[expression] = value->second;
    return;
  }

  auto *call = dynamic_cast<CallExpr *>(expression);
  auto *method = dynamic_cast<MethodCallExpr *>(expression);
  if (!call && !method) return;
  FunctionDecl *function = call ? call->ResolvedFn : method->ResolvedFn;
  auto argument = [&](size_t index) -> Expr * {
    if (method) return index == 0 ? method->Object.get() :
        index - 1 < method->Args.size() ? method->Args[index - 1].get() : nullptr;
    return index < call->Args.size() ? call->Args[index].get() : nullptr;
  };
  auto invalidate = [&](Expr *actual) {
    auto path = canonicalizeAccessPath(makeAccessPath(actual));
    if (path.RootID) { m_EnumResults.erase(path.RootID); m_EnumSelections.erase(path.RootID); }
  };
  if (function) {
    for (size_t index = 0; index < function->Args.size(); ++index) {
      const auto &formal = function->Args[index];
      auto type = formal.ResolvedType;
      if (!formal.IsCeded && (formal.IsValueMutable || formal.IsRebindable || formal.IsInit ||
          (type && (type->IsWritable || (type->getPointeeType() && type->getPointeeType()->IsWritable)))))
        invalidate(argument(index));
    }
  } else if (!call || !call->ResolvedShape) {
    // No checked formal: no preservation claim across a potentially writing call.
    if (method) { invalidate(method->Object.get()); for (auto &arg : method->Args) invalidate(arg.get()); }
    else for (auto &arg : call->Args) invalidate(arg.get());
  }

  if (call && call->ResolvedShape && call->ResolvedShape->Kind == ShapeKind::Enum &&
      call->MatchedMemberIdx >= 0 && size_t(call->MatchedMemberIdx) < call->ResolvedShape->Members.size()) {
    auto result = std::make_shared<EnumResultSource>();
    result->Producer = CurrentFunction;
    result->Edge = expression;
    result->Declaration = call->ResolvedShape;
    const size_t variant = call->MatchedMemberIdx;
    const auto &member = call->ResolvedShape->Members[variant];
    const size_t count = member.IsUnitVariant ? 0 : member.SubMembers.empty() ? 1 : member.SubMembers.size();
    if (count != call->Args.size()) return;
    result->Variants.insert(variant);
    // The first slice proves literals and immutable local literal chains only.
    // It never interprets an empty dependency set or a descriptor address as static.
    std::set<uint64_t> active;
    std::function<bool(Expr *, std::vector<SourceLocation> &)> literalOrigins =
        [&](Expr *value, std::vector<SourceLocation> &out) {
      if (!value || !value->ResolvedType || value->ResolvedType->isReference() ||
          value->ResolvedType->isRawPointer()) return false;
      if (dynamic_cast<ViewStringExpr *>(value) || dynamic_cast<StringExpr *>(value)) {
        if (!value->Loc.isValid()) return false;
        out.push_back(value->Loc); return true;
      }
      if (auto *underlying = transparentEnumSource(value)) return literalOrigins(underlying, out);
      if (collectStaticEnumPayload(value, out)) return true;
      auto *variable = dynamic_cast<VariableExpr *>(value);
      SymbolInfo *binding = nullptr;
      if (!variable || !CurrentScope->findSymbol(variable->Name, binding) || !binding ||
          binding->IsFunctionParameter || binding->IsDeclaredMutable || !binding->ASTPtr ||
          m_ReturnSourceInvalidatedRoots.count(binding->SymbolID) || m_ReturnSourceUnknownRoots.count(binding->SymbolID) ||
          !active.insert(binding->SymbolID).second) return false;
      auto *decl = dynamic_cast<VariableDecl *>(static_cast<ASTNode *>(binding->ASTPtr));
      const bool complete = decl && decl->Init && literalOrigins(decl->Init.get(), out);
      active.erase(binding->SymbolID);
      return complete;
    };
    for (size_t slot = 0; slot < count; ++slot) {
      auto type = resolveExplicitCedeStage0TypeReadOnly(getPhysicalType(
          member.SubMembers.empty() ? member : member.SubMembers[slot]));
      std::vector<SourceLocation> origins;
      if (type && call->Args[slot]->ResolvedType && type->equals(*call->Args[slot]->ResolvedType) &&
          !type->isReference() && !type->isRawPointer() &&
          queryExplicitCedeStage0OwnershipReadOnly(type) == ValueOwnership::BorrowedView &&
          literalOrigins(call->Args[slot].get(), origins) && !origins.empty())
        result->StaticSlots[{variant, slot}] = std::move(origins);
    }
    m_EnumExpressionResults[expression] = std::move(result);
    return;
  }
  if (!function || !function->Body) return;
  auto resultType = std::dynamic_pointer_cast<ShapeType>(expression->ResolvedType);
  const bool enumResult = resultType && resultType->Decl && resultType->Decl->Kind == ShapeKind::Enum &&
      hasBorrowedValueFields(resultType);
  const bool viewResult = expression->ResolvedType && !expression->ResolvedType->isReference() &&
      !expression->ResolvedType->isRawPointer() &&
      queryExplicitCedeStage0OwnershipReadOnly(expression->ResolvedType) == ValueOwnership::BorrowedView;
  if (!enumResult && !viewResult) return;
  auto summary = m_EnumReturnSummaries.find(function);
  if (summary == m_EnumReturnSummaries.end()) {
    // Ordinary producers can use existing isolated definition preparation.
    // Method/instance bodies require their real receiver/instantiation scope.
    if (method || !enumResult) return;
    prepareCallableFactory(function);
    summary = m_EnumReturnSummaries.find(function);
  }
  if (summary == m_EnumReturnSummaries.end() || !summary->second.Checked || !summary->second.Valid) return;
  if (function->TemplateOrigin) {
    auto cached = InstantiationCache.find(function->Name);
    if (cached == InstantiationCache.end() || !cached->second || cached->second->Instance != function ||
        cached->second->Validation != GenericSpecializationValidationState::Valid) return;
  }
  const auto &facts = summary->second;
  if (facts.CompleteResults && facts.Result && resultType && resultType->Decl == facts.Result->Declaration) {
    auto result = std::make_shared<EnumResultSource>(*facts.Result);
    result->Producer = function;
    result->Edge = expression;
    m_EnumExpressionResults[expression] = std::move(result);
  }
  if (facts.CompleteSelection && facts.Selection) {
    auto input = m_EnumExpressionResults.find(argument(facts.Selection->Parameter));
    if (input != m_EnumExpressionResults.end() && input->second->Declaration == facts.Selection->Declaration) {
      auto selected = *facts.Selection;
      selected.Source = input->second;
      m_EnumExpressionSelections[expression] = std::move(selected);
    }
  }
}

void Sema::recordEnumReturn(ReturnStmt *statement, bool valid) {
  auto found = m_EnumReturnSummaries.find(CurrentFunction);
  if (!m_EnableStage1ExplicitCallerCede || found == m_EnumReturnSummaries.end() ||
      found->second.ClosureDepth != m_CallableReturnClosureDepth) return;
  auto &summary = found->second;
  summary.SawReturn = true;
  auto *expression = statement ? statement->ReturnValue.get() : nullptr;
  auto result = m_EnumExpressionResults.find(expression);
  auto selected = m_EnumExpressionSelections.find(expression);
  if (!valid || result == m_EnumExpressionResults.end()) summary.CompleteResults = false;
  else if (!summary.Result) summary.Result = *result->second;
  else {
    auto &combined = *summary.Result;
    const auto &incoming = *result->second;
    if (combined.Declaration != incoming.Declaration) summary.CompleteResults = false;
    else {
      for (auto it = combined.StaticSlots.begin(); it != combined.StaticSlots.end();) {
        auto other = incoming.StaticSlots.find(it->first);
        if (incoming.Variants.count(it->first.first) && other == incoming.StaticSlots.end())
          it = combined.StaticSlots.erase(it);
        else { if (other != incoming.StaticSlots.end()) it->second.insert(it->second.end(), other->second.begin(), other->second.end()); ++it; }
      }
      for (const auto &[slot, origins] : incoming.StaticSlots)
        if (!combined.Variants.count(slot.first)) combined.StaticSlots[slot] = origins;
      combined.Variants.insert(incoming.Variants.begin(), incoming.Variants.end());
    }
  }
  if (!valid || selected == m_EnumExpressionSelections.end() || selected->second.Source ||
      selected->second.Function != CurrentFunction) summary.CompleteSelection = false;
  else if (!summary.Selection) summary.Selection = selected->second;
  else if (*summary.Selection != selected->second) summary.CompleteSelection = false;
}
} // namespace toka
