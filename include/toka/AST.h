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

#include "toka/AccessPath.h"
#include "toka/BindingPermission.h"
#include "toka/PlaceState.h"
#include "toka/Token.h"
#include "toka/TypeSyntax.h"
#include "toka/Type.h" // Added for ResolvedType
#include "toka/ComptimeValue.h"
#include "toka/ExplicitCedePlan.h"
#include "toka/MemorySummary.h"
#include "toka/NominalShapeId.h"
#include <algorithm>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace toka {

class ASTNode;
class FunctionDecl;

enum class CallableParameterProvenance : uint8_t {
  Indeterminate,
  Concrete,
  GenericOrMorphic,
};

enum class DynFnEnvironmentDisposition : uint8_t {
  None,
  Retain,
  Transfer,
};

// Sema-qualified ownership behavior for a value inserted into aggregate
// storage.  The disposition belongs to the insertion edge, not to the source
// binding: CodeGen must never infer it from a morphic name.
enum class AggregateTransferKind {
  Unqualified,
  CopyValue,
  MoveOwned,
  RetainShared,
  CopyIdentity,
};

// Audit-only RC9 M1 shadow plan for a resolved call argument. Normal Sema,
// PAL, diagnostics, and CodeGen continue to use the RC8 paths until the
// signature-driven call-transfer ADR activation gates pass.
enum class CallTransferRoute {
  Ordinary,
  Static,
  Method,
  Callable,
  IndirectFunction,
  IndirectDynFunction,
  DynamicTraitMethod,
  Extern,
};

enum class CallTransferDisposition {
  Unplanned,
  InitStorage,
  BorrowCapture,
  CopyValue,
  CopyIdentity,
  TransferShared,
  MoveOwned,
  ConsumeTemporary,
  Reject,
};

enum class CallValueCategory {
  Unclassified,
  Place,
  Temporary,
  InitStorage,
  Indeterminate,
};

enum class CallSourceDisposition {
  Unplanned,
  KeepLive,
  InvalidatePlace,
  NoSourcePlace,
  NoStateChange,
};

enum class CallDependencyDisposition {
  Unclassified,
  None,
  Borrowed,
  RawUnsafe,
  Indeterminate,
};

enum class CallPlaceEligibility {
  Unclassified,
  NotApplicable,
  PendingValidation,
  Eligible,
  Reject,
};

enum class CallDropDisposition {
  Unclassified,
  SourceRetainsLiability,
  DestinationAssumesLiability,
  SharedLiabilityIncremented,
  NoLiability,
  NoStateChange,
  PendingValidation,
};

enum class CallExecutionBoundary {
  None,
  StartHandoff,
  ThreadHandoff,
  StartAndThreadHandoff,
};

struct CallTransferPlan {
  unsigned ArgumentIndex = 0;
  unsigned FormalIndex = 0;
  CallTransferRoute Route = CallTransferRoute::Ordinary;
  CallValueCategory ValueCategory = CallValueCategory::Unclassified;
  CallTransferDisposition Transfer = CallTransferDisposition::Unplanned;
  CallSourceDisposition Source = CallSourceDisposition::Unplanned;
  CallDependencyDisposition Dependency =
      CallDependencyDisposition::Unclassified;
  CallPlaceEligibility PlaceEligibility =
      CallPlaceEligibility::Unclassified;
  CallDropDisposition Drop = CallDropDisposition::Unclassified;
  CallExecutionBoundary ExecutionBoundary = CallExecutionBoundary::None;
  bool FormalIsCeded = false;
  bool FormalIsInit = false;
  bool ActualIsInit = false;
  bool ExplicitCede = false;
  bool LegacyCallerRuleApplied = false;
  bool LegacyCedeExempt = false;
  bool LegacyMissingCede = false;
  bool IsAsync = false;
  bool HasCleanupMask = false;
  uint64_t CleanupMask = 0;
  AccessPath SourcePlace;
  AccessPath ReferentPath;
  std::vector<std::string> DependencyPaths;
  std::optional<ExplicitCedePlan> Stage0Plan;
};

inline const char *callTransferRouteName(CallTransferRoute route) {
  switch (route) {
  case CallTransferRoute::Ordinary: return "ordinary";
  case CallTransferRoute::Static: return "static";
  case CallTransferRoute::Method: return "method";
  case CallTransferRoute::Callable: return "callable";
  case CallTransferRoute::IndirectFunction: return "indirect-fn";
  case CallTransferRoute::IndirectDynFunction: return "indirect-dyn-fn";
  case CallTransferRoute::DynamicTraitMethod: return "dynamic-trait-method";
  case CallTransferRoute::Extern: return "extern";
  }
  return "ordinary";
}

inline const char *callValueCategoryName(CallValueCategory category) {
  switch (category) {
  case CallValueCategory::Unclassified: return "Unclassified";
  case CallValueCategory::Place: return "Place";
  case CallValueCategory::Temporary: return "Temporary";
  case CallValueCategory::InitStorage: return "InitStorage";
  case CallValueCategory::Indeterminate: return "Indeterminate";
  }
  return "Unclassified";
}

inline const char *callTransferDispositionName(
    CallTransferDisposition disposition) {
  switch (disposition) {
  case CallTransferDisposition::Unplanned: return "Unplanned";
  case CallTransferDisposition::InitStorage: return "InitStorage";
  case CallTransferDisposition::BorrowCapture: return "BorrowCapture";
  case CallTransferDisposition::CopyValue: return "CopyValue";
  case CallTransferDisposition::CopyIdentity: return "CopyIdentity";
  case CallTransferDisposition::TransferShared: return "TransferShared";
  case CallTransferDisposition::MoveOwned: return "MoveOwned";
  case CallTransferDisposition::ConsumeTemporary: return "ConsumeTemporary";
  case CallTransferDisposition::Reject: return "Reject";
  }
  return "Unplanned";
}

inline const char *callSourceDispositionName(
    CallSourceDisposition disposition) {
  switch (disposition) {
  case CallSourceDisposition::Unplanned: return "Unplanned";
  case CallSourceDisposition::KeepLive: return "KeepLive";
  case CallSourceDisposition::InvalidatePlace: return "InvalidatePlace";
  case CallSourceDisposition::NoSourcePlace: return "NoSourcePlace";
  case CallSourceDisposition::NoStateChange: return "NoStateChange";
  }
  return "Unplanned";
}

inline const char *callDependencyDispositionName(
    CallDependencyDisposition disposition) {
  switch (disposition) {
  case CallDependencyDisposition::Unclassified: return "Unclassified";
  case CallDependencyDisposition::None: return "None";
  case CallDependencyDisposition::Borrowed: return "Borrowed";
  case CallDependencyDisposition::RawUnsafe: return "RawUnsafe";
  case CallDependencyDisposition::Indeterminate: return "Indeterminate";
  }
  return "Unclassified";
}

inline const char *callPlaceEligibilityName(CallPlaceEligibility eligibility) {
  switch (eligibility) {
  case CallPlaceEligibility::Unclassified: return "Unclassified";
  case CallPlaceEligibility::NotApplicable: return "NotApplicable";
  case CallPlaceEligibility::PendingValidation: return "PendingValidation";
  case CallPlaceEligibility::Eligible: return "Eligible";
  case CallPlaceEligibility::Reject: return "Reject";
  }
  return "Unclassified";
}

inline const char *callDropDispositionName(CallDropDisposition disposition) {
  switch (disposition) {
  case CallDropDisposition::Unclassified: return "Unclassified";
  case CallDropDisposition::SourceRetainsLiability:
    return "SourceRetainsLiability";
  case CallDropDisposition::DestinationAssumesLiability:
    return "DestinationAssumesLiability";
  case CallDropDisposition::SharedLiabilityIncremented:
    return "SharedLiabilityIncremented";
  case CallDropDisposition::NoLiability: return "NoLiability";
  case CallDropDisposition::NoStateChange: return "NoStateChange";
  case CallDropDisposition::PendingValidation: return "PendingValidation";
  }
  return "Unclassified";
}

inline const char *callExecutionBoundaryName(CallExecutionBoundary boundary) {
  switch (boundary) {
  case CallExecutionBoundary::None: return "None";
  case CallExecutionBoundary::StartHandoff: return "StartHandoff";
  case CallExecutionBoundary::ThreadHandoff: return "ThreadHandoff";
  case CallExecutionBoundary::StartAndThreadHandoff:
    return "StartAndThreadHandoff";
  }
  return "None";
}

enum class Stage0CodeGenAuthorityKind {
  None,
  CallTransaction,
  NonCallItem,
  NonCallGroup,
};

// Sema-owned authority attached to the exact AST edge consumed by CodeGen.
// CodeGen may validate and execute this carrier, but must never reconstruct
// ownership, Drop, source-place, or obligation facts on its own.
struct Stage0CodeGenAuthority {
  Stage0CodeGenAuthorityKind Kind = Stage0CodeGenAuthorityKind::None;
  bool RequiresAuthority = false;
  bool SemaValidated = false;
  bool Complete = false;
  bool DestinationMatching = false;
  uint64_t SnapshotRevision = 0;
  std::string Route;
  TransferDestination Destination = TransferDestination::Indeterminate;
  std::optional<ExplicitCedePlan> ItemPlan;
  std::optional<ExplicitCedeWholeCallPlan> CallPlan;
  std::vector<ExplicitCedePlan> GroupPlans;
};

enum class MorphologyConstraintKind {
  SoulOnly,
  BorrowExtendable,
  RawExtendable,
};

inline const char *morphologyConstraintName(MorphologyConstraintKind kind) {
  switch (kind) {
  case MorphologyConstraintKind::SoulOnly:
    return "soul_only";
  case MorphologyConstraintKind::BorrowExtendable:
    return "borrow_extendable";
  case MorphologyConstraintKind::RawExtendable:
    return "raw_extendable";
  }
  return "unknown";
}

struct GenericParam {
  std::string Name;
  std::string Type; // Empty if it's a type parameter
  // Kept in lockstep with Type.  The textual field is the canonical printer
  // cache consumed by the pre-existing semantic Type / CodeGen boundary.
  TypeSyntaxPtr TypeSyntax;
  bool IsConst = false;
  std::vector<std::string> TraitBounds;
  std::vector<MorphologyConstraintKind> MorphologyBounds;
  bool IsMorphic = false; // [NEW] True if name starts with '
};

class ASTNode {
public:
  static uint32_t NextNodeSerial;
  static uint32_t CurrentExpansionContext;

  SourceLocation Loc;
  uint32_t NodeSerial;
  uint32_t ExpansionContext;
  bool Stage0CodeGenAuthorityRequired = false;
  std::optional<Stage0CodeGenAuthority> Stage0Authority;

  ASTNode() {
    NodeSerial = NextNodeSerial++;
    ExpansionContext = CurrentExpansionContext;
  }

  virtual ~ASTNode() = default;
  virtual std::string toString() const = 0;

  virtual std::unique_ptr<ASTNode> clone() const {
    return nullptr;
  } // TODO: Make pure virtual after implementing for all

  void setLocation(const Token &tok, const std::string &file = "") {
    Loc = tok.Loc;
  }
};

// Helper for deep copying unique_ptr<T> where T : ASTNode
template <typename T>
std::unique_ptr<T> cloneNode(const std::unique_ptr<T> &node) {
  if (!node)
    return nullptr;
  // clone() returns unique_ptr<ASTNode>, we cast it back to unique_ptr<T>
  // This assumes the clone() implementation returns the correct type.
  return std::unique_ptr<T>(static_cast<T *>(node->clone().release()));
}

// Helper for deep copying vector of unique_ptr<T>
template <typename T>
std::vector<std::unique_ptr<T>>
cloneVec(const std::vector<std::unique_ptr<T>> &vec) {
  std::vector<std::unique_ptr<T>> res;
  res.reserve(vec.size());
  for (const auto &el : vec) {
    res.push_back(cloneNode(el));
  }
  return res;
}

class Expr : public ASTNode {
public:
  std::shared_ptr<Type> ResolvedType;
  bool IsMorphicExempt = false; // [NEW] Track morphic exemption at expression level
  bool HasParens = false; // [NEW] Track explicit parentheses
  bool ExtendLifetime = false; // [NEW] Flag for Temporary Lifetime Extension
};
class Stmt : public ASTNode {};

// --- Expressions ---

class NumberExpr : public Expr {
public:
  uint64_t Value;
  NumberExpr(uint64_t val) : Value(val) {}
  std::string toString() const override {
    return "Number(" + std::to_string(Value) + ")";
  }
  std::unique_ptr<ASTNode> clone() const override {
    auto n = std::make_unique<NumberExpr>(Value);
    n->Loc = Loc;
    n->ResolvedType = ResolvedType;
    return n;
  }
};

