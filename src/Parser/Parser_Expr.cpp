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

std::unique_ptr<MatchArm::Pattern> Parser::parsePattern(bool inheritedFresh) {
  auto pat = parseSinglePattern(inheritedFresh);
  if (!pat) return nullptr;

  if (check(TokenType::Pipe)) {
    auto orPat = std::make_unique<MatchArm::Pattern>(MatchArm::Pattern::Or);
    orPat->Loc = pat->Loc;
    const bool wholePatternFresh = inheritedFresh || pat->HasAutoBinding;
    orPat->HasAutoBinding = pat->HasAutoBinding;
    if (orPat->HasAutoBinding)
      pat->HasAutoBinding = false;
    orPat->SubPatterns.push_back(std::move(pat));
    while (match(TokenType::Pipe)) {
      auto next = parseSinglePattern(wholePatternFresh);
      if (next) {
        orPat->SubPatterns.push_back(std::move(next));
      }
    }
    return orPat;
  }

  return pat;
}

std::unique_ptr<MatchArm::Pattern> Parser::parseSinglePattern(bool inheritedFresh) {
  if (match(TokenType::DotDot)) {
    auto p = std::make_unique<MatchArm::Pattern>(MatchArm::Pattern::Elision);
    p->Loc = previous().Loc;
    return p;
  }

  bool isMut = false;
  bool isRef = false;
  bool isUnique = false;
  bool isShared = false;
  bool isPointer = false;
  bool isPtrNullable = false;
  bool isRebindable = false;
  bool isRebindBlocked = false;
  bool hasAutoBinding = match(TokenType::KwAuto);
  const bool introducesFresh = inheritedFresh || hasAutoBinding;

  std::string morphologyPrefix = "";
  while (true) {
    if (match(TokenType::Ampersand)) {
      isRef = true;
      morphologyPrefix += "&";
      Token tok = previous();
      isRebindable = tok.IsSwappablePtr;
      isPtrNullable = isPtrNullable || tok.HasNull;
      isRebindBlocked = tok.IsBlocked;
      if (isPtrNullable) {
        error(tok, DiagID::ERR_PARSER_BORROWED_POINTERS_CANNOT_BE_NULLABLE);
      }
      if (match(TokenType::KwMut))
        isMut = true;
    } else if (match(TokenType::And)) {
      isRef = true;
      morphologyPrefix += "&&";
      Token tok = previous();
      isRebindable = tok.IsSwappablePtr;
      isPtrNullable = isPtrNullable || tok.HasNull;
      isRebindBlocked = tok.IsBlocked;
      if (isPtrNullable) {
        error(tok, DiagID::ERR_PARSER_BORROWED_POINTERS_CANNOT_BE_NULLABLE);
      }
      if (match(TokenType::KwMut))
        isMut = true;
    } else if (match(TokenType::Caret)) {
      isUnique = true;
      morphologyPrefix += "^";
      Token tok = previous();
      isRebindable = tok.IsSwappablePtr;
      isPtrNullable = isPtrNullable || tok.HasNull;
      isRebindBlocked = tok.IsBlocked;
    } else if (match(TokenType::Tilde)) {
      isShared = true;
      morphologyPrefix += "~";
      Token tok = previous();
      isRebindable = tok.IsSwappablePtr;
      isPtrNullable = isPtrNullable || tok.HasNull;
      isRebindBlocked = tok.IsBlocked;
    } else if (match(TokenType::Star)) {
      isPointer = true;
      morphologyPrefix += "*";
      Token tok = previous();
      isRebindable = tok.IsSwappablePtr;
      isPtrNullable = isPtrNullable || tok.HasNull;
      isRebindBlocked = tok.IsBlocked;
    } else {
      break;
    }
  }

  if (check(TokenType::LParen)) {
    Token lpTok = peek();
    consume(TokenType::LParen, DiagID::ERR_PARSER_EXPECTED_LPAREN);
    std::vector<std::unique_ptr<MatchArm::Pattern>> subs;
    while (!check(TokenType::RParen) && !check(TokenType::EndOfFile)) {
      subs.push_back(parsePattern(introducesFresh));
      if (!check(TokenType::RParen))
        match(TokenType::Comma);
    }
    consume(TokenType::RParen, DiagID::ERR_PARSER_EXPECTED_AFTER_SUBPATTERNS);
    auto p = std::make_unique<MatchArm::Pattern>(MatchArm::Pattern::Decons);
    p->Loc = lpTok.Loc;
    p->Name = "";
    p->SubPatterns = std::move(subs);
    p->HasAutoBinding = hasAutoBinding;
    p->IsReference = isRef;
    p->Permission = BindingPermission::fromLegacy(
        isPointer, isUnique, isShared, isRef, isRebindable,
        isPtrNullable, isRebindBlocked, false, false, false);
    return p;
  }

  if (check(TokenType::Integer) || check(TokenType::String) ||
      check(TokenType::ViewString) ||
      check(TokenType::CharLiteral) ||
      check(TokenType::KwTrue) || check(TokenType::KwFalse)) {
    auto p = std::make_unique<MatchArm::Pattern>(MatchArm::Pattern::Literal);
    p->Loc = peek().Loc;
    p->HasAutoBinding = hasAutoBinding;
    Token t = advance();
    if (t.Kind == TokenType::String || t.Kind == TokenType::ViewString) {
      p->Name = "\"" + t.Text + "\"";
    } else if (t.Kind == TokenType::CharLiteral) {
      p->Name = "'" + t.Text + "'";
    } else {
      p->Name = t.Text;
    }
    
    if (t.Kind == TokenType::Integer) {
      try {
        p->LiteralVal = std::stoull(t.Text, nullptr, 0);
      } catch (...) {
        p->LiteralVal = 0;
      }
    } else if (t.Kind == TokenType::CharLiteral) {
      if (!t.Text.empty()) {
        p->LiteralVal = static_cast<uint64_t>(static_cast<unsigned char>(t.Text[0]));
      }
    }

    // Support Range patterns: start ..< end or start ..= end
    if (check(TokenType::DotDotLess) || check(TokenType::DotDotEqual)) {
      if (t.Kind != TokenType::Integer && t.Kind != TokenType::CharLiteral) {
        error(peek(), DiagID::ERR_PARSER_RANGE_PATTERN_START_MUST_BE_AN_INTEGER);
      }
      Token opTok = peek();
      if (!opTok.HasSpacesAround) {
        error(opTok, DiagID::ERR_PARSER_RANGE_OPERATORS_AND_MUST_BE_SURROUNDED);
      }
      bool isInclusive = (opTok.Kind == TokenType::DotDotEqual);
      advance(); // consume the operator

      if (!check(TokenType::Integer) && !check(TokenType::CharLiteral)) {
        error(peek(), DiagID::ERR_PARSER_EXPECTED_INTEGER_OR_CHARACTER_LITERAL_F);
      }
      
      auto endPat = std::make_unique<MatchArm::Pattern>(MatchArm::Pattern::Literal);
      endPat->Loc = peek().Loc;
      Token endTok = advance();
      if (endTok.Kind == TokenType::CharLiteral) {
        endPat->Name = "'" + endTok.Text + "'";
      } else {
        endPat->Name = endTok.Text;
      }
      if (endTok.Kind == TokenType::Integer) {
        try {
          endPat->LiteralVal = std::stoull(endTok.Text, nullptr, 0);
        } catch (...) {
          endPat->LiteralVal = 0;
        }
      } else {
        if (!endTok.Text.empty()) {
          endPat->LiteralVal = static_cast<uint64_t>(static_cast<unsigned char>(endTok.Text[0]));
        }
      }

      auto rangePat = std::make_unique<MatchArm::Pattern>(MatchArm::Pattern::Range);
      rangePat->Loc = p->Loc;
      rangePat->IsInclusive = isInclusive;
      rangePat->SubPatterns.push_back(std::move(p));
      rangePat->SubPatterns.push_back(std::move(endPat));
      return rangePat;
    }
    return p;
  }

  if (check(TokenType::Identifier)) {
    if (peek().Text == "_") {
      Token wildcard = advance();
      auto pattern =
          std::make_unique<MatchArm::Pattern>(MatchArm::Pattern::Wildcard);
      pattern->Loc = wildcard.Loc;
      return pattern;
    }
    if (peek().Text == "default" && !hasAutoBinding) {
      error(peek(), DiagID::ERR_PARSER_DEFAULT_WILDCARD_REMOVED);
      return nullptr;
    }

    Token nameTok = peek();
    std::string name = parseNamespaceOrIdentifier();

    if (match(TokenType::Less) || match(TokenType::GenericLT)) {
      name += "<";
      while (true) {
        name += parseTypeArgumentSyntax().toCanonicalString();
        if (match(TokenType::Comma)) {
          name += ",";
        } else {
          break;
        }
      }
      consume(TokenType::Greater, DiagID::ERR_EXPECTED_GREATER);
      name += ">";
    }

    // Handle Path::Variant
    if (check(TokenType::Colon) && checkAt(1, TokenType::Colon)) {
      consume(TokenType::Colon, DiagID::ERR_PARSER_EXPECTED_COLON);
      consume(TokenType::Colon, DiagID::ERR_PARSER_EXPECTED_COLON);
      name +=
          "::" + consume(TokenType::Identifier, DiagID::ERR_PARSER_EXPECTED_VARIANT_NAME).Text;
    }

    if (match(TokenType::LParen)) {
      std::vector<std::unique_ptr<MatchArm::Pattern>> subs;
      std::vector<std::string> subNames;
      bool hasNamed = false;
      bool hasPositional = false;
      while (!check(TokenType::RParen) && !check(TokenType::EndOfFile)) {
        if (match(TokenType::DotDot)) {
          subs.push_back(std::make_unique<MatchArm::Pattern>(MatchArm::Pattern::Elision));
          subNames.push_back("");
        } else {
          if (isNextNamedField(0)) {
            hasNamed = true;
            auto pat = parsePattern(introducesFresh);
            consume(TokenType::Equal, DiagID::ERR_PARSER_EXPECTED_AFTER_PATTERN);
            consume(TokenType::Dot, DiagID::ERR_PARSER_EXPECTED_AFTER_IN_NAMED_PATTERN);

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

            subs.push_back(std::move(pat));
            subNames.push_back(fieldName);
          } else {
            hasPositional = true;
            subs.push_back(parsePattern(introducesFresh));
            subNames.push_back("");
          }
        }
        if (!check(TokenType::RParen))
          match(TokenType::Comma);
      }
      consume(TokenType::RParen, DiagID::ERR_PARSER_EXPECTED_AFTER_SUBPATTERNS);
      if (hasNamed && hasPositional) {
        error(peek(), DiagID::ERR_PARSER_CANNOT_MIX_POSITIONAL_AND_NAMED_FIELD_2);
      }
      auto p = std::make_unique<MatchArm::Pattern>(MatchArm::Pattern::Decons);
      p->Loc = nameTok.Loc;
      p->Name = name;
      p->SubPatterns = std::move(subs);
      p->SubPatternNames = std::move(subNames);
      p->HasAutoBinding = hasAutoBinding;
      p->IsReference = isRef;
      p->Permission = BindingPermission::fromLegacy(
          isPointer, isUnique, isShared, isRef, isRebindable,
          isPtrNullable, isRebindBlocked, false, false, false);
      return p;
    }

    auto p = std::make_unique<MatchArm::Pattern>(MatchArm::Pattern::Variable);
    p->Loc = nameTok.Loc;
    p->Name = morphologyPrefix + name;
    p->HasAutoBinding = hasAutoBinding;
    p->Binding = introducesFresh ? MatchArm::Pattern::BindingOrigin::Fresh
                                 : MatchArm::Pattern::BindingOrigin::Existing;
    p->IsReference = isRef;
    p->IsValueMutable = nameTok.HasWrite;
    p->IsValueBlocked = nameTok.IsBlocked;
    p->Permission = BindingPermission::fromLegacy(
        isPointer, isUnique, isShared, isRef, isRebindable,
        isPtrNullable, isRebindBlocked, nameTok.HasWrite, false,
        nameTok.IsBlocked);
    return p;
  }

  error(peek(), DiagID::ERR_PARSER_EXPECTED_PATTERN);
  return nullptr;
}

