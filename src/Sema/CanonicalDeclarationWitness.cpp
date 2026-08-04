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
#include <initializer_list>
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

class ByteReader {
public:
  explicit ByteReader(const std::string &bytes) : Bytes(bytes) {}

  bool readU8(uint8_t &value) {
    if (Offset == Bytes.size())
      return false;
    value = static_cast<unsigned char>(Bytes[Offset++]);
    return true;
  }

  bool readU16BE(uint16_t &value) {
    uint8_t high = 0;
    uint8_t low = 0;
    if (!readU8(high) || !readU8(low))
      return false;
    value = static_cast<uint16_t>((high << 8) | low);
    return true;
  }

  bool readU32BE(uint32_t &value) {
    uint8_t first = 0;
    uint8_t second = 0;
    uint8_t third = 0;
    uint8_t fourth = 0;
    if (!readU8(first) || !readU8(second) || !readU8(third) ||
        !readU8(fourth))
      return false;
    value = (static_cast<uint32_t>(first) << 24) |
            (static_cast<uint32_t>(second) << 16) |
            (static_cast<uint32_t>(third) << 8) | fourth;
    return true;
  }

  bool readBytes(size_t count, std::string &value) {
    if (count > Bytes.size() - Offset)
      return false;
    value.assign(Bytes.data() + Offset, count);
    Offset += count;
    return true;
  }

  bool atEnd() const { return Offset == Bytes.size(); }

private:
  const std::string &Bytes;
  size_t Offset = 0;
};

std::optional<std::vector<Field>> decodeFieldList(ByteReader &reader) {
  uint16_t count = 0;
  if (!reader.readU16BE(count))
    return std::nullopt;

  std::vector<Field> fields;
  fields.reserve(count);
  uint16_t previous = 0;
  bool hasPrevious = false;
  for (uint16_t index = 0; index < count; ++index) {
    uint16_t tag = 0;
    uint32_t length = 0;
    std::string bytes;
    if (!reader.readU16BE(tag) || !reader.readU32BE(length) ||
        !reader.readBytes(length, bytes) ||
        (hasPrevious && tag <= previous))
      return std::nullopt;
    fields.emplace_back(tag, std::move(bytes));
    previous = tag;
    hasPrevious = true;
  }
  return fields;
}

std::optional<std::vector<Field>> decodeNestedFieldList(
    const std::string &bytes) {
  ByteReader reader(bytes);
  auto fields = decodeFieldList(reader);
  if (!fields || !reader.atEnd())
    return std::nullopt;
  return fields;
}

std::optional<std::vector<std::string>> decodeSequence(ByteReader &reader) {
  uint32_t count = 0;
  if (!reader.readU32BE(count))
    return std::nullopt;

  std::vector<std::string> items;
  for (uint32_t index = 0; index < count; ++index) {
    uint32_t length = 0;
    std::string item;
    if (!reader.readU32BE(length) || !reader.readBytes(length, item))
      return std::nullopt;
    items.push_back(std::move(item));
  }
  return items;
}

std::optional<std::vector<std::string>> decodeNestedSequence(
    const std::string &bytes) {
  ByteReader reader(bytes);
  auto items = decodeSequence(reader);
  if (!items || !reader.atEnd())
    return std::nullopt;
  return items;
}

bool hasTags(const std::vector<Field> &fields,
             std::initializer_list<uint16_t> expectedTags) {
  if (fields.size() != expectedTags.size())
    return false;
  size_t index = 0;
  for (uint16_t tag : expectedTags) {
    if (fields[index++].first != tag)
      return false;
  }
  return true;
}

const std::string *findField(const std::vector<Field> &fields, uint16_t tag) {
  for (const auto &[fieldTag, bytes] : fields) {
    if (fieldTag == tag)
      return &bytes;
  }
  return nullptr;
}

std::optional<uint32_t> decodeU32Field(const std::string &bytes) {
  if (bytes.size() != 4)
    return std::nullopt;
  ByteReader reader(bytes);
  uint32_t value = 0;
  if (!reader.readU32BE(value) || !reader.atEnd())
    return std::nullopt;
  return value;
}

std::optional<bool> decodeBoolField(const std::string &bytes) {
  if (bytes.size() != 1)
    return std::nullopt;
  if (bytes[0] == '\x00')
    return false;
  if (bytes[0] == '\x01')
    return true;
  return std::nullopt;
}

