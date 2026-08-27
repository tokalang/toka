#pragma once

#include "toka/SemanticModel.h"

#include <algorithm>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace toka::synthetic {

enum class TransferMode : uint8_t {
  BorrowCapture,
  CopyValue,
  CopyIdentity,
  MoveOwned,
  TransferShared,
  ConsumeTemporary,
  InitHandoff,
  PendingOutcome,
};

enum class SourceDisposition : uint8_t {
  KeepLive,
  InvalidateWhole,
  InvalidateProjection,
  NoSourcePlace,
};

enum class SharedDisposition : uint8_t {
  NotShared,
  MoveIdentity,
  RetainIdentity,
};

enum class AdmissionKind : uint8_t { None, Whole, Projection };
enum class LiabilitySourceKind : uint8_t {
  None,
  PlaceCleanup,
  TemporaryCleanup,
  SharedHandle,
};
enum class LiabilityTargetKind : uint8_t {
  SourceRetained,
  DestinationAssumed,
  SharedRetained,
  CompletedAtFinalization,
  NoLiability,
};
enum class LoanExitKind : uint8_t { End, TransferRegion };
enum class CleanupExitKind : uint8_t { Complete, Disarm, TransferRegion };
enum class OutcomeState : uint8_t {
  Pending,
  Resolved,
  Forwarded,
  Cancelled,
};

struct RegionRef {
  RegionId Id;
  SemanticRegionKind Kind = SemanticRegionKind::Lexical;
  uint32_t Depth = 0;

  bool valid() const noexcept { return Id.valid(); }
  bool strictlyOutlives(const RegionRef &other) const noexcept {
    return valid() && other.valid() && Depth < other.Depth && Id != other.Id;
  }

  friend bool operator==(const RegionRef &lhs, const RegionRef &rhs) noexcept {
    return std::tie(lhs.Id, lhs.Kind, lhs.Depth) ==
           std::tie(rhs.Id, rhs.Kind, rhs.Depth);
  }
  friend bool operator!=(const RegionRef &lhs, const RegionRef &rhs) noexcept {
    return !(lhs == rhs);
  }
  friend bool operator<(const RegionRef &lhs, const RegionRef &rhs) noexcept {
    return std::tie(lhs.Id, lhs.Kind, lhs.Depth) <
           std::tie(rhs.Id, rhs.Kind, rhs.Depth);
  }
};

struct ExactPlaceAdmission {
  AdmissionKind Kind = AdmissionKind::None;
  PlaceId Place;
  uint64_t CleanupMask = 0;

  static ExactPlaceAdmission none() { return {}; }
  static ExactPlaceAdmission whole(PlaceId place) {
    return {AdmissionKind::Whole, std::move(place), 0};
  }
  static ExactPlaceAdmission projection(PlaceId place, uint64_t mask) {
    return {AdmissionKind::Projection, std::move(place), mask};
  }
};

struct LiabilitySource {
  LiabilitySourceKind Kind = LiabilitySourceKind::None;
  PlaceId Place;
  TemporaryId Temporary;
  CleanupId Cleanup;

  bool valid() const noexcept {
    switch (Kind) {
    case LiabilitySourceKind::None:
      return true;
    case LiabilitySourceKind::PlaceCleanup:
      return Place.valid() && Cleanup.valid();
    case LiabilitySourceKind::TemporaryCleanup:
      return Temporary.valid() && Cleanup.valid();
    case LiabilitySourceKind::SharedHandle:
      return Place.valid();
    }
    return false;
  }
};

struct LiabilityTarget {
  LiabilityTargetKind Kind = LiabilityTargetKind::NoLiability;
  DestinationId Destination;
  CleanupId Cleanup;

  bool valid() const noexcept {
    switch (Kind) {
    case LiabilityTargetKind::SourceRetained:
    case LiabilityTargetKind::NoLiability:
      return true;
    case LiabilityTargetKind::DestinationAssumed:
    case LiabilityTargetKind::SharedRetained:
      return Destination.valid();
    case LiabilityTargetKind::CompletedAtFinalization:
      return Cleanup.valid();
    }
    return false;
  }
};

struct BoundaryLoanPlan {
  LoanId Loan;
  PlaceId Source;
  PlaceId Referent;
  RegionRef CallRegion;
  DestinationId Destination;
  LoanExitKind Exit = LoanExitKind::End;
  std::optional<RegionRef> NewRegion;

