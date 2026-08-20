// Copyright (c) 2025 YiZhonghua<zhyi@dpai.com>. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "toka/SemanticIndex.h"
#include "toka/Lexer.h"
#include "toka/PathUtils.h"
#include "toka/Type.h"
#include <algorithm>
#include <cctype>
#include <iomanip>
#include <set>
#include <sstream>
#include <type_traits>
#include <unordered_map>

namespace toka {
namespace {

std::string normalizeFile(const std::string &file) {
  return file.empty() ? "" : PathUtils::canonicalize(file);
}

uint64_t fnv1a(const std::string &value) {
  uint64_t hash = 14695981039346656037ULL;
  for (unsigned char ch : value) {
    hash ^= ch;
    hash *= 1099511628211ULL;
  }
  return hash;
}

std::string stableID(const std::string &key) {
  std::ostringstream stream;
  stream << "s:" << std::hex << std::setw(16) << std::setfill('0')
         << fnv1a(key);
  return stream.str();
}

std::string stripped(std::string name) { return Type::stripMorphology(name); }

std::string typeName(const std::shared_ptr<Type> &type,
                     const std::string &fallback = "") {
  return type ? type->toString() : fallback;
}

bool beforeOrEqual(const SemanticPosition &lhs, const SemanticPosition &rhs) {
  return lhs.Line < rhs.Line ||
         (lhs.Line == rhs.Line && lhs.Character <= rhs.Character);
}

bool contains(const SemanticRange &range, unsigned line, unsigned character) {
  SemanticPosition position{line, character};
  return beforeOrEqual(range.Start, position) &&
         beforeOrEqual(position, range.End);
}

llvm::json::Object positionJSON(const SemanticPosition &position) {
  return llvm::json::Object{{"line", position.Line},
                            {"character", position.Character}};
}

llvm::json::Object rangeJSON(const SemanticRange &range) {
  return llvm::json::Object{{"file", range.File},
                            {"start", positionJSON(range.Start)},
                            {"end", positionJSON(range.End)}};
}

llvm::json::Object locationJSON(const SemanticRange &range) {
  return llvm::json::Object{
      {"file", range.File},
      {"range", llvm::json::Object{{"start", positionJSON(range.Start)},
                                   {"end", positionJSON(range.End)}}}};
}

const char *effectName(EffectKind effect) {
  switch (effect) {
  case EffectKind::None:
    return "sync";
  case EffectKind::Async:
    return "async";
  case EffectKind::Wait:
    return "wait";
  }
  return "sync";
}

template <typename Arg>
std::string morphologyName(const Arg &arg) {
  if (arg.IsRawPointer)
    return "raw";
  if (arg.IsUnique)
    return "unique";
  if (arg.IsShared)
    return "shared";
  if (arg.IsReference)
    return "reference";
  return "value";
}

template <typename Arg>
std::string flowName(const Arg &arg) {
  if (arg.IsCeded)
    return "cede";
  if (arg.IsReference)
    return "borrow";
  if (arg.IsRawPointer)
    return "unsafe-raw";
  if (arg.IsShared)
    return "shared";
  if (arg.IsUnique)
    return "unique";
  return "value";
}

template <typename Arg>
SemanticParameterContract parameterContract(const Arg &arg) {
  SemanticParameterContract contract;
  contract.Name = stripped(arg.Name);
  contract.Type = typeName(arg.ResolvedType, arg.Type);
  contract.Morphology = morphologyName(arg);
  contract.Flow = flowName(arg);
  contract.PayloadWritable = arg.IsValueMutable;
  contract.PayloadBlocked = arg.IsValueBlocked;
  contract.HandleRebindable = arg.IsRebindable;
  contract.HandleBlocked = arg.IsRebindBlocked;
  contract.HandleNullable = arg.IsPointerNullable;
  contract.PayloadNullable = false;
  return contract;
}

template <typename Decl>
SemanticCallableContract callableContract(const Decl &function) {
  SemanticCallableContract contract;
  contract.Effect = effectName(function.Effect);
  contract.Variadic = function.IsVariadic;
  if constexpr (std::is_same_v<Decl, FunctionDecl>)
    contract.ReturnType = typeName(function.ResolvedReturnType,
                                   function.ReturnType);
  else
    contract.ReturnType = function.ReturnType;
  for (const auto &arg : function.Args)
    contract.Parameters.push_back(parameterContract(arg));
  if constexpr (std::is_same_v<Decl, FunctionDecl>)
    contract.ReturnDependencies = function.LifeDependencies;
  return contract;
}

SemanticFieldContract fieldContract(const ShapeMember &member) {
  SemanticFieldContract contract;
  if (member.IsRawPointer)
    contract.Morphology = "raw";
  else if (member.IsUnique)
    contract.Morphology = "unique";
  else if (member.IsShared)
    contract.Morphology = "shared";
  else if (member.IsReference)
    contract.Morphology = "reference";
  else
    contract.Morphology = "value";
  if (member.IsReference)
    contract.Flow = "borrow";
  else if (member.IsRawPointer)
    contract.Flow = "unsafe-raw";
  else if (member.IsShared)
    contract.Flow = "shared";
  else if (member.IsUnique)
    contract.Flow = "unique";
  else
    contract.Flow = "value";
  contract.PayloadWritable = member.IsValueMutable;
  contract.PayloadBlocked = member.IsValueBlocked;
  contract.HandleRebindable = member.IsRebindable;
  contract.HandleBlocked = member.IsRebindBlocked;
  contract.HandleNullable = member.IsPointerNullable;
  contract.PayloadNullable = false;
  return contract;
}

llvm::json::Object parameterContractJSON(
    const SemanticParameterContract &contract) {
  return llvm::json::Object{
      {"name", contract.Name},
      {"type", contract.Type},
      {"morphology", contract.Morphology},
      {"flow", contract.Flow},
      {"payloadWritable", contract.PayloadWritable},
      {"payloadBlocked", contract.PayloadBlocked},
      {"handleRebindable", contract.HandleRebindable},
      {"handleBlocked", contract.HandleBlocked},
      {"handleNullable", contract.HandleNullable},
      {"payloadNullable", contract.PayloadNullable}};
}

llvm::json::Object callableContractJSON(
    const SemanticCallableContract &contract) {
  llvm::json::Array parameters;
  for (const auto &parameter : contract.Parameters)
    parameters.emplace_back(parameterContractJSON(parameter));
  llvm::json::Array dependencies;
  for (const auto &dependency : contract.ReturnDependencies)
    dependencies.emplace_back(dependency);
  return llvm::json::Object{
      {"kind", "callable"},
      {"effect", contract.Effect},
      {"variadic", contract.Variadic},
      {"parameters", std::move(parameters)},
      {"return", llvm::json::Object{{"type", contract.ReturnType},
                                      {"dependencies", std::move(dependencies)}}}};
}

llvm::json::Object fieldContractJSON(const SemanticFieldContract &contract) {
  return llvm::json::Object{
      {"kind", "field"},
      {"morphology", contract.Morphology},
      {"flow", contract.Flow},
      {"payloadWritable", contract.PayloadWritable},
      {"payloadBlocked", contract.PayloadBlocked},
      {"handleRebindable", contract.HandleRebindable},
      {"handleBlocked", contract.HandleBlocked},
      {"handleNullable", contract.HandleNullable},
      {"payloadNullable", contract.PayloadNullable}};
}

llvm::json::Object symbolJSON(const SemanticSymbol &symbol) {
  llvm::json::Object result{{"id", symbol.ID},
                            {"name", symbol.Name},
                            {"kind", toString(symbol.Kind)},
                            {"detail", symbol.Detail},
                            {"type", symbol.Type},
                            {"container", symbol.ContainerID},
                            {"scope", symbol.ScopeID},
                            {"public", symbol.IsPublic},
                            {"documentation", symbol.Documentation},
                            {"declaration", locationJSON(symbol.Declaration)}};
  if (symbol.CallableContract)
    result["contract"] = callableContractJSON(*symbol.CallableContract);
  else if (symbol.FieldContract)
    result["contract"] = fieldContractJSON(*symbol.FieldContract);
  return result;
}

llvm::json::Object occurrenceJSON(const SemanticOccurrence &occurrence) {
  return llvm::json::Object{{"symbol", occurrence.SymbolID},
                            {"role", toString(occurrence.Role)},
                            {"declaration", occurrence.IsDeclaration},
                            {"location", locationJSON(occurrence.Range)}};
}

bool validIdentifier(const std::string &name) {
  if (name.empty() ||
      !(std::isalpha(static_cast<unsigned char>(name[0])) || name[0] == '_'))
    return false;
  return std::all_of(name.begin() + 1, name.end(), [](unsigned char ch) {
    return std::isalnum(ch) || ch == '_';
  });
}

} // namespace

class SemanticIndexBuilder {
public:
  SemanticIndexBuilder(const std::vector<Module *> &modules,
                       SourceManager &sourceManager)
      : Modules(modules), SM(sourceManager) {}

