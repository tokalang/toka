#pragma once
#include "toka/SourceLocation.h"
#include <cstddef>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <vector>

namespace toka {
class Expr;
class FunctionDecl;
class ShapeDecl;
using EnumPayloadSlot = std::pair<size_t, size_t>;
// Internal source-visible evidence. Not serialized into TKI or public Evidence.
struct EnumResultSource {
  const FunctionDecl *Producer = nullptr;
  const Expr *Edge = nullptr;
  const ShapeDecl *Declaration = nullptr;
  std::set<size_t> Variants;
  std::map<EnumPayloadSlot, std::vector<SourceLocation>> StaticSlots;
};
using EnumResultSourcePtr = std::shared_ptr<const EnumResultSource>;
struct EnumPayloadSelection {
  const FunctionDecl *Function = nullptr;
  const ShapeDecl *Declaration = nullptr;
  size_t Parameter = 0, Variant = 0, Slot = 0;
  EnumResultSourcePtr Source;
  bool operator==(const EnumPayloadSelection &other) const {
    return Function == other.Function && Declaration == other.Declaration &&
        Parameter == other.Parameter && Variant == other.Variant && Slot == other.Slot && Source == other.Source;
  }
  bool operator!=(const EnumPayloadSelection &other) const { return !(*this == other); }
};
struct EnumReturnSourceSummary {
  unsigned ClosureDepth = 0;
  bool Checked = false, Valid = false, SawReturn = false;
  bool CompleteResults = true, CompleteSelection = true;
  std::optional<EnumResultSource> Result;
  std::optional<EnumPayloadSelection> Selection;
};
} // namespace toka
