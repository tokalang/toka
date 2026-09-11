#pragma once
#include "toka/AccessPath.h"
#include <memory>
#include <string>
#include <vector>

namespace toka {
class Expr;
class CastExpr;
class FunctionDecl;
class Type;

// Ancestry only: this identifies an allocation expression that contributed
// an address. It does NOT establish current liveness, initialized elements,
// ownership, dependency freedom, or permission to load/take that address.
struct RawAllocationAncestry {
  std::string SourceEdge;
  std::shared_ptr<Type> StorageType;
  bool IsArray = false;
  bool HasInitializerSyntax = false;
};

enum class RawWriteAuthority { None, UnsafeCallerPrecondition };

// Immutable value provenance. Opaque carries no pointee permission; it is
// deliberately different from an observed readonly/frozen source.
struct RawAddressSource {
  enum class Kind { Value, Unresolved, Merge, ParameterValue, ParameterView, ParameterStorage, Call };
  Kind Tag = Kind::Value;
  bool ReadOnly = false;
  bool MayBeNull = false;
  bool NonNull = false;
  bool UnsafeConstruction = false;
  unsigned Parameter = 0;
  FunctionDecl *Function = nullptr;
  std::vector<AccessPath> Origins;
  std::vector<std::shared_ptr<const RawAddressSource>> Inputs;
  std::vector<std::shared_ptr<const RawAddressSource>> ArgumentValues;
  std::vector<std::shared_ptr<const RawAddressSource>> ArgumentViews;
  std::vector<std::shared_ptr<const RawAddressSource>> ArgumentStorage;
  std::shared_ptr<const RawAllocationAncestry> AllocationAncestry;
};
using RawAddressSourcePtr = std::shared_ptr<const RawAddressSource>;

struct UnsafeRawConstructionPlan {
  const CastExpr *Site = nullptr;
  const Expr *SourceEdge = nullptr;
  std::string SourceType;
  std::string TargetType;
  RawWriteAuthority Authority = RawWriteAuthority::None;
  bool Prepared = false;
  bool SemaValidated = false;
  bool RestrictionsComplete = false;
  bool Nullable = false;
  std::string Rejection;
  std::vector<std::string> KnownOrigins;
};
}
