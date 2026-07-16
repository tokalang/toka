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
#include "toka/Type.h"
#include "toka/AST.h"
#include "toka/Sema.h"
#include <memory>
#include <sstream>
#include <string>

namespace toka {

bool Type::equals(const toka::Type &other) const {
  if (typeKind != other.typeKind)
    return false;
  return IsWritable == other.IsWritable && IsNullable == other.IsNullable &&
         IsBlocked == other.IsBlocked && IsCede == other.IsCede;
}

bool Type::isSend(class Sema *S) const { return false; }
bool Type::isSync(class Sema *S) const { return false; }

// Check compatibility (Permission Flow)
bool Type::isCompatibleWith(const Type &target) const {
  if (typeKind != target.typeKind)
    return false;
  // Target Writable? Source must be Writable.
  // Mutability check removed from base: T -> T# is allowed for values (copy).
  // Strict mutability is enforced in Pointer/Reference types where it matters.
  // Target Non-Nullable? Source must be Non-Nullable.
  if (!target.IsNullable && IsNullable)
    return false;
  return true;
}

// --- Attribute Helpers ---
template <typename T>
std::shared_ptr<Type> cloneWithAttrs(const T *original, bool w, bool n,
                                     bool b = false) {
  auto clone = std::make_shared<T>(*original);
  clone->IsBlocked = b || original->IsBlocked;
  if (clone->IsBlocked) {
    clone->IsWritable = false;
    clone->IsNullable = n; // Keep original/requested nullability
  } else {
    clone->IsWritable = w;
    clone->IsNullable = n;
  }
  return clone;
}

// --- Implementations ---

std::shared_ptr<Type> VoidType::withAttributes(bool w, bool n, bool b) const {
  return cloneWithAttrs(this, w, n, b);
}

bool VoidType::isSend(class Sema *S) const { return true; }
bool VoidType::isSync(class Sema *S) const { return true; }

std::string UninitType::toString() const {
  std::string s = "";
  if (IsCede) s += "cede ";
  s += "Uninit<" + InnerType->toString() + ">";
  if (IsWritable && IsNullable) {
    s += "?#";
  } else {
    if (IsNullable) s += "?";
    if (IsWritable) s += "#";
    if (IsBlocked) s += "$";
  }
  return s;
}

bool UninitType::equals(const Type &other) const {
  if (!Type::equals(other)) return false;
  const auto *otherU = dynamic_cast<const UninitType *>(&other);
  return otherU && InnerType->equals(*otherU->InnerType);
}

bool UninitType::isCompatibleWith(const Type &target) const {
  if (!Type::isCompatibleWith(target)) return false;
  const auto *otherU = dynamic_cast<const UninitType *>(&target);
  return otherU && InnerType->isCompatibleWith(*otherU->InnerType);
}

std::shared_ptr<Type> UninitType::withAttributes(bool w, bool n, bool b) const {
  return cloneWithAttrs(this, w, n, b);
}

std::shared_ptr<Type> UninitType::substitute(const std::map<std::string, std::shared_ptr<Type>> &substMap) const {
  auto ut = std::dynamic_pointer_cast<UninitType>(withAttributes(IsWritable, IsNullable, IsBlocked));
  if (InnerType) ut->InnerType = InnerType->substitute(substMap);
  return ut;
}

std::string PrimitiveType::toString() const {
  std::string s = "";
  if (IsCede) s += "cede ";
  s += Name;
  if (IsBlocked)
    s += "$";
  if (IsWritable && IsNullable)
    s += "?#";
  else {
    if (IsNullable)
      s += "?";
    if (IsWritable)
      s += "#";
  }
  return s;
}

bool PrimitiveType::equals(const Type &other) const {
  if (!Type::equals(other))
    return false;
  const auto *otherPrim = dynamic_cast<const PrimitiveType *>(&other);
  return otherPrim && Name == otherPrim->Name;
}

std::shared_ptr<Type> PrimitiveType::withAttributes(bool w, bool n,
                                                    bool b) const {
  return cloneWithAttrs(this, w, n, b);
}

bool PrimitiveType::isCompatibleWith(const Type &target) const {
  if (!Type::isCompatibleWith(target)) {
    const auto *otherPrim = dynamic_cast<const PrimitiveType *>(&target);
    if (otherPrim && isInteger() && otherPrim->isInteger())
      return true; // Loose integer compatibility
    return false;
  }
  const auto *otherPrim = dynamic_cast<const PrimitiveType *>(&target);
  if (!otherPrim)
    return false;
  if (Name == otherPrim->Name)
    return true;
  return isInteger() && otherPrim->isInteger();
}

bool PrimitiveType::isSend(class Sema *S) const { return true; }
bool PrimitiveType::isSync(class Sema *S) const { return true; }

// --- Pointers ---

bool PointerType::equals(const Type &other) const {
  if (!Type::equals(other))
    return false;
  const auto *otherPtr = dynamic_cast<const PointerType *>(&other);
  if (!otherPtr)
    return false;
  return PointeeType->equals(*otherPtr->PointeeType);
}

bool PointerType::isCompatibleWith(const Type &target) const {
  if (!Type::isCompatibleWith(target))
    return false;
  const auto *otherPtr = dynamic_cast<const PointerType *>(&target);
  if (!otherPtr)
    return false;
  if (otherPtr->PointeeType->IsWritable && !PointeeType->IsWritable)
    return false;
  return PointeeType->isCompatibleWith(*otherPtr->PointeeType);
}

std::string RawPointerType::toString() const {
  std::string s = "";
  if (IsCede) s += "cede ";
  if (IsNullable) {
    s += "nul ";
  }
  s += "*";
  if (IsWritable)
    s += "#";
  if (IsBlocked)
    s += "$";
  return s + PointeeType->toString();
}

bool RawPointerType::isCompatibleWith(const Type &target) const {
  const auto *otherPtr = dynamic_cast<const RawPointerType *>(&target);
  if (!otherPtr)
    return false;
  // Raw pointers are unsafe; we relax soul mutability checks to allow
  // easier interfacing with memory management (e.g. malloc/realloc).
  if (!Type::isCompatibleWith(target)) return false;
  if (PointeeType->typeKind == Void || otherPtr->PointeeType->typeKind == Void)
    return true;
  return PointeeType->isCompatibleWith(*otherPtr->PointeeType);
}

std::shared_ptr<Type> RawPointerType::withAttributes(bool w, bool n,
                                                     bool b) const {
  return cloneWithAttrs(this, w, n, b);
}

bool RawPointerType::isSend(class Sema *S) const { return false; }
bool RawPointerType::isSync(class Sema *S) const { return false; }

std::string UniquePointerType::toString() const {
  std::string s = "";
  if (IsCede) s += "cede ";
  if (IsNullable) {
    s += "nul ";
  }
  s += "^";
  if (IsWritable)
    s += "#";
  if (IsBlocked)
    s += "$";
  return s + PointeeType->toString();
}

bool UniquePointerType::isCompatibleWith(const Type &target) const {
  const auto *otherPtr = dynamic_cast<const UniquePointerType *>(&target);
  if (otherPtr) {
    if (otherPtr->PointeeType->IsWritable && !PointeeType->IsWritable)
      return false;
    return Type::isCompatibleWith(target) &&
           PointeeType->isCompatibleWith(*otherPtr->PointeeType);
  }
  return false;
}

std::shared_ptr<Type> UniquePointerType::withAttributes(bool w, bool n,
                                                        bool b) const {
  return cloneWithAttrs(this, w, n, b);
}

bool UniquePointerType::isSend(class Sema *S) const { return PointeeType ? PointeeType->isSend(S) : false; }
bool UniquePointerType::isSync(class Sema *S) const { return PointeeType ? PointeeType->isSync(S) : false; }

std::string SharedPointerType::toString() const {
  std::string s = "";
  if (IsCede) s += "cede ";
  if (IsNullable) {
    s += "nul ";
  }
  s += "~";
  if (IsWritable)
    s += "#";
  if (IsBlocked)
    s += "$";
  return s + PointeeType->toString();
}

bool SharedPointerType::isCompatibleWith(const Type &target) const {
  const auto *otherPtr = dynamic_cast<const SharedPointerType *>(&target);
  if (otherPtr) {
    if (otherPtr->PointeeType->IsWritable && !PointeeType->IsWritable)
      return false;
    return Type::isCompatibleWith(target) &&
           PointeeType->isCompatibleWith(*otherPtr->PointeeType);
  }
  if (!dynamic_cast<const PointerType *>(&target)) {
    return PointeeType->isCompatibleWith(target);
  }
  return false;
}

std::shared_ptr<Type> SharedPointerType::withAttributes(bool w, bool n,
                                                        bool b) const {
  return cloneWithAttrs(this, w, n, b);
}

bool SharedPointerType::isSend(class Sema *S) const { return PointeeType ? (PointeeType->isSend(S) && PointeeType->isSync(S)) : false; }
bool SharedPointerType::isSync(class Sema *S) const { return PointeeType ? (PointeeType->isSend(S) && PointeeType->isSync(S)) : false; }

std::string ReferenceType::toString() const {
  std::string s = "";
  if (IsCede) s += "cede ";
  if (IsNullable) {
    s += "nul ";
  }
  s += "&";
  if (IsWritable)
    s += "#";
  if (IsBlocked)
    s += "$";
  return s + PointeeType->toString();
}

bool ReferenceType::isCompatibleWith(const Type &target) const {
  const auto *otherPtr = dynamic_cast<const ReferenceType *>(&target);
  if (otherPtr) {
    if (otherPtr->PointeeType->IsWritable && !PointeeType->IsWritable)
      return false;
    return Type::isCompatibleWith(target) &&
           PointeeType->isCompatibleWith(*otherPtr->PointeeType);
  }
  return false;
}

std::shared_ptr<Type> ReferenceType::withAttributes(bool w, bool n,
                                                    bool b) const {
  return cloneWithAttrs(this, w, n, b);
}

bool ReferenceType::isSend(class Sema *S) const { return PointeeType ? PointeeType->isSync(S) : false; }
bool ReferenceType::isSync(class Sema *S) const { return PointeeType ? PointeeType->isSync(S) : false; }

// --- Composite ---

std::string ArrayType::toString() const {
  std::string s = "";
  if (IsCede) s += "cede ";
  s += "[";
  s += ElementType->toString();
  s += "; ";
  if (!SymbolicSize.empty()) {
    s += SymbolicSize;
  } else {
    s += std::to_string(Size);
  }
  s += "]";
  if (IsWritable && IsNullable) {
    s += "?#";
  } else {
    if (IsNullable)
      s += "?";
    if (IsWritable)
      s += "#";
    if (IsBlocked)
      s += "$";
  }
  return s;
}

std::string SliceType::toString() const {
  std::string s = "";
  if (IsCede) s += "cede ";
  s += "[";
  s += ElementType->toString();
  s += "]";
  if (IsWritable && IsNullable) {
    s += "?#";
  } else {
    if (IsNullable)
      s += "?";
    if (IsWritable)
      s += "#";
    if (IsBlocked)
      s += "$";
  }
  return s;
}

bool SliceType::equals(const Type &other) const {
  if (!Type::equals(other))
    return false;
  const auto *otherSlice = dynamic_cast<const SliceType *>(&other);
  return otherSlice && ElementType->equals(*otherSlice->ElementType);
}

bool SliceType::isCompatibleWith(const Type &target) const {
  if (!Type::isCompatibleWith(target))
    return false;
  const auto *otherSlice = dynamic_cast<const SliceType *>(&target);
  return otherSlice && ElementType->isCompatibleWith(*otherSlice->ElementType);
}

std::shared_ptr<Type> SliceType::withAttributes(bool w, bool n, bool b) const {
  return cloneWithAttrs(this, w, n, b);
}

bool SliceType::isSend(class Sema *S) const { return ElementType ? ElementType->isSend(S) : false; }
bool SliceType::isSync(class Sema *S) const { return ElementType ? ElementType->isSync(S) : false; }

bool ArrayType::equals(const Type &other) const {
  if (!Type::equals(other))
    return false;
  const auto *otherArr = dynamic_cast<const ArrayType *>(&other);
  return otherArr && Size == otherArr->Size &&
         ElementType->equals(*otherArr->ElementType);
}

bool ArrayType::isCompatibleWith(const Type &target) const {
  if (!Type::isCompatibleWith(target))
    return false;
  const auto *otherArr = dynamic_cast<const ArrayType *>(&target);
  return otherArr && Size == otherArr->Size &&
         ElementType->isCompatibleWith(*otherArr->ElementType);
}

std::shared_ptr<Type> ArrayType::withAttributes(bool w, bool n, bool b) const {
  return cloneWithAttrs(this, w, n, b);
}

bool ArrayType::isSend(class Sema *S) const { return ElementType ? ElementType->isSend(S) : false; }
bool ArrayType::isSync(class Sema *S) const { return ElementType ? ElementType->isSync(S) : false; }

std::string ShapeType::toString() const {
  std::string s = "";
  if (IsCede) s += "cede ";
  s += Name;
  if (!GenericArgs.empty()) {
    s += "<";
    for (size_t i = 0; i < GenericArgs.size(); ++i) {
      if (i > 0)
        s += ", ";
      s += GenericArgs[i]->toString();
    }
    s += ">";
  }
  s += VariantSuffix;
  if (IsWritable && IsNullable) {
    s += "?#";
  } else {
    if (IsNullable)
      s += "?";
    if (IsWritable)
      s += "#";
    if (IsBlocked)
      s += "$";
  }
  return s;
}

bool ShapeType::equals(const Type &other) const {
  if (!Type::equals(other))
    return false;
  const auto *otherSh = dynamic_cast<const ShapeType *>(&other);
  if (!otherSh)
    return false;
  if (Decl && otherSh->Decl)
    return Decl == otherSh->Decl;
  return Name == otherSh->Name;
}

bool ShapeType::isCompatibleWith(const Type &target) const {
  const auto *otherSh = dynamic_cast<const ShapeType *>(&target);
  if (otherSh) {
    if (otherSh->Name.rfind("dyn@", 0) == 0)
      return true;
    if (Decl && otherSh->Decl && Decl != otherSh->Decl)
      return false;
    if (Name == otherSh->Name)
      return Type::isCompatibleWith(target);
    return false;
  }
  return false;
}

std::shared_ptr<Type> ShapeType::withAttributes(bool w, bool n, bool b) const {
  auto clone = cloneWithAttrs(this, w, n, b);
  if (Decl)
    std::dynamic_pointer_cast<ShapeType>(clone)->resolve(Decl);
  return clone;
}

void ShapeType::resolve(ShapeDecl *decl) {
  Decl = decl;
  if (decl) {
    Name = decl->Name;
    IsSync = decl->IsSync; // [NEW] Propagate thread-safety bounds
  }
}

bool ShapeType::isSend(class Sema *S) const {
  if (!S) return false;
  return S->isShapeSend(S->resolveType(this->toString()));
}
bool ShapeType::isSync(class Sema *S) const {
  if (!S) return false;
  return S->isShapeSync(S->resolveType(this->toString()));
}
std::string FunctionType::toString() const {
  std::string s = "";
  if (ReceiverMode == CallableReceiverMode::Consuming) s += "cede ";
  s += ReceiverMode == CallableReceiverMode::Mutable ? "fn#(" : "fn(";
  for (size_t i = 0; i < ParamTypes.size(); ++i) {
    if (i > 0)
      s += ", ";
    s += ParamTypes[i]->toString();
  }
  if (IsVariadic)
    s += ", ...";
  s += ")";
  if (ReturnType && ReturnType->typeKind != Void) {
    s += " -> ";
    s += ReturnType->toString();
  }
  if (IsWritable) s += "#";
  return s;
}

bool FunctionType::equals(const Type &other) const {
  if (!Type::equals(other))
    return false;
  const auto *otherFn = dynamic_cast<const FunctionType *>(&other);
  if (!otherFn || ReceiverMode != otherFn->ReceiverMode ||
      ParamTypes.size() != otherFn->ParamTypes.size())
    return false;
  return ReturnType->equals(*otherFn->ReturnType);
}

bool FunctionType::isCompatibleWith(const Type &target) const {
  if (!Type::isCompatibleWith(target))
    return false;
  const auto *otherFn = dynamic_cast<const FunctionType *>(&target);
  if (!otherFn || ReceiverMode != otherFn->ReceiverMode ||
      ParamTypes.size() != otherFn->ParamTypes.size())
    return false;
  if (!ReturnType->isCompatibleWith(*otherFn->ReturnType))
    return false;
  for (size_t i = 0; i < ParamTypes.size(); ++i) {
    if (!ParamTypes[i]->equals(*otherFn->ParamTypes[i]))
      return false;
  }
  return true;
}

std::shared_ptr<Type> FunctionType::withAttributes(bool w, bool n,
                                                   bool b) const {
  return cloneWithAttrs(this, w, n, b);
}

bool FunctionType::isSend(class Sema *S) const { return true; } // Function pointers are inherently stateless hence sendable
bool FunctionType::isSync(class Sema *S) const { return true; }

std::string DynFnType::toString() const {
  std::string s = "";
  if (ReceiverMode == CallableReceiverMode::Consuming) s += "cede ";
  s += ReceiverMode == CallableReceiverMode::Mutable ? "dyn fn#(" : "dyn fn(";
  for (size_t i = 0; i < ParamTypes.size(); ++i) {
    if (i > 0)
      s += ", ";
    s += ParamTypes[i]->toString();
  }
  s += ")";
  if (ReturnType && ReturnType->typeKind != Void) {
    s += " -> ";
    s += ReturnType->toString();
  }
  if (IsWritable) s += "#";
  return s;
}

bool DynFnType::equals(const Type &other) const {
  if (!Type::equals(other))
    return false;
  const auto *otherFn = dynamic_cast<const DynFnType *>(&other);
  if (!otherFn || ReceiverMode != otherFn->ReceiverMode ||
      ParamTypes.size() != otherFn->ParamTypes.size())
    return false;
  return ReturnType->equals(*otherFn->ReturnType);
}

bool DynFnType::isCompatibleWith(const Type &target) const {
  if (!Type::isCompatibleWith(target))
    return false;
  const auto *otherFn = dynamic_cast<const DynFnType *>(&target);
  if (!otherFn || ReceiverMode != otherFn->ReceiverMode ||
      ParamTypes.size() != otherFn->ParamTypes.size())
    return false;
  if (!ReturnType->isCompatibleWith(*otherFn->ReturnType))
    return false;
  for (size_t i = 0; i < ParamTypes.size(); ++i) {
    if (!ParamTypes[i]->equals(*otherFn->ParamTypes[i]))
      return false;
  }
  return true;
}

std::shared_ptr<Type> DynFnType::withAttributes(bool w, bool n,
                                                   bool b) const {
  return cloneWithAttrs(this, w, n, b);
}

bool DynFnType::isSend(class Sema *S) const { return true; }
bool DynFnType::isSync(class Sema *S) const { return true; }

std::shared_ptr<Type> UnresolvedType::withAttributes(bool w, bool n,
                                                     bool b) const {
  return cloneWithAttrs(this, w, n, b);
}

// --- Substitution Implementations ---

std::shared_ptr<Type> PointerType::substitute(const std::map<std::string, std::shared_ptr<Type>> &substMap) const {
  auto pt = std::dynamic_pointer_cast<PointerType>(withAttributes(IsWritable, IsNullable, IsBlocked));
  if (PointeeType) pt->PointeeType = PointeeType->substitute(substMap);
  return pt;
}

std::shared_ptr<Type> ArrayType::substitute(const std::map<std::string, std::shared_ptr<Type>> &substMap) const {
  auto at = std::dynamic_pointer_cast<ArrayType>(withAttributes(IsWritable, IsNullable, IsBlocked));
  if (ElementType) at->ElementType = ElementType->substitute(substMap);
  if (!SymbolicSize.empty() && substMap.count(SymbolicSize)) {
    at->SymbolicSize = substMap.at(SymbolicSize)->toString();
  }
  return at;
}

std::shared_ptr<Type> SliceType::substitute(const std::map<std::string, std::shared_ptr<Type>> &substMap) const {
  auto st = std::dynamic_pointer_cast<SliceType>(withAttributes(IsWritable, IsNullable, IsBlocked));
  if (ElementType) st->ElementType = ElementType->substitute(substMap);
  return st;
}

std::shared_ptr<Type> ShapeType::substitute(const std::map<std::string, std::shared_ptr<Type>> &substMap) const {
  if (substMap.count(Name)) {
    // Substitute base. Does not usually have VariantSuffix but we can append it if needed, or just return.
    auto substituted = substMap.at(Name)->withAttributes(IsWritable, IsNullable, IsBlocked);
    if (!VariantSuffix.empty()) {
      if (auto st = std::dynamic_pointer_cast<ShapeType>(substituted)) {
        st->VariantSuffix += VariantSuffix;
      }
    }
    return substituted;
  }
  auto st = std::dynamic_pointer_cast<ShapeType>(withAttributes(IsWritable, IsNullable, IsBlocked));
  st->VariantSuffix = VariantSuffix;
  for (auto &arg : st->GenericArgs) {
    if (arg) arg = arg->substitute(substMap);
  }
  return st;
}

std::shared_ptr<Type> FunctionType::substitute(const std::map<std::string, std::shared_ptr<Type>> &substMap) const {
  auto ft = std::dynamic_pointer_cast<FunctionType>(withAttributes(IsWritable, IsNullable, IsBlocked));
  for (auto &param : ft->ParamTypes) {
    if (param) param = param->substitute(substMap);
  }
  if (ft->ReturnType) ft->ReturnType = ft->ReturnType->substitute(substMap);
  return ft;
}

std::shared_ptr<Type> DynFnType::substitute(const std::map<std::string, std::shared_ptr<Type>> &substMap) const {
  auto dt = std::dynamic_pointer_cast<DynFnType>(withAttributes(IsWritable, IsNullable, IsBlocked));
  for (auto &param : dt->ParamTypes) {
    if (param) param = param->substitute(substMap);
  }
  if (dt->ReturnType) dt->ReturnType = dt->ReturnType->substitute(substMap);
  return dt;
}

// --- Static Factory (The Parser) ---

std::string Type::stripMorphology(const std::string &name) {
  std::string s = name;
  if (s.empty())
    return "";

  // 0. Strip "nul " prefix
  if (s.rfind("nul ", 0) == 0) {
    s = s.substr(4);
  }
  
  // 1. Strip Prefixes (*, ^, ~, &) and their modifiers
  size_t start = 0;
  while (start < s.size()) {
    char c = s[start];
    if (c == '*' || c == '^' || c == '~' || c == '&' || c == '#' || c == '?' ||
        c == '$') {
      start++;
    } else {
      break;
    }
  }
  s = s.substr(start);

  // 2. Strip Suffixes (#, ?, $)
  while (!s.empty()) {
    char c = s.back();
    if (c == '#' || c == '?' || c == '$') {
      s.pop_back();
    } else {
      break;
    }
  }
  return s;
}


std::string Type::stripPrefixes(const std::string &name) {
  std::string s = name;
  if (s.empty()) return "";
  if (s.rfind("nul ", 0) == 0) s = s.substr(4);
  size_t start = 0;
  while (start < s.size()) {
    char c = s[start];
    if (c == '*' || c == '^' || c == '~' || c == '&' || c == '#' || c == '?' || c == '$') start++;
    else break;
  }
  return s.substr(start);
}

static std::string trim(const std::string &str) {
  size_t first = str.find_first_not_of(' ');
  if (std::string::npos == first)
    return str;
  size_t last = str.find_last_not_of(' ');
  return str.substr(first, (last - first + 1));
}

std::shared_ptr<Type> Type::fromString(const std::string &rawType) {
  std::string s = trim(rawType);
  if (s.empty())
    return std::make_shared<VoidType>();

  // [NEW] Strip Lifetime Dependency "<-" (Metadata Only)
  int balance = 0;
  for (size_t i = 0; i < s.size(); ++i) {
    // Look ahead for "<-" at top level
    if (balance == 0 && i + 1 < s.size() && s[i] == '<' && s[i + 1] == '-') {
      s = trim(s.substr(0, i)); // Strip it off
      break;
    }

    if (s[i] == '<')
      balance++;
    else if (s[i] == '>')
      balance--;
    else if (s[i] == '(')
      balance++;
    else if (s[i] == ')')
      balance--;
    else if (s[i] == '[')
      balance++;
    else if (s[i] == ']')
      balance--;
  }

  // Parse Suffixes (applies to the OUTERMOST type being constructed)
  bool isWritable = false;
  bool isNullable = false;
  bool isBlocked = false;
  while (!s.empty()) {
    char back = s.back();
    if (back == '#') {
      isWritable = true;
      s.pop_back();
    } else if (back == '?') {
      isNullable = true;
      s.pop_back();
    } else if (back == '$') {
      isBlocked = true;
      s.pop_back();
    } else if (back == ' ') {
      s.pop_back();
    } else
      break;
  }

  if (s.empty())
    return std::make_shared<UnresolvedType>(rawType);

  bool isCede = false;
  if (s.rfind("cede ", 0) == 0) {
    isCede = true;
    s = trim(s.substr(5));
  }

  bool explicitPtrNullable = false;
  if (s.rfind("nul ", 0) == 0) { // starts_with
    explicitPtrNullable = true;
    s = trim(s.substr(4));
  } else if (s.rfind("nul", 0) == 0 && s.size() > 3 && (s[3] == '*' || s[3] == '^' || s[3] == '~' || s[3] == '&')) {
    explicitPtrNullable = true;
    s = trim(s.substr(3));
  }

  if (s.empty())
    return std::make_shared<UnresolvedType>(rawType);

  bool callableMutable = false;
  if (s.rfind("dyn fn#(", 0) == 0) {
    callableMutable = true;
    s.erase(6, 1);
  } else if (s.rfind("dynfn#(", 0) == 0) {
    callableMutable = true;
    s.erase(5, 1);
  } else if (s.rfind("fn#(", 0) == 0) {
    callableMutable = true;
    s.erase(2, 1);
  }

  bool isDynFnWithSpace = s.rfind("dyn fn(", 0) == 0;
  bool isDynFnWithoutSpace = s.rfind("dynfn(", 0) == 0;
  if (isDynFnWithSpace || isDynFnWithoutSpace) {
    int parenBalance = 0;
    size_t paramsStart = isDynFnWithSpace ? 7 : 6;
    size_t paramsEnd = std::string::npos;
    for (size_t i = paramsStart; i < s.size(); ++i) {
      if (s[i] == '(') parenBalance++;
      else if (s[i] == ')') {
        if (parenBalance == 0) {
          paramsEnd = i;
          break;
        }
        parenBalance--;
      }
    }
    
    if (paramsEnd != std::string::npos) {
      std::string paramsStr = s.substr(paramsStart, paramsEnd - paramsStart);
      std::vector<std::shared_ptr<Type>> paramTypes;
      
      int bal = 0;
      size_t start = 0;
      for (size_t i = 0; i < paramsStr.size(); ++i) {
        if (paramsStr[i] == '<' || paramsStr[i] == '(' || paramsStr[i] == '[') bal++;
        else if (paramsStr[i] == '>' || paramsStr[i] == ')' || paramsStr[i] == ']') bal--;
        else if (paramsStr[i] == ',' && bal == 0) {
          std::string p = trim(paramsStr.substr(start, i - start));
          if (!p.empty()) paramTypes.push_back(Type::fromString(p));
          start = i + 1;
        }
      }
      if (start < paramsStr.size()) {
        std::string p = trim(paramsStr.substr(start));
        if (!p.empty()) paramTypes.push_back(Type::fromString(p));
      }
      
      std::shared_ptr<Type> retType = nullptr;
      size_t arrowPos = s.find("->", paramsEnd);
      if (arrowPos != std::string::npos) {
        retType = Type::fromString(trim(s.substr(arrowPos + 2)));
      } else {
        retType = std::make_shared<VoidType>();
      }
      
      auto fnNode = std::make_shared<DynFnType>(paramTypes, retType);
      fnNode->IsWritable = isWritable;
      fnNode->IsNullable = isNullable;
      fnNode->IsBlocked = isBlocked;
      fnNode->IsCede = isCede;
      fnNode->ReceiverMode =
          isCede ? CallableReceiverMode::Consuming
                 : callableMutable ? CallableReceiverMode::Mutable
                                   : CallableReceiverMode::Shared;
      return fnNode;
    }
  }

  if (s.rfind("fn(", 0) == 0) {
    int parenBalance = 0;
    size_t paramsStart = 3;
    size_t paramsEnd = std::string::npos;
    for (size_t i = paramsStart; i < s.size(); ++i) {
      if (s[i] == '(') parenBalance++;
      else if (s[i] == ')') {
        if (parenBalance == 0) {
          paramsEnd = i;
          break;
        }
        parenBalance--;
      }
    }
    
    if (paramsEnd != std::string::npos) {
      std::string paramsStr = s.substr(paramsStart, paramsEnd - paramsStart);
      std::vector<std::shared_ptr<Type>> paramTypes;
      bool isVariadic = false;
      
      int bal = 0;
      size_t start = 0;
      for (size_t i = 0; i < paramsStr.size(); ++i) {
        if (paramsStr[i] == '<' || paramsStr[i] == '(' || paramsStr[i] == '[') bal++;
        else if (paramsStr[i] == '>' || paramsStr[i] == ')' || paramsStr[i] == ']') bal--;
        else if (paramsStr[i] == ',' && bal == 0) {
          std::string p = trim(paramsStr.substr(start, i - start));
          if (p == "...") isVariadic = true;
          else if (!p.empty()) paramTypes.push_back(Type::fromString(p));
          start = i + 1;
        }
      }
      if (start < paramsStr.size()) {
        std::string p = trim(paramsStr.substr(start));
        if (p == "...") isVariadic = true;
        else if (!p.empty()) paramTypes.push_back(Type::fromString(p));
      }
      
      std::shared_ptr<Type> retType = nullptr;
      size_t arrowPos = s.find("->", paramsEnd);
      if (arrowPos != std::string::npos) {
        retType = Type::fromString(trim(s.substr(arrowPos + 2)));
      } else {
        retType = std::make_shared<VoidType>();
      }
      
      auto fnNode = std::make_shared<FunctionType>(paramTypes, retType);
      fnNode->IsVariadic = isVariadic;
      fnNode->IsWritable = isWritable;
      fnNode->IsNullable = isNullable;
      fnNode->IsBlocked = isBlocked;
      fnNode->IsCede = isCede;
      fnNode->ReceiverMode =
          isCede ? CallableReceiverMode::Consuming
                 : callableMutable ? CallableReceiverMode::Mutable
                                   : CallableReceiverMode::Shared;
      return fnNode;
    }
  }

  char first = s[0];
  if (first == '*' || first == '^' || first == '~' || first == '&') {
    size_t offset = 1;
    bool ptrNullable = explicitPtrNullable;
    bool ptrWritable = false;
    bool ptrBlocked = false;
    while (offset < s.size()) {
      if (s[offset] == '#') {
        ptrWritable = true;
        offset++;
      } else if (s[offset] == '$') {
        ptrBlocked = true;
        offset++;
      } else
        break;
    }
    auto pointee = Type::fromString(s.substr(offset));
    // Duality: the outer suffixes stripped earlier belong to the soul
    if (isWritable || isNullable || isBlocked) {
      pointee = pointee->withAttributes(isWritable, isNullable, isBlocked);
    }

    std::shared_ptr<PointerType> ptr;
    if (first == '*')
      ptr = std::make_shared<RawPointerType>(pointee);
    else if (first == '^')
      ptr = std::make_shared<UniquePointerType>(pointee);
    else if (first == '~')
      ptr = std::make_shared<SharedPointerType>(pointee);
    else
      ptr = std::make_shared<ReferenceType>(pointee);

    // Identity: the attributes following the sigil belong to the handle
    ptr->IsNullable = ptrNullable;
    ptr->IsWritable = ptrWritable;
    ptr->IsBlocked = ptrBlocked;
    ptr->IsCede = isCede;
    return ptr;
  }

  if (first == '[') {
    size_t semi = s.find(';');
    size_t close = s.find_last_of(']');
    if (semi != std::string::npos && close != std::string::npos) {
      auto elem = Type::fromString(s.substr(1, semi - 1));
      uint64_t size = 0;
      std::string symSize = "";
      std::string sizeStr = s.substr(semi + 1, close - semi - 1);
      try {
        size = std::stoull(sizeStr);
      } catch (...) {
        symSize = trim(sizeStr);
      }
      auto arr = std::make_shared<ArrayType>(elem, size, symSize);
      arr->IsWritable = isWritable;
      arr->IsNullable = isNullable;
      arr->IsBlocked = isBlocked;
      arr->IsCede = isCede;
      return arr;
    } else if (close != std::string::npos && semi == std::string::npos) {
      // Dynamic Array [T]
      auto elem = Type::fromString(trim(s.substr(1, close - 1)));
      auto slice = std::make_shared<SliceType>(elem);
      slice->IsWritable = isWritable;
      slice->IsNullable = isNullable;
      slice->IsBlocked = isBlocked;
      slice->IsCede = isCede;
      return slice;
    }
  }

  if (s == "void")
    return std::make_shared<VoidType>();
  if (s == "i32" || s == "i64" || s == "u32" || s == "u64" || s == "f32" ||
      s == "f64" || s == "bool" || s == "char" || s == "i8" ||
      s == "u8" || s == "i16" || s == "u16" || s == "usize" || s == "isize" ||
      s == "null" || s == "none" || s == "Addr" || s == "OAddr") {
    auto prim = std::make_shared<PrimitiveType>(s);
    prim->IsWritable = isWritable;
    prim->IsNullable = isNullable;
    prim->IsBlocked = isBlocked;
    prim->IsCede = isCede;
    return prim;
  }

  // Trim whitespace
  auto trim = [](std::string s) {
    size_t first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos)
      return std::string("");
    size_t last = s.find_last_not_of(" \t\r\n");
    return s.substr(first, (last - first + 1));
  };

