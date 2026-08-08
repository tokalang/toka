#ifdef NDEBUG
#undef NDEBUG
#endif

#include "toka/SemanticManifestEnvelope.h"
#include "toka/CanonicalDeclarationWitness.h"
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

toka::OutcomeDeclarationWitnessInput sample(const std::string &name) {
  toka::OutcomeDeclarationWitnessInput input;
  input.FunctionCrateId = "workspace-test";
  input.FunctionLogicalModulePath = "pkg/example";
  input.FunctionName = name;
  input.Parameters = {{0, true, false, kType}};
  input.CanonicalResultType = kType;
  input.OutcomeFormalIndex = 0;
  input.ReturnEnum = {"workspace-test", "pkg/example", "ReadResult", 0};
  input.Cases = {{"Err", 1, false}, {"Ok", 0, true}};
  return input;
}

std::string encode(const std::string &name) {
  auto encoded =
      toka::CanonicalDeclarationWitnessEncoder::encodeOutcomeTransition(
          sample(name));
  assert(encoded);
  return *encoded;
}

std::string readFile(const std::string &path) {
  std::ifstream input(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(input),
                     std::istreambuf_iterator<char>());
}

void writeFile(const std::string &path, const std::string &content) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  assert(output);
  output << content;
  assert(output);
}

toka::SemanticManifestEnvelopeInput sampleEnvelope() {
  toka::SemanticManifestEnvelopeInput input;
  input.TargetTriple = "x86_64-unknown-linux-gnu";
  input.Module = {"workspace-test", "pkg/example"};
  input.InterfaceContent = "@tki version:2\nfn try_read(...)\n";
  input.SemanticDependencyClosureDigest = std::string(64, 'a');
  input.CDW1Records = {encode("try_read_b"), encode("try_read_a")};
  return input;
}

} // namespace

