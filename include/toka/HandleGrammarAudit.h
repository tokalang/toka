// Copyright (c) 2026 YiZhonghua<zhyi@dpai.com>. All rights reserved.
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

#include "toka/DiagnosticEngine.h"
#include "toka/SourceLocation.h"
#include "toka/SourceManager.h"
#include "toka/Type.h"
#include <string>
#include <vector>
#include <map>
#include <set>
#include <fstream>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <algorithm>
#include <cstdint>
#ifdef _WIN32
#include <process.h>
#define getpid _getpid
#else
#include <unistd.h>
#endif
#include <chrono>

namespace toka {

class FunctionDecl;

enum class SyntaxOrigin {
  SourceSurface, // Explicit source syntax in AST (TypeSyntax, Shape field, Fn signature, Let binding)
  TKIImport,     // Imported interface signature from compiled .tki
  Generated      // Synthesized/injected by compiler AST transformations (e.g. synthetic closures, wrapper thunks)
};

inline const char *syntaxOriginToString(SyntaxOrigin origin) {
  switch (origin) {
  case SyntaxOrigin::SourceSurface:
    return "SourceSurface";
  case SyntaxOrigin::TKIImport:
    return "TKIImport";
  case SyntaxOrigin::Generated:
    return "Generated";
  }
  return "Unknown";
}

enum class FormationPhase {
  DirectResolution,      // Ordinary non-generic declaration/type resolution
  AliasExpansion,        // Type alias expansion
  GenericInstance,       // Monomorphization / generic substitution of shapes, impls, or functions
  IntermediateLowering,  // Intermediate AST expression evaluation / unary deref / temporary cast
  CodeGen                // Type lowered to LLVM IR (getLLVMType)
};

inline const char *formationPhaseToString(FormationPhase phase) {
  switch (phase) {
  case FormationPhase::DirectResolution:
    return "DirectResolution";
  case FormationPhase::AliasExpansion:
    return "AliasExpansion";
  case FormationPhase::GenericInstance:
    return "GenericInstance";
  case FormationPhase::IntermediateLowering:
    return "IntermediateLowering";
  case FormationPhase::CodeGen:
    return "CodeGen";
  }
  return "Unknown";
}

enum class ReachabilityStatus {
  Unknown,
  Reachable,
  Unreachable
};

inline const char *reachabilityStatusToString(ReachabilityStatus status) {
  switch (status) {
  case ReachabilityStatus::Unknown:
    return "Unknown";
  case ReachabilityStatus::Reachable:
    return "Reachable";
  case ReachabilityStatus::Unreachable:
    return "Unreachable";
  }
  return "Unknown";
}

enum class CodeGenStatus {
  Unknown,
  Lowered,
  NotLowered
};

inline const char *codeGenStatusToString(CodeGenStatus status) {
  switch (status) {
  case CodeGenStatus::Unknown:
    return "Unknown";
  case CodeGenStatus::Lowered:
    return "Lowered";
  case CodeGenStatus::NotLowered:
    return "NotLowered";
  }
  return "Unknown";
}

enum class AuditDecision {
  Observed,
  RejectedSFINAE,
  RejectedSource
};

inline const char *auditDecisionToString(AuditDecision d) {
  switch (d) {
  case AuditDecision::Observed:
    return "Observed";
  case AuditDecision::RejectedSFINAE:
    return "RejectedSFINAE";
  case AuditDecision::RejectedSource:
    return "RejectedSource";
  }
  return "Observed";
}

struct HandleGrammarAuditEntry {
  std::string CanonicalKey;
  std::string SourceLoc;
  std::string TemplateOrShapeName;
  std::string GenericArgs;
  std::string MemberOrMethodName;
  std::string ConcreteTypeString;
  std::string CanonicalTypeIdentity;
  HandleGrammarProfile Profile;
  SyntaxOrigin Origin = SyntaxOrigin::SourceSurface;
  std::set<FormationPhase> Phases;
  std::string CanonicalFnId;
  ReachabilityStatus Reachability = ReachabilityStatus::Unknown;
  CodeGenStatus EnclosingFunctionCodeGen = CodeGenStatus::Unknown;
  bool IsLLVMTypeLowered = false;
  bool IsMethodInstantiated = false;
  AuditDecision Decision = AuditDecision::Observed;
  bool IsTransient = false;
  bool IsAdmitted = false;

  void addPhase(FormationPhase phase) {
    Phases.insert(phase);
  }
};

class HandleGrammarAuditRecorder {
public:
  bool Enabled = false;
  bool Flushed = false;
  std::string OutputPath;
  std::string OutputDir;

