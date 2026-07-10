// Copyright (c) 2025 YiZhonghua<zhyi@dpai.com>. All rights reserved.
#pragma once

#include "toka/AST.h"
#include "toka/Type.h"
#include <string>
#include <vector>
#include <map>

namespace toka {

struct TopologyCacheMetrics {
  uint64_t topologyQueries = 0;
  uint64_t validCacheHits = 0;
  uint64_t cacheRecomputations = 0;
  uint64_t invalidations = 0;
  uint64_t retainedThroughPayload = 0;
  uint64_t extensionalFalseInvalidations = 0;
  uint64_t nodesVisited = 0; // only B1 tracks this
  bool cacheValid = false;
};

struct GroupedMetrics {
  TopologyCacheMetrics B0;
  TopologyCacheMetrics B1;
  TopologyCacheMetrics B2;
  TopologyCacheMetrics B3;
};

// Main entry point for the evaluation run
void runTopologyCacheEvaluation(
    const std::vector<std::string> &testFiles,
    const std::vector<std::string> &searchPaths,
    const std::map<std::string, std::string> &pkgMap);

} // namespace toka