  SemanticIndex run() {
    collectModuleMap();
    collectGlobals();
    buildVisibleSymbols();
    visitBodies();
    collectTypeReferences();
    std::sort(Result.Occurrences.begin(), Result.Occurrences.end(),
              [](const SemanticOccurrence &lhs, const SemanticOccurrence &rhs) {
                if (lhs.Range.File != rhs.Range.File)
                  return lhs.Range.File < rhs.Range.File;
                if (lhs.Range.Start.Line != rhs.Range.Start.Line)
                  return lhs.Range.Start.Line < rhs.Range.Start.Line;
                if (lhs.Range.Start.Character != rhs.Range.Start.Character)
                  return lhs.Range.Start.Character < rhs.Range.Start.Character;
                if (lhs.IsDeclaration != rhs.IsDeclaration)
                  return lhs.IsDeclaration;
                return lhs.SymbolID < rhs.SymbolID;
              });
    return std::move(Result);
  }

private:
  struct ScopeFrame {
    std::string ID;
    std::map<std::string, std::string> Symbols;
  };

  const std::vector<Module *> &Modules;
  SourceManager &SM;
  SemanticIndex Result;
  Module *CurrentModule = nullptr;
  std::string CurrentContainer;
  unsigned ScopeSerial = 0;
  std::vector<ScopeFrame> Scopes;
  std::map<const ASTNode *, std::string> NodeSymbols;
  std::map<const FunctionDecl *, std::string> FunctionSymbols;
  std::map<const ExternDecl *, std::string> ExternSymbols;
  std::map<const ShapeDecl *, std::string> ShapeSymbols;
  std::map<std::string, Module *> ModuleByPath;
  std::map<Module *, std::map<std::string, std::vector<std::string>>>
      OwnSymbols;
  std::map<Module *, std::map<std::string, std::vector<std::string>>>
      VisibleSymbols;
  std::map<std::string, std::map<std::string, std::string>> FieldSymbols;
  std::set<std::string> OccurrenceKeys;
  mutable std::map<std::string, std::string> NormalizedFiles;
  mutable std::map<uint32_t, std::vector<std::string>> SourceLines;

  const std::string &normalizedFile(const std::string &file) const {
    auto found = NormalizedFiles.find(file);
    if (found != NormalizedFiles.end())
      return found->second;
    return NormalizedFiles.emplace(file, normalizeFile(file)).first->second;
  }

  SemanticRange rangeFor(SourceLocation loc, const std::string &name) const {
    SemanticRange range;
    if (loc.isInvalid())
      return range;
    FullSourceLoc full = SM.getFullSourceLoc(loc);
    if (!full.isValid())
      return range;
    range.File = normalizedFile(full.FileName);
    unsigned line = full.Line > 0 ? full.Line - 1 : 0;
    unsigned column = full.Column > 0 ? full.Column - 1 : 0;
    std::string lineData = SM.getLineData(loc);
    std::string plainName = stripped(name);
    if (!plainName.empty() && column < lineData.size() &&
        lineData.compare(column, plainName.size(), plainName) != 0) {
      size_t found = lineData.find(plainName, column);
      if (found == std::string::npos)
        found = lineData.find(plainName);
      if (found != std::string::npos)
        column = static_cast<unsigned>(found);
    }
    range.Start = {line, column};
    range.End = {line, column + static_cast<unsigned>(plainName.size())};
    return range;
  }

