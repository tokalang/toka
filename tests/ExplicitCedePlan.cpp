// Copyright (c) 2026 YiZhonghua<zhyi@dpai.com>. All rights reserved.

#include "toka/ExplicitCedePlan.h"

#include <array>

using namespace toka;

#define CHECK(condition)                                                       \
  do {                                                                         \
    if (!(condition))                                                          \
      return __LINE__;                                                         \
  } while (false)

PlaceId rootPlace() {
  auto declaration = SemanticIdentityBuilder::declaration("module:m", "fn:f");
  auto root = SemanticIdentityBuilder::rootSymbol(declaration.value(),
                                                  "binding:value;direct");
  return PlaceId(root.value());
}

PlaceId otherPlace() {
  auto declaration = SemanticIdentityBuilder::declaration("module:m", "fn:f");
  auto root = SemanticIdentityBuilder::rootSymbol(declaration.value(),
                                                  "binding:other;direct");
  return PlaceId(root.value());
}

PlaceId fieldPlace(const std::string &name) {
  auto declaration = SemanticIdentityBuilder::declaration("module:m", "fn:f");
  auto root = SemanticIdentityBuilder::rootSymbol(declaration.value(),
                                                  "binding:value;direct");
  auto field =
      SemanticIdentityBuilder::field(declaration.value(), "field:" + name);
  return PlaceId(root.value(), {PlaceProjection::field(field.value())});
}

ExplicitCedePreparedFacts baseCall() {
  ExplicitCedePreparedFacts facts;
  facts.ActualTypeKey = "type:Resource";
  facts.FormalTypeKey = "type:Resource";
  facts.SourcePlace = rootPlace();
  facts.SourceCategory = TransferSourceCategory::NamedSourcePlace;
  facts.SourceView = TransferSourceView::DirectValue;
  facts.Ownership = TransferOwnershipKind::OwnedValue;
  facts.CopyProof = TransferCopyProof::ProvenNonCopy;
  facts.FormalContract = TransferFormalContract::Cede;
  facts.FormalMorphology = TransferFormalMorphology::DirectValue;
  facts.FormalCapabilities.Complete = true;
  facts.ActualCapabilities.Complete = true;
  facts.Destination = TransferDestination::CalleeParameter;
  facts.EligibilityContext = TransferEligibilityContext::Argument;
  facts.Eligibility = TransferEligibility::Eligible;
  facts.TemporaryEligibility = TransferTemporaryEligibility::Ineligible;
  facts.TypeCompatibility = TransferTypeCompatibility::Compatible;
  facts.Dependency = TransferDependencyKind::None;
  facts.DependencyFactsComplete = true;
  facts.Reachability = TransferReachability::ExactSubtree;
  facts.BorrowStateComplete = true;
  facts.SourceTransferAuthorized = true;
  facts.SourceTransferAuthorityComplete = true;
  facts.CarriesDropLiability = true;
  facts.DropLiabilityComplete = true;
  return facts;
}

