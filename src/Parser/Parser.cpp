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
#include "toka/DiagnosticEngine.h"
#include <algorithm>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace toka {

std::string Parser::TargetTriple = "";

const Token &Parser::peek() const {
  if (m_Pos >= m_Tokens.size())
    return m_Tokens.back(); // EOF
  return m_Tokens[m_Pos];
}

const Token &Parser::peekAt(int offset) const {
  if (m_Pos + offset >= m_Tokens.size())
    return m_Tokens.back();
  return m_Tokens[m_Pos + offset];
}

const Token &Parser::previous() const { return m_Tokens[m_Pos - 1]; }

Token Parser::advance() {
  if (m_Pos < m_Tokens.size())
    m_Pos++;
  return previous();
}

bool Parser::check(TokenType type) const { return peek().Kind == type; }

bool Parser::checkAt(int offset, TokenType type) const {
  if (peekAt(offset).Kind == TokenType::EndOfFile)
    return false;
  return peekAt(offset).Kind == type;
}

bool Parser::match(TokenType type) {
  if (check(type)) {
    advance();
    return true;
  }
  return false;
}

Token Parser::consume(TokenType type, DiagID id) {
  if (check(type))
    return advance();
  if (type == TokenType::Identifier && check(TokenType::KwEncap)) {
    error(peek(), DiagID::ERR_PARSER_ENCAP_RESERVED_KEYWORD);
    return advance();
  }
  error(peek(), id);
  return peek();
}

bool Parser::isTypeStart() const {
  switch (peek().Kind) {
  case TokenType::Identifier:
  case TokenType::KwUpperSelf:
  case TokenType::KwFn:
  case TokenType::KwNever:
  case TokenType::KwFnType:
  case TokenType::KwDyn:
  case TokenType::KwCede:
  case TokenType::KwNul:
  case TokenType::Ampersand:
  case TokenType::And:
  case TokenType::Star:
  case TokenType::Caret:
  case TokenType::Tilde:
  case TokenType::TokenWrite:
  case TokenType::LBracket:
  case TokenType::LParen:
    return true;
  default:
    return false;
  }
}

void Parser::expectEndOfStatement() {
  if (isEndOfStatement()) {
    if (match(TokenType::Semicolon)) {
      // Consumed
    }
    return;
  }
  error(peek(), DiagID::ERR_EXPECTED_SEMI);
}

bool Parser::isEndOfStatement() {
  if (check(TokenType::Semicolon))
    return true;
  if (check(TokenType::RBrace))
    return true;
  if (peek().Kind == TokenType::EndOfFile)
    return true;

  // Newline rule
  if (peek().HasNewlineBefore) {
    // If previous was an operator, it's not a terminator
    TokenType prev = previous().Kind;
    switch (prev) {
    case TokenType::Plus:
    case TokenType::Minus:
    case TokenType::Star:
    case TokenType::Slash:
    case TokenType::Equal:
    case TokenType::PlusEqual:
    case TokenType::MinusEqual:
    case TokenType::StarEqual:
    case TokenType::SlashEqual:
    case TokenType::DoubleEqual:
    case TokenType::Neq:
    case TokenType::Less:
    // TokenType::Greater omitted intentionally: Toka statements that validly 
    // end in '>' are terminating generics (Option<T>) and should form colons.
    case TokenType::And:
    case TokenType::Or:
    case TokenType::Dot:
    case TokenType::Arrow:
    case TokenType::Comma:
    case TokenType::Colon:
    case TokenType::At:
    case TokenType::Dependency:
    case TokenType::LParen:
    case TokenType::LBracket:
    case TokenType::LBrace:
    case TokenType::Ampersand:
    case TokenType::Pipe:
    case TokenType::Caret:
    case TokenType::Tilde:
      return false;
    default:
      return true;
    }
  }

  return false;
}



void Parser::error(const Token &tok, DiagID id) {
  if (PanicMode) { return; }
  PanicMode = true;
  HasError = true;
  DiagnosticEngine::report(tok.Loc, id);
}

void Parser::errorTypeSideMorphicBinding(const Token &nameTok,
                                         const std::string &bindingPrefix,
                                         const std::string &typeName) {
  std::string suggestedName = nameTok.Text;
  if (suggestedName.empty() || suggestedName[0] != '\'')
    suggestedName = "'" + suggestedName;

  std::string suggestedType = typeName;
  if (!suggestedType.empty() && suggestedType[0] == '\'')
    suggestedType = suggestedType.substr(1);

  std::string suggestion = bindingPrefix + suggestedName + ": " + suggestedType;
  std::string msg =
      "Morphic generic marker (') must prefix the binding name, not the type "
      "name. Did you mean '" +
      suggestion + "'?";
  error(nameTok, DiagID::ERR_GENERIC_PARSE, msg);
}

