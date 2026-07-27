// Copyright (c) 2025 YiZhonghua<zhyi@dpai.com>. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "toka/AST.h"
#include "toka/SourceManager.h"
#include "llvm/Support/JSON.h"
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace toka {

inline constexpr const char *TOKA_SEMANTIC_INDEX_SCHEMA = "toka.semantic-index";
inline constexpr unsigned TOKA_SEMANTIC_INDEX_VERSION = 1;

enum class SemanticSymbolKind {
  Module,
  Function,
  Method,
  ExternFunction,
  Shape,
  Trait,
  TypeAlias,
  Field,
  EnumVariant,
  Global,
  Parameter,
  Variable,
  Pattern,
};

enum class SemanticReferenceRole {
  Declaration,
  Read,
  Write,
  Call,
  Type,
  Import,
};

struct SemanticPosition {
  unsigned Line = 0;
  unsigned Character = 0;
};

struct SemanticRange {
  std::string File;
  SemanticPosition Start;
  SemanticPosition End;
};

// These facts are declaration-backed capabilities, not inferred intentions at
// a particular call site.  They let an AI or editor decide how to construct a
// legal call without scraping a rendered signature.
struct SemanticParameterContract {
  std::string Name;
  std::string Type;
  std::string Morphology;
  std::string Flow;
  bool PayloadWritable = false;
  bool PayloadBlocked = false;
  bool HandleRebindable = false;
  bool HandleBlocked = false;
  bool HandleNullable = false;
  bool PayloadNullable = false;
};

struct SemanticCallableContract {
  std::string Effect;
  bool Variadic = false;
  std::vector<SemanticParameterContract> Parameters;
  std::string ReturnType;
  std::vector<std::string> ReturnDependencies;
};

struct SemanticFieldContract {
  std::string Morphology;
  std::string Flow;
  bool PayloadWritable = false;
  bool PayloadBlocked = false;
  bool HandleRebindable = false;
  bool HandleBlocked = false;
  bool HandleNullable = false;
  bool PayloadNullable = false;
};

struct SemanticSymbol {
  std::string ID;
  std::string Name;
  SemanticSymbolKind Kind = SemanticSymbolKind::Variable;
  std::string Detail;
  std::string Type;
  std::string ContainerID;
  std::string ScopeID;
  std::string Documentation;
  bool IsPublic = false;
  SemanticRange Declaration;
  std::optional<SemanticCallableContract> CallableContract;
  std::optional<SemanticFieldContract> FieldContract;
};

struct SemanticOccurrence {
  std::string SymbolID;
  SemanticReferenceRole Role = SemanticReferenceRole::Read;
  SemanticRange Range;
  bool IsDeclaration = false;
};

class SemanticIndexBuilder;

class SemanticIndex {
public:
  static SemanticIndex build(const std::vector<Module *> &modules,
                             SourceManager &sourceManager);

  const std::map<std::string, SemanticSymbol> &symbols() const {
    return Symbols;
  }
  const std::vector<SemanticOccurrence> &occurrences() const {
    return Occurrences;
  }

  const SemanticOccurrence *occurrenceAt(const std::string &file, unsigned line,
                                         unsigned character) const;
  const SemanticSymbol *symbol(const std::string &id) const;
  std::vector<const SemanticOccurrence *>
  references(const std::string &symbolID, bool includeDeclaration = true) const;
  std::vector<const SemanticSymbol *>
  documentSymbols(const std::string &file) const;
  std::vector<const SemanticSymbol *> workspaceSymbols() const;
  std::vector<const SemanticSymbol *>
  completions(const std::string &file, unsigned line, unsigned character) const;

  llvm::json::Value toJSON() const;
  llvm::json::Value queryJSON(const std::string &query, const std::string &file,
                              unsigned line, unsigned character,
                              const std::string &newName = "") const;

private:
  friend class SemanticIndexBuilder;

  std::map<std::string, SemanticSymbol> Symbols;
  std::vector<SemanticOccurrence> Occurrences;
  std::map<std::string, std::vector<std::string>> ModuleImports;
};

const char *toString(SemanticSymbolKind kind);
const char *toString(SemanticReferenceRole role);

} // namespace toka