class FloatExpr : public Expr {
public:
  double Value;
  FloatExpr(double val) : Value(val) {}
  std::string toString() const override {
    return "Float(" + std::to_string(Value) + ")";
  }
  std::unique_ptr<ASTNode> clone() const override {
    auto n = std::make_unique<FloatExpr>(Value);
    n->Loc = Loc;
    n->ResolvedType = ResolvedType;
    return n;
  }
};

class BoolExpr : public Expr {
public:
  bool Value;
  BoolExpr(bool val) : Value(val) {}
  std::string toString() const override { return Value ? "true" : "false"; }
  std::unique_ptr<ASTNode> clone() const override {
    auto n = std::make_unique<BoolExpr>(Value);
    n->Loc = Loc;
    n->ResolvedType = ResolvedType;
    return n;
  }
};

class NullExpr : public Expr {
public:
  NullExpr() {}
  std::string toString() const override { return "nullptr"; }
  std::unique_ptr<ASTNode> clone() const override {
    auto n = std::make_unique<NullExpr>();
    n->Loc = Loc;
    n->ResolvedType = ResolvedType;
    return n;
  }
};

class NoneExpr : public Expr {
public:
  NoneExpr() {}
  std::string toString() const override { return "none"; }
  std::unique_ptr<ASTNode> clone() const override {
    auto n = std::make_unique<NoneExpr>();
    n->Loc = Loc;
    n->ResolvedType = ResolvedType;
    return n;
  }
};

class UnsetExpr : public Expr {
public:
  UnsetExpr() {}
  std::string toString() const override { return "uninit"; }
  std::unique_ptr<ASTNode> clone() const override {
    auto n = std::make_unique<UnsetExpr>();
    n->Loc = Loc;
    n->ResolvedType = ResolvedType;
    return n;
  }
};

// `todo` is deliberately not a VariableExpr.  Its identity is stable within
// one parse so later tooling can refer to an incomplete occurrence without
// inventing a user-visible binding.
class TodoExpr : public Expr {
public:
  uint64_t TodoId;
  explicit TodoExpr(uint64_t todoId) : TodoId(todoId) {}
  std::string toString() const override {
    return "Todo(" + std::to_string(TodoId) + ")";
  }
  std::unique_ptr<ASTNode> clone() const override {
    auto n = std::make_unique<TodoExpr>(TodoId);
    n->Loc = Loc;
    n->ResolvedType = ResolvedType;
    return n;
  }
};

class SizeOfExpr : public Expr {
public:
  std::string TypeStr;
  TypeSyntaxPtr TypeSyntax;
  std::shared_ptr<Type> OperandType;
  SizeOfExpr(const std::string &ty) : TypeStr(ty) {}
  std::string toString() const override { return "sizeof(" + TypeStr + ")"; }
  std::unique_ptr<ASTNode> clone() const override {
    auto n = std::make_unique<SizeOfExpr>(TypeStr);
    n->TypeSyntax = TypeSyntax;
    n->OperandType = OperandType;
    n->Loc = Loc;
    n->ResolvedType = ResolvedType;
    return n;
  }
};

class VariableExpr : public Expr {
public:
  std::string Name;
  std::string ResolvedName;
  bool IsRawPointer = false;
  bool IsUnique = false;
  bool IsShared = false;
  bool IsValueMutable = false;
  bool IsValueNullable = false;
  bool IsValueBlocked = false; // "$" identifier attribute
  bool IsImplicitDeref = false; // [Fix] Indicates Sema applied an implicit deref
  BindingPermission Permission;
  bool HasConstantValue = false;
  uint64_t ConstantValue = 0;
  ComptimeValue ConstantValObj;

  VariableExpr(const std::string &name) : Name(name) {}
  const std::string &codegenName() const {
    return ResolvedName.empty() ? Name : ResolvedName;
  }
  std::string toString() const override {
    return std::string("Var(") + (IsRawPointer ? "^" : "") + Name +
           (IsValueMutable ? "#" : "") + ")";
  }
  std::unique_ptr<ASTNode> clone() const override {
    auto n = std::make_unique<VariableExpr>(Name);
    n->ResolvedName = ResolvedName;
    n->IsRawPointer = IsRawPointer;
    n->IsUnique = IsUnique;
    n->IsShared = IsShared;
    n->IsValueMutable = IsValueMutable;
    n->IsValueNullable = IsValueNullable;
    n->IsValueBlocked = IsValueBlocked;
    n->IsImplicitDeref = IsImplicitDeref;
    n->Permission = Permission;
    n->HasConstantValue = HasConstantValue;
    n->ConstantValue = ConstantValue;
    n->ConstantValObj = ConstantValObj;
    n->Loc = Loc;
    n->ResolvedType = ResolvedType;
    return n;
  }
};

class StringExpr : public Expr {
public:
  std::string Value;
  StringExpr(const std::string &val) : Value(val) {}
  std::string toString() const override { return "String(\"" + Value + "\")"; }
  std::unique_ptr<ASTNode> clone() const override {
    auto n = std::make_unique<StringExpr>(Value);
    n->Loc = Loc;
    n->ResolvedType = ResolvedType;
    return n;
  }
};

class ViewStringExpr : public Expr {
public:
  std::string Value;
  ViewStringExpr(const std::string &val) : Value(val) {}
  std::string toString() const override { return "ViewString(\"" + Value + "\")"; }
  std::unique_ptr<ASTNode> clone() const override {
    auto n = std::make_unique<ViewStringExpr>(Value);
    n->Loc = Loc;
    n->ResolvedType = ResolvedType;
    return n;
  }
};

class CharLiteralExpr : public Expr {
public:
  char Value;
  CharLiteralExpr(char val) : Value(val) {}
  std::string toString() const override {
    return "Char('" + std::string(1, Value) + "')";
  }
  std::unique_ptr<ASTNode> clone() const override {
    auto n = std::make_unique<CharLiteralExpr>(Value);
    n->Loc = Loc;
    n->ResolvedType = ResolvedType;
    return n;
  }
};

class DereferenceExpr : public Expr {
public:
  std::unique_ptr<Expr> Expression;
  DereferenceExpr(std::unique_ptr<Expr> expr) : Expression(std::move(expr)) {}
  std::string toString() const override {
    return std::string("Dereference(") + Expression->toString() + ")";
  }
  std::unique_ptr<ASTNode> clone() const override {
    auto n = std::make_unique<DereferenceExpr>(cloneNode(Expression));
    n->Loc = Loc;
    n->ResolvedType = ResolvedType;
    return n;
  }
};

class FunctionDecl;

enum class AssignmentSemanticKind {
  Unclassified,
  Payload,
  Handle,
  ResidualCompound,
};

class BinaryExpr : public Expr {
public:
  std::string Op;
  std::string OverloadedMethod; // [NEW] Syntactic sugar method dispatch
  // `init place = value` shares assignment lowering but carries a distinct
  // construction transition contract.
  bool IsInitialization = false;
  // `place is uninit` is a compile-time state observation with a dedicated
  // runtime liveness lowering. It is never a value comparison with `uninit`.
  bool IsInitStatePredicate = false;
  AssignmentSemanticKind AssignmentKind =
      AssignmentSemanticKind::Unclassified;
  std::unique_ptr<Expr> LHS, RHS;
  BinaryExpr(const std::string &op, std::unique_ptr<Expr> lhs,
             std::unique_ptr<Expr> rhs)
      : Op(op), LHS(std::move(lhs)), RHS(std::move(rhs)) {}
  std::string toString() const override {
    return "Binary(" + Op + (OverloadedMethod.empty() ? "" : "[" + OverloadedMethod + "]") + ", " + LHS->toString() + ", " + RHS->toString() +
           ")";
  }
  std::unique_ptr<ASTNode> clone() const override {
    auto n = std::make_unique<BinaryExpr>(Op, cloneNode(LHS), cloneNode(RHS));
    n->OverloadedMethod = OverloadedMethod;
    n->IsInitialization = IsInitialization;
    n->IsInitStatePredicate = IsInitStatePredicate;
    n->AssignmentKind = AssignmentKind;
    n->Loc = Loc;
    n->ResolvedType = ResolvedType;
    return n;
  }
};

class UnaryExpr : public Expr {
public:
  TokenType Op;
  std::unique_ptr<Expr> RHS;
  bool HasNull = false;      // Raw may-zero or removed nullable recovery.
  bool IsRebindable = false; // For ^# or *#
  bool IsValueMutable =
      false; // For identifier# (unlikely in Unary op token but consistent)
  bool IsValueNullable = false; // Removed `T?` parser-recovery bit only.
  bool IsRebindBlocked = false; // For ^$ or *$
  bool IsValueBlocked = false;  // For identifier$
  bool SelectsHandleIdentity = false; // Inner `&` in `&&x`
  BindingPermission Permission;
  // Actually UnaryExpr covers ^, *, ~, etc.

  UnaryExpr(TokenType op, std::unique_ptr<Expr> rhs)
      : Op(op), RHS(std::move(rhs)) {}
  std::string toString() const override {
    return "Unary(" + std::to_string((int)Op) + (HasNull ? "?" : "") +
           (IsRebindable ? "#" : "") + ", " + RHS->toString() + ")";
  }
  std::unique_ptr<ASTNode> clone() const override {
    auto n = std::make_unique<UnaryExpr>(Op, cloneNode(RHS));
    n->HasNull = HasNull;
    n->IsRebindable = IsRebindable;
    n->IsValueMutable = IsValueMutable;
    n->IsValueNullable = IsValueNullable;
    n->IsRebindBlocked = IsRebindBlocked;
    n->IsValueBlocked = IsValueBlocked;
    n->SelectsHandleIdentity = SelectsHandleIdentity;
    n->Permission = Permission;
    n->Loc = Loc;
    n->ResolvedType = ResolvedType;
    return n;
  }
};

class PostfixExpr : public Expr {
public:
  TokenType Op;
  std::unique_ptr<Expr> LHS;
  PostfixExpr(TokenType op, std::unique_ptr<Expr> lhs)
      : Op(op), LHS(std::move(lhs)) {}
  std::string toString() const override {
    return "Postfix(" + std::to_string((int)Op) + ", " + LHS->toString() + ")";
  }
  std::unique_ptr<ASTNode> clone() const override {
    auto n = std::make_unique<PostfixExpr>(Op, cloneNode(LHS));
    n->Loc = Loc;
    n->ResolvedType = ResolvedType;
    return n;
  }
};

class UnwrapPropagationExpr : public Expr {
public:
  std::unique_ptr<Expr> Base;
  std::shared_ptr<toka::Type> SourceErrorType;
  std::shared_ptr<toka::Type> TargetErrorType;
  FunctionDecl *ErrorConversionFn = nullptr;
  UnwrapPropagationExpr(std::unique_ptr<Expr> base)
      : Base(std::move(base)) {}
  std::string toString() const override {
    return "UnwrapProp(" + Base->toString() + ")";
  }
  std::unique_ptr<ASTNode> clone() const override {
    auto n = std::make_unique<UnwrapPropagationExpr>(cloneNode(Base));
    n->Loc = Loc;
    n->ResolvedType = ResolvedType;
    n->SourceErrorType = SourceErrorType;
    n->TargetErrorType = TargetErrorType;
    n->ErrorConversionFn = nullptr;
    return n;
  }
};

class AwaitExpr : public Expr {
public:
  std::unique_ptr<Expr> Expression;
  // `.await?` is the explicit cancellation-capture boundary.  Ordinary
  // `.await` continues to propagate cancellation as a terminal scope exit.
  bool CatchesCancellation = false;
  std::shared_ptr<Type> AwaitedType;
  AwaitExpr(std::unique_ptr<Expr> expr) : Expression(std::move(expr)) {}
  std::string toString() const override {
    return "Await" + std::string(CatchesCancellation ? "?" : "") +
           "(" + Expression->toString() + ")";
  }
  std::unique_ptr<ASTNode> clone() const override {
    auto n = std::make_unique<AwaitExpr>(cloneNode(Expression));
    n->CatchesCancellation = CatchesCancellation;
    n->AwaitedType = AwaitedType;
    n->Loc = Loc;
    n->ResolvedType = ResolvedType;
    return n;
  }
};

class WaitExpr : public Expr {
public:
  std::unique_ptr<Expr> Expression;
  WaitExpr(std::unique_ptr<Expr> expr) : Expression(std::move(expr)) {}
  std::string toString() const override {
    return "Wait(" + Expression->toString() + ")";
  }
  std::unique_ptr<ASTNode> clone() const override {
    auto n = std::make_unique<WaitExpr>(cloneNode(Expression));
    n->Loc = Loc;
    n->ResolvedType = ResolvedType;
    return n;
  }
};

