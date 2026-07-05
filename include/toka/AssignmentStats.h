// Copyright (c) 2025 YiZhonghua<zhyi@dpai.com>. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
#pragma once

#include <cstdint>
#include <unordered_map>

#ifndef __EMSCRIPTEN__
#include "llvm/Support/raw_ostream.h"
#endif

namespace toka {

enum class AssignmentFrontendEvidence {
  Payload,
  Handle,
  ResidualCompound,
  Unclassified,
};

enum class AssignmentLoweringCarrier {
  SoulStore,
  EnvelopeRebind,
  ResidualLowering,
  Missing,
};

struct AssignmentEvidenceSite {
  AssignmentFrontendEvidence Frontend =
      AssignmentFrontendEvidence::Unclassified;
  AssignmentLoweringCarrier Lowering = AssignmentLoweringCarrier::Missing;
};

struct AssignmentStats {
  bool Enabled = false;
  uint64_t TotalAssignmentSites = 0;
  uint64_t PlainPayloadAssignments = 0;
  uint64_t ImplicitDerefPayloadAssignments = 0;
  uint64_t HandleRebindings = 0;
  uint64_t ReferenceRebindings = 0;
  uint64_t MemberLHSAssignments = 0;
  uint64_t TerminalMemberMorphologyAssignments = 0;
  uint64_t CompoundAssignments = 0;
  uint64_t LoweredSoulAssignments = 0;
  uint64_t LoweredEnvelopeRebindings = 0;
  std::unordered_map<const void *, AssignmentEvidenceSite>
      EvidencePreservationSites;

  void reset() { *this = AssignmentStats{}; }
};

inline AssignmentStats &assignmentStats() {
  static AssignmentStats stats;
  return stats;
}

inline void enableAssignmentStats(bool enabled) {
  assignmentStats().reset();
  assignmentStats().Enabled = enabled;
}

inline bool assignmentStatsEnabled() { return assignmentStats().Enabled; }

inline void
recordAssignmentFrontendEvidence(const void *site,
                                 AssignmentFrontendEvidence evidence) {
  if (!assignmentStatsEnabled() || !site)
    return;
  assignmentStats().EvidencePreservationSites[site].Frontend = evidence;
}

inline void
recordAssignmentLoweringCarrier(const void *site,
                                AssignmentLoweringCarrier carrier) {
  if (!assignmentStatsEnabled() || !site)
    return;
  assignmentStats().EvidencePreservationSites[site].Lowering = carrier;
}

#ifndef __EMSCRIPTEN__
inline bool isDirectAssignmentEvidence(AssignmentFrontendEvidence evidence) {
  return evidence == AssignmentFrontendEvidence::Payload ||
         evidence == AssignmentFrontendEvidence::Handle;
}

inline bool assignmentEvidenceAgrees(AssignmentFrontendEvidence evidence,
                                     AssignmentLoweringCarrier carrier) {
  return (evidence == AssignmentFrontendEvidence::Payload &&
          carrier == AssignmentLoweringCarrier::SoulStore) ||
         (evidence == AssignmentFrontendEvidence::Handle &&
          carrier == AssignmentLoweringCarrier::EnvelopeRebind);
}

inline void dumpAssignmentStatsJson(llvm::raw_ostream &os, uint64_t files) {
  const AssignmentStats &stats = assignmentStats();
  uint64_t directlyClassifiedSites = 0;
  uint64_t frontendLoweringAgree = 0;
  uint64_t mismatches = 0;
  uint64_t missingLowering = 0;
  uint64_t residualCompoundSites = 0;
  uint64_t unclassifiedSites = 0;
  uint64_t payloadToSoulStore = 0;
  uint64_t handleToEnvelopeRebind = 0;

  for (const auto &entry : stats.EvidencePreservationSites) {
    const AssignmentEvidenceSite &site = entry.second;
    if (site.Frontend == AssignmentFrontendEvidence::ResidualCompound) {
      residualCompoundSites++;
      continue;
    }
    if (site.Frontend == AssignmentFrontendEvidence::Unclassified) {
      unclassifiedSites++;
      continue;
    }
    if (!isDirectAssignmentEvidence(site.Frontend))
      continue;

    directlyClassifiedSites++;
    if (site.Lowering == AssignmentLoweringCarrier::Missing)
      missingLowering++;

    if (assignmentEvidenceAgrees(site.Frontend, site.Lowering)) {
      frontendLoweringAgree++;
      if (site.Frontend == AssignmentFrontendEvidence::Payload)
        payloadToSoulStore++;
      else if (site.Frontend == AssignmentFrontendEvidence::Handle)
        handleToEnvelopeRebind++;
    } else {
      mismatches++;
    }
  }

  uint64_t coverageBasisPoints =
      stats.TotalAssignmentSites == 0
          ? 0
          : (directlyClassifiedSites * 10000 + stats.TotalAssignmentSites / 2) /
                stats.TotalAssignmentSites;

  os << "{\n";
  os << "  \"tool\": \"toka\",\n";
  os << "  \"mode\": \"assignment-classification-audit\",\n";
  os << "  \"files\": " << files << ",\n";
  os << "  \"total_assignment_sites\": " << stats.TotalAssignmentSites << ",\n";
  os << "  \"sema\": {\n";
  os << "    \"plain_payload_assignments\": "
     << stats.PlainPayloadAssignments << ",\n";
  os << "    \"implicit_deref_payload_assignments\": "
     << stats.ImplicitDerefPayloadAssignments << ",\n";
  os << "    \"handle_rebindings\": " << stats.HandleRebindings << ",\n";
  os << "    \"reference_rebindings\": " << stats.ReferenceRebindings << ",\n";
  os << "    \"member_lhs_assignments\": " << stats.MemberLHSAssignments
     << ",\n";
  os << "    \"terminal_member_morphology_assignments\": "
     << stats.TerminalMemberMorphologyAssignments << ",\n";
  os << "    \"compound_assignments\": " << stats.CompoundAssignments << "\n";
  os << "  },\n";
  os << "  \"codegen\": {\n";
  os << "    \"lowered_soul_assignments\": "
     << stats.LoweredSoulAssignments << ",\n";
  os << "    \"lowered_envelope_rebindings\": "
     << stats.LoweredEnvelopeRebindings << "\n";
  os << "  },\n";
  os << "  \"evidence_preservation\": {\n";
  os << "    \"total_assignment_sites\": " << stats.TotalAssignmentSites
     << ",\n";
  os << "    \"directly_classified_sites\": " << directlyClassifiedSites
     << ",\n";
  os << "    \"coverage_percent\": " << (coverageBasisPoints / 100) << ".";
  if (coverageBasisPoints % 100 < 10)
    os << "0";
  os << (coverageBasisPoints % 100) << ",\n";
  os << "    \"frontend_lowering_agree\": " << frontendLoweringAgree
     << ",\n";
  os << "    \"mismatches\": " << mismatches << ",\n";
  os << "    \"missing_lowering\": " << missingLowering << ",\n";
  os << "    \"residual_compound_sites\": " << residualCompoundSites << ",\n";
  os << "    \"unclassified_sites\": " << unclassifiedSites << ",\n";
  os << "    \"payload_to_soul_store\": " << payloadToSoulStore << ",\n";
  os << "    \"handle_to_envelope_rebind\": " << handleToEnvelopeRebind
     << "\n";
  os << "  }\n";
  os << "}\n";
}
#endif

} // namespace toka
