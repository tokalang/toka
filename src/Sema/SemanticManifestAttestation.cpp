// Copyright (c) 2026 YiZhonghua<zhyi@dpai.com>. All rights reserved.
//
// Licensed under the Apache License, Version 2.0.
#include "toka/SemanticManifestAttestation.h"
#include "toka/CanonicalDeclarationWitness.h"
#include "toka/InterfaceVersion.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Module.h"
#include "llvm/Transforms/Utils/ModuleUtils.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/RandomNumberGenerator.h"
#include "llvm/Support/SHA256.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <initializer_list>
#include <iterator>
#include <optional>
#include <set>
#include <sstream>

namespace toka {
namespace {

constexpr llvm::StringLiteral EnvelopeSchema =
    "toka.semantic-manifest-envelope";
constexpr llvm::StringLiteral PayloadSchema =
    "toka.outcome-fulfilment-p2";
constexpr llvm::StringLiteral ProvenanceKind = "local-hmac-v1";
constexpr llvm::StringLiteral KeyFileName =
    "toka-semantic-manifest-p2-local.key";
constexpr llvm::StringLiteral MarkerPrefix = "TOKASMAN2:";

std::optional<std::string> readFile(const std::string &path) {
  std::ifstream input(path, std::ios::binary);
  if (!input)
    return std::nullopt;
  return std::string(std::istreambuf_iterator<char>(input),
                     std::istreambuf_iterator<char>());
}

std::string sha256Raw(llvm::StringRef content) {
  llvm::SHA256 hasher;
  hasher.update(content);
  auto digest = hasher.final();
  return std::string(reinterpret_cast<const char *>(digest.data()),
                     digest.size());
}

std::string hexEncode(llvm::StringRef bytes) {
  std::ostringstream out;
  for (unsigned char byte : bytes)
    out << std::hex << std::setw(2) << std::setfill('0')
        << static_cast<unsigned>(byte);
  return out.str();
}

std::string sha256(llvm::StringRef content) {
  return hexEncode(sha256Raw(content));
}

std::string hmacSHA256(llvm::StringRef key, llvm::StringRef message) {
  constexpr size_t BlockSize = 64;
  std::string normalized(key.str());
  if (normalized.size() > BlockSize)
    normalized = sha256Raw(normalized);
  normalized.resize(BlockSize, '\0');
  std::string inner(BlockSize, '\0');
  std::string outer(BlockSize, '\0');
  for (size_t index = 0; index < BlockSize; ++index) {
    inner[index] = static_cast<char>(normalized[index] ^ 0x36);
    outer[index] = static_cast<char>(normalized[index] ^ 0x5c);
  }
  inner.append(message.data(), message.size());
  const std::string innerDigest = sha256Raw(inner);
  outer.append(innerDigest);
  return sha256(outer);
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
    reason = "manifest has no Outcome records";
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

  if (requireCanonicalOrder && !std::is_sorted(records.begin(), records.end())) {
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
  std::string result = "toka.semantic-manifest-p2-payload-v1\0";
  appendU32(result, static_cast<uint32_t>(records.size()));
  for (const std::string &record : records)
    if (!appendBytes(result, record))
      return std::nullopt;
  return result;
}

std::string markerForPayload(llvm::StringRef payloadDigest) {
  return MarkerPrefix.str() + payloadDigest.str();
}

std::string attestationMessage(
    const SemanticManifestAttestationPrepared &prepared,
    llvm::StringRef objectDigest) {
  std::string result = "toka.semantic-manifest-p2-attestation-v1\0";
  const auto &input = prepared.Input;
  (void)appendBytes(result, input.TargetTriple);
  (void)appendBytes(result, input.Module.CrateId);
  (void)appendBytes(result, input.Module.LogicalModulePath);
  (void)appendBytes(result, prepared.InterfaceDigest);
  (void)appendBytes(result, input.SemanticDependencyClosureDigest);
  (void)appendBytes(result, prepared.PayloadDigest);
  (void)appendBytes(result, objectDigest);
  (void)appendBytes(result, prepared.ObjectMarker);
  return result;
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

std::optional<std::string> provenanceKey(const std::string &directory,
                                         bool create, std::string &reason) {
  std::filesystem::path state(directory);
  if (directory.empty() || !state.is_absolute()) {
    reason = "semantic manifest provenance directory must be absolute";
    return std::nullopt;
  }
  const std::filesystem::path keyPath = state / KeyFileName.str();
  std::error_code ec;
  if (!std::filesystem::exists(keyPath, ec)) {
    if (!create) {
      reason = "semantic manifest local provenance key is missing";
      return std::nullopt;
    }
    std::filesystem::create_directories(state, ec);
    if (ec) {
      reason = "could not create semantic manifest provenance directory";
      return std::nullopt;
    }
    std::string key(32, '\0');
    if (std::error_code randomError = llvm::getRandomBytes(key.data(), key.size())) {
      reason = "could not create semantic manifest provenance key: " +
               randomError.message();
      return std::nullopt;
    }
    const std::filesystem::path temporary = keyPath.string() + ".tmp";
    {
      std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
      if (!output) {
        reason = "could not create semantic manifest provenance key";
        return std::nullopt;
      }
      output.write(key.data(), static_cast<std::streamsize>(key.size()));
      if (!output) {
        std::filesystem::remove(temporary, ec);
        reason = "could not write semantic manifest provenance key";
        return std::nullopt;
      }
    }
    std::filesystem::permissions(
        temporary, std::filesystem::perms::owner_read |
                       std::filesystem::perms::owner_write,
        std::filesystem::perm_options::replace, ec);
    if (ec) {
      std::filesystem::remove(temporary, ec);
      reason = "could not protect semantic manifest provenance key";
      return std::nullopt;
    }
    std::filesystem::rename(temporary, keyPath, ec);
    if (ec) {
      std::filesystem::remove(temporary, ec);
      reason = "could not publish semantic manifest provenance key";
      return std::nullopt;
    }
  }

  std::filesystem::file_status status = std::filesystem::status(keyPath, ec);
  if (ec || !std::filesystem::is_regular_file(status)) {
    reason = "semantic manifest provenance key is not a regular file";
    return std::nullopt;
  }
#ifndef _WIN32
  const auto permissions = status.permissions();
  const auto exposed = permissions & (std::filesystem::perms::group_read |
                                      std::filesystem::perms::group_write |
                                      std::filesystem::perms::group_exec |
                                      std::filesystem::perms::others_read |
                                      std::filesystem::perms::others_write |
                                      std::filesystem::perms::others_exec);
  if (exposed != std::filesystem::perms::none) {
    reason = "semantic manifest provenance key is not owner-only";
    return std::nullopt;
  }
#endif
  auto key = readFile(keyPath.string());
  if (!key || key->size() != 32) {
    reason = "semantic manifest provenance key is invalid";
    return std::nullopt;
  }
  return key;
}

std::string canonicalDocument(
    const SemanticManifestAttestationPrepared &prepared,
    llvm::StringRef objectDigest, llvm::StringRef keyId,
    llvm::StringRef signature) {
  const auto &input = prepared.Input;
  std::ostringstream out;
  out << "{\"schema\":\"" << EnvelopeSchema.str() << "\",\"version\":"
      << SemanticManifestAttestation::SchemaVersion
      << ",\"payload_schema\":\"" << PayloadSchema.str()
      << "\",\"compiler_version\":\"" << TOKA_COMPILER_INTERFACE_VERSION
      << "\",\"interface_format\":\"" << TOKA_INTERFACE_FORMAT_VERSION
      << "\",\"target_triple\":\"" << escapeJSON(input.TargetTriple)
      << "\",\"module\":{\"crate_id\":\""
      << escapeJSON(input.Module.CrateId)
      << "\",\"logical_module_path\":\""
      << escapeJSON(input.Module.LogicalModulePath)
      << "\"},\"interface_sha256\":\"" << prepared.InterfaceDigest
      << "\",\"semantic_dependency_closure_sha256\":\""
      << input.SemanticDependencyClosureDigest
      << "\",\"payload_sha256\":\"" << prepared.PayloadDigest
      << "\",\"object\":{\"sha256\":\"" << objectDigest.str()
      << "\",\"marker\":\"" << prepared.ObjectMarker
      << "\",\"provenance\":{\"kind\":\"" << ProvenanceKind.str()
      << "\",\"key_id\":\"" << keyId.str()
      << "\",\"attestation_hmac_sha256\":\"" << signature.str()
      << "\"}},\"records\":[";
  for (size_t index = 0; index < prepared.CDW1Records.size(); ++index) {
    if (index)
      out << ',';
    const std::string hex =
        CanonicalDeclarationWitnessEncoder::hexEncode(prepared.CDW1Records[index]);
    out << "{\"kind\":\"outcome-transition\",\"criticality\":\"SafetyRequired\""
        << ",\"trust_class\":\"RecomputedDeclarationFact\",\"cdw1\":\""
        << hex << "\"},"
        << "{\"kind\":\"outcome-fulfilment\",\"criticality\":\"SafetyRequired\""
        << ",\"trust_class\":\"AcceptedProvenanceObjectFact\",\"cdw1\":\""
        << hex << "\"}";
  }
  out << "]}\n";
  return out.str();
}

SemanticManifestAttestationStatus fail(
    SemanticManifestAttestationStatus status, const std::string &message,
    std::string &reason) {
  reason = message;
  return status;
}

SemanticManifestAttestationStatus loadImpl(
    const std::string &path, const std::string &interfaceContent,
    const SemanticManifestCoordinate &expectedModule,
    const std::string &expectedTargetTriple,
    const std::string &expectedSemanticDependencyClosureDigest,
    const std::string &objectPath, const std::string &provenanceStateDirectory,
    SemanticManifestAttestationResult &result, std::string &reason) {
  result = {};
  auto content = readFile(path);
  if (!content)
    return fail(SemanticManifestAttestationStatus::Missing,
                "semantic manifest attestation is missing", reason);
  llvm::Expected<llvm::json::Value> parsed = llvm::json::parse(*content);
  if (!parsed) {
    llvm::consumeError(parsed.takeError());
    return fail(SemanticManifestAttestationStatus::ReadError,
                "semantic manifest attestation is not valid JSON", reason);
  }
  llvm::json::Object *document = parsed->getAsObject();
  if (!document ||
      !hasExactFields(*document, {"schema", "version", "payload_schema",
                                  "compiler_version", "interface_format",
                                  "target_triple", "module", "interface_sha256",
                                  "semantic_dependency_closure_sha256",
                                  "payload_sha256", "object", "records"}))
    return fail(SemanticManifestAttestationStatus::InvalidSchema,
                "semantic manifest attestation has an invalid field set", reason);

  const auto schema = document->getString("schema");
  const auto version = document->getInteger("version");
  const auto payloadSchema = document->getString("payload_schema");
  if (!schema || *schema != EnvelopeSchema || !version ||
      *version != SemanticManifestAttestation::SchemaVersion || !payloadSchema ||
      *payloadSchema != PayloadSchema)
    return fail(SemanticManifestAttestationStatus::InvalidSchema,
                "semantic manifest attestation schema is unsupported", reason);

  llvm::json::Object *module = document->getObject("module");
  llvm::json::Object *object = document->getObject("object");
  if (!module || !object ||
      !hasExactFields(*module, {"crate_id", "logical_module_path"}) ||
      !hasExactFields(*object, {"sha256", "marker", "provenance"}))
    return fail(SemanticManifestAttestationStatus::InvalidSchema,
                "semantic manifest attestation identity is malformed", reason);
  const auto crateId = module->getString("crate_id");
  const auto logicalModulePath = module->getString("logical_module_path");
  const auto compilerVersion = document->getString("compiler_version");
  const auto interfaceFormat = document->getString("interface_format");
  const auto targetTriple = document->getString("target_triple");
  const auto interfaceDigest = document->getString("interface_sha256");
  const auto closureDigest =
      document->getString("semantic_dependency_closure_sha256");
  const auto payloadDigest = document->getString("payload_sha256");
  const auto objectDigest = object->getString("sha256");
  const auto marker = object->getString("marker");
  if (!crateId || !logicalModulePath || !compilerVersion || !interfaceFormat ||
      !targetTriple || !interfaceDigest || !closureDigest || !payloadDigest ||
      !objectDigest || !marker || !validText(*crateId) ||
      !validText(*logicalModulePath) || !validText(*targetTriple) ||
      !isLowerSHA256(*interfaceDigest) || !isLowerSHA256(*closureDigest) ||
      !isLowerSHA256(*payloadDigest) || !isLowerSHA256(*objectDigest) ||
      *marker != markerForPayload(*payloadDigest))
    return fail(SemanticManifestAttestationStatus::InvalidSchema,
                "semantic manifest attestation identity is malformed", reason);

  if (*compilerVersion != TOKA_COMPILER_INTERFACE_VERSION ||
      *interfaceFormat != TOKA_INTERFACE_FORMAT_VERSION ||
      *targetTriple != expectedTargetTriple ||
      *crateId != expectedModule.CrateId ||
      *logicalModulePath != expectedModule.LogicalModulePath ||
      *interfaceDigest != sha256(interfaceContent) ||
      (!expectedSemanticDependencyClosureDigest.empty() &&
       *closureDigest != expectedSemanticDependencyClosureDigest))
    return fail(SemanticManifestAttestationStatus::IdentityMismatch,
                "semantic manifest attestation identity does not match interface",
                reason);

  auto objectBytes = readFile(objectPath);
  if (!objectBytes)
    return fail(SemanticManifestAttestationStatus::MissingObject,
                "attested backing object is missing", reason);
  if (*objectDigest != sha256(*objectBytes))
    return fail(SemanticManifestAttestationStatus::ObjectMismatch,
                "attested backing object digest does not match", reason);
  if (objectBytes->find(marker->str()) == std::string::npos)
    return fail(SemanticManifestAttestationStatus::MarkerMismatch,
                "attestation marker is absent from backing object", reason);

  llvm::json::Object *provenance = object->getObject("provenance");
  if (!provenance || !hasExactFields(*provenance,
                                     {"kind", "key_id",
                                      "attestation_hmac_sha256"}))
    return fail(SemanticManifestAttestationStatus::InvalidSchema,
                "semantic manifest provenance is malformed", reason);
  const auto provenanceKind = provenance->getString("kind");
  const auto keyId = provenance->getString("key_id");
  const auto signature = provenance->getString("attestation_hmac_sha256");
  if (!provenanceKind || *provenanceKind != ProvenanceKind || !keyId ||
      !signature || !isLowerSHA256(*keyId) || !isLowerSHA256(*signature))
    return fail(SemanticManifestAttestationStatus::InvalidSchema,
                "semantic manifest provenance classification is invalid", reason);

  llvm::json::Array *encodedRecords = document->getArray("records");
  if (!encodedRecords || encodedRecords->empty() ||
      encodedRecords->size() % 2 != 0)
    return fail(SemanticManifestAttestationStatus::InvalidRecord,
                "semantic manifest attestation records are malformed", reason);
  std::vector<std::string> decodedRecords;
  decodedRecords.reserve(encodedRecords->size() / 2);
  for (size_t index = 0; index < encodedRecords->size(); index += 2) {
    llvm::json::Object *declaration = (*encodedRecords)[index].getAsObject();
    llvm::json::Object *fulfilment =
        (*encodedRecords)[index + 1].getAsObject();
    const auto validRecord = [](llvm::json::Object *record,
                                llvm::StringRef kind,
                                llvm::StringRef trust) {
      if (!record || !hasExactFields(*record,
                                     {"kind", "criticality", "trust_class",
                                      "cdw1"}))
        return std::optional<llvm::StringRef>();
      const auto actualKind = record->getString("kind");
      const auto criticality = record->getString("criticality");
      const auto trustClass = record->getString("trust_class");
      const auto recordHex = record->getString("cdw1");
      if (!actualKind || *actualKind != kind || !criticality ||
          *criticality != "SafetyRequired" || !trustClass ||
          *trustClass != trust || !recordHex)
        return std::optional<llvm::StringRef>();
      return recordHex;
    };
    const auto declarationHex = validRecord(
        declaration, "outcome-transition", "RecomputedDeclarationFact");
    const auto fulfilmentHex = validRecord(
        fulfilment, "outcome-fulfilment", "AcceptedProvenanceObjectFact");
    if (!declarationHex || !fulfilmentHex || *declarationHex != *fulfilmentHex)
      return fail(SemanticManifestAttestationStatus::InvalidRecord,
                  "semantic manifest attestation record pair is invalid", reason);
    auto rawRecord = decodeLowerHex(*declarationHex);
    if (!rawRecord)
      return fail(SemanticManifestAttestationStatus::InvalidRecord,
                  "semantic manifest attestation record is not lowercase hexadecimal",
                  reason);
    decodedRecords.push_back(std::move(*rawRecord));
  }

  SemanticManifestCoordinate actualModule = {crateId->str(),
                                             logicalModulePath->str()};
  std::vector<std::string> normalizedRecords;
  std::string recordReason;
  if (!validateRecords(decodedRecords, actualModule, true, normalizedRecords,
                       recordReason))
    return fail(SemanticManifestAttestationStatus::InvalidRecord, recordReason,
                reason);
  const auto payload = canonicalPayload(normalizedRecords);
  if (!payload || *payloadDigest != sha256(*payload))
    return fail(SemanticManifestAttestationStatus::PayloadMismatch,
                "semantic manifest attestation payload does not match records",
                reason);

  SemanticManifestAttestationPrepared prepared;
  prepared.Input.TargetTriple = targetTriple->str();
  prepared.Input.Module = actualModule;
  prepared.Input.InterfaceContent = interfaceContent;
  prepared.Input.SemanticDependencyClosureDigest = closureDigest->str();
  prepared.CDW1Records = normalizedRecords;
  prepared.InterfaceDigest = interfaceDigest->str();
  prepared.PayloadDigest = payloadDigest->str();
  prepared.ObjectMarker = marker->str();

  std::string keyReason;
  const auto key = provenanceKey(provenanceStateDirectory, false, keyReason);
  if (!key)
    return fail(SemanticManifestAttestationStatus::ProvenanceMismatch,
                keyReason, reason);
  if (*keyId != sha256(*key) ||
      *signature != hmacSHA256(*key, attestationMessage(prepared, *objectDigest)))
    return fail(SemanticManifestAttestationStatus::ProvenanceMismatch,
                "semantic manifest provenance does not attest this object", reason);

  const std::string canonical =
      canonicalDocument(prepared, *objectDigest, *keyId, *signature);
  if (*content != canonical)
    return fail(SemanticManifestAttestationStatus::InvalidSchema,
                "semantic manifest attestation bytes are not canonical", reason);

  result.CDW1Records = std::move(normalizedRecords);
  result.SemanticDependencyClosureDigest = closureDigest->str();
  result.ObjectDigest = objectDigest->str();
  result.ObjectMarker = marker->str();
  reason = "validated as an object-bound Outcome fulfilment attestation";
  return SemanticManifestAttestationStatus::Valid;
}

} // namespace

bool SemanticManifestAttestation::prepare(
    const SemanticManifestAttestationInput &input,
    SemanticManifestAttestationPrepared &prepared,
    std::vector<std::string> &errors) {
  prepared = {};
  if (!validText(input.TargetTriple)) {
    errors.push_back("semantic manifest attestation target triple is incomplete");
    return false;
  }
  if (!isLowerSHA256(input.SemanticDependencyClosureDigest)) {
    errors.push_back(
        "semantic manifest attestation dependency closure digest is invalid");
    return false;
  }
  std::string reason;
  std::vector<std::string> records;
  if (!validateRecords(input.CDW1Records, input.Module, false, records,
                       reason)) {
    errors.push_back(reason);
    return false;
  }
  const auto payload = canonicalPayload(records);
  if (!payload) {
    errors.push_back("semantic manifest attestation payload is too large");
    return false;
  }
  prepared.Input = input;
  prepared.CDW1Records = std::move(records);
  prepared.InterfaceDigest = sha256(input.InterfaceContent);
  prepared.PayloadDigest = sha256(*payload);
  prepared.ObjectMarker = markerForPayload(prepared.PayloadDigest);
  return true;
}

bool SemanticManifestAttestation::bindObjectMarker(
    llvm::Module &irModule, const SemanticManifestAttestationPrepared &prepared,
    std::vector<std::string> &errors) {
  if (prepared.ObjectMarker.empty() ||
      prepared.ObjectMarker != markerForPayload(prepared.PayloadDigest)) {
    errors.push_back("semantic manifest attestation marker is invalid");
    return false;
  }
  llvm::Constant *data = llvm::ConstantDataArray::getString(
      irModule.getContext(), prepared.ObjectMarker, true);
  auto *global = new llvm::GlobalVariable(
      irModule, data->getType(), true, llvm::GlobalValue::PrivateLinkage, data,
      "__toka_semantic_manifest_p2_" + prepared.PayloadDigest.substr(0, 16));
  global->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);
  global->setAlignment(llvm::Align(1));
  llvm::appendToUsed(irModule, {global});
  return true;
}

bool SemanticManifestAttestation::write(
    const std::string &path, const std::string &objectPath,
    const SemanticManifestAttestationPrepared &prepared,
    const std::string &provenanceStateDirectory,
    std::vector<std::string> &errors) {
  auto object = readFile(objectPath);
  if (!object) {
    errors.push_back("could not read attested backing object " + objectPath);
    return false;
  }
  std::string keyReason;
  const auto key = provenanceKey(provenanceStateDirectory, true, keyReason);
  if (!key) {
    errors.push_back(keyReason);
    return false;
  }
  const std::string objectDigest = sha256(*object);
  const std::string keyId = sha256(*key);
  const std::string signature =
      hmacSHA256(*key, attestationMessage(prepared, objectDigest));
  const std::string document =
      canonicalDocument(prepared, objectDigest, keyId, signature);

  const std::string temporary = path + ".tmp";
  std::error_code error;
  llvm::raw_fd_ostream out(temporary, error, llvm::sys::fs::OF_Text);
  if (error) {
    errors.push_back("could not create semantic manifest attestation " +
                     temporary + ": " + error.message());
    return false;
  }
  out << document;
  out.close();
  if (out.has_error()) {
    errors.push_back("failed writing semantic manifest attestation " + temporary);
    llvm::sys::fs::remove(temporary);
    return false;
  }
  if (std::error_code renameError = llvm::sys::fs::rename(temporary, path)) {
    errors.push_back("could not publish semantic manifest attestation " + path +
                     ": " + renameError.message());
    llvm::sys::fs::remove(temporary);
    return false;
  }
  return true;
}

SemanticManifestAttestationStatus SemanticManifestAttestation::loadCandidate(
    const std::string &path, const std::string &interfaceContent,
    const SemanticManifestCoordinate &expectedModule,
    const std::string &expectedTargetTriple, const std::string &objectPath,
    const std::string &provenanceStateDirectory,
    SemanticManifestAttestationResult &result, std::string &reason) {
  return loadImpl(path, interfaceContent, expectedModule, expectedTargetTriple,
                  "", objectPath, provenanceStateDirectory, result, reason);
}

SemanticManifestAttestationStatus SemanticManifestAttestation::load(
    const std::string &path, const std::string &interfaceContent,
    const SemanticManifestCoordinate &expectedModule,
    const std::string &expectedTargetTriple,
    const std::string &expectedSemanticDependencyClosureDigest,
    const std::string &objectPath, const std::string &provenanceStateDirectory,
    SemanticManifestAttestationResult &result, std::string &reason) {
  return loadImpl(path, interfaceContent, expectedModule, expectedTargetTriple,
                  expectedSemanticDependencyClosureDigest, objectPath,
                  provenanceStateDirectory, result, reason);
}

const char *toString(SemanticManifestAttestationStatus status) {
  switch (status) {
  case SemanticManifestAttestationStatus::Missing: return "Missing";
  case SemanticManifestAttestationStatus::ReadError: return "ReadError";
  case SemanticManifestAttestationStatus::InvalidSchema: return "InvalidSchema";
  case SemanticManifestAttestationStatus::IdentityMismatch:
    return "IdentityMismatch";
  case SemanticManifestAttestationStatus::InvalidRecord: return "InvalidRecord";
  case SemanticManifestAttestationStatus::PayloadMismatch:
    return "PayloadMismatch";
  case SemanticManifestAttestationStatus::MissingObject: return "MissingObject";
  case SemanticManifestAttestationStatus::ObjectMismatch: return "ObjectMismatch";
  case SemanticManifestAttestationStatus::MarkerMismatch: return "MarkerMismatch";
  case SemanticManifestAttestationStatus::ProvenanceMismatch:
    return "ProvenanceMismatch";
  case SemanticManifestAttestationStatus::Valid: return "Valid";
  }
  return "InvalidSchema";
}

} // namespace toka
