#pragma once

#include "SyntheticTransferContract.h"

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

namespace toka::synthetic {

class BranchFrameSet;

enum class StateFamily : uint8_t {
  LexicalScopes,
  OwnershipFacts,
  PALAndPlaceState,
  Dependencies,
  CaptureEffectReachability,
  Instances,
  Elaboration,
  SyntheticLowering,
  Diagnostics,
  InternalFactsAndIndex,
  IdentityAndOrigins,
  PureCaches,
  TransactionalCaches,
  CodeGenUnavailable,
  Count,
};

class TransactionalStateManifest final {
public:
  static constexpr size_t Count = static_cast<size_t>(StateFamily::Count);
  static constexpr size_t count() { return Count; }

  static constexpr std::array<std::string_view, Count> names() {
    return {"lexical-scopes",
            "ownership-facts",
            "pal-place-state",
            "dependencies",
            "capture-effect-reachability",
            "instances",
            "elaboration",
            "synthetic-lowering",
            "diagnostics",
            "internal-facts-index",
            "identity-origins",
            "pure-caches",
            "transactional-caches",
            "codegen-unavailable"};
  }
};

class SyntheticManifestState {
public:
  uint64_t value(StateFamily family) const noexcept {
    return Values[static_cast<size_t>(family)];
  }

  void set(StateFamily family, uint64_t value) noexcept {
    Values[static_cast<size_t>(family)] = value;
  }

  uint64_t digest() const noexcept {
    uint64_t result = 1469598103934665603ULL;
    for (uint64_t value : Values) {
      for (unsigned byte = 0; byte != 8; ++byte) {
        result ^= (value >> (byte * 8U)) & 0xffU;
        result *= 1099511628211ULL;
      }
    }
    return result;
  }

  friend bool operator==(const SyntheticManifestState &lhs,
                         const SyntheticManifestState &rhs) noexcept {
    return lhs.Values == rhs.Values;
  }

private:
  std::array<uint64_t, TransactionalStateManifest::count()> Values{};
};

enum class ModelTable : uint8_t {
  DeclarationFacts,
  TypePropertiesByType,
  ExprFacts,
  ResolvedCalls,
  ImplicitConversions,
  DefaultArguments,
  SyntheticArguments,
  ReceiverLowering,
  GenericInstances,
  TemporaryCleanup,
  InitOutcome,
  ValidatedCalls,
  LoweringRecipes,
  SourceOrigins,
};

using ModelKey =
    std::variant<DeclarationId, TypeId, SemanticNodeId, CallSiteId,
                 ConversionId, DestinationId, ResolvedCalleeId, CleanupId,
                 InitObligationId, OutcomeTransitionId, ValidatedCallId,
                 LoweringRecipeId, SourceOriginId, TemporaryId>;

inline bool modelKeyValid(const ModelKey &key) {
  return std::visit([](const auto &value) { return value.valid(); }, key);
}

inline bool modelKeyMatchesTable(ModelTable table, const ModelKey &key) {
  switch (table) {
  case ModelTable::DeclarationFacts:
    return std::holds_alternative<DeclarationId>(key);
  case ModelTable::TypePropertiesByType:
    return std::holds_alternative<TypeId>(key);
  case ModelTable::ExprFacts:
  case ModelTable::SyntheticArguments:
    return std::holds_alternative<SemanticNodeId>(key);
  case ModelTable::ResolvedCalls:
  case ModelTable::ReceiverLowering:
    return std::holds_alternative<CallSiteId>(key);
  case ModelTable::ImplicitConversions:
    return std::holds_alternative<ConversionId>(key);
  case ModelTable::DefaultArguments:
    return std::holds_alternative<DestinationId>(key);
  case ModelTable::GenericInstances:
    return std::holds_alternative<ResolvedCalleeId>(key) &&
           std::get<ResolvedCalleeId>(key).kind() ==
               ResolvedCalleeKind::GenericInstance;
  case ModelTable::TemporaryCleanup:
    return std::holds_alternative<CleanupId>(key);
  case ModelTable::InitOutcome:
    return std::holds_alternative<InitObligationId>(key) ||
           std::holds_alternative<OutcomeTransitionId>(key);
  case ModelTable::ValidatedCalls:
    return std::holds_alternative<ValidatedCallId>(key);
  case ModelTable::LoweringRecipes:
    return std::holds_alternative<LoweringRecipeId>(key);
  case ModelTable::SourceOrigins:
    return std::holds_alternative<SourceOriginId>(key);
  }
  return false;
}

inline std::string modelKeyCanonical(const ModelKey &key) {
  return std::visit([](const auto &value) { return value.canonicalKey(); },
                    key);
}

struct ModelReference {
  ModelTable Table = ModelTable::ExprFacts;
  ModelKey Key;