std::unique_ptr<Expr> Parser::parseMatchExpr() {
  Token matchTok = previous(); // KwMatch or peek()
  auto target = parseExpr(0, false);
  consume(TokenType::LBrace, DiagID::ERR_PARSER_EXPECTED_AFTER_MATCH_EXPRESSION);

  std::vector<std::unique_ptr<MatchArm>> arms;
  while (!check(TokenType::RBrace) && !check(TokenType::EndOfFile)) {
    auto pat = parsePattern();
    if (!pat) {
      advance(); // Prevent infinite loop on syntax error
      continue;
    }
    std::unique_ptr<Expr> guard = nullptr;
    if (match(TokenType::KwIf)) {
      guard = parseExpr();
    }
    consume(TokenType::FatArrow, DiagID::ERR_PARSER_EXPECTED);
    auto body = parseStmt();
    arms.push_back(std::make_unique<MatchArm>(std::move(pat), std::move(guard),
                                              std::move(body)));
  }
  consume(TokenType::RBrace, DiagID::ERR_PARSER_EXPECTED_AFTER_MATCH_ARMS);
  auto matched =
      std::make_unique<MatchExpr>(std::move(target), std::move(arms));
  matched->setLocation(matchTok, m_CurrentFile);
  return matched;
}

std::unique_ptr<Expr> Parser::parseExpr(int minPrec, bool allowTrailingClosure) {
  auto lhs = parsePrimary(allowTrailingClosure);
  if (!lhs)
    return nullptr;

  while (true) {
    if (check(TokenType::Colon) || check(TokenType::KwAs) ||
        (peek().Kind == TokenType::Identifier && peek().Text == "as")) {
      Token introducer = advance(); // consume ':' or 'as'
      TypeSyntaxPtr typeSyntax = parseTypeSyntax(true, false, true);
      std::string typeName = canonicalType(typeSyntax);
      const CastKind kind = introducer.Kind == TokenType::Colon
                                ? CastKind::Ascription
                                : CastKind::Conversion;
      auto node = std::make_unique<CastExpr>(std::move(lhs), typeName, kind);
      node->TargetTypeSyntax = std::move(typeSyntax);
      node->setLocation(introducer, m_CurrentFile);
      lhs = std::move(node);
      continue;
    } // Closes if (check(KwAs))

    int prec = getPrecedence(peek().Kind);
    if (prec < minPrec)
      break;

    // Rule: Binary operators must be on the previous line to continue
    if (peek().HasNewlineBefore)
      break;

    Token op = advance();
    if (op.Kind == TokenType::Minus && !op.HasSpacesAround) {
      error(op, DiagID::ERR_PARSER_MINUS_OPERATOR_MUST_BE_SURROUNDED);
    }
    if ((op.Kind == TokenType::Ampersand || op.Kind == TokenType::Pipe ||
         op.Kind == TokenType::Caret || op.Kind == TokenType::LessLess ||
         op.Kind == TokenType::GreaterGreater) &&
        !op.HasSpacesAround) {
      error(op, DiagID::ERR_PARSER_BITWISE_OPERATOR_MUST_BE_SURROUNDED);
    }
    auto rhs = parseExpr(prec + 1, allowTrailingClosure);
    if (!rhs) {
      std::cerr << "Parser Error: Expected expression after operator\n";
      break;
    }

    auto node =
        std::make_unique<BinaryExpr>(op.Text, std::move(lhs), std::move(rhs));
    node->setLocation(op, m_CurrentFile);
    lhs = std::move(node);
  }

  return lhs;
}

