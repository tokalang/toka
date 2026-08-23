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
#include "toka/TypeSyntax.h"
#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include <map>
#include <cctype>
#include <cstdint>

namespace toka {

class ShapeDecl; // Forward declaration
class Sema;      // Forward declaration

enum class CallableReceiverMode { Shared, Mutable, Consuming };

enum class HandleGrammarViolation {
  None,
  ExceededManagedDepth,      // ^^T, ^~T, ~^T, ~~T
  ExceededBorrowDepth,       // &&&T, &&&&T
  InvalidManagedLayerOrder,  // ^&T, ~&T (Managed second layer is not an outer borrow)
  MixedManagedRaw            // *^T, ^*T, *&T, &*T, *~T, ~*T
};

struct HandleGrammarProfile {
  unsigned continuousManagedDepth = 0;
  unsigned continuousBorrowDepth = 0;
  unsigned continuousRawDepth = 0;
  bool crossesManagedRawBoundary = false;
  HandleGrammarViolation violation = HandleGrammarViolation::None;

  bool isValid() const { return violation == HandleGrammarViolation::None; }
  std::string describeViolation() const;
};

// Classifies the cleanup/transfer responsibility of a resolved value.  This
// is compiler-internal semantic metadata: it is not a source annotation and
// does not alter a type's public spelling.
enum class ValueOwnership { Trivial, BorrowedView, SharedHandle, Owned };

bool isPrimitiveValueConstructorName(const std::string &name);

class Type : public std::enable_shared_from_this<Type> {
public:
  enum Kind {
    Primitive,
    Unit,
    Void,
    Never,
    RawPtr,
    UniquePtr,
    SharedPtr,
    Reference,
    Array,
    Slice, // Reserved for future
    Shape,
    Function,
    DynFn,
    MissOutcome,
    UninitWrapper,
    Unresolved // String-based placeholder
  };

  enum class Morphology {
    None,
    Raw,       // *
    Unique,    // ^
    Shared,    // ~
    Reference, // &
    Any
  };

  Kind typeKind;
  bool IsWritable = false; // '#' (Content mutation)
  bool IsNullable = false; // RawPtr only: `nul *T` may carry address zero.
  bool IsBlocked = false;  // '$' (Inherent restriction)
  bool IsCede = false;     // 'cede' keyword for thread return/ownership transfer

  Type(Kind k) : typeKind(k) {}
  virtual ~Type() = default;

  virtual std::string toString() const = 0;
  virtual bool equals(const Type &other) const;

  // Exact structural identity for semantic caches.  Unlike toString(), this
  // retains resolved nominal declarations through nested aggregate types.
  std::string canonicalIdentity() const;
  std::string canonicalMangledName() const;

  virtual bool isSend(class Sema* S = nullptr) const;
  virtual bool isSync(class Sema* S = nullptr) const;

  // Whether assigning this value from existing storage creates a second
  // cleanup owner.  `S` supplies resolved shape lifecycle metadata; pointer
  // and aggregate cases are derived structurally from this Type.
  virtual ValueOwnership valueOwnership(class Sema *S = nullptr) const;
  bool requiresExplicitOwnershipTransfer(class Sema *S = nullptr) const {
    return valueOwnership(S) == ValueOwnership::Owned;
  }

  // Helpers
  // Checks if 'this' can be assigned to 'target' (handles permission flow)
  // e.g. i32# -> i32 (OK), i32 -> i32# (Error)
  virtual bool isCompatibleWith(const Type &target) const;

  bool isPointer() const {
    return typeKind == RawPtr || typeKind == UniquePtr ||
           typeKind == SharedPtr || typeKind == Reference;
  }
  bool isRawPointer() const { return typeKind == RawPtr; }
  bool isSmartPointer() const {
    return typeKind == UniquePtr || typeKind == SharedPtr;
  }
  bool isReference() const { return typeKind == Reference; }
  bool isUniquePtr() const { return typeKind == UniquePtr; }
  bool isSharedPtr() const { return typeKind == SharedPtr; }
  bool isArray() const { return typeKind == Array; }
  bool isSlice() const { return typeKind == Slice; }
  bool isFunction() const { return typeKind == Function; }
  bool isDynFn() const { return typeKind == DynFn; }
  bool isUnit() const { return typeKind == Unit; }
  bool isVoid() const { return typeKind == Void; }
  bool isNever() const { return typeKind == Never; }
  bool isUnknown() const { return typeKind == Unresolved; }
  bool isUninit() const { return typeKind == UninitWrapper; }
  bool isMissOutcome() const { return typeKind == MissOutcome; }