  // Global Counters
  uint64_t TotalTypesFormed = 0;
  uint64_t ValidProfiles = 0;
  uint64_t InvalidProfiles = 0;
  uint64_t TotalMethodsInstantiated = 0;
  uint64_t ReachableMethods = 0;
  uint64_t CodeGenLoweredMethods = 0;

  std::map<std::string, HandleGrammarAuditEntry> Entries;
  // Canonical function identifier -> list of Entry CanonicalKeys
  std::map<std::string, std::vector<std::string>> FunctionToEntriesMap;
  std::set<std::string> ReachableFunctions;
  std::set<std::string> CodeGenLoweredFunctions;

  HandleGrammarAuditRecorder() {
    const char *dir = std::getenv("TOKA_HANDLE_GRAMMAR_AUDIT_DIR");
    const char *path = std::getenv("TOKA_HANDLE_GRAMMAR_AUDIT_LOG");
    const char *env = std::getenv("TOKA_HANDLE_GRAMMAR_AUDIT");
    if (dir && dir[0] != '\0') {
      Enabled = true;
      OutputDir = dir;
      auto now = std::chrono::steady_clock::now().time_since_epoch().count();
      OutputPath = OutputDir + "/audit_" + std::to_string(getpid()) + "_" + std::to_string(now) + ".jsonl";
    } else if (path && path[0] != '\0') {
      Enabled = true;
      OutputPath = path;
    } else if (env && (std::string(env) == "1" || std::string(env) == "true")) {
      Enabled = true;
      OutputPath = "tmp/handle_grammar_audit.jsonl";
    }
  }

  static HandleGrammarAuditRecorder &instance() {
    static HandleGrammarAuditRecorder rec;
    return rec;
  }

  static std::string formatLocation(SourceLocation loc) {
    if (!loc.isValid())
      return "unknown";
    if (DiagnosticEngine::SrcMgr) {
      FullSourceLoc full = DiagnosticEngine::SrcMgr->getFullSourceLoc(loc);
      if (full.isValid()) {
        return std::string(full.FileName) + ":" + std::to_string(full.Line) + ":" + std::to_string(full.Column);
      }
    }
    return "raw_loc:" + std::to_string(loc.getRawEncoding());
  }

  void record(const std::shared_ptr<Type> &type,
              SyntaxOrigin origin,
              const std::vector<FormationPhase> &phases,
              const std::string &templateOrShapeName = "",
              const std::string &genericArgs = "",
              const std::string &memberOrMethodName = "",
              SourceLocation loc = SourceLocation(),
              bool isMethodInstantiated = false,
              const std::string &canonicalFnId = "") {
    if (!Enabled || !type)
      return;

    TotalTypesFormed++;
    auto profile = Type::classifyHandleGrammar(type);
    if (profile.isValid()) {
      ValidProfiles++;
      return;
    }

    InvalidProfiles++;
    std::string locStr = formatLocation(loc);
    std::string typeStr = type->toString();
    std::string typeId = type->canonicalIdentity();
    std::string originStr = syntaxOriginToString(origin);
    std::string violationStr = profile.describeViolation();
    std::string key = originStr + "|" + locStr + "|" + templateOrShapeName + "|" + genericArgs + "|" + memberOrMethodName + "|" + typeStr + "|" + typeId + "|" + violationStr + "|" + canonicalFnId;

    auto it = Entries.find(key);
    if (it == Entries.end()) {
      HandleGrammarAuditEntry entry;
      entry.CanonicalKey = key;
      entry.SourceLoc = locStr;
      entry.TemplateOrShapeName = templateOrShapeName;
      entry.GenericArgs = genericArgs;
      entry.MemberOrMethodName = memberOrMethodName;
      entry.ConcreteTypeString = typeStr;
      entry.CanonicalTypeIdentity = typeId;
      entry.Profile = profile;
      entry.Origin = origin;
      for (auto ph : phases) entry.addPhase(ph);
      entry.CanonicalFnId = canonicalFnId;
      entry.IsMethodInstantiated = isMethodInstantiated;
      bool isTransient = false;
      for (auto ph : phases) {
        if (ph == FormationPhase::IntermediateLowering)
          isTransient = true;
      }
      entry.IsTransient = isTransient;
      if (isTransient) {
        entry.Decision = AuditDecision::Observed;
        entry.IsAdmitted = false;
      } else if (origin == SyntaxOrigin::Generated) {
        entry.Decision = AuditDecision::RejectedSFINAE;
        entry.IsAdmitted = false;
      } else {
        entry.Decision = AuditDecision::RejectedSource;
        entry.IsAdmitted = false;
      }
      if (canonicalFnId.empty()) {
        entry.Reachability = ReachabilityStatus::Unknown;
        entry.EnclosingFunctionCodeGen = CodeGenStatus::Unknown;
      } else {
        entry.Reachability = ReachableFunctions.count(canonicalFnId) ? ReachabilityStatus::Reachable : ReachabilityStatus::Unreachable;
        entry.EnclosingFunctionCodeGen = CodeGenLoweredFunctions.count(canonicalFnId) ? CodeGenStatus::Lowered : CodeGenStatus::NotLowered;
        FunctionToEntriesMap[canonicalFnId].push_back(key);
      }
      if (entry.Decision == AuditDecision::RejectedSource) {
        for (auto &pair : Entries) {
          if (pair.second.CanonicalTypeIdentity == typeId) {
            pair.second.Decision = AuditDecision::RejectedSource;
            pair.second.IsTransient = false;
          }
        }
      }
      Entries.emplace(key, entry);
    } else {
      for (auto ph : phases) {
        it->second.addPhase(ph);
        if (ph == FormationPhase::IntermediateLowering)
          it->second.IsTransient = true;
      }
      if (isMethodInstantiated) {
        it->second.IsMethodInstantiated = true;
        it->second.Decision = AuditDecision::Observed;
        it->second.IsAdmitted = true;
      }
      if (!canonicalFnId.empty()) {
        it->second.CanonicalFnId = canonicalFnId;
        FunctionToEntriesMap[canonicalFnId].push_back(key);
        if (ReachableFunctions.count(canonicalFnId))
          it->second.Reachability = ReachabilityStatus::Reachable;
        else if (it->second.Reachability == ReachabilityStatus::Unknown)
          it->second.Reachability = ReachabilityStatus::Unreachable;

        if (CodeGenLoweredFunctions.count(canonicalFnId))
          it->second.EnclosingFunctionCodeGen = CodeGenStatus::Lowered;
        else if (it->second.EnclosingFunctionCodeGen == CodeGenStatus::Unknown)
          it->second.EnclosingFunctionCodeGen = CodeGenStatus::NotLowered;
      }
    }
  }

