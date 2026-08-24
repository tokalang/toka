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
#include "toka/Parser.h"
#include <algorithm>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace toka {

std::unique_ptr<Stmt> Parser::parseVariableDecl(bool isPub) {
  bool isConst = match(TokenType::KwConst);
  bool isAutoBinding = !isConst;
  if (!isConst) {
    if (match(TokenType::KwLet)) {
      error(previous(), DiagID::ERR_PARSER_DEPRECATED_KEYWORD_LET_USE_AUTO_FOR_VAR);
    } else if (check(TokenType::Identifier) && peek().Text == "var") {
      advance();
      error(previous(), DiagID::ERR_PARSER_DEPRECATED_KEYWORD_VAR_USE_AUTO_FOR_VAR);
    } else if (previous().Kind != TokenType::KwAuto) {
      match(TokenType::KwAuto);
    }
  }

  bool isRef = false;
  bool hasPointer = false;
  bool isUnique = false;
  bool isShared = false;
  bool isPtrNullable = match(TokenType::KwNul); // [New Rule] nul pointer modifier
  bool isRebindable = false;
  bool isRebindBlocked = false;
  std::vector<HandleLayer> handleLayers;

  std::string morphologyPrefix = "";
  while (true) {
    if (match(TokenType::Ampersand)) {
      morphologyPrefix += "&";
      Token tok = previous();
      HandleLayer layer;
      layer.Morphology = BindingMorphology::Reference;
      layer.Rebindable = tok.IsSwappablePtr;
      layer.Nullable = tok.HasNull;
      layer.Blocked = tok.IsBlocked;
      handleLayers.push_back(layer);
      isRebindable = tok.IsSwappablePtr;
      isPtrNullable = isPtrNullable || tok.HasNull;
      isRebindBlocked = tok.IsBlocked;
      if (isPtrNullable) {
        error(tok, DiagID::ERR_PARSER_BORROWED_POINTERS_CANNOT_BE_NULLABLE);
      }
    } else if (match(TokenType::And)) {
      morphologyPrefix += "&&";
      Token tok = previous();
      HandleLayer outer;
      outer.Morphology = BindingMorphology::Reference;
      outer.Rebindable = tok.IsSwappablePtr;
      outer.Nullable = tok.HasNull;
      outer.Blocked = tok.IsBlocked;
      handleLayers.push_back(outer);
      HandleLayer inner;
      inner.Morphology = BindingMorphology::Reference;
      handleLayers.push_back(inner);
      isRebindable = tok.IsSwappablePtr;
      isPtrNullable = isPtrNullable || tok.HasNull;
      isRebindBlocked = tok.IsBlocked;
      if (isPtrNullable) {
        error(tok, DiagID::ERR_PARSER_BORROWED_POINTERS_CANNOT_BE_NULLABLE);
      }
    } else if (match(TokenType::Caret)) {
      morphologyPrefix += "^";
      Token tok = previous();
      HandleLayer layer;
      layer.Morphology = BindingMorphology::Unique;
      layer.Rebindable = tok.IsSwappablePtr;
      layer.Nullable = tok.HasNull;
      layer.Blocked = tok.IsBlocked;
      handleLayers.push_back(layer);
      isRebindable = tok.IsSwappablePtr;
      isPtrNullable = isPtrNullable || tok.HasNull;
      isRebindBlocked = tok.IsBlocked;
    } else if (match(TokenType::Star)) {
      morphologyPrefix += "*";
      Token tok = previous();
      HandleLayer layer;
      layer.Morphology = BindingMorphology::Raw;
      layer.Rebindable = tok.IsSwappablePtr;
      layer.Nullable = tok.HasNull;
      layer.Blocked = tok.IsBlocked;
      handleLayers.push_back(layer);
      isRebindable = tok.IsSwappablePtr;
      isPtrNullable = isPtrNullable || tok.HasNull;
      isRebindBlocked = tok.IsBlocked;
    } else if (match(TokenType::Tilde)) {
      morphologyPrefix += "~";
      Token tok = previous();
      HandleLayer layer;
      layer.Morphology = BindingMorphology::Shared;
      layer.Rebindable = tok.IsSwappablePtr;
      layer.Nullable = tok.HasNull;
      layer.Blocked = tok.IsBlocked;
      handleLayers.push_back(layer);
      isRebindable = tok.IsSwappablePtr;
      isPtrNullable = isPtrNullable || tok.HasNull;
      isRebindBlocked = tok.IsBlocked;
    } else {
      break;
    }
  }

  if (!handleLayers.empty()) {
    handleLayers.front().Nullable =
        handleLayers.front().Nullable || isPtrNullable;
    const auto outer = handleLayers.front().Morphology;
    isRef = outer == BindingMorphology::Reference;
    isUnique = outer == BindingMorphology::Unique;
    isShared = outer == BindingMorphology::Shared;
    hasPointer = outer == BindingMorphology::Raw;
    isRebindable = handleLayers.front().Rebindable;
    isRebindBlocked = handleLayers.front().Blocked;
    isPtrNullable = handleLayers.front().Nullable;
  }

  if (isPtrNullable && (isUnique || isShared)) {
    HasError = true;
    DiagnosticEngine::report(
        previous().Loc, DiagID::ERR_SAFE_NULLABLE_HANDLE_REMOVED,
        isUnique ? "nul ^T" : "nul ~T");
  }

  // Check for positional destructuring: let Type(v1, v2) = ... or let (v1, v2)
  // = ...
  if ((check(TokenType::Identifier) && checkAt(1, TokenType::LParen)) ||
      check(TokenType::LParen)) {
    std::string typeName = "";
    if (check(TokenType::Identifier)) {
      typeName = advance().Text;
    }
    consume(TokenType::LParen, DiagID::ERR_PARSER_EXPECTED_FOR_DESTRUCTURING);
    std::vector<DestructuredVar> vars;
    bool hasNamed = false;
    bool hasPositional = false;
    while (!check(TokenType::RParen) && !check(TokenType::EndOfFile)) {
      if (match(TokenType::DotDot)) {
        DestructuredVar v;
        v.Name = "..";
        v.FieldName = "..";
        vars.push_back(v);
      } else {
        if (isNextNamedField(0)) {
          hasNamed = true;
          std::string varPrefix = "";
          bool isRef = false;
          if (match(TokenType::Ampersand)) {
            varPrefix = "&";
            isRef = true;
          } else if (match(TokenType::Caret)) {
            varPrefix = "^";
          } else if (match(TokenType::Tilde)) {
            varPrefix = "~";
          } else if (match(TokenType::Star)) {
            varPrefix = "*";
          }

          Token varTok = consume(TokenType::Identifier, DiagID::ERR_PARSER_EXPECTED_VARIABLE_NAME_OR);
          std::string fullVarName = varPrefix + varTok.Text;

          consume(TokenType::Equal, DiagID::ERR_PARSER_EXPECTED_AFTER_VARIABLE_NAME);
          consume(TokenType::Dot, DiagID::ERR_PARSER_EXPECTED_AFTER_IN_NAMED_DESTRUCTURING);

          std::string fieldPrefix = "";
          if (match(TokenType::Star))
            fieldPrefix = "*";
          else if (match(TokenType::Caret))
            fieldPrefix = "^";
          else if (match(TokenType::Tilde))
            fieldPrefix = "~";
          else if (match(TokenType::Ampersand))
            fieldPrefix = "&";

          if (match(TokenType::TokenNull))
            fieldPrefix += "?";
          if (match(TokenType::TokenWrite))
            fieldPrefix += "#";

          Token fieldNameTok = consume(TokenType::Identifier, DiagID::ERR_PARSER_EXPECTED_FIELD_NAME);
          std::string fieldName = fieldPrefix + fieldNameTok.Text;
          if (fieldNameTok.HasWrite || fieldNameTok.IsBlocked)
            error(fieldNameTok, DiagID::ERR_ILLEGAL_FIELD_MODIFIER);
          
          DestructuredVar v;
          v.Name = fullVarName;
          v.FieldName = fieldName;
          v.IsWildcard = (varTok.Text == "_");
          v.IsValueMutable = varTok.HasWrite;
          v.IsValueNullable = varTok.HasNull;
          v.IsValueBlocked = varTok.IsBlocked;
          v.IsReference = isRef;
          v.Permission = BindingPermission::fromLegacy(
              varPrefix == "*", varPrefix == "^", varPrefix == "~", isRef,
              false, false, false, varTok.HasWrite, varTok.HasNull,
              varTok.IsBlocked);
          vars.push_back(v);
        } else {
          hasPositional = true;
          std::string varPrefix = "";
          bool isRef = false;
          if (match(TokenType::Ampersand)) {
            varPrefix = "&";
            isRef = true;
          } else if (match(TokenType::Caret)) {
            varPrefix = "^";
          } else if (match(TokenType::Tilde)) {
            varPrefix = "~";
          } else if (match(TokenType::Star)) {
            varPrefix = "*";
          }
          Token varTok = consume(TokenType::Identifier, DiagID::ERR_PARSER_EXPECTED_VARIABLE_NAME_OR);
          std::string fullVarName = varPrefix + varTok.Text;
          
          DestructuredVar v;
          v.Name = fullVarName;
          v.FieldName = "";
          v.IsWildcard = (varTok.Text == "_");
          v.IsValueMutable = varTok.HasWrite;
          v.IsValueNullable = varTok.HasNull;
          v.IsValueBlocked = varTok.IsBlocked;
          v.IsReference = isRef;
          v.Permission = BindingPermission::fromLegacy(
              varPrefix == "*", varPrefix == "^", varPrefix == "~", isRef,
              false, false, false, varTok.HasWrite, varTok.HasNull,
              varTok.IsBlocked);
          vars.push_back(v);
        }
      }
      if (!match(TokenType::Comma))
        break;
    }
    consume(TokenType::RParen, DiagID::ERR_PARSER_EXPECTED_AFTER_DESTRUCTURING);
    consume(TokenType::Equal, DiagID::ERR_PARSER_EXPECTED_FOR_DESTRUCTURING_2);
    auto init = parseExpr();
    expectEndOfStatement();
    auto node = std::make_unique<DestructuringDecl>(typeName, std::move(vars),
                                                    std::move(init));
    node->setLocation(previous(),
                      m_CurrentFile); // Use previous (RParen or last consumed)
                                      // as location anchor
    return node;
  }

  bool isMorphicExempt = false;
  Token name = consume(TokenType::Identifier, DiagID::ERR_PARSER_EXPECTED_VARIABLE_NAME);
  if (!name.Text.empty() && name.Text[0] == '\'') {
      isMorphicExempt = true;
      // Do not strip the quote from the name to keep it consistent with
      // function arguments and pattern bindings.
  }
  std::string fullVarName = morphologyPrefix + name.Text;

  std::string typeName = "";
  TypeSyntaxPtr typeSyntax;
  if (match(TokenType::Colon)) {
    if (isAutoBinding)
      error(previous(), DiagID::ERR_PARSER_AUTO_BINDING_TYPE_ANNOTATION,
            fullVarName);
    typeSyntax = parseRequiredTypeSyntax(hasPointer);
    typeName = canonicalType(typeSyntax);
    if (!typeName.empty() && typeName[0] == '\'') {
      errorTypeSideMorphicBinding(name, morphologyPrefix, typeName);
      typeName = typeName.substr(1);
    }
  }

  std::unique_ptr<Expr> init;
  if (match(TokenType::Equal)) {
    init = parseExpr();
  }
  // Prefer the enclosing syntax error when a malformed token still follows
  // the binding name (for example `auto max-size = 1`).  The dedicated
  // missing-initializer diagnostic applies only to a completed declaration.
  if (isAutoBinding && !init && isEndOfStatement())
    error(name, DiagID::ERR_PARSER_AUTO_BINDING_REQUIRES_INITIALIZER,
          fullVarName);

  // Use fullVarName uniformly (e.g. `&x` directly as name)
  auto node = std::make_unique<VariableDecl>(fullVarName, std::move(init));
  node->setLocation(name, m_CurrentFile);
  node->IsRawPointer = hasPointer;
  node->IsUnique = isUnique;
  node->IsShared = isShared;
  node->IsReference = isRef;
  node->IsPub = isPub;
  node->IsConst = isConst;
  // node->IsMutable = name.HasWrite; // Deprecated
  // Removed nullable payload syntax is retained only in parser recovery.
  // Explicit properties mapping
  node->IsValueMutable = name.HasWrite;
  node->IsValueNullable = name.HasNull;
  node->IsValueBlocked = name.IsBlocked;
  node->IsMorphicExempt = isMorphicExempt; // [NEW]
  node->IsRebindable = isRebindable;
  node->IsPointerNullable = isPtrNullable;
  node->IsRebindBlocked = isRebindBlocked;
  node->Permission.HandleLayers = handleLayers;
  node->Permission.syncProjections();
  node->Permission.SoulWritable = node->IsValueMutable;
  node->Permission.SoulBlocked = node->IsValueBlocked;
  node->Permission.MorphicExempt = node->IsMorphicExempt;
  node->TypeName = typeName;
  node->DeclaredTypeSyntax = typeSyntax;

  expectEndOfStatement();
  return node;
}