  friend bool operator==(const ModelReference &lhs, const ModelReference &rhs) {
    return lhs.Table == rhs.Table && lhs.Key == rhs.Key;
  }
  friend bool operator<(const ModelReference &lhs, const ModelReference &rhs) {
    return std::tie(lhs.Table, lhs.Key) < std::tie(rhs.Table, rhs.Key);
  }
};

struct ModelEntry {
  ModelTable Table = ModelTable::ExprFacts;
  ModelKey Key;
  std::string Payload;
  std::vector<ModelReference> References;

  friend bool operator==(const ModelEntry &lhs, const ModelEntry &rhs) {
    return std::tie(lhs.Table, lhs.Key, lhs.Payload, lhs.References) ==
           std::tie(rhs.Table, rhs.Key, rhs.Payload, rhs.References);
  }
  friend bool operator!=(const ModelEntry &lhs, const ModelEntry &rhs) {
    return !(lhs == rhs);
  }
  friend bool operator<(const ModelEntry &lhs, const ModelEntry &rhs) {
    return std::tie(lhs.Table, lhs.Key, lhs.Payload, lhs.References) <
           std::tie(rhs.Table, rhs.Key, rhs.Payload, rhs.References);
  }
};

struct SemanticModelPatch {
  SemanticModelPatchId Id;
  std::vector<ModelEntry> Entries;
};

class SyntheticSemanticModel {
public:
  const std::vector<ModelEntry> &entries() const noexcept { return Entries; }

  bool contains(ModelTable table, const ModelKey &key) const {
    return find(table, key) != Entries.end();
  }

  std::optional<std::string> payload(ModelTable table,
                                     const ModelKey &key) const {
    auto found = find(table, key);
    if (found == Entries.end())
      return std::nullopt;
    return found->Payload;
  }

  uint64_t digest() const noexcept {
    uint64_t result = 1469598103934665603ULL;
    for (const auto &entry : Entries) {
      mix(result, std::to_string(static_cast<unsigned>(entry.Table)));
      mix(result, modelKeyCanonical(entry.Key));
      mix(result, entry.Payload);
      for (const auto &reference : entry.References) {
        mix(result, std::to_string(static_cast<unsigned>(reference.Table)));
        mix(result, modelKeyCanonical(reference.Key));
      }
    }
    return result;
  }

  friend bool operator==(const SyntheticSemanticModel &lhs,
                         const SyntheticSemanticModel &rhs) {
    return lhs.Entries == rhs.Entries;
  }

private:
  std::vector<ModelEntry> Entries;

  using Iterator = std::vector<ModelEntry>::const_iterator;
  Iterator find(ModelTable table, const ModelKey &key) const {
    return std::find_if(Entries.begin(), Entries.end(), [&](const auto &entry) {
      return entry.Table == table && entry.Key == key;
    });
  }

  static void mix(uint64_t &value, const std::string &text) noexcept {
    for (unsigned char byte : text) {
      value ^= byte;
      value *= 1099511628211ULL;
    }
  }

  friend class SyntheticStateBuilder;
  friend class BranchFrameSet;
};

enum class SyntheticFaultPoint : uint8_t {
  None,
  PatchUnion,
  FullKeyCollisionValidation,
  CrossReferenceValidation,
  ImmutableSuccessorBuild,
  ManifestDigest,
  BranchLatticeJoin,
  RejectedResultPrebuild,
  AdoptSuccessorBuild,
  RootSuccessorBuild,
  PreSwap,
};

enum class SyntheticError : uint8_t {
  None,
  LifecycleViolation,
  MovedFrom,
  InvalidIdentity,
  WrongParent,
  StaleParent,
  PatchConflict,
  DanglingReference,
  InjectedFailure,
};

class SyntheticStateBuilder final {
public:
  static SyntheticError
  validateReferences(const SyntheticSemanticModel &model,
                     SyntheticFaultPoint fault = SyntheticFaultPoint::None) {
    if (fault == SyntheticFaultPoint::CrossReferenceValidation)
      return SyntheticError::InjectedFailure;
    for (const auto &entry : model.Entries) {
      for (const auto &reference : entry.References) {
        if (!modelKeyValid(reference.Key) ||
            !modelKeyMatchesTable(reference.Table, reference.Key) ||
            !model.contains(reference.Table, reference.Key))
          return SyntheticError::DanglingReference;
      }
    }
    return SyntheticError::None;
  }

