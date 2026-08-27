#include "support/SyntheticSemanticTransaction.h"

#include <type_traits>
#include <unordered_set>

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
      StructuralIdentityBuilder::sourceOrigin("fixture", "transaction", 0);
  SemanticNodeId Node = StructuralIdentityBuilder::semanticNode(Origin, 0, 1);
  SemanticNodeId Node2 = StructuralIdentityBuilder::semanticNode(Origin, 0, 2);
  DeclarationId Decl = StructuralIdentityBuilder::declaration(
      "fixture", "transaction", "fn", "call");
  TypeId Type = StructuralIdentityBuilder::type("i32");
  CallSiteId Call = StructuralIdentityBuilder::callSite(Node);
  ConversionId Conversion =
      StructuralIdentityBuilder::conversion(Node, Type, 0);
  DestinationId Destination = DestinationId::formalSlot(Call, 1);
  RootSymbolId Root = StructuralIdentityBuilder::rootSymbol(Node, "value", 0);
  PlaceId Place = PlaceId(Root);
  ArgumentPlanId Plan = StructuralIdentityBuilder::argumentPlan(Call, 1, 1);
  TransferEdgeId Edge = StructuralIdentityBuilder::transferEdge(Plan, 0);
  SubstitutionId Substitution =
      StructuralIdentityBuilder::substitution("T=i32");
  ResolvedCalleeId Callee =
      ResolvedCalleeId::genericInstance(Decl, Substitution);
  TemporaryId Temporary = StructuralIdentityBuilder::temporary(Node, 0, 0);
  CleanupId Cleanup = StructuralIdentityBuilder::cleanup(Temporary, 0, 0);
  InitObligationId Obligation =
      StructuralIdentityBuilder::initObligation(Call, 1);
  OutcomeTransitionId Outcome =
      StructuralIdentityBuilder::outcomeTransition(Decl, 0);
  ValidatedCallId Validated = StructuralIdentityBuilder::validatedCall(Call);
  LoweringRecipeId Lowering =
      StructuralIdentityBuilder::loweringRecipe(Call, 0);
  SemanticModelPatchId Patch = StructuralIdentityBuilder::modelPatch(Call, 0);
  SemanticRevisionId Revision = StructuralIdentityBuilder::revision(Origin, 1);
  TransactionId RootTransaction =
      StructuralIdentityBuilder::transaction(Node, 0);
  StructuralForkKey Fork =
      StructuralIdentityBuilder::structuralFork(Node, 0, 0);
  BranchSetId BranchSet = StructuralIdentityBuilder::branchSet(Node, 0);
  BranchKey BranchA = StructuralIdentityBuilder::branchKey(BranchSet, 1);
  BranchKey BranchB = StructuralIdentityBuilder::branchKey(BranchSet, 2);
  BranchKey BranchC = StructuralIdentityBuilder::branchKey(BranchSet, 3);
};

ValidatedJournal transactionJournal(const Fixture &f, uint64_t actionOrdinal) {
  ValidatedTransferEdge edge;
  edge.Id = f.Edge;
  edge.Plan = f.Plan;
  edge.Mode = TransferMode::CopyValue;
  edge.Type = f.Type;
  edge.Source = f.Place;
  edge.SourceState = SourceDisposition::InvalidateWhole;
  edge.Admission = ExactPlaceAdmission::whole(f.Place);
  edge.Destination = f.Destination;

  ActionFacts facts;
  facts.Edge = f.Edge;
  facts.Place = f.Place;
  facts.Destination = f.Destination;
  JournalAction action;
  action.Id = StructuralIdentityBuilder::journalAction(
      f.Node, SemanticJournalPhase::Boundary, actionOrdinal);
  action.DeclaredPhase = SemanticJournalPhase::Boundary;
  action.SourceNode = f.Node;
  action.Payload = InvalidateWholePlaceAction{facts};
  return {f.Validated, {edge}, {}, {action}};
}

ModelEntry entry(ModelTable table, ModelKey key, std::string payload,
                 std::vector<ModelReference> references = {}) {
  return {table, std::move(key), std::move(payload), std::move(references)};
}