bool Parser::rejectTypeSideReferenceParameter(const Token &nameTok,
                                              const std::string &bindingPrefix,
                                              std::string &typeName) {
  size_t pos = typeName.find_first_not_of(" \t\r\n");
  if (pos == std::string::npos)
    return false;

  if (typeName.compare(pos, 5, "cede ") == 0) {
    pos = typeName.find_first_not_of(" \t\r\n", pos + 5);
    if (pos == std::string::npos)
      return false;
  }

  if (typeName[pos] != '&')
    return false;

  // Level-2 double borrow parameter: &x: &T is legal
  if (!bindingPrefix.empty() && bindingPrefix.find('&') != std::string::npos)
    return false;

  std::string baseType = typeName;
  baseType.erase(pos, 1);

  std::string ordinary = nameTok.Text + ": " + baseType;
  std::string identity = bindingPrefix.empty()
                             ? "&" + nameTok.Text + ": " + baseType
                             : bindingPrefix + nameTok.Text + ": " + baseType;
  std::string msg =
      "Reference morphology (&) must prefix the binding name, not the type "
      "name. Function parameters are implicitly captured; write '" +
      ordinary + "' for ordinary access, or '" + identity +
      "' only when passing or rebinding the reference identity.";
  error(nameTok, DiagID::ERR_GENERIC_PARSE, msg);

  typeName = baseType;
  return true;
}

void Parser::synchronize() {
  size_t startPos = m_Pos;
  PanicMode = false;
  while (!check(TokenType::EndOfFile)) {
    if (previous().Kind == TokenType::Semicolon) {
      if (m_Pos == startPos) {
        advance();
      }
      return;
    }
    
    switch (peek().Kind) {
      case TokenType::KwPub:
      case TokenType::KwFn:
      case TokenType::KwLet:
      case TokenType::KwAuto:
      case TokenType::KwConst:
      case TokenType::KwShape:
      case TokenType::KwUnion:
      case TokenType::KwTrait:
      case TokenType::KwImpl:
      case TokenType::KwImport:
      case TokenType::RBrace:
        if (m_Pos == startPos) {
          advance();
        }
        return;
      default:
        // continue advancing
        break;
    }
    advance();
  }
}

namespace {

std::string canonicalTypeTokenText(const Token &tok) {
  std::string text = tok.Text;
  if (tok.Kind == TokenType::Identifier || tok.Kind == TokenType::KwSelf ||
      tok.Kind == TokenType::KwUpperSelf || tok.Kind == TokenType::KwFn ||
      tok.Kind == TokenType::KwNever) {
    if (tok.IsBlocked)
      text += "$";
    if (tok.HasNull)
      text += "?";
    if (tok.HasWrite)
      text += "#";
  }
  return text;
}

std::vector<std::string> attachedTypePostfixes(const Token &tok) {
  std::vector<std::string> result;
  if (tok.IsBlocked)
    result.push_back("$");
  if (tok.HasNull)
    result.push_back("?");
  if (tok.HasWrite)
    result.push_back("#");
  return result;
}

bool usesVoidOutsideRawPointee(const TypeSyntaxPtr &syntax,
                               bool rawPointee = false) {
  if (!syntax)
    return false;

  switch (syntax->NodeKind) {
  case TypeSyntax::Kind::Named:
    return syntax->Text == "void" && !rawPointee;
  case TypeSyntax::Kind::Morphology:
    // A raw-pointer morphology is the only type constructor that may carry
    // ABI void.  Any other prefix starts a new pointee context.
    return usesVoidOutsideRawPointee(
        syntax->Subject,
        syntax->IsPostfix ? rawPointee : syntax->Text == "*");
  case TypeSyntax::Kind::GenericApplication:
    if (usesVoidOutsideRawPointee(syntax->Subject))
      return true;
    for (const auto &argument : syntax->Arguments) {
      if (argument.ArgumentKind == TypeArgumentSyntax::Kind::Type &&
          usesVoidOutsideRawPointee(argument.Type))
        return true;
    }
    return false;
  case TypeSyntax::Kind::Array:
  case TypeSyntax::Kind::Slice:
  case TypeSyntax::Kind::AssociatedProjection:
  case TypeSyntax::Kind::MissOutcome:
    return usesVoidOutsideRawPointee(syntax->Subject);
  case TypeSyntax::Kind::Tuple:
  case TypeSyntax::Kind::Function:
    for (const auto &element : syntax->Elements) {
      if (usesVoidOutsideRawPointee(element))
        return true;
    }
    return usesVoidOutsideRawPointee(syntax->Result);
  case TypeSyntax::Kind::AnonymousRecord:
    for (const auto &field : syntax->Fields) {
      if (usesVoidOutsideRawPointee(field.Type))
        return true;
    }
    return false;
  case TypeSyntax::Kind::Invalid:
  case TypeSyntax::Kind::DynTrait:
    return false;
  }
  return false;
}

class TypeSyntaxBuilder {
public:
  explicit TypeSyntaxBuilder(const std::vector<Token> &tokens)
      : Tokens(tokens) {}

  TypeSyntaxPtr parse() {
    if (Tokens.empty())
      return TypeSyntax::invalid("", SourceLocation(), SourceLocation());
    auto syntax = parseType();
    if (syntax && Pos == Tokens.size())
      return syntax;
    if (isLegacyPrefixArraySpelling()) {
      // `*[0]u8` is an older compiler-internal cast spelling still used by
      // the networking runtime.  It was accepted by the former token-text
      // path even though it is not part of the published `[T; N]` grammar.
      // Keep its canonical text as an explicit legacy bridge; Type lowering
      // handles it through the pre-existing semantic representation.
      return TypeSyntax::named(spelling(0, Tokens.size()), Tokens.front().Loc,
                               Tokens.back().Loc);
    }
    const size_t begin = 0;
    return TypeSyntax::invalid(spelling(begin, Tokens.size()), Tokens.front().Loc,
                               Tokens.back().Loc);
  }

private:
  const std::vector<Token> &Tokens;
  size_t Pos = 0;