  std::string documentation(SourceLocation loc) const {
    if (loc.isInvalid())
      return {};
    FullSourceLoc full = SM.getFullSourceLoc(loc);
    if (!full.isValid() || full.Line <= 1)
      return {};
    uint32_t fileID = SM.getFileID(loc);
    auto cached = SourceLines.find(fileID);
    if (cached == SourceLines.end()) {
      std::string_view buffer = SM.getBufferData(loc);
      std::vector<std::string> lines;
      size_t start = 0;
      while (start <= buffer.size()) {
        size_t end = buffer.find('\n', start);
        lines.emplace_back(buffer.substr(start, end - start));
        if (end == std::string_view::npos)
          break;
        start = end + 1;
      }
      cached = SourceLines.emplace(fileID, std::move(lines)).first;
    }
    const std::vector<std::string> &lines = cached->second;
    size_t index = std::min<size_t>(full.Line - 1, lines.size());
    std::vector<std::string> docs;
    while (index > 0) {
      std::string line = lines[--index];
      size_t first = line.find_first_not_of(" \t");
      if (first == std::string::npos) {
        if (docs.empty())
          continue;
        break;
      }
      if (line.compare(first, 3, "///") != 0)
        break;
      std::string text = line.substr(first + 3);
      if (!text.empty() && text.front() == ' ')
        text.erase(text.begin());
      docs.push_back(std::move(text));
    }
    std::reverse(docs.begin(), docs.end());
    std::string result;
    for (const std::string &line : docs) {
      if (!result.empty())
        result += '\n';
      result += line;
    }
    return result;
  }

  std::string addSymbol(const ASTNode *node, SourceLocation loc,
                        std::string name, SemanticSymbolKind kind,
                        std::string detail, std::string type,
                        std::string container, std::string scope,
                        bool isPublic,
                        std::optional<SemanticCallableContract> callable =
                            std::nullopt,
                        std::optional<SemanticFieldContract> field =
                            std::nullopt) {
    name = stripped(std::move(name));
    SemanticRange declaration = rangeFor(loc, name);
    std::string key = declaration.File + ":" +
                      std::to_string(declaration.Start.Line) + ":" +
                      std::to_string(declaration.Start.Character) + ":" +
                      toString(kind) + ":" + name + ":" + container;
    std::string id = stableID(key);
    SemanticSymbol symbol;
    symbol.ID = id;
    symbol.Name = name;
    symbol.Kind = kind;
    symbol.Detail = std::move(detail);
    symbol.Type = std::move(type);
    symbol.ContainerID = std::move(container);
    symbol.ScopeID = std::move(scope);
    symbol.Documentation = documentation(loc);
    symbol.IsPublic = isPublic;
    symbol.Declaration = declaration;
    symbol.CallableContract = std::move(callable);
    symbol.FieldContract = std::move(field);
    Result.Symbols[id] = std::move(symbol);
    if (node)
      NodeSymbols[node] = id;
    addOccurrence(id, SemanticReferenceRole::Declaration, declaration, true);
    return id;
  }

  void addOccurrence(const std::string &id, SemanticReferenceRole role,
                     const SemanticRange &range, bool declaration = false) {
    if (id.empty() || range.File.empty())
      return;
    std::string key =
        id + ":" + range.File + ":" + std::to_string(range.Start.Line) + ":" +
        std::to_string(range.Start.Character) + ":" + toString(role);
    if (!OccurrenceKeys.insert(key).second)
      return;
    Result.Occurrences.push_back({id, role, range, declaration});
  }

  void addOwn(Module *module, const std::string &name, const std::string &id) {
    OwnSymbols[module][stripped(name)].push_back(id);
  }

  std::string functionDetail(const FunctionDecl &function) const {
    std::string result = "fn " + function.Name;
    if (!function.GenericParams.empty()) {
      result += "<";
      for (size_t i = 0; i < function.GenericParams.size(); ++i) {
        if (i)
          result += ", ";
        result += function.GenericParams[i].Name;
      }
      result += ">";
    }
    result += "(";
    for (size_t i = 0; i < function.Args.size(); ++i) {
      if (i)
        result += ", ";
      result += stripped(function.Args[i].Name) + ": " +
                typeName(function.Args[i].ResolvedType, function.Args[i].Type);
    }
    result +=
        ") -> " + typeName(function.ResolvedReturnType, function.ReturnType);
    return result;
  }

  std::string functionSymbol(const FunctionDecl *function) const {
    if (!function)
      return {};
    auto found = FunctionSymbols.find(function);
    if (found != FunctionSymbols.end())
      return found->second;
    if (function->TemplateOrigin) {
      found = FunctionSymbols.find(function->TemplateOrigin);
      if (found != FunctionSymbols.end())
        return found->second;
    }
    return {};
  }

  std::string externDetail(const ExternDecl &function) const {
    std::string result = "extern fn " + function.Name + "(";
    for (size_t i = 0; i < function.Args.size(); ++i) {
      if (i)
        result += ", ";
      result += stripped(function.Args[i].Name) + ": " +
                typeName(function.Args[i].ResolvedType, function.Args[i].Type);
    }
    result += ") -> " + function.ReturnType;
    return result;
  }

  void collectModuleMap() {
    for (Module *module : Modules) {
      if (!module)
        continue;
      ModuleByPath[normalizedFile(module->ResolvedPath)] = module;
      ModuleByPath[normalizedFile(module->SourcePath)] = module;
      std::vector<std::string> imports;
      for (const auto &import : module->Imports)
        if (!import->ResolvedPath.empty())
          imports.push_back(normalizedFile(import->ResolvedPath));
      Result.ModuleImports[normalizedFile(module->SourcePath)] =
          std::move(imports);
    }
  }

  void collectFunction(Module *module, FunctionDecl *function,
                       SemanticSymbolKind kind, const std::string &container,
                       bool isPublic) {
    std::string id =
        addSymbol(function, function->Loc, function->Name, kind,
                  functionDetail(*function),
                  typeName(function->ResolvedReturnType, function->ReturnType),
                  container, container, isPublic, callableContract(*function));
    FunctionSymbols[function] = id;
    addOwn(module, function->Name, id);
  }

