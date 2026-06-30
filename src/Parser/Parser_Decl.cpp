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

std::vector<GenericParam> Parser::parseGenericParams() {
  std::vector<GenericParam> genericParams;
  if (match(TokenType::GenericLT)) {
    do {
      GenericParam gp;
      if (match(TokenType::KwConst)) {
        gp.IsConst = true;
      }
      gp.Name = consume(TokenType::Identifier, DiagID::ERR_PARSER_EXPECTED_GENERIC_PARAMETER_NAME).Text;
      if (!gp.Name.empty() && gp.Name[0] == '\'') {
        gp.IsMorphic = true;
        // gp.Name = gp.Name.substr(1); // Leave quote attached to parameter name!
      }
      if (match(TokenType::Colon)) {
        if (match(TokenType::At)) {
          // Trait bounds: <T: @Send> or <T: @{Read, Write}>.
          bool unionBraces = match(TokenType::LBrace);
          do {
            if (unionBraces && match(TokenType::At)) {
              error(previous(), DiagID::ERR_PARSER_TRAIT_BOUND_SET_REQUIRES_AT_PREFIX);
            }
            gp.TraitBounds.push_back(consume(TokenType::Identifier, DiagID::ERR_PARSER_EXPECTED_TRAIT_NAME_IN_CONSTRAINT).Text);
          } while (unionBraces && match(TokenType::Comma));
          if (unionBraces) {
            consume(TokenType::RBrace, DiagID::ERR_PARSER_EXPECTED_CLOSING_TRAIT_BOUNDS);
          }
        } else if (check(TokenType::LBrace)) {
          error(peek(), DiagID::ERR_PARSER_TRAIT_BOUND_SET_REQUIRES_AT_PREFIX);
          advance();
          do {
            match(TokenType::At);
            gp.TraitBounds.push_back(consume(TokenType::Identifier, DiagID::ERR_PARSER_EXPECTED_TRAIT_NAME_IN_CONSTRAINT).Text);
          } while (match(TokenType::Comma));
          consume(TokenType::RBrace, DiagID::ERR_PARSER_EXPECTED_CLOSING_TRAIT_BOUNDS);
        } else {
          // Const generic type
          gp.Type = parseTypeString();
          gp.IsConst = true;
        }
      }
      genericParams.push_back(gp);
    } while (match(TokenType::Comma));
    consume(TokenType::Greater, DiagID::ERR_PARSER_EXPECTED_TO_CLOSE_GENERIC_PARAMETERS);
  }
  return genericParams;
}

std::vector<std::string> Parser::parseTraitFacetTarget() {
  std::vector<std::string> traitBounds;
  if (!match(TokenType::At)) {
    error(peek(), DiagID::ERR_PARSER_WHERE_IMPL_EXPECTED_TRAIT_TARGET);
    if (!isWhereConstraintTerminator()) {
      advance();
    }
    return traitBounds;
  }

  bool unionBraces = match(TokenType::LBrace);
  do {
    if (unionBraces && match(TokenType::At)) {
      error(previous(), DiagID::ERR_PARSER_TRAIT_BOUND_SET_REQUIRES_AT_PREFIX);
    }
    traitBounds.push_back(
        consume(TokenType::Identifier, DiagID::ERR_PARSER_EXPECTED_TRAIT_NAME_IN_CONSTRAINT).Text);
  } while (unionBraces && match(TokenType::Comma));

  if (unionBraces) {
    consume(TokenType::RBrace, DiagID::ERR_PARSER_EXPECTED_CLOSING_TRAIT_BOUNDS);
  }
  return traitBounds;
}

bool Parser::isWhereConstraintTerminator() const {
  if (check(TokenType::EndOfFile) || check(TokenType::LBrace) ||
      check(TokenType::Equal) || check(TokenType::LParen) ||
      check(TokenType::Pipe) || check(TokenType::Dependency)) {
    return true;
  }
  return check(TokenType::Identifier) && peek().Text == "effects" &&
         checkAt(1, TokenType::Colon);
}

void Parser::parseWhereConstraints(std::vector<GenericParam> &genericParams,
                                   std::vector<std::string> *selfTraitBounds) {
  if (!match(TokenType::KwWhere)) {
    return;
  }

  consume(TokenType::Colon, DiagID::ERR_EXPECTED_COLON);
  auto appendUnique = [](std::vector<std::string> &target,
                         const std::vector<std::string> &bounds) {
    for (const auto &bound : bounds) {
      if (std::find(target.begin(), target.end(), bound) == target.end()) {
        target.push_back(bound);
      }
    }
  };

  while (!isWhereConstraintTerminator()) {
    if (!(check(TokenType::Identifier) || check(TokenType::KwUpperSelf))) {
      error(peek(), DiagID::ERR_PARSER_WHERE_EXPECTED_SUBJECT);
      advance();
      continue;
    }

    Token subject = advance();
    consume(TokenType::KwImpl, DiagID::ERR_PARSER_EXPECTED_IMPL);
    std::vector<std::string> bounds = parseTraitFacetTarget();

    if (subject.Text == "Self") {
      if (selfTraitBounds) {
        appendUnique(*selfTraitBounds, bounds);
      } else {
        error(subject, DiagID::ERR_PARSER_WHERE_UNSUPPORTED_SUBJECT,
              subject.Text);
      }
      continue;
    }

    auto paramIt = std::find_if(
        genericParams.begin(), genericParams.end(),
        [&](const GenericParam &gp) {
          if (gp.Name == subject.Text) {
            return true;
          }
          return !gp.Name.empty() && gp.Name[0] == '\'' &&
                 gp.Name.substr(1) == subject.Text;
        });

    if (paramIt == genericParams.end()) {
      error(subject, DiagID::ERR_PARSER_WHERE_UNSUPPORTED_SUBJECT,
            subject.Text);
      continue;
    }
    appendUnique(paramIt->TraitBounds, bounds);
  }
}

