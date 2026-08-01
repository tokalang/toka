// Copyright (c) 2026 YiZhonghua<zhyi@dpai.com>. All rights reserved.
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
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace toka {

/// Source-level type syntax.  The parser only publishes shared_ptr<const>
/// instances, so a syntax tree can be copied between AST declarations without
/// making a second mutable representation of the spelling.
struct TypeSyntax;
using TypeSyntaxPtr = std::shared_ptr<const TypeSyntax>;

struct TypeArgumentSyntax {
  enum class Kind { Type, Constant };

  Kind ArgumentKind = Kind::Type;
  TypeSyntaxPtr Type;
  std::string ConstantText;
  SourceLocation Begin;
  SourceLocation End;

  static TypeArgumentSyntax type(TypeSyntaxPtr syntax);

  static TypeArgumentSyntax constant(std::string text, SourceLocation begin,
                                     SourceLocation end) {
    TypeArgumentSyntax result;
    result.ArgumentKind = Kind::Constant;
    result.ConstantText = std::move(text);
    result.Begin = begin;
    result.End = end;
    return result;
  }

  std::string toCanonicalString() const;
};

struct TypeSyntax final {
  enum class Kind {
    Invalid,
    Named,
    GenericApplication,
    Array,
    Slice,
    Tuple,
    AnonymousRecord,
    Function,
    DynTrait,
    AssociatedProjection,
    Morphology,
  };

  struct Field {
    std::string Name;
    TypeSyntaxPtr Type;
    SourceLocation Begin;
    SourceLocation End;
  };

  Kind NodeKind = Kind::Invalid;
  SourceLocation Begin;
  SourceLocation End;

  // Named: name. Morphology: prefix. Function: "fn" or "dyn fn".
  // DynTrait: trait spelling. AssociatedProjection: trait spelling.
  // Invalid: recovered source spelling.
  std::string Text;
  // AssociatedProjection's final associated type name.
  std::string MemberName;
  // A namespace/variant path following a generic application (for example
  // `Result<T>::Ok`).  It is source syntax, not an associated type
  // projection.
  std::string PathSuffix;
  // Array's independent constant extent; it is never a generic type
  // argument.  This distinction lets substitution update `N_` without
  // confusing it with `T` in `[T; N_]`.
  TypeArgumentSyntax ExtentArgument;
  TypeSyntaxPtr Subject;
  std::vector<TypeArgumentSyntax> Arguments;
  std::vector<TypeSyntaxPtr> Elements;
  std::vector<Field> Fields;
  TypeSyntaxPtr Result;
  bool HasExplicitResult = false;
  bool IsVariadic = false;
  bool IsPostfix = false;

  static TypeSyntaxPtr invalid(std::string spelling, SourceLocation begin,
                               SourceLocation end) {
    auto result = std::make_shared<TypeSyntax>();
    result->NodeKind = Kind::Invalid;
    result->Text = std::move(spelling);
    result->Begin = begin;
    result->End = end;
    return result;
  }

  static TypeSyntaxPtr named(std::string name, SourceLocation begin,
                             SourceLocation end) {
    auto result = std::make_shared<TypeSyntax>();
    result->NodeKind = Kind::Named;
    result->Text = std::move(name);
    result->Begin = begin;
    result->End = end;
    return result;
  }

  static TypeSyntaxPtr generic(TypeSyntaxPtr subject,
                               std::vector<TypeArgumentSyntax> arguments,
                               SourceLocation begin, SourceLocation end,
                               std::string pathSuffix = "") {
    auto result = std::make_shared<TypeSyntax>();
    result->NodeKind = Kind::GenericApplication;
    result->Subject = std::move(subject);
    result->Arguments = std::move(arguments);
    result->Begin = begin;
    result->End = end;
    result->PathSuffix = std::move(pathSuffix);
    return result;
  }

  static TypeSyntaxPtr array(TypeSyntaxPtr element, TypeArgumentSyntax extent,
                             SourceLocation begin, SourceLocation end) {
    auto result = std::make_shared<TypeSyntax>();
    result->NodeKind = Kind::Array;
    result->Subject = std::move(element);
    result->ExtentArgument = std::move(extent);
    result->Begin = begin;
    result->End = end;
    return result;
  }

  static TypeSyntaxPtr slice(TypeSyntaxPtr element, SourceLocation begin,
                             SourceLocation end) {
    auto result = std::make_shared<TypeSyntax>();
    result->NodeKind = Kind::Slice;
    result->Subject = std::move(element);
    result->Begin = begin;
    result->End = end;
    return result;
  }

  static TypeSyntaxPtr tuple(std::vector<TypeSyntaxPtr> elements,
                             SourceLocation begin, SourceLocation end) {
    auto result = std::make_shared<TypeSyntax>();
    result->NodeKind = Kind::Tuple;
    result->Elements = std::move(elements);
    result->Begin = begin;
    result->End = end;
    return result;
  }

  static TypeSyntaxPtr anonymousRecord(std::vector<Field> fields,
                                       SourceLocation begin,
                                       SourceLocation end) {
    auto result = std::make_shared<TypeSyntax>();
    result->NodeKind = Kind::AnonymousRecord;
    result->Fields = std::move(fields);
    result->Begin = begin;
    result->End = end;
    return result;
  }

  static TypeSyntaxPtr function(std::string kind,
                                std::vector<TypeSyntaxPtr> parameters,
                                TypeSyntaxPtr resultType,
                                bool hasExplicitResult, bool isVariadic,
                                SourceLocation begin, SourceLocation end) {
    auto result = std::make_shared<TypeSyntax>();
    result->NodeKind = Kind::Function;
    result->Text = std::move(kind);
    result->Elements = std::move(parameters);
    result->Result = std::move(resultType);
    result->HasExplicitResult = hasExplicitResult;
    result->IsVariadic = isVariadic;
    result->Begin = begin;
    result->End = end;
    return result;
  }

  static TypeSyntaxPtr dynTrait(std::string trait, SourceLocation begin,
                                SourceLocation end) {
    auto result = std::make_shared<TypeSyntax>();
    result->NodeKind = Kind::DynTrait;
    result->Text = std::move(trait);
    result->Begin = begin;
    result->End = end;
    return result;
  }

  static TypeSyntaxPtr projection(TypeSyntaxPtr subject, std::string trait,
                                  std::string member, SourceLocation begin,
                                  SourceLocation end) {
    auto result = std::make_shared<TypeSyntax>();
    result->NodeKind = Kind::AssociatedProjection;
    result->Subject = std::move(subject);
    result->Text = std::move(trait);
    result->MemberName = std::move(member);
    result->Begin = begin;
    result->End = end;
    return result;
  }

  static TypeSyntaxPtr morphology(std::string spelling, TypeSyntaxPtr subject,
                                  SourceLocation begin, SourceLocation end,
                                  bool isPostfix = false) {
    auto result = std::make_shared<TypeSyntax>();
    result->NodeKind = Kind::Morphology;
    result->Text = std::move(spelling);
    result->Subject = std::move(subject);
    result->Begin = begin;
    result->End = end;
    result->IsPostfix = isPostfix;
    return result;
  }

  std::string toCanonicalString() const {
    switch (NodeKind) {
    case Kind::Invalid:
    case Kind::Named:
      return Text;
    case Kind::DynTrait:
      return "dyn @" + Text;
    case Kind::GenericApplication: {
      std::string result = Subject ? Subject->toCanonicalString() : "";
      result += "<";
      for (size_t i = 0; i < Arguments.size(); ++i) {
        if (i != 0)
          result += ",";
        result += Arguments[i].toCanonicalString();
      }
      return result + ">" + PathSuffix;
    }
    case Kind::Array:
      return "[" + (Subject ? Subject->toCanonicalString() : "") + ";" +
             ExtentArgument.toCanonicalString() + "]";
    case Kind::Slice:
      return "[" + (Subject ? Subject->toCanonicalString() : "") + "]";
    case Kind::Tuple: {
      std::string result = "(";
      for (size_t i = 0; i < Elements.size(); ++i) {
        if (i != 0)
          result += ",";
        result += Elements[i] ? Elements[i]->toCanonicalString() : "";
      }
      return result + ")";
    }
    case Kind::AnonymousRecord: {
      std::string result = "(";
      for (size_t i = 0; i < Fields.size(); ++i) {
        if (i != 0)
          result += ",";
        result += Fields[i].Name + ":";
        result += Fields[i].Type ? Fields[i].Type->toCanonicalString() : "";
      }
      return result + ")";
    }
    case Kind::Function: {
      std::string result = Text + "(";
      for (size_t i = 0; i < Elements.size(); ++i) {
        if (i != 0)
          result += ",";
        result += Elements[i] ? Elements[i]->toCanonicalString() : "";
      }
      if (IsVariadic) {
        if (!Elements.empty())
          result += ",";
        result += "...";
      }
      result += ")";
      if (HasExplicitResult)
        result += "->" + (Result ? Result->toCanonicalString() : "");
      return result;
    }
    case Kind::AssociatedProjection:
      return (Subject ? Subject->toCanonicalString() : "") + "@" + Text +
             "::" + MemberName;
    case Kind::Morphology:
      if (IsPostfix)
        return (Subject ? Subject->toCanonicalString() : "") + Text;
      return Text + (Subject ? Subject->toCanonicalString() : "");
    }
    return "";
  }

  /// Structural substitution for source-derived syntax.  Only exact named
  /// leaves are replaced; it never performs substring replacement.
  TypeSyntaxPtr substitute(
      const std::map<std::string, TypeSyntaxPtr> &replacements) const {
    if (NodeKind == Kind::Named) {
      auto it = replacements.find(Text);
      if (it != replacements.end())
        return it->second;
      return std::make_shared<TypeSyntax>(*this);
    }

    auto result = std::make_shared<TypeSyntax>(*this);
    if (Subject)
      result->Subject = Subject->substitute(replacements);
    if (Result)
      result->Result = Result->substitute(replacements);
    for (auto &element : result->Elements) {
      if (element)
        element = element->substitute(replacements);
    }
    for (auto &argument : result->Arguments) {
      if (argument.ArgumentKind == TypeArgumentSyntax::Kind::Type &&
          argument.Type)
        argument.Type = argument.Type->substitute(replacements);
      else if (argument.ArgumentKind == TypeArgumentSyntax::Kind::Constant) {
        auto it = replacements.find(argument.ConstantText);
        if (it != replacements.end())
          argument.ConstantText = it->second->toCanonicalString();
      }
    }
    for (auto &field : result->Fields) {
      if (field.Type)
        field.Type = field.Type->substitute(replacements);
    }
    if (NodeKind == Kind::Array &&
        result->ExtentArgument.ArgumentKind == TypeArgumentSyntax::Kind::Constant) {
      auto it = replacements.find(result->ExtentArgument.ConstantText);
      if (it != replacements.end())
        result->ExtentArgument.ConstantText = it->second->toCanonicalString();
    }
    return result;
  }

  static TypeSyntaxPtr withoutLeadingMorphology(const TypeSyntaxPtr &syntax,
                                                const std::string &prefix) {
    if (syntax && syntax->NodeKind == Kind::Morphology &&
        !syntax->IsPostfix && syntax->Text == prefix)
      return syntax->Subject;
    return syntax;
  }
};

inline std::string TypeArgumentSyntax::toCanonicalString() const {
  return ArgumentKind == Kind::Type
             ? (Type ? Type->toCanonicalString() : "")
             : ConstantText;
}

inline TypeArgumentSyntax TypeArgumentSyntax::type(TypeSyntaxPtr syntax) {
  TypeArgumentSyntax result;
  result.ArgumentKind = Kind::Type;
  if (syntax) {
    result.Begin = syntax->Begin;
    result.End = syntax->End;
  }
  result.Type = std::move(syntax);
  return result;
}

} // namespace toka