SemanticModelPatch inventoryPatch(const Fixture &f) {
  SemanticModelPatch patch;
  patch.Id = f.Patch;
  patch.Entries = {
      entry(ModelTable::DeclarationFacts, f.Decl, "decl"),
      entry(ModelTable::TypePropertiesByType, f.Type, "copy"),
      entry(ModelTable::ExprFacts, f.Node, "expr",
            {{ModelTable::TypePropertiesByType, f.Type}}),
      entry(ModelTable::ResolvedCalls, f.Call,
            "call;loan=call-region->lexical-region;state=live",
            {{ModelTable::DeclarationFacts, f.Decl}}),
      entry(ModelTable::ImplicitConversions, f.Conversion, "conversion",
            {{ModelTable::TypePropertiesByType, f.Type}}),
      entry(ModelTable::DefaultArguments, f.Destination, "default"),
      entry(ModelTable::SyntheticArguments, f.Node2, "synthetic"),
      entry(ModelTable::ReceiverLowering, f.Call, "receiver"),
      entry(ModelTable::GenericInstances, f.Callee, "generic"),
      entry(ModelTable::TemporaryCleanup, f.Cleanup,
            "cleanup;region=call->lexical;terminal=complete"),
      entry(ModelTable::InitOutcome, f.Obligation, "init"),
      entry(ModelTable::ValidatedCalls, f.Validated, "validated",
            {{ModelTable::ResolvedCalls, f.Call}}),
      entry(ModelTable::LoweringRecipes, f.Lowering, "lowering",
            {{ModelTable::ValidatedCalls, f.Validated}}),
      entry(ModelTable::SourceOrigins, f.Origin, "origin"),
  };
  return patch;
}

int testModelUnion(const Fixture &f) {
  SyntheticSemanticModel empty;
  auto patch = inventoryPatch(f);
  auto [error, model] = SyntheticStateBuilder::apply(empty, patch);
  CHECK(error == SyntheticError::None);
  CHECK(model.entries().size() == 14);

  auto [againError, again] = SyntheticStateBuilder::apply(model, patch);
  CHECK(againError == SyntheticError::None);
  CHECK(again == model);

  auto conflict = patch;
  conflict.Entries.front().Payload = "different";
  auto [conflictError, unchanged] =
      SyntheticStateBuilder::apply(model, conflict);
  CHECK(conflictError == SyntheticError::PatchConflict);
  CHECK(unchanged == model);

  SemanticModelPatch dangling;
  dangling.Id = StructuralIdentityBuilder::modelPatch(f.Call, 1);
  dangling.Entries = {entry(ModelTable::ExprFacts, f.Node2, "dangling",
                            {{ModelTable::TypePropertiesByType,
                              StructuralIdentityBuilder::type("missing")}})};
  auto [danglingError, danglingUnchanged] =
      SyntheticStateBuilder::apply(model, dangling);
  CHECK(danglingError == SyntheticError::DanglingReference);
  CHECK(danglingUnchanged == model);

  SemanticModelPatch wrongDomain;
  wrongDomain.Id = StructuralIdentityBuilder::modelPatch(f.Call, 2);
  wrongDomain.Entries = {
      entry(ModelTable::TypePropertiesByType, f.Node2, "wrong")};
  CHECK(SyntheticStateBuilder::apply(model, wrongDomain).first ==
        SyntheticError::InvalidIdentity);

  for (auto fault : {SyntheticFaultPoint::PatchUnion,
                     SyntheticFaultPoint::FullKeyCollisionValidation,
                     SyntheticFaultPoint::CrossReferenceValidation}) {
    auto [faultError, faultModel] =
        SyntheticStateBuilder::apply(model, patch, fault);
    CHECK(faultError == SyntheticError::InjectedFailure);
    CHECK(faultModel == model);
  }

  struct ConstantHash {
    size_t operator()(const SemanticNodeId &) const noexcept { return 1; }
  };
  std::unordered_set<SemanticNodeId, ConstantHash> collisions{f.Node, f.Node2};
  CHECK(collisions.size() == 2);
  return 0;
}

