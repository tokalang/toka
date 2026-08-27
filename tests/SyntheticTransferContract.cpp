#include "support/SyntheticTransferContract.h"

#include <type_traits>

using namespace toka;
using namespace toka::synthetic;

#define CHECK(condition)                                                       \
  do {                                                                         \
    if (!(condition))                                                          \
      return __LINE__;                                                         \
  } while (false)

namespace {

struct Fixture {
  SourceOriginId Origin =
      StructuralIdentityBuilder::sourceOrigin("fixture", "transfer", 0);
  SemanticNodeId Node = StructuralIdentityBuilder::semanticNode(Origin, 0, 1);
  CallSiteId Call = StructuralIdentityBuilder::callSite(Node);
  TypeId Type = StructuralIdentityBuilder::type("i32");
  DeclarationId Decl = StructuralIdentityBuilder::declaration(
      "fixture", "transfer", "fn", "consume");
  RootSymbolId Root = StructuralIdentityBuilder::rootSymbol(Node, "value", 0);
  PlaceId Place = PlaceId(Root);
  DestinationId Destination = DestinationId::formalSlot(Call, 1);
  ArgumentPlanId Plan = StructuralIdentityBuilder::argumentPlan(Call, 1, 1);
  TransferEdgeId Edge = StructuralIdentityBuilder::transferEdge(Plan, 0);
  ValidatedCallId Validated = StructuralIdentityBuilder::validatedCall(Call);
  TemporaryId Temporary = StructuralIdentityBuilder::temporary(Node, 0, 0);
  CleanupId Cleanup = StructuralIdentityBuilder::cleanup(Temporary, 0, 0);
  RegionRef CallRegion{StructuralIdentityBuilder::region(
                           Node, SemanticRegionKind::CallEvaluation, 3, 0),
                       SemanticRegionKind::CallEvaluation, 3};
  RegionRef LongerRegion{StructuralIdentityBuilder::region(
                             Node, SemanticRegionKind::Lexical, 1, 0),
                         SemanticRegionKind::Lexical, 1};
  LoanId Loan = StructuralIdentityBuilder::loan(Node, Place, Place, 0);
  InitObligationId Obligation =
      StructuralIdentityBuilder::initObligation(Call, 1);
  OutcomeTransitionId Transition =
      StructuralIdentityBuilder::outcomeTransition(Decl, 0);
};

LiabilitySource noLiabilitySource() { return {}; }
LiabilityTarget noLiabilityTarget() { return {}; }

ValidatedTransferEdge copyEdge(const Fixture &f,
                               SourceDisposition disposition) {
  ValidatedTransferEdge edge;
  edge.Id = f.Edge;
  edge.Plan = f.Plan;
  edge.Mode = TransferMode::CopyValue;
  edge.Type = f.Type;
  edge.Source = f.Place;
  edge.SourceState = disposition;
  edge.Admission = disposition == SourceDisposition::InvalidateWhole
                       ? ExactPlaceAdmission::whole(f.Place)
                       : ExactPlaceAdmission::none();
  edge.Destination = f.Destination;
  edge.LiabilityFrom = noLiabilitySource();
  edge.LiabilityTo = noLiabilityTarget();
  return edge;
}

template <typename Payload>
JournalAction action(const Fixture &f, uint64_t ordinal, ActionFacts facts = {},
                     std::optional<SemanticJournalPhase> phase = std::nullopt) {
  JournalAction result;
  result.Id = StructuralIdentityBuilder::journalAction(
      f.Node, Payload::ExpectedPhase, ordinal);
  result.DeclaredPhase = phase.value_or(Payload::ExpectedPhase);
  result.SourceNode = f.Node;
  result.Payload = Payload{std::move(facts)};
  return result;
}

template <typename Payload> constexpr SemanticJournalPhase wrongPhase() {
  return Payload::ExpectedPhase == SemanticJournalPhase::Evaluation
             ? SemanticJournalPhase::Boundary
             : SemanticJournalPhase::Evaluation;
}

int testActionShapes(const Fixture &f) {
  uint64_t ordinal = 1;
#define CHECK_ACTION(Name)                                                     \
  CHECK(validateActionShape(action<Name##Action>(f, ordinal++)) ==             \
        TransferValidationError::None);                                        \
  CHECK(validateActionShape(action<Name##Action>(                              \
            f, ordinal++, {}, wrongPhase<Name##Action>())) ==                  \
        TransferValidationError::InvalidPhase)
  CHECK_ACTION(WritePlace);
  CHECK_ACTION(BeginLoan);
  CHECK_ACTION(EndLoan);
  CHECK_ACTION(CreateTemporary);
  CHECK_ACTION(ScheduleTemporaryCleanup);
  CHECK_ACTION(CompleteTemporaryCleanup);
  CHECK_ACTION(ApplyNestedCall);
  CHECK_ACTION(InvalidateWholePlace);
  CHECK_ACTION(InvalidateProjection);
  CHECK_ACTION(InstallBoundaryBorrow);
  CHECK_ACTION(TransferDropLiability);
  CHECK_ACTION(TransferTemporaryLiability);
  CHECK_ACTION(RetainSharedLiability);
  CHECK_ACTION(RecordDependencyHandoff);
  CHECK_ACTION(FulfillImmediateInitObligation);
  CHECK_ACTION(CreatePendingOutcomeObligation);
  CHECK_ACTION(ResolveOutcomeCase);
  CHECK_ACTION(ForwardPendingOutcome);
  CHECK_ACTION(CancelPendingOutcome);
  CHECK_ACTION(EndBoundaryLoan);
  CHECK_ACTION(TransferBoundaryLoanRegion);
  CHECK_ACTION(CompleteFullExpressionCleanup);
  CHECK_ACTION(DisarmTransferredCleanup);
  CHECK_ACTION(TransferCleanupRegion);
  CHECK_ACTION(EndCallRegion);
#undef CHECK_ACTION
  return 0;
}

int testSourceDisposition(const Fixture &f) {
  const auto keep = copyEdge(f, SourceDisposition::KeepLive);
  CHECK(validateEdge(keep) == TransferValidationError::None);
  ValidatedJournal keepJournal{f.Validated, {keep}, {}, {}};
  CHECK(validateJournal(keepJournal) == TransferValidationError::None);

  const auto invalidate = copyEdge(f, SourceDisposition::InvalidateWhole);
  ActionFacts invalidationFacts;
  invalidationFacts.Edge = invalidate.Id;
  invalidationFacts.Place = f.Place;
  invalidationFacts.Destination = f.Destination;
  auto invalidateAction =
      action<InvalidateWholePlaceAction>(f, 1, invalidationFacts);
  ValidatedJournal invalidateJournal{
      f.Validated, {invalidate}, {}, {invalidateAction}};
  CHECK(validateJournal(invalidateJournal) == TransferValidationError::None);

  keepJournal.Actions.push_back(invalidateAction);
  CHECK(validateJournal(keepJournal) == TransferValidationError::EdgeMismatch);

  auto missingAdmission = invalidate;
  missingAdmission.Admission = ExactPlaceAdmission::none();
  CHECK(validateEdge(missingAdmission) ==
        TransferValidationError::InvalidAdmission);

  auto noSource = invalidate;
  noSource.SourceState = SourceDisposition::NoSourcePlace;
  noSource.Admission = ExactPlaceAdmission::none();
  CHECK(validateEdge(noSource) ==
        TransferValidationError::InvalidSourceDisposition);

  const auto field = StructuralIdentityBuilder::field(f.Decl, "field", 0);
  PlaceId projected(f.Root, {PlaceProjection::field(field)});
  auto projection = invalidate;
  projection.Source = projected;
  projection.SourceState = SourceDisposition::InvalidateProjection;
  projection.Admission = ExactPlaceAdmission::projection(projected, 0x1);
  ActionFacts projectionFacts;
  projectionFacts.Edge = projection.Id;
  projectionFacts.Place = projected;
  projectionFacts.Destination = f.Destination;
  projectionFacts.CleanupMask = 0x1;
  ValidatedJournal projectionJournal{
      f.Validated,
      {projection},
      {},
      {action<InvalidateProjectionAction>(f, 2, projectionFacts)}};
  CHECK(validateJournal(projectionJournal) == TransferValidationError::None);
  projectionFacts.CleanupMask = 0x2;
  projectionJournal.Actions = {
      action<InvalidateProjectionAction>(f, 3, projectionFacts)};
  CHECK(validateJournal(projectionJournal) ==
        TransferValidationError::EdgeMismatch);
  return 0;
}

int testEdgeActionCoupling(const Fixture &f) {
  auto dependency = copyEdge(f, SourceDisposition::KeepLive);
  dependency.DependencyRoots = {f.Place};
  ActionFacts dependencyFacts;
  dependencyFacts.Edge = dependency.Id;
  dependencyFacts.Destination = f.Destination;
  dependencyFacts.DependencyRoots = dependency.DependencyRoots;
  ValidatedJournal dependencyJournal{
      f.Validated,
      {dependency},
      {},
      {action<RecordDependencyHandoffAction>(f, 1, dependencyFacts)}};
  CHECK(validateJournal(dependencyJournal) == TransferValidationError::None);
  auto wrongDependency = dependencyFacts;
  wrongDependency.DependencyRoots.clear();
  dependencyJournal.Actions = {
      action<RecordDependencyHandoffAction>(f, 2, wrongDependency)};
  CHECK(validateJournal(dependencyJournal) ==
        TransferValidationError::EdgeMismatch);

  auto shared = copyEdge(f, SourceDisposition::KeepLive);
  shared.Mode = TransferMode::TransferShared;
  shared.Shared = SharedDisposition::RetainIdentity;
  ActionFacts sharedFacts;
  sharedFacts.Edge = shared.Id;
  sharedFacts.Destination = f.Destination;
  sharedFacts.Shared = SharedDisposition::RetainIdentity;
  ValidatedJournal sharedJournal{
      f.Validated,
      {shared},
      {},
      {action<RetainSharedLiabilityAction>(f, 3, sharedFacts)}};
  CHECK(validateJournal(sharedJournal) == TransferValidationError::None);
  sharedFacts.Shared = SharedDisposition::MoveIdentity;
  sharedJournal.Actions = {
      action<RetainSharedLiabilityAction>(f, 4, sharedFacts)};
  CHECK(validateJournal(sharedJournal) ==
        TransferValidationError::EdgeMismatch);

  auto liability = copyEdge(f, SourceDisposition::KeepLive);
  liability.LiabilityFrom = {
      LiabilitySourceKind::PlaceCleanup, f.Place, {}, f.Cleanup};
  liability.LiabilityTo = {LiabilityTargetKind::DestinationAssumed,
                           f.Destination, f.Cleanup};
  ActionFacts liabilityFacts;
  liabilityFacts.Edge = liability.Id;
  liabilityFacts.Destination = f.Destination;
  liabilityFacts.LiabilityFromKind = LiabilitySourceKind::PlaceCleanup;
  liabilityFacts.LiabilityToKind = LiabilityTargetKind::DestinationAssumed;
  ValidatedJournal liabilityJournal{
      f.Validated,
      {liability},
      {},
      {action<TransferDropLiabilityAction>(f, 5, liabilityFacts)}};
  CHECK(validateJournal(liabilityJournal) == TransferValidationError::None);
  liabilityFacts.LiabilityFromKind = LiabilitySourceKind::TemporaryCleanup;
  liabilityJournal.Actions = {
      action<TransferDropLiabilityAction>(f, 6, liabilityFacts)};
  CHECK(validateJournal(liabilityJournal) ==
        TransferValidationError::EdgeMismatch);

  auto init = copyEdge(f, SourceDisposition::KeepLive);
  init.Mode = TransferMode::InitHandoff;
  ActionFacts initFacts;
  initFacts.Edge = init.Id;
  initFacts.Destination = f.Destination;
  initFacts.Obligation = f.Obligation;
  ValidatedJournal initJournal{
      f.Validated,
      {init},
      {},
      {action<FulfillImmediateInitObligationAction>(f, 7, initFacts)}};
  CHECK(validateJournal(initJournal) == TransferValidationError::None);
  initJournal.Actions.clear();
  CHECK(validateJournal(initJournal) ==
        TransferValidationError::MissingTerminal);
  return 0;
}

ValidatedTransferEdge loanEdge(const Fixture &f, bool transfer) {
  auto edge = copyEdge(f, SourceDisposition::KeepLive);
  edge.Mode = TransferMode::BorrowCapture;
  BoundaryLoanPlan loan;
  loan.Loan = f.Loan;
  loan.Source = f.Place;
  loan.Referent = f.Place;
  loan.CallRegion = f.CallRegion;
  loan.Destination = f.Destination;
  loan.Exit = transfer ? LoanExitKind::TransferRegion : LoanExitKind::End;
  if (transfer)
    loan.NewRegion = f.LongerRegion;
  edge.Loan = loan;
  return edge;
}

int testLoanFinalization(const Fixture &f) {
  for (bool transfer : {false, true}) {
    auto edge = loanEdge(f, transfer);
    CHECK(validateEdge(edge) == TransferValidationError::None);
    ActionFacts install;
    install.Edge = edge.Id;
    install.Destination = f.Destination;
    install.Loan = f.Loan;
    install.Place = f.Place;
    install.Referent = f.Place;
    install.OldRegion = f.CallRegion;
    std::vector<JournalAction> actions{
        action<InstallBoundaryBorrowAction>(f, 1, install)};
    ActionFacts terminal = install;
    if (transfer) {
      terminal.NewRegion = f.LongerRegion;
      actions.push_back(
          action<TransferBoundaryLoanRegionAction>(f, 2, terminal));
    } else {
      actions.push_back(action<EndBoundaryLoanAction>(f, 2, terminal));
    }
    ValidatedJournal journal{f.Validated, {edge}, {}, actions};
    CHECK(validateJournal(journal) == TransferValidationError::None);
    journal.Actions.push_back(journal.Actions.back());
    journal.Actions.back().Id = StructuralIdentityBuilder::journalAction(
        f.Node, SemanticJournalPhase::Finalization, 90 + transfer);
    CHECK(validateJournal(journal) == TransferValidationError::MissingTerminal);
    journal.Actions.pop_back();
    journal.Actions.pop_back();
    CHECK(validateJournal(journal) == TransferValidationError::MissingTerminal);
  }

  auto invalid = loanEdge(f, true);
  invalid.Loan->NewRegion = f.CallRegion;
  CHECK(validateEdge(invalid) == TransferValidationError::InvalidLoanPlan);
  return 0;
}

int testEvaluationLoan(const Fixture &f) {
  auto edge = copyEdge(f, SourceDisposition::KeepLive);
  ActionFacts begin;
  begin.Loan = f.Loan;
  begin.Place = f.Place;
  begin.Referent = f.Place;
  begin.OldRegion = f.CallRegion;
  ValidatedJournal journal{f.Validated,
                           {edge},
                           {},
                           {action<BeginLoanAction>(f, 1, begin),
                            action<EndLoanAction>(f, 2, begin)}};
  CHECK(validateJournal(journal) == TransferValidationError::None);
  journal.Actions.pop_back();
  CHECK(validateJournal(journal) == TransferValidationError::MissingTerminal);
  return 0;
}

ValidatedTransferEdge cleanupEdge(const Fixture &f, bool extend) {
  ValidatedTransferEdge edge;
  edge.Id = f.Edge;
  edge.Plan = f.Plan;
  edge.Mode = TransferMode::ConsumeTemporary;
  edge.Type = f.Type;
  edge.SourceState = SourceDisposition::NoSourcePlace;
  edge.Destination = f.Destination;
  edge.LiabilityFrom = {
      LiabilitySourceKind::TemporaryCleanup, {}, f.Temporary, f.Cleanup};
  edge.LiabilityTo =
      extend ? LiabilityTarget{LiabilityTargetKind::CompletedAtFinalization,
                               {},
                               f.Cleanup}
             : LiabilityTarget{LiabilityTargetKind::DestinationAssumed,
                               f.Destination, f.Cleanup};
  TemporaryCleanupPlan cleanup;
  cleanup.Temporary = f.Temporary;
  cleanup.Cleanup = f.Cleanup;
  cleanup.Type = f.Type;
  if (extend) {
    cleanup.Steps.push_back({f.CallRegion, CleanupExitKind::TransferRegion,
                             f.LongerRegion, f.Destination, f.Edge});
    cleanup.Steps.push_back(
        {f.LongerRegion, CleanupExitKind::Complete, std::nullopt, {}, {}});
  } else {
    cleanup.Steps.push_back({f.CallRegion, CleanupExitKind::Disarm,
                             std::nullopt, f.Destination, f.Edge});
  }
  edge.TemporaryCleanup = cleanup;
  return edge;
}

int testCleanupRegions(const Fixture &f) {
  for (bool extend : {false, true}) {
    auto edge = cleanupEdge(f, extend);
    CHECK(validateEdge(edge) == TransferValidationError::None);
    ActionFacts schedule;
    schedule.Cleanup = f.Cleanup;
    schedule.OldRegion = f.CallRegion;
    std::vector<JournalAction> actions{
        action<ScheduleTemporaryCleanupAction>(f, 1, schedule)};
    if (extend) {
      ActionFacts transfer;
      transfer.Edge = edge.Id;
      transfer.Cleanup = f.Cleanup;
      transfer.OldRegion = f.CallRegion;
      transfer.NewRegion = f.LongerRegion;
      transfer.Destination = f.Destination;
      actions.push_back(action<TransferCleanupRegionAction>(f, 2, transfer));
      ActionFacts complete;
      complete.Edge = edge.Id;
      complete.Cleanup = f.Cleanup;
      actions.push_back(
          action<CompleteFullExpressionCleanupAction>(f, 3, complete));
    } else {
      ActionFacts transferLiability;
      transferLiability.Edge = edge.Id;
      transferLiability.Cleanup = f.Cleanup;
      transferLiability.Destination = f.Destination;
      transferLiability.LiabilityToKind = edge.LiabilityTo.Kind;
      actions.push_back(
          action<TransferTemporaryLiabilityAction>(f, 2, transferLiability));
      ActionFacts disarm = transferLiability;
      actions.push_back(action<DisarmTransferredCleanupAction>(f, 3, disarm));
    }
    ValidatedJournal journal{f.Validated, {edge}, {}, actions};
    CHECK(validateJournal(journal) == TransferValidationError::None);
    journal.Actions.push_back(journal.Actions.back());
    journal.Actions.back().Id = StructuralIdentityBuilder::journalAction(
        f.Node, SemanticJournalPhase::Finalization, 90 + extend);
    CHECK(validateJournal(journal) == TransferValidationError::MissingTerminal);
  }

  auto cycle = cleanupEdge(f, true);
  cycle.TemporaryCleanup->Steps[0].NewRegion = f.CallRegion;
  CHECK(validateEdge(cycle) == TransferValidationError::InvalidCleanupPlan);
  return 0;
}

int testPendingOutcome(const Fixture &f) {
  auto edge = copyEdge(f, SourceDisposition::KeepLive);
  edge.Mode = TransferMode::PendingOutcome;
  PendingOutcomeObligation pending;
  pending.Id = f.Obligation;
  pending.Edge = edge.Id;
  pending.Transition = f.Transition;
  pending.Destination = f.Destination;
  pending.Cases = {{1, true, noLiabilityTarget(), std::nullopt},
                   {2, false, noLiabilityTarget(), std::nullopt}};
  pending.Cases[0].Cleanup = cleanupEdge(f, true).TemporaryCleanup;
  CHECK(validateOutcome(pending) == TransferValidationError::None);
  ActionFacts create;
  create.Edge = edge.Id;
  create.Obligation = pending.Id;
  ValidatedJournal journal{
      f.Validated,
      {edge},
      {pending},
      {action<CreatePendingOutcomeObligationAction>(f, 1, create)}};
  CHECK(validateJournal(journal) == TransferValidationError::None);

  auto resolved = pending;
  resolved.State = OutcomeState::Resolved;
  ActionFacts resolve = create;
  journal.Outcomes = {resolved};
  journal.Actions.push_back(action<ResolveOutcomeCaseAction>(f, 2, resolve));
  CHECK(validateJournal(journal) == TransferValidationError::None);
  journal.Actions.push_back(action<CancelPendingOutcomeAction>(f, 3, resolve));
  CHECK(validateJournal(journal) == TransferValidationError::MissingTerminal);

  auto forwarded = pending;
  forwarded.State = OutcomeState::Forwarded;
  journal.Outcomes = {forwarded};
  journal.Actions = {action<CreatePendingOutcomeObligationAction>(f, 4, create),
                     action<ForwardPendingOutcomeAction>(f, 5, resolve)};
  CHECK(validateJournal(journal) == TransferValidationError::None);

  auto cancelled = pending;
  cancelled.State = OutcomeState::Cancelled;
  journal.Outcomes = {cancelled};
  journal.Actions = {action<CreatePendingOutcomeObligationAction>(f, 6, create),
                     action<CancelPendingOutcomeAction>(f, 7, resolve)};
  CHECK(validateJournal(journal) == TransferValidationError::None);

  auto duplicate = pending;
  duplicate.Cases.push_back(duplicate.Cases.front());
  CHECK(validateOutcome(duplicate) == TransferValidationError::DuplicateCase);
  return 0;
}

int testNestedActionUniqueness(const Fixture &f) {
  auto edge = copyEdge(f, SourceDisposition::KeepLive);
  ActionFacts nested;
  nested.NestedCall = f.Validated;
  auto duplicate = StructuralIdentityBuilder::journalAction(
      f.Node, SemanticJournalPhase::Evaluation, 99);
  nested.NestedActionIds = {duplicate, duplicate};
  ValidatedJournal journal{
      f.Validated,
      {edge},
      {},
      {action<ApplyNestedCallAction>(f, 1, std::move(nested))}};
  CHECK(validateJournal(journal) ==
        TransferValidationError::DuplicateNestedAction);
  return 0;
}

} // namespace

int main() {
  static_assert(!std::is_convertible_v<TypeId, TemporaryId>);
  static_assert(!std::is_convertible_v<LoanId, RegionId>);
  Fixture fixture;
  if (int result = testActionShapes(fixture))
    return result;
  if (int result = testSourceDisposition(fixture))
    return result;
  if (int result = testEdgeActionCoupling(fixture))
    return result;
  if (int result = testLoanFinalization(fixture))
    return result;
  if (int result = testEvaluationLoan(fixture))
    return result;
  if (int result = testCleanupRegions(fixture))
    return result;
  if (int result = testPendingOutcome(fixture))
    return result;
  if (int result = testNestedActionUniqueness(fixture))
    return result;
  return 0;
}