std::unique_ptr<ShapeDecl> Parser::parseShape(bool isPub) {
  bool isUnion = false;
  if (match(TokenType::KwUnion)) {
    error(previous(), DiagID::ERR_UNION_DEPRECATED);
    return nullptr;
  } else {
    match(TokenType::KwShape); // Optional if packed
    match(TokenType::KwPacked);
  }

  bool packed = !isUnion && previous().Kind == TokenType::KwPacked;
  if (packed)
    consume(TokenType::KwShape, DiagID::ERR_PARSER_EXPECTED_SHAPE_AFTER_PACKED);

  Token name = consume(TokenType::Identifier, DiagID::ERR_PARSER_EXPECTED_SHAPE_NAME);

  // Parse Generic Parameters: Name<T, U> or Name<T, N_: usize>
  std::vector<GenericParam> genericParams = parseGenericParams();
  parseWhereConstraints(genericParams);

  std::vector<std::string> lifeDeps;
  if (match(TokenType::Dependency)) {
    do {
      if (check(TokenType::Identifier) || check(TokenType::KwSelf) ||
          check(TokenType::KwUpperSelf)) {
        lifeDeps.push_back(advance().Text);
      } else {
        error(peek(), DiagID::ERR_PARSER_EXPECTED_DEPENDENCY_IDENTIFIER);
        return nullptr;
      }
    } while (match(TokenType::Pipe) || match(TokenType::Comma));
  }

  ShapeKind kind = isUnion ? ShapeKind::Union : ShapeKind::Struct;
  std::vector<ShapeMember> members;
  int64_t arraySize = 0;

  match(TokenType::Equal);

  if (match(TokenType::LBracket)) {
    kind = ShapeKind::Array;
    std::string elemTy = advance().Text;
    consume(TokenType::Semicolon, DiagID::ERR_PARSER_EXPECTED_2);
    arraySize = std::stoull(consume(TokenType::Integer, DiagID::ERR_PARSER_EXPECTED_SIZE).Text,
                            nullptr, 0);
    consume(TokenType::RBracket, DiagID::ERR_EXPECTED_RBRACKET);
    ShapeMember m;
    m.Name = "0";
    m.Type = elemTy;
    members.push_back(std::move(m));
  } else if (match(TokenType::KwAs)) {
    error(previous(), DiagID::ERR_UNION_DEPRECATED);
    return nullptr;
  } else if (match(TokenType::LParen)) {
    bool isEnum = false;
    bool isUnion = false;
    int depth = 0;
    for (int i = 0;; ++i) {
      TokenType t = peekAt(i).Kind;
      if (t == TokenType::EndOfFile || (t == TokenType::RParen && depth == 0))
        break;
      if (depth == 0) {
        if (t == TokenType::KwAs) {
          isUnion = true;
          break;
        }
        if (t == TokenType::Pipe) {
          isEnum = true;
          break;
        }
        if (t == TokenType::Colon) {
          isEnum = false;
          break;
        }
        if (t == TokenType::Equal) {
          isEnum = true;
          break;
        }
      }
      if (t == TokenType::LParen)
        depth++;
      else if (t == TokenType::RParen)
        depth--;
    }

    if (isUnion) {
      error(peek(), DiagID::ERR_UNION_DEPRECATED);
      return nullptr;
    } else if (isEnum) {
      kind = ShapeKind::Enum;
      while (!check(TokenType::RParen) && !check(TokenType::EndOfFile)) {
        ShapeMember v;
        v.Name = consume(TokenType::Identifier, DiagID::ERR_PARSER_EXPECTED_VARIANT).Text;
        if (match(TokenType::LParen)) {
          v.SubKind = ShapeKind::Tuple;
          while (!check(TokenType::RParen) && !check(TokenType::EndOfFile)) {
            ShapeMember field;
            field.Type = parseTypeString();
            v.SubMembers.push_back(std::move(field));
            if (!check(TokenType::RParen))
              match(TokenType::Comma);
          }
          consume(TokenType::RParen, DiagID::ERR_EXPECTED_RPAREN);
        }
        if (match(TokenType::Equal)) {
          v.TagValue = std::stoull(
              consume(TokenType::Integer, DiagID::ERR_PARSER_EXPECTED_TAG).Text, nullptr, 0);
        }
        members.push_back(std::move(v));
        if (!check(TokenType::RParen))
          match(TokenType::Pipe);
      }
    } else {
      bool hasColon = false;
      if (peekAt(0).Kind == TokenType::RParen) {
        hasColon = true;
      } else {
        int depth = 0;
        for (int i = 0;; ++i) {
          TokenType t = peekAt(i).Kind;
          if (t == TokenType::EndOfFile || (t == TokenType::RParen && depth == 0))
            break;
          if (depth == 0 && t == TokenType::Colon) {
            hasColon = true;
            break;
          }
          if (t == TokenType::LParen) depth++;
          else if (t == TokenType::RParen) depth--;
        }
      }
      if (!hasColon) {
        error(peek(), DiagID::ERR_TUPLE_DEPRECATED);
        return nullptr;
      }
      kind = ShapeKind::Struct;
      int idx = 0;
      while (!check(TokenType::RParen) && !check(TokenType::EndOfFile)) {
        ShapeMember m;
        Token nameTok;
        bool isExplicitBound = false;
        std::string memberPrefix = "";
        if (kind == ShapeKind::Struct) {
          if (match(TokenType::Backtick)) {
            isExplicitBound = true;
          }

          bool isPtrNullable = match(TokenType::KwNul);
          if (match(TokenType::Star)) {
            memberPrefix = "*";
            m.IsRawPointer = true;
            m.IsRebindable = previous().IsSwappablePtr;
            m.IsPointerNullable = isPtrNullable;
            m.IsRebindBlocked = previous().IsBlocked;
          } else if (match(TokenType::Caret)) {
            memberPrefix = "^";
            m.IsUnique = true;
            m.IsRebindable = previous().IsSwappablePtr;
            m.IsPointerNullable = isPtrNullable;
            m.IsRebindBlocked = previous().IsBlocked;
          } else if (match(TokenType::Tilde)) {
            memberPrefix = "~";
            m.IsShared = true;
            m.IsRebindable = previous().IsSwappablePtr;
            m.IsPointerNullable = isPtrNullable;
            m.IsRebindBlocked = previous().IsBlocked;
          } else if (match(TokenType::Ampersand)) {
            memberPrefix = "&";
            m.IsReference = true;
            m.IsRebindable = previous().IsSwappablePtr;
            m.IsPointerNullable = isPtrNullable;
            m.IsRebindBlocked = previous().IsBlocked;
            if (isPtrNullable) {
              error(previous(), DiagID::ERR_PARSER_BORROWED_POINTERS_CANNOT_BE_NULLABLE);
            }
          } else if (isPtrNullable) {
            error(previous(), DiagID::ERR_PARSER_NUL_CAN_ONLY_BE_APPLIED_TO_POINTER_TYPE);
          }

          nameTok = consume(TokenType::Identifier, DiagID::ERR_PARSER_EXPECTED_FIELD_NAME);
          m.Name = nameTok.Text;
          if (!m.Name.empty() && m.Name[0] == '\'') {
              m.IsMorphicExempt = true;
              m.Name = m.Name.substr(1);
          }
          m.IsValueMutable = nameTok.HasWrite;
          m.IsValueNullable = nameTok.HasNull;
          m.IsValueBlocked = nameTok.IsBlocked;
          consume(TokenType::Colon, DiagID::ERR_EXPECTED_COLON);

          m.Type = "";
        } else {
          m.Name = std::to_string(idx++);
          m.Type = "";
        }

        std::string rawType = parseTypeString();
        if (kind == ShapeKind::Struct) {
          std::string trimmed = rawType;
          size_t start = trimmed.find_first_not_of(" \t\r\n");
          if (start != std::string::npos) {
            trimmed = trimmed.substr(start);
          }
          if (!trimmed.empty() && trimmed[0] == '\'') {
            errorTypeSideMorphicBinding(nameTok, memberPrefix, trimmed);
            rawType = trimmed.substr(1);
          }
          if (!trimmed.empty() && (trimmed[0] == '&' || trimmed[0] == '^' || trimmed[0] == '~' || trimmed[0] == '*')) {
            char sigil = trimmed[0];
            std::string suggestion = std::string(1, sigil) + m.Name + ": " + trimmed.substr(1);
            std::string msg = "Pointer morphology sigil ('" + std::string(1, sigil) + 
                              "') must prefix the member name, not the type name. Did you mean '" + 
                              suggestion + "'?";
            error(nameTok, DiagID::ERR_GENERIC_PARSE, msg);
          }
        }
        m.Type = rawType;
        m.IsExplicitBound = isExplicitBound;
        m.Permission = BindingPermission::fromLegacy(
            m.IsRawPointer, m.IsUnique, m.IsShared, m.IsReference,
            m.IsRebindable, m.IsPointerNullable, m.IsRebindBlocked,
            m.IsValueMutable, m.IsValueNullable, m.IsValueBlocked,
            m.IsMorphicExempt);
        if (match(TokenType::Equal)) {
          m.DefaultValue = parseExpr();
        }
        members.push_back(std::move(m));
        if (!check(TokenType::RParen))
          match(TokenType::Comma);
      }
    }
    match(TokenType::RParen);
  } else {
    error(peek(), DiagID::ERR_PARSER_EXPECTED_OR_AFTER_SHAPE_NAME);
  }

  auto decl = std::make_unique<ShapeDecl>(isPub, name.Text, genericParams, kind,
                                          std::move(members), packed,
                                          std::move(lifeDeps));
  decl->ArraySize = arraySize;
  // decl->FileName = m_CurrentFile;
  decl->setLocation(name, m_CurrentFile);
  return decl;
}

