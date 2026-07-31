// Copyright (c) 2026 YiZhonghua<zhyi@dpai.com>. All rights reserved.
//
// Licensed under the Apache License, Version 2.0.
#include "toka/MemoryEvidence.h"
#include "toka/AST.h"
#include "toka/InterfaceVersion.h"
#include "toka/MemorySummary.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/SHA256.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/ModuleUtils.h"
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <optional>
#include <set>
#include <sstream>

namespace toka {
namespace {

constexpr uint32_t RootEffectMask = (1u << 8) - 1;
constexpr uint32_t FunctionEffectMask = (1u << 8) - 1;

std::string calculateFNV1a(const std::string &value) {
  uint64_t hash = 14695981039346656037ULL;
  for (unsigned char ch : value) {
    hash ^= ch;
    hash *= 1099511628211ULL;
  }
  char buffer[17];
  std::snprintf(buffer, sizeof(buffer), "%016llx",
                static_cast<unsigned long long>(hash));
  return buffer;
}

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

std::string escapeJSON(const std::string &value) {
  std::string result;
  for (unsigned char ch : value) {
    switch (ch) {
    case '\\': result += "\\\\"; break;
    case '"': result += "\\\""; break;
    case '\n': result += "\\n"; break;
    case '\r': result += "\\r"; break;
    case '\t': result += "\\t"; break;
    default:
      if (ch >= 0x20)
        result += static_cast<char>(ch);
      break;
    }
  }
  return result;
}

bool sameSummary(const FunctionMemorySummary &lhs,
                 const FunctionMemorySummary &rhs) {
  if (lhs.FunctionName != rhs.FunctionName ||
      lhs.LocalEffects != rhs.LocalEffects || lhs.Effects != rhs.Effects ||
      lhs.Roots.size() != rhs.Roots.size())
    return false;
  auto left = lhs.Roots.begin();
  auto right = rhs.Roots.begin();
  while (left != lhs.Roots.end()) {
    if (left->first != right->first ||
        left->second.LocalEffects != right->second.LocalEffects ||
        left->second.Effects != right->second.Effects)
      return false;
    ++left;
    ++right;
  }
  return true;
}

std::optional<std::map<std::string, FunctionMemorySummary>>
collectEvidenceSummaries(const Module &module,
                         std::vector<std::string> &errors) {
  std::map<std::string, FunctionMemorySummary> summaries;
  std::set<std::string> ambiguousNames;
  std::set<const FunctionDecl *> policyDropHooks;
  for (const auto &impl : module.Impls) {
    if (impl->TraitName != "encap" && impl->TraitName != "@encap")
      continue;
    for (const auto &method : impl->Methods)
      if (method->Name == "drop")
        policyDropHooks.insert(method.get());
  }
  std::vector<Module *> modules = {const_cast<Module *>(&module)};
  for (FunctionDecl *function : MemorySummaryAnalysis::collectFunctions(modules)) {
    if (policyDropHooks.count(function))
      continue;
    const FunctionMemorySummary &summary = function->MemorySummary;
    if (summary.Origin != MemorySummaryOrigin::SourceBody ||
        summary.FunctionName.empty() ||
        ambiguousNames.count(summary.FunctionName) != 0)
      continue;
    auto [position, inserted] = summaries.emplace(summary.FunctionName, summary);
    if (!inserted && !sameSummary(position->second, summary)) {
      // Target aliases such as usize/u64 can share a codegen name. Neither
      // source summary is authoritative for that symbol, so omit it from
      // trusted evidence and retain conservative downstream behavior.
      summaries.erase(position);
      ambiguousNames.insert(summary.FunctionName);
    }
  }
  return summaries;
}

std::string functionsJSON(
    const std::map<std::string, FunctionMemorySummary> &summaries) {
  std::ostringstream out;
  out << '[';
  size_t functionIndex = 0;
  for (const auto &entry : summaries) {
    if (functionIndex++)
      out << ',';
    const FunctionMemorySummary &summary = entry.second;
    out << "{\"name\":\"" << escapeJSON(summary.FunctionName)
        << "\",\"local_effects\":" << summary.LocalEffects
        << ",\"effects\":" << summary.Effects << ",\"roots\":[";
    size_t rootIndex = 0;
    for (const auto &root : summary.Roots) {
      if (rootIndex++)
        out << ',';
      out << "{\"name\":\"" << escapeJSON(root.first)
          << "\",\"local_effects\":" << root.second.LocalEffects
          << ",\"effects\":" << root.second.Effects << '}';
    }
    out << "]}";
  }
  out << ']';
  return out.str();
}

std::string canonicalPayload(
    const std::map<std::string, FunctionMemorySummary> &summaries,
    const std::string &sourceHash, const std::string &targetTriple) {
  std::ostringstream out;
  out << "{\"schema\":\"toka.trusted-memory-evidence\",\"version\":"
      << MemoryEvidenceCache::SchemaVersion << ",\"compiler_version\":\""
      << TOKA_COMPILER_INTERFACE_VERSION << "\",\"interface_format\":\""
      << TOKA_INTERFACE_FORMAT_VERSION << "\",\"target_triple\":\""
      << escapeJSON(targetTriple) << "\",\"source_hash\":\""
      << escapeJSON(sourceHash) << "\",\"functions\":"
      << functionsJSON(summaries) << '}';
  return out.str();
}

std::string evidenceMarker(const std::string &digest) {
  return "TOKEVID1:" + digest;
}

std::optional<uint32_t> unsignedField(const llvm::json::Object &object,
                                      llvm::StringRef name, uint32_t mask) {
  std::optional<int64_t> value = object.getInteger(name);
  if (!value || *value < 0 || static_cast<uint64_t>(*value) > mask)
    return std::nullopt;
  return static_cast<uint32_t>(*value);
}

MemoryEvidenceStatus fail(MemoryEvidenceStatus status,
                          const std::string &message, std::string &reason) {
  reason = message;
  return status;
}

} // namespace

std::string MemoryEvidenceCache::sidecarPath(
    const std::string &interfacePath) {
  if (interfacePath.size() >= 4 &&
      interfacePath.substr(interfacePath.size() - 4) == ".tki")
    return interfacePath.substr(0, interfacePath.size() - 4) + ".tke";
  return interfacePath + ".tke";
}

std::string MemoryEvidenceCache::sourceHash(const std::string &sourcePath) {
  std::optional<std::string> content = readFile(sourcePath);
  return content ? calculateFNV1a(*content) : std::string();
}

bool MemoryEvidenceCache::bindObject(
    llvm::Module &irModule, const Module &module,
    const std::string &sourceHash, const std::string &targetTriple,
    std::vector<std::string> &errors) {
  auto summaries = collectEvidenceSummaries(module, errors);
  if (!summaries)
    return false;
  std::string digest = sha256(
      canonicalPayload(*summaries, sourceHash, targetTriple));
  std::string marker = evidenceMarker(digest);
  llvm::Constant *data = llvm::ConstantDataArray::getString(
      irModule.getContext(), marker, true);
  auto *global = new llvm::GlobalVariable(
      irModule, data->getType(), true, llvm::GlobalValue::PrivateLinkage,
      data, "__toka_memory_evidence_" + digest.substr(0, 16));
  global->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);
  global->setAlignment(llvm::Align(1));
  llvm::appendToUsed(irModule, {global});
  return true;
}