  bool atEnd() const { return Pos >= Tokens.size(); }
  const Token *peek() const { return atEnd() ? nullptr : &Tokens[Pos]; }
  bool check(TokenType kind) const { return peek() && peek()->Kind == kind; }

  const Token *take() {
    if (atEnd())
      return nullptr;
    return &Tokens[Pos++];
  }

  bool match(TokenType kind) {
    if (!check(kind))
      return false;
    ++Pos;
    return true;
  }

  static bool isName(const Token *tok) {
    return tok && (tok->Kind == TokenType::Identifier ||
                   tok->Kind == TokenType::KwUpperSelf ||
                   tok->Kind == TokenType::KwFnType ||
                   tok->Kind == TokenType::KwNever);
  }

  static bool isConstArgument(const Token *tok) {
    if (!tok)
      return false;
    if (tok->Kind == TokenType::Integer || tok->Kind == TokenType::Float ||
        tok->Kind == TokenType::KwTrue || tok->Kind == TokenType::KwFalse ||
        tok->Kind == TokenType::KwNull || tok->Kind == TokenType::KwNone)
      return true;
    return tok->Kind == TokenType::Identifier && !tok->Text.empty() &&
           tok->Text.back() == '_';
  }

  bool isLegacyPrefixArraySpelling() const {
    size_t index = 0;
    while (index < Tokens.size() &&
           (Tokens[index].Kind == TokenType::KwNul ||
            Tokens[index].Kind == TokenType::Star ||
            Tokens[index].Kind == TokenType::Caret ||
            Tokens[index].Kind == TokenType::Tilde ||
            Tokens[index].Kind == TokenType::Ampersand ||
            Tokens[index].Kind == TokenType::And)) {
      ++index;
    }
    return index + 3 < Tokens.size() &&
           Tokens[index].Kind == TokenType::LBracket &&
           Tokens[index + 1].Kind == TokenType::Integer &&
           Tokens[index + 2].Kind == TokenType::RBracket &&
           isName(&Tokens[index + 3]) && index + 4 == Tokens.size();
  }

  std::string spelling(size_t begin, size_t end) const {
    std::string result;
    for (size_t i = begin; i < end; ++i) {
      const Token &tok = Tokens[i];
      result += canonicalTypeTokenText(tok);
      if (tok.Kind == TokenType::KwCede || tok.Kind == TokenType::KwDyn)
        result += " ";
    }
    return result;
  }

  TypeSyntaxPtr parseType() {
    if (atEnd())
      return nullptr;

    const size_t begin = Pos;
    std::vector<std::pair<std::string, SourceLocation>> prefixes;
    while (!atEnd()) {
      if (match(TokenType::KwNul)) {
        prefixes.emplace_back("nul", Tokens[Pos - 1].Loc);
      } else if (match(TokenType::KwCede)) {
        prefixes.emplace_back("cede ", Tokens[Pos - 1].Loc);
      } else if (match(TokenType::Ampersand)) {
        prefixes.emplace_back("&", Tokens[Pos - 1].Loc);
      } else if (match(TokenType::And)) {
        prefixes.emplace_back("&", Tokens[Pos - 1].Loc);
        prefixes.emplace_back("&", Tokens[Pos - 1].Loc);
      } else if (match(TokenType::Star)) {
        prefixes.emplace_back("*", Tokens[Pos - 1].Loc);
      } else if (match(TokenType::Caret)) {
        prefixes.emplace_back("^", Tokens[Pos - 1].Loc);
      } else if (match(TokenType::Tilde)) {
        prefixes.emplace_back("~", Tokens[Pos - 1].Loc);
      } else if (match(TokenType::TokenWrite)) {
        prefixes.emplace_back("#", Tokens[Pos - 1].Loc);
      } else {
        break;
      }
    }

    TypeSyntaxPtr syntax = parsePrimary();
    if (!syntax) {
      SourceLocation loc = begin < Tokens.size() ? Tokens[begin].Loc
                                                  : SourceLocation();
      return TypeSyntax::invalid(spelling(begin, Pos), loc,
                                 Pos ? Tokens[Pos - 1].Loc : loc);
    }

    while (match(TokenType::At)) {
      const SourceLocation projectionBegin = syntax->Begin;
      auto trait = parseNamedApplication(false, false);
      if (!trait || !match(TokenType::Colon) || !match(TokenType::Colon) ||
          !isName(peek())) {
        return TypeSyntax::invalid(spelling(begin, Pos), projectionBegin,
                                   Pos ? Tokens[Pos - 1].Loc : projectionBegin);
      }
      const Token *member = take();
      syntax = TypeSyntax::projection(std::move(syntax),
                                      trait->toCanonicalString(),
                                      canonicalTypeTokenText(*member),
                                      projectionBegin, member->Loc);
    }

    while (check(TokenType::TokenWrite) || check(TokenType::TokenNull) ||
           check(TokenType::TokenNone)) {
      const Token *suffix = take();
      const SourceLocation begin = syntax->Begin;
      syntax = TypeSyntax::morphology(canonicalTypeTokenText(*suffix),
                                      std::move(syntax), begin,
                                      suffix->Loc, true);
    }

    for (auto it = prefixes.rbegin(); it != prefixes.rend(); ++it) {
      const SourceLocation end = syntax->End;
      syntax = TypeSyntax::morphology(it->first, std::move(syntax), it->second,
                                      end);
    }

    if (match(TokenType::Pipe)) {
      const Token *miss = peek();
      if (!miss || miss->Kind != TokenType::Identifier ||
          miss->Text != "miss") {
        const SourceLocation loc = miss ? miss->Loc : syntax->End;
        return TypeSyntax::invalid(spelling(begin, Pos), syntax->Begin, loc);
      }
      take();
      const SourceLocation outcomeBegin = syntax->Begin;
      syntax = TypeSyntax::missOutcome(std::move(syntax), outcomeBegin,
                                       miss->Loc);
    }
    return syntax;
  }