std::unique_ptr<FunctionDecl> Parser::parseFunctionDecl(bool isPub) {
  if (match(TokenType::KwPub))
    isPub = true;
  consume(TokenType::KwFn, DiagID::ERR_PARSER_EXPECTED_FN);
  Token name;
  if (check(TokenType::KwMain)) {
    name = advance();
    name.Kind = TokenType::Identifier;
  } else if (check(TokenType::KwNew)) {
    name = advance();
    name.Text = "new";
    name.Kind = TokenType::Identifier;
  } else if (check(TokenType::Identifier) || check(TokenType::KwFree) ||
             check(TokenType::KwAlloc)) {
    name = advance();
  } else {
    error(peek(), DiagID::ERR_PARSER_EXPECTED_FUNCTION_NAME);
    return nullptr;
  }

  // Parse Generic Parameters: <T, N_: usize>
  std::vector<GenericParam> genericParams = parseGenericParams();

  consume(TokenType::LParen, DiagID::ERR_EXPECTED_LPAREN);
  std::vector<FunctionDecl::Arg> args;
  bool isVariadic = false;
  bool firstArg = true;
  bool hasSeenDefault = false;
  if (!check(TokenType::RParen)) {
    do {
      if (check(TokenType::DotDotDot))
        break;

      bool isCeded = match(TokenType::KwCede);

      if (firstArg && match(TokenType::KwSelf)) {
        FunctionDecl::Arg arg;
        arg.Loc = previous().Loc;
        arg.IsCeded = isCeded;
        arg.Name = "self";
        arg.Type = "Self"; // Default
        arg.IsRawPointer = false;
        // Capture mutability from token (e.g. self#)
        if (previous().HasWrite) {
          // arg.IsMutable = true; // Deprecated
          arg.IsValueMutable = true;
        }
        if (previous().IsBlocked) {
          arg.IsValueBlocked = true;
        }

        // [Fix] Allow explicit type annotation: self: Type
        if (match(TokenType::Colon)) {
          arg.Type = parseTypeString();
        }
        arg.Permission = BindingPermission::fromLegacy(
            arg.IsRawPointer, arg.IsUnique, arg.IsShared, arg.IsReference,
            arg.IsRebindable, arg.IsPointerNullable, arg.IsRebindBlocked,
            arg.IsValueMutable, arg.IsValueNullable, arg.IsValueBlocked,
            arg.IsMorphicExempt);

        args.push_back(std::move(arg));
        firstArg = false;
        continue;
      }
      firstArg = false;

      bool isPtrNullable = match(TokenType::KwNul);
      bool isRef = match(TokenType::Ampersand);
      std::string argPrefix = isRef ? "&" : "";
      bool hasPointer = false;
      bool isUnique = false;
      bool isShared = false;

      bool isRebindable = false;
      bool isRebindBlocked = false;

      if (isRef) {
        Token t = previous();
        isRebindable = t.IsSwappablePtr;
        isRebindBlocked = t.IsBlocked;
        if (isPtrNullable) {
          error(t, DiagID::ERR_PARSER_BORROWED_POINTERS_CANNOT_BE_NULLABLE);
        }
      } else if (match(TokenType::Caret)) {
        argPrefix = "^";
        isUnique = true;
        Token t = previous();
        isRebindable = t.IsSwappablePtr;
        isRebindBlocked = t.IsBlocked;
      } else if (match(TokenType::Star)) {
        argPrefix = "*";
        Token t = previous();
        hasPointer = true;
        isRebindable = t.IsSwappablePtr;
        isRebindBlocked = t.IsBlocked;
      } else if (match(TokenType::Tilde)) {
        argPrefix = "~";
        isShared = true;
        Token t = previous();
        isRebindable = t.IsSwappablePtr;
        isRebindBlocked = t.IsBlocked;
      }
      Token argName;
      if (check(TokenType::Identifier) || check(TokenType::KwSelf) ||
          check(TokenType::KwUpperSelf)) {
        argName = advance();
      } else {
        error(peek(), DiagID::ERR_PARSER_EXPECTED_ARGUMENT_NAME);
        return nullptr;
      }
      std::string argType = "i64"; // fallback
      if (match(TokenType::Colon)) {
        argType = parseTypeString();
      }
      bool nameIsMorphic = !argName.Text.empty() && argName.Text[0] == '\'';
      bool typeIsMorphic = !argType.empty() && argType[0] == '\'';
      if (typeIsMorphic) {
        errorTypeSideMorphicBinding(argName, argPrefix, argType);
        argType = argType.substr(1);
      }
      rejectTypeSideReferenceParameter(argName, argPrefix, argType);

      FunctionDecl::Arg arg;
      arg.Loc = argName.Loc;
      arg.IsCeded = isCeded;
      if (argType.rfind("cede ", 0) == 0) {
        arg.IsCeded = true;
        argType = argType.substr(5);
      }
      arg.Name = argName.Text;
      arg.Type = argType;
      arg.IsRawPointer = hasPointer;
      arg.IsReference = isRef;
      arg.IsMorphicExempt = nameIsMorphic;
      // arg.IsMutable = argName.HasWrite; // Deprecated
      // arg.IsNullable = argName.HasNull; // Deprecated

      // New Permissions
      arg.IsUnique = isUnique;
      arg.IsShared = isShared;
      arg.IsRebindable = isRebindable; // Captured from token
      arg.IsPointerNullable = isPtrNullable;
      arg.IsRebindBlocked = isRebindBlocked;
      arg.IsValueMutable = argName.HasWrite;
      arg.IsValueNullable = argName.HasNull;
      arg.IsValueBlocked = argName.IsBlocked;
      arg.Permission = BindingPermission::fromLegacy(
          arg.IsRawPointer, arg.IsUnique, arg.IsShared, arg.IsReference,
          arg.IsRebindable, arg.IsPointerNullable, arg.IsRebindBlocked,
          arg.IsValueMutable, arg.IsValueNullable, arg.IsValueBlocked,
          arg.IsMorphicExempt);

      if (match(TokenType::Equal)) {
        arg.DefaultValue = parseExpr();
        hasSeenDefault = true;
      } else if (hasSeenDefault) {
        error(previous(), DiagID::ERR_PARSER_DEFAULT_PARAMETERS_MUST_BE_CONTIGUOUS_A);
      }

      if (arg.Name == "self") {
        if (arg.IsRawPointer || arg.IsReference || arg.IsUnique || 
            arg.IsShared || arg.IsPointerNullable) {
          error(argName, DiagID::ERR_SELF_POINTER_SIGIL);
          return nullptr;
        }
      }

      args.push_back(std::move(arg));
    } while (match(TokenType::Comma));
  }
  if (match(TokenType::DotDotDot)) {
    isVariadic = true;
  }
  consume(TokenType::RParen, DiagID::ERR_EXPECTED_RPAREN);

  // Return Type
  std::string retType = "void"; // default
  std::string retName = "";
  EffectKind effect = EffectKind::None;
  if (match(TokenType::Arrow)) {
    if (match(TokenType::KwAsync)) effect = EffectKind::Async;
    else if (match(TokenType::KwWait)) effect = EffectKind::Wait;

    if (!check(TokenType::Dependency) && !check(TokenType::LBrace)) {
      int look = 0;
      if (peekAt(look).Kind == TokenType::KwNul) look++;
      if (peekAt(look).Kind == TokenType::Ampersand || peekAt(look).Kind == TokenType::Star || 
          peekAt(look).Kind == TokenType::Caret || peekAt(look).Kind == TokenType::Tilde) look++;
      if (peekAt(look).Kind == TokenType::TokenWrite) look++;
      
      if (peekAt(look).Kind == TokenType::Identifier && peekAt(look+1).Kind == TokenType::Colon) {
        bool isPtrNullable = match(TokenType::KwNul);
        std::string prefix = "";
        if (isPtrNullable) prefix += "nul ";
        if (match(TokenType::Ampersand)) prefix += "&";
        else if (match(TokenType::Star)) prefix += "*";
        else if (match(TokenType::Caret)) prefix += "^";
        else if (match(TokenType::Tilde)) prefix += "~";
        if (match(TokenType::TokenWrite)) prefix += "#";

        Token nameTok = consume(TokenType::Identifier, DiagID::ERR_PARSER_EXPECTED_RETURN_NAME);
        retName = nameTok.Text;
        consume(TokenType::Colon, DiagID::ERR_EXPECTED_COLON);
        retType = prefix + parseTypeString();
      } else {
        retType = parseTypeString();
      }
    }
  }
  std::vector<std::string> lifeDeps;

  // [NEW] Scan implicitly parsing dependencies from return type string (e.g.
  // Variant payload fields) Pattern: "<-" [whitespace] [&] Identifier
  size_t pos = 0;
  while ((pos = retType.find("<-", pos)) != std::string::npos) {
    pos += 2; // skip "<-"
    // skip whitespace
    while (pos < retType.size() && isspace(retType[pos]))
      pos++;
    // skip optional reference sigil '&'
    if (pos < retType.size() && retType[pos] == '&')
      pos++;

    // Extract identifier
    size_t start = pos;
    if (start < retType.size() &&
        (isalpha(retType[start]) || retType[start] == '_')) {
      while (pos < retType.size() &&
             (isalnum(retType[pos]) || retType[pos] == '_')) {
        pos++;
      }
      std::string dep = retType.substr(start, pos - start);
      if (!dep.empty()) {
        bool exists = false;
        for (const auto &d : lifeDeps)
          if (d == dep)
            exists = true;
        if (!exists)
          lifeDeps.push_back(dep);
      }
    }
  }

  if (match(TokenType::Dependency)) {
    do {
      match(TokenType::Ampersand); // Optional & prefix
      if (check(TokenType::Identifier) || check(TokenType::KwSelf) ||
          check(TokenType::KwUpperSelf)) {
        std::string dep = advance().Text;
        bool exists = false;
        for (const auto &d : lifeDeps)
          if (d == dep)
            exists = true;
        if (!exists)
          lifeDeps.push_back(dep);
      } else {
        error(peek(), DiagID::ERR_PARSER_EXPECTED_DEPENDENCY_IDENTIFIER);
        return nullptr;
      }
    } while (match(TokenType::Pipe) || match(TokenType::Comma));
  }

  parseWhereConstraints(genericParams);

  std::map<std::string, std::vector<std::string>> memberDeps;

  if (check(TokenType::Identifier) && peek().Text == "effects" && checkAt(1, TokenType::Colon)) {
    advance(); // get 'effects'
    advance(); // get ':'
    while (!check(TokenType::LBrace) && !check(TokenType::EndOfFile)) {
      bool isReturnAlias = false;
      std::string targetMember = "";
      if (match(TokenType::KwReturn)) {
        if (check(TokenType::Dot)) {
          advance();
          if (check(TokenType::Ampersand) || check(TokenType::Caret) || check(TokenType::Star) || check(TokenType::Tilde)) advance();
          if (check(TokenType::TokenWrite)) advance();
          if (check(TokenType::Identifier) || check(TokenType::Integer)) {
              if (check(TokenType::Identifier)) targetMember = advance().Text;
              else targetMember = advance().Text;
          }
        }
        isReturnAlias = true;
      } else if (!retName.empty()) {
        int l = 0;
        if (peekAt(l).Kind == TokenType::KwNul) l++;
        if (peekAt(l).Kind == TokenType::Ampersand || peekAt(l).Kind == TokenType::Star || 
            peekAt(l).Kind == TokenType::Caret || peekAt(l).Kind == TokenType::Tilde) l++;
        if (peekAt(l).Kind == TokenType::TokenWrite) l++;

        if (peekAt(l).Kind == TokenType::Identifier && peekAt(l).Text == retName) {
          int p = l + 1;
          std::string testMember;
          if (peekAt(p).Kind == TokenType::Dot) {
            p++;
            if (peekAt(p).Kind == TokenType::Ampersand || peekAt(p).Kind == TokenType::Star || 
                peekAt(p).Kind == TokenType::Caret || peekAt(p).Kind == TokenType::Tilde) p++;
            if (peekAt(p).Kind == TokenType::TokenWrite) p++;
            if (peekAt(p).Kind == TokenType::Identifier || peekAt(p).Kind == TokenType::Integer) {
              testMember = peekAt(p).Text;
              p++;
            }
          }
          if (peekAt(p).Kind == TokenType::Dependency) {
            for (int i=0; i<p; i++) advance(); // consume everything up to Dependency token
            targetMember = testMember;
            isReturnAlias = true;
          }
        }
      }

      if (isReturnAlias) {
        consume(TokenType::Dependency, DiagID::ERR_PARSER_EXPECTED_AFTER_LHS_IN_EFFECTS_BLOCK);
        do {
          match(TokenType::Ampersand); // Optional & prefix
          if (check(TokenType::Identifier) || check(TokenType::KwSelf) ||
              check(TokenType::KwUpperSelf)) {
            std::string dep = advance().Text;
            while (match(TokenType::Dot)) {
                if (check(TokenType::Identifier) || check(TokenType::Integer)) {
                    dep += "." + advance().Text;
                } else {
                    error(peek(), DiagID::ERR_PARSER_EXPECTED_IDENTIFIER_OR_INTEGER_AFTER_IN);
                    return nullptr;
                }
            }
            if (targetMember.empty()) {
              bool exists = false;
              for (const auto &d : lifeDeps)
                if (d == dep)
                  exists = true;
              if (!exists)
                lifeDeps.push_back(dep);
            } else {
              bool exists = false;
              for (const auto &d : memberDeps[targetMember])
                if (d == dep)
                  exists = true;
              if (!exists)
                memberDeps[targetMember].push_back(dep);
            }
          } else {
            error(peek(), DiagID::ERR_PARSER_EXPECTED_DEPENDENCY_IDENTIFIER);
            return nullptr;
          }
        } while (match(TokenType::Pipe) || match(TokenType::Comma));
      } else {
        if (isEndOfStatement()) {
          break;
        }
        error(peek(), DiagID::ERR_PARSER_ONLY_RETURN_OR_NAMED_RETURN_LHS_IS_CURR);
        return nullptr;
      }
    }
  }

  bool isDeleted = false; // [NEW] Track = delete
  std::unique_ptr<BlockStmt> body = nullptr;
  if (match(TokenType::Equal)) {
    consume(TokenType::KwDelete, DiagID::ERR_PARSER_EXPECTED_DELETE_AFTER_FOR_DELETED_FUNCT);
    isDeleted = true;
    expectEndOfStatement();
  } else if (check(TokenType::LBrace)) {
    body = parseBlock();
  } else {
    expectEndOfStatement();
  }
  auto decl = std::make_unique<FunctionDecl>(
      isPub, name.Text, std::move(args), std::move(body), retType,
      genericParams, std::move(lifeDeps), effect);
  decl->IsDeleted = isDeleted; // [NEW] Attach to Node
  decl->IsVariadic = isVariadic;
  decl->MemberDependencies = std::move(memberDeps);
  decl->setLocation(name, m_CurrentFile);
  return decl;
}

