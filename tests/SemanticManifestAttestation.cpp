// Copyright (c) 2026 YiZhonghua<zhyi@dpai.com>. All rights reserved.
//
// Licensed under the Apache License, Version 2.0.
#ifdef NDEBUG
#undef NDEBUG
#endif

#include "toka/CanonicalDeclarationWitness.h"
#include "toka/SemanticManifestAttestation.h"
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {

constexpr const char kType[] =
    "toka-outcome-type-v1;cede=1:0;writable=1:0;nullable=1:0;"
    "blocked=1:0;kind=9:primitive;name=3:i32;";

std::string encode() {
  toka::OutcomeDeclarationWitnessInput input;
  input.FunctionCrateId = "workspace-test";
  input.FunctionLogicalModulePath = "pkg/example";
  input.FunctionName = "try_read";
  input.Parameters = {{0, true, false, kType}};
  input.CanonicalResultType = kType;
  input.OutcomeFormalIndex = 0;
  input.ReturnEnum = {"workspace-test", "pkg/example", "ReadResult", 0};
  input.Cases = {{"Err", 1, false}, {"Ok", 0, true}};
  const auto encoded =
      toka::CanonicalDeclarationWitnessEncoder::encodeOutcomeTransition(input);
  assert(encoded);
  return *encoded;
}

void writeFile(const std::filesystem::path &path, const std::string &content) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  assert(output);
  output << content;
  assert(output);
}

std::string readFile(const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(input),
                     std::istreambuf_iterator<char>());
}

} // namespace

int main() {
  const auto nonce =
      std::chrono::steady_clock::now().time_since_epoch().count();
  const std::filesystem::path root =
      std::filesystem::temp_directory_path() /
      ("toka-semantic-attestation-" + std::to_string(nonce));
  const std::filesystem::path state = root / "compiler-state";
  const std::filesystem::path otherState = root / "other-state";
  const std::filesystem::path object = root / "provider.o";
  const std::filesystem::path manifest = root / "provider.tki.tsm";
  std::filesystem::create_directories(root);

  toka::SemanticManifestAttestationInput input;
  input.TargetTriple = "x86_64-unknown-linux-gnu";
  input.Module = {"workspace-test", "pkg/example"};
  input.InterfaceContent = "@tki version:2\nfn try_read(...)\n";
  input.SemanticDependencyClosureDigest = std::string(64, 'a');
  input.CDW1Records = {encode()};
  toka::SemanticManifestAttestationPrepared prepared;
  std::vector<std::string> errors;
  assert(toka::SemanticManifestAttestation::prepare(input, prepared, errors));
  assert(errors.empty());
  assert(prepared.ObjectMarker.rfind("TOKASMAN2:", 0) == 0);

  writeFile(object, "object-prefix:" + prepared.ObjectMarker + ":object-tail");
  assert(toka::SemanticManifestAttestation::write(
      manifest.string(), object.string(), prepared, state.string(), errors));
  assert(errors.empty());

  toka::SemanticManifestAttestationResult result;
  std::string reason;
  assert(toka::SemanticManifestAttestation::loadCandidate(
             manifest.string(), input.InterfaceContent, input.Module,
             input.TargetTriple, object.string(), state.string(), result,
             reason) == toka::SemanticManifestAttestationStatus::Valid);
  assert(result.CDW1Records == prepared.CDW1Records);
  assert(toka::SemanticManifestAttestation::load(
             manifest.string(), input.InterfaceContent, input.Module,
             input.TargetTriple, input.SemanticDependencyClosureDigest,
             object.string(), state.string(), result,
             reason) == toka::SemanticManifestAttestationStatus::Valid);
  assert(toka::SemanticManifestAttestation::load(
             manifest.string(), input.InterfaceContent, input.Module,
             input.TargetTriple, std::string(64, 'b'), object.string(),
             state.string(), result,
             reason) == toka::SemanticManifestAttestationStatus::IdentityMismatch);
  assert(toka::SemanticManifestAttestation::loadCandidate(
             manifest.string(), input.InterfaceContent, input.Module,
             input.TargetTriple, object.string(), otherState.string(), result,
             reason) ==
         toka::SemanticManifestAttestationStatus::ProvenanceMismatch);

  const std::string pristineObject = readFile(object);
  writeFile(object, pristineObject + "tampered");
  assert(toka::SemanticManifestAttestation::loadCandidate(
             manifest.string(), input.InterfaceContent, input.Module,
             input.TargetTriple, object.string(), state.string(), result,
             reason) == toka::SemanticManifestAttestationStatus::ObjectMismatch);
  writeFile(object, pristineObject);

  writeFile(object, "object without attestation marker");
  errors.clear();
  assert(toka::SemanticManifestAttestation::write(
      manifest.string(), object.string(), prepared, state.string(), errors));
  assert(toka::SemanticManifestAttestation::loadCandidate(
             manifest.string(), input.InterfaceContent, input.Module,
             input.TargetTriple, object.string(), state.string(), result,
             reason) == toka::SemanticManifestAttestationStatus::MarkerMismatch);

  writeFile(object, pristineObject);
  errors.clear();
  assert(toka::SemanticManifestAttestation::write(
      manifest.string(), object.string(), prepared, state.string(), errors));
  std::string noncanonical = readFile(manifest);
  noncanonical.push_back(' ');
  writeFile(manifest, noncanonical);
  assert(toka::SemanticManifestAttestation::loadCandidate(
             manifest.string(), input.InterfaceContent, input.Module,
             input.TargetTriple, object.string(), state.string(), result,
             reason) == toka::SemanticManifestAttestationStatus::InvalidSchema);

  std::filesystem::remove_all(root);
  return 0;
}