int main() {
  CHECK(rootPlace() == rootPlace());
  CHECK(rootPlace().root().canonicalKey().find("binding:value;direct") !=
        std::string::npos);

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
  CHECK(moved.Drop == TransferDropDisposition::CalleeAssumesLiability);
  CHECK(moved.TransferOrigin == named.SourcePlace);
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

  auto shared = named;
  shared.SourceView = TransferSourceView::SharedHandle;
  shared.FormalMorphology = TransferFormalMorphology::SharedHandle;
  shared.Ownership = TransferOwnershipKind::SharedOwner;
  shared.Reachability = TransferReachability::BindingAndDependentViews;
  auto sharedPlan = prepareExplicitCedePlan(shared);
  CHECK(sharedPlan.admitted());
  CHECK(sharedPlan.ValueProduction == TransferValueProduction::TransferShared);
  CHECK(sharedPlan.Source == TransferSourceDisposition::InvalidateBinding);

  auto raw = copy;
  raw.SourceView = TransferSourceView::RawHandle;
  raw.FormalMorphology = TransferFormalMorphology::RawHandle;
  raw.Ownership = TransferOwnershipKind::RawIdentity;
  raw.Dependency = TransferDependencyKind::RawUnsafe;
  raw.Reachability = TransferReachability::BindingAndDependentViews;
  auto rawPlan = prepareExplicitCedePlan(raw);
  CHECK(rawPlan.admitted());
  CHECK(rawPlan.ValueProduction == TransferValueProduction::CopyIdentity);
  CHECK(rawPlan.Source == TransferSourceDisposition::InvalidateBinding);

  auto ordinary = named;
  ordinary.FormalContract = TransferFormalContract::Ordinary;
  CHECK(prepareExplicitCedePlan(ordinary).Rejection ==
        TransferPlanRejection::ExplicitCedeToOrdinaryFormal);

  auto mismatched = named;
  mismatched.FormalMorphology = TransferFormalMorphology::UniqueHandle;
  CHECK(prepareExplicitCedePlan(mismatched).Rejection ==
        TransferPlanRejection::SourceViewMismatch);

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
  temporary.SourcePlace.reset();
  temporary.Reachability = TransferReachability::None;
  temporary.TemporaryEligibility = TransferTemporaryEligibility::Eligible;
  temporary.SourceTransferAuthorized = false;
  auto consumed = prepareExplicitCedePlan(temporary);
  CHECK(consumed.admitted());
  CHECK(consumed.ValueProduction == TransferValueProduction::ConsumeTemporary);
  CHECK(consumed.Source == TransferSourceDisposition::NoSourcePlace);

  auto ineligibleTemporary = temporary;
  ineligibleTemporary.TemporaryEligibility =
      TransferTemporaryEligibility::Ineligible;
  CHECK(prepareExplicitCedePlan(ineligibleTemporary).Rejection ==
        TransferPlanRejection::TemporaryTransferIneligible);

  auto copyTemporary = ineligibleTemporary;
  copyTemporary.Ownership = TransferOwnershipKind::PlainValue;
  copyTemporary.CopyProof = TransferCopyProof::ProvenCopy;
  copyTemporary.CarriesDropLiability = false;
  auto copiedTemporary = prepareExplicitCedePlan(copyTemporary);
  CHECK(copiedTemporary.admitted());
  CHECK(copiedTemporary.ValueProduction == TransferValueProduction::CopyValue);

  temporary.SurfaceSpelling = TransferSurfaceSpelling::ExplicitCede;
  temporary.SyntaxPurpose = CedeSyntaxPurpose::SourceInvalidation;
  temporary.Origin = TransferPlanOrigin::UserSource;
  CHECK(prepareExplicitCedePlan(temporary).Rejection ==
        TransferPlanRejection::ExplicitCedeRequiresSource);

  const std::array<TransferDestination, 9> noSourceCedeDestinations = {
      TransferDestination::CalleeParameter,
      TransferDestination::Receiver,
      TransferDestination::Assignment,
      TransferDestination::Initialization,
      TransferDestination::Return,
      TransferDestination::AggregateMember,
      TransferDestination::MatchBinding,
      TransferDestination::ClosureCapture,
      TransferDestination::StatementEndDiscard,
  };
  for (auto destination : noSourceCedeDestinations) {
    auto rejected = temporary;
    rejected.Destination = destination;
    switch (destination) {
    case TransferDestination::CalleeParameter:
      rejected.EligibilityContext = TransferEligibilityContext::Argument;
      break;
    case TransferDestination::Receiver:
      rejected.EligibilityContext = TransferEligibilityContext::Receiver;
      break;
    case TransferDestination::Assignment:
      rejected.EligibilityContext = TransferEligibilityContext::Assignment;
      break;
    case TransferDestination::Initialization:
      rejected.EligibilityContext = TransferEligibilityContext::Initialization;
      break;
    case TransferDestination::Return:
      rejected.EligibilityContext = TransferEligibilityContext::Return;
      break;
    case TransferDestination::AggregateMember:
      rejected.EligibilityContext = TransferEligibilityContext::AggregateMember;
      break;
    case TransferDestination::MatchBinding:
      rejected.EligibilityContext = TransferEligibilityContext::MatchBinding;
      break;
    case TransferDestination::ClosureCapture:
      rejected.EligibilityContext = TransferEligibilityContext::ClosureCapture;
      break;
    case TransferDestination::StatementEndDiscard:
      rejected.EligibilityContext = TransferEligibilityContext::Standalone;
      break;
    case TransferDestination::Indeterminate:
      break;
    }
    if (destination != TransferDestination::CalleeParameter &&
        destination != TransferDestination::Receiver) {
      rejected.FormalTypeKey.clear();
      rejected.FormalContract = TransferFormalContract::None;
      rejected.FormalMorphology = TransferFormalMorphology::None;
      rejected.FormalCapabilities = {};
    }
    CHECK(prepareExplicitCedePlan(rejected).Rejection ==
          TransferPlanRejection::ExplicitCedeRequiresSource);
  }

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
  reference.SourceCategory = TransferSourceCategory::NoSourcePlace;
  reference.SourcePlace.reset();
  reference.Reachability = TransferReachability::None;
  reference.TemporaryEligibility = TransferTemporaryEligibility::Ineligible;
  reference.ReferentPlace = rootPlace();
  reference.DependencyRoots = {rootPlace().root()};
  reference.Dependency = TransferDependencyKind::Borrowed;
  CHECK(prepareExplicitCedePlan(reference).Rejection ==
        TransferPlanRejection::ReferenceBindingSelectorUnavailable);

  auto ordinaryReference = reference;
  ordinaryReference.SurfaceSpelling = TransferSurfaceSpelling::Bare;
  ordinaryReference.SyntaxPurpose = CedeSyntaxPurpose::None;
  ordinaryReference.Origin = TransferPlanOrigin::CompilerSynthetic;
  ordinaryReference.SourceCategory = TransferSourceCategory::NoSourcePlace;
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
  returned.EligibilityContext = TransferEligibilityContext::Return;
  returned.ObligationBefore = TransferObligationState::Outstanding;
  returned.ObligationRoot = returned.SourcePlace->root();
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
  unrelatedTemporary.EligibilityContext = TransferEligibilityContext::Return;
  unrelatedTemporary.ObligationBefore = TransferObligationState::Outstanding;
  auto otherDeclaration =
      SemanticIdentityBuilder::declaration("module:m", "fn:f");
  unrelatedTemporary.ObligationRoot =
      SemanticIdentityBuilder::rootSymbol(otherDeclaration.value(),
                                          "binding:other;direct")
          .value();
  auto wrongReturn = prepareExplicitCedePlan(unrelatedTemporary);
  CHECK(!wrongReturn.admitted());
  CHECK(wrongReturn.Rejection == TransferPlanRejection::ContradictoryFacts);

  auto partialReturn = returned;
  partialReturn.SourcePlace = fieldPlace("left");
  auto partialReturnPlan = prepareExplicitCedePlan(partialReturn);
  CHECK(partialReturnPlan.admitted());
  CHECK(partialReturnPlan.Source ==
        TransferSourceDisposition::InvalidateSubtree);
  CHECK(partialReturnPlan.ObligationAction ==
        TransferObligationAction::Preserve);
  CHECK(partialReturnPlan.ObligationAfter ==
        TransferObligationState::Outstanding);

  auto copyReturn = copy;
  copyReturn.SurfaceSpelling = TransferSurfaceSpelling::Bare;
  copyReturn.SyntaxPurpose = CedeSyntaxPurpose::None;
  copyReturn.FormalTypeKey.clear();
  copyReturn.FormalContract = TransferFormalContract::None;
  copyReturn.FormalMorphology = TransferFormalMorphology::None;
  copyReturn.FormalCapabilities = {};
  copyReturn.Destination = TransferDestination::Return;
  copyReturn.EligibilityContext = TransferEligibilityContext::Return;
  auto keptCopy = prepareExplicitCedePlan(copyReturn);
  CHECK(keptCopy.admitted());
  CHECK(keptCopy.ValueProduction == TransferValueProduction::CopyValue);
  CHECK(keptCopy.Source == TransferSourceDisposition::KeepLive);
  copyReturn.ObligationBefore = TransferObligationState::Outstanding;
  copyReturn.ObligationRoot = copyReturn.SourcePlace->root();
  auto obligatedCopyReturn = prepareExplicitCedePlan(copyReturn);
  CHECK(obligatedCopyReturn.admitted());
  CHECK(obligatedCopyReturn.ObligationAction ==
        TransferObligationAction::Preserve);
  CHECK(obligatedCopyReturn.ObligationAfter ==
        TransferObligationState::Outstanding);

  auto obligatedOrdinaryCopy = copy;
  obligatedOrdinaryCopy.SurfaceSpelling = TransferSurfaceSpelling::Bare;
  obligatedOrdinaryCopy.SyntaxPurpose = CedeSyntaxPurpose::None;
  obligatedOrdinaryCopy.FormalContract = TransferFormalContract::Ordinary;
  obligatedOrdinaryCopy.ObligationBefore = TransferObligationState::Outstanding;
  obligatedOrdinaryCopy.ObligationRoot =
      obligatedOrdinaryCopy.SourcePlace->root();
  auto ordinaryCopyPlan = prepareExplicitCedePlan(obligatedOrdinaryCopy);
  CHECK(ordinaryCopyPlan.admitted());
  CHECK(ordinaryCopyPlan.Source == TransferSourceDisposition::KeepLive);
  CHECK(ordinaryCopyPlan.ObligationAction ==
        TransferObligationAction::Preserve);

  auto mismatchedTemporary = copyTemporary;
  mismatchedTemporary.FormalMorphology = TransferFormalMorphology::UniqueHandle;
  CHECK(prepareExplicitCedePlan(mismatchedTemporary).Rejection ==
        TransferPlanRejection::SourceViewMismatch);

  auto rawTemporary = copyTemporary;
  rawTemporary.SourceView = TransferSourceView::RawHandle;
  rawTemporary.Ownership = TransferOwnershipKind::RawIdentity;
  rawTemporary.FormalMorphology = TransferFormalMorphology::RawHandle;
  rawTemporary.Dependency = TransferDependencyKind::RawUnsafe;
  auto rawTemporaryPlan = prepareExplicitCedePlan(rawTemporary);
  CHECK(rawTemporaryPlan.admitted());
  CHECK(rawTemporaryPlan.ValueProduction ==
        TransferValueProduction::CopyIdentity);

  auto intrinsic = owner;
  intrinsic.SurfaceSpelling = TransferSurfaceSpelling::IntrinsicUniqueMove;
  intrinsic.SyntaxPurpose = CedeSyntaxPurpose::None;
  intrinsic.FormalTypeKey.clear();
  intrinsic.FormalContract = TransferFormalContract::None;
  intrinsic.FormalMorphology = TransferFormalMorphology::None;
  intrinsic.FormalCapabilities = {};
  intrinsic.Destination = TransferDestination::Return;
  intrinsic.EligibilityContext = TransferEligibilityContext::Return;
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
  bareNonCopyReturn.ObligationRoot.reset();
  CHECK(prepareExplicitCedePlan(bareNonCopyReturn).Rejection ==
        TransferPlanRejection::MissingCedeForNamedSource);

  auto incomplete = named;
  incomplete.CopyProof = TransferCopyProof::Indeterminate;
  CHECK(prepareExplicitCedePlan(incomplete).Rejection ==
        TransferPlanRejection::IncompleteFacts);

  auto incompleteBorrow = named;
  incompleteBorrow.BorrowStateComplete = false;
  CHECK(prepareExplicitCedePlan(incompleteBorrow).Rejection ==
        TransferPlanRejection::IncompleteFacts);

  auto routeRejected = named;
  routeRejected.Eligibility = TransferEligibility::Ineligible;
  CHECK(prepareExplicitCedePlan(routeRejected).Rejection ==
        TransferPlanRejection::RouteIneligible);

  auto contradictory = temporary;
  contradictory.Origin = TransferPlanOrigin::CompilerSynthetic;
  contradictory.SurfaceSpelling = TransferSurfaceSpelling::Bare;
  contradictory.SyntaxPurpose = CedeSyntaxPurpose::None;
  contradictory.SourcePlace = rootPlace();
  CHECK(prepareExplicitCedePlan(contradictory).Rejection ==
        TransferPlanRejection::ContradictoryFacts);

  auto dehatted = named;
  dehatted.Ownership = TransferOwnershipKind::UniqueOwner;
  dehatted.CopyProof = TransferCopyProof::ProvenNonCopy;
  CHECK(prepareExplicitCedePlan(dehatted).Rejection ==
        TransferPlanRejection::ContradictoryFacts);

  auto dependentTemporary = baseCall();
  dependentTemporary.Origin = TransferPlanOrigin::CompilerSynthetic;
  dependentTemporary.SurfaceSpelling = TransferSurfaceSpelling::Bare;
  dependentTemporary.SyntaxPurpose = CedeSyntaxPurpose::None;
  dependentTemporary.SourceCategory = TransferSourceCategory::NoSourcePlace;
  dependentTemporary.SourcePlace.reset();
  dependentTemporary.Reachability = TransferReachability::None;
  dependentTemporary.TemporaryEligibility =
      TransferTemporaryEligibility::Eligible;
  dependentTemporary.Dependency = TransferDependencyKind::Borrowed;
  dependentTemporary.ReferentPlace = rootPlace();
  dependentTemporary.DependencyRoots = {rootPlace().root()};
  CHECK(prepareExplicitCedePlan(dependentTemporary).Rejection ==
        TransferPlanRejection::ContradictoryFacts);

  auto incompatible = named;
  incompatible.TypeCompatibility = TransferTypeCompatibility::Incompatible;
  CHECK(prepareExplicitCedePlan(incompatible).Rejection ==
        TransferPlanRejection::TypeIncompatible);

  auto receiver = named;
  receiver.Destination = TransferDestination::Receiver;
  receiver.EligibilityContext = TransferEligibilityContext::Receiver;
  auto argument = named;
  argument.SourcePlace = otherPlace();
  ExplicitCedeWholeCallFacts wholeFacts;
  wholeFacts.Receiver = receiver;
  wholeFacts.Arguments = {argument};
  auto whole = prepareExplicitCedeWholeCallPlan(wholeFacts);
  CHECK(whole.admitted());
  CHECK(whole.CommitAllowed);
  CHECK(whole.Receiver && whole.Receiver->admitted());
  CHECK(whole.Arguments.size() == 1 && whole.Arguments[0].admitted());

  wholeFacts.Arguments[0].SourcePlace = receiver.SourcePlace;
  auto aliasConflict = prepareExplicitCedeWholeCallPlan(wholeFacts);
  CHECK(!aliasConflict.admitted());
  CHECK(!aliasConflict.CommitAllowed);
  CHECK(aliasConflict.Rejection ==
        TransferPlanRejection::WholeCallAliasConflict);

  wholeFacts.Arguments[0] = argument;
  wholeFacts.Arguments[0].SurfaceSpelling = TransferSurfaceSpelling::Bare;
  wholeFacts.Arguments[0].SyntaxPurpose = CedeSyntaxPurpose::None;
  auto laterReject = prepareExplicitCedeWholeCallPlan(wholeFacts);
  CHECK(!laterReject.admitted());
  CHECK(!laterReject.CommitAllowed);
  CHECK(laterReject.Receiver && laterReject.Receiver->admitted());
  CHECK(laterReject.Rejection == TransferPlanRejection::WholeCallItemRejected);

  ExplicitCedeWholeCallFacts wrongReceiverSlot;
  wrongReceiverSlot.Receiver = argument;
  CHECK(prepareExplicitCedeWholeCallPlan(wrongReceiverSlot).Rejection ==
        TransferPlanRejection::WholeCallDestinationMismatch);

  auto receiverInArgumentSlot = receiver;
  ExplicitCedeWholeCallFacts wrongArgumentSlot;
  wrongArgumentSlot.Arguments = {receiverInArgumentSlot};
  CHECK(prepareExplicitCedeWholeCallPlan(wrongArgumentSlot).Rejection ==
        TransferPlanRejection::WholeCallDestinationMismatch);

  auto invalidatingRoot = named;
  auto readingRoot = named;
  readingRoot.SurfaceSpelling = TransferSurfaceSpelling::Bare;
  readingRoot.SyntaxPurpose = CedeSyntaxPurpose::None;
  readingRoot.FormalContract = TransferFormalContract::Ordinary;
  ExplicitCedeWholeCallFacts invalidateAndRead;
  invalidateAndRead.Arguments = {invalidatingRoot, readingRoot};
  CHECK(prepareExplicitCedeWholeCallPlan(invalidateAndRead).Rejection ==
        TransferPlanRejection::WholeCallAliasConflict);

  auto borrowSameRoot = ordinaryReference;
  borrowSameRoot.Destination = TransferDestination::CalleeParameter;
  borrowSameRoot.EligibilityContext = TransferEligibilityContext::Argument;
  borrowSameRoot.ReferentPlace = rootPlace();
  borrowSameRoot.DependencyRoots = {rootPlace().root()};
  ExplicitCedeWholeCallFacts invalidateAndBorrow;
  invalidateAndBorrow.Arguments = {invalidatingRoot, borrowSameRoot};
  CHECK(prepareExplicitCedeWholeCallPlan(invalidateAndBorrow).Rejection ==
        TransferPlanRejection::WholeCallAliasConflict);

  auto left = named;
  left.SourcePlace = fieldPlace("left");
  auto right = named;
  right.SourcePlace = fieldPlace("right");
  ExplicitCedeWholeCallFacts siblingFields;
  siblingFields.Arguments = {left, right};
  auto siblingPlan = prepareExplicitCedeWholeCallPlan(siblingFields);
  CHECK(siblingPlan.admitted());
  CHECK(siblingPlan.CommitAllowed);

  ExplicitCedeWholeCallFacts rootAndField;
  rootAndField.Arguments = {named, left};
  CHECK(prepareExplicitCedeWholeCallPlan(rootAndField).Rejection ==
        TransferPlanRejection::WholeCallAliasConflict);

  left.Destination = TransferDestination::Receiver;
  left.EligibilityContext = TransferEligibilityContext::Receiver;
  ExplicitCedeWholeCallFacts receiverSibling;
  receiverSibling.Receiver = left;
  receiverSibling.Arguments = {right};
  auto receiverSiblingPlan = prepareExplicitCedeWholeCallPlan(receiverSibling);
  CHECK(receiverSiblingPlan.admitted());
  CHECK(receiverSiblingPlan.CommitAllowed);

  return 0;
}