std::unique_ptr<ExternDecl> Parser::parseExternDecl() {
  consume(TokenType::KwExtern, DiagID::ERR_PARSER_EXPECTED_EXTERN);
  consume(TokenType::KwFn, DiagID::ERR_PARSER_EXPECTED_FN);
  Token name;
  if (check(TokenType::Identifier) || check(TokenType::KwFree) ||
      check(TokenType::KwAlloc)) {
    name = advance();
  } else {
    error(peek(), DiagID::ERR_PARSER_EXPECTED_EXTERNAL_FUNCTION_NAME);
    return nullptr;
  }
  consume(TokenType::LParen, DiagID::ERR_EXPECTED_LPAREN);
  std::vector<ExternDecl::Arg> args;
  bool isVariadic = false;
  if (!check(TokenType::RParen)) {
    do {
      if (check(TokenType::DotDotDot))
        break;
      bool isCeded = match(TokenType::KwCede);
      bool isPtrNullable = match(TokenType::KwNul);
      bool hasPointer = false;
      std::string argPrefix = "";
      if (match(TokenType::Caret)) {
        hasPointer = true;
        argPrefix = "^";
      } else if (match(TokenType::Star)) {
        hasPointer = true;
        argPrefix = "*";
      }
      Token argName = consume(TokenType::Identifier, DiagID::ERR_PARSER_EXPECTED_ARGUMENT_NAME);
      std::string argType = "i64";
      if (match(TokenType::Colon)) {
        argType = parseTypeString();
      }
      bool nameIsMorphic = !argName.Text.empty() && argName.Text[0] == '\'';
      bool typeIsMorphic = !argType.empty() && argType[0] == '\'';
      if (typeIsMorphic) {
        errorTypeSideMorphicBinding(argName, argPrefix, argType);
        argType = argType.substr(1);
      }
      rejectTypeSideReferenceParameter(argName, argPrefix, argType);

      ExternDecl::Arg arg;
      arg.Loc = argName.Loc;
      arg.IsCeded = isCeded;
      if (argType.rfind("cede ", 0) == 0) {
        arg.IsCeded = true;
        argType = argType.substr(5);
      }
      arg.Name = argName.Text;
      arg.Type = argType;
      arg.IsRawPointer = hasPointer;
      arg.IsPointerNullable = isPtrNullable;
      arg.IsValueMutable = argName.HasWrite;
      arg.IsValueNullable = argName.HasNull;
      arg.IsMorphicExempt = nameIsMorphic;
      arg.Permission = BindingPermission::fromLegacy(
          arg.IsRawPointer, arg.IsUnique, arg.IsShared, arg.IsReference,
          arg.IsRebindable, arg.IsPointerNullable, arg.IsRebindBlocked,
          arg.IsValueMutable, arg.IsValueNullable, arg.IsValueBlocked,
          arg.IsMorphicExempt);
      if (match(TokenType::Equal)) {
        arg.DefaultValue = parseExpr();
      }
      args.push_back(std::move(arg));
    } while (match(TokenType::Comma));
  }
  if (match(TokenType::DotDotDot)) {
    isVariadic = true;
  }
  consume(TokenType::RParen, DiagID::ERR_EXPECTED_RPAREN);

  std::string retType = "void";
  EffectKind effect = EffectKind::None;
  if (match(TokenType::Arrow)) {
    if (match(TokenType::KwAsync)) effect = EffectKind::Async;
    else if (match(TokenType::KwWait)) effect = EffectKind::Wait;

    if (!check(TokenType::Semicolon)) {
      retType = parseTypeString();
    }
  }
  expectEndOfStatement();

  auto node = std::make_unique<ExternDecl>(name.Text, std::move(args), retType, effect);
  node->setLocation(name, m_CurrentFile);
  node->IsVariadic = isVariadic;
  return node;
}