  static std::pair<SyntheticError, SyntheticSemanticModel>
  apply(const SyntheticSemanticModel &base, const SemanticModelPatch &patch,
        SyntheticFaultPoint fault = SyntheticFaultPoint::None) {
    if (!patch.Id.valid())
      return {SyntheticError::InvalidIdentity, base};
    if (fault == SyntheticFaultPoint::PatchUnion ||
        fault == SyntheticFaultPoint::FullKeyCollisionValidation)
      return {SyntheticError::InjectedFailure, base};

    SyntheticSemanticModel result = base;
    for (const auto &entry : patch.Entries) {
      if (!modelKeyValid(entry.Key) ||
          !modelKeyMatchesTable(entry.Table, entry.Key))
        return {SyntheticError::InvalidIdentity, base};
      auto found = std::find_if(
          result.Entries.begin(), result.Entries.end(), [&](const auto &value) {
            return value.Table == entry.Table && value.Key == entry.Key;
          });
      if (found == result.Entries.end()) {
        result.Entries.push_back(entry);
      } else if (*found != entry) {
        return {SyntheticError::PatchConflict, base};
      }
    }
    std::sort(result.Entries.begin(), result.Entries.end());

    if (auto error = validateReferences(result, fault);
        error != SyntheticError::None)
      return {error, base};
    return {SyntheticError::None, std::move(result)};
  }
};

struct SyntheticSemanticState {
  SyntheticManifestState Manifest;
  SyntheticSemanticModel Model;
  std::vector<std::shared_ptr<const ValidatedJournal>> Journals;
  std::set<JournalActionId> JournalActionIds;
  std::vector<std::string> Diagnostics;
  std::vector<std::string> InternalFacts;

  uint64_t digest() const noexcept {
    uint64_t result = Manifest.digest() ^ (Model.digest() << 1U);
    for (const auto &journal : Journals) {
      result ^= journal ? journalDigest(*journal) : 0;
      result *= 1099511628211ULL;
    }
    for (const auto &action : JournalActionIds) {
      for (unsigned char byte : action.canonicalKey()) {
        result ^= byte;
        result *= 1099511628211ULL;
      }
    }
    auto mix = [&result](const std::vector<std::string> &values) {
      for (const auto &value : values) {
        for (unsigned char byte : value) {
          result ^= byte;
          result *= 1099511628211ULL;
        }
      }
    };
    mix(Diagnostics);
    mix(InternalFacts);
    return result;
  }

  friend bool operator==(const SyntheticSemanticState &lhs,
                         const SyntheticSemanticState &rhs) {
    return lhs.Manifest == rhs.Manifest && lhs.Model == rhs.Model &&
           journalDigests(lhs.Journals) == journalDigests(rhs.Journals) &&
           lhs.JournalActionIds == rhs.JournalActionIds &&
           lhs.Diagnostics == rhs.Diagnostics &&
           lhs.InternalFacts == rhs.InternalFacts;
  }

private:
  static std::vector<uint64_t> journalDigests(
      const std::vector<std::shared_ptr<const ValidatedJournal>> &journals) {
    std::vector<uint64_t> result;
    result.reserve(journals.size());
    for (const auto &journal : journals)
      result.push_back(journal ? journalDigest(*journal) : 0);
    return result;
  }
};

struct SemanticRevision {
  SemanticRevisionId Id;
  SyntheticSemanticState State;

  uint64_t digest() const noexcept {
    return Id.hashValue() ^ (State.digest() << 1U);
  }
};

class PublishedSemanticSnapshot final {
public:
  PublishedSemanticSnapshot(SemanticRevisionId id,
                            SyntheticSemanticState state = {})
      : Current(std::make_shared<const SemanticRevision>(
            SemanticRevision{std::move(id), std::move(state)})) {}

  const std::shared_ptr<const SemanticRevision> &snapshot() const noexcept {
    return Current;
  }
  uint64_t epoch() const noexcept { return Epoch; }
  static constexpr bool finalSwapNoexcept() noexcept {
    return noexcept(
        std::declval<std::shared_ptr<const SemanticRevision> &>().swap(
            std::declval<std::shared_ptr<const SemanticRevision> &>()));
  }

private:
  std::shared_ptr<const SemanticRevision> Current;
  uint64_t Epoch = 0;

  bool publish(uint64_t expectedEpoch,
               std::shared_ptr<const SemanticRevision> successor) noexcept {
    if (Epoch != expectedEpoch || !successor)
      return false;
    Current.swap(successor);
    ++Epoch;
    return true;
  }

