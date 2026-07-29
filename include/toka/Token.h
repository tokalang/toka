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

#include "toka/SourceLocation.h"
#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>

namespace toka {

enum class TokenType {
  // End of file
  EndOfFile,

  // Identifiers & Literals
  Identifier,
  Integer,
  Float,
  String,
  ViewString,
  CharLiteral,

  // Keywords
  KwLet,
  KwAuto,
  KwMut,
  KwType,
  KwAlias,
  KwConst,
  KwShape,
  KwPacked,
  KwTrait,
  KwImpl,
  KwFn,
  KwNew,
  KwDyn,
  KwMove,
  KwWhere,
  KwDefault,
  KwDelete,
  KwFinal,
  KwFnType, // Fn

  KwIf,
  KwGuard,
  KwNul,
  KwElse,
  KwMatch,
  KwCase,
  KwFor,
  KwWhile,
  KwBreak,
  KwContinue,
  KwReturn,
  KwYield,
  KwLoop,
  KwPass,
  KwTo,
  KwOr,
  KwBand,
  KwBor,
  KwBxor,
  KwBnot,
  KwBshl,
  KwBshr,

  KwTask,
  KwSuspend,
  KwAsync,
  KwCancel,
  KwAwait,
  KwWait,
  KwChannel,
  KwCede,
  KwCopy,
  KwImport,
  KwPub,
  KwAs,
  KwIs,
  KwIn,
  KwSelf,
  KwUpperSelf,
  KwTrue,
  KwFalse,
  KwNone,
  KwNull,
  KwDefer,
  KwMain,
  KwExtern,
  KwCrate,
  KwUnsafe,
  KwAlloc,
  KwFree,
  KwUnset,
  KwTodo,
  KwVariant,
  KwUnion,
  KwUnreachable,
  KwFile, // __FILE__
  KwLine, // __LINE__
  KwLoc,  // __LOC__
  KwSizeof,

  // Attribute Tokens (When separate, though usually parsed as part of Ident or
  // Type)
  // We treat attached tokens as properties of the Identifier token in the
  // Lexer,
  // but they might appear standalone or in types.
  TokenWrite,     // #
  TokenNull,      // ?
  TokenWriteNull, // !
  TokenNone,      // $

  // Operators & Symbols
  LParen,
  RParen, // ( )
  LBracket,
  RBracket, // [ ]
  LBrace,
  RBrace,     // { }
  Caret,      // ^
  Comma,      // ,
  Dot,        // .
  Colon,      // :
  Semicolon,  // ;
  Arrow,      // ->
  FatArrow,   // =>
  Dependency, // <-
  Pipe,       // |
  DotDot,     // ..
  DotDotDot,  // ...
  DotDotLess,  // ..<
  DotDotEqual, // ..=
  Ampersand,  // &

  // Arithmetic/Logic (Basic set)
  Plus,
  Minus,
  Star,
  Slash,
  Percent,
  Equal,
  DoubleEqual,
  PlusEqual,
  MinusEqual,
  StarEqual,
  SlashEqual,
  PercentEqual,
  Bang,
  Neq,
  Less,
  LessLess,  // <<
  GenericLT, // < (no space after)
  Greater,
  GreaterGreater, // >>
  LessEqual,
  GreaterEqual,
  PlusPlus,
  MinusMinus,
  Tilde,          // ~
  At,             // @
  And,            // &&
  Or,             // ||
  DoubleQuestion, // ??
  Backtick        // `
};

struct Token {
  TokenType Kind;
  std::string Text; // Or string_view if source is kept alive
  int Line;
  int Column;
  SourceLocation Loc;

  // Attribute flags for identifiers (e.g. if identifier is "x#")
  bool HasWrite = false;
  bool HasNull = false;
  bool IsSwappablePtr = false;   // "swappable" property for pointers
  bool IsBlocked = false;        // "$" attribute for inherent restriction
  bool HasNewlineBefore = false; // Support optional semicolons
  bool HasSpacesAround = false;  // Whether token is surrounded by spaces

  std::string toString() const {
    // Debug helper
    return "Token(" + std::to_string((int)Kind) + ", " + Text + ")";
  }
};

inline bool isAssignment(TokenType T) {
  switch (T) {
  case TokenType::Equal:
  case TokenType::PlusEqual:
  case TokenType::MinusEqual:
  case TokenType::StarEqual:
  case TokenType::SlashEqual:
    return true;
  default:
    return false;
  }
}

inline bool isComparison(TokenType T) {
  switch (T) {
  case TokenType::DoubleEqual:
  case TokenType::Neq:
  case TokenType::Less:
  case TokenType::Greater:
  case TokenType::LessEqual:
  case TokenType::GreaterEqual:
  case TokenType::KwIs:
    return true;
  default:
    return false;
  }
}

inline int getPrecedence(TokenType T) {
  switch (T) {
  case TokenType::Plus:
  case TokenType::Minus:
    return 10;
  case TokenType::Star:
  case TokenType::Slash:
  case TokenType::Percent:
    return 20;
  case TokenType::Equal:
  case TokenType::PlusEqual:
  case TokenType::MinusEqual:
  case TokenType::StarEqual:
  case TokenType::SlashEqual:
    return 1; // Assignment
  case TokenType::DoubleEqual:
  case TokenType::Neq:
  case TokenType::Less:
  case TokenType::Greater:
  case TokenType::LessEqual:
  case TokenType::GreaterEqual:
  case TokenType::KwIs:
    return 5;
  case TokenType::And:
    return 4;
  case TokenType::Or:
    return 3;
  case TokenType::Ampersand:
    return 10; // Same as KwBand
  case TokenType::Pipe:
  case TokenType::Caret:
    return 5; // Same as KwBor/KwBxor
  case TokenType::LessLess:
  case TokenType::GreaterGreater:
    return 20; // Same as KwBshl/KwBshr
  case TokenType::KwBshl:
  case TokenType::KwBshr:
    return 20; // High precedence (multiplication level)
  case TokenType::KwBand:
    return 10; // Medium precedence (additive level)
  case TokenType::KwBor:
  case TokenType::KwBxor:
    return 5; // Low precedence (comparison level)
  default:
    return -1;
  }
}

} // namespace toka
