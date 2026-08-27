// Copyright (c) 2026 YiZhonghua<zhyi@dpai.com>. All rights reserved.

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace toka {

class StructuralIdentityBuilder;

namespace semantic_identity_domain {
struct SemanticNode;
struct Declaration;
struct Substitution;
struct MethodSlot;
struct CallableContract;
struct LangItem;
struct RootSymbol;
struct Field;
struct Type;
struct CallSite;
struct SourceOrigin;
struct Conversion;
struct ArgumentPlan;
struct TransferEdge;
struct Temporary;
struct Cleanup;
struct Loan;
struct Region;
struct InitObligation;
struct OutcomeTransition;
struct ValidatedCall;
struct LoweringRecipe;
struct SemanticModelPatch;
struct SemanticRevision;
struct BranchSet;
struct BranchKey;
struct JournalAction;
struct Transaction;
struct StructuralFork;
} // namespace semantic_identity_domain

// M1b.0a identity values contain canonical structural keys. They allocate no
// IDs and expose no counter; later builders are responsible for constructing
// the canonical keys described by the M1b-D.1 design.
template <typename Domain> class SemanticIdentity {
public:
  SemanticIdentity() = default;

  bool valid() const noexcept { return !CanonicalKey.empty(); }
  const std::string &canonicalKey() const noexcept { return CanonicalKey; }

  size_t hashValue() const noexcept {
    return std::hash<std::string>{}(CanonicalKey);
  }

  friend bool operator==(const SemanticIdentity &lhs,
                         const SemanticIdentity &rhs) noexcept {
    return lhs.CanonicalKey == rhs.CanonicalKey;
  }

  friend bool operator!=(const SemanticIdentity &lhs,
                         const SemanticIdentity &rhs) noexcept {
    return !(lhs == rhs);
  }

  friend bool operator<(const SemanticIdentity &lhs,
                        const SemanticIdentity &rhs) noexcept {
    return lhs.CanonicalKey < rhs.CanonicalKey;
  }

private:
  friend class StructuralIdentityBuilder;

  std::string CanonicalKey;

  explicit SemanticIdentity(std::string canonicalKey)
      : CanonicalKey(std::move(canonicalKey)) {}
};

using SemanticNodeId = SemanticIdentity<semantic_identity_domain::SemanticNode>;
using DeclarationId = SemanticIdentity<semantic_identity_domain::Declaration>;
using SubstitutionId = SemanticIdentity<semantic_identity_domain::Substitution>;
using MethodSlotId = SemanticIdentity<semantic_identity_domain::MethodSlot>;
using CallableContractId =
    SemanticIdentity<semantic_identity_domain::CallableContract>;
using LangItemId = SemanticIdentity<semantic_identity_domain::LangItem>;
using RootSymbolId = SemanticIdentity<semantic_identity_domain::RootSymbol>;
using FieldId = SemanticIdentity<semantic_identity_domain::Field>;
using TypeId = SemanticIdentity<semantic_identity_domain::Type>;
using CallSiteId = SemanticIdentity<semantic_identity_domain::CallSite>;
using SourceOriginId = SemanticIdentity<semantic_identity_domain::SourceOrigin>;
using ConversionId = SemanticIdentity<semantic_identity_domain::Conversion>;
using ArgumentPlanId = SemanticIdentity<semantic_identity_domain::ArgumentPlan>;
using TransferEdgeId = SemanticIdentity<semantic_identity_domain::TransferEdge>;
using TemporaryId = SemanticIdentity<semantic_identity_domain::Temporary>;
using CleanupId = SemanticIdentity<semantic_identity_domain::Cleanup>;
using LoanId = SemanticIdentity<semantic_identity_domain::Loan>;
using RegionId = SemanticIdentity<semantic_identity_domain::Region>;
using InitObligationId =
    SemanticIdentity<semantic_identity_domain::InitObligation>;
using OutcomeTransitionId =
    SemanticIdentity<semantic_identity_domain::OutcomeTransition>;
using ValidatedCallId =
    SemanticIdentity<semantic_identity_domain::ValidatedCall>;
using LoweringRecipeId =
    SemanticIdentity<semantic_identity_domain::LoweringRecipe>;
using SemanticModelPatchId =
    SemanticIdentity<semantic_identity_domain::SemanticModelPatch>;
using SemanticRevisionId =
    SemanticIdentity<semantic_identity_domain::SemanticRevision>;
using BranchSetId = SemanticIdentity<semantic_identity_domain::BranchSet>;
using BranchKey = SemanticIdentity<semantic_identity_domain::BranchKey>;
using JournalActionId =
    SemanticIdentity<semantic_identity_domain::JournalAction>;
using TransactionId = SemanticIdentity<semantic_identity_domain::Transaction>;
using StructuralForkKey =
    SemanticIdentity<semantic_identity_domain::StructuralFork>;

enum class PlaceProjectionKind : uint8_t {
  Field,
  ConstantIndex,
  DynamicIndex,
  Dereference,
  Unknown,
};

class PlaceProjection {
public:
  static PlaceProjection field(FieldId field) {
    PlaceProjection projection(PlaceProjectionKind::Field);
    projection.Field = std::move(field);
    return projection;
  }

  static PlaceProjection constantIndex(uint64_t index) {
    PlaceProjection projection(PlaceProjectionKind::ConstantIndex);
    projection.Index = index;
    return projection;
  }

  static PlaceProjection dynamicIndex(SemanticNodeId expression) {
    PlaceProjection projection(PlaceProjectionKind::DynamicIndex);
    projection.Expression = std::move(expression);
    return projection;
  }

  static PlaceProjection dereference() {
    return PlaceProjection(PlaceProjectionKind::Dereference);
  }

  static PlaceProjection unknown() {
    return PlaceProjection(PlaceProjectionKind::Unknown);
  }

  PlaceProjectionKind kind() const noexcept { return Kind; }
  bool valid() const noexcept {
    switch (Kind) {
    case PlaceProjectionKind::Field:
      return Field.valid();
    case PlaceProjectionKind::DynamicIndex:
      return Expression.valid();
    case PlaceProjectionKind::ConstantIndex:
    case PlaceProjectionKind::Dereference:
    case PlaceProjectionKind::Unknown:
      return true;
    }
    return false;
  }
  const FieldId &fieldId() const noexcept { return Field; }
  uint64_t constantIndexValue() const noexcept { return Index; }
  const SemanticNodeId &dynamicIndexExpression() const noexcept {
    return Expression;
  }

  std::string canonicalKey() const {
    std::string result = "toka.place-projection.v1;";
    append(result, std::to_string(static_cast<unsigned>(Kind)));
    switch (Kind) {
    case PlaceProjectionKind::Field:
      append(result, Field.canonicalKey());
      break;
    case PlaceProjectionKind::ConstantIndex:
      append(result, std::to_string(Index));
      break;
    case PlaceProjectionKind::DynamicIndex:
      append(result, Expression.canonicalKey());
      break;
    case PlaceProjectionKind::Dereference:
    case PlaceProjectionKind::Unknown:
      break;
    }
    return result;
  }

  size_t hashValue() const noexcept {
    size_t seed = static_cast<size_t>(Kind);
    seed = hashCombine(seed, Field.hashValue());
    seed = hashCombine(seed, std::hash<uint64_t>{}(Index));
    return hashCombine(seed, Expression.hashValue());
  }

  friend bool operator==(const PlaceProjection &lhs,
                         const PlaceProjection &rhs) noexcept {
    return std::tie(lhs.Kind, lhs.Field, lhs.Index, lhs.Expression) ==
           std::tie(rhs.Kind, rhs.Field, rhs.Index, rhs.Expression);
  }

  friend bool operator!=(const PlaceProjection &lhs,
                         const PlaceProjection &rhs) noexcept {
    return !(lhs == rhs);
  }

  friend bool operator<(const PlaceProjection &lhs,
                        const PlaceProjection &rhs) noexcept {
    return std::tie(lhs.Kind, lhs.Field, lhs.Index, lhs.Expression) <
           std::tie(rhs.Kind, rhs.Field, rhs.Index, rhs.Expression);
  }

private:
  PlaceProjectionKind Kind;
  FieldId Field;
  uint64_t Index = 0;
  SemanticNodeId Expression;

  explicit PlaceProjection(PlaceProjectionKind kind) : Kind(kind) {}

  static void append(std::string &result, const std::string &component) {
    result += std::to_string(component.size());
    result += ':';
    result += component;
    result += ';';
  }

  static size_t hashCombine(size_t seed, size_t value) noexcept {
    return seed ^ (value + 0x9e3779b9U + (seed << 6U) + (seed >> 2U));
  }
};

class PlaceId {
public:
  PlaceId() = default;
  explicit PlaceId(RootSymbolId root,
                   std::vector<PlaceProjection> projections = {})
      : Root(std::move(root)), Projections(std::move(projections)) {}

  bool valid() const noexcept {
    if (!Root.valid())
      return false;
    for (const auto &projection : Projections) {
      if (!projection.valid())
        return false;
    }
    return true;
  }
  const RootSymbolId &root() const noexcept { return Root; }
  const std::vector<PlaceProjection> &projections() const noexcept {
    return Projections;
  }

  std::string canonicalKey() const {
    std::string result = "toka.place.v1;";
    append(result, Root.canonicalKey());
    append(result, std::to_string(Projections.size()));
    for (const auto &projection : Projections)
      append(result, projection.canonicalKey());
    return result;
  }

  size_t hashValue() const noexcept {
    size_t seed = Root.hashValue();
    for (const auto &projection : Projections)
      seed = hashCombine(seed, projection.hashValue());
    return seed;
  }

  friend bool operator==(const PlaceId &lhs, const PlaceId &rhs) noexcept {
    return lhs.Root == rhs.Root && lhs.Projections == rhs.Projections;
  }

  friend bool operator!=(const PlaceId &lhs, const PlaceId &rhs) noexcept {
    return !(lhs == rhs);
  }

  friend bool operator<(const PlaceId &lhs, const PlaceId &rhs) noexcept {
    return std::tie(lhs.Root, lhs.Projections) <
           std::tie(rhs.Root, rhs.Projections);
  }

private:
  RootSymbolId Root;
  std::vector<PlaceProjection> Projections;

  static void append(std::string &result, const std::string &component) {
    result += std::to_string(component.size());
    result += ':';
    result += component;
    result += ';';
  }

  static size_t hashCombine(size_t seed, size_t value) noexcept {
    return seed ^ (value + 0x9e3779b9U + (seed << 6U) + (seed >> 2U));
  }
};

struct DirectDeclarationCallee {
  DeclarationId Declaration;

  size_t hashValue() const noexcept { return Declaration.hashValue(); }
  friend bool operator==(const DirectDeclarationCallee &lhs,
                         const DirectDeclarationCallee &rhs) noexcept {
    return lhs.Declaration == rhs.Declaration;
  }
  friend bool operator<(const DirectDeclarationCallee &lhs,
                        const DirectDeclarationCallee &rhs) noexcept {
    return lhs.Declaration < rhs.Declaration;
  }
};

struct GenericInstanceCallee {
  DeclarationId TemplateDeclaration;
  SubstitutionId Substitution;

  size_t hashValue() const noexcept {
    return TemplateDeclaration.hashValue() ^
           (Substitution.hashValue() + 0x9e3779b9U);
  }
  friend bool operator==(const GenericInstanceCallee &lhs,
                         const GenericInstanceCallee &rhs) noexcept {
    return std::tie(lhs.TemplateDeclaration, lhs.Substitution) ==
           std::tie(rhs.TemplateDeclaration, rhs.Substitution);
  }
  friend bool operator<(const GenericInstanceCallee &lhs,
                        const GenericInstanceCallee &rhs) noexcept {
    return std::tie(lhs.TemplateDeclaration, lhs.Substitution) <
           std::tie(rhs.TemplateDeclaration, rhs.Substitution);
  }
};

struct TraitSlotCallee {
  DeclarationId TraitDeclaration;
  MethodSlotId MethodSlot;
  std::optional<SubstitutionId> Substitution;

  size_t hashValue() const noexcept {
    size_t seed =
        TraitDeclaration.hashValue() ^ (MethodSlot.hashValue() + 0x9e3779b9U);
    if (Substitution)
      seed ^= Substitution->hashValue() + 0x85ebca6bU;
    return seed;
  }
  friend bool operator==(const TraitSlotCallee &lhs,
                         const TraitSlotCallee &rhs) noexcept {
    return std::tie(lhs.TraitDeclaration, lhs.MethodSlot, lhs.Substitution) ==
           std::tie(rhs.TraitDeclaration, rhs.MethodSlot, rhs.Substitution);
  }
  friend bool operator<(const TraitSlotCallee &lhs,
                        const TraitSlotCallee &rhs) noexcept {
    return std::tie(lhs.TraitDeclaration, lhs.MethodSlot, lhs.Substitution) <
           std::tie(rhs.TraitDeclaration, rhs.MethodSlot, rhs.Substitution);
  }
};

struct IndirectFunctionCallee {
  CallableContractId Contract;

  size_t hashValue() const noexcept { return Contract.hashValue(); }
  friend bool operator==(const IndirectFunctionCallee &lhs,
                         const IndirectFunctionCallee &rhs) noexcept {
    return lhs.Contract == rhs.Contract;
  }
  friend bool operator<(const IndirectFunctionCallee &lhs,
                        const IndirectFunctionCallee &rhs) noexcept {
    return lhs.Contract < rhs.Contract;
  }
};

struct IndirectDynFunctionCallee {
  CallableContractId Contract;

  size_t hashValue() const noexcept { return Contract.hashValue(); }
  friend bool operator==(const IndirectDynFunctionCallee &lhs,
                         const IndirectDynFunctionCallee &rhs) noexcept {
    return lhs.Contract == rhs.Contract;
  }
  friend bool operator<(const IndirectDynFunctionCallee &lhs,
                        const IndirectDynFunctionCallee &rhs) noexcept {
    return lhs.Contract < rhs.Contract;
  }
};

struct ExternDeclarationCallee {
  DeclarationId Declaration;

  size_t hashValue() const noexcept { return Declaration.hashValue(); }
  friend bool operator==(const ExternDeclarationCallee &lhs,
                         const ExternDeclarationCallee &rhs) noexcept {
    return lhs.Declaration == rhs.Declaration;
  }
  friend bool operator<(const ExternDeclarationCallee &lhs,
                        const ExternDeclarationCallee &rhs) noexcept {
    return lhs.Declaration < rhs.Declaration;
  }
};

struct LangItemCallee {
  LangItemId LangItem;
  DeclarationId Declaration;

  size_t hashValue() const noexcept {
    return LangItem.hashValue() ^ (Declaration.hashValue() + 0x9e3779b9U);
  }
  friend bool operator==(const LangItemCallee &lhs,
                         const LangItemCallee &rhs) noexcept {
    return std::tie(lhs.LangItem, lhs.Declaration) ==
           std::tie(rhs.LangItem, rhs.Declaration);
  }
  friend bool operator<(const LangItemCallee &lhs,
                        const LangItemCallee &rhs) noexcept {
    return std::tie(lhs.LangItem, lhs.Declaration) <
           std::tie(rhs.LangItem, rhs.Declaration);
  }
};

enum class ResolvedCalleeKind : uint8_t {
  Invalid,
  DirectDeclaration,
  GenericInstance,
  TraitSlot,
  IndirectFunction,
  IndirectDynFunction,
  ExternDeclaration,
  LangItem,
};

enum class SemanticRegionKind : uint8_t {
  Lexical,
  CallEvaluation,
  TemporaryExtension,
  ExecutionBoundary,
};

enum class SemanticJournalPhase : uint8_t {
  Evaluation,
  Boundary,
  Finalization,
};

class ResolvedCalleeId {
public:
  ResolvedCalleeId() = default;

  static ResolvedCalleeId direct(DeclarationId declaration) {
    return ResolvedCalleeId(DirectDeclarationCallee{std::move(declaration)});
  }

  static ResolvedCalleeId genericInstance(DeclarationId declaration,
                                          SubstitutionId substitution) {
    return ResolvedCalleeId(
        GenericInstanceCallee{std::move(declaration), std::move(substitution)});
  }

  static ResolvedCalleeId
  traitSlot(DeclarationId trait, MethodSlotId slot,
            std::optional<SubstitutionId> substitution = std::nullopt) {
    return ResolvedCalleeId(TraitSlotCallee{std::move(trait), std::move(slot),
                                            std::move(substitution)});
  }

  static ResolvedCalleeId indirectFunction(CallableContractId contract) {
    return ResolvedCalleeId(IndirectFunctionCallee{std::move(contract)});
  }

  static ResolvedCalleeId indirectDynFunction(CallableContractId contract) {
    return ResolvedCalleeId(IndirectDynFunctionCallee{std::move(contract)});
  }

  static ResolvedCalleeId externDeclaration(DeclarationId declaration) {
    return ResolvedCalleeId(ExternDeclarationCallee{std::move(declaration)});
  }

  static ResolvedCalleeId langItem(LangItemId langItem,
                                   DeclarationId declaration) {
    return ResolvedCalleeId(
        LangItemCallee{std::move(langItem), std::move(declaration)});
  }

  bool valid() const noexcept {
    return std::visit(
        [](const auto &value) {
          using Value = std::decay_t<decltype(value)>;
          if constexpr (std::is_same_v<Value, std::monostate>) {
            return false;
          } else if constexpr (std::is_same_v<Value, DirectDeclarationCallee> ||
                               std::is_same_v<Value, ExternDeclarationCallee>) {
            return value.Declaration.valid();
          } else if constexpr (std::is_same_v<Value, GenericInstanceCallee>) {
            return value.TemplateDeclaration.valid() &&
                   value.Substitution.valid();
          } else if constexpr (std::is_same_v<Value, TraitSlotCallee>) {
            return value.TraitDeclaration.valid() && value.MethodSlot.valid() &&
                   (!value.Substitution || value.Substitution->valid());
          } else if constexpr (std::is_same_v<Value, IndirectFunctionCallee> ||
                               std::is_same_v<Value,
                                              IndirectDynFunctionCallee>) {
            return value.Contract.valid();
          } else {
            return value.LangItem.valid() && value.Declaration.valid();
          }
        },
        StorageValue);
  }

  ResolvedCalleeKind kind() const noexcept {
    return static_cast<ResolvedCalleeKind>(StorageValue.index());
  }

  std::string canonicalKey() const {
    std::string result = "toka.resolved-callee.v1;";
    append(result, std::to_string(StorageValue.index()));
    std::visit(
        [&result](const auto &value) {
          using Value = std::decay_t<decltype(value)>;
          if constexpr (std::is_same_v<Value, DirectDeclarationCallee> ||
                        std::is_same_v<Value, ExternDeclarationCallee>) {
            append(result, value.Declaration.canonicalKey());
          } else if constexpr (std::is_same_v<Value, GenericInstanceCallee>) {
            append(result, value.TemplateDeclaration.canonicalKey());
            append(result, value.Substitution.canonicalKey());
          } else if constexpr (std::is_same_v<Value, TraitSlotCallee>) {
            append(result, value.TraitDeclaration.canonicalKey());
            append(result, value.MethodSlot.canonicalKey());
            append(result, value.Substitution
                               ? value.Substitution->canonicalKey()
                               : std::string());
          } else if constexpr (std::is_same_v<Value, IndirectFunctionCallee> ||
                               std::is_same_v<Value,
                                              IndirectDynFunctionCallee>) {
            append(result, value.Contract.canonicalKey());
          } else if constexpr (std::is_same_v<Value, LangItemCallee>) {
            append(result, value.LangItem.canonicalKey());
            append(result, value.Declaration.canonicalKey());
          }
        },
        StorageValue);
    return result;
  }

  size_t hashValue() const noexcept {
    size_t seed = StorageValue.index();
    return std::visit(
        [seed](const auto &value) {
          using Value = std::decay_t<decltype(value)>;
          if constexpr (std::is_same_v<Value, std::monostate>)
            return seed;
          else
            return seed ^ (value.hashValue() + 0x9e3779b9U);
        },
        StorageValue);
  }

  friend bool operator==(const ResolvedCalleeId &lhs,
                         const ResolvedCalleeId &rhs) noexcept {
    return lhs.StorageValue == rhs.StorageValue;
  }

  friend bool operator!=(const ResolvedCalleeId &lhs,
                         const ResolvedCalleeId &rhs) noexcept {
    return !(lhs == rhs);
  }

  friend bool operator<(const ResolvedCalleeId &lhs,
                        const ResolvedCalleeId &rhs) noexcept {
    return lhs.StorageValue < rhs.StorageValue;
  }

private:
  using Storage =
      std::variant<std::monostate, DirectDeclarationCallee,
                   GenericInstanceCallee, TraitSlotCallee,
                   IndirectFunctionCallee, IndirectDynFunctionCallee,
                   ExternDeclarationCallee, LangItemCallee>;

  Storage StorageValue;

  static void append(std::string &result, const std::string &component) {
    result += std::to_string(component.size());
    result += ':';
    result += component;
    result += ';';
  }

  template <typename Value>
  explicit ResolvedCalleeId(Value value) : StorageValue(std::move(value)) {}
};

enum class DestinationKind : uint8_t {
  Invalid,
  FormalSlot,
  ReturnSlot,
  LocalInitSlot,
  AggregateFieldSlot,
  CaptureSlot,
  TemporarySlot,
};

class DestinationId {
public:
  DestinationId() = default;

  static DestinationId formalSlot(CallSiteId call, uint64_t formalIndex) {
    DestinationId result(DestinationKind::FormalSlot);
    result.Call = std::move(call);
    result.Ordinal = formalIndex;
    return result;
  }

  static DestinationId returnSlot(ResolvedCalleeId callee) {
    DestinationId result(DestinationKind::ReturnSlot);
    result.Callee = std::move(callee);
    return result;
  }

  static DestinationId localInitSlot(RootSymbolId root) {
    DestinationId result(DestinationKind::LocalInitSlot);
    result.Root = std::move(root);
    return result;
  }

  static DestinationId aggregateFieldSlot(SemanticNodeId aggregate,
                                          FieldId field) {
    DestinationId result(DestinationKind::AggregateFieldSlot);
    result.Node = std::move(aggregate);
    result.Field = std::move(field);
    return result;
  }

  static DestinationId captureSlot(SemanticNodeId closure,
                                   uint64_t captureOrdinal) {
    DestinationId result(DestinationKind::CaptureSlot);
    result.Node = std::move(closure);
    result.Ordinal = captureOrdinal;
    return result;
  }

  static DestinationId temporarySlot(TemporaryId temporary) {
    DestinationId result(DestinationKind::TemporarySlot);
    result.Temporary = std::move(temporary);
    return result;
  }

  DestinationKind kind() const noexcept { return Kind; }

  bool valid() const noexcept {
    switch (Kind) {
    case DestinationKind::FormalSlot:
      return Call.valid() && Ordinal != 0;
    case DestinationKind::ReturnSlot:
      return Callee.valid();
    case DestinationKind::LocalInitSlot:
      return Root.valid();
    case DestinationKind::AggregateFieldSlot:
      return Node.valid() && Field.valid();
    case DestinationKind::CaptureSlot:
      return Node.valid();
    case DestinationKind::TemporarySlot:
      return Temporary.valid();
    case DestinationKind::Invalid:
      return false;
    }
    return false;
  }

  std::string canonicalKey() const {
    std::string result = "toka.destination.v1;";
    append(result, std::to_string(static_cast<unsigned>(Kind)));
    append(result, Call.canonicalKey());
    append(result, std::to_string(Ordinal));
    append(result, Root.canonicalKey());
    append(result, Node.canonicalKey());
    append(result, Field.canonicalKey());
    append(result, Temporary.canonicalKey());
    append(result, Callee.canonicalKey());
    return result;
  }

  size_t hashValue() const noexcept {
    size_t seed = static_cast<size_t>(Kind);
    seed = hashCombine(seed, Call.hashValue());
    seed = hashCombine(seed, std::hash<uint64_t>{}(Ordinal));
    seed = hashCombine(seed, Root.hashValue());
    seed = hashCombine(seed, Node.hashValue());
    seed = hashCombine(seed, Field.hashValue());
    seed = hashCombine(seed, Temporary.hashValue());
    return hashCombine(seed, Callee.hashValue());
  }

  friend bool operator==(const DestinationId &lhs,
                         const DestinationId &rhs) noexcept {
    return std::tie(lhs.Kind, lhs.Call, lhs.Ordinal, lhs.Root, lhs.Node,
                    lhs.Field, lhs.Temporary, lhs.Callee) ==
           std::tie(rhs.Kind, rhs.Call, rhs.Ordinal, rhs.Root, rhs.Node,
                    rhs.Field, rhs.Temporary, rhs.Callee);
  }

  friend bool operator!=(const DestinationId &lhs,
                         const DestinationId &rhs) noexcept {
    return !(lhs == rhs);
  }

  friend bool operator<(const DestinationId &lhs,
                        const DestinationId &rhs) noexcept {
    return std::tie(lhs.Kind, lhs.Call, lhs.Ordinal, lhs.Root, lhs.Node,
                    lhs.Field, lhs.Temporary, lhs.Callee) <
           std::tie(rhs.Kind, rhs.Call, rhs.Ordinal, rhs.Root, rhs.Node,
                    rhs.Field, rhs.Temporary, rhs.Callee);
  }

private:
  DestinationKind Kind = DestinationKind::Invalid;
  CallSiteId Call;
  uint64_t Ordinal = 0;
  RootSymbolId Root;
  SemanticNodeId Node;
  FieldId Field;
  TemporaryId Temporary;
  ResolvedCalleeId Callee;

  explicit DestinationId(DestinationKind kind) : Kind(kind) {}

  static void append(std::string &result, const std::string &component) {
    result += std::to_string(component.size());
    result += ':';
    result += component;
    result += ';';
  }

  static size_t hashCombine(size_t seed, size_t value) noexcept {
    return seed ^ (value + 0x9e3779b9U + (seed << 6U) + (seed >> 2U));
  }
};

class StructuralIdentityBuilder final {
public:
  static SourceOriginId sourceOrigin(std::string unit, std::string path,
                                     uint64_t ordinal) {
    return make<semantic_identity_domain::SourceOrigin>(
        "source-origin", {std::move(unit), std::move(path), number(ordinal)});
  }

  static SemanticNodeId semanticNode(const SourceOriginId &origin,
                                     uint64_t expansionPath, uint64_t ordinal) {
    return make<semantic_identity_domain::SemanticNode>(
        "semantic-node", {key(origin), number(expansionPath), number(ordinal)});
  }

  static DeclarationId declaration(std::string crate, std::string module,
                                   std::string kind, std::string localName,
                                   uint64_t ordinal = 0) {
    return make<semantic_identity_domain::Declaration>(
        "declaration", {std::move(crate), std::move(module), std::move(kind),
                        std::move(localName), number(ordinal)});
  }

  static SubstitutionId substitution(std::string canonicalWitness) {
    return make<semantic_identity_domain::Substitution>(
        "substitution", {std::move(canonicalWitness)});
  }

  static MethodSlotId methodSlot(const DeclarationId &owner, uint64_t ordinal) {
    return make<semantic_identity_domain::MethodSlot>(
        "method-slot", {key(owner), number(ordinal)});
  }

  static CallableContractId callableContract(std::string canonicalContract) {
    return make<semantic_identity_domain::CallableContract>(
        "callable-contract", {std::move(canonicalContract)});
  }

  static LangItemId langItem(std::string canonicalName) {
    return make<semantic_identity_domain::LangItem>("lang-item",
                                                    {std::move(canonicalName)});
  }

  static RootSymbolId rootSymbol(const SemanticNodeId &scope,
                                 std::string binding, uint64_t ordinal) {
    return make<semantic_identity_domain::RootSymbol>(
        "root-symbol", {key(scope), std::move(binding), number(ordinal)});
  }

  static FieldId field(const DeclarationId &owner, std::string name,
                       uint64_t ordinal) {
    return make<semantic_identity_domain::Field>(
        "field", {key(owner), std::move(name), number(ordinal)});
  }

  static TypeId type(std::string canonicalTypeWitness) {
    return make<semantic_identity_domain::Type>(
        "type", {std::move(canonicalTypeWitness)});
  }

  static CallSiteId callSite(const SemanticNodeId &node) {
    return make<semantic_identity_domain::CallSite>("call-site", {key(node)});
  }

  static ConversionId conversion(const SemanticNodeId &node,
                                 const TypeId &destination, uint64_t ordinal) {
    return make<semantic_identity_domain::Conversion>(
        "conversion", {key(node), key(destination), number(ordinal)});
  }

  static ArgumentPlanId argumentPlan(const CallSiteId &call,
                                     uint64_t argumentIndex,
                                     uint64_t formalIndex) {
    return make<semantic_identity_domain::ArgumentPlan>(
        "argument-plan",
        {key(call), number(argumentIndex), number(formalIndex)});
  }

  static TransferEdgeId transferEdge(const ArgumentPlanId &plan,
                                     uint64_t ordinal) {
    return make<semantic_identity_domain::TransferEdge>(
        "transfer-edge", {key(plan), number(ordinal)});
  }

  static TemporaryId temporary(const SemanticNodeId &producer, uint64_t role,
                               uint64_t ordinal) {
    return make<semantic_identity_domain::Temporary>(
        "temporary", {key(producer), number(role), number(ordinal)});
  }

  static CleanupId cleanup(const TemporaryId &owner, uint64_t role,
                           uint64_t ordinal) {
    return make<semantic_identity_domain::Cleanup>(
        "cleanup", {key(owner), number(role), number(ordinal)});
  }

  static LoanId loan(const SemanticNodeId &origin, const PlaceId &source,
                     const PlaceId &referent, uint64_t ordinal) {
    return make<semantic_identity_domain::Loan>(
        "loan", {key(origin), source.canonicalKey(), referent.canonicalKey(),
                 number(ordinal)});
  }

  static RegionId region(const SemanticNodeId &owner, SemanticRegionKind kind,
                         uint64_t depth, uint64_t ordinal) {
    return make<semantic_identity_domain::Region>(
        "region", {key(owner), number(static_cast<uint64_t>(kind)),
                   number(depth), number(ordinal)});
  }

  static InitObligationId initObligation(const CallSiteId &call,
                                         uint64_t formalIndex) {
    return make<semantic_identity_domain::InitObligation>(
        "init-obligation", {key(call), number(formalIndex)});
  }

  static OutcomeTransitionId outcomeTransition(const DeclarationId &owner,
                                               uint64_t ordinal) {
    return make<semantic_identity_domain::OutcomeTransition>(
        "outcome-transition", {key(owner), number(ordinal)});
  }

  static ValidatedCallId validatedCall(const CallSiteId &call) {
    return make<semantic_identity_domain::ValidatedCall>("validated-call",
                                                         {key(call)});
  }

  static LoweringRecipeId loweringRecipe(const CallSiteId &call,
                                         uint64_t ordinal) {
    return make<semantic_identity_domain::LoweringRecipe>(
        "lowering-recipe", {key(call), number(ordinal)});
  }

  static SemanticModelPatchId modelPatch(const CallSiteId &call,
                                         uint64_t ordinal) {
    return make<semantic_identity_domain::SemanticModelPatch>(
        "semantic-model-patch", {key(call), number(ordinal)});
  }

  static SemanticRevisionId revision(const SourceOriginId &root,
                                     uint64_t ordinal) {
    return make<semantic_identity_domain::SemanticRevision>(
        "semantic-revision", {key(root), number(ordinal)});
  }

  static BranchSetId branchSet(const SemanticNodeId &owner, uint64_t ordinal) {
    return make<semantic_identity_domain::BranchSet>(
        "branch-set", {key(owner), number(ordinal)});
  }

  static BranchKey branchKey(const BranchSetId &set, uint64_t ordinal) {
    return make<semantic_identity_domain::BranchKey>(
        "branch-key", {key(set), number(ordinal)});
  }

  static JournalActionId journalAction(const SemanticNodeId &owner,
                                       SemanticJournalPhase phase,
                                       uint64_t ordinal) {
    return make<semantic_identity_domain::JournalAction>(
        "journal-action",
        {key(owner), number(static_cast<uint64_t>(phase)), number(ordinal)});
  }

  static TransactionId transaction(const SemanticNodeId &owner,
                                   uint64_t ordinal) {
    return make<semantic_identity_domain::Transaction>(
        "transaction", {key(owner), number(ordinal)});
  }

  static TransactionId childTransaction(const TransactionId &parent,
                                        const StructuralForkKey &fork) {
    return make<semantic_identity_domain::Transaction>(
        "child-transaction", {key(parent), key(fork)});
  }

  static StructuralForkKey structuralFork(const SemanticNodeId &owner,
                                          uint64_t role, uint64_t ordinal) {
    return make<semantic_identity_domain::StructuralFork>(
        "structural-fork", {key(owner), number(role), number(ordinal)});
  }

private:
  template <typename Domain>
  static SemanticIdentity<Domain> make(const std::string &domain,
                                       std::vector<std::string> components) {
    std::string result = "toka.semantic-identity.v1;";
    append(result, domain);
    for (const auto &component : components)
      append(result, component);
    return SemanticIdentity<Domain>(std::move(result));
  }

  template <typename Domain>
  static const std::string &key(const SemanticIdentity<Domain> &identity) {
    return identity.canonicalKey();
  }

  static std::string number(uint64_t value) { return std::to_string(value); }

  static void append(std::string &result, const std::string &component) {
    result += std::to_string(component.size());
    result += ':';
    result += component;
    result += ';';
  }
};

// M1b.0a intentionally has no tables or mutation API. Later slices replace
// this shell only after their separately reviewed transaction gates pass.
class SemanticModel final {
public:
  constexpr bool empty() const noexcept { return true; }
  constexpr size_t size() const noexcept { return 0; }
};

} // namespace toka

namespace std {

template <typename Domain> struct hash<toka::SemanticIdentity<Domain>> {
  size_t
  operator()(const toka::SemanticIdentity<Domain> &value) const noexcept {
    return value.hashValue();
  }
};

template <> struct hash<toka::PlaceProjection> {
  size_t operator()(const toka::PlaceProjection &value) const noexcept {
    return value.hashValue();
  }
};

template <> struct hash<toka::PlaceId> {
  size_t operator()(const toka::PlaceId &value) const noexcept {
    return value.hashValue();
  }
};

template <> struct hash<toka::ResolvedCalleeId> {
  size_t operator()(const toka::ResolvedCalleeId &value) const noexcept {
    return value.hashValue();
  }
};

template <> struct hash<toka::DestinationId> {
  size_t operator()(const toka::DestinationId &value) const noexcept {
    return value.hashValue();
  }
};

} // namespace std