std::unique_ptr<Expr> Parser::parsePrimary(bool allowTrailingClosure) {
  std::unique_ptr<Expr> expr = nullptr;
  if (match(TokenType::Bang) || match(TokenType::Minus) ||
      match(TokenType::PlusPlus) || match(TokenType::MinusMinus) ||
      match(TokenType::Caret) || match(TokenType::Tilde) ||
      match(TokenType::Star) || match(TokenType::Ampersand) ||
      match(TokenType::And) || match(TokenType::At)) {
    Token tok = previous();
    TokenType op = tok.Kind;
    auto sub = parsePrimary(allowTrailingClosure);
    if (op == TokenType::And) {
      auto inner = std::make_unique<UnaryExpr>(TokenType::Ampersand, std::move(sub));
      inner->setLocation(tok, m_CurrentFile);
      auto outer = std::make_unique<UnaryExpr>(TokenType::Ampersand, std::move(inner));
      outer->setLocation(tok, m_CurrentFile);
      return outer;
    }
    auto node = std::make_unique<UnaryExpr>(op, std::move(sub));
    node->HasNull = tok.HasNull;
    node->IsRebindable = tok.IsSwappablePtr;
    node->IsValueMutable = tok.HasWrite;
    node->IsValueNullable = tok.HasNull;
    node->IsRebindBlocked = tok.IsBlocked;
    node->IsValueBlocked = tok.IsBlocked;
    node->Permission = BindingPermission::fromLegacy(
        op == TokenType::Star, op == TokenType::Caret, op == TokenType::Tilde,
        op == TokenType::Ampersand, node->IsRebindable, node->HasNull,
        node->IsRebindBlocked, node->IsValueMutable, node->IsValueNullable,
        node->IsValueBlocked);
    node->setLocation(tok, m_CurrentFile);
    
    // [NEW] Enforce Hat Principle for references on member chains
    if (op == TokenType::Ampersand) {
      if (dynamic_cast<MemberExpr*>(node->RHS.get()) && !node->RHS->HasParens) {
        error(tok, DiagID::ERR_PARSER_USE_OF_UNARY_ON_AN_ACCESS_CHAIN_WITHOUT);
        return nullptr;
      }
    }
    
    return node;
  }

  if (isClosureExpression()) {
    return parseClosureExpr();
  }

  if (match(TokenType::Integer)) {
    Token tok = previous();
    auto node = std::make_unique<NumberExpr>(std::stoull(tok.Text, nullptr, 0));
    node->setLocation(tok, m_CurrentFile);
    expr = std::move(node);
  } else if (match(TokenType::Float)) {
    Token tok = previous();
    auto node = std::make_unique<FloatExpr>(std::stod(tok.Text));
    node->setLocation(tok, m_CurrentFile);
    expr = std::move(node);
  } else if (match(TokenType::KwTrue)) {
    Token tok = previous();
    auto node = std::make_unique<BoolExpr>(true);
    node->setLocation(tok, m_CurrentFile);
    expr = std::move(node);
  } else if (match(TokenType::KwFalse)) {
    Token tok = previous();
    auto node = std::make_unique<BoolExpr>(false);
    node->setLocation(tok, m_CurrentFile);
    expr = std::move(node);
  } else if (match(TokenType::KwNull)) {
    Token tok = previous();
    auto node = std::make_unique<NullExpr>();
    node->setLocation(tok, m_CurrentFile);
    expr = std::move(node);
  } else if (match(TokenType::DotDot)) {
    Token tok = previous();
    auto node = std::make_unique<ElisionExpr>();
    node->setLocation(tok, m_CurrentFile);
    expr = std::move(node);
  } else if (match(TokenType::KwNone)) {
    Token tok = previous();
    auto node = std::make_unique<NoneExpr>();
    node->setLocation(tok, m_CurrentFile);
    expr = std::move(node);
  } else if (match(TokenType::KwFile) || match(TokenType::KwLine) ||
             match(TokenType::KwLoc)) {
    Token tok = previous();
    auto node = std::make_unique<MagicExpr>(tok.Kind);
    node->setLocation(tok, m_CurrentFile);
    expr = std::move(node);
  } else if (match(TokenType::KwUninit)) {
    Token tok = previous();
    auto node = std::make_unique<UnsetExpr>();
    node->setLocation(tok, m_CurrentFile);
    expr = std::move(node);
  } else if (match(TokenType::KwTodo)) {
    Token tok = previous();
    auto node = std::make_unique<TodoExpr>(m_NextTodoId++);
    node->setLocation(tok, m_CurrentFile);
    expr = std::move(node);
  } else if (match(TokenType::String)) {
    Token tok = previous();
    auto node = std::make_unique<StringExpr>(tok.Text);
    node->setLocation(tok, m_CurrentFile);
    expr = std::move(node);
  } else if (match(TokenType::ViewString)) {
    Token tok = previous();
    auto node = std::make_unique<ViewStringExpr>(tok.Text);
    node->setLocation(tok, m_CurrentFile);
    expr = std::move(node);
  } else if (match(TokenType::CharLiteral)) {
    Token tok = previous();
    char val = 0;
    if (!tok.Text.empty())
      val = tok.Text[0];
    auto node = std::make_unique<CharLiteralExpr>(val);
    node->setLocation(tok, m_CurrentFile);
    expr = std::move(node);
  } else if (match(TokenType::KwIf)) {
    expr = parseIf();
  } else if (match(TokenType::KwGuard)) {
    expr = parseGuard();
  } else if (match(TokenType::KwWhile)) {
    Token whileTok = previous();
    DiagnosticEngine::report(whileTok.Loc, DiagID::ERR_WHILE_ABOLISHED);
    bool hasParen = match(TokenType::LParen);
    auto cond = parseExpr(0, false);
    if (hasParen)
      consume(TokenType::RParen, DiagID::ERR_EXPECTED_RPAREN);
    auto body = parseStmt();
    auto node = std::make_unique<LoopExpr>(std::move(cond), std::move(body));
    node->setLocation(whileTok, m_CurrentFile);
    expr = std::move(node);
  } else if (match(TokenType::KwLoop)) {
    expr = parseLoop();
  } else if (match(TokenType::KwFor)) {
    expr = parseForExpr();
  } else if (match(TokenType::KwMatch)) {
    expr = parseMatchExpr();
  } else if (match(TokenType::KwBreak)) {
    expr = parseBreak();
  } else if (match(TokenType::KwContinue)) {
    expr = parseContinue();
  } else if (match(TokenType::KwPass)) {
    expr = parsePass();
  } else if (match(TokenType::KwCede)) {
    Token tok = previous();
    auto val = parseExpr(0, allowTrailingClosure);
    auto node = std::make_unique<CedeExpr>(std::move(val));
    node->setLocation(tok, m_CurrentFile);
    expr = std::move(node);
  } else if (match(TokenType::KwSizeof)) {
    Token tok = previous();
    if (!match(TokenType::LParen)) {
      error(tok, DiagID::ERR_PARSER_EXPECTED_AFTER_SIZEOF);
      return nullptr;
    }
    auto typeSyntax = parseRequiredTypeSyntax();
    auto typeStr = canonicalType(typeSyntax);
    if (!match(TokenType::RParen)) {
      error(previous(), DiagID::ERR_PARSER_EXPECTED_AFTER_SIZEOF_TYPE_STRING);
      return nullptr;
    }
    auto node = std::make_unique<SizeOfExpr>(typeStr);
    node->TypeSyntax = typeSyntax;
    node->setLocation(tok, m_CurrentFile);
    expr = std::move(node);
  } else if (match(TokenType::KwSelf)) {
    Token tok = previous();
    auto node = std::make_unique<VariableExpr>("self");
    node->IsValueMutable = tok.HasWrite;
    node->IsValueNullable = tok.HasNull;
    node->IsValueBlocked = tok.IsBlocked;
    node->Permission = BindingPermission::fromLegacy(
        node->IsRawPointer, node->IsUnique, node->IsShared, false, false,
        false, false, node->IsValueMutable, node->IsValueNullable,
        node->IsValueBlocked);
    node->setLocation(tok, m_CurrentFile);
    expr = std::move(node);
  } else if (match(TokenType::KwUnsafe)) {
    expr = parseUnsafeExpr();
  } else if (match(TokenType::KwAlloc)) {
    expr = parseAllocExpr();
  } else if (match(TokenType::KwNew)) {
    Token kw = previous();
    Token startTok = peek();
    
    // [NEW] Parse Optional Array Scope for `new [N]Type`
    std::unique_ptr<Expr> arraySize = nullptr;
    if (match(TokenType::LBracket)) {
      if (!check(TokenType::RBracket)) {
        arraySize = parseExpr();
      } else {
        // new []T ? Empty length might be inferred later.
      }
      consume(TokenType::RBracket, DiagID::ERR_PARSER_EXPECTED_AFTER_ARRAY_SIZE);
    }

    if (!check(TokenType::Identifier)) {
      error(peek(), DiagID::ERR_PARSER_EXPECTED_TYPE_AFTER_NEW);
      return nullptr;
    }
    TypeSyntaxPtr typeSyntax = parseTypeSyntax(true, true);
    std::string typeStr = canonicalType(typeSyntax);

    std::unique_ptr<Expr> init = nullptr;
    if (check(TokenType::LBrace)) {
      advance(); // LBrace
      std::vector<std::pair<std::string, std::unique_ptr<Expr>>> fields;
      while (!check(TokenType::RBrace) && !check(TokenType::EndOfFile)) {
        if (!isNextNamedField(0)) {
            auto expr = parseExpr();
            if (dynamic_cast<ElisionExpr*>(expr.get())) {
                fields.push_back({"..", std::move(expr)});
            } else {
                error(peek(), DiagID::ERR_PARSER_EXPECTED_NAMED_ARGUMENT_KEY_VALUE_OR_EL);
            }
            if (!check(TokenType::RBrace)) match(TokenType::Comma);
            continue;
        }

        std::string prefix = "";
        if (match(TokenType::Star))
          prefix = "*";
        else if (match(TokenType::Caret))
          prefix = "^";
        else if (match(TokenType::Tilde))
          prefix = "~";
        else if (match(TokenType::Ampersand))
          prefix = "&";

        Token fieldNameTok = consume(TokenType::Identifier, DiagID::ERR_PARSER_EXPECTED_FIELD_NAME);
        std::string fieldName = fieldNameTok.Text;
        if (fieldNameTok.HasWrite || fieldNameTok.IsBlocked) error(fieldNameTok, DiagID::ERR_ILLEGAL_FIELD_MODIFIER);
        consume(TokenType::Equal, DiagID::ERR_PARSER_EXPECTED_AFTER_FIELD_NAME);
        fields.push_back({prefix + fieldName, parseExpr()});
        match(TokenType::Comma);
      }
      consume(TokenType::RBrace, DiagID::ERR_EXPECTED_RBRACE);
      auto node = std::make_unique<InitStructExpr>(typeStr, std::move(fields));
      node->setLocation(startTok, m_CurrentFile);
      init = std::move(node);
    } else if (check(TokenType::LParen)) {
      consume(TokenType::LParen, DiagID::ERR_EXPECTED_LPAREN);

      // Check for named initializer syntax: Type(field = val)
      bool isNamedInit = isNamedInitList();

      if (isNamedInit) {
        std::vector<std::pair<std::string, std::unique_ptr<Expr>>> fields;
        while (!check(TokenType::RParen) && !check(TokenType::EndOfFile)) {
          if (!isNextNamedField(0)) {
              auto expr = parseExpr();
              if (dynamic_cast<ElisionExpr*>(expr.get())) {
                  fields.push_back({"..", std::move(expr)});
              } else {
                  error(peek(), DiagID::ERR_PARSER_EXPECTED_NAMED_ARGUMENT_KEY_VALUE_OR_EL);
              }
              if (!check(TokenType::RParen)) match(TokenType::Comma);
              continue;
          }

          std::string prefix = "";
          if (match(TokenType::Star))
            prefix = "*";
          else if (match(TokenType::Caret))
            prefix = "^";
          else if (match(TokenType::Tilde))
            prefix = "~";
          else if (match(TokenType::Ampersand))
            prefix = "&";

          if (match(TokenType::TokenNull))
            prefix += "?";
          if (match(TokenType::TokenWrite))
            prefix += "#";

          Token fieldNameTok = consume(TokenType::Identifier, DiagID::ERR_PARSER_EXPECTED_FIELD_NAME);
          std::string fieldName = fieldNameTok.Text;
          if (fieldNameTok.HasWrite || fieldNameTok.IsBlocked) error(fieldNameTok, DiagID::ERR_ILLEGAL_FIELD_MODIFIER);
          consume(TokenType::Equal, DiagID::ERR_PARSER_EXPECTED_AFTER_FIELD_NAME);
          fields.push_back({prefix + fieldName, parseExpr()});
          if (!check(TokenType::RParen))
            match(TokenType::Comma);
        }
        consume(TokenType::RParen, DiagID::ERR_EXPECTED_RPAREN);
        auto node =
            std::make_unique<InitStructExpr>(typeStr, std::move(fields));
        node->setLocation(startTok, m_CurrentFile);
        init = std::move(node);
      } else {
        // new Type(...) -> treat as CallExpr for constructor (positional)
        std::vector<std::unique_ptr<Expr>> args;
        if (!check(TokenType::RParen)) {
          do {
            if (match(TokenType::DotDot)) {
              auto node = std::make_unique<ElisionExpr>();
              node->setLocation(previous(), m_CurrentFile);
              args.push_back(std::move(node));
            } else {
              args.push_back(parseExpr());
            }
          } while (match(TokenType::Comma));
        }
        consume(TokenType::RParen, DiagID::ERR_EXPECTED_RPAREN);
        auto node = std::make_unique<CallExpr>(typeStr, std::move(args));
        node->setLocation(startTok, m_CurrentFile);
        init = std::move(node);
      }
    } else {
      error(kw, DiagID::ERR_PARSER_EXPECTED_OR_INITIALIZER_FOR_NEW_EXPRESS);
      return nullptr;
    }
    auto node = std::make_unique<NewExpr>(typeStr, std::move(init), std::move(arraySize));
    node->TypeSyntax = std::move(typeSyntax);
    node->setLocation(kw, m_CurrentFile);
    expr = std::move(node);
  } else if (match(TokenType::LBracket)) {
    // Array literal [1, 2, 3]
    std::vector<std::unique_ptr<Expr>> elements;
    if (!check(TokenType::RBracket)) {
      elements.push_back(parseExpr());
      if (match(TokenType::Semicolon)) {
        auto count = parseExpr();
        consume(TokenType::RBracket, DiagID::ERR_PARSER_EXPECTED_AFTER_REPEAT_COUNT);
        auto node = std::make_unique<RepeatedArrayExpr>(std::move(elements[0]),
                                                        std::move(count));
        node->setLocation(m_Tokens[m_Pos - 1], m_CurrentFile);
        expr = std::move(node);
        return expr; // Return immediately
      }
      while (match(TokenType::Comma)) {
        elements.push_back(parseExpr());
      }
    }
    consume(TokenType::RBracket, DiagID::ERR_PARSER_EXPECTED_AFTER_ARRAY_ELEMENTS);
    if (elements.size() == 1 && check(TokenType::Identifier)) {
      std::unique_ptr<Expr> arraySize = std::move(elements[0]);
      TypeSyntaxPtr typeSyntax = parseTypeSyntax(true, true);
      std::string typeStr = canonicalType(typeSyntax);
      
      std::unique_ptr<Expr> init = nullptr;
      if (check(TokenType::LParen)) {
        consume(TokenType::LParen, DiagID::ERR_EXPECTED_LPAREN);
        bool isNamedInit = isNamedInitList();
        if (isNamedInit) {
          std::vector<std::pair<std::string, std::unique_ptr<Expr>>> fields;
          while (!check(TokenType::RParen) && !check(TokenType::EndOfFile)) {
            if (!isNextNamedField(0)) {
                auto expr = parseExpr();
                if (dynamic_cast<ElisionExpr*>(expr.get())) {
                    fields.push_back({"..", std::move(expr)});
                } else {
                    error(peek(), DiagID::ERR_PARSER_EXPECTED_NAMED_ARGUMENT_KEY_VALUE_OR_EL);
                }
                if (!check(TokenType::RParen)) match(TokenType::Comma);
                continue;
            }

            std::string prefix = "";
            if (match(TokenType::Star)) prefix = "*";
            else if (match(TokenType::Caret)) prefix = "^";
            else if (match(TokenType::Tilde)) prefix = "~";
            else if (match(TokenType::Ampersand)) prefix = "&";
            
            if (match(TokenType::TokenNull)) prefix += "?";
            if (match(TokenType::TokenWrite)) prefix += "#";
            
            Token fieldNameTok = consume(TokenType::Identifier, DiagID::ERR_PARSER_EXPECTED_FIELD_NAME);
            std::string fieldName = fieldNameTok.Text;
            if (fieldNameTok.HasWrite || fieldNameTok.IsBlocked) error(fieldNameTok, DiagID::ERR_ILLEGAL_FIELD_MODIFIER);
            consume(TokenType::Equal, DiagID::ERR_EXPECTED_EQUAL);
            fields.push_back({prefix + fieldName, parseExpr()});
            if (!check(TokenType::RParen)) match(TokenType::Comma);
          }
          consume(TokenType::RParen, DiagID::ERR_EXPECTED_RPAREN);
          auto node = std::make_unique<InitStructExpr>(typeStr, std::move(fields));
          node->setLocation(m_Tokens[m_Pos-1], m_CurrentFile);
          init = std::move(node);
        } else {
          std::vector<std::unique_ptr<Expr>> args;
          if (!check(TokenType::RParen)) {
            do { 
              args.push_back(parseExpr());
            } while (match(TokenType::Comma));
          }
          consume(TokenType::RParen, DiagID::ERR_EXPECTED_RPAREN);
          auto node = std::make_unique<CallExpr>(typeStr, std::move(args));
          node->setLocation(m_Tokens[m_Pos-1], m_CurrentFile);
          init = std::move(node);
        }
      } else {
        error(peek(), DiagID::ERR_PARSER_EXPECTED_INITIALIZER_FOR_ARRAY_INIT_EXP);
      }
      auto node = std::make_unique<ArrayInitExpr>(typeStr, std::move(init), std::move(arraySize));
      node->TypeSyntax = std::move(typeSyntax);
      node->setLocation(m_Tokens[m_Pos-1], m_CurrentFile);
      expr = std::move(node);
    } else {
      expr = std::make_unique<ArrayExpr>(std::move(elements));
    }
  } else if (check(TokenType::LParen)) {
    Token tok = peek();

    // Anonymous Record Detection: ( key = val ... )
    bool isAnonRecord = isNextNamedField(1);

    consume(TokenType::LParen, DiagID::ERR_EXPECTED_LPAREN);

    if (isAnonRecord) {
      std::vector<std::pair<std::string, std::unique_ptr<Expr>>> fields;
      while (!check(TokenType::RParen) && !check(TokenType::EndOfFile)) {
        Token keyTok = consume(TokenType::Identifier, DiagID::ERR_PARSER_EXPECTED_FIELD_NAME);
        std::string key = keyTok.Text;
        consume(TokenType::Equal, DiagID::ERR_EXPECTED_EQUAL);
        auto val = parseExpr();
        fields.push_back({key, std::move(val)});
        if (!check(TokenType::RParen)) {
          consume(TokenType::Comma, DiagID::ERR_PARSER_EXPECTED_OR);
        }
      }
      consume(TokenType::RParen, DiagID::ERR_EXPECTED_RPAREN);
      auto node = std::make_unique<AnonymousRecordExpr>(std::move(fields));
      node->setLocation(tok, m_CurrentFile);
      expr = std::move(node);
    } else {
      // Grouping or deprecated positional tuple literal
      std::vector<std::unique_ptr<Expr>> elements;
      bool isDeprecatedTuple = false;
      if (!check(TokenType::RParen)) {
        auto first = parseExpr();
        if (!first)
          return nullptr;
        elements.push_back(std::move(first));
        if (match(TokenType::Comma)) {
          isDeprecatedTuple = true;
          while (!check(TokenType::RParen) && !check(TokenType::EndOfFile)) {
            size_t elementStart = m_Pos;
            auto element = parseExpr();
            if (element)
              elements.push_back(std::move(element));
            if (m_Pos == elementStart && !check(TokenType::EndOfFile))
              advance();
            match(TokenType::Comma);
          }
        }
      }
      consume(TokenType::RParen, DiagID::ERR_EXPECTED_RPAREN);
      if (isDeprecatedTuple) {
        error(tok, DiagID::ERR_TUPLE_DEPRECATED);
        return nullptr;
      } else if (elements.empty()) {
        auto node = std::make_unique<AnonymousRecordExpr>(std::vector<std::pair<std::string, std::unique_ptr<Expr>>>{});
        node->setLocation(tok, m_CurrentFile);
        expr = std::move(node);
      } else {
        expr = std::move(elements[0]);
        expr->HasParens = true; // [NEW] Track that it was explicitly paren-wrapped
      }
    }
  } else if (check(TokenType::Identifier) || check(TokenType::KwUpperSelf)) {
    Token firstTok = peek();
    std::string nameText;
    if (match(TokenType::KwUpperSelf)) {
      nameText = "Self";
    } else {
      nameText = parseNamespaceOrIdentifier();
    }
    Token name = firstTok;
    name.Text = nameText;

    // [NEW] Check for Generics <...>
    std::vector<std::string> genericArgs;
    std::vector<TypeArgumentSyntax> genericArgSyntax;
    std::string genericSuffix = "";
    if (check(TokenType::GenericLT)) {
      match(TokenType::GenericLT); // consume <
      genericSuffix += "<";
      do {
        TypeArgumentSyntax tySyntax = parseTypeArgumentSyntax();
        std::string ty = tySyntax.toCanonicalString();
        genericArgs.push_back(ty);
        genericArgSyntax.push_back(std::move(tySyntax));
        genericSuffix += ty;
        if (check(TokenType::Comma)) {
          genericSuffix += ", ";
        }
      } while (match(TokenType::Comma));
      consume(TokenType::Greater, DiagID::ERR_EXPECTED_GREATER);
      genericSuffix += ">";
    }

    if (match(TokenType::LParen)) {
      // Function Call or Named Struct Init: Type(...) or Type(x=1)
      bool isNamed = isNamedInitList();

      if (isNamed) {
        std::vector<std::pair<std::string, std::unique_ptr<Expr>>> fields;
        while (!check(TokenType::RParen) && !check(TokenType::EndOfFile)) {
          size_t fieldStart = m_Pos;
          if (!isNextNamedField(0)) {
              auto expr = parseExpr();
              if (dynamic_cast<ElisionExpr*>(expr.get())) {
                  fields.push_back({"..", std::move(expr)});
              } else if (dynamic_cast<SpreadExpr*>(expr.get())) {
                  // Check if it's the last element (allowing trailing comma)
                  if (!check(TokenType::RParen) && !(check(TokenType::Comma) && checkAt(1, TokenType::RParen))) {
                      error(peek(), DiagID::ERR_PARSER_SPREAD_OPERATOR_MUST_STRICTLY_BE_THE_LA);
                  }
                  fields.push_back({"*", std::move(expr)});
              } else if (auto* cedeE = dynamic_cast<CedeExpr*>(expr.get())) {
                  if (dynamic_cast<SpreadExpr*>(cedeE->Value.get())) {
                      if (!check(TokenType::RParen) && !(check(TokenType::Comma) && checkAt(1, TokenType::RParen))) {
                          error(peek(), DiagID::ERR_PARSER_SPREAD_OPERATOR_MUST_STRICTLY_BE_THE_LA);
                      }
                      fields.push_back({"*", std::move(expr)});
                  } else {
                      error(peek(), DiagID::ERR_PARSER_EXPECTED_NAMED_ARGUMENT_KEY_VALUE_OR_EL);
                  }
              } else {
                  error(peek(), DiagID::ERR_PARSER_EXPECTED_NAMED_ARGUMENT_KEY_VALUE_OR_EL);
              }
              if (!check(TokenType::RParen)) match(TokenType::Comma);
              if (m_Pos == fieldStart && !check(TokenType::EndOfFile))
                advance();
              continue;
          }

          std::string prefix = "";
          if (match(TokenType::Star))
            prefix = "*";
          else if (match(TokenType::Caret))
            prefix = "^";
          else if (match(TokenType::Tilde))
            prefix = "~";
          else if (match(TokenType::Ampersand))
            prefix = "&";

          Token fieldNameTok = consume(TokenType::Identifier, DiagID::ERR_PARSER_EXPECTED_FIELD_NAME);
          std::string fieldName = fieldNameTok.Text;
          if (fieldNameTok.HasWrite || fieldNameTok.IsBlocked) error(fieldNameTok, DiagID::ERR_ILLEGAL_FIELD_MODIFIER);
          consume(TokenType::Equal, DiagID::ERR_PARSER_EXPECTED_AFTER_FIELD_NAME);
          fields.push_back({prefix + fieldName, parseExpr()});
          if (!check(TokenType::RParen))
            match(TokenType::Comma);
          if (m_Pos == fieldStart && !check(TokenType::EndOfFile))
            advance();
        }
        consume(TokenType::RParen, DiagID::ERR_PARSER_EXPECTED_AFTER_ARGUMENTS);
        expr = std::make_unique<InitStructExpr>(name.Text + genericSuffix,
                                                std::move(fields));
        expr->setLocation(name, m_CurrentFile);
      } else {
        std::vector<bool> initArguments;
        std::vector<std::unique_ptr<Expr>> args =
            parseCallArguments(initArguments);
        consume(TokenType::RParen, DiagID::ERR_PARSER_EXPECTED_AFTER_ARGUMENTS);
        auto node =
            std::make_unique<CallExpr>(name.Text, std::move(args), genericArgs,
                                       std::move(initArguments));
        node->GenericArgSyntax = genericArgSyntax;
        if (name.HasWrite)
          node->CallableReceiver = CallableReceiverMode::Mutable;
        node->setLocation(name, m_CurrentFile);
        expr = std::move(node);
      }
    } else {
      // Check for Scope Resolution (State::On)
      if (check(TokenType::Colon) && checkAt(1, TokenType::Colon)) {
        consume(TokenType::Colon, DiagID::ERR_PARSER_EXPECTED_COLON);
        consume(TokenType::Colon, DiagID::ERR_PARSER_EXPECTED_COLON);

        Token member;
        if (check(TokenType::Identifier)) {
          member = consume(TokenType::Identifier, DiagID::ERR_PARSER_EXPECTED_MEMBER_AFTER);
        } else if (check(TokenType::KwNew)) {
          member = advance();
          member.Kind = TokenType::Identifier; // Treat as identifier
        } else {
          // Fallback for other potential keywords?
          error(peek(), DiagID::ERR_PARSER_EXPECTED_MEMBER_IDENTIFIER_OR_NEW_AFTER);
          return nullptr;
        }

        std::string fullCallee = name.Text + genericSuffix + "::" + member.Text;

        if (match(TokenType::LParen)) {
          // Function Call on Member
          std::vector<bool> initArguments;
          std::vector<std::unique_ptr<Expr>> args =
              parseCallArguments(initArguments);
          consume(TokenType::RParen, DiagID::ERR_PARSER_EXPECTED_AFTER_ARGUMENTS);
          
          auto node = std::make_unique<CallExpr>(
              fullCallee, std::move(args), std::vector<std::string>{},
              std::move(initArguments));
          node->setLocation(name, m_CurrentFile);
          expr = std::move(node);
        } else {
          auto obj = std::make_unique<VariableExpr>(name.Text + genericSuffix);
          obj->IsValueMutable = name.HasWrite;
          obj->IsValueNullable = name.HasNull;
          obj->IsValueBlocked = name.IsBlocked;
          obj->Permission = BindingPermission::fromLegacy(
              obj->IsRawPointer, obj->IsUnique, obj->IsShared, false, false,
              false, false, obj->IsValueMutable, obj->IsValueNullable,
              obj->IsValueBlocked);
          obj->setLocation(name, m_CurrentFile);
          auto node = std::make_unique<MemberExpr>(std::move(obj), member.Text, true);
          node->setLocation(name, m_CurrentFile);
          expr = std::move(node);
        }
      } else {
        auto var = std::make_unique<VariableExpr>(name.Text + genericSuffix);
        var->setLocation(name, m_CurrentFile);
        var->IsValueMutable = name.HasWrite;
        var->IsValueNullable = name.HasNull;
        var->IsValueBlocked = name.IsBlocked;
        var->Permission = BindingPermission::fromLegacy(
            var->IsRawPointer, var->IsUnique, var->IsShared, false, false,
            false, false, var->IsValueMutable, var->IsValueNullable,
            var->IsValueBlocked);
        expr = std::move(var);
      }
    }
  } else if (match(TokenType::Dot)) {
    // Check if it's .a to .z for implicit closure parameter
    if (!check(TokenType::Identifier)) {
        error(peek(), DiagID::ERR_PARSER_EXPECTED_IMPLICIT_PARAMETER_NAME_A_Z);
        return nullptr;
    }
    Token member = advance();
    if (member.Text.length() == 1 && member.Text[0] >= 'a' && member.Text[0] <= 'z') {
        int index = member.Text[0] - 'a';
        if (index > m_CurrentClosureMaxImplicitArg) m_CurrentClosureMaxImplicitArg = index;
        expr = std::make_unique<VariableExpr>("_arg" + std::to_string(index));
        expr->setLocation(member, m_CurrentFile);
    } else {
        error(member, DiagID::ERR_PARSER_INVALID_IMPLICIT_PARAMETER_EXPECTED_A_T);
        return nullptr;
    }
  } else {
    error(peek(), DiagID::ERR_PARSER_EXPECTED_EXPRESSION);
    return nullptr;
  }

  // Suffixes: .member, [index], etc.
  while (expr) {
    if (dynamic_cast<TodoExpr *>(expr.get()) &&
        (check(TokenType::Dot) || check(TokenType::LBracket) ||
         check(TokenType::PlusPlus) || check(TokenType::MinusMinus) ||
         check(TokenType::DoubleQuestion) || check(TokenType::TokenWrite) ||
         check(TokenType::TokenNull) || check(TokenType::TokenNone) ||
         check(TokenType::Bang))) {
      error(peek(), DiagID::ERR_TYPED_TODO_UNSUPPORTED_CONTEXT);
      return nullptr;
    }
    if (match(TokenType::Dot)) {
      Token dotTok = previous();

      // Check if it's Spread operator .*
      // We must avoid conflicts with obj.*ptr (raw pointer hat member access).
      // If we see Star and the next token is NOT a member name (Identifier, KwUninit, KwNull, KwSelf),
      // we consume the Star and parse it as a SpreadExpr.
      if (check(TokenType::Star) &&
          !checkAt(1, TokenType::Identifier) &&
          !checkAt(1, TokenType::KwUninit) &&
          !checkAt(1, TokenType::KwNull) &&
          !checkAt(1, TokenType::KwSelf)) {
        consume(TokenType::Star, DiagID::ERR_PARSER_EXPECTED_STAR); // Safely consume the Star
        auto node = std::make_unique<SpreadExpr>(std::move(expr));
        node->setLocation(dotTok, m_CurrentFile);
        expr = std::move(node);
        continue;
      }

      std::string prefix = "";
      if (match(TokenType::Star))
        prefix = previous().Text;
      else if (match(TokenType::Caret))
        prefix = previous().Text;
      else if (match(TokenType::Tilde))
        prefix = previous().Text;
      else if (match(TokenType::Ampersand))
        prefix = previous().Text;
      else if (match(TokenType::DoubleQuestion))
        prefix = previous().Text;

      if (match(TokenType::Identifier) || match(TokenType::KwUninit) ||
          match(TokenType::KwNull) || match(TokenType::KwSelf)) {
        std::string memberName = prefix + previous().Text;
        // Method Call check
        if (match(TokenType::LParen)) {
          std::vector<std::unique_ptr<Expr>> args;
          if (!check(TokenType::RParen)) {
            do {
              args.push_back(parseExpr());
            } while (match(TokenType::Comma));
          }
          consume(TokenType::RParen, DiagID::ERR_PARSER_EXPECTED_AFTER_METHOD_ARGUMENTS);
          auto node = std::make_unique<MethodCallExpr>(
              std::move(expr), memberName, std::move(args));
          node->setLocation(dotTok, m_CurrentFile);
          expr = std::move(node);
        } else {
          // Member Access
          auto node = std::make_unique<MemberExpr>(std::move(expr), memberName);
          node->setLocation(dotTok, m_CurrentFile);

          Token nameTok =
              previous(); // The identifier matched at loop start or later?
          // Wait, 'prefix + previous().Text' was used. previous() is the
          // identifier.
          if (nameTok.HasWrite) {
            auto wrapper = std::make_unique<PostfixExpr>(TokenType::TokenWrite,
                                                         std::move(node));
            wrapper->setLocation(nameTok, m_CurrentFile);
            expr = std::move(wrapper);
          } else if (nameTok.HasNull) {
            auto wrapper = std::make_unique<PostfixExpr>(TokenType::TokenNull,
                                                         std::move(node));
            wrapper->setLocation(nameTok, m_CurrentFile);
            expr = std::move(wrapper);
          } else {
            expr = std::move(node);
          }
        }
      } else if (match(TokenType::KwAwait)) {
        Token opTok = previous();
        auto node = std::make_unique<AwaitExpr>(std::move(expr));
        node->CatchesCancellation = match(TokenType::TokenNull);
        node->setLocation(opTok, m_CurrentFile);
        expr = std::move(node);
      } else if (match(TokenType::KwWait)) {
        Token opTok = previous();
        auto node = std::make_unique<WaitExpr>(std::move(expr));
        node->setLocation(opTok, m_CurrentFile);
        expr = std::move(node);
      } else if (prefix.empty() && match(TokenType::Integer)) {
        auto node =
            std::make_unique<MemberExpr>(std::move(expr), previous().Text);
        node->setLocation(dotTok, m_CurrentFile);
        expr = std::move(node);
      } else {
        error(peek(), DiagID::ERR_PARSER_EXPECTED_MEMBER_NAME_OR_INDEX_AFTER);
      }
    } else if (match(TokenType::Arrow)) {
      // [Abolished] Arrow syntax for member access is removed.
      // Use implicit dereference via dot (.) instead.
      error(previous(), DiagID::ERR_PARSER_ARROW_MEMBER_ACCESS_IS_ABOLISHED_USE_DO);
      return nullptr;
    } else if (match(TokenType::LBracket)) {
      std::vector<std::unique_ptr<Expr>> indices;
      if (!check(TokenType::RBracket)) {
        do {
          indices.push_back(parseExpr());
        } while (match(TokenType::Comma));
      }
      consume(TokenType::RBracket, DiagID::ERR_PARSER_EXPECTED_AFTER_INDEX);
      expr =
          std::make_unique<ArrayIndexExpr>(std::move(expr), std::move(indices));
    } else if (match(TokenType::PlusPlus)) {
      expr =
          std::make_unique<PostfixExpr>(TokenType::PlusPlus, std::move(expr));
    } else if (match(TokenType::MinusMinus)) {
      expr =
          std::make_unique<PostfixExpr>(TokenType::MinusMinus, std::move(expr));
    } else if (match(TokenType::DoubleQuestion)) {
      Token opTok = previous();
      auto node = std::make_unique<PostfixExpr>(TokenType::DoubleQuestion,
                                                std::move(expr));
      node->setLocation(opTok, m_CurrentFile);
      expr = std::move(node);
    } else if (match(TokenType::TokenWrite)) {
      Token opTok = previous();
      auto node =
          std::make_unique<PostfixExpr>(TokenType::TokenWrite, std::move(expr));
      node->setLocation(opTok, m_CurrentFile);
      expr = std::move(node);
    } else if (match(TokenType::TokenNull)) {
      Token opTok = previous();
      auto node =
          std::make_unique<PostfixExpr>(TokenType::TokenNull, std::move(expr));
      node->setLocation(opTok, m_CurrentFile);
      expr = std::move(node);
    } else if (match(TokenType::TokenNone)) {
      Token opTok = previous();
      auto node =
          std::make_unique<PostfixExpr>(TokenType::TokenNone, std::move(expr));
      node->setLocation(opTok, m_CurrentFile);
      expr = std::move(node);
    } else if (match(TokenType::Bang)) {
      Token opTok = previous();
      auto node = std::make_unique<UnwrapPropagationExpr>(std::move(expr));
      node->setLocation(opTok, m_CurrentFile);
      expr = std::move(node);
    } else if (allowTrailingClosure && !isEndOfStatement() && isClosureExpression()) {
      // Trailing Closure Syntax
      auto clo = parseClosureExpr();
      if (auto *call = dynamic_cast<CallExpr*>(expr.get())) {
          call->Args.push_back(std::move(clo));
      } else if (auto *mcall = dynamic_cast<MethodCallExpr*>(expr.get())) {
          mcall->Args.push_back(std::move(clo));
      } else if (auto *member = dynamic_cast<MemberExpr*>(expr.get())) {
          std::vector<std::unique_ptr<Expr>> args;
          args.push_back(std::move(clo));
          auto newCall = std::make_unique<MethodCallExpr>(std::move(member->Object), member->Member, std::move(args));
          newCall->Loc = member->Loc;
          expr = std::move(newCall);
      } else if (auto *var = dynamic_cast<VariableExpr*>(expr.get())) {
          std::vector<std::unique_ptr<Expr>> args;
          args.push_back(std::move(clo));
          auto newCall = std::make_unique<CallExpr>(var->Name, std::move(args));
          newCall->Loc = var->Loc;
          expr = std::move(newCall);
      } else {
          error(peek(), DiagID::ERR_PARSER_TRAILING_CLOSURE_APPLIED_TO_INVALID_EXP);
          return nullptr;
      }
    } else {
      break;
    }
  }

  return expr;
}

