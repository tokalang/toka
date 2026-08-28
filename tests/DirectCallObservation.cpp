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
  input.Pre.SourceIsLocalPlace = true;
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
  LegacyCedePolicyInput policy;
  policy.TypeCategory = D3TypeCategory::Aggregate;
  policy.CanonicalSoul = "SlabID";
  policy.DropFact = LegacyCedeDropFact::HasDrop;
  CHECK(classifyLegacyCedeRequirement(policy) ==
        LegacyCedeRequirement::ImplicitExempt);
  policy.CanonicalSoul = "Resource";
  CHECK(classifyLegacyCedeRequirement(policy) ==
        LegacyCedeRequirement::ExplicitRequired);
  const char *explicitLegacyNames[] = {
      "str", "bytes", "cstr", "ViewStrSplitIterator",
      "ViewStrLinesIterator", "string", "TimerHeap"};
  for (const char *name : explicitLegacyNames) {
    policy.CanonicalSoul = name;
    policy.DropFact = LegacyCedeDropFact::Indeterminate;
    CHECK(classifyLegacyCedeRequirement(policy) ==
          LegacyCedeRequirement::ExplicitRequired);
  }
  policy.CanonicalSoul = "Pair";
  policy.DropFact = LegacyCedeDropFact::NoDrop;
  CHECK(classifyLegacyCedeRequirement(policy) ==
        LegacyCedeRequirement::ImplicitExempt);
  policy.DropFact = LegacyCedeDropFact::Indeterminate;
  CHECK(classifyLegacyCedeRequirement(policy) ==
        LegacyCedeRequirement::Indeterminate);

  auto preparedFacts = [] {
    D5ResolvedPlanningFacts facts;
    facts.Pre = baseInput().Pre;
    facts.ActualType = "Resource";
    facts.FormalType = "Resource";
    facts.TypeCategory = D3TypeCategory::Aggregate;
    facts.CopyProof = D3CopyProof::ProvenNonCopy;
    facts.OwnershipProof = D3OwnershipProof::Owned;
    facts.LegacyRequirement = LegacyCedeRequirement::ExplicitRequired;
    facts.SourceInitMask = 1;
    return facts;
  };
  auto preparedNonCopy = PreparedCallFactory::prepare(preparedFacts());
  CHECK(preparedNonCopy.admission() == D3AdmissionKind::Admitted);
  CHECK(preparedNonCopy.preparedCall());
  CHECK(preparedNonCopy.preparedCall()->transferEdges()[0].transferMode() ==
        D3TransferMode::MoveOwned);
  CHECK(validateD5PreparedResultShape(D3AdmissionKind::Admitted, true, 1, 0));
  CHECK(!validateD5PreparedResultShape(D3AdmissionKind::Admitted, false, 0,
                                       0));
  CHECK(validateD5PreparedResultShape(D3AdmissionKind::Rejected, false, 0, 0));
  auto preparedExplicitFacts = preparedFacts();
  preparedExplicitFacts.Pre.ExplicitCede = true;
  auto preparedExplicit =
      PreparedCallFactory::prepare(preparedExplicitFacts);
  CHECK(preparedExplicit.admission() == D3AdmissionKind::Admitted);
  auto slabFacts = preparedFacts();
  slabFacts.LegacyRequirement = LegacyCedeRequirement::ImplicitExempt;
  auto preparedSlab = PreparedCallFactory::prepare(slabFacts);
  CHECK(preparedSlab.admission() == D3AdmissionKind::NotInSlice);
  CHECK(preparedSlab.exclusionReason() ==
        D5PreparationExclusionReason::CededNonCopyLegacyExempt);
  auto noDropManagedFacts = slabFacts;
  noDropManagedFacts.OwnershipProof = D3OwnershipProof::Trivial;
  CHECK(PreparedCallFactory::prepare(noDropManagedFacts).admission() ==
        D3AdmissionKind::NotInSlice);
  auto indeterminatePolicyFacts = preparedFacts();
  indeterminatePolicyFacts.LegacyRequirement =
      LegacyCedeRequirement::Indeterminate;
  CHECK(PreparedCallFactory::prepare(indeterminatePolicyFacts)
            .preparationError() ==
        D5PreparationError::IndeterminateLegacyCedeRequirement);

  struct D5ExclusionFixture {
    D5PreparationExclusionReason Reason;
    void (*Mutate)(D5ResolvedPlanningFacts &);
  };
  const D5ExclusionFixture d5Exclusions[] = {
      {D5PreparationExclusionReason::ArityOrDefault,
       [](auto &v) { v.Pre.MultipleArguments = true; }},
      {D5PreparationExclusionReason::GenericOrContextual,
       [](auto &v) { v.Pre.Generic = true; }},
      {D5PreparationExclusionReason::InitOrOutcome,
       [](auto &v) { v.Pre.InitOrOutcome = true; }},
      {D5PreparationExclusionReason::AsyncOrExecutionBoundary,
       [](auto &v) { v.Pre.AsyncOrExecutionBoundary = true; }},
      {D5PreparationExclusionReason::ReturnDependencyOrRegionEscape,
       [](auto &v) { v.Pre.ReturnDependencyOrRegionEscape = true; }},
      {D5PreparationExclusionReason::ProjectionOrTemporary,
       [](auto &v) { v.Pre.ActualCategory = D3ActualCategory::Projection; }},
      {D5PreparationExclusionReason::NonLocalPlace,
       [](auto &v) { v.Pre.SourceIsLocalPlace = false; }},
      {D5PreparationExclusionReason::SharedRawReferenceOrCallable,
       [](auto &v) { v.TypeCategory = D3TypeCategory::SharedIdentity; }},
      {D5PreparationExclusionReason::DependencyBearingActual,
       [](auto &v) { v.DependencyBearingActual = true; }},
      {D5PreparationExclusionReason::TypeRequiresContextOrConversion,
       [](auto &v) { v.ActualType = "Other"; }},
      {D5PreparationExclusionReason::CededNonCopyLegacyExempt,
       [](auto &v) {
         v.LegacyRequirement = LegacyCedeRequirement::ImplicitExempt;
       }},
  };
  for (const auto &fixture : d5Exclusions) {
    auto facts = preparedFacts();
    fixture.Mutate(facts);
    auto result = PreparedCallFactory::prepare(std::move(facts));
    CHECK(result.admission() == D3AdmissionKind::NotInSlice);
    CHECK(result.exclusionReason() == fixture.Reason);
    CHECK(!result.preparedCall());
  }

  struct D5ErrorFixture {
    D5PreparationError Error;
    void (*Mutate)(D5ResolvedPlanningFacts &);
  };
  const D5ErrorFixture d5Errors[] = {
      {D5PreparationError::InvalidIdentity,
       [](auto &v) { v.Pre.CoreFactsComplete = false; }},
      {D5PreparationError::IndeterminateCopyProof,
       [](auto &v) { v.CopyProof = D3CopyProof::Indeterminate; }},
      {D5PreparationError::IndeterminateOwnership,
       [](auto &v) { v.OwnershipProof = D3OwnershipProof::Indeterminate; }},
      {D5PreparationError::IndeterminateLegacyCedeRequirement,
       [](auto &v) {
         v.LegacyRequirement = LegacyCedeRequirement::Indeterminate;
       }},
      {D5PreparationError::InconsistentLegacyCedeRequirement,
       [](auto &v) {
         v.CopyProof = D3CopyProof::ProvenCopy;
         v.OwnershipProof = D3OwnershipProof::Trivial;
         v.LegacyRequirement = LegacyCedeRequirement::ExplicitRequired;
       }},
      {D5PreparationError::InvalidWholePlaceAdmission,
       [](auto &v) { v.Pre.SourceStateBefore = "Moved"; }},
      {D5PreparationError::IncompleteLiability,
       [](auto &v) { v.SourceInitMask = 0; }},
      {D5PreparationError::IncompleteRegion,
       [](auto &v) { v.Pre.CallLocation = {}; }},
      {D5PreparationError::ConflictingPreparedPlan,
       [](auto &v) {
         v.Pre.FormalCeded = false;
         v.LegacyRequirement.reset();
         v.CopyProof = D3CopyProof::ProvenCopy;
       }},
  };
  for (const auto &fixture : d5Errors) {
    auto facts = preparedFacts();
    fixture.Mutate(facts);
    auto result = PreparedCallFactory::prepare(std::move(facts));
    CHECK(result.admission() == D3AdmissionKind::Rejected);
    CHECK(result.preparationError() == fixture.Error);
    CHECK(!result.preparedCall());
  }

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

  auto ownedWithoutLiability = baseInput();
  ownedWithoutLiability.Post.SourceLiability = D3LiabilityFact::noLiability();
  CHECK(isRejected(DirectCallObservationFactory::observe(ownedWithoutLiability),
                   D3CallValidationError::IncompleteObservationFacts));

  auto copyWithCleanup = baseInput();
  copyWithCleanup.Post.CopyProof = D3CopyProof::ProvenCopy;
  copyWithCleanup.Post.OwnershipProof = D3OwnershipProof::Trivial;
  CHECK(isRejected(DirectCallObservationFactory::observe(copyWithCleanup),
                   D3CallValidationError::IncompleteObservationFacts));

  auto temporaryWithPlaceCleanup = baseInput();
  temporaryWithPlaceCleanup.Pre.ActualCategory =
      D3ActualCategory::WholeTemporary;
  temporaryWithPlaceCleanup.Post.BoundaryAccess = D3BoundaryAccess::None;
  temporaryWithPlaceCleanup.Post.WholePlaceEligible = false;
  CHECK(isRejected(
      DirectCallObservationFactory::observe(temporaryWithPlaceCleanup),
      D3CallValidationError::IncompleteObservationFacts));

  auto moveWithoutInvalidation = baseInput();
  moveWithoutInvalidation.Post.BoundaryAccess = D3BoundaryAccess::SharedBorrow;
  CHECK(
      isRejected(DirectCallObservationFactory::observe(moveWithoutInvalidation),
                 D3CallValidationError::InvalidWholePlaceAdmission));

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
  auto copyBareWithoutBorrow = copyBareInput;
  copyBareWithoutBorrow.Post.BoundaryAccess = D3BoundaryAccess::None;
  CHECK(isRejected(DirectCallObservationFactory::observe(copyBareWithoutBorrow),
                   D3CallValidationError::InvalidWholePlaceAdmission));
  auto copyBareWithInvalidation = copyBareInput;
  copyBareWithInvalidation.Post.BoundaryAccess = D3BoundaryAccess::Invalidation;
  CHECK(isRejected(
      DirectCallObservationFactory::observe(copyBareWithInvalidation),
      D3CallValidationError::InvalidWholePlaceAdmission));
  auto scalarNonCopy = copyBareInput;
  scalarNonCopy.Post.CopyProof = D3CopyProof::ProvenNonCopy;
  CHECK(isRejected(DirectCallObservationFactory::observe(scalarNonCopy),
                   D3CallValidationError::IncompleteObservationFacts));

  auto copyExplicitInput = copyBareInput;
  copyExplicitInput.Pre.ExplicitCede = true;
  copyExplicitInput.Post.BoundaryAccess = D3BoundaryAccess::Invalidation;
  auto copyExplicit = DirectCallObservationFactory::observe(copyExplicitInput);
  CHECK(hasTransfer(copyExplicit, D3TransferMode::CopyValue,
                    D3SourceDisposition::InvalidateWhole));
  auto copyExplicitWithoutInvalidation = copyExplicitInput;
  copyExplicitWithoutInvalidation.Post.BoundaryAccess =
      D3BoundaryAccess::SharedBorrow;
  CHECK(isRejected(
      DirectCallObservationFactory::observe(copyExplicitWithoutInvalidation),
      D3CallValidationError::InvalidWholePlaceAdmission));

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
  auto temporaryWithBoundary = temporaryInput;
  temporaryWithBoundary.Post.BoundaryAccess = D3BoundaryAccess::Invalidation;
  CHECK(isRejected(DirectCallObservationFactory::observe(temporaryWithBoundary),
                   D3CallValidationError::IncompleteObservationFacts));

  auto borrowedInput = baseInput();
  borrowedInput.Pre.FormalCeded = false;
  borrowedInput.Post.TypeCategory = D3TypeCategory::Aggregate;
  borrowedInput.Post.CopyProof = D3CopyProof::Indeterminate;
  borrowedInput.Post.BoundaryAccess = D3BoundaryAccess::SharedBorrow;
  auto borrowed = DirectCallObservationFactory::observe(borrowedInput);
  CHECK(hasTransfer(borrowed, D3TransferMode::BorrowCapture,
                    D3SourceDisposition::KeepLive));
  auto borrowWithoutSharedAccess = borrowedInput;
  borrowWithoutSharedAccess.Post.BoundaryAccess =
      D3BoundaryAccess::Invalidation;
  CHECK(isRejected(
      DirectCallObservationFactory::observe(borrowWithoutSharedAccess),
      D3CallValidationError::InvalidWholePlaceAdmission));

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

  auto ownedIdentityInput = baseInput();
  ownedIdentityInput.Post.TypeCategory = D3TypeCategory::OwnedIdentity;
  CHECK(hasTransfer(DirectCallObservationFactory::observe(ownedIdentityInput),
                    D3TransferMode::MoveOwned,
                    D3SourceDisposition::InvalidateWhole));
  auto ownedIdentityCopy = ownedIdentityInput;
  ownedIdentityCopy.Post.CopyProof = D3CopyProof::ProvenCopy;
  CHECK(isRejected(DirectCallObservationFactory::observe(ownedIdentityCopy),
                   D3CallValidationError::IncompleteObservationFacts));
  auto borrowedOwnedIdentity = ownedIdentityInput;
  borrowedOwnedIdentity.Pre.FormalCeded = false;
  borrowedOwnedIdentity.Post.BoundaryAccess = D3BoundaryAccess::SharedBorrow;
  CHECK(isExcluded(DirectCallObservationFactory::observe(borrowedOwnedIdentity),
                   D3ExclusionReason::UnsupportedTypeCategory));

  auto nonLocalInput = baseInput();
  nonLocalInput.Pre.SourceIsLocalPlace = false;
  CHECK(isExcluded(DirectCallObservationFactory::observe(nonLocalInput),
                   D3ExclusionReason::NonLocalPlace));

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
      {D3ExclusionReason::NonLocalPlace,
       [](auto &v) { v.Pre.SourceIsLocalPlace = false; }},
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