int testLifecycle(const Fixture &f) {
  PublishedSemanticSnapshot published(
      StructuralIdentityBuilder::revision(f.Origin, 0));
  SyntheticError error = SyntheticError::None;
  auto rootOpt =
      SyntheticAnalysisTransaction::root(f.RootTransaction, published, error);
  CHECK(rootOpt && error == SyntheticError::None);
  auto root = std::move(*rootOpt);
  CHECK(root.setManifest(StateFamily::OwnershipFacts, 1) ==
        SyntheticError::None);

  const uint64_t parentDigest = root.digest();
  auto discardedOpt = root.fork(f.Fork, error);
  CHECK(discardedOpt);
  auto discarded = std::move(*discardedOpt);
  CHECK(discarded.setManifest(StateFamily::OwnershipFacts, 7) ==
        SyntheticError::None);
  CHECK(discarded.discard() == SyntheticError::None);
  CHECK(root.digest() == parentDigest);
  CHECK(discarded.discard() == SyntheticError::LifecycleViolation);

  auto childOpt =
      root.fork(StructuralIdentityBuilder::structuralFork(f.Node, 1, 0), error);
  CHECK(childOpt);
  auto child = std::move(*childOpt);
  auto grandOpt = child.fork(
      StructuralIdentityBuilder::structuralFork(f.Node, 2, 0), error);
  CHECK(grandOpt);
  auto grand = std::move(*grandOpt);
  CHECK(grand.setManifest(StateFamily::Dependencies, 4) ==
        SyntheticError::None);
  CHECK(grand.validate() == SyntheticError::None);
  CHECK(child.adopt(std::move(grand)) == SyntheticError::None);
  CHECK(grand.state() == TransactionState::Adopted);
  CHECK(child.validate() == SyntheticError::None);
  CHECK(root.adopt(std::move(child)) == SyntheticError::None);
  CHECK(root.working()->Manifest.value(StateFamily::Dependencies) == 4);
  CHECK(root.adopt(std::move(child)) == SyntheticError::LifecycleViolation);

  auto staleOpt =
      root.fork(StructuralIdentityBuilder::structuralFork(f.Node, 3, 0), error);
  CHECK(staleOpt);
  auto stale = std::move(*staleOpt);
  CHECK(stale.validate() == SyntheticError::None);
  CHECK(root.setManifest(StateFamily::Instances, 9) == SyntheticError::None);
  CHECK(root.adopt(std::move(stale)) == SyntheticError::StaleParent);
  CHECK(stale.state() == TransactionState::Validated);

  PublishedSemanticSnapshot otherPublished(
      StructuralIdentityBuilder::revision(f.Origin, 10));
  auto otherRootOpt = SyntheticAnalysisTransaction::root(
      StructuralIdentityBuilder::transaction(f.Node2, 0), otherPublished,
      error);
  CHECK(otherRootOpt);
  auto otherRoot = std::move(*otherRootOpt);
  auto wrongOpt =
      root.fork(StructuralIdentityBuilder::structuralFork(f.Node, 4, 0), error);
  CHECK(wrongOpt);
  auto wrong = std::move(*wrongOpt);
  CHECK(wrong.validate() == SyntheticError::None);
  CHECK(otherRoot.adopt(std::move(wrong)) == SyntheticError::WrongParent);
  CHECK(wrong.state() == TransactionState::Validated);
  CHECK(wrong.commit(f.Revision) == SyntheticError::LifecycleViolation);

  CHECK(root.validate() == SyntheticError::None);
  CHECK(root.commit(f.Revision) == SyntheticError::None);
  CHECK(root.state() == TransactionState::Committed);
  CHECK(root.commit(f.Revision) == SyntheticError::LifecycleViolation);
  CHECK(published.epoch() == 1);
  CHECK(published.snapshot()->State.Manifest.value(StateFamily::Dependencies) ==
        4);
  return 0;
}