  friend class SyntheticAnalysisTransaction;
};

enum class TransactionState : uint8_t {
  Open,
  Validated,
  Adopted,
  Committed,
  Discarded,
};

struct RejectedAnalysisResult {
  std::vector<std::string> Diagnostics;
  std::vector<std::string> InternalFacts;
};

class PublishedRejectedResult final {
public:
  const std::shared_ptr<const RejectedAnalysisResult> &
  snapshot() const noexcept {
    return Current;
  }
  void publish(std::shared_ptr<const RejectedAnalysisResult> result) noexcept {
    Current.swap(result);
  }

private:
  std::shared_ptr<const RejectedAnalysisResult> Current;
};

class SyntheticAnalysisTransaction final {
public:
  static std::optional<SyntheticAnalysisTransaction>
  root(TransactionId id, PublishedSemanticSnapshot &published,
       SyntheticError &error) {
    if (!id.valid() || !published.snapshot()) {
      error = SyntheticError::InvalidIdentity;
      return std::nullopt;
    }
    error = SyntheticError::None;
    auto initial = std::make_shared<const SyntheticSemanticState>(
        published.snapshot()->State);
    return SyntheticAnalysisTransaction(std::move(id), std::nullopt,
                                        published.epoch(), std::move(initial),
                                        &published);
  }

  SyntheticAnalysisTransaction(SyntheticAnalysisTransaction &&other) noexcept
      : Id(std::move(other.Id)), ParentId(std::move(other.ParentId)),
        BaseEpoch(other.BaseEpoch), Epoch(other.Epoch), State(other.State),
        Working(std::move(other.Working)), Publisher(other.Publisher),
        HandleValid(other.HandleValid) {
    other.HandleValid = false;
    other.Publisher = nullptr;
  }
  SyntheticAnalysisTransaction &
  operator=(SyntheticAnalysisTransaction &&) = delete;
  SyntheticAnalysisTransaction(const SyntheticAnalysisTransaction &) = delete;
  SyntheticAnalysisTransaction &
  operator=(const SyntheticAnalysisTransaction &) = delete;

  ~SyntheticAnalysisTransaction() {
    if (HandleValid && (State == TransactionState::Open ||
                        State == TransactionState::Validated))
      State = TransactionState::Discarded;
  }

  const TransactionId &id() const noexcept { return Id; }
  uint64_t epoch() const noexcept { return Epoch; }
  uint64_t baseEpoch() const noexcept { return BaseEpoch; }
  TransactionState state() const noexcept { return State; }
  uint64_t digest() const noexcept { return Working ? Working->digest() : 0; }
  const std::shared_ptr<const SyntheticSemanticState> &
  working() const noexcept {
    return Working;
  }

  std::optional<SyntheticAnalysisTransaction>
  fork(StructuralForkKey key, SyntheticError &error) const {
    if (!active()) {
      error = SyntheticError::MovedFrom;
      return std::nullopt;
    }
    if (State != TransactionState::Open) {
      error = SyntheticError::LifecycleViolation;
      return std::nullopt;
    }
    if (!key.valid()) {
      error = SyntheticError::InvalidIdentity;
      return std::nullopt;
    }
    error = SyntheticError::None;
    return SyntheticAnalysisTransaction(
        StructuralIdentityBuilder::childTransaction(Id, key), Id, Epoch,
        Working, nullptr);
  }

  SyntheticError
  setManifest(StateFamily family, uint64_t value,
              SyntheticFaultPoint fault = SyntheticFaultPoint::None) {
    if (auto error = requireOpen(); error != SyntheticError::None)
      return error;
    if (fault == SyntheticFaultPoint::ImmutableSuccessorBuild ||
        fault == SyntheticFaultPoint::ManifestDigest ||
        fault == SyntheticFaultPoint::PreSwap)
      return SyntheticError::InjectedFailure;
    if (Working->Manifest.value(family) == value)
      return SyntheticError::None;
    auto successor = std::make_shared<SyntheticSemanticState>(*Working);
    successor->Manifest.set(family, value);
    Working = std::move(successor);
    ++Epoch;
    return SyntheticError::None;
  }

  SyntheticError
  stagePatch(const SemanticModelPatch &patch,
             SyntheticFaultPoint fault = SyntheticFaultPoint::None) {
    if (auto error = requireOpen(); error != SyntheticError::None)
      return error;
    auto [error, model] =
        SyntheticStateBuilder::apply(Working->Model, patch, fault);
    if (error != SyntheticError::None)
      return error;
    if (fault == SyntheticFaultPoint::ImmutableSuccessorBuild ||
        fault == SyntheticFaultPoint::ManifestDigest ||
        fault == SyntheticFaultPoint::PreSwap)
      return SyntheticError::InjectedFailure;
    if (model == Working->Model)
      return SyntheticError::None;
    auto successor = std::make_shared<SyntheticSemanticState>(*Working);
    successor->Model = std::move(model);
    Working = std::move(successor);
    ++Epoch;
    return SyntheticError::None;
  }

  SyntheticError addDiagnostic(std::string diagnostic) {
    if (auto error = requireOpen(); error != SyntheticError::None)
      return error;
    auto successor = std::make_shared<SyntheticSemanticState>(*Working);
    successor->Diagnostics.push_back(std::move(diagnostic));
    Working = std::move(successor);
    ++Epoch;
    return SyntheticError::None;
  }