  s = trim(s);

  if (s == "unknown")
    return std::make_shared<UnresolvedType>(s);


  // Check for generics: Name<Arg1, Arg2>
  size_t lt = s.find('<');
  size_t gt = s.rfind('>');
  std::vector<std::shared_ptr<Type>> genericArgs;
  std::string baseName = s;
  std::string variantSuffix = "";

  if (lt != std::string::npos && gt != std::string::npos && gt > lt) {
    baseName = s.substr(0, lt);
    std::string argsStr = s.substr(lt + 1, gt - lt - 1);
    variantSuffix = trim(s.substr(gt + 1));
    
    // Split args logic (handle nested generics)
    int balance = 0;
    size_t start = 0;
    for (size_t i = 0; i < argsStr.size(); ++i) {
      if (i + 1 < argsStr.size() && argsStr[i] == '<' &&
          argsStr[i + 1] == '-') {
        // Skip dependencies in type
        i++;
        continue;
      }
      if (argsStr[i] == '<')
        balance++;
      else if (argsStr[i] == '>')
        balance--;
      else if (argsStr[i] == ',' && balance == 0) {
        genericArgs.push_back(
            Type::fromString(argsStr.substr(start, i - start)));
        start = i + 1;
      }
    }
    if (start < argsStr.size()) {
      genericArgs.push_back(Type::fromString(argsStr.substr(start)));
    }
  } else {
    // e.g. T::Some, we also need to extract VariantSuffix if there are no brackets
    size_t colcol = s.find("::");
    if (colcol != std::string::npos) {
      baseName = s.substr(0, colcol);
      variantSuffix = trim(s.substr(colcol));
    }
  }

  if (baseName == "Uninit" && genericArgs.size() == 1) {
    auto uninit = std::make_shared<UninitType>(genericArgs[0]);
    uninit->IsWritable = isWritable;
    uninit->IsNullable = isNullable;
    uninit->IsBlocked = isBlocked;
    uninit->IsCede = isCede;
    return uninit;
  }

  auto shape = std::make_shared<ShapeType>(baseName, genericArgs, variantSuffix);
  shape->IsWritable = isWritable;
  shape->IsNullable = isNullable;
  shape->IsBlocked = isBlocked;
  shape->IsCede = isCede;
  return shape;
}

} // namespace toka
