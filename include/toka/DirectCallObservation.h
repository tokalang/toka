// Copyright (c) 2026 YiZhonghua<zhyi@dpai.com>. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
#pragma once

#include "toka/SemanticModel.h"
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace toka {

enum class D3AdmissionKind : uint8_t { Admitted, NotInSlice, Rejected };

enum class D3ExclusionReason : uint8_t {
  Generic,
  VariadicOrDefault,
  MultipleArguments,
  InitOrOutcome,
  AsyncOrExecutionBoundary,
  ReturnDependencyOrRegionEscape,
  NestedObservation,
  Projection,
  SharedIdentity,
  RawOrReferenceIdentity,
  FunctionOrDynIdentity,
  NonCedeScalar,
  NonCedeAggregateTemporary,
  UnsupportedTypeCategory,
};

enum class D3CallValidationError : uint8_t {
  LegacyTypeMismatch,
  BorrowedFormalExplicitCede,
  IndeterminateCopyProof,
  IndeterminateOwnership,
  InvalidWholePlaceAdmission,
  IncompleteObservationFacts,
};

enum class D3ActualCategory : uint8_t {
  Indeterminate,
  WholePlace,
  WholeTemporary,
  Projection,
};

enum class D3TypeCategory : uint8_t {
  Indeterminate,
  Scalar,
  Aggregate,
  BorrowedAggregate,
  OwnedIdentity,
  SharedIdentity,
  RawOrReferenceIdentity,
  FunctionOrDynIdentity,
  Unsupported,
};

enum class D3CopyProof : uint8_t {
  ProvenCopy,
  ProvenNonCopy,
  Indeterminate,
};

enum class D3TransferMode : uint8_t {
  BorrowCapture,
  CopyValue,
  MoveOwned,
  ConsumeTemporary,
};

enum class D3SourceDisposition : uint8_t {
  KeepLive,
  InvalidateWhole,
  NoSourcePlace,
};

enum class D3StateDomain : uint8_t {
  Evaluation,
  PlaceState,
  PAL,
  Dependency,
  CleanupLiability,
  RegionObligation,
};

enum class D3DeltaLane : uint8_t {
  Evaluation,
  Boundary,
  Finalization,
};

enum class D3PatchBuildError : uint8_t {
  None,
  InvalidIdentity,
  ConflictingPayload,
};

const char *toString(D3AdmissionKind value);
const char *toString(D3ExclusionReason value);
const char *toString(D3CallValidationError value);
const char *toString(D3ActualCategory value);
const char *toString(D3TypeCategory value);
const char *toString(D3CopyProof value);
const char *toString(D3TransferMode value);
const char *toString(D3SourceDisposition value);
const char *toString(D3StateDomain value);
const char *toString(D3DeltaLane value);

struct D3SourceLocation {
  std::string File;
  unsigned Line = 0;
  unsigned Column = 0;

  friend bool operator==(const D3SourceLocation &lhs,
                         const D3SourceLocation &rhs) {
    return lhs.File == rhs.File && lhs.Line == rhs.Line &&
           lhs.Column == rhs.Column;
  }
};

// Immutable-by-convention integration DTOs. They contain no AST/Sema/PAL
// objects and are the pure factory's only input.
struct D3PreLegacyDirectCallFacts {
  std::string IdentityOrigin;
  std::string CallWitness;
  std::string CalleeWitness;
  std::string FormalWitness;
  std::string SourceWitness;
  std::string DestinationWitness;
  std::string CalleeName;
  std::string FormalName;
  std::string FormalType;
  std::string SourceSpelling;
  std::string SourceStateBefore;
  std::string PALStateBefore;
  D3SourceLocation CallLocation;
  D3SourceLocation FormalLocation;
  D3ActualCategory ActualCategory = D3ActualCategory::Indeterminate;
  bool CoreFactsComplete = false;
  bool FormalCeded = false;
  bool ExplicitCede = false;
  bool Generic = false;
  bool VariadicOrDefault = false;
  bool MultipleArguments = false;
  bool InitOrOutcome = false;
  bool AsyncOrExecutionBoundary = false;
  bool ReturnDependencyOrRegionEscape = false;
  bool NestedObservation = false;
  bool SourcePlaceAlias = false;
};

struct D3PostLegacyDirectCallFacts {
  std::string ActualType;
  std::vector<std::string> LegacyDiagnosticCodes;
  D3TypeCategory TypeCategory = D3TypeCategory::Indeterminate;
  D3CopyProof CopyProof = D3CopyProof::Indeterminate;
  bool LegacySucceeded = false;
  bool LegacyTypeMismatch = false;
  bool AdmissionFactsComplete = false;
  bool WholePlaceEligible = false;
  bool LiabilityComplete = false;
  bool RegionFactsComplete = false;
};

struct D3CallObservationInput {
  D3PreLegacyDirectCallFacts Pre;
  D3PostLegacyDirectCallFacts Post;
};

class D3ValidatedTransferEdge final {
public:
  const TransferEdgeId &id() const { return Id; }
  const ArgumentPlanId &argumentPlanId() const { return ArgumentPlan; }
  const FormalId &formalId() const { return Formal; }
  const DestinationId &destinationId() const { return Destination; }
  const std::optional<PlaceId> &sourcePlace() const { return SourcePlace; }
  const std::string &typeProof() const { return TypeProof; }
  const std::string &valueCategory() const { return ValueCategory; }
  D3TransferMode transferMode() const { return Transfer; }
  D3SourceDisposition sourceDisposition() const { return Source; }
  const std::string &dependency() const { return Dependency; }
  const std::string &liabilitySource() const { return LiabilitySource; }
  const std::string &liabilityTarget() const { return LiabilityTarget; }
  bool explicitCede() const { return ExplicitCede; }

  size_t hashValue() const noexcept;
  friend bool operator==(const D3ValidatedTransferEdge &lhs,
                         const D3ValidatedTransferEdge &rhs);

private:
  friend class DirectCallObservationFactory;
  TransferEdgeId Id;
  ArgumentPlanId ArgumentPlan;
  FormalId Formal;
  DestinationId Destination;
  std::optional<PlaceId> SourcePlace;
  std::string TypeProof;
  std::string ValueCategory;
  D3TransferMode Transfer = D3TransferMode::BorrowCapture;
  D3SourceDisposition Source = D3SourceDisposition::KeepLive;
  std::string Dependency;
  std::string LiabilitySource;
  std::string LiabilityTarget;
  bool ExplicitCede = false;
};

class D3DeltaEntry final {
public:
  const TransferEdgeId &edgeId() const { return Edge; }
  D3StateDomain stateDomain() const { return Domain; }
  const std::string &subjectIdentity() const { return SubjectIdentity; }
  const std::string &expectedBefore() const { return ExpectedBefore; }
  const std::string &resultAfter() const { return ResultAfter; }
  const std::string &provenance() const { return Provenance; }

  size_t hashValue() const noexcept;
  friend bool operator==(const D3DeltaEntry &lhs, const D3DeltaEntry &rhs);

private:
  friend class DirectCallObservationFactory;
  TransferEdgeId Edge;
  D3StateDomain Domain = D3StateDomain::Evaluation;
  std::string SubjectIdentity;
  std::string ExpectedBefore;
  std::string ResultAfter;
  std::string Provenance;
};

class D3DomainDelta final {
public:
  D3DeltaLane lane() const { return Lane; }
  const std::vector<D3DeltaEntry> &entries() const { return Entries; }
  size_t hashValue() const noexcept;
  friend bool operator==(const D3DomainDelta &lhs, const D3DomainDelta &rhs);

private:
  friend class DirectCallObservationFactory;
  D3DeltaLane Lane = D3DeltaLane::Evaluation;
  std::vector<D3DeltaEntry> Entries;
};

struct D3PatchEntrySeed {
  PatchEntryId Identity;
  std::string Payload;
};

struct D3PatchBuildResult;

class D3SemanticModelPatch final {
public:
  const std::vector<D3PatchEntrySeed> &entries() const { return Entries; }
  size_t hashValue() const noexcept;
  friend bool operator==(const D3SemanticModelPatch &lhs,
                         const D3SemanticModelPatch &rhs);

private:
  friend struct D3PatchBuildResult;
  friend D3PatchBuildResult
  buildD3SemanticModelPatch(std::vector<D3PatchEntrySeed> entries);
  std::vector<D3PatchEntrySeed> Entries;
};

struct D3PatchBuildResult {
  D3SemanticModelPatch Patch;
  D3PatchBuildError Error = D3PatchBuildError::None;
  explicit operator bool() const { return Error == D3PatchBuildError::None; }
};

D3PatchBuildResult
buildD3SemanticModelPatch(std::vector<D3PatchEntrySeed> entries);

class D3MinimalRegionWitness final {
public:
  const RegionId &callRegion() const { return CallRegion; }
  const RegionId &fullExpressionRegion() const { return FullExpressionRegion; }
  const std::string &origin() const { return Origin; }
  const std::string &subject() const { return Subject; }
  const std::string &terminal() const { return Terminal; }
  size_t hashValue() const noexcept;
  friend bool operator==(const D3MinimalRegionWitness &lhs,
                         const D3MinimalRegionWitness &rhs);

private:
  friend class DirectCallObservationFactory;
  RegionId CallRegion;
  RegionId FullExpressionRegion;
  std::string Origin;
  std::string Subject;
  std::string Terminal;
};

class D3ValidatedCall final {
public:
  const SemanticNodeId &callSiteId() const { return CallSite; }
  const ResolvedCalleeId &calleeId() const { return Callee; }
  const std::vector<D3ValidatedTransferEdge> &transferEdges() const {
    return TransferEdges;
  }
  const D3DomainDelta &evaluationDelta() const { return Evaluation; }
  const D3DomainDelta &boundaryDelta() const { return Boundary; }
  const D3DomainDelta &finalizationDelta() const { return Finalization; }
  const D3SemanticModelPatch &semanticModelPatch() const { return ModelPatch; }
  const D3MinimalRegionWitness &regionWitness() const { return RegionWitness; }
  size_t hashValue() const noexcept;
  friend bool operator==(const D3ValidatedCall &lhs,
                         const D3ValidatedCall &rhs);

private:
  friend class DirectCallObservationFactory;
  SemanticNodeId CallSite;
  ResolvedCalleeId Callee;
  std::vector<D3ValidatedTransferEdge> TransferEdges;
  D3DomainDelta Evaluation;
  D3DomainDelta Boundary;
  D3DomainDelta Finalization;
  D3SemanticModelPatch ModelPatch;
  D3MinimalRegionWitness RegionWitness;
};

class D3FactoryObservationRecord final {
public:
  D3AdmissionKind admission() const { return Admission; }
  const std::optional<D3ExclusionReason> &exclusionReason() const {
    return ExclusionReason;
  }
  const std::optional<D3CallValidationError> &validationError() const {
    return ValidationError;
  }
  const std::optional<D3ValidatedCall> &validatedCall() const {
    return ValidatedCall;
  }
  const D3CallObservationInput &input() const { return Input; }
  size_t hashValue() const noexcept;
  friend bool operator==(const D3FactoryObservationRecord &lhs,
                         const D3FactoryObservationRecord &rhs);

private:
  friend class DirectCallObservationFactory;
  D3AdmissionKind Admission = D3AdmissionKind::Rejected;
  std::optional<D3ExclusionReason> ExclusionReason;
  std::optional<D3CallValidationError> ValidationError;
  std::optional<D3ValidatedCall> ValidatedCall;
  D3CallObservationInput Input;
};

class DirectCallObservationFactory final {
public:
  static D3FactoryObservationRecord observe(D3CallObservationInput input);
};

} // namespace toka

namespace std {
template <> struct hash<toka::D3ValidatedTransferEdge> {
  size_t operator()(const toka::D3ValidatedTransferEdge &value) const noexcept {
    return value.hashValue();
  }
};
template <> struct hash<toka::D3DeltaEntry> {
  size_t operator()(const toka::D3DeltaEntry &value) const noexcept {
    return value.hashValue();
  }
};
template <> struct hash<toka::D3SemanticModelPatch> {
  size_t operator()(const toka::D3SemanticModelPatch &value) const noexcept {
    return value.hashValue();
  }
};
template <> struct hash<toka::D3MinimalRegionWitness> {
  size_t operator()(const toka::D3MinimalRegionWitness &value) const noexcept {
    return value.hashValue();
  }
};
template <> struct hash<toka::D3FactoryObservationRecord> {
  size_t
  operator()(const toka::D3FactoryObservationRecord &value) const noexcept {
    return value.hashValue();
  }
};
} // namespace std
