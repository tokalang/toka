// Copyright (c) 2026 YiZhonghua<zhyi@dpai.com>. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "toka/PureNominalOverloadProbe.h"
#include <set>

namespace toka {

const char *toString(D4LegacyReason value) {
  switch (value) {
  case D4LegacyReason::ZeroCompatible:
    return "ZeroCompatible";
  case D4LegacyReason::MultipleCompatible:
    return "MultipleCompatible";
  }
  return "ZeroCompatible";
}

const char *toString(D4ProbeInfrastructureError value) {
  switch (value) {
  case D4ProbeInfrastructureError::InvalidCallSiteIdentity:
    return "InvalidCallSiteIdentity";
  case D4ProbeInfrastructureError::InvalidNominalShapeId:
    return "InvalidNominalShapeId";
  case D4ProbeInfrastructureError::DuplicateCandidateIdentity:
    return "DuplicateCandidateIdentity";
  case D4ProbeInfrastructureError::DuplicateLegacyOrdinal:
    return "DuplicateLegacyOrdinal";
  case D4ProbeInfrastructureError::NonContiguousLegacyOrdinal:
    return "NonContiguousLegacyOrdinal";
  case D4ProbeInfrastructureError::MalformedBatch:
    return "MalformedBatch";
  }
  return "MalformedBatch";
}

const char *toString(D4ProbeDisposition value) {
  switch (value) {
  case D4ProbeDisposition::UniqueCompatible:
    return "UniqueCompatible";
  case D4ProbeDisposition::LegacyRequired:
    return "LegacyRequired";
  }
  return "LegacyRequired";
}

D4NominalProbeExpected
PureNominalOverloadProbe::run(D4PureNominalProbeInput input,
                              D4ProbeSchedule schedule) {
  auto fail = [](D4ProbeInfrastructureError error) {
    D4NominalProbeExpected result;
    result.Error = error;
    return result;
  };
  if (!input.CallSite.valid())
    return fail(D4ProbeInfrastructureError::InvalidCallSiteIdentity);
  if (!input.ActualNominal || input.ActualNominal->canonical().empty())
    return fail(D4ProbeInfrastructureError::InvalidNominalShapeId);
  if (input.Candidates.size() < 2)
    return fail(D4ProbeInfrastructureError::MalformedBatch);

  std::set<DeclarationId> declarations;
  std::set<unsigned> ordinals;
  D4NominalProbeBatchResult batch;
  batch.Candidates.resize(input.Candidates.size());
  size_t compatibleCount = 0;
  for (size_t step = 0; step < input.Candidates.size(); ++step) {
    const size_t index = schedule == D4ProbeSchedule::LegacyOrder
                             ? step
                             : input.Candidates.size() - step - 1;
    const auto &candidate = input.Candidates[index];
    if (!candidate.Declaration.valid())
      return fail(D4ProbeInfrastructureError::DuplicateCandidateIdentity);
    if (!declarations.insert(candidate.Declaration).second)
      return fail(D4ProbeInfrastructureError::DuplicateCandidateIdentity);
    if (!ordinals.insert(candidate.LegacyOrdinal).second)
      return fail(D4ProbeInfrastructureError::DuplicateLegacyOrdinal);
    if (candidate.LegacyOrdinal != index)
      return fail(D4ProbeInfrastructureError::NonContiguousLegacyOrdinal);
    if (!candidate.FormalNominal ||
        candidate.FormalNominal->canonical().empty())
      return fail(D4ProbeInfrastructureError::InvalidNominalShapeId);
    const bool compatible = *input.ActualNominal == *candidate.FormalNominal;
    compatibleCount += compatible ? 1U : 0U;
    batch.Candidates[candidate.LegacyOrdinal] = {
        candidate.Declaration, candidate.LegacyOrdinal, compatible};
    if (compatible)
      batch.SelectedDeclaration = candidate.Declaration;
  }

  if (compatibleCount == 1) {
    batch.Disposition = D4ProbeDisposition::UniqueCompatible;
  } else {
    batch.Disposition = D4ProbeDisposition::LegacyRequired;
    batch.SelectedDeclaration.reset();
    batch.LegacyReason = compatibleCount == 0
                             ? D4LegacyReason::ZeroCompatible
                             : D4LegacyReason::MultipleCompatible;
  }
  D4NominalProbeExpected result;
  result.Result = std::move(batch);
  return result;
}

} // namespace toka