  SyntheticError
  stageJournal(ValidatedJournal journal,
               SyntheticFaultPoint fault = SyntheticFaultPoint::None) {
    if (auto error = requireOpen(); error != SyntheticError::None)
      return error;
    if (validateJournal(journal) != TransferValidationError::None)
      return SyntheticError::PatchConflict;
    std::set<JournalActionId> newIds = Working->JournalActionIds;
    for (const auto &action : journal.Actions) {
      if (!newIds.insert(action.Id).second)
        return SyntheticError::PatchConflict;
      if (const auto *facts = actionFacts(action.Payload)) {
        for (const auto &nested : facts->NestedActionIds) {
          if (!newIds.insert(nested).second)
            return SyntheticError::PatchConflict;
        }
      }
    }
    if (fault == SyntheticFaultPoint::ImmutableSuccessorBuild ||
        fault == SyntheticFaultPoint::ManifestDigest ||
        fault == SyntheticFaultPoint::PreSwap)
      return SyntheticError::InjectedFailure;
    auto successor = std::make_shared<SyntheticSemanticState>(*Working);
    successor->Journals.push_back(
        std::make_shared<const ValidatedJournal>(std::move(journal)));
    successor->JournalActionIds = std::move(newIds);
    Working = std::move(successor);
    ++Epoch;
    return SyntheticError::None;
  }

  SyntheticError addInternalFact(std::string fact) {
    if (auto error = requireOpen(); error != SyntheticError::None)
      return error;
    auto successor = std::make_shared<SyntheticSemanticState>(*Working);
    successor->InternalFacts.push_back(std::move(fact));
    Working = std::move(successor);
    ++Epoch;
    return SyntheticError::None;
  }

  SyntheticError validate() {
    if (auto error = requireOpen(); error != SyntheticError::None)
      return error;
    State = TransactionState::Validated;
    return SyntheticError::None;
  }

  SyntheticError discard() {
    if (!active())
      return SyntheticError::MovedFrom;
    if (State != TransactionState::Open && State != TransactionState::Validated)
      return SyntheticError::LifecycleViolation;
    State = TransactionState::Discarded;
    return SyntheticError::None;
  }

  SyntheticError adopt(SyntheticAnalysisTransaction &&child,
                       SyntheticFaultPoint fault = SyntheticFaultPoint::None) {
    if (auto error = requireOpen(); error != SyntheticError::None)
      return error;
    if (!child.active())
      return SyntheticError::MovedFrom;
    if (child.State != TransactionState::Validated)
      return SyntheticError::LifecycleViolation;
    if (!child.ParentId || *child.ParentId != Id)
      return SyntheticError::WrongParent;
    if (child.BaseEpoch != Epoch)
      return SyntheticError::StaleParent;
    if (fault == SyntheticFaultPoint::AdoptSuccessorBuild ||
        fault == SyntheticFaultPoint::ImmutableSuccessorBuild ||
        fault == SyntheticFaultPoint::ManifestDigest ||
        fault == SyntheticFaultPoint::PreSwap)
      return SyntheticError::InjectedFailure;
    Working = child.Working;
    ++Epoch;
    child.State = TransactionState::Adopted;
    return SyntheticError::None;
  }

  SyntheticError commit(SemanticRevisionId revision,
                        SyntheticFaultPoint fault = SyntheticFaultPoint::None) {
    if (!active())
      return SyntheticError::MovedFrom;
    if (State != TransactionState::Validated)
      return SyntheticError::LifecycleViolation;
    if (ParentId || !Publisher)
      return SyntheticError::LifecycleViolation;
    if (!revision.valid())
      return SyntheticError::InvalidIdentity;
    if (fault == SyntheticFaultPoint::RootSuccessorBuild ||
        fault == SyntheticFaultPoint::ImmutableSuccessorBuild ||
        fault == SyntheticFaultPoint::ManifestDigest ||
        fault == SyntheticFaultPoint::PreSwap)
      return SyntheticError::InjectedFailure;
    auto successor = std::make_shared<const SemanticRevision>(
        SemanticRevision{std::move(revision), *Working});
    if (!Publisher->publish(BaseEpoch, std::move(successor)))
      return SyntheticError::StaleParent;
    State = TransactionState::Committed;
    return SyntheticError::None;
  }

