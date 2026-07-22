// Copyright (c) 2025 YiZhonghua<zhyi@dpai.com>. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "toka/AnalysisSession.h"
#include "toka/ModuleResolver.h"
#include "toka/PathUtils.h"
#include "toka/Sema.h"
#include <algorithm>
#include <chrono>
#include <queue>

namespace toka {

AnalysisSession::AnalysisSession(std::vector<std::string> searchPaths,
                                 std::map<std::string, std::string> packageMap,
                                 std::vector<std::string> trustedSystemRoots)
    : SearchPaths(std::move(searchPaths)), PackageMap(std::move(packageMap)),
      TrustedSystemRoots(std::move(trustedSystemRoots)),
      Sources(std::make_unique<SourceManager>()) {}

void AnalysisSession::openDocument(const std::string &path,
                                   std::string content) {
  Overlays[PathUtils::canonicalize(path)] = std::move(content);
}

void AnalysisSession::updateDocument(const std::string &path,
                                     std::string content) {
  openDocument(path, std::move(content));
}

void AnalysisSession::closeDocument(const std::string &path) {
  Overlays.erase(PathUtils::canonicalize(path));
}

bool AnalysisSession::hasDocument(const std::string &path) const {
  return Overlays.count(PathUtils::canonicalize(path)) != 0;
}

std::string AnalysisSession::modulePath(const Module &module) {
  return PathUtils::canonicalize(module.SourcePath.empty() ? module.ResolvedPath
                                                           : module.SourcePath);
}

std::map<std::string, std::set<std::string>>
AnalysisSession::reverseDependencies(
    const std::map<std::string, std::vector<std::string>> &dependencies) {
  std::map<std::string, std::set<std::string>> reverse;
  for (const auto &[module, imports] : dependencies)
    for (const std::string &import : imports)
      reverse[PathUtils::canonicalize(import)].insert(
          PathUtils::canonicalize(module));
  return reverse;
}

AnalysisResult AnalysisSession::analyze(const std::string &rootPath) {
  using Clock = std::chrono::steady_clock;
  auto started = Clock::now();
  AnalysisResult result;
  result.Stats.Revision = ++Revision;

  DiagnosticEngine::reset();
  DiagnosticEngine::init(*Sources);
  DiagnosticEngine::setPrintingEnabled(false);

  std::vector<std::unique_ptr<Module>> parsedModules;
  ModuleResolver resolver(*Sources, SearchPaths, PackageMap, true,
                          TrustedSystemRoots);
  resolver.setSourceOverrides(Overlays);
  resolver.setVersionedSources(true);
  bool parsed = resolver.resolveAndParse(rootPath, parsedModules);

  std::map<std::string, std::string> nextFingerprints;
  for (const auto &[path, info] : resolver.getResolutionRecords()) {
    std::string source = info.SourcePath.empty() ? path : info.SourcePath;
    nextFingerprints[PathUtils::canonicalize(source)] = info.ContentHash;
  }

  std::set<std::string> changed;
  for (const auto &[path, fingerprint] : nextFingerprints) {
    auto old = Fingerprints.find(path);
    if (old == Fingerprints.end() || old->second != fingerprint)
      changed.insert(path);
  }
  for (const auto &[path, fingerprint] : Fingerprints)
    if (!nextFingerprints.count(path))
      changed.insert(path);

  auto oldReverse = reverseDependencies(Dependencies);
  auto nextReverse = reverseDependencies(resolver.getDependencies());
  std::queue<std::string> pending;
  for (const std::string &path : changed)
    pending.push(path);
  std::set<std::string> invalidated = changed;
  while (!pending.empty()) {
    std::string path = pending.front();
    pending.pop();
    auto appendDependents = [&](const auto &reverse) {
      auto found = reverse.find(path);
      if (found == reverse.end())
        return;
      for (const std::string &dependent : found->second)
        if (invalidated.insert(dependent).second)
          pending.push(dependent);
    };
    appendDependents(oldReverse);
    appendDependents(nextReverse);
  }

  result.Stats.InvalidatedModules.assign(invalidated.begin(),
                                         invalidated.end());
  if (!parsed || DiagnosticEngine::hasErrors()) {
    result.Stats.TotalModules = Modules.size();
    result.Stats.ReusedModules = Modules.size();
    result.Diagnostics = DiagnosticEngine::records();
    DiagnosticEngine::setPrintingEnabled(true);
    if (HasIndex)
      result.Index = LastIndex;
    result.Stats.ElapsedMilliseconds =
        std::chrono::duration<double, std::milli>(Clock::now() - started)
            .count();
    return result;
  }

  std::vector<std::string> oldOrder;
  std::map<std::string, std::unique_ptr<Module>> cachedByPath;
  for (auto &module : Modules) {
    oldOrder.push_back(modulePath(*module));
    cachedByPath[oldOrder.back()] = std::move(module);
  }

  std::vector<std::unique_ptr<Module>> nextModules;
  nextModules.reserve(parsedModules.size());
  for (auto &module : parsedModules) {
    std::string path = modulePath(*module);
    auto cached = cachedByPath.find(path);
    if (!invalidated.count(path) && cached != cachedByPath.end()) {
      nextModules.push_back(std::move(cached->second));
      ++result.Stats.ReusedModules;
    } else {
      nextModules.push_back(std::move(module));
    }
  }
  result.Stats.TotalModules = nextModules.size();

  bool checked = !nextModules.empty();
  if (checked) {
    Sema sema;
    for (const auto &module : nextModules)
      sema.declareGlobals(*module);
    for (const auto &module : nextModules) {
      std::string path = modulePath(*module);
      if (!invalidated.count(path))
        continue;
      ++result.Stats.RecheckedModules;
      if (!sema.checkModule(*module) || DiagnosticEngine::hasErrors()) {
        checked = false;
        break;
      }
    }
    if (checked) {
      sema.checkShapeSovereignty();
      checked = !DiagnosticEngine::hasErrors();
    }
  }

  result.Diagnostics = DiagnosticEngine::records();
  DiagnosticEngine::setPrintingEnabled(true);

  if (checked) {
    std::vector<Module *> modulePointers;
    modulePointers.reserve(nextModules.size());
    for (const auto &module : nextModules)
      modulePointers.push_back(module.get());
    LastIndex = SemanticIndex::build(modulePointers, *Sources);
    HasIndex = true;
    Modules = std::move(nextModules);
    Fingerprints = std::move(nextFingerprints);
    Dependencies = resolver.getDependencies();
  } else {
    for (auto &module : nextModules) {
      if (!module)
        continue;
      std::string path = modulePath(*module);
      if (!invalidated.count(path))
        cachedByPath[path] = std::move(module);
    }
    Modules.clear();
    for (const std::string &path : oldOrder) {
      auto found = cachedByPath.find(path);
      if (found != cachedByPath.end() && found->second)
        Modules.push_back(std::move(found->second));
    }
  }

  result.Success = checked;
  result.HasFreshIndex = checked;
  if (HasIndex)
    result.Index = LastIndex;
  result.Stats.ElapsedMilliseconds =
      std::chrono::duration<double, std::milli>(Clock::now() - started).count();
  return result;
}

} // namespace toka
