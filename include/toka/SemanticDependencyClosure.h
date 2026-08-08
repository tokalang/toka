// Copyright (c) 2026 YiZhonghua<zhyi@dpai.com>. All rights reserved.
//
// Licensed under the Apache License, Version 2.0.
#pragma once

#include <optional>
#include <set>
#include <string>
#include <vector>

namespace toka {

class Module;

// Computes the P1 replay-surface Merkle closure for one resolver-selected
// module. Unknown coordinates, absent imported ASTs, duplicate coordinates in
// the reachable graph, and import cycles are rejected rather than represented
// by a path-dependent fallback.
class SemanticDependencyClosure {
public:
  static std::optional<std::string>
  calculate(const Module &root, const std::vector<Module *> &modules,
            std::vector<std::string> &errors,
            const std::set<const Module *> &bodylessOutcomeModules = {});
};

} // namespace toka
