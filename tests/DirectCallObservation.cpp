// Copyright (c) 2026 YiZhonghua<zhyi@dpai.com>. All rights reserved.

#include "toka/DirectCallObservationAudit.h"
#include <sstream>
#include <type_traits>
#include <unordered_set>

using namespace toka;

bool g_JsonDiagnostics = false;

static_assert(!std::is_default_constructible_v<D3ValidatedTransferEdge>);
static_assert(!std::is_default_constructible_v<D3DeltaEntry>);
static_assert(!std::is_default_constructible_v<D3DomainDelta>);
static_assert(!std::is_default_constructible_v<D3SemanticModelPatch>);
static_assert(!std::is_default_constructible_v<D3MinimalRegionWitness>);
static_assert(!std::is_default_constructible_v<D3ValidatedCall>);
static_assert(!std::is_default_constructible_v<D3FactoryObservationRecord>);

#define CHECK(condition)                                                       \
  do {                                                                         \
    if (!(condition))                                                          \
      return __LINE__;                                                         \
  } while (false)

template <typename Value> struct ConstantHash {
  size_t operator()(const Value &) const noexcept { return 0; }
};

static D3CallObservationInput baseInput() {
  D3CallObservationInput input;
  input.Pre.IdentityOrigin = "/test/d3.tk";
  input.Pre.CallWitness = "12:5:consume";
  input.Pre.CalleeWitness = "2:1:consume";
  input.Pre.FormalWitness = "arg:1:value";
  input.Pre.SourceWitness = "10:5:value";
  input.Pre.DestinationWitness = "formal:1";
  input.Pre.CallerBindingOwnerWitness = "8:1:main";
  input.Pre.CalleeName = "consume";
  input.Pre.FormalName = "value";
  input.Pre.FormalType = "Resource";
  input.Pre.SourceSpelling = "value";
  input.Pre.SourceStateBefore = "Live";
  input.Pre.PALStateBefore = "Free";
  input.Pre.CallLocation = {"/test/d3.tk", 12, 5};
  input.Pre.FormalLocation = {"/test/d3.tk", 2, 12};
  input.Pre.ActualCategory = D3ActualCategory::WholePlace;
  input.Pre.CoreFactsComplete = true;
  input.Pre.FormalCeded = true;
  input.Post.ActualType = "Resource";
  input.Post.TypeCategory = D3TypeCategory::Aggregate;
  input.Post.CopyProof = D3CopyProof::ProvenNonCopy;
  input.Post.OwnershipProof = D3OwnershipProof::Owned;
  input.Post.BoundaryAccess = D3BoundaryAccess::Invalidation;
  input.Post.CleanupWitness = "10:5:value:cleanup";
  input.Post.SourceLiability =
      D3LiabilityFact::sourcePlace(input.Post.CleanupWitness);
  input.Post.LegacySucceeded = true;
  input.Post.AdmissionFactsComplete = true;
  input.Post.WholePlaceEligible = true;
  input.Post.LiabilityComplete = true;
  input.Post.RegionFactsComplete = true;
  return input;
}

static bool hasTransfer(const D3FactoryObservationRecord &record,
                        D3TransferMode transfer, D3SourceDisposition source) {
  return record.admission() == D3AdmissionKind::Admitted &&
         record.validatedCall() &&
         record.validatedCall()->transferEdges().size() == 1 &&
         record.validatedCall()->transferEdges()[0].transferMode() ==
             transfer &&
         record.validatedCall()->transferEdges()[0].sourceDisposition() ==
             source;
}

static bool isExcluded(const D3FactoryObservationRecord &record,
                       D3ExclusionReason reason) {
  return record.admission() == D3AdmissionKind::NotInSlice &&
         record.exclusionReason() && *record.exclusionReason() == reason &&
         !record.validatedCall();
}

static bool isRejected(const D3FactoryObservationRecord &record,
                       D3CallValidationError error) {
  return record.admission() == D3AdmissionKind::Rejected &&
         record.validationError() && *record.validationError() == error &&
         !record.validatedCall();
}