  TypeSyntaxPtr parsePrimary() {
    if (atEnd())
      return nullptr;
    if (match(TokenType::KwDyn)) {
      const Token &dyn = Tokens[Pos - 1];
      if (check(TokenType::KwFn)) {
        const Token *fn = take();
        std::string kind = "dyn " + canonicalTypeTokenText(*fn);
        while (check(TokenType::TokenWrite) || check(TokenType::TokenNull) ||
               check(TokenType::TokenNone))
          kind += canonicalTypeTokenText(*take());
        if (check(TokenType::LParen))
          return parseFunction(std::move(kind), dyn.Loc);
        return TypeSyntax::named(std::move(kind), dyn.Loc, fn->Loc);
      }
      if (!match(TokenType::At))
        return TypeSyntax::named("dyn", dyn.Loc, dyn.Loc);
      if (atEnd())
        return TypeSyntax::invalid("dyn @", dyn.Loc,
                                   Pos ? Tokens[Pos - 1].Loc : dyn.Loc);

      // A dyn facet is its own grammar: generic trait arguments and rejected
      // associated-type bindings must reach Sema as one trait reference so it
      // can produce the established object-safety diagnostic.  They are not
      // ordinary type arguments of the enclosing `dyn` type.
      const size_t traitBegin = Pos;
      while (!atEnd())
        take();
      return TypeSyntax::dynTrait(spelling(traitBegin, Pos), dyn.Loc,
                                  Tokens[Pos - 1].Loc);
    }
    if (match(TokenType::KwFn)) {
      const Token &fn = Tokens[Pos - 1];
      std::string kind = canonicalTypeTokenText(fn);
      while (check(TokenType::TokenWrite) || check(TokenType::TokenNull) ||
             check(TokenType::TokenNone))
        kind += canonicalTypeTokenText(*take());
      if (check(TokenType::LParen))
        return parseFunction(std::move(kind), fn.Loc);
      return TypeSyntax::named(std::move(kind), fn.Loc, fn.Loc);
    }
    if (match(TokenType::LBracket))
      return parseArray(Tokens[Pos - 1]);
    if (match(TokenType::LParen))
      return parseParenthesized(Tokens[Pos - 1]);
    return parseNamedApplication(true);
  }

  TypeSyntaxPtr parseNamedApplication(bool includePathSuffix,
                                      bool includePathSegments = true) {
    if (!isName(peek()))
      return nullptr;
    const Token *first = take();
    std::string name = first->Text;
    std::vector<std::string> postfixes = attachedTypePostfixes(*first);
    SourceLocation end = first->Loc;
    while (includePathSegments && check(TokenType::Colon) &&
           Pos + 2 < Tokens.size() &&
           Tokens[Pos + 1].Kind == TokenType::Colon &&
           isName(&Tokens[Pos + 2])) {
      Pos += 2;
      const Token *part = take();
      name += "::" + part->Text;
      auto partPostfixes = attachedTypePostfixes(*part);
      postfixes.insert(postfixes.end(), partPostfixes.begin(),
                       partPostfixes.end());
      end = part->Loc;
    }

    auto applyPostfixes = [&](TypeSyntaxPtr result) {
      for (const auto &postfix : postfixes) {
        result = TypeSyntax::morphology(postfix, std::move(result),
                                        first->Loc, end, true);
      }
      return result;
    };

    TypeSyntaxPtr result = TypeSyntax::named(name, first->Loc, end);
    if (!match(TokenType::GenericLT))
      return applyPostfixes(std::move(result));

    std::vector<TypeArgumentSyntax> arguments;
    while (!atEnd() && !check(TokenType::Greater)) {
      const size_t argumentBegin = Pos;
      if (isConstArgument(peek())) {
        int balance = 0;
        do {
          if (check(TokenType::LParen) || check(TokenType::LBracket) ||
              check(TokenType::GenericLT))
            ++balance;
          if (check(TokenType::RParen) || check(TokenType::RBracket) ||
              check(TokenType::Greater))
            --balance;
          ++Pos;
        } while (!atEnd() && balance >= 0 && !check(TokenType::Comma) &&
                 !check(TokenType::Greater));
        const size_t argumentEnd = Pos;
        arguments.push_back(TypeArgumentSyntax::constant(
            spelling(argumentBegin, argumentEnd), Tokens[argumentBegin].Loc,
            Tokens[argumentEnd - 1].Loc));
      } else {
        auto argument = parseType();
        if (!argument || Pos == argumentBegin) {
          if (!atEnd())
            ++Pos;
          arguments.push_back(TypeArgumentSyntax::constant(
              spelling(argumentBegin, Pos), Tokens[argumentBegin].Loc,
              Tokens[Pos - 1].Loc));
        } else {
          auto typeArgument = TypeArgumentSyntax::type(std::move(argument));
          typeArgument.Begin = Tokens[argumentBegin].Loc;
          typeArgument.End = Tokens[Pos - 1].Loc;
          arguments.push_back(std::move(typeArgument));
        }
      }
      if (!match(TokenType::Comma))
        break;
    }
    if (!match(TokenType::Greater))
      return TypeSyntax::invalid(spelling(0, Pos), first->Loc,
                                 Pos ? Tokens[Pos - 1].Loc : first->Loc);

    end = Tokens[Pos - 1].Loc;
    std::string pathSuffix;
    if (includePathSuffix) {
    while (check(TokenType::Colon) && Pos + 2 < Tokens.size() &&
           Tokens[Pos + 1].Kind == TokenType::Colon &&
           isName(&Tokens[Pos + 2])) {
      Pos += 2;
      const Token *part = take();
      pathSuffix += "::" + part->Text;
      auto partPostfixes = attachedTypePostfixes(*part);
      postfixes.insert(postfixes.end(), partPostfixes.begin(),
                       partPostfixes.end());
      end = part->Loc;
    }
    }
    return applyPostfixes(TypeSyntax::generic(std::move(result),
                                               std::move(arguments),
                                               first->Loc, end,
                                               std::move(pathSuffix)));
  }

