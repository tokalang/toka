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
#include <algorithm>
#include <cctype>
#include <memory>
#include <sstream>
#include <string>

namespace toka {

bool isPrimitiveValueConstructorName(const std::string &name) {
  return name == "i8" || name == "u8" || name == "i16" || name == "u16" ||
         name == "i32" || name == "u32" || name == "i64" || name == "u64" ||
         name == "isize" || name == "usize" || name == "f32" ||
         name == "f64" || name == "bool" || name == "char" ||
         name == "byte";
}

bool Type::equals(const toka::Type &other) const {
  if (typeKind != other.typeKind)
    return false;
  return IsWritable == other.IsWritable && IsNullable == other.IsNullable &&
         IsBlocked == other.IsBlocked && IsCede == other.IsCede;
}

namespace {

void appendIdentityPart(std::string &out, const char *tag,
                        const std::string &value) {
  out += tag;
  out += std::to_string(value.size());
  out += ':';
  out += value;
  out += ';';
}

std::string shapeDeclIdentity(const ShapeDecl *decl) {
  if (!decl)
    return {};
  if (decl->NominalId)
    return "nominal:" + decl->NominalId->canonical();
  if (decl->InstantiationTemplate) {
    std::string result = "instance{";
    appendIdentityPart(result, "template", shapeDeclIdentity(
                                               decl->InstantiationTemplate));
    for (const auto &argument : decl->InstantiationArgs)
      appendIdentityPart(result, "argument",
                         argument ? argument->canonicalIdentity() : "null");
    result += '}';
    return result;
  }
  std::string result = "synthetic:";
  appendIdentityPart(result, "name", decl->CodegenName.empty()
                                              ? decl->Name
                                              : decl->CodegenName);
  return result;
}

} // namespace

std::string Type::canonicalIdentity() const {
  std::string result = "type{";
  appendIdentityPart(result, "kind", std::to_string(typeKind));
  appendIdentityPart(result, "attributes",
                     std::string(IsWritable ? "1" : "0") +
                         (IsNullable ? "1" : "0") +
                         (IsBlocked ? "1" : "0") +
                         (IsCede ? "1" : "0"));

  if (auto primitive = dynamic_cast<const PrimitiveType *>(this)) {
    appendIdentityPart(result, "primitive", primitive->Name);
  } else if (auto unresolved = dynamic_cast<const UnresolvedType *>(this)) {
    appendIdentityPart(result, "unresolved", unresolved->Name);
  } else if (auto uninit = dynamic_cast<const UninitType *>(this)) {
    appendIdentityPart(result, "inner", uninit->InnerType
                                                ? uninit->InnerType
                                                      ->canonicalIdentity()
                                                : "null");
  } else if (auto outcome = dynamic_cast<const MissOutcomeType *>(this)) {
    appendIdentityPart(result, "payload",
                       outcome->PayloadType
                           ? outcome->PayloadType->canonicalIdentity()
                           : "null");
  } else if (auto pointer = dynamic_cast<const PointerType *>(this)) {
    appendIdentityPart(result, "pointee", pointer->PointeeType
                                                  ? pointer->PointeeType
                                                        ->canonicalIdentity()
                                                  : "null");
  } else if (auto array = dynamic_cast<const ArrayType *>(this)) {
    appendIdentityPart(result, "element", array->ElementType
                                                  ? array->ElementType
                                                        ->canonicalIdentity()
                                                  : "null");
    appendIdentityPart(result, "extent", array->SymbolicSize.empty()
                                                 ? std::to_string(array->Size)
                                                 : array->SymbolicSize);
  } else if (auto slice = dynamic_cast<const SliceType *>(this)) {
    appendIdentityPart(result, "element", slice->ElementType
                                                  ? slice->ElementType
                                                        ->canonicalIdentity()
                                                  : "null");
  } else if (auto shape = dynamic_cast<const ShapeType *>(this)) {
    appendIdentityPart(result, shape->Decl ? "bound" : "unbound",
                       shape->Decl ? shapeDeclIdentity(shape->Decl)
                                   : shape->Name);
    const auto &arguments =
        shape->GenericArgs.empty() && shape->Decl &&
                shape->Decl->InstantiationTemplate
            ? shape->Decl->InstantiationArgs
            : shape->GenericArgs;
    for (const auto &argument : arguments)
      appendIdentityPart(result, "argument",
                         argument ? argument->canonicalIdentity() : "null");
    appendIdentityPart(result, "variant", shape->VariantSuffix);
  } else if (auto function = dynamic_cast<const FunctionType *>(this)) {
    appendIdentityPart(result, "receiver",
                       std::to_string(static_cast<int>(function->ReceiverMode)));
    appendIdentityPart(result, "variadic",
                       function->IsVariadic ? "1" : "0");
    for (const auto &parameter : function->ParamTypes)
      appendIdentityPart(result, "parameter",
                         parameter ? parameter->canonicalIdentity() : "null");
    appendIdentityPart(result, "result", function->ReturnType
                                               ? function->ReturnType
                                                     ->canonicalIdentity()
                                               : "null");
  } else if (auto function = dynamic_cast<const DynFnType *>(this)) {
    appendIdentityPart(result, "receiver",
                       std::to_string(static_cast<int>(function->ReceiverMode)));
    for (const auto &parameter : function->ParamTypes)
      appendIdentityPart(result, "parameter",
                         parameter ? parameter->canonicalIdentity() : "null");
    appendIdentityPart(result, "result", function->ReturnType
                                               ? function->ReturnType
                                                     ->canonicalIdentity()
                                               : "null");
  }
  result += '}';
  return result;
}

std::string Type::canonicalMangledName() const {
  static constexpr char Hex[] = "0123456789abcdef";
  const std::string identity = canonicalIdentity();
  std::string result;
  result.reserve(1 + identity.size() * 2);
  result += 'T';
  for (unsigned char byte : identity) {
    result += Hex[byte >> 4];
    result += Hex[byte & 0x0f];
  }
  return result;
}

std::string Type::getMangledName() const {
  auto attributes = [&](const Type &type) {
    std::string result;
    if (type.IsCede)
      result += 'C';
    if (type.IsWritable)
      result += 'M';
    if (type.IsNullable)
      result += 'O';
    if (type.IsBlocked)
      result += 'K';
    return result;
  };
  auto framed = [](const std::string &value) {
    return std::to_string(value.size()) + "_" + value;
  };

  if (auto pointer = dynamic_cast<const PointerType *>(this)) {
    char kind = 'R';
    if (typeKind == UniquePtr)
      kind = 'U';
    else if (typeKind == SharedPtr)
      kind = 'S';
    else if (typeKind == Reference)
      kind = 'B';
    const std::string pointee =
        pointer->PointeeType ? pointer->PointeeType->getMangledName()
                             : "unknown";
    return std::string(1, kind) + attributes(*this) + framed(pointee);
  }
  if (auto uninit = dynamic_cast<const UninitType *>(this)) {
    const std::string inner =
        uninit->InnerType ? uninit->InnerType->getMangledName() : "unknown";
    return "I" + attributes(*this) + framed(inner);
  }
  if (auto outcome = dynamic_cast<const MissOutcomeType *>(this)) {
    const std::string payload = outcome->PayloadType
                                    ? outcome->PayloadType->getMangledName()
                                    : "unknown";
    return "O" + attributes(*this) + framed(payload);
  }
  if (auto array = dynamic_cast<const ArrayType *>(this)) {
    const std::string element =
        array->ElementType ? array->ElementType->getMangledName() : "unknown";
    const std::string extent = array->SymbolicSize.empty()
                                   ? std::to_string(array->Size)
                                   : array->SymbolicSize;
    return "A" + attributes(*this) + framed(extent) + framed(element);
  }
  if (auto slice = dynamic_cast<const SliceType *>(this)) {
    const std::string element =
        slice->ElementType ? slice->ElementType->getMangledName() : "unknown";
    return "L" + attributes(*this) + framed(element);
  }
  if (auto function = dynamic_cast<const FunctionType *>(this)) {
    std::string result =
        "F" + attributes(*this) +
        std::to_string(static_cast<int>(function->ReceiverMode)) + "_" +
        (function->IsVariadic ? "1_" : "0_");
    for (const auto &parameter : function->ParamTypes)
      result += framed(parameter ? parameter->getMangledName() : "unknown");
    result += framed(function->ReturnType
                         ? function->ReturnType->getMangledName()
                         : "unknown");
    return result;
  }
  if (auto function = dynamic_cast<const DynFnType *>(this)) {
    std::string result =
        "D" + attributes(*this) +
        std::to_string(static_cast<int>(function->ReceiverMode)) + "_";
    for (const auto &parameter : function->ParamTypes)
      result += framed(parameter ? parameter->getMangledName() : "unknown");
    result += framed(function->ReturnType
                         ? function->ReturnType->getMangledName()
                         : "unknown");
    return result;
  }

  std::string result = toString();
  for (char &character : result) {
    if (character == '^')
      character = 'U';
    else if (character == '*')
      character = 'R';
    else if (character == '~')
      character = 'S';
    else if (character == '&')
      character = 'B';
    else if (character == '?')
      character = 'O';
    else if (character == '#')
      character = 'M';
    else if (character == '!')
      character = 'K';
    else if (!std::isalnum(static_cast<unsigned char>(character)) &&
             character != '_')
      character = '_';
  }
  return result;
}

bool Type::isSend(class Sema *S) const { return false; }
bool Type::isSync(class Sema *S) const { return false; }

ValueOwnership Type::valueOwnership(class Sema *S) const {
  switch (typeKind) {
  case RawPtr:
  case Reference:
  case Slice:
    return ValueOwnership::BorrowedView;
  case SharedPtr:
    return ValueOwnership::SharedHandle;
  case UniquePtr:
    return ValueOwnership::Owned;
  case MissOutcome: {
    const auto *outcome = dynamic_cast<const MissOutcomeType *>(this);
    return outcome && outcome->PayloadType
               ? outcome->PayloadType->valueOwnership(S)
               : ValueOwnership::Trivial;
  }
  case UninitWrapper: {
    const auto *uninit = dynamic_cast<const UninitType *>(this);
    return uninit && uninit->InnerType
               ? uninit->InnerType->valueOwnership(S)
               : ValueOwnership::Trivial;
  }
  case Array: {
    const auto *array = dynamic_cast<const ArrayType *>(this);
    return array && array->ElementType &&
                   array->ElementType->requiresExplicitOwnershipTransfer(S)
               ? ValueOwnership::Owned
               : ValueOwnership::Trivial;
  }
  case Shape: {
    std::string soul = getSoulName();
    if (S)
      soul = S->resolveType(soul);

    // Borrowed core views never own their backing storage.  `string` and
    // `Bytes` have compiler-recognised buffer cleanup that intentionally does
    // not participate in Sema::hasDrop(), so their ownership is represented
    // here with the rest of the type metadata rather than at individual use
    // sites.
    if (soul == "str" || soul == "bytes" || soul == "cstr" ||
        soul == "ViewStrSplitIterator" || soul == "ViewStrLinesIterator")
      return ValueOwnership::BorrowedView;
    if (soul == "SlabID" || soul == "TimerHeap")
      return ValueOwnership::Trivial;
    if (soul == "string" || soul == "Bytes")
      return ValueOwnership::Owned;
    return S && S->hasDrop(soul) ? ValueOwnership::Owned
                                 : ValueOwnership::Trivial;
  }
  default:
    return ValueOwnership::Trivial;
  }
}

// Check compatibility (Permission Flow)
bool Type::isCompatibleWith(const Type &target) const {
  if (typeKind != target.typeKind)
    return false;
  // Target Writable? Source must be Writable.
  // Mutability check removed from base: T -> T# is allowed for values (copy).
  // Strict mutability is enforced in Pointer/Reference types where it matters.
  // May-zero is a raw-pointer-only physical attribute.
  if (IsNullable || target.IsNullable) {
    if (typeKind != RawPtr)
      return false;
    if (!target.IsNullable && IsNullable)
      return false;
  }
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
  } else {
    clone->IsWritable = w;
  }
  clone->IsNullable = original->typeKind == Type::RawPtr ? n : false;
  return clone;
}

