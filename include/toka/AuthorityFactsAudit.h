// Copyright (c) 2026 YiZhonghua<zhyi@dpai.com>. All rights reserved.
#pragma once

#include "toka/AuthorityFacts.h"
#include "toka/SemanticEvidence.h"
#include <iosfwd>
#include <map>
#include <optional>

namespace toka {

enum class AuthorityFaultPoint : uint8_t {
  AfterFullExpressionIdentity,
  AfterObservationIdentity,
  AfterPlaceReverseLookup,
  AfterCleanupClassLookup,
  AfterCleanupFactBuild,
  BeforeRevisionValidation,
  BeforeSwap,
};

const char *toString(AuthorityFaultPoint value);
std::optional<AuthorityFaultPoint>
parseAuthorityFaultPoint(const std::string &value);

struct AuthorityParentSentinel {
  uint32_t NextNodeSerial = 0;
  uint64_t NextSymbolID = 0;
  int DiagnosticErrorCount = 0;
  size_t DiagnosticRecordCount = 0;
  SemanticEvidenceAuditState Evidence;
  size_t CopyProofCount = 0;
  std::vector<std::string> CopyProofFacts;
  size_t ShapePropertyCount = 0;
  std::vector<std::string> ShapePropertyFacts;
  size_t CleanupClassCount = 0;
  std::vector<std::string> CleanupClassFacts;
  std::string FullExpressionIdentity;
  uint64_t SourceSymbolID = 0;
  std::string SourcePlaceState;
  uint64_t SourceInitMask = 0;

  friend bool operator==(const AuthorityParentSentinel &lhs,
                         const AuthorityParentSentinel &rhs);
};

std::vector<std::string>
differingAuthorityParentFields(const AuthorityParentSentinel &before,
                               const AuthorityParentSentinel &after);

struct AuthorityObservationReceipt {
  std::string File;
  unsigned Line = 0;
  unsigned Column = 0;
  std::optional<FullExpressionId> FullExpression;
  std::optional<AuthorityObservationId> Observation;
  std::optional<AuthorityFactRecord> Record;
  std::optional<AuthorityExclusionReason> Exclusion;
  std::optional<AuthorityIndeterminateReason> Indeterminate;
  std::optional<AuthorityBuildError> Error;
  bool BuildParentUnchanged = false;
  bool PublishParentUnchanged = false;
  size_t RevisionSizeBefore = 0;
  size_t RevisionSizeAfter = 0;
  std::vector<std::string> BuildDifferences;
  std::vector<std::string> PublishDifferences;
};

class AuthorityFactsAuditSession final {
public:
  void setCleanupStore(CleanupClassStore store) {
    CleanupStore = std::move(store);
  }
  void
  setCleanupStoreQualification(bool buildUnchanged, bool publishUnchanged,
                               std::vector<std::string> buildDifferences,
                               std::vector<std::string> publishDifferences) {
    StoreBuildParentUnchanged = buildUnchanged;
    StorePublishParentUnchanged = publishUnchanged;
    StoreBuildDifferences = std::move(buildDifferences);
    StorePublishDifferences = std::move(publishDifferences);
  }
  const CleanupClassStore &cleanupStore() const { return CleanupStore; }
  void noteExclusion(AuthorityExclusionReason reason,
                     AuthorityObservationReceipt receipt);
  void noteIndeterminate(AuthorityIndeterminateReason reason,
                         AuthorityObservationReceipt receipt);
  void noteError(AuthorityBuildError error,
                 AuthorityObservationReceipt receipt);
  AuthorityBuildError publishRecord(const AuthorityFactRecord &record,
                                    const std::string &sourceFile);
  void appendReceipt(AuthorityObservationReceipt receipt) {
    append(std::move(receipt));
  }
  const std::optional<AuthorityFactsRevision> &revision() const {
    return Revision;
  }
  void setFaultPoint(std::optional<AuthorityFaultPoint> value) {
    FaultPoint = value;
  }
  void setFaultSourceSuffix(std::string value) {
    FaultSourceSuffix = std::move(value);
  }
  std::optional<AuthorityFaultPoint>
  takeFaultPoint(AuthorityFaultPoint point, const std::string &sourceFile) {
    if (FaultPoint != point ||
        (!FaultSourceSuffix.empty() &&
         (sourceFile.size() < FaultSourceSuffix.size() ||
          sourceFile.compare(sourceFile.size() - FaultSourceSuffix.size(),
                             FaultSourceSuffix.size(),
                             FaultSourceSuffix) != 0)))
      return std::nullopt;
    auto result = FaultPoint;
    FaultPoint.reset();
    return result;
  }
  void dumpJSON(std::ostream &out) const;

private:
  void append(AuthorityObservationReceipt receipt);
  CleanupClassStore CleanupStore;
  std::optional<AuthorityFactsRevision> Revision;
  std::optional<AuthorityFaultPoint> FaultPoint;
  std::string FaultSourceSuffix;
  std::map<AuthorityExclusionReason, size_t> Exclusions;
  std::map<AuthorityIndeterminateReason, size_t> Indeterminate;
  std::map<AuthorityBuildError, size_t> Errors;
  std::vector<AuthorityObservationReceipt> Receipts;
  bool StoreBuildParentUnchanged = false;
  bool StorePublishParentUnchanged = false;
  std::vector<std::string> StoreBuildDifferences;
  std::vector<std::string> StorePublishDifferences;
};

} // namespace toka
