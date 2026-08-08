#ifdef NDEBUG
#undef NDEBUG
#endif

#include "toka/SemanticDependencyClosure.h"
#include "toka/AST.h"
#include <cassert>
#include <memory>
#include <optional>
#include <string>
#include <vector>

bool g_JsonDiagnostics = false;

namespace {

std::unique_ptr<toka::Module> makeModule(const std::string &path,
                                         const std::string &crate,
                                         const std::string &logical) {
  auto module = std::make_unique<toka::Module>();
  module->ResolvedPath = path;
  module->ShadowCoordinateKnown = true;
  module->ShadowCrateId = crate;
  module->ShadowLogicalModulePath = logical;
  return module;
}

void addImport(toka::Module &module, const std::string &physical,
               const std::string &resolved) {
  auto import = std::make_unique<toka::ImportDecl>(false, physical);
  import->ResolvedPath = resolved;
  module.Imports.push_back(std::move(import));
}

std::optional<std::string> digest(toka::Module &root,
                                  std::vector<toka::Module *> modules) {
  std::vector<std::string> errors;
  auto result =
      toka::SemanticDependencyClosure::calculate(root, modules, errors);
  assert((result.has_value()) == errors.empty());
  return result;
}

} // namespace

int main() {
  auto root = makeModule("/tmp/toka-closure/a/root.tk", "workspace", "a/root");
  auto dependency =
      makeModule("/tmp/toka-closure/a/dep.tk", "workspace", "a/dep");
  addImport(*root, "./dep", dependency->ResolvedPath);
  const auto first = digest(*root, {root.get(), dependency.get()});
  assert(first);

  // Physical source metadata and ignorable @tki facts do not participate in
  // the replay surface. Resolver coordinates and declarations do.
  root->SourcePath = "/unrelated/relocated/root.tk";
  root->InterfaceV2Facts.push_back("cdw1: audit-only");
  const auto metadataFree = digest(*root, {root.get(), dependency.get()});
  assert(metadataFree && *metadataFree == *first);

  auto relocatedRoot =
      makeModule("/tmp/toka-closure/b/root.tk", "workspace", "a/root");
  auto relocatedDependency =
      makeModule("/tmp/toka-closure/b/dep.tk", "workspace", "a/dep");
  addImport(*relocatedRoot, "./dep", relocatedDependency->ResolvedPath);
  const auto relocated =
      digest(*relocatedRoot, {relocatedRoot.get(), relocatedDependency.get()});
  assert(relocated && *relocated == *first);

  relocatedDependency->ShadowLogicalModulePath = "a/changed-dep";
  const auto changedCoordinate =
      digest(*relocatedRoot, {relocatedRoot.get(), relocatedDependency.get()});
  assert(changedCoordinate && *changedCoordinate != *first);

  auto unknownRoot = makeModule("/tmp/toka-closure/unknown/root.tk",
                                "workspace", "unknown/root");
  auto unknownDependency = makeModule("/tmp/toka-closure/unknown/dep.tk",
                                      "workspace", "unknown/dep");
  unknownDependency->ShadowCoordinateKnown = false;
  addImport(*unknownRoot, "./dep", unknownDependency->ResolvedPath);
  std::vector<std::string> errors;
  assert(!toka::SemanticDependencyClosure::calculate(
      *unknownRoot, {unknownRoot.get(), unknownDependency.get()}, errors));
  assert(!errors.empty());

  auto cycleA =
      makeModule("/tmp/toka-closure/cycle/a.tk", "workspace", "cycle/a");
  auto cycleB =
      makeModule("/tmp/toka-closure/cycle/b.tk", "workspace", "cycle/b");
  addImport(*cycleA, "./b", cycleB->ResolvedPath);
  addImport(*cycleB, "./a", cycleA->ResolvedPath);
  errors.clear();
  assert(!toka::SemanticDependencyClosure::calculate(
      *cycleA, {cycleA.get(), cycleB.get()}, errors));
  assert(!errors.empty());

  auto duplicateRoot = makeModule("/tmp/toka-closure/duplicate/root.tk",
                                  "workspace", "duplicate/root");
  auto duplicateDependency = makeModule("/tmp/toka-closure/duplicate/dep.tk",
                                        "workspace", "duplicate/root");
  addImport(*duplicateRoot, "./dep", duplicateDependency->ResolvedPath);
  errors.clear();
  assert(!toka::SemanticDependencyClosure::calculate(
      *duplicateRoot, {duplicateRoot.get(), duplicateDependency.get()},
      errors));
  assert(!errors.empty());
  return 0;
}