class StartExpr : public Expr {
public:
  std::unique_ptr<Expr> Expression;
  StartExpr(std::unique_ptr<Expr> expr) : Expression(std::move(expr)) {}
  std::string toString() const override {
    return "Start(" + Expression->toString() + ")";
  }
  std::unique_ptr<ASTNode> clone() const override {
    auto n = std::make_unique<StartExpr>(cloneNode(Expression));
    n->Loc = Loc;
    n->ResolvedType = ResolvedType;
    return n;
  }
};


enum class CastKind {
  Implicit,
  Conversion,
  Ascription,
};

class CastExpr : public Expr {
public:
  std::unique_ptr<Expr> Expression;
  std::string TargetType;
  TypeSyntaxPtr TargetTypeSyntax;
  CastKind Kind = CastKind::Implicit;
  CastExpr(std::unique_ptr<Expr> expr, const std::string &type,
           CastKind kind = CastKind::Implicit)
      : Expression(std::move(expr)), TargetType(type), Kind(kind) {}
  std::string toString() const override {
    const char *form = Kind == CastKind::Ascription ? ":" : "as";
    return "Cast(" + Expression->toString() + " " + form + " " +
           TargetType + ")";
  }
  std::unique_ptr<ASTNode> clone() const override {
    auto n = std::make_unique<CastExpr>(cloneNode(Expression), TargetType, Kind);
    n->TargetTypeSyntax = TargetTypeSyntax;
    n->Loc = Loc;
    n->ResolvedType = ResolvedType;
    return n;
  }
};

class AddressOfExpr : public Expr {
public:
  std::unique_ptr<Expr> Expression;
  AddressOfExpr(std::unique_ptr<Expr> expr) : Expression(std::move(expr)) {}
  std::string toString() const override { return "&" + Expression->toString(); }
  std::unique_ptr<ASTNode> clone() const override {
    auto n = std::make_unique<AddressOfExpr>(cloneNode(Expression));
    n->Loc = Loc;
    n->ResolvedType = ResolvedType;
    return n;
  }
};

class MemberExpr : public Expr {
public:
  std::unique_ptr<Expr> Object;
  std::string Member;
  SourceLocation MemberLoc;
  bool IsStatic;
  // Set by Sema only when `receiver.start` is the TaskHandle activation
  // operation.  The parser must keep `start` available as an ordinary field
  // name for every other shape.
  bool IsTaskStart = false;
  int Index = -1;
  MemberExpr(std::unique_ptr<Expr> obj, const std::string &member,
             bool isStatic = false)
      : Object(std::move(obj)), Member(member),
        IsStatic(isStatic) {}
  std::string toString() const override {
    return Object->toString() + "." + Member;
  }
  std::unique_ptr<ASTNode> clone() const override {
    auto n = std::make_unique<MemberExpr>(cloneNode(Object), Member,
                                          IsStatic);
    n->IsTaskStart = IsTaskStart;
    n->Index = Index;
    n->MemberLoc = MemberLoc;
    n->Loc = Loc;
    n->ResolvedType = ResolvedType;
    return n;
  }
};

class ArrayIndexExpr : public Expr {
public:
  std::unique_ptr<Expr> Array;
  std::vector<std::unique_ptr<Expr>> Indices;
  SourceLocation RBracketLoc;

  ArrayIndexExpr(std::unique_ptr<Expr> arr,
                 std::vector<std::unique_ptr<Expr>> idxs)
      : Array(std::move(arr)), Indices(std::move(idxs)) {}
  std::string toString() const override {
    std::string s = Array->toString() + "[";
    for (size_t i = 0; i < Indices.size(); ++i) {
      if (i > 0)
        s += ", ";
      s += Indices[i]->toString();
    }
    s += "]";
    return s;
  }
  std::unique_ptr<ASTNode> clone() const override {
    auto n =
        std::make_unique<ArrayIndexExpr>(cloneNode(Array), cloneVec(Indices));
    n->RBracketLoc = RBracketLoc;
    n->Loc = Loc;
    n->ResolvedType = ResolvedType;
    return n;
  }
};

class ArrayExpr : public Expr {
public:
  std::vector<std::unique_ptr<Expr>> Elements;
  ArrayExpr(std::vector<std::unique_ptr<Expr>> elems)
      : Elements(std::move(elems)) {}
  std::string toString() const override {
    std::string s = "[";
    for (size_t i = 0; i < Elements.size(); ++i) {
      if (i > 0)
        s += ", ";
      s += Elements[i]->toString();
    }
    s += "]";
    return s;
  }
  std::unique_ptr<ASTNode> clone() const override {
    auto n = std::make_unique<ArrayExpr>(cloneVec(Elements));
    n->Loc = Loc;
    n->ResolvedType = ResolvedType;
    return n;
  }
};

class RepeatedArrayExpr : public Expr {
public:
  std::unique_ptr<Expr> Value;
  std::unique_ptr<Expr> Count;
  RepeatedArrayExpr(std::unique_ptr<Expr> val, std::unique_ptr<Expr> count)
      : Value(std::move(val)), Count(std::move(count)) {}
  std::string toString() const override { return "RepeatedArray"; }
  std::unique_ptr<ASTNode> clone() const override {
    auto n =
        std::make_unique<RepeatedArrayExpr>(cloneNode(Value), cloneNode(Count));
    n->Loc = Loc;
    n->ResolvedType = ResolvedType;
    return n;
  }
};

class UnsafeExpr : public Expr {
public:
  std::unique_ptr<Expr> Expression;
  UnsafeExpr(std::unique_ptr<Expr> expr) : Expression(std::move(expr)) {}
  std::string toString() const override {
    return "Unsafe(" + Expression->toString() + ")";
  }
  std::unique_ptr<ASTNode> clone() const override {
    auto n = std::make_unique<UnsafeExpr>(cloneNode(Expression));
    n->Loc = Loc;
    n->ResolvedType = ResolvedType;
    return n;
  }
};

class AllocExpr : public Expr {
public:
  std::string TypeName;
  TypeSyntaxPtr TypeSyntax;
  std::unique_ptr<Expr> Initializer;
  bool InitializerIsArgumentList = false;
  bool IsArray = false;
  std::unique_ptr<Expr> ArraySize;

  AllocExpr(const std::string &type, std::unique_ptr<Expr> init = nullptr,
            bool isArray = false, std::unique_ptr<Expr> size = nullptr)
      : TypeName(type), Initializer(std::move(init)), IsArray(isArray),
        ArraySize(std::move(size)) {}

  std::string toString() const override {
    return std::string("Alloc(") + (IsArray ? "[]" : "") + TypeName + ")";
  }
  std::unique_ptr<ASTNode> clone() const override {
    auto n = std::make_unique<AllocExpr>(TypeName, cloneNode(Initializer),
                                         IsArray, cloneNode(ArraySize));
    n->TypeSyntax = TypeSyntax;
    n->InitializerIsArgumentList = InitializerIsArgumentList;
    n->Loc = Loc;
    n->ResolvedType = ResolvedType;
    return n;
  }
};


class InitStructExpr : public Expr {
public:
  std::string ShapeName;
  // Source spelling retained across alias resolution and generic elaboration.
  // Semantic consumers must not infer direct-nominal provenance from the
  // rewritten ShapeName alone.
  std::string OriginalShapeName;
  std::vector<std::pair<std::string, std::unique_ptr<Expr>>> Members;
  std::vector<AggregateTransferKind> MemberTransfers;
  std::vector<std::string> CededBases;
  InitStructExpr(
      const std::string &name,
      std::vector<std::pair<std::string, std::unique_ptr<Expr>>> members)
      : ShapeName(name), OriginalShapeName(name), Members(std::move(members)) {}

  std::string toString() const override { return "Init(" + ShapeName + ")"; }
  std::unique_ptr<ASTNode> clone() const override {
    std::vector<std::pair<std::string, std::unique_ptr<Expr>>> members;
    for (const auto &p : Members) {
      members.emplace_back(p.first, cloneNode(p.second));
    }
    auto n = std::make_unique<InitStructExpr>(ShapeName, std::move(members));
    n->OriginalShapeName = OriginalShapeName;
    n->Loc = Loc;
    n->ResolvedType = ResolvedType;
    n->CededBases = CededBases;
    n->MemberTransfers = MemberTransfers;
    return n;
  }
};

class AnonymousRecordExpr : public Expr {
public:
  std::vector<std::pair<std::string, std::unique_ptr<Expr>>> Fields;
  std::vector<AggregateTransferKind> FieldTransfers;
  std::string AssignedTypeName; // Filled by Sema, used by CodeGen

  AnonymousRecordExpr(
      std::vector<std::pair<std::string, std::unique_ptr<Expr>>> fields)
      : Fields(std::move(fields)) {}

  std::string toString() const override {
    std::string s = "AnonRecord(";
    if (!AssignedTypeName.empty())
      s += "[" + AssignedTypeName + "] ";
    for (size_t i = 0; i < Fields.size(); ++i) {
      if (i > 0)
        s += ", ";
      s += Fields[i].first + "=" + Fields[i].second->toString();
    }
    s += ")";
    return s;
  }
  std::unique_ptr<ASTNode> clone() const override {
    std::vector<std::pair<std::string, std::unique_ptr<Expr>>> fields;
    for (const auto &p : Fields) {
      fields.emplace_back(p.first, cloneNode(p.second));
    }
    auto n = std::make_unique<AnonymousRecordExpr>(std::move(fields));
    n->AssignedTypeName = AssignedTypeName;
    n->FieldTransfers = FieldTransfers;
    n->Loc = Loc;
    n->ResolvedType = ResolvedType;
    return n;
  }
};

class ExternDecl;
class ShapeDecl;

class CallExpr : public Expr {
public:
  std::string Callee;
  std::string OriginalCallee;
  std::vector<std::unique_ptr<Expr>> Args;
  std::vector<AggregateTransferKind> ArgumentTransfers;
  // Shadow plans retain original user/formal indices and are not parallel to
  // Args; callable lowering may insert a synthetic receiver into Args.
  std::vector<CallTransferPlan> ShadowArgumentTransfers;
  // Parallel to Args: an init argument denotes storage, never an rvalue.
  std::vector<bool> IsInitArgument;
  std::vector<std::string> GenericArgs; // [NEW]
  std::vector<TypeArgumentSyntax> GenericArgSyntax;

  // Semantic Resolution Cache
  FunctionDecl *ResolvedFn = nullptr;
  ExternDecl *ResolvedExtern = nullptr;
  ShapeDecl *ResolvedShape = nullptr;
  int MatchedMemberIdx = -1; // For enum variant selection
  bool IsIsomorphicCopy = false; // [NEW] Copy/Move constructor intercept
  CallableReceiverMode CallableReceiver = CallableReceiverMode::Shared;
  // An outcome-governed init call cannot be used until its direct match has
  // consumed the returned discriminator.
  bool RequiresOutcomeMatch = false;
  bool OutcomeMatchConsumed = false;

  CallExpr(const std::string &callee, std::vector<std::unique_ptr<Expr>> args,
           std::vector<std::string> genericArgs = {},
           std::vector<bool> initArguments = {})
      : Callee(callee), OriginalCallee(callee), Args(std::move(args)),
        IsInitArgument(std::move(initArguments)),
        GenericArgs(std::move(genericArgs)) {}

  bool isInitArgument(size_t index) const {
    return index < IsInitArgument.size() && IsInitArgument[index];
  }

  std::string toString() const override {
    std::string s = "Call(" + Callee;
    if (!GenericArgs.empty()) {
      s += "<";
      for (size_t i = 0; i < GenericArgs.size(); ++i) {
        if (i > 0)
          s += ", ";
        s += GenericArgs[i];
      }
      s += ">";
    }
    s += ")";
    return s;
  }
  std::unique_ptr<ASTNode> clone() const override {
    auto n = std::make_unique<CallExpr>(Callee, cloneVec(Args), GenericArgs,
                                        IsInitArgument);
    n->GenericArgSyntax = GenericArgSyntax;
    n->OriginalCallee = OriginalCallee;
    n->Loc = Loc;
    n->ResolvedType = ResolvedType;
    n->ResolvedFn = nullptr; // Reset sema cache
    n->IsIsomorphicCopy = IsIsomorphicCopy;
    n->CallableReceiver = CallableReceiver;
    n->ArgumentTransfers = ArgumentTransfers;
    return n;
  }
};

class MethodCallExpr : public Expr {
public:
  std::unique_ptr<Expr> Object;
  std::string Method;
  std::vector<std::unique_ptr<Expr>> Args;
  // Audit-only resolved plans are intentionally not cloned with a reset
  // ResolvedFn.
  std::vector<CallTransferPlan> ShadowArgumentTransfers;
  bool IsCompilerInternal = false; // Compiler-synthesized calls may bypass visibility.
  bool ObjectIsPrechecked = false; // Synthesized wrapper reuses receiver Sema
  bool IsIntrinsicCopyDup = false; // @Copy supplies the intrinsic @Dup operation.
  FunctionDecl *ResolvedFn = nullptr;