bool MemoryEvidenceCache::write(const std::string &path,
                                const std::string &objectPath,
                                const Module &module,
                                const std::string &sourceHash,
                                const std::string &targetTriple,
                                std::vector<std::string> &errors) {
  std::optional<std::string> object = readFile(objectPath);
  if (!object) {
    errors.push_back("could not read backing object " + objectPath);
    return false;
  }

  auto summaries = collectEvidenceSummaries(module, errors);
  if (!summaries)
    return false;
  std::string evidenceDigest = sha256(
      canonicalPayload(*summaries, sourceHash, targetTriple));

  std::string temporary = path + ".tmp";
  std::error_code error;
  llvm::raw_fd_ostream out(temporary, error, llvm::sys::fs::OF_Text);
  if (error) {
    errors.push_back("could not create evidence sidecar " + temporary +
                     ": " + error.message());
    return false;
  }
  out << "{\"schema\":\"toka.trusted-memory-evidence\",\"version\":"
      << SchemaVersion << ",\"compiler_version\":\""
      << TOKA_COMPILER_INTERFACE_VERSION << "\",\"interface_format\":\""
      << TOKA_INTERFACE_FORMAT_VERSION << "\",\"target_triple\":\""
      << escapeJSON(targetTriple) << "\",\"source_hash\":\""
      << escapeJSON(sourceHash) << "\",\"evidence_sha256\":\""
      << evidenceDigest << "\",\"object_sha256\":\""
      << sha256(*object) << "\",\"functions\":"
      << functionsJSON(*summaries) << "}\n";
  out.close();
  if (out.has_error()) {
    errors.push_back("failed writing evidence sidecar " + temporary);
    llvm::sys::fs::remove(temporary);
    return false;
  }
  llvm::sys::fs::remove(path);
  if (std::error_code renameError = llvm::sys::fs::rename(temporary, path)) {
    errors.push_back("could not publish evidence sidecar " + path + ": " +
                     renameError.message());
    llvm::sys::fs::remove(temporary);
    return false;
  }
  return true;
}