  bool valid() const noexcept {
    if (!Loan.valid() || !Source.valid() || !Referent.valid() ||
        !CallRegion.valid() || !Destination.valid() ||
        CallRegion.Kind != SemanticRegionKind::CallEvaluation)
      return false;
    if (Exit == LoanExitKind::End)
      return !NewRegion;
    return NewRegion && NewRegion->strictlyOutlives(CallRegion);
  }
};

struct CleanupRegionStep {
  RegionRef Region;
  CleanupExitKind Exit = CleanupExitKind::Complete;
  std::optional<RegionRef> NewRegion;
  DestinationId Destination;
  TransferEdgeId Edge;

  bool validFor(const TransferEdgeId &ownerEdge) const noexcept {
    if (!Region.valid())
      return false;
    switch (Exit) {
    case CleanupExitKind::Complete:
      return !NewRegion && !Destination.valid() && !Edge.valid();
    case CleanupExitKind::Disarm:
      return !NewRegion && Destination.valid() && Edge == ownerEdge;
    case CleanupExitKind::TransferRegion:
      return NewRegion && NewRegion->strictlyOutlives(Region) &&
             Destination.valid() && Edge == ownerEdge;
    }
    return false;
  }
};

struct TemporaryCleanupPlan {
  TemporaryId Temporary;
  CleanupId Cleanup;
  TypeId Type;
  std::vector<CleanupRegionStep> Steps;

  bool validFor(const TransferEdgeId &ownerEdge) const noexcept {
    if (!Temporary.valid() || !Cleanup.valid() || !Type.valid() ||
        Steps.empty())
      return false;
    std::set<RegionId> seen;
    for (size_t index = 0; index < Steps.size(); ++index) {
      const auto &step = Steps[index];
      if (!step.validFor(ownerEdge) || !seen.insert(step.Region.Id).second)
        return false;
      if (step.Exit == CleanupExitKind::TransferRegion) {
        if (index + 1 >= Steps.size() ||
            Steps[index + 1].Region != *step.NewRegion)
          return false;
      } else if (index + 1 != Steps.size()) {
        return false;
      }
    }
    const auto terminal = Steps.back().Exit;
    return terminal == CleanupExitKind::Complete ||
           terminal == CleanupExitKind::Disarm;
  }
};

struct ValidatedTransferEdge {
  TransferEdgeId Id;
  ArgumentPlanId Plan;
  TransferMode Mode = TransferMode::CopyValue;
  TypeId Type;
  std::optional<PlaceId> Source;
  SourceDisposition SourceState = SourceDisposition::KeepLive;
  ExactPlaceAdmission Admission;
  DestinationId Destination;
  LiabilitySource LiabilityFrom;
  LiabilityTarget LiabilityTo;
  std::vector<PlaceId> DependencyRoots;
  SharedDisposition Shared = SharedDisposition::NotShared;
  std::optional<BoundaryLoanPlan> Loan;
  std::optional<TemporaryCleanupPlan> TemporaryCleanup;
};

struct OutcomeCasePlan {
  uint64_t CaseIdentity = 0;
  bool InitializesDestination = false;
  LiabilityTarget Liability;
  std::optional<TemporaryCleanupPlan> Cleanup;
};

struct PendingOutcomeObligation {
  InitObligationId Id;
  TransferEdgeId Edge;
  OutcomeTransitionId Transition;
  DestinationId Destination;
  OutcomeState State = OutcomeState::Pending;
  std::vector<OutcomeCasePlan> Cases;
};

enum class TransferValidationError : uint8_t {
  None,
  InvalidIdentity,
  InvalidSourceDisposition,
  InvalidAdmission,
  InvalidLiability,
  InvalidLoanPlan,
  InvalidCleanupPlan,
  InvalidOutcome,
  DuplicateCase,
  InvalidPhase,
  DuplicateAction,
  DuplicateNestedAction,
  MissingEdge,
  EdgeMismatch,
  MissingTerminal,
  DuplicateTerminal,
};