int main() {
  const auto nonce =
      std::chrono::steady_clock::now().time_since_epoch().count();
  const std::string path =
      (std::filesystem::temp_directory_path() /
       ("toka-semantic-manifest-" + std::to_string(nonce) + ".tki.tsm"))
          .string();
  std::filesystem::remove(path);
  std::filesystem::remove(path + ".tmp");

  auto input = sampleEnvelope();
  std::vector<std::string> errors;
  assert(toka::SemanticManifestEnvelope::sidecarPath("pkg/module.tki") ==
         "pkg/module.tki.tsm");
  assert(toka::SemanticManifestEnvelope::write(path, input, errors));
  assert(errors.empty());

  std::vector<std::string> records;
  std::string closureDigest;
  std::string reason;
  assert(toka::SemanticManifestEnvelope::load(
             path, input.InterfaceContent, input.Module, input.TargetTriple,
             input.SemanticDependencyClosureDigest, records, closureDigest,
             reason) == toka::SemanticManifestEnvelopeStatus::Valid);
  assert(records.size() == 2);
  assert(records[0] < records[1]);
  assert(closureDigest == input.SemanticDependencyClosureDigest);

  assert(toka::SemanticManifestEnvelope::load(
             path, input.InterfaceContent + "changed", input.Module,
             input.TargetTriple, input.SemanticDependencyClosureDigest, records,
             closureDigest,
             reason) == toka::SemanticManifestEnvelopeStatus::IdentityMismatch);

  const std::string original = readFile(path);
  std::string tamperedPayload = original;
  const size_t payload = tamperedPayload.find("\"payload_sha256\":\"");
  assert(payload != std::string::npos);
  const size_t payloadHex =
      payload + std::string("\"payload_sha256\":\"").size();
  tamperedPayload[payloadHex] = tamperedPayload[payloadHex] == '0' ? '1' : '0';
  writeFile(path, tamperedPayload);
  assert(toka::SemanticManifestEnvelope::load(
             path, input.InterfaceContent, input.Module, input.TargetTriple,
             input.SemanticDependencyClosureDigest, records, closureDigest,
             reason) == toka::SemanticManifestEnvelopeStatus::PayloadMismatch);

  std::string relabeled = original;
  const size_t criticality = relabeled.find("SafetyRequired");
  assert(criticality != std::string::npos);
  relabeled.replace(criticality, std::string("SafetyRequired").size(),
                    "OptionalOptimization");
  writeFile(path, relabeled);
  assert(toka::SemanticManifestEnvelope::load(
             path, input.InterfaceContent, input.Module, input.TargetTriple,
             input.SemanticDependencyClosureDigest, records, closureDigest,
             reason) == toka::SemanticManifestEnvelopeStatus::InvalidRecord);

  std::string duplicateField = original;
  duplicateField.insert(1, "\"version\":1,");
  writeFile(path, duplicateField);
  assert(toka::SemanticManifestEnvelope::load(
             path, input.InterfaceContent, input.Module, input.TargetTriple,
             input.SemanticDependencyClosureDigest, records, closureDigest,
             reason) != toka::SemanticManifestEnvelopeStatus::Valid);

  std::string unknownField = original;
  unknownField.insert(unknownField.size() - 2, ",\"unknown\":1");
  writeFile(path, unknownField);
  assert(toka::SemanticManifestEnvelope::load(
             path, input.InterfaceContent, input.Module, input.TargetTriple,
             input.SemanticDependencyClosureDigest, records, closureDigest,
             reason) == toka::SemanticManifestEnvelopeStatus::InvalidSchema);

  constexpr const char CDWPrefix[] = "\"cdw1\":\"";
  std::string reorderedRecords = original;
  const size_t firstField = reorderedRecords.find(CDWPrefix);
  assert(firstField != std::string::npos);
  const size_t firstHex = firstField + sizeof(CDWPrefix) - 1;
  const size_t firstEnd = reorderedRecords.find('"', firstHex);
  assert(firstEnd != std::string::npos);
  const size_t secondField = reorderedRecords.find(CDWPrefix, firstEnd);
  assert(secondField != std::string::npos);
  const size_t secondHex = secondField + sizeof(CDWPrefix) - 1;
  const size_t secondEnd = reorderedRecords.find('"', secondHex);
  assert(secondEnd != std::string::npos);
  const std::string firstRecord =
      reorderedRecords.substr(firstHex, firstEnd - firstHex);
  const std::string secondRecord =
      reorderedRecords.substr(secondHex, secondEnd - secondHex);
  assert(firstRecord.size() == secondRecord.size());
  reorderedRecords.replace(firstHex, firstRecord.size(), secondRecord);
  reorderedRecords.replace(secondHex, secondRecord.size(), firstRecord);
  writeFile(path, reorderedRecords);
  assert(toka::SemanticManifestEnvelope::load(
             path, input.InterfaceContent, input.Module, input.TargetTriple,
             input.SemanticDependencyClosureDigest, records, closureDigest,
             reason) == toka::SemanticManifestEnvelopeStatus::InvalidRecord);

  std::string tamperedClosure = original;
  const size_t closure =
      tamperedClosure.find("\"semantic_dependency_closure_sha256\":\"");
  assert(closure != std::string::npos);
  const size_t closureHex =
      closure + std::string("\"semantic_dependency_closure_sha256\":\"").size();
  tamperedClosure[closureHex] = tamperedClosure[closureHex] == '0' ? '1' : '0';
  writeFile(path, tamperedClosure);
  assert(toka::SemanticManifestEnvelope::load(
             path, input.InterfaceContent, input.Module, input.TargetTriple,
             input.SemanticDependencyClosureDigest, records, closureDigest,
             reason) == toka::SemanticManifestEnvelopeStatus::IdentityMismatch);

  auto wrongCoordinate = sampleEnvelope();
  wrongCoordinate.Module = {"workspace-test", "pkg/other"};
  errors.clear();
  assert(!toka::SemanticManifestEnvelope::write(path, wrongCoordinate, errors));
  assert(!errors.empty());

  errors.clear();
  input.CDW1Records.push_back(input.CDW1Records.front());
  assert(!toka::SemanticManifestEnvelope::write(path, input, errors));
  assert(!errors.empty());

  std::filesystem::remove(path);
  std::filesystem::remove(path + ".tmp");
  return 0;
}