int testTransactionModelPublication(const Fixture &f) {
  PublishedSemanticSnapshot published(
      StructuralIdentityBuilder::revision(f.Origin, 11));
  SyntheticError error;
  auto rootOpt = SyntheticAnalysisTransaction::root(
      StructuralIdentityBuilder::transaction(f.Node, 11), published, error);
  CHECK(rootOpt);
  auto root = std::move(*rootOpt);
  auto basePatch = inventoryPatch(f);
  CHECK(root.stagePatch(basePatch) == SyntheticError::None);

  const auto baseDigest = root.digest();
  const auto baseEpoch = root.epoch();
  CHECK(root.stagePatch(basePatch) == SyntheticError::None);
  CHECK(root.digest() == baseDigest && root.epoch() == baseEpoch);
  for (auto fault :
       {SyntheticFaultPoint::PatchUnion,
        SyntheticFaultPoint::FullKeyCollisionValidation,
        SyntheticFaultPoint::CrossReferenceValidation,
        SyntheticFaultPoint::ImmutableSuccessorBuild,
        SyntheticFaultPoint::ManifestDigest, SyntheticFaultPoint::PreSwap}) {
    CHECK(root.stagePatch(basePatch, fault) == SyntheticError::InjectedFailure);
    CHECK(root.digest() == baseDigest && root.epoch() == baseEpoch);
  }
  auto conflict = basePatch;
  conflict.Id = StructuralIdentityBuilder::modelPatch(f.Call, 11);
  conflict.Entries.front().Payload = "conflict";
  CHECK(root.stagePatch(conflict) == SyntheticError::PatchConflict);
  CHECK(root.digest() == baseDigest && root.epoch() == baseEpoch);

  auto childOpt = root.fork(
      StructuralIdentityBuilder::structuralFork(f.Node, 11, 0), error);
  CHECK(childOpt);
  auto child = std::move(*childOpt);
  auto grandOpt = child.fork(
      StructuralIdentityBuilder::structuralFork(f.Node, 12, 0), error);
  CHECK(grandOpt);
  auto grand = std::move(*grandOpt);
  SemanticModelPatch nested;
  nested.Id = StructuralIdentityBuilder::modelPatch(f.Call, 12);
  nested.Entries = {entry(ModelTable::ExprFacts, f.Node2, "nested-expr",
                          {{ModelTable::TypePropertiesByType, f.Type}})};
  CHECK(grand.stagePatch(nested) == SyntheticError::None);
  auto journal = transactionJournal(f, 12);
  CHECK(grand.stageJournal(journal) == SyntheticError::None);
  const auto journalDigestBeforeDuplicate = grand.digest();
  const auto journalEpoch = grand.epoch();
  CHECK(grand.stageJournal(journal) == SyntheticError::PatchConflict);
  auto nestedCollision = transactionJournal(f, 13);
  std::get<InvalidateWholePlaceAction>(nestedCollision.Actions.front().Payload)
      .Facts.NestedActionIds = {journal.Actions.front().Id};
  CHECK(validateJournal(nestedCollision) == TransferValidationError::None);
  CHECK(grand.stageJournal(std::move(nestedCollision)) ==
        SyntheticError::PatchConflict);
  CHECK(grand.digest() == journalDigestBeforeDuplicate &&
        grand.epoch() == journalEpoch);
  CHECK(grand.validate() == SyntheticError::None);
  CHECK(child.adopt(std::move(grand)) == SyntheticError::None);
  CHECK(child.validate() == SyntheticError::None);
  CHECK(root.adopt(std::move(child)) == SyntheticError::None);
  CHECK(root.validate() == SyntheticError::None);
  auto revision = StructuralIdentityBuilder::revision(f.Origin, 12);
  CHECK(root.commit(revision) == SyntheticError::None);
  CHECK(published.snapshot()->State.Model.contains(ModelTable::ExprFacts,
                                                   f.Node2));
  CHECK(published.snapshot()->State.Model.payload(ModelTable::ResolvedCalls,
                                                  f.Call) ==
        std::optional<std::string>(
            "call;loan=call-region->lexical-region;state=live"));
  CHECK(published.snapshot()->State.Model.payload(ModelTable::TemporaryCleanup,
                                                  f.Cleanup) ==
        std::optional<std::string>(
            "cleanup;region=call->lexical;terminal=complete"));
  CHECK(published.snapshot()->State.Journals.size() == 1);
  CHECK(published.snapshot()->State.JournalActionIds.count(
            journal.Actions.front().Id) == 1);
  CHECK(published.snapshot()->Id == revision);
  return 0;
}

int testLifecycleFaults(const Fixture &f) {
  PublishedSemanticSnapshot published(
      StructuralIdentityBuilder::revision(f.Origin, 20));
  SyntheticError error;
  auto rootOpt = SyntheticAnalysisTransaction::root(
      StructuralIdentityBuilder::transaction(f.Node, 20), published, error);
  CHECK(rootOpt);
  auto root = std::move(*rootOpt);
  const auto rootDigest = root.digest();
  for (auto fault :
       {SyntheticFaultPoint::ImmutableSuccessorBuild,
        SyntheticFaultPoint::ManifestDigest, SyntheticFaultPoint::PreSwap}) {
    CHECK(root.setManifest(StateFamily::OwnershipFacts, 1, fault) ==
          SyntheticError::InjectedFailure);
    CHECK(root.digest() == rootDigest && root.epoch() == 0);
    CHECK(root.stageJournal(transactionJournal(f, 20), fault) ==
          SyntheticError::InjectedFailure);
    CHECK(root.digest() == rootDigest && root.epoch() == 0);
  }

  auto childOpt = root.fork(f.Fork, error);
  CHECK(childOpt);
  auto child = std::move(*childOpt);
  CHECK(child.setManifest(StateFamily::OwnershipFacts, 2) ==
        SyntheticError::None);
  CHECK(child.validate() == SyntheticError::None);
  const auto childDigest = child.digest();
  for (auto fault :
       {SyntheticFaultPoint::AdoptSuccessorBuild,
        SyntheticFaultPoint::ImmutableSuccessorBuild,
        SyntheticFaultPoint::ManifestDigest, SyntheticFaultPoint::PreSwap}) {
    CHECK(root.adopt(std::move(child), fault) ==
          SyntheticError::InjectedFailure);
    CHECK(root.digest() == rootDigest && root.epoch() == 0);
    CHECK(child.digest() == childDigest &&
          child.state() == TransactionState::Validated);
  }
  CHECK(root.adopt(std::move(child)) == SyntheticError::None);
  CHECK(root.validate() == SyntheticError::None);
  const auto publishedDigest = published.snapshot()->digest();
  for (auto fault :
       {SyntheticFaultPoint::RootSuccessorBuild,
        SyntheticFaultPoint::ImmutableSuccessorBuild,
        SyntheticFaultPoint::ManifestDigest, SyntheticFaultPoint::PreSwap}) {
    CHECK(root.commit(f.Revision, fault) == SyntheticError::InjectedFailure);
    CHECK(root.state() == TransactionState::Validated);
    CHECK(published.snapshot()->digest() == publishedDigest &&
          published.epoch() == 0);
  }
  CHECK(PublishedSemanticSnapshot::finalSwapNoexcept());
  return 0;
}