inline TransferValidationError validateEdge(const ValidatedTransferEdge &edge) {
  if (!edge.Id.valid() || !edge.Plan.valid() || !edge.Type.valid() ||
      !edge.Destination.valid())
    return TransferValidationError::InvalidIdentity;
  switch (edge.SourceState) {
  case SourceDisposition::KeepLive:
    if (edge.Admission.Kind != AdmissionKind::None)
      return TransferValidationError::InvalidAdmission;
    break;
  case SourceDisposition::InvalidateWhole:
    if (!edge.Source || edge.Admission.Kind != AdmissionKind::Whole ||
        edge.Admission.Place != *edge.Source)
      return TransferValidationError::InvalidAdmission;
    break;
  case SourceDisposition::InvalidateProjection:
    if (!edge.Source || edge.Admission.Kind != AdmissionKind::Projection ||
        edge.Admission.Place != *edge.Source || edge.Admission.CleanupMask == 0)
      return TransferValidationError::InvalidAdmission;
    break;
  case SourceDisposition::NoSourcePlace:
    if (edge.Source || edge.Admission.Kind != AdmissionKind::None)
      return TransferValidationError::InvalidSourceDisposition;
    break;
  }
  if (!edge.LiabilityFrom.valid() || !edge.LiabilityTo.valid())
    return TransferValidationError::InvalidLiability;
  if (edge.Loan && !edge.Loan->valid())
    return TransferValidationError::InvalidLoanPlan;
  if (edge.TemporaryCleanup && !edge.TemporaryCleanup->validFor(edge.Id))
    return TransferValidationError::InvalidCleanupPlan;
  if (edge.Shared == SharedDisposition::NotShared &&
      edge.Mode == TransferMode::TransferShared)
    return TransferValidationError::InvalidLiability;
  return TransferValidationError::None;
}

inline TransferValidationError
validateOutcome(const PendingOutcomeObligation &outcome) {
  if (!outcome.Id.valid() || !outcome.Edge.valid() ||
      !outcome.Transition.valid() || !outcome.Destination.valid() ||
      outcome.Cases.empty())
    return TransferValidationError::InvalidOutcome;
  std::set<uint64_t> cases;
  for (const auto &entry : outcome.Cases) {
    if (entry.CaseIdentity == 0 || !cases.insert(entry.CaseIdentity).second ||
        !entry.Liability.valid() ||
        (entry.Cleanup && !entry.Cleanup->validFor(outcome.Edge)))
      return TransferValidationError::DuplicateCase;
  }
  return TransferValidationError::None;
}

enum class ActionKind : uint8_t {
  WritePlace,
  BeginLoan,
  EndLoan,
  CreateTemporary,
  ScheduleTemporaryCleanup,
  CompleteTemporaryCleanup,
  ApplyNestedCall,
  InvalidateWholePlace,
  InvalidateProjection,
  InstallBoundaryBorrow,
  TransferDropLiability,
  TransferTemporaryLiability,
  RetainSharedLiability,
  RecordDependencyHandoff,
  FulfillImmediateInitObligation,
  CreatePendingOutcomeObligation,
  ResolveOutcomeCase,
  ForwardPendingOutcome,
  CancelPendingOutcome,
  EndBoundaryLoan,
  TransferBoundaryLoanRegion,
  CompleteFullExpressionCleanup,
  DisarmTransferredCleanup,
  TransferCleanupRegion,
  EndCallRegion,
};

struct ActionFacts {
  TransferEdgeId Edge;
  PlaceId Place;
  PlaceId Referent;
  DestinationId Destination;
  TemporaryId Temporary;
  TypeId Type;
  LoanId Loan;
  CleanupId Cleanup;
  RegionRef OldRegion;
  RegionRef NewRegion;
  InitObligationId Obligation;
  uint64_t CleanupMask = 0;
  LiabilitySourceKind LiabilityFromKind = LiabilitySourceKind::None;
  LiabilityTargetKind LiabilityToKind = LiabilityTargetKind::NoLiability;
  SharedDisposition Shared = SharedDisposition::NotShared;
  std::vector<PlaceId> DependencyRoots;
  ValidatedCallId NestedCall;
  std::vector<JournalActionId> NestedActionIds;
};

template <ActionKind K, SemanticJournalPhase P> struct TypedActionPayload {
  static constexpr ActionKind Kind = K;
  static constexpr SemanticJournalPhase ExpectedPhase = P;
  ActionFacts Facts;
};

