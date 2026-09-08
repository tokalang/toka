// Synthetic compiler-plan/LLVM-emitter qualification only. This deliberately
// does not claim a source-language Sema, capture, or std/thread integration.
#include "toka/ThreadHandoffPlan.h"
#include "toka_thread_handoff_v1.h"
#include "llvm/ExecutionEngine/ExecutionEngine.h"
#include "llvm/ExecutionEngine/MCJIT.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Target/TargetMachine.h"
#include <cstring>
#include <iostream>
#include <memory>

#define CHECK(condition) do { if (!(condition)) { \
  std::cerr << "CHECK failed at " << __LINE__ << ": " #condition "\n"; \
  return __LINE__; } } while (false)

namespace {
struct Carrier { void *Environment; void *Invoke; void *Drop; };
struct Packet { Carrier Function; uint64_t Argument; };
struct Environment { uint64_t Seed; unsigned *Drops; };
struct Pair { uint64_t Left, Right; };
unsigned Invokes = 0, Started = 0, Unstarted = 0, ResultDrops = 0, PacketFrees = 0;
unsigned RuntimeRequirements = 0;
void requireRuntime() {
  toka_thread_require_compiler_0_9_9_18_v1();
  ++RuntimeRequirements;
}

uint64_t repeatable(void *environment, uint64_t argument) {
  ++Invokes;
  return static_cast<Environment *>(environment)->Seed + argument;
}
uint64_t consuming(void *environment, uint64_t argument) {
  auto *env = static_cast<Environment *>(environment);
  ++Invokes;
  ++*env->Drops; // synthetic consuming invocation consumes capture.
  uint64_t result = env->Seed + argument;
  delete env;
  return result;
}
void fullCleanup(void *packet) {
  ++Unstarted;
  auto *p = static_cast<Packet *>(packet);
  auto *env = static_cast<Environment *>(p->Function.Environment);
  ++*env->Drops;
  delete env;
  delete p;
  ++PacketFrees;
}
void repeatableCleanup(void *packet) {
  ++Started;
  auto *p = static_cast<Packet *>(packet);
  auto *env = static_cast<Environment *>(p->Function.Environment);
  ++*env->Drops;
  delete env;
  delete p;
  ++PacketFrees;
}
void consumingCleanup(void *packet) {
  ++Started; // consumed environment must not be dereferenced or dropped again.
  delete static_cast<Packet *>(packet);
  ++PacketFrees;
}
void resultDrop(void *) { ++ResultDrops; }

llvm::Function *declareCleanup(llvm::Module &m, const char *name) {
  auto &c = m.getContext();
  auto *f = llvm::Function::Create(llvm::FunctionType::get(llvm::Type::getVoidTy(c),
      {llvm::PointerType::getUnqual(c)}, false), llvm::Function::ExternalLinkage, name, m);
  f->addFnAttr(llvm::Attribute::NoUnwind);
  return f;
}

toka::ThreadHandoffAdapterPlan planFor(llvm::Module &m) {
  toka::ThreadHandoffAdapterPlan p;
  // Identity only. Neither pointer is dereferenced by the standalone emitter.
  static int site, environment;
  p.Site = reinterpret_cast<const toka::Expr *>(&site);
  p.EnvironmentEdge = reinterpret_cast<const toka::Expr *>(&environment);
  p.SemaValidated = p.TransactionComplete = p.SpecializationQualified = true;
  p.EnvironmentDependenciesComplete = p.ResultDependenciesComplete = true;
  p.EnvironmentLifetimeAdmitted = p.ResultLifetimeAdmitted = true;
  p.SendValidated = p.CleanupComplete = p.RelocationValidated = true;
  p.InvokeNoUnwind = true;
  p.Mode = p.StartedCleanupMode = toka::ThreadCallableMode::Repeatable;
  p.ABIKey = TOKA_THREAD_HANDOFF_ABI_V1;
  p.ContractKey = "test-only/module:adapter/fn:edge/mode:repeatable";
  p.ResultTypeKey = "test-only/module:adapter/result:u64/abi:value";
  auto &c = m.getContext();
  auto *ptr = llvm::PointerType::getUnqual(c);
  p.CarrierType = llvm::StructType::get(c, {ptr, ptr, ptr});
  p.ResultType = llvm::Type::getInt64Ty(c);
  p.PacketType = llvm::StructType::get(c, {p.CarrierType, p.ResultType});
  p.InvokeType = llvm::FunctionType::get(p.ResultType, {ptr, p.ResultType}, false);
  p.Arguments.push_back({1, toka::ThreadArgumentPassing::Value});
  p.DropUnstartedPacket = declareCleanup(m, "test_unstarted");
  p.DropStartedPacket = declareCleanup(m, "test_repeatable_started");
  return p;
}
} // namespace

