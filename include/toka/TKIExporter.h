// Copyright (c) 2026 YiZhonghua<zhyi@dpai.com>. All rights reserved.
#pragma once

#include "toka/AST.h"
#include <llvm/Support/raw_ostream.h>
#include <string>
#include <vector>

namespace toka {

class TKIExporter {
public:
  TKIExporter(llvm::raw_ostream &os) : m_OS(os) {}

  void exportModule(const Module &module);
  // Canonical replay surface used by compiler-owned semantic closure
  // construction. It intentionally omits @meta transport fields and @tki
  // audit comments while retaining exactly the declarations replayed by TKI.
  void exportSemanticReplaySurface(const Module &module);

private:
  llvm::raw_ostream &m_OS;
  int m_Indent = 0;

  void indent();
  void write(const std::string &str);
  void writeln(const std::string &str = "");
  void exportDeclarations(const Module &module);

  // Top level declaration exporters
  void exportImport(const ImportDecl &decl);
  void exportTypeAlias(const TypeAliasDecl &decl);
  void exportShape(const ShapeDecl &decl);
  void exportTrait(const TraitDecl &decl);
  void exportImpl(const ImplDecl &decl);
  void exportFunction(const FunctionDecl &decl, bool forceKeepBody = false);
  void exportExtern(const ExternDecl &decl);
  void exportGlobal(const Stmt &stmt);

  // Helper to serialize expressions and statements
  void exportExpr(const Expr *expr, bool stripHats = false);
  void exportStmt(const Stmt *stmt, bool indentStmt = true);
  void exportBlock(const BlockStmt &block);
  void exportPattern(const MatchArm::Pattern *pat);

  // Printing helpers
  void printGenericParams(const std::vector<GenericParam> &params);
  void printArg(const FunctionDecl::Arg &arg);
};

} // namespace toka