std::optional<OutcomeDeclarationWitnessInput::NominalEnum>
decodeEnumIdentity(const std::string &bytes) {
  auto fields = decodeNestedFieldList(bytes);
  if (!fields || !hasTags(*fields, {0x0211, 0x0212, 0x0213, 0x0214, 0x0215}))
    return std::nullopt;
  const std::string *crateId = findField(*fields, 0x0211);
  const std::string *modulePath = findField(*fields, 0x0212);
  const std::string *kind = findField(*fields, 0x0213);
  const std::string *name = findField(*fields, 0x0214);
  const std::string *arityBytes = findField(*fields, 0x0215);
  if (!crateId || !modulePath || !kind || !name || !arityBytes ||
      *kind != "enum" || !isValidUtf8(*crateId) ||
      !isValidUtf8(*modulePath) || !isValidUtf8(*name))
    return std::nullopt;
  auto arity = decodeU32Field(*arityBytes);
  if (!arity)
    return std::nullopt;
  return OutcomeDeclarationWitnessInput::NominalEnum{
      *crateId, *modulePath, *name, *arity};
}

std::optional<std::vector<OutcomeDeclarationWitnessInput::Parameter>>
decodeParameters(const std::string &bytes) {
  auto items = decodeNestedSequence(bytes);
  if (!items)
    return std::nullopt;

  std::vector<OutcomeDeclarationWitnessInput::Parameter> parameters;
  parameters.reserve(items->size());
  for (size_t index = 0; index < items->size(); ++index) {
    auto fields = decodeNestedFieldList((*items)[index]);
    if (!fields ||
        !hasTags(*fields, {0x0111, 0x0112, 0x0113, 0x0114}))
      return std::nullopt;
    const std::string *indexBytes = findField(*fields, 0x0111);
    const std::string *contractKind = findField(*fields, 0x0112);
    const std::string *cededBytes = findField(*fields, 0x0113);
    const std::string *type = findField(*fields, 0x0114);
    if (!indexBytes || !contractKind || !cededBytes || !type ||
        !hasCanonicalTypeIdentity(*type))
      return std::nullopt;
    auto parameterIndex = decodeU32Field(*indexBytes);
    auto isCeded = decodeBoolField(*cededBytes);
    if (!parameterIndex || !isCeded || *parameterIndex != index ||
        (*contractKind != "init" && *contractKind != "ordinary"))
      return std::nullopt;
    parameters.push_back({*parameterIndex, *contractKind == "init", *isCeded,
                          *type});
  }
  return parameters;
}