std::unique_ptr<ImportDecl> Parser::parseImport(bool isPub) {
  Token importTok = consume(TokenType::KwImport, DiagID::ERR_PARSER_EXPECTED_IMPORT);
  std::string physicalPath;

  // 1. Parse Physical Path (Segments)
  while (true) {
    if (peek().HasNewlineBefore)
      break;

    if (check(TokenType::KwAs))
      break;

    bool consumed = false;
    if (check(TokenType::Identifier) || (peek().Kind >= TokenType::KwLet &&
                                         peek().Kind <= TokenType::KwCrate)) {
      physicalPath += advance().Text;
      consumed = true;
    } else if (match(TokenType::Minus)) {
      physicalPath += "-";
      consumed = true;
    } else if (match(TokenType::Dot)) {
      physicalPath += ".";
      consumed = true;
    } else if (match(TokenType::DotDot)) {
      physicalPath += "..";
      consumed = true;
    } else if (match(TokenType::Slash)) {
      physicalPath += "/";
      consumed = true;
    }

    if (!consumed)
      break;
  }

  if (physicalPath == "sys/os" || physicalPath.rfind("sys/os/", 0) == 0) {
    std::string targetOS = "linux";
    if (!Parser::TargetTriple.empty()) {
      std::string triple = Parser::TargetTriple;
      if (triple.find("linux") != std::string::npos) {
        targetOS = "linux";
      } else if (triple.find("apple") != std::string::npos || triple.find("darwin") != std::string::npos || triple.find("ios") != std::string::npos) {
        targetOS = "macos";
      } else if (triple.find("windows") != std::string::npos || triple.find("mingw") != std::string::npos || triple.find("msvc") != std::string::npos || triple.find("win32") != std::string::npos) {
        targetOS = "windows";
      } else if (triple.find("wasi") != std::string::npos || triple.find("wasm32") != std::string::npos || triple.find("wasm64") != std::string::npos) {
        targetOS = "wasi";
      }
    } else {
#if defined(__linux__)
      targetOS = "linux";
#elif defined(__APPLE__)
      targetOS = "macos";
#elif defined(_WIN32)
      targetOS = "windows";
#endif
    }
    
    if (physicalPath == "sys/os") {
      physicalPath = "sys/" + targetOS;
    } else {
      physicalPath = "sys/" + targetOS + "/" + physicalPath.substr(7);
    }
  }

  std::vector<ImportItem> items;
  std::string moduleAlias;

  // 2. Parse Logical Items (::)
  // Check for :: (colon colon)
  if (check(TokenType::Colon) && checkAt(1, TokenType::Colon)) {
    advance(); // :
    advance(); // :

    if (match(TokenType::Star)) {
      items.push_back({"*", ""});
    } else if (match(TokenType::LBrace)) {
      while (!check(TokenType::RBrace) && !check(TokenType::EndOfFile)) {
        std::string symName;
        if (match(TokenType::At)) {
          // Consume @ but don't include in name for lookup
        }
        symName += consume(TokenType::Identifier, DiagID::ERR_PARSER_EXPECTED_SYMBOL_NAME).Text;
        std::string alias;
        if (match(TokenType::KwAs)) {
          alias = consume(TokenType::Identifier, DiagID::ERR_PARSER_EXPECTED_ALIAS).Text;
        }
        items.push_back({symName, alias});
        if (!match(TokenType::Comma))
          break;
      }
      consume(TokenType::RBrace, DiagID::ERR_EXPECTED_RBRACE);
    } else {
      std::string symName;
      if (match(TokenType::At)) {
        // Consume @ but don't include in name for lookup
      }
      symName += consume(TokenType::Identifier, DiagID::ERR_PARSER_EXPECTED_SYMBOL_NAME).Text;
      std::string alias;
      if (match(TokenType::KwAs)) {
        alias = consume(TokenType::Identifier, DiagID::ERR_PARSER_EXPECTED_ALIAS).Text;
      }
      items.push_back({symName, alias});
    }
  } else {
    // 3. Optional Module Alias (only if no logical items were parsed)
    if (match(TokenType::KwAs)) {
      moduleAlias =
          consume(TokenType::Identifier, DiagID::ERR_PARSER_EXPECTED_MODULE_ALIAS).Text;
      while (match(TokenType::Minus)) {
        moduleAlias += "-";
        moduleAlias +=
            consume(TokenType::Identifier, DiagID::ERR_PARSER_EXPECTED_IDENTIFIER_AFTER_IN_MODULE_ALI).Text;
      }
    }
  }

  // Special handling: 'import ... :: *' ends with *, which isEndOfStatement
  // thinks is a binary op. We manually allow newline or semicolon here to
  // avoid that check.
  if (peek().HasNewlineBefore || check(TokenType::Semicolon) ||
      check(TokenType::EndOfFile) || check(TokenType::RBrace)) {
    match(TokenType::Semicolon);
  } else {
    expectEndOfStatement();
  }

  auto decl =
      std::make_unique<ImportDecl>(isPub, physicalPath, moduleAlias, items);
  decl->setLocation(importTok, m_CurrentFile);
  return decl;
}