  void markRejected(const std::string &typeId, const std::string &canonicalFnId = "") {
    if (!Enabled || typeId.empty())
      return;
    for (auto &pair : Entries) {
      if (pair.second.CanonicalTypeIdentity == typeId) {
        if (canonicalFnId.empty() || pair.second.CanonicalFnId.empty() || pair.second.CanonicalFnId == canonicalFnId) {
          pair.second.Decision = AuditDecision::RejectedSource;
          pair.second.IsTransient = false;
        }
      }
    }
  }

  void markTypeLowered(const std::shared_ptr<Type> &type, const std::string &canonicalFnId) {
    if (!Enabled || !type)
      return;
    std::string typeId = type->canonicalIdentity();
    for (auto &pair : Entries) {
      if (pair.second.CanonicalTypeIdentity == typeId) {
        if (canonicalFnId.empty() || pair.second.CanonicalFnId.empty() || pair.second.CanonicalFnId == canonicalFnId) {
          pair.second.IsLLVMTypeLowered = true;
          pair.second.addPhase(FormationPhase::CodeGen);
        }
      }
    }
  }

  void markFunctionInstantiated(const std::string &canonicalFnId) {
    if (!Enabled || canonicalFnId.empty())
      return;
    TotalMethodsInstantiated++;
  }

  void markFunctionReachable(const std::string &canonicalFnId) {
    if (!Enabled || canonicalFnId.empty())
      return;
    if (ReachableFunctions.insert(canonicalFnId).second) {
      ReachableMethods++;
    }
    auto it = FunctionToEntriesMap.find(canonicalFnId);
    if (it != FunctionToEntriesMap.end()) {
      for (const auto &key : it->second) {
        auto eIt = Entries.find(key);
        if (eIt != Entries.end()) {
          eIt->second.Reachability = ReachabilityStatus::Reachable;
        }
      }
    }
  }

  void markFunctionCodeGenLowered(const std::string &canonicalFnId) {
    if (!Enabled || canonicalFnId.empty())
      return;
    if (CodeGenLoweredFunctions.insert(canonicalFnId).second) {
      CodeGenLoweredMethods++;
    }
    auto it = FunctionToEntriesMap.find(canonicalFnId);
    if (it != FunctionToEntriesMap.end()) {
      for (const auto &key : it->second) {
        auto eIt = Entries.find(key);
        if (eIt != Entries.end()) {
          eIt->second.EnclosingFunctionCodeGen = CodeGenStatus::Lowered;
        }
      }
    }
  }

