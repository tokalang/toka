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
#include "toka/CanonicalDeclarationWitness.h"

#include <algorithm>
#include <limits>
#include <utility>

namespace toka {
namespace {

using Field = std::pair<uint16_t, std::string>;

constexpr const char kMagic[] = "toka.declaration-witness\0";
constexpr const char kTypeIdentityPrefix[] = "toka-outcome-type-v1;";

void appendU8(std::string &out, uint8_t value) {
  out.push_back(static_cast<char>(value));
}

void appendU16BE(std::string &out, uint16_t value) {
  appendU8(out, static_cast<uint8_t>(value >> 8));
  appendU8(out, static_cast<uint8_t>(value));
}

void appendU32BE(std::string &out, uint32_t value) {
  appendU8(out, static_cast<uint8_t>(value >> 24));
  appendU8(out, static_cast<uint8_t>(value >> 16));
  appendU8(out, static_cast<uint8_t>(value >> 8));
  appendU8(out, static_cast<uint8_t>(value));
}

bool fitsU16(size_t value) {
  return value <= std::numeric_limits<uint16_t>::max();
}

bool fitsU32(size_t value) {
  return value <= std::numeric_limits<uint32_t>::max();
}

bool isValidUtf8(const std::string &value) {
  for (size_t index = 0; index < value.size();) {
    const unsigned char lead = static_cast<unsigned char>(value[index]);
    if (lead <= 0x7f) {
      ++index;
      continue;
    }

    size_t count = 0;
    uint32_t codePoint = 0;
    if (lead >= 0xc2 && lead <= 0xdf) {
      count = 2;
      codePoint = lead & 0x1f;
    } else if (lead >= 0xe0 && lead <= 0xef) {
      count = 3;
      codePoint = lead & 0x0f;
    } else if (lead >= 0xf0 && lead <= 0xf4) {
      count = 4;
      codePoint = lead & 0x07;
    } else {
      return false;
    }
    if (index + count > value.size())
      return false;
    for (size_t offset = 1; offset < count; ++offset) {
      const unsigned char next =
          static_cast<unsigned char>(value[index + offset]);
      if ((next & 0xc0) != 0x80)
        return false;
      codePoint = (codePoint << 6) | (next & 0x3f);
    }
    if ((count == 3 && codePoint < 0x800) ||
        (count == 4 && codePoint < 0x10000) ||
        (codePoint >= 0xd800 && codePoint <= 0xdfff) ||
        codePoint > 0x10ffff)
      return false;
    index += count;
  }
  return true;
}

bool hasCanonicalTypeIdentity(const std::string &type) {
  return type.rfind(kTypeIdentityPrefix, 0) == 0 && isValidUtf8(type);
}

std::optional<std::string> encodeFieldList(const std::vector<Field> &fields) {
  if (!fitsU16(fields.size()))
    return std::nullopt;

  std::string result;
  appendU16BE(result, static_cast<uint16_t>(fields.size()));
  uint16_t previous = 0;
  bool hasPrevious = false;
  for (const auto &[tag, bytes] : fields) {
    if ((hasPrevious && tag <= previous) || !fitsU32(bytes.size()))
      return std::nullopt;
    appendU16BE(result, tag);
    appendU32BE(result, static_cast<uint32_t>(bytes.size()));
    result += bytes;
    previous = tag;
    hasPrevious = true;
  }
  return result;
}

std::optional<std::string>
encodeSequence(const std::vector<std::string> &items) {
  if (!fitsU32(items.size()))
    return std::nullopt;
  std::string result;
  appendU32BE(result, static_cast<uint32_t>(items.size()));
  for (const auto &item : items) {
    if (!fitsU32(item.size()))
      return std::nullopt;
    appendU32BE(result, static_cast<uint32_t>(item.size()));
    result += item;
  }
  return result;
}

std::string u32Bytes(uint32_t value) {
  std::string result;
  appendU32BE(result, value);
  return result;
}

std::string u8Bytes(bool value) {
  return std::string(1, value ? '\x01' : '\x00');
}

std::optional<std::string>
encodeEnumIdentityWithArity(const OutcomeDeclarationWitnessInput::NominalEnum &value) {
  if (value.CrateId.empty() || value.LogicalModulePath.empty() ||
      value.Name.empty() || !isValidUtf8(value.CrateId) ||
      !isValidUtf8(value.LogicalModulePath) || !isValidUtf8(value.Name))
    return std::nullopt;
  return encodeFieldList({
      {0x0211, value.CrateId},
      {0x0212, value.LogicalModulePath},
      {0x0213, "enum"},
      {0x0214, value.Name},
      {0x0215, u32Bytes(value.GenericArity)},
  });
}

} // namespace

std::optional<std::string> CanonicalDeclarationWitnessEncoder::encodeOutcomeTransition(
    const OutcomeDeclarationWitnessInput &input) {
  if (input.FunctionCrateId.empty() || input.FunctionLogicalModulePath.empty() ||
      input.FunctionName.empty() || input.FunctionGenericArity != 0 ||
      input.ReturnEnum.GenericArity != 0 ||
      !isValidUtf8(input.FunctionCrateId) ||
      !isValidUtf8(input.FunctionLogicalModulePath) ||
      !isValidUtf8(input.FunctionName) ||
      !hasCanonicalTypeIdentity(input.CanonicalResultType) ||
      input.OutcomeFormalIndex >= input.Parameters.size() ||
      !input.Parameters[input.OutcomeFormalIndex].IsInit || input.Cases.empty())
    return std::nullopt;

  std::vector<std::string> parameters;
  parameters.reserve(input.Parameters.size());
  for (size_t index = 0; index < input.Parameters.size(); ++index) {
    const auto &parameter = input.Parameters[index];
    if (parameter.Index != index ||
        !hasCanonicalTypeIdentity(parameter.CanonicalPhysicalType))
      return std::nullopt;
    auto encoded = encodeFieldList({
        {0x0111, u32Bytes(parameter.Index)},
        {0x0112, parameter.IsInit ? "init" : "ordinary"},
        {0x0113, u8Bytes(parameter.IsCeded)},
        {0x0114, parameter.CanonicalPhysicalType},
    });
    if (!encoded)
      return std::nullopt;
    parameters.push_back(std::move(*encoded));
  }
  auto parameterSequence = encodeSequence(parameters);
  auto enumIdentity = encodeEnumIdentityWithArity(input.ReturnEnum);
  if (!parameterSequence || !enumIdentity)
    return std::nullopt;

  std::vector<std::pair<std::string, std::string>> cases;
  cases.reserve(input.Cases.size());
  for (const auto &entry : input.Cases) {
    if (entry.VariantName.empty() || !isValidUtf8(entry.VariantName))
      return std::nullopt;
    auto variant = encodeFieldList({
        {0x0231, *enumIdentity},
        {0x0232, entry.VariantName},
        {0x0233, u32Bytes(entry.VariantOrdinal)},
    });
    if (!variant)
      return std::nullopt;
    auto encodedCase = encodeFieldList({
        {0x0221, *variant},
        {0x0222, entry.InitializesSubject ? "init" : "uninit"},
    });
    if (!encodedCase)
      return std::nullopt;
    cases.emplace_back(std::move(*variant), std::move(*encodedCase));
  }
  std::sort(cases.begin(), cases.end(),
            [](const auto &lhs, const auto &rhs) { return lhs.first < rhs.first; });
  for (size_t index = 1; index < cases.size(); ++index) {
    if (cases[index - 1].first == cases[index].first)
      return std::nullopt;
  }
  std::vector<std::string> orderedCases;
  orderedCases.reserve(cases.size());
  for (auto &entry : cases)
    orderedCases.push_back(std::move(entry.second));
  auto caseSequence = encodeSequence(orderedCases);
  if (!caseSequence)
    return std::nullopt;

  auto subject = encodeFieldList({
      {0x0101, input.FunctionCrateId},
      {0x0102, input.FunctionLogicalModulePath},
      {0x0103, "function"},
      {0x0104, input.FunctionName},
      {0x0105, u32Bytes(input.FunctionGenericArity)},
      {0x0106, u32Bytes(input.EffectKind)},
      {0x0107, *parameterSequence},
      {0x0108, input.CanonicalResultType},
  });
  auto payload = encodeFieldList({
      {0x0201, u32Bytes(input.OutcomeFormalIndex)},
      {0x0202, *enumIdentity},
      {0x0203, *caseSequence},
  });
  if (!subject || !payload)
    return std::nullopt;

  auto record = encodeFieldList({
      {0x0001, "outcome-transition"},
      {0x0002, "SafetyRequired"},
      {0x0003, "RecomputedDeclarationFact"},
      {0x0004, *subject},
      {0x0005, *payload},
  });
  if (!record || !fitsU32(record->size()))
    return std::nullopt;

  std::string result(kMagic, sizeof(kMagic) - 1);
  appendU16BE(result, 1);
  appendU32BE(result, 1);
  appendU32BE(result, static_cast<uint32_t>(record->size()));
  result += *record;
  return result;
}

std::string CanonicalDeclarationWitnessEncoder::hexEncode(
    const std::string &bytes) {
  static constexpr char kHex[] = "0123456789abcdef";
  std::string result;
  result.reserve(bytes.size() * 2);
  for (unsigned char value : bytes) {
    result.push_back(kHex[value >> 4]);
    result.push_back(kHex[value & 0x0f]);
  }
  return result;
}

} // namespace toka
