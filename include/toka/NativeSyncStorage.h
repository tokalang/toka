#pragma once
#include <string>
#include <memory>
#include <cstdint>
#include <map>
#include <vector>

namespace toka {
class Sema; class CodeGen; class Expr; class CallExpr; class FunctionDecl;
class ShapeDecl; class Type; class NewExpr; class VariableDecl; class BinaryExpr;
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

// Private value-flow recipe. Unlike the sealed factory plan this may describe
// a generic body still being checked. It grants no thread/guard authority.
// A call edge is distinct from its provider's body edge, so two executions
// described by different caller sites do not acquire one lexical owner ID.
class NativeSyncOwnerCandidate {
  friend class Sema;
  friend class CodeGen;
  NativeSyncOwnerCandidate() = default;
  NativeSyncFactoryPtr Factory;
  const Expr *OwnerEdge = nullptr;
  const Expr *Allocation = nullptr;
  const FunctionDecl *Provider = nullptr;
  std::shared_ptr<Type> ValueType;
  std::shared_ptr<const NativeSyncOwnerCandidate> Parent;
  std::map<std::string, std::shared_ptr<const NativeSyncOwnerCandidate>> Children;
};
using NativeSyncOwnerCandidatePtr = std::shared_ptr<const NativeSyncOwnerCandidate>;

class NativeSyncOwnerWitness {
  friend class Sema;
  friend class CodeGen;
  NativeSyncOwnerWitness() = default;
  NativeSyncOwnerCandidatePtr Origin;
  std::shared_ptr<Type> ValueType, OwnerType, ElementType;
  const CallExpr *FactorySite = nullptr;
  const NewExpr *AllocationSite = nullptr;
  const FunctionDecl *OwnerDrop = nullptr;
  const FunctionDecl *Acquire = nullptr;
  const FunctionDecl *GuardDrop = nullptr;
  const FunctionDecl *GuardAccess = nullptr;
  const FunctionDecl *ReadAcquire = nullptr, *ReadGuardDrop = nullptr, *ReadGuardAccess = nullptr;
  const FunctionDecl *NotifyOne = nullptr, *NotifyAll = nullptr, *Wait = nullptr;
  NativeSyncFactoryKind Kind = NativeSyncFactoryKind::None;
  std::map<std::string, std::shared_ptr<const NativeSyncOwnerWitness>> Children;
  std::vector<const FunctionDecl *> CompositeOperations;
};
using NativeSyncOwnerWitnessPtr = std::shared_ptr<const NativeSyncOwnerWitness>;

class NativeSyncGuardOrigin {
  friend class Sema;
  friend class CodeGen;
  NativeSyncGuardOrigin() = default;
  NativeSyncOwnerWitnessPtr Owner;
  const Expr *AcquireSite = nullptr;
  const FunctionDecl *Access = nullptr;
  bool Writable = false;
  bool Outcome = true;
};
using NativeSyncGuardOriginPtr = std::shared_ptr<const NativeSyncGuardOrigin>;

class NativeSyncReplacementPlan {
  friend class Sema;
  friend class CodeGen;
  NativeSyncReplacementPlan() = default;
  const BinaryExpr *Site = nullptr;
  const Expr *Destination = nullptr, *Source = nullptr;
  NativeSyncGuardOriginPtr Guard;
  std::shared_ptr<Type> ElementType;
  bool ManagedHandle = false;
  const Expr *ReferenceBinding = nullptr;
  std::shared_ptr<Type> ReferenceType;
};

// Cleanup for allocation of the managed wrapper around an already prepared
// native owner. This is not a general allocation/unwind protocol.
class NativeSyncAllocationPlan {
  friend class Sema;
  friend class CodeGen;
  NativeSyncAllocationPlan() = default;
  const NewExpr *Allocation = nullptr;
  const VariableDecl *Binding = nullptr;
  const FunctionDecl *Definition = nullptr;
  const Expr *PreparedOwner = nullptr;
  std::shared_ptr<Type> OwnerType, ManagedType;
  struct FieldCleanup {
    size_t Index = 0;
    std::string Name;
    std::shared_ptr<Type> Type;
    std::shared_ptr<toka::Type> DeclaredType;
    NativeSyncOwnerCandidatePtr Recipe;
    NativeSyncOwnerWitnessPtr Witness;
  };
  const ShapeDecl *CompositeDeclaration = nullptr;
  // All declaration fields are represented. Primitive fields have no recipe
  // or cleanup; native fields require a separately sealed child witness.
  std::vector<FieldCleanup> Fields;
  bool Complete = false;
};

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