  void collectGlobals() {
    for (Module *module : Modules) {
      if (!module)
        continue;
      std::string moduleScope = "module:" + normalizedFile(module->SourcePath);
      for (const auto &alias : module->TypeAliases) {
        std::string id = addSymbol(
            alias.get(), alias->Loc, alias->Name, SemanticSymbolKind::TypeAlias,
            "alias " + alias->Name + " = " + alias->TargetType,
            alias->TargetType, "", moduleScope, alias->IsPub);
        addOwn(module, alias->Name, id);
      }
      for (const auto &shape : module->Shapes) {
        std::string id = addSymbol(
            shape.get(), shape->Loc, shape->Name, SemanticSymbolKind::Shape,
            "shape " + shape->Name, shape->Name, "", moduleScope, shape->IsPub);
        ShapeSymbols[shape.get()] = id;
        addOwn(module, shape->Name, id);
        for (const ShapeMember &member : shape->Members) {
          SemanticSymbolKind memberKind = shape->Kind == ShapeKind::Enum
                                              ? SemanticSymbolKind::EnumVariant
                                              : SemanticSymbolKind::Field;
          std::string memberID = addSymbol(
              nullptr, member.Loc, member.Name, memberKind,
              member.Name + ": " + member.Type,
              typeName(member.ResolvedType, member.Type), id, id, shape->IsPub,
              std::nullopt, fieldContract(member));
          FieldSymbols[shape->Name][member.Name] = memberID;
        }
      }
      for (const auto &trait : module->Traits) {
        std::string traitID = addSymbol(
            trait.get(), trait->Loc, trait->Name, SemanticSymbolKind::Trait,
            "trait " + trait->Name, trait->Name, "", moduleScope, trait->IsPub);
        addOwn(module, trait->Name, traitID);
        for (const auto &method : trait->Methods)
          collectFunction(module, method.get(), SemanticSymbolKind::Method,
                          traitID, trait->IsPub);
      }
      for (const auto &external : module->Externs) {
        std::string id = addSymbol(
            external.get(), external->Loc, external->Name,
            SemanticSymbolKind::ExternFunction, externDetail(*external),
            external->ReturnType, "", moduleScope, false,
            callableContract(*external));
        ExternSymbols[external.get()] = id;
        addOwn(module, external->Name, id);
      }
      for (const auto &function : module->Functions)
        collectFunction(module, function.get(), SemanticSymbolKind::Function,
                        "", function->IsPub);
      for (const auto &impl : module->Impls) {
        std::string container;
        auto shape = OwnSymbols[module].find(impl->TypeName);
        if (shape != OwnSymbols[module].end() && !shape->second.empty())
          container = shape->second.front();
        for (const auto &method : impl->Methods)
          collectFunction(module, method.get(), SemanticSymbolKind::Method,
                          container, false);
      }
      for (const auto &global : module->Globals) {
        auto *variable = dynamic_cast<VariableDecl *>(global.get());
        if (!variable)
          continue;
        std::string name = stripped(variable->Name);
        std::string type = typeName(variable->ResolvedType, variable->TypeName);
        std::string id =
            addSymbol(variable, variable->Loc, name, SemanticSymbolKind::Global,
                      "global " + name + (type.empty() ? "" : ": " + type),
                      type, "", moduleScope, variable->IsPub);
        addOwn(module, name, id);
      }
    }
  }

  void buildVisibleSymbols() {
    for (Module *module : Modules) {
      if (!module)
        continue;
      VisibleSymbols[module] = OwnSymbols[module];
      for (const auto &import : module->Imports) {
        auto target = ModuleByPath.find(normalizedFile(import->ResolvedPath));
        if (target == ModuleByPath.end())
          continue;
        const auto &targetSymbols = OwnSymbols[target->second];
        if (import->Items.empty()) {
          if (!import->Alias.empty())
            continue;
          for (const auto &[name, ids] : targetSymbols)
            for (const std::string &id : ids)
              if (Result.Symbols.at(id).IsPublic)
                VisibleSymbols[module][name].push_back(id);
          continue;
        }
        for (const ImportItem &item : import->Items) {
          if (item.Symbol == "*") {
            for (const auto &[name, ids] : targetSymbols)
              for (const std::string &id : ids)
                if (Result.Symbols.at(id).IsPublic)
                  VisibleSymbols[module][name].push_back(id);
            continue;
          }
          auto found = targetSymbols.find(item.Symbol);
          if (found == targetSymbols.end())
            continue;
          std::string localName = item.Alias.empty() ? item.Symbol : item.Alias;
          VisibleSymbols[module][localName].insert(
              VisibleSymbols[module][localName].end(), found->second.begin(),
              found->second.end());
        }
      }
    }
  }

  void pushScope(const std::string &prefix, SourceLocation loc = {}) {
    std::string key = prefix + ":" + std::to_string(ScopeSerial++);
    if (loc.isValid()) {
      FullSourceLoc full = SM.getFullSourceLoc(loc);
      key +=
          ":" + std::to_string(full.Line) + ":" + std::to_string(full.Column);
    }
    Scopes.push_back({stableID("scope:" + key), {}});
  }

  void popScope() { Scopes.pop_back(); }

  void define(const std::string &name, const std::string &id) {
    if (!Scopes.empty())
      Scopes.back().Symbols[stripped(name)] = id;
  }

  std::string lookup(const std::string &name) const {
    std::string plain = stripped(name);
    for (auto scope = Scopes.rbegin(); scope != Scopes.rend(); ++scope) {
      auto found = scope->Symbols.find(plain);
      if (found != scope->Symbols.end())
        return found->second;
    }
    auto module = VisibleSymbols.find(CurrentModule);
    if (module != VisibleSymbols.end()) {
      auto found = module->second.find(plain);
      if (found != module->second.end() && !found->second.empty())
        return found->second.front();
    }
    return {};
  }

  std::string addLocal(const ASTNode *node, SourceLocation loc,
                       const std::string &name, SemanticSymbolKind kind,
                       const std::string &type) {
    std::string scope = Scopes.empty() ? "" : Scopes.back().ID;
    std::string detail = std::string(toString(kind)) + " " + stripped(name);
    if (!type.empty())
      detail += ": " + type;
    std::string id = addSymbol(node, loc, name, kind, detail, type,
                               CurrentContainer, scope, false);
    define(name, id);
    return id;
  }