int testRootPublicationStale(const Fixture &f) {
  PublishedSemanticSnapshot published(
      StructuralIdentityBuilder::revision(f.Origin, 25));
  SyntheticError error;
  auto firstOpt = SyntheticAnalysisTransaction::root(
      StructuralIdentityBuilder::transaction(f.Node, 25), published, error);
  auto secondOpt = SyntheticAnalysisTransaction::root(
      StructuralIdentityBuilder::transaction(f.Node2, 25), published, error);
  CHECK(firstOpt && secondOpt);
  auto first = std::move(*firstOpt);
  auto second = std::move(*secondOpt);
  CHECK(first.validate() == SyntheticError::None);
  CHECK(second.validate() == SyntheticError::None);
  auto firstRevision = StructuralIdentityBuilder::revision(f.Origin, 26);
  CHECK(first.commit(firstRevision) == SyntheticError::None);
  const auto publishedDigest = published.snapshot()->digest();
  CHECK(second.commit(StructuralIdentityBuilder::revision(f.Origin, 27)) ==
        SyntheticError::StaleParent);
  CHECK(second.state() == TransactionState::Validated);
  CHECK(published.snapshot()->Id == firstRevision);
  CHECK(published.snapshot()->digest() == publishedDigest);
  return 0;
}

int testRejectedResult(const Fixture &f) {
  PublishedSemanticSnapshot published(
      StructuralIdentityBuilder::revision(f.Origin, 30));
  SyntheticError error;
  auto rootOpt = SyntheticAnalysisTransaction::root(
      StructuralIdentityBuilder::transaction(f.Node, 30), published, error);
  CHECK(rootOpt);
  auto root = std::move(*rootOpt);
  CHECK(root.addDiagnostic("E-test") == SyntheticError::None);
  CHECK(root.addInternalFact("rejected") == SyntheticError::None);
  const auto digest = root.digest();
  for (auto fault : {SyntheticFaultPoint::RejectedResultPrebuild,
                     SyntheticFaultPoint::ImmutableSuccessorBuild,
                     SyntheticFaultPoint::PreSwap}) {
    auto [faultError, absent] = root.reject(fault);
    CHECK(faultError == SyntheticError::InjectedFailure && !absent);
    CHECK(root.state() == TransactionState::Open && root.digest() == digest);
  }
  auto [ok, result] = root.reject();
  CHECK(ok == SyntheticError::None && result);
  CHECK(root.state() == TransactionState::Discarded);
  CHECK(result->Diagnostics == std::vector<std::string>{"E-test"});
  CHECK(result->InternalFacts == std::vector<std::string>{"rejected"});
  PublishedRejectedResult output;
  output.publish(result);
  CHECK(output.snapshot() == result);
  return 0;
}

int testBranchLaws() {
  for (uint64_t a = 0; a != 8; ++a) {
    for (uint64_t b = 0; b != 8; ++b) {
      CHECK(branchJoinValue(a, b) == branchJoinValue(b, a));
      CHECK(branchJoinValue(a, a) == a);
      CHECK(branchJoinValue(0, a) == a);
      for (uint64_t c = 0; c != 8; ++c) {
        CHECK(branchJoinValue(branchJoinValue(a, b), c) ==
              branchJoinValue(a, branchJoinValue(b, c)));
      }
    }
  }
  return 0;
}