  std::pair<SyntheticError, std::shared_ptr<const RejectedAnalysisResult>>
  reject(SyntheticFaultPoint fault = SyntheticFaultPoint::None) {
    if (auto error = requireOpen(); error != SyntheticError::None)
      return {error, nullptr};
    if (fault == SyntheticFaultPoint::RejectedResultPrebuild ||
        fault == SyntheticFaultPoint::ImmutableSuccessorBuild ||
        fault == SyntheticFaultPoint::PreSwap)
      return {SyntheticError::InjectedFailure, nullptr};
    auto result = std::make_shared<const RejectedAnalysisResult>(
        RejectedAnalysisResult{Working->Diagnostics, Working->InternalFacts});
    State = TransactionState::Discarded;
    return {SyntheticError::None, std::move(result)};
  }

private:
  TransactionId Id;
  std::optional<TransactionId> ParentId;
  uint64_t BaseEpoch = 0;
  uint64_t Epoch = 0;
  TransactionState State = TransactionState::Open;
  std::shared_ptr<const SyntheticSemanticState> Working;
  PublishedSemanticSnapshot *Publisher = nullptr;
  bool HandleValid = true;

  SyntheticAnalysisTransaction(
      TransactionId id, std::optional<TransactionId> parentId,
      uint64_t baseEpoch, std::shared_ptr<const SyntheticSemanticState> working,
      PublishedSemanticSnapshot *publisher)
      : Id(std::move(id)), ParentId(std::move(parentId)), BaseEpoch(baseEpoch),
        Working(std::move(working)), Publisher(publisher) {}

  bool active() const noexcept { return HandleValid; }
  SyntheticError requireOpen() const noexcept {
    if (!active())
      return SyntheticError::MovedFrom;
    return State == TransactionState::Open ? SyntheticError::None
                                           : SyntheticError::LifecycleViolation;
  }

  SyntheticError
  replaceForBranch(std::shared_ptr<const SyntheticSemanticState> successor,
                   uint64_t expectedEpoch) noexcept {
    if (State != TransactionState::Open || Epoch != expectedEpoch)
      return SyntheticError::StaleParent;
    Working.swap(successor);
    ++Epoch;
    return SyntheticError::None;
  }

  friend class BranchFrameSet;
};

enum class BranchSetState : uint8_t {
  TopologyOpen,
  Open,
  Sealed,
  Merged,
  Discarded,
};
enum class BranchFrameState : uint8_t {
  Provisional,
  Registered,
  Sealed,
  Consumed,
  Removed,
  Discarded,
};
enum class BranchReachability : uint8_t {
  Reachable,
  Unreachable,
  UnchangedBase,
};

struct BranchFrame {
  BranchKey Key;
  BranchFrameState State = BranchFrameState::Provisional;
  BranchReachability Reachability = BranchReachability::Reachable;
  SyntheticSemanticState Working;
};

inline uint64_t branchJoinValue(uint64_t lhs, uint64_t rhs) noexcept {
  return lhs | rhs;
}

class BranchFrameSet final {
public:
  static std::optional<BranchFrameSet>
  create(BranchSetId id, SyntheticAnalysisTransaction &parent,
         SyntheticError &error) {
    if (!id.valid() || parent.state() != TransactionState::Open) {
      error = SyntheticError::InvalidIdentity;
      return std::nullopt;
    }
    error = SyntheticError::None;
    return BranchFrameSet(std::move(id), parent);
  }

  BranchSetState state() const noexcept { return State; }
  uint64_t baseEpoch() const noexcept { return BaseEpoch; }

  SyntheticError addFrame(BranchKey key) {
    if (State != BranchSetState::TopologyOpen)
      return SyntheticError::LifecycleViolation;
    if (!key.valid() || Frames.count(key))
      return SyntheticError::InvalidIdentity;
    Frames.emplace(key, BranchFrame{key, BranchFrameState::Provisional,
                                    BranchReachability::Reachable, *Base});
    return SyntheticError::None;
  }

  SyntheticError removeProvisional(const BranchKey &key) {
    if (State != BranchSetState::TopologyOpen)
      return SyntheticError::LifecycleViolation;
    auto found = Frames.find(key);
    if (found == Frames.end() ||
        found->second.State != BranchFrameState::Provisional)
      return SyntheticError::LifecycleViolation;
    found->second.State = BranchFrameState::Removed;
    Frames.erase(found);
    return SyntheticError::None;
  }

  SyntheticError freezeTopology() {
    if (State != BranchSetState::TopologyOpen || Frames.empty())
      return SyntheticError::LifecycleViolation;
    for (auto &[key, frame] : Frames)
      frame.State = BranchFrameState::Registered;
    State = BranchSetState::Open;
    return SyntheticError::None;
  }

  SyntheticError setManifest(const BranchKey &key, StateFamily family,
                             uint64_t value) {
    if (State != BranchSetState::Open)
      return SyntheticError::LifecycleViolation;
    auto found = Frames.find(key);
    if (found == Frames.end() ||
        found->second.State != BranchFrameState::Registered)
      return SyntheticError::LifecycleViolation;
    found->second.Working.Manifest.set(family, value);
    return SyntheticError::None;
  }

