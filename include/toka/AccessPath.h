// Copyright (c) 2025 YiZhonghua<zhyi@dpai.com>. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
#pragma once

#include "toka/SourceLocation.h"
#include <cstdint>
#include <string>
#include <vector>

namespace toka {

enum class AccessProjectionKind {
  Field,
  ConstantIndex,
  DynamicIndex,
  Dereference,
  Unknown,
};

struct AccessProjection {
  AccessProjectionKind Kind = AccessProjectionKind::Unknown;
  std::string Name;
  uint64_t Index = 0;
  SourceLocation Loc;

  static AccessProjection field(std::string name, SourceLocation loc = {}) {
    AccessProjection projection;
    projection.Kind = AccessProjectionKind::Field;
    projection.Name = std::move(name);
    projection.Loc = loc;
    return projection;
  }

  static AccessProjection constantIndex(uint64_t index,
                                        SourceLocation loc = {}) {
    AccessProjection projection;
    projection.Kind = AccessProjectionKind::ConstantIndex;
    projection.Index = index;
    projection.Loc = loc;
    return projection;
  }

  static AccessProjection dynamicIndex(SourceLocation loc = {}) {
    AccessProjection projection;
    projection.Kind = AccessProjectionKind::DynamicIndex;
    projection.Loc = loc;
    return projection;
  }

  static AccessProjection dereference(SourceLocation loc = {}) {
    AccessProjection projection;
    projection.Kind = AccessProjectionKind::Dereference;
    projection.Loc = loc;
    return projection;
  }

  static AccessProjection unknown(SourceLocation loc = {}) {
    AccessProjection projection;
    projection.Kind = AccessProjectionKind::Unknown;
    projection.Loc = loc;
    return projection;
  }

  bool operator==(const AccessProjection &rhs) const;
  bool operator<(const AccessProjection &rhs) const;
};

struct AccessPath {
  uint64_t RootID = 0;
  std::string RootName;
  SourceLocation RootLoc;
  std::vector<AccessProjection> Projections;

  bool empty() const { return RootName.empty(); }
  explicit operator bool() const { return !empty(); }

  // Keeps diagnostics stable during the structured-path migration.
  std::string toLegacyString() const;
  std::string toDebugString() const;

  bool operator==(const AccessPath &rhs) const;
  bool operator<(const AccessPath &rhs) const;
};

enum class AccessPathOverlap {
  NoOverlap,
  MayOverlap,
  MustOverlap,
};

AccessPathOverlap classifyAccessPathOverlap(const AccessPath &lhs,
                                            const AccessPath &rhs);
bool accessPathIsLegacyPrefix(const AccessPath &prefix,
                              const AccessPath &full);

inline bool accessPathsMayOverlap(const AccessPath &lhs,
                                  const AccessPath &rhs) {
  return classifyAccessPathOverlap(lhs, rhs) != AccessPathOverlap::NoOverlap;
}

} // namespace toka