std::unique_ptr<GuardBindStmt> Parser::parseGuardBindStmt() {
  Token tok = consume(TokenType::KwGuard, DiagID::ERR_PARSER_EXPECTED_GUARD);
  consume(TokenType::KwAuto, DiagID::ERR_PARSER_EXPECTED_AUTO_AFTER_GUARD);
  auto pat = parsePattern(true);
  consume(TokenType::Equal, DiagID::ERR_PARSER_EXPECTED_IN_GUARD_AUTO_STATEMENT);
  auto target = parseExpr();
  consume(TokenType::KwElse, DiagID::ERR_PARSER_EXPECTED_ELSE_AFTER_GUARD_TARGET_EXPRES);
  
  std::unique_ptr<Stmt> elseBody;
  if (check(TokenType::LBrace)) {
      elseBody = parseBlock();
  } else {
      elseBody = parseStmt();
  }
  
  auto node = std::make_unique<GuardBindStmt>(std::move(pat), std::move(target), std::move(elseBody));
  node->setLocation(tok, m_CurrentFile);
  return node;
}

std::unique_ptr<Stmt> Parser::parseStmt() {
  if (check(TokenType::LBrace))
    return parseBlock();
  
  if (check(TokenType::KwGuard) && checkAt(1, TokenType::KwAuto))
    return parseGuardBindStmt();

  if (check(TokenType::KwIf))
    return std::make_unique<ExprStmt>(parseIf());
  if (match(TokenType::KwMatch))
    return std::make_unique<ExprStmt>(parseMatchExpr());
  if (check(TokenType::KwLoop))
    return std::make_unique<ExprStmt>(parseLoop());
  if (check(TokenType::KwFor))
    return std::make_unique<ExprStmt>(parseForExpr());
  if (check(TokenType::KwReturn))
    return parseReturn();
  // `init` stays contextual so protocol members named `init` remain ordinary
  // identifiers. The P1 forms accept one stable local name here; projections
  // and contracts are added by later P1 slices.
  if (check(TokenType::Identifier) && peek().Text == "init" &&
      checkAt(1, TokenType::Identifier) && checkAt(2, TokenType::LBrace))
    return parseInitBlockStmt();
  if (check(TokenType::Identifier) && peek().Text == "init" &&
      checkAt(1, TokenType::Identifier) && checkAt(2, TokenType::Equal))
    return parseInitStmt();
  if (check(TokenType::KwLet) || check(TokenType::KwAuto) ||
      (check(TokenType::Identifier) && peek().Text == "var"))
    return parseVariableDecl(false);
  if (check(TokenType::KwDelete))
    return parseDeleteStmt();
  if (check(TokenType::KwUnsafe))
    return parseUnsafeStmt();
  if (check(TokenType::KwFree))
    return parseFreeStmt();
  if (check(TokenType::KwUnreachable))
    return parseUnreachableStmt();

  // ExprStmt
  auto expr = parseExpr();
  if (expr) {
    expectEndOfStatement();
    return std::make_unique<ExprStmt>(std::move(expr));
  }
  return nullptr;
}