  TypeSyntaxPtr parseArray(const Token &open) {
    const size_t begin = Pos - 1;
    auto element = parseType();
    if (!element)
      return TypeSyntax::invalid(spelling(begin, Pos), open.Loc,
                                 Pos ? Tokens[Pos - 1].Loc : open.Loc);
    if (match(TokenType::Semicolon)) {
      const size_t extentBegin = Pos;
      int balance = 0;
      while (!atEnd()) {
        if (check(TokenType::RBracket) && balance == 0)
          break;
        if (check(TokenType::LBracket) || check(TokenType::LParen) ||
            check(TokenType::GenericLT))
          ++balance;
        else if (check(TokenType::RBracket) || check(TokenType::RParen) ||
                 check(TokenType::Greater))
          --balance;
        ++Pos;
      }
      if (extentBegin == Pos || !match(TokenType::RBracket))
        return TypeSyntax::invalid(spelling(begin, Pos), open.Loc,
                                   Pos ? Tokens[Pos - 1].Loc : open.Loc);
      return TypeSyntax::array(
          std::move(element),
          TypeArgumentSyntax::constant(spelling(extentBegin, Pos - 1),
                                       Tokens[extentBegin].Loc,
                                       Tokens[Pos - 2].Loc),
          open.Loc, Tokens[Pos - 1].Loc);
    }
    if (!match(TokenType::RBracket))
      return TypeSyntax::invalid(spelling(begin, Pos), open.Loc,
                                 Pos ? Tokens[Pos - 1].Loc : open.Loc);
    return TypeSyntax::slice(std::move(element), open.Loc, Tokens[Pos - 1].Loc);
  }

  TypeSyntaxPtr parseParenthesized(const Token &open) {
    const size_t begin = Pos - 1;
    if (match(TokenType::RParen))
      return TypeSyntax::tuple({}, open.Loc, Tokens[Pos - 1].Loc);

    const bool isRecord = isName(peek()) && Pos + 1 < Tokens.size() &&
                          Tokens[Pos + 1].Kind == TokenType::Colon;
    if (isRecord) {
      std::vector<TypeSyntax::Field> fields;
      while (!atEnd() && !check(TokenType::RParen)) {
        const Token *name = take();
        if (!name || !match(TokenType::Colon))
          break;
        auto fieldType = parseType();
        fields.push_back({canonicalTypeTokenText(*name), fieldType, name->Loc,
                          fieldType ? fieldType->End : name->Loc});
        if (!match(TokenType::Comma))
          break;
      }
      if (!match(TokenType::RParen))
        return TypeSyntax::invalid(spelling(begin, Pos), open.Loc,
                                   Pos ? Tokens[Pos - 1].Loc : open.Loc);
      return TypeSyntax::anonymousRecord(std::move(fields), open.Loc,
                                         Tokens[Pos - 1].Loc);
    }

    std::vector<TypeSyntaxPtr> elements;
    while (!atEnd() && !check(TokenType::RParen)) {
      auto element = parseType();
      if (!element)
        break;
      elements.push_back(std::move(element));
      if (!match(TokenType::Comma))
        break;
    }
    if (!match(TokenType::RParen))
      return TypeSyntax::invalid(spelling(begin, Pos), open.Loc,
                                 Pos ? Tokens[Pos - 1].Loc : open.Loc);
    if (elements.size() == 1)
      return elements.front();
    return TypeSyntax::tuple(std::move(elements), open.Loc, Tokens[Pos - 1].Loc);
  }