  MethodCallExpr(std::unique_ptr<Expr> obj, const std::string &method,
                 std::vector<std::unique_ptr<Expr>> args)
      : Object(std::move(obj)), Method(method), Args(std::move(args)) {}

  std::string toString() const override { return "MethodCall(" + Method + ")"; }
  std::unique_ptr<ASTNode> clone() const override {
    auto n = std::make_unique<MethodCallExpr>(cloneNode(Object), Method,
                                              cloneVec(Args));
    n->Loc = Loc;
    n->ResolvedType = ResolvedType;
    n->IsCompilerInternal = IsCompilerInternal;
    n->ObjectIsPrechecked = ObjectIsPrechecked;
    n->IsIntrinsicCopyDup = IsIntrinsicCopyDup;
    n->ResolvedFn = nullptr;
    return n;
  }
};

class MagicExpr : public Expr {
public:
  TokenType Kind;
  MagicExpr(TokenType kind) : Kind(kind) {}

  std::string toString() const override {
    switch (Kind) {
    case TokenType::KwFile:
      return "__FILE__";
    case TokenType::KwLine:
      return "__LINE__";
    case TokenType::KwLoc:
      return "__LOC__";
    default:
      return "MagicExpr";
    }
  }

  std::unique_ptr<ASTNode> clone() const override {
    auto n = std::make_unique<MagicExpr>(Kind);
    n->Loc = Loc;
    n->ResolvedType = ResolvedType;
    return n;
  }
};

class NewExpr : public Expr {
public:
  std::string Type;
  TypeSyntaxPtr TypeSyntax;
  std::unique_ptr<Expr> Initializer;
  std::unique_ptr<Expr> ArraySize; // [NEW] Support for new [N]T syntax
  NewExpr(const std::string &type, std::unique_ptr<Expr> init, std::unique_ptr<Expr> arraySize = nullptr)
      : Type(type), Initializer(std::move(init)), ArraySize(std::move(arraySize)) {}
  std::string toString() const override {
    std::string s = "New(" + Type;
    if (ArraySize) s += "[" + ArraySize->toString() + "]";
    s += ", " + (Initializer ? Initializer->toString() : "") + ")";
    return s;
  }
  std::unique_ptr<ASTNode> clone() const override {
    auto n = std::make_unique<NewExpr>(Type, cloneNode(Initializer), cloneNode(ArraySize));
    n->TypeSyntax = TypeSyntax;
    n->Loc = Loc;
    n->ResolvedType = ResolvedType;
    return n;
  }
};

class ArrayInitExpr : public Expr {
public:
  std::string Type;
  TypeSyntaxPtr TypeSyntax;
  std::unique_ptr<Expr> Initializer;
  std::unique_ptr<Expr> ArraySize;
  ArrayInitExpr(const std::string &type, std::unique_ptr<Expr> init, std::unique_ptr<Expr> arraySize)
      : Type(type), Initializer(std::move(init)), ArraySize(std::move(arraySize)) {}
  std::string toString() const override {
    std::string s = "ArrayInit([" + (ArraySize ? ArraySize->toString() : "??") + "]" + Type;
    s += ", " + (Initializer ? Initializer->toString() : "") + ")";
    return s;
  }
  std::unique_ptr<ASTNode> clone() const override {
    auto n = std::make_unique<ArrayInitExpr>(Type, cloneNode(Initializer), cloneNode(ArraySize));
    n->TypeSyntax = TypeSyntax;
    n->Loc = Loc;
    n->ResolvedType = ResolvedType;
    return n;
  }
};



class PassExpr : public Expr {
public:
  std::unique_ptr<Expr> Value;
  PassExpr(std::unique_ptr<Expr> val) : Value(std::move(val)) {}
  std::string toString() const override {
    return "Pass(" + (Value ? Value->toString() : "none") + ")";
  }
  std::unique_ptr<ASTNode> clone() const override {
    auto n = std::make_unique<PassExpr>(cloneNode(Value));
    n->Loc = Loc;
    n->ResolvedType = ResolvedType;
    return n;
  }
};

class CedeExpr : public Expr {
public:
  std::unique_ptr<Expr> Value;
  // True only when Sema elaborates a bare argument after a resolved `cede`
  // formal selects an ownership transfer.  The lowering is identical to an
  // explicit cede expression, while tooling can still recover caller spelling.
  bool IsImplicitCallTransfer = false;
  bool IsFaultInjectedMissingCallTransfer = false;
  CedeExpr(std::unique_ptr<Expr> val) : Value(std::move(val)) {}
  std::string toString() const override {
    return "Cede(" + (Value ? Value->toString() : "none") + ")";
  }
  std::unique_ptr<ASTNode> clone() const override {
    auto n = std::make_unique<CedeExpr>(cloneNode(Value));
    n->IsImplicitCallTransfer = IsImplicitCallTransfer;
    n->IsFaultInjectedMissingCallTransfer =
        IsFaultInjectedMissingCallTransfer;
    n->Loc = Loc;
    n->ResolvedType = ResolvedType;
    return n;
  }
};

class ElisionExpr : public Expr {
public:
  std::unique_ptr<Expr> Target;
  ElisionExpr(std::unique_ptr<Expr> target = nullptr) : Target(std::move(target)) {}
  std::string toString() const override {
    return (Target ? Target->toString() : "") + "..";
  }
  std::unique_ptr<ASTNode> clone() const override {
    auto n = std::make_unique<ElisionExpr>(cloneNode(Target));
    n->Loc = Loc;
    n->ResolvedType = ResolvedType;
    return n;
  }
};

class SpreadExpr : public Expr {
public:
  std::unique_ptr<Expr> Base;
  SpreadExpr(std::unique_ptr<Expr> base) : Base(std::move(base)) {}

  std::string toString() const override {
    return Base->toString() + ".*";
  }
  std::unique_ptr<ASTNode> clone() const override {
    auto n = std::make_unique<SpreadExpr>(cloneNode(Base));
    n->Loc = Loc;
    n->ResolvedType = ResolvedType;
    n->IsMorphicExempt = IsMorphicExempt;
    n->HasParens = HasParens;
    n->ExtendLifetime = ExtendLifetime;
    return n;
  }
};

class MatchArm {
public:
  struct Pattern : public ASTNode {
    enum Kind { Literal, Variable, Decons, Wildcard, Or, Elision, Range };
    enum class BindingOrigin { Fresh, Existing };
    Kind PatternKind;
    std::string Name;        // For Variable/Decons (e.g., "Maybe::One")
    uint64_t LiteralVal = 0; // For Literal
    // `auto` is source-level provenance: it either marks this pattern as the
    // whole-pattern shorthand or marks this variable leaf as fresh.
    bool HasAutoBinding = false;
    BindingOrigin Binding = BindingOrigin::Existing;
    std::shared_ptr<Type> MatchedValueType;
    std::shared_ptr<Type> ExistingBindingType;
    std::string EqualityMethod;
    bool IsReference = false;
    bool IsValueMutable = false;
    bool IsValueBlocked = false;
    bool IsInclusive = false; // For Range (true for ..=, false for ..<)
    BindingPermission Permission;
    // Sema's bounded partial-move eligibility for a fresh pattern binding.
    // Like VariableDecl::PartialMove, this is elaborated body data only.
    PartialMovePlan PartialMove;
    std::vector<std::unique_ptr<Pattern>> SubPatterns; // For Decons, Or, and Range (SubPatterns[0] = Start, SubPatterns[1] = End)
    std::vector<std::string> SubPatternNames;          // [NEW] For named deconstruction/matching (parallel to SubPatterns)

    Pattern(Kind k) : PatternKind(k) {}
    std::string toString() const override {
      const std::string autoPrefix = HasAutoBinding ? "auto " : "";
      switch (PatternKind) {
      case Literal:
        return autoPrefix + std::to_string(LiteralVal);
      case Variable:
        return autoPrefix + (IsReference ? "&" : "") + Name +
               (IsValueMutable ? "#" : "");
      case Decons: {
        std::string s = autoPrefix + Name + "(";
        for (size_t i = 0; i < SubPatterns.size(); ++i) {
          if (i > 0)
            s += ", ";
          if (SubPatternNames.size() > i && !SubPatternNames[i].empty()) {
            s += SubPatternNames[i] + " = ";
          }
          s += SubPatterns[i]->toString();
        }
        s += ")";
        return s;
      }
      case Wildcard:
        return autoPrefix + "_";
      case Elision:
        return autoPrefix + "..";
      case Range: {
        if (SubPatterns.size() == 2) {
          return autoPrefix + SubPatterns[0]->toString() +
                 (IsInclusive ? " ..= " : " ..< ") +
                 SubPatterns[1]->toString();
        }
        return "RangePattern(invalid)";
      }
      case Or: {
        std::string s = autoPrefix;
        for (size_t i = 0; i < SubPatterns.size(); ++i) {
          if (i > 0)
            s += " | ";
          s += SubPatterns[i]->toString();
        }
        return s;
      }
      }
      return "";
    }

    std::unique_ptr<ASTNode> clone() const override {
      return clonePattern();
    }

    std::unique_ptr<Pattern> clonePattern() const {
      auto n = std::make_unique<Pattern>(PatternKind);
      n->Name = Name;
      n->LiteralVal = LiteralVal;
      n->HasAutoBinding = HasAutoBinding;
      n->Binding = Binding;
      n->MatchedValueType = MatchedValueType;
      n->ExistingBindingType = ExistingBindingType;
      n->EqualityMethod = EqualityMethod;
      n->IsReference = IsReference;
      n->IsValueMutable = IsValueMutable;
      n->IsValueBlocked = IsValueBlocked;
      n->IsInclusive = IsInclusive;
      n->Permission = Permission;
      n->PartialMove = PartialMove;
      for (auto& sp : SubPatterns) {
          n->SubPatterns.push_back(sp->clonePattern());
      }
      n->SubPatternNames = SubPatternNames;
      n->Loc = Loc;
      return n;
    }
  };

  std::unique_ptr<Pattern> Pat;
  std::unique_ptr<Expr> Guard;
  std::unique_ptr<Stmt> Body;

  MatchArm(std::unique_ptr<Pattern> p, std::unique_ptr<Expr> g,
           std::unique_ptr<Stmt> b)
      : Pat(std::move(p)), Guard(std::move(g)), Body(std::move(b)) {}

  std::unique_ptr<MatchArm> clone() const {
    // Pattern is NOT ASTNode compatible with cloneNode?
    // Wait, Pattern inherits ASTNode (Line 551). So cloneNode<Pattern> works.
    auto n = std::make_unique<MatchArm>(cloneNode(Pat), cloneNode(Guard),
                                        cloneNode(Body));
    return n;
  }
};

class ComptimeFieldExpr : public Expr {
public:
  std::string FieldName;
  std::string FieldTypeName;
  int FieldOffset;
  int FieldSize;

  ComptimeFieldExpr(std::string name, std::string typeName, int off, int sz)
      : FieldName(name), FieldTypeName(typeName), FieldOffset(off),
        FieldSize(sz) {}

  std::string toString() const override { return "CmpField:" + FieldName; }
  std::unique_ptr<ASTNode> clone() const override {
    auto n = std::make_unique<ComptimeFieldExpr>(FieldName, FieldTypeName,
                                                 FieldOffset, FieldSize);
    n->Loc = Loc;
    n->ResolvedType = ResolvedType;
    return n;
  }
};

class ComptimeReflectExpr : public Expr {
public:
  std::string ReflectedTypeStr;
  TypeSyntaxPtr TypeSyntax;
  std::shared_ptr<Type> ReflectedType;
  
  ComptimeReflectExpr(std::string ty) : ReflectedTypeStr(ty) {}
  
  std::string toString() const override { return "CmpReflect:" + ReflectedTypeStr; }
  std::unique_ptr<ASTNode> clone() const override {
    auto n = std::make_unique<ComptimeReflectExpr>(ReflectedTypeStr);
    n->TypeSyntax = TypeSyntax;
    n->ReflectedType = ReflectedType;
    n->Loc = Loc;
    n->ResolvedType = ResolvedType;
    return n;
  }
};

class MatchExpr : public Expr {
public:
  std::unique_ptr<Expr> Target;
  std::vector<std::unique_ptr<MatchArm>> Arms;
  bool TransfersPayloadOwnership = false;

  MatchExpr(std::unique_ptr<Expr> target,
            std::vector<std::unique_ptr<MatchArm>> arms)
      : Target(std::move(target)), Arms(std::move(arms)) {}