std::vector<std::unique_ptr<Expr>>
Parser::parseCallArguments(std::vector<bool> &initArguments) {
  std::vector<std::unique_ptr<Expr>> args;
  if (check(TokenType::RParen))
    return args;
  do {
    const bool isInit = check(TokenType::Identifier) && peek().Text == "init" &&
                        checkAt(1, TokenType::Identifier);
    if (isInit)
      advance();
    args.push_back(parseExpr());
    initArguments.push_back(isInit);
  } while (match(TokenType::Comma));
  return args;
}

std::unique_ptr<Expr> Parser::parseUnsafeExpr() {
  Token tok = previous(); // assume KwUnsafe matched
  auto expr = parseExpr();
  auto node = std::make_unique<UnsafeExpr>(std::move(expr));
  node->setLocation(tok, m_CurrentFile);
  return node;
}

std::unique_ptr<Expr> Parser::parseAllocExpr() {
  Token tok = previous(); // assume KwAlloc matched
  bool isArray = false;
  std::unique_ptr<Expr> arraySize = nullptr;

  if (match(TokenType::LBracket)) {
    isArray = true;
    arraySize = parseExpr();
    consume(TokenType::RBracket, DiagID::ERR_EXPECTED_RBRACKET);
  }

  if (!check(TokenType::Identifier)) {
    error(peek(), DiagID::ERR_PARSER_EXPECTED_TYPE_AFTER_NEW);
    return nullptr;
  }
  TypeSyntaxPtr typeSyntax = parseTypeSyntax(true, true, true);
  std::string typeName = canonicalType(typeSyntax);

  std::unique_ptr<Expr> init = nullptr;
  if (match(TokenType::LParen)) {
    // Check if it's named field initialization: Hero(id = 1, hp = 2)
    if (isNamedInitList()) {
      std::vector<std::pair<std::string, std::unique_ptr<Expr>>> fields;
      while (!check(TokenType::RParen) && !check(TokenType::EndOfFile)) {
        if (!isNextNamedField(0)) {
            auto expr = parseExpr();
            if (dynamic_cast<ElisionExpr*>(expr.get())) {
                fields.push_back({"..", std::move(expr)});
            } else {
                error(peek(), DiagID::ERR_PARSER_EXPECTED_NAMED_ARGUMENT_KEY_VALUE_OR_EL);
            }
            if (!check(TokenType::RParen)) match(TokenType::Comma);
            continue;
        }

        std::string prefix = "";
        if (match(TokenType::Star))
          prefix = "*";
        else if (match(TokenType::Caret))
          prefix = "^";
        else if (match(TokenType::Tilde))
          prefix = "~";
        else if (match(TokenType::Ampersand))
          prefix = "&";

        Token fieldNameTok = consume(TokenType::Identifier, DiagID::ERR_PARSER_EXPECTED_FIELD_NAME);
        std::string fieldName = fieldNameTok.Text;
        if (fieldNameTok.HasWrite || fieldNameTok.IsBlocked) error(fieldNameTok, DiagID::ERR_ILLEGAL_FIELD_MODIFIER);
        consume(TokenType::Equal, DiagID::ERR_PARSER_EXPECTED_AFTER_FIELD_NAME);
        fields.push_back({prefix + fieldName, parseExpr()});
        match(TokenType::Comma);
      }
      init = std::make_unique<InitStructExpr>(typeName, std::move(fields));
    } else if (!check(TokenType::RParen)) {
      // Positional args
      std::vector<std::unique_ptr<Expr>> args;
      do {
        args.push_back(parseExpr());
      } while (match(TokenType::Comma));
      init = std::make_unique<CallExpr>(typeName, std::move(args));
    }
    consume(TokenType::RParen, DiagID::ERR_EXPECTED_RPAREN);
  }

  auto node = std::make_unique<AllocExpr>(typeName, std::move(init), isArray,
                                          std::move(arraySize));
  node->TypeSyntax = std::move(typeSyntax);
  node->setLocation(tok, m_CurrentFile);
  return node;
}

