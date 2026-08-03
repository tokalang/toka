// Copyright (c) 2025 YiZhonghua<zhyi@dpai.com>. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
#include <llvm/IR/InlineAsm.h>
#include "toka/AST.h"
#include "toka/AssignmentStats.h"
#include "toka/CodeGen.h"
#include "toka/DiagnosticEngine.h"
#include "toka/SourceManager.h"
#include "toka/Type.h"
#include <cctype>
#include <iostream>
#include <set>
#include <typeinfo>

extern bool verboseMode;

namespace toka {

static const MemberExpr *getTerminalAssignmentMember(const Expr *expr) {
  if (!expr)
    return nullptr;
  if (auto *member = dynamic_cast<const MemberExpr *>(expr))
    return member;
  if (auto *unary = dynamic_cast<const UnaryExpr *>(expr))
    return getTerminalAssignmentMember(unary->RHS.get());
  if (auto *addr = dynamic_cast<const AddressOfExpr *>(expr))
    return getTerminalAssignmentMember(addr->Expression.get());
  if (auto *cast = dynamic_cast<const CastExpr *>(expr))
    return getTerminalAssignmentMember(cast->Expression.get());
  if (auto *postfix = dynamic_cast<const PostfixExpr *>(expr))
    return getTerminalAssignmentMember(postfix->LHS.get());
  return nullptr;
}

static bool terminalAssignmentMemberHasMorphology(const Expr *expr) {
  const MemberExpr *member = getTerminalAssignmentMember(expr);
  return member && Type::stripMorphology(member->Member) != member->Member;
}

static bool assignmentLowersThroughEnvelopeCarrier(const Expr *expr) {
  return terminalAssignmentMemberHasMorphology(expr) ||
         (expr && expr->ResolvedType && expr->ResolvedType->isReference());
}

static bool isFreshAllocationExpr(const Expr *expr) {
  while (expr) {
    if (auto *cast = dynamic_cast<const CastExpr *>(expr)) {
      expr = cast->Expression.get();
    } else if (auto *unsafeExpr = dynamic_cast<const UnsafeExpr *>(expr)) {
      expr = unsafeExpr->Expression.get();
    } else {
      break;
    }
  }
  return dynamic_cast<const NewExpr *>(expr) ||
         dynamic_cast<const AllocExpr *>(expr);
}

// A call which returns ^T transfers a fresh owning pointer to its caller, just
// as `new T` does.  When Sema inserts an implicit ^T -> ~T cast for such a
// value, CodeGen must build the first shared handle rather than store the raw
// pointer in the aggregate's { data, ref_count } field.
static bool isOwnedUniquePromotionSource(const Expr *expr) {
  while (expr) {
    if (auto *cast = dynamic_cast<const CastExpr *>(expr)) {
      expr = cast->Expression.get();
    } else if (auto *unsafeExpr = dynamic_cast<const UnsafeExpr *>(expr)) {
      expr = unsafeExpr->Expression.get();
    } else {
      break;
    }
  }
  if (!expr)
    return false;
  if (isFreshAllocationExpr(expr))
    return true;
  if (dynamic_cast<const CedeExpr *>(expr))
    return expr->ResolvedType && expr->ResolvedType->isUniquePtr();
  return (dynamic_cast<const CallExpr *>(expr) ||
          dynamic_cast<const MethodCallExpr *>(expr)) &&
         expr->ResolvedType && expr->ResolvedType->isUniquePtr();
}

void CodeGen::emitAcquire(llvm::Value *sharedHandle, std::shared_ptr<Type> pointeeType) {
  if (!sharedHandle || !sharedHandle->getType()->isStructTy())
    return;

  auto *ST = llvm::cast<llvm::StructType>(sharedHandle->getType());
  if (ST->getNumElements() < 2)
    return;

  // Option A: All shared pointer refcount operations are atomic by default (@arc)
  bool isAtomic = true;
  std::string pName = "null";
  if (pointeeType) {
    if (auto shapeTy = std::dynamic_pointer_cast<ShapeType>(pointeeType->getSoulType())) {
      pName = shapeTy->Name;
    } else {
      pName = pointeeType->toString();
    }
  }


  llvm::Value *refPtr =
      m_Builder.CreateExtractValue(sharedHandle, 1, "sh.acq_ref_ptr");
  llvm::Value *nn = m_Builder.CreateIsNotNull(refPtr, "sh.acq_nn");

  llvm::Function *f = m_Builder.GetInsertBlock()->getParent();
  llvm::BasicBlock *incBB =
      llvm::BasicBlock::Create(m_Context, "sh.acq_inc", f);
  llvm::BasicBlock *contBB =
      llvm::BasicBlock::Create(m_Context, "sh.acq_cont", f);
  m_Builder.CreateCondBr(nn, incBB, contBB);

  m_Builder.SetInsertPoint(incBB);
  
  if (isAtomic) {
    // Retain/Inc uses Monotonic (Relaxed) memory ordering for maximal throughput
    m_Builder.CreateAtomicRMW(llvm::AtomicRMWInst::Add, refPtr, m_Builder.getInt32(1), llvm::MaybeAlign(4), llvm::AtomicOrdering::Monotonic);
  } else {
    llvm::Value *cnt =
        m_Builder.CreateLoad(llvm::Type::getInt32Ty(m_Context), refPtr);
    llvm::Value *inc = m_Builder.CreateAdd(
        cnt, llvm::ConstantInt::get(llvm::Type::getInt32Ty(m_Context), 1));
    m_Builder.CreateStore(inc, refPtr);
  }
  
  m_Builder.CreateBr(contBB);

  m_Builder.SetInsertPoint(contBB);
}

void CodeGen::emitRelease(llvm::Value *sharedHandle, const TokaSymbol &sym, std::shared_ptr<Type> pointeeType) {
  if (!sharedHandle || !sharedHandle->getType()->isStructTy())
    return;
    
  // Option A: All shared pointer refcount operations are atomic by default (@arc)
  bool isAtomic = true;

  llvm::Value *refPtr =
      m_Builder.CreateExtractValue(sharedHandle, 1, "sh.rel_ref_ptr");
  llvm::Value *nn = m_Builder.CreateIsNotNull(refPtr, "sh.rel_nn");

  llvm::Function *f = m_Builder.GetInsertBlock()->getParent();
  llvm::BasicBlock *decBB =
      llvm::BasicBlock::Create(m_Context, "sh.rel_dec", f);
  llvm::BasicBlock *contBB =
      llvm::BasicBlock::Create(m_Context, "sh.rel_cont", f);
  m_Builder.CreateCondBr(nn, decBB, contBB);

  m_Builder.SetInsertPoint(decBB);
  
  llvm::Value *isZero = nullptr;
  if (isAtomic) {
    // Release/Dec uses AcquireRelease memory ordering to sync prior writes before dropping
    llvm::Value *oldCnt = m_Builder.CreateAtomicRMW(llvm::AtomicRMWInst::Sub, refPtr, m_Builder.getInt32(1), llvm::MaybeAlign(4), llvm::AtomicOrdering::AcquireRelease);
    isZero = m_Builder.CreateICmpEQ(oldCnt, llvm::ConstantInt::get(llvm::Type::getInt32Ty(m_Context), 1));
  } else {
    llvm::Value *cnt =
        m_Builder.CreateLoad(llvm::Type::getInt32Ty(m_Context), refPtr);
    llvm::Value *dec = m_Builder.CreateSub(
        cnt, llvm::ConstantInt::get(llvm::Type::getInt32Ty(m_Context), 1));
    m_Builder.CreateStore(dec, refPtr);
    isZero = m_Builder.CreateICmpEQ(
        dec, llvm::ConstantInt::get(llvm::Type::getInt32Ty(m_Context), 0));
  }

  llvm::BasicBlock *freeBB =
      llvm::BasicBlock::Create(m_Context, "sh.rel_free", f);
  m_Builder.CreateCondBr(isZero, freeBB, contBB);

  m_Builder.SetInsertPoint(freeBB);
  llvm::Value *data =
      m_Builder.CreateExtractValue(sharedHandle, 0, "sh.rel_data");

  // Call drop if exists
  if (sym.hasDrop) {
    std::string cleanName = "";
    if (sym.soulTypeObj) {
      cleanName = sym.soulTypeObj->getSoulType()->getSoulName();
    }
    std::shared_ptr<Type> soulType =
        sym.soulTypeObj ? sym.soulTypeObj->getSoulType() : nullptr;
    if (soulType && soulType->IsNullable && sym.soulType &&
        sym.soulType->isStructTy()) {
      llvm::StructType *nullableType =
          llvm::cast<llvm::StructType>(sym.soulType);
      llvm::Value *payload = m_Builder.CreateLoad(
          nullableType, data, "nullable_soul.shared_release_payload");
      llvm::Value *present = m_Builder.CreateExtractValue(
          payload, 1, "nullable_soul.shared_release_present");
      llvm::BasicBlock *dropPayloadBB = llvm::BasicBlock::Create(
          m_Context, "nullable_soul.shared_release_drop", f);
      llvm::BasicBlock *afterPayloadDropBB = llvm::BasicBlock::Create(
          m_Context, "nullable_soul.shared_release_done", f);
      m_Builder.CreateCondBr(present, dropPayloadBB, afterPayloadDropBB);
      m_Builder.SetInsertPoint(dropPayloadBB);
      emitDropCascade(data, cleanName);
      m_Builder.CreateBr(afterPayloadDropBB);
      m_Builder.SetInsertPoint(afterPayloadDropBB);
    } else {
      emitDropCascade(data, cleanName);
    }
  }

  llvm::Function *freeFn = m_Module->getFunction("free");
  if (freeFn) {
    m_Builder.CreateCall(freeFn,
                         m_Builder.CreateBitCast(data, m_Builder.getPtrTy()));
    m_Builder.CreateCall(freeFn,
                         m_Builder.CreateBitCast(refPtr, m_Builder.getPtrTy()));
  }
  m_Builder.CreateBr(contBB);

  m_Builder.SetInsertPoint(contBB);
}

llvm::Value *CodeGen::emitPromotion(llvm::Value *rawPtr,
                                    llvm::Type *targetHandleType,
                                    const TokaSymbol &sym) {
  if (!rawPtr || !targetHandleType || !targetHandleType->isStructTy())
    return rawPtr;

  llvm::Function *mallocFn = m_Module->getFunction("malloc");
  if (!mallocFn) {
    std::vector<llvm::Type *> args = {getIntPtrTy()};
    llvm::FunctionType *ft =
        llvm::FunctionType::get(m_Builder.getPtrTy(), args, false);
    mallocFn = llvm::Function::Create(ft, llvm::Function::ExternalLinkage,
                                      "malloc", m_Module.get());
  }

  llvm::Value *rcSz =
      llvm::ConstantInt::get(getIntPtrTy(), 4);
  llvm::Value *rawRC =
      m_Builder.CreateCall(mallocFn, rcSz, "sh.prom_rc_malloc");
  llvm::Value *refPtr = m_Builder.CreateBitCast(
      rawRC, llvm::PointerType::getUnqual(m_Context));
  m_Builder.CreateStore(
      llvm::ConstantInt::get(llvm::Type::getInt32Ty(m_Context), 1), refPtr);

  llvm::Value *dataPtr = m_Builder.CreateBitCast(
      rawPtr, targetHandleType->getStructElementType(0));
  llvm::Value *u = llvm::UndefValue::get(targetHandleType);
  llvm::Value *handle = m_Builder.CreateInsertValue(u, dataPtr, 0);
  handle = m_Builder.CreateInsertValue(handle, refPtr, 1);
  return handle;
}

llvm::StoreInst *CodeGen::emitSoulAssignment(llvm::Value *soulAddr,
                                             llvm::Value *rhsVal,
                                             llvm::Type *type) {
  if (!soulAddr || !rhsVal || !type)
    return nullptr;
  if (assignmentStatsEnabled())
    assignmentStats().LoweredSoulAssignments++;

  llvm::Value *finalRHS = rhsVal;
  llvm::Value *destAddr = soulAddr;
  if (finalRHS->getType() != type) {
    if (finalRHS->getType()->isIntegerTy() && type->isIntegerTy()) {
      finalRHS = m_Builder.CreateIntCast(finalRHS, type, false);
    } else if (finalRHS->getType()->isFloatingPointTy() &&
               type->isFloatingPointTy()) {
      finalRHS = m_Builder.CreateFPCast(finalRHS, type);
    } else {
      // For other types, try bitcast if sizes match, otherwise it might be an
      // error that Sema should have caught.
      if (m_Module->getDataLayout().getTypeStoreSize(finalRHS->getType()) ==
          m_Module->getDataLayout().getTypeStoreSize(type)) {
        if (finalRHS->getType()->isAggregateType() || type->isAggregateType()) {
          destAddr = m_Builder.CreatePointerCast(soulAddr, llvm::PointerType::get(finalRHS->getType(), 0));
        } else {
          finalRHS = m_Builder.CreateBitCast(finalRHS, type);
        }
      }
    }
  }

  return m_Builder.CreateStore(finalRHS, destAddr);
}

void CodeGen::emitEnvelopeRebind(llvm::Value *handleAddr, llvm::Value *rhsVal,
                                 const TokaSymbol &sym, const Expr *lhsExpr) {
  if (!handleAddr || !rhsVal)
    return;
  if (assignmentStatsEnabled())
    assignmentStats().LoweredEnvelopeRebindings++;

  if (sym.morphology == Morphology::Shared) {
    // 1. Release(Old)
    llvm::Value *oldVal =
        m_Builder.CreateLoad(rhsVal->getType(), handleAddr, "sh.old_handle");
    emitRelease(oldVal, sym, sym.soulTypeObj);
    // 2. Update(Handle) with new owning handle from genExpr
    markMemoryEvent(m_Builder.CreateStore(rhsVal, handleAddr), "rebind");
  } else if (sym.morphology == Morphology::Unique) {
    // Unique: simple Release(Old) + Update
    llvm::Value *oldVal =
        m_Builder.CreateLoad(rhsVal->getType(), handleAddr, "u.old_handle");
    llvm::Value *nn = m_Builder.CreateIsNotNull(oldVal, "u.old_nn");
    llvm::Function *f = m_Builder.GetInsertBlock()->getParent();
    llvm::BasicBlock *freeBB =
        llvm::BasicBlock::Create(m_Context, "u.rel_free", f);
    llvm::BasicBlock *contBB =
        llvm::BasicBlock::Create(m_Context, "u.rel_cont", f);
    m_Builder.CreateCondBr(nn, freeBB, contBB);

    m_Builder.SetInsertPoint(freeBB);
    if (sym.hasDrop) {
      std::string cleanName = "";
      if (sym.soulTypeObj) {
        cleanName = sym.soulTypeObj->getSoulType()->getSoulName();
      }
      std::shared_ptr<Type> soulType =
          sym.soulTypeObj ? sym.soulTypeObj->getSoulType() : nullptr;
      if (soulType && soulType->IsNullable && sym.soulType &&
          sym.soulType->isStructTy()) {
        llvm::StructType *nullableType =
            llvm::cast<llvm::StructType>(sym.soulType);
        llvm::Value *payload = m_Builder.CreateLoad(
            nullableType, oldVal, "nullable_soul.rebind_payload");
        llvm::Value *present = m_Builder.CreateExtractValue(
            payload, 1, "nullable_soul.rebind_present");
        llvm::BasicBlock *dropPayloadBB = llvm::BasicBlock::Create(
            m_Context, "nullable_soul.rebind_drop", f);
        llvm::BasicBlock *afterPayloadDropBB = llvm::BasicBlock::Create(
            m_Context, "nullable_soul.rebind_done", f);
        m_Builder.CreateCondBr(present, dropPayloadBB, afterPayloadDropBB);
        m_Builder.SetInsertPoint(dropPayloadBB);
        emitDropCascade(oldVal, cleanName);
        m_Builder.CreateBr(afterPayloadDropBB);
        m_Builder.SetInsertPoint(afterPayloadDropBB);
      } else {
        emitDropCascade(oldVal, cleanName);
      }
    }
    llvm::Function *freeFn = m_Module->getFunction("free");
    if (freeFn) {
      m_Builder.CreateCall(
          freeFn, m_Builder.CreateBitCast(oldVal, m_Builder.getPtrTy()));
    }
    m_Builder.CreateBr(contBB);
    m_Builder.SetInsertPoint(contBB);

    markMemoryEvent(m_Builder.CreateStore(rhsVal, handleAddr), "rebind");
  } else {
    // Raw/Ref: direct store
    markMemoryEvent(m_Builder.CreateStore(rhsVal, handleAddr), "rebind");
  }
}

llvm::Value *CodeGen::wrapFreshAllocationAsNullableSoul(
    llvm::Value *payloadPtr, llvm::StructType *soulType) {
  if (!payloadPtr || !payloadPtr->getType()->isPointerTy() || !soulType ||
      soulType->getNumElements() != 2 ||
      !soulType->getElementType(1)->isIntegerTy(1))
    return payloadPtr;

  llvm::Function *mallocFn = m_Module->getFunction("malloc");
  if (!mallocFn) {
    mallocFn = llvm::Function::Create(
        llvm::FunctionType::get(m_Builder.getPtrTy(), {getIntPtrTy()}, false),
        llvm::Function::ExternalLinkage, "malloc", m_Module.get());
  }
  llvm::Function *freeFn = m_Module->getFunction("free");
  if (!freeFn) {
    freeFn = llvm::Function::Create(
        llvm::FunctionType::get(llvm::Type::getVoidTy(m_Context),
                                {m_Builder.getPtrTy()}, false),
        llvm::Function::ExternalLinkage, "free", m_Module.get());
  }

  llvm::CallInst *wrapperPtr = m_Builder.CreateCall(
      mallocFn,
      {llvm::ConstantInt::get(getIntPtrTy(),
                              m_Module->getDataLayout().getTypeAllocSize(soulType))},
      "nullable_soul_alloc");
  markMemoryEvent(wrapperPtr, "allocate");

  llvm::Value *payload = m_Builder.CreateLoad(
      soulType->getElementType(0), payloadPtr, "nullable_soul_payload");
  llvm::Value *payloadAddr =
      m_Builder.CreateStructGEP(soulType, wrapperPtr, 0, "nullable_soul_value");
  llvm::Value *presentAddr = m_Builder.CreateStructGEP(
      soulType, wrapperPtr, 1, "nullable_soul_present");
  m_Builder.CreateStore(payload, payloadAddr);
  m_Builder.CreateStore(llvm::ConstantInt::getTrue(m_Context), presentAddr);
  markMemoryEvent(m_Builder.CreateCall(freeFn, {payloadPtr}), "deallocate");
  return wrapperPtr;
}

PhysEntity CodeGen::emitAssignment(const Expr *lhsExpr, const Expr *rhsExpr,
                                   const BinaryExpr *assignmentSite) {
  auto verifyLowering = [&](AssignmentLoweringCarrier carrier) {
    if (!assignmentSite)
      return;
    bool agrees =
        (assignmentSite->AssignmentKind == AssignmentSemanticKind::Payload &&
         carrier == AssignmentLoweringCarrier::SoulStore) ||
        (assignmentSite->AssignmentKind == AssignmentSemanticKind::Handle &&
         carrier == AssignmentLoweringCarrier::EnvelopeRebind);
    bool classified =
        assignmentSite->AssignmentKind == AssignmentSemanticKind::Payload ||
        assignmentSite->AssignmentKind == AssignmentSemanticKind::Handle;
    if (classified && !agrees) {
      error(assignmentSite, DiagID::ERR_CODEGEN,
            "assignment semantic classification disagrees with lowering");
    }
  };
  // 1. Resolve Intent
  bool hasRebind = false;
  bool explicitRebind = false;
  const Expr *targetLHS = lhsExpr;
  while (auto *ue = dynamic_cast<const UnaryExpr *>(targetLHS)) {
    if (ue->IsRebindable || ue->Op == TokenType::TokenWrite) {
      hasRebind = true;
      explicitRebind = true;
    }
    targetLHS = ue->RHS.get();
  }

  m_InLHS = true;
  // Physically we don't need to 'gen' the LHS here yet if using emitAssignment
  // structure. But we need to know the address.

  // 2. Resolve LHS Metadata
  TokaSymbol *symLHS = nullptr;
  llvm::Value *lhsAlloca = nullptr;
  if (auto *varLHS = dynamic_cast<const VariableExpr *>(targetLHS)) {
    std::string baseName = varLHS->Name;
    while (!baseName.empty() &&
           (baseName[0] == '*' || baseName[0] == '#' || baseName[0] == '&' ||
            baseName[0] == '^' || baseName[0] == '~' || baseName[0] == '!'))
      baseName = baseName.substr(1);
    while (!baseName.empty() &&
           (baseName.back() == '#' || baseName.back() == '?' ||
            baseName.back() == '!'))
      baseName.pop_back();

    if (m_Symbols.count(baseName)) {
      symLHS = &m_Symbols[baseName];
      lhsAlloca = symLHS->allocaPtr;
    }
  }
  if (symLHS &&
      (symLHS->mode == AddressingMode::Reference ||
       symLHS->mode == AddressingMode::Pointer ||
       symLHS->morphology != Morphology::None)) {
    hasRebind = true;
  }

  // 3. Resolve RHS Value
  llvm::Value *rhsVal = nullptr;

  // [Fix] Handle UnsetExpr (x = uninit)
  // Generating code for 'uninit' directly returns nullptr/error in genExpr.
  // We must handle it here to produce an UndefValue.
  if (dynamic_cast<const UnsetExpr *>(rhsExpr)) {
    // Generate Undef for LHS Type
    llvm::Type *destTy = nullptr;

    // 1. Try to get from Symbol (Most reliable for variables/morphology)
    if (symLHS) {
      if (hasRebind) {
        if (symLHS->morphology == Morphology::Shared) {
          llvm::Type *ptrTy = llvm::PointerType::getUnqual(m_Context);
          llvm::Type *refTy =
              llvm::PointerType::getUnqual(m_Context);
          destTy = llvm::StructType::get(m_Context, {ptrTy, refTy});
        } else if (symLHS->morphology == Morphology::Unique) {
          destTy = llvm::PointerType::getUnqual(m_Context);
        } else {
          destTy = symLHS->soulType;
        }
      } else {
        destTy = symLHS->soulType;
      }
    }

    // 2. Try to get from AST Type (For array index, deref, etc)
    if (!destTy && lhsExpr->ResolvedType) {
      destTy = getLLVMType(lhsExpr->ResolvedType);
    }

    // 3. Fallback: Alloca type (if available and valid)
    if (!destTy && lhsAlloca) {
      if (auto *AI = llvm::dyn_cast<llvm::AllocaInst>(lhsAlloca)) {
        destTy = AI->getAllocatedType();
      }
    }

    if (destTy) {
      if (destTy->isPointerTy()) {
        rhsVal = llvm::Constant::getNullValue(destTy);
      } else {
        rhsVal = llvm::UndefValue::get(destTy);
      }
    } else {
      // Cannot infer type for an uninitialized assignment. CodeGen error?
      // Sema should have caught this or we rely on explicit typing.
      return nullptr;
    }
  } else {
    // Normal Expr
    m_InLHS = false;
    PhysEntity rhs_ent = genExpr(rhsExpr).load(m_Builder);
    rhsVal = rhs_ent.load(m_Builder);
  }

  if (!rhsVal)
    return nullptr;

  // Sema has already classified ordinary assignments as either a handle
  // rebind or a payload write.  Do not re-infer that decision from the LLVM
  // shape of the RHS: `none` is represented as a null pointer, but assigning
  // it to the nullable soul of `^#p#: T?` is still a payload write.
  bool effectiveRebind = hasRebind;
  if (assignmentSite &&
      (assignmentSite->AssignmentKind == AssignmentSemanticKind::Payload ||
       assignmentSite->AssignmentKind == AssignmentSemanticKind::Handle)) {
    effectiveRebind =
        assignmentSite->AssignmentKind == AssignmentSemanticKind::Handle;
  } else if (effectiveRebind && !explicitRebind && symLHS && rhsVal) {
    bool isHandleType = rhsVal->getType()->isStructTy(); // SharedPtr
    if (rhsVal->getType()->isPointerTy()) {
      // Could be RawPtr, UniquePtr, or promoted SharedPtr source
      isHandleType = true;
    }
    // If RHS matches Soul Type exactly, prefer Soul Assignment
    if (rhsVal->getType() == symLHS->soulType) {
      // e.g. s has soul i32. RHS is i32.
      // Even if i32 could be a pointer (no), it matches soul.
      isHandleType = false;
    }

    // Determine strict preference
    if (!isHandleType) {
      effectiveRebind = false;
    }
  }

  // A morphic member such as `holder.~field` has no standalone symbol-table
  // entry, but its storage is still a handle slot.  Materialize the exact
  // field address and its declared handle metadata so Sema's Handle result is
  // lowered through the same envelope-rebind carrier as a local binding.
  TokaSymbol memberHandle;
  if (effectiveRebind && !symLHS) {
    const MemberExpr *member = getTerminalAssignmentMember(targetLHS);
    if (member && terminalAssignmentMemberHasMorphology(targetLHS) &&
        member->ResolvedType) {
      llvm::Value *memberHandleAddr = emitEntityAddr(lhsExpr);
      if (memberHandleAddr) {
        fillSymbolMetadata(memberHandle, member->ResolvedType,
                           getLLVMType(member->ResolvedType));
        memberHandle.isRebindable = true;
        auto soul = member->ResolvedType->getSoulType();
        if (soul && m_Shapes.count(soul->getSoulName())) {
          memberHandle.hasDrop = true;
          memberHandle.dropFunc =
              m_Shapes[soul->getSoulName()]->MangledDestructorName;
        }
        symLHS = &memberHandle;
        lhsAlloca = memberHandleAddr;
      }
    }
  }
  if (effectiveRebind && symLHS && lhsAlloca) {
    // Scene B: Envelope Rebind
    std::shared_ptr<Type> targetSoulType =
        symLHS->soulTypeObj ? symLHS->soulTypeObj->getSoulType() : nullptr;
    const Expr *freshAllocation = rhsExpr;
    while (freshAllocation) {
      if (auto *unsafeExpr = dynamic_cast<const UnsafeExpr *>(freshAllocation)) {
        freshAllocation = unsafeExpr->Expression.get();
      } else if (auto *castExpr = dynamic_cast<const CastExpr *>(freshAllocation)) {
        freshAllocation = castExpr->Expression.get();
      } else {
        break;
      }
    }
    if ((dynamic_cast<const NewExpr *>(freshAllocation) ||
         dynamic_cast<const AllocExpr *>(freshAllocation)) &&
        targetSoulType &&
        targetSoulType->IsNullable && symLHS->soulType &&
        symLHS->soulType->isStructTy()) {
      rhsVal = wrapFreshAllocationAsNullableSoul(
          rhsVal, llvm::cast<llvm::StructType>(symLHS->soulType));
    }
    if (symLHS->morphology == Morphology::Shared &&
        rhsVal->getType()->isPointerTy()) {
      // Correctly pass the Handle Struct type
      rhsVal =
          emitPromotion(rhsVal, getLLVMType(lhsExpr->ResolvedType), *symLHS);
    }
    recordAssignmentLoweringCarrier(
        assignmentSite, AssignmentLoweringCarrier::EnvelopeRebind);
    verifyLowering(AssignmentLoweringCarrier::EnvelopeRebind);
    llvm::Value *handleAddr = lhsAlloca;
    if (symLHS->isCallerHandleSlot) {
      handleAddr = m_Builder.CreateLoad(m_Builder.getPtrTy(), lhsAlloca,
                                        "rebind.caller_handle_slot");
    }
    emitEnvelopeRebind(handleAddr, rhsVal, *symLHS, lhsExpr);
  } else {
    // Scene A: Soul Assignment
    llvm::Value *soulAddr = emitEntityAddr(lhsExpr);
    llvm::Type *destTy = rhsVal->getType();
    if (symLHS)
      destTy = symLHS->soulType;

    // Nullable souls are stored as { T, i1 }.  Inspect the destination
    // storage type rather than the outer handle type, so a nullable payload
    // remains independent from a nullable/rebindable handle.
    std::shared_ptr<Type> targetSoulType =
        symLHS && symLHS->soulTypeObj ? symLHS->soulTypeObj->getSoulType()
                                      : nullptr;
    bool replacesNullableSoul = targetSoulType && targetSoulType->IsNullable &&
                                symLHS && symLHS->hasDrop && destTy &&
                                destTy->isStructTy() &&
                                destTy->getStructNumElements() == 2 &&
                                destTy->getStructElementType(1)->isIntegerTy(1);
    if (replacesNullableSoul) {
      llvm::StructType *nullableType = llvm::cast<llvm::StructType>(destTy);
      llvm::Value *oldPayload =
          m_Builder.CreateLoad(nullableType, soulAddr, "nullable_soul.old");
      llvm::Value *wasPresent = m_Builder.CreateExtractValue(
          oldPayload, 1, "nullable_soul.was_present");
      llvm::Function *function = m_Builder.GetInsertBlock()->getParent();
      llvm::BasicBlock *dropBB =
          llvm::BasicBlock::Create(m_Context, "nullable_soul.drop", function);
      llvm::BasicBlock *continueBB = llvm::BasicBlock::Create(
          m_Context, "nullable_soul.drop_done", function);
      m_Builder.CreateCondBr(wasPresent, dropBB, continueBB);
      m_Builder.SetInsertPoint(dropBB);
      emitDropCascade(soulAddr, targetSoulType->getSoulName());
      m_Builder.CreateBr(continueBB);
      m_Builder.SetInsertPoint(continueBB);
    }
    if (targetSoulType && targetSoulType->IsNullable && destTy &&
        destTy->isStructTy() &&
        destTy->getStructNumElements() == 2 &&
        destTy->getStructElementType(1)->isIntegerTy(1)) {
      llvm::Type *targetStructTy = destTy;
      if (rhsVal->getType() != targetStructTy) {
        // Wrapping T into { T, i1 }
        llvm::Value *wrapped = llvm::UndefValue::get(targetStructTy);
        if (rhsVal->getType() == targetStructTy->getStructElementType(0)) {
          wrapped = m_Builder.CreateInsertValue(wrapped, rhsVal, {0});
          wrapped = m_Builder.CreateInsertValue(
              wrapped,
              llvm::ConstantInt::get(llvm::Type::getInt1Ty(m_Context), 1), {1});
          rhsVal = wrapped;
          destTy = targetStructTy;
        } else if (dynamic_cast<const NoneExpr *>(rhsExpr) ||
                   rhsVal->getType()->isPointerTy()) { // Handle none
          wrapped = m_Builder.CreateInsertValue(
              wrapped,
              llvm::Constant::getNullValue(
                  targetStructTy->getStructElementType(0)),
              {0});
          wrapped = m_Builder.CreateInsertValue(
              wrapped,
              llvm::ConstantInt::get(llvm::Type::getInt1Ty(m_Context), 0), {1});
          rhsVal = wrapped;
          destTy = targetStructTy;
        }
      }
    }
    AssignmentLoweringCarrier carrier =
        AssignmentLoweringCarrier::SoulStore;
    if (soulAddr && destTy) {
      carrier =
          assignmentLowersThroughEnvelopeCarrier(lhsExpr)
              ? AssignmentLoweringCarrier::EnvelopeRebind
              : AssignmentLoweringCarrier::SoulStore;
      recordAssignmentLoweringCarrier(assignmentSite, carrier);
      verifyLowering(carrier);
    }
    llvm::StoreInst *store = emitSoulAssignment(soulAddr, rhsVal, destTy);
    if (carrier == AssignmentLoweringCarrier::EnvelopeRebind)
      markMemoryEvent(store, "rebind");
    if (store) {
      if (auto *member = dynamic_cast<const MemberExpr *>(targetLHS))
        restoreDropForMemberAssignment(member);
      else if (auto *index = dynamic_cast<const ArrayIndexExpr *>(targetLHS))
        restoreDropForIndexAssignment(index);
    }
  }

  m_InLHS = false;
  return PhysEntity(rhsVal, "void", rhsVal->getType(), false);
}

PhysEntity CodeGen::genBinaryExpr(const BinaryExpr *expr) {
  auto unwrapHandle = [&](llvm::Value *v) -> llvm::Value * {
    if (!v)
      return nullptr;
    while (v->getType()->isStructTy()) {
      unsigned numElems = v->getType()->getStructNumElements();
      if (numElems == 1 || numElems == 2) {
        v = m_Builder.CreateExtractValue(v, 0);
      } else {
        break;
      }
    }
    return v;
  };

  const BinaryExpr *bin = expr;

  // [Phase 2] Syntactic Sugar / Operator Overloading Dispatch
  if (!bin->OverloadedMethod.empty()) {
      std::vector<std::unique_ptr<Expr>> args;
      args.push_back(std::unique_ptr<Expr>(static_cast<Expr*>(bin->RHS->clone().release())));
      MethodCallExpr mc(std::unique_ptr<Expr>(static_cast<Expr*>(bin->LHS->clone().release())), bin->OverloadedMethod, std::move(args));
      mc.Loc = bin->Loc;
      mc.ResolvedType = lowerTypeSyntax(nullptr, "bool");
      
      PhysEntity ret = genMethodCall(&mc);
      if (bin->Op == "!=") {
          llvm::Value* val = ret.load(m_Builder);
          val = m_Builder.CreateNot(val, "not_eq");
          return PhysEntity(val, "bool", val->getType(), false);
      }
      return ret;
  }

  if (bin->Op == "=" || bin->Op == "+=" || bin->Op == "-=" || bin->Op == "*=" ||
      bin->Op == "/=" || bin->Op == "%=") {

    if (bin->Op == "=") {
      return emitAssignment(bin->LHS.get(), bin->RHS.get(), bin);
    }

    llvm::Value *soulAddr = emitEntityAddr(bin->LHS.get());
    
    PhysEntity rhsVal_ent = genExpr(bin->RHS.get());
    llvm::Value *rhsVal = rhsVal_ent.load(m_Builder);
    
    if (!soulAddr || !rhsVal) {
      return nullptr;
    }

    // Determine destType for Load [Fix for Opaque Pointers]
    llvm::Type *destType = nullptr;
    if (auto *ve = dynamic_cast<const VariableExpr *>(bin->LHS.get())) {
      std::string baseName = ve->Name;
      while (!baseName.empty() &&
             (baseName[0] == '*' || baseName[0] == '#' || baseName[0] == '&'))
        baseName = baseName.substr(1);
      while (!baseName.empty() &&
             (baseName.back() == '#' || baseName.back() == '!'))
        baseName.pop_back();

      if (m_Symbols.count(baseName)) {
        destType = m_Symbols[baseName].soulType;
      }
    }

    if (!destType && bin->LHS->ResolvedType) {
      destType = getLLVMType(bin->LHS->ResolvedType);
    }

    if (!destType) {
      destType = rhsVal->getType(); // Fallback
    }
    

    // Standard Compound Logic
    llvm::Value *lhsVal = m_Builder.CreateLoad(destType, soulAddr, "lhs_val");
    
    // [Fix] If LHS is reference-like, soulAddr might be address-of-pointer.
    // However, emitEntityAddr is supposed to return the final Soul address.
    // Let's ensure we are using the correct value for the operation.
    lhsVal = unwrapHandle(lhsVal);

    llvm::Type *lhsTy = lhsVal->getType();
    llvm::Type *rhsTy = rhsVal->getType();

    // [Fix] Handle Pointer Arithmetic in Compound Assignment
    if (lhsTy->isPointerTy() && rhsTy->isIntegerTy()) {
      if (bin->Op == "+=" || bin->Op == "-=") {
        llvm::Type *elemTy = nullptr;
        elemTy = llvm::Type::getInt8Ty(m_Context);

        rhsVal = m_Builder.CreateIntCast(rhsVal, getIntPtrTy(), true);
        if (bin->Op == "-=")
          rhsVal = m_Builder.CreateNeg(rhsVal);

        llvm::Value *res =
            m_Builder.CreateInBoundsGEP(elemTy, lhsVal, {rhsVal}, "ptradd");
        m_Builder.CreateStore(res, soulAddr);
        recordAssignmentLoweringCarrier(
            bin, AssignmentLoweringCarrier::ResidualLowering);
        return PhysEntity(res, "void", res->getType(), false);
      }
    }

    // [Fix] Type Promotion for Compound Assignment
    if (lhsTy != rhsTy) {
      if (lhsTy->isIntegerTy() && rhsTy->isIntegerTy()) {
        rhsVal = m_Builder.CreateIntCast(rhsVal, lhsTy, false);
      } else if (lhsTy->isFloatingPointTy() && rhsTy->isFloatingPointTy()) {
        rhsVal = m_Builder.CreateFPCast(rhsVal, lhsTy);
      } else {
        error(bin, DiagID::ERR_CODEGEN_TYPE_MISMATCH_IN_COMPOUND_ASSIGNMENT);
        return nullptr;
      }
    }

    llvm::Value *res = nullptr;
    if (lhsTy->isFloatingPointTy()) {
      if (bin->Op == "+=")
        res = m_Builder.CreateFAdd(lhsVal, rhsVal);
      else if (bin->Op == "-=")
        res = m_Builder.CreateFSub(lhsVal, rhsVal);
      else if (bin->Op == "*=")
        res = m_Builder.CreateFMul(lhsVal, rhsVal);
      else if (bin->Op == "/=")
        res = m_Builder.CreateFDiv(lhsVal, rhsVal);
    } else {
      if (bin->Op == "+=") {
        res = m_Builder.CreateAdd(lhsVal, rhsVal);
      } else if (bin->Op == "-=")
        res = m_Builder.CreateSub(lhsVal, rhsVal);
      else if (bin->Op == "*=")
        res = m_Builder.CreateMul(lhsVal, rhsVal);
      else if (bin->Op == "/=")
        res = m_Builder.CreateSDiv(lhsVal, rhsVal);
      else if (bin->Op == "%=") {
        bool isUnsigned = false;
        if (lhsTy->isIntegerTy()) {
          if (bin->LHS && bin->LHS->ResolvedType) {
            std::string lty = bin->LHS->ResolvedType->toString();
            if (lty.size() >= 1 && lty[0] == 'u')
              isUnsigned = true;
          }
        } else {
          std::cerr << "FATAL: LHS of %= is not integer!" << std::endl;
        }

        if (isUnsigned)
          res = m_Builder.CreateURem(lhsVal, rhsVal);
        else
          res = m_Builder.CreateSRem(lhsVal, rhsVal);
      }
    }

    m_Builder.CreateStore(res, soulAddr);
    recordAssignmentLoweringCarrier(bin,
                                    AssignmentLoweringCarrier::ResidualLowering);
    return res;
  }

  // Logical Operators (Short-circuiting)
  if (bin->Op == "&&") {
    PhysEntity lhs_ent = genExpr(bin->LHS.get()).load(m_Builder);
    llvm::Value *lhs = lhs_ent.load(m_Builder);
    if (!lhs)
      return nullptr;
    if (!lhs->getType()->isIntegerTy(1))
      lhs = m_Builder.CreateICmpNE(
          lhs, llvm::ConstantInt::get(lhs->getType(), 0), "tobool");

    llvm::Function *TheFunction = m_Builder.GetInsertBlock()->getParent();
    llvm::BasicBlock *EntryBB = m_Builder.GetInsertBlock();
    llvm::BasicBlock *RHSBB =
        llvm::BasicBlock::Create(m_Context, "land.rhs", TheFunction);
    llvm::BasicBlock *MergeBB = llvm::BasicBlock::Create(m_Context, "land.end");

    m_Builder.CreateCondBr(lhs, RHSBB, MergeBB);

    // Eval RHS
    m_Builder.SetInsertPoint(RHSBB);
    PhysEntity rhs_ent = genExpr(bin->RHS.get()).load(m_Builder);
    llvm::Value *rhs = rhs_ent.load(m_Builder);
    if (!rhs)
      return nullptr;
    if (!rhs->getType()->isIntegerTy(1))
      rhs = m_Builder.CreateICmpNE(
          rhs, llvm::ConstantInt::get(rhs->getType(), 0), "tobool");

    m_Builder.CreateBr(MergeBB);
    RHSBB = m_Builder.GetInsertBlock();

    // Merge
    MergeBB->insertInto(TheFunction);
    m_Builder.SetInsertPoint(MergeBB);
    llvm::PHINode *PN =
        m_Builder.CreatePHI(llvm::Type::getInt1Ty(m_Context), 2, "land.val");
    PN->addIncoming(llvm::ConstantInt::getFalse(m_Context), EntryBB);
    PN->addIncoming(rhs, RHSBB);
    return PN;
  }

  if (bin->Op == "||") {
    PhysEntity lhs_ent = genExpr(bin->LHS.get()).load(m_Builder);
    llvm::Value *lhs = lhs_ent.load(m_Builder);
    if (!lhs)
      return nullptr;
    if (!lhs->getType()->isIntegerTy(1))
      lhs = m_Builder.CreateICmpNE(
          lhs, llvm::ConstantInt::get(lhs->getType(), 0), "tobool");

    llvm::Function *TheFunction = m_Builder.GetInsertBlock()->getParent();
    llvm::BasicBlock *EntryBB = m_Builder.GetInsertBlock();
    llvm::BasicBlock *RHSBB =
        llvm::BasicBlock::Create(m_Context, "lor.rhs", TheFunction);
    llvm::BasicBlock *MergeBB = llvm::BasicBlock::Create(m_Context, "lor.end");

    m_Builder.CreateCondBr(lhs, MergeBB, RHSBB);

    // Eval RHS
    m_Builder.SetInsertPoint(RHSBB);
    PhysEntity rhs_ent = genExpr(bin->RHS.get()).load(m_Builder);
    llvm::Value *rhs = rhs_ent.load(m_Builder);
    if (!rhs)
      return nullptr;
    if (!rhs->getType()->isIntegerTy(1))
      rhs = m_Builder.CreateICmpNE(
          rhs, llvm::ConstantInt::get(rhs->getType(), 0), "tobool");

    m_Builder.CreateBr(MergeBB);
    RHSBB = m_Builder.GetInsertBlock();

    // Merge
    MergeBB->insertInto(TheFunction);
    m_Builder.SetInsertPoint(MergeBB);
    llvm::PHINode *PN =
        m_Builder.CreatePHI(llvm::Type::getInt1Ty(m_Context), 2, "lor.val");
    PN->addIncoming(llvm::ConstantInt::getTrue(m_Context), EntryBB);
    PN->addIncoming(rhs, RHSBB);
    return PN;
  }

  // 'is' operator - Specialized 'peek' evaluation to avoid destructive moves
  if (bin->Op == "is") {
    // Special handling for 'var is nullptr' (or '~var is nullptr') to avoid
    // unsafe dereferencing
    const VariableExpr *targetVar = nullptr;
    const Expr *currentLHS = bin->LHS.get();

    // Peel all UnaryExpr layers (e.g. ~?p -> Unary(~) -> Unary(?) -> Var(p))
    while (true) {
      if (auto *ve = dynamic_cast<const VariableExpr *>(currentLHS)) {
        targetVar = ve;
        break;
      } else if (auto *ue = dynamic_cast<const UnaryExpr *>(currentLHS)) {
        currentLHS = ue->RHS.get();
      } else {
        break;
      }
    }

    if (targetVar) {
      if (dynamic_cast<const NullExpr *>(bin->RHS.get())) {
        // Get the base name sans morphology
        std::string baseName = targetVar->Name;
        while (!baseName.empty() && (baseName[0] == '*' || baseName[0] == '#' ||
                                     baseName[0] == '&' || baseName[0] == '^' ||
                                     baseName[0] == '~' || baseName[0] == '!'))
          baseName = baseName.substr(1);
        while (!baseName.empty() &&
               (baseName.back() == '#' || baseName.back() == '?' ||
                baseName.back() == '!'))
          baseName.pop_back();

        if (m_Symbols.count(baseName)) {
          TokaSymbol &sym = m_Symbols[baseName];
          // Shared Pointer: { ptr, ptr }
          if (sym.morphology == Morphology::Shared) {
            llvm::Value *identity = sym.allocaPtr;
            if (identity) {
              // Get pointer to the first element (data ptr)
              llvm::Value *dataPtrAddr = m_Builder.CreateStructGEP(
                  sym.soulType, identity, 0, "sh_data_ptr_addr");
              llvm::Value *dataPtr = m_Builder.CreateLoad(
                  m_Builder.getPtrTy(), dataPtrAddr, "sh_data_ptr");
              return m_Builder.CreateIsNull(dataPtr, "sh_is_null");
            }
          }
        }
      }
    }

    auto evaluatePeek = [&](const Expr *e) -> llvm::Value * {
      const Expr *target = e;
      if (auto *u = dynamic_cast<const UnaryExpr *>(e)) {
        if (u->Op == TokenType::Caret || u->Op == TokenType::Tilde ||
            u->Op == TokenType::Star) {
          target = u->RHS.get();
        }
      }
      if (auto *v = dynamic_cast<const VariableExpr *>(target)) {
        std::string baseName = v->Name;
        while (!baseName.empty() &&
               (baseName.back() == '#' || baseName.back() == '?' ||
                baseName.back() == '!'))
          baseName.pop_back();

        if (m_Symbols.count(baseName) &&
            m_Symbols[baseName].morphology == Morphology::Shared) {
          return genExpr(target).load(m_Builder);
        }
        return getEntityAddr(v->codegenName());
      }
      return genExpr(e).load(m_Builder);
    };

    llvm::Value *lhsVal = evaluatePeek(bin->LHS.get());
    if (!lhsVal)
      return nullptr;

    llvm::Type *lhsTy = lhsVal->getType();
    if (lhsTy->isStructTy() && lhsTy->getStructNumElements() == 2) {
      // shared pointer: extract raw pointer
      lhsVal = m_Builder.CreateExtractValue(lhsVal, 0, "shared_ptr_val");
      lhsTy = lhsVal->getType();
    } else if (lhsTy->isStructTy() && lhsTy->getStructNumElements() == 1) {
      lhsVal = m_Builder.CreateExtractValue(lhsVal, 0);
      lhsTy = lhsVal->getType();
    }

    // Special Case: 'expr is nullptr' (Null check)
    if (dynamic_cast<const NullExpr *>(bin->RHS.get()) ||
        dynamic_cast<const NoneExpr *>(bin->RHS.get())) {

      // Handle Nullable Soul Wrapper ({ T, i1 })
      if (lhsTy->isStructTy() && lhsTy->getStructNumElements() == 2 &&
          lhsTy->getStructElementType(1)->isIntegerTy(1)) {
        llvm::Value *isPresent = m_Builder.CreateExtractValue(lhsVal, 1, "is_present");
        if (bin->Op == "==") {
          return m_Builder.CreateNot(isPresent, "is_none");
        } else {
          return isPresent;
        }
      }

      while (lhsVal->getType()->isStructTy() &&
             lhsVal->getType()->getStructNumElements() == 1) {
        lhsVal = m_Builder.CreateExtractValue(lhsVal, 0);
      }

      if (lhsVal->getType()->isIntegerTy()) {
        // ADDR0 is null?
        llvm::Value *cmp = m_Builder.CreateICmpEQ(
            lhsVal, llvm::ConstantInt::get(lhsVal->getType(), 0),
            "is_null_int");
        if (bin->Op == "!=") return m_Builder.CreateNot(cmp, "is_not_null_int");
        return cmp;
      }

      llvm::Value *cmp = m_Builder.CreateIsNull(lhsVal, "is_null");
      if (bin->Op == "!=") return m_Builder.CreateNot(cmp, "is_not_null");
      return cmp;
    }

    // Implicit Case: 'expr is Type' or 'expr is pattern' (Not-Null check)
    if (lhsTy->isPointerTy()) {
      return m_Builder.CreateIsNotNull(lhsVal, "is_not_null");
    }
    return llvm::ConstantInt::getTrue(m_Context);
  }

  // Standard Arithmetic and Comparisons
  PhysEntity lhs_ent = genExpr(bin->LHS.get()).load(m_Builder);
  llvm::Value *lhs = lhs_ent.load(m_Builder);
  if (!lhs) {
    return nullptr;
  }

  if (!m_Builder.GetInsertBlock() ||
      m_Builder.GetInsertBlock()->getTerminator()) {
    return nullptr;
  }

  PhysEntity rhs_ent = genExpr(bin->RHS.get()).load(m_Builder);
  llvm::Value *rhs = rhs_ent.load(m_Builder);
  if (!rhs) {
    return nullptr;
  }

  if (!m_Builder.GetInsertBlock() ||
      m_Builder.GetInsertBlock()->getTerminator()) {
    return nullptr;
  }

  llvm::Type *lhsType = lhs->getType();
  llvm::Type *rhsType = rhs->getType();

  // [Fix] Implicit Smart Pointer Dereference (Bridge Sema -> CodeGen)
  // If Sema authorized a Value usage (resolvedType is generic) but we
  // generated a Pointer/Handle, we must unwrap/load the Soul.

  auto unwrapSmartPtr = [&](llvm::Value *val,
                            const Expr *expr) -> llvm::Value * {
    if (!val || !expr || !expr->ResolvedType) {
      return val;
    }

    llvm::Type *currentTy = val->getType();
    bool isTargetStruct = currentTy->isStructTy();
    bool isTargetPtr = currentTy->isPointerTy();

    bool semaIsValue = !expr->ResolvedType->isPointer() &&
                       !expr->ResolvedType->isReference() &&
                       !expr->ResolvedType->isSmartPointer() &&
                       !expr->ResolvedType->isNullType() &&
                       !expr->ResolvedType->isAddrType() &&
                       !expr->ResolvedType->isOAddrType();

    if (semaIsValue) {
      if (isTargetStruct && currentTy->getStructNumElements() == 2) {
        // Shared Pointer Handle: Extract Data Ptr then Load
        llvm::Value *dataPtr =
            m_Builder.CreateExtractValue(val, 0, "smart_deref_ptr");
            
        if (!dataPtr->getType()->isPointerTy()) {
          return dataPtr;
        }

        // Check if loading is valid (opaque pointers make dataPtr typeless,
        // need Element Type) We rely on ResolvedType to provide the Element
        // Type
        llvm::Type *loadTy = getLLVMType(expr->ResolvedType);
        if (loadTy) {
          return m_Builder.CreateLoad(loadTy, dataPtr, "smart_deref_val");
        }
      } else if (isTargetPtr) {
        // Unique Pointer or Raw Pointer: Load the Value
        // Verify we aren't loading a pointer-to-pointer if the target IS a
        // pointer But semaIsValue=true means target is i32, struct, etc. not
        // pointer.
        llvm::Type *loadTy = getLLVMType(expr->ResolvedType);
        if (loadTy) {
          return m_Builder.CreateLoad(loadTy, val, "ptr_deref_val");
        }
      }
    }
    return val;
  };

  if (bin->Op == "==" || bin->Op == "!=") {
    if (dynamic_cast<const NoneExpr *>(bin->RHS.get()) || dynamic_cast<const NullExpr *>(bin->RHS.get())) {
      if (lhs->getType()->isStructTy() && lhs->getType()->getStructNumElements() == 2 &&
          lhs->getType()->getStructElementType(1)->isIntegerTy(1)) {
        llvm::Value *isPresent = m_Builder.CreateExtractValue(lhs, 1, "is_present");
        if (bin->Op == "==") return m_Builder.CreateNot(isPresent, "is_none");
        return isPresent;
      }
    }
    if (dynamic_cast<const NoneExpr *>(bin->LHS.get()) || dynamic_cast<const NullExpr *>(bin->LHS.get())) {
      if (rhs->getType()->isStructTy() && rhs->getType()->getStructNumElements() == 2 &&
          rhs->getType()->getStructElementType(1)->isIntegerTy(1)) {
        llvm::Value *isPresent = m_Builder.CreateExtractValue(rhs, 1, "is_present");
        if (bin->Op == "==") return m_Builder.CreateNot(isPresent, "is_none");
        return isPresent;
      }
    }
  }

  lhs = unwrapSmartPtr(lhs, bin->LHS.get());
  rhs = unwrapSmartPtr(rhs, bin->RHS.get());

  // Refresh types after unwrap
  lhsType = lhs->getType();
  rhsType = rhs->getType();

  if (bin->Op == "==" || bin->Op == "!=") {
    // None check moved above unwrapSmartPtr
  }

  bool isPtrArith =
      (lhsType->isPointerTy() && rhsType->isIntegerTy()) ||
      (rhsType->isPointerTy() && lhsType->isIntegerTy() && bin->Op == "+");

  if (lhsType != rhsType && !isPtrArith) {
    if (lhsType->isPointerTy() && rhsType->isPointerTy()) {
      rhs = m_Builder.CreateBitCast(rhs, lhsType);
    } else if (lhsType->isPointerTy() && rhsType->isIntegerTy()) {
      rhs = m_Builder.CreateIntToPtr(rhs, lhsType);
    } else if (lhsType->isIntegerTy() && rhsType->isPointerTy()) {
      lhs = m_Builder.CreateIntToPtr(lhs, rhsType);
    } else if (lhsType->isIntegerTy() && rhsType->isIntegerTy()) {
      // Promote to widest
      if (lhsType->getIntegerBitWidth() < rhsType->getIntegerBitWidth()) {
        bool isUnsigned = false;
        if (bin->LHS->ResolvedType) {
          std::string lty = bin->LHS->ResolvedType->toString();
          if (lty.size() >= 1 && lty[0] == 'u')
            isUnsigned = true;
        }
        if (isUnsigned)
          lhs = m_Builder.CreateZExt(lhs, rhsType, "lhs_ext");
        else
          lhs = m_Builder.CreateSExt(lhs, rhsType, "lhs_ext");
        lhsType = rhsType;
      } else if (lhsType->getIntegerBitWidth() >
                 rhsType->getIntegerBitWidth()) {
        bool isUnsigned = false;
        if (bin->RHS->ResolvedType) {
          std::string rty = bin->RHS->ResolvedType->toString();
          if (rty.size() >= 1 && rty[0] == 'u')
            isUnsigned = true;
        }
        if (isUnsigned)
          rhs = m_Builder.CreateZExt(rhs, lhsType, "rhs_ext");
        else
          rhs = m_Builder.CreateSExt(rhs, lhsType, "rhs_ext");
        rhsType = lhsType;
      }
    } else {
      if (lhsType != rhsType) {
        std::string ls, rs;
        llvm::raw_string_ostream los(ls), ros(rs);
        lhsType->print(los);
        rhsType->print(ros);
        error(bin, DiagID::ERR_CODEGEN_TYPE_MISMATCH_IN_BINARY_EXPRESSION_VS, ls, rs);
        return nullptr;
      }
    }
  }
  // [Fix] Zero-payload enum equality
  // If the types are simple structs with exactly one integer field (discriminant), extract them for comparison.
  if (lhsType->isStructTy() && rhsType->isStructTy() && (bin->Op == "==" || bin->Op == "!=")) {
    if (lhsType->getStructNumElements() == 1 && rhsType->getStructNumElements() == 1) {
      if (lhsType->getStructElementType(0)->isIntegerTy() && rhsType->getStructElementType(0)->isIntegerTy()) {
        lhs = m_Builder.CreateExtractValue(lhs, 0, "enum_tag_lhs");
        rhs = m_Builder.CreateExtractValue(rhs, 0, "enum_tag_rhs");
        lhsType = lhs->getType();
        rhsType = rhs->getType();
      }
    }
  }

  if (!lhsType->isIntOrIntVectorTy() && !lhsType->isPtrOrPtrVectorTy() &&
      !lhsType->isFloatingPointTy()) {
    std::string s;
    llvm::raw_string_ostream os(s);
    lhsType->print(os);
    error(bin, DiagID::ERR_CODEGEN_INVALID_TYPE_FOR_COMPARISON_COMPARISON, os.str());
    return nullptr;
  }

  // Final check to avoid assertion

  if (lhsType->isFloatingPointTy() && rhsType->isFloatingPointTy()) {
    if (bin->Op == "+")
      return m_Builder.CreateFAdd(lhs, rhs, "addtmp");
    if (bin->Op == "-")
      return m_Builder.CreateFSub(lhs, rhs, "subtmp");
    if (bin->Op == "*")
      return m_Builder.CreateFMul(lhs, rhs, "multmp");
    if (bin->Op == "/")
      return m_Builder.CreateFDiv(lhs, rhs, "divtmp");
    if (bin->Op == "<")
      return m_Builder.CreateFCmpOLT(lhs, rhs, "lt_tmp");
    if (bin->Op == ">")
      return m_Builder.CreateFCmpOGT(lhs, rhs, "gt_tmp");
    if (bin->Op == "<=")
      return m_Builder.CreateFCmpOLE(lhs, rhs, "le_tmp");
    if (bin->Op == ">=")
      return m_Builder.CreateFCmpOGE(lhs, rhs, "ge_tmp");
    if (bin->Op == "==")
      return m_Builder.CreateFCmpOEQ(lhs, rhs, "eq_tmp");
    if (bin->Op == "!=")
      return m_Builder.CreateFCmpONE(lhs, rhs, "ne_tmp");
  }

  if (bin->Op == "+") {
    if (lhs->getType()->isPointerTy()) {
      llvm::Type *elemTy = llvm::Type::getInt8Ty(m_Context);

      // Ensure RHS matches pointer size for GEP
      if (rhs->getType()->isPointerTy()) {
        rhs = m_Builder.CreatePtrToInt(rhs, getIntPtrTy(), "idx_ptr2int");
      } else {
        rhs = m_Builder.CreateIntCast(rhs, getIntPtrTy(), true);
      }
      return m_Builder.CreateGEP(elemTy, lhs, {rhs}, "ptradd");
    }
    llvm::Value *add_res = m_Builder.CreateAdd(lhs, rhs, "addtmp");
    return add_res;
  }
  if (bin->Op == "-") {
    if (lhs->getType()->isPointerTy()) {
      if (rhs->getType()->isPointerTy()) {
        llvm::Value *lhsInt = m_Builder.CreatePtrToInt(lhs, getIntPtrTy());
        llvm::Value *rhsInt = m_Builder.CreatePtrToInt(rhs, getIntPtrTy());
        llvm::Value *diff = m_Builder.CreateSub(lhsInt, rhsInt, "ptrdiff");
        // Maintain the same type as lhs (which is a pointer, fulfilling Addr expectations)
        return m_Builder.CreateIntToPtr(diff, lhs->getType(), "ptrdiff_ptr");
      } else {
        llvm::Type *elemTy = llvm::Type::getInt8Ty(m_Context);
        rhs = m_Builder.CreateIntCast(rhs, getIntPtrTy(), true);
        llvm::Value *negR = m_Builder.CreateNeg(rhs, "neg_idx");
        return m_Builder.CreateGEP(elemTy, lhs, {negR}, "ptrsub");
      }
    }
    return m_Builder.CreateSub(lhs, rhs, "subtmp");
  }
  if (bin->Op == "*")
    return m_Builder.CreateMul(lhs, rhs, "multmp");
  if (bin->Op == "/")
    return m_Builder.CreateSDiv(lhs, rhs, "divtmp");
  if (bin->Op == "%") {
    bool isUnsigned = false;
    // Check resolved type for signedness using robust type check
    if (bin->LHS && bin->LHS->ResolvedType) {
      if (bin->LHS->ResolvedType->isInteger() &&
          !bin->LHS->ResolvedType->isSignedInteger()) {
        isUnsigned = true;
      }
    }

    if (!lhs->getType()->isIntegerTy() || !rhs->getType()->isIntegerTy()) {
      std::cerr << "FATAL: Operands of % are not integers!" << std::endl;
      return llvm::ConstantInt::get(lhs->getType(), 0);
    }

    if (isUnsigned)
      return m_Builder.CreateURem(lhs, rhs, "urem");
    return m_Builder.CreateSRem(lhs, rhs, "srem");
  }

  bool isSigned = true;
  if (bin->LHS && bin->LHS->ResolvedType) {
      isSigned = bin->LHS->ResolvedType->isSignedInteger();
  }

  if (bin->Op == "<") {
    lhs = unwrapHandle(lhs);
    rhs = unwrapHandle(rhs);
    if (isSigned) return m_Builder.CreateICmpSLT(lhs, rhs, "lt_tmp");
    else return m_Builder.CreateICmpULT(lhs, rhs, "lt_tmp");
  }
  if (bin->Op == ">") {
    lhs = unwrapHandle(lhs);
    rhs = unwrapHandle(rhs);
    if (isSigned) return m_Builder.CreateICmpSGT(lhs, rhs, "gt_tmp");
    else return m_Builder.CreateICmpUGT(lhs, rhs, "gt_tmp");
  }
  if (bin->Op == "<=") {
    if (isSigned) return m_Builder.CreateICmpSLE(lhs, rhs, "le_tmp");
    else return m_Builder.CreateICmpULE(lhs, rhs, "le_tmp");
  }
  if (bin->Op == ">=") {
    if (isSigned) return m_Builder.CreateICmpSGE(lhs, rhs, "ge_tmp");
    else return m_Builder.CreateICmpUGE(lhs, rhs, "ge_tmp");
  }
  if (bin->Op == "==" || bin->Op == "!=") {
    // 1. Unwrap Single-Element Structs (Strong Types)
    lhs = unwrapHandle(lhs);
    rhs = unwrapHandle(rhs);

    if (lhs->getType() != rhs->getType()) {
      if (lhs->getType()->isIntegerTy() && rhs->getType()->isIntegerTy()) {
        if (lhs->getType()->getIntegerBitWidth() >
            rhs->getType()->getIntegerBitWidth())
          rhs = m_Builder.CreateZExt(rhs, lhs->getType());
        else
          lhs = m_Builder.CreateZExt(lhs, rhs->getType());
      } else if (lhs->getType()->isPointerTy() &&
                 rhs->getType()->isPointerTy()) {
        rhs = m_Builder.CreateBitCast(rhs, lhs->getType());
      } else {
        // Ptr vs Int Mismatch (e.g. ptr == ADDR0)
        if (lhs->getType()->isPointerTy() && rhs->getType()->isIntegerTy()) {
          lhs = m_Builder.CreatePtrToInt(lhs, rhs->getType());
        } else if (lhs->getType()->isIntegerTy() &&
                   rhs->getType()->isPointerTy()) {
          rhs = m_Builder.CreatePtrToInt(rhs, lhs->getType());
        }
      }
    }

    // Final check to avoid assertion
    // Final check to avoid assertion
    if (!lhs->getType()->isIntOrIntVectorTy() &&
        !lhs->getType()->isPtrOrPtrVectorTy()) {
      std::string ls;
      llvm::raw_string_ostream los(ls);
      lhs->getType()->print(los);
    }

    // Debug print types
    {
      std::string ls, rs;
      llvm::raw_string_ostream los(ls), ros(rs);
      lhs->getType()->print(los);
      rhs->getType()->print(ros);
    }

    llvm::Value *cmp = m_Builder.CreateICmpEQ(lhs, rhs, "eq_tmp");
    if (bin->Op == "!=")
      return m_Builder.CreateNot(cmp, "ne_tmp");
    return cmp;
  }
  if (bin->Op == "!=") {
    // Should have been handled above
    return nullptr;
  }
  if (bin->Op == "&")
    return m_Builder.CreateAnd(lhs, rhs, "andtmp");
  if (bin->Op == "|")
    return m_Builder.CreateOr(lhs, rhs, "ortmp");
  if (bin->Op == "^")
    return m_Builder.CreateXor(lhs, rhs, "xortmp");
  if (bin->Op == "<<")
    return m_Builder.CreateShl(lhs, rhs, "shltmp");
  if (bin->Op == ">>") {
    // Check signedness of LHS
    if (lhs->getType()->isIntegerTy()) {
      // If type implies signedness (in Toka Types, not LLVM types which are
      // opaque) We need to look up source type. But genBinaryExpr has limited
      // Type info unless passed or resolved. Since Toka relies on Sema for
      // types, CodeGen often relies on 'isSigned' property if stored? LLVM
      // integer types don't carry sign. Does CodeGen store resolved types in
      // AST? Yes, Bin->LHS->ResolvedType.
      bool isSigned = false;
      if (bin->LHS->ResolvedType) {
        isSigned = bin->LHS->ResolvedType->isSignedInteger();
        // Or check if it starts with 'i' vs 'u'.
        // AST ResolvedType is standard way.
      } else {
        // Fallback: Default to arithmetic right shift for safety? Or logical?
        // C uses arith for signed, logical for unsigned.
        // If we don't know, AShr is usually safer for general math, LShr for
        // bitwise. Toka "bshr" is explicitly Bitwise. However, user said
        // "i8...i64 -> ashr", "u8...u64 -> lshr". We MUST check type. Let's
        // assume ResolvedType is populated by Sema. Checking ResolvedType.
      }
      if (isSigned)
        return m_Builder.CreateAShr(lhs, rhs, "ashrtmp");
      else
        return m_Builder.CreateLShr(lhs, rhs, "lshrtmp");
    }
    return m_Builder.CreateLShr(lhs, rhs, "lshrtmp"); // Default
  }
  return nullptr;
}

PhysEntity CodeGen::genUnaryExpr(const UnaryExpr *unary) {

  if (unary->Op == TokenType::PlusPlus || unary->Op == TokenType::MinusMinus) {
    llvm::Value *addr = genAddr(unary->RHS.get());
    if (!addr)
      return nullptr;
    llvm::Type *type = nullptr;
    if (auto *var = dynamic_cast<const VariableExpr *>(unary->RHS.get())) {
      if (m_Symbols.count(var->Name))
        type = m_Symbols[var->Name].soulType;
    } else if (auto *gep = llvm::dyn_cast<llvm::GetElementPtrInst>(addr)) {
      type = gep->getResultElementType();
    } else if (auto *alloca = llvm::dyn_cast<llvm::AllocaInst>(addr)) {
      type = alloca->getAllocatedType();
    }
    if (!type)
      return nullptr;
    llvm::Value *oldVal = m_Builder.CreateLoad(type, addr, "pre_old");
    llvm::Value *newVal;
    if (unary->Op == TokenType::PlusPlus)
      newVal = m_Builder.CreateAdd(oldVal, llvm::ConstantInt::get(type, 1),
                                   "preinc_new");
    else
      newVal = m_Builder.CreateSub(oldVal, llvm::ConstantInt::get(type, 1),
                                   "predec_new");
    m_Builder.CreateStore(newVal, addr);
    return newVal;
  }

  if (unary->Op == TokenType::Tilde && unary->RHS->ResolvedType &&
      unary->RHS->ResolvedType->isInteger()) {
    PhysEntity rhs_ent = genExpr(unary->RHS.get()).load(m_Builder);
    llvm::Value *rhs = rhs_ent.load(m_Builder);
    if (!rhs)
      return nullptr;
    return m_Builder.CreateNot(rhs, "nottmp");
  }

  // [Constitution 1.3] Morphology symbols: *p (Raw Pointer Identity)
  if (unary->Op == TokenType::Star) {
    // A handle is something that contains the pointer (Unique/Shared/Reference)
    bool isHandle = false;
    if (unary->RHS->ResolvedType) {
      auto t = unary->RHS->ResolvedType;
      // Note: We only treat it as a handle if it's a Top-Level pointer
      // variable/member access.
      if ((t->isPointer() || t->isReference() || t->isSmartPointer()) &&
          (dynamic_cast<const VariableExpr *>(unary->RHS.get()) ||
           dynamic_cast<const MemberExpr *>(unary->RHS.get()))) {
        isHandle = true;
      }
    }

    if (isHandle) {
      llvm::Value *identityAddr = emitHandleAddr(unary->RHS.get());
      if (!identityAddr)
        return nullptr;
      llvm::Type *ptrTy = m_Builder.getPtrTy();
      if (unary->RHS->ResolvedType)
        ptrTy = getLLVMType(unary->RHS->ResolvedType);
      llvm::Value *val = m_Builder.CreateLoad(ptrTy, identityAddr, "raw.ident");
      std::string typeName =
          unary->RHS->ResolvedType ? unary->RHS->ResolvedType->toString() : "";
      return PhysEntity(val, typeName, ptrTy, false);
    } else {
      // It's a value (like raw[start] or a simple local i32). *p is the
      // address.
      llvm::Value *addr = emitEntityAddr(unary->RHS.get());
      if (!addr)
        return nullptr;
      std::string typeName = "";
      if (unary->RHS->ResolvedType)
        typeName = "*" + unary->RHS->ResolvedType->toString();
      return PhysEntity(addr, typeName, addr->getType(), false);
    }
  }

  // [Constitution 1.3] Reference Sigil: &p (Static Borrow)
  if (unary->Op == TokenType::Ampersand) {
    llvm::Value *soulAddr = emitEntityAddr(unary->RHS.get());
    std::string typeName = "";
    if (unary->RHS->ResolvedType)
      typeName = "&" + unary->RHS->ResolvedType->toString();
    return PhysEntity(soulAddr, typeName, m_Builder.getPtrTy(), false);
  }

  // [Constitution 1.3] Morphology symbols: ^p, ~p
  if (unary->Op == TokenType::Caret) {
    llvm::Value *identityAddr = emitHandleAddr(unary->RHS.get());
    if (!identityAddr)
      return nullptr;

    // Favor actual symbol handle type over resolved soul type
    llvm::Type *handleTy = nullptr;
    if (auto *v = dynamic_cast<const VariableExpr *>(unary->RHS.get())) {
      std::string baseName = v->Name;
      while (!baseName.empty() && (baseName[0] == '*' || baseName[0] == '^' ||
                                   baseName[0] == '~' || baseName[0] == '&'))
        baseName = baseName.substr(1);
      if (m_Symbols.count(baseName)) {
        TokaSymbol &sym = m_Symbols[baseName];
        if (sym.morphology == Morphology::Shared) {
          llvm::Type *ptrTy = llvm::PointerType::getUnqual(m_Context);
          llvm::Type *refTy =
              llvm::PointerType::getUnqual(m_Context);
          handleTy = llvm::StructType::get(m_Context, {ptrTy, refTy});
        } else if (sym.morphology == Morphology::Unique ||
                   sym.morphology == Morphology::Raw) {
          handleTy = m_Builder.getPtrTy();
        }
      }
    }
    if (!handleTy)
      handleTy = getLLVMType(unary->RHS->ResolvedType);

    bool isRHSVal = true;
    if (unary->RHS->ResolvedType) {
      auto rt = unary->RHS->ResolvedType;
      if (rt->isPointer() || rt->isReference() || rt->isSmartPointer()) {
        isRHSVal = false;
      }
    }

    if (isRHSVal) {
      // Elevate the value to a unique pointer on heap (Heap Promotion on Move)
      const llvm::DataLayout &dl = m_Module->getDataLayout();
      uint64_t size = dl.getTypeAllocSize(handleTy);
      
      llvm::Function *mallocFn = m_Module->getFunction("malloc");
      if (!mallocFn) {
        llvm::Type *sizeTy = getIntPtrTy();
        llvm::Type *ptrTy = m_Builder.getPtrTy();
        mallocFn = llvm::Function::Create(
            llvm::FunctionType::get(ptrTy, {sizeTy}, false),
            llvm::Function::ExternalLinkage, "malloc", m_Module.get());
      }
      
      llvm::Value *sizeVal = llvm::ConstantInt::get(getIntPtrTy(), size);
      llvm::Value *heapAddr = m_Builder.CreateCall(mallocFn, {sizeVal}, "heap.promoted");
      
      // Copy the value from stack to heap
      llvm::Value *val = m_Builder.CreateLoad(handleTy, identityAddr, "move.val");
      m_Builder.CreateStore(val, heapAddr);
      
      // Zero out the original stack allocation to denote a move
      if (handleTy->isStructTy()) {
        m_Builder.CreateStore(llvm::ConstantAggregateZero::get(handleTy),
                              identityAddr);
      } else {
        m_Builder.CreateStore(llvm::Constant::getNullValue(handleTy),
                              identityAddr);
      }
      
      std::string typeName =
          unary->RHS->ResolvedType ? unary->RHS->ResolvedType->toString() : "";
      return PhysEntity(heapAddr, typeName, m_Builder.getPtrTy(), false);
    }

    llvm::Value *val = m_Builder.CreateLoad(handleTy, identityAddr, "move.val");

    // For Move (^): Nullify the handle
    if (handleTy->isPointerTy()) {
      m_Builder.CreateStore(llvm::ConstantPointerNull::get(
                                llvm::cast<llvm::PointerType>(handleTy)),
                            identityAddr);
    } else if (handleTy->isStructTy()) {
      m_Builder.CreateStore(llvm::ConstantAggregateZero::get(handleTy),
                            identityAddr);
    }

    std::string typeName =
        unary->RHS->ResolvedType ? unary->RHS->ResolvedType->toString() : "";
    return PhysEntity(val, typeName, handleTy, false);
  }

  if (unary->Op == TokenType::Tilde) {
    llvm::Value *identityAddr = emitHandleAddr(unary->RHS.get());
    if (!identityAddr)
      return nullptr;

    // Favor actual symbol handle type
    llvm::Type *handleTy = nullptr;
    if (auto *v = dynamic_cast<const VariableExpr *>(unary->RHS.get())) {
      std::string baseName = v->Name;
      while (!baseName.empty() && (baseName[0] == '*' || baseName[0] == '^' ||
                                   baseName[0] == '~' || baseName[0] == '&'))
        baseName = baseName.substr(1);
      if (m_Symbols.count(baseName)) {
        TokaSymbol &sym = m_Symbols[baseName];
        if (sym.morphology == Morphology::Shared) {
          llvm::Type *ptrTy = llvm::PointerType::getUnqual(m_Context);
          llvm::Type *refTy =
              llvm::PointerType::getUnqual(m_Context);
          handleTy = llvm::StructType::get(m_Context, {ptrTy, refTy});
        }
      }
    } else if (auto *m = dynamic_cast<const MemberExpr *>(unary->RHS.get())) {
      PhysEntity pe = genMemberExpr(m);
      if (pe.isAddress && pe.irType) {
        handleTy = pe.irType;
      }
    }
    if (!handleTy)
      handleTy = getLLVMType(unary->RHS->ResolvedType);

    llvm::Value *val =
        m_Builder.CreateLoad(handleTy, identityAddr, "share.val");
    std::shared_ptr<Type> pType = nullptr;
    if (unary->RHS->ResolvedType) pType = unary->RHS->ResolvedType->getPointeeType();
    
    
    emitAcquire(val, pType);

    std::string typeName =
        unary->RHS->ResolvedType ? unary->RHS->ResolvedType->toString() : "";
    return PhysEntity(val, typeName, handleTy, false);
  }

  PhysEntity rhs_ent = genExpr(unary->RHS.get()).load(m_Builder);
  llvm::Value *rhs = rhs_ent.load(m_Builder);
  if (!rhs)
    return nullptr;
  if (unary->Op == TokenType::Bang) {
    return m_Builder.CreateNot(rhs, "nottmp");
  } else if (unary->Op == TokenType::Minus) {
    if (rhs->getType()->isFloatingPointTy())
      return m_Builder.CreateFNeg(rhs, "negtmp");
    return m_Builder.CreateNeg(rhs, "negtmp");
  } else if (unary->Op == TokenType::TokenNull) {
    // Morphology pass-through
    return rhs_ent;
  }
  return nullptr;
}

PhysEntity CodeGen::genCastExpr(const CastExpr *cast) {
  if (!cast->Expression)
    return nullptr;

  if (cast->Kind != CastKind::Conversion)
    return genExpr(cast->Expression.get());

  bool targetIsOAddr = (cast->TargetType == "OAddr");
  const UnaryExpr *UE = dynamic_cast<const UnaryExpr *>(cast->Expression.get());
  bool isCaret = (UE && UE->Op == TokenType::Caret);

  PhysEntity srcEnt;
  if (targetIsOAddr && isCaret) {
    if (const VariableExpr *v =
            dynamic_cast<const VariableExpr *>(UE->RHS.get())) {
      std::string vName = v->Name;
      while (!vName.empty() && (vName.back() == '?' || vName.back() == '!'))
        vName.pop_back();
      llvm::Value *alloca = getIdentityAddr(vName);
      if (alloca) {
        llvm::Value *ptrVal =
            m_Builder.CreateLoad(m_Builder.getPtrTy(), alloca, vName + ".peek");
        srcEnt = PhysEntity(ptrVal, v->Name, m_Builder.getPtrTy(), false);
      } else {
        srcEnt = genExpr(cast->Expression.get());
      }
    } else if (const MemberExpr *m =
                   dynamic_cast<const MemberExpr *>(UE->RHS.get())) {
      PhysEntity meEnt = genAddr(m);
      if (meEnt.value && meEnt.isAddress) {
        llvm::Value *ptrVal = m_Builder.CreateLoad(
            m_Builder.getPtrTy(), meEnt.value, m->Member + ".peek");
        srcEnt = PhysEntity(ptrVal, m->Member, m_Builder.getPtrTy(), false);
      } else {
        srcEnt = genExpr(cast->Expression.get());
      }
    } else {
      srcEnt = genExpr(cast->Expression.get());
    }
  } else {
    srcEnt = genExpr(cast->Expression.get());
  }

  // Legacy bare union L-value reinterpretation
  std::shared_ptr<Type> srcTypeObj = cast->Expression->ResolvedType;
  if (srcEnt.isAddress && srcTypeObj && srcTypeObj->isShape()) {
    auto st = std::dynamic_pointer_cast<ShapeType>(srcTypeObj);
    if (st->Decl && st->Decl->Kind == ShapeKind::Union) {
      llvm::Value *addr = srcEnt.value;
      llvm::Type *destTy = nullptr;
      if (cast->ResolvedType) {
        destTy = getLLVMType(cast->ResolvedType);
      } else {
        destTy = resolveType(cast->TargetType, false);
      }
      // [CRITICAL] bitcast address, preserving L-Value. DO NOT LOAD.
      llvm::Value *newAddr =
          m_Builder.CreateBitCast(addr, llvm::PointerType::get(m_Context, 0));
      return PhysEntity(newAddr, cast->TargetType, destTy, true);
    }
  }

  PhysEntity val_ent = srcEnt.load(m_Builder);
  llvm::Value *val = val_ent.load(m_Builder);
  if (!val)
    return nullptr;
  llvm::Type *targetType = nullptr;
  if (cast->ResolvedType) {
    targetType = getLLVMType(cast->ResolvedType);
  } else {
    targetType = resolveType(cast->TargetType, false);
  }
  if (!targetType)
    return val;

  // Rule: Unwrap Smart Pointer handles (like SharedPtr) if casting to
  // integer/pointer.
  // [Fix] Skip unwrap for Nullable Soul wrappers { T, i1 } and regular shapes.
  if (val->getType()->isStructTy() &&
      !val->getType()->isStructTy()) { // Original logic was here
    // Placeholder for removal of old aggressive logic
  }

  llvm::Type *srcType = val->getType();

  // Sema represents an owned ^T flowing into ~T as an implicit cast. A
  // pointer-sized owner cannot be reinterpreted as a shared handle; create
  // the first owning { data_ptr, ref_count_ptr } handle instead. This includes
  // factories returning ^T, which are fresh at their call boundary.
  if (cast->ResolvedType && cast->ResolvedType->isSharedPtr() &&
      isOwnedUniquePromotionSource(cast->Expression.get()) &&
      srcType->isPointerTy() && targetType->isStructTy()) {
    TokaSymbol sharedValue;
    sharedValue.morphology = Morphology::Shared;
    return PhysEntity(emitPromotion(val, targetType, sharedValue),
                      cast->TargetType, targetType, false);
  }

  // [Safety Pillar 2] Fat Pointer Downgrade. Extract raw address from Struct.
  if (srcType->isStructTy() && targetType->isPointerTy()) {
    if (cast->Expression->ResolvedType && 
       (cast->Expression->ResolvedType->isFatPointer() || 
       (cast->Expression->ResolvedType->isSharedPtr() && cast->Expression->ResolvedType->getPointeeType() && cast->Expression->ResolvedType->getPointeeType()->isSlice()))) {
        val = m_Builder.CreateExtractValue(val, {0}, "fat.downgrade");
        srcType = val->getType();
    }
  }
  if (srcType->isIntegerTy() && targetType->isIntegerTy()) {
    bool isSigned = false;
    if (cast->Expression && cast->Expression->ResolvedType) {
      isSigned = cast->Expression->ResolvedType->isSignedInteger();
    } else {
      isSigned = false;
    }
    return PhysEntity(m_Builder.CreateIntCast(val, targetType, isSigned),
                      cast->TargetType, targetType, false);
  }

  // [Fix] Safe Nullable Soul Wrap in Cast
  if (targetType->isStructTy() && targetType->getStructNumElements() == 2 &&
      targetType->getStructElementType(1)->isIntegerTy(1)) {
    // 1. T -> T?
    if (srcType == targetType->getStructElementType(0)) {
      llvm::Value *wrapped = llvm::UndefValue::get(targetType);
      wrapped = m_Builder.CreateInsertValue(wrapped, val, {0});
      wrapped = m_Builder.CreateInsertValue(
          wrapped, llvm::ConstantInt::get(llvm::Type::getInt1Ty(m_Context), 1),
          {1});
      return PhysEntity(wrapped, cast->TargetType, targetType, false);
    }
    // 2. none/null -> T?
    if (dynamic_cast<const NoneExpr *>(cast->Expression.get()) ||
        (srcType->isPointerTy() && llvm::isa<llvm::ConstantPointerNull>(val))) {
      llvm::Value *wrapped = llvm::UndefValue::get(targetType);
      wrapped = m_Builder.CreateInsertValue(
          wrapped,
          llvm::Constant::getNullValue(targetType->getStructElementType(0)),
          {0});
      wrapped = m_Builder.CreateInsertValue(
          wrapped, llvm::ConstantInt::get(llvm::Type::getInt1Ty(m_Context), 0),
          {1});
      return PhysEntity(wrapped, cast->TargetType, targetType, false);
    }
  }

  // Floating Point Conversions
  if (srcType->isFloatingPointTy() && targetType->isFloatingPointTy()) {
    return PhysEntity(m_Builder.CreateFPCast(val, targetType, "fp_cast"),
                      cast->TargetType, targetType, false);
  }
  if (srcType->isFloatingPointTy() && targetType->isIntegerTy()) {
    return PhysEntity(m_Builder.CreateFPToSI(val, targetType, "fp_to_int"),
                      cast->TargetType, targetType, false);
  }
  if (srcType->isIntegerTy() && targetType->isFloatingPointTy()) {
    return PhysEntity(m_Builder.CreateSIToFP(val, targetType, "int_to_fp"),
                      cast->TargetType, targetType, false);
  }

  // Physical Interpretation: bitcast or int-ptr cast if types are different
  if (srcType != targetType) {
    if (srcType->isPointerTy() && targetType->isPointerTy()) {
      return PhysEntity(m_Builder.CreateBitCast(val, targetType),
                        cast->TargetType, targetType, false);
    }
    if (srcType->isPointerTy() && targetType->isIntegerTy()) {
      return PhysEntity(m_Builder.CreatePtrToInt(val, targetType),
                        cast->TargetType, targetType, false);
    }
    if (srcType->isIntegerTy() && targetType->isPointerTy()) {
      return PhysEntity(m_Builder.CreateIntToPtr(val, targetType),
                        cast->TargetType, targetType, false);
    }
    // If one is not a pointer, we need alloca/bitcast (Zero-cost
    // GEP/Address logic)
    llvm::Value *tmp = createEntryBlockAlloca(srcType);
    m_Builder.CreateStore(val, tmp);
    llvm::Value *castPtr =
        m_Builder.CreateBitCast(tmp, llvm::PointerType::getUnqual(m_Context));
    // Propagate TargetType as the semantic type name
    return PhysEntity(m_Builder.CreateLoad(targetType, castPtr),
                      cast->TargetType, targetType, false);
  }
  // Propagate TargetType as the semantic type name
  return PhysEntity(val, cast->TargetType, targetType, false);
}

PhysEntity CodeGen::genVariableExpr(const VariableExpr *var) {
  // [Annotated AST] Constant Substitution: RValue Generation

  llvm::Value *soulAddr = nullptr;
  bool isShared = false;
  std::string varName = var->codegenName();
  std::string checkName = varName;
  // Strip morphology for lookup
  while (!checkName.empty() &&
         (checkName.back() == '?' || checkName.back() == '!'))
    checkName.pop_back();

  // Use getEntityAddr to get the Soul address (fully dereferenced if needed)
  soulAddr = getEntityAddr(varName);

  if (var->ResolvedType && var->ResolvedType->isSharedPtr()) {
    isShared = true;
    soulAddr = getIdentityAddr(varName); // [Fix] Handle Address for RValue
  } else if (m_Symbols.count(checkName) &&
             m_Symbols[checkName].morphology == Morphology::Shared) {
    isShared = true;
    soulAddr = getIdentityAddr(checkName); // [Fix] Handle Address for RValue
  }

  if (!soulAddr) {
    return nullptr;
  }

  // Get the base name (no morphology) for symbol lookup
  std::string baseName = varName;
  while (!baseName.empty() &&
         (baseName[0] == '*' || baseName[0] == '#' || baseName[0] == '&' ||
          baseName[0] == '^' || baseName[0] == '~' || baseName[0] == '!'))
    baseName = baseName.substr(1);
  while (!baseName.empty() &&
         (baseName.back() == '#' || baseName.back() == '?' ||
          baseName.back() == '!'))
    baseName.pop_back();

  llvm::Type *soulType = nullptr;
  if (m_Symbols.count(baseName)) {
    soulType = m_Symbols[baseName].soulType;
  } else {
    // [Fix] Closure Environment Fallback: Retrieve exact type from ShapeDecl
    if (m_Symbols.count("self")) {
      auto selfTy = m_Symbols["self"].soulTypeObj;
      if (selfTy && selfTy->isReference()) {
        selfTy = std::static_pointer_cast<toka::PointerType>(selfTy)->PointeeType;
      }
      if (selfTy && selfTy->isShape() && selfTy->getSoulName().find("__Closure_") == 0) {
        auto shapeTy = std::static_pointer_cast<ShapeType>(selfTy);
        if (shapeTy->Decl) {
          for (const auto &memb : shapeTy->Decl->Members) {
            if (memb.Name == baseName) {
              if (memb.ResolvedType && memb.ResolvedType->isReference()) {
                soulType = getLLVMType(std::static_pointer_cast<toka::PointerType>(memb.ResolvedType)->PointeeType);
              } else if (memb.ResolvedType) {
                soulType = getLLVMType(memb.ResolvedType);
              }
              break;
            }
          }
        }
      }
    }

    if (!soulType && var->ResolvedType) {
      // [New] Use ResolvedType as fallback if not in symbols
      soulType = getLLVMType(var->ResolvedType);
    } else if (!soulType) {
      // Fallback for globals/externs
      if (auto *ai = llvm::dyn_cast<llvm::AllocaInst>(soulAddr)) {
        soulType = ai->getAllocatedType();
      } else if (auto *li = llvm::dyn_cast<llvm::LoadInst>(soulAddr)) {
        soulType = li->getType();
      } else if (auto *gv = llvm::dyn_cast<llvm::GlobalVariable>(soulAddr)) {
        soulType = gv->getValueType();
      }
    }
  }

  if (!soulType) {
  }

  // A generic local can be instantiated as a reference even when its copied
  // AST annotation is still the template placeholder.  At a direct stack
  // slot, the allocated LLVM type is authoritative; using the stale scalar
  // metadata would load an i32 from a pointer slot and later emit an invalid
  // i32-to-ptr bitcast on return or Option construction.
  if (auto *slot = llvm::dyn_cast<llvm::AllocaInst>(soulAddr)) {
    llvm::Type *storageType = slot->getAllocatedType();
    if (storageType && soulType && storageType != soulType)
      soulType = storageType;
  }

  // [Fix] Shared Pointer Handle Type Correction
  if (isShared && soulType) {
    llvm::Type *ptrTy = llvm::PointerType::getUnqual(m_Context);
    llvm::Type *refTy =
        llvm::PointerType::getUnqual(m_Context);
    soulType = llvm::StructType::get(m_Context, {ptrTy, refTy});
  } else if (var->ResolvedType && var->ResolvedType->isUniquePtr() &&
             soulType) {
    // [Fix] Unique Pointer Handle is Ptr to Soul
    // The symbol stores the Soul type (Data), but the alloca stores Data*.
    soulType = llvm::PointerType::getUnqual(m_Context);
  }

  if (!soulType) {
    // Last resort for opaque pointers if we really don't know the type
    // (though soulType should be set for all Toka-defined variables)
    soulType = m_Builder.getPtrTy();
  }

  std::string typeName = "";
  TokaSymbol *sym = nullptr;
  if (m_Symbols.count(baseName)) {
    typeName = m_Symbols[baseName].typeName;
    sym = &m_Symbols[baseName];
  } else if (m_TypeToName.count(soulType)) {
    typeName = m_TypeToName[soulType];
  }

  // [Fix] Uniform Ownership Transfer for Smart Pointers
  // If we are using a Smart Pointer as an RValue (not in LHS), we must perform
  // an ownership transfer to ensure it survives the current statement.
  bool isUnique = (var->ResolvedType && var->ResolvedType->isUniquePtr());
  if (sym && sym->morphology == Morphology::Unique)
    isUnique = true;

  // [Fix] Handle Type Adjustment for Unique Pointers
  // if (isUnique && soulType && !soulType->isPointerTy()) {
  //   soulType = llvm::PointerType::getUnqual(m_Context);
  // }

  if (!m_InLHS && soulAddr && !llvm::isa<llvm::Function>(soulAddr) &&
      !llvm::isa<llvm::GlobalVariable>(soulAddr)) {

    if (var->ResolvedType && var->ResolvedType->isSharedPtr()) {
      // SharedPtr: Share (Load + Acquire)
      llvm::Value *val = m_Builder.CreateLoad(soulType, soulAddr, "share.val");
      emitAcquire(val, var->ResolvedType->getPointeeType());
      return PhysEntity(val, typeName, soulType, false); // Return RValue
    }
  }

  return PhysEntity(soulAddr, typeName, soulType, true);
}

PhysEntity CodeGen::genLiteralExpr(const Expr *expr) {
  if (auto *num = dynamic_cast<const NumberExpr *>(expr)) {
    if (expr->ResolvedType && expr->ResolvedType->isInteger()) {
      llvm::Type *targetTy = getLLVMType(expr->ResolvedType);
      if (targetTy && targetTy->isIntegerTy()) {
        return llvm::ConstantInt::get(targetTy, num->Value);
      }
    }
    if (num->Value > 2147483647) {
      return llvm::ConstantInt::get(llvm::Type::getInt64Ty(m_Context),
                                    num->Value);
    }
    return llvm::ConstantInt::get(llvm::Type::getInt32Ty(m_Context),
                                  num->Value);
  }
  if (auto *flt = dynamic_cast<const FloatExpr *>(expr)) {
    if (expr->ResolvedType) {
      llvm::Type *targetTy = getLLVMType(expr->ResolvedType);
      if (targetTy && targetTy->isFloatingPointTy())
        return llvm::ConstantFP::get(targetTy, flt->Value);
    }
    return llvm::ConstantFP::get(llvm::Type::getDoubleTy(m_Context),
                                 flt->Value);
  }
  if (auto *bl = dynamic_cast<const BoolExpr *>(expr)) {
    return llvm::ConstantInt::get(llvm::Type::getInt1Ty(m_Context), bl->Value);
  }
  if (dynamic_cast<const NullExpr *>(expr) ||
      dynamic_cast<const NoneExpr *>(expr)) {
    return llvm::ConstantPointerNull::get(m_Builder.getPtrTy());
  }
  if (auto *str = dynamic_cast<const StringExpr *>(expr)) {
    llvm::Value *ptr = m_Builder.CreateGlobalString(str->Value);
    if (str->ResolvedType && str->ResolvedType->isShape()) {
      std::string shpName = str->ResolvedType->toString();
      if (shpName == "str") {
        llvm::Type *fatTy = getLLVMType(str->ResolvedType);
        llvm::Value *fatVal = llvm::UndefValue::get(fatTy);
        fatVal = m_Builder.CreateInsertValue(fatVal, ptr, {0});
        fatVal = m_Builder.CreateInsertValue(
            fatVal,
            llvm::ConstantInt::get(getIntPtrTy(),
                                   str->Value.size()),
            {1});
        return PhysEntity(fatVal, "str", fatVal->getType(), false);
      } else if (shpName == "cstr") {
        llvm::Type *cstrTy = getLLVMType(str->ResolvedType);
        llvm::Value *cstrVal = llvm::UndefValue::get(cstrTy);
        cstrVal = m_Builder.CreateInsertValue(cstrVal, ptr, {0});
        return PhysEntity(cstrVal, "cstr", cstrVal->getType(), false);
      }
    }
    return PhysEntity(ptr, "*char", ptr->getType(), false);
  }
  if (auto *vstr = dynamic_cast<const ViewStringExpr *>(expr)) {
    llvm::Value *ptr = m_Builder.CreateGlobalString(vstr->Value);
    if (!vstr->ResolvedType) return nullptr;
    llvm::Type *fatTy = getLLVMType(vstr->ResolvedType);
    llvm::Value *fatVal = llvm::UndefValue::get(fatTy);
    fatVal = m_Builder.CreateInsertValue(fatVal, ptr, {0});
    fatVal = m_Builder.CreateInsertValue(
        fatVal,
        llvm::ConstantInt::get(getIntPtrTy(),
                               vstr->Value.size()),
        {1});
    return PhysEntity(fatVal, "str", fatVal->getType(), false);
  }
  if (auto *chr = dynamic_cast<const CharLiteralExpr *>(expr)) {
    if (expr->ResolvedType) {
      llvm::Type *targetTy = getLLVMType(expr->ResolvedType);
      if (targetTy && targetTy->isIntegerTy())
        return llvm::ConstantInt::get(targetTy, chr->Value);
    }
    return llvm::ConstantInt::get(llvm::Type::getInt8Ty(m_Context), chr->Value);
  }
  return nullptr;
}

llvm::Value *CodeGen::genNullCheck(llvm::Value *val, const ASTNode *node,
                                   const std::string &msg) {
  if (!val)
    return val;

  llvm::Value *nn = nullptr;
  if (val->getType()->isPointerTy()) {
    nn = m_Builder.CreateIsNotNull(val, "nn_check");
  } else if (val->getType()->isIntegerTy(1)) {
    // For Soul flags, nn is just the value itself
    nn = val;
  } else {
    return val;
  }
  llvm::Function *f = m_Builder.GetInsertBlock()->getParent();
  llvm::BasicBlock *okBB = llvm::BasicBlock::Create(m_Context, "nn.ok", f);
  llvm::BasicBlock *panicBB =
      llvm::BasicBlock::Create(m_Context, "nn.panic", f);
  m_Builder.CreateCondBr(nn, okBB, panicBB);

  m_Builder.SetInsertPoint(panicBB);

  // Precision Panic: call __toka_panic(message, file, line)
  llvm::Function *panicFunc = m_Module->getFunction("__toka_panic");
  if (!panicFunc) {
    // Declare if not found (happens during early codegen stages or if not
    // imported)
    llvm::Type *strTy = llvm::StructType::get(
        m_Context, {m_Builder.getPtrTy(), getIntPtrTy()});
    llvm::Type *ptrTy = llvm::PointerType::getUnqual(m_Context);
    std::vector<llvm::Type *> panicArgs = {ptrTy, ptrTy,
                                           m_Builder.getInt32Ty()};
    llvm::FunctionType *panicFT =
        llvm::FunctionType::get(m_Builder.getVoidTy(), panicArgs, false);
    panicFunc = llvm::Function::Create(panicFT, llvm::Function::ExternalLinkage,
                                       "__toka_panic", m_Module.get());
  }

  // Extract location info
  std::string fileName = "";
  int line = -1;
  if (node) {
    auto fullLoc = DiagnosticEngine::SrcMgr->getFullSourceLoc(node->Loc);
    if (fullLoc.isValid()) {
      fileName = fullLoc.FileName;
      line = (int)fullLoc.Line;
    }
  }

  llvm::Type *strTy = llvm::StructType::get(
      m_Context, {m_Builder.getPtrTy(), getIntPtrTy()});

  llvm::Value *msgPtr = m_Builder.CreateGlobalString(msg, "panic_msg");
  llvm::Value *msgStr = llvm::UndefValue::get(strTy);
  msgStr = m_Builder.CreateInsertValue(msgStr, msgPtr, 0);
  msgStr = m_Builder.CreateInsertValue(msgStr, llvm::ConstantInt::get(getIntPtrTy(), msg.length()), 1);

  llvm::Value *filePtr = m_Builder.CreateGlobalString(fileName, "panic_file");
  llvm::Value *fileStr = llvm::UndefValue::get(strTy);
  fileStr = m_Builder.CreateInsertValue(fileStr, filePtr, 0);
  fileStr = m_Builder.CreateInsertValue(fileStr, llvm::ConstantInt::get(getIntPtrTy(), fileName.length()), 1);

  // Toka ABI passes aggregates (shapes) by pointer.
  llvm::IRBuilder<> tmpB(&f->getEntryBlock(), f->getEntryBlock().begin());
  llvm::Value *msgAlloc = tmpB.CreateAlloca(strTy, nullptr, "panic_msg_alloc");
  llvm::Value *fileAlloc = tmpB.CreateAlloca(strTy, nullptr, "panic_file_alloc");
  
  m_Builder.CreateStore(msgStr, msgAlloc);
  m_Builder.CreateStore(fileStr, fileAlloc);

  std::vector<llvm::Value *> args;
  args.push_back(msgAlloc);
  args.push_back(fileAlloc);
  args.push_back(m_Builder.getInt32(line));

  m_Builder.CreateCall(panicFunc, args);

  // Ensure execution terminates even if __toka_panic somehow returns (it
  // shouldn't)
  llvm::Function *trap =
      llvm::Intrinsic::getOrInsertDeclaration(m_Module.get(), llvm::Intrinsic::trap);
  m_Builder.CreateCall(trap);
  m_Builder.CreateUnreachable();

  m_Builder.SetInsertPoint(okBB);
  return val;
}

PhysEntity CodeGen::genMatchExpr(const MatchExpr *expr) {
  // [New] Match expressions create their own temporary scope
  m_ScopeStack.push_back({});

  bool isCeded = dynamic_cast<const CedeExpr*>(expr->Target.get()) != nullptr;

  bool hasDirectReferencePattern = false;
  for (const auto &arm : expr->Arms) {
    if (arm->Pat && arm->Pat->PatternKind == MatchArm::Pattern::Variable &&
        arm->Pat->IsReference) {
      hasDirectReferencePattern = true;
      break;
    }
  }

  // A direct reference pattern must borrow an addressable scrutinee, not a
  // staging copy of its loaded value.  Generate the source address first so
  // an index expression is evaluated exactly once and the same storage feeds
  // both pattern matching and the binder.
  llvm::Value *targetAddr = nullptr;
  llvm::Type *targetType = nullptr;
  llvm::Value *targetVal = nullptr;
  PhysEntity targetValEnt;
  if (hasDirectReferencePattern && expr->Target && expr->Target->ResolvedType) {
    targetAddr = genAddr(expr->Target.get());
    targetType = getLLVMType(expr->Target->ResolvedType);
    if (targetAddr && targetType && !targetType->isVoidTy()) {
      targetVal = m_Builder.CreateLoad(targetType, targetAddr, "match_target");
      targetValEnt = PhysEntity(targetAddr, "", targetType, true);
    } else {
      targetAddr = nullptr;
    }
  }

  if (!targetVal) {
    targetValEnt = genExpr(expr->Target.get());
    targetVal = targetValEnt.load(m_Builder);
    targetType = targetVal->getType();
  }
  std::string targetSemaType = expr->Target->ResolvedType ? expr->Target->ResolvedType->toString() : "";

  std::string shapeName;
  if (targetType->isStructTy() && m_TypeToName.count(targetType)) {
    shapeName = m_TypeToName[targetType];
  }

  // Create result alloca (Assume i32 for now, ideally get from Sema or expr)
  llvm::Type *resultType = llvm::Type::getInt32Ty(m_Context);
  if (expr->ResolvedType) {
    resultType = getLLVMType(expr->ResolvedType);
  }

  if (!resultType)
    resultType = llvm::Type::getInt32Ty(m_Context);

  llvm::AllocaInst *resultAddr = nullptr;
  if (!resultType->isVoidTy() &&
      !(expr->ResolvedType && expr->ResolvedType->isUnit())) {
    resultAddr =
        createEntryBlockAlloca(resultType, nullptr, "match_result_addr");
  }

  // Use the physical address if it already exists, otherwise create a temporary staging block
  bool isNewlyAllocated = false;
  if (targetAddr) {
      // Direct reference pattern uses the original lvalue address above.
  } else if (targetValEnt.isAddress) {
      targetAddr = targetValEnt.value;
  } else {
      targetAddr = createEntryBlockAlloca(targetType, nullptr, "match_target_addr");
      m_Builder.CreateStore(targetVal, targetAddr);
      isNewlyAllocated = true;
  }

  // [New] Temporary Lifetime Extension
  // If the target is an RValue temporary and needs its lifetime extended,
  // register it in the current scope so it survives until the end of the block.
  if (expr->Target && expr->Target->ExtendLifetime && isNewlyAllocated && !m_ScopeStack.empty()) {
      bool hasDrop = false;
      std::string baseShapeName = shapeName;
      if (baseShapeName.find('<') != std::string::npos) {
          baseShapeName = baseShapeName.substr(0, baseShapeName.find('<'));
      }
      if (!baseShapeName.empty() && m_Shapes.count(baseShapeName)) {
          hasDrop = true;
      }
      bool isUnique = expr->Target->ResolvedType ? expr->Target->ResolvedType->isUniquePtr() : false;
      bool isShared = expr->Target->ResolvedType ? expr->Target->ResolvedType->isSharedPtr() : false;
      if (isUnique || isShared) {
          hasDrop = true;
      }
      const Expr *targetInner = expr->Target.get();
      if (isCeded) {
          // `match cede value` transfers the scrutinee into its pattern
          // binders.  The staging alloca is only transport storage; letting
          // its scope cleanup run would destroy the transferred payload a
          // second time.
          hasDrop = false;
      }
      while (targetInner && hasDrop) {
          if (dynamic_cast<const CedeExpr*>(targetInner)) {
              hasDrop = false;
              break;
          }
          if (auto *ce = dynamic_cast<const CastExpr*>(targetInner)) {
              targetInner = ce->Expression.get();
          } else if (auto *ue = dynamic_cast<const UnaryExpr*>(targetInner)) {
              targetInner = ue->RHS.get();
          } else if (auto *pe = dynamic_cast<const PostfixExpr*>(targetInner)) {
              targetInner = pe->LHS.get();
          } else {
              break;
          }
      }
      
      if (hasDrop) {
          VariableScopeInfo vsi;
          static int matchExtId = 0;
          if (auto *ve = dynamic_cast<const VariableExpr*>(expr->Target.get())) {
              vsi.Name = ve->Name;
          } else {
              vsi.Name = ".match_ext_" + std::to_string(matchExtId++);
          }
          vsi.Alloca = targetAddr;
          vsi.AllocType = targetType;
          vsi.IsUniquePointer = isUnique;
          vsi.IsShared = isShared;
          vsi.HasDrop = true;
          vsi.SoulName = shapeName; // Original full shape name
          m_ScopeStack.back().push_back(vsi);
      }
  }

  llvm::Function *func = m_Builder.GetInsertBlock()->getParent();
  llvm::BasicBlock *mergeBB =
      llvm::BasicBlock::Create(m_Context, "match_merge", func);

  // For Enums, we use a Switch
  std::string baseShapeName = shapeName;
  if (baseShapeName.find('<') != std::string::npos) {
    baseShapeName = baseShapeName.substr(0, baseShapeName.find('<'));
  }

  bool anyArmHasGuard = false;
  for (const auto &arm : expr->Arms) {
    if (arm->Guard) {
      anyArmHasGuard = true;
      break;
    }
  }

  // The enum switch fast path can only dispatch each outer variant tag once.
  // Patterns such as `Option<Flavor>::Some(Plain)` and
  // `Option<Flavor>::Some(Spicy)` share the outer `Some` tag and must fall
  // back to the general matcher, which checks the payload pattern after the
  // outer variant has matched.  Adding both directly to an LLVM switch would
  // create duplicate case values and invalid IR.
  bool hasRepeatedEnumTag = false;
  if (baseShapeName != "" && m_Shapes.count(baseShapeName) &&
      m_Shapes[baseShapeName]->Kind == ShapeKind::Enum) {
    const ShapeDecl *shape = m_Shapes[baseShapeName];
    std::set<int> seenTags;
    auto recordPatternTag = [&](const MatchArm::Pattern *pattern) {
      if (!pattern)
        return;
      std::string patternName = pattern->Name;
      size_t scopePos = patternName.rfind("::");
      if (scopePos != std::string::npos)
        patternName = patternName.substr(scopePos + 2);
      for (size_t i = 0; i < shape->Members.size(); ++i) {
        if (shape->Members[i].Name != patternName)
          continue;
        int tag = shape->Members[i].TagValue == -1
                      ? static_cast<int>(i)
                      : static_cast<int>(shape->Members[i].TagValue);
        if (!seenTags.insert(tag).second)
          hasRepeatedEnumTag = true;
        return;
      }
    };
    for (const auto &arm : expr->Arms) {
      if (arm->Pat->PatternKind == MatchArm::Pattern::Or) {
        for (const auto &subPattern : arm->Pat->SubPatterns)
          recordPatternTag(subPattern.get());
      } else {
        recordPatternTag(arm->Pat.get());
      }
    }
  }

  std::function<bool(const MatchArm::Pattern *)> hasValueConstraint =
      [&](const MatchArm::Pattern *pattern) {
        if (!pattern)
          return false;
        if (pattern->PatternKind == MatchArm::Pattern::Literal ||
            pattern->PatternKind == MatchArm::Pattern::Range ||
            (pattern->PatternKind == MatchArm::Pattern::Variable &&
             pattern->Binding ==
                 MatchArm::Pattern::BindingOrigin::Existing)) {
          return true;
        }
        for (const auto &subPattern : pattern->SubPatterns) {
          if (hasValueConstraint(subPattern.get()))
            return true;
        }
        return false;
      };
  std::function<bool(const MatchArm::Pattern *)> isTopLevelZeroPayloadVariant =
      [&](const MatchArm::Pattern *pattern) {
        if (!pattern || baseShapeName.empty() || !m_Shapes.count(baseShapeName) ||
            m_Shapes[baseShapeName]->Kind != ShapeKind::Enum) {
          return false;
        }
        if (pattern->PatternKind == MatchArm::Pattern::Or) {
          return !pattern->SubPatterns.empty() &&
                 std::all_of(pattern->SubPatterns.begin(),
                             pattern->SubPatterns.end(),
                             [&](const auto &subPattern) {
                               return isTopLevelZeroPayloadVariant(
                                   subPattern.get());
                             });
        }
        if (pattern->PatternKind != MatchArm::Pattern::Variable)
          return false;

        std::string variantName = pattern->Name;
        const size_t scopePos = variantName.rfind("::");
        if (scopePos != std::string::npos)
          variantName = variantName.substr(scopePos + 2);
        for (const auto &member : m_Shapes[baseShapeName]->Members) {
          if (member.Name == variantName && member.IsUnitVariant) {
            return true;
          }
        }
        return false;
      };
  bool hasValueConstraints = false;
  for (const auto &arm : expr->Arms) {
    if (hasValueConstraint(arm->Pat.get()) &&
        !isTopLevelZeroPayloadVariant(arm->Pat.get())) {
      hasValueConstraints = true;
      break;
    }
  }

  if (baseShapeName != "" && m_Shapes.count(baseShapeName) &&
      m_Shapes[baseShapeName]->Kind == ShapeKind::Enum && !anyArmHasGuard &&
      !hasRepeatedEnumTag && !hasValueConstraints) {
    const ShapeDecl *sh = m_Shapes[baseShapeName];
    llvm::Value *tagVal = m_Builder.CreateExtractValue(targetVal, 0, "tag");

    llvm::BasicBlock *defaultBB =
        llvm::BasicBlock::Create(m_Context, "match_default", func);
    llvm::SwitchInst *sw =
        m_Builder.CreateSwitch(tagVal, defaultBB, expr->Arms.size());

    std::vector<bool> handledArms(expr->Arms.size(), false);

    for (size_t k = 0; k < expr->Arms.size(); ++k) {
      const auto &arm = expr->Arms[k];
      
      struct VariantMatchInfo {
        int Tag = -1;
        const ShapeMember *Variant = nullptr;
        const MatchArm::Pattern *Pat = nullptr;
      };
      std::vector<VariantMatchInfo> variantsMatched;

      if (arm->Pat->PatternKind == MatchArm::Pattern::Or) {
        for (auto &subPat : arm->Pat->SubPatterns) {
          int tag = -1;
          const ShapeMember *variant = nullptr;
          for (size_t i = 0; i < sh->Members.size(); ++i) {
            std::string patName = subPat->Name;
            size_t scopePos = patName.rfind("::");
            if (scopePos != std::string::npos) {
              patName = patName.substr(scopePos + 2);
            }
            if (sh->Members[i].Name == patName) {
              tag = (sh->Members[i].TagValue == -1) ? (int)i
                                                    : (int)sh->Members[i].TagValue;
              variant = &sh->Members[i];
              break;
            }
          }
          if (tag != -1) {
            variantsMatched.push_back({tag, variant, subPat.get()});
          }
        }
      } else {
        int tag = -1;
        const ShapeMember *variant = nullptr;
        for (size_t i = 0; i < sh->Members.size(); ++i) {
          std::string patName = arm->Pat->Name;
          size_t scopePos = patName.rfind("::");
          if (scopePos != std::string::npos) {
            patName = patName.substr(scopePos + 2);
          }
          if (sh->Members[i].Name == patName) {
            tag = (sh->Members[i].TagValue == -1) ? (int)i
                                                  : (int)sh->Members[i].TagValue;
            variant = &sh->Members[i];
            break;
          }
        }
        if (tag != -1) {
          variantsMatched.push_back({tag, variant, arm->Pat.get()});
        }
      }

      if (!variantsMatched.empty()) {
        handledArms[k] = true;
        llvm::BasicBlock *armBodyBB =
            llvm::BasicBlock::Create(m_Context, "arm_body", func);

        // Pre-push the shared arm body scope so all pattern bindings register to it
        m_ScopeStack.push_back({});

        for (const auto &vMatch : variantsMatched) {
          llvm::BasicBlock *caseBB =
              llvm::BasicBlock::Create(m_Context, "case_" + vMatch.Variant->Name, func);
          sw->addCase(llvm::cast<llvm::ConstantInt>(
                          llvm::ConstantInt::get(tagVal->getType(), vMatch.Tag)),
                      caseBB);

          m_Builder.SetInsertPoint(caseBB);

          const ShapeMember *variant = vMatch.Variant;
          const MatchArm::Pattern *subPat = vMatch.Pat;

          if (!variant->IsUnitVariant &&
              (!variant->SubMembers.empty() || !variant->Type.empty())) {
            llvm::Value *payloadAddr =
                m_Builder.CreateStructGEP(targetType, targetAddr, 1);

            if (subPat->SubPatterns.size() == 1 &&
                subPat->SubPatterns[0]->IsReference &&
                (variant->Type == "void" ||
                 (variant->SubMembers.size() == 1 &&
                  variant->SubMembers[0].ResolvedType &&
                  variant->SubMembers[0].ResolvedType->isReference()))) {
              // A specialized generic enum can retain `void` in its AST
              // variant.  Whether its physical payload is a pointer comes
              // from the resolved generic argument, not from the pattern:
              // `Some(&value)` borrows an owned `T`, whereas `Some(&view)`
              // may unwrap an actual `&T` payload.
              std::shared_ptr<Type> payloadTypeObj = nullptr;
              if (variant->SubMembers.size() == 1 &&
                  variant->SubMembers[0].ResolvedType) {
                payloadTypeObj = variant->SubMembers[0].ResolvedType;
              }
              std::shared_ptr<Type> targetTypeObj = nullptr;
              // Prefer the matched expression: it retains the concrete
              // generic arguments, unlike the `Result::Ok`-style pattern.
              if (expr->Target)
                targetTypeObj = expr->Target->ResolvedType;
              if (!targetTypeObj) {
                size_t scopePos = subPat->Name.rfind("::");
                if (scopePos != std::string::npos) {
                  std::string patternShape = subPat->Name.substr(0, scopePos);
                  auto alias = m_TypeAliases.find(patternShape);
                  targetTypeObj = alias == m_TypeAliases.end()
                                      ? std::make_shared<ShapeType>(patternShape)
                                      : alias->second;
                }
              }
              if (!payloadTypeObj && targetTypeObj && targetTypeObj->isShape()) {
                auto targetShape = std::static_pointer_cast<ShapeType>(
                    targetTypeObj);
                if (targetShape->GenericArgs.size() == 1)
                  payloadTypeObj = targetShape->GenericArgs[0];
              }
              if (payloadTypeObj && !payloadTypeObj->isReference()) {
                genPatternBinding(subPat->SubPatterns[0].get(), payloadAddr,
                                  getLLVMType(payloadTypeObj),
                                  payloadTypeObj);
              } else {
                llvm::Value *payloadRef = m_Builder.CreateLoad(
                    llvm::PointerType::getUnqual(m_Context), payloadAddr,
                    "enum_ref_payload");
                genPatternBinding(subPat->SubPatterns[0].get(), payloadRef,
                                  llvm::PointerType::getUnqual(m_Context),
                                  payloadTypeObj);
              }
            } else {
            llvm::Type *payloadLayoutType = nullptr;
            std::vector<llvm::Type *> fieldTypes;

            if (!variant->SubMembers.empty()) {
              for (const auto &f : variant->SubMembers) {
                if (f.ResolvedType) {
                  fieldTypes.push_back(getLLVMType(f.ResolvedType));
                } else {
                  fieldTypes.push_back(resolveType(f.Type, false));
                }
              }
              payloadLayoutType =
                  llvm::StructType::get(m_Context, fieldTypes, false);
            } else if (!variant->Type.empty()) {
              if (variant->ResolvedType) {
                  payloadLayoutType = getLLVMType(variant->ResolvedType);
              } else {
                  payloadLayoutType = resolveType(variant->Type, false);
              }
            }

            if (payloadLayoutType) {
              llvm::Value *variantAddr = m_Builder.CreateBitCast(
                  payloadAddr, llvm::PointerType::getUnqual(m_Context));

              size_t elisionIndex = -1;
              size_t elisionCount = 0;
              for (size_t i = 0; i < subPat->SubPatterns.size(); ++i) {
                if (subPat->SubPatterns[i]->PatternKind == MatchArm::Pattern::Elision) {
                  elisionIndex = i;
                  elisionCount++;
                }
              }

              size_t expectedSize = fieldTypes.empty() ? 1 : fieldTypes.size();
              size_t elidedCount = 0;
              if (elisionCount == 1) {
                elidedCount = expectedSize - (subPat->SubPatterns.size() - 1);
              }

              for (size_t i = 0; i < subPat->SubPatterns.size(); ++i) {
                if (subPat->SubPatterns[i]->PatternKind == MatchArm::Pattern::Elision) {
                  continue;
                }

                size_t memberIndex = i;
                if (elisionCount == 1) {
                  memberIndex = (i < elisionIndex) ? i : (i + elidedCount - 1);
                }

                if (fieldTypes.empty() && memberIndex > 0)
                  break;
                if (!fieldTypes.empty() && memberIndex >= fieldTypes.size())
                  break;

                llvm::Value *fieldAddr = variantAddr;
                llvm::Type *fieldTy = payloadLayoutType;

                if (!fieldTypes.empty()) {
                  fieldAddr = m_Builder.CreateStructGEP(payloadLayoutType,
                                                        variantAddr, memberIndex);
                  fieldTy = fieldTypes[memberIndex];
                }

                std::shared_ptr<Type> subTypeObj = nullptr;
                if (variant->SubMembers.size() > memberIndex && variant->SubMembers[memberIndex].ResolvedType) {
                    subTypeObj = variant->SubMembers[memberIndex].ResolvedType;
                } else if (variant->ResolvedType) {
                    subTypeObj = variant->ResolvedType;
                }

                genPatternBinding(subPat->SubPatterns[i].get(), fieldAddr,
                                  fieldTy, subTypeObj);
              }
            }
            }
          }
          m_Builder.CreateBr(armBodyBB);
        }

        m_Builder.SetInsertPoint(armBodyBB);

        if (isCeded && baseShapeName != "" && m_Shapes.count(baseShapeName)) {
            int movedTag = -1;
            for (size_t i = 0; i < sh->Members.size(); ++i) {
                if (sh->Members[i].Name == "Moved") {
                    movedTag = sh->Members[i].TagValue != -1 ? sh->Members[i].TagValue : (int)i;
                    break;
                }
            }
            if (movedTag != -1) {
                llvm::Value *tagAddr = m_Builder.CreateStructGEP(targetType, targetAddr, 0);
                m_Builder.CreateStore(llvm::ConstantInt::get(llvm::Type::getInt8Ty(m_Context), movedTag), tagAddr);
            }
        }

        m_CFStack.push_back(
            {"", mergeBB, nullptr, resultAddr, m_ScopeStack.size()});
        genStmt(arm->Body.get());
        m_CFStack.pop_back();

        executeScopeUnwinding(m_ScopeStack.size() - 1);
        m_ScopeStack.pop_back();
        if (m_Builder.GetInsertBlock() &&
            !m_Builder.GetInsertBlock()->getTerminator())
          m_Builder.CreateBr(mergeBB);
      } else {
        // No match logic here intended, loop continues
      }
    }

    m_Builder.SetInsertPoint(defaultBB);
    // Find Wildcard OR Variable arm if any
    for (size_t k = 0; k < expr->Arms.size(); ++k) {
      if (handledArms[k])
        continue;
      const auto &arm = expr->Arms[k];

      if (arm->Pat->PatternKind == MatchArm::Pattern::Wildcard ||
          arm->Pat->PatternKind == MatchArm::Pattern::Variable) {
        m_ScopeStack.push_back({});

        // [Fix] Bind Variable Pattern if needed
        if (arm->Pat->PatternKind == MatchArm::Pattern::Variable) {
          genPatternBinding(arm->Pat.get(), targetAddr, targetType, expr->Target->ResolvedType);
        }

        m_CFStack.push_back(
            {"", mergeBB, nullptr, resultAddr, m_ScopeStack.size()});
        genStmt(arm->Body.get());
        m_CFStack.pop_back();
        executeScopeUnwinding(m_ScopeStack.size() - 1);
        m_ScopeStack.pop_back();
        break;
      }
    }
    if (m_Builder.GetInsertBlock() &&
        !m_Builder.GetInsertBlock()->getTerminator())
      m_Builder.CreateBr(mergeBB);

    // [Fix] Explicitly finish the if block logic (though 'else' follows)
    // We don't want to fall through to General Match logic in any weird edge
    // case. The C++ control flow jumps to 'else' boundary, so it's fine.
  } else {
    // General pattern matching (Sequence of Ifs)
    auto emitExistingBindingEquality =
        [&](const MatchArm::Pattern *pat, llvm::Value *matchedValue,
            const std::string &matchedType) -> llvm::Value * {
      if (!pat || !pat->ExistingBindingType)
        return m_Builder.getInt1(false);

      auto existing = std::make_unique<VariableExpr>(pat->Name);
      existing->ResolvedType = pat->ExistingBindingType;

      if (!pat->EqualityMethod.empty()) {
        static unsigned patternValueId = 0;
        const std::string temporaryName =
            ".match_value_" + std::to_string(patternValueId++);
        llvm::AllocaInst *temporaryAddr = createEntryBlockAlloca(
            matchedValue->getType(), nullptr, temporaryName);
        m_Builder.CreateStore(matchedValue, temporaryAddr);

        TokaSymbol temporary;
        temporary.allocaPtr = temporaryAddr;
        temporary.soulType = matchedValue->getType();
        temporary.typeName = matchedType;
        temporary.soulTypeObj = pat->MatchedValueType;
        m_Symbols.emplace(temporaryName, temporary);

        auto matched = std::make_unique<VariableExpr>(temporaryName);
        matched->ResolvedType = pat->MatchedValueType;
        std::vector<std::unique_ptr<Expr>> args;
        args.push_back(std::move(existing));
        MethodCallExpr equality(std::move(matched), pat->EqualityMethod,
                                std::move(args));
        equality.ResolvedType = lowerTypeSyntax(nullptr, "bool");
        llvm::Value *result = genMethodCall(&equality).load(m_Builder);
        m_Symbols.erase(temporaryName);
        return result ? result : m_Builder.getInt1(false);
      }

      llvm::Value *existingValue = genExpr(existing.get()).load(m_Builder);
      if (!existingValue)
        return m_Builder.getInt1(false);

      auto unwrapHandle = [&](llvm::Value *value) {
        while (value->getType()->isStructTy()) {
          const unsigned count = value->getType()->getStructNumElements();
          if (count != 1 && count != 2)
            break;
          value = m_Builder.CreateExtractValue(value, 0);
        }
        return value;
      };
      matchedValue = unwrapHandle(matchedValue);
      existingValue = unwrapHandle(existingValue);

      if (matchedValue->getType()->isFloatingPointTy() &&
          existingValue->getType()->isFloatingPointTy()) {
        return m_Builder.CreateFCmpOEQ(matchedValue, existingValue,
                                       "pattern_eq");
      }
      if (matchedValue->getType() != existingValue->getType()) {
        if (matchedValue->getType()->isIntegerTy() &&
            existingValue->getType()->isIntegerTy()) {
          if (matchedValue->getType()->getIntegerBitWidth() >
              existingValue->getType()->getIntegerBitWidth()) {
            existingValue = m_Builder.CreateZExt(existingValue,
                                                  matchedValue->getType());
          } else {
            matchedValue = m_Builder.CreateZExt(matchedValue,
                                                 existingValue->getType());
          }
        } else if (matchedValue->getType()->isPointerTy() &&
                   existingValue->getType()->isPointerTy()) {
          existingValue = m_Builder.CreateBitCast(existingValue,
                                                   matchedValue->getType());
        } else {
          return m_Builder.getInt1(false);
        }
      }
      return m_Builder.CreateICmpEQ(matchedValue, existingValue,
                                    "pattern_eq");
    };

    for (const auto &arm : expr->Arms) {
      llvm::BasicBlock *armBB =
          llvm::BasicBlock::Create(m_Context, "match_arm", func);
      llvm::BasicBlock *nextArmBB =
          llvm::BasicBlock::Create(m_Context, "match_next", func);

      // 1. Check Pattern (Recursive helper for value/or patterns)
      std::function<llvm::Value *(const MatchArm::Pattern *, llvm::Value *, const std::string &)> genValuePatCond =
          [&](const MatchArm::Pattern *pat, llvm::Value *currVal, const std::string &currSemaType) -> llvm::Value * {
        llvm::Type *currTy = currVal->getType();
        if (pat->PatternKind == MatchArm::Pattern::Literal) {
          if (!pat->Name.empty() && pat->Name[0] == '"') {
            std::string rawLit = pat->Name.substr(1, pat->Name.size() - 2);
            auto strLit = std::make_unique<StringExpr>(rawLit);
            strLit->ResolvedType = lowerTypeSyntax(nullptr, "*char");
            PhysEntity litEnt = genExpr(strLit.get());
            llvm::Value *rawPtr = litEnt.load(m_Builder);

            std::string shapeName = "";
            if (currTy->isStructTy()) {
              auto *st = llvm::cast<llvm::StructType>(currTy);
              if (m_TypeToName.count(st)) {
                shapeName = m_TypeToName[st];
                size_t lt = shapeName.find('<');
                if (lt != std::string::npos) {
                  shapeName = shapeName.substr(0, lt);
                }
              }
            }

            if (currTy->isPointerTy()) {
              llvm::FunctionCallee strcmpFn = m_Module->getOrInsertFunction(
                  "strcmp",
                  llvm::FunctionType::get(m_Builder.getInt32Ty(), {m_Builder.getPtrTy(), m_Builder.getPtrTy()}, false)
              );
              llvm::Value *cmpRes = m_Builder.CreateCall(strcmpFn, {currVal, rawPtr});
              return m_Builder.CreateICmpEQ(cmpRes, m_Builder.getInt32(0));
            } else if (shapeName == "str" || shapeName == "String" || shapeName == "string") {
              llvm::Value *currBuf = m_Builder.CreateExtractValue(currVal, 0, "str_buf");
              llvm::Value *currLen = m_Builder.CreateExtractValue(currVal, 1, "str_len");

              size_t litSize = rawLit.size();
              llvm::Value *lenMatch = m_Builder.CreateICmpEQ(currLen, llvm::ConstantInt::get(currLen->getType(), litSize));

              if (litSize == 0) {
                return lenMatch;
              } else {
                // 1. Get current block, and create two new blocks
                llvm::BasicBlock *CurrentBB = m_Builder.GetInsertBlock();
                llvm::Function *TheFunction = CurrentBB->getParent();
                llvm::BasicBlock *MemcmpBB = llvm::BasicBlock::Create(m_Context, "str_memcmp", TheFunction);
                llvm::BasicBlock *MergeBB = llvm::BasicBlock::Create(m_Context, "str_match_merge", TheFunction);

                // 2. Only jump to memcmp if lengths match, otherwise jump to merge merge
                m_Builder.CreateCondBr(lenMatch, MemcmpBB, MergeBB);

                // 3. Construct Memcmp block
                m_Builder.SetInsertPoint(MemcmpBB);
                llvm::FunctionCallee memcmpFn = m_Module->getOrInsertFunction(
                    "memcmp",
                    llvm::FunctionType::get(m_Builder.getInt32Ty(), {m_Builder.getPtrTy(), m_Builder.getPtrTy(), getIntPtrTy()}, false)
                );
                llvm::Value *memcmpRes = m_Builder.CreateCall(memcmpFn, {
                    currBuf,
                    rawPtr,
                    llvm::ConstantInt::get(getIntPtrTy(), litSize)
                });
                llvm::Value *contentMatch = m_Builder.CreateICmpEQ(memcmpRes, m_Builder.getInt32(0));
                m_Builder.CreateBr(MergeBB);

                // 4. Construct Merge block, merge results using Phi node
                m_Builder.SetInsertPoint(MergeBB);
                llvm::PHINode *phiMatch = m_Builder.CreatePHI(m_Builder.getInt1Ty(), 2, "is_str_match");
                phiMatch->addIncoming(m_Builder.getInt1(false), CurrentBB); // Length mismatch, directly false
                phiMatch->addIncoming(contentMatch, MemcmpBB);              // Length match, take memcmp result

                return phiMatch;
              }
            }
            return m_Builder.getInt1(false);
          } else {
            if (currTy->isIntegerTy() || currTy->isPointerTy()) {
              llvm::Value *litVal = nullptr;
              if (pat->Name == "true") {
                litVal = llvm::ConstantInt::getTrue(m_Context);
              } else if (pat->Name == "false") {
                litVal = llvm::ConstantInt::getFalse(m_Context);
              } else if (!pat->Name.empty() && pat->Name[0] == '\'') {
                litVal = llvm::ConstantInt::get(currTy, pat->LiteralVal);
              } else {
                litVal = llvm::ConstantInt::get(currTy, pat->LiteralVal);
              }
              return m_Builder.CreateICmpEQ(currVal, litVal);
            }
          }
          return m_Builder.getInt1(false);
        } else if (pat->PatternKind == MatchArm::Pattern::Range) {
          llvm::Type* valTy = currVal->getType();

          // Extract left and right boundary literals precisely and materialize as LLVM constants
          uint64_t startVal = pat->SubPatterns[0]->LiteralVal;
          uint64_t endVal = pat->SubPatterns[1]->LiteralVal;
          llvm::Value* startLit = llvm::ConstantInt::get(valTy, startVal);
          llvm::Value* endLit = llvm::ConstantInt::get(valTy, endVal);

          // Dynamically determine physical sign attributes to avoid character range overflow
          bool isSigned = currTy->isIntegerTy() && 
                          (currSemaType.rfind("u", 0) != 0) && 
                          currSemaType != "Char16" && 
                          currSemaType != "char";

          // Emit combined range comparison instructions
          llvm::Value *ge = isSigned ? m_Builder.CreateICmpSGE(currVal, startLit) 
                                     : m_Builder.CreateICmpUGE(currVal, startLit);

          llvm::Value *leOrLt = pat->IsInclusive 
              ? (isSigned ? m_Builder.CreateICmpSLE(currVal, endLit) : m_Builder.CreateICmpULE(currVal, endLit))
              : (isSigned ? m_Builder.CreateICmpSLT(currVal, endLit) : m_Builder.CreateICmpULT(currVal, endLit));

          return m_Builder.CreateAnd(ge, leOrLt);
        } else if (pat->PatternKind == MatchArm::Pattern::Or) {
          llvm::Value *accum = m_Builder.getInt1(false);
          for (auto &sub : pat->SubPatterns) {
            accum = m_Builder.CreateOr(accum, genValuePatCond(sub.get(), currVal, currSemaType));
          }
          return accum;
        } else if (pat->PatternKind == MatchArm::Pattern::Variable) {
          // A no-payload enum variant such as `Flavor::Plain` is parsed as a
          // variable-shaped pattern.  At a nested enum payload it must still
          // test the variant tag; treating it as an unconditional binder makes
          // the first arm match every variant.
          std::string enumName;
          if (currTy->isStructTy()) {
            auto *st = llvm::cast<llvm::StructType>(currTy);
            enumName = m_TypeToName.count(st) ? m_TypeToName[st]
                                                 : st->getName().str();
          }
          if (enumName.empty())
            enumName = Type::stripMorphology(currSemaType);
          size_t lt = enumName.find('<');
          if (lt != std::string::npos)
            enumName = enumName.substr(0, lt);
          auto enumIt = m_Shapes.find(enumName);
          if (enumIt != m_Shapes.end() &&
              enumIt->second->Kind == ShapeKind::Enum) {
            std::string variantName = pat->Name;
            size_t scopePos = variantName.rfind("::");
            if (scopePos != std::string::npos)
              variantName = variantName.substr(scopePos + 2);
            const auto &members = enumIt->second->Members;
            for (size_t i = 0; i < members.size(); ++i) {
              const auto &member = members[i];
              if (member.Name != variantName || !member.Type.empty() ||
                  !member.SubMembers.empty())
                continue;
              llvm::Value *tagVal =
                  m_Builder.CreateExtractValue(currVal, 0, "tag");
              int tag = member.TagValue == -1 ? static_cast<int>(i)
                                               : member.TagValue;
              return m_Builder.CreateICmpEQ(
                  tagVal, llvm::ConstantInt::get(tagVal->getType(), tag));
            }
          }
          if (pat->Binding == MatchArm::Pattern::BindingOrigin::Existing) {
            return emitExistingBindingEquality(pat, currVal, currSemaType);
          }
          return m_Builder.getInt1(true);
        } else if (pat->PatternKind == MatchArm::Pattern::Wildcard ||
                   pat->PatternKind == MatchArm::Pattern::Elision) {
          return m_Builder.getInt1(true);
        } else if (pat->PatternKind == MatchArm::Pattern::Decons) {
          llvm::Value *accum = m_Builder.getInt1(true);

          std::string baseShapeName = "";
          llvm::StructType *st = nullptr;
          if (currVal->getType()->isStructTy()) {
            st = llvm::cast<llvm::StructType>(currVal->getType());
            if (m_TypeToName.count(st)) {
              baseShapeName = m_TypeToName[st];
              size_t lt = baseShapeName.find('<');
              if (lt != std::string::npos) {
                baseShapeName = baseShapeName.substr(0, lt);
              }
            } else {
              // Named LLVM structs preserve the source shape name even when
              // generic materialization gives them a distinct type instance.
              baseShapeName = st->getName().str();
            }
          }

          // A payload type materialized through a generic enum can use a
          // structurally equivalent LLVM type that is not present in the
          // LLVM-type-to-shape cache. The semantic type passed down by the
          // enclosing variant remains authoritative for pattern matching.
          if (baseShapeName.empty() && !currSemaType.empty()) {
            baseShapeName = Type::stripMorphology(currSemaType);
            size_t lt = baseShapeName.find('<');
            if (lt != std::string::npos)
              baseShapeName = baseShapeName.substr(0, lt);
          }

          bool isEnum = false;
          const ShapeDecl *sh = nullptr;
          if (!baseShapeName.empty() && m_Shapes.count(baseShapeName)) {
            sh = m_Shapes[baseShapeName];
            if (sh->Kind == ShapeKind::Enum) {
              isEnum = true;
            }
          }

          if (isEnum) {
            std::string patName = pat->Name;
            size_t scopePos = patName.rfind("::");
            if (scopePos != std::string::npos) {
              patName = patName.substr(scopePos + 2);
            }

            const ShapeMember *variant = nullptr;
            int variantTag = -1;
            for (size_t m = 0; m < sh->Members.size(); ++m) {
              if (sh->Members[m].Name == patName) {
                variant = &sh->Members[m];
                variantTag = (sh->Members[m].TagValue == -1) ? (int)m : (int)sh->Members[m].TagValue;
                break;
              }
            }

            if (!variant) return m_Builder.getInt1(false);

            llvm::Value *tagVal = m_Builder.CreateExtractValue(currVal, 0, "tag");
            llvm::Value *tagCond = m_Builder.CreateICmpEQ(tagVal, llvm::ConstantInt::get(tagVal->getType(), variantTag));
            accum = tagCond;

            if (!pat->SubPatterns.empty() && (!variant->SubMembers.empty() || !variant->Type.empty())) {
              llvm::Value *tempAddr = createEntryBlockAlloca(currVal->getType(), nullptr, "match_enum_temp");
              m_Builder.CreateStore(currVal, tempAddr);
              llvm::Value *payloadAddr = m_Builder.CreateStructGEP(currVal->getType(), tempAddr, 1);

              llvm::Type *payloadLayoutType = nullptr;
              std::vector<llvm::Type *> fieldTypes;
              if (!variant->SubMembers.empty()) {
                for (const auto &f : variant->SubMembers) {
                  if (f.ResolvedType) {
                    fieldTypes.push_back(getLLVMType(f.ResolvedType));
                  } else {
                    fieldTypes.push_back(resolveType(f.Type, false));
                  }
                }
                payloadLayoutType = llvm::StructType::get(m_Context, fieldTypes, false);
              } else if (!variant->Type.empty()) {
                if (variant->ResolvedType) {
                  payloadLayoutType = getLLVMType(variant->ResolvedType);
                } else {
                  payloadLayoutType = resolveType(variant->Type, false);
                }
              }

              if (payloadLayoutType) {
                llvm::Value *variantAddr = m_Builder.CreateBitCast(payloadAddr, llvm::PointerType::getUnqual(m_Context));

                size_t elisionIndex = -1;
                size_t elisionCount = 0;
                for (size_t i = 0; i < pat->SubPatterns.size(); ++i) {
                  if (pat->SubPatterns[i]->PatternKind == MatchArm::Pattern::Elision) {
                    elisionIndex = i;
                    elisionCount++;
                  }
                }

                size_t expectedSize = fieldTypes.empty() ? 1 : fieldTypes.size();
                size_t elidedCount = 0;
                if (elisionCount == 1) {
                  elidedCount = expectedSize - (pat->SubPatterns.size() - 1);
                }

                for (size_t i = 0; i < pat->SubPatterns.size(); ++i) {
                  if (pat->SubPatterns[i]->PatternKind == MatchArm::Pattern::Elision) {
                    continue;
                  }

                  size_t memberIndex = i;
                  if (elisionCount == 1) {
                    memberIndex = (i < elisionIndex) ? i : (i + elidedCount - 1);
                  }

                  llvm::Value *fieldAddr = variantAddr;
                  llvm::Type *fieldTy = payloadLayoutType;
                  if (!fieldTypes.empty()) {
                    fieldAddr = m_Builder.CreateStructGEP(payloadLayoutType, variantAddr, memberIndex);
                    fieldTy = fieldTypes[memberIndex];
                  }

                  llvm::Value *fieldVal = m_Builder.CreateLoad(fieldTy, fieldAddr, "subpat_val");
                  std::string fieldSemaType = "";
                  if (!variant->SubMembers.empty()) {
                    auto subTypeObj = variant->SubMembers[memberIndex].ResolvedType;
                    fieldSemaType = subTypeObj ? subTypeObj->toString() : variant->SubMembers[memberIndex].Type;
                  } else if (!variant->Type.empty()) {
                    auto subTypeObj = variant->ResolvedType;
                    fieldSemaType = subTypeObj ? subTypeObj->toString() : variant->Type;
                  }
                  llvm::Value *subCond = genValuePatCond(pat->SubPatterns[i].get(), fieldVal, fieldSemaType);
                  accum = m_Builder.CreateAnd(accum, subCond);
                }
              }
            }
            return accum;
          }

          bool isNamed = false;
          for (const auto& name : pat->SubPatternNames) {
            if (!name.empty() && name != "..") {
              isNamed = true;
              break;
            }
          }

          size_t elisionIndex = -1;
          size_t elisionCount = 0;
          for (size_t i = 0; i < pat->SubPatterns.size(); ++i) {
            if (pat->SubPatterns[i]->PatternKind == MatchArm::Pattern::Elision) {
              elisionIndex = i;
              elisionCount++;
            }
          }

          size_t expectedSize = 1;
          st = nullptr;
          if (currVal->getType()->isStructTy()) {
            st = llvm::cast<llvm::StructType>(currVal->getType());
            expectedSize = st->getNumElements();
          }

          size_t elidedCount = 0;
          if (elisionCount == 1) {
            elidedCount = expectedSize - (pat->SubPatterns.size() - 1);
          }

          for (size_t i = 0; i < pat->SubPatterns.size(); ++i) {
            if (pat->SubPatterns[i]->PatternKind == MatchArm::Pattern::Elision) {
              continue;
            }

            size_t memberIndex = -1;
            if (isNamed) {
              std::string shapeName = Type::stripMorphology(pat->Name);
              if (shapeName.empty() && st) {
                if (m_TypeToName.count(st)) {
                  shapeName = m_TypeToName[st];
                } else {
                  shapeName = st->getName().str();
                }
              }
              if (!shapeName.empty() && shapeName.front() == '(' && shapeName.back() == ')') {
                if (m_ParenthesizedRecordTypes.count(shapeName)) {
                  auto resolved = m_ParenthesizedRecordTypes[shapeName];
                  if (resolved && resolved->typeKind == Type::Shape) {
                    shapeName = std::static_pointer_cast<ShapeType>(resolved)->Name;
                  }
                }
              }
              if (m_Shapes.count(shapeName)) {
                const auto *sh = m_Shapes[shapeName];
                for (size_t m = 0; m < sh->Members.size(); ++m) {
                  if (sh->Members[m].Name ==
                      Type::stripMorphology(pat->SubPatternNames[i])) {
                    memberIndex = m;
                    break;
                  }
                }
              }
            } else {
              memberIndex = i;
              if (elisionCount == 1) {
                memberIndex = (i < elisionIndex) ? i : (i + elidedCount - 1);
              }
            }

            if (memberIndex >= expectedSize || memberIndex == (size_t)-1) {
              continue;
            }

            llvm::Value *memberVal = nullptr;
            if (st) {
              memberVal = m_Builder.CreateExtractValue(currVal, memberIndex);
            } else {
              memberVal = currVal;
            }

            std::string memberSemaType = "";
            if (st && m_Shapes.count(baseShapeName)) {
              const auto *sh = m_Shapes[baseShapeName];
              if (memberIndex < sh->Members.size()) {
                auto subTypeObj = sh->Members[memberIndex].ResolvedType;
                memberSemaType = subTypeObj ? subTypeObj->toString() : sh->Members[memberIndex].Type;
              }
            }
            llvm::Value *subCond = genValuePatCond(pat->SubPatterns[i].get(), memberVal, memberSemaType);
            accum = m_Builder.CreateAnd(accum, subCond);
          }
          return accum;
        }
        return m_Builder.getInt1(false);
      };

      llvm::Value *cond = genValuePatCond(arm->Pat.get(), targetVal, targetSemaType);

      // 2. Branch to guard-check, arm or next
      if (arm->Guard) {
        llvm::BasicBlock *guardBB =
            llvm::BasicBlock::Create(m_Context, "match_guard", func);
        m_Builder.CreateCondBr(cond, guardBB, nextArmBB);
        m_Builder.SetInsertPoint(guardBB);

        m_ScopeStack.push_back({});
        genPatternBinding(arm->Pat.get(), targetAddr, targetType, expr->Target->ResolvedType);

        PhysEntity guardVal_ent = genExpr(arm->Guard.get()).load(m_Builder);
        llvm::Value *guardVal = guardVal_ent.load(m_Builder);

        executeScopeUnwinding(m_ScopeStack.size() - 1);
        m_ScopeStack.pop_back(); // Clean up guard scope
        m_Builder.CreateCondBr(guardVal, armBB, nextArmBB);
      } else {
        m_Builder.CreateCondBr(cond, armBB, nextArmBB);
      }

      // 3. ARM Body
      m_Builder.SetInsertPoint(armBB);
      m_ScopeStack.push_back({});
      genPatternBinding(arm->Pat.get(), targetAddr, targetType, expr->Target->ResolvedType);

      m_CFStack.push_back(
          {"", mergeBB, nullptr, resultAddr, m_ScopeStack.size()});
      genStmt(arm->Body.get());
      m_CFStack.pop_back();

      executeScopeUnwinding(m_ScopeStack.size() - 1);
      m_ScopeStack.pop_back();
      if (m_Builder.GetInsertBlock() &&
          !m_Builder.GetInsertBlock()->getTerminator())
        m_Builder.CreateBr(mergeBB);

      m_Builder.SetInsertPoint(nextArmBB);
    }
    m_Builder.CreateBr(mergeBB);
  }

  // Check if mergeBB is reachable (i.e. has predecessors)
  // If use_empty() is true, it means no branches jump to it, so it's dead
  // code.
  if (mergeBB->use_empty()) {
    mergeBB->eraseFromParent(); // Remove dead block
    
    // Compile-time only: pop the match scope
    m_ScopeStack.pop_back();

    // Return dummy value since we can't be here at runtime
    return llvm::UndefValue::get(resultType);
  }

  m_Builder.SetInsertPoint(mergeBB);
  
  // Clean up match-level temporary scope (unwind temporaries generated by match target scrutinee)
  executeScopeUnwinding(m_ScopeStack.size() - 1);
  m_ScopeStack.pop_back();

  if (!resultAddr) {
    if (expr->ResolvedType && expr->ResolvedType->isUnit()) {
      return PhysEntity(llvm::Constant::getNullValue(resultType), "()",
                        resultType, false);
    }
    // No result
    return PhysEntity(nullptr, "", llvm::Type::getVoidTy(m_Context), false);
  }
  return PhysEntity(
      m_Builder.CreateLoad(resultType, resultAddr, "match_result"), "",
      resultType, false);
}

PhysEntity CodeGen::genIfExpr(const IfExpr *ie,
                              llvm::AllocaInst *inheritedResultAddr) {
  llvm::AllocaInst *resultAddr = inheritedResultAddr;
  llvm::Type *resTy = nullptr;
  if (resultAddr) {
    resTy = resultAddr->getAllocatedType();
  } else if (ie->ResolvedType && !ie->ResolvedType->isVoid() &&
             !ie->ResolvedType->isUnit()) {
    resTy = getLLVMType(ie->ResolvedType);
    resultAddr = createEntryBlockAlloca(resTy, nullptr, "if_result_addr");
    m_Builder.CreateStore(llvm::Constant::getNullValue(resTy), resultAddr);
  }

  if (ie->IsComptime) {
      llvm::Function *f = m_Builder.GetInsertBlock()->getParent();
      llvm::BasicBlock *mergeBB = llvm::BasicBlock::Create(m_Context, "comptime_ifcont");
      
      if (ie->ComptimeTaken) {
         if (ie->Then) {
             m_CFStack.push_back({"", mergeBB, nullptr, resultAddr, m_ScopeStack.size()});
             genStmt(ie->Then.get());
             m_CFStack.pop_back();
         }
      } else if (ie->Else) {
          m_CFStack.push_back({"", mergeBB, nullptr, resultAddr, m_ScopeStack.size()});
          genStmt(ie->Else.get());
          m_CFStack.pop_back();
      }
      
      if (m_Builder.GetInsertBlock() && !m_Builder.GetInsertBlock()->getTerminator()) {
          m_Builder.CreateBr(mergeBB);
      }
      
      if (mergeBB->use_empty()) {
          delete mergeBB;
          llvm::BasicBlock *deadBB = llvm::BasicBlock::Create(m_Context, "comptime.dead", f);
          m_Builder.SetInsertPoint(deadBB);
      } else {
          mergeBB->insertInto(f);
          m_Builder.SetInsertPoint(mergeBB);
      }
      if (resultAddr) {
        return m_Builder.CreateLoad(resTy, resultAddr, "if_result");
      }
      if (ie->ResolvedType && ie->ResolvedType->isUnit()) {
        llvm::Type *unitTy = getLLVMType(ie->ResolvedType);
        return PhysEntity(llvm::Constant::getNullValue(unitTy), "()", unitTy,
                          false);
      }
      return PhysEntity();
  }

  // Track result via alloca if this if yields a value (determined by
  // PassExpr)
  // Initialize with 0 or some default

  PhysEntity cond_ent = genExpr(ie->Condition.get()).load(m_Builder);
  llvm::Value *cond = cond_ent.load(m_Builder);
  if (!cond)
    return nullptr;
  if (!cond->getType()->isIntegerTy(1)) {
    cond = m_Builder.CreateICmpNE(
        cond, llvm::ConstantInt::get(cond->getType(), 0), "ifcond");
  }

  llvm::Function *f = m_Builder.GetInsertBlock()->getParent();
  llvm::BasicBlock *thenBB = llvm::BasicBlock::Create(m_Context, "then", f);
  llvm::BasicBlock *elseBB = llvm::BasicBlock::Create(m_Context, "else");
  llvm::BasicBlock *mergeBB = llvm::BasicBlock::Create(m_Context, "ifcont");

  m_Builder.CreateCondBr(cond, thenBB, elseBB);

  m_Builder.SetInsertPoint(thenBB);
  m_CFStack.push_back({"", mergeBB, nullptr, resultAddr, m_ScopeStack.size()});
  genStmt(ie->Then.get());
  m_CFStack.pop_back();
  llvm::BasicBlock *thenEndBB = m_Builder.GetInsertBlock();
  if (thenEndBB && !thenEndBB->getTerminator())
    m_Builder.CreateBr(mergeBB);

  elseBB->insertInto(f);
  m_Builder.SetInsertPoint(elseBB);
  if (ie->Else) {
    m_CFStack.push_back(
        {"", mergeBB, nullptr, resultAddr, m_ScopeStack.size()});
    // An `else if` is parsed as an expression statement containing a nested
    // IfExpr.  Give it this if-expression's result slot so each nested
    // `pass` initializes the original value-producing expression.
    if (auto *elseExpr = dynamic_cast<const ExprStmt *>(ie->Else.get())) {
      if (auto *nestedIf =
              dynamic_cast<const IfExpr *>(elseExpr->Expression.get())) {
        genIfExpr(nestedIf, resultAddr);
      } else {
        genStmt(ie->Else.get());
      }
    } else {
      genStmt(ie->Else.get());
    }
    m_CFStack.pop_back();
  }
  llvm::BasicBlock *elseEndBB = m_Builder.GetInsertBlock();
  if (elseEndBB && !elseEndBB->getTerminator())
    m_Builder.CreateBr(mergeBB);

  mergeBB->insertInto(f);
  m_Builder.SetInsertPoint(mergeBB);
  if (resultAddr) {
    return m_Builder.CreateLoad(resTy, resultAddr, "if_result");
  }
  if (ie->ResolvedType && ie->ResolvedType->isUnit()) {
    llvm::Type *unitTy = getLLVMType(ie->ResolvedType);
    return PhysEntity(llvm::Constant::getNullValue(unitTy), "()", unitTy,
                      false);
  }
  return PhysEntity();
}

PhysEntity CodeGen::genGuardExpr(const GuardExpr *guard) {
  llvm::AllocaInst *resultAddr = nullptr;
  llvm::Type *resTy = nullptr;
  if (guard->ResolvedType && !guard->ResolvedType->isVoid() &&
      !guard->ResolvedType->isUnit()) {
    resTy = getLLVMType(guard->ResolvedType);
    resultAddr = createEntryBlockAlloca(resTy, nullptr, "guard_result_addr");
    m_Builder.CreateStore(llvm::Constant::getNullValue(resTy), resultAddr);
  }

  llvm::Value *condVal = nullptr;
  llvm::Value *deepGuardHandle = nullptr;
  llvm::StructType *deepGuardSoulType = nullptr;

  // A bare guard on a nullable handle normally denotes payload-path
  // availability.  Do not materialize that payload before testing the handle:
  // getEntityAddr() dereferences unique/raw handles and would otherwise load
  // through null.  When the soul is nullable, the second branch below also
  // checks its presence bit.
  if (auto *var = dynamic_cast<const VariableExpr *>(guard->Condition.get())) {
    std::string baseName = Type::stripMorphology(var->Name);
    auto symbol = m_Symbols.find(baseName);
    if (symbol != m_Symbols.end()) {
      TokaSymbol &sym = symbol->second;
      std::shared_ptr<Type> declaredType = sym.soulTypeObj;
      std::shared_ptr<Type> soulType =
          declaredType ? declaredType->getSoulType() : nullptr;
      bool checksNullableHandle = declaredType && declaredType->IsNullable;
      bool checksNullableSoul = soulType && soulType->IsNullable;
      if ((sym.morphology == Morphology::Unique ||
           sym.morphology == Morphology::Raw ||
           sym.morphology == Morphology::Shared) &&
          (checksNullableHandle || checksNullableSoul)) {
        llvm::Value *identityAddr = emitHandleAddr(var);
        if (identityAddr) {
          if (sym.morphology == Morphology::Shared) {
            llvm::StructType *handleType = llvm::StructType::get(
                m_Context, {m_Builder.getPtrTy(), m_Builder.getPtrTy()});
            llvm::Value *handle = m_Builder.CreateLoad(
                handleType, identityAddr, "guard.shared_handle.load");
            condVal = m_Builder.CreateExtractValue(
                handle, 0, "guard.shared_handle.data");
          } else {
            condVal = m_Builder.CreateLoad(m_Builder.getPtrTy(), identityAddr,
                                           "guard.handle.load");
          }
          if (checksNullableSoul && sym.soulType &&
              sym.soulType->isStructTy()) {
            deepGuardHandle = condVal;
            deepGuardSoulType = llvm::cast<llvm::StructType>(sym.soulType);
          }
        }
      }
    }
  }

  if (auto *unary = dynamic_cast<const UnaryExpr *>(guard->Condition.get())) {
    if (unary->Op == TokenType::Caret || unary->Op == TokenType::Star ||
        unary->Op == TokenType::Tilde || unary->Op == TokenType::Ampersand) {
      llvm::Value *identityAddr = emitHandleAddr(unary->RHS.get());
      if (identityAddr) {
        llvm::Type *handleTy = nullptr;
        if (unary->RHS->ResolvedType)
            handleTy = getLLVMType(unary->RHS->ResolvedType);
        else
            handleTy = m_Builder.getPtrTy();
            
        if (auto *v = dynamic_cast<const VariableExpr *>(unary->RHS.get())) {
            std::string baseName = v->Name;
            while (!baseName.empty() && (baseName[0] == '*' || baseName[0] == '^' ||
                                        baseName[0] == '~' || baseName[0] == '&'))
                baseName = baseName.substr(1);
            if (m_Symbols.count(baseName)) {
                TokaSymbol &sym = m_Symbols[baseName];
                if (sym.morphology == Morphology::Shared) {
                    llvm::Type *ptrTy = llvm::PointerType::getUnqual(m_Context);
                    llvm::Type *refTy =
                        llvm::PointerType::getUnqual(m_Context);
                    handleTy = llvm::StructType::get(m_Context, {ptrTy, refTy});
                } else if (sym.morphology == Morphology::Unique ||
                        sym.morphology == Morphology::Raw) {
                    handleTy = m_Builder.getPtrTy();
                }
            }
        }
        condVal = m_Builder.CreateLoad(handleTy, identityAddr, "guard.direct.load");
      }
    }
  }

  if (!condVal) {
    PhysEntity cond_ent = genExpr(guard->Condition.get()).load(m_Builder);
    condVal = cond_ent.load(m_Builder);
  }

  if (!condVal)
    return nullptr;

  llvm::Value *condBool = nullptr;
  bool isNullableSoul = (guard->Condition->ResolvedType && guard->Condition->ResolvedType->IsNullable && !guard->Condition->ResolvedType->isPointer());

  // A nullable payload behind a handle remains in its owning wrapper.  The
  // guard above has already proven that wrapper present; shadowing it as a
  // stack value would both extract from a pointer and destroy the handle
  // representation needed by `cede ^binding` in the then-branch.  Only a
  // direct nullable value uses the temporary unwrapped symbol.
  if (isNullableSoul && !deepGuardHandle) {
    llvm::Value *present =
        m_Builder.CreateExtractValue(condVal, 1, "guard_payload_present");
    condBool = m_Builder.CreateICmpNE(
        present, llvm::ConstantInt::get(present->getType(), 0),
        "guard_payload_available");
  } else if (condVal->getType()->isPointerTy()) {
    condBool = m_Builder.CreateIsNotNull(condVal, "guard_not_null");
  } else if (condVal->getType()->isStructTy() && condVal->getType()->getStructNumElements() == 2) {
    llvm::Value *dataPtr = m_Builder.CreateExtractValue(condVal, 0, "guard_sh_ptr");
    condBool = m_Builder.CreateIsNotNull(dataPtr, "guard_sh_not_null");
  } else if (condVal->getType()->isStructTy() && condVal->getType()->getStructNumElements() == 1) {
    llvm::Value *inner = m_Builder.CreateExtractValue(condVal, 0);
    if (inner->getType()->isPointerTy()) {
      condBool = m_Builder.CreateIsNotNull(inner, "guard_not_null");
    } else {
      condBool = m_Builder.CreateICmpNE(inner, llvm::ConstantInt::get(inner->getType(), 0));
    }
  } else if (condVal->getType()->isIntegerTy()) {
    condBool = m_Builder.CreateICmpNE(condVal, llvm::ConstantInt::get(condVal->getType(), 0));
  } else {
    condBool = m_Builder.CreateICmpNE(condVal, llvm::ConstantInt::get(condVal->getType(), 0));
  }

  llvm::Function *f = m_Builder.GetInsertBlock()->getParent();
  llvm::BasicBlock *thenBB = llvm::BasicBlock::Create(m_Context, "guard_then", f);
  llvm::BasicBlock *elseBB = llvm::BasicBlock::Create(m_Context, "guard_else");
  llvm::BasicBlock *mergeBB = llvm::BasicBlock::Create(m_Context, "guard_cont");

  if (deepGuardHandle && deepGuardSoulType) {
    llvm::BasicBlock *payloadCheckBB =
        llvm::BasicBlock::Create(m_Context, "guard_payload_check", f);
    m_Builder.CreateCondBr(condBool, payloadCheckBB, elseBB);
    m_Builder.SetInsertPoint(payloadCheckBB);
    llvm::Value *payload = m_Builder.CreateLoad(
        deepGuardSoulType, deepGuardHandle, "guard_payload.load");
    llvm::Value *present =
        m_Builder.CreateExtractValue(payload, 1, "guard_payload.present");
    m_Builder.CreateCondBr(present, thenBB, elseBB);
  } else {
    m_Builder.CreateCondBr(condBool, thenBB, elseBB);
  }

  m_Builder.SetInsertPoint(thenBB);
  m_CFStack.push_back({"", mergeBB, nullptr, resultAddr, m_ScopeStack.size()});

  TokaSymbol oldSym;
  bool shadowed = false;
  std::string baseName;
  if (isNullableSoul && !deepGuardHandle) {
      if (auto *v = dynamic_cast<const VariableExpr *>(guard->Condition.get())) {
          baseName = v->Name;
          while (!baseName.empty() && (baseName[0] == '*' || baseName[0] == '#' || baseName[0] == '&' || baseName[0] == '^' || baseName[0] == '~' || baseName[0] == '!')) baseName = baseName.substr(1);
          while (!baseName.empty() && (baseName.back() == '#' || baseName.back() == '?' || baseName.back() == '!')) baseName.pop_back();

          if (m_Symbols.count(baseName)) {
              oldSym = m_Symbols[baseName];
              shadowed = true;
              llvm::Value *payload = m_Builder.CreateExtractValue(condVal, 0, "guard_payload");
              llvm::AllocaInst *newAlloca = createEntryBlockAlloca(payload->getType(), nullptr, baseName + "_guard");
              m_Builder.CreateStore(payload, newAlloca);
              TokaSymbol newSym = oldSym;
              newSym.allocaPtr = newAlloca;
              newSym.soulType = payload->getType();
              newSym.mode = AddressingMode::Direct;
              newSym.indirectionLevel = 0;
              m_Symbols[baseName] = newSym;
          }
      }
  }

  genStmt(guard->Then.get());
  m_CFStack.pop_back();
  llvm::BasicBlock *thenEndBB = m_Builder.GetInsertBlock();
  if (thenEndBB && !thenEndBB->getTerminator())
    m_Builder.CreateBr(mergeBB);

  if (shadowed) {
      m_Symbols[baseName] = oldSym;
  }

  elseBB->insertInto(f);
  m_Builder.SetInsertPoint(elseBB);
  if (guard->Else) {
    m_CFStack.push_back({"", mergeBB, nullptr, resultAddr, m_ScopeStack.size()});
    genStmt(guard->Else.get());
    m_CFStack.pop_back();
  }
  llvm::BasicBlock *elseEndBB = m_Builder.GetInsertBlock();
  if (elseEndBB && !elseEndBB->getTerminator())
    m_Builder.CreateBr(mergeBB);

  mergeBB->insertInto(f);
  m_Builder.SetInsertPoint(mergeBB);
  if (resultAddr) {
    return m_Builder.CreateLoad(resTy, resultAddr, "guard_result");
  }
  if (guard->ResolvedType && guard->ResolvedType->isUnit()) {
    llvm::Type *unitTy = getLLVMType(guard->ResolvedType);
    return PhysEntity(llvm::Constant::getNullValue(unitTy), "()", unitTy,
                      false);
  }
  return PhysEntity();
}

PhysEntity CodeGen::genLoopExpr(const LoopExpr *le) {
  llvm::Function *f = m_Builder.GetInsertBlock()->getParent();
  
  const bool isUnitResult = le->ResolvedType && le->ResolvedType->isUnit();
  llvm::AllocaInst *resultAddr = nullptr;
  if (!isUnitResult) {
    resultAddr = createEntryBlockAlloca(m_Builder.getInt32Ty(), nullptr,
                                        "loop_result_addr");
    m_Builder.CreateStore(m_Builder.getInt32(0), resultAddr);
  }

  llvm::BasicBlock *afterBB = llvm::BasicBlock::Create(m_Context, "loop_after");

  std::string myLabel = "";
  if (!m_CFStack.empty() && m_CFStack.back().BreakTarget == nullptr)
    myLabel = m_CFStack.back().Label;

  if (le->Condition) {
    llvm::BasicBlock *condBB =
        llvm::BasicBlock::Create(m_Context, "loop_cond", f);
    llvm::BasicBlock *loopBB = llvm::BasicBlock::Create(m_Context, "loop");

    m_Builder.CreateBr(condBB);
    m_Builder.SetInsertPoint(condBB);

    PhysEntity cond_ent = genExpr(le->Condition.get()).load(m_Builder);
    llvm::Value *cond = cond_ent.load(m_Builder);
    m_Builder.CreateCondBr(cond, loopBB, afterBB);

    loopBB->insertInto(f);
    m_Builder.SetInsertPoint(loopBB);

    m_CFStack.push_back(
        {myLabel, afterBB, condBB, resultAddr, m_ScopeStack.size()});
    genStmt(le->Body.get());
    m_CFStack.pop_back();
    if (m_Builder.GetInsertBlock() &&
        !m_Builder.GetInsertBlock()->getTerminator())
      m_Builder.CreateBr(condBB);
  } else {
    llvm::BasicBlock *loopBB = llvm::BasicBlock::Create(m_Context, "loop", f);

    m_Builder.CreateBr(loopBB);
    m_Builder.SetInsertPoint(loopBB);

    m_CFStack.push_back(
        {myLabel, afterBB, loopBB, resultAddr, m_ScopeStack.size()});
    genStmt(le->Body.get());
    m_CFStack.pop_back();
    if (m_Builder.GetInsertBlock() &&
        !m_Builder.GetInsertBlock()->getTerminator())
      m_Builder.CreateBr(loopBB);
  }

  afterBB->insertInto(f);
  m_Builder.SetInsertPoint(afterBB);
  if (isUnitResult) {
    llvm::Type *unitTy = getLLVMType(le->ResolvedType);
    return PhysEntity(llvm::Constant::getNullValue(unitTy), "()", unitTy,
                      false);
  }
  return m_Builder.CreateLoad(m_Builder.getInt32Ty(), resultAddr,
                              "loop_result");
}

PhysEntity CodeGen::genForExpr(const ForExpr *fe) {
  if (fe->IsComptimeUnrolled) {
    for (const auto &body : fe->UnrolledBodies) {
        m_ScopeStack.push_back({});
        genStmt(body.get());
        m_ScopeStack.pop_back();
    }
    if (fe->ResolvedType && fe->ResolvedType->isUnit()) {
      llvm::Type *unitTy = getLLVMType(fe->ResolvedType);
      return PhysEntity(llvm::Constant::getNullValue(unitTy), "()", unitTy,
                        false);
    }
    return PhysEntity(llvm::Constant::getNullValue(m_Builder.getInt32Ty()),
                      "void", m_Builder.getInt32Ty(), false);
  }

  PhysEntity collSourceEnt = genExpr(fe->Collection.get());
  PhysEntity collVal_ent = collSourceEnt.load(m_Builder);
  llvm::Value *collVal = collVal_ent.load(m_Builder);
  if (!collVal)
    return nullptr;

  // Keep the original array storage for reference iteration.  Loading an
  // array into an rvalue and then taking an element address would otherwise
  // bind `&value` to a per-iteration staging copy.
  llvm::Value *arraySourceAddr = nullptr;
  if (collSourceEnt.isAddress && collSourceEnt.irType &&
      collSourceEnt.irType->isArrayTy()) {
    arraySourceAddr = collSourceEnt.value;
  }

  llvm::Function *f = m_Builder.GetInsertBlock()->getParent();

  // array/pointer checks are deferred to iterator dispatch

  llvm::BasicBlock *condBB = llvm::BasicBlock::Create(m_Context, "for_cond", f);
  llvm::BasicBlock *loopBB = llvm::BasicBlock::Create(m_Context, "for_loop");
  llvm::BasicBlock *incrBB = llvm::BasicBlock::Create(m_Context, "for_incr");
  llvm::BasicBlock *elseBB = llvm::BasicBlock::Create(m_Context, "for_else");
  llvm::BasicBlock *afterBB = llvm::BasicBlock::Create(m_Context, "for_after");

  const bool isUnitResult = fe->ResolvedType && fe->ResolvedType->isUnit();
  llvm::AllocaInst *resultAddr = nullptr;
  if (!isUnitResult) {
    resultAddr = createEntryBlockAlloca(m_Builder.getInt32Ty(), nullptr,
                                        "for_result_addr");
    m_Builder.CreateStore(m_Builder.getInt32(0), resultAddr);
  }

  // Loop index
  llvm::AllocaInst *idxAlloca = createEntryBlockAlloca(
      llvm::Type::getInt32Ty(m_Context), nullptr, "for_idx");
  m_Builder.CreateStore(
      llvm::ConstantInt::get(llvm::Type::getInt32Ty(m_Context), 0), idxAlloca);


  bool isArray = false;
  std::string collTypeNameCheck = collVal_ent.typeName;
  if (collTypeNameCheck.size() > 0 && collTypeNameCheck[0] == '[') isArray = true;
  if (collVal->getType()->isArrayTy()) isArray = true;

  llvm::AllocaInst *iterAlloca = nullptr;
  llvm::Value *iterVal = nullptr;
  if (!isArray) {
    std::string collObjTy = collVal_ent.typeName;
    if (collObjTy.empty() && m_TypeToName.count(collVal->getType())) {
        collObjTy = m_TypeToName[collVal->getType()];
    }
    std::string stripName = toka::Type::stripMorphology(collObjTy);
    std::string iterMangled =
        fe->ResolvedIterFn && !fe->ResolvedIterFn->CodegenName.empty()
            ? fe->ResolvedIterFn->CodegenName
            : "Encap_" + stripName + "_iter";
    llvm::Function *iterFn = m_Module->getFunction(iterMangled);
    if (!iterFn) {
        iterMangled = stripName + "_iter"; // Fallback without Encap_ prefix
        iterFn = m_Module->getFunction(iterMangled);
    }
    if (!iterFn) {
        error(fe, DiagID::ERR_CODEGEN_ITERATOR_SETUP_FAILED_FUNCTION_NOT_FOU, iterMangled, stripName);
        return nullptr;
    }

    bool isSRet = false;
    llvm::Type *sretTy = nullptr;
    if (iterFn && iterFn->arg_size() > 0 && iterFn->hasParamAttribute(0, llvm::Attribute::StructRet)) {
        isSRet = true;
        sretTy = iterFn->getParamAttribute(0, llvm::Attribute::StructRet).getValueAsType();
    }

    if (isSRet) {
        llvm::Value *sretAlloc = createEntryBlockAlloca(sretTy, nullptr, "iter_state");
        std::vector<llvm::Value *> args;
        args.push_back(sretAlloc);

        if (collVal_ent.typeName[0] != '*' && collVal_ent.typeName[0] != '&' && collVal_ent.typeName[0] != '^' && collVal_ent.typeName[0] != '~') {
           if (iterFn && iterFn->arg_size() > 1 && iterFn->getArg(1)->getType()->isPointerTy()) {
               llvm::AllocaInst *tmp = createEntryBlockAlloca(collVal->getType());
               m_Builder.CreateStore(collVal, tmp);
               args.push_back(tmp);
           } else {
               args.push_back(collVal);
           }
        } else {
           args.push_back(collVal);
        }

        llvm::CallInst *ci = m_Builder.CreateCall(iterFn, args);
        ci->addParamAttr(0, llvm::Attribute::get(m_Context, llvm::Attribute::StructRet, sretTy));
        iterAlloca = llvm::cast<llvm::AllocaInst>(sretAlloc);
    } else {
        if (collVal_ent.typeName[0] != '*' && collVal_ent.typeName[0] != '&' && collVal_ent.typeName[0] != '^' && collVal_ent.typeName[0] != '~') {
           if (iterFn && iterFn->arg_size() > 0 && iterFn->getArg(0)->getType()->isPointerTy()) {
               llvm::AllocaInst *tmp = createEntryBlockAlloca(collVal->getType());
               m_Builder.CreateStore(collVal, tmp);
               iterVal = m_Builder.CreateCall(iterFn, {tmp}, "iter_inst");
           } else {
               iterVal = m_Builder.CreateCall(iterFn, {collVal}, "iter_inst");
           }
        } else {
           iterVal = m_Builder.CreateCall(iterFn, {collVal}, "iter_inst");
        }

        iterAlloca = createEntryBlockAlloca(iterVal->getType(), nullptr, "iter_state");
        m_Builder.CreateStore(iterVal, iterAlloca);
    }
  }

  // Define loop variable scope (moved here to allow optAlloca to bind correctly if needed, though alloca is block-level)
  m_ScopeStack.push_back({});
  size_t iteratorScopeDepth = m_ScopeStack.size() - 1;
  if (!isArray && iterAlloca) {
    std::string iteratorType = fe->IteratorType;
    if (iteratorType.empty() &&
        m_TypeToName.count(iterAlloca->getAllocatedType())) {
      iteratorType = m_TypeToName[iterAlloca->getAllocatedType()];
    }
    std::string dropName = "Encap_" + iteratorType + "_drop";
    llvm::Function *dropFn = m_Module->getFunction(dropName);
    VariableScopeInfo iteratorInfo;
    iteratorInfo.Name = "__for_iterator";
    iteratorInfo.Alloca = iterAlloca;
    iteratorInfo.AllocType = iterAlloca->getAllocatedType();
    iteratorInfo.IsUniquePointer = false;
    iteratorInfo.IsShared = false;
    iteratorInfo.HasDrop = dropFn != nullptr;
    iteratorInfo.DropFunc = dropFn ? dropName : "";
    iteratorInfo.SoulName = iteratorType;
    m_ScopeStack.back().push_back(iteratorInfo);
  }


  m_Builder.CreateBr(condBB);
  m_Builder.SetInsertPoint(condBB);

  llvm::Value *currIdx = m_Builder.CreateLoad(llvm::Type::getInt32Ty(m_Context),
                                              idxAlloca, "curr_idx");

  llvm::AllocaInst *optAlloca = nullptr; // For iterators

  if (isArray) {
      llvm::Value *limit = nullptr;
      // 1. Array Size Detection via Semantic Type
      std::string typeStr = collVal_ent.typeName;
      bool foundSize = false;
      if (typeStr.size() > 1 && typeStr[0] == '[') {
        size_t lastSemi = typeStr.find_last_of(';');
        if (lastSemi != std::string::npos) {
          std::string countStr =
              typeStr.substr(lastSemi + 1, typeStr.size() - lastSemi - 2);
          try {
            uint64_t n = std::stoull(countStr);
            limit = llvm::ConstantInt::get(llvm::Type::getInt32Ty(m_Context), n);
            foundSize = true;
          } catch (...) {
          }
        }
      }

      if (!foundSize) {
        if (collVal->getType()->isArrayTy()) {
          limit = llvm::ConstantInt::get(llvm::Type::getInt32Ty(m_Context),
                                         collVal->getType()->getArrayNumElements());
        } else {
          // Fallback to hardcoded 10 for pointers if unknown
          limit = llvm::ConstantInt::get(llvm::Type::getInt32Ty(m_Context), 10);
        }
      }

      llvm::Value *cond = m_Builder.CreateICmpULT(currIdx, limit, "forcond");
      m_Builder.CreateCondBr(cond, loopBB, elseBB);
  } else {
    std::string iterTyName = "";
    if (iterAlloca && m_TypeToName.count(iterAlloca->getAllocatedType())) {
        iterTyName = m_TypeToName[iterAlloca->getAllocatedType()];
    }
    std::string nextMethodName = fe->IsReference ? "next_ref" : "next";
    std::string nextFnName =
        fe->ResolvedNextFn && !fe->ResolvedNextFn->CodegenName.empty()
            ? fe->ResolvedNextFn->CodegenName
            : "Encap_" + iterTyName + "_" + nextMethodName;
    llvm::Function *nextFn = m_Module->getFunction(nextFnName);
    if (!nextFn) {
        nextFnName = iterTyName + "_" + nextMethodName; // Fallback
        nextFn = m_Module->getFunction(nextFnName);
    }
    if (!nextFn) {
        error(fe, DiagID::ERR_CODEGEN_ITERATOR_PROTOCOL_FAILED_NEXT_FUNCTION, nextFnName, iterTyName);
        return nullptr;
    }

    bool isNextSRet = false;
    llvm::Type *nextSRetTy = nullptr;
    if (nextFn && nextFn->arg_size() > 0 && nextFn->hasParamAttribute(0, llvm::Attribute::StructRet)) {
        isNextSRet = true;
        nextSRetTy = nextFn->getParamAttribute(0, llvm::Attribute::StructRet).getValueAsType();
    }

    llvm::Type *optTy = nullptr;
    if (isNextSRet) {
        optAlloca = createEntryBlockAlloca(nextSRetTy, nullptr, "opt_struct");
        llvm::CallInst *optResCall = nullptr;
        if (nextFn && nextFn->arg_size() > 1 && nextFn->getArg(1)->getType()->isPointerTy()) {
            optResCall = m_Builder.CreateCall(nextFn, {optAlloca, iterAlloca});
        } else {
            llvm::Value *loadedIter = m_Builder.CreateLoad(iterAlloca->getAllocatedType(), iterAlloca);
            optResCall = m_Builder.CreateCall(nextFn, {optAlloca, loadedIter});
        }
        optResCall->addParamAttr(0, llvm::Attribute::get(m_Context, llvm::Attribute::StructRet, nextSRetTy));
        optTy = nextSRetTy;
    } else {
        llvm::Value *optRes = nullptr;
        if (nextFn && nextFn->arg_size() > 0 && nextFn->getArg(0)->getType()->isPointerTy()) {
            optRes = m_Builder.CreateCall(nextFn, {iterAlloca}, "next_opt");
        } else {
            llvm::Value *loadedIter = m_Builder.CreateLoad(iterAlloca->getAllocatedType(), iterAlloca);
            optRes = m_Builder.CreateCall(nextFn, {loadedIter}, "next_opt");
        }

        optAlloca = createEntryBlockAlloca(optRes->getType(), nullptr, "opt_struct");
        m_Builder.CreateStore(optRes, optAlloca);
        optTy = optRes->getType();
    }

    llvm::Value *tagPtr = m_Builder.CreateStructGEP(optTy, optAlloca, 0);
    llvm::Value *tagVal = m_Builder.CreateLoad(m_Builder.getInt8Ty(), tagPtr);
    llvm::Value *isSome = m_Builder.CreateICmpEQ(tagVal, m_Builder.getInt8(1), "is_some");
    m_Builder.CreateCondBr(isSome, loopBB, elseBB);
  }

  loopBB->insertInto(f);
  m_Builder.SetInsertPoint(loopBB);

  // Define loop variable scope
  m_ScopeStack.push_back({});

  // 2. Determine Element Type
  llvm::Type *elemTy = nullptr;
  if (collVal->getType()->isArrayTy()) {
    elemTy = collVal->getType()->getArrayElementType();
  } else if (collVal->getType()->isPointerTy()) {
    // Use semantic type to resolve element type because of opaque pointers
    std::string collTypeName = collVal_ent.typeName;
    if (collTypeName.size() > 1) {
      if (collTypeName[0] == '[') {
        size_t lastSemi = collTypeName.find_last_of(';');
        if (lastSemi != std::string::npos) {
          std::string inner = collTypeName.substr(1, lastSemi - 1);
          elemTy = resolveType(inner, false);
        }
      } else if (collTypeName[0] == '*' || collTypeName[0] == '^' ||
                 collTypeName[0] == '&' || collTypeName[0] == '~') {
        size_t offset = 1;
        if (collTypeName.length() > 1 &&
            (collTypeName[1] == '?' || collTypeName[1] == '#' ||
             collTypeName[1] == '!'))
          offset++;
        elemTy = resolveType(collTypeName.substr(offset), false);
      }
    }
    if (!elemTy)
      elemTy = llvm::Type::getInt32Ty(m_Context);
  } else {
    elemTy = llvm::Type::getInt32Ty(m_Context);
  }
  std::string vName = fe->VarName;

  llvm::Value *elemPtr = nullptr;
  llvm::Value *elem = nullptr;

  if (isArray) {
    if (arraySourceAddr) {
      elemPtr = m_Builder.CreateGEP(
          collSourceEnt.irType, arraySourceAddr,
          {llvm::ConstantInt::get(llvm::Type::getInt32Ty(m_Context), 0),
           currIdx});
    } else if (collVal->getType()->isPointerTy()) {
      std::string collTypeName = collVal_ent.typeName;
      if (collTypeName.size() > 0 && collTypeName[0] == '[') {
        // Pointer to array literal or alloca'd array
        llvm::Type *arrTy = resolveType(collTypeName, false);
        elemPtr = m_Builder.CreateGEP(
            arrTy, collVal,
            {llvm::ConstantInt::get(llvm::Type::getInt32Ty(m_Context), 0),
             currIdx});
      } else {
        // Raw pointer iteration
        elemPtr = m_Builder.CreateGEP(elemTy, collVal, {currIdx});
      }
    } else {
      // Array R-Value (LLVM Array)
      llvm::Value *allocaColl =
          createEntryBlockAlloca(collVal->getType(), nullptr, "for_arr_tmp");
      m_Builder.CreateStore(collVal, allocaColl);
      elemPtr = m_Builder.CreateGEP(
          collVal->getType(), allocaColl,
          {llvm::ConstantInt::get(llvm::Type::getInt32Ty(m_Context), 0),
           currIdx});
    }

    elem = m_Builder.CreateLoad(elemTy, elemPtr, vName);

  } else {
    // Extract payload
    llvm::Value *payloadGEP = m_Builder.CreateStructGEP(optAlloca->getAllocatedType(), optAlloca, 1);
    elemTy = getLLVMType(fe->ResolvedIterElementType
                             ? fe->ResolvedIterElementType
                             : lowerTypeSyntax(nullptr, fe->IterElementType));
    llvm::Value *payloadValuePtr = m_Builder.CreateBitCast(payloadGEP, llvm::PointerType::get(m_Context, 0), "payload_cast");
    elem = m_Builder.CreateLoad(elemTy, payloadValuePtr, vName);
    elemPtr = payloadValuePtr;
  }


  std::string vBaseName = vName;
  while (!vBaseName.empty() &&
         (vBaseName[0] == '*' || vBaseName[0] == '#' || vBaseName[0] == '&' ||
          vBaseName[0] == '^' || vBaseName[0] == '~' || vBaseName[0] == '!'))
    vBaseName = vBaseName.substr(1);
  while (!vBaseName.empty() &&
         (vBaseName.back() == '#' || vBaseName.back() == '?' || vBaseName.back() == '!'))
    vBaseName.pop_back();

  // 4. Extract into Loop Variable
  // Array elements are loaded values, so an array reference iterator must
  // store the element address.  Protocol `next_ref()` already returns that
  // address as its payload and must keep the loaded payload instead.
  llvm::Value *bindingValue = (fe->IsReference && isArray) ? elemPtr : elem;
  llvm::AllocaInst *vAlloca =
      createEntryBlockAlloca(bindingValue->getType(), nullptr, vBaseName);
  m_Builder.CreateStore(bindingValue, vAlloca);

  // Register in legacy and new symbol tables
  m_NamedValues[vBaseName] = vAlloca;

  TokaSymbol sym;
  sym.allocaPtr = vAlloca;
  fillSymbolMetadata(sym,
                     fe->ResolvedIterElementType
                         ? fe->ResolvedIterElementType
                         : lowerTypeSyntax(nullptr, fe->IterElementType),
                     elem->getType());
  sym.typeName = fe->IterElementType;
  m_Symbols[vBaseName] = sym;

  std::string myLabel = "";
  if (!m_CFStack.empty() && m_CFStack.back().BreakTarget == nullptr)
    myLabel = m_CFStack.back().Label;

  m_CFStack.push_back(
      {myLabel, afterBB, incrBB, resultAddr, m_ScopeStack.size()});
  genStmt(fe->Body.get());
  m_CFStack.pop_back();
  executeScopeUnwinding(m_ScopeStack.size() - 1);
  m_ScopeStack.pop_back();

  if (m_Builder.GetInsertBlock() &&
      !m_Builder.GetInsertBlock()->getTerminator())
    m_Builder.CreateBr(incrBB);

  incrBB->insertInto(f);
  m_Builder.SetInsertPoint(incrBB);
  llvm::Value *nextIdx = m_Builder.CreateAdd(
      currIdx, llvm::ConstantInt::get(llvm::Type::getInt32Ty(m_Context), 1));
  m_Builder.CreateStore(nextIdx, idxAlloca);
  m_Builder.CreateBr(condBB);

  elseBB->insertInto(f);
  m_Builder.SetInsertPoint(elseBB);
  if (fe->ElseBody) {
    m_CFStack.push_back(
        {"", afterBB, nullptr, resultAddr, m_ScopeStack.size()});
    genStmt(fe->ElseBody.get());
    m_CFStack.pop_back();
  }
  if (m_Builder.GetInsertBlock() &&
      !m_Builder.GetInsertBlock()->getTerminator())
    m_Builder.CreateBr(afterBB);
  afterBB->insertInto(f);
  m_Builder.SetInsertPoint(afterBB);
  executeScopeUnwinding(iteratorScopeDepth);
  m_ScopeStack.pop_back();
  if (isUnitResult) {
    llvm::Type *unitTy = getLLVMType(fe->ResolvedType);
    return PhysEntity(llvm::Constant::getNullValue(unitTy), "()", unitTy,
                      false);
  }
  return m_Builder.CreateLoad(m_Builder.getInt32Ty(), resultAddr, "for_result");
}

void CodeGen::genPatternBinding(const MatchArm::Pattern *pat,
                                llvm::Value *targetAddr,
                                llvm::Type *targetType,
                                std::shared_ptr<Type> targetTypeObj) {
  if (targetType && targetType->isVoidTy()) return;

  if (pat->PatternKind == MatchArm::Pattern::Variable) {
    if (pat->Binding == MatchArm::Pattern::BindingOrigin::Existing)
      return;

    llvm::Value *val = targetAddr;
    std::string pName = pat->Name;
    bool isUnique = false;
    bool isShared = false;
    bool isRaw = false;

    // Detect Morphology from Name
    std::string checkName = pName;
    while (!checkName.empty()) {
      char c = checkName[0];
      if (c == '^')
        isUnique = true;
      else if (c == '~')
        isShared = true;
      else if (c == '*')
        isRaw = true;
      else if (c == '#' || c == '&' || c == '!') { /* skip */
      } else
        break;
      checkName = checkName.substr(1);
    }

    while (!pName.empty() &&
           (pName[0] == '*' || pName[0] == '#' || pName[0] == '&' ||
            pName[0] == '^' || pName[0] == '~' || pName[0] == '!'))
      pName = pName.substr(1);
    while (!pName.empty() &&
           (pName.back() == '#' || pName.back() == '?' || pName.back() == '!'))
      pName.pop_back();

    llvm::Value *alloca = nullptr;
    bool alreadyRegistered = false;
    if (!m_ScopeStack.empty()) {
      for (const auto &info : m_ScopeStack.back()) {
        if (info.Name == pName) {
          alloca = info.Alloca;
          alreadyRegistered = true;
          break;
        }
      }
    }

    if (!pat->IsReference) {
      val = m_Builder.CreateLoad(targetType, targetAddr, pName);
      
      if (isShared && val->getType()->isStructTy()) {
          llvm::Value *refPtr = m_Builder.CreateExtractValue(val, 1, pName + "_refptr");
          llvm::Value *refNN = m_Builder.CreateIsNotNull(refPtr, pName + "_ref_nn");

          llvm::Function *F = m_Builder.GetInsertBlock()->getParent();
          llvm::BasicBlock *doIncBB = llvm::BasicBlock::Create(m_Context, pName + "_inc", F);
          llvm::BasicBlock *contBB = llvm::BasicBlock::Create(m_Context, pName + "_cont", F);

          m_Builder.CreateCondBr(refNN, doIncBB, contBB);
          m_Builder.SetInsertPoint(doIncBB);

          // Option A: All shared pointer refcount operations are atomic by default (@arc)
          bool isAtomic = true;

          if (isAtomic) {
              // Retain/Inc uses Monotonic (Relaxed) memory ordering for maximal throughput
              m_Builder.CreateAtomicRMW(llvm::AtomicRMWInst::Add, refPtr, m_Builder.getInt32(1), llvm::MaybeAlign(4), llvm::AtomicOrdering::Monotonic);
          } else {
              llvm::Value *cnt = m_Builder.CreateLoad(llvm::Type::getInt32Ty(m_Context), refPtr);
              llvm::Value *inc = m_Builder.CreateAdd(cnt, m_Builder.getInt32(1));
              m_Builder.CreateStore(inc, refPtr);
          }

          m_Builder.CreateBr(contBB);
          m_Builder.SetInsertPoint(contBB);
      }
    }

    if (alreadyRegistered) {
      m_Builder.CreateStore(val, alloca);
    } else {
      // Create local alloca
      llvm::Type *allocaType = val->getType();
      alloca = createEntryBlockAlloca(allocaType, nullptr, pName);
      m_Builder.CreateStore(val, alloca);

      m_NamedValues[pName] = alloca;
    }

    TokaSymbol sym;
    sym.allocaPtr = alloca;
    // For pattern bindings, metadata is often already inferred by Sema
    fillSymbolMetadata(sym, "", false, false, false, pat->IsReference,
                       pat->IsValueMutable, false, targetType);
    sym.isRebindable = false;
    sym.isContinuous = targetType->isArrayTy();

    // [HOTFIX] Exempt variables (like 'val) preserve their raw morphology!
    if (!pName.empty() && pName[0] == '\'') {
        sym.mode = AddressingMode::Direct;
        sym.indirectionLevel = 0;
        sym.morphology = Morphology::None;
        sym.soulType = targetType;
    }

    // [Fix] Set Morphology and Indirection explicitly for Pattern Bindings
    if (isUnique) {
      sym.morphology = Morphology::Unique;
      sym.indirectionLevel = 1;
    } else if (isShared) {
      sym.morphology = Morphology::Shared;
      // Shared is a struct value, so indirection is 0 (alloca holds the struct)
      // Unless targetType was already a pointer to shared?
      // genMatchExpr loads target. If variable was ~T, targetVal is {T*, Ref}.
      // So alloca stores {T*, Ref}. Indirection 0.
      sym.indirectionLevel = 0;
    } else if (isRaw || targetType->isPointerTy()) {
      sym.morphology = Morphology::Raw;
      sym.indirectionLevel = 1;
    }

    std::string typeName = "";
    if (targetTypeObj) {
      auto soul = targetTypeObj;
      while (soul && (soul->isPointer() || soul->isReference() ||
                      soul->isSmartPointer())) {
        soul = soul->getPointeeType();
      }
      if (soul) {
        typeName = soul->getSoulName();
        // A reference binder stores an address, but its entity is the
        // referenced payload. Keep the payload LLVM type here so subsequent
        // reads load `T` once rather than interpreting `T` as another pointer.
        if (pat->IsReference)
          sym.soulType = getLLVMType(soul);
      }
      sym.soulTypeObj = targetTypeObj;
    }

    std::string dropFunc = "";
    bool hasDrop = false;

    if (!typeName.empty()) {
      if (m_Shapes.count(typeName)) {
        dropFunc = m_Shapes[typeName]->MangledDestructorName;
      }

      if (!dropFunc.empty()) {
        hasDrop = true;
      } else {
        std::string base = typeName;
        while (!base.empty() &&
               (base[0] == '^' || base[0] == '*' || base[0] == '&' ||
                base[0] == '~' || base[0] == '!' || base[0] == '#' ||
                base[0] == '?'))
          base = base.substr(1);
        while (!base.empty() &&
               (base.back() == '#' || base.back() == '?' || base.back() == '!'))
          base.pop_back();

        if (m_Shapes.count(base)) {
          hasDrop = true;
          auto SD = m_Shapes[base];
          if (!SD->MangledDestructorName.empty()) {
            dropFunc = SD->MangledDestructorName;
          }
        }
      }
    }

    bool canDrop = !pat->IsReference && (!isRaw || isUnique || isShared);
    if (!canDrop) {
      hasDrop = false;
      dropFunc = "";
    }

    sym.hasDrop = hasDrop;
    sym.dropFunc = dropFunc;

    if (!alreadyRegistered) {
      m_Symbols[pName] = sym;

      if (!m_ScopeStack.empty()) {
        VariableScopeInfo info;
        info.Name = pName;
        info.Alloca = alloca;
        info.AllocType = targetType;
        info.IsUniquePointer = isUnique;
        info.IsShared = isShared;
        info.HasDrop = hasDrop;
        info.DropFunc = dropFunc;
        info.SoulName = typeName;
        if (alloca && (hasDrop || isUnique || isShared)) {
          info.DropFlag = createEntryBlockAlloca(
              llvm::Type::getInt1Ty(m_Context), nullptr, pName + ".drop.live");
          m_Builder.CreateStore(llvm::ConstantInt::getTrue(m_Context),
                                info.DropFlag);
        }
        // A by-value pattern binder can itself be the root of a later
        // `match cede binder.field`.  Give compiler-managed records the same
        // field liveness mask as ordinary local declarations so that moving
        // one field cannot make the binder's eventual cascade double-drop
        // it.  Explicit destructors remain opaque whole-object invariants.
        auto shapeIt = m_Shapes.find(typeName);
        bool hasSharedMember = false;
        if (shapeIt != m_Shapes.end()) {
          for (const auto &member : shapeIt->second->Members) {
            if (member.IsShared) {
              hasSharedMember = true;
              break;
            }
          }
        }
        if (alloca && hasDrop && shapeIt != m_Shapes.end() &&
            !shapeIt->second->HasExplicitDrop &&
            !hasSharedMember &&
            (shapeIt->second->Kind == ShapeKind::Struct ||
             shapeIt->second->Kind == ShapeKind::Tuple) &&
            shapeIt->second->Members.size() <= 64) {
          const size_t fieldCount = shapeIt->second->Members.size();
          const uint64_t fullMask =
              fieldCount == 64 ? ~0ULL : ((1ULL << fieldCount) - 1ULL);
          info.DropMask = createEntryBlockAlloca(
              llvm::Type::getInt64Ty(m_Context), nullptr,
              pName + ".drop.mask");
          m_Builder.CreateStore(m_Builder.getInt64(fullMask), info.DropMask);
        }
        m_ScopeStack.back().push_back(info);
      }
    }
  } else if (pat->PatternKind == MatchArm::Pattern::Decons) {
    if (targetType->isStructTy()) {
      auto *st = llvm::cast<llvm::StructType>(targetType);

      std::string baseShapeName = "";
      if (m_TypeToName.count(st)) {
        baseShapeName = m_TypeToName[st];
        size_t lt = baseShapeName.find('<');
        if (lt != std::string::npos) baseShapeName = baseShapeName.substr(0, lt);
      }

      bool isEnum = false;
      const ShapeDecl *sh = nullptr;
      if (!baseShapeName.empty() && m_Shapes.count(baseShapeName)) {
        sh = m_Shapes[baseShapeName];
        if (sh->Kind == ShapeKind::Enum) isEnum = true;
      }

      if (isEnum) {
        std::string patName = pat->Name;
        size_t scopePos = patName.rfind("::");
        if (scopePos != std::string::npos) patName = patName.substr(scopePos + 2);

        const ShapeMember *variant = nullptr;
        for (size_t m = 0; m < sh->Members.size(); ++m) {
          if (sh->Members[m].Name == patName) {
            variant = &sh->Members[m];
            break;
          }
        }

        if (variant && (!variant->SubMembers.empty() || !variant->Type.empty())) {
          llvm::Value *payloadAddr = m_Builder.CreateStructGEP(st, targetAddr, 1);
          llvm::Type *payloadLayoutType = nullptr;
          std::vector<llvm::Type *> fieldTypes;
          if (!variant->SubMembers.empty()) {
            for (const auto &f : variant->SubMembers) {
              fieldTypes.push_back(f.ResolvedType ? getLLVMType(f.ResolvedType) : resolveType(f.Type, false));
            }
            payloadLayoutType = llvm::StructType::get(m_Context, fieldTypes, false);
          } else if (!variant->Type.empty()) {
            payloadLayoutType = variant->ResolvedType ? getLLVMType(variant->ResolvedType) : resolveType(variant->Type, false);
          }

          if (payloadLayoutType) {
            llvm::Value *variantAddr = m_Builder.CreateBitCast(payloadAddr, llvm::PointerType::getUnqual(m_Context));

            size_t elisionIndex = -1;
            size_t elisionCount = 0;
            for (size_t i = 0; i < pat->SubPatterns.size(); ++i) {
              if (pat->SubPatterns[i]->PatternKind == MatchArm::Pattern::Elision) {
                elisionIndex = i;
                elisionCount++;
              }
            }

            size_t expectedSize = fieldTypes.empty() ? 1 : fieldTypes.size();
            size_t elidedCount = (elisionCount == 1) ? (expectedSize - (pat->SubPatterns.size() - 1)) : 0;

            for (size_t i = 0; i < pat->SubPatterns.size(); ++i) {
              if (pat->SubPatterns[i]->PatternKind == MatchArm::Pattern::Elision) continue;
              if (pat->SubPatterns[i]->PatternKind == MatchArm::Pattern::Wildcard) continue;

              size_t memberIndex = i;
              if (elisionCount == 1) {
                memberIndex = (i < elisionIndex) ? i : (i + elidedCount - 1);
              }

              llvm::Value *fieldAddr = variantAddr;
              llvm::Type *fieldTy = payloadLayoutType;
              if (!fieldTypes.empty()) {
                fieldAddr = m_Builder.CreateStructGEP(payloadLayoutType, variantAddr, memberIndex);
                fieldTy = fieldTypes[memberIndex];
              }

              std::shared_ptr<Type> subTypeObj = nullptr;
              if (!variant->SubMembers.empty() && variant->SubMembers.size() > memberIndex) {
                subTypeObj = variant->SubMembers[memberIndex].ResolvedType;
              } else {
                subTypeObj = variant->ResolvedType;
              }

              genPatternBinding(pat->SubPatterns[i].get(), fieldAddr, fieldTy, subTypeObj);
            }
          }
        }
      } else {
        bool isNamed = false;
        for (const auto& name : pat->SubPatternNames) {
          if (!name.empty() && name != "..") {
            isNamed = true;
            break;
          }
        }

        size_t elisionIndex = -1;
        size_t elisionCount = 0;
        for (size_t i = 0; i < pat->SubPatterns.size(); ++i) {
          if (pat->SubPatterns[i]->PatternKind == MatchArm::Pattern::Elision) {
            elisionIndex = i;
            elisionCount++;
          }
        }

        size_t expectedSize = st->getNumElements();
        size_t elidedCount = 0;
        if (elisionCount == 1) {
          elidedCount = expectedSize - (pat->SubPatterns.size() - 1);
        }

        for (size_t i = 0; i < pat->SubPatterns.size(); ++i) {
          if (pat->SubPatterns[i]->PatternKind == MatchArm::Pattern::Elision) {
            continue;
          }

          // Skip wildcard ignoring fields
          if (pat->SubPatterns[i]->PatternKind == MatchArm::Pattern::Wildcard) {
            continue;
          }

          size_t memberIndex = -1;
          if (isNamed) {
            std::string shapeName = Type::stripMorphology(pat->Name);
            if (shapeName.empty()) {
              if (st && m_TypeToName.count(st)) {
                shapeName = m_TypeToName[st];
              } else if (st) {
                shapeName = st->getName().str();
              }
            }
            if (!shapeName.empty() && shapeName.front() == '(' && shapeName.back() == ')') {
              if (m_ParenthesizedRecordTypes.count(shapeName)) {
                auto resolved = m_ParenthesizedRecordTypes[shapeName];
                if (resolved && resolved->typeKind == Type::Shape) {
                  shapeName = std::static_pointer_cast<ShapeType>(resolved)->Name;
                }
              }
            }
            if (m_Shapes.count(shapeName)) {
              const auto *sh = m_Shapes[shapeName];
              for (size_t m = 0; m < sh->Members.size(); ++m) {
                if (sh->Members[m].Name ==
                    Type::stripMorphology(pat->SubPatternNames[i])) {
                  memberIndex = m;
                  break;
                }
              }
            }
          } else {
            memberIndex = i;
            if (elisionCount == 1) {
              memberIndex = (i < elisionIndex) ? i : (i + elidedCount - 1);
            }
          }

          if (memberIndex >= expectedSize || memberIndex == (size_t)-1) {
            continue;
          }

          llvm::Value *fieldAddr =
              m_Builder.CreateStructGEP(st, targetAddr, memberIndex);
          std::shared_ptr<Type> subTypeObj = nullptr;
          if (targetTypeObj && targetTypeObj->isShape()) {
              auto stType = std::static_pointer_cast<ShapeType>(targetTypeObj);
              if (stType->Decl && stType->Decl->Members.size() > memberIndex && stType->Decl->Members[memberIndex].ResolvedType) {
                  subTypeObj = stType->Decl->Members[memberIndex].ResolvedType;
              }
          }
          genPatternBinding(pat->SubPatterns[i].get(), fieldAddr,
                            st->getElementType(memberIndex), subTypeObj);
        }
      }
    } else if (!pat->SubPatterns.empty()) {
      // Single payload case (not wrapped in a payload record/struct)
      genPatternBinding(pat->SubPatterns[0].get(), targetAddr, targetType, targetTypeObj);
    }
  }
}

PhysEntity CodeGen::genCallExpr(const CallExpr *call) {
  if (call->Callee == "core/mem::bit_cast" || call->Callee == "bit_cast") {
      PhysEntity argEnt = genExpr(call->Args[0].get());
      llvm::Type *destLLVMTy = getLLVMType(call->ResolvedType);
      llvm::Value *resVal = nullptr;

      if (argEnt.isAddress) {
          llvm::Value *srcAddr = argEnt.value;
          llvm::Value *destPtr = m_Builder.CreatePointerCast(srcAddr, llvm::PointerType::get(destLLVMTy, 0));
          resVal = m_Builder.CreateLoad(destLLVMTy, destPtr, "bitcast.res");
      } else {
          llvm::Value *srcVal = argEnt.load(m_Builder);
          llvm::Type *srcLLVMTy = srcVal->getType();
          if (!srcLLVMTy->isAggregateType() && !destLLVMTy->isAggregateType()) {
              resVal = m_Builder.CreateBitCast(srcVal, destLLVMTy);
          } else {
              llvm::Value *temp = m_Builder.CreateAlloca(destLLVMTy, nullptr, "bitcast.temp");
              llvm::Value *tempSrcPtr = m_Builder.CreatePointerCast(temp, llvm::PointerType::get(srcLLVMTy, 0));
              m_Builder.CreateStore(srcVal, tempSrcPtr);
              resVal = m_Builder.CreateLoad(destLLVMTy, temp, "bitcast.res");
          }
      }
      return PhysEntity(resVal, call->ResolvedType->toString(), destLLVMTy, false);
  }

  if (call->Callee == "__builtin_await") {
      if (!m_CurrentCoroHandle) {
          error(call, DiagID::ERR_CODEGEN_AWAIT_CAN_ONLY_BE_USED_INSIDE_AN_ASYNC);
          return {};
      }
      llvm::Function *saveFn = llvm::Intrinsic::getOrInsertDeclaration(m_Module.get(), llvm::Intrinsic::coro_save);
      llvm::Value *saveToken = m_Builder.CreateCall(saveFn, {m_CurrentCoroHandle});
      
      llvm::Function *suspendFn = llvm::Intrinsic::getOrInsertDeclaration(m_Module.get(), llvm::Intrinsic::coro_suspend);
      llvm::Value *suspendRes = m_Builder.CreateCall(suspendFn, {saveToken, m_Builder.getInt1(false)});
      
      llvm::BasicBlock *resumeBB = llvm::BasicBlock::Create(m_Context, "await.resume", m_Builder.GetInsertBlock()->getParent());
      llvm::BasicBlock *cleanupBB = llvm::BasicBlock::Create(m_Context, "await.cleanup", m_Builder.GetInsertBlock()->getParent());
      
      llvm::SwitchInst *sw = m_Builder.CreateSwitch(suspendRes, m_CurrentCoroSuspendRetBB, 2);
      sw->addCase(m_Builder.getInt8(0), resumeBB);
      sw->addCase(m_Builder.getInt8(1), cleanupBB);
      
      m_Builder.SetInsertPoint(cleanupBB);
      m_Builder.CreateBr(m_CurrentCoroCleanupBB);
      
      m_Builder.SetInsertPoint(resumeBB);
      return PhysEntity(llvm::ConstantInt::get(m_Builder.getInt32Ty(), 0), "i32", m_Builder.getInt32Ty(), false);
  }
  if (call->Callee == "__builtin_coro_resume") {
      PhysEntity handleEnt = genExpr(call->Args[0].get());
      llvm::Value *handleVal = handleEnt.load(m_Builder);
      llvm::Function *resumeFn = llvm::Intrinsic::getOrInsertDeclaration(m_Module.get(), llvm::Intrinsic::coro_resume);
      m_Builder.CreateCall(resumeFn, {handleVal});
      return PhysEntity(llvm::Constant::getNullValue(m_Builder.getInt32Ty()), "void", m_Builder.getVoidTy(), false);
  }
  
  if (call->Callee == "__builtin_coro_done") {
      PhysEntity handleEnt = genExpr(call->Args[0].get());
      llvm::Value *handleVal = handleEnt.load(m_Builder);
      llvm::Function *doneFn = llvm::Intrinsic::getOrInsertDeclaration(m_Module.get(), llvm::Intrinsic::coro_done);
      llvm::Value *res = m_Builder.CreateCall(doneFn, {handleVal});
      return PhysEntity(res, "bool", m_Builder.getInt1Ty(), false);
  }
  
  if (call->Callee == "__builtin_coro_destroy") {
      PhysEntity handleEnt = genExpr(call->Args[0].get());
      llvm::Value *handleVal = handleEnt.load(m_Builder);
      llvm::Function *destroyFn = llvm::Intrinsic::getOrInsertDeclaration(m_Module.get(), llvm::Intrinsic::coro_destroy);
      m_Builder.CreateCall(destroyFn, {handleVal});
      return PhysEntity(llvm::Constant::getNullValue(m_Builder.getInt32Ty()), "void", m_Builder.getVoidTy(), false);
  }

  if (call->Callee == "__builtin_coro_current_handle") {
      if (!m_CurrentCoroHandle) {
          error(call, DiagID::ERR_CODEGEN_BUILTIN_CORO_CURRENT_HANDLE_CAN_ONLY_B);
          return {};
      }
      return PhysEntity(m_CurrentCoroHandle, "*void", m_Builder.getPtrTy(), false);
  }

  if (call->Callee == "__builtin_coro_suspend") {
      if (!m_CurrentCoroHandle) {
          error(call, DiagID::ERR_CODEGEN_BUILTIN_CORO_SUSPEND_CAN_ONLY_BE_USED);
          return {};
      }

      if (m_CurrentCoroTCB) {
          llvm::Function *suspRegFn = m_Module->getFunction("toka_task_suspend_and_register");
          if (!suspRegFn) {
              llvm::FunctionType *ft = llvm::FunctionType::get(m_Builder.getInt32Ty(), {m_Builder.getPtrTy()}, false);
              suspRegFn = llvm::Function::Create(ft, llvm::Function::ExternalLinkage, "toka_task_suspend_and_register", m_Module.get());
          }
          m_Builder.CreateCall(suspRegFn, {m_CurrentCoroTCB});
      }

      llvm::Function *saveFn = llvm::Intrinsic::getOrInsertDeclaration(m_Module.get(), llvm::Intrinsic::coro_save);
      llvm::Value *saveToken = m_Builder.CreateCall(saveFn, {m_CurrentCoroHandle});
      
      llvm::Function *suspendFn = llvm::Intrinsic::getOrInsertDeclaration(m_Module.get(), llvm::Intrinsic::coro_suspend);
      llvm::Value *suspendRes = m_Builder.CreateCall(suspendFn, {saveToken, m_Builder.getInt1(false)});
      
      llvm::BasicBlock *resumeBB = llvm::BasicBlock::Create(m_Context, "suspend.resume", m_Builder.GetInsertBlock()->getParent());
      llvm::BasicBlock *cleanupBB = llvm::BasicBlock::Create(m_Context, "suspend.cleanup", m_Builder.GetInsertBlock()->getParent());
      
      llvm::SwitchInst *sw = m_Builder.CreateSwitch(suspendRes, m_CurrentCoroSuspendRetBB, 2);
      sw->addCase(m_Builder.getInt8(0), resumeBB);
      sw->addCase(m_Builder.getInt8(1), cleanupBB);
      
      m_Builder.SetInsertPoint(cleanupBB);
      m_Builder.CreateBr(m_CurrentCoroCleanupBB);
      
      m_Builder.SetInsertPoint(resumeBB);
      return PhysEntity(llvm::Constant::getNullValue(m_Builder.getInt32Ty()), "void", m_Builder.getVoidTy(), false);
  }

  // Primitives as constructors: i32(42)
  if (call->Callee == "i32" || call->Callee == "u32" || call->Callee == "i64" ||
      call->Callee == "u64" || call->Callee == "f32" || call->Callee == "f64" ||
      call->Callee == "i16" || call->Callee == "u16" || call->Callee == "i8" ||
      call->Callee == "u8" || call->Callee == "usize" ||
      call->Callee == "isize" || call->Callee == "bool") {
    llvm::Type *targetTy = resolveType(call->Callee, false);
    if (call->Args.empty())
      return llvm::Constant::getNullValue(targetTy);
    PhysEntity val_ent = genExpr(call->Args[0].get()).load(m_Builder);
    llvm::Value *val = val_ent.load(m_Builder);
    if (!val)
      return nullptr;
    if (val->getType() != targetTy) {
      if (val->getType()->isIntegerTy() && targetTy->isIntegerTy()) {
        bool isSigned = false;
        if (call->Args[0] && call->Args[0]->ResolvedType) {
            isSigned = call->Args[0]->ResolvedType->isSignedInteger();
        }
        return m_Builder.CreateIntCast(val, targetTy, isSigned);
      } else if (val->getType()->isPointerTy() && targetTy->isIntegerTy()) {
        return m_Builder.CreatePtrToInt(val, targetTy);
      } else if (val->getType()->isIntegerTy() && targetTy->isPointerTy()) {
        return m_Builder.CreateIntToPtr(val, targetTy);
      } else if (val->getType()->isFloatingPointTy() &&
                 targetTy->isFloatingPointTy()) {
        return m_Builder.CreateFPCast(val, targetTy);
      }
    }
    return val;
  }

  // Check if it is a Shape/Struct Constructor
  const ShapeDecl *sh = nullptr;
  if (call->ResolvedShape) {
    sh = call->ResolvedShape;
  } else if (m_Shapes.count(call->Callee)) {
    sh = m_Shapes[call->Callee];
  }

  if (sh) {
    const std::string shapeName =
        sh->CodegenName.empty() ? sh->Name : sh->CodegenName;
    // [Fix] On-Demand Generation (Top-Level)
    if (!m_StructTypes.count(shapeName)) {
      genShape(sh);
    }

    if (sh->Kind == ShapeKind::Struct || sh->Kind == ShapeKind::Tuple ||
        sh->Kind == ShapeKind::Union) {
      if (!m_StructTypes.count(shapeName)) {
        // Should have been generated by now
        return nullptr; // Avoids crashing on CreateAlloca(nullptr)
      }

      // [NEW] Isomorphic Copy Intercept
      if (call->IsIsomorphicCopy && call->Args.size() == 1) {
          return genExpr(call->Args[0].get());
      }
      llvm::StructType *st = m_StructTypes[shapeName];
      auto *alloca = createEntryBlockAlloca(st, nullptr, sh->Name + "_ctor");

      // [Fix] Union Alignment
      if (sh->Kind == ShapeKind::Union) {
        alloca->setAlignment(llvm::Align(sh->MaxAlign));
      }

      size_t argIdx = 0;
      for (const auto &arg : call->Args) {
        std::string fieldName;
        const Expr *valExpr = arg.get();
        bool isNamed = false;

        // Check for named arg (x = val)
        if (auto *bin = dynamic_cast<const BinaryExpr *>(arg.get())) {
          // Attempt auto-deref in CodeGen if Sema authorized it?
          // If LHS is ptr and RHS is int (identity/value mismatch)

          if (bin->Op == "=") {
            if (auto *var =
                    dynamic_cast<const VariableExpr *>(bin->LHS.get())) {
              fieldName = var->Name;
              valExpr = bin->RHS.get();
              isNamed = true;
            } else if (auto *un =
                           dynamic_cast<const UnaryExpr *>(bin->LHS.get())) {
              if (auto *v = dynamic_cast<const VariableExpr *>(un->RHS.get())) {
                fieldName = v->Name;
                valExpr = bin->RHS.get();
                isNamed = true;
              }
            }
          }
        }

        int memberIdx = -1;
        if (sh->Kind == ShapeKind::Union) {
          memberIdx = call->MatchedMemberIdx;
        } else if (isNamed) {
          for (size_t i = 0; i < sh->Members.size(); ++i) {
            if (sh->Members[i].Name == fieldName) {
              memberIdx = (int)i;
              break;
            }
          }
        } else {
          memberIdx = (int)argIdx;
        }

        if (memberIdx >= 0 && memberIdx < (int)sh->Members.size()) {
          PhysEntity val_ent = genExpr(valExpr).load(m_Builder);
          llvm::Value *val = val_ent.load(m_Builder);
          if (!val)
            return nullptr;

          // Auto-cast if needed (e.g. integer promotion) - minimal support
          llvm::Type *destTy = nullptr;
          if (sh->Kind == ShapeKind::Union) {
            const ShapeMember &M = sh->Members[memberIdx];
            if (M.ResolvedType) {
              destTy = getLLVMType(M.ResolvedType);
            } else {
              destTy = resolveType(M.Type, false);
            }
          } else {
            destTy = st->getElementType(memberIdx);
          }
          if (val->getType() != destTy) {
            if (val->getType()->isIntegerTy() && destTy->isIntegerTy()) {
              val = m_Builder.CreateIntCast(val, destTy, true);
            }
          }

          llvm::Value *ptr = nullptr;
          if (sh->Kind == ShapeKind::Union) {
            // [CRITICAL] Legacy bare union physically has only one element: the storage
            // array. We bitcast the base address to the actual member type we
            // matched.
            ptr = m_Builder.CreateBitCast(alloca,
                                          llvm::PointerType::getUnqual(m_Context));
          } else {
            ptr = m_Builder.CreateStructGEP(st, alloca, memberIdx);
          }
          m_Builder.CreateStore(val, ptr);
        }
        argIdx++;
      }
      return m_Builder.CreateLoad(st, alloca);
    }
  }

  // Intrinsic: println / print / String::fmt (Compiler Magic)
  bool isPrintlnLegacy = (call->Callee == "println_legacy" || (call->Callee.size() > 16 && call->Callee.substr(call->Callee.size() - 16) == "::println_legacy"));
  bool isPrintLegacy = (call->Callee == "print_legacy" || (call->Callee.size() > 14 && call->Callee.substr(call->Callee.size() - 14) == "::print_legacy"));
  bool isPrintln = (call->Callee == "println" || (call->Callee.size() > 9 && call->Callee.substr(call->Callee.size() - 9) == "::println"));
  bool isPrint = (call->Callee == "print" || (call->Callee.size() > 7 && call->Callee.substr(call->Callee.size() - 7) == "::print"));
  bool isStringFmt = (call->Callee == "String::fmt" || call->Callee == "std::string::String::fmt" || call->Callee == "string::fmt" || call->Callee == "std::string::string::fmt" || call->Callee == "fmt" || call->Callee == "std::string::fmt");

  // New non-magical zero-overhead println/print unrolled code generation
  if (isPrintln || isPrint) {
      if (call->Args.empty())
          return nullptr;

      std::string fmt = "";
      if (auto *fmtExpr = dynamic_cast<const StringExpr *>(call->Args[0].get())) {
          fmt = fmtExpr->Value;
      } else if (auto *vfmtExpr = dynamic_cast<const ViewStringExpr *>(call->Args[0].get())) {
          fmt = vfmtExpr->Value;
      } else {
          error(call, DiagID::ERR_CODEGEN_INTRINSIC_REQUIRES_A_STRING_LITERAL_AS, call->Callee);
          return nullptr;
      }

      llvm::FunctionCallee printStrFn = m_Module->getOrInsertFunction("toka_print_str", llvm::FunctionType::get(m_Builder.getVoidTy(), {m_Builder.getPtrTy()}, false));
      llvm::FunctionCallee printI32Fn = m_Module->getOrInsertFunction("toka_print_i32", llvm::FunctionType::get(m_Builder.getVoidTy(), {m_Builder.getInt32Ty()}, false));
      llvm::FunctionCallee printF64Fn = m_Module->getOrInsertFunction("toka_print_f64", llvm::FunctionType::get(m_Builder.getVoidTy(), {m_Builder.getDoubleTy()}, false));

      size_t lastPos = 0;
      int argIndex = 1;

      while (lastPos < fmt.size()) {
          size_t startPos = fmt.find('{', lastPos);
          if (startPos == std::string::npos) break;

          if (startPos + 1 < fmt.size() && fmt[startPos + 1] == '{') {
              std::string segment = fmt.substr(lastPos, startPos - lastPos + 1);
              if (!segment.empty()) {
                  llvm::Value *segVal = m_Builder.CreateGlobalString(segment);
                  m_Builder.CreateCall(printStrFn, {segVal});
              }
              lastPos = startPos + 2;
              continue;
          }

          size_t endPos = fmt.find('}', startPos + 1);
          if (endPos == std::string::npos) break;

          std::string segment = fmt.substr(lastPos, startPos - lastPos);
          if (!segment.empty()) {
              llvm::Value *segVal = m_Builder.CreateGlobalString(segment);
              m_Builder.CreateCall(printStrFn, {segVal});
          }

          std::string formatSpecifier = fmt.substr(startPos + 1, endPos - startPos - 1);
          lastPos = endPos + 1;

          if (argIndex < (int)call->Args.size()) {
              auto *argExpr = call->Args[argIndex].get();
              PhysEntity argEnt = genExpr(argExpr);
              llvm::Value *argVal = argEnt.load(m_Builder);

              bool isFmt = (!formatSpecifier.empty() && formatSpecifier[0] == ':');
              if (isFmt) {
                  formatSpecifier = formatSpecifier.substr(1);
              }

              bool isPointer = (argExpr->ResolvedType && argExpr->ResolvedType->isPointer());
              if (isPointer) {
                  auto pointeeTy = argExpr->ResolvedType->getPointeeType();
                  std::string pointeeSoul = pointeeTy ? Type::stripMorphology(pointeeTy->getSoulName()) : "";
                  if (pointeeSoul == "char" || pointeeSoul == "u8") {
                      m_Builder.CreateCall(printStrFn, {argVal});
                  } else {
                      llvm::Value *ptrToPrint = argVal;
                      if (ptrToPrint->getType()->isStructTy()) {
                          ptrToPrint = m_Builder.CreateExtractValue(ptrToPrint, 0, "ptr.addr");
                      }
                      auto printfFunc = m_Module->getOrInsertFunction("printf", llvm::FunctionType::get(m_Builder.getInt32Ty(), {m_Builder.getPtrTy()}, true));
                      llvm::Value *fmtStr = m_Builder.CreateGlobalString("%p");
                      m_Builder.CreateCall(printfFunc, {fmtStr, ptrToPrint});
                  }
              } else {
                  std::string soulTy = Type::stripMorphology(argExpr->ResolvedType ? argExpr->ResolvedType->getSoulName() : "");

              if (soulTy == "String" || soulTy == "string" || soulTy == "str") {
                  llvm::Value *finalArg = argVal;
                  if (!argVal->getType()->isPointerTy()) {
                      finalArg = argEnt.isAddress ? argEnt.value : nullptr;
                      if (!finalArg) {
                          llvm::AllocaInst *tmp = createEntryBlockAlloca(argVal->getType());
                          m_Builder.CreateStore(argVal, tmp);
                          finalArg = tmp;
                      }
                  }

                  if (soulTy == "String" || soulTy == "string") {
                      llvm::Function *cStrFn = m_Module->getFunction("String_c_str");
                      if (!cStrFn) cStrFn = m_Module->getFunction("string_c_str");
                      if (cStrFn) {
                          llvm::Value *cstrVal = m_Builder.CreateCall(cStrFn, {finalArg});
                          m_Builder.CreateCall(printStrFn, {cstrVal});
                      }
                  } else {
                      bool isActualShape = false;
                      if (argExpr->ResolvedType && argExpr->ResolvedType->isShape()) {
                          isActualShape = true;
                      }

                      if (isActualShape) {
                          llvm::Value *ptrVal = nullptr;
                          llvm::Value *lenVal = nullptr;
                          
                          if (argVal->getType()->isPointerTy()) {
                              llvm::Type *viewStrTy = getLLVMType(lowerTypeSyntax(nullptr, "str"));
                              llvm::Value *ptrGEP = m_Builder.CreateStructGEP(viewStrTy, argVal, 0);
                              ptrVal = m_Builder.CreateLoad(m_Builder.getPtrTy(), ptrGEP);
                              llvm::Value *lenGEP = m_Builder.CreateStructGEP(viewStrTy, argVal, 1);
                              lenVal = m_Builder.CreateLoad(getIntPtrTy(), lenGEP);
                          } else {
                              ptrVal = m_Builder.CreateExtractValue(argVal, 0);
                              lenVal = m_Builder.CreateExtractValue(argVal, 1);
                          }
                          
                          auto printfFunc = m_Module->getOrInsertFunction("printf", llvm::FunctionType::get(m_Builder.getInt32Ty(), {m_Builder.getPtrTy()}, true));
                          llvm::Value *fmtStr = m_Builder.CreateGlobalString("%.*s");
                          m_Builder.CreateCall(printfFunc, {fmtStr, m_Builder.CreateTruncOrBitCast(lenVal, m_Builder.getInt32Ty()), ptrVal});
                      } else {
                          m_Builder.CreateCall(printStrFn, {argVal});
                      }
                  }
              }
              else if (soulTy == "i32" || soulTy == "i64" || soulTy == "u32" || soulTy == "u64" || soulTy == "usize" || soulTy == "i16" || soulTy == "i8" || soulTy == "u16" || soulTy == "u8" || soulTy == "bool" || soulTy == "char" || soulTy == "f64" || soulTy == "f32") {
                  if (soulTy == "f64" || soulTy == "f32") {
                      llvm::Value *doubleVal = argVal;
                      if (argVal->getType()->isFloatTy()) {
                          doubleVal = m_Builder.CreateFPExt(argVal, m_Builder.getDoubleTy());
                      }
                      m_Builder.CreateCall(printF64Fn, {doubleVal});
                  } else if (soulTy == "char") {
                      auto putcharFunc = m_Module->getOrInsertFunction("putchar", llvm::FunctionType::get(m_Builder.getInt32Ty(), {m_Builder.getInt32Ty()}, false));
                      llvm::Value *charVal = m_Builder.CreateZExtOrBitCast(argVal, m_Builder.getInt32Ty());
                      m_Builder.CreateCall(putcharFunc, {charVal});
                  } else if (soulTy == "bool") {
                      llvm::Value *cond = m_Builder.CreateICmpNE(argVal, llvm::ConstantInt::get(argVal->getType(), 0));
                      llvm::Value *trueStr = m_Builder.CreateGlobalString("true");
                      llvm::Value *falseStr = m_Builder.CreateGlobalString("false");
                      llvm::Value *selectedStr = m_Builder.CreateSelect(cond, trueStr, falseStr);
                      m_Builder.CreateCall(printStrFn, {selectedStr});
                  } else {
                      llvm::Value *i32Val = m_Builder.CreateIntCast(argVal, m_Builder.getInt32Ty(), true);
                      m_Builder.CreateCall(printI32Fn, {i32Val});
                  }
              }
              else {
                  std::string funcName = soulTy + (isFmt ? "_to_string_fmt" : "_to_string");
                  llvm::Function *toStrFn = m_Module->getFunction(funcName);
                  if (!toStrFn) {
                      std::string traitPrefix = isFmt ? "ToFormat_" : "ToString_";
                      toStrFn = m_Module->getFunction(traitPrefix + soulTy + (isFmt ? "_to_string_fmt" : "_to_string"));
                  }
                  if (!toStrFn) {
                      for (auto const &[traitName, traitDecl] : m_Traits) {
                          std::string traitFunc = traitName + "_" + soulTy + (isFmt ? "_to_string_fmt" : "_to_string");
                          toStrFn = m_Module->getFunction(traitFunc);
                          if (toStrFn) break;
                      }
                  }

                  if (toStrFn) {
                      bool isSRet = toStrFn->getReturnType()->isVoidTy();
                      llvm::Value *finalArg = argVal;
                      if (toStrFn->arg_size() > (isSRet ? 1 : 0)) {
                          llvm::Type *targetTy = toStrFn->getArg(isSRet ? 1 : 0)->getType();
                          if (targetTy->isPointerTy() && !argVal->getType()->isPointerTy()) {
                              finalArg = argEnt.isAddress ? argEnt.value : nullptr;
                              if (!finalArg) {
                                  llvm::AllocaInst *tmp = createEntryBlockAlloca(argVal->getType());
                                  m_Builder.CreateStore(argVal, tmp);
                                  finalArg = tmp;
                              }
                          } else if (!targetTy->isPointerTy() && argVal->getType()->isPointerTy()) {
                              finalArg = m_Builder.CreateLoad(targetTy, argVal);
                          }
                      }

                      llvm::Value *sretAlloca = nullptr;
                      if (isSRet) {
                          llvm::Type *stringTy = resolveType("string", false);
                          if (!stringTy) stringTy = resolveType("String", false);
                          if (!stringTy) stringTy = getLLVMType(lowerTypeSyntax(nullptr, "string"));
                          sretAlloca = createEntryBlockAlloca(stringTy, nullptr, "sret.tmp");
                      }

                      llvm::Value *tmpStr = nullptr;
                      if (isFmt && toStrFn->arg_size() > (isSRet ? 2 : 1)) {
                          llvm::Value *fmtStr = m_Builder.CreateGlobalString(formatSpecifier);
                          llvm::StructType *viewStrTy = llvm::StructType::get(m_Context, std::vector<llvm::Type*>{m_Builder.getPtrTy(), getIntPtrTy()});
                          llvm::Value *viewStrVal = llvm::UndefValue::get(viewStrTy);
                          viewStrVal = m_Builder.CreateInsertValue(viewStrVal, fmtStr, 0);
                          viewStrVal = m_Builder.CreateInsertValue(viewStrVal, llvm::ConstantInt::get(getIntPtrTy(), formatSpecifier.size()), 1);

                          llvm::AllocaInst *viewStrAlloca = createEntryBlockAlloca(viewStrTy);
                          m_Builder.CreateStore(viewStrVal, viewStrAlloca);

                          llvm::Value *fmtArg = viewStrAlloca;
                          if (!toStrFn->getArg(isSRet ? 2 : 1)->getType()->isPointerTy()) {
                              fmtArg = m_Builder.CreateLoad(viewStrTy, viewStrAlloca);
                          }
                          
                          if (isSRet) {
                              m_Builder.CreateCall(toStrFn, {sretAlloca, finalArg, fmtArg});
                              tmpStr = sretAlloca;
                          } else {
                              tmpStr = m_Builder.CreateCall(toStrFn, {finalArg, fmtArg});
                          }
                      } else {
                          if (isSRet) {
                              m_Builder.CreateCall(toStrFn, {sretAlloca, finalArg});
                              tmpStr = sretAlloca;
                          } else {
                              tmpStr = m_Builder.CreateCall(toStrFn, {finalArg});
                          }
                      }

                      if (tmpStr) {
                          llvm::Value *tmpAlloca = nullptr;
                          if (isSRet) {
                              tmpAlloca = tmpStr;
                          } else {
                              tmpAlloca = m_Builder.CreateAlloca(tmpStr->getType());
                              m_Builder.CreateStore(tmpStr, tmpAlloca);
                          }

                          llvm::Function *cStrFn = m_Module->getFunction("String_c_str");
                          llvm::Function *dropFn = m_Module->getFunction("Encap_String_drop");

                          if (cStrFn) {
                              llvm::Value *cstrVal = m_Builder.CreateCall(cStrFn, {tmpAlloca});
                              m_Builder.CreateCall(printStrFn, {cstrVal});
                          }

                          if (dropFn) {
                              m_Builder.CreateCall(dropFn, {tmpAlloca});
                          }
                      }
                  } else {
                      error(call, DiagID::ERR_CODEGEN_TYPE_DOES_NOT_IMPLEMENT_TO_STRING_OR_T, soulTy);
                  }
              }
              }
              argIndex++;
          }
      }

      std::string tail = fmt.substr(lastPos);
      if (!tail.empty()) {
          llvm::Value *segVal = m_Builder.CreateGlobalString(tail);
          m_Builder.CreateCall(printStrFn, {segVal});
      }

      if (isPrintln) {
          llvm::Value *nlVal = m_Builder.CreateGlobalString("\n");
          m_Builder.CreateCall(printStrFn, {nlVal});
      }

      return nullptr;
  }

  if (isPrintlnLegacy || isPrintLegacy || isStringFmt) {
    if (call->Args.empty())
      return nullptr;

    bool isPrintln = isPrintlnLegacy;

    std::string fmt = "";
    if (auto *fmtExpr = dynamic_cast<const StringExpr *>(call->Args[0].get())) {
        fmt = fmtExpr->Value;
    } else if (auto *vfmtExpr = dynamic_cast<const ViewStringExpr *>(call->Args[0].get())) {
        fmt = vfmtExpr->Value;
    } else {
        error(call, DiagID::ERR_CODEGEN_INTRINSIC_REQUIRES_A_STRING_LITERAL_AS, call->Callee);
        return nullptr;
    }

    size_t lastPos = 0;
    size_t pos = 0;
    int argIndex = 1;

    std::string giantFmt = "";
    std::vector<llvm::Value *> callArgs;
    callArgs.push_back(nullptr); // Placeholder for format string

    std::function<void(llvm::Type*, llvm::Value*, std::string)> appendFmt; // Keep it declared but unused to avoid diff errors, or just remove it

    llvm::Function *fromFn = m_Module->getFunction("String_from_cstr");
    if (!fromFn) fromFn = m_Module->getFunction("string_from_cstr");
    llvm::Function *pushStrFn = m_Module->getFunction("String_push_cstr");
    if (!pushStrFn) pushStrFn = m_Module->getFunction("string_push_cstr");
    llvm::Function *cStrFn = m_Module->getFunction("String_c_str");
    if (!cStrFn) cStrFn = m_Module->getFunction("string_c_str");
    llvm::Function *dropFn = m_Module->getFunction("Encap_String_drop");
    if (!dropFn) dropFn = m_Module->getFunction("Encap_string_drop");

    if (!fromFn || !pushStrFn || !cStrFn) {
        error(call, DiagID::ERR_CODEGEN_STRING_FORMATTING_INTRINSIC_REQUIRES_S);
        return nullptr;
    }

    llvm::Type *stringTy = resolveType("string", false);
    if (!stringTy) stringTy = resolveType("String", false);
    if (!stringTy) stringTy = getLLVMType(lowerTypeSyntax(nullptr, "string"));
    llvm::Value *sAlloca = createEntryBlockAlloca(stringTy, nullptr, "fmt_string_builder");
    llvm::Value *emptyStr = m_Builder.CreateGlobalString("");
    m_Builder.CreateCall(fromFn, {sAlloca, emptyStr});

    
    while (lastPos < fmt.size()) {
        size_t startPos = fmt.find('{', lastPos);
        if (startPos == std::string::npos) break;

        if (startPos + 1 < fmt.size() && fmt[startPos + 1] == '{') {
            std::string segment = fmt.substr(lastPos, startPos - lastPos + 1);
            if (!segment.empty()) {
                llvm::Value *segVal = m_Builder.CreateGlobalString(segment);
                m_Builder.CreateCall(pushStrFn, {sAlloca, segVal});
            }
            lastPos = startPos + 2;
            continue;
        }

        size_t endPos = fmt.find('}', startPos + 1);
        if (endPos == std::string::npos) break;

        std::string segment = fmt.substr(lastPos, startPos - lastPos);
        if (!segment.empty()) {
            llvm::Value *segVal = m_Builder.CreateGlobalString(segment);
            m_Builder.CreateCall(pushStrFn, {sAlloca, segVal});
        }

        std::string formatSpecifier = fmt.substr(startPos + 1, endPos - startPos - 1);
        lastPos = endPos + 1;

        if (argIndex < (int)call->Args.size()) {
            std::string soulTy = Type::stripMorphology(call->Args[argIndex]->ResolvedType ? call->Args[argIndex]->ResolvedType->getSoulName() : "");
            PhysEntity argEnt = genExpr(call->Args[argIndex].get());
            llvm::Value *argVal = argEnt.load(m_Builder);

            bool isFmt = (!formatSpecifier.empty() && formatSpecifier[0] == ':');
            if (isFmt) {
                formatSpecifier = formatSpecifier.substr(1);
            }
            std::string funcName = soulTy + (isFmt ? "_to_string_fmt" : "_to_string");

            llvm::Function *toStrFn = m_Module->getFunction(funcName);
            if (!toStrFn) {
                std::string traitPrefix = isFmt ? "ToFormat_" : "ToString_";
                toStrFn = m_Module->getFunction(traitPrefix + soulTy + (isFmt ? "_to_string_fmt" : "_to_string"));
            }
            if (!toStrFn) {
                for (auto const &[traitName, traitDecl] : m_Traits) {
                    std::string traitFunc = traitName + "_" + soulTy + (isFmt ? "_to_string_fmt" : "_to_string");
                    toStrFn = m_Module->getFunction(traitFunc);
                    if (toStrFn) break;
                }
            }

            if (soulTy == "String" || soulTy == "string" || soulTy == "str") {
                if (isFmt) {
                    // String natively ignores format specifier for now, or we could pass it if we implement to_string_fmt on String
                    // For now, treat like regular {}
                }
                llvm::Value *finalArg = argVal;
                if (!argVal->getType()->isPointerTy()) {
                    finalArg = argEnt.isAddress ? argEnt.value : nullptr;
                    if (!finalArg) {
                        llvm::AllocaInst *tmp = createEntryBlockAlloca(argVal->getType());
                        m_Builder.CreateStore(argVal, tmp);
                        finalArg = tmp;
                    }
                }
                if (soulTy == "String" || soulTy == "string") {
                    llvm::Value *cstrVal = m_Builder.CreateCall(cStrFn, {finalArg});
                    m_Builder.CreateCall(pushStrFn, {sAlloca, cstrVal});
                } else {
                    llvm::Function *pushViewFn = m_Module->getFunction("String_push_view");
                    if (!pushViewFn) pushViewFn = m_Module->getFunction("string_push_view");
                    if (pushViewFn) {
                        m_Builder.CreateCall(pushViewFn, {sAlloca, finalArg});
                    }
                }
            } else {
                bool isSRet = toStrFn->getReturnType()->isVoidTy();
                llvm::Value *sretAlloca = nullptr;
                if (isSRet) {
                    llvm::Type *stringTy = resolveType("string", false);
                    if (!stringTy) stringTy = resolveType("String", false);
                    if (!stringTy) stringTy = getLLVMType(lowerTypeSyntax(nullptr, "string"));
                    sretAlloca = createEntryBlockAlloca(stringTy, nullptr, "sret.tmp");
                }

                llvm::Value *tmpStr = nullptr;
                if (toStrFn && argVal) {
                    llvm::Value *finalArg = argVal;
                    if (toStrFn->arg_size() > (isSRet ? 1 : 0)) {
                        llvm::Type *targetTy = toStrFn->getArg(isSRet ? 1 : 0)->getType();
                        if (targetTy->isPointerTy() && !argVal->getType()->isPointerTy()) {
                            finalArg = argEnt.isAddress ? argEnt.value : nullptr;
                            if (!finalArg) {
                                llvm::AllocaInst *tmp = createEntryBlockAlloca(argVal->getType());
                                m_Builder.CreateStore(argVal, tmp);
                                finalArg = tmp;
                            }
                        } else if (!targetTy->isPointerTy() && argVal->getType()->isPointerTy()) {
                            finalArg = m_Builder.CreateLoad(targetTy, argVal);
                        }
                    }
                    
                    if (isFmt && toStrFn->arg_size() > (isSRet ? 2 : 1)) {
                        llvm::Value *fmtStr = m_Builder.CreateGlobalString(formatSpecifier);
                        llvm::StructType *viewStrTy = llvm::StructType::get(m_Context, std::vector<llvm::Type*>{m_Builder.getPtrTy(), getIntPtrTy()});
                        llvm::Value *viewStrVal = llvm::UndefValue::get(viewStrTy);
                        viewStrVal = m_Builder.CreateInsertValue(viewStrVal, fmtStr, 0);
                        viewStrVal = m_Builder.CreateInsertValue(viewStrVal, llvm::ConstantInt::get(getIntPtrTy(), formatSpecifier.size()), 1);
                        
                        llvm::AllocaInst *viewStrAlloca = createEntryBlockAlloca(viewStrTy);
                        m_Builder.CreateStore(viewStrVal, viewStrAlloca);
                        
                        llvm::Value *fmtArg = viewStrAlloca;
                        if (!toStrFn->getArg(isSRet ? 2 : 1)->getType()->isPointerTy()) {
                            fmtArg = m_Builder.CreateLoad(viewStrTy, viewStrAlloca);
                        }
                        
                        if (isSRet) {
                            m_Builder.CreateCall(toStrFn, {sretAlloca, finalArg, fmtArg});
                            tmpStr = sretAlloca;
                        } else {
                            tmpStr = m_Builder.CreateCall(toStrFn, {finalArg, fmtArg});
                        }
                    } else {
                        if (isSRet) {
                            m_Builder.CreateCall(toStrFn, {sretAlloca, finalArg});
                            tmpStr = sretAlloca;
                        } else {
                            tmpStr = m_Builder.CreateCall(toStrFn, {finalArg});
                        }
                    }
                }
                
                if (tmpStr) {
                    llvm::Value *tmpAlloca = nullptr;
                    if (isSRet) {
                        tmpAlloca = tmpStr;
                    } else {
                        tmpAlloca = m_Builder.CreateAlloca(tmpStr->getType());
                        m_Builder.CreateStore(tmpStr, tmpAlloca);
                    }
                    
                    llvm::Value *cstrVal = m_Builder.CreateCall(cStrFn, {tmpAlloca});
                    m_Builder.CreateCall(pushStrFn, {sAlloca, cstrVal});
                    
                    if (dropFn) {
                        m_Builder.CreateCall(dropFn, {tmpAlloca});
                    }
                }
            }
            argIndex++;
        }
    }


    std::string tail = fmt.substr(lastPos);
    if (!tail.empty()) {
        llvm::Value *segVal = m_Builder.CreateGlobalString(tail);
        m_Builder.CreateCall(pushStrFn, {sAlloca, segVal});
    }

    if (isPrintln) {
        llvm::Value *segVal = m_Builder.CreateGlobalString("\n");
        m_Builder.CreateCall(pushStrFn, {sAlloca, segVal});
    }

    if (isStringFmt) {
        llvm::Type *stringTy = resolveType("string", false);
        if (!stringTy) stringTy = resolveType("String", false);
        if (!stringTy) stringTy = getLLVMType(lowerTypeSyntax(nullptr, "string"));
        return m_Builder.CreateLoad(stringTy, sAlloca);
    } else {
        llvm::Value *cstrVal = m_Builder.CreateCall(cStrFn, {sAlloca});
        auto printfFunc = m_Module->getOrInsertFunction("printf", llvm::FunctionType::get(m_Builder.getInt32Ty(), {m_Builder.getPtrTy()}, true));
        llvm::Value *fmtStr = m_Builder.CreateGlobalString("%s");
        m_Builder.CreateCall(printfFunc, {fmtStr, cstrVal});
        
        if (dropFn) {
            m_Builder.CreateCall(dropFn, {sAlloca});
        }
        return nullptr;
    }
  }

  std::string calleeName = call->Callee;
  if (call->ResolvedFn) {
    if (call->ResolvedFn->Name == "main" &&
        call->ResolvedFn->Effect == EffectKind::Async &&
        call->ResolvedFn->CodegenName.empty()) {
      calleeName = "__toka_async_main";
    } else {
      calleeName = call->ResolvedFn->CodegenName.empty()
                       ? call->ResolvedFn->Name
                       : call->ResolvedFn->CodegenName;
    }
  } else if (call->ResolvedExtern) {
    calleeName = call->ResolvedExtern->Name;
    genExtern(call->ResolvedExtern);
  }

  if (calleeName.size() > 5 && calleeName.substr(0, 5) == "libc_") {
    calleeName = calleeName.substr(5);
  }

  llvm::Function *callee = m_Module->getFunction(calleeName);
  if (!callee && call->ResolvedFn) {
    genFunction(call->ResolvedFn, "", true);
    callee = m_Module->getFunction(calleeName);
  }
  if (!callee && call->ResolvedExtern) {
    genExtern(call->ResolvedExtern);
    callee = m_Module->getFunction(call->ResolvedExtern->Name);
  }
  
  if (!callee) {
    // [NEW] Fat Pointer Invocation Intercept
    // Handle both local variables and closure captures
    bool isValidVar = m_Symbols.count(calleeName) > 0;
    std::shared_ptr<Type> symTy = nullptr;
    
    if (isValidVar) {
        symTy = m_Symbols[calleeName].soulTypeObj;
    } else if (m_Symbols.count("self")) {
        auto selfTy = m_Symbols["self"].soulTypeObj;
        if (selfTy && selfTy->isReference()) {
            selfTy = std::static_pointer_cast<toka::PointerType>(selfTy)->PointeeType;
        }
        if (selfTy && selfTy->isShape() && selfTy->getSoulName().find("__Closure_") == 0) {
            auto shapeTy = std::static_pointer_cast<ShapeType>(selfTy);
            if (shapeTy->Decl) {
                for (const auto &memb : shapeTy->Decl->Members) {
                    if (memb.Name == calleeName) {
                        isValidVar = true;
                        symTy = memb.ResolvedType;
                        break;
                    }
                }
            }
        }
    }

    if (isValidVar) {
        if (symTy && symTy->isPointer()) {
            symTy = std::static_pointer_cast<PointerType>(symTy)->PointeeType;
        }
        
        if (symTy && (symTy->isFunction() || symTy->isDynFn())) {
            bool isDynFn = symTy->isDynFn();
            std::vector<std::shared_ptr<Type>> paramTypes;
            std::shared_ptr<Type> returnType;
            if (isDynFn) {
                auto fnTy = std::static_pointer_cast<DynFnType>(symTy);
                paramTypes = fnTy->ParamTypes;
                returnType = fnTy->ReturnType;
            } else {
                auto fnTy = std::static_pointer_cast<FunctionType>(symTy);
                paramTypes = fnTy->ParamTypes;
                returnType = fnTy->ReturnType;
            }
            
            auto varExpr = std::make_unique<VariableExpr>(calleeName);
            varExpr->ResolvedType = symTy; // Optional, but helps downstream
            PhysEntity fatVal_ent = genExpr(varExpr.get());
            llvm::Value *fatVal = fatVal_ent.load(m_Builder);
            
            if (fatVal && fatVal->getType()->isStructTy() && 
                (fatVal->getType()->getStructNumElements() == 2 || fatVal->getType()->getStructNumElements() == 3)) {
                
                llvm::Value *envPtr = m_Builder.CreateExtractValue(fatVal, 0, "closure_env");
                llvm::Value *funcPtr = m_Builder.CreateExtractValue(fatVal, 1, "closure_func");
                
                std::vector<llvm::Type*> argTys;
                std::vector<llvm::Value*> argVals;

                bool isSRet = returnType && shouldReturnSRet(returnType);
                llvm::Value *sretAlloc = nullptr;
                if (isSRet) {
                    llvm::Type *retLLVMTy = getLLVMType(returnType);
                    sretAlloc = createEntryBlockAlloca(retLLVMTy, nullptr, "sret.tmp");
                    argTys.push_back(llvm::PointerType::getUnqual(m_Context));
                    argVals.push_back(sretAlloc);
                }
                
                argTys.push_back(llvm::PointerType::getUnqual(m_Context));
                argVals.push_back(envPtr);
                
                for (size_t i = 0; i < call->Args.size(); ++i) {
                    llvm::Value *av = genExpr(call->Args[i].get()).load(m_Builder);
                    llvm::Type *expectedTy = getLLVMType(paramTypes[i]);
                    
                    if (av && expectedTy->isPointerTy() && av->getType()->isStructTy()) {
                       llvm::AllocaInst *tmp = createEntryBlockAlloca(av->getType(), nullptr, "arg_tmp_byref");
                       m_Builder.CreateStore(av, tmp);
                       av = tmp;
                       expectedTy = av->getType();
                    }
                    
                    argVals.push_back(av);
                    argTys.push_back(expectedTy);
                }
                
                llvm::Type *retTy = isSRet ? llvm::Type::getVoidTy(m_Context) : getLLVMType(returnType);
                llvm::FunctionType *llFnTy = llvm::FunctionType::get(retTy, argTys, false);
                
                llvm::CallInst *ci = m_Builder.CreateCall(llFnTy, funcPtr, argVals);
                if (call->CallableReceiver ==
                    CallableReceiverMode::Consuming) {
                  suppressDropForMove(calleeName);
                  if (isDynFn) {
                    llvm::Function *freeFn = m_Module->getFunction("free");
                    if (freeFn)
                      m_Builder.CreateCall(freeFn, envPtr);
                  }
                }
                if (isSRet) {
                    ci->addParamAttr(0, llvm::Attribute::get(m_Context, llvm::Attribute::StructRet, getLLVMType(returnType)));
                    return PhysEntity(sretAlloc, returnType->getSoulName(), getLLVMType(returnType), true);
                }
                return PhysEntity(ci, returnType->getSoulName(), ci->getType(), false);
            }
        }
    }

    // Check for ADT Constructor (Type::Member)
    std::string callName = call->Callee;
    size_t delim = callName.find("::");
    if (delim != std::string::npos) {
      std::string optName = callName.substr(0, delim);
      std::string varName = callName.substr(delim + 2);
      const ShapeDecl *enumShape =
          call->ResolvedShape && call->ResolvedShape->Kind == ShapeKind::Enum
              ? call->ResolvedShape
              : m_Shapes.count(optName) &&
                        m_Shapes[optName]->Kind == ShapeKind::Enum
                    ? m_Shapes[optName]
                    : nullptr;

      // Try static call Type::Member -> Type_Member
      std::string mangledName = optName + "_" + varName;
      if (auto *f = m_Module->getFunction(mangledName)) {
        calleeName = mangledName;
        callee = f;
        // Don't return here! Break to let the normal Call generation run,
        // which includes the `isAsync` check and wrapper logic!
      } else if (enumShape) {
        const ShapeDecl *sh = enumShape;
        const std::string shapeName =
            sh->CodegenName.empty() ? sh->Name : sh->CodegenName;
        auto structType = m_StructTypes.find(shapeName);
        if (structType == m_StructTypes.end() || !structType->second) {
          genShape(sh);
          structType = m_StructTypes.find(shapeName);
        }
        if (structType == m_StructTypes.end() || !structType->second)
          return nullptr;

        int tag = -1;
        const ShapeMember *targetVar = nullptr;
        for (int i = 0; i < (int)sh->Members.size(); ++i) {
          if (sh->Members[i].Name == varName) {
            tag = (sh->Members[i].TagValue == -1)
                      ? i
                      : (int)sh->Members[i].TagValue;
            targetVar = &sh->Members[i];
            break;
          }
        }

        if (tag != -1) {
          std::vector<llvm::Value *> args;
          for (auto &argExpr : call->Args) {
            args.push_back(genExpr(argExpr.get()).load(m_Builder));
          }
          if (!args.empty() && !args.back())
            return nullptr;

          llvm::StructType *st = structType->second;
          llvm::Value *alloca = createEntryBlockAlloca(st, nullptr, "opt_ctor");
          llvm::Value *tagAddr =
              m_Builder.CreateStructGEP(st, alloca, 0, "tag_addr");
          m_Builder.CreateStore(
              llvm::ConstantInt::get(llvm::Type::getInt8Ty(m_Context), tag),
              tagAddr);

          if (targetVar) {
            llvm::Type *payloadType = nullptr;
            std::vector<llvm::Type *> fieldTypes;

            if (!targetVar->SubMembers.empty()) {
              for (auto &f : targetVar->SubMembers) {
                if (f.ResolvedType) {
                  fieldTypes.push_back(getLLVMType(f.ResolvedType));
                } else {
                  fieldTypes.push_back(resolveType(f.Type, false));
                }
              }
              payloadType = llvm::StructType::get(m_Context, fieldTypes, false);
            } else if (!targetVar->Type.empty()) {
              if (targetVar->ResolvedType) {
                  payloadType = getLLVMType(targetVar->ResolvedType);
              } else {
                  payloadType = resolveType(targetVar->Type, false);
              }
            }

            if (payloadType && !payloadType->isVoidTy()) {
              llvm::Value *payloadAddr =
                  m_Builder.CreateStructGEP(st, alloca, 1, "payload_addr");
              llvm::Value *castPtr = m_Builder.CreateBitCast(
                  payloadAddr, llvm::PointerType::getUnqual(m_Context));

              if (!targetVar->SubMembers.empty()) {
                // Multi-field enum payload record
                for (size_t i = 0; i < args.size() && i < fieldTypes.size();
                     ++i) {
                  llvm::Value *fPtr =
                      m_Builder.CreateStructGEP(payloadType, castPtr, i);
                  m_Builder.CreateStore(args[i], fPtr);
                }
              } else {
                // Single Payload
                if (args.size() == 1) {
                  m_Builder.CreateStore(args[0], castPtr);
                }
              }
            }
          }
          return m_Builder.CreateLoad(st, alloca);
        }
      }
    }
  }

  // Double check callee because we might have skipped it
  if (!callee) {
    error(call, DiagID::ERR_CODEGEN_CANNOT_RESOLVE_FUNCTION, calleeName);
    return nullptr;
  }

  // Proceed with normal Call compilation
  const FunctionDecl *funcDecl = call->ResolvedFn;
  const ExternDecl *extDecl = call->ResolvedExtern;
  if (!funcDecl && !extDecl) {
    if (m_Functions.count(call->Callee)) {
      funcDecl = m_Functions[call->Callee];
    } else if (m_Functions.count(calleeName)) {
      funcDecl = m_Functions[calleeName];
    }
  }

  if (!funcDecl && !extDecl && m_Externs.count(calleeName))
    extDecl = m_Externs[calleeName];

  bool isAsync = false;
  if (funcDecl && funcDecl->Effect == EffectKind::Async) isAsync = true;
  if (extDecl && extDecl->Effect == EffectKind::Async) isAsync = true;

  bool isSRet = call->ResolvedType && shouldReturnSRet(call->ResolvedType) && !isAsync;

  std::vector<llvm::Value *> argsV;
  std::vector<llvm::Value *> cededNullablePayloadShells;
  for (size_t i = 0; i < call->Args.size(); ++i) {
    const auto *cededArg =
        dynamic_cast<const CedeExpr *>(call->Args[i].get());
    bool isRef = false;
    if (funcDecl && i < funcDecl->Args.size()) {
      isRef = funcDecl->Args[i].IsReference;
    } else if (extDecl && i < extDecl->Args.size()) {
      isRef = extDecl->Args[i].IsReference;
    }

    llvm::Value *val = nullptr;
    bool shouldPassAddr = isRef;
    llvm::Type *pTy = nullptr;
    size_t paramIdx = isSRet ? i + 1 : i;
    if (callee && paramIdx < callee->getFunctionType()->getNumParams())
      pTy = callee->getFunctionType()->getParamType(paramIdx);

    bool isCaptured = false;

    if (funcDecl && i < funcDecl->Args.size()) {
      const auto &arg = funcDecl->Args[i];
      
      bool isArgUnique = arg.IsUnique;
      bool isArgShared = arg.IsShared;
      bool isArgRef = arg.IsReference;
      bool hasArgPtr = arg.IsRawPointer;
      if (arg.ResolvedType) {
          isArgUnique = isArgUnique || arg.ResolvedType->isUniquePtr();
          isArgShared = isArgShared || arg.ResolvedType->isSharedPtr();
          isArgRef = isArgRef || arg.ResolvedType->isReference();
          hasArgPtr = hasArgPtr || arg.ResolvedType->isPointer();
      }

      // Force Capture for Unique Pointers to enable In-Place Move / Borrow
      // [ABI Fix] Force Capture for Shared Pointers to pass by Pointer
      // (avoiding ABI split)
      // [NEW] Lifetime Union: Force capture if param is a dependency
      for (const auto &dep : funcDecl->LifeDependencies) {
        if (dep == arg.Name && !hasArgPtr && !isArgRef &&
            !isArgUnique && !isArgShared) {
          isCaptured = true;
          break;
        }
      }

      // Only capture if it's a Value Type (Struct/Array/Mutable) AND NOT a
      // Pointer/Shared/Reference
      if (!isCaptured && !hasArgPtr && !isArgRef) {
        if (arg.IsValueMutable) {
          isCaptured = true;
        } else {
          llvm::Type *logicalTy = resolveType(arg.Type, false);
          if (logicalTy && (logicalTy->isStructTy() || logicalTy->isArrayTy()))
            isCaptured = true;
        }
      }

      // [Fix] Unique/Shared/Rebindable Pointers MUST be passed by Reference
      // (Capture) This matches genFunction ABI where they are treated as
      // Captured Arguments. This allows the Callee to manipulate the Handle
      // (e.g. invalidating it on Move or rebinding it).
      if (isArgUnique || isArgShared || arg.IsRebindable) {
        isCaptured = true;
      }
    } else if (extDecl && i < extDecl->Args.size()) {
      const auto &arg = extDecl->Args[i];
      if (!arg.IsRawPointer) {
        llvm::Type *logicalTy = resolveType(arg.Type, false);
        if (logicalTy && (logicalTy->isStructTy() || logicalTy->isArrayTy()))
          isCaptured = true;
      }
    }

    if (isCaptured || isRef) {
      shouldPassAddr = true;
    }

    if (shouldPassAddr) {
      if (dynamic_cast<const AddressOfExpr *>(call->Args[i].get())) {
        val = genExpr(call->Args[i].get()).load(m_Builder);
      } else {
        // [Fix] Explicit Identity Capture
        // If we are capturing (Pass-By-Reference), we want the Identity
        // Address (Alloca), not the Soul Address (Heap Ptr). genAddr often
        // peels to Soul. We manually unwrap and seek Identity.
        const Expr *rawArg = call->Args[i].get();
        // Unwrap decorators to find the variable
        while (true) {
          if (auto *ce = dynamic_cast<const CedeExpr *>(rawArg))
            rawArg = ce->Value.get();
          else if (auto *cast = dynamic_cast<const CastExpr *>(rawArg))
            rawArg = cast->Expression.get();
          else if (auto *ue = dynamic_cast<const UnaryExpr *>(rawArg))
            rawArg = ue->RHS.get();
          else if (auto *pe = dynamic_cast<const PostfixExpr *>(rawArg))
            rawArg = pe->LHS.get();
          else
            break;
        }

        if (auto *ve = dynamic_cast<const VariableExpr *>(rawArg)) {
          if (ve->HasConstantValue) {
            // [Fix] Constants are RValues. Fall through to Temp
            // Materialization (genExpr)
            val = nullptr;
          } else {
            std::string baseName = toka::Type::stripMorphology(ve->Name);
            if (m_Symbols.count(baseName)) {
               auto &sym = m_Symbols[baseName];
               if (sym.mode == AddressingMode::Reference || 
                   (sym.mode == AddressingMode::Pointer && sym.morphology == Morphology::None)) {
                   val = getEntityAddr(ve->codegenName());
               } else {
                   val = getIdentityAddr(ve->codegenName());
               }
            } else {
               val = getIdentityAddr(ve->codegenName());
            }
          }
        }

        // `genAddr` may lower a call-shaped expression in order to find an
        // address.  Captured rvalues have no caller-owned identity, so doing
        // that before materializing the temporary evaluates their side
        // effects twice.  Evaluate such an expression exactly once below and
        // pass the address of that materialized value instead.
        const auto *taskStart = dynamic_cast<const MemberExpr *>(rawArg);
        const bool isCallShapedRValue =
            dynamic_cast<const CallExpr *>(rawArg) ||
            dynamic_cast<const MethodCallExpr *>(rawArg) ||
            dynamic_cast<const StartExpr *>(rawArg) ||
            dynamic_cast<const NewExpr *>(rawArg) ||
            dynamic_cast<const InitStructExpr *>(rawArg) ||
            dynamic_cast<const ArrayInitExpr *>(rawArg) ||
            (taskStart && taskStart->IsTaskStart);
        if (!val && !isCallShapedRValue) {
          val = genAddr(call->Args[i].get());
        }

        // [Fix] Handle RValues for Captured Arguments (Temp
        // Materialization)
        if (!val) {
          PhysEntity pe = genExpr(call->Args[i].get());
          // If genAddr failed, it's likely an RValue (return from call,
          // etc.) We must create a temporary alloca to pass its address.
          llvm::Value *rVal = pe.load(m_Builder);
          if (rVal) {
            llvm::AllocaInst *tmp =
                createEntryBlockAlloca(rVal->getType(), nullptr, "arg_tmp");
            m_Builder.CreateStore(rVal, tmp);
            val = tmp;
          }
        }
      }
    } else {
      val = genExpr(call->Args[i].get()).load(m_Builder);
    }

    const Expr *cededSource = nullptr;
    if (cededArg) {
      cededSource = cededArg->Value.get();
      while (auto *cast = dynamic_cast<const CastExpr *>(cededSource))
        cededSource = cast->Expression.get();
      if (auto *unary = dynamic_cast<const UnaryExpr *>(cededSource))
        cededSource = unary->RHS.get();

      // A guarded `^T?` is an owner slot pointing to a `{ T, present }`
      // allocation. A captured `cede` parameter for T expects T's address,
      // which is the nullable shell's offset-zero payload. Pass that shell
      // directly, then release it after the callee moves out the payload.
      if (shouldPassAddr) {
        if (auto *var = dynamic_cast<const VariableExpr *>(cededSource)) {
          const std::string baseName = Type::stripMorphology(var->Name);
          auto source = m_Symbols.find(baseName);
          std::shared_ptr<Type> sourceSoul =
              source != m_Symbols.end() && source->second.soulTypeObj
                  ? source->second.soulTypeObj->getSoulType()
                  : nullptr;
          if (source != m_Symbols.end() &&
              source->second.morphology == Morphology::Unique && sourceSoul &&
              sourceSoul->IsNullable) {
            if (llvm::Value *sourceSlot = getIdentityAddr(var->codegenName())) {
              llvm::Value *shell = m_Builder.CreateLoad(
                  m_Builder.getPtrTy(), sourceSlot, "cede.nullable.shell");
              val = shell;
              cededNullablePayloadShells.push_back(shell);
            }
          }
        }
      }
    }

    // A captured argument is passed as the address of the caller's slot, so
    // that path deliberately bypasses genCedeExpr. Preserve cede's cleanup
    // effect here: the callee now owns the resource and the caller must not
    // destroy the same handle when its scope/frame unwinds.
    if (cededArg && shouldPassAddr) {
      if (auto *var = dynamic_cast<const VariableExpr *>(cededSource))
        suppressDropForMove(var->Name);
      else if (auto *member = dynamic_cast<const MemberExpr *>(cededSource))
        suppressDropForPartialMove(member);
      else if (auto *index = dynamic_cast<const ArrayIndexExpr *>(cededSource))
        suppressDropForPartialMove(index);
    }

    if (!val) {
      error(call, DiagID::ERR_CODEGEN_FAILED_TO_GENERATE_ARGUMENT_FOR, std::to_string(i), call->Callee);
      return nullptr;
    }

    // [ABI Fix] Shared Pointer Argument Copy (Incref) REMOVED
    // We now pass Shared Pointers by "Single Pointer" (Address of Handle).
    // This implies Borrowing semantics (no transfer of ownership, no new
    // reference). The Callee will see the Caller's handle via pointer.
    // Explicit Incref/Decref is NOT needed for this ABI strategy unless we
    // implement explicit cloning.
    if (funcDecl && i < funcDecl->Args.size() && funcDecl->Args[i].IsShared) {
      // No-op for Pass-By-Pointer
    }

    // [NEW] Fat Pointer Synthesis for Strings
    if (funcDecl && i < funcDecl->Args.size()) {
       auto targetTyObj = funcDecl->Args[i].ResolvedType;
       if (targetTyObj && targetTyObj->isShape()) {
           auto shpTarget = std::static_pointer_cast<toka::ShapeType>(targetTyObj);
           if (shpTarget->Name == "str") {
               auto argTyObj = call->Args[i]->ResolvedType;
               bool isNakedCString = false;
               if (argTyObj) {
                   std::string argStr = argTyObj->toString();
                   if (argStr == "*char") isNakedCString = true;
               }
               auto *strExpr = dynamic_cast<const StringExpr *>(call->Args[i].get());
               
               if (isNakedCString && !strExpr) {
                   // User requested Safety Mode B: Do not allow dynamic *char implicitly!
                   error(call->Args[i].get(), DiagID::ERR_CODEGEN_UNSAFE_IMPLICIT_CAST_CANNOT_AUTOMATICA);
                   return nullptr;
               }
           }
       }
    }

    // [NEW] Fat Pointer Synthesis for Closures
    if (val && call->Args[i]->ResolvedType && call->Args[i]->ResolvedType->isShape()) {
      auto shp = std::static_pointer_cast<toka::ShapeType>(call->Args[i]->ResolvedType);
      if (shp->Name.find("__Closure_") == 0) {
        bool expectsFunction = false;
        if (funcDecl && i < funcDecl->Args.size()) {
           auto resTy = funcDecl->Args[i].ResolvedType;
           if (resTy && resTy->typeKind == toka::Type::Function) expectsFunction = true;
           else if (funcDecl->Args[i].Type.find("fn(") == 0) expectsFunction = true;
        }
        
        if (expectsFunction) {
           bool isDynFn = false;
           if (funcDecl && i < funcDecl->Args.size()) {
               auto resTy = funcDecl->Args[i].ResolvedType;
               if (resTy && resTy->typeKind == toka::Type::DynFn) isDynFn = true;
               else if (funcDecl->Args[i].Type.find("dyn fn(") == 0) isDynFn = true;
           }

           llvm::Type *envTy = val->getType();
           llvm::Value *envPtrAddr;
           
           if (isDynFn) {
               // Heap Allocation for `dyn fn`
               llvm::Type *objTy = getLLVMType(call->Args[i]->ResolvedType);
               
               llvm::Function *mallocFn = m_Module->getFunction("malloc");
               if (!mallocFn) {
                   mallocFn = llvm::Function::Create(llvm::FunctionType::get(m_Builder.getPtrTy(), {getIntPtrTy()}, false), llvm::Function::ExternalLinkage, "malloc", m_Module.get());
               }
               uint64_t size = m_Module->getDataLayout().getTypeAllocSize(objTy);
               llvm::Value *heapMem = m_Builder.CreateCall(mallocFn, {llvm::ConstantInt::get(getIntPtrTy(), size)});
               envPtrAddr = m_Builder.CreatePointerCast(heapMem, llvm::PointerType::getUnqual(m_Context));
               
               if (envTy->isPointerTy()) {
                   llvm::Value *loadedEnv = m_Builder.CreateLoad(objTy, val);
                   m_Builder.CreateStore(loadedEnv, envPtrAddr);
               } else {
                   m_Builder.CreateStore(val, envPtrAddr);
               }
           } else {
               // Stack Allocation for `fn`
               if (envTy->isPointerTy()) {
                   envPtrAddr = val;
               } else {
                   envPtrAddr = createEntryBlockAlloca(envTy, nullptr, "closure_env_alloc");
                   m_Builder.CreateStore(val, envPtrAddr);
               }
           }
           
           llvm::Value *opaqueEnv = m_Builder.CreatePointerCast(envPtrAddr, llvm::PointerType::getUnqual(m_Context));
           
           std::string invokeName = shp->Name + "___invoke";
           llvm::Function *invokeFn = m_Module->getFunction(invokeName);
           if (!invokeFn) {
              error(call, DiagID::ERR_CODEGEN_CLOSURE_INVOKE_FUNCTION_NOT_GENERATED, invokeName);
              return nullptr;
           }
           llvm::Value *opaqueFunc = m_Builder.CreatePointerCast(invokeFn, llvm::PointerType::getUnqual(m_Context));
           
           llvm::StructType *fatPtrTy;
           if (isDynFn) {
               fatPtrTy = llvm::StructType::get(
                   llvm::PointerType::getUnqual(m_Context),
                   llvm::PointerType::getUnqual(m_Context),
                   llvm::PointerType::getUnqual(m_Context)
               );
           } else {
               fatPtrTy = llvm::StructType::get(
                   llvm::PointerType::getUnqual(m_Context),
                   llvm::PointerType::getUnqual(m_Context)
               );
           }
           
           llvm::Value *fatPtr = llvm::UndefValue::get(fatPtrTy);
           fatPtr = m_Builder.CreateInsertValue(fatPtr, opaqueEnv, 0);
           fatPtr = m_Builder.CreateInsertValue(fatPtr, opaqueFunc, 1);
           
           if (isDynFn) {
               std::string dropName = "Encap_" + shp->Name + "_drop";
               llvm::Function *dropFn = m_Module->getFunction(dropName);
               llvm::Value *opaqueDrop = llvm::ConstantPointerNull::get(llvm::PointerType::getUnqual(m_Context));
               if (dropFn) {
                   opaqueDrop = m_Builder.CreatePointerCast(dropFn, llvm::PointerType::getUnqual(m_Context));
               }
               fatPtr = m_Builder.CreateInsertValue(fatPtr, opaqueDrop, 2);
           }
           
           // Pass fat pointer by reference (ABI for DynFnType/FunctionType)
           llvm::AllocaInst *fatPtrAlloc = createEntryBlockAlloca(fatPtrTy, nullptr, "fat_ptr_alloc");
           m_Builder.CreateStore(fatPtr, fatPtrAlloc);
           val = fatPtrAlloc;
        }
      }
    }

    // Fallback: If we generated a Value (e.g. Struct) but Function expects
    // Pointer (Implicit ByRef), wrap it now.
    if (val && paramIdx < callee->getFunctionType()->getNumParams()) {
      llvm::Type *paramTy = callee->getFunctionType()->getParamType(paramIdx);
      if (paramTy->isPointerTy() && val->getType()->isStructTy()) {
        llvm::Value *lvalueAddr = nullptr;
        const Expr *rawArg = call->Args[i].get();
        while (true) {
          if (auto *ce = dynamic_cast<const CedeExpr *>(rawArg))
            rawArg = ce->Value.get();
          else if (auto *cast = dynamic_cast<const CastExpr *>(rawArg))
            rawArg = cast->Expression.get();
          else if (auto *ue = dynamic_cast<const UnaryExpr *>(rawArg))
            rawArg = ue->RHS.get();
          else if (auto *pe = dynamic_cast<const PostfixExpr *>(rawArg))
            rawArg = pe->LHS.get();
          else
            break;
        }

        if (auto *ve = dynamic_cast<const VariableExpr *>(rawArg)) {
          if (!ve->HasConstantValue) {
            std::string baseName = toka::Type::stripMorphology(ve->Name);
            if (m_Symbols.count(baseName)) {
              auto &sym = m_Symbols[baseName];
              if (sym.mode == AddressingMode::Reference ||
                  (sym.mode == AddressingMode::Pointer &&
                   sym.morphology == Morphology::None)) {
                lvalueAddr = getEntityAddr(ve->codegenName());
              } else {
                lvalueAddr = getIdentityAddr(ve->codegenName());
              }
            } else {
              lvalueAddr = getIdentityAddr(ve->codegenName());
            }
          }
        } else if (auto *member = dynamic_cast<const MemberExpr *>(rawArg)) {
          // `.start` is a call-shaped rvalue.  It was materialized above;
          // asking for its address here would lower it a second time.
          if (!member->IsTaskStart)
            lvalueAddr = genAddr(rawArg);
        }

        if (lvalueAddr) {
          val = lvalueAddr;
        } else {
          llvm::AllocaInst *tmp = createEntryBlockAlloca(
              val->getType(), nullptr, "arg_fallback_byref");
          m_Builder.CreateStore(val, tmp);
          val = tmp;
        }
      }
    }

    if (paramIdx < callee->getFunctionType()->getNumParams()) {
      llvm::Type *paramType = callee->getFunctionType()->getParamType(paramIdx);

      // Unsizing Coercion (Concrete -> dyn @Trait)
      std::string targetArgType =
          (funcDecl ? funcDecl->Args[i].Type
                    : (extDecl ? extDecl->Args[i].Type : ""));
      if (targetArgType.size() >= 4 && targetArgType.substr(0, 3) == "dyn") {
        // 1. Identify Trait Name
        std::string traitName = "";
        if (targetArgType.find("dyn @") == 0)
          traitName = targetArgType.substr(5);
        else if (targetArgType.find("dyn@") == 0)
          traitName = targetArgType.substr(4);

        // 2. Identify Concrete Type Name
        std::string concreteName = "";
        const Expr *argExpr = call->Args[i].get();

        // [New] Annotated AST: Use ResolvedType
        if (argExpr->ResolvedType) {
          auto rt = argExpr->ResolvedType;
          // Strip pointer/reference layers to get the core Shape/Value
          // implementation (Traits are usually implemented on value types)
          while (rt && (rt->isPointer() || rt->isReference() ||
                        rt->isSmartPointer())) {
            if (auto inner = rt->getPointeeType())
              rt = inner;
            else
              break;
          }
          if (rt) {
            concreteName = rt->toString();
            // Strip suffixes (#, ?, !) from the resulting name to match
            // VTable expectation
            while (!concreteName.empty() &&
                   (concreteName.back() == '#' || concreteName.back() == '?' ||
                    concreteName.back() == '!')) {
              concreteName.pop_back();
            }
          }
        }

        // Legacy Fallback / Refinement
        if (concreteName.empty() || concreteName == "void") {
          if (auto *ve = dynamic_cast<const VariableExpr *>(argExpr)) {
            if (m_Symbols.count(ve->Name)) {
              llvm::Type *ct = m_Symbols[ve->Name].soulType;
              if (m_TypeToName.count(ct))
                concreteName = m_TypeToName[ct];
            }
          } else if (auto *ne = dynamic_cast<const NewExpr *>(argExpr)) {
            concreteName = ne->Type;
          } else if (auto *ie = dynamic_cast<const InitStructExpr *>(argExpr)) {
            concreteName = ie->ShapeName;
          }
        }

        // 3. Construct Fat Pointer
        if (!concreteName.empty() && !traitName.empty()) {
          std::string vtableName = "_VTable_" + concreteName + "_" + traitName;
          llvm::GlobalVariable *vtable =
              m_Module->getGlobalVariable(vtableName);
          if (!vtable) {
            llvm::Type *voidPtrTy = llvm::PointerType::getUnqual(m_Context);
            llvm::ArrayType *arrTy = llvm::ArrayType::get(voidPtrTy, 0);
            vtable = new llvm::GlobalVariable(*m_Module, arrTy, true,
                                             llvm::GlobalValue::ExternalLinkage,
                                             nullptr, vtableName);
          }
          if (vtable) {
            llvm::Type *fatPtrTy = resolveType(targetArgType, false);
            llvm::Value *ctxPtr = m_Builder.CreateBitCast(
                val,
                llvm::PointerType::getUnqual(m_Context));
            llvm::Value *vtablePtr = m_Builder.CreateBitCast(
                vtable,
                llvm::PointerType::getUnqual(m_Context));

            llvm::Value *fatPtr = llvm::UndefValue::get(fatPtrTy);
            fatPtr = m_Builder.CreateInsertValue(fatPtr, ctxPtr, 0);
            fatPtr = m_Builder.CreateInsertValue(fatPtr, vtablePtr, 1);

            // Pass address of Fat Pointer (Struct)
            llvm::AllocaInst *tmp =
                createEntryBlockAlloca(fatPtrTy, nullptr, "dyn_tmp");
            m_Builder.CreateStore(fatPtr, tmp);
            val = tmp;
            // Skip standard casting if we handled it here?
            // paramType is ptr (to dyn struct). val is ptr (to dyn struct).
            // types match.
          }
        }
      }

      // FFI boundary automatic decay for cstr -> *char
      if (val && call->Args[i]->ResolvedType && call->Args[i]->ResolvedType->isShape()) {
          auto shp = std::static_pointer_cast<toka::ShapeType>(call->Args[i]->ResolvedType);
          if (shp->Name == "cstr") {
              std::string targetArgType =
                  (funcDecl && i < funcDecl->Args.size() ? funcDecl->Args[i].Type
                            : (extDecl && i < extDecl->Args.size() ? extDecl->Args[i].Type : ""));
              if (targetArgType.find("cstr") == std::string::npos) {
                  if (paramType->isPointerTy()) {
                      if (val->getType()->isPointerTy()) {
                          // If val is a pointer to the cstr struct, load the pointer field
                          llvm::Value *ptrGEP = m_Builder.CreateStructGEP(getLLVMType(call->Args[i]->ResolvedType), val, 0);
                          val = m_Builder.CreateLoad(m_Builder.getPtrTy(), ptrGEP);
                      } else {
                          // If val is the struct value itself, extract the first field
                          val = m_Builder.CreateExtractValue(val, 0);
                      }
                  }
              }
          }
      }

      if (val->getType() != paramType) {
        if (val->getType()->isIntegerTy() && paramType->isIntegerTy()) {
          val = m_Builder.CreateIntCast(val, paramType, true);
        } else if (val->getType()->isPointerTy() && paramType->isPointerTy()) {
          val = m_Builder.CreateBitCast(val, paramType);
        }
      }
    }

    argsV.push_back(val);
  }

  // [NEW] Native LLVM Atomics Intercept
  std::string fname = call->Callee;

  if (!fname.empty()) {
    if (fname == "__toka_str_raw_ptr" || fname == "__toka_bytes_raw_ptr") {
        llvm::Value *structPtr = argsV[0];
        if (!structPtr->getType()->isPointerTy()) {
            llvm::AllocaInst *tmp = createEntryBlockAlloca(structPtr->getType());
            m_Builder.CreateStore(structPtr, tmp);
            structPtr = tmp;
        }
        std::string structName = (fname == "__toka_str_raw_ptr" ? "str" : "bytes");
        llvm::StructType *structTy = nullptr;
        if (m_StructTypes.count(structName)) {
            structTy = m_StructTypes[structName];
        }
        if (!structTy) {
            structTy = llvm::StructType::get(m_Context, {m_Builder.getPtrTy(), getIntPtrTy()});
        }
        llvm::Value *gep = m_Builder.CreateStructGEP(structTy, structPtr, 0, "raw_ptr.gep");
        llvm::Value *ptr = m_Builder.CreateLoad(m_Builder.getPtrTy(), gep, "raw_ptr.load");
        return PhysEntity(ptr, fname == "__toka_str_raw_ptr" ? "*char" : "*byte", m_Builder.getPtrTy(), false);
    }
    if (fname == "__toka_coro_resume") {
        llvm::Function *resFn = llvm::Intrinsic::getOrInsertDeclaration(m_Module.get(), llvm::Intrinsic::coro_resume);
        m_Builder.CreateCall(resFn->getFunctionType(), resFn, argsV);
        return PhysEntity(llvm::ConstantInt::get(m_Builder.getInt32Ty(), 0), "void", m_Builder.getVoidTy(), false);
    }
    if (fname == "__toka_coro_done") {
        llvm::Function *doneFn = llvm::Intrinsic::getOrInsertDeclaration(m_Module.get(), llvm::Intrinsic::coro_done);
        llvm::Value *res = m_Builder.CreateCall(doneFn->getFunctionType(), doneFn, argsV);
        return PhysEntity(res, "bool", m_Builder.getInt1Ty(), false);
    }
    if (fname == "__toka_coro_destroy") {
        llvm::Function *destroyFn = llvm::Intrinsic::getOrInsertDeclaration(m_Module.get(), llvm::Intrinsic::coro_destroy);
        m_Builder.CreateCall(destroyFn->getFunctionType(), destroyFn, argsV);
        return PhysEntity(llvm::ConstantInt::get(m_Builder.getInt32Ty(), 0), "void", m_Builder.getVoidTy(), false);
    }
    if (fname.find("__toka_atomic_") == 0) {
      fname = fname.substr(14); // strip prefix
      
    // Helper to extract Ordering from Argument Value
    auto getOrder = [&](llvm::Value *v) -> std::pair<llvm::AtomicOrdering, bool> {
      if (auto *ci = llvm::dyn_cast<llvm::ConstantInt>(v)) {
        int tag = ci->getSExtValue();
        switch (tag) {
          case 0: return {llvm::AtomicOrdering::Monotonic, false}; // Relaxed
          case 1: return {llvm::AtomicOrdering::Release, false};
          case 2: return {llvm::AtomicOrdering::Acquire, false};
          case 3: return {llvm::AtomicOrdering::AcquireRelease, false};
          case 4: default: return {llvm::AtomicOrdering::SequentiallyConsistent, false};
        }
      }
      // Dynamic fallback
      return {llvm::AtomicOrdering::SequentiallyConsistent, true};
    };

    if (fname.find("fence") == 0) {
      if (fname == "fence_acquire") m_Builder.CreateFence(llvm::AtomicOrdering::Acquire);
      else if (fname == "fence_release") m_Builder.CreateFence(llvm::AtomicOrdering::Release);
      else if (argsV.size() > 0) m_Builder.CreateFence(getOrder(argsV.back()).first);
      else m_Builder.CreateFence(llvm::AtomicOrdering::SequentiallyConsistent);
      return llvm::ConstantInt::get(m_Builder.getInt32Ty(), 0);
    }

    if (fname.find("load") == 0) {
      auto order = getOrder(argsV.back());
      llvm::Type *valTy = callee->getFunctionType()->getReturnType();
      llvm::LoadInst *li = m_Builder.CreateLoad(valTy, argsV[0], "atomic_load");
      li->setAtomic(order.first);
      if (li->getOrdering() == llvm::AtomicOrdering::NotAtomic) li->setAtomic(llvm::AtomicOrdering::Monotonic);
      unsigned bits = valTy->getPrimitiveSizeInBits();
      if (bits == 0 && valTy->isPointerTy()) bits = 64;
      unsigned alignVal = bits / 8;
      if (alignVal == 0) alignVal = 4;
      li->setAlignment(llvm::Align(alignVal));
      return li;
    }

    if (fname.find("store") == 0) {
      auto order = getOrder(argsV.back());
      llvm::Type *valTy = argsV[1]->getType();
      llvm::StoreInst *si = m_Builder.CreateStore(argsV[1], argsV[0]);
      llvm::AtomicOrdering o = order.second ? llvm::AtomicOrdering::SequentiallyConsistent : order.first;
      if (o == llvm::AtomicOrdering::Acquire || o == llvm::AtomicOrdering::AcquireRelease) o = llvm::AtomicOrdering::Release; // Store cannot be Acquire or AcqRel
      if (o == llvm::AtomicOrdering::NotAtomic) o = llvm::AtomicOrdering::Monotonic;
      si->setAtomic(o);
      unsigned bits = valTy->getPrimitiveSizeInBits();
      if (bits == 0 && valTy->isPointerTy()) bits = 64;
      unsigned alignVal = bits / 8;
      if (alignVal == 0) alignVal = 4;
      si->setAlignment(llvm::Align(alignVal));
      return llvm::ConstantInt::get(m_Builder.getInt32Ty(), 0);
    }

    if (fname.find("compare_exchange") == 0) {
      auto success = getOrder(argsV[3]).first;
      auto fail = getOrder(argsV[4]).first;
      if (fail == llvm::AtomicOrdering::Release || fail == llvm::AtomicOrdering::AcquireRelease) fail = llvm::AtomicOrdering::Acquire;
      // LLVM CmpXchg Failure cannot be stronger than Success
      if (success == llvm::AtomicOrdering::Monotonic && fail != llvm::AtomicOrdering::Monotonic) fail = llvm::AtomicOrdering::Monotonic;
      if (success == llvm::AtomicOrdering::Release && fail != llvm::AtomicOrdering::Monotonic) fail = llvm::AtomicOrdering::Monotonic;
      if (success == llvm::AtomicOrdering::Acquire && fail == llvm::AtomicOrdering::SequentiallyConsistent) fail = llvm::AtomicOrdering::Acquire;

      llvm::Type *valTy = argsV[1]->getType();
      unsigned bits = valTy->getPrimitiveSizeInBits();
      if (bits == 0 && valTy->isPointerTy()) bits = 64;
      unsigned alignVal = bits / 8;
      if (alignVal == 0) alignVal = 4;
      llvm::Align align(alignVal);
      llvm::AtomicCmpXchgInst *cxi = m_Builder.CreateAtomicCmpXchg(argsV[0], argsV[1], argsV[2], llvm::MaybeAlign(align), success, fail);
      return cxi; // Exact {T, i1} signature match!
    }

    llvm::AtomicRMWInst::BinOp rop = llvm::AtomicRMWInst::Add;
    bool isRMW = true;
    if (fname.find("fetch_add") == 0) rop = llvm::AtomicRMWInst::Add;
    else if (fname.find("fetch_sub") == 0) rop = llvm::AtomicRMWInst::Sub;
    else if (fname.find("fetch_and") == 0) rop = llvm::AtomicRMWInst::And;
    else if (fname.find("fetch_or") == 0) rop = llvm::AtomicRMWInst::Or;
    else if (fname.find("fetch_xor") == 0) rop = llvm::AtomicRMWInst::Xor;
    else if (fname.find("swap") == 0) rop = llvm::AtomicRMWInst::Xchg;
    else isRMW = false;

    if (isRMW) {
      auto order = getOrder(argsV.back());
      llvm::Type *valTy = argsV[1]->getType();
      unsigned bits = valTy->getPrimitiveSizeInBits();
      if (bits == 0 && valTy->isPointerTy()) bits = 64;
      unsigned alignVal = bits / 8;
      if (alignVal == 0) alignVal = 4;
      llvm::Align align(alignVal);
      llvm::AtomicOrdering o = order.second ? llvm::AtomicOrdering::SequentiallyConsistent : order.first;
      if (o == llvm::AtomicOrdering::NotAtomic) o = llvm::AtomicOrdering::Monotonic;
      llvm::AtomicRMWInst *rmw = m_Builder.CreateAtomicRMW(rop, argsV[0], argsV[1], llvm::MaybeAlign(align), o);
      return rmw;
    }
  }
  }

  llvm::Value *sretAlloc = nullptr;
  if (isSRet) {
      llvm::Type *retLLVMTy = getLLVMType(call->ResolvedType);
      sretAlloc = createEntryBlockAlloca(retLLVMTy, nullptr, "sret.tmp");
      argsV.insert(argsV.begin(), sretAlloc);
  }

  llvm::CallInst *ci = m_Builder.CreateCall(callee->getFunctionType(), callee, argsV);

  if (!cededNullablePayloadShells.empty()) {
    llvm::Function *freeFn = m_Module->getFunction("free");
    if (!freeFn) {
      freeFn = llvm::Function::Create(
          llvm::FunctionType::get(m_Builder.getVoidTy(),
                                  {m_Builder.getPtrTy()}, false),
          llvm::Function::ExternalLinkage, "free", m_Module.get());
    }
    for (llvm::Value *shell : cededNullablePayloadShells)
      m_Builder.CreateCall(freeFn, {shell});
  }

  if (call->CallableReceiver == CallableReceiverMode::Consuming)
    suppressDropForMove(call->Callee);

  if (isSRet) {
      ci->addParamAttr(0, llvm::Attribute::get(m_Context, llvm::Attribute::StructRet, getLLVMType(call->ResolvedType)));
      return PhysEntity(sretAlloc, call->ResolvedType->getSoulName(), getLLVMType(call->ResolvedType), true);
  }

  if (isAsync) {
      if (!call->ResolvedType) {
          error(call, DiagID::ERR_CODEGEN_INTERNAL_CODEGEN_ERROR_ASYNC_CALL_MISS);
          return nullptr;
      }
      std::string tName = call->ResolvedType->toString();
      llvm::Type *handleTy = m_StructTypes[tName];
      
      // Wrap the raw coroutine handle pointer into the generated TaskHandle struct
      llvm::Value *sVal = llvm::UndefValue::get(handleTy);
      sVal = m_Builder.CreateInsertValue(sVal, ci, 0, "task.wrap");
      
    return PhysEntity(sVal, tName, handleTy, false);
  }

  // Source-level Unit is a real storable value, while its ordinary function
  // ABI intentionally remains LLVM void. Materialize the canonical Unit value
  // after the call when the result participates in another expression.
  if (call->ResolvedType && call->ResolvedType->isUnit() &&
      ci->getType()->isVoidTy()) {
    llvm::Type *unitTy = getLLVMType(call->ResolvedType);
    return PhysEntity(llvm::Constant::getNullValue(unitTy),
                      call->ResolvedType->toString(), unitTy, false);
  }

  return ci;
}

PhysEntity CodeGen::genUnwrapPropagationExpr(const UnwrapPropagationExpr *expr) {
  PhysEntity baseEnt = genExpr(expr->Base.get());
  llvm::Value *baseVal = baseEnt.load(m_Builder);
  if (!baseVal) return nullptr;

  if (!expr->Base->ResolvedType) {
    error(expr, DiagID::ERR_CODEGEN_PROPAGATION_EXPRESSION_LACKS_RESOLVED);
    return nullptr;
  }
  std::string soul = expr->Base->ResolvedType->getSoulName();

  llvm::StructType *baseStructTy = llvm::dyn_cast<llvm::StructType>(baseVal->getType());
  if (!baseStructTy || baseStructTy->getNumElements() < 2) {
    error(expr, DiagID::ERR_CODEGEN_INVALID_REPRESENTATION_FOR, soul);
    return nullptr;
  }

  llvm::Value *tagVal = m_Builder.CreateExtractValue(baseVal, {0}, "unwrap.tag");
  llvm::Value *isOk = m_Builder.CreateICmpEQ(tagVal, llvm::ConstantInt::get(tagVal->getType(), 1), "unwrap.is_ok");

  // [NEW] Destructive Match Mutation for ! Operator (Early Return / Unwrap)
  // The ! operator extracts the payload out of the container implicitly.
  // We must mutate the original container's tag to 'Moved' to prevent double-free
  // when the current scope ends or returns early.
  if (baseEnt.isAddress) {
      std::string baseShapeName = toka::Type::stripMorphology(expr->Base->ResolvedType->toString());
      if (baseShapeName.find("_M_") != std::string::npos) baseShapeName = baseShapeName.substr(0, baseShapeName.find("_M_"));
      if (m_Shapes.count(baseShapeName)) {
          int movedTag = -1;
          for (size_t i = 0; i < m_Shapes[baseShapeName]->Members.size(); ++i) {
              if (m_Shapes[baseShapeName]->Members[i].Name == "Moved") {
                  movedTag = m_Shapes[baseShapeName]->Members[i].TagValue != -1 ? m_Shapes[baseShapeName]->Members[i].TagValue : (int)i;
                  break;
              }
          }
          if (movedTag != -1) {
              llvm::Value *tagAddr = m_Builder.CreateStructGEP(baseVal->getType(), baseEnt.value, 0);
              m_Builder.CreateStore(llvm::ConstantInt::get(llvm::Type::getInt8Ty(m_Context), movedTag), tagAddr);
          }
      }
  }

  llvm::Function *f = m_Builder.GetInsertBlock()->getParent();
  llvm::BasicBlock *failBB = llvm::BasicBlock::Create(m_Context, "unwrap.fail", f);
  llvm::BasicBlock *succBB = llvm::BasicBlock::Create(m_Context, "unwrap.succ", f);

  m_Builder.CreateCondBr(isOk, succBB, failBB);

  // -- FAIL Path --
  m_Builder.SetInsertPoint(failBB);
  
  if (soul == "Option" || soul == "Option" || soul.find("Option_") == 0) {
    llvm::Type *targetRetTy = m_CurrentCoroRetTy ? m_CurrentCoroRetTy : (m_CurrentSRetTy ? m_CurrentSRetTy : f->getReturnType());
    llvm::Value *retVal = llvm::UndefValue::get(targetRetTy);
    if (targetRetTy->isStructTy() && targetRetTy->getStructNumElements() > 0) {
      llvm::Type *tagTy = targetRetTy->getStructElementType(0);
      retVal = m_Builder.CreateInsertValue(retVal, llvm::ConstantInt::get(tagTy, 0), {0});
    }
    
    executeScopeUnwinding(0); 
    
    if (m_CurrentCoroRetTy) {
       genCoroutineReturn(retVal);
    } else if (m_CurrentSRetPtr && m_CurrentSRetTy) {
       if (retVal->getType() != m_CurrentSRetTy) {
         llvm::Value *tempSrcPtr = m_Builder.CreatePointerCast(m_CurrentSRetPtr, llvm::PointerType::get(retVal->getType(), 0));
         m_Builder.CreateStore(retVal, tempSrcPtr);
       } else {
         m_Builder.CreateStore(retVal, m_CurrentSRetPtr);
       }
       m_Builder.CreateRetVoid();
    } else {
       m_Builder.CreateRet(retVal);
    }
  } else if (soul == "Result" || soul == "Result" || soul.find("Result_") == 0) {
    llvm::Value *unionData = m_Builder.CreateExtractValue(baseVal, {1}, "unwrap.err_data");

    llvm::Type *targetRetTy = m_CurrentCoroRetTy ? m_CurrentCoroRetTy : (m_CurrentSRetTy ? m_CurrentSRetTy : f->getReturnType());
    llvm::Value *retVal = llvm::UndefValue::get(targetRetTy);
    
    if (targetRetTy->isStructTy() && targetRetTy->getStructNumElements() >= 2) {
      llvm::Type *tagTy = targetRetTy->getStructElementType(0);
      retVal = m_Builder.CreateInsertValue(
          retVal, llvm::ConstantInt::get(tagTy, 0), {0});

      if (!expr->SourceErrorType || !expr->TargetErrorType) {
        error(expr, DiagID::ERR_CODEGEN_PROPAGATION_EXPRESSION_LACKS_RESOLVED);
        return nullptr;
      }

      llvm::Type *sourceErrTy = getLLVMType(expr->SourceErrorType);
      llvm::Type *targetErrTy = getLLVMType(expr->TargetErrorType);
      if (!sourceErrTy || !targetErrTy) {
        error(expr, DiagID::ERR_CODEGEN_INVALID_REPRESENTATION_FOR, soul);
        return nullptr;
      }

      llvm::AllocaInst *srcAlloc = createEntryBlockAlloca(
          unionData->getType(), nullptr, "unwrap.err.src");
      srcAlloc->setAlignment(
          m_Module->getDataLayout().getABITypeAlign(sourceErrTy));
      m_Builder.CreateStore(unionData, srcAlloc);
      llvm::Value *sourceError =
          m_Builder.CreateLoad(sourceErrTy, srcAlloc, "unwrap.err.value");
      llvm::Value *targetError = sourceError;

      if (expr->ErrorConversionFn) {
        const FunctionDecl *conversion = expr->ErrorConversionFn;
        std::string calleeName = conversion->CodegenName.empty()
                                     ? conversion->Name
                                     : conversion->CodegenName;
        llvm::Function *callee = m_Module->getFunction(calleeName);
        if (!callee)
          callee = genFunction(conversion, "", true);
        if (!callee) {
          error(expr, DiagID::ERR_CODEGEN_METHOD_NOT_FOUND_FOR_TYPE_MANGLED,
                "into_error", expr->SourceErrorType->toString(), calleeName);
          return nullptr;
        }

        bool conversionSRet = shouldReturnSRet(expr->TargetErrorType);
        size_t selfIndex = conversionSRet ? 1 : 0;
        std::vector<llvm::Value *> args;
        llvm::Value *convertedStorage = nullptr;
        if (conversionSRet) {
          convertedStorage = createEntryBlockAlloca(
              targetErrTy, nullptr, "unwrap.err.converted");
          args.push_back(convertedStorage);
        }

        llvm::Value *selfArg = sourceError;
        if (callee->arg_size() > selfIndex &&
            callee->getArg(selfIndex)->getType()->isPointerTy())
          selfArg = srcAlloc;
        args.push_back(selfArg);

        llvm::CallInst *converted = m_Builder.CreateCall(callee, args);
        if (conversionSRet) {
          converted->addParamAttr(
              0, llvm::Attribute::get(m_Context, llvm::Attribute::StructRet,
                                      targetErrTy));
          targetError = m_Builder.CreateLoad(
              targetErrTy, convertedStorage, "unwrap.err.converted.value");
        } else {
          targetError = converted;
        }
      }

      llvm::Type *dstUnionTy = targetRetTy->getStructElementType(1);
      llvm::AllocaInst *dstAlloc = createEntryBlockAlloca(
          dstUnionTy, nullptr, "unwrap.err.dst");
      dstAlloc->setAlignment(
          m_Module->getDataLayout().getABITypeAlign(targetErrTy));
      m_Builder.CreateStore(llvm::Constant::getNullValue(dstUnionTy), dstAlloc);
      m_Builder.CreateStore(targetError, dstAlloc);
      llvm::Value *newUnionData =
          m_Builder.CreateLoad(dstUnionTy, dstAlloc, "unwrap.err.packed");
      retVal = m_Builder.CreateInsertValue(retVal, newUnionData, {1});
    }

    executeScopeUnwinding(0); 

    if (m_CurrentCoroRetTy) {
       genCoroutineReturn(retVal);
    } else if (m_CurrentSRetPtr && m_CurrentSRetTy) {
       if (retVal->getType() != m_CurrentSRetTy) {
         llvm::Value *tempSrcPtr = m_Builder.CreatePointerCast(m_CurrentSRetPtr, llvm::PointerType::get(retVal->getType(), 0));
         m_Builder.CreateStore(retVal, tempSrcPtr);
       } else {
         m_Builder.CreateStore(retVal, m_CurrentSRetPtr);
       }
       m_Builder.CreateRetVoid();
    } else {
       m_Builder.CreateRet(retVal);
    }
  }

  // -- SUCCESS Path --
  m_Builder.SetInsertPoint(succBB);
  
  if (!expr->ResolvedType) return nullptr;
  
  llvm::Type *payloadTy = getLLVMType(expr->ResolvedType);
  if (!payloadTy) return nullptr;
  
  llvm::Value *unionData = m_Builder.CreateExtractValue(baseVal, {1}, "unwrap.succ_data");
  llvm::Value *succAlloc = createEntryBlockAlloca(unionData->getType(), nullptr, "unwrap.succ.src");
  m_Builder.CreateStore(unionData, succAlloc);
  
  llvm::Value *castPtr = m_Builder.CreateBitCast(succAlloc, llvm::PointerType::getUnqual(m_Context));
  llvm::Value *payload = m_Builder.CreateLoad(payloadTy, castPtr, "unwrap.payload");
  
  return PhysEntity(payload, expr->ResolvedType->toString(), payloadTy, false);
}

PhysEntity CodeGen::genPostfixExpr(const PostfixExpr *post) {
  if (post->Op == TokenType::TokenWrite) {
    return genExpr(post->LHS.get());
  }
  if (post->Op == TokenType::DoubleQuestion) {
    PhysEntity lhs_pe = genExpr(post->LHS.get());
    llvm::Value *lhs_val = lhs_pe.value;
    if (!lhs_val)
      return nullptr;

    bool isNullableSoul = false;
    llvm::Type *innerType = nullptr;
    if (lhs_pe.irType && lhs_pe.irType->isStructTy() &&
        lhs_pe.irType->getStructNumElements() == 2 &&
        lhs_pe.irType->getStructElementType(1)->isIntegerTy(1)) {
      isNullableSoul = true;
      innerType = lhs_pe.irType->getStructElementType(0);
    }

    if (lhs_pe.isAddress) {
      if (isNullableSoul) {
        // L-Value Propagation for Soul: { T, i1 }*
        // 1. GEP to isPresent (index 1) and check
        llvm::Value *isPresentPtr = m_Builder.CreateStructGEP(
            lhs_pe.irType, lhs_val, 1, "soul.isPresentPtr");
        llvm::Value *isPresent = m_Builder.CreateLoad(
            m_Builder.getInt1Ty(), isPresentPtr, "soul.isPresent");
        genNullCheck(isPresent, post);
        // 2. GEP to data (index 0) and return its address
        llvm::Value *dataPtr = m_Builder.CreateStructGEP(lhs_pe.irType, lhs_val,
                                                         0, "soul.dataPtr");
        llvm::Type *resTy = getLLVMType(post->ResolvedType);
        return PhysEntity(dataPtr, "", resTy, true);
      } else {
        // Raw Pointer case: T**
        llvm::Value *ptrVal =
            m_Builder.CreateLoad(lhs_pe.irType, lhs_val, "nn.load");
        genNullCheck(ptrVal, post);
        llvm::Type *resTy = getLLVMType(post->ResolvedType);
        return PhysEntity(ptrVal, "", resTy, true);
      }
    } else {
      // R-Value (already loaded / value type)
      if (isNullableSoul) {
        llvm::Value *isPresent =
            m_Builder.CreateExtractValue(lhs_val, {1}, "soul.isPresent");
        genNullCheck(isPresent, post);
        llvm::Value *data =
            m_Builder.CreateExtractValue(lhs_val, {0}, "soul.data");
        return PhysEntity(data, lhs_pe.typeName, innerType, false);
      } else {
        genNullCheck(lhs_val, post);
        return lhs_pe;
      }
    }
  }

  llvm::Value *addr = genAddr(post->LHS.get());
  if (!addr)
    return nullptr;
  llvm::Type *type = nullptr;
  if (auto *var = dynamic_cast<const VariableExpr *>(post->LHS.get())) {
    if (m_Symbols.count(var->Name))
      type = m_Symbols[var->Name].soulType;
  } else if (auto *gep = llvm::dyn_cast<llvm::GetElementPtrInst>(addr)) {
    type = gep->getResultElementType();
  } else if (auto *alloca = llvm::dyn_cast<llvm::AllocaInst>(addr)) {
    type = alloca->getAllocatedType();
  }
  if (!type)
    return nullptr;
  llvm::Value *oldVal = m_Builder.CreateLoad(type, addr, "post_old");
  llvm::Value *newVal;
  if (post->Op == TokenType::PlusPlus)
    newVal = m_Builder.CreateAdd(oldVal, llvm::ConstantInt::get(type, 1),
                                 "postinc_new");
  else
    newVal = m_Builder.CreateSub(oldVal, llvm::ConstantInt::get(type, 1),
                                 "postdec_new");
  m_Builder.CreateStore(newVal, addr);
  return oldVal;
}

PhysEntity CodeGen::genPassExpr(const PassExpr *pe) {
  llvm::Value *val = nullptr;
  if (pe->Value) {
    val = genExpr(pe->Value.get()).load(m_Builder);
  }

  if (!m_CFStack.empty()) {
    // `pass` yields from the nearest value-producing control-flow
    // expression.  A plain nested `if` may have no result slot of its own,
    // for example inside an `else if` branch that validates a Result before
    // passing it onward.  In that case, routing to the innermost CF frame
    // loses the value; walk outward to the first frame that owns a slot.
    auto targetIt = m_CFStack.rbegin();
    while (targetIt != m_CFStack.rend() && !targetIt->ResultAddr)
      ++targetIt;
    auto target = targetIt != m_CFStack.rend() ? *targetIt : m_CFStack.back();
    executeScopeUnwinding(target.ScopeDepth);
    if (val && target.ResultAddr) {
      m_Builder.CreateStore(val, target.ResultAddr);
    } else if (!pe->Value && target.ResultAddr) {
      // pass none
      llvm::Type *allocaTy =
          llvm::cast<llvm::AllocaInst>(target.ResultAddr)->getAllocatedType();
      m_Builder.CreateStore(llvm::Constant::getNullValue(allocaTy),
                            target.ResultAddr);
    }
    if (target.BreakTarget)
      m_Builder.CreateBr(target.BreakTarget);
    m_Builder.ClearInsertionPoint();
  }
  return nullptr;
}

PhysEntity CodeGen::genCedeExpr(const CedeExpr *ce) {
  // Cede is a move semantic marker checked heavily in Sema. 
  // In LLVM IR, we evaluate it to the underlying value explicitly transferring ownership.
  if (ce->Value) {
    const Expr *directSource = ce->Value.get();
    while (auto *cast = dynamic_cast<const CastExpr *>(directSource))
      directSource = cast->Expression.get();

    const VariableExpr *ve = dynamic_cast<const VariableExpr *>(directSource);
    const UnaryExpr *ue = nullptr;
    if (!ve) {
      if ((ue = dynamic_cast<const UnaryExpr *>(directSource))) {
        directSource = ue->RHS.get();
        ve = dynamic_cast<const VariableExpr *>(directSource);
      } else if (auto *se = dynamic_cast<const SpreadExpr *>(directSource)) {
        ve = dynamic_cast<const VariableExpr *>(se->Base.get());
      }
    }

    bool isShared = false;
    if ((ce->Value->ResolvedType && ce->Value->ResolvedType->isSharedPtr()) ||
        (directSource->ResolvedType && directSource->ResolvedType->isSharedPtr())) {
      isShared = true;
    } else if (ve) {
      std::string baseName = Type::stripMorphology(ve->Name);
      if (m_Symbols.count(baseName) && m_Symbols[baseName].morphology == Morphology::Shared) {
        isShared = true;
      }
    } else if (ue && ue->Op == TokenType::Tilde && ue->RHS && ue->RHS->ResolvedType && ue->RHS->ResolvedType->isSharedPtr()) {
      isShared = true;
    }

    if (ve) {
      suppressDropForMove(ve->Name);
    } else if (auto *member = dynamic_cast<const MemberExpr *>(directSource)) {
      suppressDropForPartialMove(member);
    } else if (auto *index = dynamic_cast<const ArrayIndexExpr *>(directSource)) {
      suppressDropForPartialMove(index);
    }

    if (isShared) {
      const Expr *targetExpr = (ue && ue->Op == TokenType::Tilde) ? ue->RHS.get() : (ve ? ve : ce->Value.get());
      llvm::Value *identityAddr = emitHandleAddr(targetExpr);
      if (identityAddr) {
        llvm::Type *ptrTy = llvm::PointerType::getUnqual(m_Context);
        llvm::StructType *handleTy = llvm::StructType::get(m_Context, {ptrTy, ptrTy});

        // 1. Load current 16-byte handle value WITHOUT calling emitAcquire (NO RETAIN!)
        llvm::Value *val = m_Builder.CreateLoad(handleTy, identityAddr, "share.cede_val");

        // 2. Zero out the source slot so caller/source cannot decref it
        llvm::Value *nullHandle = llvm::Constant::getNullValue(handleTy);
        m_Builder.CreateStore(nullHandle, identityAddr);

        std::string typeName = targetExpr->ResolvedType ? targetExpr->ResolvedType->toString() : "";
        return PhysEntity(val, typeName, handleTy, false);
      }
    }

    llvm::Type *targetTy = ce->ResolvedType ? getLLVMType(ce->ResolvedType)
                                             : nullptr;

    // A narrowed member/index keeps its declared nullable carrier in storage
    // even though Sema presents the exact guarded path as T.  Materializing
    // through genExpr would use that refined T as the load type.  Recover the
    // physical GEP element type instead, then extract the carrier payload.
    if ((dynamic_cast<const MemberExpr *>(directSource) ||
         dynamic_cast<const ArrayIndexExpr *>(directSource)) &&
        targetTy) {
      if (llvm::Value *sourceAddr = genAddr(directSource)) {
        llvm::Type *sourceTy = nullptr;
        if (auto *gep = llvm::dyn_cast<llvm::GetElementPtrInst>(sourceAddr))
          sourceTy = gep->getResultElementType();
        if (auto *carrier = llvm::dyn_cast_or_null<llvm::StructType>(sourceTy)) {
          if (carrier->getNumElements() == 2 &&
              carrier->getElementType(0) == targetTy &&
              carrier->getElementType(1)->isIntegerTy(1)) {
            llvm::Value *stored =
                m_Builder.CreateLoad(sourceTy, sourceAddr, "cede.carrier");
            llvm::Value *payload =
                m_Builder.CreateExtractValue(stored, 0, "cede.nonnull");
            return PhysEntity(payload, ce->ResolvedType->toString(), targetTy,
                              false);
          }
        }
      }
    }

    PhysEntity moved = genExpr(ce->Value.get());
    llvm::Value *movedValue = moved.load(m_Builder);

    // Sema may refine an exact nullable path in a proven non-null branch.
    // The storage retains its `{T, present}` carrier, so lower the cede by
    // extracting T only when the semantic result is the carrier's payload.
    // This preserves the physical representation for nullable destinations
    // while keeping a guarded `cede values[i]` ABI-compatible with `T`.
    if (movedValue && targetTy && movedValue->getType() != targetTy) {
      if (auto *carrier = llvm::dyn_cast<llvm::StructType>(movedValue->getType())) {
        if (carrier->getNumElements() == 2 &&
            carrier->getElementType(0) == targetTy &&
            carrier->getElementType(1)->isIntegerTy(1)) {
          llvm::Value *payload =
              m_Builder.CreateExtractValue(movedValue, 0, "cede.nonnull");
          return PhysEntity(payload, ce->ResolvedType->toString(), targetTy,
                            false);
        }
      }
    }
    return moved;
  }
  return {};
}

PhysEntity CodeGen::genBreakExpr(const BreakExpr *be) {
  llvm::Value *val = nullptr;
  if (be->Value)
    val = genExpr(be->Value.get()).load(m_Builder);

  CFInfo *target = nullptr;
  if (be->TargetLabel.empty()) {
    for (auto it = m_CFStack.rbegin(); it != m_CFStack.rend(); ++it) {
      if (it->ContinueTarget != nullptr) {
        target = &(*it);
        break;
      }
    }
  } else {
    for (auto it = m_CFStack.rbegin(); it != m_CFStack.rend(); ++it) {
      if (it->Label == be->TargetLabel) {
        target = &(*it);
        break;
      }
    }
  }

  if (target) {
    executeScopeUnwinding(target->ScopeDepth);
    if (val && target->ResultAddr)
      m_Builder.CreateStore(val, target->ResultAddr);
    if (target->BreakTarget)
      m_Builder.CreateBr(target->BreakTarget);
    m_Builder.ClearInsertionPoint();
  }
  return nullptr;
}

PhysEntity CodeGen::genContinueExpr(const ContinueExpr *ce) {
  CFInfo *target = nullptr;
  if (ce->TargetLabel.empty()) {
    for (auto it = m_CFStack.rbegin(); it != m_CFStack.rend(); ++it) {
      if (it->ContinueTarget != nullptr) {
        target = &(*it);
        break;
      }
    }
  } else {
    for (auto it = m_CFStack.rbegin(); it != m_CFStack.rend(); ++it) {
      if (it->Label == ce->TargetLabel) {
        target = &(*it);
        break;
      }
    }
  }
  if (target && target->ContinueTarget) {
    executeScopeUnwinding(target->ScopeDepth);
    m_Builder.CreateBr(target->ContinueTarget);
    m_Builder.ClearInsertionPoint();
  }
  return nullptr;
}

PhysEntity CodeGen::genUnsafeExpr(const UnsafeExpr *ue) {
  return genExpr(ue->Expression.get());
}

PhysEntity CodeGen::genInitStructExpr(const InitStructExpr *init) {
  std::string shapeName = init->ShapeName;
  if (init->ResolvedType && init->ResolvedType->isShape()) {
    auto shapeType = std::dynamic_pointer_cast<ShapeType>(init->ResolvedType);
    shapeName = shapeType && shapeType->Decl &&
                        !shapeType->Decl->CodegenName.empty()
                    ? shapeType->Decl->CodegenName
                    : init->ResolvedType->getSoulName();
  }
  if (verboseMode) {
    std::cerr << "[DEBUG] genInitStructExpr shapeName=" << shapeName
              << " insertBlock=" << (m_Builder.GetInsertBlock() ? m_Builder.GetInsertBlock()->getName().str() : "NULL") << std::endl;
  }

  // Suppress drop for any variables ceded by base.*
  for (const auto &varName : init->CededBases) {
    suppressDropForMove(varName);
  }

  llvm::StructType *st = m_StructTypes[shapeName];
  if (!st) {
    error(init, DiagID::ERR_CODEGEN_UNKNOWN_STRUCT_TYPE, shapeName);
    return nullptr;
  }

  llvm::Value *alloca =
      createEntryBlockAlloca(st, nullptr, init->ShapeName + "_init");
  auto &fields = m_StructFieldNames[shapeName];

  for (const auto &f : init->Members) {
    int idx = -1;
    for (int i = 0; i < (int)fields.size(); ++i) {
      std::string fn = fields[i];
      while (!fn.empty() && (fn[0] == '^' || fn[0] == '*' || fn[0] == '&' ||
                             fn[0] == '#' || fn[0] == '~' || fn[0] == '!'))
        fn = fn.substr(1);
      while (!fn.empty() &&
             (fn.back() == '#' || fn.back() == '?' || fn.back() == '!'))
        fn.pop_back();

      if (fn == f.first) {
        idx = i;
        break;
      }

      // Try stripping the initializer name as well
      std::string initName = f.first;
      while (!initName.empty() &&
             (initName[0] == '^' || initName[0] == '*' || initName[0] == '&' ||
              initName[0] == '#' || initName[0] == '~' || initName[0] == '!' ||
              initName[0] == '?'))
        initName = initName.substr(1);
      while (!initName.empty() &&
             (initName.back() == '#' || initName.back() == '?' ||
              initName.back() == '!'))
        initName.pop_back();

      if (fn == initName) {
        idx = i;
        break;
      }
    }
    if (idx == -1) {
      error(init, DiagID::ERR_CODEGEN_UNKNOWN_FIELD, f.first);
      return nullptr;
    }

    llvm::Value *fieldVal = nullptr;

    // [Fix] ShapeKind Aware Type Lookup
    llvm::Type *elemTy = nullptr;
    std::shared_ptr<Type> fieldType;
    auto kind = ShapeKind::Struct;
    if (m_Shapes.count(shapeName)) {
      kind = m_Shapes[shapeName]->Kind;
      if (idx >= 0 && idx < static_cast<int>(m_Shapes[shapeName]->Members.size()))
        fieldType = m_Shapes[shapeName]->Members[idx].ResolvedType;
      if (kind == ShapeKind::Union) {
        auto sh = m_Shapes[shapeName];
        if (idx >= 0 && idx < (int)sh->Members.size()) {
          if (sh->Members[idx].ResolvedType)
            elemTy = getLLVMType(sh->Members[idx].ResolvedType);
          else
            elemTy = resolveType(sh->Members[idx].Type, false);
        }
      } else {
        elemTy = st->getElementType(idx);
      }
    } else {
      elemTy = st->getElementType(idx);
    }
    if (!elemTy)
      elemTy = llvm::Type::getInt8Ty(m_Context);

    if (dynamic_cast<const UnsetExpr *>(f.second.get())) {
      fieldVal = elemTy->isPointerTy() ? llvm::Constant::getNullValue(elemTy)
                                       : llvm::UndefValue::get(elemTy);
    } else {
      fieldVal = genExpr(f.second.get()).load(m_Builder);
      // [Chapter 6 Extension] Nullable Soul Wrap for Init
      if (fieldVal && fieldVal->getType() != elemTy && elemTy->isStructTy() &&
          elemTy->getStructNumElements() == 2 &&
          elemTy->getStructElementType(1)->isIntegerTy(1)) {
        if (fieldVal->getType() == elemTy->getStructElementType(0)) {
          llvm::Value *wrapped = llvm::UndefValue::get(elemTy);
          wrapped = m_Builder.CreateInsertValue(wrapped, fieldVal, {0});
          wrapped = m_Builder.CreateInsertValue(
              wrapped,
              llvm::ConstantInt::get(llvm::Type::getInt1Ty(m_Context), 1), {1});
          fieldVal = wrapped;
        } else if (dynamic_cast<const NoneExpr *>(f.second.get()) ||
                   fieldVal->getType()->isPointerTy()) {
          llvm::Value *wrapped = llvm::UndefValue::get(elemTy);
          wrapped = m_Builder.CreateInsertValue(
              wrapped,
              llvm::Constant::getNullValue(elemTy->getStructElementType(0)),
              {0});
          wrapped = m_Builder.CreateInsertValue(
              wrapped,
              llvm::ConstantInt::get(llvm::Type::getInt1Ty(m_Context), 0), {1});
          fieldVal = wrapped;
        }
      }
    }

    if (!fieldVal)
      return nullptr;

    // A fresh unique allocation assigned to a shared field owns only its data
    // pointer. The field stores a shared { data, refcount } handle, so create
    // that first handle before the aggregate is materialized.
    if (fieldType && fieldType->isSharedPtr() &&
        fieldVal->getType()->isPointerTy() &&
        isOwnedUniquePromotionSource(f.second.get())) {
      TokaSymbol sharedField;
      sharedField.morphology = Morphology::Shared;
      fieldVal = emitPromotion(fieldVal, elemTy, sharedField);
    }

    llvm::Value *fieldAddr = nullptr;
    if (m_Shapes.count(shapeName)) {
      auto kind = m_Shapes[shapeName]->Kind;
      if (kind == ShapeKind::Union) {
        // Legacy bare union: bitcast base to member pointer type
        fieldAddr = m_Builder.CreateBitCast(
            alloca, llvm::PointerType::getUnqual(m_Context));
      } else if (kind == ShapeKind::Enum) {
        // Tagged enum: store tag and payload
        // 1. Tag
        llvm::Value *tagAddr =
            m_Builder.CreateStructGEP(st, alloca, 0, "tag_addr");
        m_Builder.CreateStore(m_Builder.getInt8(idx), tagAddr);
        // 2. Payload
        if (st->getNumElements() > 1) {
          llvm::Value *payloadAddr =
              m_Builder.CreateStructGEP(st, alloca, 1, "payload_addr");
          fieldAddr = m_Builder.CreateBitCast(
              payloadAddr, llvm::PointerType::getUnqual(m_Context));
        }
      } else {
        fieldAddr =
            m_Builder.CreateStructGEP(st, alloca, idx, "field_" + f.first);
      }
    } else {
      fieldAddr =
          m_Builder.CreateStructGEP(st, alloca, idx, "field_" + f.first);
    }

    const bool ownsFreshSharedHandle = f.second && f.second->ResolvedType &&
                                       f.second->ResolvedType->isSharedPtr() &&
                                       isOwnedUniquePromotionSource(f.second.get());
    if (!ownsFreshSharedHandle && f.second && f.second->ResolvedType &&
        f.second->ResolvedType->isSharedPtr()) {
      emitAcquire(fieldVal, f.second->ResolvedType->getPointeeType());
    }

    if (fieldVal && !fieldVal->getType()->isVoidTy()) {
      m_Builder.CreateStore(fieldVal, fieldAddr);
    }
  }

  return m_Builder.CreateLoad(st, alloca);
}

PhysEntity CodeGen::genNewExpr(const NewExpr *newExpr) {
  llvm::Type *type = nullptr;
  if (newExpr->ResolvedType) {
    auto rt = newExpr->ResolvedType;
    if (rt->isPointer() || rt->isSmartPointer()) {
      if (auto ptr = std::dynamic_pointer_cast<PointerType>(rt)) {
        rt = ptr->PointeeType;
      }
    }
    type = getLLVMType(rt);
  } else {
    type = resolveType(newExpr->Type, false);
  }

  if (!type)
    return nullptr;

  // Size of type
  const llvm::DataLayout &dl = m_Module->getDataLayout();
  uint64_t size = dl.getTypeAllocSize(type);

  // Call malloc
  llvm::Function *mallocFn = m_Module->getFunction("malloc");
  if (!mallocFn) {
    // Attempt lib_malloc or just declare malloc
    llvm::Type *sizeTy = getIntPtrTy();
    llvm::Type *ptrTy = m_Builder.getPtrTy();
    mallocFn = llvm::Function::Create(
        llvm::FunctionType::get(ptrTy, {sizeTy}, false),
        llvm::Function::ExternalLinkage, "malloc", m_Module.get());
  }

  llvm::Value *sizeVal =
      llvm::ConstantInt::get(getIntPtrTy(), size);

  llvm::Value *arrayCount = nullptr;
  if (newExpr->ArraySize) {
    llvm::Value *count = genExpr(newExpr->ArraySize.get()).load(m_Builder);
    if (count->getType() != getIntPtrTy()) {
      count = m_Builder.CreateIntCast(count, getIntPtrTy(), false);
    }
    arrayCount = count;
    sizeVal = m_Builder.CreateMul(sizeVal, count);
  }

  llvm::CallInst *voidPtr =
      m_Builder.CreateCall(mallocFn, sizeVal, "new_alloc");
  markMemoryEvent(voidPtr, "allocate");

  // In LLVM 17 with opaque pointers, we just use the pointer.
  // But we need to handle initialization.
  llvm::Value *heapPtr = voidPtr;

  if (newExpr->Initializer && (!newExpr->ResolvedType || newExpr->ResolvedType->toString().find("Uninit<") == std::string::npos)) {
    llvm::Value *initVal = genExpr(newExpr->Initializer.get()).load(m_Builder);
    if (initVal) {
      if (initVal->getType() != type) {
        // Attempt cast
        if (initVal->getType()->isIntegerTy() && type->isIntegerTy()) {
          initVal = m_Builder.CreateIntCast(initVal, type, true);
        }
      }
      
      if (arrayCount) {
        // Loop to initialize all elements
        llvm::BasicBlock *preHeaderBB = m_Builder.GetInsertBlock();
        llvm::Function *F = preHeaderBB->getParent();
        llvm::BasicBlock *loopBB = llvm::BasicBlock::Create(m_Context, "new_init_loop", F);
        llvm::BasicBlock *afterBB = llvm::BasicBlock::Create(m_Context, "new_init_after", F);

        m_Builder.CreateBr(loopBB);
        m_Builder.SetInsertPoint(loopBB);

        llvm::PHINode *iVar = m_Builder.CreatePHI(getIntPtrTy(), 2, "i");
        iVar->addIncoming(llvm::ConstantInt::get(getIntPtrTy(), 0), preHeaderBB);

        // GEP to element
        llvm::Value *elemPtr = m_Builder.CreateInBoundsGEP(type, heapPtr, iVar);
        m_Builder.CreateStore(initVal, elemPtr);

        llvm::Value *nextI = m_Builder.CreateAdd(iVar, llvm::ConstantInt::get(getIntPtrTy(), 1));
        llvm::Value *cond = m_Builder.CreateICmpULT(nextI, arrayCount);
        iVar->addIncoming(nextI, loopBB);

        m_Builder.CreateCondBr(cond, loopBB, afterBB);
        m_Builder.SetInsertPoint(afterBB);
      } else {
        m_Builder.CreateStore(initVal, heapPtr);
      }
    }
  }

  if (newExpr->ResolvedType && newExpr->ResolvedType->isFatPointer()) {
    llvm::Type *fatTy = getLLVMType(newExpr->ResolvedType);
    llvm::Value *fatVal = llvm::UndefValue::get(fatTy);
    fatVal = m_Builder.CreateInsertValue(fatVal, heapPtr, {0});
    llvm::Value *lenVal = arrayCount ? arrayCount : llvm::ConstantInt::get(getIntPtrTy(), 1);
    fatVal = m_Builder.CreateInsertValue(fatVal, lenVal, {1});
    return fatVal;
  }

  return heapPtr;
}
PhysEntity CodeGen::genArrayExpr(const ArrayExpr *expr) {
  if (expr->Elements.empty())
    return nullptr;

  std::vector<llvm::Constant *> consts;
  std::vector<llvm::Value *> values;
  bool allConst = true;
  llvm::Type *declaredElemTy = nullptr;
  if (expr->ResolvedType && expr->ResolvedType->isArray()) {
    declaredElemTy =
        getLLVMType(expr->ResolvedType->getArrayElementType());
  }

  for (auto &e : expr->Elements) {
    PhysEntity v_ent = genExpr(e.get()).load(m_Builder);
    llvm::Value *v = v_ent.load(m_Builder);
    if (!v)
      return nullptr;
    if (declaredElemTy && v->getType() != declaredElemTy &&
        declaredElemTy->isStructTy() &&
        declaredElemTy->getStructNumElements() == 2 &&
        declaredElemTy->getStructElementType(0) == v->getType() &&
        declaredElemTy->getStructElementType(1)->isIntegerTy(1)) {
      llvm::Value *wrapped = llvm::UndefValue::get(declaredElemTy);
      wrapped = m_Builder.CreateInsertValue(wrapped, v, {0});
      v = m_Builder.CreateInsertValue(
          wrapped, llvm::ConstantInt::getTrue(m_Context), {1});
    }
    values.push_back(v);
    if (auto *c = llvm::dyn_cast<llvm::Constant>(v)) {
      consts.push_back(c);
    } else {
      allConst = false;
    }
  }

  llvm::Type *elemTy = values[0]->getType();
  llvm::ArrayType *arrTy = llvm::ArrayType::get(elemTy, values.size());

  std::string elemTypeName = "i32"; // default
  if (!values.empty()) {
    if (m_TypeToName.count(elemTy)) {
      elemTypeName = m_TypeToName[elemTy];
    }
  }
  std::string arrayTypeName =
      "[" + elemTypeName + "; " + std::to_string(values.size()) + "]";

  if (allConst) {
    return PhysEntity(llvm::ConstantArray::get(arrTy, consts), arrayTypeName,
                      arrTy, false);
  }

  llvm::Value *val = llvm::UndefValue::get(arrTy);
  for (size_t i = 0; i < values.size(); ++i) {
    llvm::Value *elt = values[i];
    if (elt->getType() != elemTy) {
      // Minimal cast attempt for safety
      if (elt->getType()->isIntegerTy() && elemTy->isIntegerTy())
        elt = m_Builder.CreateIntCast(elt, elemTy, true);
      else if (elt->getType()->isPointerTy() && elemTy->isPointerTy())
        elt = m_Builder.CreateBitCast(elt, elemTy);
    }
    val = m_Builder.CreateInsertValue(val, elt, i);
  }
  return PhysEntity(val, arrayTypeName, arrTy, false);
}

PhysEntity CodeGen::genAnonymousRecordExpr(const AnonymousRecordExpr *expr) {
  std::string uniqueName = expr->AssignedTypeName;
  if (uniqueName.empty()) {
    error(expr, DiagID::ERR_CODEGEN_ANONYMOUS_RECORD_MISSING_TYPE_NAME);
    return nullptr;
  }

  llvm::Type *recType = nullptr;
  if (expr->ResolvedType) {
    recType = getLLVMType(expr->ResolvedType);
  } else {
    recType = resolveType(uniqueName, false);
    if (!recType && m_StructTypes.count(uniqueName)) {
      recType = m_StructTypes[uniqueName];
    }
  }

  if (!recType) {
    error(expr, DiagID::ERR_CODEGEN_ANONYMOUS_RECORD_TYPE_NOT_FOUND, uniqueName);
    return nullptr;
  }

  llvm::Value *alloca = createEntryBlockAlloca(recType, nullptr, "anon_rec");

  for (size_t i = 0; i < expr->Fields.size(); ++i) {
    PhysEntity val_ent = genExpr(expr->Fields[i].second.get()).load(m_Builder);
    llvm::Value *val = val_ent.load(m_Builder);
    if (!val)
      return nullptr;

    // GEP to element i (Struct layout matches Fields order)
    llvm::Value *ptr = m_Builder.CreateStructGEP(recType, alloca, i);

    // [Constitution] Shared Pointer RC Acquisition for Anonymous Records
    if (expr->Fields[i].second->ResolvedType &&
        expr->Fields[i].second->ResolvedType->isSharedPtr()) {
      emitAcquire(val, expr->Fields[i].second->ResolvedType->getPointeeType());
    }

    m_Builder.CreateStore(val, ptr);
  }

  return m_Builder.CreateLoad(recType, alloca);
}

llvm::Constant *CodeGen::genConstant(const Expr *expr, llvm::Type *targetType) {
  if (auto *num = dynamic_cast<const NumberExpr *>(expr)) {
    if (targetType && targetType->isIntegerTy()) {
      return llvm::ConstantInt::get(targetType, num->Value);
    }
    return llvm::ConstantInt::get(llvm::Type::getInt32Ty(m_Context),
                                  num->Value);
  }

  if (auto *b = dynamic_cast<const BoolExpr *>(expr)) {
    return llvm::ConstantInt::get(llvm::Type::getInt1Ty(m_Context),
                                  b->Value ? 1 : 0);
  }

  if (auto *flt = dynamic_cast<const FloatExpr *>(expr)) {
    if (targetType && targetType->isFloatingPointTy())
      return llvm::ConstantFP::get(targetType, flt->Value);
    return llvm::ConstantFP::get(llvm::Type::getFloatTy(m_Context), flt->Value);
  }

  if (dynamic_cast<const NoneExpr *>(expr) ||
      dynamic_cast<const NullExpr *>(expr)) {
    if (targetType && targetType->isStructTy() &&
        targetType->getStructNumElements() == 2 &&
        targetType->getStructElementType(1)->isIntegerTy(1)) {
      // Nullable Soul Wrapper constant for none/nullptr
      llvm::Type *baseTy = targetType->getStructElementType(0);
      return llvm::ConstantStruct::get(
          (llvm::StructType *)targetType,
          {llvm::Constant::getNullValue(baseTy),
           llvm::ConstantInt::get(llvm::Type::getInt1Ty(m_Context), 0)});
    }
    return llvm::ConstantPointerNull::get(m_Builder.getPtrTy());
  }

  if (auto *rec = dynamic_cast<const AnonymousRecordExpr *>(expr)) {
    std::string uniqueName = rec->AssignedTypeName;
    if (uniqueName.empty())
      return nullptr;

    if (!m_StructTypes.count(uniqueName))
      return nullptr;

    llvm::StructType *st = m_StructTypes[uniqueName];
    std::vector<llvm::Constant *> fields;
    for (size_t i = 0; i < rec->Fields.size(); ++i) {
      llvm::Type *elemTy = st->getElementType(i);
      llvm::Constant *c = genConstant(rec->Fields[i].second.get(), elemTy);
      if (!c) {
        // Fallback: If we can't generate constant, maybe return null (error
        // upstream)
        return nullptr;
      }
      fields.push_back(c);
    }
    return llvm::ConstantStruct::get(st, fields);
  }

  if (auto *unary = dynamic_cast<const UnaryExpr *>(expr)) {
    if (unary->Op == TokenType::Minus) {
      llvm::Constant *rhs = genConstant(unary->RHS.get(), targetType);
      if (rhs)
        return llvm::ConstantExpr::getNeg(rhs);
    }
  }

  if (auto *cast = dynamic_cast<const CastExpr *>(expr)) {
    llvm::Type *destTy = resolveType(cast->TargetType, false);
    if (!destTy)
      return nullptr;

    llvm::Constant *rhs = genConstant(cast->Expression.get());
    if (!rhs)
      return nullptr;

    if (rhs->getType() == destTy)
      return rhs;

    return nullptr;
  }

  return nullptr;
}

PhysEntity CodeGen::genRepeatedArrayExpr(const RepeatedArrayExpr *expr) {
  PhysEntity val_ent = genExpr(expr->Value.get());
  llvm::Value *val = val_ent.load(m_Builder);
  if (!val)
    return nullptr;
  uint64_t count = 0;

  if (auto *num = dynamic_cast<const NumberExpr *>(expr->Count.get())) {
    count = num->Value;
  } else if (auto *var =
                 dynamic_cast<const VariableExpr *>(expr->Count.get())) {
    if (var->HasConstantValue) {
      count = var->ConstantValue;
    } else {
      error(expr, DiagID::ERR_CODEGEN_REPEAT_COUNT_VARIABLE_MUST_BE_A_COMPIL);
      return nullptr;
    }
  } else {
    error(expr, DiagID::ERR_CODEGEN_REPEAT_COUNT_MUST_BE_A_NUMERIC_LITERAL);
    return nullptr;
  }
  llvm::Type *elemTy = val->getType();

  // The initializer expression can be a bare `T` while the declared array
  // element is `T?`.  Preserve the semantic element layout rather than
  // constructing `[N x T]` and leaving VariableDecl to reconcile it with
  // `[N x {T, present}]` later.
  if (expr->ResolvedType && expr->ResolvedType->isArray()) {
    auto declaredElem = expr->ResolvedType->getArrayElementType();
    llvm::Type *declaredElemTy = getLLVMType(declaredElem);
    if (declaredElemTy && declaredElemTy != elemTy) {
      if (declaredElemTy->isStructTy() &&
          declaredElemTy->getStructNumElements() == 2 &&
          declaredElemTy->getStructElementType(0) == elemTy &&
          declaredElemTy->getStructElementType(1)->isIntegerTy(1)) {
        llvm::Value *wrapped = llvm::UndefValue::get(declaredElemTy);
        wrapped = m_Builder.CreateInsertValue(wrapped, val, {0});
        val = m_Builder.CreateInsertValue(
            wrapped, llvm::ConstantInt::getTrue(m_Context), {1});
      }
      elemTy = declaredElemTy;
    }
  }

  llvm::ArrayType *arrTy = llvm::ArrayType::get(elemTy, count);
  if (auto *c = llvm::dyn_cast<llvm::Constant>(val)) {
    std::vector<llvm::Constant *> elements(count, c);
    llvm::Value *arrVal = llvm::ConstantArray::get(arrTy, elements);
    std::string arrayTypeName =
        "[" + val_ent.typeName + "; " + std::to_string(count) + "]";
    return PhysEntity(arrVal, arrayTypeName, arrTy, false);
  }
  llvm::Value *alloca =
      createEntryBlockAlloca(arrTy, nullptr, "repeated_array");
  for (uint64_t i = 0; i < count; ++i) {
    llvm::Value *ptr = m_Builder.CreateInBoundsGEP(
        arrTy, alloca,
        {m_Builder.getInt32(0),
         llvm::ConstantInt::get(getIntPtrTy(), i)});
    m_Builder.CreateStore(val, ptr);
  }
  std::string arrayTypeName =
      "[" + val_ent.typeName + "; " + std::to_string(count) + "]";
  return PhysEntity(alloca, arrayTypeName, arrTy, true);
}

PhysEntity CodeGen::genClosureExpr(const ClosureExpr *expr) {
  auto shapeType = std::dynamic_pointer_cast<toka::ShapeType>(expr->ResolvedType);
  if (!shapeType || !shapeType->Decl) {
      if (expr->ResolvedType && expr->ResolvedType->isReference()) {
         auto refTy = std::dynamic_pointer_cast<toka::ReferenceType>(expr->ResolvedType);
         if (refTy) shapeType = std::dynamic_pointer_cast<toka::ShapeType>(refTy->PointeeType);
      }
  }

  if (!shapeType || !shapeType->Decl) {
      std::cerr << "CodeGen Internal Error: Closure's ResolvedType was not ShapeType!\n";
      return nullptr;
  }

  llvm::Type *llvmTy = getLLVMType(shapeType);
  if (!llvmTy) return nullptr;

  llvm::Value *alloca = createEntryBlockAlloca(llvmTy, nullptr, "closure_env");

  // Populate captures
  for (size_t i = 0; i < shapeType->Decl->Members.size(); ++i) {
    const auto &member = shapeType->Decl->Members[i];

    llvm::Value *fieldAddr = m_Builder.CreateStructGEP(llvmTy, alloca, i);

    if (member.ResolvedType && member.ResolvedType->isReference()) {
       // Reference capture: Store the address
       llvm::Value *srcAddr = getEntityAddr(member.Name); 
       if (srcAddr) {
           m_Builder.CreateStore(srcAddr, fieldAddr);
       } else {
           std::cerr << "CodeGen Internal Error: Captured variable '" << member.Name << "' addr not found.\n";
       }
    } else {
       // Value capture
       llvm::Value *srcAddr = getIdentityAddr(member.Name); 
       if (srcAddr) {
           llvm::Type *loadTy = nullptr;
           if (member.ResolvedType) {
               loadTy = getLLVMType(member.ResolvedType);
           } else {
               loadTy = resolveType(member.Type, member.IsRawPointer);
           }
           if (!loadTy) {
               std::cerr << "CodeGen Internal Error: Captured variable '" << member.Name << "' type could not be resolved.\n";
               continue;
           }
           bool isCede = false;
           bool isDup = false;
           for (const auto& cap : expr->ExplicitCaptures) {
               if (toka::Type::stripMorphology(cap.Name) != member.Name)
                   continue;
               isCede = cap.Mode == CaptureMode::ExplicitCede;
               isDup = cap.Mode == CaptureMode::ExplicitDup;
               break;
           }

           llvm::Value *val = nullptr;
           if (isDup && member.ResolvedType && member.ResolvedType->isShape()) {
               std::string typeName = toka::Type::stripMorphology(
                   member.ResolvedType->getSoulName());
               std::string providerName = "Dup_" + typeName + "_dup";
               if (llvm::Function *provider = m_Module->getFunction(providerName)) {
                   auto receiver = std::make_unique<VariableExpr>(member.Name);
                   receiver->ResolvedType = member.ResolvedType;
                   MethodCallExpr duplicate(std::move(receiver), "dup", {});
                   duplicate.ResolvedType = member.ResolvedType;
                   PhysEntity result = genMethodCall(&duplicate);
                   val = result.load(m_Builder);
               }
           }
           if (!val)
               val = m_Builder.CreateLoad(loadTy, srcAddr);
           m_Builder.CreateStore(val, fieldAddr);

           // [Fix] Memory Leak/Double Free: Nullify the original pointer if the capture is `cede`
           if (isCede) {
               if (loadTy->isPointerTy()) {
                   m_Builder.CreateStore(llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(loadTy)), srcAddr);
               } else if (loadTy->isStructTy()) {
                   m_Builder.CreateStore(llvm::ConstantAggregateZero::get(loadTy), srcAddr);
               }
               
               suppressDropForMove(member.Name);
           }
       } else {
           std::cerr << "CodeGen Internal Error: Captured variable '" << member.Name << "' val not found.\n";
       }
    }
  }

  return PhysEntity(alloca, shapeType->Name, llvmTy, true);
}


PhysEntity CodeGen::genArrayInitExpr(const ArrayInitExpr *expr) {
  // Not supported as a bare stack value yet. Usually handled within ImplicitBoxExpr or NewExpr.
  // We'll leave it returning nullptr for now unless explicitly needed on stack.
  std::cerr << "genArrayInitExpr on stack not fully implemented yet." << std::endl;
  return nullptr;
}
PhysEntity CodeGen::genAwaitExpr(const AwaitExpr *awaitExpr) {
    if (!m_CurrentCoroPromiseType) {
        error(awaitExpr, DiagID::ERR_CODEGEN_AWAIT_CAN_ONLY_BE_USED_INSIDE_AN_ASYNC);
        return {};
    }
    
    PhysEntity handleEnt = genExpr(awaitExpr->Expression.get());
    llvm::Value *handleVal = handleEnt.load(m_Builder);
    
    llvm::Value *targetTCBPtr = handleVal;
    if (handleVal->getType()->isStructTy()) {
        targetTCBPtr = m_Builder.CreateExtractValue(handleVal, 0, "await.tcb_ptr");
    }
    
    llvm::Function *getFrameFn = m_Module->getFunction("toka_tcb_get_coro_frame");
    if (!getFrameFn) {
        llvm::FunctionType *ft = llvm::FunctionType::get(m_Builder.getPtrTy(), {m_Builder.getPtrTy()}, false);
        getFrameFn = llvm::Function::Create(ft, llvm::Function::ExternalLinkage, "toka_tcb_get_coro_frame", m_Module.get());
    }
    llvm::Value *targetCoroHandle = m_Builder.CreateCall(getFrameFn, {targetTCBPtr}, "target.coro_handle");

    // Ensure target task is started if still in CREATED state
    llvm::Function *startFn = m_Module->getFunction("toka_task_start");
    if (!startFn) {
        llvm::FunctionType *ft = llvm::FunctionType::get(m_Builder.getInt32Ty(), {m_Builder.getPtrTy()}, false);
        startFn = llvm::Function::Create(ft, llvm::Function::ExternalLinkage, "toka_task_start", m_Module.get());
    }
    m_Builder.CreateCall(startFn, {targetTCBPtr});

    llvm::Function *promiseFn = llvm::Intrinsic::getOrInsertDeclaration(m_Module.get(), llvm::Intrinsic::coro_promise);
    llvm::Value *alignment = m_Builder.getInt32(8);
    llvm::Value *fromPromise = m_Builder.getInt1(false);
    llvm::Value *targetPromisePtrRaw = m_Builder.CreateCall(promiseFn, {targetCoroHandle, alignment, fromPromise}, "target.promise.raw");
    
    std::shared_ptr<Type> targetInnerTyObj =
        awaitExpr->AwaitedType ? awaitExpr->AwaitedType : awaitExpr->ResolvedType;
    llvm::Type *targetInnerTy = getLLVMType(targetInnerTyObj);
    
    llvm::Function *getStateFn = m_Module->getFunction("toka_task_get_result_state");
    if (!getStateFn) {
        llvm::FunctionType *ft = llvm::FunctionType::get(m_Builder.getInt8Ty(), {m_Builder.getPtrTy()}, false);
        getStateFn = llvm::Function::Create(ft, llvm::Function::ExternalLinkage, "toka_task_get_result_state", m_Module.get());
    }
    llvm::Value *targetState = m_Builder.CreateCall(getStateFn, {targetPromisePtrRaw}, "target.state");
    
    llvm::BasicBlock *readyBB = llvm::BasicBlock::Create(m_Context, "await.ready", m_Builder.GetInsertBlock()->getParent());
    llvm::BasicBlock *canceledBB = llvm::BasicBlock::Create(m_Context, "await.canceled", m_Builder.GetInsertBlock()->getParent());
    llvm::BasicBlock *outcomeMergeBB = awaitExpr->CatchesCancellation
        ? llvm::BasicBlock::Create(m_Context, "await.outcome", m_Builder.GetInsertBlock()->getParent())
        : nullptr;
    llvm::BasicBlock *stateDispatchBB = llvm::BasicBlock::Create(m_Context, "await.state_dispatch", m_Builder.GetInsertBlock()->getParent());
    llvm::BasicBlock *suspendCheckBB = llvm::BasicBlock::Create(m_Context, "await.suspend_check", m_Builder.GetInsertBlock()->getParent());
    llvm::BasicBlock *suspendBB = llvm::BasicBlock::Create(m_Context, "await.suspend", m_Builder.GetInsertBlock()->getParent());

    llvm::Function *isCurrentCanceledFn = m_Module->getFunction("toka_task_is_current_canceled");
    if (!isCurrentCanceledFn) {
        llvm::FunctionType *ft = llvm::FunctionType::get(m_Builder.getInt32Ty(), {m_Builder.getPtrTy()}, false);
        isCurrentCanceledFn = llvm::Function::Create(ft, llvm::Function::ExternalLinkage, "toka_task_is_current_canceled", m_Module.get());
    }
    llvm::Value *entrySelfCanceled = m_Builder.CreateCall(isCurrentCanceledFn, {m_CurrentCoroHandle}, "entry.self.canceled");
    llvm::Value *entryIsSelfCanceled = m_Builder.CreateICmpNE(entrySelfCanceled, m_Builder.getInt32(0));
    llvm::Value *entryTargetCanceled = m_Builder.CreateICmpEQ(targetState, m_Builder.getInt8(3), "entry.target.canceled");
    llvm::Value *entryCancel = m_Builder.CreateOr(entryIsSelfCanceled, entryTargetCanceled, "entry.cancel");
    m_Builder.CreateCondBr(entryCancel, canceledBB, stateDispatchBB);

    m_Builder.SetInsertPoint(stateDispatchBB);
    llvm::SwitchInst *swState = m_Builder.CreateSwitch(targetState, suspendCheckBB, 2);
    swState->addCase(m_Builder.getInt8(1), readyBB);
    swState->addCase(m_Builder.getInt8(3), canceledBB);

    m_Builder.SetInsertPoint(suspendCheckBB);
    llvm::Function *awaitPrepFn = m_Module->getFunction("toka_task_await_prepare");
    if (!awaitPrepFn) {
        llvm::FunctionType *ft = llvm::FunctionType::get(m_Builder.getInt32Ty(), {m_Builder.getPtrTy(), m_Builder.getPtrTy()}, false);
        awaitPrepFn = llvm::Function::Create(ft, llvm::Function::ExternalLinkage, "toka_task_await_prepare", m_Module.get());
    }
    llvm::Value *mustSuspend = m_Builder.CreateCall(awaitPrepFn, {targetPromisePtrRaw, m_CurrentCoroTCB});
    llvm::Value *condSuspend = m_Builder.CreateICmpNE(mustSuspend, m_Builder.getInt32(0));
    
    llvm::BasicBlock *postPrepareBB = llvm::BasicBlock::Create(m_Context, "await.post_prepare", m_Builder.GetInsertBlock()->getParent());
    m_Builder.CreateCondBr(condSuspend, suspendBB, postPrepareBB);

    m_Builder.SetInsertPoint(postPrepareBB);
    llvm::Value *postState = m_Builder.CreateCall(getStateFn, {targetPromisePtrRaw}, "post.target.state");
    llvm::Value *postIsCanceled = m_Builder.CreateICmpEQ(postState, m_Builder.getInt8(3), "post_is_canceled");
    llvm::Value *postSelfCanceled = m_Builder.CreateCall(isCurrentCanceledFn, {m_CurrentCoroHandle}, "post.self.canceled");
    llvm::Value *postIsSelfCanceled = m_Builder.CreateICmpNE(postSelfCanceled, m_Builder.getInt32(0));
    llvm::Value *postCancel = m_Builder.CreateOr(postIsCanceled, postIsSelfCanceled, "post.cancel");
    m_Builder.CreateCondBr(postCancel, canceledBB, readyBB);

    m_Builder.SetInsertPoint(suspendBB);
    
    llvm::Function *saveFn = llvm::Intrinsic::getOrInsertDeclaration(m_Module.get(), llvm::Intrinsic::coro_save);
    llvm::Function *suspFn = llvm::Intrinsic::getOrInsertDeclaration(m_Module.get(), llvm::Intrinsic::coro_suspend);
    
    llvm::Value *saveToken = m_Builder.CreateCall(saveFn, {m_CurrentCoroHandle});
    llvm::Value *suspendRes = m_Builder.CreateCall(suspFn, {saveToken, m_Builder.getInt1(false)});
    
    llvm::BasicBlock *resumeContBB = llvm::BasicBlock::Create(m_Context, "await.resume", m_Builder.GetInsertBlock()->getParent());
    llvm::BasicBlock *cleanupContBB = llvm::BasicBlock::Create(m_Context, "await.cleanup.await", m_Builder.GetInsertBlock()->getParent());
    
    llvm::SwitchInst *sw = m_Builder.CreateSwitch(suspendRes, m_CurrentCoroSuspendRetBB, 3);
    sw->addCase(m_Builder.getInt8(-1), m_CurrentCoroSuspendRetBB);
    sw->addCase(m_Builder.getInt8(0), resumeContBB);
    sw->addCase(m_Builder.getInt8(1), cleanupContBB);
    
    m_Builder.SetInsertPoint(cleanupContBB);
    m_Builder.CreateBr(m_CurrentCoroCleanupBB);
    
    m_Builder.SetInsertPoint(resumeContBB);
    llvm::Value *resState = m_Builder.CreateCall(getStateFn, {targetPromisePtrRaw}, "res.target.state");
    llvm::Value *resIsCanceled = m_Builder.CreateICmpEQ(resState, m_Builder.getInt8(3), "res_is_canceled");

    llvm::Value *selfCanceled = m_Builder.CreateCall(isCurrentCanceledFn, {m_CurrentCoroHandle}, "self.canceled");
    llvm::Value *isSelfCanceled = m_Builder.CreateICmpNE(selfCanceled, m_Builder.getInt32(0));

    llvm::Value *unwindCond = m_Builder.CreateOr(resIsCanceled, isSelfCanceled, "unwind_cond");
    m_Builder.CreateCondBr(unwindCond, canceledBB, readyBB);

    m_Builder.SetInsertPoint(canceledBB);
    llvm::Value *canceledOutcome = nullptr;
    if (awaitExpr->CatchesCancellation) {
        llvm::Type *outcomeTy = getLLVMType(awaitExpr->ResolvedType);
        auto *outcomeStructTy = llvm::dyn_cast<llvm::StructType>(outcomeTy);
        if (!outcomeStructTy || outcomeStructTy->getNumElements() < 2) {
            error(awaitExpr, DiagID::ERR_CODEGEN_INVALID_REPRESENTATION_FOR,
                  awaitExpr->ResolvedType->toString());
            return {};
        }
        llvm::Value *outcomeSlot = createEntryBlockAlloca(outcomeTy, nullptr, "await.canceled.outcome");
        llvm::Value *tagAddr = m_Builder.CreateStructGEP(outcomeStructTy, outcomeSlot, 0);
        m_Builder.CreateStore(m_Builder.getInt8(0), tagAddr);
        canceledOutcome = m_Builder.CreateLoad(outcomeTy, outcomeSlot);

        llvm::Function *markHandledFn = m_Module->getFunction("toka_task_mark_current_cancellation_handled");
        if (!markHandledFn) {
            llvm::FunctionType *ft = llvm::FunctionType::get(
                m_Builder.getInt32Ty(), {m_Builder.getPtrTy()}, false);
            markHandledFn = llvm::Function::Create(
                ft, llvm::Function::ExternalLinkage,
                "toka_task_mark_current_cancellation_handled", m_Module.get());
        }
        m_Builder.CreateCall(markHandledFn, {m_CurrentCoroHandle});
        m_Builder.CreateBr(outcomeMergeBB);
    } else {
        genCoroutineCancelReturn();
    }

    m_Builder.SetInsertPoint(readyBB);
    llvm::Function *takeResultFn = m_Module->getFunction("toka_task_take_result");
    if (!takeResultFn) {
        llvm::FunctionType *ft = llvm::FunctionType::get(
            m_Builder.getInt32Ty(), {m_Builder.getPtrTy()}, false);
        takeResultFn = llvm::Function::Create(
            ft, llvm::Function::ExternalLinkage, "toka_task_take_result",
            m_Module.get());
    }
    m_Builder.CreateCall(takeResultFn, {targetPromisePtrRaw});
    llvm::Value *readyVal = nullptr;
    if (!targetInnerTy->isVoidTy()) {
        llvm::Function *valuePtrFn = m_Module->getFunction("toka_task_result_value_ptr");
        if (!valuePtrFn) {
            llvm::FunctionType *ft = llvm::FunctionType::get(m_Builder.getPtrTy(), {m_Builder.getPtrTy()}, false);
            valuePtrFn = llvm::Function::Create(ft, llvm::Function::ExternalLinkage, "toka_task_result_value_ptr", m_Module.get());
        }
        llvm::Value *targetValPtrRaw = m_Builder.CreateCall(valuePtrFn, {targetPromisePtrRaw}, "target.val.ptr");
        llvm::Value *targetValPtr = m_Builder.CreatePointerCast(targetValPtrRaw, llvm::PointerType::getUnqual(targetInnerTy));
        readyVal = m_Builder.CreateLoad(targetInnerTy, targetValPtr, "target.val");
        if (!awaitExpr->CatchesCancellation) {
            return PhysEntity(readyVal, awaitExpr->ResolvedType->toString(), targetInnerTy, false);
        }

        llvm::Type *outcomeTy = getLLVMType(awaitExpr->ResolvedType);
        auto *outcomeStructTy = llvm::dyn_cast<llvm::StructType>(outcomeTy);
        if (!outcomeStructTy || outcomeStructTy->getNumElements() < 2) {
            error(awaitExpr, DiagID::ERR_CODEGEN_INVALID_REPRESENTATION_FOR,
                  awaitExpr->ResolvedType->toString());
            return {};
        }
        llvm::Value *outcomeSlot = createEntryBlockAlloca(outcomeTy, nullptr, "await.ready.outcome");
        llvm::Value *tagAddr = m_Builder.CreateStructGEP(outcomeStructTy, outcomeSlot, 0);
        m_Builder.CreateStore(m_Builder.getInt8(1), tagAddr);
        llvm::Value *payloadAddr = m_Builder.CreateStructGEP(outcomeStructTy, outcomeSlot, 1);
        llvm::Value *typedPayloadAddr = m_Builder.CreateBitCast(
            payloadAddr, llvm::PointerType::getUnqual(m_Context));
        m_Builder.CreateStore(readyVal, typedPayloadAddr);
        llvm::Value *readyOutcome = m_Builder.CreateLoad(outcomeTy, outcomeSlot);
        m_Builder.CreateBr(outcomeMergeBB);

        m_Builder.SetInsertPoint(outcomeMergeBB);
        llvm::PHINode *outcome = m_Builder.CreatePHI(outcomeTy, 2, "await.outcome.value");
        outcome->addIncoming(canceledOutcome, canceledBB);
        outcome->addIncoming(readyOutcome, readyBB);
        return PhysEntity(outcome, awaitExpr->ResolvedType->toString(), outcomeTy, false);
    }
    if (awaitExpr->CatchesCancellation) {
        error(awaitExpr, DiagID::ERR_CODEGEN_INVALID_REPRESENTATION_FOR,
              "Option<void>");
        return {};
    }
    return PhysEntity(llvm::Constant::getNullValue(m_Builder.getInt32Ty()), "void", targetInnerTy, false);
}

PhysEntity CodeGen::genWaitExpr(const WaitExpr *waitExpr) {
    PhysEntity handleEnt = genExpr(waitExpr->Expression.get());
    llvm::Value *handleVal = handleEnt.load(m_Builder);
    
    llvm::Value *targetTCBPtr = handleVal;
    if (handleVal->getType()->isStructTy()) {
        targetTCBPtr = m_Builder.CreateExtractValue(handleVal, 0, "wait.tcb_ptr");
    }

    llvm::Function *startFn = m_Module->getFunction("toka_task_start");
    if (!startFn) {
        llvm::FunctionType *ft = llvm::FunctionType::get(m_Builder.getInt32Ty(), {m_Builder.getPtrTy()}, false);
        startFn = llvm::Function::Create(ft, llvm::Function::ExternalLinkage, "toka_task_start", m_Module.get());
    }
    m_Builder.CreateCall(startFn, {targetTCBPtr});

    llvm::Function *spawnFn = m_Module->getFunction("__toka_spawn_blocking");
    if (!spawnFn) {
        llvm::FunctionType *ft = llvm::FunctionType::get(m_Builder.getVoidTy(), {m_Builder.getPtrTy()}, false);
        spawnFn = llvm::Function::Create(ft, llvm::Function::ExternalLinkage, "__toka_spawn_blocking", m_Module.get());
    }
    m_Builder.CreateCall(spawnFn, {targetTCBPtr});

    llvm::Function *getFrameFn = m_Module->getFunction("toka_tcb_get_coro_frame");
    if (!getFrameFn) {
        llvm::FunctionType *ft = llvm::FunctionType::get(m_Builder.getPtrTy(), {m_Builder.getPtrTy()}, false);
        getFrameFn = llvm::Function::Create(ft, llvm::Function::ExternalLinkage, "toka_tcb_get_coro_frame", m_Module.get());
    }
    llvm::Value *targetCoroHandle = m_Builder.CreateCall(getFrameFn, {targetTCBPtr}, "target.coro_handle");
    
    llvm::Function *promiseFn = llvm::Intrinsic::getOrInsertDeclaration(m_Module.get(), llvm::Intrinsic::coro_promise);
    llvm::Value *alignment = m_Builder.getInt32(8);
    llvm::Value *fromPromise = m_Builder.getInt1(false);
    llvm::Value *targetPromisePtrRaw = m_Builder.CreateCall(promiseFn, {targetCoroHandle, alignment, fromPromise}, "target.promise.raw");
    
    std::shared_ptr<Type> targetInnerTyObj = waitExpr->ResolvedType;
    llvm::Type *targetInnerTy = getLLVMType(targetInnerTyObj);
    
    llvm::Function *getStateFn = m_Module->getFunction("toka_task_get_result_state");
    if (!getStateFn) {
        llvm::FunctionType *ft = llvm::FunctionType::get(m_Builder.getInt8Ty(), {m_Builder.getPtrTy()}, false);
        getStateFn = llvm::Function::Create(ft, llvm::Function::ExternalLinkage, "toka_task_get_result_state", m_Module.get());
    }
    llvm::Value *targetState = m_Builder.CreateCall(getStateFn, {targetPromisePtrRaw}, "wait.target.state");
    llvm::Value *isCanceled = m_Builder.CreateICmpEQ(targetState, m_Builder.getInt8(3), "wait.target.canceled");
    llvm::BasicBlock *canceledBB = llvm::BasicBlock::Create(m_Context, "wait.canceled", m_Builder.GetInsertBlock()->getParent());
    llvm::BasicBlock *readyBB = llvm::BasicBlock::Create(m_Context, "wait.ready", m_Builder.GetInsertBlock()->getParent());
    m_Builder.CreateCondBr(isCanceled, canceledBB, readyBB);

    m_Builder.SetInsertPoint(canceledBB);
    llvm::Function *unhandledCancelFn = m_Module->getFunction("toka_task_unhandled_cancellation");
    if (!unhandledCancelFn) {
        llvm::FunctionType *ft = llvm::FunctionType::get(m_Builder.getVoidTy(), {}, false);
        unhandledCancelFn = llvm::Function::Create(ft, llvm::Function::ExternalLinkage, "toka_task_unhandled_cancellation", m_Module.get());
        unhandledCancelFn->addFnAttr(llvm::Attribute::NoReturn);
    }
    m_Builder.CreateCall(unhandledCancelFn);
    m_Builder.CreateUnreachable();

    m_Builder.SetInsertPoint(readyBB);
    llvm::Function *takeResultFn = m_Module->getFunction("toka_task_take_result");
    if (!takeResultFn) {
        llvm::FunctionType *ft = llvm::FunctionType::get(
            m_Builder.getInt32Ty(), {m_Builder.getPtrTy()}, false);
        takeResultFn = llvm::Function::Create(
            ft, llvm::Function::ExternalLinkage, "toka_task_take_result",
            m_Module.get());
    }
    m_Builder.CreateCall(takeResultFn, {targetPromisePtrRaw});
    
    if (!targetInnerTy->isVoidTy()) {
        llvm::Function *valuePtrFn = m_Module->getFunction("toka_task_result_value_ptr");
        if (!valuePtrFn) {
            llvm::FunctionType *ft = llvm::FunctionType::get(m_Builder.getPtrTy(), {m_Builder.getPtrTy()}, false);
            valuePtrFn = llvm::Function::Create(ft, llvm::Function::ExternalLinkage, "toka_task_result_value_ptr", m_Module.get());
        }
        llvm::Value *targetValPtrRaw = m_Builder.CreateCall(valuePtrFn, {targetPromisePtrRaw}, "target.val.ptr");
        llvm::Value *targetValPtr = m_Builder.CreatePointerCast(targetValPtrRaw, llvm::PointerType::getUnqual(targetInnerTy));
        llvm::Value *targetVal = m_Builder.CreateLoad(targetInnerTy, targetValPtr, "target.val");
        return PhysEntity(targetVal, waitExpr->ResolvedType->toString(), targetInnerTy, false);
    }
    
    return PhysEntity(llvm::Constant::getNullValue(m_Builder.getInt32Ty()), "void", targetInnerTy, false);
}

PhysEntity CodeGen::genComptimeReflectExpr(const ComptimeReflectExpr *expr) {
  std::string targetTyStr = expr->ReflectedType
                                ? expr->ReflectedType->toString()
                                : expr->TypeSyntax
                                      ? toka::Type::fromSyntax(expr->TypeSyntax)
                                            ->toString()
                                      : expr->ReflectedTypeStr;
  std::string targetSoul = toka::Type::stripPrefixes(targetTyStr);

  llvm::Type *typeInfoTy = getLLVMType(lowerTypeSyntax(nullptr, "TypeInfo"));
  if (!typeInfoTy || !typeInfoTy->isSized()) {
      error(expr, DiagID::ERR_CODEGEN_TYPEINFO_SHAPE_NOT_DEFINED_OR_OPAQUE_I);
      return {};
  }
  
  if (m_Shapes.count(targetSoul) == 0) {
      error(expr, DiagID::ERR_CODEGEN_CANNOT_REFLECT_UNINSTANTIATED_OR_PRIMI, targetSoul);
      return {};
  }
  auto *SD = m_Shapes[targetSoul];

  llvm::Type *fieldInfoTy = getLLVMType(lowerTypeSyntax(nullptr, "FieldInfo"));
  if (!fieldInfoTy || !fieldInfoTy->isSized()) {
      error(expr, DiagID::ERR_CODEGEN_FIELDINFO_SHAPE_NOT_DEFINED_OR_OPAQUE);
      return {};
  }
  llvm::ArrayType *fieldArrayTy = llvm::ArrayType::get(fieldInfoTy, SD->Members.size());
  llvm::Value *fieldArrayAlloc = createEntryBlockAlloca(fieldArrayTy, nullptr, "reflect_fields_" + targetSoul);

  uint64_t currentOffset = 0;
  for (size_t i = 0; i < SD->Members.size(); ++i) {
      const auto &member = SD->Members[i];
      llvm::Value *fieldPtr = m_Builder.CreateGEP(fieldArrayTy, fieldArrayAlloc, {m_Builder.getInt32(0), m_Builder.getInt32(i)});
      
      // name: str (RowFat)
      llvm::Value *namePtr = m_Builder.CreateGlobalString(member.Name);
      llvm::Type *fatTy = getLLVMType(lowerTypeSyntax(nullptr, "str"));
      llvm::Value *nameFat = llvm::UndefValue::get(fatTy);
      nameFat = m_Builder.CreateInsertValue(nameFat, namePtr, {0});
      nameFat = m_Builder.CreateInsertValue(nameFat, llvm::ConstantInt::get(getIntPtrTy(), member.Name.size()), {1});
      m_Builder.CreateStore(nameFat, m_Builder.CreateStructGEP(fieldInfoTy, fieldPtr, 0));

      // type_name: str
      llvm::Value *tyNamePtr = m_Builder.CreateGlobalString(member.Type);
      llvm::Value *tyNameFat = llvm::UndefValue::get(fatTy);
      tyNameFat = m_Builder.CreateInsertValue(tyNameFat, tyNamePtr, {0});
      tyNameFat = m_Builder.CreateInsertValue(tyNameFat, llvm::ConstantInt::get(getIntPtrTy(), member.Type.size()), {1});
      m_Builder.CreateStore(tyNameFat, m_Builder.CreateStructGEP(fieldInfoTy, fieldPtr, 1));

      // offset: usize
      m_Builder.CreateStore(llvm::ConstantInt::get(getIntPtrTy(), currentOffset), m_Builder.CreateStructGEP(fieldInfoTy, fieldPtr, 2));
      // size: usize
      uint64_t ptrSize = (getIntPtrTy() == llvm::Type::getInt32Ty(m_Context) ? 4 : 8);
      m_Builder.CreateStore(llvm::ConstantInt::get(getIntPtrTy(), ptrSize), m_Builder.CreateStructGEP(fieldInfoTy, fieldPtr, 3));
      currentOffset += ptrSize;
  }

  // TypeInfo allocation
  llvm::Value *typeInfoAlloc = createEntryBlockAlloca(typeInfoTy, nullptr, "reflect_typeinfo_" + targetSoul);

  // set name
  llvm::Value *namePtr = m_Builder.CreateGlobalString(targetSoul);
  llvm::Type *fatTy = getLLVMType(lowerTypeSyntax(nullptr, "str"));
  llvm::Value *nameFat = llvm::UndefValue::get(fatTy);
  nameFat = m_Builder.CreateInsertValue(nameFat, namePtr, {0});
  nameFat = m_Builder.CreateInsertValue(nameFat, llvm::ConstantInt::get(getIntPtrTy(), targetSoul.size()), {1});
  m_Builder.CreateStore(nameFat, m_Builder.CreateStructGEP(typeInfoTy, typeInfoAlloc, 0));

  // set kind
  m_Builder.CreateStore(llvm::ConstantInt::get(llvm::Type::getInt8Ty(m_Context), 0), m_Builder.CreateStructGEP(typeInfoTy, typeInfoAlloc, 1));

  // set fields (RowFat<FieldInfo>)
  llvm::Type *fatFieldInfoTy =
      getLLVMType(lowerTypeSyntax(nullptr, "RowFat_M_FieldInfo"));
  if (!fatFieldInfoTy) {
      fatFieldInfoTy = getLLVMType(lowerTypeSyntax(nullptr, "str")); // fallback if aliased
  }
  llvm::Value *fieldsFat = llvm::UndefValue::get(fatFieldInfoTy);
  fieldsFat = m_Builder.CreateInsertValue(fieldsFat, m_Builder.CreateBitCast(fieldArrayAlloc, llvm::PointerType::getUnqual(m_Context)), {0});
  fieldsFat = m_Builder.CreateInsertValue(fieldsFat, llvm::ConstantInt::get(getIntPtrTy(), SD->Members.size()), {1});
  m_Builder.CreateStore(fieldsFat, m_Builder.CreateStructGEP(typeInfoTy, typeInfoAlloc, 2));

  // set size
  m_Builder.CreateStore(llvm::ConstantInt::get(getIntPtrTy(), currentOffset), m_Builder.CreateStructGEP(typeInfoTy, typeInfoAlloc, 3));
  // set align
  uint64_t ptrSize = (getIntPtrTy() == llvm::Type::getInt32Ty(m_Context) ? 4 : 8);
  m_Builder.CreateStore(llvm::ConstantInt::get(getIntPtrTy(), ptrSize), m_Builder.CreateStructGEP(typeInfoTy, typeInfoAlloc, 4));

  llvm::Value *typeInfoVal = m_Builder.CreateLoad(typeInfoTy, typeInfoAlloc);
  return PhysEntity(typeInfoVal, "TypeInfo", typeInfoTy, false);
}

PhysEntity CodeGen::genStartExpr(const StartExpr *E) {
    return genTaskStart(E->Expression.get());
}

PhysEntity CodeGen::genTaskStart(const Expr *E) {
    PhysEntity handleEnt = genExpr(E);
    llvm::Value *handleVal = handleEnt.load(m_Builder);
    llvm::Value *tcbPtr = handleVal;
    if (handleVal->getType()->isStructTy()) {
        tcbPtr = m_Builder.CreateExtractValue(handleVal, 0, "tcb_ptr");
    }

    llvm::Function *startFn = m_Module->getFunction("toka_task_start");
    if (!startFn) {
        llvm::FunctionType *ft = llvm::FunctionType::get(m_Builder.getInt32Ty(), {m_Builder.getPtrTy()}, false);
        startFn = llvm::Function::Create(ft, llvm::Function::ExternalLinkage, "toka_task_start", m_Module.get());
    }
    m_Builder.CreateCall(startFn, {tcbPtr});
    
    llvm::Function *spawnFn = m_Module->getFunction("__toka_spawn");
    if (!spawnFn) {
        llvm::FunctionType *ft = llvm::FunctionType::get(m_Builder.getVoidTy(), {m_Builder.getPtrTy()}, false);
        spawnFn = llvm::Function::Create(ft, llvm::Function::ExternalLinkage, "__toka_spawn", m_Module.get());
    }
    m_Builder.CreateCall(spawnFn, {tcbPtr});
    
    return handleEnt;
}
} // namespace toka