  std::string toString() const override { return "Match(...)"; }
  std::unique_ptr<ASTNode> clone() const override {
    auto n = std::make_unique<MatchExpr>(cloneNode(Target), cloneVec(Arms));
    n->TransfersPayloadOwnership = TransfersPayloadOwnership;
    n->Loc = Loc;
    n->ResolvedType = ResolvedType;
    return n;
  }
};

class BreakExpr : public Expr {
public:
  std::string TargetLabel;
  std::unique_ptr<Expr> Value;
  BreakExpr(std::string label, std::unique_ptr<Expr> val)
      : TargetLabel(std::move(label)), Value(std::move(val)) {}
  std::string toString() const override { return "Break"; }
  std::unique_ptr<ASTNode> clone() const override {
    auto n = std::make_unique<BreakExpr>(TargetLabel, cloneNode(Value));
    n->Loc = Loc;
    n->ResolvedType = ResolvedType;
    return n;
  }
};

class ContinueExpr : public Expr {
public:
  std::string TargetLabel;
  ContinueExpr(std::string label) : TargetLabel(std::move(label)) {}
  std::string toString() const override { return "Continue"; }
  std::unique_ptr<ASTNode> clone() const override {
    auto n = std::make_unique<ContinueExpr>(TargetLabel);
    n->Loc = Loc;
    n->ResolvedType = ResolvedType;
    return n;
  }
};

// --- Statements ---

class BlockStmt : public Stmt {
public:
  std::vector<std::unique_ptr<Stmt>> Statements;
  BlockStmt() = default;
  BlockStmt(std::vector<std::unique_ptr<Stmt>> stmts)
      : Statements(std::move(stmts)) {}
  std::string toString() const override { return "Block"; }
  std::unique_ptr<ASTNode> clone() const override {
    auto n = std::make_unique<BlockStmt>(cloneVec(Statements));
    n->Loc = Loc;
    return n;
  }
};

// `init place { ... }` is a lexical proof boundary.  The body does not gain a
// second write operation; it must discharge the place's existing first-
// construction authority before normal fallthrough.
class InitBlockStmt : public Stmt {
public:
  std::string PlaceName;
  SourceLocation PlaceLoc;
  bool IsValueMutable = false;
  bool IsValueNullable = false;
  bool IsValueBlocked = false;
  std::unique_ptr<BlockStmt> Body;

  InitBlockStmt(std::string placeName, std::unique_ptr<BlockStmt> body)
      : PlaceName(std::move(placeName)), Body(std::move(body)) {}

  std::string toString() const override { return "InitBlock(" + PlaceName + ")"; }
  std::unique_ptr<ASTNode> clone() const override {
    auto n = std::make_unique<InitBlockStmt>(PlaceName, cloneNode(Body));
    n->PlaceLoc = PlaceLoc;
    n->IsValueMutable = IsValueMutable;
    n->IsValueNullable = IsValueNullable;
    n->IsValueBlocked = IsValueBlocked;
    n->Loc = Loc;
    return n;
  }
};

class ReturnStmt : public Stmt {
public:
  enum class MissOutcomeKind { None, Hit, Miss, Forward };
  std::unique_ptr<Expr> ReturnValue;
  MissOutcomeKind OutcomeKind = MissOutcomeKind::None;
  ReturnStmt(std::unique_ptr<Expr> val) : ReturnValue(std::move(val)) {}
  std::string toString() const override { return "Return"; }
  std::unique_ptr<ASTNode> clone() const override {
    auto n = std::make_unique<ReturnStmt>(cloneNode(ReturnValue));
    n->OutcomeKind = OutcomeKind;
    n->Loc = Loc;
    return n;
  }
};

class ExprStmt : public Stmt {
public:
  std::unique_ptr<Expr> Expression;
  ExprStmt(std::unique_ptr<Expr> expr) : Expression(std::move(expr)) {}
  std::string toString() const override { return "ExprStmt"; }
  std::unique_ptr<ASTNode> clone() const override {
    auto n = std::make_unique<ExprStmt>(cloneNode(Expression));
    n->Loc = Loc;
    return n;
  }
};

class DeleteStmt : public Stmt {
public:
  std::unique_ptr<Expr> Expression;
  DeleteStmt(std::unique_ptr<Expr> expr) : Expression(std::move(expr)) {}
  std::string toString() const override { return "Delete"; }
  std::unique_ptr<ASTNode> clone() const override {
    auto n = std::make_unique<DeleteStmt>(cloneNode(Expression));
    n->Loc = Loc;
    return n;
  }
};

class UnsafeStmt : public Stmt {
public:
  std::unique_ptr<Stmt> Statement;
  UnsafeStmt(std::unique_ptr<Stmt> stmt) : Statement(std::move(stmt)) {}
  std::string toString() const override { return "UnsafeStmt"; }
  std::unique_ptr<ASTNode> clone() const override {
    auto n = std::make_unique<UnsafeStmt>(cloneNode(Statement));
    n->Loc = Loc;
    return n;
  }
};

class FreeStmt : public Stmt {
public:
  std::unique_ptr<Expr> Expression;
  std::unique_ptr<Expr> Count;
  FreeStmt(std::unique_ptr<Expr> expr, std::unique_ptr<Expr> count = nullptr)
      : Expression(std::move(expr)), Count(std::move(count)) {}
  std::string toString() const override { return "FreeStmt"; }
  std::unique_ptr<ASTNode> clone() const override {
    auto n =
        std::make_unique<FreeStmt>(cloneNode(Expression), cloneNode(Count));
    n->Loc = Loc;
    return n;
  }
};
class UnreachableStmt : public Stmt {
public:
  UnreachableStmt() {}
  std::string toString() const override { return "Unreachable"; }
  std::unique_ptr<ASTNode> clone() const override {
    auto n = std::make_unique<UnreachableStmt>();
    n->Loc = Loc;
    return n;
  }
};

class IfExpr : public Expr {
public:
  std::unique_ptr<Expr> Condition;
  std::unique_ptr<Stmt> Then;
  std::unique_ptr<Stmt> Else;
  bool IsComptime = false;
  bool ComptimeTaken = false;

  IfExpr(std::unique_ptr<Expr> cond, std::unique_ptr<Stmt> thenStmt,
         std::unique_ptr<Stmt> elseStmt)
      : Condition(std::move(cond)), Then(std::move(thenStmt)),
        Else(std::move(elseStmt)) {}

  std::string toString() const override { return "If(...)"; }
  std::unique_ptr<ASTNode> clone() const override {
    auto n = std::make_unique<IfExpr>(cloneNode(Condition), cloneNode(Then),
                                      cloneNode(Else));
    n->Loc = Loc;
    n->ResolvedType = ResolvedType;
    return n;
  }
};

class GuardExpr : public Expr {
public:
  std::unique_ptr<Expr> Condition;
  std::unique_ptr<Stmt> Then;
  std::unique_ptr<Stmt> Else;

  GuardExpr(std::unique_ptr<Expr> cond, std::unique_ptr<Stmt> thenStmt,
            std::unique_ptr<Stmt> elseStmt = nullptr)
      : Condition(std::move(cond)), Then(std::move(thenStmt)),
        Else(std::move(elseStmt)) {}

  std::string toString() const override { return "Guard(...)"; }
  std::unique_ptr<ASTNode> clone() const override {
    auto n = std::make_unique<GuardExpr>(cloneNode(Condition), cloneNode(Then),
                                         cloneNode(Else));
    n->Loc = Loc;
    n->ResolvedType = ResolvedType;
    return n;
  }
};

class LoopExpr : public Expr {
public:
  std::unique_ptr<Expr> Condition;
  std::unique_ptr<Stmt> Body;

  LoopExpr(std::unique_ptr<Stmt> body) : Body(std::move(body)) {}
  LoopExpr(std::unique_ptr<Expr> cond, std::unique_ptr<Stmt> body)
      : Condition(std::move(cond)), Body(std::move(body)) {}

  std::string toString() const override { return "Loop"; }
  std::unique_ptr<ASTNode> clone() const override {
    auto n = std::make_unique<LoopExpr>(cloneNode(Condition), cloneNode(Body));
    n->Loc = Loc;
    n->ResolvedType = ResolvedType;
    return n;
  }
};

class ForExpr : public Expr {
public:
  std::string VarName;
  std::string MorphologyPrefix;
  bool IsReference = false;
  bool IsPlaceAlias = false;
  bool IsMutable = false;
  BindingPermission Permission;
  std::unique_ptr<Expr> Collection;
  std::unique_ptr<Stmt> Body;
  std::unique_ptr<Stmt> ElseBody;
  std::string IterElementType;
  std::shared_ptr<toka::Type> ResolvedIterElementType;
  std::string IteratorType;
  FunctionDecl *ResolvedIterFn = nullptr;
  FunctionDecl *ResolvedNextFn = nullptr;
  bool UsesPlaceIterator = false;

  // [Phase 2] Comptime Macro Unrolling
  bool IsComptimeUnrolled = false;
  std::vector<std::unique_ptr<Stmt>> UnrolledBodies;

  ForExpr(const std::string &varName, bool isRef, bool isMut,
          std::unique_ptr<Expr> coll, std::unique_ptr<Stmt> body,
          std::unique_ptr<Stmt> elseBody = nullptr)
      : VarName(varName), IsReference(isRef), IsMutable(isMut),
        Collection(std::move(coll)), Body(std::move(body)),
        ElseBody(std::move(elseBody)) {}

  std::string toString() const override { return "For(" + VarName + ")"; }
  std::unique_ptr<ASTNode> clone() const override {
    auto n = std::make_unique<ForExpr>(VarName, IsReference, IsMutable,
                                       cloneNode(Collection), cloneNode(Body),
                                       cloneNode(ElseBody));
    n->Loc = Loc;
    n->ResolvedType = ResolvedType;
    n->Permission = Permission;
    n->IsPlaceAlias = IsPlaceAlias;
    n->MorphologyPrefix = MorphologyPrefix;
    n->IterElementType = IterElementType;
    n->ResolvedIterElementType = ResolvedIterElementType;
    n->IteratorType = IteratorType;
    n->ResolvedIterFn = nullptr;
    n->ResolvedNextFn = nullptr;
    n->UsesPlaceIterator = UsesPlaceIterator;
    return n;
  }
};

// Deprecated: MatchStmt is replaced by MatchExpr since match is now an
// expression.
using MatchStmt = MatchExpr;

struct DestructuredVar {
  std::string Name;
  std::string FieldName;          // [NEW] Target field name inside struct (for named destructuring "y = val", FieldName is "y")
  bool IsWildcard = false;        // [NEW] Set to true for wildcard placeholders (e.g. "_", "x = _")
  bool IsValueMutable = false;
  bool IsValueNullable = false;
  bool IsValueBlocked = false;
  bool IsReference = false;
  BindingPermission Permission;
};

class DestructuringDecl : public Stmt {
public:
  std::string TypeName;
  std::vector<DestructuredVar> Variables;
  std::unique_ptr<Expr> Init;

  DestructuringDecl(const std::string &typeName,
                    std::vector<DestructuredVar> vars,
                    std::unique_ptr<Expr> init)
      : TypeName(typeName), Variables(std::move(vars)), Init(std::move(init)) {}

  std::string toString() const override { return "Destructuring " + TypeName; }
  std::unique_ptr<ASTNode> clone() const override {
    auto n = std::make_unique<DestructuringDecl>(TypeName, Variables,
                                                 cloneNode(Init));
    n->Loc = Loc;
    return n;
  }
};

class VariableDecl : public Stmt {
public:
  std::string Name;
  std::unique_ptr<Expr> Init;
  std::string TypeName;
  TypeSyntaxPtr DeclaredTypeSyntax;
  bool IsRawPointer = false;
  bool IsUnique = false;
  bool IsShared = false;
  bool IsReference = false;
  bool IsPub = false;
  bool IsConst = false;
  // Permissions (Dual-Location Attributes)
  bool IsRebindable = false;      // Pointer Attribute # (^#p)
  bool IsValueMutable = false;    // Identifier Attribute # (p#)
  bool IsPointerNullable = false; // Raw `nul *` (owning forms are errors).
  bool IsValueNullable = false;   // Removed `T?` parser-recovery bit only.
  bool IsRebindBlocked = false;   // Pointer Attribute $ (^$p)
  bool IsValueBlocked = false;    // Identifier Attribute $ (p$)
  bool IsMorphicExempt = false;   // [NEW] Exempt from strict hat rules
  BindingPermission Permission;
  // Elaborated by Sema for the bounded partial-cede slice.  This is not
  // source syntax and is deliberately recomputed for source-less TKI bodies.
  PartialMovePlan PartialMove;
  // Sema-owned lifetime action for the erased closure environment. CodeGen
  // executes this disposition and never reconstructs copy-vs-transfer intent.
  DynFnEnvironmentDisposition DynFnEnvironment =
      DynFnEnvironmentDisposition::None;