int main() {
  auto nonCopy = DirectCallObservationFactory::observe(baseInput());
  CHECK(hasTransfer(nonCopy, D3TransferMode::MoveOwned,
                    D3SourceDisposition::InvalidateWhole));

  auto explicitNonCopyInput = baseInput();
  explicitNonCopyInput.Pre.ExplicitCede = true;
  auto explicitNonCopy =
      DirectCallObservationFactory::observe(explicitNonCopyInput);
  CHECK(hasTransfer(explicitNonCopy, D3TransferMode::MoveOwned,
                    D3SourceDisposition::InvalidateWhole));
  CHECK(nonCopy.validatedCall()->transferEdges()[0].liabilitySource() ==
        explicitNonCopy.validatedCall()->transferEdges()[0].liabilitySource());

  auto supersededSpellingInput = baseInput();
  supersededSpellingInput.Post.LegacySucceeded = false;
  supersededSpellingInput.Post.LegacyDiagnosticCodes = {"E04570"};
  CHECK(hasTransfer(
      DirectCallObservationFactory::observe(supersededSpellingInput),
      D3TransferMode::MoveOwned, D3SourceDisposition::InvalidateWhole));

  auto unrelatedLegacyFailureInput = baseInput();
  unrelatedLegacyFailureInput.Post.LegacySucceeded = false;
  unrelatedLegacyFailureInput.Post.LegacyDiagnosticCodes = {"E09999"};
  CHECK(isRejected(
      DirectCallObservationFactory::observe(unrelatedLegacyFailureInput),
      D3CallValidationError::IncompleteObservationFacts));

  auto otherCalleeInput = baseInput();
  otherCalleeInput.Pre.CalleeWitness = "3:1:consume_other";
  otherCalleeInput.Pre.CalleeName = "consume_other";
  otherCalleeInput.Pre.FormalWitness = "3:20:value";
  otherCalleeInput.Pre.DestinationWitness = "formal:1:other";
  auto otherCallee = DirectCallObservationFactory::observe(otherCalleeInput);
  CHECK(otherCallee.validatedCall());
  CHECK(nonCopy.validatedCall()->transferEdges()[0].sourcePlace() ==
        otherCallee.validatedCall()->transferEdges()[0].sourcePlace());

  auto copyBareInput = baseInput();
  copyBareInput.Pre.FormalType = "i32";
  copyBareInput.Post.ActualType = "i32";
  copyBareInput.Post.TypeCategory = D3TypeCategory::Scalar;
  copyBareInput.Post.CopyProof = D3CopyProof::ProvenCopy;
  copyBareInput.Post.OwnershipProof = D3OwnershipProof::Trivial;
  copyBareInput.Post.BoundaryAccess = D3BoundaryAccess::SharedBorrow;
  copyBareInput.Post.SourceLiability = D3LiabilityFact::noLiability();
  auto copyBare = DirectCallObservationFactory::observe(copyBareInput);
  CHECK(hasTransfer(copyBare, D3TransferMode::CopyValue,
                    D3SourceDisposition::KeepLive));

  auto copyExplicitInput = copyBareInput;
  copyExplicitInput.Pre.ExplicitCede = true;
  copyExplicitInput.Post.BoundaryAccess = D3BoundaryAccess::Invalidation;
  auto copyExplicit = DirectCallObservationFactory::observe(copyExplicitInput);
  CHECK(hasTransfer(copyExplicit, D3TransferMode::CopyValue,
                    D3SourceDisposition::InvalidateWhole));

  auto temporaryInput = baseInput();
  temporaryInput.Pre.ActualCategory = D3ActualCategory::WholeTemporary;
  temporaryInput.Pre.SourceWitness = "temporary:Resource";
  temporaryInput.Post.WholePlaceEligible = false;
  temporaryInput.Post.CopyProof = D3CopyProof::Indeterminate;
  temporaryInput.Post.BoundaryAccess = D3BoundaryAccess::None;
  temporaryInput.Post.SourceLiability =
      D3LiabilityFact::temporary("temporary:Resource:cleanup");
  temporaryInput.Post.CleanupWitness = "temporary:Resource:cleanup";
  auto temporary = DirectCallObservationFactory::observe(temporaryInput);
  CHECK(hasTransfer(temporary, D3TransferMode::ConsumeTemporary,
                    D3SourceDisposition::NoSourcePlace));

  auto borrowedInput = baseInput();
  borrowedInput.Pre.FormalCeded = false;
  borrowedInput.Post.TypeCategory = D3TypeCategory::Aggregate;
  borrowedInput.Post.CopyProof = D3CopyProof::Indeterminate;
  borrowedInput.Post.BoundaryAccess = D3BoundaryAccess::SharedBorrow;
  auto borrowed = DirectCallObservationFactory::observe(borrowedInput);
  CHECK(hasTransfer(borrowed, D3TransferMode::BorrowCapture,
                    D3SourceDisposition::KeepLive));

  auto borrowedExplicitInput = borrowedInput;
  borrowedExplicitInput.Pre.ExplicitCede = true;
  CHECK(isRejected(DirectCallObservationFactory::observe(borrowedExplicitInput),
                   D3CallValidationError::BorrowedFormalExplicitCede));

  auto cededBorrowedInput = baseInput();
  cededBorrowedInput.Post.TypeCategory = D3TypeCategory::BorrowedAggregate;
  cededBorrowedInput.Post.OwnershipProof = D3OwnershipProof::Borrowed;
  cededBorrowedInput.Post.SourceLiability = D3LiabilityFact::noLiability();
  CHECK(isExcluded(DirectCallObservationFactory::observe(cededBorrowedInput),
                   D3ExclusionReason::UnsupportedTypeCategory));

  auto scalarInput = copyBareInput;
  scalarInput.Pre.FormalCeded = false;
  CHECK(isExcluded(DirectCallObservationFactory::observe(scalarInput),
                   D3ExclusionReason::NonCedeScalar));

  auto aggregateTemporaryInput = temporaryInput;
  aggregateTemporaryInput.Pre.FormalCeded = false;
  CHECK(
      isExcluded(DirectCallObservationFactory::observe(aggregateTemporaryInput),
                 D3ExclusionReason::NonCedeAggregateTemporary));

  auto invalidPlaceInput = baseInput();
  invalidPlaceInput.Post.WholePlaceEligible = false;
  CHECK(isRejected(DirectCallObservationFactory::observe(invalidPlaceInput),
                   D3CallValidationError::InvalidWholePlaceAdmission));

  auto mismatchInput = baseInput();
  mismatchInput.Post.LegacyTypeMismatch = true;
  CHECK(isRejected(DirectCallObservationFactory::observe(mismatchInput),
                   D3CallValidationError::LegacyTypeMismatch));

  auto indeterminateCopyInput = copyBareInput;
  indeterminateCopyInput.Post.CopyProof = D3CopyProof::Indeterminate;
  CHECK(
      isRejected(DirectCallObservationFactory::observe(indeterminateCopyInput),
                 D3CallValidationError::IndeterminateCopyProof));

  auto indeterminateOwnershipInput = baseInput();
  indeterminateOwnershipInput.Post.TypeCategory = D3TypeCategory::Indeterminate;
  CHECK(isRejected(
      DirectCallObservationFactory::observe(indeterminateOwnershipInput),
      D3CallValidationError::IndeterminateOwnership));

  auto incompleteInput = baseInput();
  incompleteInput.Pre.CoreFactsComplete = false;
  incompleteInput.Pre.Generic = true;
  CHECK(isRejected(DirectCallObservationFactory::observe(incompleteInput),
                   D3CallValidationError::IncompleteObservationFacts));

  struct ExclusionFixture {
    D3ExclusionReason Reason;
    void (*Mutate)(D3CallObservationInput &);
  };
  const ExclusionFixture exclusions[] = {
      {D3ExclusionReason::Generic, [](auto &v) { v.Pre.Generic = true; }},
      {D3ExclusionReason::VariadicOrDefault,
       [](auto &v) { v.Pre.VariadicOrDefault = true; }},
      {D3ExclusionReason::MultipleArguments,
       [](auto &v) { v.Pre.MultipleArguments = true; }},
      {D3ExclusionReason::InitOrOutcome,
       [](auto &v) { v.Pre.InitOrOutcome = true; }},
      {D3ExclusionReason::AsyncOrExecutionBoundary,
       [](auto &v) { v.Pre.AsyncOrExecutionBoundary = true; }},
      {D3ExclusionReason::ReturnDependencyOrRegionEscape,
       [](auto &v) { v.Pre.ReturnDependencyOrRegionEscape = true; }},
      {D3ExclusionReason::NestedObservation,
       [](auto &v) { v.Pre.NestedObservation = true; }},
      {D3ExclusionReason::Projection,
       [](auto &v) { v.Pre.ActualCategory = D3ActualCategory::Projection; }},
      {D3ExclusionReason::SharedIdentity,
       [](auto &v) { v.Post.TypeCategory = D3TypeCategory::SharedIdentity; }},
      {D3ExclusionReason::RawOrReferenceIdentity,
       [](auto &v) {
         v.Post.TypeCategory = D3TypeCategory::RawOrReferenceIdentity;
       }},
      {D3ExclusionReason::FunctionOrDynIdentity,
       [](auto &v) {
         v.Post.TypeCategory = D3TypeCategory::FunctionOrDynIdentity;
       }},
      {D3ExclusionReason::UnsupportedTypeCategory,
       [](auto &v) { v.Post.TypeCategory = D3TypeCategory::Unsupported; }},
  };
  for (const auto &fixture : exclusions) {
    auto input = baseInput();
    fixture.Mutate(input);
    // Structural exclusions win over admitted-path proof completeness.
    input.Post.AdmissionFactsComplete = false;
    input.Post.CopyProof = D3CopyProof::Indeterminate;
    CHECK(isExcluded(DirectCallObservationFactory::observe(input),
                     fixture.Reason));
  }

  // Repeated and reordered queries have no hidden cache authority.
  const auto copyFirst = DirectCallObservationFactory::observe(copyBareInput);
  (void)DirectCallObservationFactory::observe(temporaryInput);
  const auto copyAfterOther =
      DirectCallObservationFactory::observe(copyBareInput);
  CHECK(copyFirst == copyAfterOther);

  const auto callA = copyBare.validatedCall()->callSiteId();
  auto otherCallResult =
      SemanticIdentityBuilder::semanticNode("/test/d3.tk", "99:1:consume");
  CHECK(otherCallResult);
  std::unordered_set<SemanticNodeId, ConstantHash<SemanticNodeId>> identities;
  identities.insert(callA);
  identities.insert(otherCallResult.value());
  CHECK(identities.size() == 2);

  std::unordered_set<D3ValidatedTransferEdge,
                     ConstantHash<D3ValidatedTransferEdge>>
      edges;
  edges.insert(copyBare.validatedCall()->transferEdges()[0]);
  edges.insert(copyExplicit.validatedCall()->transferEdges()[0]);
  CHECK(edges.size() == 2);

  std::unordered_set<D3DeltaEntry, ConstantHash<D3DeltaEntry>> deltas;
  deltas.insert(nonCopy.validatedCall()->boundaryDelta().entries()[0]);
  deltas.insert(borrowed.validatedCall()->boundaryDelta().entries()[0]);
  CHECK(deltas.size() == 2);

  std::unordered_set<D3SemanticModelPatch, ConstantHash<D3SemanticModelPatch>>
      patches;
  patches.insert(nonCopy.validatedCall()->semanticModelPatch());
  patches.insert(borrowed.validatedCall()->semanticModelPatch());
  CHECK(patches.size() == 2);

  auto otherSourceInput = temporaryInput;
  otherSourceInput.Pre.SourceWitness = "temporary:OtherResource";
  otherSourceInput.Post.CleanupWitness = "temporary:OtherResource:cleanup";
  otherSourceInput.Post.SourceLiability =
      D3LiabilityFact::temporary(otherSourceInput.Post.CleanupWitness);
  auto otherSource = DirectCallObservationFactory::observe(otherSourceInput);
  std::unordered_set<D3MinimalRegionWitness,
                     ConstantHash<D3MinimalRegionWitness>>
      regions;
  regions.insert(temporary.validatedCall()->regionWitness());
  regions.insert(otherSource.validatedCall()->regionWitness());
  CHECK(regions.size() == 2);

  std::unordered_set<D3FactoryObservationRecord,
                     ConstantHash<D3FactoryObservationRecord>>
      receipts;
  receipts.insert(copyBare);
  receipts.insert(copyExplicit);
  CHECK(receipts.size() == 2);

  auto patchIdentity = SemanticIdentityBuilder::patchEntry(
      nonCopy.validatedCall()->transferEdges()[0].id(), "collision");
  CHECK(patchIdentity);
  auto conflictingPatch = buildD3SemanticModelPatch(
      {{patchIdentity.value(), "first"}, {patchIdentity.value(), "second"}});
  CHECK(!conflictingPatch &&
        conflictingPatch.Error == D3PatchBuildError::ConflictingPayload);
  auto idempotentPatch = buildD3SemanticModelPatch(
      {{patchIdentity.value(), "same"}, {patchIdentity.value(), "same"}});
  CHECK(idempotentPatch && idempotentPatch.Patch.entries().size() == 1);

  D3ObservationSession emptySession;
  std::ostringstream emptyJSON;
  emptySession.dumpJSON(emptyJSON);
  CHECK(emptyJSON.str().find("\"considered_call_count\":0") !=
        std::string::npos);
  CHECK(emptyJSON.str().find("\"integrity\":true") != std::string::npos);

  D3ObservationSession scopedSession;
  {
    D3ObservationScope scope(&scopedSession);
    CHECK(scopedSession.hasActiveObservation());
    D3ObservationScope moved(std::move(scope));
    CHECK(scopedSession.hasActiveObservation());
  }
  CHECK(!scopedSession.hasActiveObservation());
  CHECK(scopedSession.consideredCallCount() == 1);
  CHECK(scopedSession.claimFinalTraversal("call:17"));
  CHECK(!scopedSession.claimFinalTraversal("call:17"));
  scopedSession.noteGateExclusion(D3GateExclusionReason::WrongRoute);
  scopedSession.noteGateExclusion(D3GateExclusionReason::NonSameLexical);
  scopedSession.noteGateExclusion(
      D3GateExclusionReason::CandidateProbeOrSpeculativeContext);
  scopedSession.noteGateExclusion(
      D3GateExclusionReason::NonFinalSemanticTraversal);
  scopedSession.noteGateExclusion(
      D3GateExclusionReason::NestedObservationContext);

  D3ObservationSentinel before;
  D3ObservationSentinel after;
  after.SourceMoved = true;
  after.DiagnosticCodes.push_back("E0001");
  auto differences = differingD3SentinelFields(before, after);
  CHECK(differences.size() == 2);
  CHECK(differences[0] == "source_moved");
  CHECK(differences[1] == "diagnostic_codes");

  return 0;
}