  SyntheticError
  stagePatch(const BranchKey &key, const SemanticModelPatch &patch,
             SyntheticFaultPoint fault = SyntheticFaultPoint::None) {
    if (State != BranchSetState::Open)
      return SyntheticError::LifecycleViolation;
    auto found = Frames.find(key);
    if (found == Frames.end() ||
        found->second.State != BranchFrameState::Registered)
      return SyntheticError::LifecycleViolation;
    auto [error, model] =
        SyntheticStateBuilder::apply(found->second.Working.Model, patch, fault);
    if (error != SyntheticError::None)
      return error;
    found->second.Working.Model = std::move(model);
    return SyntheticError::None;
  }

  SyntheticError addDiagnostic(const BranchKey &key, std::string diagnostic) {
    if (State != BranchSetState::Open)
      return SyntheticError::LifecycleViolation;
    auto found = Frames.find(key);
    if (found == Frames.end() ||
        found->second.State != BranchFrameState::Registered)
      return SyntheticError::LifecycleViolation;
    found->second.Working.Diagnostics.push_back(std::move(diagnostic));
    return SyntheticError::None;
  }

  SyntheticError addInternalFact(const BranchKey &key, std::string fact) {
    if (State != BranchSetState::Open)
      return SyntheticError::LifecycleViolation;
    auto found = Frames.find(key);
    if (found == Frames.end() ||
        found->second.State != BranchFrameState::Registered)
      return SyntheticError::LifecycleViolation;
    found->second.Working.InternalFacts.push_back(std::move(fact));
    return SyntheticError::None;
  }

  SyntheticError stageJournal(const BranchKey &key, ValidatedJournal journal) {
    if (State != BranchSetState::Open)
      return SyntheticError::LifecycleViolation;
    auto found = Frames.find(key);
    if (found == Frames.end() ||
        found->second.State != BranchFrameState::Registered)
      return SyntheticError::LifecycleViolation;
    if (validateJournal(journal) != TransferValidationError::None)
      return SyntheticError::PatchConflict;
    std::set<JournalActionId> ids = found->second.Working.JournalActionIds;
    for (const auto &action : journal.Actions) {
      if (!ids.insert(action.Id).second)
        return SyntheticError::PatchConflict;
      if (const auto *facts = actionFacts(action.Payload)) {
        for (const auto &nested : facts->NestedActionIds) {
          if (!ids.insert(nested).second)
            return SyntheticError::PatchConflict;
        }
      }
    }
    found->second.Working.Journals.push_back(
        std::make_shared<const ValidatedJournal>(std::move(journal)));
    found->second.Working.JournalActionIds = std::move(ids);
    return SyntheticError::None;
  }

  SyntheticError sealFrame(const BranchKey &key,
                           BranchReachability reachability) {
    if (State != BranchSetState::Open)
      return SyntheticError::LifecycleViolation;
    auto found = Frames.find(key);
    if (found == Frames.end() ||
        found->second.State != BranchFrameState::Registered)
      return SyntheticError::LifecycleViolation;
    found->second.Reachability = reachability;
    found->second.State = BranchFrameState::Sealed;
    return SyntheticError::None;
  }

  SyntheticError discardRegistered(const BranchKey &key) {
    auto found = Frames.find(key);
    if (State != BranchSetState::Open && State != BranchSetState::Sealed)
      return SyntheticError::LifecycleViolation;
    if (found == Frames.end() ||
        (found->second.State != BranchFrameState::Registered &&
         found->second.State != BranchFrameState::Sealed))
      return SyntheticError::LifecycleViolation;
    discardWholeSet();
    return SyntheticError::None;
  }

  SyntheticError sealSet() {
    if (State != BranchSetState::Open)
      return SyntheticError::LifecycleViolation;
    for (const auto &[key, frame] : Frames) {
      if (frame.State != BranchFrameState::Sealed)
        return SyntheticError::LifecycleViolation;
    }
    State = BranchSetState::Sealed;
    return SyntheticError::None;
  }