int testBranchMerge(const Fixture &f) {
  PublishedSemanticSnapshot published(
      StructuralIdentityBuilder::revision(f.Origin, 40));
  SyntheticError error;
  auto rootOpt = SyntheticAnalysisTransaction::root(
      StructuralIdentityBuilder::transaction(f.Node, 40), published, error);
  CHECK(rootOpt);
  auto root = std::move(*rootOpt);
  SemanticModelPatch typePatch;
  typePatch.Id = StructuralIdentityBuilder::modelPatch(f.Call, 40);
  typePatch.Entries = {entry(ModelTable::TypePropertiesByType, f.Type, "copy")};
  CHECK(root.stagePatch(typePatch) == SyntheticError::None);
  auto setOpt = BranchFrameSet::create(f.BranchSet, root, error);
  CHECK(setOpt);
  auto set = std::move(*setOpt);
  CHECK(set.addFrame(f.BranchA) == SyntheticError::None);
  CHECK(set.addFrame(f.BranchB) == SyntheticError::None);
  CHECK(set.addFrame(f.BranchC) == SyntheticError::None);
  CHECK(set.removeProvisional(f.BranchC) == SyntheticError::None);
  CHECK(set.addFrame(f.BranchC) == SyntheticError::None);
  CHECK(set.freezeTopology() == SyntheticError::None);
  CHECK(set.setManifest(f.BranchA, StateFamily::OwnershipFacts, 1) ==
        SyntheticError::None);
  CHECK(set.setManifest(f.BranchB, StateFamily::OwnershipFacts, 2) ==
        SyntheticError::None);
  CHECK(set.setManifest(f.BranchC, StateFamily::OwnershipFacts, 4) ==
        SyntheticError::None);
  SemanticModelPatch branchPatch;
  branchPatch.Id = StructuralIdentityBuilder::modelPatch(f.Call, 41);
  branchPatch.Entries = {entry(ModelTable::ExprFacts, f.Node2, "branch-expr",
                               {{ModelTable::TypePropertiesByType, f.Type}})};
  CHECK(set.stagePatch(f.BranchA, branchPatch) == SyntheticError::None);
  CHECK(set.stagePatch(f.BranchB, branchPatch) == SyntheticError::None);
  auto branchJournal = transactionJournal(f, 40);
  CHECK(set.stageJournal(f.BranchA, branchJournal) == SyntheticError::None);
  CHECK(set.stageJournal(f.BranchB, branchJournal) == SyntheticError::None);
  CHECK(set.addDiagnostic(f.BranchB, "branch-diagnostic") ==
        SyntheticError::None);
  CHECK(set.addDiagnostic(f.BranchC, "unreachable-diagnostic") ==
        SyntheticError::None);
  CHECK(set.sealFrame(f.BranchA, BranchReachability::Reachable) ==
        SyntheticError::None);
  CHECK(set.sealFrame(f.BranchB, BranchReachability::UnchangedBase) ==
        SyntheticError::None);
  CHECK(set.sealFrame(f.BranchC, BranchReachability::Unreachable) ==
        SyntheticError::None);
  CHECK(set.sealSet() == SyntheticError::None);
  CHECK(set.merge(root) == SyntheticError::None);
  CHECK(set.state() == BranchSetState::Merged);
  CHECK(set.merge(root) == SyntheticError::LifecycleViolation);
  CHECK(root.working()->Manifest.value(StateFamily::OwnershipFacts) == 3);
  CHECK(root.working()->Model.contains(ModelTable::ExprFacts, f.Node2));
  CHECK(root.working()->Journals.size() == 1);
  CHECK(root.working()->JournalActionIds.count(
            branchJournal.Actions.front().Id) == 1);
  CHECK(root.working()->Diagnostics ==
        std::vector<std::string>(
            {"branch-diagnostic", "unreachable-diagnostic"}));
  return 0;
}

