// Copyright (c) 2026 YiZhonghua<zhyi@dpai.com>. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
#pragma once

#include "toka/NominalShapeId.h"
#include "toka/SemanticModel.h"
#include <cstdint>
#include <optional>
#include <vector>

namespace toka {

enum class D4LegacyReason : uint8_t {
  ZeroCompatible,
  MultipleCompatible,
};

enum class D4ProbeInfrastructureError : uint8_t {
  InvalidCallSiteIdentity,
  InvalidNominalShapeId,
  DuplicateCandidateIdentity,
  DuplicateLegacyOrdinal,
  NonContiguousLegacyOrdinal,
  MalformedBatch,
};

enum class D4ProbeDisposition : uint8_t {
  UniqueCompatible,
  LegacyRequired,
};

enum class D4ProbeSchedule : uint8_t {
  LegacyOrder,
  ReverseForTesting,
};

const char *toString(D4LegacyReason value);
const char *toString(D4ProbeInfrastructureError value);
const char *toString(D4ProbeDisposition value);

struct D4NominalCandidateInput {
  DeclarationId Declaration;
  unsigned LegacyOrdinal = 0;
  std::optional<NominalShapeId> FormalNominal;

  D4NominalCandidateInput(DeclarationId declaration, unsigned legacyOrdinal,
                          std::optional<NominalShapeId> formalNominal)
      : Declaration(std::move(declaration)), LegacyOrdinal(legacyOrdinal),
        FormalNominal(std::move(formalNominal)) {}

  friend bool operator==(const D4NominalCandidateInput &lhs,
                         const D4NominalCandidateInput &rhs) {
    return lhs.Declaration == rhs.Declaration &&
           lhs.LegacyOrdinal == rhs.LegacyOrdinal &&
           lhs.FormalNominal == rhs.FormalNominal;
  }
};

struct D4PureNominalProbeInput {
  SemanticNodeId CallSite;
  std::optional<NominalShapeId> ActualNominal;
  std::vector<D4NominalCandidateInput> Candidates;

  D4PureNominalProbeInput(SemanticNodeId callSite,
                          std::optional<NominalShapeId> actualNominal,
                          std::vector<D4NominalCandidateInput> candidates)
      : CallSite(std::move(callSite)), ActualNominal(std::move(actualNominal)),
        Candidates(std::move(candidates)) {}
};

struct D4NominalCandidateResult {
  DeclarationId Declaration;
  unsigned LegacyOrdinal = 0;
  bool Compatible = false;

  friend bool operator==(const D4NominalCandidateResult &lhs,
                         const D4NominalCandidateResult &rhs) {
    return lhs.Declaration == rhs.Declaration &&
           lhs.LegacyOrdinal == rhs.LegacyOrdinal &&
           lhs.Compatible == rhs.Compatible;
  }
};

class D4NominalProbeBatchResult final {
public:
  D4ProbeDisposition disposition() const { return Disposition; }
  const std::optional<DeclarationId> &selectedDeclaration() const {
    return SelectedDeclaration;
  }
  const std::optional<D4LegacyReason> &legacyReason() const {
    return LegacyReason;
  }
  const std::vector<D4NominalCandidateResult> &candidates() const {
    return Candidates;
  }

  friend bool operator==(const D4NominalProbeBatchResult &lhs,
                         const D4NominalProbeBatchResult &rhs) {
    return lhs.Disposition == rhs.Disposition &&
           lhs.SelectedDeclaration == rhs.SelectedDeclaration &&
           lhs.LegacyReason == rhs.LegacyReason &&
           lhs.Candidates == rhs.Candidates;
  }

private:
  friend class PureNominalOverloadProbe;
  D4ProbeDisposition Disposition = D4ProbeDisposition::LegacyRequired;
  std::optional<DeclarationId> SelectedDeclaration;
  std::optional<D4LegacyReason> LegacyReason;
  std::vector<D4NominalCandidateResult> Candidates;
};

struct D4NominalProbeExpected {
  std::optional<D4NominalProbeBatchResult> Result;
  std::optional<D4ProbeInfrastructureError> Error;

  explicit operator bool() const { return Result.has_value() && !Error; }
};

class PureNominalOverloadProbe final {
public:
  static D4NominalProbeExpected
  run(D4PureNominalProbeInput input,
      D4ProbeSchedule schedule = D4ProbeSchedule::LegacyOrder);
};

} // namespace toka