std::optional<std::vector<OutcomeDeclarationWitnessInput::Case>>
decodeCases(const std::string &bytes, const std::string &enumIdentity) {
  auto items = decodeNestedSequence(bytes);
  if (!items)
    return std::nullopt;

  std::vector<OutcomeDeclarationWitnessInput::Case> cases;
  cases.reserve(items->size());
  for (const auto &item : *items) {
    auto fields = decodeNestedFieldList(item);
    if (!fields || !hasTags(*fields, {0x0221, 0x0222}))
      return std::nullopt;
    const std::string *variantBytes = findField(*fields, 0x0221);
    const std::string *post = findField(*fields, 0x0222);
    if (!variantBytes || !post || (*post != "init" && *post != "uninit"))
      return std::nullopt;
    auto variant = decodeNestedFieldList(*variantBytes);
    if (!variant || !hasTags(*variant, {0x0231, 0x0232, 0x0233}))
      return std::nullopt;
    const std::string *variantEnum = findField(*variant, 0x0231);
    const std::string *name = findField(*variant, 0x0232);
    const std::string *ordinalBytes = findField(*variant, 0x0233);
    if (!variantEnum || !name || !ordinalBytes || *variantEnum != enumIdentity ||
        name->empty() || !isValidUtf8(*name))
      return std::nullopt;
    auto ordinal = decodeU32Field(*ordinalBytes);
    if (!ordinal)
      return std::nullopt;
    cases.push_back({*name, *ordinal, *post == "init"});
  }
  return cases;
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

std::optional<OutcomeDeclarationWitnessInput>
CanonicalDeclarationWitnessDecoder::decodeOutcomeTransition(
    const std::string &bytes) {
  ByteReader reader(bytes);
  std::string magic;
  if (!reader.readBytes(sizeof(kMagic) - 1, magic) ||
      magic != std::string(kMagic, sizeof(kMagic) - 1))
    return std::nullopt;
  uint16_t version = 0;
  uint32_t recordCount = 0;
  uint32_t recordLength = 0;
  std::string recordBytes;
  if (!reader.readU16BE(version) || version != 1 ||
      !reader.readU32BE(recordCount) || recordCount != 1 ||
      !reader.readU32BE(recordLength) ||
      !reader.readBytes(recordLength, recordBytes) || !reader.atEnd())
    return std::nullopt;

  auto record = decodeNestedFieldList(recordBytes);
  if (!record || !hasTags(*record, {0x0001, 0x0002, 0x0003, 0x0004, 0x0005}))
    return std::nullopt;
  const std::string *kind = findField(*record, 0x0001);
  const std::string *criticality = findField(*record, 0x0002);
  const std::string *trustClass = findField(*record, 0x0003);
  const std::string *subjectBytes = findField(*record, 0x0004);
  const std::string *payloadBytes = findField(*record, 0x0005);
  if (!kind || !criticality || !trustClass || !subjectBytes || !payloadBytes ||
      *kind != "outcome-transition" || *criticality != "SafetyRequired" ||
      *trustClass != "RecomputedDeclarationFact")
    return std::nullopt;

  auto subject = decodeNestedFieldList(*subjectBytes);
  if (!subject ||
      !hasTags(*subject,
               {0x0101, 0x0102, 0x0103, 0x0104, 0x0105, 0x0106, 0x0107,
                0x0108}))
    return std::nullopt;
  const std::string *functionCrateId = findField(*subject, 0x0101);
  const std::string *functionModulePath = findField(*subject, 0x0102);
  const std::string *functionKind = findField(*subject, 0x0103);
  const std::string *functionName = findField(*subject, 0x0104);
  const std::string *functionArityBytes = findField(*subject, 0x0105);
  const std::string *effectKindBytes = findField(*subject, 0x0106);
  const std::string *parametersBytes = findField(*subject, 0x0107);
  const std::string *resultType = findField(*subject, 0x0108);
  if (!functionCrateId || !functionModulePath || !functionKind || !functionName ||
      !functionArityBytes || !effectKindBytes || !parametersBytes || !resultType ||
      *functionKind != "function" || !isValidUtf8(*functionCrateId) ||
      !isValidUtf8(*functionModulePath) || !isValidUtf8(*functionName) ||
      !hasCanonicalTypeIdentity(*resultType))
    return std::nullopt;
  auto functionArity = decodeU32Field(*functionArityBytes);
  auto effectKind = decodeU32Field(*effectKindBytes);
  auto parameters = decodeParameters(*parametersBytes);
  if (!functionArity || !effectKind || !parameters)
    return std::nullopt;

  auto payload = decodeNestedFieldList(*payloadBytes);
  if (!payload || !hasTags(*payload, {0x0201, 0x0202, 0x0203}))
    return std::nullopt;
  const std::string *formalIndexBytes = findField(*payload, 0x0201);
  const std::string *enumIdentityBytes = findField(*payload, 0x0202);
  const std::string *casesBytes = findField(*payload, 0x0203);
  if (!formalIndexBytes || !enumIdentityBytes || !casesBytes)
    return std::nullopt;
  auto formalIndex = decodeU32Field(*formalIndexBytes);
  auto returnEnum = decodeEnumIdentity(*enumIdentityBytes);
  auto cases = decodeCases(*casesBytes, *enumIdentityBytes);
  if (!formalIndex || !returnEnum || !cases)
    return std::nullopt;

  OutcomeDeclarationWitnessInput input;
  input.FunctionCrateId = *functionCrateId;
  input.FunctionLogicalModulePath = *functionModulePath;
  input.FunctionName = *functionName;
  input.FunctionGenericArity = *functionArity;
  input.EffectKind = *effectKind;
  input.Parameters = std::move(*parameters);
  input.CanonicalResultType = *resultType;
  input.OutcomeFormalIndex = *formalIndex;
  input.ReturnEnum = std::move(*returnEnum);
  input.Cases = std::move(*cases);

  auto canonical =
      CanonicalDeclarationWitnessEncoder::encodeOutcomeTransition(input);
  if (!canonical || *canonical != bytes)
    return std::nullopt;
  return input;
}

} // namespace toka