  TypeSyntaxPtr parseFunction(std::string kind, SourceLocation begin) {
    if (!match(TokenType::LParen))
      return TypeSyntax::invalid(kind, begin, begin);
    std::vector<TypeSyntaxPtr> parameters;
    bool variadic = false;
    while (!atEnd() && !check(TokenType::RParen)) {
      if (match(TokenType::DotDotDot)) {
        variadic = true;
        break;
      }
      auto parameter = parseType();
      if (!parameter)
        break;
      parameters.push_back(std::move(parameter));
      if (!match(TokenType::Comma))
        break;
    }
    if (!match(TokenType::RParen))
      return TypeSyntax::invalid(kind, begin,
                                 Pos ? Tokens[Pos - 1].Loc : begin);
    TypeSyntaxPtr result;
    const bool hasExplicitResult = match(TokenType::Arrow);
    if (hasExplicitResult)
      result = parseType();
    return TypeSyntax::function(std::move(kind), std::move(parameters),
                                std::move(result), hasExplicitResult, variadic,
                                begin, Pos ? Tokens[Pos - 1].Loc : begin);
  }
};

} // namespace

TypeSyntaxPtr Parser::parseTypeSyntax(bool allowAssociatedProjection,
                                      bool stopAtConstructor,
                                      bool stopAtExpression,
                                      bool allowNever,
                                      bool allowAbiVoid) {
  std::vector<Token> tokens;
  std::vector<TokenType> delimiters;
  auto isOpeningDelimiter = [](TokenType type) {
    return type == TokenType::LBracket || type == TokenType::LParen ||
           type == TokenType::GenericLT;
  };
  auto closes = [](TokenType opening, TokenType closing) {
    return (opening == TokenType::LBracket && closing == TokenType::RBracket) ||
           (opening == TokenType::LParen && closing == TokenType::RParen) ||
           (opening == TokenType::GenericLT && closing == TokenType::Greater);
  };
  while (!check(TokenType::EndOfFile)) {
    TokenType t = peek().Kind;
    const bool containsOnlyTypePrefixes =
        !tokens.empty() &&
        std::all_of(tokens.begin(), tokens.end(), [](const Token &token) {
          return token.Kind == TokenType::KwNul ||
                 token.Kind == TokenType::KwCede ||
                 token.Kind == TokenType::Ampersand ||
                 token.Kind == TokenType::And ||
                 token.Kind == TokenType::Star ||
                 token.Kind == TokenType::Caret ||
                 token.Kind == TokenType::Tilde ||
                 token.Kind == TokenType::TokenWrite;
        });
    const bool nextIsTypePrefix =
        check(TokenType::Ampersand) || check(TokenType::And) ||
        check(TokenType::Star) || check(TokenType::Caret) ||
        check(TokenType::Tilde) || check(TokenType::TokenWrite) ||
        check(TokenType::KwNul) || check(TokenType::KwCede);
    // A leading `*`, `&`, or other morphology token is part of a type (for
    // example the cast in `value as *char`).  Once a complete type has begun,
    // the same token kinds delimit the enclosing expression grammar.
    const bool expressionBoundary =
        stopAtExpression && !tokens.empty() && delimiters.empty() &&
        !(containsOnlyTypePrefixes && nextIsTypePrefix) &&
        (check(TokenType::Greater) || check(TokenType::Less) ||
         check(TokenType::Colon) || check(TokenType::KwAs) ||
         (check(TokenType::Identifier) && peek().Text == "as") ||
         check(TokenType::Comma) || check(TokenType::RParen) ||
         check(TokenType::RBrace) || check(TokenType::Equal) ||
         check(TokenType::DoubleEqual) || check(TokenType::Neq) ||
         check(TokenType::KwIs) || check(TokenType::Plus) ||
         check(TokenType::Minus) || check(TokenType::Star) ||
         check(TokenType::Slash) || check(TokenType::Percent) ||
         check(TokenType::And) || check(TokenType::Or) ||
         check(TokenType::Dot));
    const bool constructorBoundary =
        stopAtConstructor && delimiters.empty() && !tokens.empty() &&
        check(TokenType::LParen);
    if (expressionBoundary || constructorBoundary ||
        (delimiters.empty() &&
        (check(TokenType::Comma) || check(TokenType::RParen) ||
         check(TokenType::Equal) || isEndOfStatement() ||
         check(TokenType::LBrace) || check(TokenType::Greater) ||
         (check(TokenType::Pipe) &&
          !(checkAt(1, TokenType::Identifier) &&
            peekAt(1).Text == "miss")) ||
         check(TokenType::KwFor) ||
         check(TokenType::KwWhere) || check(TokenType::Dependency))))
      break;

    // A mismatched closing delimiter belongs to the surrounding grammar.  Do
    // not consume it while recovering an unclosed generic/array/function
    // type: the TypeSyntax becomes Invalid and the caller still sees its own
    // delimiter.
    if (!delimiters.empty() &&
        (t == TokenType::RBracket || t == TokenType::RParen ||
         t == TokenType::Greater) &&
        !closes(delimiters.back(), t))
      break;

    if (delimiters.empty() && check(TokenType::At)) {
      const bool isDynTrait = tokens.size() == 1 &&
                              tokens.front().Kind == TokenType::KwDyn;
      if (isDynTrait && checkAt(1, TokenType::LBrace)) {
        bool wasPanic = PanicMode;
        error(peekAt(1), DiagID::ERR_PARSER_DYN_TRAIT_SET_OBJECT_UNSUPPORTED);
        if (!wasPanic)
          PanicMode = false;
        tokens.push_back(advance());
        int braceBalance = 0;
        do {
          Token tok = advance();
          tokens.push_back(tok);
          if (tok.Kind == TokenType::LBrace)
            ++braceBalance;
          else if (tok.Kind == TokenType::RBrace)
            --braceBalance;
        } while (braceBalance > 0 && !check(TokenType::EndOfFile));
        continue;
      }
      if (!isDynTrait && !tokens.empty() && !allowAssociatedProjection)
        break;
    }

    if (delimiters.empty() && t == TokenType::Pipe &&
        checkAt(1, TokenType::Identifier) && peekAt(1).Text == "miss") {
      tokens.push_back(advance());
      tokens.push_back(advance());
      break;
    }

    if (isOpeningDelimiter(t)) {
      delimiters.push_back(t);
    } else if (!delimiters.empty() && closes(delimiters.back(), t)) {
      delimiters.pop_back();
    }
    tokens.push_back(advance());
  }
  if (tokens.empty())
    return TypeSyntax::invalid("", peek().Loc, peek().Loc);

  for (size_t index = 0; index < tokens.size(); ++index) {
    if (tokens[index].Kind == TokenType::TokenNull) {
      HasError = true;
      DiagnosticEngine::report(
          tokens[index].Loc,
          DiagID::ERR_SAFE_NULLABLE_PAYLOAD_REMOVED, "?");
      continue;
    }
    if (tokens[index].Kind != TokenType::KwNul)
      continue;
    for (size_t next = index + 1; next < tokens.size(); ++next) {
      if (tokens[next].Kind == TokenType::Caret ||
          tokens[next].Kind == TokenType::Tilde) {
        HasError = true;
        DiagnosticEngine::report(
            tokens[index].Loc,
            DiagID::ERR_SAFE_NULLABLE_HANDLE_REMOVED,
            tokens[next].Kind == TokenType::Caret ? "nul ^T" : "nul ~T");
      }
      if (tokens[next].Kind == TokenType::Caret ||
          tokens[next].Kind == TokenType::Tilde ||
          tokens[next].Kind == TokenType::Star ||
          tokens[next].Kind == TokenType::Ampersand ||
          tokens[next].Kind == TokenType::And ||
          tokens[next].Kind == TokenType::Identifier ||
          tokens[next].Kind == TokenType::KwUpperSelf)
        break;
    }
  }

  TypeSyntaxPtr syntax = TypeSyntaxBuilder(tokens).parse();
  const bool containsNever = std::any_of(
      tokens.begin(), tokens.end(), [](const Token &token) {
        return token.Kind == TokenType::KwNever;
      });
  if (!allowNever && containsNever) {
    if (!PanicMode) {
      HasError = true;
      DiagnosticEngine::report(tokens.front().Loc,
                               DiagID::ERR_PARSER_NEVER_TYPE_RESTRICTED);
    }
  }
  const bool isDirectAbiVoid =
      allowAbiVoid && syntax->NodeKind == TypeSyntax::Kind::Named &&
      syntax->Text == "void";
  if (!isDirectAbiVoid && usesVoidOutsideRawPointee(syntax)) {
    if (!PanicMode) {
      HasError = true;
      DiagnosticEngine::report(tokens.front().Loc,
                               DiagID::ERR_PARSER_VOID_TYPE_RESTRICTED);
    }
  }
  if (syntax->NodeKind == TypeSyntax::Kind::Invalid) {
    // The caller still owns its surrounding delimiter.  This is a recovered
    // type error, not a declaration-level synchronization point: entering
    // panic mode here would make parseBlock consume that delimiter and report
    // a misleading second error.
    if (!PanicMode) {
      HasError = true;
      DiagnosticEngine::report(tokens.front().Loc, DiagID::ERR_GENERIC_PARSE,
                               "invalid type syntax");
    }
  }
  return syntax;
}