#define TOKA_SYNTHETIC_ACTIONS(X)                                              \
  X(WritePlace, Evaluation)                                                    \
  X(BeginLoan, Evaluation)                                                     \
  X(EndLoan, Evaluation)                                                       \
  X(CreateTemporary, Evaluation)                                               \
  X(ScheduleTemporaryCleanup, Evaluation)                                      \
  X(CompleteTemporaryCleanup, Evaluation)                                      \
  X(ApplyNestedCall, Evaluation)                                               \
  X(InvalidateWholePlace, Boundary)                                            \
  X(InvalidateProjection, Boundary)                                            \
  X(InstallBoundaryBorrow, Boundary)                                           \
  X(TransferDropLiability, Boundary)                                           \
  X(TransferTemporaryLiability, Boundary)                                      \
  X(RetainSharedLiability, Boundary)                                           \
  X(RecordDependencyHandoff, Boundary)                                         \
  X(FulfillImmediateInitObligation, Boundary)                                  \
  X(CreatePendingOutcomeObligation, Boundary)                                  \
  X(ResolveOutcomeCase, Boundary)                                              \
  X(ForwardPendingOutcome, Boundary)                                           \
  X(CancelPendingOutcome, Boundary)                                            \
  X(EndBoundaryLoan, Finalization)                                             \
  X(TransferBoundaryLoanRegion, Finalization)                                  \
  X(CompleteFullExpressionCleanup, Finalization)                               \
  X(DisarmTransferredCleanup, Finalization)                                    \
  X(TransferCleanupRegion, Finalization)                                       \
  X(EndCallRegion, Finalization)

#define TOKA_DECLARE_ACTION(Name, Phase)                                       \
  using Name##Action =                                                         \
      TypedActionPayload<ActionKind::Name, SemanticJournalPhase::Phase>;
TOKA_SYNTHETIC_ACTIONS(TOKA_DECLARE_ACTION)
#undef TOKA_DECLARE_ACTION

#define TOKA_ACTION_VARIANT(Name, Phase) Name##Action,
using JournalPayload =
    std::variant<TOKA_SYNTHETIC_ACTIONS(TOKA_ACTION_VARIANT) std::monostate>;
#undef TOKA_ACTION_VARIANT

struct JournalAction {
  JournalActionId Id;
  SemanticJournalPhase DeclaredPhase = SemanticJournalPhase::Evaluation;
  SemanticNodeId SourceNode;
  JournalPayload Payload;
};

inline ActionKind actionKind(const JournalPayload &payload) {
  return std::visit(
      [](const auto &value) {
        using Value = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<Value, std::monostate>)
          return ActionKind::WritePlace;
        else
          return Value::Kind;
      },
      payload);
}

inline SemanticJournalPhase expectedPhase(const JournalPayload &payload) {
  return std::visit(
      [](const auto &value) {
        using Value = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<Value, std::monostate>)
          return SemanticJournalPhase::Evaluation;
        else
          return Value::ExpectedPhase;
      },
      payload);
}

inline const ActionFacts *actionFacts(const JournalPayload &payload) {
  return std::visit(
      [](const auto &value) -> const ActionFacts * {
        using Value = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<Value, std::monostate>)
          return nullptr;
        else
          return &value.Facts;
      },
      payload);
}

struct ValidatedJournal {
  ValidatedCallId Call;
  std::vector<ValidatedTransferEdge> Edges;
  std::vector<PendingOutcomeObligation> Outcomes;
  std::vector<JournalAction> Actions;
};

inline TransferValidationError
validateActionShape(const JournalAction &action) {
  if (!action.Id.valid() || !action.SourceNode.valid() ||
      !actionFacts(action.Payload))
    return TransferValidationError::InvalidIdentity;
  return action.DeclaredPhase == expectedPhase(action.Payload)
             ? TransferValidationError::None
             : TransferValidationError::InvalidPhase;
}