int main() {
  llvm::InitializeNativeTarget();
  llvm::InitializeNativeTargetAsmPrinter();
  llvm::LLVMContext context;
  auto module = std::make_unique<llvm::Module>("adapter-test", context);
  auto *m = module.get();
  std::string error;
  std::unique_ptr<llvm::ExecutionEngine> engine(llvm::EngineBuilder(std::move(module))
      .setEngineKind(llvm::EngineKind::JIT).setErrorStr(&error).create());
  CHECK(engine != nullptr);
  m->setDataLayout(engine->getDataLayout());
  auto p = planFor(*m);
  toka::ThreadHandoffAdapterArtifacts out;
  std::string reason;
  unsigned faults = 0;
  auto rejected = [&](const toka::ThreadHandoffAdapterPlan &candidate) {
    auto functions = m->size();
    auto globals = m->global_size();
    bool result = toka::emitThreadHandoffAdapters(*m, candidate, p.Site,
        p.EnvironmentEdge, "fault", out, reason);
    ++faults;
    return !result && !reason.empty() && !out.RunOnce && !out.EnvOps &&
           m->size() == functions && m->global_size() == globals;
  };
  for (bool toka::ThreadHandoffAdapterPlan::*member : {
       &toka::ThreadHandoffAdapterPlan::SemaValidated,
       &toka::ThreadHandoffAdapterPlan::TransactionComplete,
       &toka::ThreadHandoffAdapterPlan::SpecializationQualified,
       &toka::ThreadHandoffAdapterPlan::EnvironmentDependenciesComplete,
       &toka::ThreadHandoffAdapterPlan::ResultDependenciesComplete,
       &toka::ThreadHandoffAdapterPlan::EnvironmentLifetimeAdmitted,
       &toka::ThreadHandoffAdapterPlan::ResultLifetimeAdmitted,
       &toka::ThreadHandoffAdapterPlan::SendValidated,
       &toka::ThreadHandoffAdapterPlan::CleanupComplete,
       &toka::ThreadHandoffAdapterPlan::RelocationValidated,
       &toka::ThreadHandoffAdapterPlan::InvokeNoUnwind}) {
    auto bad = p; bad.*member = false; CHECK(rejected(bad));
  }
  { auto bad = p; bad.Site = nullptr; CHECK(rejected(bad)); }
  { auto bad = p; bad.EnvironmentEdge = p.Site; CHECK(rejected(bad)); }
  { auto bad = p; bad.ABIKey = "old-interface"; CHECK(rejected(bad)); }
  { auto bad = p; bad.ResultTypeKey.clear(); CHECK(rejected(bad)); }
  { auto bad = p; bad.ContractKey.clear(); CHECK(rejected(bad)); }
  { auto bad = p; bad.ContractKey = std::string("key\0suffix", 10); CHECK(rejected(bad)); }
  { auto bad = p; bad.ResultTypeKey = std::string("type\0suffix", 11); CHECK(rejected(bad)); }
  { auto bad = p; bad.CarrierField = 8; CHECK(rejected(bad)); }
  { auto bad = p; bad.InvokeField = bad.EnvironmentField; CHECK(rejected(bad)); }
  { auto bad = p; bad.Arguments[0].PacketField = 0; CHECK(rejected(bad)); }
  { auto bad = p; bad.Arguments.push_back(bad.Arguments[0]); CHECK(rejected(bad)); }
  { auto bad = p; bad.Arguments[0].Passing = toka::ThreadArgumentPassing::Address; CHECK(rejected(bad)); }
  { auto bad = p; bad.DropStartedPacket = nullptr; CHECK(rejected(bad)); }
  { auto bad = p; bad.ResultHasDrop = true; CHECK(rejected(bad)); }
  { auto bad = p; bad.ResultSRet = true; CHECK(rejected(bad)); }
  { auto bad = p; bad.InvokeCallingConvention = llvm::CallingConv::Fast; CHECK(rejected(bad)); }
  { auto bad = p; bad.Arguments[0].Passing = static_cast<toka::ThreadArgumentPassing>(99); CHECK(rejected(bad)); }
  { auto bad = p; bad.Mode = bad.StartedCleanupMode = static_cast<toka::ThreadCallableMode>(99); CHECK(rejected(bad)); }
  { auto bad = p; bad.Mode = toka::ThreadCallableMode::Consuming; CHECK(rejected(bad)); }
  { auto bad = p; bad.Mode = bad.StartedCleanupMode = toka::ThreadCallableMode::Consuming;
    bad.ConsumingEnvironmentUnique = true; bad.DropStartedPacket = bad.DropUnstartedPacket;
    CHECK(rejected(bad)); }
  {
    auto *badRequire = llvm::Function::Create(llvm::FunctionType::get(
        llvm::Type::getInt32Ty(context), false), llvm::Function::ExternalLinkage,
        "toka_thread_require_compiler_0_9_9_18_v1", *m);
    CHECK(rejected(p));
    badRequire->eraseFromParent();
  }

  toka::ThreadHandoffAdapterArtifacts repeat;
  CHECK(toka::emitThreadHandoffAdapters(*m, p, p.Site, p.EnvironmentEdge, "repeat", repeat, reason));
  for (auto &block : *repeat.MoveOut)
    for (auto &instruction : block)
      CHECK(!llvm::isa<llvm::CallBase>(instruction));
  CHECK(repeat.ResultOps->isConstant() && repeat.EnvOps->isConstant());
  auto *resultConstant = llvm::cast<llvm::ConstantStruct>(repeat.ResultOps->getInitializer());
  CHECK(llvm::cast<llvm::ConstantInt>(resultConstant->getOperand(1))->getZExtValue() == sizeof(TokaThreadResultOpsV1));
  auto *envConstant = llvm::cast<llvm::ConstantStruct>(repeat.EnvOps->getInitializer());
  CHECK(llvm::cast<llvm::ConstantInt>(envConstant->getOperand(1))->getZExtValue() == sizeof(TokaThreadEnvOpsV1));
  auto *resultLayout = m->getDataLayout().getStructLayout(resultConstant->getType());
  CHECK(resultLayout->getElementOffset(2) == offsetof(TokaThreadResultOpsV1, abi_key));
  CHECK(resultLayout->getElementOffset(4) == offsetof(TokaThreadResultOpsV1, value_size));
  CHECK(resultLayout->getElementOffset(6) == offsetof(TokaThreadResultOpsV1, move_out));
  auto *envLayout = m->getDataLayout().getStructLayout(envConstant->getType());
  CHECK(envLayout->getElementOffset(4) == offsetof(TokaThreadEnvOpsV1, result_ops));
  CHECK(envLayout->getElementOffset(5) == offsetof(TokaThreadEnvOpsV1, run_once));
  p.Mode = p.StartedCleanupMode = toka::ThreadCallableMode::Consuming;
  p.ConsumingEnvironmentUnique = true;
  p.DropStartedPacket = declareCleanup(*m, "test_consuming_started");
  p.ResultHasDrop = true;
  p.DropResult = declareCleanup(*m, "test_result_drop");
  toka::ThreadHandoffAdapterArtifacts consume;
  CHECK(toka::emitThreadHandoffAdapters(*m, p, p.Site, p.EnvironmentEdge, "consume", consume, reason));
  auto aggregate = p;
  aggregate.Mode = aggregate.StartedCleanupMode = toka::ThreadCallableMode::Repeatable;
  aggregate.DropStartedPacket = m->getFunction("test_repeatable_started");
  aggregate.ResultSRet = true;
  aggregate.ResultType = llvm::StructType::get(context,
      {llvm::Type::getInt64Ty(context), llvm::Type::getInt64Ty(context)});
  aggregate.ResultTypeKey = "test-only/result:pair/full-fields/abi:sret";
  auto *ptr = llvm::PointerType::getUnqual(context);
  aggregate.InvokeType = llvm::FunctionType::get(llvm::Type::getVoidTy(context),
      {ptr, ptr, ptr}, false);
  aggregate.Arguments[0].Passing = toka::ThreadArgumentPassing::Address;
  // Define a genuine LLVM sret callee. Casting a C void(Pair*, ...) callback
  // would be wrong on targets whose sret pointer uses a separate register.
  auto *sretInvoke = llvm::Function::Create(aggregate.InvokeType,
      llvm::Function::ExternalLinkage, "test_sret_invoke", *m);
  sretInvoke->addFnAttr(llvm::Attribute::NoUnwind);
  sretInvoke->addParamAttr(0, llvm::Attribute::get(context,
      llvm::Attribute::StructRet, aggregate.ResultType));
  llvm::IRBuilder<> sretBuilder(llvm::BasicBlock::Create(context, "entry", sretInvoke));
  auto *seed = sretBuilder.CreateLoad(sretBuilder.getInt64Ty(), sretInvoke->getArg(1));
  auto *argument = sretBuilder.CreateLoad(sretBuilder.getInt64Ty(), sretInvoke->getArg(2));
  sretBuilder.CreateStore(seed, sretBuilder.CreateStructGEP(aggregate.ResultType,
      sretInvoke->getArg(0), 0));
  sretBuilder.CreateStore(argument, sretBuilder.CreateStructGEP(aggregate.ResultType,
      sretInvoke->getArg(0), 1));
  sretBuilder.CreateRetVoid();
  toka::ThreadHandoffAdapterArtifacts pairArtifacts;
  CHECK(toka::emitThreadHandoffAdapters(*m, aggregate, p.Site, p.EnvironmentEdge,
      "pair", pairArtifacts, reason));
  CHECK(pairArtifacts.MoveOut->size() == 1);
  for (auto &instruction : pairArtifacts.MoveOut->getEntryBlock())
    CHECK(!llvm::isa<llvm::CallBase>(instruction));
  CHECK(!llvm::verifyModule(*m, &llvm::errs()));
  engine->addGlobalMapping(m->getFunction("test_unstarted"), reinterpret_cast<void *>(&fullCleanup));
  engine->addGlobalMapping(m->getFunction("test_repeatable_started"), reinterpret_cast<void *>(&repeatableCleanup));
  engine->addGlobalMapping(m->getFunction("test_consuming_started"), reinterpret_cast<void *>(&consumingCleanup));
  engine->addGlobalMapping(m->getFunction("test_result_drop"), reinterpret_cast<void *>(&resultDrop));
  engine->addGlobalMapping(m->getFunction("toka_thread_require_compiler_0_9_9_18_v1"),
      reinterpret_cast<void *>(&requireRuntime));
  engine->finalizeObject();
  auto *repeatOps = reinterpret_cast<const TokaThreadEnvOpsV1 *>(engine->getPointerToGlobal(repeat.EnvOps));
  auto *consumeOps = reinterpret_cast<const TokaThreadEnvOpsV1 *>(engine->getPointerToGlobal(consume.EnvOps));
  CHECK(repeatOps && consumeOps);
  CHECK(std::strcmp(repeatOps->abi_key, TOKA_THREAD_HANDOFF_ABI_V1) == 0);
  CHECK(repeatOps->result_ops->value_size == sizeof(uint64_t));
  unsigned firstDrops = 0, secondDrops = 0, notStartedDrops = 0, pairDrops = 0;
  auto *a = new Packet{{new Environment{31, &firstDrops}, reinterpret_cast<void *>(&repeatable), nullptr}, 11};
  auto *b = new Packet{{new Environment{40, &secondDrops}, reinterpret_cast<void *>(&consuming), nullptr}, 2};
  auto *c = new Packet{{new Environment{99, &notStartedDrops}, reinterpret_cast<void *>(&consuming), nullptr}, 0};
  uint64_t result = 0, moved = 0;
  repeatOps->run_once(a, &result);
  CHECK(result == 42 && firstDrops == 1 && Invokes == 1 && Started == 1);
  repeatOps->result_ops->move_out(&result, &moved);
  CHECK(moved == 42 && ResultDrops == 0 && Invokes == 1);
  repeatOps->result_ops->drop_live(&moved);
  CHECK(ResultDrops == 0);
  consumeOps->run_once(b, &result);
  CHECK(result == 42 && secondDrops == 1 && Invokes == 2 && Started == 2);
  consumeOps->drop_unstarted(c);
  CHECK(notStartedDrops == 1 && Invokes == 2 && Unstarted == 1);
  consumeOps->result_ops->drop_live(&result);
  CHECK(ResultDrops == 1);
  auto *pairOps = reinterpret_cast<const TokaThreadEnvOpsV1 *>(engine->getPointerToGlobal(pairArtifacts.EnvOps));
  auto *d = new Packet{{new Environment{71, &pairDrops},
      reinterpret_cast<void *>(engine->getFunctionAddress("test_sret_invoke")), nullptr}, 83};
  Pair pairResult{}, movedPair{};
  pairOps->run_once(d, &pairResult);
  CHECK(pairResult.Left == 71 && pairResult.Right == 83 && pairDrops == 1);
  pairOps->result_ops->move_out(&pairResult, &movedPair);
  CHECK(movedPair.Left == 71 && movedPair.Right == 83 && ResultDrops == 1);
  pairOps->result_ops->drop_live(&movedPair);
  CHECK(ResultDrops == 2 && PacketFrees == 4 && Invokes == 2 && RuntimeRequirements == 4);

  // Actual v1 runtime consumes JIT-produced descriptors. This proves shared
  // private ABI interoperation, still using synthetic qualified compiler facts.
  TokaThreadPrepared *prepared = nullptr;
  TokaThreadControl *handle = nullptr;
  TokaThreadResultLease *lease = nullptr;
  int32_t native = 0;
  unsigned threadDrops = 0;
  auto *threadPacket = new Packet{{new Environment{19, &threadDrops},
      reinterpret_cast<void *>(&repeatable), nullptr}, 23};
  CHECK(toka_thread_prepare_v1(repeatOps, threadPacket, &prepared) == TOKA_THREAD_OK_V1);
  CHECK(prepared && !handle);
  CHECK(toka_thread_start_v1(&prepared, &handle, &native) == TOKA_THREAD_OK_V1);
  CHECK(!prepared && handle && native == 0);
  CHECK(toka_thread_join_v1(&handle, repeatOps->result_ops, &lease, &native) == TOKA_THREAD_OK_V1);
  CHECK(!handle && lease && threadDrops == 1);
  moved = 0;
  toka_thread_take_result_v1(&lease, repeatOps->result_ops, &moved);
  CHECK(!lease && moved == 42 && PacketFrees == 5);
  unsigned threadPairDrops = 0;
  auto *threadPairPacket = new Packet{{new Environment{17, &threadPairDrops},
      reinterpret_cast<void *>(engine->getFunctionAddress("test_sret_invoke")), nullptr}, 29};
  CHECK(toka_thread_prepare_v1(pairOps, threadPairPacket, &prepared) == TOKA_THREAD_OK_V1);
  CHECK(toka_thread_start_v1(&prepared, &handle, &native) == TOKA_THREAD_OK_V1);
  CHECK(toka_thread_join_v1(&handle, pairOps->result_ops, &lease, &native) == TOKA_THREAD_OK_V1);
  Pair threadPair{};
  toka_thread_take_result_v1(&lease, pairOps->result_ops, &threadPair);
  CHECK(threadPair.Left == 17 && threadPair.Right == 29 && threadPairDrops == 1);
  CHECK(!lease && PacketFrees == 6 && ResultDrops == 2);
  pairOps->result_ops->drop_live(&threadPair);
  CHECK(ResultDrops == 3);

  // Cross-target descriptor sizes come from target DataLayout, not this host.
  llvm::Module target32("target32", context);
  target32.setDataLayout("e-p:32:32-i64:64-n8:16:32-S128");
  auto p32 = planFor(target32);
  toka::ThreadHandoffAdapterArtifacts a32;
  CHECK(toka::emitThreadHandoffAdapters(target32, p32, p32.Site,
      p32.EnvironmentEdge, "target32", a32, reason));
  auto *r32 = llvm::cast<llvm::ConstantStruct>(a32.ResultOps->getInitializer());
  auto *e32 = llvm::cast<llvm::ConstantStruct>(a32.EnvOps->getInitializer());
  CHECK(llvm::cast<llvm::ConstantInt>(r32->getOperand(1))->getZExtValue() == 32);
  CHECK(llvm::cast<llvm::ConstantInt>(e32->getOperand(1))->getZExtValue() == 28);
  CHECK(!llvm::verifyModule(target32, &llvm::errs()));
  std::cout << "thread adapter synthetic LLVM/JIT: " << faults
            << " no-mutation rejection cases; repeatable/consuming/unstarted/result cleanup; sret/by-address; target32 descriptors; native runtime scalar/struct join+take\n";
}