TypeSyntaxPtr Parser::parseRequiredTypeSyntax(bool allowDirectVoid) {
  if (!isTypeStart()) {
    error(peek(), DiagID::ERR_PARSER_EXPECTED_TYPE_ANNOTATION);
    return TypeSyntax::invalid("", peek().Loc, peek().Loc);
  }
  return parseTypeSyntax(true, false, false, false, allowDirectVoid);
}

TypeArgumentSyntax Parser::parseTypeArgumentSyntax() {
  const Token &start = peek();
  const bool isConst =
      start.Kind == TokenType::Integer || start.Kind == TokenType::Float ||
      start.Kind == TokenType::KwTrue || start.Kind == TokenType::KwFalse ||
      (start.Kind == TokenType::Identifier && !start.Text.empty() &&
       start.Text.back() == '_');
  if (isConst) {
    Token value = advance();
    return TypeArgumentSyntax::constant(canonicalTypeTokenText(value), value.Loc,
                                        value.Loc);
  }
  return TypeArgumentSyntax::type(parseTypeSyntax());
}

std::string Parser::canonicalType(const TypeSyntaxPtr &syntax,
                                  const std::string &fallback) {
  return syntax ? syntax->toCanonicalString() : fallback;
}

bool Parser::isNextNamedField(int startOffset) const {
  int lookAhead = startOffset;
  int balance = 0;
  while (true) {
    const Token &tok = peekAt(lookAhead);
    if (tok.Kind == TokenType::EndOfFile) {
      break;
    }
    if (balance == 0 && (tok.Kind == TokenType::RParen || tok.Kind == TokenType::RBrace || tok.Kind == TokenType::Comma)) {
      break;
    }
    
    if (tok.Kind == TokenType::LParen || tok.Kind == TokenType::LBrace || tok.Kind == TokenType::LBracket) {
      balance++;
      lookAhead++;
      continue;
    }
    if (tok.Kind == TokenType::RParen || tok.Kind == TokenType::RBrace || tok.Kind == TokenType::RBracket) {
      if (balance > 0) {
        balance--;
      }
      lookAhead++;
      continue;
    }
    
    if (balance == 0 && tok.Kind == TokenType::Equal) {
      return true;
    }
    
    lookAhead++;
  }
  return false;
}

