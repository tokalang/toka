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

std::string Parser::parseTraitBoundName() {
  if (!check(TokenType::Identifier)) {
    return consume(TokenType::Identifier,
                   DiagID::ERR_PARSER_EXPECTED_TRAIT_NAME_IN_CONSTRAINT)
        .Text;
  }
  return canonicalType(parseTypeSyntax());
}

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
            gp.TraitBounds.push_back(parseTraitBoundName());
          } while (unionBraces && match(TokenType::Comma));
          if (unionBraces) {
            consume(TokenType::RBrace, DiagID::ERR_PARSER_EXPECTED_CLOSING_TRAIT_BOUNDS);
          }
        } else if (check(TokenType::LBrace)) {
          error(peek(), DiagID::ERR_PARSER_TRAIT_BOUND_SET_REQUIRES_AT_PREFIX);
          advance();
          do {
            match(TokenType::At);
            gp.TraitBounds.push_back(parseTraitBoundName());
          } while (match(TokenType::Comma));
          consume(TokenType::RBrace, DiagID::ERR_PARSER_EXPECTED_CLOSING_TRAIT_BOUNDS);
        } else {
          // Const generic type
          gp.TypeSyntax = parseRequiredTypeSyntax();
          gp.Type = canonicalType(gp.TypeSyntax);
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
    traitBounds.push_back(parseTraitBoundName());
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
    if (!match(TokenType::Colon)) {
      if (match(TokenType::KwImpl)) {
        error(previous(), DiagID::ERR_PARSER_WHERE_IMPL_REMOVED);
      } else {
        consume(TokenType::Colon, DiagID::ERR_PARSER_EXPECTED_WHERE_RELATION);
      }
    }
    std::vector<std::string> bounds;
    std::optional<MorphologyConstraintKind> morphologyBound;
    if (check(TokenType::Identifier) && peek().Text == "morphology") {
      advance();
      Token constraint = consume(
          TokenType::Identifier,
          DiagID::ERR_PARSER_EXPECTED_TRAIT_NAME_IN_CONSTRAINT);
      if (constraint.Text == "soul_only") {
        morphologyBound = MorphologyConstraintKind::SoulOnly;
      } else if (constraint.Text == "borrow_extendable") {
        morphologyBound = MorphologyConstraintKind::BorrowExtendable;
      } else if (constraint.Text == "raw_extendable") {
        morphologyBound = MorphologyConstraintKind::RawExtendable;
      } else {
        error(constraint, DiagID::ERR_PARSER_UNKNOWN_MORPHOLOGY_CONSTRAINT,
              constraint.Text);
      }
    } else {
      bounds = parseTraitFacetTarget();
    }

    if (subject.Text == "Self") {
      if (morphologyBound.has_value()) {
        error(subject, DiagID::ERR_PARSER_WHERE_UNSUPPORTED_SUBJECT,
              subject.Text);
      } else if (selfTraitBounds) {
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
    if (morphologyBound.has_value() &&
        std::find(paramIt->MorphologyBounds.begin(),
                  paramIt->MorphologyBounds.end(), *morphologyBound) ==
            paramIt->MorphologyBounds.end()) {
      paramIt->MorphologyBounds.push_back(*morphologyBound);
    }
    appendUnique(paramIt->TraitBounds, bounds);
  }
}

bool Parser::looksLikeNamedReturn() const {
  int look = 0;
  if (peekAt(look).Kind == TokenType::KwNul)
    look++;
  if (peekAt(look).Kind == TokenType::Ampersand ||
      peekAt(look).Kind == TokenType::Star ||
      peekAt(look).Kind == TokenType::Caret ||
      peekAt(look).Kind == TokenType::Tilde)
    look++;
  if (peekAt(look).Kind == TokenType::TokenWrite)
    look++;
  return peekAt(look).Kind == TokenType::Identifier &&
         peekAt(look + 1).Kind == TokenType::Colon;
}

std::vector<DependencyPathSyntax>
Parser::parseReturnDependencySources(bool allowMemberPath) {
  std::vector<DependencyPathSyntax> sources;
  do {
    Token begin = peek();
    const bool isReference = match(TokenType::Ampersand);
    if (!(check(TokenType::Identifier) || check(TokenType::KwSelf) ||
          check(TokenType::KwUpperSelf))) {
      error(peek(), DiagID::ERR_PARSER_EXPECTED_DEPENDENCY_IDENTIFIER);
      return {};
    }

    Token root = advance();
    DependencyPathSyntax source;
    source.Root = root.Text;
    source.IsReference = isReference;
    source.Begin = begin.Loc;
    source.End = root.Loc;
    while (allowMemberPath && match(TokenType::Dot)) {
      if (!(check(TokenType::Identifier) || check(TokenType::Integer))) {
        error(peek(),
              DiagID::ERR_PARSER_EXPECTED_IDENTIFIER_OR_INTEGER_AFTER_IN);
        return {};
      }
      Token member = advance();
      source.Members.push_back(member.Text);
      source.End = member.Loc;
    }
    sources.push_back(std::move(source));
  } while (match(TokenType::Pipe) || match(TokenType::Comma));
  return sources;
}

bool Parser::parseReturnDependencyTarget(
    const ReturnContractSyntax &contract, ReturnDependencyTargetSyntax &target) {
  Token begin = peek();
  target.Begin = begin.Loc;
  target.End = begin.Loc;
  if (match(TokenType::KwReturn)) {
    target.Kind = ReturnDependencyTargetKind::ReturnValue;
    target.End = previous().Loc;
  } else {
    if (contract.BindingName.empty())
      return false;

    target.Kind = ReturnDependencyTargetKind::NamedBinding;
    if (match(TokenType::KwNul))
      target.BindingPrefix += "nul ";
    if (match(TokenType::Ampersand))
      target.BindingPrefix += "&";
    else if (match(TokenType::Caret))
      target.BindingPrefix += "^";
    else if (match(TokenType::Star))
      target.BindingPrefix += "*";
    else if (match(TokenType::Tilde))
      target.BindingPrefix += "~";
    if (match(TokenType::TokenWrite))
      target.BindingPrefix += "#";
    if (!check(TokenType::Identifier) ||
        peek().Text != contract.BindingName)
      return false;
    target.BindingName = advance().Text;
    target.End = previous().Loc;
  }

  if (!match(TokenType::Dot))
    return true;

  if (match(TokenType::Ampersand))
    target.MemberPrefix += "&";
  else if (match(TokenType::Caret))
    target.MemberPrefix += "^";
  else if (match(TokenType::Star))
    target.MemberPrefix += "*";
  else if (match(TokenType::Tilde))
    target.MemberPrefix += "~";
  if (match(TokenType::TokenWrite))
    target.MemberPrefix += "#";
  if (!(check(TokenType::Identifier) || check(TokenType::Integer))) {
    error(peek(), DiagID::ERR_PARSER_EXPECTED_IDENTIFIER_OR_INTEGER_AFTER_IN);
    return false;
  }
  target.MemberName = advance().Text;
  target.End = previous().Loc;
  return true;
}

bool Parser::parseReturnDependencyRoute(ReturnContractSyntax &contract,
                                        ReturnDependencyTargetSyntax target,
                                        bool allowMemberPath) {
  if (!match(TokenType::Dependency)) {
    error(peek(), DiagID::ERR_PARSER_EXPECTED_AFTER_LHS_IN_EFFECTS_BLOCK);
    return false;
  }
  std::vector<DependencyPathSyntax> sources =
      parseReturnDependencySources(allowMemberPath);
  if (sources.empty())
    return false;

  ReturnDependencyRouteSyntax route;
  route.Target = std::move(target);
  route.Sources = std::move(sources);
  route.Begin = route.Target.Begin;
  route.End = route.Sources.back().End;
  contract.End = route.End;
  contract.Routes.push_back(std::move(route));
  return true;
}

ReturnContractSyntax Parser::parseReturnContract(bool allowDependencies,
                                                 bool allowNever) {
  ReturnContractSyntax contract;
  if (!match(TokenType::Arrow))
    return contract;

  contract.HasArrow = true;
  contract.Begin = previous().Loc;
  contract.End = contract.Begin;
  if (match(TokenType::KwAsync))
    contract.Effect = EffectKind::Async;
  else if (match(TokenType::KwWait))
    contract.Effect = EffectKind::Wait;

  if (looksLikeNamedReturn()) {
    if (match(TokenType::KwNul))
      contract.BindingPrefix += "nul ";
    if (match(TokenType::Ampersand))
      contract.BindingPrefix += "&";
    else if (match(TokenType::Star))
      contract.BindingPrefix += "*";
    else if (match(TokenType::Caret))
      contract.BindingPrefix += "^";
    else if (match(TokenType::Tilde))
      contract.BindingPrefix += "~";
    if (match(TokenType::TokenWrite))
      contract.BindingPrefix += "#";

    Token nameTok =
        consume(TokenType::Identifier, DiagID::ERR_PARSER_EXPECTED_RETURN_NAME);
    contract.BindingName = nameTok.Text;
    contract.BindingSoulWritable = nameTok.HasWrite;
    contract.BindingBorrowsSoul = contract.BindingPrefix == "&";
    consume(TokenType::Colon, DiagID::ERR_EXPECTED_COLON);
    if (!isTypeStart()) {
      error(peek(), DiagID::ERR_PARSER_EXPECTED_RETURN_TYPE);
    } else {
      contract.TypeSyntax = parseTypeSyntax(true, false, false, allowNever);
      contract.HasExplicitResultType = true;
      if (contract.BindingSoulWritable) {
        contract.TypeSyntax = TypeSyntax::morphology(
            "#", contract.TypeSyntax, contract.TypeSyntax->Begin,
            contract.TypeSyntax->End, true);
      }
      if (!contract.BindingPrefix.empty()) {
        contract.TypeSyntax = TypeSyntax::morphology(
            contract.BindingPrefix, contract.TypeSyntax,
            contract.TypeSyntax->Begin, contract.TypeSyntax->End);
      }
      contract.Type = canonicalType(contract.TypeSyntax);
      contract.End = contract.TypeSyntax->End;
    }
  } else if (!isTypeStart()) {
    // Effects are declaration modifiers, not result types.  `-> async` and
    // `-> wait` therefore spell a Unit task/result without inventing a
    // redundant `void` or `()` type annotation.
    if (contract.Effect != EffectKind::None &&
        (check(TokenType::LBrace) || isEndOfStatement())) {
      contract.classifyResult();
      return contract;
    }
    error(peek(), DiagID::ERR_PARSER_EXPECTED_RETURN_TYPE);
    if (!isEndOfStatement() && !check(TokenType::LBrace) &&
        !check(TokenType::Dependency))
      advance();
  } else {
    contract.TypeSyntax = parseTypeSyntax(true, false, false, allowNever,
                                          true);
    contract.HasExplicitResultType = true;
    contract.Type = canonicalType(contract.TypeSyntax);
    contract.End = contract.TypeSyntax->End;
  }

  contract.classifyResult();
  if (contract.ResultKind == ReturnResultKind::Never &&
      (!allowNever || contract.TypeSyntax->NodeKind != TypeSyntax::Kind::Named)) {
    error(peek(), DiagID::ERR_PARSER_NEVER_TYPE_RESTRICTED);
  }

  if (check(TokenType::Dependency)) {
    Token dependency = peek();
    if (!allowDependencies)
      error(dependency, DiagID::ERR_PARSER_EXTERN_RETURN_DEPENDENCY_UNSUPPORTED);
    ReturnDependencyTargetSyntax target;
    target.Kind = ReturnDependencyTargetKind::ReturnValue;
    target.Begin = contract.Begin;
    target.End = contract.End;
    parseReturnDependencyRoute(contract, std::move(target), false);
  }
  return contract;
}

void Parser::parseReturnContractEffects(ReturnContractSyntax &contract) {
  if (!(check(TokenType::Identifier) && peek().Text == "effects" &&
        checkAt(1, TokenType::Colon))) {
    return;
  }

  advance();
  advance();
  while (!check(TokenType::LBrace) && !check(TokenType::EndOfFile)) {
    ReturnDependencyTargetSyntax target;
    if (!parseReturnDependencyTarget(contract, target)) {
      if (isEndOfStatement())
        return;
      error(peek(), DiagID::ERR_PARSER_ONLY_RETURN_OR_NAMED_RETURN_LHS_IS_CURR);
      return;
    }
    if (!parseReturnDependencyRoute(contract, std::move(target), true))
      return;
  }
}

OutcomeContractSyntax Parser::parseOutcomeContract() {
  OutcomeContractSyntax contract;
  if (!(check(TokenType::Identifier) && peek().Text == "outcomes" &&
        checkAt(1, TokenType::Colon))) {
    return contract;
  }

  Token begin = advance();
  contract.Begin = begin.Loc;
  advance();
  while (!check(TokenType::LBrace) && !check(TokenType::EndOfFile)) {
    Token variant = consume(TokenType::Identifier,
                            DiagID::ERR_PARSER_EXPECTED_VARIANT);
    consume(TokenType::FatArrow, DiagID::ERR_PARSER_EXPECTED);
    Token subject = consume(TokenType::Identifier,
                            DiagID::ERR_PARSER_EXPECTED_ARGUMENT_NAME);
    consume(TokenType::Colon, DiagID::ERR_EXPECTED_COLON);

    OutcomeTransitionSyntax transition;
    transition.Variant = variant.Text;
    transition.Subject = subject.Text;
    transition.Begin = variant.Loc;
    if (check(TokenType::Identifier) && peek().Text == "init") {
      transition.Post = OutcomePostState::Init;
      transition.End = advance().Loc;
    } else if (match(TokenType::KwUninit)) {
      transition.Post = OutcomePostState::Uninit;
      transition.End = previous().Loc;
    } else {
      error(peek(), DiagID::ERR_PARSER_EXPECTED_VARIANT);
      return contract;
    }
    contract.Transitions.push_back(std::move(transition));
  }
  contract.End = previous().Loc;
  return contract;
}

/*
 * Return dependencies are deliberately parsed once into
 * ReturnDependencyRouteSyntax.  Inline `<-` constructs a return target and
 * `effects:` supplies a parsed target; both use parseReturnDependencyRoute.
 */
std::unique_ptr<ShapeDecl> Parser::parseShape(bool isPub) {
  if (match(TokenType::KwUnion)) {
    error(previous(), DiagID::ERR_UNION_DEPRECATED);
    return nullptr;
  } else {
    match(TokenType::KwShape);
  }

  Token name = consume(TokenType::Identifier, DiagID::ERR_PARSER_EXPECTED_SHAPE_NAME);

  // Parse Generic Parameters: Name<T, U> or Name<T, N_: usize>
  std::vector<GenericParam> genericParams = parseGenericParams();
  parseWhereConstraints(genericParams);

  if (match(TokenType::Dependency)) {
    error(previous(), DiagID::ERR_PARSER_SHAPE_HEADER_DEPENDENCY_REMOVED);
    while (!check(TokenType::EndOfFile) && !check(TokenType::LParen) &&
           !check(TokenType::LBracket) && !check(TokenType::Equal) &&
           !check(TokenType::LBrace)) {
      advance();
    }
  }

  ShapeKind kind = ShapeKind::Struct;
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
        Token variantToken = consume(TokenType::Identifier, DiagID::ERR_PARSER_EXPECTED_VARIANT);
        v.Name = variantToken.Text;
        v.Loc = variantToken.Loc;
        if (match(TokenType::LParen)) {
          v.SubKind = ShapeKind::Tuple;
          while (!check(TokenType::RParen) && !check(TokenType::EndOfFile)) {
            ShapeMember field;
            field.TypeSyntax = parseRequiredTypeSyntax();
            field.Type = canonicalType(field.TypeSyntax);
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
        v.IsUnitVariant = v.SubMembers.empty();
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

          if (isPtrNullable && (m.IsUnique || m.IsShared)) {
            HasError = true;
            DiagnosticEngine::report(
                previous().Loc,
                DiagID::ERR_SAFE_NULLABLE_HANDLE_REMOVED,
                m.IsUnique ? "nul ^T" : "nul ~T");
          }

          nameTok = consume(TokenType::Identifier, DiagID::ERR_PARSER_EXPECTED_FIELD_NAME);
          m.Name = nameTok.Text;
          m.Loc = nameTok.Loc;
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

        TypeSyntaxPtr rawTypeSyntax = parseRequiredTypeSyntax(m.IsRawPointer);
        std::string rawType = canonicalType(rawTypeSyntax);
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
          if (match(TokenType::Dependency)) {
            error(previous(), DiagID::ERR_PARSER_SHAPE_MEMBER_DEPENDENCY_UNSUPPORTED);
            while (!check(TokenType::EndOfFile) && !check(TokenType::Comma) &&
                   !check(TokenType::RParen)) {
              advance();
            }
          }
        }
        m.Type = rawType;
        m.TypeSyntax = rawTypeSyntax;
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
                                          std::move(members));
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

      // `init` is contextual: a parameter named `init` remains valid when
      // immediately followed by its type colon.
      bool isInit = check(TokenType::Identifier) && peek().Text == "init" &&
                    !checkAt(1, TokenType::Colon);
      if (isInit)
        advance();
      bool isCeded = match(TokenType::KwCede);

      if (firstArg && match(TokenType::KwSelf)) {
        FunctionDecl::Arg arg;
        arg.Loc = previous().Loc;
        arg.IsCeded = isCeded;
        arg.IsInit = isInit;
        arg.Name = "self";
        arg.Type = "Self"; // Default
        arg.TypeSyntax = TypeSyntax::named("Self", arg.Loc, arg.Loc);
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
          if (!isTypeStart()) {
            error(peek(), DiagID::ERR_PARSER_EXPECTED_PARAMETER_TYPE);
          } else {
            arg.TypeSyntax = parseTypeSyntax();
            arg.Type = canonicalType(arg.TypeSyntax);
          }
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

      std::vector<HandleLayer> handleLayers;
      std::string argPrefix = "";

      while (true) {
        bool layerNull = match(TokenType::KwNul);
        if (match(TokenType::And)) {
          Token t = previous();
          if (layerNull) {
            error(t, DiagID::ERR_PARSER_BORROWED_POINTERS_CANNOT_BE_NULLABLE);
          }
          HandleLayer l1;
          l1.Morphology = BindingMorphology::Reference;
          l1.Nullable = layerNull;
          l1.Rebindable = t.IsSwappablePtr;
          l1.Blocked = t.IsBlocked;
          handleLayers.push_back(l1);

          HandleLayer l2;
          l2.Morphology = BindingMorphology::Reference;
          handleLayers.push_back(l2);
          argPrefix += "&&";
        } else if (match(TokenType::Ampersand)) {
          Token t = previous();
          if (layerNull) {
            error(t, DiagID::ERR_PARSER_BORROWED_POINTERS_CANNOT_BE_NULLABLE);
          }
          HandleLayer l;
          l.Morphology = BindingMorphology::Reference;
          l.Nullable = layerNull;
          l.Rebindable = t.IsSwappablePtr;
          l.Blocked = t.IsBlocked;
          handleLayers.push_back(l);
          argPrefix += "&";
        } else if (match(TokenType::Caret)) {
          Token t = previous();
          if (layerNull) {
            DiagnosticEngine::report(t.Loc, DiagID::ERR_SAFE_NULLABLE_HANDLE_REMOVED, "nul ^T");
            HasError = true;
          }
          HandleLayer l;
          l.Morphology = BindingMorphology::Unique;
          l.Nullable = layerNull;
          l.Rebindable = t.IsSwappablePtr;
          l.Blocked = t.IsBlocked;
          handleLayers.push_back(l);
          argPrefix += "^";
        } else if (match(TokenType::Tilde)) {
          Token t = previous();
          if (layerNull) {
            DiagnosticEngine::report(t.Loc, DiagID::ERR_SAFE_NULLABLE_HANDLE_REMOVED, "nul ~T");
            HasError = true;
          }
          HandleLayer l;
          l.Morphology = BindingMorphology::Shared;
          l.Nullable = layerNull;
          l.Rebindable = t.IsSwappablePtr;
          l.Blocked = t.IsBlocked;
          handleLayers.push_back(l);
          argPrefix += "~";
        } else if (match(TokenType::Star)) {
          Token t = previous();
          HandleLayer l;
          l.Morphology = BindingMorphology::Raw;
          l.Nullable = layerNull;
          l.Rebindable = t.IsSwappablePtr;
          l.Blocked = t.IsBlocked;
          handleLayers.push_back(l);
          argPrefix += "*";
        } else {
          if (layerNull) {
            error(previous(), DiagID::ERR_PARSER_BORROWED_POINTERS_CANNOT_BE_NULLABLE);
          }
          break;
        }
      }

      Token argName;
      if (check(TokenType::Identifier) || check(TokenType::KwSelf) ||
          check(TokenType::KwUpperSelf)) {
        argName = advance();
      } else {
        error(peek(), DiagID::ERR_PARSER_EXPECTED_ARGUMENT_NAME);
        return nullptr;
      }
      std::string argType = "i64"; // recovery type after a reported parse error
      TypeSyntaxPtr argTypeSyntax;
      if (!match(TokenType::Colon)) {
        error(argName, DiagID::ERR_PARSER_EXPECTED_PARAMETER_TYPE);
      } else if (!isTypeStart()) {
        error(peek(), DiagID::ERR_PARSER_EXPECTED_PARAMETER_TYPE);
      } else {
        argTypeSyntax = parseTypeSyntax(true, false, false, false, argPrefix == "*");
        argType = canonicalType(argTypeSyntax);
      }
      bool nameIsMorphic = !argName.Text.empty() && argName.Text[0] == '\'';
      bool typeIsMorphic = !argType.empty() && argType[0] == '\'';
      if (typeIsMorphic) {
        errorTypeSideMorphicBinding(argName, argPrefix, argType);
        argType = argType.substr(1);
      }
      if (argType.rfind("cede ", 0) == 0) {
        isCeded = true;
        argType = argType.substr(5);
        argTypeSyntax =
            TypeSyntax::withoutLeadingMorphology(argTypeSyntax, "cede ");
      }
      bool rejectedTypeSide = rejectTypeSideHandleMorphology(argName, argPrefix, argTypeSyntax, argType);

      FunctionDecl::Arg arg;
      arg.Loc = argName.Loc;
      arg.IsCeded = isCeded;
      arg.IsInit = isInit;
      arg.HadRejectedTypeSideMorphology = rejectedTypeSide;
      arg.Name = argName.Text;
      arg.Type = argType;
      arg.TypeSyntax = argTypeSyntax;

      if (!handleLayers.empty()) {
        arg.Permission.HandleLayers = handleLayers;
        arg.Permission.syncProjections();
      }
      arg.Permission.SoulWritable = argName.HasWrite;
      arg.Permission.SoulBlocked = argName.IsBlocked;
      arg.Permission.MorphicExempt = nameIsMorphic;

      // Project into legacy boolean fields on FunctionDecl::Arg for compatibility
      arg.IsRawPointer = (arg.Permission.outerMorphology() == BindingMorphology::Raw);
      arg.IsReference = (arg.Permission.outerMorphology() == BindingMorphology::Reference);
      arg.IsUnique = (arg.Permission.outerMorphology() == BindingMorphology::Unique);
      arg.IsShared = (arg.Permission.outerMorphology() == BindingMorphology::Shared);
      arg.IsRebindable = arg.Permission.IdentityRebindable;
      arg.IsPointerNullable = arg.Permission.IdentityMayBeZero;
      arg.IsRebindBlocked = arg.Permission.IdentityBlocked;
      arg.IsValueMutable = argName.HasWrite;
      arg.IsValueNullable = argName.HasNull;
      arg.IsValueBlocked = argName.IsBlocked;
      arg.IsMorphicExempt = nameIsMorphic;

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

  ReturnContractSyntax contract = parseReturnContract(true, true);
  parseWhereConstraints(genericParams);
  parseReturnContractEffects(contract);
  OutcomeContractSyntax outcomeContract = parseOutcomeContract();

  std::unique_ptr<BlockStmt> body = nullptr;
  if (match(TokenType::Equal)) {
    if (check(TokenType::KwDelete) || check(TokenType::Identifier))
      advance();
    error(previous(), DiagID::ERR_PARSER_DELETED_FUNCTIONS_REMOVED);
    expectEndOfStatement();
  } else if (check(TokenType::LBrace)) {
    body = parseBlock();
  } else {
    expectEndOfStatement();
  }
  auto decl = std::make_unique<FunctionDecl>(
      isPub, name.Text, std::move(args), std::move(body), contract.Type,
      genericParams, std::vector<std::string>{}, contract.Effect);
  decl->setReturnContract(std::move(contract));
  decl->OutcomeContract = std::move(outcomeContract);
  decl->IsVariadic = isVariadic;
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
      std::string argPrefix = "";
      std::vector<HandleLayer> handleLayers;
      while (true) {
        bool layerNullable = match(TokenType::KwNul);
        if (match(TokenType::And)) {
          Token t = previous();
          if (layerNullable) {
            error(t, DiagID::ERR_PARSER_BORROWED_POINTERS_CANNOT_BE_NULLABLE);
          }
          handleLayers.push_back({BindingMorphology::Reference, false, layerNullable, false});
          handleLayers.push_back({BindingMorphology::Reference, false, false, false});
          argPrefix += "&&";
        } else if (match(TokenType::Ampersand)) {
          Token t = previous();
          if (layerNullable) {
            error(t, DiagID::ERR_PARSER_BORROWED_POINTERS_CANNOT_BE_NULLABLE);
          }
          handleLayers.push_back({BindingMorphology::Reference, false, layerNullable, false});
          argPrefix += "&";
        } else if (match(TokenType::Caret)) {
          Token t = previous();
          if (layerNullable) {
            DiagnosticEngine::report(t.Loc, DiagID::ERR_SAFE_NULLABLE_HANDLE_REMOVED, "nul ^T");
            HasError = true;
          }
          handleLayers.push_back({BindingMorphology::Unique, false, layerNullable, false});
          argPrefix += "^";
        } else if (match(TokenType::Tilde)) {
          Token t = previous();
          if (layerNullable) {
            DiagnosticEngine::report(t.Loc, DiagID::ERR_SAFE_NULLABLE_HANDLE_REMOVED, "nul ~T");
            HasError = true;
          }
          handleLayers.push_back({BindingMorphology::Shared, false, layerNullable, false});
          argPrefix += "~";
        } else if (match(TokenType::Star)) {
          handleLayers.push_back({BindingMorphology::Raw, false, layerNullable, false});
          argPrefix += "*";
        } else {
          if (layerNullable) {
            error(previous(), DiagID::ERR_PARSER_BORROWED_POINTERS_CANNOT_BE_NULLABLE);
          }
          break;
        }
      }

      Token argName = consume(TokenType::Identifier, DiagID::ERR_PARSER_EXPECTED_ARGUMENT_NAME);
      std::string argType = "i64"; // recovery type after a reported parse error
      TypeSyntaxPtr argTypeSyntax;
      if (!match(TokenType::Colon)) {
        error(argName, DiagID::ERR_PARSER_EXPECTED_PARAMETER_TYPE);
      } else if (!isTypeStart()) {
        error(peek(), DiagID::ERR_PARSER_EXPECTED_PARAMETER_TYPE);
      } else {
        argTypeSyntax =
            parseTypeSyntax(true, false, false, false, argPrefix == "*");
        argType = canonicalType(argTypeSyntax);
      }
      bool nameIsMorphic = !argName.Text.empty() && argName.Text[0] == '\'';
      bool typeIsMorphic = !argType.empty() && argType[0] == '\'';
      if (typeIsMorphic) {
        errorTypeSideMorphicBinding(argName, argPrefix, argType);
        argType = argType.substr(1);
      }
      if (argType.rfind("cede ", 0) == 0) {
        isCeded = true;
        argType = argType.substr(5);
        argTypeSyntax =
            TypeSyntax::withoutLeadingMorphology(argTypeSyntax, "cede ");
      }
      bool rejectedTypeSide = rejectTypeSideHandleMorphology(argName, argPrefix, argTypeSyntax, argType);

      ExternDecl::Arg arg;
      arg.Loc = argName.Loc;
      arg.IsCeded = isCeded;
      arg.HadRejectedTypeSideMorphology = rejectedTypeSide;
      arg.Name = argName.Text;
      arg.Type = argType;
      arg.TypeSyntax = argTypeSyntax;
      arg.IsRawPointer = !handleLayers.empty() && handleLayers[0].Morphology == BindingMorphology::Raw;
      arg.IsPointerNullable = !handleLayers.empty() && handleLayers[0].Nullable;
      arg.IsValueMutable = argName.HasWrite;
      arg.IsValueNullable = argName.HasNull;
      arg.IsMorphicExempt = nameIsMorphic;
      arg.Permission.HandleLayers = handleLayers;
      arg.Permission.syncProjections();
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

  ReturnContractSyntax contract = parseReturnContract(false, false);
  expectEndOfStatement();

  auto node = std::make_unique<ExternDecl>(name.Text, std::move(args),
                                           contract.Type, contract.Effect);
  node->setReturnContract(std::move(contract));
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

  TypeSyntaxPtr targetTypeSyntax = parseRequiredTypeSyntax();
  std::string targetType = canonicalType(targetTypeSyntax);

  expectEndOfStatement();

  auto decl = std::make_unique<TypeAliasDecl>(isPub, name.Text, targetType,
                                              isStrong, genericParams);
  decl->TargetTypeSyntax = targetTypeSyntax;
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
    decl.TypeSyntax = parseRequiredTypeSyntax();
    decl.Type = canonicalType(decl.TypeSyntax);
  } else if (match(TokenType::Equal)) {
    error(startTok, DiagID::ERR_PARSER_TRAIT_ASSOCIATED_TYPE_CANNOT_HAVE_DEFAULT);
    decl.TypeSyntax = parseRequiredTypeSyntax();
    decl.Type = canonicalType(decl.TypeSyntax);
  }

  expectEndOfStatement();
  return decl;
}

std::unique_ptr<ImplDecl> Parser::parseImpl() {
  Token startTok = consume(TokenType::KwImpl, DiagID::ERR_PARSER_EXPECTED_IMPL);

  // 1. [NEW] Parse Generic Parameters <T, U>
  std::vector<GenericParam> genericParams = parseGenericParams();

  // The impl subject is a TypeSyntax; its trait facet remains a separate
  // grammar so a trait bound is never mistaken for an ordinary type.
  TypeSyntaxPtr firstTypeSyntax = parseTypeSyntax(false);
  std::string firstTypeStr = canonicalType(firstTypeSyntax);

  std::string traitName;
  std::string typeName;
  Token traitNameToken = startTok;

  if (match(TokenType::At)) {
    // impl Type@Trait
    typeName = firstTypeStr;
    traitNameToken = peek();
    if (check(TokenType::KwEncap)) {
      // `encap` is reserved, but it is parsed here as a trait-facet token so
      // the dedicated reserved-keyword diagnostic remains the only error.
      traitName = advance().Text;
    } else {
      traitName = canonicalType(parseTypeSyntax());
    }
  } else if (match(TokenType::KwFor)) {
    error(previous(), DiagID::ERR_PARSER_IMPL_TRAIT_FOR_REMOVED);
    traitName = firstTypeStr;
    typeName = canonicalType(parseTypeSyntax());
  } else {
    // impl Type
    typeName = firstTypeStr;
  }

  if (!traitName.empty() && traitName[0] == '@') {
    traitName = traitName.substr(1);
  }
  if (traitName == "encap") {
    error(traitNameToken, DiagID::ERR_PARSER_ENCAP_RESERVED_KEYWORD);
    traitName = "Encap";
  }

  parseWhereConstraints(genericParams);

  consume(TokenType::LBrace, DiagID::ERR_EXPECTED_LBRACE);

  std::vector<std::unique_ptr<FunctionDecl>> methods;
  std::vector<EncapEntry> encapEntries;
  std::vector<AssociatedTypeDecl> associatedTypes;

  if (traitName == "Encap") {
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
        if (check(TokenType::LParen) || check(TokenType::Star)) {
          error(peek(), DiagID::ERR_PARSER_ENCAP_NONEXACT_GRANT_REMOVED);
          while (!check(TokenType::RBrace) && !check(TokenType::EndOfFile) &&
                 !peek().HasNewlineBefore) {
            advance();
          }
          continue;
        }

        while (check(TokenType::Identifier)) {
          entry.Fields.push_back(advance().Text);
          if (!match(TokenType::Comma))
            break;
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
  decl->HeaderSyntax.Type = firstTypeSyntax;
  decl->HeaderSyntax.TraitName = traitName;
  decl->HeaderSyntax.Begin = firstTypeSyntax ? firstTypeSyntax->Begin : startTok.Loc;
  decl->HeaderSyntax.End = firstTypeSyntax ? firstTypeSyntax->End : startTok.Loc;
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
  auto declaration = std::make_unique<TraitDecl>(
      isPub, name.Text, std::move(methods), std::move(genericParams),
      std::move(selfTraitBounds), std::move(associatedTypes));
  declaration->setLocation(name, m_CurrentFile);
  return declaration;
}

} // namespace toka