std::unique_ptr<Stmt> Parser::parseInitStmt() {
  Token init = advance();
  Token target = consume(TokenType::Identifier,
                         DiagID::ERR_PARSER_EXPECTED_VARIABLE_NAME);
  consume(TokenType::Equal, DiagID::ERR_PARSER_EXPECTED_AFTER_VARIABLE_NAME);
  auto value = parseExpr();
  expectEndOfStatement();

  auto place = std::make_unique<VariableExpr>(target.Text);
  place->Loc = target.Loc;
  place->IsValueMutable = target.HasWrite;
  place->IsValueNullable = target.HasNull;
  place->IsValueBlocked = target.IsBlocked;
  auto assignment = std::make_unique<BinaryExpr>(
      "=", std::move(place), std::move(value));
  assignment->IsInitialization = true;
  assignment->Loc = init.Loc;
  return std::make_unique<ExprStmt>(std::move(assignment));
}

std::unique_ptr<Stmt> Parser::parseInitBlockStmt() {
  Token init = advance();
  Token target = consume(TokenType::Identifier,
                         DiagID::ERR_PARSER_EXPECTED_VARIABLE_NAME);
  auto body = parseBlock();

  auto block = std::make_unique<InitBlockStmt>(target.Text, std::move(body));
  block->PlaceLoc = target.Loc;
  block->IsValueMutable = target.HasWrite;
  block->IsValueNullable = target.HasNull;
  block->IsValueBlocked = target.IsBlocked;
  block->setLocation(init, m_CurrentFile);
  return block;
}