  bool isFatPointer() const {
    return false;
  }

  virtual bool isBoolean() const { return false; }
  virtual bool isInteger() const { return false; }
  virtual bool isSignedInteger() const { return false; }
  virtual bool isFloatingPoint() const { return false; }

  virtual std::shared_ptr<Type> getPointeeType() const { return nullptr; }
  virtual std::shared_ptr<Type> getArrayElementType() const { return nullptr; }

  // Clone with new attributes
  virtual std::shared_ptr<Type> withAttributes(bool writable, bool nullable,
                                               bool blocked = false) const = 0;

  // Static Factory for String Parsing (The Bridge)
  static std::shared_ptr<Type> fromString(const std::string &typeStr);

  // Lowers already-parsed source type syntax without reparsing its canonical
  // spelling.  Name lookup, aliases, and generic instantiation remain Sema's
  // responsibility; this only constructs the semantic type shape.
  static std::shared_ptr<Type> fromSyntax(const TypeSyntaxPtr &syntax);

  // Reifies an already-lowered semantic type as structural source syntax.
  // This is used by semantic substitution; it never reparses `toString()`.
  TypeSyntaxPtr toSyntax(SourceLocation begin = {},
                         SourceLocation end = {}) const;

  // Helper: Strip morphology characters (*, ^, &, ~, #, ?, !) to get the "Soul"
  // name
  static std::string stripMorphology(const std::string &name);
  static std::string stripPrefixes(const std::string &name);

  // Pure classification of Handle / Pointer continuous chains
  static HandleGrammarProfile classifyHandleGrammar(const std::shared_ptr<Type> &type);

  // Recursive inspection of Handle Grammar issues across all structural boundaries
  static std::optional<HandleGrammarProfile> findHandleGrammarIssueRecursive(const std::shared_ptr<Type> &type);

  bool isShape() const { return typeKind == Shape; }
  virtual std::string getSoulName() const { return toString(); }

  virtual std::string getMangledName() const;

  virtual bool isStringType() const { return false; }
  virtual bool isAddrType() const { return false; }
  virtual bool isOAddrType() const { return false; }
  virtual bool isNullType() const { return false; }

  // [NEW] Substitute generic parameters
  virtual std::shared_ptr<Type> substitute(const std::map<std::string, std::shared_ptr<Type>> &substMap) const {
    return const_cast<Type *>(this)->shared_from_this();
  }


  // [NEW] Get the "Soul" Type (underlying non-pointer type)
  virtual std::shared_ptr<Type> getSoulType() const {
    return const_cast<Type *>(this)->shared_from_this();
  }