std::unique_ptr<TypeAliasDecl> Parser::parseTypeAliasDecl(bool isPub) {
  bool isStrong = false;
  if (match(TokenType::KwType)) {
    isStrong = true;
  } else {
    consume(TokenType::KwAlias, DiagID::ERR_PARSER_EXPECTED_ALIAS_OR_TYPE);
  }
  Token name = consume(TokenType::Identifier, DiagID::ERR_PARSER_EXPECTED_TYPE_ALIAS_NAME);

  // Parse Generic Parameters: <T, U>
  std::vector<GenericParam> genericParams = parseGenericParams();

  consume(TokenType::Equal, DiagID::ERR_EXPECTED_EQUAL);

  std::string targetType = parseTypeString();

  expectEndOfStatement();

  auto decl = std::make_unique<TypeAliasDecl>(isPub, name.Text, targetType,
                                              isStrong, genericParams);
  decl->setLocation(name, m_CurrentFile);
  return decl;
}

bool Parser::isAssociatedTypeDeclStart() const {
  return check(TokenType::KwType) ||
         (check(TokenType::Identifier) && peek().Text == "per" &&
          checkAt(1, TokenType::KwType));
}

AssociatedTypeDecl Parser::parseAssociatedTypeDecl(bool requireDefinition) {
  AssociatedTypeDecl decl;
  Token startTok = peek();
  if (check(TokenType::Identifier) && peek().Text == "per" &&
      checkAt(1, TokenType::KwType)) {
    decl.IsPer = true;
    advance();
  }

  consume(TokenType::KwType, DiagID::ERR_PARSER_EXPECTED_ASSOCIATED_TYPE);
  Token name =
      consume(TokenType::Identifier, DiagID::ERR_PARSER_EXPECTED_ASSOCIATED_TYPE_NAME);
  decl.Name = name.Text;
  decl.Loc = name.Loc;

  if (requireDefinition) {
    consume(TokenType::Equal, DiagID::ERR_PARSER_ASSOCIATED_TYPE_EXPECTED_EQUAL);
    decl.Type = parseTypeString();
  } else if (match(TokenType::Equal)) {
    error(startTok, DiagID::ERR_PARSER_TRAIT_ASSOCIATED_TYPE_CANNOT_HAVE_DEFAULT);
    decl.Type = parseTypeString();
  }

  expectEndOfStatement();
  return decl;
}