int testBranchDiscardAndStale(const Fixture &f) {
  PublishedSemanticSnapshot published(
      StructuralIdentityBuilder::revision(f.Origin, 50));
  SyntheticError error;
  auto rootOpt = SyntheticAnalysisTransaction::root(
      StructuralIdentityBuilder::transaction(f.Node, 50), published, error);
  CHECK(rootOpt);
  auto root = std::move(*rootOpt);
  const auto digest = root.digest();
  auto discardOpt = BranchFrameSet::create(
      StructuralIdentityBuilder::branchSet(f.Node, 50), root, error);
  CHECK(discardOpt);
  auto discardSet = std::move(*discardOpt);
  auto setId = StructuralIdentityBuilder::branchSet(f.Node, 50);
  auto a = StructuralIdentityBuilder::branchKey(setId, 1);
  auto b = StructuralIdentityBuilder::branchKey(setId, 2);
  CHECK(discardSet.addFrame(a) == SyntheticError::None);
  CHECK(discardSet.addFrame(b) == SyntheticError::None);
  CHECK(discardSet.freezeTopology() == SyntheticError::None);
  CHECK(discardSet.discardRegistered(a) == SyntheticError::None);
  CHECK(discardSet.state() == BranchSetState::Discarded);
  CHECK(discardSet.sealSet() == SyntheticError::LifecycleViolation);
  CHECK(root.digest() == digest && root.epoch() == 0);

  auto staleOpt = BranchFrameSet::create(
      StructuralIdentityBuilder::branchSet(f.Node, 51), root, error);
  CHECK(staleOpt);
  auto staleSet = std::move(*staleOpt);
  auto staleKey = StructuralIdentityBuilder::branchKey(
      StructuralIdentityBuilder::branchSet(f.Node, 51), 1);
  CHECK(staleSet.addFrame(staleKey) == SyntheticError::None);
  CHECK(staleSet.freezeTopology() == SyntheticError::None);
  CHECK(staleSet.sealFrame(staleKey, BranchReachability::Reachable) ==
        SyntheticError::None);
  CHECK(staleSet.sealSet() == SyntheticError::None);
  CHECK(root.setManifest(StateFamily::OwnershipFacts, 1) ==
        SyntheticError::None);
  CHECK(staleSet.merge(root) == SyntheticError::StaleParent);
  CHECK(staleSet.state() == BranchSetState::Sealed);

  auto secondOpt = BranchFrameSet::create(
      StructuralIdentityBuilder::branchSet(f.Node, 52), root, error);
  CHECK(secondOpt);
  auto second = std::move(*secondOpt);
  auto secondKey = StructuralIdentityBuilder::branchKey(
      StructuralIdentityBuilder::branchSet(f.Node, 52), 1);
  CHECK(second.addFrame(secondKey) == SyntheticError::None);
  CHECK(second.freezeTopology() == SyntheticError::None);
  CHECK(second.sealFrame(secondKey, BranchReachability::Reachable) ==
        SyntheticError::None);
  CHECK(second.sealSet() == SyntheticError::None);
  uint64_t hookDigest = 0;
  SyntheticError hookError = SyntheticError::None;
  CHECK(second.merge(root, SyntheticFaultPoint::None, [&] {
    hookError = root.setManifest(StateFamily::Dependencies, 7);
    hookDigest = root.digest();
  }) == SyntheticError::StaleParent);
  CHECK(hookError == SyntheticError::None);
  CHECK(root.digest() == hookDigest);
  CHECK(second.state() == BranchSetState::Sealed);
  return 0;
}

std::pair<SyntheticError, uint64_t> branchOrderDigest(const Fixture &f,
                                                      bool reverse) {
  PublishedSemanticSnapshot published(
      StructuralIdentityBuilder::revision(f.Origin, reverse ? 71 : 70));
  SyntheticError error;
  auto rootOpt = SyntheticAnalysisTransaction::root(
      StructuralIdentityBuilder::transaction(f.Node, reverse ? 71 : 70),
      published, error);
  if (!rootOpt)
    return {error, 0};
  auto root = std::move(*rootOpt);
  auto setId = StructuralIdentityBuilder::branchSet(f.Node, 70);
  auto setOpt = BranchFrameSet::create(setId, root, error);
  if (!setOpt)
    return {error, 0};
  auto set = std::move(*setOpt);
  auto first = StructuralIdentityBuilder::branchKey(setId, 1);
  auto second = StructuralIdentityBuilder::branchKey(setId, 2);
  if (reverse)
    std::swap(first, second);
  if (set.addFrame(first) != SyntheticError::None ||
      set.addFrame(second) != SyntheticError::None ||
      set.freezeTopology() != SyntheticError::None ||
      set.setManifest(first, StateFamily::OwnershipFacts, reverse ? 2 : 1) !=
          SyntheticError::None ||
      set.setManifest(second, StateFamily::OwnershipFacts, reverse ? 1 : 2) !=
          SyntheticError::None ||
      set.sealFrame(first, BranchReachability::Reachable) !=
          SyntheticError::None ||
      set.sealFrame(second, BranchReachability::Reachable) !=
          SyntheticError::None ||
      set.sealSet() != SyntheticError::None ||
      set.merge(root) != SyntheticError::None)
    return {SyntheticError::LifecycleViolation, 0};
  return {SyntheticError::None, root.digest()};
}