// --- Implementations ---

std::shared_ptr<Type> UnitType::withAttributes(bool w, bool n, bool b) const {
  return cloneWithAttrs(this, w, n, b);
}

bool UnitType::isSend(class Sema *S) const { return true; }
bool UnitType::isSync(class Sema *S) const { return true; }

std::shared_ptr<Type> VoidType::withAttributes(bool w, bool n, bool b) const {
  return cloneWithAttrs(this, w, n, b);
}

bool VoidType::isSend(class Sema *S) const { return true; }
bool VoidType::isSync(class Sema *S) const { return true; }

std::shared_ptr<Type> NeverType::withAttributes(bool w, bool n, bool b) const {
  return cloneWithAttrs(this, w, n, b);
}

bool NeverType::isSend(class Sema *S) const { return true; }
bool NeverType::isSync(class Sema *S) const { return true; }

std::string UninitType::toString() const {
  std::string s = "";
  if (IsCede) s += "cede ";
  s += "Uninit<" + InnerType->toString() + ">";
  if (IsWritable) s += "#";
  if (IsBlocked) s += "$";
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

std::string MissOutcomeType::toString() const {
  return (PayloadType ? PayloadType->toString() : "unknown") + "|miss";
}

bool MissOutcomeType::equals(const Type &other) const {
  if (!Type::equals(other))
    return false;
  const auto *outcome = dynamic_cast<const MissOutcomeType *>(&other);
  return outcome && PayloadType && outcome->PayloadType &&
         PayloadType->equals(*outcome->PayloadType);
}

std::shared_ptr<Type>
MissOutcomeType::withAttributes(bool w, bool n, bool b) const {
  return cloneWithAttrs(this, w, n, b);
}

std::shared_ptr<Type> MissOutcomeType::substitute(
    const std::map<std::string, std::shared_ptr<Type>> &substMap) const {
  auto result = std::dynamic_pointer_cast<MissOutcomeType>(
      withAttributes(IsWritable, IsNullable, IsBlocked));
  if (PayloadType)
    result->PayloadType = PayloadType->substitute(substMap);
  return result;
}

ValueOwnership MissOutcomeType::valueOwnership(class Sema *S) const {
  return PayloadType ? PayloadType->valueOwnership(S)
                     : ValueOwnership::Trivial;
}

bool MissOutcomeType::isSend(class Sema *S) const {
  return PayloadType && PayloadType->isSend(S);
}

bool MissOutcomeType::isSync(class Sema *S) const {
  return PayloadType && PayloadType->isSync(S);
}

std::string PrimitiveType::toString() const {
  std::string s = "";
  if (IsCede) s += "cede ";
  s += Name;
  if (IsBlocked)
    s += "$";
  if (IsWritable)
    s += "#";
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
  if (IsWritable)
    s += "#";
  if (IsBlocked)
    s += "$";
  return s;
}

std::string SliceType::toString() const {
  std::string s = "";
  if (IsCede) s += "cede ";
  s += "[";
  s += ElementType->toString();
  s += "]";
  if (IsWritable)
    s += "#";
  if (IsBlocked)
    s += "$";
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
         SymbolicSize == otherArr->SymbolicSize &&
         ElementType->equals(*otherArr->ElementType);
}

bool ArrayType::isCompatibleWith(const Type &target) const {
  if (!Type::isCompatibleWith(target))
    return false;
  const auto *otherArr = dynamic_cast<const ArrayType *>(&target);
  return otherArr && Size == otherArr->Size &&
         SymbolicSize == otherArr->SymbolicSize &&
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
  if (IsWritable)
    s += "#";
  if (IsBlocked)
    s += "$";
  return s;
}

std::string ShapeType::getMangledName() const {
  std::string result;
  bool hasStableBase = false;
  if (Decl && Decl->NominalId) {
    result = Decl->NominalId->mangled();
    hasStableBase = true;
  } else if (Decl && Decl->InstantiationTemplate &&
             !Decl->CodegenName.empty()) {
    result = Decl->CodegenName;
    hasStableBase = true;
  } else {
    result = Type::getMangledName();
  }

  if (hasStableBase) {
    result += "_A";
    if (IsCede)
      result += 'C';
    if (IsWritable)
      result += 'M';
    if (IsBlocked)
      result += 'K';
    if (!VariantSuffix.empty()) {
      static constexpr char Hex[] = "0123456789abcdef";
      result += "_V";
      for (unsigned char byte : VariantSuffix) {
        result += Hex[byte >> 4];
        result += Hex[byte & 0x0f];
      }
    }
  }

  if (!GenericArgs.empty()) {
    result += "_G";
    for (const auto &argument : GenericArgs)
      result += "_" +
                (argument ? argument->getMangledName() : std::string("unknown"));
  }
  return result;
}

bool ShapeType::equals(const Type &other) const {
  if (!Type::equals(other))
    return false;
  const auto *otherSh = dynamic_cast<const ShapeType *>(&other);
  if (!otherSh)
    return false;
  if (static_cast<bool>(Decl) != static_cast<bool>(otherSh->Decl))
    return false;
  const bool sameFamily =
      Decl && otherSh->Decl
          ? shapeDeclIdentity(Decl) == shapeDeclIdentity(otherSh->Decl)
          : Name == otherSh->Name;
  if (!sameFamily || VariantSuffix != otherSh->VariantSuffix)
    return false;
  if (GenericArgs.size() != otherSh->GenericArgs.size())
    return false;
  for (size_t i = 0; i < GenericArgs.size(); ++i) {
    if (!GenericArgs[i] || !otherSh->GenericArgs[i]) {
      if (GenericArgs[i] != otherSh->GenericArgs[i])
        return false;
    } else if (!GenericArgs[i]->equals(*otherSh->GenericArgs[i])) {
      return false;
    }
  }
  return true;
}

bool ShapeType::isCompatibleWith(const Type &target) const {
  const auto *otherSh = dynamic_cast<const ShapeType *>(&target);
  if (otherSh) {
    if (otherSh->Name.rfind("dyn@", 0) == 0)
      return true;
    if (static_cast<bool>(Decl) != static_cast<bool>(otherSh->Decl))
      return false;
    if (Decl && otherSh->Decl
            ? shapeDeclIdentity(Decl) != shapeDeclIdentity(otherSh->Decl)
            : Name != otherSh->Name) {
      return false;
    }
    if (VariantSuffix != otherSh->VariantSuffix ||
        GenericArgs.size() != otherSh->GenericArgs.size())
      return false;
    for (size_t i = 0; i < GenericArgs.size(); ++i) {
      if (!GenericArgs[i] || !otherSh->GenericArgs[i]) {
        if (GenericArgs[i] != otherSh->GenericArgs[i])
          return false;
      } else if (!GenericArgs[i]->isCompatibleWith(
                     *otherSh->GenericArgs[i])) {
        return false;
      }
    }
    return Type::isCompatibleWith(target);
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
  if (Decl) {
    return S->isShapeSend(Decl->Name);
  }
  return S->isShapeSend(S->resolveType(this->toString()));
}
bool ShapeType::isSync(class Sema *S) const {
  if (!S) return false;
  if (Decl) {
    return S->isShapeSync(Decl->Name);
  }
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
      IsVariadic != otherFn->IsVariadic ||
      ParamTypes.size() != otherFn->ParamTypes.size())
    return false;
  if (!ReturnType->equals(*otherFn->ReturnType))
    return false;
  for (size_t i = 0; i < ParamTypes.size(); ++i) {
    if (!ParamTypes[i]->equals(*otherFn->ParamTypes[i]))
      return false;
  }
  return true;
}

bool FunctionType::isCompatibleWith(const Type &target) const {
  if (!Type::isCompatibleWith(target))
    return false;
  const auto *otherFn = dynamic_cast<const FunctionType *>(&target);
  if (!otherFn || ReceiverMode != otherFn->ReceiverMode ||
      IsVariadic != otherFn->IsVariadic ||
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
  if (!ReturnType->equals(*otherFn->ReturnType))
    return false;
  for (size_t i = 0; i < ParamTypes.size(); ++i) {
    if (!ParamTypes[i]->equals(*otherFn->ParamTypes[i]))
      return false;
  }
  return true;
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
    const std::string replacement = substMap.at(SymbolicSize)->toString();
    const bool numeric = !replacement.empty() &&
                         std::all_of(replacement.begin(), replacement.end(),
                                     [](char c) { return c >= '0' && c <= '9'; });
    if (numeric) {
      try {
        at->Size = std::stoull(replacement);
        at->SymbolicSize.clear();
      } catch (...) {
        at->SymbolicSize = replacement;
      }
    } else {
      at->SymbolicSize = replacement;
    }
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
    substituted->IsCede = substituted->IsCede || IsCede;
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

namespace {

bool isPrimitiveTypeName(const std::string &name) {
  return name == "i32" || name == "i64" || name == "u32" ||
         name == "u64" || name == "f32" || name == "f64" ||
         name == "bool" || name == "char" || name == "i8" ||
         name == "u8" || name == "i16" || name == "u16" ||
         name == "usize" || name == "isize" || name == "byte" ||
         name == "null" ||
         name == "none" || name == "Addr" || name == "OAddr";
}

std::shared_ptr<Type> lowerTypeArgument(const TypeArgumentSyntax &argument) {
  if (argument.ArgumentKind == TypeArgumentSyntax::Kind::Type)
    return Type::fromSyntax(argument.Type);

  // Const arguments have a distinct syntax category.  The existing semantic
  // Type representation deliberately carries them as symbolic shape names,
  // which keeps generic substitution and TKI spelling stable without making
  // a constant look like a source type at the parser boundary.
  return std::make_shared<ShapeType>(argument.ConstantText);
}

std::shared_ptr<Type> lowerNamedType(const std::string &name) {
  if (name == "never")
    return std::make_shared<NeverType>();
  if (name == "void")
    return std::make_shared<VoidType>();
  if (name == "unknown")
    return std::make_shared<UnresolvedType>(name);
  if (isPrimitiveTypeName(name))
    return std::make_shared<PrimitiveType>(name);
  return std::make_shared<ShapeType>(name);
}

std::shared_ptr<Type>
replacePointerPointee(const std::shared_ptr<Type> &pointer,
                      const std::shared_ptr<Type> &pointee) {
  auto old = std::dynamic_pointer_cast<PointerType>(pointer);
  if (!old)
    return pointer;

  std::shared_ptr<PointerType> result;
  switch (old->typeKind) {
  case Type::RawPtr:
    result = std::make_shared<RawPointerType>(pointee);
    break;
  case Type::UniquePtr:
    result = std::make_shared<UniquePointerType>(pointee);
    break;
  case Type::SharedPtr:
    result = std::make_shared<SharedPointerType>(pointee);
    break;
  case Type::Reference:
    result = std::make_shared<ReferenceType>(pointee);
    break;
  default:
    return pointer;
  }
  result->IsWritable = old->IsWritable;
  result->IsNullable = old->typeKind == Type::RawPtr && old->IsNullable;
  result->IsBlocked = old->IsBlocked;
  result->IsCede = old->IsCede;
  return result;
}

bool isGeneratedCanonicalTypeLeaf(const std::string &name) {
  // TypeSyntax::named() is also used by the pre-existing generic-template
  // substitution cache.  That cache may hold a whole *generated semantic*
  // spelling (for example "&string"), whereas parser-produced Named nodes
  // contain identifiers only.  Keep this narrow compatibility bridge until
  // template substitution carries semantic Type directly.
  return name.rfind("nul ", 0) == 0 || name.rfind("cede ", 0) == 0 ||
         name.find_first_of("*^~&<[(") != std::string::npos;
}

TypeSyntaxPtr applyTypeSyntaxAttributes(TypeSyntaxPtr syntax, const Type &type,
                                        SourceLocation begin,
                                        SourceLocation end) {
  if (type.IsWritable)
    syntax = TypeSyntax::morphology("#", std::move(syntax), begin, end, true);
  if (type.IsBlocked)
    syntax = TypeSyntax::morphology("$", std::move(syntax), begin, end, true);
  if (type.IsCede)
    syntax = TypeSyntax::morphology("cede ", std::move(syntax), begin, end);
  return syntax;
}

TypeSyntaxPtr withoutOuterTypeAttributes(TypeSyntaxPtr syntax) {
  while (syntax && syntax->NodeKind == TypeSyntax::Kind::Morphology &&
         (syntax->Text == "#" || syntax->Text == "$" ||
          syntax->Text == "cede "))
    syntax = syntax->Subject;
  return syntax;
}

TypeSyntaxPtr typeSyntaxFromType(const Type &type, SourceLocation begin,
                                 SourceLocation end) {
  TypeSyntaxPtr syntax;
  if (dynamic_cast<const UnitType *>(&type)) {
    syntax = TypeSyntax::tuple({}, begin, end);
  } else if (dynamic_cast<const NeverType *>(&type)) {
    syntax = TypeSyntax::named("never", begin, end);
  } else if (dynamic_cast<const VoidType *>(&type)) {
    syntax = TypeSyntax::named("void", begin, end);
  } else if (auto primitive = dynamic_cast<const PrimitiveType *>(&type)) {
    syntax = TypeSyntax::named(primitive->Name, begin, end);
  } else if (auto unresolved = dynamic_cast<const UnresolvedType *>(&type)) {
    syntax = TypeSyntax::invalid(unresolved->Name, begin, end);
  } else if (auto uninit = dynamic_cast<const UninitType *>(&type)) {
    syntax = TypeSyntax::generic(
        TypeSyntax::named("Uninit", begin, end),
        {TypeArgumentSyntax::type(uninit->InnerType
                                      ? uninit->InnerType->toSyntax(begin, end)
                                      : TypeSyntax::named("unknown", begin, end))},
        begin, end);
  } else if (auto outcome = dynamic_cast<const MissOutcomeType *>(&type)) {
    syntax = TypeSyntax::missOutcome(
        outcome->PayloadType
            ? outcome->PayloadType->toSyntax(begin, end)
            : TypeSyntax::named("unknown", begin, end),
        begin, end);
  } else if (auto pointer = dynamic_cast<const PointerType *>(&type)) {
    TypeSyntaxPtr pointee = pointer->PointeeType
                                ? pointer->PointeeType->toSyntax(begin, end)
                                : TypeSyntax::named("unknown", begin, end);
    std::string prefix = "*";
    switch (pointer->typeKind) {
    case Type::UniquePtr:
      prefix = "^";
      break;
    case Type::SharedPtr:
      prefix = "~";
      break;
    case Type::Reference:
      prefix = "&";
      break;
    default:
      break;
    }
    syntax = TypeSyntax::morphology(prefix, std::move(pointee), begin, end);
    // Handle attributes are represented as leading morphology so a
    // source->semantic->source round-trip preserves which layer owns them.
    if (pointer->typeKind == Type::RawPtr && pointer->IsNullable)
      syntax = TypeSyntax::morphology("nul", std::move(syntax), begin, end);
    if (pointer->IsWritable)
      syntax = TypeSyntax::morphology("#", std::move(syntax), begin, end);
    if (pointer->IsBlocked)
      syntax = TypeSyntax::morphology("$", std::move(syntax), begin, end);
    if (pointer->IsCede)
      syntax = TypeSyntax::morphology("cede ", std::move(syntax), begin,
                                      end);
    return syntax;
  } else if (auto array = dynamic_cast<const ArrayType *>(&type)) {
    const std::string extent = array->SymbolicSize.empty()
                                   ? std::to_string(array->Size)
                                   : array->SymbolicSize;
    syntax = TypeSyntax::array(
        array->ElementType ? array->ElementType->toSyntax(begin, end)
                           : TypeSyntax::named("unknown", begin, end),
        TypeArgumentSyntax::constant(extent, begin, end), begin, end);
  } else if (auto slice = dynamic_cast<const SliceType *>(&type)) {
    syntax = TypeSyntax::slice(
        slice->ElementType ? slice->ElementType->toSyntax(begin, end)
                           : TypeSyntax::named("unknown", begin, end),
        begin, end);
  } else if (auto shape = dynamic_cast<const ShapeType *>(&type)) {
    if (shape->SourceSyntax)
      return applyTypeSyntaxAttributes(
          withoutOuterTypeAttributes(shape->SourceSyntax), type, begin, end);
    if (shape->Name.rfind("dyn @", 0) == 0)
      syntax = TypeSyntax::dynTrait(shape->Name.substr(5), begin, end);
    else if (shape->GenericArgs.empty())
      syntax = TypeSyntax::named(shape->Name, begin, end);
    else {
      std::vector<TypeArgumentSyntax> arguments;
      arguments.reserve(shape->GenericArgs.size());
      for (const auto &argument : shape->GenericArgs) {
        TypeSyntaxPtr argumentSyntax = argument
                                           ? argument->toSyntax(begin, end)
                                           : TypeSyntax::named("unknown", begin,
                                                                    end);
        const bool isConst = argumentSyntax->NodeKind == TypeSyntax::Kind::Named &&
                             (!argumentSyntax->Text.empty() &&
                              (std::all_of(argumentSyntax->Text.begin(),
                                           argumentSyntax->Text.end(),
                                           [](unsigned char c) {
                                             return std::isdigit(c);
                                           }) ||
                               argumentSyntax->Text.back() == '_'));
        arguments.push_back(isConst
                                ? TypeArgumentSyntax::constant(
                                      argumentSyntax->Text, begin, end)
                                : TypeArgumentSyntax::type(
                                      std::move(argumentSyntax)));
      }
      syntax = TypeSyntax::generic(TypeSyntax::named(shape->Name, begin, end),
                                   std::move(arguments), begin, end,
                                   shape->VariantSuffix);
      return applyTypeSyntaxAttributes(std::move(syntax), type, begin, end);
    }
  } else if (auto function = dynamic_cast<const FunctionType *>(&type)) {
    std::vector<TypeSyntaxPtr> parameters;
    parameters.reserve(function->ParamTypes.size());
    for (const auto &parameter : function->ParamTypes)
      parameters.push_back(parameter ? parameter->toSyntax(begin, end)
                                     : TypeSyntax::named("unknown", begin, end));
    const std::string kind = function->ReceiverMode == CallableReceiverMode::Mutable
                                 ? "fn#"
                                 : "fn";
    syntax = TypeSyntax::function(
        kind, std::move(parameters),
        function->ReturnType ? function->ReturnType->toSyntax(begin, end)
                             : TypeSyntax::tuple({}, begin, end),
        function->ReturnType && !function->ReturnType->isUnit(),
        function->IsVariadic, begin, end);
    return applyTypeSyntaxAttributes(std::move(syntax), type, begin, end);
  } else if (auto function = dynamic_cast<const DynFnType *>(&type)) {
    std::vector<TypeSyntaxPtr> parameters;
    parameters.reserve(function->ParamTypes.size());
    for (const auto &parameter : function->ParamTypes)
      parameters.push_back(parameter ? parameter->toSyntax(begin, end)
                                     : TypeSyntax::named("unknown", begin, end));
    const std::string kind = function->ReceiverMode == CallableReceiverMode::Mutable
                                 ? "dyn fn#"
                                 : "dyn fn";
    syntax = TypeSyntax::function(
        kind, std::move(parameters),
        function->ReturnType ? function->ReturnType->toSyntax(begin, end)
                             : TypeSyntax::tuple({}, begin, end),
        function->ReturnType && !function->ReturnType->isUnit(),
        false, begin, end);
    return applyTypeSyntaxAttributes(std::move(syntax), type, begin, end);
  } else {
    syntax = TypeSyntax::invalid(type.toString(), begin, end);
  }
  return applyTypeSyntaxAttributes(std::move(syntax), type, begin, end);
}

} // namespace

TypeSyntaxPtr Type::toSyntax(SourceLocation begin, SourceLocation end) const {
  return typeSyntaxFromType(*this, begin, end);
}

std::shared_ptr<Type> Type::fromSyntax(const TypeSyntaxPtr &syntax) {
  if (!syntax)
    return std::make_shared<UnitType>();

  switch (syntax->NodeKind) {
  case TypeSyntax::Kind::Invalid:
    return std::make_shared<UnresolvedType>(syntax->Text);

  case TypeSyntax::Kind::Named:
    if (isGeneratedCanonicalTypeLeaf(syntax->Text))
      return fromString(syntax->Text);
    return lowerNamedType(syntax->Text);

  case TypeSyntax::Kind::GenericApplication: {
    std::vector<std::shared_ptr<Type>> arguments;
    arguments.reserve(syntax->Arguments.size());
    for (const auto &argument : syntax->Arguments)
      arguments.push_back(lowerTypeArgument(argument));

    const std::string name = syntax->Subject
                                 ? syntax->Subject->toCanonicalString()
                                 : std::string();
    if (name == "Uninit" && arguments.size() == 1)
      return std::make_shared<UninitType>(arguments.front());
    return std::make_shared<ShapeType>(name, std::move(arguments),
                                       syntax->PathSuffix);
  }

  case TypeSyntax::Kind::MissOutcome:
    return std::make_shared<MissOutcomeType>(fromSyntax(syntax->Subject));

  case TypeSyntax::Kind::Array: {
    auto element = fromSyntax(syntax->Subject);
    const std::string extent = syntax->ExtentArgument.toCanonicalString();
    uint64_t size = 0;
    bool numeric = !extent.empty();
    for (char c : extent)
      numeric = numeric && c >= '0' && c <= '9';
    if (numeric) {
      try {
        size = std::stoull(extent);
      } catch (...) {
        numeric = false;
      }
    }
    return std::make_shared<ArrayType>(element, size,
                                       numeric ? std::string() : extent);
  }

  case TypeSyntax::Kind::Slice:
    return std::make_shared<SliceType>(fromSyntax(syntax->Subject));

  case TypeSyntax::Kind::Tuple:
    if (syntax->Elements.empty())
      return std::make_shared<UnitType>();
    [[fallthrough]];
  case TypeSyntax::Kind::AnonymousRecord:
    // These source forms do not have separate legacy semantic Type classes.
    // Preserve their canonical semantic name; Sema owns their declaration or
    // projection resolution.  Crucially, no source text is reparsed here.
    {
      auto shape = std::make_shared<ShapeType>(syntax->toCanonicalString());
      shape->SourceSyntax = syntax;
      return shape;
    }

  case TypeSyntax::Kind::AssociatedProjection: {
    auto shape = std::make_shared<ShapeType>(syntax->toCanonicalString());
    shape->SourceSyntax = syntax;
    return shape;
  }

  case TypeSyntax::Kind::DynTrait:
    return std::make_shared<ShapeType>("dyn @" + syntax->Text);

  case TypeSyntax::Kind::Function: {
    std::vector<std::shared_ptr<Type>> parameters;
    parameters.reserve(syntax->Elements.size());
    for (const auto &parameter : syntax->Elements)
      parameters.push_back(fromSyntax(parameter));
    auto result = syntax->HasExplicitResult ? fromSyntax(syntax->Result)
                                            : std::make_shared<UnitType>();
    const bool isDyn = syntax->Text.rfind("dyn fn", 0) == 0;
    const bool mutableReceiver = syntax->Text.find("fn#") != std::string::npos;
    const bool consumingReceiver = syntax->Text.rfind("cede ", 0) == 0;
    if (isDyn) {
      auto function = std::make_shared<DynFnType>(std::move(parameters), result);
      function->ReceiverMode = consumingReceiver
                                   ? CallableReceiverMode::Consuming
                                   : mutableReceiver
                                         ? CallableReceiverMode::Mutable
                                         : CallableReceiverMode::Shared;
      function->IsCede = consumingReceiver;
      return function;
    }
    auto function =
        std::make_shared<FunctionType>(std::move(parameters), result,
                                       syntax->IsVariadic);
    function->ReceiverMode = consumingReceiver
                                 ? CallableReceiverMode::Consuming
                                 : mutableReceiver
                                       ? CallableReceiverMode::Mutable
                                       : CallableReceiverMode::Shared;
    function->IsCede = consumingReceiver;
    return function;
  }

  case TypeSyntax::Kind::Morphology: {
    auto subject = fromSyntax(syntax->Subject);
    if (!subject)
      return std::make_shared<UnresolvedType>(syntax->toCanonicalString());

    // Writable/blocked payload morphology remains structural. Nullable
    // payload morphology is rejected below.
    const bool postfixPointer = syntax->IsPostfix && subject->isPointer();
    const auto attributeTarget =
        postfixPointer ? subject->getPointeeType() : subject;
    auto applyPostfix = [&](bool writable, bool nullable, bool blocked) {
      if (postfixPointer) {
        auto pointee = attributeTarget->withAttributes(writable, nullable,
                                                       blocked);
        return replacePointerPointee(subject, pointee);
      }
      return subject->withAttributes(writable, nullable, blocked);
    };

    if (syntax->Text == "#")
      return applyPostfix(true, attributeTarget->IsNullable,
                          attributeTarget->IsBlocked);
    if (syntax->Text == "?")
      return std::make_shared<UnresolvedType>(syntax->toCanonicalString());
    if (syntax->Text == "$")
      return applyPostfix(attributeTarget->IsWritable,
                          attributeTarget->IsNullable, true);
    if (syntax->Text == "cede ") {
      auto result = subject->withAttributes(subject->IsWritable,
                                            subject->IsNullable,
                                            subject->IsBlocked);
      result->IsCede = true;
      if (auto function = std::dynamic_pointer_cast<FunctionType>(result))
        function->ReceiverMode = CallableReceiverMode::Consuming;
      if (auto function = std::dynamic_pointer_cast<DynFnType>(result))
        function->ReceiverMode = CallableReceiverMode::Consuming;
      return result;
    }
    if (syntax->Text == "nul") {
      // `nul` is a raw-pointer-only may-zero attribute.
      if (subject->isRawPointer()) {
        auto result = subject->withAttributes(subject->IsWritable, true,
                                              subject->IsBlocked);
        result->IsCede = subject->IsCede;
        return result;
      }
      return std::make_shared<UnresolvedType>(syntax->toCanonicalString());
    }

    std::shared_ptr<PointerType> pointer;
    if (syntax->Text == "*")
      pointer = std::make_shared<RawPointerType>(subject);
    else if (syntax->Text == "^")
      pointer = std::make_shared<UniquePointerType>(subject);
    else if (syntax->Text == "~")
      pointer = std::make_shared<SharedPointerType>(subject);
    else if (syntax->Text == "&")
      pointer = std::make_shared<ReferenceType>(subject);
    else
      return std::make_shared<UnresolvedType>(syntax->toCanonicalString());
    return pointer;
  }
  }

  return std::make_shared<UnresolvedType>(syntax->toCanonicalString());
}

std::shared_ptr<Type> Type::fromString(const std::string &rawType) {
  std::string s = trim(rawType);
  if (s.empty())
    return std::make_shared<UnitType>();

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

  if (isNullable)
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

  if (explicitPtrNullable && s[0] != '*')
    return std::make_shared<UnresolvedType>(rawType);

  size_t missSuffix = std::string::npos;
  if (s.size() > 5 && s.compare(s.size() - 5, 5, "|miss") == 0)
    missSuffix = s.size() - 5;
  else if (s.size() > 6 && s.compare(s.size() - 6, 6, "| miss") == 0)
    missSuffix = s.size() - 6;
  if (missSuffix != std::string::npos) {
    auto outcome = std::make_shared<MissOutcomeType>(
        Type::fromString(s.substr(0, missSuffix)));
    outcome->IsWritable = isWritable;
    outcome->IsNullable = false;
    outcome->IsBlocked = isBlocked;
    outcome->IsCede = isCede;
    return outcome;
  }

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
        retType = std::make_shared<UnitType>();
      }
      
      auto fnNode = std::make_shared<DynFnType>(paramTypes, retType);
      fnNode->IsWritable = isWritable;
      fnNode->IsNullable = false;
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
        retType = std::make_shared<UnitType>();
      }
      
      auto fnNode = std::make_shared<FunctionType>(paramTypes, retType);
      fnNode->IsVariadic = isVariadic;
      fnNode->IsWritable = isWritable;
      fnNode->IsNullable = false;
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
    if (isWritable || isBlocked) {
      pointee = pointee->withAttributes(isWritable, false, isBlocked);
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
      arr->IsNullable = false;
      arr->IsBlocked = isBlocked;
      arr->IsCede = isCede;
      return arr;
    } else if (close != std::string::npos && semi == std::string::npos) {
      // Dynamic Array [T]
      auto elem = Type::fromString(trim(s.substr(1, close - 1)));
      auto slice = std::make_shared<SliceType>(elem);
      slice->IsWritable = isWritable;
      slice->IsNullable = false;
      slice->IsBlocked = isBlocked;
      slice->IsCede = isCede;
      return slice;
    }
  }

  if (s == "()")
    return std::make_shared<UnitType>();
  if (s == "never")
    return std::make_shared<NeverType>();
  if (s == "void")
    return std::make_shared<VoidType>();
  if (s == "i32" || s == "i64" || s == "u32" || s == "u64" || s == "f32" ||
      s == "f64" || s == "bool" || s == "char" || s == "i8" ||
      s == "u8" || s == "i16" || s == "u16" || s == "usize" || s == "isize" ||
      s == "byte" || s == "null" || s == "Addr" ||
      s == "OAddr") {
    auto prim = std::make_shared<PrimitiveType>(s);
    prim->IsWritable = isWritable;
    prim->IsNullable = false;
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
    uninit->IsNullable = false;
    uninit->IsBlocked = isBlocked;
    uninit->IsCede = isCede;
    return uninit;
  }

  auto shape = std::make_shared<ShapeType>(baseName, genericArgs, variantSuffix);
  shape->IsWritable = isWritable;
  shape->IsNullable = false;
  shape->IsBlocked = isBlocked;
  shape->IsCede = isCede;
  return shape;
}

