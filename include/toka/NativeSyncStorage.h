#pragma once
#include <string>
#include <memory>
#include <cstdint>

namespace toka {
class Sema; class CodeGen; class Expr; class CallExpr; class FunctionDecl;
class ShapeDecl; class Type;
enum class NativeSyncFactoryKind : uint8_t { None, Mutex, RwMutex, CondVar };

// This is the initialized factory edge, not a nominal-type exemption and not
// yet a complete thread-publication witness. In particular, it cannot stand
// in for the owner's terminal cleanup or a guard acquisition/discharge plan.
// Only Sema seals it; CodeGen consumes the same exact edge and instantiated
// types. Source-hidden declarations do not serialize or regain this authority.
class NativeSyncFactoryPlan {
  friend class Sema;
  friend class CodeGen;
  NativeSyncFactoryPlan() = default;
  const CallExpr *Site = nullptr;
  const FunctionDecl *Declaration = nullptr;
  const FunctionDecl *OwnerDefinition = nullptr;
  const ShapeDecl *OwnerTemplate = nullptr;
  const Expr *Input = nullptr;
  std::shared_ptr<Type> OwnerType, ElementType;
  NativeSyncFactoryKind Kind = NativeSyncFactoryKind::None;
  bool Validated = false;
  bool ReplacementClosed = false;
public:
  // Read-only provenance comparison, not a permission or transfer grant.
  // Only a sealed direct-owner factory plan can answer positively.
  bool matchesOwnerView(const std::shared_ptr<Type> &view) const;
};
using NativeSyncFactoryPtr = std::shared_ptr<const NativeSyncFactoryPlan>;

// A type-level prerequisite only. Closed does NOT create an initialized slot,
// native resource, owner/guard relation or thread-publication authority.
enum class NativeClosedPayloadState { Closed, ExternalDependency, Incomplete };
struct NativeClosedPayloadResult {
  NativeClosedPayloadState State = NativeClosedPayloadState::Incomplete;
  std::string Path;
  std::string Reason;
  bool closed() const { return State == NativeClosedPayloadState::Closed; }
};
}