std::unique_ptr<Expr> Parser::parseIf() {
  Token tok = previous(); // consumed by match(KwIf)
  if (tok.Kind != TokenType::KwIf)
    tok = consume(TokenType::KwIf, DiagID::ERR_PARSER_EXPECTED_IF);
  bool hasParen = match(TokenType::LParen);
  auto cond = parseExpr(0, false);
  if (hasParen)
    consume(TokenType::RParen, DiagID::ERR_EXPECTED_RPAREN);
  auto thenStmt = parseStmt();
  std::unique_ptr<Stmt> elseStmt;
  if (match(TokenType::KwElse)) {
    elseStmt = parseStmt();
  }
  auto node = std::make_unique<IfExpr>(std::move(cond), std::move(thenStmt),
                                       std::move(elseStmt));
  node->setLocation(tok, m_CurrentFile);
  return node;
}

std::unique_ptr<Expr> Parser::parseGuard() {
  Token tok = previous(); // consumed by match(KwGuard)
  if (tok.Kind != TokenType::KwGuard)
    tok = consume(TokenType::KwGuard, DiagID::ERR_PARSER_EXPECTED_GUARD);
  auto cond = parseExpr(0, false);
  
  auto thenStmt = parseBlock();
  std::unique_ptr<Stmt> elseStmt = nullptr;
  
  if (match(TokenType::KwElse)) {
    if (check(TokenType::LBrace)) {
      elseStmt = parseBlock();
    } else {
      elseStmt = parseStmt();
    }
  }
  
  auto node = std::make_unique<GuardExpr>(std::move(cond), std::move(thenStmt),
                                          std::move(elseStmt));
  node->setLocation(tok, m_CurrentFile);
  return node;
}

