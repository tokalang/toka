// Copyright (c) 2026 YiZhonghua<zhyi@dpai.com>. All rights reserved.
//
// Licensed under the Apache License, Version 2.0.
#include "toka/SemanticDependencyClosure.h"
#include "toka/AST.h"
#include "toka/PathUtils.h"
#include "toka/TKIExporter.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/SHA256.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cstdint>
#include <functional>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>

namespace toka {
namespace {

std::string sha256(const std::string &content) {
  llvm::SHA256 hasher;
  hasher.update(llvm::StringRef(content));
  std::ostringstream out;
  for (uint8_t byte : hasher.final())
    out << std::hex << std::setw(2) << std::setfill('0')
        << static_cast<unsigned>(byte);
  return out.str();
}

void appendU32(std::string &out, uint32_t value) {
  out.push_back(static_cast<char>((value >> 24) & 0xff));
  out.push_back(static_cast<char>((value >> 16) & 0xff));
  out.push_back(static_cast<char>((value >> 8) & 0xff));
  out.push_back(static_cast<char>(value & 0xff));
}

bool appendText(std::string &out, llvm::StringRef value) {
  if (value.size() > UINT32_MAX)
    return false;
  appendU32(out, static_cast<uint32_t>(value.size()));
  out.append(value.data(), value.size());
  return true;
}

bool appendCoordinate(std::string &out, const Module &module) {
  return appendText(out, module.ShadowCrateId) &&
         appendText(out, module.ShadowLogicalModulePath);
}

std::optional<std::string> coordinateKey(const Module &module) {
  if (!module.ShadowCoordinateKnown || module.ShadowCrateId.empty() ||
      module.ShadowLogicalModulePath.empty())
    return std::nullopt;
  std::string key;
  if (!appendCoordinate(key, module))
    return std::nullopt;
  return key;
}

std::optional<std::string> replaySurfaceDigest(const Module &module) {
  std::string surface;
  llvm::raw_string_ostream out(surface);
  TKIExporter exporter(out);
  exporter.exportSemanticReplaySurface(module);
  out.flush();
  return sha256(surface);
}

enum class VisitState { Visiting, Complete };

} // namespace

std::optional<std::string>
SemanticDependencyClosure::calculate(const Module &root,
                                     const std::vector<Module *> &modules,
                                     std::vector<std::string> &errors) {
  std::map<std::string, const Module *> modulesByPath;
  for (const Module *module : modules) {
    if (!module || module->ResolvedPath.empty()) {
      errors.push_back("semantic dependency closure has an unnamed module");
      return std::nullopt;
    }
    const std::string path = PathUtils::canonicalize(module->ResolvedPath);
    if (!modulesByPath.emplace(path, module).second) {
      errors.push_back(
          "semantic dependency closure has duplicate module path " + path);
      return std::nullopt;
    }
  }

  const std::string rootPath = PathUtils::canonicalize(root.ResolvedPath);
  auto rootEntry = modulesByPath.find(rootPath);
  if (root.ResolvedPath.empty() || rootEntry == modulesByPath.end() ||
      rootEntry->second != &root) {
    errors.push_back(
        "semantic dependency closure root is not resolver-selected");
    return std::nullopt;
  }

  std::map<const Module *, VisitState> states;
  std::map<const Module *, std::string> digests;
  std::map<std::string, const Module *> coordinateOwners;
  std::function<std::optional<std::string>(const Module &)> visit;
  visit = [&](const Module &module) -> std::optional<std::string> {
    if (auto state = states.find(&module); state != states.end()) {
      if (state->second == VisitState::Visiting) {
        errors.push_back("semantic dependency closure rejects import cycles");
        return std::nullopt;
      }
      return digests.at(&module);
    }

    auto coordinate = coordinateKey(module);
    if (!coordinate) {
      errors.push_back(
          "semantic dependency closure requires a known module coordinate");
      return std::nullopt;
    }
    auto [owner, inserted] = coordinateOwners.emplace(*coordinate, &module);
    if (!inserted && owner->second != &module) {
      errors.push_back(
          "semantic dependency closure has duplicate module coordinate");
      return std::nullopt;
    }
    states.emplace(&module, VisitState::Visiting);

    std::vector<const Module *> children;
    std::set<const Module *> uniqueChildren;
    for (const auto &import : module.Imports) {
      if (!import || import->ResolvedPath.empty()) {
        errors.push_back(
            "semantic dependency closure has an unresolved import");
        return std::nullopt;
      }
      const std::string importedPath =
          PathUtils::canonicalize(import->ResolvedPath);
      auto child = modulesByPath.find(importedPath);
      if (child == modulesByPath.end()) {
        errors.push_back("semantic dependency closure import is missing from "
                         "the resolver graph");
        return std::nullopt;
      }
      if (uniqueChildren.insert(child->second).second)
        children.push_back(child->second);
    }

    struct ChildDigest {
      std::string Coordinate;
      std::string Digest;
    };
    std::vector<ChildDigest> childDigests;
    childDigests.reserve(children.size());
    for (const Module *child : children) {
      auto childCoordinate = coordinateKey(*child);
      if (!childCoordinate) {
        errors.push_back("semantic dependency closure requires a known "
                         "dependency coordinate");
        return std::nullopt;
      }
      auto childDigest = visit(*child);
      if (!childDigest)
        return std::nullopt;
      childDigests.push_back(
          {std::move(*childCoordinate), std::move(*childDigest)});
    }
    std::sort(childDigests.begin(), childDigests.end(),
              [](const ChildDigest &left, const ChildDigest &right) {
                return left.Coordinate < right.Coordinate;
              });

    auto surfaceDigest = replaySurfaceDigest(module);
    if (!surfaceDigest) {
      errors.push_back(
          "semantic dependency closure could not export replay surface");
      return std::nullopt;
    }
    std::string payload = "toka.semantic-dependency-closure-v1\0";
    if (!appendCoordinate(payload, module) ||
        !appendText(payload, *surfaceDigest) ||
        childDigests.size() > UINT32_MAX) {
      errors.push_back("semantic dependency closure node is too large");
      return std::nullopt;
    }
    appendU32(payload, static_cast<uint32_t>(childDigests.size()));
    for (const ChildDigest &child : childDigests) {
      if (!appendText(payload, child.Coordinate) ||
          !appendText(payload, child.Digest)) {
        errors.push_back("semantic dependency closure child is too large");
        return std::nullopt;
      }
    }
    std::string digest = sha256(payload);
    states[&module] = VisitState::Complete;
    digests.emplace(&module, digest);
    return digest;
  };

  return visit(root);
}

} // namespace toka