inline TransferValidationError
validateJournal(const ValidatedJournal &journal) {
  if (!journal.Call.valid())
    return TransferValidationError::InvalidIdentity;
  std::map<TransferEdgeId, const ValidatedTransferEdge *> edges;
  for (const auto &edge : journal.Edges) {
    auto error = validateEdge(edge);
    if (error != TransferValidationError::None)
      return error;
    if (!edges.emplace(edge.Id, &edge).second)
      return TransferValidationError::InvalidIdentity;
  }
  std::map<InitObligationId, const PendingOutcomeObligation *> outcomes;
  for (const auto &outcome : journal.Outcomes) {
    auto error = validateOutcome(outcome);
    if (error != TransferValidationError::None)
      return error;
    if (!outcomes.emplace(outcome.Id, &outcome).second)
      return TransferValidationError::InvalidOutcome;
    auto edge = edges.find(outcome.Edge);
    if (edge == edges.end() || edge->second->Destination != outcome.Destination)
      return TransferValidationError::EdgeMismatch;
  }

  std::set<JournalActionId> ids;
  std::map<TransferEdgeId, unsigned> invalidations;
  std::map<TransferEdgeId, unsigned> loanInstalls;
  std::map<TransferEdgeId, unsigned> loanTerminals;
  std::map<LoanId, unsigned> evaluationLoanBegins;
  std::map<LoanId, unsigned> evaluationLoanEnds;
  std::map<CleanupId, unsigned> cleanupSchedules;
  std::map<CleanupId, unsigned> cleanupTerminals;
  std::map<InitObligationId, unsigned> outcomeCreates;
  std::map<InitObligationId, unsigned> outcomeTerminals;
  std::map<TransferEdgeId, unsigned> liabilityTransfers;
  std::map<TransferEdgeId, unsigned> temporaryLiabilityTransfers;
  std::map<TransferEdgeId, unsigned> sharedRetains;
  std::map<TransferEdgeId, unsigned> dependencyHandoffs;
  std::map<TransferEdgeId, unsigned> immediateInitFulfills;
  std::map<TransferEdgeId, unsigned> pendingOutcomeCreates;
  SemanticJournalPhase previous = SemanticJournalPhase::Evaluation;

  for (const auto &action : journal.Actions) {
    if (auto shapeError = validateActionShape(action);
        shapeError != TransferValidationError::None)
      return shapeError;
    if (!ids.insert(action.Id).second)
      return TransferValidationError::DuplicateAction;
    if (static_cast<unsigned>(action.DeclaredPhase) <
            static_cast<unsigned>(previous) ||
        action.DeclaredPhase != expectedPhase(action.Payload))
      return TransferValidationError::InvalidPhase;
    previous = action.DeclaredPhase;
    const ActionFacts *facts = actionFacts(action.Payload);
    if (!facts)
      return TransferValidationError::InvalidPhase;
    for (const auto &nested : facts->NestedActionIds) {
      if (!nested.valid() || !ids.insert(nested).second)
        return TransferValidationError::DuplicateNestedAction;
    }

    const auto kind = actionKind(action.Payload);
    const ValidatedTransferEdge *edge = nullptr;
    if (facts->Edge.valid()) {
      auto found = edges.find(facts->Edge);
      if (found == edges.end())
        return TransferValidationError::MissingEdge;
      edge = found->second;
      if (facts->Destination.valid() && facts->Destination != edge->Destination)
        return TransferValidationError::EdgeMismatch;
    }

    switch (kind) {
    case ActionKind::WritePlace:
      if (!facts->Place.valid())
        return TransferValidationError::InvalidIdentity;
      break;
    case ActionKind::BeginLoan:
      if (!facts->Loan.valid() || !facts->Place.valid() ||
          !facts->Referent.valid() || !facts->OldRegion.valid())
        return TransferValidationError::InvalidLoanPlan;
      ++evaluationLoanBegins[facts->Loan];
      break;
    case ActionKind::EndLoan:
      if (!facts->Loan.valid() || !facts->OldRegion.valid())
        return TransferValidationError::InvalidLoanPlan;
      ++evaluationLoanEnds[facts->Loan];
      break;
    case ActionKind::CreateTemporary:
      if (!facts->Temporary.valid() || !facts->Type.valid())
        return TransferValidationError::InvalidIdentity;
      break;
    case ActionKind::ApplyNestedCall:
      if (!facts->NestedCall.valid())
        return TransferValidationError::InvalidIdentity;
      break;
    case ActionKind::InvalidateWholePlace:
      if (!edge || edge->SourceState != SourceDisposition::InvalidateWhole ||
          !edge->Source || facts->Place != *edge->Source)
        return TransferValidationError::EdgeMismatch;
      ++invalidations[edge->Id];
      break;
    case ActionKind::InvalidateProjection:
      if (!edge ||
          edge->SourceState != SourceDisposition::InvalidateProjection ||
          !edge->Source || facts->Place != *edge->Source ||
          facts->CleanupMask != edge->Admission.CleanupMask)
        return TransferValidationError::EdgeMismatch;
      ++invalidations[edge->Id];
      break;
    case ActionKind::InstallBoundaryBorrow:
      if (!edge || !edge->Loan || facts->Loan != edge->Loan->Loan ||
          facts->Place != edge->Loan->Source ||
          facts->Referent != edge->Loan->Referent ||
          facts->OldRegion != edge->Loan->CallRegion)
        return TransferValidationError::EdgeMismatch;
      ++loanInstalls[edge->Id];
      break;
    case ActionKind::EndBoundaryLoan:
      if (!edge || !edge->Loan || edge->Loan->Exit != LoanExitKind::End ||
          facts->Loan != edge->Loan->Loan ||
          facts->OldRegion != edge->Loan->CallRegion)
        return TransferValidationError::EdgeMismatch;
      ++loanTerminals[edge->Id];
      break;
    case ActionKind::TransferBoundaryLoanRegion:
      if (!edge || !edge->Loan ||
          edge->Loan->Exit != LoanExitKind::TransferRegion ||
          !edge->Loan->NewRegion || facts->Loan != edge->Loan->Loan ||
          facts->OldRegion != edge->Loan->CallRegion ||
          facts->NewRegion != *edge->Loan->NewRegion)
        return TransferValidationError::EdgeMismatch;
      ++loanTerminals[edge->Id];
      break;
    case ActionKind::ScheduleTemporaryCleanup:
      if (!facts->Cleanup.valid() || !facts->OldRegion.valid())
        return TransferValidationError::InvalidCleanupPlan;
      ++cleanupSchedules[facts->Cleanup];
      break;
    case ActionKind::CompleteTemporaryCleanup:
      if (!facts->Cleanup.valid() || !facts->OldRegion.valid())
        return TransferValidationError::InvalidCleanupPlan;
      ++cleanupTerminals[facts->Cleanup];
      break;
    case ActionKind::CompleteFullExpressionCleanup:
      if (!edge || !edge->TemporaryCleanup ||
          facts->Cleanup != edge->TemporaryCleanup->Cleanup ||
          edge->TemporaryCleanup->Steps.back().Exit !=
              CleanupExitKind::Complete)
        return TransferValidationError::EdgeMismatch;
      ++cleanupTerminals[facts->Cleanup];
      break;
    case ActionKind::DisarmTransferredCleanup:
      if (!edge || !edge->TemporaryCleanup ||
          facts->Cleanup != edge->TemporaryCleanup->Cleanup ||
          edge->TemporaryCleanup->Steps.back().Exit !=
              CleanupExitKind::Disarm ||
          facts->Destination != edge->Destination)
        return TransferValidationError::EdgeMismatch;
      ++cleanupTerminals[facts->Cleanup];
      break;
    case ActionKind::TransferDropLiability:
      if (!edge || facts->LiabilityFromKind != edge->LiabilityFrom.Kind ||
          facts->LiabilityToKind != edge->LiabilityTo.Kind)
        return TransferValidationError::EdgeMismatch;
      ++liabilityTransfers[edge->Id];
      break;
    case ActionKind::TransferTemporaryLiability:
      if (!edge || !edge->TemporaryCleanup ||
          facts->Cleanup != edge->TemporaryCleanup->Cleanup ||
          facts->LiabilityToKind != edge->LiabilityTo.Kind)
        return TransferValidationError::EdgeMismatch;
      ++temporaryLiabilityTransfers[edge->Id];
      break;
    case ActionKind::RetainSharedLiability:
      if (!edge || facts->Shared != edge->Shared ||
          edge->Shared != SharedDisposition::RetainIdentity)
        return TransferValidationError::EdgeMismatch;
      ++sharedRetains[edge->Id];
      break;
    case ActionKind::RecordDependencyHandoff:
      if (!edge || facts->DependencyRoots != edge->DependencyRoots)
        return TransferValidationError::EdgeMismatch;
      ++dependencyHandoffs[edge->Id];
      break;
    case ActionKind::TransferCleanupRegion:
      if (!edge || !edge->TemporaryCleanup ||
          facts->Cleanup != edge->TemporaryCleanup->Cleanup ||
          !facts->NewRegion.strictlyOutlives(facts->OldRegion))
        return TransferValidationError::EdgeMismatch;
      if (std::none_of(edge->TemporaryCleanup->Steps.begin(),
                       edge->TemporaryCleanup->Steps.end(),
                       [&](const auto &step) {
                         return step.Exit == CleanupExitKind::TransferRegion &&
                                step.Region == facts->OldRegion &&
                                step.NewRegion &&
                                *step.NewRegion == facts->NewRegion &&
                                step.Destination == facts->Destination &&
                                step.Edge == edge->Id;
                       }))
        return TransferValidationError::EdgeMismatch;
      break;
    case ActionKind::CreatePendingOutcomeObligation:
      if (!facts->Obligation.valid() ||
          outcomes.find(facts->Obligation) == outcomes.end())
        return TransferValidationError::InvalidOutcome;
      if (!edge || outcomes.at(facts->Obligation)->Edge != edge->Id)
        return TransferValidationError::EdgeMismatch;
      ++outcomeCreates[facts->Obligation];
      ++pendingOutcomeCreates[edge->Id];
      break;
    case ActionKind::FulfillImmediateInitObligation:
      if (!edge || !facts->Obligation.valid() || !facts->Destination.valid())
        return TransferValidationError::InvalidOutcome;
      ++immediateInitFulfills[edge->Id];
      break;
    case ActionKind::EndCallRegion:
      if (!facts->OldRegion.valid())
        return TransferValidationError::InvalidLoanPlan;
      break;
    case ActionKind::ResolveOutcomeCase:
    case ActionKind::ForwardPendingOutcome:
    case ActionKind::CancelPendingOutcome:
      if (!facts->Obligation.valid() ||
          outcomes.find(facts->Obligation) == outcomes.end())
        return TransferValidationError::InvalidOutcome;
      ++outcomeTerminals[facts->Obligation];
      break;
    default:
      break;
    }
  }

  for (const auto &[loan, begins] : evaluationLoanBegins) {
    if (begins != 1 || evaluationLoanEnds[loan] != 1)
      return TransferValidationError::MissingTerminal;
  }
  for (const auto &[loan, ends] : evaluationLoanEnds) {
    if (ends != 1 || evaluationLoanBegins[loan] != 1)
      return TransferValidationError::MissingTerminal;
  }
  for (const auto &[cleanup, schedules] : cleanupSchedules) {
    if (schedules != 1 || cleanupTerminals[cleanup] != 1)
      return TransferValidationError::MissingTerminal;
  }

  for (const auto &[id, edge] : edges) {
    const unsigned expectedInvalidation =
        edge->SourceState == SourceDisposition::InvalidateWhole ||
                edge->SourceState == SourceDisposition::InvalidateProjection
            ? 1U
            : 0U;
    if (invalidations[id] != expectedInvalidation)
      return expectedInvalidation ? TransferValidationError::MissingTerminal
                                  : TransferValidationError::EdgeMismatch;
    if (edge->Loan && (loanInstalls[id] != 1 || loanTerminals[id] != 1))
      return TransferValidationError::MissingTerminal;
    if (edge->TemporaryCleanup) {
      const auto cleanup = edge->TemporaryCleanup->Cleanup;
      if (cleanupSchedules[cleanup] != 1 || cleanupTerminals[cleanup] != 1)
        return TransferValidationError::MissingTerminal;
    }
    const bool needsPlaceLiabilityTransfer =
        edge->LiabilityFrom.Kind == LiabilitySourceKind::PlaceCleanup &&
        edge->LiabilityTo.Kind == LiabilityTargetKind::DestinationAssumed;
    if (liabilityTransfers[id] != (needsPlaceLiabilityTransfer ? 1U : 0U))
      return TransferValidationError::MissingTerminal;
    const bool needsTemporaryLiabilityTransfer =
        edge->LiabilityFrom.Kind == LiabilitySourceKind::TemporaryCleanup &&
        edge->LiabilityTo.Kind == LiabilityTargetKind::DestinationAssumed;
    if (temporaryLiabilityTransfers[id] !=
        (needsTemporaryLiabilityTransfer ? 1U : 0U))
      return TransferValidationError::MissingTerminal;
    const bool needsRetain = edge->Shared == SharedDisposition::RetainIdentity;
    if (sharedRetains[id] != (needsRetain ? 1U : 0U))
      return TransferValidationError::MissingTerminal;
    const bool needsDependencies = !edge->DependencyRoots.empty();
    if (dependencyHandoffs[id] != (needsDependencies ? 1U : 0U))
      return TransferValidationError::MissingTerminal;
    const bool needsImmediateInit = edge->Mode == TransferMode::InitHandoff;
    if (immediateInitFulfills[id] != (needsImmediateInit ? 1U : 0U))
      return TransferValidationError::MissingTerminal;
    const bool needsPendingOutcome = edge->Mode == TransferMode::PendingOutcome;
    if (pendingOutcomeCreates[id] != (needsPendingOutcome ? 1U : 0U))
      return TransferValidationError::MissingTerminal;
  }
  for (const auto &[id, outcome] : outcomes) {
    if (outcomeCreates[id] != 1 || outcomeTerminals[id] > 1)
      return TransferValidationError::MissingTerminal;
    const bool resolved = outcome->State != OutcomeState::Pending;
    if ((resolved ? 1U : 0U) != outcomeTerminals[id])
      return TransferValidationError::InvalidOutcome;
  }
  return TransferValidationError::None;
}

