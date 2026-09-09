#include "toka/ModuleResolver.h"
#include "toka/Sema.h"
#include <fstream>
#include <iostream>
#include <sstream>

bool g_JsonDiagnostics = false;
using namespace toka;
#define CHECK(x) do { if (!(x)) { std::cerr << "failed: " << #x << ":" << __LINE__ << '\n'; return 1; } } while (false)

int main(int argc, char **argv) {
  CHECK(argc == 2);
  const std::string library = argv[1];
  const std::string path = library + "/std/sync.tk";
  std::ifstream input(path);
  CHECK(input.good());
  std::ostringstream text;
  text << input.rdbuf();
  // Resolve the actual SDK adapters. No tests fabricate a trusted declaration,
  // initialized-state bit or native storage witness in C++.
  text << R"(
fn __native_observe(owner: Mutex<i32>) {}
pub fn __native_flow_test(flag: bool) {
    auto original# = __sync_mutex_create<i32>(7)
    __native_observe(original)
    auto moved# = cede original
    __native_observe(moved)
    auto second# = __sync_mutex_create<i32>(8)
    __native_observe(second)
    if flag { moved.handle = 0:Addr }
    __native_observe(moved)
    __native_observe(second)
}
)";
  SourceManager sources;
  DiagnosticEngine::init(sources);
  ModuleResolver resolver(sources, {library}, {}, true, {library}, {},
                          "native-flow-test", library, "test-toolchain");
  std::vector<std::unique_ptr<Module>> modules;
  CHECK(resolver.resolveAndParse(path, modules, text.str()));
  Sema sema;
  sema.setSignatureDrivenCallCedeEnabled(true);
  sema.setStage1ExplicitCallerCedeEnabled(true);
  for (auto &module : modules) sema.declareGlobals(*module);
  for (auto &module : modules) CHECK(sema.checkModule(*module));
  CHECK(!DiagnosticEngine::hasErrors());
  CHECK(sema.finalizeNativeSyncFactoryPlans());
  FunctionDecl *test = nullptr;
  for (auto &module : modules)
    for (auto &function : module->Functions)
      if (function->Name == "__native_flow_test") test = function.get();
  CHECK(test && test->Body);
  std::vector<NativeSyncFactoryPtr> observations;
  NativeSyncFactoryPtr first, second;
  for (auto &statement : test->Body->Statements) {
    if (auto *binding = dynamic_cast<VariableDecl *>(statement.get())) {
      if (binding->Name == "original") first = binding->Init->NativeSyncFactoryOrigin;
      if (binding->Name == "second") second = binding->Init->NativeSyncFactoryOrigin;
    }
    auto *expression = dynamic_cast<ExprStmt *>(statement.get());
    auto *call = expression ? dynamic_cast<CallExpr *>(expression->Expression.get()) : nullptr;
    if (call && call->Args.size() == 1) {
      observations.push_back(call->Args[0]->NativeSyncFactoryOrigin);
      if (!observations.back()) std::cerr << "no origin: " << call->Args[0]->toString()
          << " type=" << call->Args[0]->ResolvedType->toString() << '\n';
    }
  }
  CHECK(first && second && first != second);
  CHECK(observations.size() == 5);
  CHECK(observations[0] == first);
  CHECK(observations[1] == first); // move preserves exact factory/storage edge
  CHECK(observations[2] == second);
  CHECK(!observations[3]); // possible storage identity write kills provenance
  CHECK(observations[4] == second); // unrelated owner survives the branch join
  std::cout << "native factory flow: real source move, distinct owners, branch invalidation pass\n";
}
