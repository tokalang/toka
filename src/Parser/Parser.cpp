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
bool Parser::EncapPolicyEpochV2 = false;
bool Parser::EncapLifecycleEpochV3 = false;
bool Parser::EncapCopyEpochV4 = false;
bool Parser::EncapTkiEpochV5 = false;
bool Parser::EncapLibraryEpochV6 = false;

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
  error(peek(), id);
  return peek();
}

std::string Parser::typeTokenText(const Token &tok) const {
  std::string text = tok.Text;
  if (tok.Kind == TokenType::Identifier || tok.Kind == TokenType::KwSelf ||
      tok.Kind == TokenType::KwUpperSelf || tok.Kind == TokenType::KwFn) {
    if (tok.IsBlocked)
      text += "$";
    if (tok.HasNull)
      text += "?";
    if (tok.HasWrite)
      text += "#";
  }
  return text;
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
      case TokenType::KwPacked:
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

std::string Parser::parseTypeString(bool allowAssociatedProjection) {
  std::string type;
  int balance = 0;
  while (!check(TokenType::EndOfFile)) {
    TokenType t = peek().Kind;
    if (t == TokenType::LBracket || t == TokenType::LParen ||
        t == TokenType::GenericLT)
      balance++;

    if (balance == 0 &&
        (check(TokenType::Comma) || check(TokenType::RParen) ||
         check(TokenType::Equal) || isEndOfStatement() ||
         check(TokenType::LBrace) || check(TokenType::Greater) ||
         check(TokenType::Pipe) || check(TokenType::KwFor) ||
         check(TokenType::KwWhere) || check(TokenType::Dependency)))
      break;

    // Special handling for @: stop only if it's not following 'dyn'
    if (balance == 0 && check(TokenType::At)) {
      std::string compactType = type;
      compactType.erase(std::remove(compactType.begin(), compactType.end(), ' '),
                        compactType.end());
      bool isDynTrait = compactType == "dyn";
      if (isDynTrait && checkAt(1, TokenType::LBrace)) {
        bool wasPanic = PanicMode;
        error(peekAt(1), DiagID::ERR_PARSER_DYN_TRAIT_SET_OBJECT_UNSUPPORTED);
        if (!wasPanic) {
          PanicMode = false;
        }
        type += advance().Text; // '@'
        int braceBalance = 0;
        do {
          Token tok = advance();
          type += tok.Text;
          if (tok.Kind == TokenType::LBrace) {
            braceBalance++;
          } else if (tok.Kind == TokenType::RBrace) {
            braceBalance--;
          }
        } while (braceBalance > 0 && !check(TokenType::EndOfFile));
        continue;
      }
      if (!isDynTrait && !type.empty() && !allowAssociatedProjection)
        break;
    }

    if (t == TokenType::RBracket || t == TokenType::RParen ||
        t == TokenType::Greater)
      balance--;

    Token tok = advance();
    std::string text = typeTokenText(tok);
    if (tok.Kind == TokenType::Identifier && !text.empty() && text[0] == '\'') {
        // text = text.substr(1);
    }
    type += text;
    if (tok.Text == "cede" || tok.Text == "dyn") {
      type += " ";
    }
  }
  return type;
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
    } else if (check(TokenType::KwShape) || check(TokenType::KwPacked) ||
               check(TokenType::KwUnion)) {
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