MemoryEvidenceStatus MemoryEvidenceCache::load(
    const std::string &path, const std::string &objectPath,
    const std::string &expectedSourceHash,
    const std::string &expectedTargetTriple,
    std::map<std::string, FunctionMemorySummary> &summaries,
    std::string &reason) {
  summaries.clear();
  std::optional<std::string> content = readFile(path);
  if (!content)
    return fail(MemoryEvidenceStatus::Missing,
                "evidence sidecar is missing", reason);
  llvm::Expected<llvm::json::Value> parsed = llvm::json::parse(*content);
  if (!parsed) {
    llvm::consumeError(parsed.takeError());
    return fail(MemoryEvidenceStatus::ReadError,
                "evidence sidecar is not valid JSON", reason);
  }
  llvm::json::Object *document = parsed->getAsObject();
  if (!document)
    return fail(MemoryEvidenceStatus::InvalidSchema,
                "evidence root is not an object", reason);
  std::optional<llvm::StringRef> schema = document->getString("schema");
  std::optional<int64_t> version = document->getInteger("version");
  if (!schema || *schema != "toka.trusted-memory-evidence" || !version ||
      *version != SchemaVersion)
    return fail(MemoryEvidenceStatus::InvalidSchema,
                "unsupported evidence schema", reason);

  auto matches = [&](llvm::StringRef field, llvm::StringRef expected) {
    std::optional<llvm::StringRef> value = document->getString(field);
    return value && *value == expected;
  };
  if (!matches("compiler_version", TOKA_COMPILER_INTERFACE_VERSION) ||
      !matches("interface_format", TOKA_INTERFACE_FORMAT_VERSION) ||
      !matches("target_triple", expectedTargetTriple) ||
      !matches("source_hash", expectedSourceHash))
    return fail(MemoryEvidenceStatus::IdentityMismatch,
                "evidence identity does not match interface", reason);

  std::optional<std::string> object = readFile(objectPath);
  if (!object)
    return fail(MemoryEvidenceStatus::MissingObject,
                "backing object is missing", reason);
  if (!matches("object_sha256", sha256(*object)))
    return fail(MemoryEvidenceStatus::ObjectMismatch,
                "backing object digest does not match evidence", reason);

  llvm::json::Array *functions = document->getArray("functions");
  if (!functions)
    return fail(MemoryEvidenceStatus::InvalidRecord,
                "evidence functions array is missing", reason);
  for (llvm::json::Value &value : *functions) {
    llvm::json::Object *record = value.getAsObject();
    if (!record)
      return fail(MemoryEvidenceStatus::InvalidRecord,
                  "function record is not an object", reason);
    std::optional<llvm::StringRef> name = record->getString("name");
    std::optional<uint32_t> localEffects =
        unsignedField(*record, "local_effects", FunctionEffectMask);
    std::optional<uint32_t> effects =
        unsignedField(*record, "effects", FunctionEffectMask);
    llvm::json::Array *roots = record->getArray("roots");
    if (!name || name->empty() || !localEffects || !effects || !roots)
      return fail(MemoryEvidenceStatus::InvalidRecord,
                  "function record is incomplete", reason);
    FunctionMemorySummary summary;
    summary.FunctionName = name->str();
    summary.Origin = MemorySummaryOrigin::TrustedCache;
    summary.LocalEffects = *localEffects;
    summary.Effects = *effects;
    for (llvm::json::Value &rootValue : *roots) {
      llvm::json::Object *root = rootValue.getAsObject();
      if (!root)
        return fail(MemoryEvidenceStatus::InvalidRecord,
                    "root record is not an object", reason);
      std::optional<llvm::StringRef> rootName = root->getString("name");
      std::optional<uint32_t> rootLocal =
          unsignedField(*root, "local_effects", RootEffectMask);
      std::optional<uint32_t> rootEffects =
          unsignedField(*root, "effects", RootEffectMask);
      if (!rootName || rootName->empty() || !rootLocal || !rootEffects ||
          !summary.Roots.emplace(rootName->str(),
                                 MemoryRootSummary{*rootLocal, *rootEffects})
               .second)
        return fail(MemoryEvidenceStatus::InvalidRecord,
                    "root record is incomplete or duplicated", reason);
    }
    if (!summaries.emplace(summary.FunctionName, std::move(summary)).second)
      return fail(MemoryEvidenceStatus::InvalidRecord,
                  "function record is duplicated", reason);
  }
  std::optional<llvm::StringRef> recordedEvidenceDigest =
      document->getString("evidence_sha256");
  std::string actualEvidenceDigest = sha256(canonicalPayload(
      summaries, expectedSourceHash, expectedTargetTriple));
  if (!recordedEvidenceDigest ||
      *recordedEvidenceDigest != actualEvidenceDigest ||
      object->find(evidenceMarker(actualEvidenceDigest)) == std::string::npos)
    return fail(MemoryEvidenceStatus::EvidenceMismatch,
                "evidence digest is not bound to backing object", reason);
  reason = "validated against backing object";
  return MemoryEvidenceStatus::Valid;
}

const char *toString(MemoryEvidenceStatus status) {
  switch (status) {
  case MemoryEvidenceStatus::NotApplicable: return "NotApplicable";
  case MemoryEvidenceStatus::Missing: return "Missing";
  case MemoryEvidenceStatus::ReadError: return "ReadError";
  case MemoryEvidenceStatus::InvalidSchema: return "InvalidSchema";
  case MemoryEvidenceStatus::IdentityMismatch: return "IdentityMismatch";
  case MemoryEvidenceStatus::MissingObject: return "MissingObject";
  case MemoryEvidenceStatus::ObjectMismatch: return "ObjectMismatch";
  case MemoryEvidenceStatus::EvidenceMismatch: return "EvidenceMismatch";
  case MemoryEvidenceStatus::InvalidRecord: return "InvalidRecord";
  case MemoryEvidenceStatus::Valid: return "Valid";
  }
  return "NotApplicable";
}

} // namespace toka
