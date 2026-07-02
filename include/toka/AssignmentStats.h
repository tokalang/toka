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

#include "llvm/Support/raw_ostream.h"
#include <cstdint>

namespace toka {

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

inline void dumpAssignmentStatsJson(llvm::raw_ostream &os, uint64_t files) {
  const AssignmentStats &stats = assignmentStats();
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
  os << "  }\n";
  os << "}\n";
}

} // namespace toka
