// Copyright (c) 2026 YiZhonghua<zhyi@dpai.com>. All rights reserved.

#include "toka/ExplicitCedePlan.h"

using namespace toka;

#define CHECK(condition)                                                       \
  do {                                                                         \
    if (!(condition))                                                          \
      return __LINE__;                                                         \
  } while (false)

ExplicitCedePreparedFacts baseCall() {
  ExplicitCedePreparedFacts facts;
  facts.ActualTypeKey = "type:Resource";
  facts.FormalTypeKey = "type:Resource";
  facts.SemanticRootKey = "module:m;fn:f;binding:value";
  facts.ExactPath = "value";
  facts.SourceCategory = TransferSourceCategory::NamedSourcePlace;
  facts.SourceView = TransferSourceView::DirectValue;
  facts.Ownership = TransferOwnershipKind::OwnedValue;
  facts.CopyProof = TransferCopyProof::ProvenNonCopy;
  facts.FormalContract = TransferFormalContract::Cede;
  facts.FormalMorphology = TransferFormalMorphology::DirectValue;
  facts.FormalCapabilities.Complete = true;
  facts.ActualCapabilities.Complete = true;
  facts.Destination = TransferDestination::CalleeParameter;
  facts.Eligibility = TransferEligibility::Eligible;
  facts.Reachability = TransferReachability::ExactSubtree;
  facts.SourceTransferAuthorized = true;
  facts.CarriesDropLiability = true;
  return facts;
}

