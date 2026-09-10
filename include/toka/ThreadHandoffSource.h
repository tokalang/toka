#pragma once
#include <memory>
#include <string>
#include <cstdint>
#include <vector>
#include "toka/NativeSyncStorage.h"
namespace toka {
class Sema; class CodeGen; class Type; class CallExpr; class Expr; class FunctionDecl;
enum class ThreadProbeKind : uint8_t { None, Run, Discard, RunAndDrop };
enum class PublicThreadKind : uint8_t { None, Spawn, SpawnState, Join, Detach, Drop };
class PublicThreadPlan {
  friend class Sema;
  friend class CodeGen;
  PublicThreadPlan() = default;
  const CallExpr *Site = nullptr;
  const FunctionDecl *Declaration = nullptr;
  const FunctionDecl *OwnerDefinition = nullptr;
  const FunctionDecl *Invoke = nullptr;
  const Expr *Callable = nullptr, *State = nullptr, *Handle = nullptr;
  // Fresh closure construction may materialize directly in the owning
  // packet. No pre-existing boxed identity or aliases are elided.
  const Expr *EnvironmentConstruction = nullptr;
  std::shared_ptr<Type> CallableType, EnvironmentType, StateType, ResultType;
  std::shared_ptr<Type> OutputType, HandleType, ErrorType;
  PublicThreadKind Kind = PublicThreadKind::None;
  bool Dynamic = false, Consuming = false, StateHasDrop = false, ResultHasDrop = false;
  bool Complete = false;
  int64_t OkTag = -1, ErrTag = -1;
  std::string ResultIdentity;
  std::vector<NativeSyncOwnerWitnessPtr> NativeOwners;
  size_t NativeOwnerCount = 0;
};
// Only the real Sema producer can construct/seal this carrier. Tests exercise
// source programs and fault injection, not writable qualification booleans.
class ThreadHandoffSourcePlan {
  friend class Sema;
  friend class CodeGen;
  ThreadHandoffSourcePlan() = default;
  const CallExpr *Site = nullptr;
  const Expr *Source = nullptr;
  const FunctionDecl *Declaration = nullptr;
  const FunctionDecl *InvokeDeclaration = nullptr;
  std::shared_ptr<Type> CallableType, ResultType;
  ThreadProbeKind Kind = ThreadProbeKind::None;
  bool Consuming = false;
  bool ResultHasDrop = false;
  bool Complete = false;
  std::string ResultIdentity;
};
}