  void flush() {
    if (!Enabled || OutputPath.empty() || Flushed)
      return;
    Flushed = true;

    std::ofstream out(OutputPath, OutputDir.empty() ? std::ios::app : std::ios::out);
    if (!out.is_open())
      return;

    for (const auto &pair : Entries) {
      const auto &e = pair.second;
      out << "{\"key\":\"" << escapeJson(e.CanonicalKey)
          << "\",\"loc\":\"" << escapeJson(e.SourceLoc)
          << "\",\"syntax_origin\":\"" << syntaxOriginToString(e.Origin)
          << "\",\"phases\":[";
      bool firstPhase = true;
      for (auto ph : e.Phases) {
        if (!firstPhase) out << ",";
        out << "\"" << formationPhaseToString(ph) << "\"";
        firstPhase = false;
      }
      out << "],\"template\":\"" << escapeJson(e.TemplateOrShapeName)
          << "\",\"generic_args\":\"" << escapeJson(e.GenericArgs)
          << "\",\"member\":\"" << escapeJson(e.MemberOrMethodName)
          << "\",\"type\":\"" << escapeJson(e.ConcreteTypeString)
          << "\",\"type_id\":\"" << escapeJson(e.CanonicalTypeIdentity)
          << "\",\"violation\":\"" << e.Profile.describeViolation()
          << "\",\"managed_depth\":" << e.Profile.continuousManagedDepth
          << ",\"borrow_depth\":" << e.Profile.continuousBorrowDepth
          << ",\"raw_depth\":" << e.Profile.continuousRawDepth
          << ",\"fn_id\":\"" << escapeJson(e.CanonicalFnId)
          << "\",\"reachability\":\"" << reachabilityStatusToString(e.Reachability)
          << "\",\"enclosing_fn_codegen\":\"" << codeGenStatusToString(e.EnclosingFunctionCodeGen)
          << "\",\"llvm_type_lowered\":" << (e.IsLLVMTypeLowered ? "true" : "false")
          << ",\"instantiated\":" << (e.IsMethodInstantiated ? "true" : "false")
          << ",\"decision\":\"" << auditDecisionToString(e.Decision) << "\""
          << ",\"is_transient\":" << (e.IsTransient ? "true" : "false")
          << ",\"is_admitted\":" << (e.IsAdmitted ? "true" : "false")
          << "}\n";
    }
  }

private:
  static std::string escapeJson(const std::string &s) {
    std::string res;
    for (char c : s) {
      if (c == '"') res += "\\\"";
      else if (c == '\\') res += "\\\\";
      else if (c == '\n') res += "\\n";
      else if (c == '\r') res += "\\r";
      else if (c == '\t') res += "\\t";
      else res += c;
    }
    return res;
  }
};

inline bool handleGrammarAuditEnabled() {
  return HandleGrammarAuditRecorder::instance().Enabled;
}

inline void recordHandleGrammarAudit(const std::shared_ptr<Type> &type,
                                    SyntaxOrigin origin,
                                    const std::vector<FormationPhase> &phases,
                                    const std::string &templateOrShapeName = "",
                                    const std::string &genericArgs = "",
                                    const std::string &memberOrMethodName = "",
                                    SourceLocation loc = SourceLocation(),
                                    bool isMethodInstantiated = false,
                                    const std::string &canonicalFnId = "") {
  HandleGrammarAuditRecorder::instance().record(type, origin, phases, templateOrShapeName, genericArgs, memberOrMethodName, loc, isMethodInstantiated, canonicalFnId);
}

inline void markHandleGrammarTypeLowered(const std::shared_ptr<Type> &type, const std::string &canonicalFnId = "") {
  HandleGrammarAuditRecorder::instance().markTypeLowered(type, canonicalFnId);
}

inline void markHandleGrammarRejected(const std::shared_ptr<Type> &type, const std::string &canonicalFnId = "") {
  if (type) {
    HandleGrammarAuditRecorder::instance().markRejected(type->canonicalIdentity(), canonicalFnId);
  }
}

inline void markHandleGrammarFunctionInstantiated(const std::string &canonicalFnId) {
  HandleGrammarAuditRecorder::instance().markFunctionInstantiated(canonicalFnId);
}

inline void markHandleGrammarFunctionReachable(const std::string &canonicalFnId) {
  HandleGrammarAuditRecorder::instance().markFunctionReachable(canonicalFnId);
}

inline void markHandleGrammarFunctionCodeGenLowered(const std::string &canonicalFnId) {
  HandleGrammarAuditRecorder::instance().markFunctionCodeGenLowered(canonicalFnId);
}

inline void flushHandleGrammarAudit() {
  HandleGrammarAuditRecorder::instance().flush();
}

} // namespace toka
