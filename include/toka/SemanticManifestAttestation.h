// Copyright (c) 2026 YiZhonghua<zhyi@dpai.com>. All rights reserved.
//
// Licensed under the Apache License, Version 2.0.
#pragma once

#include "toka/SemanticManifestEnvelope.h"
#include <string>
#include <vector>

namespace llvm {
class Module;
}

namespace toka {

// P2 is intentionally separate from the P1 declaration-recomputed envelope:
// it carries one body-derived Outcome fulfilment class and therefore needs an
// exact object plus accepted local compiler provenance.
struct SemanticManifestAttestationInput {
  std::string TargetTriple;
  SemanticManifestCoordinate Module;
  std::string InterfaceContent;
  std::string SemanticDependencyClosureDigest;
  std::vector<std::string> CDW1Records;
};

struct SemanticManifestAttestationPrepared {
  SemanticManifestAttestationInput Input;
  std::vector<std::string> CDW1Records;
  std::string InterfaceDigest;
  std::string PayloadDigest;
  std::string ObjectMarker;
};

struct SemanticManifestAttestationResult {
  std::vector<std::string> CDW1Records;
  std::string SemanticDependencyClosureDigest;
  std::string ObjectDigest;
  std::string ObjectMarker;
};

enum class SemanticManifestAttestationStatus {
  Missing,
  ReadError,
  InvalidSchema,
  IdentityMismatch,
  InvalidRecord,
  PayloadMismatch,
  MissingObject,
  ObjectMismatch,
  MarkerMismatch,
  ProvenanceMismatch,
  Valid,
};

class SemanticManifestAttestation {
public:
  static constexpr unsigned SchemaVersion = 2;

  static bool prepare(const SemanticManifestAttestationInput &input,
                      SemanticManifestAttestationPrepared &prepared,
                      std::vector<std::string> &errors);
  static bool bindObjectMarker(llvm::Module &irModule,
                               const SemanticManifestAttestationPrepared &prepared,
                               std::vector<std::string> &errors);
  static bool write(const std::string &path, const std::string &objectPath,
                    const SemanticManifestAttestationPrepared &prepared,
                    const std::string &provenanceStateDirectory,
                    std::vector<std::string> &errors);

  // Candidate validation intentionally omits the resolver-owned dependency
  // closure comparison. Callers may use it only as a provisional bodyless
  // Outcome candidate and must call `load` after full graph reconstruction.
  static SemanticManifestAttestationStatus
  loadCandidate(const std::string &path, const std::string &interfaceContent,
                const SemanticManifestCoordinate &expectedModule,
                const std::string &expectedTargetTriple,
                const std::string &objectPath,
                const std::string &provenanceStateDirectory,
                SemanticManifestAttestationResult &result,
                std::string &reason);

  static SemanticManifestAttestationStatus
  load(const std::string &path, const std::string &interfaceContent,
       const SemanticManifestCoordinate &expectedModule,
       const std::string &expectedTargetTriple,
       const std::string &expectedSemanticDependencyClosureDigest,
       const std::string &objectPath,
       const std::string &provenanceStateDirectory,
       SemanticManifestAttestationResult &result, std::string &reason);
};

const char *toString(SemanticManifestAttestationStatus status);

} // namespace toka