  virtual Morphology getMorphology() const { return Morphology::None; }
};

// --- Basic Types ---

// `()` is Toka's ordinary completed-action value.  It deliberately has a
// distinct semantic identity from ABI `void`, even though an ordinary
// Unit-returning function is lowered with LLVM's void result ABI.
class UnitType : public Type {
public:
  UnitType() : Type(Unit) {}
  std::string toString() const override { return "()"; }
  std::shared_ptr<Type> withAttributes(bool w, bool n,
                                       bool b = false) const override;
  bool isSend(class Sema* S = nullptr) const override;
  bool isSync(class Sema* S = nullptr) const override;
};

class VoidType : public Type {
public:
  VoidType() : Type(Void) {}
  std::string toString() const override { return "void"; }
  std::shared_ptr<Type> withAttributes(bool w, bool n,
                                       bool b = false) const override;
  bool isSend(class Sema* S = nullptr) const override;
  bool isSync(class Sema* S = nullptr) const override;
};

// `never` is the bottom type.  It has no source-level values, but is
// assignment-compatible with any target because evaluation cannot continue.
class NeverType : public Type {
public:
  NeverType() : Type(Never) {}
  std::string toString() const override { return "never"; }
  std::shared_ptr<Type> withAttributes(bool w, bool n,
                                       bool b = false) const override;
  bool isSend(class Sema* S = nullptr) const override;
  bool isSync(class Sema* S = nullptr) const override;
};

class UninitType : public Type {
public:
  std::shared_ptr<Type> InnerType;
  UninitType(std::shared_ptr<Type> inner) : Type(UninitWrapper), InnerType(inner) {}
  std::string toString() const override;
  bool equals(const Type &other) const override;
  bool isCompatibleWith(const Type &target) const override;
  std::shared_ptr<Type> withAttributes(bool w, bool n, bool b = false) const override;
  bool isSend(class Sema* S = nullptr) const override { return InnerType ? InnerType->isSend(S) : false; }
  bool isSync(class Sema* S = nullptr) const override { return InnerType ? InnerType->isSync(S) : false; }
  std::shared_ptr<Type> substitute(const std::map<std::string, std::shared_ptr<Type>> &substMap) const override;
  std::shared_ptr<Type> getSoulType() const override { return InnerType->getSoulType(); }
};

class PrimitiveType : public Type {
public:
  std::string Name; // i32, f64, bool, str
  PrimitiveType(const std::string &name) : Type(Primitive), Name(name) {}
  std::string toString() const override;
  bool equals(const Type &other) const override;
  std::shared_ptr<Type> withAttributes(bool w, bool n,
                                       bool b = false) const override;
  bool isCompatibleWith(const Type &target) const override;
  bool isSend(class Sema* S = nullptr) const override;
  bool isSync(class Sema* S = nullptr) const override;

  bool isBoolean() const override { return Name == "bool"; }
  bool isInteger() const override {
    return Name == "i32" || Name == "i64" || Name == "u32" || Name == "u64" ||
           Name == "i8" || Name == "u8" || Name == "i16" || Name == "u16" ||
           Name == "isize" || Name == "usize" || Name == "char" ||
           Name == "byte";
  }
  bool isSignedInteger() const override {
    return Name == "i32" || Name == "i64" || Name == "i8" || Name == "i16" ||
           Name == "isize";
  }
  bool isFloatingPoint() const override {
    return Name == "f32" || Name == "f64";
  }
  bool isAddrType() const override { return Name == "Addr"; }
  bool isOAddrType() const override { return Name == "OAddr"; }
  bool isNullType() const override { return Name == "null"; }
};

// --- Pointer Types ---

class PointerType : public Type {
public:
  std::shared_ptr<Type> substitute(const std::map<std::string, std::shared_ptr<Type>> &substMap) const override;
  std::shared_ptr<Type> PointeeType;

  PointerType(Kind k, std::shared_ptr<Type> pointee)
      : Type(k), PointeeType(pointee) {}