std::string HandleGrammarProfile::describeViolation() const {
  switch (violation) {
  case HandleGrammarViolation::None:
    return "Valid";
  case HandleGrammarViolation::ExceededManagedDepth:
    return "ExceededManagedDepth";
  case HandleGrammarViolation::ExceededBorrowDepth:
    return "ExceededBorrowDepth";
  case HandleGrammarViolation::InvalidManagedLayerOrder:
    return "InvalidManagedLayerOrder";
  case HandleGrammarViolation::MixedManagedRaw:
    return "MixedManagedRaw";
  }
  return "Unknown";
}

HandleGrammarProfile Type::classifyHandleGrammar(const std::shared_ptr<Type> &type) {
  HandleGrammarProfile profile;
  if (!type)
    return profile;

  // Unpack continuous pointer layers until hitting a structural boundary
  std::vector<Type::Kind> layers;
  std::shared_ptr<Type> current = type;
  while (current && current->isPointer()) {
    layers.push_back(current->typeKind);
    current = current->getPointeeType();
  }

  if (layers.empty()) {
    return profile;
  }

  // Check for presence of both Raw and Managed in the continuous chain
  bool hasRaw = false;
  bool hasManaged = false;
  for (auto k : layers) {
    if (k == Type::RawPtr)
      hasRaw = true;
    else if (k == Type::UniquePtr || k == Type::SharedPtr || k == Type::Reference)
      hasManaged = true;
  }

  if (hasRaw && hasManaged) {
    profile.crossesManagedRawBoundary = true;
    profile.violation = HandleGrammarViolation::MixedManagedRaw;
    for (auto k : layers) {
      if (k == Type::RawPtr)
        profile.continuousRawDepth++;
      else {
        profile.continuousManagedDepth++;
        if (k == Type::Reference)
          profile.continuousBorrowDepth++;
      }
    }
    return profile;
  }

  if (hasRaw) {
    // Pure raw chain: *T, **T, ***T ...
    profile.continuousRawDepth = layers.size();
    profile.violation = HandleGrammarViolation::None;
    return profile;
  }

  // Pure managed chain: combinations of ^, ~, &
  profile.continuousManagedDepth = layers.size();
  for (size_t i = 0; i < layers.size(); ++i) {
    if (layers[i] == Type::Reference)
      profile.continuousBorrowDepth++;
    else
      break; // Borrow depth is continuous leading references from outer to inner
  }

  if (layers.size() == 1) {
    // Single managed handle: ^T, ~T, &T
    profile.violation = HandleGrammarViolation::None;
    return profile;
  }

  if (layers.size() == 2) {
    // Two managed handles.
    // Allowed: &^T, &~T, &&T (outer layer must be Reference)
    if (layers[0] == Type::Reference) {
      profile.violation = HandleGrammarViolation::None;
      return profile;
    }

    // Outer layer is ^ or ~
    if (layers[1] == Type::Reference) {
      // ^&T or ~&T: Managed second layer is not an outer borrow (invalid order)
      profile.violation = HandleGrammarViolation::InvalidManagedLayerOrder;
      return profile;
    }

    // ^^T, ^~T, ~^T, ~~T: Exceeded managed owning depth
    profile.violation = HandleGrammarViolation::ExceededManagedDepth;
    return profile;
  }

  // layers.size() >= 3
  if (layers.size() == profile.continuousBorrowDepth) {
    // &&&T, &&&&T: Exceeded borrow depth
    profile.violation = HandleGrammarViolation::ExceededBorrowDepth;
  } else {
    // Multi-level managed combinations >= 3 (e.g. &^^T, ^^^T)
    profile.violation = HandleGrammarViolation::ExceededManagedDepth;
  }
  return profile;
}

} // namespace toka