std::unique_ptr<BlockStmt> Parser::parseBlock() {
  Token tok = consume(TokenType::LBrace, DiagID::ERR_EXPECTED_LBRACE);
  auto block = std::make_unique<BlockStmt>();
  block->setLocation(tok, m_CurrentFile);

  while (!check(TokenType::RBrace) && !check(TokenType::EndOfFile)) {
    auto stmt = parseStmt();
    if (stmt)
      block->Statements.push_back(std::move(stmt));
    else
      advance(); // Avoid infinite loop if null
      
    if (PanicMode) {
      synchronize();
    }
  }

  consume(TokenType::RBrace, DiagID::ERR_EXPECTED_RBRACE);
  return block;
}

std::unique_ptr<ReturnStmt> Parser::parseReturn() {
  Token tok = consume(TokenType::KwReturn, DiagID::ERR_PARSER_EXPECTED_RETURN);
  std::unique_ptr<Expr> val;
  if (!isEndOfStatement()) {
    val = parseExpr();
  }
  expectEndOfStatement();
  auto node = std::make_unique<ReturnStmt>(std::move(val));
  node->setLocation(tok, m_CurrentFile);
  return node;
}

std::unique_ptr<Stmt> Parser::parseDeleteStmt() {
  Token kw = consume(TokenType::KwDelete, DiagID::ERR_PARSER_EXPECTED_DEL_OR_DELETE);
  auto expr = parseExpr();
  expectEndOfStatement();
  auto node = std::make_unique<DeleteStmt>(std::move(expr));
  node->setLocation(kw, m_CurrentFile);
  return node;
}

