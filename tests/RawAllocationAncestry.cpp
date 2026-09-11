#include "toka/ModuleResolver.h"
#include "toka/Sema.h"
#include <fstream>
#include <functional>
#include <iostream>
#include <set>
#include <sstream>

bool g_JsonDiagnostics = false;
using namespace toka;
#define CHECK(x) do { if (!(x)) { std::cerr << "failed: " << #x << ':' << __LINE__ << '\n'; return 1; } } while (false)

int main(int argc, char **argv) {
  CHECK(argc == 2);
  const std::string library = argv[1];
  const std::string path = library + "/std/vec.tk";
  std::ifstream input(path);
  CHECK(input.good());
  std::ostringstream text;
  text << input.rdbuf();
  text << R"(
fn __allocation_ancestry() {
    auto *first# = unsafe alloc [4] i32
    auto *copied = *first
    auto *second = unsafe alloc [4] i32
    auto address = 0:Addr
    unsafe { first[0] = 11 }
    unsafe free [0] *first
    unsafe free [0] *second
}
)";
  SourceManager sources;
  DiagnosticEngine::init(sources);
  ModuleResolver resolver(sources, {library}, {}, true, {library}, {},
                          "allocation-source-test", library, "test-toolchain");
  std::vector<std::unique_ptr<Module>> modules;
  CHECK(resolver.resolveAndParse(path, modules, text.str()));
  Sema sema;
  sema.setSignatureDrivenCallCedeEnabled(true);
  sema.setStage1ExplicitCallerCedeEnabled(true);
  for (auto &module : modules) sema.declareGlobals(*module);
  for (auto &module : modules) CHECK(sema.checkModule(*module));
  CHECK(!DiagnosticEngine::hasErrors());
  FunctionDecl *function = nullptr;
  for (auto &module : modules)
    for (auto &candidate : module->Functions)
      if (candidate->Name == "__allocation_ancestry") function = candidate.get();
  CHECK(function && function->Body);
  std::map<std::string, std::set<std::string>> ancestry;
  for (const auto &statement : function->Body->Statements) {
    auto *variable = dynamic_cast<VariableDecl *>(statement.get());
    if (!variable || !variable->Init) continue;
    std::set<const RawAddressSource *> seen;
    std::vector<RawAddressSourcePtr> pending{variable->Init->RawAddressValueFacts};
    while (!pending.empty()) {
      auto value = pending.back();
      pending.pop_back();
      if (!value || !seen.insert(value.get()).second) continue;
      if (value->AllocationAncestry) {
        const auto &origin = *value->AllocationAncestry;
        CHECK(origin.IsArray && !origin.HasInitializerSyntax);
        CHECK(origin.StorageType && origin.StorageType->isRawPointer());
        CHECK(origin.StorageType->getPointeeType()->isSlice());
        CHECK(origin.StorageType->getPointeeType()->getArrayElementType()->isInteger());
        CHECK(!origin.SourceEdge.empty() && origin.SourceEdge.find(library) == std::string::npos);
        ancestry[Type::stripMorphology(variable->Name)].insert(origin.SourceEdge);
      }
      pending.insert(pending.end(), value->Inputs.begin(), value->Inputs.end());
    }
  }
  CHECK(ancestry["first"].size() == 1);
  CHECK(ancestry["copied"] == ancestry["first"]);
  CHECK(ancestry["second"].size() == 1 && ancestry["second"] != ancestry["first"]);
  CHECK(ancestry["address"].empty());
  unsigned writes = 0;
  std::set<std::string> released;
  std::function<void(Stmt *)> inspect = [&](Stmt *statement) {
    if (auto *block = dynamic_cast<BlockStmt *>(statement)) {
      for (auto &child : block->Statements) inspect(child.get());
    } else if (auto *unsafe = dynamic_cast<UnsafeStmt *>(statement)) {
      inspect(unsafe->Statement.get());
    } else if (auto *release = dynamic_cast<FreeStmt *>(statement)) {
      if (release->RawStorageRelease) {
        const auto &observation = *release->RawStorageRelease;
        auto *count = dynamic_cast<const NumberExpr *>(observation.DeclaredCount);
        if (count && count->Value == 0 && observation.StorageType->isRawPointer() &&
            observation.StorageBinding.RootID && !observation.SourceEdge.empty())
          released.insert(observation.StorageBinding.RootName);
      }
    } else if (auto *expression = dynamic_cast<ExprStmt *>(statement)) {
      auto *assignment = dynamic_cast<BinaryExpr *>(expression->Expression.get());
      if (assignment && assignment->RawStorageWrite) {
        const auto &write = *assignment->RawStorageWrite;
        if (write.Slot.RootName == "first" && write.Slot.RootID &&
            write.Value == assignment->RHS.get() && write.ElementType->isInteger() &&
            write.StorageType->isRawPointer() && !write.SourceEdge.empty()) ++writes;
      }
    }
  };
  inspect(function->Body.get());
  CHECK(writes == 1);
  CHECK(released == std::set<std::string>({"first", "second"}));
  std::cout << "Allocation ancestry only: distinct source edges, copied ancestry, opaque address excluded.\n";
}
