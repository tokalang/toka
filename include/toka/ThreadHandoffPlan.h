// Copyright (c) 2026 YiZhonghua<zhyi@dpai.com>. All rights reserved.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace llvm {
class Function;
class FunctionType;
class GlobalVariable;
class Module;
class StructType;
class Type;
}

namespace toka {
class Expr;

enum class ThreadCallableMode { Unresolved, Repeatable, Consuming };
enum class ThreadArgumentPassing { Value, Address };

struct ThreadAdapterArgument {
  unsigned PacketField = 0;
  ThreadArgumentPassing Passing = ThreadArgumentPassing::Value;
};

// Private, unactivated bridge carrier. The future Sema producer must attach
// these facts to the exact handoff edge after normal validation. None of the
// booleans may be inferred merely from a type name, @Send, or lack of errors.
// The isolated emitter tests populate synthetic facts; they do NOT qualify
// source-language thread entry, capture layout, or an actual Sema producer.
struct ThreadHandoffAdapterPlan {
  const Expr *Site = nullptr;
  const Expr *EnvironmentEdge = nullptr;
  bool SemaValidated = false;
  bool TransactionComplete = false;
  bool SpecializationQualified = false;
  bool EnvironmentDependenciesComplete = false;
  bool ResultDependenciesComplete = false;
  bool EnvironmentLifetimeAdmitted = false;
  bool ResultLifetimeAdmitted = false;
  bool SendValidated = false;
  bool CleanupComplete = false;
  bool RelocationValidated = false;
  bool InvokeNoUnwind = false;
  bool ConsumingEnvironmentUnique = false;
  bool ResultHasDrop = false;
  ThreadCallableMode Mode = ThreadCallableMode::Unresolved;
  std::string ABIKey;
  std::string ContractKey;
  std::string ResultTypeKey;

  // Compiler-lowered exact types/layout, never reconstructed by this emitter.
  llvm::StructType *PacketType = nullptr;
  unsigned CarrierField = 0;
  llvm::StructType *CarrierType = nullptr;
  unsigned EnvironmentField = 0;
  unsigned InvokeField = 1;
  llvm::FunctionType *InvokeType = nullptr;
  unsigned InvokeCallingConvention = 0;
  llvm::Type *ResultType = nullptr;
  bool ResultSRet = false;
  std::vector<ThreadAdapterArgument> Arguments;

  // Exact typed cleanup wrappers are supplied by existing CodeGen paths.
  // Both packet wrappers consume/free packet storage. AfterInvoke must use
  // release(false) for consuming dyn fn, complete drop for repeatable dyn fn.
  // No capture offset, reference-count algorithm, or Drop is guessed here.
  llvm::Function *DropUnstartedPacket = nullptr;
  llvm::Function *DropStartedPacket = nullptr;
  ThreadCallableMode StartedCleanupMode = ThreadCallableMode::Unresolved;
  llvm::Function *DropResult = nullptr;
};

struct ThreadHandoffAdapterArtifacts {
  llvm::Function *RunOnce = nullptr;
  llvm::Function *DropUnstarted = nullptr;
  llvm::Function *MoveOut = nullptr;
  llvm::Function *DropLive = nullptr;
  llvm::GlobalVariable *ResultOps = nullptr;
  llvm::GlobalVariable *EnvOps = nullptr;
};

// All validation happens before module mutation. Failure is an E0701 reason
// for the eventual CodeGen caller; this helper does not produce an artifact.
bool emitThreadHandoffAdapters(llvm::Module &module,
                              const ThreadHandoffAdapterPlan &plan,
                              const Expr *expectedSite,
                              const Expr *expectedEnvironment,
                              const std::string &symbolPrefix,
                              ThreadHandoffAdapterArtifacts &out,
                              std::string &rejection);
} // namespace toka