  VariableDecl(const std::string &name, std::unique_ptr<Expr> init)
      : Name(name), Init(std::move(init)) {}

  std::string toString() const override { return "Val " + Name; }
  std::unique_ptr<ASTNode> clone() const override {
    auto n = std::make_unique<VariableDecl>(Name, cloneNode(Init));
    n->TypeName = TypeName;
    n->DeclaredTypeSyntax = DeclaredTypeSyntax;
    n->IsRawPointer = IsRawPointer;
    n->IsUnique = IsUnique;
    n->IsShared = IsShared;
    n->IsReference = IsReference;
    n->IsPub = IsPub;
    n->IsConst = IsConst;
    n->IsRebindable = IsRebindable;
    n->IsValueMutable = IsValueMutable; // VariableDecl has this field
    n->IsValueBlocked = IsValueBlocked;
    n->IsMorphicExempt = IsMorphicExempt;
    n->Permission = Permission;
    n->IsPointerNullable = IsPointerNullable;
    n->IsValueNullable = IsValueNullable;
    n->IsRebindBlocked = IsRebindBlocked;
    n->PartialMove = PartialMove;
    n->DynFnEnvironment = DynFnEnvironment;
    n->Loc = Loc;
    n->ResolvedType = ResolvedType;
    return n;
  }

  std::shared_ptr<Type> ResolvedType;
};

class GuardBindStmt : public Stmt {
public:
  std::unique_ptr<MatchArm::Pattern> Pat;
  std::unique_ptr<Expr> Target;
  std::unique_ptr<Stmt> ElseBody;

  GuardBindStmt(std::unique_ptr<MatchArm::Pattern> pat,
                std::unique_ptr<Expr> target, std::unique_ptr<Stmt> elseBody)
      : Pat(std::move(pat)), Target(std::move(target)), ElseBody(std::move(elseBody)) {}

  std::string toString() const override { return "GuardBind"; }
  std::unique_ptr<ASTNode> clone() const override {
    auto n = std::make_unique<GuardBindStmt>(
        Pat ? Pat->clonePattern() : nullptr, 
        cloneNode(Target),
        cloneNode(ElseBody));
    n->Loc = Loc;
    return n;
  }
};


// --- High-level Declarations ---

class TypeAliasDecl : public ASTNode {
public:
  bool IsPub = false;
  std::string Name;
  std::string TargetType;
  TypeSyntaxPtr TargetTypeSyntax;
  bool IsStrong = false;
  std::vector<GenericParam> GenericParams; // [NEW]

  TypeAliasDecl(bool isPub, const std::string &name, const std::string &target,
                bool isStrong = false, std::vector<GenericParam> generics = {})
      : IsPub(isPub), Name(name), TargetType(target), IsStrong(isStrong),
        GenericParams(std::move(generics)) {}
  std::string toString() const override {
    return std::string(IsPub ? "Pub" : "") + "TypeAlias(" + Name + " = " +
           TargetType + ")";
  }
};

enum class ShapeKind { Struct, Tuple, Array, Enum, Union };

struct ShapeMember {
  SourceLocation Loc;
  std::string Name; // Member or Variant name
  std::string Type;
  TypeSyntaxPtr TypeSyntax;
  int64_t TagValue = -1; // Specific value for tagged enum variants (= 1)
  bool IsRawPointer = false;
  bool IsUnique = false;
  bool IsShared = false;
  bool IsReference = false;
  bool IsValueMutable = false;
  bool IsValueNullable = false;   // Removed syntax recovery only.
  bool IsRebindable = false;      // "#" handle attribute
  bool IsPointerNullable = false; // Raw may-zero handle attribute.
  bool IsRebindBlocked = false;   // "$" pointer attribute
  bool IsValueBlocked = false;    // "$" identifier attribute
  bool IsExplicitBound = false;   // "`" explicit lifetime binding attribute
  bool IsMorphicExempt = false;   // [NEW] Exempt from strict hat rules
  BindingPermission Permission;

  // For enum variant payloads and legacy bare unions.
  std::vector<ShapeMember> SubMembers;
  ShapeKind SubKind = ShapeKind::Struct;
  // A unit enum variant has no payload.  This is deliberately not encoded as
  // an empty type spelling or ABI `void`.
  bool IsUnitVariant = false;

  // Resolution Cache from Sema
  std::shared_ptr<toka::Type> ResolvedType = nullptr;
  std::unique_ptr<Expr> DefaultValue = nullptr;

  ShapeMember() = default;
  ShapeMember(ShapeMember &&) = default;
  ShapeMember &operator=(ShapeMember &&) = default;

  ShapeMember(const ShapeMember &other) {
    Loc = other.Loc;
    Name = other.Name;
    Type = other.Type;
    TypeSyntax = other.TypeSyntax;
    TagValue = other.TagValue;
    IsRawPointer = other.IsRawPointer;
    IsUnique = other.IsUnique;
    IsShared = other.IsShared;
    IsReference = other.IsReference;
    IsValueMutable = other.IsValueMutable;
    IsValueNullable = other.IsValueNullable;
    IsRebindable = other.IsRebindable;
    IsPointerNullable = other.IsPointerNullable;
    IsRebindBlocked = other.IsRebindBlocked;
    IsValueBlocked = other.IsValueBlocked;
    IsExplicitBound = other.IsExplicitBound;
    IsMorphicExempt = other.IsMorphicExempt;
    Permission = other.Permission;
    SubMembers = other.SubMembers;
    SubKind = other.SubKind;
    IsUnitVariant = other.IsUnitVariant;
    ResolvedType = other.ResolvedType;
    if (other.DefaultValue) {
      DefaultValue = std::unique_ptr<Expr>(
          static_cast<Expr *>(other.DefaultValue->clone().release()));
    }
  }

  ShapeMember &operator=(const ShapeMember &other) {
    if (this == &other)
      return *this;
    Loc = other.Loc;
    Name = other.Name;
    Type = other.Type;
    TypeSyntax = other.TypeSyntax;
    TagValue = other.TagValue;
    IsRawPointer = other.IsRawPointer;
    IsUnique = other.IsUnique;
    IsShared = other.IsShared;
    IsReference = other.IsReference;
    IsValueMutable = other.IsValueMutable;
    IsValueNullable = other.IsValueNullable;
    IsRebindable = other.IsRebindable;
    IsPointerNullable = other.IsPointerNullable;
    IsRebindBlocked = other.IsRebindBlocked;
    IsValueBlocked = other.IsValueBlocked;
    IsExplicitBound = other.IsExplicitBound;
    IsMorphicExempt = other.IsMorphicExempt;
    Permission = other.Permission;
    SubMembers = other.SubMembers;
    SubKind = other.SubKind;
    IsUnitVariant = other.IsUnitVariant;
    ResolvedType = other.ResolvedType;
    if (other.DefaultValue) {
      DefaultValue = std::unique_ptr<Expr>(
          static_cast<Expr *>(other.DefaultValue->clone().release()));
    } else {
      DefaultValue = nullptr;
    }
    return *this;
  }
};

class ShapeDecl : public ASTNode {
public:
  bool IsPub = false;
  std::string Name;
  std::string CodegenName;
  // Stable physical owner identity for impl/drop linkage.  CodegenName keeps
  // its legacy LLVM-layout role; OwnerLinkName must not depend on which other
  // modules happen to be present in the current compilation graph.
  std::string OwnerLinkName;
  std::optional<NominalShapeId> NominalId;
  // Compiler-derived declarations are implementation artifacts rather than
  // source-level nominal definitions.  This survives AST reuse so a later
  // semantic-analysis revision does not assign them a source identity.
  bool IsCompilerSynthesized = false;
  ShapeDecl *InstantiationTemplate = nullptr;
  std::vector<std::shared_ptr<toka::Type>> InstantiationArgs;
  // struct GenericParam moved to top-level
  std::vector<GenericParam> GenericParams; // [UPDATED] e.g. <T, N_: usize>
  ShapeKind Kind;
  std::vector<ShapeMember> Members;
  int64_t ArraySize = 0; // For Array kind
  uint64_t MaxAlign = 1; // For enum/legacy union alignment persistence
  bool IsSync = false;   // [NEW] Indication for thread-safe/atomic reference bounds

  ShapeDecl(bool isPub, const std::string &name,
            std::vector<GenericParam> generics, ShapeKind kind,
            std::vector<ShapeMember> members)
      : IsPub(isPub), Name(name),
        GenericParams(std::move(generics)), Kind(kind),
        Members(std::move(members)) {}

  std::string toString() const override {
    std::string s = std::string(IsPub ? "Pub " : "") + "Shape(" + Name;
    if (!GenericParams.empty()) {
      s += "<";
      for (size_t i = 0; i < GenericParams.size(); ++i) {
        if (i > 0)
          s += ", ";
        s += GenericParams[i].Name;
        if (GenericParams[i].IsConst)
          s += ": " + GenericParams[i].Type;
      }
      s += ">";
    }
    s += ")";
    return s;
  }

  // [NEW] Cache for the mangled name of the destructor (drop method)
  std::string MangledDestructorName;
  // Distinguishes a source-level `@Encap drop` from the compiler-generated
  // structural destructor used for resource-containing records.
  bool HasExplicitDrop = false;
};

// Deprecated: Use ShapeDecl
using StructDecl = ShapeDecl;
using OptionDecl = ShapeDecl;

struct ImportItem {
  std::string Symbol; // Name of symbol, or "*" for wildcard
  std::string Alias;  // Optional alias
};

class ImportDecl : public ASTNode {
public:
  bool IsPub = false;
  std::string PhysicalPath;
  std::string ResolvedPath;      // Pre-resolved canonical absolute path
  std::string Alias;             // Module alias (e.g. import path as alias)
  std::vector<ImportItem> Items; // If empty, it's a module import (import path)
  bool HasBeenUsed = false;

  ImportDecl(bool isPub, const std::string &path, const std::string &alias = "",
             std::vector<ImportItem> items = {})
      : IsPub(isPub), PhysicalPath(path), Alias(alias),
        Items(std::move(items)) {}

  bool IsImplicit = false;

  std::string toString() const override {
    std::string s = IsPub ? "PubImport(" : "Import(";
    s += PhysicalPath;
    if (!Items.empty()) {
      s += " :: {";
      for (size_t i = 0; i < Items.size(); ++i) {
        if (i > 0)
          s += ", ";
        s += Items[i].Symbol;
        if (!Items[i].Alias.empty())
          s += " as " + Items[i].Alias;
      }
      s += "}";
    }
    s += ")";
    return s;
  }
};

enum class EffectKind { None, Async, Wait };

struct DependencyPathSyntax {
  std::string Root;
  std::vector<std::string> Members;
  bool IsReference = false;
  SourceLocation Begin;
  SourceLocation End;

  std::string toCanonicalString() const {
    std::string result = Root;
    for (const auto &member : Members)
      result += "." + member;
    return result;
  }
};

enum class ReturnDependencyTargetKind { ReturnValue, NamedBinding };

enum class ReturnResultKind { Unit, Typed, AbiVoid, Never };

struct ReturnDependencyTargetSyntax {
  ReturnDependencyTargetKind Kind = ReturnDependencyTargetKind::ReturnValue;
  std::string BindingName;
  std::string BindingPrefix;
  std::string MemberName;
  std::string MemberPrefix;
  SourceLocation Begin;
  SourceLocation End;
};

struct ReturnDependencyRouteSyntax {
  ReturnDependencyTargetSyntax Target;
  std::vector<DependencyPathSyntax> Sources;
  SourceLocation Begin;
  SourceLocation End;
};

/// Source-level return contract.  Legacy FunctionDecl fields are derived
/// caches for Sema and CodeGen while those layers still use string-based
/// contracts.
struct ReturnContractSyntax {
  bool HasArrow = false;
  // `-> async` / `-> wait` carries an effect but intentionally omits a
  // result type.  Keep that distinct from a source-level `-> ()` so generic
  // substitution and contract diagnostics retain source intent.
  bool HasExplicitResultType = false;
  // A legacy string cache derived by the parser/printer boundary.  The
  // result category is authoritative: an omitted ordinary result is Unit,
  // not ABI `void`.
  std::string Type = "()";
  TypeSyntaxPtr TypeSyntax;
  ReturnResultKind ResultKind = ReturnResultKind::Unit;
  std::string BindingName;
  std::string BindingPrefix;
  // Source-level payload permission belongs to the named result binding
  // (`-> &item#: T`), not to the Soul spelling after the colon.  TypeSyntax
  // receives the corresponding postfix morphology only as a semantic cache.
  bool BindingSoulWritable = false;
  // A named `&result: T` contract borrows the payload selected by `T`.  If a
  // morphic generic later substitutes `T = ^U`, `~U`, or `&U`, the result is
  // `&U`, not an identity borrow such as `&^U`.
  bool BindingBorrowsSoul = false;
  EffectKind Effect = EffectKind::None;
  std::vector<ReturnDependencyRouteSyntax> Routes;
  SourceLocation Begin;
  SourceLocation End;