  bool equals(const Type &other) const override;
  bool isCompatibleWith(const Type &target) const override;
  std::shared_ptr<Type> getPointeeType() const override { return PointeeType; }
  std::shared_ptr<Type> getSoulType() const override {
    if (PointeeType)
      return PointeeType->getSoulType();
    return const_cast<PointerType *>(this)->shared_from_this();
  }
};

class RawPointerType : public PointerType {
public:
  RawPointerType(std::shared_ptr<Type> pointee)
      : PointerType(RawPtr, pointee) {}
  std::string toString() const override;
  std::shared_ptr<Type> withAttributes(bool w, bool n,
                                       bool b = false) const override;
  bool isCompatibleWith(const Type &target) const override;
  Morphology getMorphology() const override { return Morphology::Raw; }
  bool isSend(class Sema* S = nullptr) const override;
  bool isSync(class Sema* S = nullptr) const override;
};

class UniquePointerType : public PointerType {
public:
  UniquePointerType(std::shared_ptr<Type> pointee)
      : PointerType(UniquePtr, pointee) {}
  std::string toString() const override;
  std::shared_ptr<Type> withAttributes(bool w, bool n,
                                       bool b = false) const override;
  bool isCompatibleWith(const Type &target) const override;
  Morphology getMorphology() const override { return Morphology::Unique; }
  bool isSend(class Sema* S = nullptr) const override;
  bool isSync(class Sema* S = nullptr) const override;
};

class SharedPointerType : public PointerType {
public:
  SharedPointerType(std::shared_ptr<Type> pointee)
      : PointerType(SharedPtr, pointee) {}
  std::string toString() const override;
  std::shared_ptr<Type> withAttributes(bool w, bool n,
                                       bool b = false) const override;
  bool isCompatibleWith(const Type &target) const override;
  Morphology getMorphology() const override { return Morphology::Shared; }
  bool isSend(class Sema* S = nullptr) const override;
  bool isSync(class Sema* S = nullptr) const override;
};

class ReferenceType : public PointerType {
public:
  ReferenceType(std::shared_ptr<Type> pointee)
      : PointerType(Reference, pointee) {}
  std::string toString() const override;
  std::shared_ptr<Type> withAttributes(bool w, bool n,
                                       bool b = false) const override;
  bool isCompatibleWith(const Type &target) const override;
  bool isSend(class Sema* S = nullptr) const override;
  bool isSync(class Sema* S = nullptr) const override;
};

// --- Composite Types ---

class ArrayType : public Type {
public:
  std::shared_ptr<Type> substitute(const std::map<std::string, std::shared_ptr<Type>> &substMap) const override;
  std::shared_ptr<Type> ElementType;
  uint64_t Size;
  std::string SymbolicSize; // [NEW] For const generics like N_

  ArrayType(std::shared_ptr<Type> elem, uint64_t size, std::string sym = "")
      : Type(Array), ElementType(elem), Size(size),
        SymbolicSize(std::move(sym)) {}
  std::string toString() const override;
  bool equals(const Type &other) const override;
  std::shared_ptr<Type> withAttributes(bool w, bool n,
                                       bool b = false) const override;
  bool isCompatibleWith(const Type &target) const override;
  std::shared_ptr<Type> getArrayElementType() const override {
    return ElementType;
  }
  bool isSend(class Sema* S = nullptr) const override;
  bool isSync(class Sema* S = nullptr) const override;
};

class SliceType : public Type {
public:
  std::shared_ptr<Type> substitute(const std::map<std::string, std::shared_ptr<Type>> &substMap) const override;
  std::shared_ptr<Type> ElementType;

  SliceType(std::shared_ptr<Type> elem)
      : Type(Slice), ElementType(elem) {}
  std::string toString() const override;
  bool equals(const Type &other) const override;
  std::shared_ptr<Type> withAttributes(bool w, bool n,
                                       bool b = false) const override;
  bool isCompatibleWith(const Type &target) const override;
  std::shared_ptr<Type> getArrayElementType() const override {
    return ElementType;
  }
  bool isSend(class Sema* S = nullptr) const override;
  bool isSync(class Sema* S = nullptr) const override;
};

class ShapeType : public Type {
public:
  std::shared_ptr<Type> substitute(const std::map<std::string, std::shared_ptr<Type>> &substMap) const override;
  std::string Name;
  std::vector<std::shared_ptr<Type>> GenericArgs; // [NEW] Generic Arguments
  std::string VariantSuffix; // For ::VariantName
  // Retains structural source information for semantic forms which share the
  // legacy ShapeType carrier (anonymous records and projections).  It is not
  // part of type identity or exported spelling.
  TypeSyntaxPtr SourceSyntax;
  ShapeDecl *Decl = nullptr;
  bool IsSync = false; // [NEW] Track atomic reference status based on definition
  ShapeType(const std::string &name,
            std::vector<std::shared_ptr<Type>> args = {},
            const std::string &variantSuffix = "")
      : Type(Shape), Name(name), GenericArgs(std::move(args)), VariantSuffix(variantSuffix) {}
  void resolve(ShapeDecl *decl);
  bool isResolved() const { return Decl != nullptr; }
  std::string toString() const override;
  std::string getMangledName() const override;
  bool equals(const Type &other) const override;
  std::shared_ptr<Type> withAttributes(bool w, bool n,
                                       bool b = false) const override;
  bool isCompatibleWith(const Type &target) const override;
  std::string getSoulName() const override { return Name; }
  bool isSend(class Sema* S = nullptr) const override;
  bool isSync(class Sema* S = nullptr) const override;
};

class FunctionType : public Type {
public:
  std::shared_ptr<Type> substitute(const std::map<std::string, std::shared_ptr<Type>> &substMap) const override;
  std::vector<std::shared_ptr<Type>> ParamTypes;
  std::shared_ptr<Type> ReturnType;
  bool IsVariadic = false;
  CallableReceiverMode ReceiverMode = CallableReceiverMode::Shared;

