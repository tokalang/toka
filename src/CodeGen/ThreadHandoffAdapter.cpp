// Copyright (c) 2026 YiZhonghua<zhyi@dpai.com>. All rights reserved.
#include "toka/ThreadHandoffPlan.h"
#include "toka_thread_handoff_v1.h"

#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Module.h"

namespace toka {
namespace {
constexpr const char *RequireSymbol = "toka_thread_require_compiler_0_9_9_18_v1";
bool pointer(llvm::Type *type) {
  return type && type->isPointerTy() && type->getPointerAddressSpace() == 0;
}

bool cleanup(llvm::Module &module, llvm::Function *function) {
  return function && function->getParent() == &module &&
         function->getCallingConv() == llvm::CallingConv::C &&
         function->doesNotThrow() && !function->isVarArg() &&
         function->getReturnType()->isVoidTy() && function->arg_size() == 1 &&
         pointer(function->getFunctionType()->getParamType(0));
}

bool validate(llvm::Module &module, const ThreadHandoffAdapterPlan &p,
              const Expr *site, const Expr *environment, std::string &reason) {
  auto reject = [&](const char *why) { reason = why; return false; };
  if (!site || !environment || p.Site != site || p.EnvironmentEdge != environment)
    return reject("ThreadHandoffEdgeMismatch");
  if (!p.SemaValidated || !p.TransactionComplete || !p.SpecializationQualified)
    return reject("ThreadHandoffNotQualified");
  if (!p.EnvironmentDependenciesComplete || !p.ResultDependenciesComplete ||
      !p.EnvironmentLifetimeAdmitted || !p.ResultLifetimeAdmitted ||
      !p.SendValidated || !p.CleanupComplete || !p.RelocationValidated)
    return reject("ThreadHandoffIncompleteFacts");
  if (p.ABIKey != TOKA_THREAD_HANDOFF_ABI_V1 || p.ContractKey.empty() ||
      p.ResultTypeKey.empty() || p.ContractKey.find('\0') != std::string::npos ||
      p.ResultTypeKey.find('\0') != std::string::npos ||
      module.getDataLayoutStr().empty())
    return reject("ThreadHandoffABIMismatch");
  if ((p.Mode != ThreadCallableMode::Repeatable &&
       p.Mode != ThreadCallableMode::Consuming) ||
      p.StartedCleanupMode != p.Mode ||
      (p.Mode == ThreadCallableMode::Consuming && !p.ConsumingEnvironmentUnique))
    return reject("ThreadHandoffCallableModeMismatch");
  if (!p.PacketType || !p.PacketType->isSized() || !p.CarrierType ||
      &p.PacketType->getContext() != &module.getContext() ||
      &p.CarrierType->getContext() != &module.getContext() ||
      module.getDataLayout().getTypeAllocSize(p.PacketType).isScalable() ||
      p.CarrierType->isOpaque() || p.CarrierField >= p.PacketType->getNumElements() ||
      p.PacketType->getElementType(p.CarrierField) != p.CarrierType ||
      p.EnvironmentField == p.InvokeField ||
      p.EnvironmentField >= p.CarrierType->getNumElements() ||
      p.InvokeField >= p.CarrierType->getNumElements() ||
      !pointer(p.CarrierType->getElementType(p.EnvironmentField)) ||
      !pointer(p.CarrierType->getElementType(p.InvokeField)))
    return reject("ThreadHandoffCarrierLayoutMismatch");
  if (!p.ResultType || !p.ResultType->isSized() ||
      &p.ResultType->getContext() != &module.getContext() ||
      module.getDataLayout().getTypeAllocSize(p.ResultType).isScalable() ||
      !p.InvokeType || &p.InvokeType->getContext() != &module.getContext() ||
      p.InvokeType->isVarArg() || !p.InvokeNoUnwind ||
      p.InvokeCallingConvention != llvm::CallingConv::C)
    return reject("ThreadHandoffInvokeIncomplete");
  unsigned head = p.ResultSRet ? 2 : 1;
  if (p.InvokeType->getNumParams() != head + p.Arguments.size() ||
      !pointer(p.InvokeType->getParamType(head - 1)) ||
      (p.ResultSRet && (!p.InvokeType->getReturnType()->isVoidTy() ||
                       !pointer(p.InvokeType->getParamType(0)))) ||
      (!p.ResultSRet && p.InvokeType->getReturnType() != p.ResultType))
    return reject("ThreadHandoffInvokeTypeMismatch");
  std::vector<bool> used(p.PacketType->getNumElements(), false);
  used[p.CarrierField] = true;
  for (unsigned i = 0; i < p.Arguments.size(); ++i) {
    const auto &argument = p.Arguments[i];
    if (argument.Passing != ThreadArgumentPassing::Value &&
        argument.Passing != ThreadArgumentPassing::Address)
      return reject("ThreadHandoffArgumentPassingUnknown");
    if (argument.PacketField >= used.size() || used[argument.PacketField])
      return reject("ThreadHandoffArgumentEdgeMismatch");
    used[argument.PacketField] = true;
    auto *actual = p.InvokeType->getParamType(head + i);
    if (argument.Passing == ThreadArgumentPassing::Address ? !pointer(actual)
        : actual != p.PacketType->getElementType(argument.PacketField))
      return reject("ThreadHandoffArgumentTypeMismatch");
  }
  if (!cleanup(module, p.DropUnstartedPacket) ||
      !cleanup(module, p.DropStartedPacket) ||
      (p.Mode == ThreadCallableMode::Consuming &&
       p.DropUnstartedPacket == p.DropStartedPacket) ||
      (p.ResultHasDrop ? !cleanup(module, p.DropResult) : p.DropResult != nullptr))
    return reject("ThreadHandoffCleanupMismatch");
  return true;
}
} // namespace

bool emitThreadHandoffAdapters(llvm::Module &module,
                              const ThreadHandoffAdapterPlan &p,
                              const Expr *expectedSite,
                              const Expr *expectedEnvironment,
                              const std::string &prefix,
                              ThreadHandoffAdapterArtifacts &out,
                              std::string &rejection) {
  out = {};
  rejection.clear();
  if (!validate(module, p, expectedSite, expectedEnvironment, rejection))
    return false;
  if (auto *existing = module.getNamedValue(RequireSymbol)) {
    auto *function = llvm::dyn_cast<llvm::Function>(existing);
    if (!function || !function->getReturnType()->isVoidTy() ||
        function->arg_size() != 0 || function->isVarArg() ||
        function->getCallingConv() != llvm::CallingConv::C) {
      rejection = "ThreadHandoffRuntimeABIMismatch";
      return false;
    }
  }
  for (const char *suffix : {".run", ".unstarted", ".move", ".drop",
                             ".result", ".environment", ".abi", ".type", ".contract"}) {
    if (prefix.empty() || module.getNamedValue(prefix + suffix)) {
      rejection = "ThreadHandoffSymbolCollision";
      return false;
    }
  }
  auto &context = module.getContext();
  const auto &layout = module.getDataLayout();
  llvm::IRBuilder<> builder(context);
  auto *ptr = builder.getPtrTy();
  auto *voidTy = builder.getVoidTy();
  auto requireRuntime = module.getOrInsertFunction(RequireSymbol,
      llvm::FunctionType::get(voidTy, false));
  auto makeFunction = [&](const char *suffix, unsigned arity) {
    auto *type = llvm::FunctionType::get(voidTy,
        std::vector<llvm::Type *>(arity, ptr), false);
    auto *function = llvm::Function::Create(type, llvm::GlobalValue::InternalLinkage,
                                           prefix + suffix, module);
    function->addFnAttr(llvm::Attribute::NoUnwind);
    builder.SetInsertPoint(llvm::BasicBlock::Create(context, "entry", function));
    return function;
  };
  out.RunOnce = makeFunction(".run", 2);
  builder.CreateCall(requireRuntime)->setDoesNotThrow();
  auto *packet = out.RunOnce->getArg(0);
  auto *result = out.RunOnce->getArg(1);
  auto *carrierAddress = builder.CreateStructGEP(p.PacketType, packet, p.CarrierField);
  auto *carrier = builder.CreateLoad(p.CarrierType, carrierAddress);
  auto *environment = builder.CreateExtractValue(carrier, p.EnvironmentField);
  auto *invoke = builder.CreateExtractValue(carrier, p.InvokeField);
  std::vector<llvm::Value *> arguments;
  if (p.ResultSRet) arguments.push_back(result);
  arguments.push_back(environment);
  for (const auto &argument : p.Arguments) {
    auto *address = builder.CreateStructGEP(p.PacketType, packet, argument.PacketField);
    arguments.push_back(argument.Passing == ThreadArgumentPassing::Address
        ? address : builder.CreateLoad(p.PacketType->getElementType(argument.PacketField), address));
  }
  auto *call = builder.CreateCall(p.InvokeType, invoke, arguments);
  call->setCallingConv(p.InvokeCallingConvention);
  call->setDoesNotThrow();
  if (p.ResultSRet)
    call->addParamAttr(0, llvm::Attribute::get(context, llvm::Attribute::StructRet, p.ResultType));
  else
    builder.CreateStore(call, result);
  builder.CreateCall(p.DropStartedPacket, {packet});
  builder.CreateRetVoid();

  out.DropUnstarted = makeFunction(".unstarted", 1);
  builder.CreateCall(requireRuntime)->setDoesNotThrow();
  builder.CreateCall(p.DropUnstartedPacket, {out.DropUnstarted->getArg(0)});
  builder.CreateRetVoid();
  out.MoveOut = makeFunction(".move", 2);
  // Typed load/store only: no memcpy library fallback, allocator, user move,
  // retain, cleanup, or recoverable operation after the runtime claims lease.
  auto *value = builder.CreateLoad(p.ResultType, out.MoveOut->getArg(0));
  builder.CreateStore(value, out.MoveOut->getArg(1));
  builder.CreateRetVoid();
  out.DropLive = makeFunction(".drop", 1);
  if (p.ResultHasDrop)
    builder.CreateCall(p.DropResult, {out.DropLive->getArg(0)});
  builder.CreateRetVoid();

  auto textConstant = [&](const char *suffix, const std::string &value) {
    auto *data = llvm::ConstantDataArray::getString(context, value, true);
    auto *global = new llvm::GlobalVariable(module, data->getType(), true,
        llvm::GlobalValue::PrivateLinkage, data, prefix + suffix);
    global->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);
    return global;
  };
  auto *abi = textConstant(".abi", p.ABIKey);
  auto *typeKey = textConstant(".type", p.ResultTypeKey);
  auto *contractKey = textConstant(".contract", p.ContractKey);
  auto *sizeTy = layout.getIntPtrType(context);
  auto *resultOpsType = llvm::StructType::get(context,
      {builder.getInt32Ty(), builder.getInt32Ty(), ptr, ptr, sizeTy, sizeTy, ptr, ptr});
  auto *envOpsType = llvm::StructType::get(context,
      {builder.getInt32Ty(), builder.getInt32Ty(), ptr, ptr, ptr, ptr, ptr});
  auto number = [&](uint64_t n) { return llvm::ConstantInt::get(sizeTy, n); };
  auto *resultConstant = llvm::ConstantStruct::get(resultOpsType,
      {builder.getInt32(TOKA_THREAD_HANDOFF_VERSION_V1),
       builder.getInt32(layout.getTypeAllocSize(resultOpsType).getFixedValue()),
       abi, typeKey, number(layout.getTypeAllocSize(p.ResultType).getFixedValue()),
       number(layout.getABITypeAlign(p.ResultType).value()), out.MoveOut, out.DropLive});
  out.ResultOps = new llvm::GlobalVariable(module, resultOpsType, true,
      llvm::GlobalValue::InternalLinkage, resultConstant, prefix + ".result");
  auto *envConstant = llvm::ConstantStruct::get(envOpsType,
      {builder.getInt32(TOKA_THREAD_HANDOFF_VERSION_V1),
       builder.getInt32(layout.getTypeAllocSize(envOpsType).getFixedValue()),
       abi, contractKey, out.ResultOps, out.RunOnce, out.DropUnstarted});
  out.EnvOps = new llvm::GlobalVariable(module, envOpsType, true,
      llvm::GlobalValue::InternalLinkage, envConstant, prefix + ".environment");
  return true;
}
} // namespace toka
