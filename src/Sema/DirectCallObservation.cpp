// Copyright (c) 2026 YiZhonghua<zhyi@dpai.com>. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "toka/DirectCallObservation.h"
#include <algorithm>
#include <tuple>

namespace toka {
namespace {

size_t combine(size_t seed, size_t value) noexcept {
  return seed ^ (value + 0x9e3779b9U + (seed << 6U) + (seed >> 2U));
}

size_t hashText(size_t seed, const std::string &value) noexcept {
  return combine(seed, std::hash<std::string>{}(value));
}

template <typename Enum> size_t hashEnum(size_t seed, Enum value) noexcept {
  return combine(seed, static_cast<size_t>(value));
}

bool equalLocation(const D3SourceLocation &lhs, const D3SourceLocation &rhs) {
  return lhs == rhs;
}

bool equalPre(const D3PreLegacyDirectCallFacts &lhs,
              const D3PreLegacyDirectCallFacts &rhs) {
  return std::tie(lhs.IdentityOrigin, lhs.CallWitness, lhs.CalleeWitness,
                  lhs.FormalWitness, lhs.SourceWitness, lhs.DestinationWitness,
                  lhs.CallerBindingOwnerWitness, lhs.CalleeName, lhs.FormalName,
                  lhs.FormalType, lhs.SourceSpelling, lhs.SourceStateBefore,
                  lhs.PALStateBefore, lhs.ActualCategory, lhs.CoreFactsComplete,
                  lhs.FormalCeded, lhs.ExplicitCede, lhs.Generic,
                  lhs.VariadicOrDefault, lhs.MultipleArguments,
                  lhs.InitOrOutcome, lhs.AsyncOrExecutionBoundary,
                  lhs.ReturnDependencyOrRegionEscape, lhs.NestedObservation,
                  lhs.SourcePlaceAlias, lhs.SourceIsLocalPlace) ==
             std::tie(rhs.IdentityOrigin, rhs.CallWitness, rhs.CalleeWitness,
                      rhs.FormalWitness, rhs.SourceWitness,
                      rhs.DestinationWitness, rhs.CallerBindingOwnerWitness,
                      rhs.CalleeName, rhs.FormalName, rhs.FormalType,
                      rhs.SourceSpelling, rhs.SourceStateBefore,
                      rhs.PALStateBefore, rhs.ActualCategory,
                      rhs.CoreFactsComplete, rhs.FormalCeded, rhs.ExplicitCede,
                      rhs.Generic, rhs.VariadicOrDefault, rhs.MultipleArguments,
                      rhs.InitOrOutcome, rhs.AsyncOrExecutionBoundary,
                      rhs.ReturnDependencyOrRegionEscape, rhs.NestedObservation,
                      rhs.SourcePlaceAlias, rhs.SourceIsLocalPlace) &&
         equalLocation(lhs.CallLocation, rhs.CallLocation) &&
         equalLocation(lhs.FormalLocation, rhs.FormalLocation);
}

bool equalPost(const D3PostLegacyDirectCallFacts &lhs,
               const D3PostLegacyDirectCallFacts &rhs) {
  return std::tie(lhs.ActualType, lhs.LegacyDiagnosticCodes, lhs.TypeCategory,
                  lhs.CopyProof, lhs.OwnershipProof, lhs.BoundaryAccess,
                  lhs.SourceLiability, lhs.CleanupWitness, lhs.LegacySucceeded,
                  lhs.LegacyTypeMismatch, lhs.AdmissionFactsComplete,
                  lhs.WholePlaceEligible, lhs.LiabilityComplete,
                  lhs.RegionFactsComplete) ==
         std::tie(rhs.ActualType, rhs.LegacyDiagnosticCodes, rhs.TypeCategory,
                  rhs.CopyProof, rhs.OwnershipProof, rhs.BoundaryAccess,
                  rhs.SourceLiability, rhs.CleanupWitness, rhs.LegacySucceeded,
                  rhs.LegacyTypeMismatch, rhs.AdmissionFactsComplete,
                  rhs.WholePlaceEligible, rhs.LiabilityComplete,
                  rhs.RegionFactsComplete);
}

struct ProspectiveTransfer {
  D3TransferMode Transfer = D3TransferMode::BorrowCapture;
  D3SourceDisposition Source = D3SourceDisposition::KeepLive;
  D3BoundaryAccess Boundary = D3BoundaryAccess::SharedBorrow;
};

ProspectiveTransfer classifyProspectiveTransfer(
    const D3PreLegacyDirectCallFacts &pre, D3CopyProof copyProof) {
  ProspectiveTransfer result;
  if (!pre.FormalCeded)
    return result;
  if (pre.ActualCategory == D3ActualCategory::WholeTemporary) {
    result.Transfer = D3TransferMode::ConsumeTemporary;
    result.Source = D3SourceDisposition::NoSourcePlace;
    result.Boundary = D3BoundaryAccess::None;
  } else if (copyProof == D3CopyProof::ProvenCopy) {
    result.Transfer = D3TransferMode::CopyValue;
    result.Source = pre.ExplicitCede
                        ? D3SourceDisposition::InvalidateWhole
                        : D3SourceDisposition::KeepLive;
    result.Boundary = pre.ExplicitCede ? D3BoundaryAccess::Invalidation
                                      : D3BoundaryAccess::SharedBorrow;
  } else {
    result.Transfer = D3TransferMode::MoveOwned;
    result.Source = D3SourceDisposition::InvalidateWhole;
    result.Boundary = D3BoundaryAccess::Invalidation;
  }
  return result;
}

D3LiabilityFact deriveSourceLiability(
    const D3PreLegacyDirectCallFacts &pre, D3OwnershipProof ownership,
    const std::string &cleanupWitness) {
  if (ownership != D3OwnershipProof::Owned)
    return D3LiabilityFact::noLiability();
  return pre.ActualCategory == D3ActualCategory::WholeTemporary
             ? D3LiabilityFact::temporary(cleanupWitness)
             : D3LiabilityFact::sourcePlace(cleanupWitness);
}

} // namespace

const char *toString(D3AdmissionKind value) {
  switch (value) {
  case D3AdmissionKind::Admitted:
    return "Admitted";
  case D3AdmissionKind::NotInSlice:
    return "NotInSlice";
  case D3AdmissionKind::Rejected:
    return "Rejected";
  }
  return "Rejected";
}

const char *toString(D3ExclusionReason value) {
  switch (value) {
  case D3ExclusionReason::Generic:
    return "Generic";
  case D3ExclusionReason::VariadicOrDefault:
    return "VariadicOrDefault";
  case D3ExclusionReason::MultipleArguments:
    return "MultipleArguments";
  case D3ExclusionReason::InitOrOutcome:
    return "InitOrOutcome";
  case D3ExclusionReason::AsyncOrExecutionBoundary:
    return "AsyncOrExecutionBoundary";
  case D3ExclusionReason::ReturnDependencyOrRegionEscape:
    return "ReturnDependencyOrRegionEscape";
  case D3ExclusionReason::NestedObservation:
    return "NestedObservation";
  case D3ExclusionReason::Projection:
    return "Projection";
  case D3ExclusionReason::NonLocalPlace:
    return "NonLocalPlace";
  case D3ExclusionReason::SharedIdentity:
    return "SharedIdentity";
  case D3ExclusionReason::RawOrReferenceIdentity:
    return "RawOrReferenceIdentity";
  case D3ExclusionReason::FunctionOrDynIdentity:
    return "FunctionOrDynIdentity";
  case D3ExclusionReason::NonCedeScalar:
    return "NonCedeScalar";
  case D3ExclusionReason::NonCedeAggregateTemporary:
    return "NonCedeAggregateTemporary";
  case D3ExclusionReason::UnsupportedTypeCategory:
    return "UnsupportedTypeCategory";
  }
  return "UnsupportedTypeCategory";
}

const char *toString(D3CallValidationError value) {
  switch (value) {
  case D3CallValidationError::LegacyTypeMismatch:
    return "LegacyTypeMismatch";
  case D3CallValidationError::BorrowedFormalExplicitCede:
    return "BorrowedFormalExplicitCede";
  case D3CallValidationError::IndeterminateCopyProof:
    return "IndeterminateCopyProof";
  case D3CallValidationError::IndeterminateOwnership:
    return "IndeterminateOwnership";
  case D3CallValidationError::InvalidWholePlaceAdmission:
    return "InvalidWholePlaceAdmission";
  case D3CallValidationError::IncompleteObservationFacts:
    return "IncompleteObservationFacts";
  }
  return "IncompleteObservationFacts";
}

const char *toString(D3ActualCategory value) {
  switch (value) {
  case D3ActualCategory::Indeterminate:
    return "Indeterminate";
  case D3ActualCategory::WholePlace:
    return "WholePlace";
  case D3ActualCategory::WholeTemporary:
    return "WholeTemporary";
  case D3ActualCategory::Projection:
    return "Projection";
  }
  return "Indeterminate";
}

const char *toString(D3TypeCategory value) {
  switch (value) {
  case D3TypeCategory::Indeterminate:
    return "Indeterminate";
  case D3TypeCategory::Scalar:
    return "Scalar";
  case D3TypeCategory::Aggregate:
    return "Aggregate";
  case D3TypeCategory::BorrowedAggregate:
    return "BorrowedAggregate";
  case D3TypeCategory::OwnedIdentity:
    return "OwnedIdentity";
  case D3TypeCategory::SharedIdentity:
    return "SharedIdentity";
  case D3TypeCategory::RawOrReferenceIdentity:
    return "RawOrReferenceIdentity";
  case D3TypeCategory::FunctionOrDynIdentity:
    return "FunctionOrDynIdentity";
  case D3TypeCategory::Unsupported:
    return "Unsupported";
  }
  return "Indeterminate";
}

const char *toString(D3CopyProof value) {
  switch (value) {
  case D3CopyProof::ProvenCopy:
    return "ProvenCopy";
  case D3CopyProof::ProvenNonCopy:
    return "ProvenNonCopy";
  case D3CopyProof::Indeterminate:
    return "Indeterminate";
  }
  return "Indeterminate";
}

const char *toString(D3TransferMode value) {
  switch (value) {
  case D3TransferMode::BorrowCapture:
    return "BorrowCapture";
  case D3TransferMode::CopyValue:
    return "CopyValue";
  case D3TransferMode::MoveOwned:
    return "MoveOwned";
  case D3TransferMode::ConsumeTemporary:
    return "ConsumeTemporary";
  }
  return "BorrowCapture";
}

const char *toString(D3SourceDisposition value) {
  switch (value) {
  case D3SourceDisposition::KeepLive:
    return "KeepLive";
  case D3SourceDisposition::InvalidateWhole:
    return "InvalidateWhole";
  case D3SourceDisposition::NoSourcePlace:
    return "NoSourcePlace";
  }
  return "KeepLive";
}

const char *toString(D3StateDomain value) {
  switch (value) {
  case D3StateDomain::Evaluation:
    return "Evaluation";
  case D3StateDomain::PlaceState:
    return "PlaceState";
  case D3StateDomain::PAL:
    return "PAL";
  case D3StateDomain::Dependency:
    return "Dependency";
  case D3StateDomain::CleanupLiability:
    return "CleanupLiability";
  case D3StateDomain::RegionObligation:
    return "RegionObligation";
  }
  return "Evaluation";
}

const char *toString(D3DeltaLane value) {
  switch (value) {
  case D3DeltaLane::Evaluation:
    return "Evaluation";
  case D3DeltaLane::Boundary:
    return "Boundary";
  case D3DeltaLane::Finalization:
    return "Finalization";
  }
  return "Evaluation";
}

const char *toString(D3OwnershipProof value) {
  switch (value) {
  case D3OwnershipProof::Trivial:
    return "Trivial";
  case D3OwnershipProof::Borrowed:
    return "Borrowed";
  case D3OwnershipProof::Owned:
    return "Owned";
  case D3OwnershipProof::Shared:
    return "Shared";
  case D3OwnershipProof::Indeterminate:
    return "Indeterminate";
  }
  return "Indeterminate";
}

const char *toString(D3BoundaryAccess value) {
  switch (value) {
  case D3BoundaryAccess::None:
    return "None";
  case D3BoundaryAccess::SharedBorrow:
    return "SharedBorrow";
  case D3BoundaryAccess::Invalidation:
    return "Invalidation";
  case D3BoundaryAccess::Unsupported:
    return "Unsupported";
  }
  return "Unsupported";
}

const char *toString(D3DependencyRelation value) {
  switch (value) {
  case D3DependencyRelation::None:
    return "None";
  case D3DependencyRelation::BorrowedCallRegion:
    return "BorrowedCallRegion";
  }
  return "None";
}

const char *toString(D3SubjectKind value) {
  switch (value) {
  case D3SubjectKind::SourcePlace:
    return "SourcePlace";
  case D3SubjectKind::Destination:
    return "Destination";
  case D3SubjectKind::Loan:
    return "Loan";
  case D3SubjectKind::Temporary:
    return "Temporary";
  case D3SubjectKind::Cleanup:
    return "Cleanup";
  }
  return "SourcePlace";
}

const char *toString(D3LiabilityKind value) {
  switch (value) {
  case D3LiabilityKind::NoLiability:
    return "NoLiability";
  case D3LiabilityKind::SourcePlaceCleanup:
    return "SourcePlaceCleanup";
  case D3LiabilityKind::TemporaryCleanup:
    return "TemporaryCleanup";
  case D3LiabilityKind::SourceRetained:
    return "SourceRetained";
  case D3LiabilityKind::DestinationCleanup:
    return "DestinationCleanup";
  }
  return "NoLiability";
}

const char *toString(LegacyCedeRequirement value) {
  switch (value) {
  case LegacyCedeRequirement::ExplicitRequired:
    return "ExplicitRequired";
  case LegacyCedeRequirement::ImplicitExempt:
    return "ImplicitExempt";
  case LegacyCedeRequirement::Indeterminate:
    return "Indeterminate";
  }
  return "Indeterminate";
}

const char *toString(LegacyCedeDropFact value) {
  switch (value) {
  case LegacyCedeDropFact::HasDrop:
    return "HasDrop";
  case LegacyCedeDropFact::NoDrop:
    return "NoDrop";
  case LegacyCedeDropFact::Indeterminate:
    return "Indeterminate";
  }
  return "Indeterminate";
}

const char *toString(D5PreparationExclusionReason value) {
  switch (value) {
  case D5PreparationExclusionReason::ArityOrDefault:
    return "ArityOrDefault";
  case D5PreparationExclusionReason::GenericOrContextual:
    return "GenericOrContextual";
  case D5PreparationExclusionReason::InitOrOutcome:
    return "InitOrOutcome";
  case D5PreparationExclusionReason::AsyncOrExecutionBoundary:
    return "AsyncOrExecutionBoundary";
  case D5PreparationExclusionReason::ReturnDependencyOrRegionEscape:
    return "ReturnDependencyOrRegionEscape";
  case D5PreparationExclusionReason::ProjectionOrTemporary:
    return "ProjectionOrTemporary";
  case D5PreparationExclusionReason::NonLocalPlace:
    return "NonLocalPlace";
  case D5PreparationExclusionReason::SharedRawReferenceOrCallable:
    return "SharedRawReferenceOrCallable";
  case D5PreparationExclusionReason::DependencyBearingActual:
    return "DependencyBearingActual";
  case D5PreparationExclusionReason::TypeRequiresContextOrConversion:
    return "TypeRequiresContextOrConversion";
  case D5PreparationExclusionReason::CededNonCopyLegacyExempt:
    return "CededNonCopyLegacyExempt";
  }
  return "TypeRequiresContextOrConversion";
}

const char *toString(D5PreparationError value) {
  switch (value) {
  case D5PreparationError::InvalidIdentity:
    return "InvalidIdentity";
  case D5PreparationError::IncompatibleType:
    return "IncompatibleType";
  case D5PreparationError::IndeterminateCopyProof:
    return "IndeterminateCopyProof";
  case D5PreparationError::IndeterminateOwnership:
    return "IndeterminateOwnership";
  case D5PreparationError::IndeterminateLegacyCedeRequirement:
    return "IndeterminateLegacyCedeRequirement";
  case D5PreparationError::InconsistentLegacyCedeRequirement:
    return "InconsistentLegacyCedeRequirement";
  case D5PreparationError::InvalidWholePlaceAdmission:
    return "InvalidWholePlaceAdmission";
  case D5PreparationError::IncompleteLiability:
    return "IncompleteLiability";
  case D5PreparationError::IncompleteRegion:
    return "IncompleteRegion";
  case D5PreparationError::ConflictingPreparedPlan:
    return "ConflictingPreparedPlan";
  }
  return "ConflictingPreparedPlan";
}

LegacyCedeRequirement
classifyLegacyCedeRequirement(const LegacyCedePolicyInput &input) {
  if (input.TypeCategory == D3TypeCategory::Indeterminate ||
      input.CanonicalSoul.empty())
    return LegacyCedeRequirement::Indeterminate;

  if (input.TypeCategory == D3TypeCategory::Scalar ||
      input.TypeCategory == D3TypeCategory::RawOrReferenceIdentity ||
      input.TypeCategory == D3TypeCategory::FunctionOrDynIdentity)
    return LegacyCedeRequirement::ImplicitExempt;

  if (input.CanonicalSoul == "str" || input.CanonicalSoul == "bytes" ||
      input.CanonicalSoul == "cstr" ||
      input.CanonicalSoul == "ViewStrSplitIterator" ||
      input.CanonicalSoul == "ViewStrLinesIterator" ||
      input.CanonicalSoul == "string" || input.CanonicalSoul == "TimerHeap")
    return LegacyCedeRequirement::ExplicitRequired;
  if (input.CanonicalSoul == "SlabID")
    return LegacyCedeRequirement::ImplicitExempt;

  if (input.DropFact == LegacyCedeDropFact::HasDrop)
    return LegacyCedeRequirement::ExplicitRequired;
  if (input.DropFact == LegacyCedeDropFact::NoDrop)
    return LegacyCedeRequirement::ImplicitExempt;
  return LegacyCedeRequirement::Indeterminate;
}

size_t D3SubjectIdentity::hashValue() const noexcept {
  return combine(static_cast<size_t>(Kind), std::hash<std::string>{}(Key));
}

D3LiabilityFact D3LiabilityFact::noLiability() { return {}; }

D3LiabilityFact D3LiabilityFact::sourcePlace(std::string key) {
  D3LiabilityFact fact;
  fact.Kind = D3LiabilityKind::SourcePlaceCleanup;
  fact.Subject = D3SubjectIdentity::cleanup(std::move(key));
  return fact;
}

D3LiabilityFact D3LiabilityFact::temporary(std::string key) {
  D3LiabilityFact fact;
  fact.Kind = D3LiabilityKind::TemporaryCleanup;
  fact.Subject = D3SubjectIdentity::cleanup(std::move(key));
  return fact;
}

D3LiabilityFact D3LiabilityFact::sourceRetained(std::string key) {
  D3LiabilityFact fact;
  fact.Kind = D3LiabilityKind::SourceRetained;
  fact.Subject = D3SubjectIdentity::cleanup(std::move(key));
  return fact;
}

D3LiabilityFact D3LiabilityFact::destination(std::string key) {
  D3LiabilityFact fact;
  fact.Kind = D3LiabilityKind::DestinationCleanup;
  fact.Subject = D3SubjectIdentity::destination(std::move(key));
  return fact;
}

size_t D3LiabilityFact::hashValue() const noexcept {
  size_t seed = static_cast<size_t>(Kind);
  return Subject ? combine(seed, Subject->hashValue()) : seed;
}

size_t D3ValidatedTransferEdge::hashValue() const noexcept {
  size_t seed = Id.hashValue();
  seed = combine(seed, ArgumentPlan.hashValue());
  seed = combine(seed, Formal.hashValue());
  seed = combine(seed, Destination.hashValue());
  if (SourcePlace)
    seed = combine(seed, SourcePlace->hashValue());
  seed = hashText(seed, TypeProof);
  seed = hashText(seed, ValueCategory);
  seed = hashEnum(seed, Transfer);
  seed = hashEnum(seed, Source);
  seed = hashEnum(seed, BoundaryAccess);
  seed = hashEnum(seed, Dependency);
  seed = combine(seed, LiabilitySource.hashValue());
  seed = combine(seed, LiabilityTarget.hashValue());
  return combine(seed, ExplicitCede ? 1U : 0U);
}

bool operator==(const D3ValidatedTransferEdge &lhs,
                const D3ValidatedTransferEdge &rhs) {
  return std::tie(lhs.Id, lhs.ArgumentPlan, lhs.Formal, lhs.Destination,
                  lhs.SourcePlace, lhs.TypeProof, lhs.ValueCategory,
                  lhs.Transfer, lhs.Source, lhs.BoundaryAccess, lhs.Dependency,
                  lhs.LiabilitySource, lhs.LiabilityTarget, lhs.ExplicitCede) ==
         std::tie(rhs.Id, rhs.ArgumentPlan, rhs.Formal, rhs.Destination,
                  rhs.SourcePlace, rhs.TypeProof, rhs.ValueCategory,
                  rhs.Transfer, rhs.Source, rhs.BoundaryAccess, rhs.Dependency,
                  rhs.LiabilitySource, rhs.LiabilityTarget, rhs.ExplicitCede);
}

size_t D3DeltaEntry::hashValue() const noexcept {
  size_t seed = Edge.hashValue();
  seed = hashEnum(seed, Domain);
  seed = combine(seed, SubjectIdentity.hashValue());
  seed = hashText(seed, ExpectedBefore);
  seed = hashText(seed, ResultAfter);
  return hashText(seed, Provenance);
}

bool operator==(const D3DeltaEntry &lhs, const D3DeltaEntry &rhs) {
  return std::tie(lhs.Edge, lhs.Domain, lhs.SubjectIdentity, lhs.ExpectedBefore,
                  lhs.ResultAfter, lhs.Provenance) ==
         std::tie(rhs.Edge, rhs.Domain, rhs.SubjectIdentity, rhs.ExpectedBefore,
                  rhs.ResultAfter, rhs.Provenance);
}

size_t D3DomainDelta::hashValue() const noexcept {
  size_t seed = static_cast<size_t>(Lane);
  for (const auto &entry : Entries)
    seed = combine(seed, entry.hashValue());
  return seed;
}

bool operator==(const D3DomainDelta &lhs, const D3DomainDelta &rhs) {
  return lhs.Lane == rhs.Lane && lhs.Entries == rhs.Entries;
}

D3PatchBuildResult
buildD3SemanticModelPatch(std::vector<D3PatchEntrySeed> entries) {
  D3PatchBuildResult result;
  std::sort(entries.begin(), entries.end(),
            [](const auto &lhs, const auto &rhs) {
              return lhs.Identity < rhs.Identity;
            });
  for (const auto &entry : entries) {
    if (!entry.Identity.valid()) {
      result.Error = D3PatchBuildError::InvalidIdentity;
      return result;
    }
    if (!result.Patch.Entries.empty() &&
        result.Patch.Entries.back().Identity == entry.Identity) {
      if (result.Patch.Entries.back().Payload != entry.Payload) {
        result.Patch.Entries.clear();
        result.Error = D3PatchBuildError::ConflictingPayload;
        return result;
      }
      continue;
    }
    result.Patch.Entries.push_back(entry);
  }
  return result;
}

size_t D3SemanticModelPatch::hashValue() const noexcept {
  size_t seed = Entries.size();
  for (const auto &entry : Entries) {
    seed = combine(seed, entry.Identity.hashValue());
    seed = hashText(seed, entry.Payload);
  }
  return seed;
}

bool operator==(const D3SemanticModelPatch &lhs,
                const D3SemanticModelPatch &rhs) {
  if (lhs.Entries.size() != rhs.Entries.size())
    return false;
  for (size_t i = 0; i < lhs.Entries.size(); ++i) {
    if (lhs.Entries[i].Identity != rhs.Entries[i].Identity ||
        lhs.Entries[i].Payload != rhs.Entries[i].Payload)
      return false;
  }
  return true;
}

size_t D3MinimalRegionWitness::hashValue() const noexcept {
  size_t seed = CallRegion.hashValue();
  seed = combine(seed, FullExpressionRegion.hashValue());
  seed = hashText(seed, Origin);
  seed = combine(seed, Subject.hashValue());
  return hashText(seed, Terminal);
}

bool operator==(const D3MinimalRegionWitness &lhs,
                const D3MinimalRegionWitness &rhs) {
  return std::tie(lhs.CallRegion, lhs.FullExpressionRegion, lhs.Origin,
                  lhs.Subject, lhs.Terminal) ==
         std::tie(rhs.CallRegion, rhs.FullExpressionRegion, rhs.Origin,
                  rhs.Subject, rhs.Terminal);
}

size_t D3ValidatedCall::hashValue() const noexcept {
  size_t seed = CallSite.hashValue();
  seed = combine(seed, Callee.hashValue());
  for (const auto &edge : TransferEdges)
    seed = combine(seed, edge.hashValue());
  seed = combine(seed, Evaluation.hashValue());
  seed = combine(seed, Boundary.hashValue());
  seed = combine(seed, Finalization.hashValue());
  seed = combine(seed, ModelPatch.hashValue());
  return combine(seed, RegionWitness.hashValue());
}

bool operator==(const D3ValidatedCall &lhs, const D3ValidatedCall &rhs) {
  return lhs.CallSite == rhs.CallSite && lhs.Callee == rhs.Callee &&
         lhs.TransferEdges == rhs.TransferEdges &&
         lhs.Evaluation == rhs.Evaluation && lhs.Boundary == rhs.Boundary &&
         lhs.Finalization == rhs.Finalization &&
         lhs.ModelPatch == rhs.ModelPatch &&
         lhs.RegionWitness == rhs.RegionWitness;
}

size_t D3FactoryObservationRecord::hashValue() const noexcept {
  size_t seed = static_cast<size_t>(Admission);
  if (ExclusionReason)
    seed = hashEnum(seed, *ExclusionReason);
  if (ValidationError)
    seed = hashEnum(seed, *ValidationError);
  if (ValidatedCall)
    seed = combine(seed, ValidatedCall->hashValue());
  seed = hashText(seed, Input.Pre.CallWitness);
  seed = hashText(seed, Input.Pre.CalleeWitness);
  seed = hashText(seed, Input.Post.ActualType);
  return seed;
}

bool operator==(const D3FactoryObservationRecord &lhs,
                const D3FactoryObservationRecord &rhs) {
  return lhs.Admission == rhs.Admission &&
         lhs.ExclusionReason == rhs.ExclusionReason &&
         lhs.ValidationError == rhs.ValidationError &&
         lhs.ValidatedCall == rhs.ValidatedCall &&
         equalPre(lhs.Input.Pre, rhs.Input.Pre) &&
         equalPost(lhs.Input.Post, rhs.Input.Post);
}

D3FactoryObservationRecord
DirectCallObservationFactory::observe(D3CallObservationInput input) {
  const auto &pre = input.Pre;
  const auto &post = input.Post;
  auto reject = [](D3CallObservationInput value, D3CallValidationError error) {
    D3FactoryObservationRecord record;
    record.Input = std::move(value);
    record.Admission = D3AdmissionKind::Rejected;
    record.ValidationError = error;
    return record;
  };
  auto exclude = [](D3CallObservationInput value, D3ExclusionReason reason) {
    D3FactoryObservationRecord record;
    record.Input = std::move(value);
    record.Admission = D3AdmissionKind::NotInSlice;
    record.ExclusionReason = reason;
    record.ValidationError.reset();
    return record;
  };

  // Core classification facts and the legacy compatibility result precede
  // structural exclusion. Facts required only by an admitted edge do not.
  if (!pre.CoreFactsComplete)
    return reject(std::move(input),
                  D3CallValidationError::IncompleteObservationFacts);
  if (post.LegacyTypeMismatch)
    return reject(std::move(input), D3CallValidationError::LegacyTypeMismatch);

  if (pre.Generic)
    return exclude(std::move(input), D3ExclusionReason::Generic);
  if (pre.VariadicOrDefault)
    return exclude(std::move(input), D3ExclusionReason::VariadicOrDefault);
  if (pre.MultipleArguments)
    return exclude(std::move(input), D3ExclusionReason::MultipleArguments);
  if (pre.InitOrOutcome)
    return exclude(std::move(input), D3ExclusionReason::InitOrOutcome);
  if (pre.AsyncOrExecutionBoundary)
    return exclude(std::move(input),
                   D3ExclusionReason::AsyncOrExecutionBoundary);
  if (pre.ReturnDependencyOrRegionEscape)
    return exclude(std::move(input),
                   D3ExclusionReason::ReturnDependencyOrRegionEscape);
  if (pre.NestedObservation)
    return exclude(std::move(input), D3ExclusionReason::NestedObservation);
  if (pre.ActualCategory == D3ActualCategory::Projection)
    return exclude(std::move(input), D3ExclusionReason::Projection);
  if (pre.ActualCategory == D3ActualCategory::WholePlace &&
      !pre.SourceIsLocalPlace)
    return exclude(std::move(input), D3ExclusionReason::NonLocalPlace);
  if (post.TypeCategory == D3TypeCategory::SharedIdentity)
    return exclude(std::move(input), D3ExclusionReason::SharedIdentity);
  if (post.TypeCategory == D3TypeCategory::RawOrReferenceIdentity)
    return exclude(std::move(input), D3ExclusionReason::RawOrReferenceIdentity);
  if (post.TypeCategory == D3TypeCategory::FunctionOrDynIdentity)
    return exclude(std::move(input), D3ExclusionReason::FunctionOrDynIdentity);
  if (post.TypeCategory == D3TypeCategory::Unsupported)
    return exclude(std::move(input),
                   D3ExclusionReason::UnsupportedTypeCategory);
  if (pre.FormalCeded && post.TypeCategory == D3TypeCategory::BorrowedAggregate)
    return exclude(std::move(input),
                   D3ExclusionReason::UnsupportedTypeCategory);

  if (!pre.FormalCeded) {
    if (post.TypeCategory == D3TypeCategory::Scalar)
      return exclude(std::move(input), D3ExclusionReason::NonCedeScalar);
    if (pre.ActualCategory == D3ActualCategory::WholeTemporary)
      return exclude(std::move(input),
                     D3ExclusionReason::NonCedeAggregateTemporary);
    if (pre.ExplicitCede)
      return reject(std::move(input),
                    D3CallValidationError::BorrowedFormalExplicitCede);
    if (post.TypeCategory == D3TypeCategory::OwnedIdentity)
      return exclude(std::move(input),
                     D3ExclusionReason::UnsupportedTypeCategory);
  }

  if (post.TypeCategory == D3TypeCategory::Indeterminate)
    return reject(std::move(input),
                  D3CallValidationError::IndeterminateOwnership);
  if (pre.ActualCategory == D3ActualCategory::Indeterminate)
    return reject(std::move(input),
                  D3CallValidationError::IncompleteObservationFacts);
  const bool typeMatchesOwnership =
      (post.TypeCategory == D3TypeCategory::Scalar &&
       post.OwnershipProof == D3OwnershipProof::Trivial) ||
      (post.TypeCategory == D3TypeCategory::Aggregate &&
       (post.OwnershipProof == D3OwnershipProof::Trivial ||
        post.OwnershipProof == D3OwnershipProof::Owned)) ||
      (post.TypeCategory == D3TypeCategory::BorrowedAggregate &&
       post.OwnershipProof == D3OwnershipProof::Borrowed) ||
      (post.TypeCategory == D3TypeCategory::OwnedIdentity &&
       post.OwnershipProof == D3OwnershipProof::Owned);
  if (!typeMatchesOwnership)
    return reject(std::move(input),
                  D3CallValidationError::IncompleteObservationFacts);
  if ((post.TypeCategory == D3TypeCategory::Scalar &&
       post.CopyProof == D3CopyProof::ProvenNonCopy) ||
      (post.TypeCategory == D3TypeCategory::OwnedIdentity &&
       post.CopyProof == D3CopyProof::ProvenCopy))
    return reject(std::move(input),
                  D3CallValidationError::IncompleteObservationFacts);
  const auto sourceLiabilityKind = post.SourceLiability.kind();
  const bool sourceLiabilityHasMatchingCleanup =
      post.SourceLiability.subject() &&
      post.SourceLiability.subject()->kind() == D3SubjectKind::Cleanup &&
      !post.CleanupWitness.empty() &&
      post.SourceLiability.subject()->key() == post.CleanupWitness;
  const auto expectedSourceLiability = deriveSourceLiability(
      pre, post.OwnershipProof, post.CleanupWitness);
  const bool liabilityMatchesOwnership =
      post.SourceLiability == expectedSourceLiability &&
      (post.OwnershipProof != D3OwnershipProof::Owned ||
       sourceLiabilityHasMatchingCleanup);
  if (!liabilityMatchesOwnership ||
      (post.CopyProof == D3CopyProof::ProvenCopy &&
       (post.OwnershipProof != D3OwnershipProof::Trivial ||
        sourceLiabilityKind != D3LiabilityKind::NoLiability)))
    return reject(std::move(input),
                  D3CallValidationError::IncompleteObservationFacts);
  if (!post.LegacySucceeded) {
    const bool onlySupersededSpelling =
        !post.LegacyDiagnosticCodes.empty() &&
        std::all_of(post.LegacyDiagnosticCodes.begin(),
                    post.LegacyDiagnosticCodes.end(),
                    [](const std::string &code) { return code == "E04570"; });
    if (!onlySupersededSpelling)
      return reject(std::move(input),
                    D3CallValidationError::IncompleteObservationFacts);
  }
  if (post.OwnershipProof == D3OwnershipProof::Indeterminate ||
      post.BoundaryAccess == D3BoundaryAccess::Unsupported ||
      !post.SourceLiability.valid())
    return reject(std::move(input),
                  D3CallValidationError::IncompleteObservationFacts);
  if (!post.AdmissionFactsComplete || !post.LiabilityComplete ||
      !post.RegionFactsComplete)
    return reject(std::move(input),
                  D3CallValidationError::IncompleteObservationFacts);
  if (pre.ActualCategory == D3ActualCategory::WholePlace &&
      (!post.WholePlaceEligible || pre.SourcePlaceAlias ||
       pre.SourceStateBefore != "Live"))
    return reject(std::move(input),
                  D3CallValidationError::InvalidWholePlaceAdmission);

  if (pre.FormalCeded &&
      pre.ActualCategory != D3ActualCategory::WholeTemporary &&
      post.CopyProof == D3CopyProof::Indeterminate)
    return reject(std::move(input),
                  D3CallValidationError::IndeterminateCopyProof);
  const auto prospective = classifyProspectiveTransfer(pre, post.CopyProof);
  const auto transfer = prospective.Transfer;
  const auto source = prospective.Source;

  if (post.BoundaryAccess != prospective.Boundary)
    return reject(
        std::move(input),
        pre.ActualCategory == D3ActualCategory::WholeTemporary
            ? D3CallValidationError::IncompleteObservationFacts
            : D3CallValidationError::InvalidWholePlaceAdmission);

  auto callId = SemanticIdentityBuilder::semanticNode(pre.IdentityOrigin,
                                                      pre.CallWitness);
  auto declarationId = SemanticIdentityBuilder::declaration(pre.IdentityOrigin,
                                                            pre.CalleeWitness);
  auto callerOwnerId = SemanticIdentityBuilder::declaration(
      pre.IdentityOrigin, pre.CallerBindingOwnerWitness);
  if (!callId || !declarationId || !callerOwnerId)
    return reject(std::move(input),
                  D3CallValidationError::IncompleteObservationFacts);
  auto formalId =
      SemanticIdentityBuilder::formal(declarationId.value(), pre.FormalWitness);
  auto planId = SemanticIdentityBuilder::argumentPlan(callId.value(), "arg:1");
  if (!formalId || !planId)
    return reject(std::move(input),
                  D3CallValidationError::IncompleteObservationFacts);
  auto edgeId = SemanticIdentityBuilder::transferEdge(
      planId.value(), pre.SourceWitness + "->" + pre.DestinationWitness);
  auto destinationId = SemanticIdentityBuilder::destination(
      formalId.value(), pre.DestinationWitness);
  if (!edgeId || !destinationId)
    return reject(std::move(input),
                  D3CallValidationError::IncompleteObservationFacts);

  D3ValidatedTransferEdge edge;
  edge.Id = edgeId.value();
  edge.ArgumentPlan = planId.value();
  edge.Formal = formalId.value();
  edge.Destination = destinationId.value();
  edge.TypeProof = std::string(toString(post.TypeCategory)) + "/" +
                   toString(post.CopyProof) + "/" +
                   toString(post.OwnershipProof);
  edge.ValueCategory = toString(pre.ActualCategory);
  edge.Transfer = transfer;
  edge.Source = source;
  edge.BoundaryAccess = post.BoundaryAccess;
  edge.Dependency = transfer == D3TransferMode::BorrowCapture
                        ? D3DependencyRelation::BorrowedCallRegion
                        : D3DependencyRelation::None;
  edge.ExplicitCede = pre.ExplicitCede;
  if (pre.ActualCategory == D3ActualCategory::WholePlace) {
    auto rootId = SemanticIdentityBuilder::rootSymbol(callerOwnerId.value(),
                                                      pre.SourceWitness);
    if (!rootId)
      return reject(std::move(input),
                    D3CallValidationError::IncompleteObservationFacts);
    edge.SourcePlace = PlaceId(rootId.value());
  }
  edge.LiabilitySource = post.SourceLiability;
  if ((transfer == D3TransferMode::MoveOwned ||
       transfer == D3TransferMode::ConsumeTemporary) &&
      post.OwnershipProof == D3OwnershipProof::Owned)
    edge.LiabilityTarget =
        D3LiabilityFact::destination(destinationId.value().canonicalKey());
  else if (transfer == D3TransferMode::BorrowCapture &&
           post.SourceLiability.kind() != D3LiabilityKind::NoLiability)
    edge.LiabilitySource = D3LiabilityFact::sourceRetained(post.CleanupWitness);
  else
    edge.LiabilityTarget = D3LiabilityFact::noLiability();

  D3ValidatedCall validated;
  validated.CallSite = callId.value();
  validated.Callee = ResolvedCalleeId::direct(declarationId.value());
  validated.TransferEdges.push_back(edge);
  validated.Evaluation.Lane = D3DeltaLane::Evaluation;
  validated.Boundary.Lane = D3DeltaLane::Boundary;
  validated.Finalization.Lane = D3DeltaLane::Finalization;

  auto makeEntry = [&](D3StateDomain domain, D3SubjectIdentity subject,
                       std::string before, std::string after,
                       std::string provenance) {
    D3DeltaEntry entry;
    entry.Edge = edge.Id;
    entry.Domain = domain;
    entry.SubjectIdentity = std::move(subject);
    entry.ExpectedBefore = std::move(before);
    entry.ResultAfter = std::move(after);
    entry.Provenance = std::move(provenance);
    return entry;
  };

  if (pre.ActualCategory == D3ActualCategory::WholeTemporary) {
    validated.Evaluation.Entries.push_back(
        makeEntry(D3StateDomain::Evaluation,
                  D3SubjectIdentity::temporary(pre.SourceWitness), "Absent",
                  "Materialized", "legacy-expression-evaluation"));
  }
  if (source == D3SourceDisposition::InvalidateWhole) {
    validated.Boundary.Entries.push_back(
        makeEntry(D3StateDomain::PlaceState,
                  D3SubjectIdentity::sourcePlace(pre.SourceWitness), "Live",
                  "Moved", "validated-transfer-edge"));
  } else if (transfer == D3TransferMode::BorrowCapture) {
    validated.Boundary.Entries.push_back(makeEntry(
        D3StateDomain::PAL, D3SubjectIdentity::loan(pre.SourceWitness),
        pre.PALStateBefore, "BorrowedShared", "validated-transfer-edge"));
    validated.Finalization.Entries.push_back(makeEntry(
        D3StateDomain::PAL, D3SubjectIdentity::loan(pre.SourceWitness),
        "BorrowedShared", "Free", "call-region-terminal"));
  }
  if (transfer == D3TransferMode::MoveOwned &&
      post.SourceLiability.kind() == D3LiabilityKind::SourcePlaceCleanup) {
    validated.Boundary.Entries.push_back(
        makeEntry(D3StateDomain::CleanupLiability,
                  D3SubjectIdentity::cleanup(post.CleanupWitness), "Armed",
                  "TransferredToDestination", "validated-transfer-edge"));
  } else if (transfer == D3TransferMode::ConsumeTemporary &&
             post.SourceLiability.kind() == D3LiabilityKind::TemporaryCleanup) {
    validated.Finalization.Entries.push_back(
        makeEntry(D3StateDomain::CleanupLiability,
                  D3SubjectIdentity::cleanup(post.CleanupWitness), "Armed",
                  "DisarmedToDestination", "full-expression-terminal"));
  }

  auto callRegion = SemanticIdentityBuilder::region(callId.value(), "call");
  auto fullExpressionRegion =
      SemanticIdentityBuilder::region(callId.value(), "full-expression");
  if (!callRegion || !fullExpressionRegion)
    return reject(std::move(input),
                  D3CallValidationError::IncompleteObservationFacts);
  validated.RegionWitness.CallRegion = callRegion.value();
  validated.RegionWitness.FullExpressionRegion = fullExpressionRegion.value();
  validated.RegionWitness.Origin = pre.CallWitness;
  validated.RegionWitness.Subject =
      transfer == D3TransferMode::BorrowCapture
          ? D3SubjectIdentity::loan(pre.SourceWitness)
          : (pre.ActualCategory == D3ActualCategory::WholeTemporary
                 ? (post.SourceLiability.kind() ==
                            D3LiabilityKind::TemporaryCleanup
                        ? D3SubjectIdentity::cleanup(post.CleanupWitness)
                        : D3SubjectIdentity::temporary(pre.SourceWitness))
                 : D3SubjectIdentity::sourcePlace(pre.SourceWitness));
  validated.RegionWitness.Terminal =
      transfer == D3TransferMode::BorrowCapture
          ? "EndBoundaryLoan"
          : (transfer == D3TransferMode::ConsumeTemporary &&
                     post.SourceLiability.kind() ==
                         D3LiabilityKind::TemporaryCleanup
                 ? "DisarmTemporaryCleanup"
                 : "NoLocalRegionObligation");

  std::vector<D3PatchEntrySeed> patchEntries;
  auto appendPatchEntries = [&](const D3DomainDelta &delta) {
    for (const auto &entry : delta.entries()) {
      const std::string witness = std::string(toString(delta.lane())) + ":" +
                                  toString(entry.stateDomain()) + ":" +
                                  toString(entry.subjectIdentity().kind()) +
                                  ":" + entry.subjectIdentity().key();
      auto patchId = SemanticIdentityBuilder::patchEntry(edge.Id, witness);
      if (!patchId)
        return false;
      patchEntries.push_back({patchId.value(), entry.expectedBefore() + "->" +
                                                   entry.resultAfter() + "|" +
                                                   entry.provenance()});
    }
    return true;
  };
  if (!appendPatchEntries(validated.Evaluation) ||
      !appendPatchEntries(validated.Boundary) ||
      !appendPatchEntries(validated.Finalization))
    return reject(std::move(input),
                  D3CallValidationError::IncompleteObservationFacts);
  auto patch = buildD3SemanticModelPatch(std::move(patchEntries));
  if (!patch)
    return reject(std::move(input),
                  D3CallValidationError::IncompleteObservationFacts);
  validated.ModelPatch = std::move(patch.Patch);

  D3FactoryObservationRecord record;
  record.Input = std::move(input);
  record.Admission = D3AdmissionKind::Admitted;
  record.ValidationError.reset();
  record.ValidatedCall = std::move(validated);
  return record;
}

D5PreparedCallResult
PreparedCallFactory::prepare(D5ResolvedPlanningFacts facts) {
  D5PreparedCallResult result;
  result.Facts = facts;
  auto exclude = [&](D5PreparationExclusionReason reason) {
    result.Admission = D3AdmissionKind::NotInSlice;
    result.ExclusionReason = reason;
    return result;
  };
  auto reject = [&](D5PreparationError error) {
    result.Admission = D3AdmissionKind::Rejected;
    result.PreparationError = error;
    return result;
  };

  const auto &pre = facts.Pre;
  if (!pre.CoreFactsComplete)
    return reject(D5PreparationError::InvalidIdentity);
  if (pre.MultipleArguments || pre.VariadicOrDefault)
    return exclude(D5PreparationExclusionReason::ArityOrDefault);
  if (pre.Generic || pre.NestedObservation)
    return exclude(D5PreparationExclusionReason::GenericOrContextual);
  if (pre.InitOrOutcome)
    return exclude(D5PreparationExclusionReason::InitOrOutcome);
  if (pre.AsyncOrExecutionBoundary)
    return exclude(D5PreparationExclusionReason::AsyncOrExecutionBoundary);
  if (pre.ReturnDependencyOrRegionEscape)
    return exclude(
        D5PreparationExclusionReason::ReturnDependencyOrRegionEscape);
  if (pre.ActualCategory != D3ActualCategory::WholePlace)
    return exclude(D5PreparationExclusionReason::ProjectionOrTemporary);
  if (!pre.SourceIsLocalPlace)
    return exclude(D5PreparationExclusionReason::NonLocalPlace);
  if (facts.DependencyBearingActual)
    return exclude(D5PreparationExclusionReason::DependencyBearingActual);
  if (pre.SourcePlaceAlias || pre.SourceStateBefore != "Live" ||
      pre.PALStateBefore != "Free")
    return reject(D5PreparationError::InvalidWholePlaceAdmission);
  if (facts.ActualType.empty() || facts.FormalType.empty() ||
      facts.ActualType != facts.FormalType)
    return exclude(
        D5PreparationExclusionReason::TypeRequiresContextOrConversion);
  if (!facts.TypesCompatible)
    return reject(D5PreparationError::IncompatibleType);
  if (facts.TypeCategory == D3TypeCategory::SharedIdentity ||
      facts.TypeCategory == D3TypeCategory::RawOrReferenceIdentity ||
      facts.TypeCategory == D3TypeCategory::FunctionOrDynIdentity)
    return exclude(
        D5PreparationExclusionReason::SharedRawReferenceOrCallable);
  if (facts.TypeCategory != D3TypeCategory::Aggregate)
    return exclude(
        D5PreparationExclusionReason::TypeRequiresContextOrConversion);
  if (facts.CopyProof == D3CopyProof::Indeterminate)
    return reject(D5PreparationError::IndeterminateCopyProof);
  if (facts.OwnershipProof == D3OwnershipProof::Indeterminate)
    return reject(D5PreparationError::IndeterminateOwnership);

  if (pre.FormalCeded) {
    if (!facts.LegacyRequirement ||
        *facts.LegacyRequirement == LegacyCedeRequirement::Indeterminate)
      return reject(D5PreparationError::IndeterminateLegacyCedeRequirement);
    if (facts.CopyProof == D3CopyProof::ProvenNonCopy) {
      if (*facts.LegacyRequirement == LegacyCedeRequirement::ImplicitExempt)
        return exclude(
            D5PreparationExclusionReason::CededNonCopyLegacyExempt);
      if (facts.OwnershipProof != D3OwnershipProof::Owned)
        return reject(D5PreparationError::InconsistentLegacyCedeRequirement);
    } else if (facts.OwnershipProof != D3OwnershipProof::Trivial ||
               *facts.LegacyRequirement !=
                   LegacyCedeRequirement::ImplicitExempt) {
      return reject(D5PreparationError::InconsistentLegacyCedeRequirement);
    }
  } else if (facts.LegacyRequirement) {
    return reject(D5PreparationError::InconsistentLegacyCedeRequirement);
  }

  if (facts.OwnershipProof == D3OwnershipProof::Owned &&
      !facts.SourceCleanupAuthority)
    return reject(D5PreparationError::IncompleteLiability);
  if (!facts.RegionAuthorityComplete)
    return reject(D5PreparationError::IncompleteRegion);

  D3CallObservationInput input;
  input.Pre = pre;
  input.Post.ActualType = facts.ActualType;
  input.Post.TypeCategory = facts.TypeCategory;
  input.Post.CopyProof = facts.CopyProof;
  input.Post.OwnershipProof = facts.OwnershipProof;
  const auto prospective = classifyProspectiveTransfer(pre, facts.CopyProof);
  input.Post.BoundaryAccess = prospective.Boundary;
  input.Post.CleanupWitness = pre.SourceWitness + ":cleanup";
  input.Post.SourceLiability = deriveSourceLiability(
      pre, facts.OwnershipProof, input.Post.CleanupWitness);
  input.Post.LegacySucceeded = true;
  input.Post.LegacyTypeMismatch = false;
  input.Post.AdmissionFactsComplete = true;
  input.Post.WholePlaceEligible = true;
  input.Post.LiabilityComplete = true;
  input.Post.RegionFactsComplete = true;

  auto prepared = DirectCallObservationFactory::observe(std::move(input));
  if (prepared.admission() != D3AdmissionKind::Admitted ||
      !prepared.validatedCall())
    return reject(D5PreparationError::ConflictingPreparedPlan);
  if (!prepared.validatedCall()->evaluationDelta().entries().empty())
    return reject(D5PreparationError::ConflictingPreparedPlan);

  result.Admission = D3AdmissionKind::Admitted;
  result.PreparedCall = *prepared.validatedCall();
  return result;
}

} // namespace toka