int testBranchOrderAndCollision(const Fixture &f) {
  auto [forwardError, forwardDigest] = branchOrderDigest(f, false);
  auto [reverseError, reverseDigest] = branchOrderDigest(f, true);
  CHECK(forwardError == SyntheticError::None);
  CHECK(reverseError == SyntheticError::None);
  CHECK(forwardDigest == reverseDigest);

  PublishedSemanticSnapshot published(
      StructuralIdentityBuilder::revision(f.Origin, 72));
  SyntheticError error;
  auto rootOpt = SyntheticAnalysisTransaction::root(
      StructuralIdentityBuilder::transaction(f.Node, 72), published, error);
  CHECK(rootOpt);
  auto root = std::move(*rootOpt);
  auto setId = StructuralIdentityBuilder::branchSet(f.Node, 72);
  auto setOpt = BranchFrameSet::create(setId, root, error);
  CHECK(setOpt);
  auto set = std::move(*setOpt);
  auto a = StructuralIdentityBuilder::branchKey(setId, 1);
  auto b = StructuralIdentityBuilder::branchKey(setId, 2);
  CHECK(set.addFrame(a) == SyntheticError::None);
  CHECK(set.addFrame(b) == SyntheticError::None);
  CHECK(set.freezeTopology() == SyntheticError::None);
  SemanticModelPatch left;
  left.Id = StructuralIdentityBuilder::modelPatch(f.Call, 72);
  left.Entries = {entry(ModelTable::ExprFacts, f.Node2, "left")};
  auto right = left;
  right.Id = StructuralIdentityBuilder::modelPatch(f.Call, 73);
  right.Entries.front().Payload = "right";
  CHECK(set.stagePatch(a, left) == SyntheticError::None);
  CHECK(set.stagePatch(b, right) == SyntheticError::None);
  CHECK(set.sealFrame(a, BranchReachability::Reachable) ==
        SyntheticError::None);
  CHECK(set.sealFrame(b, BranchReachability::Reachable) ==
        SyntheticError::None);
  CHECK(set.sealSet() == SyntheticError::None);
  const auto digest = root.digest();
  CHECK(set.merge(root) == SyntheticError::PatchConflict);
  CHECK(root.digest() == digest && root.epoch() == 0);
  CHECK(set.state() == BranchSetState::Sealed);
  return 0;
}

int testBranchFaults(const Fixture &f) {
  for (auto fault :
       {SyntheticFaultPoint::BranchLatticeJoin, SyntheticFaultPoint::PatchUnion,
        SyntheticFaultPoint::FullKeyCollisionValidation,
        SyntheticFaultPoint::CrossReferenceValidation,
        SyntheticFaultPoint::ImmutableSuccessorBuild,
        SyntheticFaultPoint::ManifestDigest, SyntheticFaultPoint::PreSwap}) {
    PublishedSemanticSnapshot published(
        StructuralIdentityBuilder::revision(f.Origin, 60));
    SyntheticError error;
    auto rootOpt = SyntheticAnalysisTransaction::root(
        StructuralIdentityBuilder::transaction(f.Node, 60), published, error);
    CHECK(rootOpt);
    auto root = std::move(*rootOpt);
    auto setId = StructuralIdentityBuilder::branchSet(f.Node, 60);
    auto setOpt = BranchFrameSet::create(setId, root, error);
    CHECK(setOpt);
    auto set = std::move(*setOpt);
    auto key = StructuralIdentityBuilder::branchKey(setId, 1);
    CHECK(set.addFrame(key) == SyntheticError::None);
    CHECK(set.freezeTopology() == SyntheticError::None);
    CHECK(set.sealFrame(key, BranchReachability::Reachable) ==
          SyntheticError::None);
    CHECK(set.sealSet() == SyntheticError::None);
    const auto digest = root.digest();
    CHECK(set.merge(root, fault) == SyntheticError::InjectedFailure);
    CHECK(root.digest() == digest && root.epoch() == 0);
    CHECK(set.state() == BranchSetState::Sealed);
  }
  return 0;
}

} // namespace

int main() {
  static_assert(TransactionalStateManifest::names().size() ==
                TransactionalStateManifest::count());
  static_assert(PublishedSemanticSnapshot::finalSwapNoexcept());
  Fixture fixture;
  if (int result = testModelUnion(fixture))
    return result;
  if (int result = testLifecycle(fixture))
    return result;
  if (int result = testTransactionModelPublication(fixture))
    return result;
  if (int result = testLifecycleFaults(fixture))
    return result;
  if (int result = testRootPublicationStale(fixture))
    return result;
  if (int result = testRejectedResult(fixture))
    return result;
  if (int result = testBranchLaws())
    return result;
  if (int result = testBranchMerge(fixture))
    return result;
  if (int result = testBranchDiscardAndStale(fixture))
    return result;
  if (int result = testBranchOrderAndCollision(fixture))
    return result;
  if (int result = testBranchFaults(fixture))
    return result;
  return 0;
}
