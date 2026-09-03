// Copyright (c) 2026 YiZhonghua<zhyi@dpai.com>. All rights reserved.

#pragma once

#include "toka/SemanticModel.h"
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace toka {

enum class TransferPlanOutcome : uint8_t { Admitted, Rejected };

enum class TransferPlanRejection : uint8_t {
  None,
  IncompleteFacts,
  ClosedWorldCombination,
  ContradictoryFacts,
  TypeIncompatible,
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
  WholeCallItemRejected,
  WholeCallAliasConflict,
  WholeCallDestinationMismatch,
  WholeCallArityIncomplete,
  WholeCallValidationFailed,
  WholeCallValidationIncomplete,
  MissingPreMutationTransaction,
  OwnershipContractMismatch,
  ProjectedHandleRequiresSubroot,
  SourceNotLive,
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
  Morphic,
  Indeterminate,
};
enum class TransferFormalOwnershipKind : uint8_t {
  None,
  PlainValue,
  Owning,
  Borrowed,
  RawIdentity,
  CallableIdentity,
  Indeterminate,
};
enum class TransferFormalTransferClass : uint8_t {
  None,
  BorrowCapture,
  ValueTransfer,
  OwnershipTransfer,
  IdentityTransfer,
  CallableTransfer,
  Indeterminate,
};
enum class TransferFormalContractOrigin : uint8_t {
  None,
  ConcreteDeclaration,
  GenericValueDeclaration,
  MorphicGenericDeclaration,
  Indeterminate,
};

struct TransferFormalDeclarationFacts {
  TransferFormalMorphology DeclaredMorphology =
      TransferFormalMorphology::Indeterminate;
  TransferFormalContractOrigin ContractOrigin =
      TransferFormalContractOrigin::Indeterminate;
  bool Complete = false;
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
enum class TransferEligibilityContext : uint8_t {
  Argument,
  Receiver,
  Assignment,
  Initialization,
  Return,
  AggregateMember,
  MatchBinding,
  ClosureCapture,
  Standalone,
  Indeterminate,
};
enum class TransferTemporaryEligibility : uint8_t {
  Eligible,
  Ineligible,
  Indeterminate,
};
enum class TransferTypeCompatibility : uint8_t {
  Compatible,
  Incompatible,
  Indeterminate,
};
enum class TransferDependencyKind : uint8_t {
  None,
  Borrowed,
  RawUnsafe,
  Structural,
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
  CalleeAssumesLiability,
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
enum class TransferSourceLiveness : uint8_t {
  None,
  Live,
  Moved,
  Uninitialized,
  PartiallyLive,
  Indeterminate,
};
enum class TransferObligationAction : uint8_t {
  None,
  TransferToCallee,
  DischargeToReturn,
  DischargeToStorage,
  DischargeToStatementDiscard,
  Preserve,
};
enum class TransferDestinationObligationAction : uint8_t {
  None,
  CreateOutstanding,
  ReceiveTransferred,
};

struct TransferAccessCapabilities {
  bool HandleRebindable = false;
  bool PayloadWritable = false;
  bool Complete = false;
};

struct ExplicitCedePreparedFacts {
  std::string ActualTypeKey;
  std::string FormalTypeKey;
  std::optional<PlaceId> SourcePlace;
  std::optional<PlaceId> ReferentPlace;
  std::vector<RootSymbolId> DependencyRoots;
  std::optional<RootSymbolId> ObligationRoot;
  TransferPlanOrigin Origin = TransferPlanOrigin::UserSource;
  CedeSyntaxPurpose SyntaxPurpose = CedeSyntaxPurpose::None;
  TransferSurfaceSpelling SurfaceSpelling = TransferSurfaceSpelling::Bare;
  TransferSourceCategory SourceCategory = TransferSourceCategory::Indeterminate;
  TransferSourceView SourceView = TransferSourceView::Indeterminate;
  TransferOwnershipKind Ownership = TransferOwnershipKind::Indeterminate;
  TransferCopyProof CopyProof = TransferCopyProof::Indeterminate;
  TransferFormalContract FormalContract = TransferFormalContract::None;
  TransferFormalMorphology DeclaredFormalMorphology =
      TransferFormalMorphology::None;
  TransferFormalMorphology FormalMorphology = TransferFormalMorphology::None;
  TransferFormalOwnershipKind FormalOwnership =
      TransferFormalOwnershipKind::None;
  TransferFormalTransferClass FormalTransferClass =
      TransferFormalTransferClass::None;
  TransferFormalContractOrigin FormalContractOrigin =
      TransferFormalContractOrigin::None;
  bool FormalDeclarationFactsComplete = false;
  TransferAccessCapabilities FormalCapabilities;
  TransferAccessCapabilities ActualCapabilities;
  TransferDestination Destination = TransferDestination::Indeterminate;
  TransferEligibility Eligibility = TransferEligibility::Indeterminate;
  TransferEligibilityContext EligibilityContext =
      TransferEligibilityContext::Indeterminate;
  TransferTemporaryEligibility TemporaryEligibility =
      TransferTemporaryEligibility::Indeterminate;
  TransferTypeCompatibility TypeCompatibility =
      TransferTypeCompatibility::Indeterminate;
  TransferDependencyKind Dependency = TransferDependencyKind::Indeterminate;
  TransferReachability Reachability = TransferReachability::Indeterminate;
  TransferObligationState ObligationBefore = TransferObligationState::None;
  TransferSourceLiveness SourceLiveness = TransferSourceLiveness::Indeterminate;
  uint64_t SnapshotRevision = 0;
  uint64_t InitMask = 0;
  uint64_t CleanupMask = 0;
  std::string LiabilityIdentity;
  bool SourceLivenessComplete = false;
  bool InitMaskComplete = false;
  bool CleanupMaskComplete = false;
  bool LiabilityIdentityComplete = false;
  bool ObligationFactsComplete = false;
  bool DependencyFactsComplete = false;
  bool ActiveDerivedBorrow = false;
  bool BorrowStateComplete = false;
  bool SourceTransferAuthorized = false;
  bool SourceTransferAuthorityComplete = false;
  bool CarriesDropLiability = false;
  bool DropLiabilityComplete = false;
};

struct ExplicitCedePlan {
  ExplicitCedePreparedFacts Prepared;
  TransferPlanOutcome Outcome = TransferPlanOutcome::Rejected;
  TransferPlanRejection Rejection =
      TransferPlanRejection::ClosedWorldCombination;
  TransferValueProduction ValueProduction = TransferValueProduction::None;
  TransferSourceDisposition Source = TransferSourceDisposition::NoStateChange;
  TransferDestination Destination = TransferDestination::Indeterminate;
  TransferDropDisposition Drop = TransferDropDisposition::None;
  TransferObligationAction ObligationAction = TransferObligationAction::None;
  TransferObligationState ObligationAfter = TransferObligationState::None;
  TransferDestinationObligationAction DestinationObligationAction =
      TransferDestinationObligationAction::None;
  TransferObligationState DestinationObligationAfter =
      TransferObligationState::None;
  std::optional<PlaceId> TransferOrigin;
  TransferSourceView TransferOriginView = TransferSourceView::Indeterminate;
  TransferReachability Reachability = TransferReachability::Indeterminate;

  bool admitted() const noexcept {
    return Outcome == TransferPlanOutcome::Admitted;
  }
};

struct ExplicitCedeWholeCallFacts {
  std::optional<ExplicitCedePreparedFacts> Receiver;
  std::vector<ExplicitCedePreparedFacts> Arguments;
  unsigned ExpectedArgumentCount = 0;
  unsigned ActualArgumentCount = 0;
  bool ArityComplete = true;
};

struct ExplicitCedeWholeCallPlan {
  TransferPlanOutcome Outcome = TransferPlanOutcome::Rejected;
  TransferPlanRejection Rejection =
      TransferPlanRejection::WholeCallItemRejected;
  std::optional<ExplicitCedePlan> Receiver;
  std::vector<ExplicitCedePlan> Arguments;
  bool CommitAllowed = false;

  bool admitted() const noexcept {
    return Outcome == TransferPlanOutcome::Admitted;
  }
};

// Pure Stage-0 classifier. It neither reads nor mutates AST, PAL, PlaceState,
// diagnostics, caches, TKI, Evidence, or CodeGen state. Every combination not
// explicitly admitted returns a closed-world rejection.
ExplicitCedePlan
prepareExplicitCedePlan(const ExplicitCedePreparedFacts &facts);
ExplicitCedeWholeCallPlan
prepareExplicitCedeWholeCallPlan(const ExplicitCedeWholeCallFacts &facts);

const char *toString(TransferPlanRejection value);
const char *toString(TransferPlanOutcome value);
const char *toString(TransferValueProduction value);
const char *toString(TransferSourceDisposition value);
const char *toString(TransferDestination value);
const char *toString(TransferDropDisposition value);
const char *toString(TransferObligationAction value);
const char *toString(TransferObligationState value);
const char *toString(TransferSourceLiveness value);
const char *toString(TransferSourceView value);
const char *toString(TransferReachability value);
const char *toString(TransferPlanOrigin value);
const char *toString(CedeSyntaxPurpose value);
const char *toString(TransferSurfaceSpelling value);
const char *toString(TransferSourceCategory value);
const char *toString(TransferOwnershipKind value);
const char *toString(TransferCopyProof value);
const char *toString(TransferFormalContract value);
const char *toString(TransferFormalMorphology value);
const char *toString(TransferFormalOwnershipKind value);
const char *toString(TransferFormalTransferClass value);
const char *toString(TransferFormalContractOrigin value);
const char *toString(TransferEligibility value);
const char *toString(TransferEligibilityContext value);
const char *toString(TransferTemporaryEligibility value);
const char *toString(TransferTypeCompatibility value);
const char *toString(TransferDependencyKind value);
const char *toString(TransferDestinationObligationAction value);
std::string semanticPlaceKey(const PlaceId &place);

} // namespace toka
