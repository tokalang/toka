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
  const auto sdk = text.str();
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
    auto readonly = cede second
    __native_observe(readonly)
}
pub fn __native_shared_owner_test() {
    auto ~mutex = Mutex<i32>::make_shared(7)
    auto ~worker_mutex = ~mutex
    __native_observe(worker_mutex)
    ({ [cede ~worker_mutex] => return 0 }:dyn fn() -> i32)
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
  std::shared_ptr<Type> factoryType, writableType, lastObservedType;
  for (auto &statement : test->Body->Statements) {
    if (auto *binding = dynamic_cast<VariableDecl *>(statement.get())) {
      if (binding->Name == "original") {
        first = binding->Init->NativeSyncFactoryOrigin;
        factoryType = binding->Init->ResolvedType;
      }
      if (binding->Name == "second") second = binding->Init->NativeSyncFactoryOrigin;
    }
    auto *expression = dynamic_cast<ExprStmt *>(statement.get());
    auto *call = expression ? dynamic_cast<CallExpr *>(expression->Expression.get()) : nullptr;
    if (call && call->Args.size() == 1) {
      observations.push_back(call->Args[0]->NativeSyncFactoryOrigin);
      lastObservedType = call->Args[0]->ResolvedType;
      if (!writableType) writableType = call->Args[0]->ResolvedType;
    }
  }
  CHECK(first && second && first != second);
  CHECK(observations.size() == 6);
  CHECK(observations[0] == first);
  CHECK(observations[1] == first); // move preserves exact factory/storage edge
  CHECK(observations[2] == second);
  CHECK(!observations[3]); // possible storage identity write kills provenance
  CHECK(observations[4] == second); // unrelated owner survives the branch join
  CHECK(observations[5] == second && lastObservedType && !lastObservedType->IsWritable);
  CHECK(factoryType && writableType && !factoryType->IsWritable && writableType->IsWritable);
  const auto factoryIdentity = factoryType->canonicalIdentity();
  const auto viewIdentity = writableType->canonicalIdentity();
  auto owner = std::dynamic_pointer_cast<ShapeType>(factoryType);
  CHECK(owner && owner->Decl && owner->Decl->InstantiationArgs.size() == 1);
  const auto *decl = owner->Decl;
  auto element = decl->InstantiationArgs[0];
  const auto elementIdentity = element->canonicalIdentity();
  CHECK(first->matchesOwnerView(factoryType));
  CHECK(first->matchesOwnerView(writableType));
  auto malformedNullable = std::make_shared<ShapeType>(*owner);
  malformedNullable->IsNullable = true;
  CHECK(!first->matchesOwnerView(malformedNullable));
  CHECK(!first->matchesOwnerView(factoryType->withAttributes(false, false, true)));
  CHECK(!first->matchesOwnerView(std::make_shared<UniquePointerType>(factoryType)));
  CHECK(!first->matchesOwnerView(std::make_shared<SharedPointerType>(factoryType)));
  CHECK(!first->matchesOwnerView(std::make_shared<RawPointerType>(factoryType)));
  CHECK(!first->matchesOwnerView(std::make_shared<ReferenceType>(factoryType)));
  CHECK(!first->matchesOwnerView(Type::fromString("i32#")));
  CHECK(!first->matchesOwnerView(nullptr));
  // Complete element morphology participates in instantiated nominal identity.
  // Only test-side candidate types are changed; the real sealed plan is never
  // constructed or altered by the test.
  for (const char *spelling : {"i32#", "^i32", "~i32", "*i32", "&i32", "nul *i32", "i32$"}) {
    auto changedDecl = std::make_unique<ShapeDecl>(*owner->Decl);
    changedDecl->InstantiationTemplate = decl->InstantiationTemplate;
    changedDecl->InstantiationArgs = decl->InstantiationArgs;
    auto changedType = std::make_shared<ShapeType>(*owner);
    changedType->Decl = changedDecl.get();
    CHECK(first->matchesOwnerView(changedType));
    changedDecl->InstantiationArgs = {Type::fromString(spelling)};
    CHECK(!first->matchesOwnerView(changedType));
  }
  auto impostorDecl = std::make_unique<ShapeDecl>(*owner->Decl);
  impostorDecl->InstantiationTemplate = nullptr;
  impostorDecl->NominalId = NominalShapeId::fromResolverCoordinate("other", "other/module", decl->Name, 0);
  auto impostor = std::make_shared<ShapeType>(*owner);
  impostor->Decl = impostorDecl.get();
  CHECK(!first->matchesOwnerView(impostor));
  auto unresolved = std::make_shared<ShapeType>(*owner);
  unresolved->Decl = nullptr;
  CHECK(!first->matchesOwnerView(unresolved));
  CHECK(factoryType->canonicalIdentity() == factoryIdentity && !factoryType->IsWritable);
  CHECK(writableType->canonicalIdentity() == viewIdentity && writableType->IsWritable);
  CHECK(owner->Decl == decl && decl->InstantiationArgs[0] == element);
  CHECK(element->canonicalIdentity() == elementIdentity);

  FunctionDecl *sharedOwner = nullptr;
  for (auto &module : modules)
    for (auto &function : module->Functions)
      if (function->Name == "__native_shared_owner_test") sharedOwner = function.get();
  CHECK(sharedOwner && sharedOwner->Body && sharedOwner->Body->Statements.size() == 4);
  auto *sharedBinding = dynamic_cast<VariableDecl *>(sharedOwner->Body->Statements[0].get());
  auto *sharedCopy = dynamic_cast<VariableDecl *>(sharedOwner->Body->Statements[1].get());
  CHECK(sharedBinding && sharedCopy && sharedBinding->Init->NativeSyncOwnerRecipe);
  CHECK(sharedCopy->Init->NativeSyncOwnerRecipe == sharedBinding->Init->NativeSyncOwnerRecipe);
  auto *captureStatement = dynamic_cast<ExprStmt *>(sharedOwner->Body->Statements[3].get());
  auto *captureType = captureStatement ? dynamic_cast<CastExpr *>(captureStatement->Expression.get()) : nullptr;
  auto *capture = captureType ? dynamic_cast<ClosureExpr *>(captureType->Expression.get()) : nullptr;
  CHECK(capture && capture->NativeSyncCaptureRecipes.size() == 1);
  CHECK(capture->NativeSyncCaptureRecipes.begin()->second == sharedBinding->Init->NativeSyncOwnerRecipe);

  // Real rejection path: the outer call checks a destructive argument before
  // rejecting a later type. The following read must see the same source edge,
  // and normal diagnostics must not report a leaked move/uninitialized state.
  DiagnosticEngine::reset();
  SourceManager rejectedSources;
  DiagnosticEngine::init(rejectedSources);
  DiagnosticEngine::setPrintingEnabled(false);
  ModuleResolver rejectedResolver(rejectedSources, {library}, {}, true, {library}, {},
                                  "native-flow-test", library, "test-toolchain");
  std::vector<std::unique_ptr<Module>> rejectedModules;
  CHECK(rejectedResolver.resolveAndParse(path, rejectedModules, sdk + R"(
fn __native_reject(cede owner: Mutex<i32>, amount: i32) { cede owner }
fn __native_observe(owner: Mutex<i32>) {}
pub fn __native_rollback_test() {
    auto original# = __sync_mutex_create<i32>(7)
    __native_reject(cede original, true)
    __native_observe(original)
}
)"));
  Sema rejectedSema;
  rejectedSema.setSignatureDrivenCallCedeEnabled(true);
  rejectedSema.setStage1ExplicitCallerCedeEnabled(true);
  for (auto &module : rejectedModules) rejectedSema.declareGlobals(*module);
  for (auto &module : rejectedModules) rejectedSema.checkModule(*module);
  CHECK(DiagnosticEngine::hasErrors());
  FunctionDecl *rollback = nullptr;
  for (auto &module : rejectedModules)
    for (auto &function : module->Functions)
      if (function->Name == "__native_rollback_test") rollback = function.get();
  CHECK(rollback && rollback->Body && rollback->Body->Statements.size() == 3);
  auto *binding = dynamic_cast<VariableDecl *>(rollback->Body->Statements[0].get());
  auto *after = dynamic_cast<ExprStmt *>(rollback->Body->Statements[2].get());
  auto *read = after ? dynamic_cast<CallExpr *>(after->Expression.get()) : nullptr;
  CHECK(binding && binding->Init->NativeSyncFactoryOrigin && read && read->Args.size() == 1);
  CHECK(read->Args[0]->NativeSyncFactoryOrigin == binding->Init->NativeSyncFactoryOrigin);
  size_t errors = 0;
  for (const auto &record : DiagnosticEngine::records())
    if (record.Level == DiagLevel::Error) {
      ++errors;
      CHECK(record.Code != "E0438" && record.Code != "E0410");
    }
  CHECK(errors == 1); // only the intended argument rejection, no move cascade
  std::cout << "native factory flow: view/type isolation, unchanged types, move, distinct owners, "
               "branch invalidation and rejected-call rollback pass\n";
}
