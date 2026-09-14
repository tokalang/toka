#include "toka/Sema.h"
#include "toka/MemberAccess.h"

namespace toka {

bool Sema::isWholeGenericField(const ShapeDecl *shape, const ShapeMember &field) const {
  auto *declaration = shape && shape->InstantiationTemplate ? shape->InstantiationTemplate : shape;
  if (!declaration || !field.TypeSyntax ||
      field.TypeSyntax->NodeKind != TypeSyntax::Kind::Named ||
      !field.Permission.HandleLayers.empty() ||
      field.Permission.Morphology != BindingMorphology::None) return false;
  for (const auto &parameter : declaration->GenericParams) {
    auto name = parameter.Name;
    if (!name.empty() && name.front() == '\'') name = name.substr(1);
    auto fieldName = field.TypeSyntax->Text;
    if (!fieldName.empty() && fieldName.front() == '\'') fieldName = fieldName.substr(1);
    if (!parameter.IsConst && name == fieldName) return true;
  }
  return false;
}

GenericValueContractPtr Sema::makeGenericValueContract(
    TypeSyntaxPtr syntax, const std::set<std::string> &parameters) const {
  auto contains = [&](auto &&self, const TypeSyntaxPtr &type) -> bool {
    if (!type) return false;
    if (type->NodeKind == TypeSyntax::Kind::Named && parameters.count(type->Text)) return true;
    if (self(self, type->Subject) || self(self, type->Result)) return true;
    for (const auto &arg : type->Arguments)
      if (arg.ArgumentKind == TypeArgumentSyntax::Kind::Type && self(self, arg.Type)) return true;
    for (const auto &element : type->Elements) if (self(self, element)) return true;
    for (const auto &field : type->Fields) if (self(self, field.Type)) return true;
    return false;
  };
  if (!contains(contains, syntax)) return nullptr;
  auto result = std::make_shared<GenericValueContract>();
  result->Type = std::move(syntax);
  result->Parameters = parameters;
  return result;
}

GenericValueContractPtr Sema::projectGenericFieldContract(
    Expr *object, const ShapeDecl *shape, size_t index) {
  auto contract = queryGenericValueContract(object);
  if (!contract || !shape) return nullptr;
  auto *declaration = shape->InstantiationTemplate ? shape->InstantiationTemplate : shape;
  if (index >= declaration->Members.size()) return nullptr;
  return projectGenericMemberContract(contract, shape, declaration->Members[index]);
}

GenericValueContractPtr Sema::projectGenericMemberContract(
    GenericValueContractPtr contract, const ShapeDecl *shape,
    const ShapeMember &sourceMember) {
  if (!contract || !shape) return nullptr;
  auto type = contract->Type;
  while (type && type->NodeKind == TypeSyntax::Kind::Morphology) type = type->Subject;
  auto *declaration = shape->InstantiationTemplate ? shape->InstantiationTemplate : shape;
  if (!type || type->NodeKind != TypeSyntax::Kind::GenericApplication ||
      !type->Subject ||
      type->Arguments.size() != declaration->GenericParams.size()) return nullptr;
  // Read the recorded source lexical table. findVisibleShapeDecl would also
  // mark imports used, and the current caller's scope may contain a namesake.
  auto *scope = getLexicalModule(type->Subject->Begin);
  std::string name = type->Subject->toCanonicalString();
  if (auto separator = name.find("::"); separator != std::string::npos && scope) {
    auto module = scope->LexicalSymbols.find(name.substr(0, separator));
    scope = module == scope->LexicalSymbols.end() ? nullptr
        : static_cast<ModuleScope *>(module->second.ReferencedModule);
    name = name.substr(separator + 2);
  }
  if (!scope) return nullptr;
  auto symbol = scope->LexicalTypes.find(name);
  if (symbol == scope->LexicalTypes.end() || !symbol->second.IsTypeName ||
      symbol->second.IsTypeAlias || symbol->second.IsTraitName ||
      symbol->second.ASTPtr != declaration) return nullptr;
  std::map<std::string, TypeSyntaxPtr> substitutions;
  for (size_t i = 0; i < type->Arguments.size(); ++i) {
    if (declaration->GenericParams[i].IsConst ||
        type->Arguments[i].ArgumentKind != TypeArgumentSyntax::Kind::Type) continue;
    auto name = declaration->GenericParams[i].Name;
    substitutions[name] = type->Arguments[i].Type;
    if (!name.empty() && name.front() == '\'') substitutions[name.substr(1)] = type->Arguments[i].Type;
  }
  auto physical = synthesizePhysicalTypeObject(sourceMember);
  if (!physical) return nullptr;
  auto location = sourceMember.Loc;
  auto syntax = physical->toSyntax(location, location)->substitute(substitutions);
  auto result = std::make_shared<GenericValueContract>();
  result->Type = std::move(syntax);
  result->Parameters = contract->Parameters;
  return result;
}

GenericValueContractPtr Sema::queryGenericValueContract(Expr *expression) {
  if (!expression) return nullptr;
  if (expression->GenericContract) return expression->GenericContract;
  if (auto *value = dynamic_cast<VariableExpr *>(expression)) {
    SymbolInfo *binding = nullptr;
    std::string name;
    if (CurrentScope->findVariableWithDeref(value->Name, binding, name) && binding)
      return binding->GenericContract;
    return nullptr;
  }
  if (auto *cede = dynamic_cast<CedeExpr *>(expression)) return queryGenericValueContract(cede->Value.get());
  if (auto *unsafe = dynamic_cast<UnsafeExpr *>(expression)) return queryGenericValueContract(unsafe->Expression.get());
  if (auto *member = dynamic_cast<MemberExpr *>(expression)) {
    auto objectType = member->Object->ResolvedType;
    if (!objectType) objectType = queryExplicitCedeStage0NonCallType(member->Object.get(), nullptr);
    auto shape = std::dynamic_pointer_cast<ShapeType>(objectType ? objectType->getSoulType() : nullptr);
    if (!shape || !shape->Decl) return nullptr;
    auto access = parseMemberAccess(member->Member);
    for (size_t i = 0; i < shape->Decl->Members.size(); ++i) {
      if (Type::stripMorphology(shape->Decl->Members[i].Name) != access.MemberName) continue;
      auto contract = projectGenericFieldContract(member->Object.get(), shape->Decl, i);
      if (!contract || !access.Prefix.empty() || access.IsMorphicIdentity) return contract;
      auto selected = std::make_shared<GenericValueContract>(*contract);
      while (!selected->isWholeValue() && selected->Type &&
             selected->Type->NodeKind == TypeSyntax::Kind::Morphology)
        selected->Type = selected->Type->Subject;
      return selected;
    }
    return nullptr;
  }
  if (auto *index = dynamic_cast<ArrayIndexExpr *>(expression)) {
    auto contract = queryGenericValueContract(index->Array.get());
    if (!contract || contract->isWholeValue()) return nullptr;
    auto type = contract->Type;
    while (type && type->NodeKind == TypeSyntax::Kind::Morphology) type = type->Subject;
    if (!type || (type->NodeKind != TypeSyntax::Kind::Array && type->NodeKind != TypeSyntax::Kind::Slice)) return nullptr;
    auto result = std::make_shared<GenericValueContract>(*contract);
    result->Type = type->Subject;
    return result;
  }
  const auto parameters = callableDeclarationGenericNames(CurrentFunction);
  if (auto *cast = dynamic_cast<CastExpr *>(expression)) {
    if (cast->Kind == CastKind::Implicit) return queryGenericValueContract(cast->Expression.get());
    return makeGenericValueContract(cast->TargetTypeSyntax, parameters);
  }
  if (auto *init = dynamic_cast<InitStructExpr *>(expression)) {
    auto source = Type::fromString(init->OriginalShapeName);
    return source ? makeGenericValueContract(source->toSyntax(init->Loc, init->Loc), parameters) : nullptr;
  }
  if (auto *call = dynamic_cast<CallExpr *>(expression)) {
    if (call->ResolvedShape && !call->GenericArgSyntax.empty()) {
      auto syntax = TypeSyntax::generic(TypeSyntax::named(call->OriginalCallee, call->Loc, call->Loc),
                                       call->GenericArgSyntax, call->Loc, call->Loc);
      return makeGenericValueContract(syntax, parameters);
    }
    auto *function = call->ResolvedFn;
    auto *declaration = function && function->TemplateOrigin ? function->TemplateOrigin : function;
    if (!declaration || declaration->GenericParams.empty() || !declaration->ReturnTypeSyntax) return nullptr;
    std::map<std::string, TypeSyntaxPtr> substitutions;
    for (size_t i = 0; i < declaration->GenericParams.size(); ++i) {
      const auto &parameter = declaration->GenericParams[i];
      if (parameter.IsConst) continue;
      TypeSyntaxPtr argument;
      if (i < call->GenericArgSyntax.size() &&
          call->GenericArgSyntax[i].ArgumentKind == TypeArgumentSyntax::Kind::Type)
        argument = call->GenericArgSyntax[i].Type;
      if (!argument) {
        for (size_t j = 0; j < declaration->Args.size() && j < call->Args.size(); ++j) {
          auto formal = declaration->Args[j].TypeSyntax;
          if (!formal || formal->NodeKind != TypeSyntax::Kind::Named ||
              Type::stripMorphology(formal->Text) != Type::stripMorphology(parameter.Name)) continue;
          auto source = queryGenericValueContract(call->Args[j].get());
          auto candidate = source ? source->Type : call->Args[j]->ResolvedType
              ? call->Args[j]->ResolvedType->toSyntax(call->Loc, call->Loc) : nullptr;
          if (!candidate) return nullptr;
          if (argument && argument->toCanonicalString() != candidate->toCanonicalString()) return nullptr;
          argument = candidate;
        }
      }
      if (!argument) return nullptr;
      substitutions[parameter.Name] = argument;
      if (!parameter.Name.empty() && parameter.Name.front() == '\'')
        substitutions[parameter.Name.substr(1)] = argument;
    }
    return makeGenericValueContract(declaration->ReturnTypeSyntax->substitute(substitutions), parameters);
  }
  return nullptr;
}

void Sema::recordGenericValueContract(Expr *expression) {
  auto contract = queryGenericValueContract(expression);
  if (!contract) return;
  expression->GenericContract = contract;
  if (contract->isWholeValue()) {
    expression->IsAbstractWholeValue = true;
    expression->IsMorphicExempt = true;
  }
}

} // namespace toka