std::unique_ptr<Expr> Parser::parseLoop() {
  Token tok = previous();
  if (tok.Kind != TokenType::KwLoop)
    tok = consume(TokenType::KwLoop, DiagID::ERR_PARSER_EXPECTED_LOOP);

  if (check(TokenType::LBrace)) {
    auto body = parseStmt();
    auto node = std::make_unique<LoopExpr>(std::move(body));
    node->setLocation(tok, m_CurrentFile);
    return node;
  }

  bool hasParen = match(TokenType::LParen);
  auto cond = parseExpr(0, false);
  if (hasParen)
    consume(TokenType::RParen, DiagID::ERR_EXPECTED_RPAREN);
  auto body = parseStmt();
  auto node = std::make_unique<LoopExpr>(std::move(cond), std::move(body));
  node->setLocation(tok, m_CurrentFile);
  return node;
}

std::unique_ptr<Expr> Parser::parseForExpr() {
  Token tok = previous();
  if (tok.Kind != TokenType::KwFor)
    tok = consume(TokenType::KwFor, DiagID::ERR_PARSER_EXPECTED_FOR);

  if (match(TokenType::KwLet)) {
    error(previous(), DiagID::ERR_PARSER_FOR_LET_REMOVED);
  } else if (!match(TokenType::KwAuto)) {
    consume(TokenType::KwAuto, DiagID::ERR_PARSER_EXPECTED_AUTO_OR_LET_DECLARATION_IN_FOR);
  }
  std::string morphologyPrefix = "";
  bool isRef = false;
  while (true) {
    if (match(TokenType::Ampersand)) {
      isRef = true;
      morphologyPrefix += "&";
    } else if (match(TokenType::And)) {
      isRef = true;
      morphologyPrefix += "&&";
    } else if (match(TokenType::Caret)) {
      morphologyPrefix += "^";
    } else if (match(TokenType::Star)) {
      morphologyPrefix += "*";
    } else if (match(TokenType::Tilde)) {
      morphologyPrefix += "~";
    } else {
      break;
    }
  }
  Token varName =
      consume(TokenType::Identifier, DiagID::ERR_PARSER_EXPECTED_VARIABLE_NAME_IN_FOR);
  bool isMut = varName.HasWrite;
  consume(TokenType::KwIn, DiagID::ERR_PARSER_EXPECTED_IN_IN_FOR_LOOP);
  auto collection = parseExpr(0, false);
  auto body = parseStmt();
  std::unique_ptr<Stmt> elseBody;
  if (match(TokenType::KwOr)) {
    elseBody = parseBlock();
  }
  auto node = std::make_unique<ForExpr>(varName.Text, isRef, isMut,
                                        std::move(collection), std::move(body),
                                        std::move(elseBody));
  node->MorphologyPrefix = morphologyPrefix;
  node->Permission = BindingPermission::fromLegacy(
      morphologyPrefix == "*", morphologyPrefix == "^", morphologyPrefix == "~",
      isRef, false, false, false, isMut, false, false);
  node->setLocation(tok, m_CurrentFile);
  return node;
}