  FunctionType(std::vector<std::shared_ptr<Type>> params,
               std::shared_ptr<Type> ret, bool variadic = false)
      : Type(Function), ParamTypes(std::move(params)), ReturnType(ret),
        IsVariadic(variadic) {}

  std::string toString() const override;
  bool equals(const Type &other) const override;
  std::shared_ptr<Type> withAttributes(bool w, bool n,
                                       bool b = false) const override;
  bool isCompatibleWith(const Type &target) const override;
  bool isSend(class Sema* S = nullptr) const override;
  bool isSync(class Sema* S = nullptr) const override;
};

class DynFnType : public Type {
public:
  std::shared_ptr<Type> substitute(const std::map<std::string, std::shared_ptr<Type>> &substMap) const override;
  std::vector<std::shared_ptr<Type>> ParamTypes;
  std::shared_ptr<Type> ReturnType;
  CallableReceiverMode ReceiverMode = CallableReceiverMode::Shared;

  DynFnType(std::vector<std::shared_ptr<Type>> params, std::shared_ptr<Type> ret)
      : Type(DynFn), ParamTypes(std::move(params)), ReturnType(ret) {}

  std::string toString() const override;
  bool equals(const Type &other) const override;
  std::shared_ptr<Type> withAttributes(bool w, bool n, bool b = false) const override;
  bool isCompatibleWith(const Type &target) const override;
  bool isSend(class Sema* S = nullptr) const override;
  bool isSync(class Sema* S = nullptr) const override;
};

class MissOutcomeType : public Type {
public:
  std::shared_ptr<Type> PayloadType;

  explicit MissOutcomeType(std::shared_ptr<Type> payload)
      : Type(MissOutcome), PayloadType(std::move(payload)) {}
  std::string toString() const override;
  bool equals(const Type &other) const override;
  std::shared_ptr<Type> withAttributes(bool w, bool n,
                                       bool b = false) const override;
  std::shared_ptr<Type> substitute(
      const std::map<std::string, std::shared_ptr<Type>> &substMap) const override;
  ValueOwnership valueOwnership(class Sema *S = nullptr) const override;
  bool isSend(class Sema *S = nullptr) const override;
  bool isSync(class Sema *S = nullptr) const override;
};

inline CallableReceiverMode getCallableReceiverMode(const Type &type) {
  if (auto *fn = dynamic_cast<const FunctionType *>(&type))
    return fn->ReceiverMode;
  if (auto *fn = dynamic_cast<const DynFnType *>(&type))
    return fn->ReceiverMode;
  return CallableReceiverMode::Shared;
}

// #include "toka/Type.h" -> Removed self-include

// ... (in UnresolvedType)
class UnresolvedType : public Type {
public:
  std::string Name;
  UnresolvedType(const std::string &name) : Type(Unresolved), Name(name) {}
  std::string toString() const override { return "Unresolved(" + Name + ")"; }
  bool equals(const Type &other) const override {
    return false;
  } // Should resolve first
  std::shared_ptr<Type> withAttributes(bool w, bool n,
                                       bool b = false) const override;
  bool isSend(class Sema* S = nullptr) const override { return false; }
  bool isSync(class Sema* S = nullptr) const override { return false; }
};

} // namespace toka
