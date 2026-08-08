// Copyright (c) 2026 YiZhonghua<zhyi@dpai.com>. All rights reserved.
//
// Licensed under the Apache License, Version 2.0.
#pragma once

#include <string>
#include <vector>

namespace toka {

struct SemanticManifestCoordinate {
  std::string CrateId;
  std::string LogicalModulePath;
};

struct SemanticManifestEnvelopeInput {
  std::string TargetTriple;
  SemanticManifestCoordinate Module;
  std::string InterfaceContent;
  std::string SemanticDependencyClosureDigest;
  std::vector<std::string> CDW1Records;
};

enum class SemanticManifestEnvelopeStatus {
  Missing,
  ReadError,
  InvalidSchema,
  IdentityMismatch,
  InvalidRecord,
  PayloadMismatch,
  Valid,
};

// P1 transport for declaration-recomputed CDW1 records. This class is
// deliberately independent of TKI comments and ModuleResolver: a valid record
// remains non-authoritative until a later resolver slice opts into comparison.
class SemanticManifestEnvelope {
public:
  static constexpr unsigned SchemaVersion = 1;

  static std::string sidecarPath(const std::string &interfacePath);
  static bool write(const std::string &path,
                    const SemanticManifestEnvelopeInput &input,
                    std::vector<std::string> &errors);
  static SemanticManifestEnvelopeStatus
  load(const std::string &path, const std::string &interfaceContent,
       const SemanticManifestCoordinate &expectedModule,
       const std::string &expectedTargetTriple,
       const std::string &expectedSemanticDependencyClosureDigest,
       std::vector<std::string> &records,
       std::string &semanticDependencyClosureDigest, std::string &reason);
};

const char *toString(SemanticManifestEnvelopeStatus status);

} // namespace toka
