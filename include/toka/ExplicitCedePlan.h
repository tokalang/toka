// Copyright (c) 2026 YiZhonghua<zhyi@dpai.com>. All rights reserved.

#pragma once

#include <cstdint>
#include <string>

namespace toka {

enum class TransferPlanOutcome : uint8_t { Admitted, Rejected };

enum class TransferPlanRejection : uint8_t {
  None,
  IncompleteFacts,
  ClosedWorldCombination,
  RouteIneligible,
  ExplicitCedeRequiresSource,
  ExplicitCedeToOrdinaryFormal,
  MissingCedeForNamedSource,
  SourceViewMismatch,
  AccessCapabilityMismatch,
  DereferencedOwningPayload,
  ReferenceBindingSelectorUnavailable,
  ActiveDerivedBorrow,
  SourceTransferUnauthorized,
  TemporaryTransferIneligible,
  InvalidIntrinsicUniqueMove,
  RedundantIntrinsicUniqueCede,
};

enum class TransferPlanOrigin : uint8_t { UserSource, CompilerSynthetic };
enum class CedeSyntaxPurpose : uint8_t { None, SourceInvalidation };
enum class TransferSurfaceSpelling : uint8_t {
  Bare,
  ExplicitCede,
  IntrinsicUniqueMove,
};
enum class TransferSourceCategory : uint8_t {
  NamedSourcePlace,
  NoSourcePlace,
  Indeterminate,
};
enum class TransferSourceView : uint8_t {
  DirectValue,
  DereferencedOwningPayload,
  UniqueHandle,
  SharedHandle,
  RawHandle,
  ReferenceConstruction,
  CallableIdentity,
  Indeterminate,
};
enum class TransferOwnershipKind : uint8_t {
  PlainValue,
  OwnedValue,
  UniqueOwner,
  SharedOwner,
  BorrowedView,
  RawIdentity,
  CallableIdentity,
  OwnedCallable,
  Indeterminate,
};
enum class TransferCopyProof : uint8_t {
  ProvenCopy,
  ProvenNonCopy,
  Indeterminate,
};
enum class TransferFormalContract : uint8_t { None, Ordinary, Cede };
enum class TransferFormalMorphology : uint8_t {
  None,
  DirectValue,
  UniqueHandle,
  SharedHandle,
  RawHandle,
  Reference,
  Callable,
  Indeterminate,
};
enum class TransferDestination : uint8_t {
  CalleeParameter,
  Receiver,
  Assignment,
  Initialization,
  Return,
  AggregateMember,
  MatchBinding,
  ClosureCapture,
  StatementEndDiscard,
  Indeterminate,
};
enum class TransferEligibility : uint8_t {
  Eligible,
  Ineligible,
  Indeterminate,
};
enum class TransferReachability : uint8_t {
  RootAndDependentViews,
  ExactSubtree,
  BindingAndDependentViews,
  None,
  Indeterminate,
};
enum class TransferValueProduction : uint8_t {
  None,
  BorrowCapture,
  CopyValue,
  CopyIdentity,
  MoveOwned,
  TransferShared,
  ConsumeTemporary,
};
enum class TransferSourceDisposition : uint8_t {
  NoStateChange,
  KeepLive,
  InvalidateRoot,
  InvalidateSubtree,
  InvalidateBinding,
  NoSourcePlace,
};
enum class TransferDropDisposition : uint8_t {
  None,
  SourceRetainsLiability,
  DestinationAssumesLiability,
  StatementEndAssumesLiability,
  SharedLiabilityIncremented,
  NoLiability,
};
enum class TransferObligationState : uint8_t {
  None,
  Outstanding,
  Discharged,
};
enum class TransferObligationAction : uint8_t {
  None,
  CreateForCallee,
  TransferToCallee,
  DischargeToReturn,
  DischargeToStorage,
  DischargeToStatementDiscard,
  Preserve,
};

struct TransferAccessCapabilities {
  bool HandleRebindable = false;
  bool PayloadWritable = false;
  bool Complete = false;
};

struct ExplicitCedePreparedFacts {
  std::string ActualTypeKey;
  std::string FormalTypeKey;
  std::string SemanticRootKey;
  std::string ExactPath;
  std::string ObligationRootKey;
  TransferPlanOrigin Origin = TransferPlanOrigin::UserSource;
  CedeSyntaxPurpose SyntaxPurpose = CedeSyntaxPurpose::None;
  TransferSurfaceSpelling SurfaceSpelling = TransferSurfaceSpelling::Bare;
  TransferSourceCategory SourceCategory = TransferSourceCategory::Indeterminate;
  TransferSourceView SourceView = TransferSourceView::Indeterminate;
  TransferOwnershipKind Ownership = TransferOwnershipKind::Indeterminate;
  TransferCopyProof CopyProof = TransferCopyProof::Indeterminate;
  TransferFormalContract FormalContract = TransferFormalContract::None;
  TransferFormalMorphology FormalMorphology = TransferFormalMorphology::None;
  TransferAccessCapabilities FormalCapabilities;
  TransferAccessCapabilities ActualCapabilities;
  TransferDestination Destination = TransferDestination::Indeterminate;
  TransferEligibility Eligibility = TransferEligibility::Indeterminate;
  TransferReachability Reachability = TransferReachability::Indeterminate;
  TransferObligationState ObligationBefore = TransferObligationState::None;
  bool WholeOwnedTemporaryEligible = false;
  bool ActiveDerivedBorrow = false;
  bool SourceTransferAuthorized = false;
  bool CarriesDropLiability = false;
};

struct ExplicitCedePlan {
  TransferPlanOutcome Outcome = TransferPlanOutcome::Rejected;
  TransferPlanRejection Rejection =
      TransferPlanRejection::ClosedWorldCombination;
  TransferValueProduction ValueProduction = TransferValueProduction::None;
  TransferSourceDisposition Source = TransferSourceDisposition::NoStateChange;
  TransferDestination Destination = TransferDestination::Indeterminate;
  TransferDropDisposition Drop = TransferDropDisposition::None;
  TransferObligationAction ObligationAction = TransferObligationAction::None;
  TransferObligationState ObligationAfter = TransferObligationState::None;
  std::string SemanticRootKey;
  std::string ExactPath;
  TransferSourceView TransferOriginView = TransferSourceView::Indeterminate;
  TransferReachability Reachability = TransferReachability::Indeterminate;

  bool admitted() const noexcept {
    return Outcome == TransferPlanOutcome::Admitted;
  }
};

// Pure Stage-0 classifier. It neither reads nor mutates AST, PAL, PlaceState,
// diagnostics, caches, TKI, Evidence, or CodeGen state. Every combination not
// explicitly admitted returns a closed-world rejection.
ExplicitCedePlan
prepareExplicitCedePlan(const ExplicitCedePreparedFacts &facts);

const char *toString(TransferPlanRejection value);

} // namespace toka