std::unique_ptr<ImplDecl> Parser::parseImpl() {
  Token startTok = consume(TokenType::KwImpl, DiagID::ERR_PARSER_EXPECTED_IMPL);

  // 1. [NEW] Parse Generic Parameters <T, U>
  std::vector<GenericParam> genericParams = parseGenericParams();

  // 2. Parse First Type/Trait String (Not just Identifier)
  // This allows "Box<T>" or "Iterator<T>"
  std::string firstTypeStr = parseTypeString(false);

  std::string traitName;
  std::string typeName;

  if (match(TokenType::At)) {
    // impl Type@Trait
    typeName = firstTypeStr;
    // Trait might also be generic? For now assume identifier or
    // parseTypeString? Let's assume Traits are strictly Identifiers for now,
    // or use parseTypeString if Traits can be generic. Existing code used
    // Identifier. Let's upgrade to parseTypeString for future proofing or
    // consistency.
    traitName = parseTypeString();
  } else if (match(TokenType::KwFor)) {
    // impl Trait for Type
    traitName = firstTypeStr;
    typeName = parseTypeString();
  } else {
    // impl Type
    typeName = firstTypeStr;
  }

  if (!traitName.empty() && traitName[0] == '@') {
    traitName = traitName.substr(1);
  }

  parseWhereConstraints(genericParams);

  consume(TokenType::LBrace, DiagID::ERR_EXPECTED_LBRACE);

  std::vector<std::unique_ptr<FunctionDecl>> methods;
  std::vector<EncapEntry> encapEntries;
  std::vector<AssociatedTypeDecl> associatedTypes;

  if (traitName == "encap") {
    while (!check(TokenType::RBrace) && !check(TokenType::EndOfFile)) {
      if ((check(TokenType::KwFn)) ||
          (check(TokenType::KwPub) && checkAt(1, TokenType::KwFn))) {
        // Parse as function
        bool isPub = false;
        if (match(TokenType::KwPub)) {
          isPub = true;
        }
        methods.push_back(parseFunctionDecl(isPub));
      } else if (match(TokenType::KwPub)) {
        EncapEntry entry;
        entry.Level = EncapEntry::Global;

        if (match(TokenType::LParen)) {
          if (match(TokenType::KwCrate)) {
            entry.Level = EncapEntry::Crate;
          } else {
            entry.Level = EncapEntry::Path;
            // Parse targeted path segments. Match import path permissiveness so
            // pub(tests/pass) and pub(core/str) are valid member grants.
            while (check(TokenType::Identifier) ||
                   (peek().Kind >= TokenType::KwLet &&
                    peek().Kind <= TokenType::KwCrate) ||
                   check(TokenType::Slash) || check(TokenType::Colon) ||
                   check(TokenType::Minus) || check(TokenType::Dot) ||
                   check(TokenType::DotDot)) {
              if (match(TokenType::Slash)) {
                entry.TargetPath += "/";
              } else if (match(TokenType::Colon)) {
                entry.TargetPath += ":";
              } else if (match(TokenType::Minus)) {
                entry.TargetPath += "-";
              } else if (match(TokenType::Dot)) {
                entry.TargetPath += ".";
              } else if (match(TokenType::DotDot)) {
                entry.TargetPath += "..";
              } else {
                entry.TargetPath += advance().Text;
              }
            }
          }
          consume(TokenType::RParen, DiagID::ERR_EXPECTED_RPAREN);
        }

        if (match(TokenType::Star)) {
          entry.IsExclusion = true;
          match(TokenType::Bang); // Optional !
          while (check(TokenType::Identifier)) {
            entry.Fields.push_back(advance().Text);
            if (!match(TokenType::Comma))
              break;
          }
        } else {
          // One or more fields
          while (check(TokenType::Identifier)) {
            entry.Fields.push_back(advance().Text);
            if (!match(TokenType::Comma))
              break;
          }
        }
        encapEntries.push_back(std::move(entry));
      } else if (check(TokenType::KwFn)) {
        // Non-pub function (private to trait impl?)
        methods.push_back(parseFunctionDecl(false));
      } else {
        error(peek(), DiagID::ERR_PARSER_EXPECTED_PUB_OR_FN_INSIDE_ENCAP_BLOCK);
        advance();
      }
    }
  } else {
    while (!check(TokenType::RBrace) && !check(TokenType::EndOfFile)) {
      if (isAssociatedTypeDeclStart()) {
        associatedTypes.push_back(parseAssociatedTypeDecl(true));
        continue;
      }

      bool isPub = false;
      if (match(TokenType::KwPub)) {
        isPub = true;
      }

      if (check(TokenType::KwFn)) {
        methods.push_back(parseFunctionDecl(isPub));
      } else {
        error(peek(), DiagID::ERR_PARSER_EXPECTED_METHOD_IN_IMPL_BLOCK);
        advance();
      }
    }
  }
  consume(TokenType::RBrace, DiagID::ERR_EXPECTED_RBRACE);

  auto decl = std::make_unique<ImplDecl>(typeName, std::move(methods),
                                         traitName, genericParams);
  decl->EncapEntries = std::move(encapEntries);
  decl->AssociatedTypes = std::move(associatedTypes);
  decl->setLocation(startTok, m_CurrentFile);
  return decl;
}