  void classifyResult() {
    if (!HasArrow) {
      ResultKind = ReturnResultKind::Unit;
      Type = "()";
      return;
    }
    if (Type == "void")
      ResultKind = ReturnResultKind::AbiVoid;
    else if (Type == "never")
      ResultKind = ReturnResultKind::Never;
    else if (Type == "()")
      ResultKind = ReturnResultKind::Unit;
    else
      ResultKind = ReturnResultKind::Typed;
  }

  void deriveLegacyDependencies(
      std::vector<std::string> &lifeDependencies,
      std::map<std::string, std::vector<std::string>> &memberDependencies) const {
    lifeDependencies.clear();
    memberDependencies.clear();
    auto appendUnique = [](std::vector<std::string> &target,
                           const std::string &dependency) {
      if (std::find(target.begin(), target.end(), dependency) == target.end())
        target.push_back(dependency);
    };
    for (const auto &route : Routes) {
      std::vector<std::string> &target = route.Target.MemberName.empty()
                                             ? lifeDependencies
                                             : memberDependencies[route.Target.MemberName];
      for (const auto &source : route.Sources)
        appendUnique(target, source.toCanonicalString());
    }
  }
};

// Outcome Contracts are independent of return-dependency routes.  Their
// source spelling is `Variant => place: init|uninit`; Sema resolves both names
// to the direct nominal return variant and the exact init formal.
enum class OutcomePostState { Init, Uninit };

struct OutcomeTransitionSyntax {
  std::string Variant;
  std::string Subject;
  OutcomePostState Post = OutcomePostState::Uninit;
  SourceLocation Begin;
  SourceLocation End;
};

struct OutcomeContractSyntax {
  std::vector<OutcomeTransitionSyntax> Transitions;
  SourceLocation Begin;
  SourceLocation End;

  bool empty() const { return Transitions.empty(); }

  const OutcomeTransitionSyntax *find(const std::string &variant) const {
    for (const auto &transition : Transitions) {
      if (transition.Variant == variant)
        return &transition;
    }
    return nullptr;
  }
};

class FunctionDecl : public ASTNode {
public:
  struct Arg {
    SourceLocation Loc;
    std::string Name;
    std::string Type;
    TypeSyntaxPtr TypeSyntax;
    bool IsRawPointer = false;
    bool IsUnique = false;
    bool IsShared = false;
    bool IsReference = false;

    // Permissions
    bool IsRebindable = false;
    bool IsValueMutable = false;
    bool IsPointerNullable = false;
    bool IsValueNullable = false;
    bool IsRebindBlocked = false; // "$" pointer attribute
    bool IsValueBlocked = false;  // "$" identifier attribute
    bool IsMorphicExempt = false; // [NEW] Exempt from strict hat rules
    bool IsCeded = false;         // [NEW] Ownership consumed by callee
    // The callee constructs caller-owned storage supplied as `init place`.
    bool IsInit = false;
    bool HadRejectedTypeSideMorphology = false;
    // Declaration provenance for explicit-cede planning and staged
    // activation. It is derived from typed syntax and the exact generic
    // binder set before substitution, then preserved by cloning.
    bool Stage0GenericValueRole = false;
    bool Stage0MorphicGenericRole = false;
    bool Stage0DeclarationProvenanceComplete = false;
    std::vector<CallableParameterProvenance> CallableParameterOrigins;
    bool CallableParameterOriginsComplete = false;
    BindingPermission Permission;

    std::shared_ptr<toka::Type> ResolvedType;
    std::unique_ptr<Expr> DefaultValue;

    Arg clone() const {
      Arg a;
      a.Loc = Loc;
      a.Name = Name;
      a.Type = Type;
      a.TypeSyntax = TypeSyntax;
      a.IsRawPointer = IsRawPointer;
      a.IsUnique = IsUnique;
      a.IsShared = IsShared;
      a.IsReference = IsReference;
      a.IsRebindable = IsRebindable;
      a.IsValueMutable = IsValueMutable;
      a.IsPointerNullable = IsPointerNullable;
      a.IsValueNullable = IsValueNullable;
      a.IsRebindBlocked = IsRebindBlocked;
      a.IsValueBlocked = IsValueBlocked;
      a.IsMorphicExempt = IsMorphicExempt;
      a.IsCeded = IsCeded;
      a.IsInit = IsInit;
      a.HadRejectedTypeSideMorphology = HadRejectedTypeSideMorphology;
      a.Stage0GenericValueRole = Stage0GenericValueRole;
      a.Stage0MorphicGenericRole = Stage0MorphicGenericRole;
      a.Stage0DeclarationProvenanceComplete =
          Stage0DeclarationProvenanceComplete;
      a.CallableParameterOrigins = CallableParameterOrigins;
      a.CallableParameterOriginsComplete = CallableParameterOriginsComplete;
      a.Permission = Permission;
      a.ResolvedType = ResolvedType;
      a.DefaultValue = cloneNode(DefaultValue);
      return a;
    }
  };

  // This is the resolved semantic form of an `outcomes:` declaration.  The
  // syntax block remains available for parsing and diagnostics; consumers use
  // these declaration identities after Sema has checked the contract.
  struct OutcomeTransition {
    struct Case {
      const ShapeMember *Variant = nullptr;
      size_t VariantOrdinal = 0;
      std::string VariantIdentity;
      OutcomePostState Post = OutcomePostState::Uninit;
    };

    const Arg *Subject = nullptr;
    size_t SubjectIndex = 0;
    std::string FunctionIdentity;
    std::string SubjectIdentity;
    const ShapeDecl *ReturnEnum = nullptr;
    std::string ReturnEnumIdentity;
    // Audit-only P1 admission condition: every identity root in this narrow
    // declaration fact has a resolver-owned coordinate.  It grants no import
    // authority and does not enable a declaration witness payload.
    bool HasKnownDeclarationCoordinates = false;
    // The current audit-only candidate type encoding admits only concrete
    // first-order physical types with resolver-known nominal owners.  An
    // unavailable result is fail-closed for a future CDW schema.
    bool HasCanonicalTypeIdentities = false;
    std::vector<Case> Cases;

    const Case *findVariant(const std::string &name) const {
      for (const auto &entry : Cases) {
        if (entry.Variant && entry.Variant->Name == name)
          return &entry;
      }
      return nullptr;
    }

    const Case *findVariant(const ShapeMember *variant) const {
      for (const auto &entry : Cases) {
        if (entry.Variant == variant)
          return &entry;
      }
      return nullptr;
    }
  };

  bool IsPub = false;
  std::string Name;
  std::string CodegenName;
  std::vector<Arg> Args;
  std::string ReturnType;
  TypeSyntaxPtr ReturnTypeSyntax;
  EffectKind Effect = EffectKind::None;
  ReturnContractSyntax ReturnContract;
  OutcomeContractSyntax OutcomeContract;
  std::shared_ptr<toka::Type> ResolvedReturnType;
  std::vector<std::string> LifeDependencies; // [NEW] e.g., <- x|y
  std::map<std::string, std::vector<std::string>> MemberDependencies; // [NEW] e.g. res.&left <- a
  std::unique_ptr<BlockStmt> Body;
  FunctionMemorySummary MemorySummary;

  bool IsVariadic = false;
  bool IsClosureInvoke = false;
  // Set by Sema only for declarations resolved from the trusted
  // core/intrinsics/atomic toolchain module.
  bool IsTrustedAtomicIntrinsic = false;
  CallableReceiverMode ClosureReceiver = CallableReceiverMode::Shared;
  std::optional<OutcomeTransition> ResolvedOutcomeTransition;
  // Set only by the explicit P2 profile while its containing bodyless TKI is
  // awaiting full post-Sema attestation validation. It is never serialized and
  // cannot establish authority without that later atomic gate.
  bool HasSemanticManifestAttestationCandidate = false;
  std::vector<GenericParam> GenericParams; // [NEW] e.g. <T>
  FunctionDecl *TemplateOrigin = nullptr;  // Tooling identity for an instance.
  bool Stage0BodyQualificationRequired = false;
  bool Stage0BodyQualificationComplete = false;
  std::string Stage0BodySpecializationIdentity;
  // Exact generic type binders supplied by the enclosing trait/impl.  This is
  // declaration provenance, not a resolved-type property, and is preserved on
  // materialized methods after those binders have been substituted.
  std::set<std::string> Stage0EnclosingGenericTypeNames;

  FunctionDecl(bool isPub, const std::string &name, std::vector<Arg> args,
               std::unique_ptr<BlockStmt> body, const std::string &retType,
               std::vector<GenericParam> generics = {},
               std::vector<std::string> lifeDeps = {},
               EffectKind effect = EffectKind::None)
      : IsPub(isPub), Name(name), Args(std::move(args)), ReturnType(retType),
        Effect(effect), Body(std::move(body)), GenericParams(std::move(generics)),
        LifeDependencies(std::move(lifeDeps)) {
    ReturnContract.Type = ReturnType;
    ReturnContract.Effect = Effect;
    ReturnContract.HasArrow = ReturnType != "()";
    ReturnContract.classifyResult();
    if (!LifeDependencies.empty()) {
      ReturnDependencyRouteSyntax route;
      route.Target.Kind = ReturnDependencyTargetKind::ReturnValue;
      for (const auto &dependency : LifeDependencies)
        route.Sources.push_back(DependencyPathSyntax{dependency});
      ReturnContract.Routes.push_back(std::move(route));
    }
  }
  void setReturnContract(ReturnContractSyntax contract) {
    ReturnContract = std::move(contract);
    ReturnContract.classifyResult();
    ReturnType = ReturnContract.Type;
    ReturnTypeSyntax = ReturnContract.TypeSyntax;
    Effect = ReturnContract.Effect;
    ReturnContract.deriveLegacyDependencies(LifeDependencies,
                                            MemberDependencies);
  }
  void syncReturnContractTypeCache() {
    const bool wasExplicitTypedResult =
        ReturnContract.HasExplicitResultType &&
        ReturnContract.ResultKind == ReturnResultKind::Typed;
    ReturnContract.Type = ReturnType;
    ReturnContract.TypeSyntax = ReturnTypeSyntax;
    ReturnContract.Effect = Effect;
    ReturnContract.classifyResult();
    // Generic substitution may turn a source-level `-> T` into Unit.  That
    // remains an explicit typed result contract; only source `-> ()` is the
    // redundant spelling rejected for ordinary functions.
    if (wasExplicitTypedResult && ReturnContract.Type == "()")
      ReturnContract.ResultKind = ReturnResultKind::Typed;
  }
  std::string toString() const override {
    return std::string(IsPub ? "Pub" : "") + "Fn(" + Name + ")";
  }
  std::unique_ptr<ASTNode> clone() const override {
    // Manually deep copy args (since they contain strings/bools and shared_ptr)
    // shared_ptr copy is shallow for resolved type, which is what we want?
    // Wait, generic template args have unresolved types usually? Or generic
    // types? We copy whatever is there.
    std::vector<Arg> clonedArgs;
    for (const auto &arg : Args) {
      clonedArgs.push_back(arg.clone());
    }

    // Deep copy body
    auto clonedBody =
        Body ? std::unique_ptr<BlockStmt>(
                   static_cast<BlockStmt *>(Body->clone().release()))
             : nullptr;

    auto n = std::make_unique<FunctionDecl>(IsPub, Name, std::move(clonedArgs),
                                            std::move(clonedBody), ReturnType,
                                            GenericParams, LifeDependencies, Effect);
    n->CodegenName = CodegenName;
    n->setReturnContract(ReturnContract);
    n->OutcomeContract = OutcomeContract;
    n->MemberDependencies = MemberDependencies;
    n->IsVariadic = IsVariadic;
    n->IsClosureInvoke = IsClosureInvoke;
    n->IsTrustedAtomicIntrinsic = IsTrustedAtomicIntrinsic;
    n->ClosureReceiver = ClosureReceiver;
    n->TemplateOrigin = TemplateOrigin;
    n->Stage0EnclosingGenericTypeNames = Stage0EnclosingGenericTypeNames;
    n->Loc = Loc;
    n->ResolvedReturnType = ResolvedReturnType;
    // FunctionDecl is NOT an Expr, does not have ResolvedType?
    // FunctionDecl inherits ASTNode directly (Line 993).
    // ASTNode doesn't have ResolvedType.
    return n;
  }
};

