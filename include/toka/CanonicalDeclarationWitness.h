// Copyright (c) 2026 YiZhonghua<zhyi@dpai.com>. All rights reserved.
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
#include <optional>
#include <string>
#include <vector>

namespace toka {

// A structured, declaration-only CDW1 input. This is intentionally separate
// from the AST and from TKI comments: the caller supplies already-resolved,
// admitted identities, and the encoder emits canonical bytes only.
struct OutcomeDeclarationWitnessInput {
  struct Parameter {
    uint32_t Index = 0;
    bool IsInit = false;
    bool IsCeded = false;
    std::string CanonicalPhysicalType;
  };

  struct NominalEnum {
    std::string CrateId;
    std::string LogicalModulePath;
    std::string Name;
    uint32_t GenericArity = 0;
  };

  struct Case {
    std::string VariantName;
    uint32_t VariantOrdinal = 0;
    bool InitializesSubject = false;
  };

  std::string FunctionCrateId;
  std::string FunctionLogicalModulePath;
  std::string FunctionName;
  uint32_t FunctionGenericArity = 0;
  uint32_t EffectKind = 0;
  std::vector<Parameter> Parameters;
  std::string CanonicalResultType;
  uint32_t OutcomeFormalIndex = 0;
  NominalEnum ReturnEnum;
  std::vector<Case> Cases;
};

class CanonicalDeclarationWitnessEncoder {
public:
  // Returns the complete CDW1 byte string for one outcome-transition record.
  // Unsupported, incomplete, non-canonical, or non-P1 inputs return nullopt.
  static std::optional<std::string>
  encodeOutcomeTransition(const OutcomeDeclarationWitnessInput &input);

  // The @tki prototype transports canonical bytes only as lowercase hex. This
  // rendering is deliberately not an import format or manifest envelope.
  static std::string hexEncode(const std::string &bytes);
};

} // namespace toka
