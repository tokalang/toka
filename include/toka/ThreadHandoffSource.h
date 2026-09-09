#pragma once
#include <memory>
#include <string>
#include <cstdint>
namespace toka {
class Sema; class CodeGen; class Type; class CallExpr; class Expr; class FunctionDecl;
enum class ThreadProbeKind : uint8_t { None, Run, Discard, RunAndDrop };
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
