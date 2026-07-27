// Copyright (c) 2025 YiZhonghua<zhyi@dpai.com>. All rights reserved.
#include "toka/AccessPath.h"
#include <algorithm>
#include <sstream>
#include <tuple>

namespace toka {

bool AccessProjection::operator==(const AccessProjection &rhs) const {
  return Kind == rhs.Kind && Name == rhs.Name && Index == rhs.Index;
}

bool AccessProjection::operator<(const AccessProjection &rhs) const {
  return std::tie(Kind, Name, Index) < std::tie(rhs.Kind, rhs.Name, rhs.Index);
}

std::string AccessPath::toLegacyString() const {
  std::string result = RootName;
  for (const auto &projection : Projections) {
    if (projection.Kind == AccessProjectionKind::Field) {
      result += "." + projection.Name;
    }
  }
  return result;
}

std::string AccessPath::toDebugString() const {
  std::ostringstream out;
  out << RootName;
  for (const auto &projection : Projections) {
    switch (projection.Kind) {
    case AccessProjectionKind::Field:
      out << "." << projection.Name;
      break;
    case AccessProjectionKind::ConstantIndex:
      out << "[" << projection.Index << "]";
      break;
    case AccessProjectionKind::DynamicIndex:
      out << "[?]";
      break;
    case AccessProjectionKind::Dereference:
      out << "->*";
      break;
    case AccessProjectionKind::Unknown:
      out << "<?>";
      break;
    }
  }
  return out.str();
}

bool AccessPath::operator==(const AccessPath &rhs) const {
  if (RootID != rhs.RootID)
    return false;
  if (RootID == 0 && RootName != rhs.RootName)
    return false;
  return Projections == rhs.Projections;
}

bool AccessPath::operator<(const AccessPath &rhs) const {
  if (RootID != rhs.RootID)
    return RootID < rhs.RootID;
  if (RootID == 0 && RootName != rhs.RootName)
    return RootName < rhs.RootName;
  return std::lexicographical_compare(
      Projections.begin(), Projections.end(), rhs.Projections.begin(),
      rhs.Projections.end());
}

AccessPathOverlap classifyAccessPathOverlap(const AccessPath &lhs,
                                            const AccessPath &rhs) {
  if (lhs.empty() || rhs.empty())
    return AccessPathOverlap::MayOverlap;

  if (lhs.RootID != 0 && rhs.RootID != 0 && lhs.RootID != rhs.RootID)
    return AccessPathOverlap::NoOverlap;
  if ((lhs.RootID == 0 || rhs.RootID == 0) && lhs.RootName != rhs.RootName)
    return AccessPathOverlap::NoOverlap;

  bool uncertain = false;
  const size_t common = std::min(lhs.Projections.size(), rhs.Projections.size());
  for (size_t i = 0; i < common; ++i) {
    const auto &left = lhs.Projections[i];
    const auto &right = rhs.Projections[i];

    if (left.Kind == AccessProjectionKind::Field &&
        right.Kind == AccessProjectionKind::Field) {
      if (left.Name != right.Name)
        return AccessPathOverlap::NoOverlap;
      continue;
    }

    if (left == right) {
      if (left.Kind != AccessProjectionKind::ConstantIndex)
        uncertain = uncertain ||
                    left.Kind == AccessProjectionKind::DynamicIndex ||
                    left.Kind == AccessProjectionKind::Dereference ||
                    left.Kind == AccessProjectionKind::Unknown;
      continue;
    }

    if (left.Kind == AccessProjectionKind::ConstantIndex &&
        right.Kind == AccessProjectionKind::ConstantIndex) {
      return AccessPathOverlap::NoOverlap;
    }

    // Dynamic indexes, dereferences, and unknown provenance remain
    // conservative until a later alias analysis can prove them disjoint.
    uncertain = true;
  }

  if (lhs.Projections.size() != rhs.Projections.size()) {
    const auto &longer = lhs.Projections.size() > rhs.Projections.size()
                             ? lhs.Projections
                             : rhs.Projections;
    for (size_t i = common; i < longer.size(); ++i) {
      if (longer[i].Kind != AccessProjectionKind::Field)
        uncertain = true;
    }
  }

  return uncertain ? AccessPathOverlap::MayOverlap
                   : AccessPathOverlap::MustOverlap;
}

bool accessPathIsLegacyPrefix(const AccessPath &prefix,
                              const AccessPath &full) {
  if (prefix.empty() || full.empty())
    return false;
  if (prefix.RootID != 0 && full.RootID != 0 && prefix.RootID != full.RootID)
    return false;
  if ((prefix.RootID == 0 || full.RootID == 0) &&
      prefix.RootName != full.RootName)
    return false;

  std::vector<std::string> prefixFields;
  std::vector<std::string> fullFields;
  for (const auto &projection : prefix.Projections) {
    if (projection.Kind == AccessProjectionKind::Field)
      prefixFields.push_back(projection.Name);
  }
  for (const auto &projection : full.Projections) {
    if (projection.Kind == AccessProjectionKind::Field)
      fullFields.push_back(projection.Name);
  }
  if (prefixFields.size() > fullFields.size())
    return false;
  return std::equal(prefixFields.begin(), prefixFields.end(),
                    fullFields.begin());
}

} // namespace toka
