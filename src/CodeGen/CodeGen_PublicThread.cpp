#include "toka/CodeGen.h"
#include "toka/ThreadHandoffPlan.h"
#include "toka_thread_handoff_v1.h"

namespace toka {
PhysEntity CodeGen::genPublicThread(const CallExpr *call) {
  auto p = call->PublicThreadSource;
#ifdef TOKA_BUILD_TESTING
  if (m_PublicThreadFault == "missing") p.reset();
  else if (p && !m_PublicThreadFault.empty()) {
    auto changed = std::shared_ptr<PublicThreadPlan>(new PublicThreadPlan(*p));
    if (m_PublicThreadFault == "site") changed->Site = nullptr;
    if (m_PublicThreadFault == "incomplete") changed->Complete = false;
    if (m_PublicThreadFault == "output") changed->OutputType = changed->HandleType;
    if (m_PublicThreadFault == "owner") changed->OwnerDefinition = nullptr;
    p = std::move(changed);
  }
#endif
  auto reject = [&](const char *why) -> PhysEntity {
    error(call, DiagID::ERR_CODEGEN, std::string("public thread: ") + why); return {};
  };
  if (!p) return reject("MissingPlan");
  if (!call->ResolvedFn || !p->Declaration) return reject("MissingDeclaration");
  if (!p->Complete) return reject("IncompletePlan");
#ifdef TOKA_BUILD_TESTING
  if (m_NativeSyncWitnessFault == "thread-list" && p->NativeOwnerCount) {
    auto changed = std::shared_ptr<PublicThreadPlan>(new PublicThreadPlan(*p));
    changed->NativeOwners.clear();
    p = std::move(changed);
  }
#endif
  if (p->NativeOwnerCount != p->NativeOwners.size()) return reject("NativeWitnessCountMismatch");
  for (const auto &owner : p->NativeOwners)
    if (!validateNativeSyncOwner(owner, call)) return {};
  if (!p->OwnerDefinition || p->OwnerDefinition != m_CurrentFunction)
    return reject("OwnerDefinitionMismatch");
  if (p->Site != call) return reject("SiteMismatch");
  if (p->Declaration != call->ResolvedFn) return reject("DeclarationMismatch");
  if (p->Kind != call->ResolvedFn->PublicThread) return reject("KindMismatch");
  if (!call->ResolvedType || !p->OutputType || !p->OutputType->equals(*call->ResolvedType))
    return reject("OutputTypeMismatch");
  auto *ptr = m_Builder.getPtrTy();
  auto *i32 = m_Builder.getInt32Ty();
  auto *voidTy = m_Builder.getVoidTy();
  struct RuntimeABI { const char *Name; llvm::Type *Return; unsigned Arity; };
  for (const auto &abi : {
      RuntimeABI{"toka_thread_drop_handle_v1", voidTy, 1},
      RuntimeABI{"toka_thread_detach_v1", i32, 2},
      RuntimeABI{"toka_thread_prepare_v1", i32, 3},
      RuntimeABI{"toka_thread_start_v1", i32, 3},
      RuntimeABI{"toka_thread_dispose_prepared_v1", voidTy, 1},
      RuntimeABI{"toka_thread_join_v1", i32, 4},
      RuntimeABI{"toka_thread_take_result_v1", voidTy, 3}}) {
    auto *existing = m_Module->getNamedValue(abi.Name);
    if (!existing) continue;
    auto *decl = llvm::dyn_cast<llvm::Function>(existing);
    if (!decl || decl->getCallingConv() != llvm::CallingConv::C ||
        decl->getFunctionType() != llvm::FunctionType::get(abi.Return,
            std::vector<llvm::Type *>(abi.Arity, ptr), false))
      return reject("RuntimeABIMismatch");
  }
  auto *handleTy = llvm::dyn_cast_or_null<llvm::StructType>(getLLVMType(p->HandleType));
  auto *outputTy = getLLVMType(p->OutputType);
  if (!handleTy || handleTy->getNumElements() != 2 ||
      handleTy->getElementType(0) != getIntPtrTy() || handleTy->getElementType(1) != getIntPtrTy() || !outputTy)
    return reject("HandleLayoutMismatch");
  auto runtime = [&](const char *name, llvm::Type *ret, unsigned count) {
    return m_Module->getOrInsertFunction(name,
        llvm::FunctionType::get(ret, std::vector<llvm::Type *>(count, ptr), false));
  };
  auto emptySlot = [&](const char *name) {
    auto *slot = createEntryBlockAlloca(ptr, nullptr, name);
    m_Builder.CreateStore(llvm::ConstantPointerNull::get(ptr), slot); return slot;
  };
  auto *native = createEntryBlockAlloca(i32, nullptr, "thread.native");
  m_Builder.CreateStore(m_Builder.getInt32(0), native);
  auto *function = m_Builder.GetInsertBlock()->getParent();
  auto *output = createEntryBlockAlloca(outputTy, nullptr, "thread.outcome");
  auto *done = llvm::BasicBlock::Create(m_Context, "thread.done", function);
  auto *errorTy = p->ErrorType ? getLLVMType(p->ErrorType) : nullptr;
  auto outcome = [&](bool ok, llvm::Value *value) {
    m_Builder.CreateStore(llvm::Constant::getNullValue(outputTy), output);
    m_Builder.CreateStore(m_Builder.getInt8(ok ? p->OkTag : p->ErrTag),
        m_Builder.CreateStructGEP(outputTy, output, 0));
    if (value) m_Builder.CreateStore(value, m_Builder.CreateStructGEP(outputTy, output, 1));
    m_Builder.CreateBr(done);
  };
  auto failure = [&](llvm::Value *status) {
    llvm::Value *value = llvm::UndefValue::get(errorTy);
    value = m_Builder.CreateInsertValue(value, status, 0);
    value = m_Builder.CreateInsertValue(value, m_Builder.CreateLoad(i32, native), 1);
    outcome(false, value);
  };
  const bool spawn = p->Kind == PublicThreadKind::Spawn || p->Kind == PublicThreadKind::SpawnState;
  if (!spawn) {
    if (call->Args.size() != 1 || p->Handle != call->Args[0].get()) return reject("HandleEdgeMismatch");
    auto *handle = genAddr(p->Handle);
    if (!handle) return reject("MissingHandleAddress");
    auto *control = m_Builder.CreateStructGEP(handleTy, handle, 0);
    if (p->Kind == PublicThreadKind::Drop) {
      auto *dropped = m_Builder.CreateCall(runtime("toka_thread_drop_handle_v1", voidTy, 1), {control});
      m_Builder.CreateBr(done);
      m_Builder.SetInsertPoint(done);
      return PhysEntity(dropped, "()", voidTy, false);
    }
    if (!errorTy || !outputTy->isStructTy() || outputTy->getStructNumElements() != 2)
      return reject("ResultLayoutMismatch");
    llvm::Value *status = nullptr;
    llvm::Value *result = nullptr;
    if (p->Kind == PublicThreadKind::Join) {
      auto *closed = llvm::BasicBlock::Create(m_Context, "thread.closed", function);
      auto *join = llvm::BasicBlock::Create(m_Context, "thread.join", function);
      m_Builder.CreateCondBr(m_Builder.CreateICmpEQ(m_Builder.CreateLoad(getIntPtrTy(), control),
          llvm::ConstantInt::get(getIntPtrTy(), 0)), closed, join);
      m_Builder.SetInsertPoint(closed); failure(m_Builder.getInt32(TOKA_THREAD_CLOSED_V1));
      m_Builder.SetInsertPoint(join);
      auto *joinWord = m_Builder.CreateLoad(getIntPtrTy(), m_Builder.CreateStructGEP(handleTy, handle, 1));
      auto *joinFn = m_Builder.CreateIntToPtr(joinWord, ptr);
      auto *resultTy = getLLVMType(p->ResultType);
      if (!resultTy || !resultTy->isSized()) return reject("ResultStorageUnqualified");
      result = createEntryBlockAlloca(resultTy, nullptr, "thread.result");
      status = m_Builder.CreateCall(llvm::FunctionType::get(i32, {ptr, ptr, ptr}, false),
          joinFn, {control, result, native});
    } else if (p->Kind == PublicThreadKind::Detach) {
      status = m_Builder.CreateCall(runtime("toka_thread_detach_v1", i32, 2), {control, native});
    } else return reject("OperationMismatch");
    auto *success = llvm::BasicBlock::Create(m_Context, "thread.success", function);
    auto *failed = llvm::BasicBlock::Create(m_Context, "thread.failed", function);
    m_Builder.CreateCondBr(m_Builder.CreateICmpEQ(status, m_Builder.getInt32(0)), success, failed);
    m_Builder.SetInsertPoint(failed); failure(status);
    m_Builder.SetInsertPoint(success);
    llvm::Value *returned = m_Builder.getInt8(0);
    if (result) returned = m_Builder.CreateLoad(getLLVMType(p->ResultType), result);
    outcome(true, returned);
  } else {
    const bool stateful = p->Kind == PublicThreadKind::SpawnState;
    if (call->Args.size() != (stateful ? 2u : 1u) || p->Callable != call->Args[stateful ? 1 : 0].get() ||
        (stateful && p->State != call->Args[0].get()) || !p->Invoke || !p->EnvironmentType || !p->ResultType)
      return reject("SourceEdgeMismatch");
    if (!p->CallableType || !p->Callable->ResolvedType ||
        !p->CallableType->equals(*p->Callable->ResolvedType) ||
        !p->Invoke->ResolvedReturnType || !p->ResultType->equals(*p->Invoke->ResolvedReturnType) ||
        (p->Invoke->ClosureReceiver == CallableReceiverMode::Consuming) != p->Consuming)
      return reject("SourceContractMismatch");
    if (p->EnvironmentConstruction) {
      const Expr *construction = p->Callable;
      while (construction) {
        if (auto *cast = dynamic_cast<const CastExpr *>(construction); cast && cast->Kind == CastKind::Ascription)
          construction = cast->Expression.get();
        else if (auto *unsafe = dynamic_cast<const UnsafeExpr *>(construction)) construction = unsafe->Expression.get();
        else break;
      }
      if (construction != p->EnvironmentConstruction || !dynamic_cast<const ClosureExpr *>(construction) ||
          !construction->ResolvedType ||
          !construction->ResolvedType->withAttributes(false, false)->equals(*p->EnvironmentType))
        return reject("ConstructionEdgeMismatch");
    }
    auto *envTy = getLLVMType(p->EnvironmentType);
    auto *resultTy = getLLVMType(p->ResultType);
    auto *carrierTy = p->Dynamic ? llvm::StructType::get(m_Context, {ptr, ptr, ptr})
                                 : llvm::StructType::get(m_Context, {ptr, ptr});
    std::vector<llvm::Type *> fields{carrierTy};
    if (!p->Dynamic) fields.push_back(envTy);
    const unsigned stateIndex = fields.size();
    if (stateful) fields.push_back(getLLVMType(p->StateType));
    auto *packetTy = llvm::StructType::get(m_Context, fields);
    auto *invoke = m_Module->getFunction(p->EnvironmentType->getSoulName() + "___invoke");
    if (!envTy || !resultTy || !invoke || !errorTy || !packetTy->isSized())
      return reject("ActualLayoutOrInvokeMissing");
    const std::string prefix = "__toka_public_thread_" + std::to_string(m_ThreadHandoffAdapterIndex++);
    auto freeFn = m_Module->getOrInsertFunction("free", llvm::FunctionType::get(voidTy, {ptr}, false));
    auto packetCleanup = [&](const std::string &name, bool started) {
      llvm::IRBuilderBase::InsertPointGuard guard(m_Builder);
      auto *f = llvm::Function::Create(llvm::FunctionType::get(voidTy, {ptr}, false),
          llvm::GlobalValue::InternalLinkage, name, m_Module.get());
      f->addFnAttr(llvm::Attribute::NoUnwind);
      m_Builder.SetInsertPoint(llvm::BasicBlock::Create(m_Context, "entry", f));
      m_Builder.SetCurrentDebugLocation(llvm::DebugLoc());
      if (p->Dynamic) emitDynFnRelease(m_Builder.CreateLoad(carrierTy,
          m_Builder.CreateStructGEP(packetTy, f->getArg(0), 0)), !started || !p->Consuming);
      else if (!started || !p->Consuming)
        emitDropForType(m_Builder.CreateStructGEP(packetTy, f->getArg(0), 1), p->EnvironmentType);
      if (stateful && p->StateHasDrop)
        emitDropForType(m_Builder.CreateStructGEP(packetTy, f->getArg(0), stateIndex), p->StateType);
      m_Builder.CreateCall(freeFn, {f->getArg(0)}); m_Builder.CreateRetVoid();
      return f;
    };
    ThreadHandoffAdapterPlan adapter;
    adapter.Site = call; adapter.EnvironmentEdge = p->Callable;
    adapter.SemaValidated = adapter.TransactionComplete = adapter.SpecializationQualified = p->Complete;
    adapter.EnvironmentDependenciesComplete = adapter.ResultDependenciesComplete = p->Complete;
    adapter.EnvironmentLifetimeAdmitted = adapter.ResultLifetimeAdmitted = p->Complete;
    adapter.SendValidated = adapter.CleanupComplete = adapter.RelocationValidated = p->Complete;
    adapter.InvokeNoUnwind = true;
    adapter.ConsumingEnvironmentUnique = p->Consuming;
    adapter.Mode = adapter.StartedCleanupMode = p->Consuming ? ThreadCallableMode::Consuming : ThreadCallableMode::Repeatable;
    adapter.ABIKey = TOKA_THREAD_HANDOFF_ABI_V1;
    adapter.ContractKey = p->ResultIdentity + ";edge:" + prefix;
    adapter.ResultTypeKey = p->ResultIdentity + ";target:" + m_Module->getTargetTriple() + ";layout:" + m_Module->getDataLayoutStr();
    adapter.PacketType = packetTy; adapter.CarrierType = carrierTy;
    adapter.ResultType = resultTy; adapter.ResultSRet = shouldReturnSRet(p->ResultType);
    adapter.ResultUnit = p->ResultType->isUnit(); adapter.ResultHasDrop = p->ResultHasDrop;
    adapter.InvokeType = invoke->getFunctionType(); adapter.InvokeCallingConvention = invoke->getCallingConv();
    adapter.DropUnstartedPacket = packetCleanup(prefix + ".cancel", false);
    adapter.DropStartedPacket = packetCleanup(prefix + ".finish", true);
    if (stateful) {
      const auto &arg = p->Invoke->Args[1];
      auto type = arg.ResolvedType;
      const bool direct = type && !type->isPointer() && !type->isReference();
      const bool capture = (direct && (fields[stateIndex]->isAggregateType() || arg.IsValueMutable)) ||
          arg.IsRebindable || (arg.IsUnique && !arg.IsCeded) || arg.IsShared || (type && type->isSharedPtr());
      adapter.Arguments.push_back({stateIndex, capture ? ThreadArgumentPassing::Address : ThreadArgumentPassing::Value});
    }
    if (p->ResultHasDrop) {
      llvm::IRBuilderBase::InsertPointGuard guard(m_Builder);
      adapter.DropResult = llvm::Function::Create(llvm::FunctionType::get(voidTy, {ptr}, false),
          llvm::GlobalValue::InternalLinkage, prefix + ".typed_drop", m_Module.get());
      adapter.DropResult->addFnAttr(llvm::Attribute::NoUnwind);
      m_Builder.SetInsertPoint(llvm::BasicBlock::Create(m_Context, "entry", adapter.DropResult));
      m_Builder.SetCurrentDebugLocation(llvm::DebugLoc());
      emitDropForType(adapter.DropResult->getArg(0), p->ResultType); m_Builder.CreateRetVoid();
    }
    ThreadHandoffAdapterArtifacts artifacts;
    std::string why;
    if (!emitThreadHandoffAdapters(*m_Module, adapter, call, p->Callable, prefix, artifacts, why))
      return reject(why.c_str());
    llvm::Value *stateValue = stateful ? genExpr(p->State).load(m_Builder) : nullptr;
    auto incoming = genExpr(p->EnvironmentConstruction ? p->EnvironmentConstruction : p->Callable);
    auto *value = incoming.load(m_Builder);
    if (!value || (stateful && !stateValue)) return reject("SourceValueMissing");
    llvm::Value *environment = nullptr;
    if (!p->Dynamic) {
      if (value->getType() == envTy) environment = value;
      else if (value->getType() == carrierTy)
        environment = m_Builder.CreateLoad(envTy, m_Builder.CreateExtractValue(value, 0));
      else return reject("ActualCarrierMismatch");
    } else if (value->getType() != carrierTy) return reject("ActualCarrierMismatch");
    auto mallocFn = m_Module->getOrInsertFunction("malloc", llvm::FunctionType::get(ptr, {getIntPtrTy()}, false));
    auto *packet = m_Builder.CreateCall(mallocFn, {llvm::ConstantInt::get(getIntPtrTy(),
        m_Module->getDataLayout().getTypeAllocSize(packetTy).getFixedValue())});
    auto *allocated = llvm::BasicBlock::Create(m_Context, "thread.allocated", function);
    auto *allocationFailed = llvm::BasicBlock::Create(m_Context, "thread.allocation.failed", function);
    m_Builder.CreateCondBr(m_Builder.CreateIsNotNull(packet), allocated, allocationFailed);
    m_Builder.SetInsertPoint(allocationFailed);
    if (p->Dynamic) emitDynFnRelease(value, true);
    else { auto *temp = createEntryBlockAlloca(envTy, nullptr, "thread.unstarted.env");
      m_Builder.CreateStore(environment, temp); emitDropForType(temp, p->EnvironmentType); }
    if (stateful && p->StateHasDrop) { auto *temp = createEntryBlockAlloca(fields[stateIndex], nullptr, "thread.unstarted.state");
      m_Builder.CreateStore(stateValue, temp); emitDropForType(temp, p->StateType); }
    failure(m_Builder.getInt32(TOKA_THREAD_ALLOCATION_V1));
    m_Builder.SetInsertPoint(allocated);
    if (!p->Dynamic) {
      auto *envAddress = m_Builder.CreateStructGEP(packetTy, packet, 1);
      m_Builder.CreateStore(environment, envAddress);
      value = llvm::UndefValue::get(carrierTy);
      value = m_Builder.CreateInsertValue(value, envAddress, 0);
      value = m_Builder.CreateInsertValue(value, invoke, 1);
    }
    m_Builder.CreateStore(value, m_Builder.CreateStructGEP(packetTy, packet, 0));
    if (stateful) m_Builder.CreateStore(stateValue, m_Builder.CreateStructGEP(packetTy, packet, stateIndex));
    auto *control = emptySlot("thread.control");
    auto *status = m_Builder.CreateCall(artifacts.StartOwned, {packet, control, native});
    auto *started = llvm::BasicBlock::Create(m_Context, "thread.started", function);
    auto *failed = llvm::BasicBlock::Create(m_Context, "thread.start.failed", function);
    m_Builder.CreateCondBr(m_Builder.CreateICmpEQ(status, m_Builder.getInt32(0)), started, failed);
    m_Builder.SetInsertPoint(failed); failure(status);
    m_Builder.SetInsertPoint(started);
    llvm::Value *handle = llvm::UndefValue::get(handleTy);
    handle = m_Builder.CreateInsertValue(handle, m_Builder.CreatePtrToInt(m_Builder.CreateLoad(ptr, control), getIntPtrTy()), 0);
    handle = m_Builder.CreateInsertValue(handle, m_Builder.CreatePtrToInt(artifacts.JoinOwned, getIntPtrTy()), 1);
    outcome(true, handle);
  }
  m_Builder.SetInsertPoint(done);
  return PhysEntity(output, p->OutputType->toString(), outputTy, true);
}
} // namespace toka