std::unique_ptr<TraitDecl> Parser::parseTrait(bool isPub) {
  consume(TokenType::KwTrait, DiagID::ERR_PARSER_EXPECTED_TRAIT);
  if (!match(TokenType::At)) {
    if (check(TokenType::Identifier)) {
      error(peek(), DiagID::ERR_PARSER_TRAIT_REQUIRES_AT_PREFIX,
            peek().Text, peek().Text);
    } else {
      error(peek(), DiagID::ERR_PARSER_TRAIT_REQUIRES_AT_PREFIX,
            "<Name>", "<Name>");
    }
  }
  Token name = consume(TokenType::Identifier, DiagID::ERR_PARSER_EXPECTED_TRAIT_NAME);
  std::vector<GenericParam> genericParams = parseGenericParams();
  std::vector<std::string> selfTraitBounds;
  parseWhereConstraints(genericParams, &selfTraitBounds);
  consume(TokenType::LBrace, DiagID::ERR_EXPECTED_LBRACE);

  std::vector<std::unique_ptr<FunctionDecl>> methods;
  std::vector<AssociatedTypeDecl> associatedTypes;
  while (!check(TokenType::RBrace) && !check(TokenType::EndOfFile)) {
    if (isAssociatedTypeDeclStart()) {
      associatedTypes.push_back(parseAssociatedTypeDecl(false));
      continue;
    }

    bool isPub = false;
    if (match(TokenType::KwPub)) {
      isPub = true;
    }
    if (check(TokenType::KwFn)) {
      methods.push_back(parseFunctionDecl(isPub));
    } else {
      error(peek(), DiagID::ERR_PARSER_EXPECTED_METHOD_PROTOTYPE_IN_TRAIT);
    }
  }
  consume(TokenType::RBrace, DiagID::ERR_EXPECTED_RBRACE);
  return std::make_unique<TraitDecl>(isPub, name.Text, std::move(methods),
                                     std::move(genericParams),
                                     std::move(selfTraitBounds),
                                     std::move(associatedTypes));
}

} // namespace toka
