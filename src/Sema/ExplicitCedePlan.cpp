// Copyright (c) 2026 YiZhonghua<zhyi@dpai.com>. All rights reserved.

#include "toka/ExplicitCedePlan.h"

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
  return TransferDropDisposition::DestinationAssumesLiability;
}

TransferObligationAction
obligationAction(const ExplicitCedePreparedFacts &facts) {
  if (facts.ObligationBefore != TransferObligationState::Outstanding)
    return isCallBoundary(facts.Destination) &&
                   facts.FormalContract == TransferFormalContract::Cede
               ? TransferObligationAction::CreateForCallee
               : TransferObligationAction::None;
  const bool transfersOutstandingSource =
      facts.SourceCategory == TransferSourceCategory::NamedSourcePlace &&
      facts.SourcePlace && facts.SourcePlace->valid() && facts.ObligationRoot &&
      facts.SourcePlace->root() == *facts.ObligationRoot;
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
  plan.ObligationAction = obligationAction(facts);
  switch (plan.ObligationAction) {
  case TransferObligationAction::CreateForCallee:
  case TransferObligationAction::TransferToCallee:
    plan.ObligationAfter = TransferObligationState::Outstanding;
    break;
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
  plan.TransferOrigin = facts.SourcePlace;
  plan.TransferOriginView = facts.SourceView;
  plan.Reachability = facts.Reachability;
  return plan;
}

} // namespace

ExplicitCedePlan
prepareExplicitCedePlan(const ExplicitCedePreparedFacts &facts) {
  if (facts.ActualTypeKey.empty() ||
      facts.Destination == TransferDestination::Indeterminate ||
      facts.SourceCategory == TransferSourceCategory::Indeterminate ||
      facts.SourceView == TransferSourceView::Indeterminate ||
      facts.Ownership == TransferOwnershipKind::Indeterminate ||
      facts.CopyProof == TransferCopyProof::Indeterminate ||
      facts.Eligibility == TransferEligibility::Indeterminate ||
      !facts.ActualCapabilities.Complete || !facts.BorrowStateComplete ||
      !facts.DropLiabilityComplete ||
      (facts.ObligationBefore == TransferObligationState::Outstanding &&
       (!facts.ObligationRoot || !facts.ObligationRoot->valid())))
    return reject(TransferPlanRejection::IncompleteFacts, facts);
  if (facts.Eligibility == TransferEligibility::Ineligible)
    return reject(TransferPlanRejection::RouteIneligible, facts);

  const bool callBoundary = isCallBoundary(facts.Destination);
  if (callBoundary &&
      (facts.FormalTypeKey.empty() ||
       facts.FormalContract == TransferFormalContract::None ||
       facts.FormalMorphology == TransferFormalMorphology::None ||
       facts.FormalMorphology == TransferFormalMorphology::Indeterminate ||
       !facts.FormalCapabilities.Complete))
    return reject(TransferPlanRejection::IncompleteFacts, facts);
  if (callBoundary && ((facts.FormalCapabilities.HandleRebindable &&
                        !facts.ActualCapabilities.HandleRebindable) ||
                       (facts.FormalCapabilities.PayloadWritable &&
                        !facts.ActualCapabilities.PayloadWritable)))
    return reject(TransferPlanRejection::AccessCapabilityMismatch, facts);

  if (facts.SourceView == TransferSourceView::ReferenceConstruction &&
      (facts.SurfaceSpelling == TransferSurfaceSpelling::ExplicitCede ||
       facts.FormalContract == TransferFormalContract::Cede))
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
    if (facts.SourceView == TransferSourceView::DereferencedOwningPayload)
      return reject(TransferPlanRejection::DereferencedOwningPayload, facts);
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
      if (!morphologyMatches(facts.SourceView, facts.FormalMorphology))
        return reject(TransferPlanRejection::SourceViewMismatch, facts);
    } else if (facts.FormalContract != TransferFormalContract::None) {
      return reject(TransferPlanRejection::ClosedWorldCombination, facts);
    }
    auto production = productionFor(facts);
    auto source = invalidationFor(facts.Reachability);
    if (production == TransferValueProduction::None ||
        source == TransferSourceDisposition::NoStateChange)
      return reject(TransferPlanRejection::ClosedWorldCombination, facts);
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
      if (!morphologyMatches(facts.SourceView, facts.FormalMorphology))
        return reject(TransferPlanRejection::SourceViewMismatch, facts);
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
    if (!facts.WholeOwnedTemporaryEligible &&
        facts.CopyProof != TransferCopyProof::ProvenCopy)
      return reject(TransferPlanRejection::TemporaryTransferIneligible, facts);
    const auto production = facts.CopyProof == TransferCopyProof::ProvenCopy
                                ? TransferValueProduction::CopyValue
                                : TransferValueProduction::ConsumeTemporary;
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
    if (!morphologyMatches(facts.SourceView, facts.FormalMorphology))
      return reject(TransferPlanRejection::SourceViewMismatch, facts);
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

const char *toString(TransferPlanRejection value) {
  switch (value) {
  case TransferPlanRejection::None:
    return "None";
  case TransferPlanRejection::IncompleteFacts:
    return "IncompleteFacts";
  case TransferPlanRejection::ClosedWorldCombination:
    return "ClosedWorldCombination";
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
  case TransferObligationAction::CreateForCallee:
    return "CreateForCallee";
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
  case TransferFormalMorphology::Indeterminate:
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

} // namespace toka
