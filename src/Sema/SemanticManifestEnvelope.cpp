// Copyright (c) 2026 YiZhonghua<zhyi@dpai.com>. All rights reserved.
//
// Licensed under the Apache License, Version 2.0.
#include "toka/SemanticManifestEnvelope.h"
#include "toka/CanonicalDeclarationWitness.h"
#include "toka/InterfaceVersion.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/SHA256.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cstdint>
#include <fstream>
#include <initializer_list>
#include <iomanip>
#include <iterator>
#include <optional>
#include <set>
#include <sstream>

namespace toka {
namespace {

constexpr llvm::StringLiteral EnvelopeSchema =
    "toka.semantic-manifest-envelope";
constexpr llvm::StringLiteral PayloadSchema = "toka.cdw1-recomputed-v1";

std::optional<std::string> readFile(const std::string &path) {
  std::ifstream input(path, std::ios::binary);
  if (!input)
    return std::nullopt;
  return std::string(std::istreambuf_iterator<char>(input),
                     std::istreambuf_iterator<char>());
}

std::string sha256(const std::string &content) {
  llvm::SHA256 hasher;
  hasher.update(llvm::StringRef(content));
  std::ostringstream out;
  for (uint8_t byte : hasher.final())
    out << std::hex << std::setw(2) << std::setfill('0')
        << static_cast<unsigned>(byte);
  return out.str();
}

bool isLowerSHA256(llvm::StringRef value) {
  if (value.size() != 64)
    return false;
  for (char character : value) {
    if (!((character >= '0' && character <= '9') ||
          (character >= 'a' && character <= 'f')))
      return false;
  }
  return true;
}

bool hasJSONControlCharacter(llvm::StringRef value) {
  for (unsigned char character : value)
    if (character < 0x20)
      return true;
  return false;
}

bool validText(llvm::StringRef value) {
  return !value.empty() && !hasJSONControlCharacter(value);
}

std::string escapeJSON(llvm::StringRef value) {
  std::string result;
  result.reserve(value.size());
  for (char character : value) {
    if (character == '\\')
      result += "\\\\";
    else if (character == '"')
      result += "\\\"";
    else
      result += character;
  }
  return result;
}

void appendU32(std::string &out, uint32_t value) {
  out.push_back(static_cast<char>((value >> 24) & 0xff));
  out.push_back(static_cast<char>((value >> 16) & 0xff));
  out.push_back(static_cast<char>((value >> 8) & 0xff));
  out.push_back(static_cast<char>(value & 0xff));
}

bool appendBytes(std::string &out, llvm::StringRef value) {
  if (value.size() > UINT32_MAX)
    return false;
  appendU32(out, static_cast<uint32_t>(value.size()));
  out.append(value.data(), value.size());
  return true;
}

std::optional<unsigned> hexNibble(char value) {
  if (value >= '0' && value <= '9')
    return value - '0';
  if (value >= 'a' && value <= 'f')
    return value - 'a' + 10;
  return std::nullopt;
}

std::optional<std::string> decodeLowerHex(llvm::StringRef hex) {
  if (hex.empty() || hex.size() % 2 != 0)
    return std::nullopt;
  std::string bytes;
  bytes.reserve(hex.size() / 2);
  for (size_t index = 0; index < hex.size(); index += 2) {
    auto high = hexNibble(hex[index]);
    auto low = hexNibble(hex[index + 1]);
    if (!high || !low)
      return std::nullopt;
    bytes.push_back(static_cast<char>((*high << 4) | *low));
  }
  return bytes;
}

bool appendWitnessSubject(std::string &out,
                          const OutcomeDeclarationWitnessInput &input) {
  if (!appendBytes(out, input.FunctionCrateId) ||
      !appendBytes(out, input.FunctionLogicalModulePath) ||
      !appendBytes(out, input.FunctionName))
    return false;
  appendU32(out, input.FunctionGenericArity);
  appendU32(out, input.EffectKind);
  if (input.Parameters.size() > UINT32_MAX)
    return false;
  appendU32(out, static_cast<uint32_t>(input.Parameters.size()));
  for (const auto &parameter : input.Parameters) {
    appendU32(out, parameter.Index);
    out.push_back(parameter.IsInit ? '\1' : '\0');
    out.push_back(parameter.IsCeded ? '\1' : '\0');
    if (!appendBytes(out, parameter.CanonicalPhysicalType))
      return false;
  }
  return appendBytes(out, input.CanonicalResultType);
}

bool validateRecords(const std::vector<std::string> &source,
                     const SemanticManifestCoordinate &coordinate,
                     bool requireCanonicalOrder,
                     std::vector<std::string> &records, std::string &reason) {
  if (!validText(coordinate.CrateId) ||
      !validText(coordinate.LogicalModulePath)) {
    reason = "manifest module coordinate is incomplete";
    return false;
  }
  if (source.empty()) {
    reason = "manifest has no CDW1 records";
    return false;
  }

  records.clear();
  records.reserve(source.size());
  std::set<std::string> subjects;
  for (const std::string &record : source) {
    auto decoded =
        CanonicalDeclarationWitnessDecoder::decodeOutcomeTransition(record);
    if (!decoded) {
      reason = "manifest contains an invalid CDW1 record";
      return false;
    }
    auto recanonical =
        CanonicalDeclarationWitnessEncoder::encodeOutcomeTransition(*decoded);
    if (!recanonical || *recanonical != record) {
      reason = "manifest contains a non-canonical CDW1 record";
      return false;
    }
    if (decoded->FunctionCrateId != coordinate.CrateId ||
        decoded->FunctionLogicalModulePath != coordinate.LogicalModulePath) {
      reason = "CDW1 record coordinate does not match manifest module";
      return false;
    }
    std::string subject;
    if (!appendWitnessSubject(subject, *decoded)) {
      reason = "CDW1 subject is too large";
      return false;
    }
    if (!subjects.insert(std::move(subject)).second) {
      reason = "manifest contains duplicate CDW1 subjects";
      return false;
    }
    records.push_back(record);
  }

  if (requireCanonicalOrder &&
      !std::is_sorted(records.begin(), records.end())) {
    reason = "manifest CDW1 records are not in canonical order";
    return false;
  }
  if (!requireCanonicalOrder)
    std::sort(records.begin(), records.end());
  return true;
}

std::optional<std::string>
canonicalPayload(const std::vector<std::string> &records) {
  if (records.size() > UINT32_MAX)
    return std::nullopt;
  std::string result = "toka.semantic-manifest-payload-v1\0";
  appendU32(result, static_cast<uint32_t>(records.size()));
  for (const std::string &record : records)
    if (!appendBytes(result, record))
      return std::nullopt;
  return result;
}

std::string canonicalDocument(const SemanticManifestCoordinate &coordinate,
                              llvm::StringRef targetTriple,
                              llvm::StringRef interfaceDigest,
                              llvm::StringRef closureDigest,
                              llvm::StringRef payloadDigest,
                              const std::vector<std::string> &records) {
  std::ostringstream out;
  out << "{\"schema\":\"" << EnvelopeSchema.str()
      << "\",\"version\":" << SemanticManifestEnvelope::SchemaVersion
      << ",\"payload_schema\":\"" << PayloadSchema.str()
      << "\",\"compiler_version\":\"" << TOKA_COMPILER_INTERFACE_VERSION
      << "\",\"interface_format\":\"" << TOKA_INTERFACE_FORMAT_VERSION
      << "\",\"target_triple\":\"" << escapeJSON(targetTriple)
      << "\",\"module\":{\"crate_id\":\"" << escapeJSON(coordinate.CrateId)
      << "\",\"logical_module_path\":\""
      << escapeJSON(coordinate.LogicalModulePath)
      << "\"},\"interface_sha256\":\"" << interfaceDigest.str()
      << "\",\"semantic_dependency_closure_sha256\":\"" << closureDigest.str()
      << "\",\"payload_sha256\":\"" << payloadDigest.str()
      << "\",\"records\":[";
  for (size_t index = 0; index < records.size(); ++index) {
    if (index)
      out << ',';
    out << "{\"kind\":\"outcome-transition\",\"criticality\":\"SafetyRequired\""
        << ",\"trust_class\":\"RecomputedDeclarationFact\",\"cdw1\":\""
        << CanonicalDeclarationWitnessEncoder::hexEncode(records[index])
        << "\"}";
  }
  out << "]}\n";
  return out.str();
}

bool hasExactFields(const llvm::json::Object &object,
                    std::initializer_list<llvm::StringRef> names) {
  if (object.size() != names.size())
    return false;
  for (llvm::StringRef name : names)
    if (!object.get(name))
      return false;
  return true;
}

SemanticManifestEnvelopeStatus fail(SemanticManifestEnvelopeStatus status,
                                    const std::string &message,
                                    std::string &reason) {
  reason = message;
  return status;
}

} // namespace

std::string
SemanticManifestEnvelope::sidecarPath(const std::string &interfacePath) {
  return interfacePath + ".tsm";
}

bool SemanticManifestEnvelope::write(const std::string &path,
                                     const SemanticManifestEnvelopeInput &input,
                                     std::vector<std::string> &errors) {
  if (!validText(input.TargetTriple)) {
    errors.push_back("semantic manifest target triple is incomplete");
    return false;
  }
  if (!isLowerSHA256(input.SemanticDependencyClosureDigest)) {
    errors.push_back("semantic manifest dependency closure digest is invalid");
    return false;
  }

  std::vector<std::string> records;
  std::string reason;
  if (!validateRecords(input.CDW1Records, input.Module, false, records,
                       reason)) {
    errors.push_back(reason);
    return false;
  }
  auto payload = canonicalPayload(records);
  if (!payload) {
    errors.push_back("semantic manifest payload is too large");
    return false;
  }

  std::string document = canonicalDocument(
      input.Module, input.TargetTriple, sha256(input.InterfaceContent),
      input.SemanticDependencyClosureDigest, sha256(*payload), records);
  std::string temporary = path + ".tmp";
  std::error_code error;
  llvm::raw_fd_ostream out(temporary, error, llvm::sys::fs::OF_Text);
  if (error) {
    errors.push_back("could not create semantic manifest " + temporary + ": " +
                     error.message());
    return false;
  }
  out << document;
  out.close();
  if (out.has_error()) {
    errors.push_back("failed writing semantic manifest " + temporary);
    llvm::sys::fs::remove(temporary);
    return false;
  }
  if (std::error_code renameError = llvm::sys::fs::rename(temporary, path)) {
    errors.push_back("could not publish semantic manifest " + path + ": " +
                     renameError.message());
    llvm::sys::fs::remove(temporary);
    return false;
  }
  return true;
}

SemanticManifestEnvelopeStatus SemanticManifestEnvelope::load(
    const std::string &path, const std::string &interfaceContent,
    const SemanticManifestCoordinate &expectedModule,
    const std::string &expectedTargetTriple,
    const std::string &expectedSemanticDependencyClosureDigest,
    std::vector<std::string> &records,
    std::string &semanticDependencyClosureDigest, std::string &reason) {
  records.clear();
  semanticDependencyClosureDigest.clear();
  auto content = readFile(path);
  if (!content)
    return fail(SemanticManifestEnvelopeStatus::Missing,
                "semantic manifest is missing", reason);
  llvm::Expected<llvm::json::Value> parsed = llvm::json::parse(*content);
  if (!parsed) {
    llvm::consumeError(parsed.takeError());
    return fail(SemanticManifestEnvelopeStatus::ReadError,
                "semantic manifest is not valid JSON", reason);
  }
  llvm::json::Object *document = parsed->getAsObject();
  if (!document ||
      !hasExactFields(*document, {"schema", "version", "payload_schema",
                                  "compiler_version", "interface_format",
                                  "target_triple", "module", "interface_sha256",
                                  "semantic_dependency_closure_sha256",
                                  "payload_sha256", "records"}))
    return fail(SemanticManifestEnvelopeStatus::InvalidSchema,
                "semantic manifest has an invalid field set", reason);

  std::optional<llvm::StringRef> schema = document->getString("schema");
  std::optional<int64_t> version = document->getInteger("version");
  std::optional<llvm::StringRef> payloadSchema =
      document->getString("payload_schema");
  if (!schema || *schema != EnvelopeSchema || !version ||
      *version != SchemaVersion || !payloadSchema ||
      *payloadSchema != PayloadSchema)
    return fail(SemanticManifestEnvelopeStatus::InvalidSchema,
                "semantic manifest schema is unsupported", reason);

  llvm::json::Object *module = document->getObject("module");
  if (!module || !hasExactFields(*module, {"crate_id", "logical_module_path"}))
    return fail(SemanticManifestEnvelopeStatus::InvalidSchema,
                "semantic manifest module is malformed", reason);
  std::optional<llvm::StringRef> crateId = module->getString("crate_id");
  std::optional<llvm::StringRef> logicalModulePath =
      module->getString("logical_module_path");
  std::optional<llvm::StringRef> compilerVersion =
      document->getString("compiler_version");
  std::optional<llvm::StringRef> interfaceFormat =
      document->getString("interface_format");
  std::optional<llvm::StringRef> targetTriple =
      document->getString("target_triple");
  std::optional<llvm::StringRef> interfaceDigest =
      document->getString("interface_sha256");
  std::optional<llvm::StringRef> closureDigest =
      document->getString("semantic_dependency_closure_sha256");
  std::optional<llvm::StringRef> payloadDigest =
      document->getString("payload_sha256");
  if (!crateId || !logicalModulePath || !compilerVersion || !interfaceFormat ||
      !targetTriple || !interfaceDigest || !closureDigest || !payloadDigest ||
      !validText(*crateId) || !validText(*logicalModulePath) ||
      !validText(*targetTriple) || !isLowerSHA256(*interfaceDigest) ||
      !isLowerSHA256(*closureDigest) || !isLowerSHA256(*payloadDigest))
    return fail(SemanticManifestEnvelopeStatus::InvalidSchema,
                "semantic manifest identity is malformed", reason);

  if (*compilerVersion != TOKA_COMPILER_INTERFACE_VERSION ||
      *interfaceFormat != TOKA_INTERFACE_FORMAT_VERSION ||
      *targetTriple != expectedTargetTriple ||
      *crateId != expectedModule.CrateId ||
      *logicalModulePath != expectedModule.LogicalModulePath ||
      *interfaceDigest != sha256(interfaceContent) ||
      *closureDigest != expectedSemanticDependencyClosureDigest)
    return fail(SemanticManifestEnvelopeStatus::IdentityMismatch,
                "semantic manifest identity does not match interface", reason);

  llvm::json::Array *encodedRecords = document->getArray("records");
  if (!encodedRecords)
    return fail(SemanticManifestEnvelopeStatus::InvalidRecord,
                "semantic manifest records are malformed", reason);
  std::vector<std::string> decodedRecords;
  decodedRecords.reserve(encodedRecords->size());
  for (llvm::json::Value &value : *encodedRecords) {
    llvm::json::Object *record = value.getAsObject();
    if (!record || !hasExactFields(
                       *record, {"kind", "criticality", "trust_class", "cdw1"}))
      return fail(SemanticManifestEnvelopeStatus::InvalidRecord,
                  "semantic manifest record has an invalid field set", reason);
    std::optional<llvm::StringRef> kind = record->getString("kind");
    std::optional<llvm::StringRef> criticality =
        record->getString("criticality");
    std::optional<llvm::StringRef> trustClass =
        record->getString("trust_class");
    std::optional<llvm::StringRef> hex = record->getString("cdw1");
    if (!kind || *kind != "outcome-transition" || !criticality ||
        *criticality != "SafetyRequired" || !trustClass ||
        *trustClass != "RecomputedDeclarationFact" || !hex) {
      return fail(SemanticManifestEnvelopeStatus::InvalidRecord,
                  "semantic manifest record classification is invalid", reason);
    }
    auto rawRecord = decodeLowerHex(*hex);
    if (!rawRecord)
      return fail(SemanticManifestEnvelopeStatus::InvalidRecord,
                  "semantic manifest record is not lowercase hexadecimal",
                  reason);
    decodedRecords.push_back(std::move(*rawRecord));
  }

  SemanticManifestCoordinate actualModule = {crateId->str(),
                                             logicalModulePath->str()};
  std::vector<std::string> normalizedRecords;
  std::string recordReason;
  if (!validateRecords(decodedRecords, actualModule, true, normalizedRecords,
                       recordReason))
    return fail(SemanticManifestEnvelopeStatus::InvalidRecord, recordReason,
                reason);
  auto payload = canonicalPayload(normalizedRecords);
  if (!payload || *payloadDigest != sha256(*payload))
    return fail(SemanticManifestEnvelopeStatus::PayloadMismatch,
                "semantic manifest payload digest does not match records",
                reason);

  std::string canonical =
      canonicalDocument(actualModule, *targetTriple, *interfaceDigest,
                        *closureDigest, *payloadDigest, normalizedRecords);
  if (*content != canonical)
    return fail(SemanticManifestEnvelopeStatus::InvalidSchema,
                "semantic manifest bytes are not canonical", reason);

  records = std::move(normalizedRecords);
  semanticDependencyClosureDigest = closureDigest->str();
  reason = "validated as a declaration-recomputed envelope";
  return SemanticManifestEnvelopeStatus::Valid;
}

const char *toString(SemanticManifestEnvelopeStatus status) {
  switch (status) {
  case SemanticManifestEnvelopeStatus::Missing:
    return "Missing";
  case SemanticManifestEnvelopeStatus::ReadError:
    return "ReadError";
  case SemanticManifestEnvelopeStatus::InvalidSchema:
    return "InvalidSchema";
  case SemanticManifestEnvelopeStatus::IdentityMismatch:
    return "IdentityMismatch";
  case SemanticManifestEnvelopeStatus::InvalidRecord:
    return "InvalidRecord";
  case SemanticManifestEnvelopeStatus::PayloadMismatch:
    return "PayloadMismatch";
  case SemanticManifestEnvelopeStatus::Valid:
    return "Valid";
  }
  return "InvalidSchema";
}

} // namespace toka
