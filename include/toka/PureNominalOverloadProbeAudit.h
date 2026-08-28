// Copyright (c) 2026 YiZhonghua<zhyi@dpai.com>. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
#pragma once

#include "toka/PureNominalOverloadProbe.h"
#include "toka/SemanticEvidence.h"
#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace toka {

enum class D4ProbeExclusionReason : uint8_t {
  WrongRoute,
  NonSourceBacked,
  NotOverloaded,
  ArityOrDefault,
  NonLocalOrNonLivePlace,
  NonDirectNominalActual,
  NonDirectNominalFormal,
  VariantOrRefinedNominal,
  PrimitiveOrAlias,
  AttributesOrConversion,
  GenericOrContextual,
  HandleOrPermission,
  ContractUnsupported,
  PALOrDependencyConflict,
  SourceHiddenOrIncomplete,
};

const char *toString(D4ProbeExclusionReason value);

struct D4ProbeLocation {
  std::string File;
  unsigned Line = 0;
  unsigned Column = 0;
};

struct D4ProbeAuditCandidate {
  std::string DeclarationIdentity;
  unsigned LegacyOrdinal = 0;
  std::string FormalNominalIdentity;
  bool Compatible = false;
};

struct D4FrozenMappingEntry {
  DeclarationId Declaration;
  unsigned LegacyOrdinal = 0;
  const void *DeclarationPointer = nullptr;
};

std::optional<D4ProbeInfrastructureError>
validateD4FrozenMapping(const std::vector<D4FrozenMappingEntry> &entries,
                        std::optional<DeclarationId> fallbackIdentity,
                        const void *fallbackPointer);

struct D4ProbeParentSentinel {
  uint32_t NextNodeSerial = 0;
  int DiagnosticErrorCount = 0;
  size_t DiagnosticRecordCount = 0;
  SemanticEvidenceAuditState Evidence;
  uint64_t SourceSymbolId = 0;
  uint64_t SourceInitMask = 0;
  bool SourceMoved = false;
  bool SourceUsed = false;
  bool SourceMutated = false;
  std::string SourcePlaceState;
  std::string SourcePALState;
  uintptr_t CallResolvedFunction = 0;
  uintptr_t ActualResolvedType = 0;
  size_t CopyProofCount = 0;
  size_t CopyFactCount = 0;
  size_t InstantiationCount = 0;
  size_t GenericShapeCount = 0;
  size_t D3ConsideredCount = 0;
  size_t D3FactoryCount = 0;

  friend bool operator==(const D4ProbeParentSentinel &lhs,
                         const D4ProbeParentSentinel &rhs);
};

std::vector<std::string>
differingD4ProbeParentFields(const D4ProbeParentSentinel &before,
                             const D4ProbeParentSentinel &after);

struct D4ProbeAuditRecord {
  D4ProbeLocation Location;
  std::string Callee;
  std::optional<D4ProbeExclusionReason> Exclusion;
  std::vector<D4ProbeAuditCandidate> Candidates;
  std::optional<D4ProbeDisposition> Disposition;
  std::optional<D4LegacyReason> LegacyReason;
  std::optional<std::string> SelectedDeclarationIdentity;
  std::optional<std::string> ForcedLegacySelectedDeclarationIdentity;
  size_t CandidateDiagnosticCount = 0;
  size_t FinalLegacyCheckCount = 0;
  bool ParentUnchanged = true;
  std::vector<std::string> DifferingParentFields;
};

class D4ProbeAuditSession final {
public:
  explicit D4ProbeAuditSession(bool forceLegacy = false)
      : ForceLegacy(forceLegacy) {}

  bool forceLegacy() const { return ForceLegacy; }
  void setForceLegacy(bool value) { ForceLegacy = value; }
  bool reverseSchedule() const { return ReverseSchedule; }
  void setReverseSchedule(bool value) { ReverseSchedule = value; }
  size_t append(D4ProbeAuditRecord record, const void *callKey = nullptr);
  void noteFinalLegacyCheck(const void *callKey);
  void setForcedLegacySelection(size_t recordIndex,
                                std::optional<std::string> declaration,
                                size_t diagnosticCount);
  void noteInfrastructureError(D4ProbeInfrastructureError error);
  size_t attemptedBatchCount() const { return AttemptedBatchCount; }
  size_t pureBatchCount() const { return PureBatchCount; }
  void dumpJSON(std::ostream &out) const;

private:
  bool ForceLegacy = false;
  bool ReverseSchedule = false;
  size_t AttemptedBatchCount = 0;
  size_t PureBatchCount = 0;
  std::map<D4ProbeExclusionReason, size_t> ExcludedCounts;
  std::map<D4LegacyReason, size_t> LegacyRequiredCounts;
  std::map<D4ProbeInfrastructureError, size_t> InfrastructureErrors;
  std::vector<D4ProbeAuditRecord> Records;
  std::map<const void *, size_t> PendingFinalChecks;
};

} // namespace toka
