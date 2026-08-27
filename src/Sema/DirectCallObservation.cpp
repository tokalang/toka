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
                  lhs.CalleeName, lhs.FormalName, lhs.FormalType,
                  lhs.SourceSpelling, lhs.SourceStateBefore, lhs.PALStateBefore,
                  lhs.ActualCategory, lhs.CoreFactsComplete, lhs.FormalCeded,
                  lhs.ExplicitCede, lhs.Generic, lhs.VariadicOrDefault,
                  lhs.MultipleArguments, lhs.InitOrOutcome,
                  lhs.AsyncOrExecutionBoundary,
                  lhs.ReturnDependencyOrRegionEscape, lhs.NestedObservation,
                  lhs.SourcePlaceAlias) ==
             std::tie(rhs.IdentityOrigin, rhs.CallWitness, rhs.CalleeWitness,
                      rhs.FormalWitness, rhs.SourceWitness,
                      rhs.DestinationWitness, rhs.CalleeName, rhs.FormalName,
                      rhs.FormalType, rhs.SourceSpelling, rhs.SourceStateBefore,
                      rhs.PALStateBefore, rhs.ActualCategory,
                      rhs.CoreFactsComplete, rhs.FormalCeded, rhs.ExplicitCede,
                      rhs.Generic, rhs.VariadicOrDefault, rhs.MultipleArguments,
                      rhs.InitOrOutcome, rhs.AsyncOrExecutionBoundary,
                      rhs.ReturnDependencyOrRegionEscape, rhs.NestedObservation,
                      rhs.SourcePlaceAlias) &&
         equalLocation(lhs.CallLocation, rhs.CallLocation) &&
         equalLocation(lhs.FormalLocation, rhs.FormalLocation);
}

bool equalPost(const D3PostLegacyDirectCallFacts &lhs,
               const D3PostLegacyDirectCallFacts &rhs) {
  return std::tie(lhs.ActualType, lhs.LegacyDiagnosticCodes, lhs.TypeCategory,
                  lhs.CopyProof, lhs.LegacySucceeded, lhs.LegacyTypeMismatch,
                  lhs.AdmissionFactsComplete, lhs.WholePlaceEligible,
                  lhs.LiabilityComplete, lhs.RegionFactsComplete) ==
         std::tie(rhs.ActualType, rhs.LegacyDiagnosticCodes, rhs.TypeCategory,
                  rhs.CopyProof, rhs.LegacySucceeded, rhs.LegacyTypeMismatch,
                  rhs.AdmissionFactsComplete, rhs.WholePlaceEligible,
                  rhs.LiabilityComplete, rhs.RegionFactsComplete);
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
  seed = hashText(seed, Dependency);
  seed = hashText(seed, LiabilitySource);
  seed = hashText(seed, LiabilityTarget);
  return combine(seed, ExplicitCede ? 1U : 0U);
}

bool operator==(const D3ValidatedTransferEdge &lhs,
                const D3ValidatedTransferEdge &rhs) {
  return std::tie(lhs.Id, lhs.ArgumentPlan, lhs.Formal, lhs.Destination,
                  lhs.SourcePlace, lhs.TypeProof, lhs.ValueCategory,
                  lhs.Transfer, lhs.Source, lhs.Dependency, lhs.LiabilitySource,
                  lhs.LiabilityTarget, lhs.ExplicitCede) ==
         std::tie(rhs.Id, rhs.ArgumentPlan, rhs.Formal, rhs.Destination,
                  rhs.SourcePlace, rhs.TypeProof, rhs.ValueCategory,
                  rhs.Transfer, rhs.Source, rhs.Dependency, rhs.LiabilitySource,
                  rhs.LiabilityTarget, rhs.ExplicitCede);
}