std::unique_ptr<Stmt> Parser::parseUnsafeStmt() {
  Token tok = consume(TokenType::KwUnsafe, DiagID::ERR_PARSER_EXPECTED_UNSAFE);
  if (check(TokenType::LBrace)) {
    auto block = parseBlock();
    auto node = std::make_unique<UnsafeStmt>(std::move(block));
    node->setLocation(tok, m_CurrentFile);
    return node;
  }
  if (check(TokenType::KwFree)) {
    auto freeStmt = parseFreeStmt();
    auto node = std::make_unique<UnsafeStmt>(std::move(freeStmt));
    node->setLocation(tok, m_CurrentFile);
    return node;
  }
  // Line-level unsafe: unsafe p#[0] = 1
  auto stmt = parseStmt();
  auto node = std::make_unique<UnsafeStmt>(std::move(stmt));
  node->setLocation(tok, m_CurrentFile);
  return node;
}

std::unique_ptr<Stmt> Parser::parseFreeStmt() {
  Token tok = consume(TokenType::KwFree, DiagID::ERR_PARSER_EXPECTED_FREE);
  std::unique_ptr<Expr> count = nullptr;
  if (match(TokenType::LBracket)) {
    count = parseExpr();
    consume(TokenType::RBracket, DiagID::ERR_EXPECTED_RBRACKET);
  }
  auto expr = parseExpr();
  expectEndOfStatement();
  auto node = std::make_unique<FreeStmt>(std::move(expr), std::move(count));
  node->setLocation(tok, m_CurrentFile);
  return node;
}
std::unique_ptr<Stmt> Parser::parseUnreachableStmt() {
  Token tok = consume(TokenType::KwUnreachable, DiagID::ERR_PARSER_EXPECTED_UNREACHABLE);
  expectEndOfStatement();
  auto node = std::make_unique<UnreachableStmt>();
  node->setLocation(tok, m_CurrentFile);
  return node;
}

} // namespace toka