std::unique_ptr<Expr> Parser::parseBreak() {
  Token tok = previous();
  std::string label = "";
  if (match(TokenType::KwTo)) {
    label = consume(TokenType::Identifier, DiagID::ERR_PARSER_EXPECTED_LABEL_AFTER_TO).Text;
  }
  std::unique_ptr<Expr> val;
  if (!isEndOfStatement() && !check(TokenType::RBrace)) {
    val = parseExpr();
  }
  auto node = std::make_unique<BreakExpr>(label, std::move(val));
  node->setLocation(tok, m_CurrentFile);
  return node;
}

std::unique_ptr<Expr> Parser::parseContinue() {
  Token tok = previous();
  std::string label = "";
  if (match(TokenType::KwTo)) {
    label = consume(TokenType::Identifier, DiagID::ERR_PARSER_EXPECTED_LABEL_AFTER_TO).Text;
  }
  auto node = std::make_unique<ContinueExpr>(label);
  node->setLocation(tok, m_CurrentFile);
  return node;
}

std::unique_ptr<Expr> Parser::parsePass() {
  Token tok = previous();
  auto val = parseExpr();
  auto node = std::make_unique<PassExpr>(std::move(val));
  node->setLocation(tok, m_CurrentFile);
  return node;
}

bool Parser::isClosureExpression() {
  if (check(TokenType::LBrace)) return true;
  
  return false;
}