  void visitBodies() {
    for (Module *module : Modules) {
      if (!module)
        continue;
      CurrentModule = module;
      ScopeSerial = 0;
      Scopes.clear();
      pushScope("module:" + normalizedFile(module->SourcePath), module->Loc);
      for (const auto &[name, ids] : VisibleSymbols[module])
        if (!ids.empty())
          define(name, ids.front());
      for (const auto &global : module->Globals)
        if (auto *variable = dynamic_cast<VariableDecl *>(global.get()))
          visitExpr(variable->Init.get(), SemanticReferenceRole::Read);
      for (const auto &function : module->Functions)
        visitFunction(function.get());
      for (const auto &impl : module->Impls)
        for (const auto &method : impl->Methods)
          visitFunction(method.get());
      for (const auto &trait : module->Traits)
        for (const auto &method : trait->Methods)
          visitFunction(method.get());
      popScope();
    }
  }

  void visitFunction(FunctionDecl *function) {
    auto found = FunctionSymbols.find(function);
    if (found == FunctionSymbols.end())
      return;
    std::string savedContainer = CurrentContainer;
    CurrentContainer = found->second;
    pushScope("function:" + CurrentContainer, function->Loc);
    for (auto &argument : function->Args) {
      std::string type = typeName(argument.ResolvedType, argument.Type);
      addLocal(nullptr, argument.Loc, argument.Name,
               SemanticSymbolKind::Parameter, type);
      visitExpr(argument.DefaultValue.get(), SemanticReferenceRole::Read);
    }
    visitStmt(function->Body.get(), false);
    popScope();
    CurrentContainer = std::move(savedContainer);
  }

  void visitStmt(const Stmt *statement, bool createScope = true) {
    if (!statement)
      return;
    if (auto *block = dynamic_cast<const BlockStmt *>(statement)) {
      if (createScope)
        pushScope("block:" + CurrentContainer, block->Loc);
      for (const auto &child : block->Statements)
        visitStmt(child.get());
      if (createScope)
        popScope();
      return;
    }
    if (auto *value = dynamic_cast<const InitBlockStmt *>(statement)) {
      addOccurrence(lookup(value->PlaceName), SemanticReferenceRole::Write,
                    rangeFor(value->PlaceLoc, value->PlaceName));
      visitStmt(value->Body.get());
      return;
    }
    if (auto *ret = dynamic_cast<const ReturnStmt *>(statement)) {
      visitExpr(ret->ReturnValue.get(), SemanticReferenceRole::Read);
      return;
    }
    if (auto *expr = dynamic_cast<const ExprStmt *>(statement)) {
      visitExpr(expr->Expression.get(), SemanticReferenceRole::Read);
      return;
    }
    if (auto *decl = dynamic_cast<const VariableDecl *>(statement)) {
      visitExpr(decl->Init.get(), SemanticReferenceRole::Read);
      addLocal(decl, decl->Loc, decl->Name, SemanticSymbolKind::Variable,
               typeName(decl->ResolvedType, decl->TypeName));
      return;
    }
    if (auto *decl = dynamic_cast<const DestructuringDecl *>(statement)) {
      visitExpr(decl->Init.get(), SemanticReferenceRole::Read);
      for (const auto &variable : decl->Variables)
        if (!variable.IsWildcard)
          addLocal(nullptr, decl->Loc, variable.Name,
                   SemanticSymbolKind::Variable, "");
      return;
    }
    if (auto *value = dynamic_cast<const DeleteStmt *>(statement)) {
      visitExpr(value->Expression.get(), SemanticReferenceRole::Write);
      return;
    }
    if (auto *value = dynamic_cast<const FreeStmt *>(statement)) {
      visitExpr(value->Expression.get(), SemanticReferenceRole::Write);
      visitExpr(value->Count.get(), SemanticReferenceRole::Read);
      return;
    }
    if (auto *value = dynamic_cast<const UnsafeStmt *>(statement)) {
      visitStmt(value->Statement.get());
      return;
    }
    if (auto *value = dynamic_cast<const GuardBindStmt *>(statement)) {
      visitExpr(value->Target.get(), SemanticReferenceRole::Read);
      visitStmt(value->ElseBody.get());
      return;
    }
    if (auto *expr = dynamic_cast<const Expr *>(statement))
      visitExpr(expr, SemanticReferenceRole::Read);
  }

  void visitPattern(const MatchArm::Pattern *pattern) {
    if (!pattern)
      return;
    if (pattern->PatternKind == MatchArm::Pattern::Variable &&
        pattern->Name != "_")
      addLocal(pattern, pattern->Loc, pattern->Name,
               SemanticSymbolKind::Pattern, "");
    for (const auto &subpattern : pattern->SubPatterns)
      visitPattern(subpattern.get());
  }

  std::string fieldFor(const MemberExpr &member) const {
    std::shared_ptr<Type> type =
        member.Object ? member.Object->ResolvedType : nullptr;
    while (type &&
           (type->isPointer() || type->isReference() || type->isSmartPointer()))
      type = type->getPointeeType();
    std::string shapeName = type ? type->getSoulName() : "";
    auto shape = FieldSymbols.find(shapeName);
    if (shape == FieldSymbols.end())
      return {};
    auto field = shape->second.find(member.Member);
    return field == shape->second.end() ? "" : field->second;
  }

