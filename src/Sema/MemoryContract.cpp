// Copyright (c) 2025 YiZhonghua<zhyi@dpai.com>. All rights reserved.
//
// Licensed under the Apache License, Version 2.0.
#include "toka/MemoryContract.h"
#include "toka/AST.h"
#include "toka/MemorySummary.h"
#include "llvm/Analysis/CaptureTracking.h"
#include "llvm/IR/Argument.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/Transforms/Utils/PromoteMemToReg.h"
#include <algorithm>
#include <map>
#include <memory>
#include <ostream>
#include <set>
#include <tuple>

namespace toka {
namespace {

uint32_t bit(MemoryRootEffect effect) {
  return static_cast<uint32_t>(effect);
}

uint32_t bit(FunctionMemoryEffect effect) {
  return static_cast<uint32_t>(effect);
}

bool has(uint32_t effects, MemoryRootEffect effect) {
  return (effects & bit(effect)) != 0;
}

bool has(uint32_t effects, FunctionMemoryEffect effect) {
  return (effects & bit(effect)) != 0;
}

std::string rootName(const FunctionDecl::Arg &arg) {
  return Type::stripMorphology(arg.Name);
}

const llvm::Argument *findIRArgument(const llvm::Function *function,
                                     const std::string &name,
                                     unsigned parameterIndex) {
  if (!function)
    return nullptr;
  for (const llvm::Argument &argument : function->args())
    if (argument.getName() == name)
      return &argument;
  if (parameterIndex < function->arg_size())
    return function->getArg(parameterIndex);
  return nullptr;
}

llvm::Argument *findIRArgument(llvm::Function *function,
                               const std::string &name,
                               unsigned parameterIndex) {
  if (!function)
    return nullptr;
  for (llvm::Argument &argument : function->args())
    if (argument.getName() == name)
      return &argument;
  if (parameterIndex < function->arg_size())
    return function->getArg(parameterIndex);
  return nullptr;
}

MemoryContractReason commonRejection(
    const FunctionDecl &function, const llvm::Function *irFunction,
    const llvm::Argument *irArgument, bool borrowCheckEnabled) {
  const auto &summary = function.MemorySummary;
  if (summary.Origin == MemorySummaryOrigin::SignatureOnly)
    return MemoryContractReason::SignatureOnly;
  if (!irFunction ||
      (irFunction->isDeclaration() &&
       summary.Origin != MemorySummaryOrigin::TrustedCache))
    return MemoryContractReason::MissingIRFunction;
  if (!irArgument)
    return MemoryContractReason::MissingIRParameter;
  if (!irArgument->getType()->isPointerTy())
    return MemoryContractReason::NonPointerABI;
  if (!borrowCheckEnabled)
    return MemoryContractReason::BorrowCheckDisabled;
  if (has(summary.Effects, FunctionMemoryEffect::Suspend))
    return MemoryContractReason::SuspendBoundary;
  if (has(summary.Effects, FunctionMemoryEffect::UnsafeBoundary))
    return MemoryContractReason::UnsafeBoundary;
  if (has(summary.Effects, FunctionMemoryEffect::RawProvenance))
    return MemoryContractReason::RawProvenance;
  if (has(summary.Effects, FunctionMemoryEffect::UnknownBoundary))
    return MemoryContractReason::UnknownBoundary;
  return MemoryContractReason::ProvenBySummary;
}

MemoryContractRecord evaluate(const FunctionDecl &function,
                              const FunctionDecl::Arg &argument,
                              unsigned parameterIndex,
                              MemoryContractKind kind,
                              const llvm::Module &irModule,
                              const llvm::Module &captureModule,
                              bool borrowCheckEnabled) {
  const auto &summary = function.MemorySummary;
  std::string parameterName = rootName(argument);
  MemoryContractRecord record{summary.FunctionName, parameterName,
                              parameterIndex, kind,
                              MemoryContractDecision::Reject,
                              MemoryContractReason::UnknownRoot};
  if (kind == MemoryContractKind::NoAlias) {
    record.Reason = MemoryContractReason::SeparateNoAliasGate;
    return record;
  }
  const llvm::Function *irFunction =
      irModule.getFunction(summary.FunctionName);
  const llvm::Argument *irArgument =
      findIRArgument(irFunction, parameterName, parameterIndex);
  MemoryContractReason common = commonRejection(
      function, irFunction, irArgument, borrowCheckEnabled);
  if (common != MemoryContractReason::ProvenBySummary) {
    record.Reason = common;
    return record;
  }

  auto root = summary.Roots.find(parameterName);
  if (root == summary.Roots.end())
    return record;
  uint32_t effects = root->second.Effects;
  if (has(effects, MemoryRootEffect::Unknown))
    return record;

  auto reject = [&](MemoryContractReason reason) {
    record.Reason = reason;
    return record;
  };
  auto candidate = [&]() {
    record.Decision = MemoryContractDecision::Candidate;
    record.Reason =
        summary.Origin == MemorySummaryOrigin::TrustedCache
            ? MemoryContractReason::ProvenByTrustedCache
            : MemoryContractReason::ProvenBySummary;
    return record;
  };

  if (kind == MemoryContractKind::NoCapture) {
    if (has(effects, MemoryRootEffect::Transfer))
      return reject(MemoryContractReason::TransfersOwnership);
    if (has(effects, MemoryRootEffect::Capture))
      return reject(MemoryContractReason::CapturesRoot);
    if (has(effects, MemoryRootEffect::Escape))
      return reject(MemoryContractReason::EscapesRoot);
    if (summary.Origin != MemorySummaryOrigin::TrustedCache) {
      const llvm::Function *captureFunction =
          captureModule.getFunction(summary.FunctionName);
      const llvm::Argument *captureArgument =
          findIRArgument(captureFunction, parameterName, parameterIndex);
      if (!captureArgument ||
          llvm::PointerMayBeCaptured(captureArgument, true, true))
        return reject(MemoryContractReason::IRCaptureDetected);
    }
    return candidate();
  }

  if (kind == MemoryContractKind::ReadOnly) {
    if (has(effects, MemoryRootEffect::Transfer))
      return reject(MemoryContractReason::TransfersOwnership);
    if (has(effects, MemoryRootEffect::Invalidate))
      return reject(MemoryContractReason::InvalidatesRoot);
    if (has(effects, MemoryRootEffect::Rebind))
      return reject(MemoryContractReason::RebindsHandle);
    if (has(effects, MemoryRootEffect::Write))
      return reject(MemoryContractReason::WritesMemory);
    return candidate();
  }

  if (has(effects, MemoryRootEffect::Transfer))
    return reject(MemoryContractReason::TransfersOwnership);
  if (has(effects, MemoryRootEffect::Invalidate))
    return reject(MemoryContractReason::InvalidatesRoot);
  if (has(effects, MemoryRootEffect::Read))
    return reject(MemoryContractReason::ReadsMemory);
  if (!has(effects, MemoryRootEffect::Write))
    return reject(MemoryContractReason::NoWrites);
  return candidate();
}

std::unique_ptr<llvm::Module>
makeCaptureAnalysisModule(const llvm::Module &irModule) {
  std::unique_ptr<llvm::Module> captureModule = llvm::CloneModule(irModule);
  for (llvm::Function &function : *captureModule) {
    if (function.isDeclaration())
      continue;
    std::vector<llvm::AllocaInst *> promotableAllocas;
    for (llvm::BasicBlock &block : function)
      for (llvm::Instruction &instruction : block)
        if (auto *alloca = llvm::dyn_cast<llvm::AllocaInst>(&instruction);
            alloca && llvm::isAllocaPromotable(alloca))
          promotableAllocas.push_back(alloca);
    if (promotableAllocas.empty())
      continue;
    llvm::DominatorTree dominatorTree(function);
    llvm::PromoteMemToReg(promotableAllocas, dominatorTree);
  }
  return captureModule;
}

bool sameRecord(const MemoryContractRecord &lhs,
                const MemoryContractRecord &rhs) {
  return std::tie(lhs.FunctionName, lhs.ParameterName, lhs.ParameterIndex,
                  lhs.Kind, lhs.Decision, lhs.Reason, lhs.Emitted) ==
         std::tie(rhs.FunctionName, rhs.ParameterName, rhs.ParameterIndex,
                  rhs.Kind, rhs.Decision, rhs.Reason, rhs.Emitted);
}

bool hasEmittedAttribute(const llvm::Argument &argument,
                         MemoryContractKind kind) {
  switch (kind) {
  case MemoryContractKind::NoCapture:
    return argument.hasAttribute(llvm::Attribute::NoCapture);
  case MemoryContractKind::ReadOnly:
    return argument.hasAttribute(llvm::Attribute::ReadOnly);
  case MemoryContractKind::WriteOnly:
    return argument.hasAttribute(llvm::Attribute::WriteOnly);
  case MemoryContractKind::NoAlias:
    return argument.hasAttribute(llvm::Attribute::NoAlias);
  }
  return false;
}

std::string escapeJSON(const std::string &value) {
  std::string result;
  for (unsigned char ch : value) {
    switch (ch) {
    case '\\': result += "\\\\"; break;
    case '"': result += "\\\""; break;
    case '\n': result += "\\n"; break;
    case '\r': result += "\\r"; break;
    case '\t': result += "\\t"; break;
    default:
      if (ch >= 0x20)
        result += static_cast<char>(ch);
      break;
    }
  }
  return result;
}

} // namespace

MemoryContractShadow MemoryContractShadow::analyze(
    const std::vector<Module *> &modules, const llvm::Module &irModule,
    bool borrowCheckEnabled) {
  MemoryContractShadow result;
  std::unique_ptr<llvm::Module> captureModule =
      makeCaptureAnalysisModule(irModule);
  std::map<std::tuple<std::string, unsigned, MemoryContractKind>,
           MemoryContractRecord>
      merged;
  constexpr MemoryContractKind kinds[] = {
      MemoryContractKind::NoCapture, MemoryContractKind::ReadOnly,
      MemoryContractKind::WriteOnly, MemoryContractKind::NoAlias};
  for (FunctionDecl *function :
       MemorySummaryAnalysis::collectFunctions(modules)) {
    for (size_t i = 0; i < function->Args.size(); ++i) {
      for (MemoryContractKind kind : kinds) {
        MemoryContractRecord candidate =
            evaluate(*function, function->Args[i],
                     static_cast<unsigned>(i), kind, irModule, *captureModule,
                     borrowCheckEnabled);
        auto key = std::make_tuple(candidate.FunctionName,
                                   candidate.ParameterIndex, candidate.Kind);
        auto [position, inserted] = merged.emplace(key, candidate);
        if (inserted)
          continue;
        MemoryContractRecord &current = position->second;
        if (current.Decision == MemoryContractDecision::Candidate &&
            candidate.Decision == MemoryContractDecision::Reject) {
          current = candidate;
        } else if (current.Decision == MemoryContractDecision::Reject &&
                   candidate.Decision == MemoryContractDecision::Reject &&
                   candidate.Reason < current.Reason) {
          current = candidate;
        }
      }
    }
  }
  for (auto &entry : merged)
    result.Records.push_back(std::move(entry.second));
  std::sort(result.Records.begin(), result.Records.end(),
            [](const MemoryContractRecord &lhs,
               const MemoryContractRecord &rhs) {
              return std::tie(lhs.FunctionName, lhs.ParameterIndex, lhs.Kind,
                              lhs.ParameterName) <
                     std::tie(rhs.FunctionName, rhs.ParameterIndex, rhs.Kind,
                              rhs.ParameterName);
            });
  return result;
}

bool MemoryContractShadow::verify(const std::vector<Module *> &modules,
                                  const llvm::Module &irModule,
                                  bool borrowCheckEnabled,
                                  std::vector<std::string> &errors) const {
  MemoryContractShadow expected =
      analyze(modules, irModule, borrowCheckEnabled);
  if (Records.size() != expected.Records.size()) {
    errors.push_back("record count differs from a fresh shadow analysis");
  } else {
    for (size_t i = 0; i < Records.size(); ++i)
      if (!sameRecord(Records[i], expected.Records[i])) {
        errors.push_back("record differs from a fresh shadow analysis at index " +
                         std::to_string(i));
        break;
      }
  }

  std::set<std::tuple<std::string, unsigned, MemoryContractKind>> seen;
  std::set<std::string> irFunctionNames;
  for (const auto &record : Records) {
    auto key = std::make_tuple(record.FunctionName, record.ParameterIndex,
                               record.Kind);
    if (!seen.insert(key).second)
      errors.push_back(record.FunctionName + ": duplicate shadow contract");
    irFunctionNames.insert(record.FunctionName);
    if (record.Kind == MemoryContractKind::NoAlias &&
        record.Decision != MemoryContractDecision::Reject)
      errors.push_back(record.FunctionName +
                       ": noalias escaped its separate soundness gate");
  }
  constexpr MemoryContractKind kinds[] = {
      MemoryContractKind::NoCapture, MemoryContractKind::ReadOnly,
      MemoryContractKind::WriteOnly, MemoryContractKind::NoAlias};
  for (const auto &name : irFunctionNames) {
    const llvm::Function *function = irModule.getFunction(name);
    if (!function)
      continue;
    for (const llvm::Argument &argument : function->args())
      for (MemoryContractKind kind : kinds)
        if (hasEmittedAttribute(argument, kind))
          errors.push_back(name + ": shadow contract " + toString(kind) +
                           " was emitted on IR parameter " +
                           std::to_string(argument.getArgNo()));
  }
  return errors.empty();
}

unsigned MemoryContractShadow::emitExperimentalNoCapture(
    llvm::Module &irModule) {
  unsigned emitted = 0;
  for (auto &record : Records) {
    if (record.Kind != MemoryContractKind::NoCapture ||
        record.Decision != MemoryContractDecision::Candidate)
      continue;
    llvm::Function *function = irModule.getFunction(record.FunctionName);
    llvm::Argument *argument = findIRArgument(
        function, record.ParameterName, record.ParameterIndex);
    if (!argument || !argument->getType()->isPointerTy())
      continue;
    function->addParamAttr(argument->getArgNo(), llvm::Attribute::NoCapture);
    record.Emitted = true;
    ++emitted;
  }
  return emitted;
}

bool MemoryContractShadow::verifyExperimentalNoCapture(
    const llvm::Module &irModule, bool enabled,
    std::vector<std::string> &errors) const {
  for (const auto &record : Records) {
    const llvm::Function *function = irModule.getFunction(record.FunctionName);
    const llvm::Argument *argument =
        findIRArgument(function, record.ParameterName, record.ParameterIndex);
    bool expected = enabled &&
                    record.Kind == MemoryContractKind::NoCapture &&
                    record.Decision == MemoryContractDecision::Candidate;
    bool actual = argument &&
                  argument->hasAttribute(llvm::Attribute::NoCapture);
    if (record.Kind == MemoryContractKind::NoCapture && actual != expected)
      errors.push_back(record.FunctionName + ": nocapture emission mismatch for " +
                       record.ParameterName);
    if (record.Emitted != expected)
      errors.push_back(record.FunctionName +
                       ": nocapture emission evidence mismatch for " +
                       record.ParameterName);
  }
  return errors.empty();
}

void MemoryContractShadow::dumpJSON(std::ostream &out) const {
  out << "{\"schema\":\"toka.memory-contract-shadow\",\"version\":"
      << SchemaVersion << ",\"records\":[";
  for (size_t i = 0; i < Records.size(); ++i) {
    if (i)
      out << ',';
    const auto &record = Records[i];
    out << "{\"function\":\"" << escapeJSON(record.FunctionName)
        << "\",\"parameter\":\"" << escapeJSON(record.ParameterName)
        << "\",\"parameter_index\":" << record.ParameterIndex
        << ",\"contract\":\"" << toString(record.Kind)
        << "\",\"decision\":\"" << toString(record.Decision)
        << "\",\"reason\":\"" << toString(record.Reason)
        << "\",\"emitted\":" << (record.Emitted ? "true" : "false")
        << '}';
  }
  out << "]}\n";
}

const char *toString(MemoryContractKind value) {
  switch (value) {
  case MemoryContractKind::NoCapture: return "nocapture";
  case MemoryContractKind::ReadOnly: return "readonly";
  case MemoryContractKind::WriteOnly: return "writeonly";
  case MemoryContractKind::NoAlias: return "noalias";
  }
  return "noalias";
}

const char *toString(MemoryContractDecision value) {
  switch (value) {
  case MemoryContractDecision::Candidate: return "Candidate";
  case MemoryContractDecision::Reject: return "Reject";
  }
  return "Reject";
}

const char *toString(MemoryContractReason value) {
  switch (value) {
  case MemoryContractReason::ProvenBySummary: return "ProvenBySummary";
  case MemoryContractReason::ProvenByTrustedCache: return "ProvenByTrustedCache";
  case MemoryContractReason::MissingIRFunction: return "MissingIRFunction";
  case MemoryContractReason::MissingIRParameter: return "MissingIRParameter";
  case MemoryContractReason::SignatureOnly: return "SignatureOnly";
  case MemoryContractReason::NonPointerABI: return "NonPointerABI";
  case MemoryContractReason::BorrowCheckDisabled: return "BorrowCheckDisabled";
  case MemoryContractReason::SuspendBoundary: return "SuspendBoundary";
  case MemoryContractReason::UnsafeBoundary: return "UnsafeBoundary";
  case MemoryContractReason::RawProvenance: return "RawProvenance";
  case MemoryContractReason::UnknownBoundary: return "UnknownBoundary";
  case MemoryContractReason::UnknownRoot: return "UnknownRoot";
  case MemoryContractReason::IRCaptureDetected: return "IRCaptureDetected";
  case MemoryContractReason::ReadsMemory: return "ReadsMemory";
  case MemoryContractReason::NoWrites: return "NoWrites";
  case MemoryContractReason::WritesMemory: return "WritesMemory";
  case MemoryContractReason::RebindsHandle: return "RebindsHandle";
  case MemoryContractReason::InvalidatesRoot: return "InvalidatesRoot";
  case MemoryContractReason::CapturesRoot: return "CapturesRoot";
  case MemoryContractReason::EscapesRoot: return "EscapesRoot";
  case MemoryContractReason::TransfersOwnership: return "TransfersOwnership";
  case MemoryContractReason::SeparateNoAliasGate: return "SeparateNoAliasGate";
  }
  return "UnknownBoundary";
}

} // namespace toka