inline uint64_t journalDigest(const ValidatedJournal &journal) noexcept {
  uint64_t result = 1469598103934665603ULL;
  auto mix = [&result](const std::string &value) {
    for (unsigned char byte : value) {
      result ^= byte;
      result *= 1099511628211ULL;
    }
  };
  auto number = [&mix](uint64_t value) { mix(std::to_string(value)); };
  mix(journal.Call.canonicalKey());
  for (const auto &edge : journal.Edges) {
    mix(edge.Id.canonicalKey());
    mix(edge.Plan.canonicalKey());
    number(static_cast<uint64_t>(edge.Mode));
    mix(edge.Type.canonicalKey());
    mix(edge.Source ? edge.Source->canonicalKey() : std::string());
    number(static_cast<uint64_t>(edge.SourceState));
    number(static_cast<uint64_t>(edge.Admission.Kind));
    mix(edge.Admission.Place.canonicalKey());
    number(edge.Admission.CleanupMask);
    mix(edge.Destination.canonicalKey());
    number(static_cast<uint64_t>(edge.LiabilityFrom.Kind));
    mix(edge.LiabilityFrom.Place.canonicalKey());
    mix(edge.LiabilityFrom.Temporary.canonicalKey());
    mix(edge.LiabilityFrom.Cleanup.canonicalKey());
    number(static_cast<uint64_t>(edge.LiabilityTo.Kind));
    mix(edge.LiabilityTo.Destination.canonicalKey());
    mix(edge.LiabilityTo.Cleanup.canonicalKey());
    number(static_cast<uint64_t>(edge.Shared));
    for (const auto &dependency : edge.DependencyRoots)
      mix(dependency.canonicalKey());
    if (edge.Loan) {
      mix(edge.Loan->Loan.canonicalKey());
      mix(edge.Loan->Source.canonicalKey());
      mix(edge.Loan->Referent.canonicalKey());
      mix(edge.Loan->CallRegion.Id.canonicalKey());
      number(edge.Loan->CallRegion.Depth);
      number(static_cast<uint64_t>(edge.Loan->Exit));
      if (edge.Loan->NewRegion) {
        mix(edge.Loan->NewRegion->Id.canonicalKey());
        number(edge.Loan->NewRegion->Depth);
      }
    }
    if (edge.TemporaryCleanup) {
      mix(edge.TemporaryCleanup->Temporary.canonicalKey());
      mix(edge.TemporaryCleanup->Cleanup.canonicalKey());
      for (const auto &step : edge.TemporaryCleanup->Steps) {
        mix(step.Region.Id.canonicalKey());
        number(step.Region.Depth);
        number(static_cast<uint64_t>(step.Exit));
        if (step.NewRegion)
          mix(step.NewRegion->Id.canonicalKey());
        mix(step.Destination.canonicalKey());
        mix(step.Edge.canonicalKey());
      }
    }
  }
  for (const auto &outcome : journal.Outcomes) {
    mix(outcome.Id.canonicalKey());
    mix(outcome.Edge.canonicalKey());
    mix(outcome.Transition.canonicalKey());
    mix(outcome.Destination.canonicalKey());
    number(static_cast<uint64_t>(outcome.State));
    for (const auto &casePlan : outcome.Cases) {
      number(casePlan.CaseIdentity);
      number(casePlan.InitializesDestination ? 1 : 0);
      number(static_cast<uint64_t>(casePlan.Liability.Kind));
    }
  }
  for (const auto &action : journal.Actions) {
    mix(action.Id.canonicalKey());
    number(static_cast<uint64_t>(action.DeclaredPhase));
    number(static_cast<uint64_t>(actionKind(action.Payload)));
    mix(action.SourceNode.canonicalKey());
    if (const auto *facts = actionFacts(action.Payload)) {
      mix(facts->Edge.canonicalKey());
      mix(facts->Place.canonicalKey());
      mix(facts->Referent.canonicalKey());
      mix(facts->Destination.canonicalKey());
      mix(facts->Temporary.canonicalKey());
      mix(facts->Type.canonicalKey());
      mix(facts->Loan.canonicalKey());
      mix(facts->Cleanup.canonicalKey());
      mix(facts->OldRegion.Id.canonicalKey());
      mix(facts->NewRegion.Id.canonicalKey());
      mix(facts->Obligation.canonicalKey());
      number(facts->CleanupMask);
      for (const auto &nested : facts->NestedActionIds)
        mix(nested.canonicalKey());
    }
  }
  return result;
}

#undef TOKA_SYNTHETIC_ACTIONS

} // namespace toka::synthetic