  void visitExpr(const Expr *expression, SemanticReferenceRole role) {
    if (!expression)
      return;
    if (auto *variable = dynamic_cast<const VariableExpr *>(expression)) {
      std::string id = lookup(variable->Name);
      addOccurrence(id, role, rangeFor(variable->Loc, variable->Name));
      return;
    }
    if (auto *binary = dynamic_cast<const BinaryExpr *>(expression)) {
      bool assignment =
          binary->Op == "=" || binary->Op == "+=" || binary->Op == "-=" ||
          binary->Op == "*=" || binary->Op == "/=" || binary->Op == "%=" ||
          binary->Op == "&=" || binary->Op == "|=" || binary->Op == "^=" ||
          binary->Op == "<<=" || binary->Op == ">>=";
      visitExpr(binary->RHS.get(), SemanticReferenceRole::Read);
      visitExpr(binary->LHS.get(), assignment ? SemanticReferenceRole::Write
                                              : SemanticReferenceRole::Read);
      return;
    }
    if (auto *call = dynamic_cast<const CallExpr *>(expression)) {
      std::string id;
      if (call->ResolvedFn)
        id = functionSymbol(call->ResolvedFn);
      else if (call->ResolvedExtern)
        id = ExternSymbols[call->ResolvedExtern];
      else if (call->ResolvedShape)
        id = ShapeSymbols[call->ResolvedShape];
      else
        id = lookup(call->Callee);
      const std::string &sourceName =
          call->OriginalCallee.empty() ? call->Callee : call->OriginalCallee;
      addOccurrence(id, SemanticReferenceRole::Call,
                    rangeFor(call->Loc, sourceName));
      for (const auto &argument : call->Args)
        visitExpr(argument.get(), SemanticReferenceRole::Read);
      return;
    }
    if (auto *call = dynamic_cast<const MethodCallExpr *>(expression)) {
      visitExpr(call->Object.get(), SemanticReferenceRole::Read);
      std::string id = functionSymbol(call->ResolvedFn);
      addOccurrence(id, SemanticReferenceRole::Call,
                    rangeFor(call->Loc, call->Method));
      for (const auto &argument : call->Args)
        visitExpr(argument.get(), SemanticReferenceRole::Read);
      return;
    }
    if (auto *member = dynamic_cast<const MemberExpr *>(expression)) {
      visitExpr(member->Object.get(), role);
      if (member->IsTaskStart)
        return;
      addOccurrence(fieldFor(*member), role,
                    rangeFor(member->Loc, member->Member));
      return;
    }
    if (auto *value = dynamic_cast<const IfExpr *>(expression)) {
      visitExpr(value->Condition.get(), SemanticReferenceRole::Read);
      visitStmt(value->Then.get());
      visitStmt(value->Else.get());
      return;
    }
    if (auto *value = dynamic_cast<const GuardExpr *>(expression)) {
      visitExpr(value->Condition.get(), SemanticReferenceRole::Read);
      visitStmt(value->Then.get());
      visitStmt(value->Else.get());
      return;
    }
    if (auto *value = dynamic_cast<const LoopExpr *>(expression)) {
      visitExpr(value->Condition.get(), SemanticReferenceRole::Read);
      visitStmt(value->Body.get());
      return;
    }
    if (auto *value = dynamic_cast<const ForExpr *>(expression)) {
      visitExpr(value->Collection.get(), SemanticReferenceRole::Read);
      pushScope("for:" + CurrentContainer, value->Loc);
      addLocal(value, value->Loc, value->VarName, SemanticSymbolKind::Variable,
               value->IterElementType);
      visitStmt(value->Body.get(), false);
      popScope();
      visitStmt(value->ElseBody.get());
      return;
    }
    if (auto *value = dynamic_cast<const MatchExpr *>(expression)) {
      visitExpr(value->Target.get(), SemanticReferenceRole::Read);
      for (const auto &arm : value->Arms) {
        pushScope("match:" + CurrentContainer,
                  arm->Pat ? arm->Pat->Loc : value->Loc);
        visitPattern(arm->Pat.get());
        visitExpr(arm->Guard.get(), SemanticReferenceRole::Read);
        visitStmt(arm->Body.get(), false);
        popScope();
      }
      return;
    }
    if (auto *value = dynamic_cast<const ClosureExpr *>(expression)) {
      pushScope("closure:" + CurrentContainer, value->Loc);
      visitStmt(value->Body.get(), false);
      popScope();
      return;
    }
    if (auto *value = dynamic_cast<const ArrayIndexExpr *>(expression)) {
      visitExpr(value->Array.get(), role);
      for (const auto &index : value->Indices)
        visitExpr(index.get(), SemanticReferenceRole::Read);
    } else if (auto *value = dynamic_cast<const DereferenceExpr *>(expression))
      visitExpr(value->Expression.get(), role);
    else if (auto *value = dynamic_cast<const AddressOfExpr *>(expression))
      visitExpr(value->Expression.get(), role);
    else if (auto *value = dynamic_cast<const UnaryExpr *>(expression))
      visitExpr(value->RHS.get(), role);
    else if (auto *value = dynamic_cast<const PostfixExpr *>(expression))
      visitExpr(value->LHS.get(), SemanticReferenceRole::Write);
    else if (auto *value =
                 dynamic_cast<const UnwrapPropagationExpr *>(expression))
      visitExpr(value->Base.get(), role);
    else if (auto *value = dynamic_cast<const CastExpr *>(expression))
      visitExpr(value->Expression.get(), role);
    else if (auto *value = dynamic_cast<const UnsafeExpr *>(expression))
      visitExpr(value->Expression.get(), role);
    else if (auto *value = dynamic_cast<const AwaitExpr *>(expression))
      visitExpr(value->Expression.get(), role);
    else if (auto *value = dynamic_cast<const WaitExpr *>(expression))
      visitExpr(value->Expression.get(), role);
    else if (auto *value = dynamic_cast<const StartExpr *>(expression))
      visitExpr(value->Expression.get(), role);
    else if (auto *value = dynamic_cast<const CedeExpr *>(expression))
      visitExpr(value->Value.get(), SemanticReferenceRole::Write);
    else if (auto *value = dynamic_cast<const PassExpr *>(expression))
      visitExpr(value->Value.get(), role);
    else if (auto *value = dynamic_cast<const SpreadExpr *>(expression))
      visitExpr(value->Base.get(), role);
    else if (auto *value = dynamic_cast<const ElisionExpr *>(expression))
      visitExpr(value->Target.get(), role);
    else if (auto *value = dynamic_cast<const BreakExpr *>(expression))
      visitExpr(value->Value.get(), role);
    else if (auto *value = dynamic_cast<const ArrayExpr *>(expression))
      for (const auto &element : value->Elements)
        visitExpr(element.get(), SemanticReferenceRole::Read);
    else if (auto *value =
                 dynamic_cast<const RepeatedArrayExpr *>(expression)) {
      visitExpr(value->Value.get(), SemanticReferenceRole::Read);
      visitExpr(value->Count.get(), SemanticReferenceRole::Read);
    } else if (auto *value =
                   dynamic_cast<const AnonymousRecordExpr *>(expression))
      for (const auto &field : value->Fields)
        visitExpr(field.second.get(), SemanticReferenceRole::Read);
    else if (auto *value = dynamic_cast<const InitStructExpr *>(expression)) {
      auto shape = VisibleSymbols[CurrentModule].find(value->ShapeName);
      if (shape != VisibleSymbols[CurrentModule].end() &&
          !shape->second.empty())
        addOccurrence(shape->second.front(), SemanticReferenceRole::Call,
                      rangeFor(value->Loc, value->ShapeName));
      for (const auto &field : value->Members)
        visitExpr(field.second.get(), SemanticReferenceRole::Read);
    } else if (auto *value = dynamic_cast<const AllocExpr *>(expression)) {
      visitExpr(value->Initializer.get(), SemanticReferenceRole::Read);
      visitExpr(value->ArraySize.get(), SemanticReferenceRole::Read);
    } else if (auto *value = dynamic_cast<const NewExpr *>(expression)) {
      visitExpr(value->Initializer.get(), SemanticReferenceRole::Read);
      visitExpr(value->ArraySize.get(), SemanticReferenceRole::Read);
    } else if (auto *value = dynamic_cast<const ArrayInitExpr *>(expression)) {
      visitExpr(value->Initializer.get(), SemanticReferenceRole::Read);
      visitExpr(value->ArraySize.get(), SemanticReferenceRole::Read);
    }
  }