std::unique_ptr<Expr> Parser::parseClosureExpr() {
  auto expr = std::make_unique<ClosureExpr>();
  expr->setLocation(peek(), m_CurrentFile);

  Token braceTok = consume(TokenType::LBrace, DiagID::ERR_PARSER_EXPECTED_FOR_CLOSURE_BODY);
  expr->Body = std::make_unique<BlockStmt>();
  expr->Body->setLocation(braceTok, m_CurrentFile);
  expr->ReturnType = "unknown";

  // Lookahead to find '=>'
  int lookahead = 0;
  bool hasArrow = false;
  
  auto isCaptureModifier = [](const Token &token) {
    return token.Kind == TokenType::KwCede || token.Kind == TokenType::KwCopy ||
           (token.Kind == TokenType::Identifier && token.Text == "dup");
  };
  if (peekAt(lookahead).Kind == TokenType::LBracket &&
      isCaptureModifier(peekAt(lookahead + 1))) {
      lookahead++; // '['
      while (peekAt(lookahead).Kind != TokenType::RBracket && peekAt(lookahead).Kind != TokenType::EndOfFile) {
          lookahead++;
      }
      if (peekAt(lookahead).Kind == TokenType::RBracket) lookahead++;
  }

  while (true) {
    TokenType t = peekAt(lookahead).Kind;
    if (t == TokenType::FatArrow) {
      hasArrow = true;
      break;
    }
    if (t == TokenType::EndOfFile) break;
    if (t != TokenType::Identifier && t != TokenType::Comma && t != TokenType::Ampersand && t != TokenType::Caret && t != TokenType::Tilde && t != TokenType::Star && t != TokenType::TokenNull && t != TokenType::TokenWrite) {
      break;
    }
    lookahead++;
  }

  if (hasArrow) {
    // 1. Optional Captures inside the braces
    if (match(TokenType::LBracket)) {
      while (!check(TokenType::RBracket) && !check(TokenType::EndOfFile)) {
         CaptureItem cap;
         cap.Loc = peek().Loc;
         if (match(TokenType::KwCede)) cap.Mode = CaptureMode::ExplicitCede;
         else if (match(TokenType::KwCopy)) cap.Mode = CaptureMode::ExplicitCopy;
         else if (check(TokenType::Identifier) && peek().Text == "dup") {
           advance();
           cap.Mode = CaptureMode::ExplicitDup;
         }
         else { error(peek(), DiagID::ERR_PARSER_EXPECTED_CEDE_OR_COPY_MODIFIER_IN_CLOSU); return nullptr; }
         
         std::string prefix = "";
         if (match(TokenType::Tilde)) prefix = "~";
         else if (match(TokenType::Caret)) prefix = "^";
         else if (match(TokenType::Star)) prefix = "*";
         else if (match(TokenType::Ampersand)) prefix = "&";

         if (match(TokenType::TokenNull)) prefix += "?";
         if (match(TokenType::TokenWrite)) prefix += "#";

         cap.Name = prefix + consume(TokenType::Identifier, DiagID::ERR_PARSER_EXPECTED_VARIABLE_NAME_TO_CAPTURE).Text;
         
         expr->ExplicitCaptures.push_back(cap);
         if (!check(TokenType::RBracket)) consume(TokenType::Comma, DiagID::ERR_PARSER_EXPECTED_IN_CAPTURE_LIST);
      }
      consume(TokenType::RBracket, DiagID::ERR_PARSER_EXPECTED_TO_END_CAPTURE_LIST);
    }

    // 2. Explicit typed parameters
    while (!check(TokenType::FatArrow) && !check(TokenType::EndOfFile)) {
      // skip basic sigils if user puts them
      match(TokenType::Caret); match(TokenType::Tilde); match(TokenType::Star); match(TokenType::Ampersand);
      match(TokenType::TokenNull); match(TokenType::TokenWrite);
      if (check(TokenType::FatArrow)) break; // handle zero explicit args `{ [cede x] => ... }`
      Token name = consume(TokenType::Identifier, DiagID::ERR_PARSER_EXPECTED_PARAMETER_NAME);
      expr->ArgNames.push_back(name.Text);
      if (!check(TokenType::FatArrow)) {
        consume(TokenType::Comma, DiagID::ERR_PARSER_EXPECTED_BETWEEN_PARAMETER_NAMES);
      }
    }
    consume(TokenType::FatArrow, DiagID::ERR_PARSER_EXPECTED_AFTER_CLOSURE_PARAMETERS);
  }

  if (!hasArrow && check(TokenType::LBracket) && isCaptureModifier(peekAt(1))) {
      error(peek(), DiagID::ERR_PARSER_EXPECTED_AFTER_CLOSURE_CAPTURE_LIST);
      return nullptr;
  }

  int oldMax = m_CurrentClosureMaxImplicitArg;
  m_CurrentClosureMaxImplicitArg = -1;

  // Parse the rest of the block
  while (!check(TokenType::RBrace) && !check(TokenType::EndOfFile)) {
    auto stmt = parseStmt();
    if (stmt) {
      expr->Body->Statements.push_back(std::move(stmt));
    } else {
      advance();
    }
  }
  consume(TokenType::RBrace, DiagID::ERR_EXPECTED_RBRACE);

  if (!hasArrow && m_CurrentClosureMaxImplicitArg == -1) {
      error(braceTok, DiagID::ERR_PARSER_ZERO_ARGUMENT_CLOSURES_MUST_USE_TO_DISA);
  }

  if (!expr->ArgNames.empty()) {
      expr->HasExplicitArgs = true;
  } else if (hasArrow && m_CurrentClosureMaxImplicitArg == -1) {
      expr->HasExplicitArgs = true;
  } else {
      expr->HasExplicitArgs = false;
  }

  expr->MaxImplicitArgIndex = m_CurrentClosureMaxImplicitArg;
  m_CurrentClosureMaxImplicitArg = oldMax;

  // Implicit Return Transformation:
  // If the last statement is an ExprStmt, convert it into a ReturnStmt.
  if (!expr->Body->Statements.empty()) {
      auto* lastStmt = expr->Body->Statements.back().get();
      if (auto* exprStmt = dynamic_cast<ExprStmt*>(lastStmt)) {
          auto retStmt = std::make_unique<ReturnStmt>(std::move(exprStmt->Expression));
          retStmt->Loc = exprStmt->Loc;
          expr->Body->Statements.back() = std::move(retStmt);
      }
  }

  return expr;
}

} // namespace toka
