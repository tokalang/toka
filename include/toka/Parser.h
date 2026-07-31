// Copyright (c) 2025 YiZhonghua<zhyi@dpai.com>. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
#pragma once

#include "toka/AST.h"
#include "toka/Lexer.h"
#include "toka/DiagnosticEngine.h"
#include <memory>
#include <string>
#include <vector>
#include <algorithm>

namespace toka {

class Parser {
public:
  static std::string TargetTriple;

  Parser(const std::vector<Token> &tokens, const std::string &fileName = "")
      : m_Tokens(tokens), m_Pos(0), m_CurrentFile(fileName) {
    std::replace(m_CurrentFile.begin(), m_CurrentFile.end(), '\\', '/');
  }

  // Top level
  std::unique_ptr<Module> parseModule();

private:
  const std::vector<Token> &m_Tokens;
  size_t m_Pos;
  std::string m_CurrentFile;
  int m_CurrentClosureMaxImplicitArg = -1;
  uint64_t m_NextTodoId = 1;

  // Helpers
  const Token &peek() const;
  const Token &peekAt(int offset) const;
  const Token &previous() const;
  Token advance();
  bool check(TokenType type) const;
  bool checkAt(int offset, TokenType type) const;
  bool match(TokenType type);
  Token consume(TokenType type, DiagID id);
  template <typename... Args>
  Token consume(TokenType type, DiagID id, Args &&...args) {
    if (check(type))
      return advance();
    error(peek(), id, std::forward<Args>(args)...);
    return peek();
  }
  void expectEndOfStatement();
  bool isEndOfStatement();

  void error(const Token &tok, DiagID id);
  template <typename... Args>
  void error(const Token &tok, DiagID id, Args &&...args) {
    if (PanicMode) { return; }
    PanicMode = true;
    HasError = true;
    DiagnosticEngine::report(tok.Loc, id, std::forward<Args>(args)...);
  }
  void synchronize();
  void errorTypeSideMorphicBinding(const Token &nameTok,
                                   const std::string &bindingPrefix,
                                   const std::string &typeName);
  bool rejectTypeSideReferenceParameter(const Token &nameTok,
                                        const std::string &bindingPrefix,
                                        std::string &typeName);
  std::string typeTokenText(const Token &tok) const;

  bool HasError = false;
  bool PanicMode = false;

  // Recursive Descent Methods
  std::unique_ptr<FunctionDecl> parseFunctionDecl(bool isPub = false);
  std::unique_ptr<Stmt> parseVariableDecl(bool isPub = false);
  std::unique_ptr<ExternDecl> parseExternDecl();
  std::unique_ptr<ImportDecl> parseImport(bool isPub = false);
  std::unique_ptr<TypeAliasDecl> parseTypeAliasDecl(bool isPub = false);
  std::unique_ptr<ShapeDecl> parseShape(bool isPub = false);
  std::unique_ptr<ImplDecl> parseImpl();
  std::unique_ptr<TraitDecl> parseTrait(bool isPub = false);
  AssociatedTypeDecl parseAssociatedTypeDecl(bool requireDefinition);
  bool isAssociatedTypeDeclStart() const;
  std::unique_ptr<Expr> parseMatchExpr();
  std::unique_ptr<MatchArm::Pattern> parseSinglePattern();
  std::unique_ptr<MatchArm::Pattern> parsePattern();
  std::vector<GenericParam> parseGenericParams();
  std::string parseTraitBoundName();
  std::vector<std::string> parseTraitFacetTarget();
  void parseWhereConstraints(std::vector<GenericParam> &genericParams,
                             std::vector<std::string> *selfTraitBounds = nullptr);
  bool isWhereConstraintTerminator() const;

  std::unique_ptr<Stmt> parseStmt();
  std::unique_ptr<GuardBindStmt> parseGuardBindStmt();
  std::unique_ptr<Expr> parseIf();
  std::unique_ptr<Expr> parseGuard();
  std::unique_ptr<Expr> parseRangeExpr(std::unique_ptr<Expr> start);
  std::unique_ptr<Expr> parseClosureExpr();

  bool isClosureExpression();
  bool isTypeCast();
  std::unique_ptr<Expr> parseWhile();
  std::unique_ptr<Expr> parseLoop();
  std::unique_ptr<Expr> parseForExpr();
  std::unique_ptr<Expr> parseBreak();
  std::unique_ptr<Expr> parseContinue();
  std::unique_ptr<Expr> parsePass();
  std::unique_ptr<BlockStmt> parseBlock();
  std::unique_ptr<Stmt> parseDeleteStmt();
  std::unique_ptr<Stmt> parseUnsafeStmt();
  std::unique_ptr<Expr> parseUnsafeExpr();
  std::unique_ptr<Stmt> parseFreeStmt();
  std::unique_ptr<Expr> parseAllocExpr();
  std::unique_ptr<ReturnStmt> parseReturn();
  std::unique_ptr<Stmt> parseUnreachableStmt();

  std::unique_ptr<Expr> parseExpr(int minPrec = 0, bool allowTrailingClosure = true);
  std::unique_ptr<Expr> parsePrimary(bool allowTrailingClosure = true);
  std::string parseTypeString(bool allowAssociatedProjection = true);
  bool isNextNamedField(int startOffset = 0) const;
  bool isNamedInitList() const;
  std::string parseNamespaceOrIdentifier();

  // ... Add more precedence helpers here
};

} // namespace toka