size_t D3DeltaEntry::hashValue() const noexcept {
  size_t seed = Edge.hashValue();
  seed = hashEnum(seed, Domain);
  seed = hashText(seed, SubjectIdentity);
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
  seed = hashText(seed, Subject);
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
  if (post.TypeCategory == D3TypeCategory::SharedIdentity)
    return exclude(std::move(input), D3ExclusionReason::SharedIdentity);
  if (post.TypeCategory == D3TypeCategory::RawOrReferenceIdentity)
    return exclude(std::move(input), D3ExclusionReason::RawOrReferenceIdentity);
  if (post.TypeCategory == D3TypeCategory::FunctionOrDynIdentity)
    return exclude(std::move(input), D3ExclusionReason::FunctionOrDynIdentity);
  if (post.TypeCategory == D3TypeCategory::Unsupported)
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
  }

  if (post.TypeCategory == D3TypeCategory::Indeterminate)
    return reject(std::move(input),
                  D3CallValidationError::IndeterminateOwnership);
  if (pre.ActualCategory == D3ActualCategory::Indeterminate)
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

  D3TransferMode transfer = D3TransferMode::BorrowCapture;
  D3SourceDisposition source = D3SourceDisposition::KeepLive;
  if (pre.FormalCeded) {
    if (pre.ActualCategory == D3ActualCategory::WholeTemporary) {
      transfer = D3TransferMode::ConsumeTemporary;
      source = D3SourceDisposition::NoSourcePlace;
    } else {
      if (post.CopyProof == D3CopyProof::Indeterminate)
        return reject(std::move(input),
                      D3CallValidationError::IndeterminateCopyProof);
      if (post.CopyProof == D3CopyProof::ProvenCopy) {
        transfer = D3TransferMode::CopyValue;
        source = pre.ExplicitCede ? D3SourceDisposition::InvalidateWhole
                                  : D3SourceDisposition::KeepLive;
      } else {
        transfer = D3TransferMode::MoveOwned;
        source = D3SourceDisposition::InvalidateWhole;
      }
    }
  }

  auto callId = SemanticIdentityBuilder::semanticNode(pre.IdentityOrigin,
                                                      pre.CallWitness);
  auto declarationId = SemanticIdentityBuilder::declaration(pre.IdentityOrigin,
                                                            pre.CalleeWitness);
  if (!callId || !declarationId)
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
  edge.TypeProof =
      std::string(toString(post.TypeCategory)) + "/" + toString(post.CopyProof);
  edge.ValueCategory = toString(pre.ActualCategory);
  edge.Transfer = transfer;
  edge.Source = source;
  edge.Dependency = post.TypeCategory == D3TypeCategory::BorrowedAggregate
                        ? "Borrowed(call-region)"
                        : "None";
  edge.ExplicitCede = pre.ExplicitCede;
  if (pre.ActualCategory == D3ActualCategory::WholePlace) {
    auto rootId = SemanticIdentityBuilder::rootSymbol(declarationId.value(),
                                                      pre.SourceWitness);
    if (!rootId)
      return reject(std::move(input),
                    D3CallValidationError::IncompleteObservationFacts);
    edge.SourcePlace = PlaceId(rootId.value());
  }
  if (transfer == D3TransferMode::MoveOwned ||
      transfer == D3TransferMode::ConsumeTemporary) {
    edge.LiabilitySource = pre.SourceWitness;
    edge.LiabilityTarget = destinationId.value().canonicalKey();
  } else if (transfer == D3TransferMode::CopyValue) {
    edge.LiabilitySource = source == D3SourceDisposition::KeepLive
                               ? pre.SourceWitness
                               : "NoLiability";
    edge.LiabilityTarget = "NoLiability";
  } else {
    edge.LiabilitySource = pre.SourceWitness;
    edge.LiabilityTarget = "SourceRetainsLiability";
  }

  D3ValidatedCall validated;
  validated.CallSite = callId.value();
  validated.Callee = ResolvedCalleeId::direct(declarationId.value());
  validated.TransferEdges.push_back(edge);
  validated.Evaluation.Lane = D3DeltaLane::Evaluation;
  validated.Boundary.Lane = D3DeltaLane::Boundary;
  validated.Finalization.Lane = D3DeltaLane::Finalization;

  auto makeEntry = [&](D3StateDomain domain, std::string subject,
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
        makeEntry(D3StateDomain::Evaluation, pre.SourceWitness, "Absent",
                  "Materialized", "legacy-expression-evaluation"));
  }
  if (source == D3SourceDisposition::InvalidateWhole) {
    validated.Boundary.Entries.push_back(
        makeEntry(D3StateDomain::PlaceState, pre.SourceWitness, "Live", "Moved",
                  "validated-transfer-edge"));
  } else if (transfer == D3TransferMode::BorrowCapture) {
    validated.Boundary.Entries.push_back(
        makeEntry(D3StateDomain::PAL, pre.SourceWitness, pre.PALStateBefore,
                  "BorrowedShared", "validated-transfer-edge"));
    validated.Finalization.Entries.push_back(
        makeEntry(D3StateDomain::PAL, pre.SourceWitness, "BorrowedShared",
                  "Free", "call-region-terminal"));
  }
  if (transfer == D3TransferMode::MoveOwned) {
    validated.Boundary.Entries.push_back(makeEntry(
        D3StateDomain::CleanupLiability, pre.SourceWitness, "SourceOwner",
        "FormalDestinationOwner", "validated-transfer-edge"));
  } else if (transfer == D3TransferMode::ConsumeTemporary) {
    validated.Finalization.Entries.push_back(
        makeEntry(D3StateDomain::CleanupLiability, pre.SourceWitness, "Armed",
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
  validated.RegionWitness.Subject = pre.SourceWitness;
  validated.RegionWitness.Terminal =
      transfer == D3TransferMode::BorrowCapture
          ? "EndBoundaryLoan"
          : (transfer == D3TransferMode::ConsumeTemporary
                 ? "DisarmTemporaryCleanup"
                 : "NoLocalRegionObligation");

  std::vector<D3PatchEntrySeed> patchEntries;
  auto appendPatchEntries = [&](const D3DomainDelta &delta) {
    for (const auto &entry : delta.entries()) {
      const std::string witness = std::string(toString(delta.lane())) + ":" +
                                  toString(entry.stateDomain()) + ":" +
                                  entry.subjectIdentity();
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

} // namespace toka
