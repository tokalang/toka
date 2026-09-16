#include "toka/Sema.h"
#include "toka/MemberAccess.h"

namespace toka {

bool Sema::isWholeGenericField(const ShapeDecl *shape, const ShapeMember &field) const {
  while (shape && shape->NominalLayoutOrigin)
    shape = shape->NominalLayoutOrigin;
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

TypeSyntaxPtr Sema::bindGenericSourceTypeSyntax(
    TypeSyntaxPtr syntax, const std::set<std::string> &parameters,
    const Module *sourceModule) {
  std::set<const TypeAliasDecl *> activeAliases;
  auto expand = [&](auto &&self, const TypeSyntaxPtr &input,
                    const Module *module, const std::set<std::string> &binders) -> TypeSyntaxPtr {
    if (!input) return nullptr;
    if (input->NominalDeclaration) return input;
    auto result = std::make_shared<TypeSyntax>(*input);
    for (auto &argument : result->Arguments)
      if (argument.ArgumentKind == TypeArgumentSyntax::Kind::Type)
        argument.Type = self(self, argument.Type, module, binders);
    const auto head = input->NodeKind == TypeSyntax::Kind::GenericApplication
        ? input->Subject : input;
    if (head && head->NodeKind == TypeSyntax::Kind::Named &&
        !binders.count(head->Text)) {
      auto *scope = getLexicalModule(head->Begin);
      std::string name = head->Text;
      if (auto separator = name.find("::"); separator != std::string::npos && scope) {
        auto found = scope->LexicalSymbols.find(name.substr(0, separator));
        scope = found == scope->LexicalSymbols.end() ? nullptr
            : static_cast<ModuleScope *>(found->second.ReferencedModule);
        name = name.substr(separator + 2);
        module = scope ? scope->SourceModule : nullptr;
      }
      if (!module && scope) module = scope->SourceModule;
      const TypeAliasDecl *alias = nullptr;
      ShapeDecl *nominal = nullptr;
      if (scope) {
        auto *lookupScope = scope;
        auto lookupName = name;
        std::set<std::pair<const ModuleScope *, std::string>> visited;
        while (lookupScope && visited.insert({lookupScope, lookupName}).second) {
          auto symbol = lookupScope->LexicalTypes.find(lookupName);
          if (symbol == lookupScope->LexicalTypes.end()) break;
          if (symbol->second.ASTPtr) {
            alias = dynamic_cast<TypeAliasDecl *>(static_cast<ASTNode *>(symbol->second.ASTPtr));
            nominal = dynamic_cast<ShapeDecl *>(static_cast<ASTNode *>(symbol->second.ASTPtr));
            break;
          }
          auto *import = symbol->second.ImportingDecl;
          auto *owner = static_cast<ModuleScope *>(symbol->second.ReferencedModule);
          if (!import || !owner) break;
          bool matched = false;
          for (const auto &item : import->Items) {
            if (item.Symbol == "*") { matched = true; break; }
            if ((item.Alias.empty() ? item.Symbol : item.Alias) == lookupName) {
              lookupName = item.Symbol;
              matched = true;
              break;
            }
          }
          if (!matched) break;
          lookupScope = owner;
        }
      }
      // During declaration collection the module's lexical type table is
      // not populated yet. Its own AST declarations are still exact owners.
      if (!alias && module && name == head->Text)
        for (const auto &candidate : module->TypeAliases)
          if (candidate->Name == name) { alias = candidate.get(); break; }
      if (!alias && !nominal && module && name == head->Text)
        for (const auto &candidate : module->Shapes)
          if (candidate->Name == name) { nominal = candidate.get(); break; }
      if (nominal && result->NodeKind == TypeSyntax::Kind::Named)
        result->NominalDeclaration = nominal;
      if (alias && !alias->IsStrong && alias->TargetTypeSyntax &&
          alias->GenericParams.size() == result->Arguments.size()) {
        if (!activeAliases.insert(alias).second) return nullptr;
        std::map<std::string, TypeSyntaxPtr> substitutions;
        std::set<std::string> aliasBinders;
        for (size_t i = 0; i < alias->GenericParams.size(); ++i) {
          if (alias->GenericParams[i].IsConst ||
              result->Arguments[i].ArgumentKind != TypeArgumentSyntax::Kind::Type) {
            activeAliases.erase(alias);
            return input;
          }
          substitutions[alias->GenericParams[i].Name] = result->Arguments[i].Type;
          aliasBinders.insert(alias->GenericParams[i].Name);
        }
        const auto *owner = getLexicalModule(alias->Loc);
        // Resolve the body in the alias's binder scope *before* substituting
        // caller arguments. Concrete names in that body are not caller T.
        auto target = self(self, alias->TargetTypeSyntax,
                           owner && owner->SourceModule ? owner->SourceModule : module,
                           aliasBinders);
        activeAliases.erase(alias);
        return target ? target->substitute(substitutions) : nullptr;
      }
    }
    result->Subject = self(self, result->Subject, module, binders);
    result->Result = self(self, result->Result, module, binders);
    for (auto &element : result->Elements) element = self(self, element, module, binders);
    for (auto &field : result->Fields) field.Type = self(self, field.Type, module, binders);
    return result;
  };
  return expand(expand, syntax, sourceModule, parameters);
}

GenericValueContractPtr Sema::makeGenericValueContract(
    TypeSyntaxPtr syntax, const std::set<std::string> &parameters,
    const Module *sourceModule) {
  syntax = bindGenericSourceTypeSyntax(syntax, parameters, sourceModule);
  auto contains = [&](auto &&self, const TypeSyntaxPtr &type) -> bool {
    if (!type) return false;
    if (type->NodeKind == TypeSyntax::Kind::Named && !type->NominalDeclaration &&
        parameters.count(type->Text)) return true;
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
  // Project the alias's source-level layout expression, not the concrete
  // specialization's field types. Otherwise Strong<T> would reveal T's
  // concrete fields while the equivalent Box<T> still treats T as opaque.
  while (shape->NominalLayoutOrigin) {
    auto application = contract->Type;
    while (application && application->NodeKind == TypeSyntax::Kind::Morphology)
      application = application->Subject;
    if (!application || application->NodeKind != TypeSyntax::Kind::GenericApplication ||
        !shape->NominalLayoutSyntax ||
        application->Arguments.size() != shape->NominalLayoutParameters.size())
      return nullptr;
    std::map<std::string, TypeSyntaxPtr> substitutions;
    for (size_t i = 0; i < application->Arguments.size(); ++i) {
      const auto &parameter = shape->NominalLayoutParameters[i];
      const auto &argument = application->Arguments[i];
      if (!parameter.IsConst && argument.ArgumentKind == TypeArgumentSyntax::Kind::Type)
        substitutions[parameter.Name] = argument.Type;
    }
    contract = makeGenericValueContract(
        shape->NominalLayoutSyntax->substitute(substitutions), contract->Parameters);
    if (!contract) return nullptr;
    shape = shape->NominalLayoutOrigin;
  }
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
  if (auto *unary = dynamic_cast<UnaryExpr *>(expression);
      unary && unary->Op == TokenType::Ampersand && unary->ResolvedType &&
      unary->ResolvedType->isReference()) {
    auto source = queryGenericValueContract(unary->RHS.get());
    if (!source) return nullptr;
    auto result = std::make_shared<GenericValueContract>(*source);
    result->Type = TypeSyntax::morphology("&", source->Type, unary->Loc, unary->Loc);
    return result;
  }
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
  if (auto *method = dynamic_cast<MethodCallExpr *>(expression)) {
    auto *function = method->ResolvedFn;
    auto *declaration = function && function->TemplateOrigin ? function->TemplateOrigin : function;
    auto receiver = queryGenericValueContract(method->Object.get());
    auto type = method->Object->ResolvedType;
    auto shape = std::dynamic_pointer_cast<ShapeType>(type ? type->getSoulType() : nullptr);
    if (!declaration || !declaration->GenericParams.empty() ||
        !declaration->GenericReturnContract || !receiver || !shape || !shape->Decl)
      return nullptr;
    // Project the declaration's result type through the same exact nominal
    // receiver substitution used for fields. This describes only a type
    // view; actual result dependencies remain the call planner's concern.
    ShapeMember resultType;
    resultType.TypeSyntax = declaration->GenericReturnContract->Type;
    resultType.Type = resultType.TypeSyntax->toCanonicalString();
    resultType.Loc = declaration->Loc;
    return projectGenericMemberContract(receiver, shape->Decl, resultType);
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
        // This is source-contract projection after successful deduction, not
        // a second type inference pass. Match the complete formal structure
        // against the checked actual's source view, retaining opaque leaves.
        auto match = [&](auto &&self, TypeSyntaxPtr formal, TypeSyntaxPtr actual,
                         bool bind) -> bool {
          if (!formal || !actual) return !formal && !actual;
          if (bind && formal->NodeKind == TypeSyntax::Kind::Named &&
              !formal->NominalDeclaration && formal->Text == parameter.Name) {
            if (argument && !self(self, argument, actual, false)) return false;
            argument = actual;
            return true;
          }
          if (bind && formal->NodeKind == TypeSyntax::Kind::Named &&
              !formal->NominalDeclaration &&
              callableDeclarationGenericNames(declaration).count(formal->Text))
            return true; // The other binder is projected in its own iteration.
          if (formal->NodeKind != actual->NodeKind) return false;
          if (formal->NodeKind == TypeSyntax::Kind::Named &&
              (formal->NominalDeclaration || actual->NominalDeclaration)) {
            auto origin = [](ShapeDecl *decl) {
              return decl && decl->InstantiationTemplate ? decl->InstantiationTemplate : decl;
            };
            return origin(formal->NominalDeclaration) == origin(actual->NominalDeclaration);
          }
          if (formal->Text != actual->Text || formal->MemberName != actual->MemberName ||
              formal->PathSuffix != actual->PathSuffix || formal->IsPostfix != actual->IsPostfix ||
              formal->HasExplicitResult != actual->HasExplicitResult || formal->IsVariadic != actual->IsVariadic ||
              formal->Arguments.size() != actual->Arguments.size() ||
              formal->Elements.size() != actual->Elements.size() || formal->Fields.size() != actual->Fields.size() ||
              formal->ExtentArgument.toCanonicalString() != actual->ExtentArgument.toCanonicalString()) return false;
          if (!self(self, formal->Subject, actual->Subject, bind) ||
              !self(self, formal->Result, actual->Result, bind)) return false;
          for (size_t k = 0; k < formal->Arguments.size(); ++k) {
            const auto &f = formal->Arguments[k]; const auto &a = actual->Arguments[k];
            if (f.ArgumentKind != a.ArgumentKind || f.ConstantText != a.ConstantText ||
                !self(self, f.Type, a.Type, bind)) return false;
          }
          for (size_t k = 0; k < formal->Elements.size(); ++k)
            if (!self(self, formal->Elements[k], actual->Elements[k], bind)) return false;
          for (size_t k = 0; k < formal->Fields.size(); ++k)
            if (formal->Fields[k].Name != actual->Fields[k].Name ||
                !self(self, formal->Fields[k].Type, actual->Fields[k].Type, bind)) return false;
          return true;
        };
        for (size_t j = 0; j < declaration->Args.size() && j < call->Args.size(); ++j) {
          auto physical = synthesizePhysicalTypeObject(declaration->Args[j], false);
          auto contract = makeGenericValueContract(
              physical ? physical->toSyntax(declaration->Args[j].Loc, declaration->Args[j].Loc) : nullptr,
              callableDeclarationGenericNames(declaration));
          if (!contract) continue;
          auto formal = contract->Type;
          auto source = queryGenericValueContract(call->Args[j].get());
          auto candidate = source ? source->Type : call->Args[j]->ResolvedType
              ? call->Args[j]->ResolvedType->toSyntax(call->Loc, call->Loc) : nullptr;
          if (!candidate || !match(match, formal, candidate, true)) return nullptr;
        }
      }
      if (!argument) return nullptr;
      substitutions[parameter.Name] = argument;
      if (!parameter.Name.empty() && parameter.Name.front() == '\'')
        substitutions[parameter.Name.substr(1)] = argument;
    }
    auto resultType = declaration->GenericReturnContract
        ? declaration->GenericReturnContract->Type : declaration->ReturnTypeSyntax;
    return makeGenericValueContract(resultType->substitute(substitutions), parameters);
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

void Sema::refreshGenericSourceContracts(FunctionDecl *function) {
  if (!function || function->TemplateOrigin) return;
  const auto parameters = callableDeclarationGenericNames(function);
  if (parameters.empty()) return;
  // Imports may become available after initial declaration collection. This
  // refresh uses only original syntax and resolved lexical declaration IDs;
  // it never rechecks the function body or uses a specialization's types.
  for (auto &argument : function->Args) {
    auto syntax = argument.GenericContract ? argument.GenericContract->Type : argument.TypeSyntax;
    argument.GenericContract = makeGenericValueContract(syntax, parameters);
    if (!argument.IsRawPointer && !argument.IsUnique &&
        !argument.IsShared && !argument.IsReference && argument.GenericContract) {
      argument.IsAbstractWholeValue = argument.GenericContract->isWholeValue();
      if (argument.IsAbstractWholeValue) {
        argument.IsMorphicExempt = true;
        argument.Permission.MorphicExempt = true;
        argument.Stage0MorphicGenericRole = true;
        argument.Stage0GenericValueRole = false;
      }
    }
  }
  function->GenericReturnContract = makeGenericValueContract(
      function->GenericReturnContract ? function->GenericReturnContract->Type
                                      : function->ReturnTypeSyntax, parameters);
}

} // namespace toka
