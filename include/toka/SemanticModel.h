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

namespace semantic_identity_domain {
struct SemanticNode;
struct Declaration;
struct Substitution;
struct MethodSlot;
struct CallableContract;
struct LangItem;
struct RootSymbol;
struct Field;
} // namespace semantic_identity_domain

// M1b.0a identity values contain canonical structural keys. They allocate no
// IDs and expose no counter; later builders are responsible for constructing
// the canonical keys described by the M1b-D.1 design.
template <typename Domain> class SemanticIdentity {
public:
  SemanticIdentity() = default;

  static SemanticIdentity fromCanonicalKey(std::string canonicalKey) {
    return SemanticIdentity(std::move(canonicalKey));
  }

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

  template <typename Value>
  explicit ResolvedCalleeId(Value value) : StorageValue(std::move(value)) {}
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

} // namespace std