int main() {
  auto named = baseCall();
  auto missing = prepareExplicitCedePlan(named);
  CHECK(!missing.admitted());
  CHECK(missing.Rejection == TransferPlanRejection::MissingCedeForNamedSource);
  CHECK(missing.Source == TransferSourceDisposition::NoStateChange);

  named.SurfaceSpelling = TransferSurfaceSpelling::ExplicitCede;
  named.SyntaxPurpose = CedeSyntaxPurpose::SourceInvalidation;
  auto moved = prepareExplicitCedePlan(named);
  CHECK(moved.admitted());
  CHECK(moved.ValueProduction == TransferValueProduction::MoveOwned);
  CHECK(moved.Source == TransferSourceDisposition::InvalidateSubtree);
  CHECK(moved.Drop == TransferDropDisposition::DestinationAssumesLiability);
  CHECK(moved.ObligationAction == TransferObligationAction::CreateForCallee);
  CHECK(moved.ObligationAfter == TransferObligationState::Outstanding);

  auto copy = named;
  copy.Ownership = TransferOwnershipKind::PlainValue;
  copy.CopyProof = TransferCopyProof::ProvenCopy;
  copy.CarriesDropLiability = false;
  auto copied = prepareExplicitCedePlan(copy);
  CHECK(copied.admitted());
  CHECK(copied.ValueProduction == TransferValueProduction::CopyValue);
  CHECK(copied.Source == TransferSourceDisposition::InvalidateSubtree);

  auto ordinary = named;
  ordinary.FormalContract = TransferFormalContract::Ordinary;
  CHECK(prepareExplicitCedePlan(ordinary).Rejection ==
        TransferPlanRejection::ExplicitCedeToOrdinaryFormal);

  auto payloadRead = ordinary;
  payloadRead.SurfaceSpelling = TransferSurfaceSpelling::Bare;
  payloadRead.SyntaxPurpose = CedeSyntaxPurpose::None;
  payloadRead.SourceView = TransferSourceView::DereferencedOwningPayload;
  auto borrowedPayload = prepareExplicitCedePlan(payloadRead);
  CHECK(borrowedPayload.admitted());
  CHECK(borrowedPayload.ValueProduction ==
        TransferValueProduction::BorrowCapture);
  CHECK(borrowedPayload.Source == TransferSourceDisposition::KeepLive);

  auto missingCapability = payloadRead;
  missingCapability.FormalCapabilities.PayloadWritable = true;
  CHECK(prepareExplicitCedePlan(missingCapability).Rejection ==
        TransferPlanRejection::AccessCapabilityMismatch);

  auto temporary = baseCall();
  temporary.Origin = TransferPlanOrigin::CompilerSynthetic;
  temporary.SourceCategory = TransferSourceCategory::NoSourcePlace;
  temporary.SemanticRootKey.clear();
  temporary.ExactPath.clear();
  temporary.Reachability = TransferReachability::None;
  temporary.WholeOwnedTemporaryEligible = true;
  temporary.SourceTransferAuthorized = false;
  auto consumed = prepareExplicitCedePlan(temporary);
  CHECK(consumed.admitted());
  CHECK(consumed.ValueProduction == TransferValueProduction::ConsumeTemporary);
  CHECK(consumed.Source == TransferSourceDisposition::NoSourcePlace);

  temporary.SurfaceSpelling = TransferSurfaceSpelling::ExplicitCede;
  temporary.SyntaxPurpose = CedeSyntaxPurpose::SourceInvalidation;
  temporary.Origin = TransferPlanOrigin::UserSource;
  CHECK(prepareExplicitCedePlan(temporary).Rejection ==
        TransferPlanRejection::ExplicitCedeRequiresSource);

  auto owner = named;
  owner.SourceView = TransferSourceView::UniqueHandle;
  owner.FormalMorphology = TransferFormalMorphology::UniqueHandle;
  owner.Ownership = TransferOwnershipKind::UniqueOwner;
  owner.Reachability = TransferReachability::RootAndDependentViews;
  auto ownerMove = prepareExplicitCedePlan(owner);
  CHECK(ownerMove.admitted());
  CHECK(ownerMove.Source == TransferSourceDisposition::InvalidateRoot);

  owner.ActiveDerivedBorrow = true;
  CHECK(prepareExplicitCedePlan(owner).Rejection ==
        TransferPlanRejection::ActiveDerivedBorrow);

  auto payload = named;
  payload.SourceView = TransferSourceView::DereferencedOwningPayload;
  CHECK(prepareExplicitCedePlan(payload).Rejection ==
        TransferPlanRejection::DereferencedOwningPayload);

  auto reference = named;
  reference.SourceView = TransferSourceView::ReferenceConstruction;
  reference.FormalMorphology = TransferFormalMorphology::Reference;
  reference.Ownership = TransferOwnershipKind::BorrowedView;
  reference.CopyProof = TransferCopyProof::ProvenCopy;
  CHECK(prepareExplicitCedePlan(reference).Rejection ==
        TransferPlanRejection::ReferenceBindingSelectorUnavailable);

  auto ordinaryReference = reference;
  ordinaryReference.SurfaceSpelling = TransferSurfaceSpelling::Bare;
  ordinaryReference.SyntaxPurpose = CedeSyntaxPurpose::None;
  ordinaryReference.Origin = TransferPlanOrigin::CompilerSynthetic;
  ordinaryReference.SourceCategory = TransferSourceCategory::NoSourcePlace;
  ordinaryReference.SemanticRootKey.clear();
  ordinaryReference.ExactPath.clear();
  ordinaryReference.Reachability = TransferReachability::None;
  ordinaryReference.FormalContract = TransferFormalContract::Ordinary;
  auto borrowConstruction = prepareExplicitCedePlan(ordinaryReference);
  CHECK(borrowConstruction.admitted());
  CHECK(borrowConstruction.ValueProduction ==
        TransferValueProduction::CopyIdentity);

  auto returned = named;
  returned.FormalTypeKey.clear();
  returned.FormalContract = TransferFormalContract::None;
  returned.FormalMorphology = TransferFormalMorphology::None;
  returned.FormalCapabilities = {};
  returned.Destination = TransferDestination::Return;
  returned.ObligationBefore = TransferObligationState::Outstanding;
  returned.ObligationRootKey = returned.SemanticRootKey;
  auto returnPlan = prepareExplicitCedePlan(returned);
  CHECK(returnPlan.admitted());
  CHECK(returnPlan.ObligationAction ==
        TransferObligationAction::DischargeToReturn);
  CHECK(returnPlan.ObligationAfter == TransferObligationState::Discharged);

  auto unrelatedTemporary = temporary;
  unrelatedTemporary.SurfaceSpelling = TransferSurfaceSpelling::Bare;
  unrelatedTemporary.SyntaxPurpose = CedeSyntaxPurpose::None;
  unrelatedTemporary.Origin = TransferPlanOrigin::CompilerSynthetic;
  unrelatedTemporary.FormalTypeKey.clear();
  unrelatedTemporary.FormalContract = TransferFormalContract::None;
  unrelatedTemporary.FormalMorphology = TransferFormalMorphology::None;
  unrelatedTemporary.FormalCapabilities = {};
  unrelatedTemporary.Destination = TransferDestination::Return;
  unrelatedTemporary.ObligationBefore = TransferObligationState::Outstanding;
  unrelatedTemporary.ObligationRootKey = "module:m;fn:f;binding:other";
  auto wrongReturn = prepareExplicitCedePlan(unrelatedTemporary);
  CHECK(wrongReturn.admitted());
  CHECK(wrongReturn.ObligationAction == TransferObligationAction::Preserve);
  CHECK(wrongReturn.ObligationAfter == TransferObligationState::Outstanding);

  auto copyReturn = copy;
  copyReturn.SurfaceSpelling = TransferSurfaceSpelling::Bare;
  copyReturn.SyntaxPurpose = CedeSyntaxPurpose::None;
  copyReturn.FormalTypeKey.clear();
  copyReturn.FormalContract = TransferFormalContract::None;
  copyReturn.FormalMorphology = TransferFormalMorphology::None;
  copyReturn.FormalCapabilities = {};
  copyReturn.Destination = TransferDestination::Return;
  auto keptCopy = prepareExplicitCedePlan(copyReturn);
  CHECK(keptCopy.admitted());
  CHECK(keptCopy.ValueProduction == TransferValueProduction::CopyValue);
  CHECK(keptCopy.Source == TransferSourceDisposition::KeepLive);

  auto intrinsic = owner;
  intrinsic.SurfaceSpelling = TransferSurfaceSpelling::IntrinsicUniqueMove;
  intrinsic.SyntaxPurpose = CedeSyntaxPurpose::None;
  intrinsic.FormalTypeKey.clear();
  intrinsic.FormalContract = TransferFormalContract::None;
  intrinsic.FormalMorphology = TransferFormalMorphology::None;
  intrinsic.FormalCapabilities = {};
  intrinsic.Destination = TransferDestination::Return;
  intrinsic.ActiveDerivedBorrow = false;
  auto uniqueReturn = prepareExplicitCedePlan(intrinsic);
  CHECK(uniqueReturn.admitted());
  CHECK(uniqueReturn.ValueProduction == TransferValueProduction::MoveOwned);
  CHECK(uniqueReturn.Source == TransferSourceDisposition::InvalidateRoot);

  auto redundantUnique = intrinsic;
  redundantUnique.SurfaceSpelling = TransferSurfaceSpelling::ExplicitCede;
  redundantUnique.SyntaxPurpose = CedeSyntaxPurpose::SourceInvalidation;
  CHECK(prepareExplicitCedePlan(redundantUnique).Rejection ==
        TransferPlanRejection::RedundantIntrinsicUniqueCede);

  auto borrowedParameterReturn = returned;
  borrowedParameterReturn.SourceTransferAuthorized = false;
  CHECK(prepareExplicitCedePlan(borrowedParameterReturn).Rejection ==
        TransferPlanRejection::SourceTransferUnauthorized);

  auto bareNonCopyReturn = returned;
  bareNonCopyReturn.SurfaceSpelling = TransferSurfaceSpelling::Bare;
  bareNonCopyReturn.SyntaxPurpose = CedeSyntaxPurpose::None;
  bareNonCopyReturn.ObligationBefore = TransferObligationState::None;
  bareNonCopyReturn.ObligationRootKey.clear();
  CHECK(prepareExplicitCedePlan(bareNonCopyReturn).Rejection ==
        TransferPlanRejection::MissingCedeForNamedSource);

  auto incomplete = named;
  incomplete.CopyProof = TransferCopyProof::Indeterminate;
  CHECK(prepareExplicitCedePlan(incomplete).Rejection ==
        TransferPlanRejection::IncompleteFacts);

  return 0;
}