  SyntheticError merge(SyntheticAnalysisTransaction &parent,
                       SyntheticFaultPoint fault = SyntheticFaultPoint::None,
                       const std::function<void()> &preSwapHook = {}) {
    if (State != BranchSetState::Sealed)
      return SyntheticError::LifecycleViolation;
    if (parent.id() != ParentId || parent.epoch() != BaseEpoch)
      return SyntheticError::StaleParent;
    if (fault == SyntheticFaultPoint::BranchLatticeJoin ||
        fault == SyntheticFaultPoint::PatchUnion ||
        fault == SyntheticFaultPoint::FullKeyCollisionValidation ||
        fault == SyntheticFaultPoint::CrossReferenceValidation ||
        fault == SyntheticFaultPoint::ImmutableSuccessorBuild ||
        fault == SyntheticFaultPoint::ManifestDigest)
      return SyntheticError::InjectedFailure;

    auto successor = std::make_shared<SyntheticSemanticState>(*Base);
    bool anyReachable = false;
    for (size_t family = 0; family < TransactionalStateManifest::count();
         ++family) {
      uint64_t joined = 0;
      for (const auto &[key, frame] : Frames) {
        if (frame.Reachability == BranchReachability::Unreachable)
          continue;
        anyReachable = true;
        joined = branchJoinValue(joined, frame.Working.Manifest.value(
                                             static_cast<StateFamily>(family)));
      }
      successor->Manifest.set(static_cast<StateFamily>(family), joined);
    }

    std::vector<ModelEntry> entries = Base->Model.entries();
    std::set<std::string> diagnostics(Base->Diagnostics.begin(),
                                      Base->Diagnostics.end());
    std::set<std::string> internalFacts(Base->InternalFacts.begin(),
                                        Base->InternalFacts.end());
    std::set<uint64_t> journalDigests;
    for (const auto &journal : Base->Journals)
      journalDigests.insert(journalDigest(*journal));
    for (const auto &[key, frame] : Frames) {
      diagnostics.insert(frame.Working.Diagnostics.begin(),
                         frame.Working.Diagnostics.end());
      internalFacts.insert(frame.Working.InternalFacts.begin(),
                           frame.Working.InternalFacts.end());
      if (frame.Reachability == BranchReachability::Unreachable)
        continue;
      for (const auto &entry : frame.Working.Model.entries()) {
        auto existing = std::find_if(entries.begin(), entries.end(),
                                     [&](const auto &candidate) {
                                       return candidate.Table == entry.Table &&
                                              candidate.Key == entry.Key;
                                     });
        if (existing == entries.end())
          entries.push_back(entry);
        else if (*existing != entry)
          return SyntheticError::PatchConflict;
      }
      for (const auto &journal : frame.Working.Journals) {
        const auto digest = journalDigest(*journal);
        if (journalDigests.count(digest))
          continue;
        for (const auto &action : journal->Actions) {
          if (!successor->JournalActionIds.insert(action.Id).second)
            return SyntheticError::PatchConflict;
          if (const auto *facts = actionFacts(action.Payload)) {
            for (const auto &nested : facts->NestedActionIds) {
              if (!successor->JournalActionIds.insert(nested).second)
                return SyntheticError::PatchConflict;
            }
          }
        }
        successor->Journals.push_back(journal);
        journalDigests.insert(digest);
      }
    }
    std::sort(entries.begin(), entries.end());
    successor->Model.Entries = std::move(entries);
    successor->Diagnostics.assign(diagnostics.begin(), diagnostics.end());
    successor->InternalFacts.assign(internalFacts.begin(), internalFacts.end());
    if (auto referenceError =
            SyntheticStateBuilder::validateReferences(successor->Model, fault);
        referenceError != SyntheticError::None)
      return referenceError;
    if (!anyReachable)
      successor->Manifest = SyntheticManifestState{};
    if (fault == SyntheticFaultPoint::PreSwap)
      return SyntheticError::InjectedFailure;
    if (preSwapHook)
      preSwapHook();
    if (parent.id() != ParentId || parent.epoch() != BaseEpoch)
      return SyntheticError::StaleParent;
    auto error = parent.replaceForBranch(std::move(successor), BaseEpoch);
    if (error != SyntheticError::None)
      return error;
    State = BranchSetState::Merged;
    for (auto &[key, frame] : Frames)
      frame.State = BranchFrameState::Consumed;
    return SyntheticError::None;
  }

  const std::map<BranchKey, BranchFrame> &frames() const noexcept {
    return Frames;
  }

private:
  BranchSetId Id;
  TransactionId ParentId;
  uint64_t BaseEpoch = 0;
  std::shared_ptr<const SyntheticSemanticState> Base;
  BranchSetState State = BranchSetState::TopologyOpen;
  std::map<BranchKey, BranchFrame> Frames;

  BranchFrameSet(BranchSetId id, SyntheticAnalysisTransaction &parent)
      : Id(std::move(id)), ParentId(parent.id()), BaseEpoch(parent.epoch()),
        Base(parent.working()) {}

  BranchFrameSet(const BranchFrameSet &) = delete;
  BranchFrameSet &operator=(const BranchFrameSet &) = delete;

public:
  BranchFrameSet(BranchFrameSet &&) noexcept = default;
  BranchFrameSet &operator=(BranchFrameSet &&) = delete;

private:
  void discardWholeSet() noexcept {
    State = BranchSetState::Discarded;
    for (auto &[key, frame] : Frames)
      frame.State = BranchFrameState::Discarded;
  }
};

} // namespace toka::synthetic