  bool hasOccurrenceAt(const SemanticRange &range) const {
    return std::any_of(
        Result.Occurrences.begin(), Result.Occurrences.end(),
        [&](const SemanticOccurrence &occurrence) {
          return occurrence.Range.File == range.File &&
                 occurrence.Range.Start.Line == range.Start.Line &&
                 occurrence.Range.Start.Character == range.Start.Character;
        });
  }

  void collectTypeReferences() {
    for (Module *module : Modules) {
      if (!module || module->IsInterface)
        continue;
      CurrentModule = module;
      SourceLocation start = SM.loadFile(module->SourcePath);
      if (start.isInvalid())
        continue;
      std::string source(SM.getBufferData(start));
      Lexer lexer(source.c_str(), start);
      for (const Token &token : lexer.tokenize()) {
        if (token.Kind != TokenType::Identifier)
          continue;
        auto visible = VisibleSymbols[module].find(token.Text);
        if (visible == VisibleSymbols[module].end())
          continue;
        auto symbol =
            std::find_if(visible->second.begin(), visible->second.end(),
                         [&](const std::string &id) {
                           SemanticSymbolKind kind = Result.Symbols.at(id).Kind;
                           return kind == SemanticSymbolKind::Shape ||
                                  kind == SemanticSymbolKind::Trait ||
                                  kind == SemanticSymbolKind::TypeAlias;
                         });
        if (symbol == visible->second.end())
          continue;
        SemanticRange range = rangeFor(token.Loc, token.Text);
        if (!hasOccurrenceAt(range))
          addOccurrence(*symbol, SemanticReferenceRole::Type, range);
      }
    }
  }
};

const char *toString(SemanticSymbolKind kind) {
  switch (kind) {
  case SemanticSymbolKind::Module:
    return "module";
  case SemanticSymbolKind::Function:
    return "function";
  case SemanticSymbolKind::Method:
    return "method";
  case SemanticSymbolKind::ExternFunction:
    return "extern_function";
  case SemanticSymbolKind::Shape:
    return "shape";
  case SemanticSymbolKind::Trait:
    return "trait";
  case SemanticSymbolKind::TypeAlias:
    return "type_alias";
  case SemanticSymbolKind::Field:
    return "field";
  case SemanticSymbolKind::EnumVariant:
    return "enum_variant";
  case SemanticSymbolKind::Global:
    return "global";
  case SemanticSymbolKind::Parameter:
    return "parameter";
  case SemanticSymbolKind::Variable:
    return "variable";
  case SemanticSymbolKind::Pattern:
    return "pattern";
  }
  return "variable";
}

const char *toString(SemanticReferenceRole role) {
  switch (role) {
  case SemanticReferenceRole::Declaration:
    return "declaration";
  case SemanticReferenceRole::Read:
    return "read";
  case SemanticReferenceRole::Write:
    return "write";
  case SemanticReferenceRole::Call:
    return "call";
  case SemanticReferenceRole::Type:
    return "type";
  case SemanticReferenceRole::Import:
    return "import";
  }
  return "read";
}

SemanticIndex SemanticIndex::build(const std::vector<Module *> &modules,
                                   SourceManager &sourceManager) {
  return SemanticIndexBuilder(modules, sourceManager).run();
}

const SemanticOccurrence *
SemanticIndex::occurrenceAt(const std::string &file, unsigned line,
                            unsigned character) const {
  std::string normalized = normalizeFile(file);
  const SemanticOccurrence *best = nullptr;
  for (const SemanticOccurrence &occurrence : Occurrences) {
    if (normalizeFile(occurrence.Range.File) != normalized ||
        !contains(occurrence.Range, line, character))
      continue;
    if (!best ||
        (occurrence.Range.End.Character - occurrence.Range.Start.Character <
         best->Range.End.Character - best->Range.Start.Character))
      best = &occurrence;
  }
  return best;
}

const SemanticSymbol *SemanticIndex::symbol(const std::string &id) const {
  auto found = Symbols.find(id);
  return found == Symbols.end() ? nullptr : &found->second;
}

std::vector<const SemanticOccurrence *>
SemanticIndex::references(const std::string &symbolID,
                          bool includeDeclaration) const {
  std::vector<const SemanticOccurrence *> result;
  for (const SemanticOccurrence &occurrence : Occurrences)
    if (occurrence.SymbolID == symbolID &&
        (includeDeclaration || !occurrence.IsDeclaration))
      result.push_back(&occurrence);
  return result;
}

std::vector<const SemanticSymbol *>
SemanticIndex::documentSymbols(const std::string &file) const {
  std::vector<const SemanticSymbol *> result;
  std::string normalized = normalizeFile(file);
  for (const auto &[id, symbol] : Symbols)
    if (normalizeFile(symbol.Declaration.File) == normalized)
      result.push_back(&symbol);
  return result;
}

std::vector<const SemanticSymbol *> SemanticIndex::workspaceSymbols() const {
  std::vector<const SemanticSymbol *> result;
  for (const auto &[id, symbol] : Symbols)
    result.push_back(&symbol);
  return result;
}

std::vector<const SemanticSymbol *>
SemanticIndex::completions(const std::string &file, unsigned line,
                           unsigned character) const {
  std::string normalized = normalizeFile(file);
  std::string container;
  SemanticPosition position{line, character};
  for (const auto &[id, symbol] : Symbols) {
    if (normalizeFile(symbol.Declaration.File) != normalized ||
        (symbol.Kind != SemanticSymbolKind::Function &&
         symbol.Kind != SemanticSymbolKind::Method) ||
        !beforeOrEqual(symbol.Declaration.Start, position))
      continue;
    if (container.empty() ||
        beforeOrEqual(Symbols.at(container).Declaration.Start,
                      symbol.Declaration.Start))
      container = id;
  }
  std::map<std::string, const SemanticSymbol *> byName;
  for (const auto &[id, symbol] : Symbols) {
    bool sameFile = normalizeFile(symbol.Declaration.File) == normalized;
    bool localToContainer =
        !container.empty() && symbol.ContainerID == container;
    bool global = symbol.ContainerID.empty() ||
                  symbol.Kind == SemanticSymbolKind::Field ||
                  symbol.Kind == SemanticSymbolKind::Method;
    if ((sameFile && (global || localToContainer)) || symbol.IsPublic)
      byName[symbol.Name] = &symbol;
  }
  std::vector<const SemanticSymbol *> result;
  for (const auto &[name, symbol] : byName)
    result.push_back(symbol);
  return result;
}

llvm::json::Value SemanticIndex::toJSON() const {
  llvm::json::Array symbols;
  for (const auto &[id, symbol] : Symbols)
    symbols.emplace_back(symbolJSON(symbol));
  llvm::json::Array occurrences;
  for (const SemanticOccurrence &occurrence : Occurrences)
    occurrences.emplace_back(occurrenceJSON(occurrence));
  llvm::json::Array modules;
  for (const auto &[file, imports] : ModuleImports) {
    llvm::json::Array importValues;
    for (const std::string &import : imports)
      importValues.emplace_back(import);
    modules.emplace_back(llvm::json::Object{
        {"file", file}, {"imports", std::move(importValues)}});
  }
  return llvm::json::Object{{"schema", TOKA_SEMANTIC_INDEX_SCHEMA},
                            {"version", TOKA_SEMANTIC_INDEX_VERSION},
                            {"modules", std::move(modules)},
                            {"symbols", std::move(symbols)},
                            {"occurrences", std::move(occurrences)}};
}

llvm::json::Value SemanticIndex::queryJSON(const std::string &query,
                                           const std::string &file,
                                           unsigned line, unsigned character,
                                           const std::string &newName) const {
  llvm::json::Object response{
      {"schema", "toka.semantic-query"}, {"version", 1}, {"query", query}};
  const SemanticOccurrence *occurrence = occurrenceAt(file, line, character);
  const SemanticSymbol *target =
      occurrence ? symbol(occurrence->SymbolID) : nullptr;
  if (query == "symbolAt" || query == "hover") {
    response["result"] = target ? llvm::json::Value(symbolJSON(*target))
                                : llvm::json::Value(nullptr);
  } else if (query == "definition") {
    response["result"] =
        target ? llvm::json::Value(locationJSON(target->Declaration))
               : llvm::json::Value(nullptr);
  } else if (query == "references") {
    llvm::json::Array result;
    if (target)
      for (const SemanticOccurrence *reference : references(target->ID))
        result.emplace_back(occurrenceJSON(*reference));
    response["result"] = std::move(result);
  } else if (query == "completion") {
    llvm::json::Array result;
    for (const SemanticSymbol *candidate : completions(file, line, character))
      result.emplace_back(symbolJSON(*candidate));
    response["result"] = std::move(result);
  } else if (query == "documentSymbols") {
    llvm::json::Array result;
    for (const SemanticSymbol *candidate : documentSymbols(file))
      result.emplace_back(symbolJSON(*candidate));
    response["result"] = std::move(result);
  } else if (query == "workspaceSymbols") {
    llvm::json::Array result;
    for (const SemanticSymbol *candidate : workspaceSymbols())
      result.emplace_back(symbolJSON(*candidate));
    response["result"] = std::move(result);
  } else if (query == "context") {
    llvm::json::Object result;
    result["symbol"] = target ? llvm::json::Value(symbolJSON(*target))
                              : llvm::json::Value(nullptr);
    result["definition"] =
        target ? llvm::json::Value(locationJSON(target->Declaration))
               : llvm::json::Value(nullptr);
    llvm::json::Array referenceValues;
    if (target) {
      auto targetReferences = references(target->ID);
      size_t count = std::min<size_t>(targetReferences.size(), 32);
      for (size_t i = 0; i < count; ++i)
        referenceValues.emplace_back(occurrenceJSON(*targetReferences[i]));
    }
    result["references"] = std::move(referenceValues);
    llvm::json::Array visible;
    auto candidates = completions(file, line, character);
    size_t count = std::min<size_t>(candidates.size(), 20);
    for (size_t i = 0; i < count; ++i)
      visible.emplace_back(symbolJSON(*candidates[i]));
    result["visibleSymbols"] = std::move(visible);
    result["truncated"] = candidates.size() > 20 ||
                          (target && references(target->ID).size() > 32);
    response["result"] = std::move(result);
  } else if (query == "rename") {
    llvm::json::Object result;
    if (!target) {
      result["allowed"] = false;
      result["reason"] = "no symbol at position";
    } else if (!validIdentifier(newName)) {
      result["allowed"] = false;
      result["reason"] = "new name is not a valid Toka identifier";
    } else {
      bool conflict = false;
      for (const auto &[id, candidate] : Symbols)
        if (candidate.ID != target->ID && candidate.Name == newName &&
            candidate.ScopeID == target->ScopeID)
          conflict = true;
      result["allowed"] = !conflict;
      result["reason"] = conflict ? "name conflicts in the same scope" : "";
      llvm::json::Array edits;
      if (!conflict)
        for (const SemanticOccurrence *reference : references(target->ID))
          edits.emplace_back(
              llvm::json::Object{{"location", locationJSON(reference->Range)},
                                 {"newText", newName}});
      result["edits"] = std::move(edits);
    }
    response["result"] = std::move(result);
  } else {
    response["error"] = llvm::json::Object{
        {"code", "unknown_query"}, {"message", "unknown semantic query"}};
  }
  return response;
}

} // namespace toka