enum class CaptureMode {
  ImplicitBorrow,
  ExplicitCede,
  ExplicitCopy,
  ExplicitDup,
};

struct CaptureItem {
  std::string Name;
  CaptureMode Mode;
  SourceLocation Loc;

  CaptureItem clone() const {
    CaptureItem c;
    c.Name = Name;
    c.Mode = Mode;
    c.Loc = Loc;
    return c;
  }
};

struct ClosureParamSyntax {
  std::string Name;
  BindingPermission Permission;
  SourceLocation Loc;
  bool HadRejectedTypeSideMorphology = false;

  ClosureParamSyntax clone() const {
    ClosureParamSyntax p;
    p.Name = Name;
    p.Permission = Permission;
    p.Loc = Loc;
    p.HadRejectedTypeSideMorphology = HadRejectedTypeSideMorphology;
    return p;
  }
};

class ClosureExpr : public Expr {
public:
  std::vector<CaptureItem> ExplicitCaptures;
  std::vector<std::string> ImplicitCaptures; // Filled by Sema
  // Boundary facts are semantic metadata, not a runtime closure layout.  They
  // travel with a closure expression until its binding copies them into the
  // symbol table for detached-execution checks.
  bool HasBoundaryCaptureSummary = false;
  std::vector<std::string> BoundaryImplicitCaptures;
  std::vector<std::string> BoundaryNonSendCaptures;
  std::vector<std::string> BoundaryNonSyncCopyCaptures;
  
  bool HasExplicitArgs = false;
  std::vector<ClosureParamSyntax> Params; // Structured closure parameters
  std::vector<std::string> ArgNames; // Either explicit names or filled lazily by Sema
  std::vector<std::shared_ptr<toka::Type>> InjectedParamTypes; // [NEW] Top-down type injection
  int MaxImplicitArgIndex = -1; // Tracks max index (.a=0, .b=1) used in the body
  
  std::string ReturnType;
  std::shared_ptr<toka::Type> ResolvedReturnType;
  std::unique_ptr<BlockStmt> Body;
  std::string SynthesizedShapeName;
  CallableReceiverMode CallableReceiver = CallableReceiverMode::Shared;

  ClosureExpr() {}
  std::string toString() const override { return "ClosureExpr(" + SynthesizedShapeName + ")"; }
  std::unique_ptr<ASTNode> clone() const override {
    auto n = std::make_unique<ClosureExpr>();
    for (const auto &cap : ExplicitCaptures) {
      n->ExplicitCaptures.push_back(cap.clone());
    }
    n->ImplicitCaptures = ImplicitCaptures;
    n->HasBoundaryCaptureSummary = HasBoundaryCaptureSummary;
    n->BoundaryImplicitCaptures = BoundaryImplicitCaptures;
    n->BoundaryNonSendCaptures = BoundaryNonSendCaptures;
    n->BoundaryNonSyncCopyCaptures = BoundaryNonSyncCopyCaptures;
    n->HasExplicitArgs = HasExplicitArgs;
    for (const auto &p : Params) {
      n->Params.push_back(p.clone());
    }
    n->ArgNames = ArgNames;
    n->MaxImplicitArgIndex = MaxImplicitArgIndex;
    n->ReturnType = ReturnType;
    n->ResolvedReturnType = ResolvedReturnType;
    if (Body) {
      n->Body = std::unique_ptr<BlockStmt>(
          static_cast<BlockStmt *>(Body->clone().release()));
    }
    n->SynthesizedShapeName = SynthesizedShapeName;
    n->CallableReceiver = CallableReceiver;
    n->Loc = Loc;
    n->ResolvedType = ResolvedType;
    return n;
  }
};

class ExternDecl : public ASTNode {
public:
  struct Arg {
    SourceLocation Loc;
    std::string Name;
    std::string Type;
    TypeSyntaxPtr TypeSyntax;
    bool IsRawPointer = false;
    bool IsReference = false;

    // New Permissions match FunctionDecl
    bool IsUnique = false;
    bool IsShared = false;
    bool IsRebindable = false;
    bool IsValueMutable = false;
    bool IsPointerNullable = false;
    bool IsValueNullable = false;
    bool IsRebindBlocked = false;
    bool IsValueBlocked = false;
    bool IsMorphicExempt = false;
    bool IsCeded = false;
    bool HadRejectedTypeSideMorphology = false;
    BindingPermission Permission;

    std::shared_ptr<toka::Type> ResolvedType;
    std::unique_ptr<Expr> DefaultValue;

    Arg clone() const {
      Arg a;
      a.Loc = Loc;
      a.Name = Name;
      a.Type = Type;
      a.TypeSyntax = TypeSyntax;
      a.IsRawPointer = IsRawPointer;
      a.IsReference = IsReference;
      a.IsUnique = IsUnique;
      a.IsShared = IsShared;
      a.IsRebindable = IsRebindable;
      a.IsValueMutable = IsValueMutable;
      a.IsPointerNullable = IsPointerNullable;
      a.IsValueNullable = IsValueNullable;
      a.IsRebindBlocked = IsRebindBlocked;
      a.IsValueBlocked = IsValueBlocked;
      a.IsMorphicExempt = IsMorphicExempt;
      a.IsCeded = IsCeded;
      a.HadRejectedTypeSideMorphology = HadRejectedTypeSideMorphology;
      a.Permission = Permission;
      a.ResolvedType = ResolvedType;
      a.DefaultValue = cloneNode(DefaultValue);
      return a;
    }
  };
  std::string Name;
  std::vector<Arg> Args;
  std::string ReturnType;
  TypeSyntaxPtr ReturnTypeSyntax;
  EffectKind Effect = EffectKind::None;
  ReturnContractSyntax ReturnContract;
  bool IsVariadic = false;

  ExternDecl(const std::string &name, std::vector<Arg> args,
             std::string retType, EffectKind effect = EffectKind::None)
      : Name(name), Args(std::move(args)), ReturnType(retType), Effect(effect) {
    ReturnContract.Type = ReturnType;
    ReturnContract.Effect = Effect;
    ReturnContract.HasArrow = ReturnType != "()";
    ReturnContract.classifyResult();
  }
  void setReturnContract(ReturnContractSyntax contract) {
    ReturnContract = std::move(contract);
    ReturnContract.classifyResult();
    ReturnType = ReturnContract.Type;
    ReturnTypeSyntax = ReturnContract.TypeSyntax;
    Effect = ReturnContract.Effect;
  }
  void syncReturnContractTypeCache() {
    const bool wasExplicitTypedResult =
        ReturnContract.HasExplicitResultType &&
        ReturnContract.ResultKind == ReturnResultKind::Typed;
    ReturnContract.Type = ReturnType;
    ReturnContract.TypeSyntax = ReturnTypeSyntax;
    ReturnContract.Effect = Effect;
    ReturnContract.classifyResult();
    if (wasExplicitTypedResult && ReturnContract.Type == "()")
      ReturnContract.ResultKind = ReturnResultKind::Typed;
  }
  std::string toString() const override { return "Extern(" + Name + ")"; }
  std::unique_ptr<ASTNode> clone() const override {
    std::vector<Arg> clonedArgs;
    for (const auto &arg : Args) {
      clonedArgs.push_back(arg.clone());
    }
    auto n =
        std::make_unique<ExternDecl>(Name, std::move(clonedArgs), ReturnType, Effect);
    n->setReturnContract(ReturnContract);
    n->IsVariadic = IsVariadic;
    n->Loc = Loc;
    return n;
  }
};

struct EncapEntry {
  std::vector<std::string> Fields;
};

struct AssociatedTypeDecl {
  std::string Name;
  std::string Type;
  TypeSyntaxPtr TypeSyntax;
  // Exact semantic RHS for materialized generic impls. Source spelling is
  // retained for diagnostics/TKI, but cannot carry a cross-module nominal
  // ShapeDecl through a T -> Item substitution.
  std::shared_ptr<toka::Type> ResolvedType;
  bool IsPer = false;
  SourceLocation Loc;
};

struct ImplHeaderSyntax {
  TypeSyntaxPtr Type;
  std::string TraitName;
  SourceLocation Begin;
  SourceLocation End;
};

class ImplDecl : public ASTNode {
public:
  // Non-null only for a materialized generic impl.  Its method signatures
  // have already been substituted with resolved semantic Types and must not
  // be rebuilt from their lossy source spelling.
  ImplDecl *TemplateOrigin = nullptr;
  // Exact semantic owner of this impl after declaration resolution.  Short
  // TypeName remains source/diagnostic spelling only.
  ShapeDecl *ResolvedOwner = nullptr;
  std::string TypeName;
  ImplHeaderSyntax HeaderSyntax;
  std::string TraitName;
  std::vector<std::unique_ptr<FunctionDecl>> Methods;
  std::vector<EncapEntry> EncapEntries;
  std::vector<AssociatedTypeDecl> AssociatedTypes;
  std::vector<GenericParam> GenericParams; // [NEW] e.g. <T>
  // True only for the compiler-generated resource destructor.  Such an impl
  // supplies lifetime behavior, not an encapsulation boundary.
  bool IsStructuralDrop = false;

  ImplDecl(const std::string &name,
           std::vector<std::unique_ptr<FunctionDecl>> methods,
           const std::string &traitName = "",
           std::vector<GenericParam> generics = {})
      : TypeName(name), Methods(std::move(methods)), TraitName(traitName),
        GenericParams(std::move(generics)) {}
  std::string toString() const override {
    return "Impl(" + (TraitName.empty() ? "" : TraitName + " for ") + TypeName +
           ")";
  }
};

class TraitDecl : public ASTNode {
public:
  bool IsPub = false;
  std::string Name;
  std::vector<GenericParam> GenericParams;
  std::vector<std::string> SelfTraitBounds;
  std::vector<AssociatedTypeDecl> AssociatedTypes;
  std::vector<std::unique_ptr<FunctionDecl>> Methods;

  TraitDecl(bool isPub, const std::string &name,
            std::vector<std::unique_ptr<FunctionDecl>> methods,
            std::vector<GenericParam> generics = {},
            std::vector<std::string> selfTraitBounds = {},
            std::vector<AssociatedTypeDecl> associatedTypes = {})
      : IsPub(isPub), Name(name), GenericParams(std::move(generics)),
        SelfTraitBounds(std::move(selfTraitBounds)),
        AssociatedTypes(std::move(associatedTypes)),
        Methods(std::move(methods)) {}
  std::string toString() const override {
    return std::string(IsPub ? "Pub" : "") + "Trait(" + Name + ")";
  }
};

class Module : public ASTNode {
public:
  std::string SourcePath;
  std::string ResolvedPath;
  bool IsRootModule = false;
  bool IsInterface = false;
  bool IsTrustedSystemModule = false; // Resolver provenance; never serialized.
  bool HasBackingObject = false;
  std::string BackingObjectPath;
  // Resolver evidence used by stable nominal declaration identities.  A
  // source_path alone never grants a resolver coordinate.
  bool ShadowCoordinateKnown = false;
  std::string ShadowCrateId;
  std::string ShadowLogicalModulePath;
  std::string ShadowCoordinateOrigin;
  std::string ShadowCoordinateReason;
  // `.tki` transport fact: these shapes have compiler-generated structural
  // destructors, not source-defined `@Encap drop` implementations.
  std::set<std::string> InterfaceStructuralDropShapes;
  // Slice 5 emits these replay facts as TKI v2 comments.  They are derived
  // after semantic analysis; the source declarations remain authoritative.
  std::vector<std::string> InterfaceV2Facts;
  // Raw, canonical CDW1 values for the admitted Outcome P1 subset. This is
  // compiler semantic data, not a parsed TKI comment or an import authority.
  std::vector<std::string> CanonicalOutcomeDeclarationWitnesses;
  bool RequiresSemanticManifestAttestation = false;
  std::vector<std::string> SemanticManifestAttestationRecords;
  std::string SemanticManifestAttestationStatus = "NotApplicable";
  std::string SemanticManifestAttestationReason;
  std::map<std::string, FunctionMemorySummary> TrustedMemorySummaries;
  std::string MemoryEvidenceStatus = "NotApplicable";
  std::string MemoryEvidenceReason;
  std::vector<std::unique_ptr<ImportDecl>> Imports;
  std::vector<std::unique_ptr<TypeAliasDecl>> TypeAliases;
  std::vector<std::unique_ptr<ShapeDecl>> Shapes;
  std::vector<std::unique_ptr<Stmt>> Globals;
  std::vector<std::unique_ptr<ImplDecl>> Impls;
  std::vector<std::unique_ptr<TraitDecl>> Traits;
  std::vector<std::unique_ptr<ExternDecl>> Externs;
  std::vector<std::unique_ptr<FunctionDecl>> Functions;

  std::string toString() const override { return "Module"; }
};

} // namespace toka