bool Parser::isNamedInitList() const {
  int balance = 0;
  int offset = 0;
  while (true) {
    const Token &tok = peekAt(offset);
    if (tok.Kind == TokenType::EndOfFile) {
      break;
    }
    if (balance == 0 && (tok.Kind == TokenType::RParen || tok.Kind == TokenType::RBrace)) {
      break;
    }
    
    if (tok.Kind == TokenType::LParen || tok.Kind == TokenType::LBrace || tok.Kind == TokenType::LBracket) {
      balance++;
      offset++;
      continue;
    }
    if (tok.Kind == TokenType::RParen || tok.Kind == TokenType::RBrace || tok.Kind == TokenType::RBracket) {
      balance--;
      offset++;
      continue;
    }
    
    if (balance == 0) {
      if (isNextNamedField(offset)) {
        return true;
      }
    }
    
    offset++;
  }
  return false;
}

std::string Parser::parseNamespaceOrIdentifier() {
  Token nameTok = consume(TokenType::Identifier, DiagID::ERR_EXPECTED_IDENTIFIER);
  return nameTok.Text;
}

std::unique_ptr<Module> Parser::parseModule() {
  auto module = std::make_unique<Module>();
  // module->FileName = m_CurrentFile;
  if (peek().Kind != TokenType::EndOfFile) {
    module->Loc = peek().Loc;
  } else {
    // Empty file, usage might be tricky, but we should still have valid loc
    // from EOF token
    module->Loc = peek().Loc;
  }

  // [NEW] Inject implicit prelude import
  // Exclude core library files to prevent circular dependencies (e.g. types.tk,
  // traits.tk)
  bool isCoreLib =
      m_CurrentFile.find("lib/core/") != std::string::npos ||
      m_CurrentFile.find("core/") == 0 ||
      m_CurrentFile.find("lib/sys/libc.tk") != std::string::npos; // if relative path starts with core/

  if (!isCoreLib && m_CurrentFile.find("prelude.tk") == std::string::npos) {
    // import core/prelude::{*}
    std::vector<ImportItem> items;
    items.push_back({"*", ""});
    auto preludeImp =
        std::make_unique<ImportDecl>(false, "core/prelude", "", items);
    preludeImp->Loc = module->Loc;
    preludeImp->IsImplicit = true;
    module->Imports.push_back(std::move(preludeImp));
  }

  while (peek().Kind != TokenType::EndOfFile) {

    bool isPub = false;
    if (match(TokenType::KwPub)) {
      isPub = true;
    }

    if (check(TokenType::KwImport)) {
      module->Imports.push_back(parseImport(isPub));
    } else if (check(TokenType::KwFn)) {
      module->Functions.push_back(parseFunctionDecl(isPub));
    } else if (check(TokenType::KwLet) || check(TokenType::KwAuto) ||
               check(TokenType::KwConst) ||
               (check(TokenType::Identifier) && peek().Text == "var")) {
      module->Globals.push_back(parseVariableDecl(isPub));
    } else if (check(TokenType::KwType) || check(TokenType::KwAlias)) {
      module->TypeAliases.push_back(parseTypeAliasDecl(isPub));
    } else if (check(TokenType::KwExtern)) {
      if (isPub) {
        error(peek(), DiagID::ERR_EXTERN_PUB);
      }
      module->Externs.push_back(parseExternDecl());
    } else if (check(TokenType::KwImpl)) {
      if (isPub) {
        error(peek(), DiagID::ERR_IMPL_PUB);
      }
      module->Impls.push_back(parseImpl());
    } else if (check(TokenType::KwTrait)) {
      module->Traits.push_back(parseTrait(isPub));
    } else if (check(TokenType::KwShape) || check(TokenType::KwUnion)) {
      module->Shapes.push_back(parseShape(isPub));
    } else if (check(TokenType::Identifier) && checkAt(1, TokenType::Equal)) {
      // Legacy or alternate struct init?
      module->Shapes.push_back(parseShape(isPub));
    } else {
      if (isPub) {
        error(peek(), DiagID::ERR_EXPECTED_DECL);
      } else {
        error(peek(), DiagID::ERR_PARSER_UNEXPECTED_TOP_LEVEL_TOKEN, peek().toString());
      }
      advance();
    }

    if (PanicMode) {
        synchronize();
    }
  }
  return module;
}

} // namespace toka
