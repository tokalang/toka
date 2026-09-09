#include "toka/CodeGen.h"
#include "toka/ThreadHandoffPlan.h"
#include "toka_thread_handoff_v1.h"

namespace toka {
PhysEntity CodeGen::genThreadHandoffProbe(const CallExpr *call) {
  auto source = call->ThreadHandoffSource;
  auto reject = [&](const char *why) -> PhysEntity {
    error(call, DiagID::ERR_CODEGEN, std::string("thread handoff: ") + why);
    return {};
  };
#ifdef TOKA_BUILD_TESTING
  if (m_ThreadHandoffSourceFault == "missing") source.reset();
  else if (source && !m_ThreadHandoffSourceFault.empty()) {
    auto changed = std::shared_ptr<ThreadHandoffSourcePlan>(new ThreadHandoffSourcePlan(*source));
    if (m_ThreadHandoffSourceFault == "source") changed->Source = nullptr;
    if (m_ThreadHandoffSourceFault == "result") changed->ResultType = source->CallableType;
    if (m_ThreadHandoffSourceFault == "cleanup") changed->Complete = false;
    source = std::move(changed);
  }
#endif
  if (!source || !source->Complete || source->Site != call || call->Args.size() != 1 ||
      source->Source != call->Args[0].get() || source->Declaration != call->ResolvedFn ||
      source->Kind != call->ResolvedFn->ThreadProbe)
    return reject("MissingOrMismatchedSourcePlan");
  auto actual = std::dynamic_pointer_cast<DynFnType>(call->Args[0]->ResolvedType);
  if (!actual || !source->InvokeDeclaration || !source->InvokeDeclaration->IsClosureInvoke ||
      (source->InvokeDeclaration->ClosureReceiver == CallableReceiverMode::Consuming) != source->Consuming ||
      !source->CallableType || !source->ResultType || !actual->ReturnType ||
      !actual->equals(*source->CallableType) || !actual->ReturnType->equals(*source->ResultType) ||
      (getCallableReceiverMode(*actual) == CallableReceiverMode::Consuming) != source->Consuming)
    return reject("ActualCallableOrResultMismatch");
  auto *carrierType = llvm::dyn_cast_or_null<llvm::StructType>(getLLVMType(source->CallableType));
  auto *resultType = getLLVMType(source->ResultType);
  if (!carrierType || carrierType->getNumElements() != 3 || !resultType || !resultType->isSized())
    return reject("IncompleteCarrierOrResultLayout");
  auto *packetType = llvm::StructType::get(m_Context, std::vector<llvm::Type *>{carrierType});
  auto *ptr = m_Builder.getPtrTy();
  auto *voidType = m_Builder.getVoidTy();
  const auto &layout = m_Module->getDataLayout();
  const std::string prefix = "__toka_thread_source_" + std::to_string(m_ThreadHandoffAdapterIndex++);
  auto freeFunction = m_Module->getOrInsertFunction("free",
      llvm::FunctionType::get(voidType, {ptr}, false));

  auto packetCleanup = [&](const std::string &name, bool completeDrop) {
    llvm::IRBuilderBase::InsertPointGuard insertion(m_Builder);
    auto *function = llvm::Function::Create(llvm::FunctionType::get(voidType, {ptr}, false),
        llvm::GlobalValue::InternalLinkage, name, m_Module.get());
    function->addFnAttr(llvm::Attribute::NoUnwind);
    m_Builder.SetInsertPoint(llvm::BasicBlock::Create(m_Context, "entry", function));
    m_Builder.SetCurrentDebugLocation(llvm::DebugLoc());
    auto *address = m_Builder.CreateStructGEP(packetType, function->getArg(0), 0);
    auto *carrier = m_Builder.CreateLoad(carrierType, address);
    // Reuse exactly the accepted dyn-fn lifecycle paths. No capture offsets,
    // header/refcount algorithm or destructor responsibility is reconstructed.
    emitDynFnRelease(carrier, completeDrop);
    m_Builder.CreateCall(freeFunction, {function->getArg(0)});
    m_Builder.CreateRetVoid();
    return function;
  };
  ThreadHandoffAdapterPlan plan;
  plan.Site = call;
  plan.EnvironmentEdge = source->Source;
  plan.SemaValidated = plan.TransactionComplete = plan.SpecializationQualified = source->Complete;
  plan.EnvironmentDependenciesComplete = plan.ResultDependenciesComplete = source->Complete;
  plan.EnvironmentLifetimeAdmitted = plan.ResultLifetimeAdmitted = source->Complete;
  plan.SendValidated = plan.CleanupComplete = plan.RelocationValidated = source->Complete;
  plan.InvokeNoUnwind = true; // synchronous Toka probe; native unwind is not an admitted route
  plan.ConsumingEnvironmentUnique = source->Consuming;
  plan.Mode = plan.StartedCleanupMode = source->Consuming
      ? ThreadCallableMode::Consuming : ThreadCallableMode::Repeatable;
  plan.ABIKey = TOKA_THREAD_HANDOFF_ABI_V1;
  plan.ContractKey = source->ResultIdentity + ";mode:" + (source->Consuming ? "consume" : "repeat") +
                     ";edge:" + prefix;
  plan.ResultTypeKey = source->ResultIdentity + ";target:" + m_Module->getTargetTriple() +
                       ";layout:" + m_Module->getDataLayoutStr();
  plan.PacketType = packetType;
  plan.CarrierType = carrierType;
  plan.ResultType = resultType;
  plan.ResultHasDrop = source->ResultHasDrop;
  // The same ABI decision used by ordinary callable invocation and genFunction.
  // A small struct can return by value; struct-ness alone is not sret evidence.
  plan.ResultSRet = shouldReturnSRet(source->ResultType);
  plan.InvokeType = llvm::FunctionType::get(plan.ResultSRet ? voidType : resultType,
      plan.ResultSRet ? std::vector<llvm::Type *>{ptr, ptr} : std::vector<llvm::Type *>{ptr}, false);
  plan.DropUnstartedPacket = packetCleanup(prefix + ".cancel_packet", true);
  plan.DropStartedPacket = packetCleanup(prefix + ".finish_packet", !source->Consuming);
  if (source->ResultHasDrop) {
    llvm::IRBuilderBase::InsertPointGuard insertion(m_Builder);
    plan.DropResult = llvm::Function::Create(llvm::FunctionType::get(voidType, {ptr}, false),
        llvm::GlobalValue::InternalLinkage, prefix + ".typed_drop", m_Module.get());
    plan.DropResult->addFnAttr(llvm::Attribute::NoUnwind);
    m_Builder.SetInsertPoint(llvm::BasicBlock::Create(m_Context, "entry", plan.DropResult));
    m_Builder.SetCurrentDebugLocation(llvm::DebugLoc());
    emitDropForType(plan.DropResult->getArg(0), source->ResultType);
    m_Builder.CreateRetVoid();
  }
  ThreadHandoffAdapterArtifacts adapters;
  std::string reason;
  if (!emitThreadHandoffAdapters(*m_Module, plan, call, source->Source, prefix, adapters, reason))
    return reject(reason.c_str());

  auto incoming = genExpr(source->Source);
  llvm::Value *carrier = incoming.load(m_Builder);
  if (!carrier) return reject("MissingIncomingCarrier");
  if (carrier->getType() != carrierType) {
    const Expr *closure = source->Source;
    while (closure) {
      if (auto *cast = dynamic_cast<const CastExpr *>(closure); cast && cast->Kind == CastKind::Ascription)
        closure = cast->Expression.get();
      else if (auto *unsafe = dynamic_cast<const UnsafeExpr *>(closure)) closure = unsafe->Expression.get();
      else break;
    }
    carrier = emitDynFnClosureValue(closure, carrier, source->CallableType);
    if (!carrier || carrier->getType() != carrierType) return reject("ActualCarrierMismatch");
  }
  auto mallocFunction = m_Module->getOrInsertFunction("malloc",
      llvm::FunctionType::get(ptr, {getIntPtrTy()}, false));
  auto *packet = m_Builder.CreateCall(mallocFunction,
      {llvm::ConstantInt::get(getIntPtrTy(), layout.getTypeAllocSize(packetType).getFixedValue())});
  auto fatalFunction = m_Module->getOrInsertFunction("_Exit",
      llvm::FunctionType::get(voidType, {m_Builder.getInt32Ty()}, false));
  auto require = [&](llvm::Value *ok, auto cleanup) {
    auto *function = m_Builder.GetInsertBlock()->getParent();
    auto *good = llvm::BasicBlock::Create(m_Context, "thread.ok", function);
    auto *bad = llvm::BasicBlock::Create(m_Context, "thread.failed", function);
    m_Builder.CreateCondBr(ok, good, bad);
    m_Builder.SetInsertPoint(bad);
    cleanup();
    m_Builder.CreateCall(fatalFunction, {m_Builder.getInt32(TOKA_THREAD_FATAL_EXIT_V1)});
    m_Builder.CreateUnreachable();
    m_Builder.SetInsertPoint(good);
  };
  require(m_Builder.CreateIsNotNull(packet), [&] { emitDynFnRelease(carrier, true); });
  m_Builder.CreateStore(carrier, m_Builder.CreateStructGEP(packetType, packet, 0));
  auto runtime = [&](const char *name, llvm::Type *ret, unsigned count) {
    return m_Module->getOrInsertFunction(name,
        llvm::FunctionType::get(ret, std::vector<llvm::Type *>(count, ptr), false));
  };
  auto emptySlot = [&](const char *name) {
    auto *slot = createEntryBlockAlloca(ptr, nullptr, name);
    m_Builder.CreateStore(llvm::ConstantPointerNull::get(ptr), slot);
    return slot;
  };
  auto *prepared = emptySlot("thread.prepared");
  auto *status = m_Builder.CreateCall(runtime("toka_thread_prepare_v1", m_Builder.getInt32Ty(), 3),
                                    {adapters.EnvOps, packet, prepared});
  require(m_Builder.CreateICmpEQ(status, m_Builder.getInt32(0)),
          [&] { m_Builder.CreateCall(adapters.DropUnstarted, {packet}); });
  auto dispose = runtime("toka_thread_dispose_prepared_v1", voidType, 1);
  if (source->Kind == ThreadProbeKind::Discard) {
    auto *discarded = m_Builder.CreateCall(dispose, {prepared});
    return PhysEntity(discarded, "()", voidType, false);
  }
  auto *handle = emptySlot("thread.handle");
  auto *lease = emptySlot("thread.lease");
  auto *errorSlot = createEntryBlockAlloca(m_Builder.getInt32Ty(), nullptr, "thread.error");
  auto *started = m_Builder.CreateCall(runtime("toka_thread_start_v1", m_Builder.getInt32Ty(), 3),
                                     {prepared, handle, errorSlot});
  require(m_Builder.CreateICmpEQ(started, m_Builder.getInt32(0)),
          [&] { m_Builder.CreateCall(dispose, {prepared}); });
  auto *joined = m_Builder.CreateCall(runtime("toka_thread_join_v1", m_Builder.getInt32Ty(), 4),
                                    {handle, adapters.ResultOps, lease, errorSlot});
  require(m_Builder.CreateICmpEQ(joined, m_Builder.getInt32(0)), [&] {
    m_Builder.CreateCall(runtime("toka_thread_drop_handle_v1", voidType, 1), {handle});
  });
  if (source->Kind == ThreadProbeKind::RunAndDrop) {
    auto *dropped = m_Builder.CreateCall(runtime("toka_thread_drop_result_v1", voidType, 1), {lease});
    return PhysEntity(dropped, "()", voidType, false);
  }
  auto *destination = createEntryBlockAlloca(resultType, nullptr, "thread.result");
  m_Builder.CreateCall(runtime("toka_thread_take_result_v1", voidType, 3),
                       {lease, adapters.ResultOps, destination});
  return PhysEntity(destination, source->ResultType->toString(), resultType, true);
}
}
