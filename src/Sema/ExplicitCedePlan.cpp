// Copyright (c) 2026 YiZhonghua<zhyi@dpai.com>. All rights reserved.

#include "toka/ExplicitCedePlan.h"

#include <algorithm>

namespace toka {
namespace {

ExplicitCedePlan reject(TransferPlanRejection reason,
                        const ExplicitCedePreparedFacts &facts) {
  ExplicitCedePlan plan;
  plan.Prepared = facts;
  plan.Rejection = reason;
  plan.Destination = facts.Destination;
  plan.ObligationAfter = facts.ObligationBefore;
  plan.TransferOrigin = facts.SourcePlace;
  plan.TransferOriginView = facts.SourceView;
  plan.Reachability = facts.Reachability;
  return plan;
}

bool isCallBoundary(TransferDestination destination) {
  return destination == TransferDestination::CalleeParameter ||
         destination == TransferDestination::Receiver;
}

bool morphologyMatches(TransferSourceView source,
                       TransferFormalMorphology formal) {
  switch (formal) {
  case TransferFormalMorphology::DirectValue:
    return source == TransferSourceView::DirectValue ||
           source == TransferSourceView::DereferencedOwningPayload;
  case TransferFormalMorphology::UniqueHandle:
    return source == TransferSourceView::UniqueHandle;
  case TransferFormalMorphology::SharedHandle:
    return source == TransferSourceView::SharedHandle;
  case TransferFormalMorphology::RawHandle:
    return source == TransferSourceView::RawHandle;
  case TransferFormalMorphology::Callable:
    return source == TransferSourceView::CallableIdentity;
  case TransferFormalMorphology::Reference:
    return source == TransferSourceView::ReferenceConstruction;
  case TransferFormalMorphology::Morphic:
  case TransferFormalMorphology::None:
  case TransferFormalMorphology::Indeterminate:
    return false;
  }
  return false;
}

TransferSourceDisposition invalidationFor(TransferReachability reachability) {
  switch (reachability) {
  case TransferReachability::RootAndDependentViews:
    return TransferSourceDisposition::InvalidateRoot;
  case TransferReachability::ExactSubtree:
    return TransferSourceDisposition::InvalidateSubtree;
  case TransferReachability::BindingAndDependentViews:
    return TransferSourceDisposition::InvalidateBinding;
  case TransferReachability::None:
  case TransferReachability::Indeterminate:
    return TransferSourceDisposition::NoStateChange;
  }
  return TransferSourceDisposition::NoStateChange;
}

TransferValueProduction productionFor(const ExplicitCedePreparedFacts &facts) {
  if (facts.Ownership == TransferOwnershipKind::SharedOwner)
    return TransferValueProduction::TransferShared;
  if (facts.Ownership == TransferOwnershipKind::RawIdentity ||
      facts.Ownership == TransferOwnershipKind::CallableIdentity ||
      facts.Ownership == TransferOwnershipKind::BorrowedView)
    return TransferValueProduction::CopyIdentity;
  if (facts.CopyProof == TransferCopyProof::ProvenCopy)
    return TransferValueProduction::CopyValue;
  if (facts.Ownership == TransferOwnershipKind::OwnedValue ||
      facts.Ownership == TransferOwnershipKind::UniqueOwner ||
      facts.Ownership == TransferOwnershipKind::OwnedCallable)
    return TransferValueProduction::MoveOwned;
  return TransferValueProduction::None;
}

TransferDropDisposition
destinationDrop(const ExplicitCedePreparedFacts &facts) {
  if (!facts.CarriesDropLiability)
    return TransferDropDisposition::NoLiability;
  if (facts.Destination == TransferDestination::StatementEndDiscard)
    return TransferDropDisposition::StatementEndAssumesLiability;
  if (isCallBoundary(facts.Destination))
    return TransferDropDisposition::CalleeAssumesLiability;
  return TransferDropDisposition::DestinationAssumesLiability;
}

TransferObligationAction
obligationAction(const ExplicitCedePreparedFacts &facts,
                 TransferSourceDisposition source) {
  if (facts.ObligationBefore != TransferObligationState::Outstanding)
    return TransferObligationAction::None;
  const bool invalidatesSource =
      source == TransferSourceDisposition::InvalidateRoot ||
      source == TransferSourceDisposition::InvalidateSubtree ||
      source == TransferSourceDisposition::InvalidateBinding;
  if (!invalidatesSource)
    return TransferObligationAction::Preserve;
  const bool transfersOutstandingSource =
      facts.SourceCategory == TransferSourceCategory::NamedSourcePlace &&
      facts.SourcePlace && facts.SourcePlace->valid() && facts.ObligationRoot &&
      facts.SourcePlace->root() == *facts.ObligationRoot &&
      facts.SourcePlace->projections().empty();
  if (!transfersOutstandingSource)
    return TransferObligationAction::Preserve;
  if (isCallBoundary(facts.Destination))
    return TransferObligationAction::TransferToCallee;
  if (facts.Destination == TransferDestination::Return)
    return TransferObligationAction::DischargeToReturn;
  if (facts.Destination == TransferDestination::StatementEndDiscard)
    return TransferObligationAction::DischargeToStatementDiscard;
  return TransferObligationAction::DischargeToStorage;
}

TransferDestinationObligationAction
destinationObligationAction(const ExplicitCedePreparedFacts &facts,
                            TransferObligationAction sourceAction) {
  if (!isCallBoundary(facts.Destination) ||
      facts.FormalContract != TransferFormalContract::Cede)
    return TransferDestinationObligationAction::None;
  return sourceAction == TransferObligationAction::TransferToCallee
             ? TransferDestinationObligationAction::ReceiveTransferred
             : TransferDestinationObligationAction::CreateOutstanding;
}

bool factsAreConsistent(const ExplicitCedePreparedFacts &facts) {
  const bool contextMatchesDestination =
      (facts.EligibilityContext == TransferEligibilityContext::Argument &&
       facts.Destination == TransferDestination::CalleeParameter) ||
      (facts.EligibilityContext == TransferEligibilityContext::Receiver &&
       facts.Destination == TransferDestination::Receiver) ||
      (facts.EligibilityContext == TransferEligibilityContext::Assignment &&
       facts.Destination == TransferDestination::Assignment) ||
      (facts.EligibilityContext == TransferEligibilityContext::Initialization &&
       facts.Destination == TransferDestination::Initialization) ||
      (facts.EligibilityContext == TransferEligibilityContext::Return &&
       facts.Destination == TransferDestination::Return) ||
      (facts.EligibilityContext ==
           TransferEligibilityContext::AggregateMember &&
       facts.Destination == TransferDestination::AggregateMember) ||
      (facts.EligibilityContext == TransferEligibilityContext::MatchBinding &&
       facts.Destination == TransferDestination::MatchBinding) ||
      (facts.EligibilityContext == TransferEligibilityContext::ClosureCapture &&
       facts.Destination == TransferDestination::ClosureCapture) ||
      (facts.EligibilityContext == TransferEligibilityContext::Standalone &&
       facts.Destination == TransferDestination::StatementEndDiscard);
  if (!contextMatchesDestination)
    return false;
  if (facts.Destination == TransferDestination::Assignment) {
    if (!facts.DestinationFactsComplete || !facts.DestinationPlace ||
        !facts.DestinationPlace->valid() ||
        facts.DestinationView == TransferSourceView::Indeterminate ||
        facts.DestinationReachability == TransferReachability::Indeterminate ||
        facts.DestinationReachability == TransferReachability::None)
      return false;
  }
  const bool hasTypedNonCallDestination =
      !isCallBoundary(facts.Destination) && !facts.FormalTypeKey.empty();
  if (hasTypedNonCallDestination &&
      (!facts.DestinationFactsComplete ||
       facts.DestinationMorphology == TransferFormalMorphology::Indeterminate ||
       !facts.DestinationCapabilities.Complete ||
       !facts.DestinationFlowCeiling.Complete ||
       !facts.SourceFlowCeiling.Complete))
    return false;
  if (isCallBoundary(facts.Destination)) {
    bool formalTupleValid = false;
    if (facts.FormalContractOrigin ==
        TransferFormalContractOrigin::GenericValueDeclaration) {
      const bool resolvedTupleValid =
          (facts.FormalMorphology == TransferFormalMorphology::DirectValue &&
           (facts.FormalOwnership == TransferFormalOwnershipKind::PlainValue ||
            facts.FormalOwnership == TransferFormalOwnershipKind::Owning ||
            facts.FormalOwnership == TransferFormalOwnershipKind::Borrowed)) ||
          ((facts.FormalMorphology == TransferFormalMorphology::UniqueHandle ||
            facts.FormalMorphology == TransferFormalMorphology::SharedHandle) &&
           facts.FormalOwnership == TransferFormalOwnershipKind::Owning) ||
          (facts.FormalMorphology == TransferFormalMorphology::RawHandle &&
           facts.FormalOwnership == TransferFormalOwnershipKind::RawIdentity) ||
          (facts.FormalMorphology == TransferFormalMorphology::Reference &&
           facts.FormalOwnership == TransferFormalOwnershipKind::Borrowed) ||
          (facts.FormalMorphology == TransferFormalMorphology::Callable &&
           facts.FormalOwnership ==
               TransferFormalOwnershipKind::CallableIdentity);
      formalTupleValid =
          facts.DeclaredFormalMorphology ==
              TransferFormalMorphology::DirectValue &&
          resolvedTupleValid &&
          facts.FormalTransferClass ==
              (facts.FormalContract == TransferFormalContract::Cede
                   ? TransferFormalTransferClass::ValueTransfer
                   : TransferFormalTransferClass::BorrowCapture);
    } else if (facts.FormalContractOrigin ==
                   TransferFormalContractOrigin::ConcreteDeclaration ||
               facts.FormalContractOrigin ==
                   TransferFormalContractOrigin::MorphicGenericDeclaration) {
      const bool declaredMorphologyMatches =
          facts.FormalContractOrigin ==
                  TransferFormalContractOrigin::ConcreteDeclaration
              ? facts.DeclaredFormalMorphology == facts.FormalMorphology
              : facts.DeclaredFormalMorphology ==
                    TransferFormalMorphology::Morphic;
      if (!declaredMorphologyMatches)
        return false;
      switch (facts.FormalMorphology) {
      case TransferFormalMorphology::DirectValue:
        if (facts.FormalOwnership == TransferFormalOwnershipKind::Borrowed) {
          formalTupleValid = facts.FormalTransferClass ==
                             TransferFormalTransferClass::IdentityTransfer;
        } else if (facts.FormalOwnership ==
                       TransferFormalOwnershipKind::PlainValue ||
                   facts.FormalOwnership ==
                       TransferFormalOwnershipKind::Owning) {
          formalTupleValid =
              facts.FormalTransferClass ==
              (facts.FormalContract == TransferFormalContract::Cede
                   ? TransferFormalTransferClass::ValueTransfer
                   : TransferFormalTransferClass::BorrowCapture);
        }
        break;
      case TransferFormalMorphology::UniqueHandle:
      case TransferFormalMorphology::SharedHandle:
        formalTupleValid =
            facts.FormalOwnership == TransferFormalOwnershipKind::Owning &&
            facts.FormalTransferClass ==
                (facts.FormalContract == TransferFormalContract::Cede
                     ? TransferFormalTransferClass::OwnershipTransfer
                     : TransferFormalTransferClass::BorrowCapture);
        break;
      case TransferFormalMorphology::RawHandle:
        formalTupleValid =
            facts.FormalOwnership == TransferFormalOwnershipKind::RawIdentity &&
            facts.FormalTransferClass ==
                TransferFormalTransferClass::IdentityTransfer;
        break;
      case TransferFormalMorphology::Reference:
        formalTupleValid =
            facts.FormalOwnership == TransferFormalOwnershipKind::Borrowed &&
            facts.FormalTransferClass ==
                TransferFormalTransferClass::IdentityTransfer;
        break;
      case TransferFormalMorphology::Callable:
        formalTupleValid = facts.FormalOwnership ==
                               TransferFormalOwnershipKind::CallableIdentity &&
                           facts.FormalTransferClass ==
                               TransferFormalTransferClass::CallableTransfer;
        break;
      case TransferFormalMorphology::Morphic:
      case TransferFormalMorphology::None:
      case TransferFormalMorphology::Indeterminate:
        break;
      }
    }
    if (!formalTupleValid)
      return false;
  }
  if (facts.SourceCategory == TransferSourceCategory::NamedSourcePlace) {
    if (!facts.SourcePlace || !facts.SourcePlace->valid() ||
        facts.Reachability == TransferReachability::None ||
        facts.TemporaryEligibility !=
            TransferTemporaryEligibility::Ineligible ||
        facts.SourceLiveness == TransferSourceLiveness::None)
      return false;
  } else if (facts.SourceCategory == TransferSourceCategory::NoSourcePlace) {
    if (facts.SourcePlace || facts.Reachability != TransferReachability::None ||
        facts.ObligationBefore != TransferObligationState::None ||
        facts.ObligationRoot ||
        facts.SourceLiveness != TransferSourceLiveness::None ||
        facts.TemporaryEligibility ==
            TransferTemporaryEligibility::Indeterminate)
      return false;
  }
  for (const auto &root : facts.DependencyRoots) {
    if (!root.valid())
      return false;
  }
  if (facts.Dependency == TransferDependencyKind::None &&
      (!facts.DependencyRoots.empty() || facts.ReferentPlace))
    return false;
  if (facts.Dependency == TransferDependencyKind::Borrowed &&
      (!facts.ReferentPlace || !facts.ReferentPlace->valid() ||
       facts.DependencyRoots.empty()))
    return false;
  if (facts.TemporaryEligibility == TransferTemporaryEligibility::Eligible &&
      (facts.SourceCategory != TransferSourceCategory::NoSourcePlace ||
       facts.Dependency != TransferDependencyKind::None ||
       !facts.DependencyFactsComplete || facts.ReferentPlace ||
       !facts.DependencyRoots.empty() ||
       (facts.Ownership != TransferOwnershipKind::OwnedValue &&
        facts.Ownership != TransferOwnershipKind::UniqueOwner &&
        facts.Ownership != TransferOwnershipKind::SharedOwner &&
        facts.Ownership != TransferOwnershipKind::OwnedCallable)))
    return false;

  switch (facts.SourceView) {
  case TransferSourceView::DirectValue:
    if (facts.Reachability != TransferReachability::ExactSubtree &&
        facts.Reachability != TransferReachability::None)
      return false;
    return facts.Ownership == TransferOwnershipKind::PlainValue ||
           (facts.Ownership == TransferOwnershipKind::OwnedValue &&
            facts.CopyProof == TransferCopyProof::ProvenNonCopy) ||
           facts.Ownership == TransferOwnershipKind::BorrowedView;
  case TransferSourceView::DereferencedOwningPayload:
    return facts.SourceCategory == TransferSourceCategory::NamedSourcePlace &&
           facts.Reachability == TransferReachability::ExactSubtree &&
           (facts.Ownership == TransferOwnershipKind::PlainValue ||
            (facts.Ownership == TransferOwnershipKind::OwnedValue &&
             facts.CopyProof == TransferCopyProof::ProvenNonCopy));
  case TransferSourceView::UniqueHandle:
    return facts.Ownership == TransferOwnershipKind::UniqueOwner &&
           facts.CopyProof == TransferCopyProof::ProvenNonCopy &&
           (facts.Reachability == TransferReachability::RootAndDependentViews ||
            facts.Reachability == TransferReachability::None);
  case TransferSourceView::SharedHandle:
    return facts.Ownership == TransferOwnershipKind::SharedOwner &&
           facts.CopyProof == TransferCopyProof::ProvenNonCopy &&
           (facts.Reachability ==
                TransferReachability::BindingAndDependentViews ||
            facts.Reachability == TransferReachability::None);
  case TransferSourceView::RawHandle:
    return facts.Ownership == TransferOwnershipKind::RawIdentity &&
           (facts.Reachability ==
                TransferReachability::BindingAndDependentViews ||
            facts.Reachability == TransferReachability::None);
  case TransferSourceView::ReferenceConstruction:
    return facts.SourceCategory == TransferSourceCategory::NoSourcePlace &&
           facts.Ownership == TransferOwnershipKind::BorrowedView &&
           facts.Reachability == TransferReachability::None;
  case TransferSourceView::CallableIdentity:
    return ((facts.Ownership == TransferOwnershipKind::CallableIdentity) ||
            (facts.Ownership == TransferOwnershipKind::OwnedCallable &&
             facts.CopyProof == TransferCopyProof::ProvenNonCopy)) &&
           (facts.Reachability ==
                TransferReachability::BindingAndDependentViews ||
            facts.Reachability == TransferReachability::None);
  case TransferSourceView::Indeterminate:
    return false;
  }
  return false;
}

bool invalidates(const ExplicitCedePlan &plan) {
  return plan.Source == TransferSourceDisposition::InvalidateRoot ||
         plan.Source == TransferSourceDisposition::InvalidateSubtree ||
         plan.Source == TransferSourceDisposition::InvalidateBinding;
}

bool placesMayOverlap(const PlaceId &left, const PlaceId &right) {
  if (left.root() != right.root())
    return false;
  const auto &lhs = left.projections();
  const auto &rhs = right.projections();
  const size_t common = std::min(lhs.size(), rhs.size());
  for (size_t index = 0; index < common; ++index) {
    if (lhs[index] == rhs[index])
      continue;
    if (lhs[index].kind() == PlaceProjectionKind::Field &&
        rhs[index].kind() == PlaceProjectionKind::Field)
      return false;
    if (lhs[index].kind() == PlaceProjectionKind::ConstantIndex &&
        rhs[index].kind() == PlaceProjectionKind::ConstantIndex)
      return false;
    return true;
  }
  return true;
}

bool invalidationRegionConflictsWithPlace(const ExplicitCedePlan &invalidator,
                                          const PlaceId &place) {
  if (!invalidator.TransferOrigin)
    return true;
  const PlaceId &origin = *invalidator.TransferOrigin;
  switch (invalidator.Source) {
  case TransferSourceDisposition::InvalidateRoot:
    return origin.root() == place.root();
  case TransferSourceDisposition::InvalidateSubtree:
    return placesMayOverlap(origin, place);
  case TransferSourceDisposition::InvalidateBinding:
    return origin.projections().empty() ? origin.root() == place.root()
                                        : placesMayOverlap(origin, place);
  case TransferSourceDisposition::NoStateChange:
  case TransferSourceDisposition::KeepLive:
  case TransferSourceDisposition::NoSourcePlace:
    return false;
  }
  return true;
}

ExplicitCedePlan admit(const ExplicitCedePreparedFacts &facts,
                       TransferValueProduction production,
                       TransferSourceDisposition source,
                       TransferDropDisposition drop) {
  ExplicitCedePlan plan;
  plan.Prepared = facts;
  plan.Outcome = TransferPlanOutcome::Admitted;
  plan.Rejection = TransferPlanRejection::None;
  plan.ValueProduction = production;
  plan.Source = source;
  plan.Destination = facts.Destination;
  plan.Drop = drop;
  plan.ObligationAction = obligationAction(facts, source);
  plan.DestinationObligationAction =
      destinationObligationAction(facts, plan.ObligationAction);
  switch (plan.ObligationAction) {
  case TransferObligationAction::TransferToCallee:
  case TransferObligationAction::DischargeToReturn:
  case TransferObligationAction::DischargeToStorage:
  case TransferObligationAction::DischargeToStatementDiscard:
    plan.ObligationAfter = TransferObligationState::Discharged;
    break;
  case TransferObligationAction::Preserve:
  case TransferObligationAction::None:
    plan.ObligationAfter = facts.ObligationBefore;
    break;
  }
  plan.DestinationObligationAfter =
      plan.DestinationObligationAction ==
              TransferDestinationObligationAction::None
          ? TransferObligationState::None
          : TransferObligationState::Outstanding;
  plan.TransferOrigin = facts.SourcePlace;
  plan.TransferOriginView = facts.SourceView;
  plan.Reachability = facts.Reachability;
  return plan;
}

} // namespace

ExplicitCedePlan
prepareExplicitCedePlan(const ExplicitCedePreparedFacts &facts) {
  const bool callBoundary = isCallBoundary(facts.Destination);
  if (facts.SurfaceSpelling == TransferSurfaceSpelling::ExplicitCede &&
      facts.SourceView == TransferSourceView::ReferenceConstruction)
    return reject(TransferPlanRejection::ReferenceBindingSelectorUnavailable,
                  facts);
  if (facts.SurfaceSpelling == TransferSurfaceSpelling::ExplicitCede &&
      facts.SourceCategory == TransferSourceCategory::NoSourcePlace)
    return reject(TransferPlanRejection::ExplicitCedeRequiresSource, facts);
  if (facts.SurfaceSpelling == TransferSurfaceSpelling::ExplicitCede &&
      facts.SourceView == TransferSourceView::DereferencedOwningPayload)
    return reject(TransferPlanRejection::DereferencedOwningPayload, facts);
  if (facts.SourceCategory == TransferSourceCategory::NamedSourcePlace &&
      facts.SourceView == TransferSourceView::UniqueHandle &&
      facts.SourcePlace && !facts.SourcePlace->projections().empty())
    return reject(TransferPlanRejection::ProjectedHandleRequiresSubroot, facts);
  if (callBoundary && facts.FormalContract != TransferFormalContract::None &&
      facts.FormalMorphology != TransferFormalMorphology::None &&
      facts.FormalMorphology != TransferFormalMorphology::Indeterminate &&
      facts.SourceView != TransferSourceView::Indeterminate) {
    if (facts.SourceView == TransferSourceView::ReferenceConstruction &&
        facts.FormalContract == TransferFormalContract::Cede)
      return reject(TransferPlanRejection::ReferenceBindingSelectorUnavailable,
                    facts);
    if (!morphologyMatches(facts.SourceView, facts.FormalMorphology))
      return reject(TransferPlanRejection::SourceViewMismatch, facts);
    if ((facts.FormalTransferClass ==
             TransferFormalTransferClass::OwnershipTransfer ||
         facts.FormalTransferClass ==
             TransferFormalTransferClass::ValueTransfer) &&
        (facts.Ownership == TransferOwnershipKind::BorrowedView ||
         facts.Ownership == TransferOwnershipKind::RawIdentity ||
         facts.Ownership == TransferOwnershipKind::CallableIdentity))
      return reject(TransferPlanRejection::OwnershipContractMismatch, facts);
    if (facts.SurfaceSpelling == TransferSurfaceSpelling::ExplicitCede &&
        facts.FormalContract == TransferFormalContract::Ordinary)
      return reject(TransferPlanRejection::ExplicitCedeToOrdinaryFormal, facts);
    if (facts.SourceCategory == TransferSourceCategory::NamedSourcePlace &&
        facts.SurfaceSpelling == TransferSurfaceSpelling::Bare &&
        facts.FormalContract == TransferFormalContract::Cede)
      return reject(TransferPlanRejection::MissingCedeForNamedSource, facts);
  }
  if (!callBoundary && !facts.FormalTypeKey.empty() &&
      facts.DestinationCapabilities.Complete &&
      facts.DestinationFlowCeiling.Complete &&
      facts.SourceFlowCeiling.Complete &&
      facts.DestinationMorphology != TransferFormalMorphology::DirectValue &&
      ((facts.DestinationCapabilities.HandleRebindable &&
        (!facts.DestinationFlowCeiling.HandleRebindable ||
         !facts.SourceFlowCeiling.HandleRebindable)) ||
       (facts.DestinationCapabilities.PayloadWritable &&
        (!facts.DestinationFlowCeiling.PayloadWritable ||
         !facts.SourceFlowCeiling.PayloadWritable))))
    return reject(TransferPlanRejection::AccessCapabilityMismatch, facts);
  if (facts.ActualTypeKey.empty() ||
      facts.Destination == TransferDestination::Indeterminate ||
      facts.SourceCategory == TransferSourceCategory::Indeterminate ||
      facts.SourceView == TransferSourceView::Indeterminate ||
      facts.Ownership == TransferOwnershipKind::Indeterminate ||
      facts.CopyProof == TransferCopyProof::Indeterminate ||
      facts.Eligibility == TransferEligibility::Indeterminate ||
      facts.EligibilityContext == TransferEligibilityContext::Indeterminate ||
      facts.TemporaryEligibility ==
          TransferTemporaryEligibility::Indeterminate ||
      facts.TypeCompatibility == TransferTypeCompatibility::Indeterminate ||
      facts.Dependency == TransferDependencyKind::Indeterminate ||
      !facts.DependencyFactsComplete || !facts.ActualCapabilities.Complete ||
      !facts.BorrowStateComplete || !facts.DropLiabilityComplete ||
      facts.SnapshotRevision == 0 || !facts.SourceLivenessComplete ||
      !facts.InitMaskComplete || !facts.CleanupMaskComplete ||
      !facts.LiabilityIdentityComplete || !facts.ObligationFactsComplete ||
      (facts.Destination == TransferDestination::Assignment &&
       !facts.DestinationFactsComplete) ||
      (facts.ObligationBefore == TransferObligationState::Outstanding &&
       (!facts.ObligationRoot || !facts.ObligationRoot->valid())))
    return reject(TransferPlanRejection::IncompleteFacts, facts);
  if (facts.SourceCategory == TransferSourceCategory::NamedSourcePlace &&
      facts.SourceLiveness != TransferSourceLiveness::Live)
    return reject(TransferPlanRejection::SourceNotLive, facts);
  if (facts.Destination == TransferDestination::Assignment &&
      facts.DestinationPlace && facts.SourcePlace &&
      (facts.SurfaceSpelling == TransferSurfaceSpelling::ExplicitCede ||
       facts.SurfaceSpelling == TransferSurfaceSpelling::IntrinsicUniqueMove)) {
    ExplicitCedePlan sourceRegion;
    sourceRegion.TransferOrigin = facts.SourcePlace;
    sourceRegion.Source = invalidationFor(facts.Reachability);
    if (sourceRegion.Source != TransferSourceDisposition::NoStateChange &&
        invalidationRegionConflictsWithPlace(sourceRegion,
                                             *facts.DestinationPlace))
      return reject(TransferPlanRejection::DestinationOverlap, facts);
  }
  if (callBoundary &&
      (facts.FormalTypeKey.empty() ||
       facts.FormalContract == TransferFormalContract::None ||
       facts.DeclaredFormalMorphology == TransferFormalMorphology::None ||
       facts.DeclaredFormalMorphology ==
           TransferFormalMorphology::Indeterminate ||
       facts.FormalMorphology == TransferFormalMorphology::None ||
       facts.FormalMorphology == TransferFormalMorphology::Indeterminate ||
       facts.FormalOwnership == TransferFormalOwnershipKind::None ||
       facts.FormalOwnership == TransferFormalOwnershipKind::Indeterminate ||
       facts.FormalTransferClass == TransferFormalTransferClass::None ||
       facts.FormalTransferClass ==
           TransferFormalTransferClass::Indeterminate ||
       facts.FormalContractOrigin == TransferFormalContractOrigin::None ||
       facts.FormalContractOrigin ==
           TransferFormalContractOrigin::Indeterminate ||
       !facts.FormalDeclarationFactsComplete ||
       !facts.FormalCapabilities.Complete))
    return reject(TransferPlanRejection::IncompleteFacts, facts);
  if (!factsAreConsistent(facts))
    return reject(TransferPlanRejection::ContradictoryFacts, facts);
  if (facts.Eligibility == TransferEligibility::Ineligible)
    return reject(TransferPlanRejection::RouteIneligible, facts);
  if (facts.TypeCompatibility == TransferTypeCompatibility::Incompatible)
    return reject(TransferPlanRejection::TypeIncompatible, facts);
  if (callBoundary &&
      (facts.FormalTransferClass ==
           TransferFormalTransferClass::OwnershipTransfer ||
       facts.FormalTransferClass ==
           TransferFormalTransferClass::ValueTransfer) &&
      (facts.Ownership == TransferOwnershipKind::BorrowedView ||
       facts.Ownership == TransferOwnershipKind::RawIdentity ||
       facts.Ownership == TransferOwnershipKind::CallableIdentity))
    return reject(TransferPlanRejection::OwnershipContractMismatch, facts);
  if (callBoundary &&
      facts.SourceView == TransferSourceView::ReferenceConstruction &&
      facts.FormalContract == TransferFormalContract::Cede)
    return reject(TransferPlanRejection::ReferenceBindingSelectorUnavailable,
                  facts);
  if (callBoundary &&
      !morphologyMatches(facts.SourceView, facts.FormalMorphology))
    return reject(TransferPlanRejection::SourceViewMismatch, facts);
  if (callBoundary && ((facts.FormalCapabilities.HandleRebindable &&
                        !facts.ActualCapabilities.HandleRebindable) ||
                       (facts.FormalCapabilities.PayloadWritable &&
                        !facts.ActualCapabilities.PayloadWritable)))
    return reject(TransferPlanRejection::AccessCapabilityMismatch, facts);

  if (facts.SourceView == TransferSourceView::ReferenceConstruction &&
      facts.SurfaceSpelling == TransferSurfaceSpelling::ExplicitCede)
    return reject(TransferPlanRejection::ReferenceBindingSelectorUnavailable,
                  facts);

  if (facts.SurfaceSpelling == TransferSurfaceSpelling::ExplicitCede) {
    if (facts.Origin != TransferPlanOrigin::UserSource ||
        facts.SyntaxPurpose != CedeSyntaxPurpose::SourceInvalidation)
      return reject(TransferPlanRejection::ClosedWorldCombination, facts);
    if (facts.SourceCategory != TransferSourceCategory::NamedSourcePlace)
      return reject(TransferPlanRejection::ExplicitCedeRequiresSource, facts);
    if (!facts.SourcePlace || !facts.SourcePlace->valid() ||
        facts.Reachability == TransferReachability::None ||
        facts.Reachability == TransferReachability::Indeterminate)
      return reject(TransferPlanRejection::IncompleteFacts, facts);
    if (facts.Destination == TransferDestination::Return &&
        facts.SourceView == TransferSourceView::UniqueHandle)
      return reject(TransferPlanRejection::RedundantIntrinsicUniqueCede, facts);
    if (facts.ActiveDerivedBorrow)
      return reject(TransferPlanRejection::ActiveDerivedBorrow, facts);
    if (!facts.SourceTransferAuthorityComplete)
      return reject(TransferPlanRejection::IncompleteFacts, facts);
    if (!facts.SourceTransferAuthorized)
      return reject(TransferPlanRejection::SourceTransferUnauthorized, facts);
    if (callBoundary) {
      if (facts.FormalContract != TransferFormalContract::Cede)
        return reject(TransferPlanRejection::ExplicitCedeToOrdinaryFormal,
                      facts);
    } else if (facts.FormalContract != TransferFormalContract::None) {
      return reject(TransferPlanRejection::ClosedWorldCombination, facts);
    }
    auto production = productionFor(facts);
    auto source = invalidationFor(facts.Reachability);
    if (production == TransferValueProduction::None ||
        source == TransferSourceDisposition::NoStateChange)
      return reject(TransferPlanRejection::ClosedWorldCombination, facts);
    ExplicitCedePlan sourceRegion;
    sourceRegion.TransferOrigin = facts.SourcePlace;
    sourceRegion.Source = source;
    if (facts.Destination == TransferDestination::Assignment &&
        facts.DestinationPlace &&
        invalidationRegionConflictsWithPlace(sourceRegion,
                                             *facts.DestinationPlace))
      return reject(TransferPlanRejection::DestinationOverlap, facts);
    return admit(facts, production, source, destinationDrop(facts));
  }

  if (facts.SyntaxPurpose != CedeSyntaxPurpose::None)
    return reject(TransferPlanRejection::ClosedWorldCombination, facts);

  if (facts.SurfaceSpelling == TransferSurfaceSpelling::IntrinsicUniqueMove) {
    if (facts.Origin != TransferPlanOrigin::UserSource ||
        facts.SourceCategory != TransferSourceCategory::NamedSourcePlace ||
        facts.SourceView != TransferSourceView::UniqueHandle ||
        facts.Ownership != TransferOwnershipKind::UniqueOwner ||
        !facts.SourcePlace || !facts.SourcePlace->valid() ||
        !facts.SourceTransferAuthorityComplete || facts.ActiveDerivedBorrow ||
        !facts.SourceTransferAuthorized ||
        (facts.Destination != TransferDestination::Return &&
         facts.Destination != TransferDestination::Assignment &&
         facts.Destination != TransferDestination::Initialization))
      return reject(TransferPlanRejection::InvalidIntrinsicUniqueMove, facts);
    ExplicitCedePlan sourceRegion;
    sourceRegion.TransferOrigin = facts.SourcePlace;
    sourceRegion.Source = TransferSourceDisposition::InvalidateRoot;
    if (facts.Destination == TransferDestination::Assignment &&
        facts.DestinationPlace &&
        invalidationRegionConflictsWithPlace(sourceRegion,
                                             *facts.DestinationPlace))
      return reject(TransferPlanRejection::DestinationOverlap, facts);
    return admit(facts, TransferValueProduction::MoveOwned,
                 TransferSourceDisposition::InvalidateRoot,
                 destinationDrop(facts));
  }

  if (facts.SourceCategory == TransferSourceCategory::NoSourcePlace) {
    if (facts.Origin != TransferPlanOrigin::CompilerSynthetic ||
        facts.SurfaceSpelling != TransferSurfaceSpelling::Bare)
      return reject(TransferPlanRejection::ClosedWorldCombination, facts);
    if (callBoundary &&
        facts.FormalContract == TransferFormalContract::Ordinary) {
      if (productionFor(facts) == TransferValueProduction::CopyIdentity)
        return admit(facts, TransferValueProduction::CopyIdentity,
                     TransferSourceDisposition::NoSourcePlace,
                     TransferDropDisposition::NoLiability);
      if (facts.CopyProof == TransferCopyProof::ProvenCopy)
        return admit(facts, TransferValueProduction::CopyValue,
                     TransferSourceDisposition::NoSourcePlace,
                     TransferDropDisposition::NoLiability);
      return admit(facts, TransferValueProduction::BorrowCapture,
                   TransferSourceDisposition::NoSourcePlace,
                   TransferDropDisposition::SourceRetainsLiability);
    }
    if (callBoundary && facts.FormalContract != TransferFormalContract::Cede)
      return reject(TransferPlanRejection::ClosedWorldCombination, facts);
    if (facts.TemporaryEligibility != TransferTemporaryEligibility::Eligible &&
        facts.CopyProof != TransferCopyProof::ProvenCopy)
      return reject(TransferPlanRejection::TemporaryTransferIneligible, facts);
    const auto classified = productionFor(facts);
    const auto production =
        classified == TransferValueProduction::CopyIdentity
            ? classified
            : (facts.CopyProof == TransferCopyProof::ProvenCopy
                   ? TransferValueProduction::CopyValue
                   : TransferValueProduction::ConsumeTemporary);
    return admit(facts, production, TransferSourceDisposition::NoSourcePlace,
                 destinationDrop(facts));
  }

  if (facts.SourceCategory != TransferSourceCategory::NamedSourcePlace ||
      facts.Origin != TransferPlanOrigin::UserSource ||
      facts.SurfaceSpelling != TransferSurfaceSpelling::Bare)
    return reject(TransferPlanRejection::ClosedWorldCombination, facts);
  if (!facts.SourcePlace || !facts.SourcePlace->valid())
    return reject(TransferPlanRejection::IncompleteFacts, facts);
  if (callBoundary) {
    if (facts.FormalContract == TransferFormalContract::Cede)
      return reject(TransferPlanRejection::MissingCedeForNamedSource, facts);
    if (facts.FormalContract != TransferFormalContract::Ordinary)
      return reject(TransferPlanRejection::ClosedWorldCombination, facts);
    if (productionFor(facts) == TransferValueProduction::CopyIdentity)
      return admit(facts, TransferValueProduction::CopyIdentity,
                   TransferSourceDisposition::KeepLive,
                   TransferDropDisposition::NoLiability);
    if (facts.CopyProof == TransferCopyProof::ProvenCopy)
      return admit(facts, TransferValueProduction::CopyValue,
                   TransferSourceDisposition::KeepLive,
                   TransferDropDisposition::NoLiability);
    return admit(facts, TransferValueProduction::BorrowCapture,
                 TransferSourceDisposition::KeepLive,
                 facts.CarriesDropLiability
                     ? TransferDropDisposition::SourceRetainsLiability
                     : TransferDropDisposition::NoLiability);
  }
  if (facts.CopyProof == TransferCopyProof::ProvenCopy)
    return admit(facts, TransferValueProduction::CopyValue,
                 TransferSourceDisposition::KeepLive,
                 TransferDropDisposition::NoLiability);
  return reject(TransferPlanRejection::MissingCedeForNamedSource, facts);
}

ExplicitCedeWholeCallPlan
prepareExplicitCedeWholeCallPlan(const ExplicitCedeWholeCallFacts &facts) {
  ExplicitCedeWholeCallPlan result;
  const bool arityIncomplete =
      !facts.ArityComplete ||
      facts.ExpectedArgumentCount != facts.ActualArgumentCount ||
      facts.Arguments.size() != facts.ActualArgumentCount;
  if (facts.Receiver &&
      facts.Receiver->Destination != TransferDestination::Receiver) {
    result.Rejection = TransferPlanRejection::WholeCallDestinationMismatch;
    return result;
  }
  for (const auto &argument : facts.Arguments) {
    if (argument.Destination != TransferDestination::CalleeParameter &&
        argument.Destination != TransferDestination::Initialization) {
      result.Rejection = TransferPlanRejection::WholeCallDestinationMismatch;
      return result;
    }
  }
  if (facts.Receiver)
    result.Receiver = prepareExplicitCedePlan(*facts.Receiver);
  result.Arguments.reserve(facts.Arguments.size());
  for (const auto &argument : facts.Arguments)
    result.Arguments.push_back(prepareExplicitCedePlan(argument));

  if (arityIncomplete) {
    result.Rejection = TransferPlanRejection::WholeCallArityIncomplete;
    return result;
  }

  if ((result.Receiver && !result.Receiver->admitted()) ||
      std::any_of(
          result.Arguments.begin(), result.Arguments.end(),
          [](const ExplicitCedePlan &plan) { return !plan.admitted(); })) {
    result.Rejection = TransferPlanRejection::WholeCallItemRejected;
    return result;
  }

  std::vector<const ExplicitCedePlan *> items;
  if (result.Receiver)
    items.push_back(&*result.Receiver);
  for (const auto &argument : result.Arguments)
    items.push_back(&argument);
  for (size_t invalidator = 0; invalidator < items.size(); ++invalidator) {
    if (!invalidates(*items[invalidator]))
      continue;
    if (!items[invalidator]->TransferOrigin) {
      result.Rejection = TransferPlanRejection::WholeCallAliasConflict;
      return result;
    }
    for (size_t other = 0; other < items.size(); ++other) {
      if (invalidator == other)
        continue;
      const auto &invalidated = *items[invalidator]->TransferOrigin;
      const auto &prepared = items[other]->Prepared;
      bool conflict = prepared.SourcePlace &&
                      invalidationRegionConflictsWithPlace(
                          *items[invalidator], *prepared.SourcePlace);
      conflict =
          conflict || (prepared.ReferentPlace &&
                       invalidationRegionConflictsWithPlace(
                           *items[invalidator], *prepared.ReferentPlace));
      conflict =
          conflict ||
          std::any_of(prepared.DependencyRoots.begin(),
                      prepared.DependencyRoots.end(),
                      [&](const RootSymbolId &root) {
                        if (root != invalidated.root())
                          return false;
                        if (items[invalidator]->Source ==
                            TransferSourceDisposition::InvalidateRoot)
                          return true;
                        if (prepared.ReferentPlace &&
                            prepared.ReferentPlace->root() == root)
                          return invalidationRegionConflictsWithPlace(
                              *items[invalidator], *prepared.ReferentPlace);
                        return true;
                      });
      if (conflict) {
        result.Rejection = TransferPlanRejection::WholeCallAliasConflict;
        return result;
      }
    }
  }
  result.Outcome = TransferPlanOutcome::Admitted;
  result.Rejection = TransferPlanRejection::None;
  result.CommitAllowed = true;
  return result;
}

ExplicitCedeNonCallGroupPlan prepareExplicitCedeNonCallGroupPlan(
    const ExplicitCedeNonCallGroupFacts &facts) {
  ExplicitCedeNonCallGroupPlan result;
  if (facts.ExpectedSnapshotRevision == 0 ||
      std::any_of(facts.Items.begin(), facts.Items.end(),
                  [&](const ExplicitCedePreparedFacts &item) {
                    return item.SnapshotRevision !=
                           facts.ExpectedSnapshotRevision;
                  })) {
    result.Rejection = TransferPlanRejection::NonCallGroupSnapshotMismatch;
    return result;
  }
  result.Items.reserve(facts.Items.size());
  for (const auto &item : facts.Items) {
    if (item.Destination != facts.Destination) {
      result.Rejection = TransferPlanRejection::NonCallGroupDestinationMismatch;
      return result;
    }
    result.Items.push_back(prepareExplicitCedePlan(item));
  }
  if (std::any_of(
          result.Items.begin(), result.Items.end(),
          [](const ExplicitCedePlan &plan) { return !plan.admitted(); })) {
    result.Rejection = TransferPlanRejection::NonCallGroupItemRejected;
    return result;
  }
  for (size_t invalidator = 0; invalidator < result.Items.size();
       ++invalidator) {
    if (!invalidates(result.Items[invalidator]))
      continue;
    for (size_t other = 0; other < result.Items.size(); ++other) {
      if (invalidator == other)
        continue;
      const auto &otherFacts = result.Items[other].Prepared;
      if ((otherFacts.SourcePlace &&
           invalidationRegionConflictsWithPlace(result.Items[invalidator],
                                                *otherFacts.SourcePlace)) ||
          (otherFacts.ReferentPlace &&
           invalidationRegionConflictsWithPlace(result.Items[invalidator],
                                                *otherFacts.ReferentPlace)) ||
          std::any_of(
              otherFacts.DependencyRoots.begin(),
              otherFacts.DependencyRoots.end(), [&](const RootSymbolId &root) {
                return result.Items[invalidator].TransferOrigin &&
                       result.Items[invalidator].TransferOrigin->root() == root;
              })) {
        result.Rejection = TransferPlanRejection::NonCallGroupAliasConflict;
        return result;
      }
    }
  }
  result.Outcome = TransferPlanOutcome::Admitted;
  result.Rejection = TransferPlanRejection::None;
  return result;
}

const char *toString(TransferPlanRejection value) {
  switch (value) {
  case TransferPlanRejection::None:
    return "None";
  case TransferPlanRejection::IncompleteFacts:
    return "IncompleteFacts";
  case TransferPlanRejection::ClosedWorldCombination:
    return "ClosedWorldCombination";
  case TransferPlanRejection::ContradictoryFacts:
    return "ContradictoryFacts";
  case TransferPlanRejection::TypeIncompatible:
    return "TypeIncompatible";
  case TransferPlanRejection::RouteIneligible:
    return "RouteIneligible";
  case TransferPlanRejection::ExplicitCedeRequiresSource:
    return "ExplicitCedeRequiresSource";
  case TransferPlanRejection::ExplicitCedeToOrdinaryFormal:
    return "ExplicitCedeToOrdinaryFormal";
  case TransferPlanRejection::MissingCedeForNamedSource:
    return "MissingCedeForNamedSource";
  case TransferPlanRejection::SourceViewMismatch:
    return "SourceViewMismatch";
  case TransferPlanRejection::AccessCapabilityMismatch:
    return "AccessCapabilityMismatch";
  case TransferPlanRejection::DereferencedOwningPayload:
    return "DereferencedOwningPayload";
  case TransferPlanRejection::ReferenceBindingSelectorUnavailable:
    return "ReferenceBindingSelectorUnavailable";
  case TransferPlanRejection::ActiveDerivedBorrow:
    return "ActiveDerivedBorrow";
  case TransferPlanRejection::SourceTransferUnauthorized:
    return "SourceTransferUnauthorized";
  case TransferPlanRejection::TemporaryTransferIneligible:
    return "TemporaryTransferIneligible";
  case TransferPlanRejection::InvalidIntrinsicUniqueMove:
    return "InvalidIntrinsicUniqueMove";
  case TransferPlanRejection::RedundantIntrinsicUniqueCede:
    return "RedundantIntrinsicUniqueCede";
  case TransferPlanRejection::WholeCallItemRejected:
    return "WholeCallItemRejected";
  case TransferPlanRejection::WholeCallAliasConflict:
    return "WholeCallAliasConflict";
  case TransferPlanRejection::WholeCallDestinationMismatch:
    return "WholeCallDestinationMismatch";
  case TransferPlanRejection::WholeCallArityIncomplete:
    return "WholeCallArityIncomplete";
  case TransferPlanRejection::WholeCallValidationFailed:
    return "WholeCallValidationFailed";
  case TransferPlanRejection::WholeCallValidationIncomplete:
    return "WholeCallValidationIncomplete";
  case TransferPlanRejection::DestinationOverlap:
    return "DestinationOverlap";
  case TransferPlanRejection::NonCallGroupItemRejected:
    return "NonCallGroupItemRejected";
  case TransferPlanRejection::NonCallGroupAliasConflict:
    return "NonCallGroupAliasConflict";
  case TransferPlanRejection::NonCallGroupDestinationMismatch:
    return "NonCallGroupDestinationMismatch";
  case TransferPlanRejection::NonCallGroupSnapshotMismatch:
    return "NonCallGroupSnapshotMismatch";
  case TransferPlanRejection::MissingPreMutationTransaction:
    return "MissingPreMutationTransaction";
  case TransferPlanRejection::OwnershipContractMismatch:
    return "OwnershipContractMismatch";
  case TransferPlanRejection::ProjectedHandleRequiresSubroot:
    return "ProjectedHandleRequiresSubroot";
  case TransferPlanRejection::SourceNotLive:
    return "SourceNotLive";
  }
  return "ClosedWorldCombination";
}

const char *toString(TransferPlanOutcome value) {
  return value == TransferPlanOutcome::Admitted ? "Admitted" : "Rejected";
}

const char *toString(TransferValueProduction value) {
  switch (value) {
  case TransferValueProduction::None:
    return "None";
  case TransferValueProduction::BorrowCapture:
    return "BorrowCapture";
  case TransferValueProduction::CopyValue:
    return "CopyValue";
  case TransferValueProduction::CopyIdentity:
    return "CopyIdentity";
  case TransferValueProduction::MoveOwned:
    return "MoveOwned";
  case TransferValueProduction::TransferShared:
    return "TransferShared";
  case TransferValueProduction::ConsumeTemporary:
    return "ConsumeTemporary";
  }
  return "None";
}

const char *toString(TransferSourceDisposition value) {
  switch (value) {
  case TransferSourceDisposition::NoStateChange:
    return "NoStateChange";
  case TransferSourceDisposition::KeepLive:
    return "KeepLive";
  case TransferSourceDisposition::InvalidateRoot:
    return "InvalidateRoot";
  case TransferSourceDisposition::InvalidateSubtree:
    return "InvalidateSubtree";
  case TransferSourceDisposition::InvalidateBinding:
    return "InvalidateBinding";
  case TransferSourceDisposition::NoSourcePlace:
    return "NoSourcePlace";
  }
  return "NoStateChange";
}

const char *toString(TransferDestination value) {
  switch (value) {
  case TransferDestination::CalleeParameter:
    return "CalleeParameter";
  case TransferDestination::Receiver:
    return "Receiver";
  case TransferDestination::Assignment:
    return "Assignment";
  case TransferDestination::Initialization:
    return "Initialization";
  case TransferDestination::Return:
    return "Return";
  case TransferDestination::AggregateMember:
    return "AggregateMember";
  case TransferDestination::MatchBinding:
    return "MatchBinding";
  case TransferDestination::ClosureCapture:
    return "ClosureCapture";
  case TransferDestination::StatementEndDiscard:
    return "StatementEndDiscard";
  case TransferDestination::Indeterminate:
    return "Indeterminate";
  }
  return "Indeterminate";
}

const char *toString(TransferDropDisposition value) {
  switch (value) {
  case TransferDropDisposition::None:
    return "None";
  case TransferDropDisposition::SourceRetainsLiability:
    return "SourceRetainsLiability";
  case TransferDropDisposition::CalleeAssumesLiability:
    return "CalleeAssumesLiability";
  case TransferDropDisposition::DestinationAssumesLiability:
    return "DestinationAssumesLiability";
  case TransferDropDisposition::StatementEndAssumesLiability:
    return "StatementEndAssumesLiability";
  case TransferDropDisposition::SharedLiabilityIncremented:
    return "SharedLiabilityIncremented";
  case TransferDropDisposition::NoLiability:
    return "NoLiability";
  }
  return "None";
}

const char *toString(TransferObligationAction value) {
  switch (value) {
  case TransferObligationAction::None:
    return "None";
  case TransferObligationAction::TransferToCallee:
    return "TransferToCallee";
  case TransferObligationAction::DischargeToReturn:
    return "DischargeToReturn";
  case TransferObligationAction::DischargeToStorage:
    return "DischargeToStorage";
  case TransferObligationAction::DischargeToStatementDiscard:
    return "DischargeToStatementDiscard";
  case TransferObligationAction::Preserve:
    return "Preserve";
  }
  return "None";
}

const char *toString(TransferObligationState value) {
  switch (value) {
  case TransferObligationState::None:
    return "None";
  case TransferObligationState::Outstanding:
    return "Outstanding";
  case TransferObligationState::Discharged:
    return "Discharged";
  }
  return "None";
}

const char *toString(TransferSourceLiveness value) {
  switch (value) {
  case TransferSourceLiveness::None:
    return "None";
  case TransferSourceLiveness::Live:
    return "Live";
  case TransferSourceLiveness::Moved:
    return "Moved";
  case TransferSourceLiveness::Uninitialized:
    return "Uninitialized";
  case TransferSourceLiveness::PartiallyLive:
    return "PartiallyLive";
  case TransferSourceLiveness::Indeterminate:
    return "Indeterminate";
  }
  return "Indeterminate";
}

const char *toString(TransferSourceView value) {
  switch (value) {
  case TransferSourceView::DirectValue:
    return "DirectValue";
  case TransferSourceView::DereferencedOwningPayload:
    return "DereferencedOwningPayload";
  case TransferSourceView::UniqueHandle:
    return "UniqueHandle";
  case TransferSourceView::SharedHandle:
    return "SharedHandle";
  case TransferSourceView::RawHandle:
    return "RawHandle";
  case TransferSourceView::ReferenceConstruction:
    return "ReferenceConstruction";
  case TransferSourceView::CallableIdentity:
    return "CallableIdentity";
  case TransferSourceView::Indeterminate:
    return "Indeterminate";
  }
  return "Indeterminate";
}

const char *toString(TransferReachability value) {
  switch (value) {
  case TransferReachability::RootAndDependentViews:
    return "RootAndDependentViews";
  case TransferReachability::ExactSubtree:
    return "ExactSubtree";
  case TransferReachability::BindingAndDependentViews:
    return "BindingAndDependentViews";
  case TransferReachability::None:
    return "None";
  case TransferReachability::Indeterminate:
    return "Indeterminate";
  }
  return "Indeterminate";
}

const char *toString(TransferPlanOrigin value) {
  return value == TransferPlanOrigin::UserSource ? "UserSource"
                                                 : "CompilerSynthetic";
}

const char *toString(CedeSyntaxPurpose value) {
  return value == CedeSyntaxPurpose::SourceInvalidation ? "SourceInvalidation"
                                                        : "None";
}

const char *toString(TransferSurfaceSpelling value) {
  switch (value) {
  case TransferSurfaceSpelling::Bare:
    return "Bare";
  case TransferSurfaceSpelling::ExplicitCede:
    return "ExplicitCede";
  case TransferSurfaceSpelling::IntrinsicUniqueMove:
    return "IntrinsicUniqueMove";
  }
  return "Bare";
}

const char *toString(TransferSourceCategory value) {
  switch (value) {
  case TransferSourceCategory::NamedSourcePlace:
    return "NamedSourcePlace";
  case TransferSourceCategory::NoSourcePlace:
    return "NoSourcePlace";
  case TransferSourceCategory::Indeterminate:
    return "Indeterminate";
  }
  return "Indeterminate";
}

const char *toString(TransferOwnershipKind value) {
  switch (value) {
  case TransferOwnershipKind::PlainValue:
    return "PlainValue";
  case TransferOwnershipKind::OwnedValue:
    return "OwnedValue";
  case TransferOwnershipKind::UniqueOwner:
    return "UniqueOwner";
  case TransferOwnershipKind::SharedOwner:
    return "SharedOwner";
  case TransferOwnershipKind::BorrowedView:
    return "BorrowedView";
  case TransferOwnershipKind::RawIdentity:
    return "RawIdentity";
  case TransferOwnershipKind::CallableIdentity:
    return "CallableIdentity";
  case TransferOwnershipKind::OwnedCallable:
    return "OwnedCallable";
  case TransferOwnershipKind::Indeterminate:
    return "Indeterminate";
  }
  return "Indeterminate";
}

const char *toString(TransferCopyProof value) {
  switch (value) {
  case TransferCopyProof::ProvenCopy:
    return "ProvenCopy";
  case TransferCopyProof::ProvenNonCopy:
    return "ProvenNonCopy";
  case TransferCopyProof::Indeterminate:
    return "Indeterminate";
  }
  return "Indeterminate";
}

const char *toString(TransferFormalContract value) {
  switch (value) {
  case TransferFormalContract::None:
    return "None";
  case TransferFormalContract::Ordinary:
    return "Ordinary";
  case TransferFormalContract::Cede:
    return "Cede";
  }
  return "None";
}

const char *toString(TransferFormalMorphology value) {
  switch (value) {
  case TransferFormalMorphology::None:
    return "None";
  case TransferFormalMorphology::DirectValue:
    return "DirectValue";
  case TransferFormalMorphology::UniqueHandle:
    return "UniqueHandle";
  case TransferFormalMorphology::SharedHandle:
    return "SharedHandle";
  case TransferFormalMorphology::RawHandle:
    return "RawHandle";
  case TransferFormalMorphology::Reference:
    return "Reference";
  case TransferFormalMorphology::Callable:
    return "Callable";
  case TransferFormalMorphology::Morphic:
    return "Morphic";
  case TransferFormalMorphology::Indeterminate:
    return "Indeterminate";
  }
  return "Indeterminate";
}

const char *toString(TransferFormalOwnershipKind value) {
  switch (value) {
  case TransferFormalOwnershipKind::None:
    return "None";
  case TransferFormalOwnershipKind::PlainValue:
    return "PlainValue";
  case TransferFormalOwnershipKind::Owning:
    return "Owning";
  case TransferFormalOwnershipKind::Borrowed:
    return "Borrowed";
  case TransferFormalOwnershipKind::RawIdentity:
    return "RawIdentity";
  case TransferFormalOwnershipKind::CallableIdentity:
    return "CallableIdentity";
  case TransferFormalOwnershipKind::Indeterminate:
    return "Indeterminate";
  }
  return "Indeterminate";
}

const char *toString(TransferFormalTransferClass value) {
  switch (value) {
  case TransferFormalTransferClass::None:
    return "None";
  case TransferFormalTransferClass::BorrowCapture:
    return "BorrowCapture";
  case TransferFormalTransferClass::ValueTransfer:
    return "ValueTransfer";
  case TransferFormalTransferClass::OwnershipTransfer:
    return "OwnershipTransfer";
  case TransferFormalTransferClass::IdentityTransfer:
    return "IdentityTransfer";
  case TransferFormalTransferClass::CallableTransfer:
    return "CallableTransfer";
  case TransferFormalTransferClass::Indeterminate:
    return "Indeterminate";
  }
  return "Indeterminate";
}

const char *toString(TransferFormalContractOrigin value) {
  switch (value) {
  case TransferFormalContractOrigin::None:
    return "None";
  case TransferFormalContractOrigin::ConcreteDeclaration:
    return "ConcreteDeclaration";
  case TransferFormalContractOrigin::GenericValueDeclaration:
    return "GenericValueDeclaration";
  case TransferFormalContractOrigin::MorphicGenericDeclaration:
    return "MorphicGenericDeclaration";
  case TransferFormalContractOrigin::Indeterminate:
    return "Indeterminate";
  }
  return "Indeterminate";
}

const char *toString(TransferEligibility value) {
  switch (value) {
  case TransferEligibility::Eligible:
    return "Eligible";
  case TransferEligibility::Ineligible:
    return "Ineligible";
  case TransferEligibility::Indeterminate:
    return "Indeterminate";
  }
  return "Indeterminate";
}

const char *toString(TransferEligibilityContext value) {
  switch (value) {
  case TransferEligibilityContext::Argument:
    return "Argument";
  case TransferEligibilityContext::Receiver:
    return "Receiver";
  case TransferEligibilityContext::Assignment:
    return "Assignment";
  case TransferEligibilityContext::Initialization:
    return "Initialization";
  case TransferEligibilityContext::Return:
    return "Return";
  case TransferEligibilityContext::AggregateMember:
    return "AggregateMember";
  case TransferEligibilityContext::MatchBinding:
    return "MatchBinding";
  case TransferEligibilityContext::ClosureCapture:
    return "ClosureCapture";
  case TransferEligibilityContext::Standalone:
    return "Standalone";
  case TransferEligibilityContext::Indeterminate:
    return "Indeterminate";
  }
  return "Indeterminate";
}

const char *toString(TransferTemporaryEligibility value) {
  switch (value) {
  case TransferTemporaryEligibility::Eligible:
    return "Eligible";
  case TransferTemporaryEligibility::Ineligible:
    return "Ineligible";
  case TransferTemporaryEligibility::Indeterminate:
    return "Indeterminate";
  }
  return "Indeterminate";
}

const char *toString(TransferTypeCompatibility value) {
  switch (value) {
  case TransferTypeCompatibility::Compatible:
    return "Compatible";
  case TransferTypeCompatibility::Incompatible:
    return "Incompatible";
  case TransferTypeCompatibility::Indeterminate:
    return "Indeterminate";
  }
  return "Indeterminate";
}

const char *toString(TransferDependencyKind value) {
  switch (value) {
  case TransferDependencyKind::None:
    return "None";
  case TransferDependencyKind::Borrowed:
    return "Borrowed";
  case TransferDependencyKind::RawUnsafe:
    return "RawUnsafe";
  case TransferDependencyKind::Structural:
    return "Structural";
  case TransferDependencyKind::Indeterminate:
    return "Indeterminate";
  }
  return "Indeterminate";
}

const char *toString(TransferDestinationObligationAction value) {
  switch (value) {
  case TransferDestinationObligationAction::None:
    return "None";
  case TransferDestinationObligationAction::CreateOutstanding:
    return "CreateOutstanding";
  case TransferDestinationObligationAction::ReceiveTransferred:
    return "ReceiveTransferred";
  }
  return "None";
}

std::string semanticPlaceKey(const PlaceId &place) {
  if (!place.valid())
    return {};
  std::string key = place.root().canonicalKey();
  for (const auto &projection : place.projections()) {
    switch (projection.kind()) {
    case PlaceProjectionKind::Field:
      key += "/field:" + projection.fieldId().canonicalKey();
      break;
    case PlaceProjectionKind::ConstantIndex:
      key += "/index:" + std::to_string(projection.constantIndexValue());
      break;
    case PlaceProjectionKind::DynamicIndex:
      key += "/dynamic:" + projection.dynamicIndexExpression().canonicalKey();
      break;
    case PlaceProjectionKind::Dereference:
      key += "/dereference";
      break;
    case PlaceProjectionKind::Unknown:
      key += "/unknown";
      break;
    }
  }
  return key;
}

} // namespace toka
