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
#include "toka/AST.h"
#include "toka/CodeGen.h"
#include "toka/MemberAccess.h"
#include "toka/Type.h"
#include <cctype>
#include <functional>
#include <iostream>
#include <set>
#include <typeinfo>

namespace toka {

llvm::Value *CodeGen::genReturnStmt(const ReturnStmt *ret) {
  llvm::Value *retVal = nullptr;
  const bool returnsMiss =
      ret->OutcomeKind == ReturnStmt::MissOutcomeKind::Miss;
  const bool wrapsHit =
      ret->OutcomeKind == ReturnStmt::MissOutcomeKind::Hit;
  const bool forwardsOutcome =
      ret->OutcomeKind == ReturnStmt::MissOutcomeKind::Forward;
  if (ret->ReturnValue && !returnsMiss) {
    retVal = genExpr(ret->ReturnValue.get()).load(m_Builder);
    if (!retVal) {
    }

    // [Fix] Premature Drop in Return
    // If we are returning a local Shared Pointer variable (by Copy/Share
    // LValue), we must increment RefCount to survive the upcoming scope cleanup
    // (Drop).

    bool isSharedReturn = false;
    const Expr *inner = ret->ReturnValue.get();

    // 1. Peel the onion: Unwrap Casts, Parens, Unary (~), Postfix
    while (true) {
      if (auto *pe = dynamic_cast<const PostfixExpr *>(inner)) {
        inner = pe->LHS.get();
      } else if (auto *ue = dynamic_cast<const UnaryExpr *>(inner)) {
        // If Unary is '~' (Shared), it marks an LValue Copy intention.
        if (ue->Op == TokenType::Tilde)
          isSharedReturn = true;
        inner = ue->RHS.get();
      } else {
        // Check for ImplicitCastExpr if it existed in Toka AST (it doesn't seem
        // to yet explicitly) Just break if we hit a variable or other terminal
        break;
      }
    }

    if (auto *ve = dynamic_cast<const VariableExpr *>(inner)) {
      std::string baseName = ve->Name;
      // Scrub decorators
      std::string cleanName = Type::stripMorphology(baseName);

      if (m_Symbols.count(cleanName)) {
        TokaSymbol &sym = m_Symbols[cleanName];

        // Only IncRef if it's a Shared variable AND we are returning it as a
        // Shared Copy (~) If we just return `p` (Raw), we don't IncRef. If we
        // return `move(p)`, we might not IncRef (logic elsewhere). Here we
        // assume `return ~p` means Copy.
        if (sym.morphology == Morphology::Shared && isSharedReturn &&
            retVal->getType()->isStructTy()) {

          llvm::Type *stTy = retVal->getType();
          if (stTy->getStructNumElements() == 2) {
            // Manual IncRef
            llvm::Value *refPtr =
                m_Builder.CreateExtractValue(retVal, 1, "ret_inc_refptr");
            llvm::Value *refNN =
                m_Builder.CreateIsNotNull(refPtr, "ret_inc_nn");

            llvm::Function *F = m_Builder.GetInsertBlock()->getParent();
            llvm::BasicBlock *doIncBB =
                llvm::BasicBlock::Create(m_Context, "ret_inc", F);
            llvm::BasicBlock *contBB =
                llvm::BasicBlock::Create(m_Context, "ret_cont", F);

            m_Builder.CreateCondBr(refNN, doIncBB, contBB);
            m_Builder.SetInsertPoint(doIncBB);

            // Option A: All shared pointer refcount operations are atomic by default (@arc)
            bool isAtomic = true;

            if (isAtomic) {
                // Retain/Inc uses Monotonic (Relaxed) memory ordering for maximal throughput
                m_Builder.CreateAtomicRMW(llvm::AtomicRMWInst::Add, refPtr, m_Builder.getInt32(1), llvm::MaybeAlign(4), llvm::AtomicOrdering::Monotonic);
            } else {
                llvm::Value *cnt = m_Builder.CreateLoad(llvm::Type::getInt32Ty(m_Context), refPtr);
                llvm::Value *inc = m_Builder.CreateAdd(
                    cnt, llvm::ConstantInt::get(llvm::Type::getInt32Ty(m_Context), 1));
                m_Builder.CreateStore(inc, refPtr);
            }

            m_Builder.CreateBr(contBB);
            m_Builder.SetInsertPoint(contBB);
          }
        }
      }
    }

    if (auto *varExpr =
            dynamic_cast<const VariableExpr *>(ret->ReturnValue.get())) {
      if (varExpr->IsUnique) {
        if (m_Symbols.count(varExpr->Name)) {
          llvm::Value *alloca = m_Symbols[varExpr->Name].allocaPtr;
          if (auto *ai = llvm::dyn_cast<llvm::AllocaInst>(alloca)) {
            m_Builder.CreateStore(
                llvm::Constant::getNullValue(ai->getAllocatedType()), alloca);
          }
        }
      }
    }

    // [Fix] Move Semantics on Return
    // If we are returning a local variable by value (Identity), we must
    // SUPPRESS its drop in the current scope because ownership is being
    // transferred to the caller.
    const Expr *moveTarget = ret->ReturnValue.get();
    while (moveTarget) {
      if (auto *ve = dynamic_cast<const VariableExpr *>(moveTarget)) {
        suppressDropForMove(ve->Name);
        break;
      } else if (auto *pe = dynamic_cast<const PostfixExpr *>(moveTarget)) {
        moveTarget = pe->LHS.get();
      } else if (auto *ue = dynamic_cast<const UnaryExpr *>(moveTarget)) {
        moveTarget = ue->RHS.get();
      } else {
        break;
      }
    }
  }

  if (returnsMiss || wrapsHit) {
    llvm::Type *outcomeType = m_CurrentSRetTy;
    if (!outcomeType && m_CurrentCoroRetTy)
      outcomeType = m_CurrentCoroRetTy;
    if (!outcomeType) {
      llvm::Function *function = m_Builder.GetInsertBlock()->getParent();
      outcomeType = function->getReturnType();
    }
    auto *layout = llvm::dyn_cast_or_null<llvm::StructType>(outcomeType);
    if (layout && layout->getNumElements() == 2 &&
        layout->getElementType(0)->isIntegerTy(1)) {
      llvm::Value *outcome = llvm::UndefValue::get(layout);
      outcome = m_Builder.CreateInsertValue(
          outcome, m_Builder.getInt1(wrapsHit), 0,
          wrapsHit ? "miss.hit.tag" : "miss.miss.tag");
      llvm::Type *payloadType = layout->getElementType(1);
      llvm::Value *payload = retVal;
      if (!payload) {
        payload = llvm::Constant::getNullValue(payloadType);
      } else if (payload->getType() != payloadType) {
        if (payload->getType()->isIntegerTy() && payloadType->isIntegerTy()) {
          payload = m_Builder.CreateIntCast(payload, payloadType, false);
        } else if (payload->getType()->isFloatingPointTy() &&
                   payloadType->isFloatingPointTy()) {
          payload = m_Builder.CreateFPCast(payload, payloadType);
        } else if (payload->getType()->isPointerTy() &&
                   payloadType->isPointerTy()) {
          payload = m_Builder.CreatePointerCast(payload, payloadType);
        }
      }
      outcome = m_Builder.CreateInsertValue(
          outcome, payload, 1,
          wrapsHit ? "miss.hit.payload" : "miss.miss.payload");
      retVal = outcome;
    }
  } else if (forwardsOutcome) {
    // `retVal` already carries the complete discriminator and payload.
  }

  // A return may be nested inside an outer control-flow expression whose
  // merge performs the final full-expression cleanup. Emit the same guarded
  // cleanup on this terminating edge without discarding the merge's plan.
  emitFullExpressionTemporaryDrops(false);

  if (m_CurrentSRetPtr && m_CurrentSRetTy) {
    if (retVal) {
      if (retVal->getType() != m_CurrentSRetTy) {
        llvm::Value *tempSrcPtr = m_Builder.CreatePointerCast(m_CurrentSRetPtr, llvm::PointerType::get(retVal->getType(), 0));
        m_Builder.CreateStore(retVal, tempSrcPtr);
      } else {
        m_Builder.CreateStore(retVal, m_CurrentSRetPtr);
      }
    }
    executeScopeUnwinding(0);
    return m_Builder.CreateRetVoid();
  }

  llvm::Function *f = m_Builder.GetInsertBlock()->getParent();
  executeScopeUnwinding(0);

  if (m_CurrentCoroHandle) {
    genCoroutineReturn(retVal);
    return nullptr;
  }

  if (retVal) {

    if (retVal->getType() != f->getReturnType()) {
      if (f->getReturnType()->isVoidTy())
        return m_Builder.CreateRetVoid();
      
      llvm::Type *srcTy = retVal->getType();
      llvm::Type *dstTy = f->getReturnType();
      
      if (srcTy->isIntegerTy() && dstTy->isIntegerTy()) {
          bool isSigned = false;
          if (ret->ReturnValue && ret->ReturnValue->ResolvedType) {
              isSigned = ret->ReturnValue->ResolvedType->isSignedInteger();
          }
          retVal = m_Builder.CreateIntCast(retVal, dstTy, isSigned);
      } else if (srcTy->isFloatingPointTy() && dstTy->isFloatingPointTy()) {
          retVal = m_Builder.CreateFPCast(retVal, dstTy);
      } else {
          if (srcTy->isAggregateType() || dstTy->isAggregateType()) {
              llvm::Value *temp = m_Builder.CreateAlloca(dstTy, nullptr, "ret.cast.temp");
              llvm::Value *tempSrcPtr = m_Builder.CreatePointerCast(temp, llvm::PointerType::get(srcTy, 0));
              m_Builder.CreateStore(retVal, tempSrcPtr);
              retVal = m_Builder.CreateLoad(dstTy, temp, "ret.cast.res");
          } else {
              retVal = m_Builder.CreateBitCast(retVal, dstTy);
          }
      }
    }
    return m_Builder.CreateRet(retVal);
  } else {
  }

  if (f->getReturnType()->isVoidTy())
    return m_Builder.CreateRetVoid();

  // Fallback: return default value if none provided but expected
  return m_Builder.CreateRet(llvm::Constant::getNullValue(f->getReturnType()));
}

llvm::Value *CodeGen::genBlockStmt(const BlockStmt *bs) {
  const auto symbolsBeforeBlock = m_Symbols;
  const auto namedValuesBeforeBlock = m_NamedValues;
  m_ScopeStack.push_back({});
  llvm::Value *lastVal = nullptr;
  for (const auto &s : bs->Statements) {
    lastVal = genStmt(s.get());
    // Liveness check: stop if terminator was generated
    if (m_Builder.GetInsertBlock() &&
        m_Builder.GetInsertBlock()->getTerminator())
      break;
  }

  if (m_ScopeStack.empty())
    return lastVal;

  executeScopeUnwinding(m_ScopeStack.size() - 1);
  m_ScopeStack.pop_back();
  m_Symbols = symbolsBeforeBlock;
  m_NamedValues = namedValuesBeforeBlock;
  return lastVal;
}

llvm::Value *CodeGen::genDeleteStmt(const DeleteStmt *del) {
  llvm::Function *freeFunc = m_Module->getFunction("free");
  if (freeFunc) {
    llvm::Value *val = genExpr(del->Expression.get()).load(m_Builder);
    if (val && val->getType()->isPointerTy()) {
      llvm::Value *casted = m_Builder.CreateBitCast(
          val, llvm::PointerType::getUnqual(llvm::Type::getInt8Ty(m_Context)));
      llvm::CallInst *freeCall = m_Builder.CreateCall(freeFunc, casted);
      markMemoryEvent(freeCall, "free");
    }
  }
  return nullptr;
}

void CodeGen::suppressDropForMove(const std::string &name) {
  std::string movedName = Type::stripMorphology(name);
  for (int i = (int)m_ScopeStack.size() - 1; i >= 0; --i) {
    for (auto entry = m_ScopeStack[i].rbegin();
         entry != m_ScopeStack[i].rend(); ++entry) {
      if (Type::stripMorphology(entry->Name) != movedName)
        continue;
      if (entry->DropFlag) {
        m_Builder.CreateStore(
            llvm::ConstantInt::getFalse(m_Context), entry->DropFlag);
      } else {
        entry->HasDrop = false;
        entry->IsShared = false;
        entry->IsUniquePointer = false;
      }
    }
  }
}

int CodeGen::getDirectMemberDropIndex(const VariableScopeInfo &entry,
                                      const MemberExpr *member) const {
  if (!member)
    return -1;
  auto *root = dynamic_cast<const VariableExpr *>(member->Object.get());
  if (!root || Type::stripMorphology(root->Name) !=
                   Type::stripMorphology(entry.Name))
    return -1;
  if (member->Index < 0 ||
      !entry.PartialMove.admits(PartialMoveProjectionKind::DirectField,
                                member->Index))
    return -1;
  return member->Index;
}

int CodeGen::getDirectArrayDropIndex(const VariableScopeInfo &entry,
                                     const ArrayIndexExpr *index) const {
  if (!index || index->Indices.size() != 1)
    return -1;
  auto *root = dynamic_cast<const VariableExpr *>(index->Array.get());
  auto *constant = dynamic_cast<const NumberExpr *>(index->Indices[0].get());
  if (!root || !constant || Type::stripMorphology(root->Name) !=
                               Type::stripMorphology(entry.Name))
    return -1;
  if (!entry.PartialMove.admits(
          PartialMoveProjectionKind::FixedArrayElement, constant->Value))
    return -1;
  return static_cast<int>(constant->Value);
}

void CodeGen::markInitLive(const BinaryExpr *assignment) {
  if (!assignment)
    return;
  auto *target = dynamic_cast<const VariableExpr *>(assignment->LHS.get());
  if (!target)
    return;

  markInitLive(target);
}

void CodeGen::markInitLive(const VariableExpr *target) {
  markInitState(target, llvm::ConstantInt::getTrue(m_Context));
}

void CodeGen::markInitState(const VariableExpr *target, llvm::Value *isLive) {
  if (!target)
    return;

  const std::string targetName = Type::stripMorphology(target->Name);
  for (int i = static_cast<int>(m_ScopeStack.size()) - 1; i >= 0; --i) {
    for (auto entry = m_ScopeStack[i].rbegin();
         entry != m_ScopeStack[i].rend(); ++entry) {
      if (Type::stripMorphology(entry->Name) != targetName)
        continue;
      if (entry->InitFlag)
        m_Builder.CreateStore(isLive, entry->InitFlag);
      if (entry->DropFlag)
        m_Builder.CreateStore(isLive, entry->DropFlag);
      return;
    }
  }
}

llvm::Value *CodeGen::getInitLiveFlag(const VariableExpr *place) {
  if (!place)
    return nullptr;

  const std::string placeName = Type::stripMorphology(place->Name);
  for (int i = static_cast<int>(m_ScopeStack.size()) - 1; i >= 0; --i) {
    for (auto entry = m_ScopeStack[i].rbegin();
         entry != m_ScopeStack[i].rend(); ++entry) {
      if (Type::stripMorphology(entry->Name) != placeName)
        continue;
      if (!entry->InitFlag)
        return nullptr;
      return m_Builder.CreateLoad(llvm::Type::getInt1Ty(m_Context),
                                  entry->InitFlag, placeName + ".init.live");
    }
  }
  return nullptr;
}

void CodeGen::suppressDropForPartialMove(const MemberExpr *member) {
  for (int i = static_cast<int>(m_ScopeStack.size()) - 1; i >= 0; --i) {
    for (auto entry = m_ScopeStack[i].rbegin();
         entry != m_ScopeStack[i].rend(); ++entry) {
      if (!entry->DropMask)
        continue;
      const int index =
          getDirectMemberDropIndex(*entry, member);
      if (index < 0 || index >= 64)
        continue;
      llvm::Value *mask = m_Builder.CreateLoad(
          llvm::Type::getInt64Ty(m_Context), entry->DropMask,
          "drop.mask.current");
      llvm::Value *cleared = m_Builder.CreateAnd(
          mask, m_Builder.getInt64(~(1ULL << index)), "drop.mask.cleared");
      m_Builder.CreateStore(cleared, entry->DropMask);
      return;
    }
  }
}

void CodeGen::suppressDropForPartialMove(const ArrayIndexExpr *index) {
  for (int i = static_cast<int>(m_ScopeStack.size()) - 1; i >= 0; --i) {
    for (auto entry = m_ScopeStack[i].rbegin();
         entry != m_ScopeStack[i].rend(); ++entry) {
      if (!entry->DropMask)
        continue;
      const int position = getDirectArrayDropIndex(*entry, index);
      if (position < 0 || position >= 64)
        continue;
      llvm::Value *mask = m_Builder.CreateLoad(
          llvm::Type::getInt64Ty(m_Context), entry->DropMask,
          "drop.mask.current");
      llvm::Value *cleared = m_Builder.CreateAnd(
          mask, m_Builder.getInt64(~(1ULL << position)),
          "drop.mask.cleared");
      m_Builder.CreateStore(cleared, entry->DropMask);
      return;
    }
  }
}

void CodeGen::restoreDropForMemberAssignment(const MemberExpr *member) {
  for (int i = static_cast<int>(m_ScopeStack.size()) - 1; i >= 0; --i) {
    for (auto entry = m_ScopeStack[i].rbegin();
         entry != m_ScopeStack[i].rend(); ++entry) {
      if (!entry->DropMask)
        continue;
      const int index =
          getDirectMemberDropIndex(*entry, member);
      if (index < 0 || index >= 64)
        continue;
      llvm::Value *mask = m_Builder.CreateLoad(
          llvm::Type::getInt64Ty(m_Context), entry->DropMask,
          "drop.mask.current");
      llvm::Value *restored = m_Builder.CreateOr(
          mask, m_Builder.getInt64(1ULL << index), "drop.mask.restored");
      m_Builder.CreateStore(restored, entry->DropMask);
      return;
    }
  }
}

void CodeGen::restoreDropForIndexAssignment(const ArrayIndexExpr *index) {
  for (int i = static_cast<int>(m_ScopeStack.size()) - 1; i >= 0; --i) {
    for (auto entry = m_ScopeStack[i].rbegin();
         entry != m_ScopeStack[i].rend(); ++entry) {
      if (!entry->DropMask)
        continue;
      const int position = getDirectArrayDropIndex(*entry, index);
      if (position < 0 || position >= 64)
        continue;
      llvm::Value *mask = m_Builder.CreateLoad(
          llvm::Type::getInt64Ty(m_Context), entry->DropMask,
          "drop.mask.current");
      llvm::Value *restored = m_Builder.CreateOr(
          mask, m_Builder.getInt64(1ULL << position),
          "drop.mask.restored");
      m_Builder.CreateStore(restored, entry->DropMask);
      return;
    }
  }
}

void CodeGen::executeScopeUnwinding(size_t targetDepth) {
  llvm::BasicBlock *currBB = m_Builder.GetInsertBlock();
  if (!currBB || currBB->getTerminator())
    return;

  // Cleanup scopes from high to low (up to but not including targetDepth)
  for (int i = (int)m_ScopeStack.size() - 1; i >= (int)targetDepth; --i) {
    auto &scope = m_ScopeStack[i];
    for (auto it = scope.rbegin(); it != scope.rend(); ++it) {
      if (!it->HasDrop)
        continue;
      llvm::BasicBlock *dropFlagContBB = nullptr;
      if (it->DropFlag) {
        llvm::Function *function = currBB->getParent();
        llvm::BasicBlock *dropBB =
            llvm::BasicBlock::Create(m_Context, "drop.live", function);
        dropFlagContBB =
            llvm::BasicBlock::Create(m_Context, "drop.done", function);
        llvm::Value *isLive = m_Builder.CreateLoad(
            llvm::Type::getInt1Ty(m_Context), it->DropFlag, "drop.is_live");
        m_Builder.CreateCondBr(isLive, dropBB, dropFlagContBB);
        m_Builder.SetInsertPoint(dropBB);
        currBB = dropBB;
      }

      if (it->IsShared && it->Alloca && it->AllocType) {
        llvm::Type *shTy = it->AllocType;
        llvm::Value *sh = nullptr;

        if (shTy->isStructTy()) {
          sh = m_Builder.CreateLoad(shTy, it->Alloca, "sh_pop");
        } else if (shTy->isPointerTy() && m_Symbols.count(it->Name)) {
          // [Fix] Captured Shared Pointer (Indirect)
          TokaSymbol &sym = m_Symbols[it->Name];
          if (sym.soulType) {
            llvm::Value *handlePtr =
                m_Builder.CreateLoad(shTy, it->Alloca, "sh_cap_ptr");
            llvm::Type *elemPtrTy = llvm::PointerType::getUnqual(m_Context);
            llvm::Type *refPtrTy =
                llvm::PointerType::getUnqual(llvm::Type::getInt32Ty(m_Context));
            llvm::StructType *handleTy =
                llvm::StructType::get(m_Context, {elemPtrTy, refPtrTy});
            sh = m_Builder.CreateLoad(handleTy, handlePtr, "sh_cap_load");
          }
        }

        if (sh) {
          llvm::Value *refPtr = m_Builder.CreateExtractValue(sh, 1, "ref_ptr");
          llvm::Value *refIsNotNull =
              m_Builder.CreateIsNotNull(refPtr, "ref_not_null");

          llvm::Function *f = currBB->getParent();
          if (f) {
            llvm::BasicBlock *decBB =
                llvm::BasicBlock::Create(m_Context, "sh_dec", f);
            llvm::BasicBlock *afterDecBB =
                llvm::BasicBlock::Create(m_Context, "sh_after_dec", f);

            m_Builder.CreateCondBr(refIsNotNull, decBB, afterDecBB);
            m_Builder.SetInsertPoint(decBB);

            // Option A: All shared pointer refcount operations are atomic by default (@arc)
            bool isAtomic = true;

            llvm::Value *isZero = nullptr;
            if (isAtomic) {
              // Release/Dec uses AcquireRelease memory ordering to sync prior writes before dropping
              llvm::Value *oldCnt = m_Builder.CreateAtomicRMW(llvm::AtomicRMWInst::Sub, refPtr, m_Builder.getInt32(1), llvm::MaybeAlign(4), llvm::AtomicOrdering::AcquireRelease);
              isZero = m_Builder.CreateICmpEQ(oldCnt, llvm::ConstantInt::get(llvm::Type::getInt32Ty(m_Context), 1));
            } else {
              llvm::Value *count = m_Builder.CreateLoad(
                  llvm::Type::getInt32Ty(m_Context), refPtr, "ref_count");
              llvm::Value *dec = m_Builder.CreateSub(
                  count,
                  llvm::ConstantInt::get(llvm::Type::getInt32Ty(m_Context), 1));
              m_Builder.CreateStore(dec, refPtr);
              isZero = m_Builder.CreateICmpEQ(
                  dec,
                  llvm::ConstantInt::get(llvm::Type::getInt32Ty(m_Context), 0));
            }

            llvm::BasicBlock *freeBB =
                llvm::BasicBlock::Create(m_Context, "sh_free", f);
            // Reuse afterDecBB as continuation
            m_Builder.CreateCondBr(isZero, freeBB, afterDecBB);

            m_Builder.SetInsertPoint(freeBB);

            // [Fix] Drop BEFORE Free
            llvm::Value *data = m_Builder.CreateExtractValue(sh, 0, "data_ptr");

            // Check data for null (short-circuit logic if nullable)
            // Even if it's not strictly nullable, checking data != null is safe
            // practice here before calling drop, but crucial if morphology is
            // '?'
            llvm::Value *dataNN = m_Builder.CreateIsNotNull(data, "sh_data_nn");

            llvm::BasicBlock *dropBB =
                llvm::BasicBlock::Create(m_Context, "sh_drop", f);
            llvm::BasicBlock *realFreeBB =
                llvm::BasicBlock::Create(m_Context, "sh_dealloc", f);

            m_Builder.CreateCondBr(dataNN, dropBB, realFreeBB);
            m_Builder.SetInsertPoint(dropBB);

            if (it->HasDrop) {
              std::string cleanName = it->SoulName;
              if (cleanName.find("Uninit<") == 0) {
                 // Bypass dropping
              } else if (m_Symbols.count(it->Name) && m_Symbols[it->Name].soulTypeObj && m_Symbols[it->Name].soulTypeObj->getPointeeType() && m_Symbols[it->Name].soulTypeObj->getPointeeType()->isSlice()) {
                  llvm::Type *cbTy = llvm::StructType::get(m_Context, {llvm::Type::getInt32Ty(m_Context), llvm::Type::getInt64Ty(m_Context)});
                  llvm::Value *lenAddr = m_Builder.CreateStructGEP(cbTy, refPtr, 1, "sh_len_addr");
                  llvm::Value *lenVal = m_Builder.CreateLoad(llvm::Type::getInt64Ty(m_Context), lenAddr, "sh_len");
                  
                  std::string elemTypeName = cleanName;
                  auto pointee = m_Symbols[it->Name].soulTypeObj->getPointeeType();
                  elemTypeName = pointee->toString();
                  while (!elemTypeName.empty() && elemTypeName.back() == '#') {
                      elemTypeName.pop_back();
                  }
                  if (!elemTypeName.empty() && elemTypeName.front() == '[' && elemTypeName.back() == ']') {
                      elemTypeName = elemTypeName.substr(1, elemTypeName.length() - 2);
                  }
                  
                  bool bypassDrop = (elemTypeName.find("Uninit<") == 0);
                  
                  if (!bypassDrop) {
                      llvm::BasicBlock *loopBB = llvm::BasicBlock::Create(m_Context, "drop_loop", f);
                      llvm::BasicBlock *afterBB = llvm::BasicBlock::Create(m_Context, "drop_after", f);

                      llvm::Value *isZero = m_Builder.CreateICmpEQ(
                          lenVal, llvm::ConstantInt::get(llvm::Type::getInt64Ty(m_Context), 0), "is_zero");
                      m_Builder.CreateCondBr(isZero, afterBB, loopBB);
                      m_Builder.SetInsertPoint(loopBB);
                      
                      llvm::PHINode *iVar = m_Builder.CreatePHI(llvm::Type::getInt64Ty(m_Context), 2, "i");
                      iVar->addIncoming(llvm::ConstantInt::get(llvm::Type::getInt64Ty(m_Context), 0), dropBB);
                      
                      llvm::Type *elemTy = resolveType(elemTypeName, false);
                      if (!elemTy) elemTy = llvm::Type::getInt8Ty(m_Context);
                      llvm::Value *elemPtr = m_Builder.CreateInBoundsGEP(elemTy, data, iVar);
                      
                      emitDropCascade(elemPtr, elemTypeName);
                      
                      llvm::Value *nextI = m_Builder.CreateAdd(iVar, llvm::ConstantInt::get(llvm::Type::getInt64Ty(m_Context), 1));
                      llvm::Value *cond = m_Builder.CreateICmpULT(nextI, lenVal);
                      llvm::BasicBlock *loopEndBB = m_Builder.GetInsertBlock();
                      iVar->addIncoming(nextI, loopEndBB);
                      m_Builder.CreateCondBr(cond, loopBB, afterBB);
                      m_Builder.SetInsertPoint(afterBB);
                  }
              } else {
                  TokaSymbol *sym = nullptr;
                  if (m_Symbols.count(it->Name))
                    sym = &m_Symbols[it->Name];
                  std::shared_ptr<Type> soulType =
                      sym && sym->soulTypeObj ? sym->soulTypeObj->getSoulType()
                                               : nullptr;
                  if (soulType && soulType->IsNullable && sym->soulType &&
                      sym->soulType->isStructTy()) {
                    llvm::StructType *nullableType =
                        llvm::cast<llvm::StructType>(sym->soulType);
                    llvm::Value *payload = m_Builder.CreateLoad(
                        nullableType, data,
                        "nullable_soul.shared_scope_payload");
                    llvm::Value *present = m_Builder.CreateExtractValue(
                        payload, 1, "nullable_soul.shared_scope_present");
                    llvm::BasicBlock *dropPayloadBB = llvm::BasicBlock::Create(
                        m_Context, "nullable_soul.shared_scope_drop", f);
                    llvm::BasicBlock *afterPayloadDropBB =
                        llvm::BasicBlock::Create(
                            m_Context, "nullable_soul.shared_scope_done", f);
                    m_Builder.CreateCondBr(present, dropPayloadBB,
                                            afterPayloadDropBB);
                    m_Builder.SetInsertPoint(dropPayloadBB);
                    emitDropCascade(data, cleanName);
                    m_Builder.CreateBr(afterPayloadDropBB);
                    m_Builder.SetInsertPoint(afterPayloadDropBB);
                  } else {
                    emitDropCascade(data, cleanName);
                  }
              }
            }
            m_Builder.CreateBr(realFreeBB);
            m_Builder.SetInsertPoint(realFreeBB);

            llvm::Function *freeFunc = m_Module->getFunction("free");
            if (freeFunc) {
              m_Builder.CreateCall(
                  freeFunc, m_Builder.CreateBitCast(
                                data, llvm::PointerType::getUnqual(m_Context)));
              m_Builder.CreateCall(
                  freeFunc, m_Builder.CreateBitCast(
                                refPtr, llvm::PointerType::getUnqual(m_Context)));
            }
            m_Builder.CreateBr(afterDecBB);

            m_Builder.SetInsertPoint(afterDecBB);
            currBB = afterDecBB;
          }
        }
      } else if (it->IsUniquePointer && it->Alloca && it->AllocType) {
        llvm::Value *ptr = m_Builder.CreateLoad(it->AllocType, it->Alloca);

        bool isFat = it->AllocType->isStructTy();
        llvm::Value *dataPtr = isFat ? m_Builder.CreateExtractValue(ptr, 0, "fat_data") : ptr;
        llvm::Value *lenVal = isFat ? m_Builder.CreateExtractValue(ptr, 1, "fat_len") : nullptr;
        
        // [Fix] Nullable Short-Circuit
        llvm::Value *notNull = m_Builder.CreateIsNotNull(dataPtr, "not_null");
        llvm::Function *f = currBB->getParent();
        if (f) {
          llvm::BasicBlock *valBB =
              llvm::BasicBlock::Create(m_Context, "un_valid", f);
          llvm::BasicBlock *contBB =
              llvm::BasicBlock::Create(m_Context, "un_cont", f);

          m_Builder.CreateCondBr(notNull, valBB, contBB);
          m_Builder.SetInsertPoint(valBB);

          if (it->HasDrop) {
            std::string cleanName = it->SoulName;
            
              if (cleanName.find("Uninit<") == 0) {
                 // Bypass 
            } else if (isFat) {
                std::string elemTypeName = cleanName;
                if (m_Symbols.count(it->Name)) {
                    auto pointee = m_Symbols[it->Name].soulTypeObj->getPointeeType();
                    if (pointee && pointee->isSlice()) {
                        elemTypeName = pointee->toString();
                        // Strip mutability markers before stripping brackets
                        while (!elemTypeName.empty() && elemTypeName.back() == '#') {
                            elemTypeName.pop_back();
                        }
                        if (!elemTypeName.empty() && elemTypeName.front() == '[' && elemTypeName.back() == ']') {
                            elemTypeName = elemTypeName.substr(1, elemTypeName.length() - 2);
                        }
                    }
                }
                
                
                bool bypassDrop = (elemTypeName.find("Uninit<") == 0);
                
                if (!bypassDrop) {
                    llvm::BasicBlock *loopBB = llvm::BasicBlock::Create(m_Context, "drop_loop", f);
                    llvm::BasicBlock *afterBB = llvm::BasicBlock::Create(m_Context, "drop_after", f);

                    llvm::Value *isZero = m_Builder.CreateICmpEQ(
                        lenVal, llvm::ConstantInt::get(llvm::Type::getInt64Ty(m_Context), 0), "is_zero");
                    m_Builder.CreateCondBr(isZero, afterBB, loopBB);
                    m_Builder.SetInsertPoint(loopBB);
                    
                    llvm::PHINode *iVar = m_Builder.CreatePHI(llvm::Type::getInt64Ty(m_Context), 2, "i");
                    iVar->addIncoming(llvm::ConstantInt::get(llvm::Type::getInt64Ty(m_Context), 0), valBB);
                    
                    llvm::Type *elemTy = resolveType(elemTypeName, false);
                    if (!elemTy) elemTy = llvm::Type::getInt8Ty(m_Context);
                    llvm::Value *elemPtr = m_Builder.CreateInBoundsGEP(elemTy, dataPtr, iVar);
                    
                    emitDropCascade(elemPtr, elemTypeName);
                    
                    llvm::Value *nextI = m_Builder.CreateAdd(iVar, llvm::ConstantInt::get(llvm::Type::getInt64Ty(m_Context), 1));
                    llvm::Value *cond = m_Builder.CreateICmpULT(nextI, lenVal);
                    llvm::BasicBlock *loopEndBB = m_Builder.GetInsertBlock();
                    iVar->addIncoming(nextI, loopEndBB);
                    m_Builder.CreateCondBr(cond, loopBB, afterBB);
                    m_Builder.SetInsertPoint(afterBB);
                }
            } else {
                bool nullableSoul = false;
                llvm::StructType *nullableType = nullptr;
                auto symbol = m_Symbols.find(it->Name);
                if (symbol != m_Symbols.end() && symbol->second.soulTypeObj &&
                    symbol->second.soulTypeObj->getSoulType()->IsNullable &&
                    symbol->second.soulType &&
                    symbol->second.soulType->isStructTy()) {
                  nullableSoul = true;
                  nullableType =
                      llvm::cast<llvm::StructType>(symbol->second.soulType);
                }
                if (nullableSoul) {
                  llvm::Value *payload = m_Builder.CreateLoad(
                      nullableType, dataPtr, "nullable_soul.scope_payload");
                  llvm::Value *present = m_Builder.CreateExtractValue(
                      payload, 1, "nullable_soul.scope_present");
                  llvm::BasicBlock *dropPayloadBB = llvm::BasicBlock::Create(
                      m_Context, "nullable_soul.scope_drop", f);
                  llvm::BasicBlock *afterPayloadDropBB =
                      llvm::BasicBlock::Create(m_Context,
                                                "nullable_soul.scope_done", f);
                  m_Builder.CreateCondBr(present, dropPayloadBB,
                                         afterPayloadDropBB);
                  m_Builder.SetInsertPoint(dropPayloadBB);
                  emitDropCascade(dataPtr, cleanName);
                  m_Builder.CreateBr(afterPayloadDropBB);
                  m_Builder.SetInsertPoint(afterPayloadDropBB);
                } else {
                  emitDropCascade(dataPtr, cleanName);
                }
            }
          }

          llvm::Function *freeFunc = m_Module->getFunction("free");
          if (freeFunc) {
            m_Builder.CreateCall(
                freeFunc, m_Builder.CreateBitCast(
                              dataPtr, llvm::PointerType::getUnqual(m_Context)));
          }
          m_Builder.CreateBr(contBB);

          m_Builder.SetInsertPoint(contBB);
          currBB = contBB;
        }
      } else if (it->HasDrop && it->Alloca) {
        if (it->DropType) {
          if (it->DropMask && it->DropType->isArray()) {
            emitDropForTypeWithMask(it->Alloca, it->DropType, it->DropMask);
          } else if (it->DropMask) {
            auto shape = std::dynamic_pointer_cast<ShapeType>(
                it->DropType->getSoulType());
            if (shape && shape->Decl) {
              const std::string &dropIdentity =
                  shape->Decl->OwnerLinkName.empty()
                      ? (shape->Decl->CodegenName.empty()
                             ? shape->Decl->Name
                             : shape->Decl->CodegenName)
                      : shape->Decl->OwnerLinkName;
              emitDropCascadeWithMask(it->Alloca, dropIdentity,
                                      it->DropMask);
            } else {
              emitDropCascadeWithMask(it->Alloca, it->SoulName,
                                      it->DropMask);
            }
          } else {
            emitDropForType(it->Alloca, it->DropType);
          }
        } else {
          std::string cleanName = it->SoulName;
          if (it->DropMask)
          emitDropCascadeWithMask(it->Alloca, cleanName, it->DropMask);
          else
          emitDropCascade(it->Alloca, cleanName);
        }
      }

      if (dropFlagContBB) {
        llvm::BasicBlock *dropEnd = m_Builder.GetInsertBlock();
        if (dropEnd && !dropEnd->getTerminator())
          m_Builder.CreateBr(dropFlagContBB);
        m_Builder.SetInsertPoint(dropFlagContBB);
        currBB = dropFlagContBB;
      }
    }
  }
}

llvm::Value *CodeGen::genUnsafeStmt(const UnsafeStmt *us) {
  return genStmt(us->Statement.get());
}

llvm::Value *CodeGen::genExprStmt(const ExprStmt *es) {
  PhysEntity result = genExpr(es->Expression.get());
  llvm::Value *value = result.load(m_Builder);

  const Expr *root = es->Expression.get();
  while (root) {
    if (auto *cast = dynamic_cast<const CastExpr *>(root)) {
      root = cast->Expression.get();
      continue;
    }
    if (auto *unsafeExpr = dynamic_cast<const UnsafeExpr *>(root)) {
      root = unsafeExpr->Expression.get();
      continue;
    }
    if (auto *postfix = dynamic_cast<const PostfixExpr *>(root)) {
      root = postfix->LHS.get();
      continue;
    }
    break;
  }

  const bool isOwnedRvalue =
      dynamic_cast<const CedeExpr *>(root) ||
      dynamic_cast<const AwaitExpr *>(root) ||
      dynamic_cast<const CallExpr *>(root) ||
      dynamic_cast<const MethodCallExpr *>(root) ||
      dynamic_cast<const InitStructExpr *>(root) ||
      dynamic_cast<const ArrayInitExpr *>(root) ||
      dynamic_cast<const NewExpr *>(root) ||
      dynamic_cast<const AllocExpr *>(root);
  if (isOwnedRvalue && es->Expression->ResolvedType && value &&
      !value->getType()->isVoidTy()) {
    llvm::Value *address = result.isAddress ? result.value : nullptr;
    if (!address) {
      address = createEntryBlockAlloca(value->getType(), nullptr,
                                       "unused.result.tmp");
      m_Builder.CreateStore(value, address);
    }
    emitDropForType(address, es->Expression->ResolvedType);
  }
  return value;
}

llvm::Value *CodeGen::genUnreachableStmt(const UnreachableStmt *stmt) {
  return m_Builder.CreateUnreachable();
}

llvm::Value *CodeGen::genGuardBindStmt(const GuardBindStmt *gbs) {
  // A reference binding must retain the guard target's storage identity.
  // This applies equally to a top-level `&binding` and to a reference nested
  // in a destructuring pattern.  Staging its loaded value in an alloca would
  // make that binding point at a copy rather than the guarded lvalue.
  std::function<bool(const MatchArm::Pattern *)> hasReferenceBinding =
      [&](const MatchArm::Pattern *pat) {
        if (!pat)
          return false;
        if (pat->PatternKind == MatchArm::Pattern::Variable &&
            pat->IsReference)
          return true;
        for (const auto &sub : pat->SubPatterns) {
          if (hasReferenceBinding(sub.get()))
            return true;
        }
        return false;
      };
  llvm::Value *targetAddr = nullptr;
  llvm::Value *targetVal = nullptr;
  llvm::Type *targetType = nullptr;
  bool usesSourceAddr = false;

  if (hasReferenceBinding(gbs->Pat.get()) && gbs->Target &&
      gbs->Target->ResolvedType) {
    targetAddr = genAddr(gbs->Target.get());
    targetType = getLLVMType(gbs->Target->ResolvedType);
    if (targetAddr && targetType && !targetType->isVoidTy()) {
      targetVal = m_Builder.CreateLoad(targetType, targetAddr, "guard_target");
      usesSourceAddr = true;
    } else {
      targetAddr = nullptr;
    }
  }

  if (!targetVal) {
    PhysEntity targetValEnt = genExpr(gbs->Target.get());
    targetVal = targetValEnt.load(m_Builder);
    targetType = targetVal->getType();
  }
  
  std::string targetTypeStr = "unknown";
  if (targetType->isStructTy() && m_TypeToName.count(targetType)) {
      targetTypeStr = m_TypeToName[targetType];
  } else if (gbs->Target->ResolvedType) {
      targetTypeStr = gbs->Target->ResolvedType->toString();
  }

  if (!targetAddr) {
    targetAddr = createEntryBlockAlloca(targetType, nullptr, "guard_target_addr");
    m_Builder.CreateStore(targetVal, targetAddr);
  }

  std::string baseShapeName = targetTypeStr;
  if (baseShapeName.find('<') != std::string::npos) {
    baseShapeName = baseShapeName.substr(0, baseShapeName.find('<'));
  }

  if (!usesSourceAddr && gbs->Target && gbs->Target->ExtendLifetime && !m_ScopeStack.empty()) {
      bool hasDrop = false;
      if (!baseShapeName.empty() && m_Shapes.count(baseShapeName)) {
          hasDrop = true;
      }
      if (hasDrop) {
          VariableScopeInfo vsi;
          static int extendIdCount = 0;
          vsi.Name = ".guard_ext_" + std::to_string(extendIdCount++);
          vsi.Alloca = targetAddr;
          vsi.AllocType = targetType;
          vsi.IsUniquePointer = false;
          vsi.IsShared = false;
          vsi.HasDrop = true;
          vsi.SoulName = targetTypeStr;
          m_ScopeStack.back().push_back(vsi);
      }
  }

  llvm::Function *func = m_Builder.GetInsertBlock()->getParent();
  llvm::BasicBlock *contBB = llvm::BasicBlock::Create(m_Context, "guard_cont", func);
  llvm::BasicBlock *elseBB = llvm::BasicBlock::Create(m_Context, "guard_else", func);

  int expectedTag = -1;
  const ShapeMember *variant = nullptr;
  
  if (!baseShapeName.empty() && m_Shapes.count(baseShapeName) && m_Shapes[baseShapeName]->Kind == ShapeKind::Enum) {
    const ShapeDecl *sh = m_Shapes[baseShapeName];
    std::string patName = gbs->Pat->Name;
    size_t scopePos = patName.rfind("::");
    if (scopePos != std::string::npos) patName = patName.substr(scopePos + 2);
    for (size_t i = 0; i < sh->Members.size(); ++i) {
      if (sh->Members[i].Name == patName) {
        expectedTag = (sh->Members[i].TagValue == -1) ? (int)i : (int)sh->Members[i].TagValue;
        variant = &sh->Members[i];
        break;
      }
    }
  }

  bool isNullableSoul = (gbs->Target && gbs->Target->ResolvedType && gbs->Target->ResolvedType->IsNullable);

  if (expectedTag != -1) {
    llvm::Value *tagVal = m_Builder.CreateExtractValue(targetVal, 0, "tag");
    llvm::Value *cond = m_Builder.CreateICmpEQ(
        tagVal, llvm::ConstantInt::get(tagVal->getType(), expectedTag), "guard_cond");
    m_Builder.CreateCondBr(cond, contBB, elseBB);
  } else if (isNullableSoul) {
    llvm::Value *isNullVal = m_Builder.CreateExtractValue(targetVal, 1, "is_null");
    llvm::Value *cond = m_Builder.CreateICmpEQ(
        isNullVal, llvm::ConstantInt::get(isNullVal->getType(), 0), "guard_cond_not_null");
    m_Builder.CreateCondBr(cond, contBB, elseBB);
  } else {
    m_Builder.CreateBr(contBB);
  }

  // --- Else Block ---
  llvm::BasicBlock *savedBB = m_Builder.GetInsertBlock();
  m_Builder.SetInsertPoint(elseBB);
  
  // Notice: The manual emitDropCascade(targetAddr) was REMOVED here.
  // We now rely on Temporary Lifetime Extension via m_ScopeStack registration
  // OR the `else` body must properly drop it anyway if we didn't extend it.
  // Actually, wait, if ExtendLifetime is false, then we MUST drop it.
  
  if (!baseShapeName.empty() && m_Shapes.count(baseShapeName)) {
      if (!usesSourceAddr && (!gbs->Target || !gbs->Target->ExtendLifetime)) {
          emitDropCascade(targetAddr, targetTypeStr);
      }
  }
  
  genStmt(gbs->ElseBody.get());
  
  if (!m_Builder.GetInsertBlock()->getTerminator()) {
     m_Builder.CreateUnreachable();
  }

  // --- Cont Block ---
  m_Builder.SetInsertPoint(contBB);
  
  if (variant && !gbs->Pat->SubPatterns.empty()) {
      if (gbs->Pat->SubPatterns.size() == 1 &&
          gbs->Pat->SubPatterns[0]->IsReference &&
          variant->SubMembers.size() == 1 &&
          variant->SubMembers[0].ResolvedType &&
          variant->SubMembers[0].ResolvedType->isReference()) {
          llvm::Value *payloadAddr = m_Builder.CreateStructGEP(
              targetType, targetAddr, 1, "enum_payload_addr");
          llvm::Value *payloadRef = m_Builder.CreateLoad(
              llvm::PointerType::getUnqual(m_Context), payloadAddr,
              "enum_ref_payload");
          genPatternBinding(gbs->Pat->SubPatterns[0].get(), payloadRef,
                            llvm::PointerType::getUnqual(m_Context),
                            variant->SubMembers[0].ResolvedType);
          return nullptr;
      }
      if (variant->Type == "void" && gbs->Pat->SubPatterns.size() == 1 &&
          gbs->Pat->SubPatterns[0]->IsReference) {
          llvm::Value *payloadAddr = m_Builder.CreateStructGEP(
              targetType, targetAddr, 1, "enum_payload_addr");
          std::shared_ptr<Type> payloadTypeObj = nullptr;
          if (variant->SubMembers.size() == 1 &&
              variant->SubMembers[0].ResolvedType) {
            payloadTypeObj = variant->SubMembers[0].ResolvedType;
          }
          std::shared_ptr<Type> targetTypeObj = nullptr;
          // The target carries the instantiated generic arguments.  The
          // pattern spelling (for example `Result::Ok`) only names the
          // uninstantiated shape and must therefore be a fallback.
          if (gbs->Target)
            targetTypeObj = gbs->Target->ResolvedType;
          if (!targetTypeObj) {
            size_t scopePos = gbs->Pat->Name.rfind("::");
            if (scopePos != std::string::npos) {
              std::string patternShape = gbs->Pat->Name.substr(0, scopePos);
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
            // A `&binding` of an owned generic payload borrows its enum
            // storage.  Loading the first word here would turn a value such
            // as `MutexLock { handle, data }` into a fabricated pointer.
            genPatternBinding(gbs->Pat->SubPatterns[0].get(), payloadAddr,
                              getLLVMType(payloadTypeObj),
                              payloadTypeObj);
          } else {
            // A reference-valued payload stores the referent address in the
            // enum storage; only that case loads the slot itself.
            llvm::Value *payloadRef = m_Builder.CreateLoad(
                llvm::PointerType::getUnqual(m_Context), payloadAddr,
                "enum_ref_payload");
            genPatternBinding(gbs->Pat->SubPatterns[0].get(), payloadRef,
                              llvm::PointerType::getUnqual(m_Context),
                              payloadTypeObj);
          }
          return nullptr;
      }
      llvm::Value *payloadAddr = m_Builder.CreateStructGEP(targetType, targetAddr, 1, "enum_payload_addr");
      llvm::Type *payloadLayoutType = nullptr;
      std::vector<llvm::Type*> fieldTypes;
      
      if (!variant->SubMembers.empty()) {
          for (const auto &f : variant->SubMembers) {
              fieldTypes.push_back(f.ResolvedType
                                       ? getLLVMType(f.ResolvedType)
                                       : resolveType(f.Type, false));
          }
          payloadLayoutType = llvm::StructType::get(m_Context, fieldTypes, false);
      } else if (!variant->Type.empty()) {
          payloadLayoutType = resolveType(variant->Type, false);
      }
      
      if (payloadLayoutType) {
          llvm::Value *variantAddr = m_Builder.CreateBitCast(payloadAddr, llvm::PointerType::getUnqual(m_Context), "variant_addr");
          for (size_t i = 0; i < gbs->Pat->SubPatterns.size(); ++i) {
              if (fieldTypes.empty() && i > 0) break;
              if (!fieldTypes.empty() && i >= fieldTypes.size()) break;

              llvm::Value *fieldAddr = variantAddr;
              llvm::Type *fieldTy = payloadLayoutType;

              if (!fieldTypes.empty()) {
                  fieldAddr = m_Builder.CreateStructGEP(payloadLayoutType, variantAddr, i);
                  fieldTy = fieldTypes[i];
              }

              std::shared_ptr<Type> subTypeObj = nullptr;
              if (variant->SubMembers.size() > i && variant->SubMembers[i].ResolvedType) {
                  subTypeObj = variant->SubMembers[i].ResolvedType;
              } else if (variant->ResolvedType) {
                  subTypeObj = variant->ResolvedType;
              }
              genPatternBinding(gbs->Pat->SubPatterns[i].get(), fieldAddr, fieldTy, subTypeObj);
          }
      }
  } else if (isNullableSoul && gbs->Pat->PatternKind == MatchArm::Pattern::Variable) {
      llvm::Value *payloadAddr = m_Builder.CreateStructGEP(targetType, targetAddr, 0, "nullable_payload_addr");
      llvm::Type *payloadType = targetType->getStructElementType(0);
      auto subTypeObj = gbs->Target->ResolvedType->withAttributes(gbs->Target->ResolvedType->IsWritable, false, gbs->Target->ResolvedType->IsBlocked);
      genPatternBinding(gbs->Pat.get(), payloadAddr, payloadType, subTypeObj);
  } else if (!variant && gbs->Pat->PatternKind == MatchArm::Pattern::Variable) {
      genPatternBinding(gbs->Pat.get(), targetAddr, targetType, gbs->Target->ResolvedType);
  } else if (!variant && gbs->Pat->PatternKind == MatchArm::Pattern::Decons) {
      genPatternBinding(gbs->Pat.get(), targetAddr, targetType,
                        gbs->Target->ResolvedType);
  }
  
  return nullptr;
}

void CodeGen::genCoroutineReturn(llvm::Value *retVal) {
    if (m_CurrentCoroPromiseType) {
        if (!m_CurrentCoroRetTy->isVoidTy() && retVal) {
            llvm::Function *valuePtrFn = m_Module->getFunction("toka_task_result_value_ptr");
            if (!valuePtrFn) {
                llvm::FunctionType *ft = llvm::FunctionType::get(m_Builder.getPtrTy(), {m_Builder.getPtrTy()}, false);
                valuePtrFn = llvm::Function::Create(ft, llvm::Function::ExternalLinkage, "toka_task_result_value_ptr", m_Module.get());
            }
            llvm::Value *valPtrRaw = m_Builder.CreateCall(valuePtrFn, {m_CurrentCoroPromise}, "coro.ret.value.ptr");
            llvm::Value *valPtr = m_Builder.CreatePointerCast(valPtrRaw, llvm::PointerType::getUnqual(m_CurrentCoroRetTy));
            if (retVal->getType() != m_CurrentCoroRetTy) {
                llvm::Type *srcTy = retVal->getType();
                llvm::Type *dstTy = m_CurrentCoroRetTy;
                if (srcTy->isIntegerTy() && dstTy->isIntegerTy()) {
                    retVal = m_Builder.CreateIntCast(retVal, dstTy, false);
                } else if (srcTy->isFloatingPointTy() && dstTy->isFloatingPointTy()) {
                    retVal = m_Builder.CreateFPCast(retVal, dstTy);
                } else {
                    if (srcTy->isAggregateType() || dstTy->isAggregateType()) {
                        llvm::Value *temp = m_Builder.CreateAlloca(dstTy, nullptr, "coro.ret.cast.temp");
                        llvm::Value *tempSrcPtr = m_Builder.CreatePointerCast(temp, llvm::PointerType::get(srcTy, 0));
                        m_Builder.CreateStore(retVal, tempSrcPtr);
                        retVal = m_Builder.CreateLoad(dstTy, temp, "coro.ret.cast.res");
                        uint64_t sizeInBytes = m_Module->getDataLayout().getTypeAllocSize(dstTy);
                        m_Builder.CreateMemSet(temp, m_Builder.getInt8(0xFF), sizeInBytes, llvm::MaybeAlign());
                    } else {
                        retVal = m_Builder.CreateBitCast(retVal, dstTy);
                    }
                }
            }
            m_Builder.CreateStore(retVal, valPtr);
        }
        // The runtime's Pending -> ReadyLive CAS is also the unique terminal
        // publication claim. Do not pre-publish a result state here: a racing
        // cancellation must not observe a payload without its finalization.
        llvm::Function *completeFn = m_Module->getFunction("toka_task_complete");
        if (!completeFn) {
            llvm::FunctionType *ft = llvm::FunctionType::get(m_Builder.getVoidTy(), {m_Builder.getPtrTy()}, false);
            completeFn = llvm::Function::Create(ft, llvm::Function::ExternalLinkage, "toka_task_complete", m_Module.get());
        }
        m_Builder.CreateCall(completeFn, {m_CurrentCoroPromise});
    } // Close if (m_CurrentCoroPromiseType)

    if (!m_CurrentCoroFinalSuspendBB) {
        // Save current insert point
        llvm::BasicBlock *savedBB = m_Builder.GetInsertBlock();
        
        m_CurrentCoroFinalSuspendBB = llvm::BasicBlock::Create(m_Context, "coro.suspend.final", savedBB->getParent());
        m_Builder.SetInsertPoint(m_CurrentCoroFinalSuspendBB);
        
        llvm::Function *suspendFn = llvm::Intrinsic::getOrInsertDeclaration(m_Module.get(), llvm::Intrinsic::coro_suspend);
        llvm::BasicBlock *cleanupBB = llvm::BasicBlock::Create(m_Context, "coro.cleanup", savedBB->getParent());
        llvm::BasicBlock *trapBB = llvm::BasicBlock::Create(m_Context, "coro.trap", savedBB->getParent());
        
        llvm::Value *suspendRes = m_Builder.CreateCall(suspendFn, {llvm::ConstantTokenNone::get(m_Context), m_Builder.getInt1(true)});
        
        llvm::SwitchInst *sw = m_Builder.CreateSwitch(suspendRes, m_CurrentCoroSuspendRetBB, 2);
        sw->addCase(m_Builder.getInt8(0), trapBB);
        sw->addCase(m_Builder.getInt8(1), cleanupBB);
        
        m_Builder.SetInsertPoint(trapBB);
        m_Builder.CreateUnreachable();
        
        m_Builder.SetInsertPoint(cleanupBB);
        if (m_CurrentCoroCleanupBB) {
            m_Builder.CreateBr(m_CurrentCoroCleanupBB);
        } else {
            m_Builder.CreateUnreachable();
        }
        
        // Restore insert point
        m_Builder.SetInsertPoint(savedBB);
    }
    
    m_Builder.CreateBr(m_CurrentCoroFinalSuspendBB);
    
    // Create a dummy block so subsequent instructions don't complain
    m_Builder.SetInsertPoint(llvm::BasicBlock::Create(m_Context, "coro.dead", m_Builder.GetInsertBlock()->getParent()));
}

void CodeGen::genCoroutineCancelReturn() {
    // Cancellation is a terminal scope exit, not merely a promise-state
    // change.  Run the same ownership cleanup as an ordinary return before
    // publishing completion, so awaiters cannot observe a canceled task whose
    // local resources (including partial-cede drop masks) are still live.
    executeScopeUnwinding(0);

    if (m_CurrentCoroPromiseType) {
        llvm::Function *completeCanceledFn = m_Module->getFunction("toka_task_complete_canceled");
        if (!completeCanceledFn) {
            llvm::FunctionType *ft = llvm::FunctionType::get(m_Builder.getVoidTy(), {m_Builder.getPtrTy()}, false);
            completeCanceledFn = llvm::Function::Create(ft, llvm::Function::ExternalLinkage, "toka_task_complete_canceled", m_Module.get());
        }
        m_Builder.CreateCall(completeCanceledFn, {m_CurrentCoroPromise});
    }

    if (!m_CurrentCoroFinalSuspendBB) {
        llvm::BasicBlock *savedBB = m_Builder.GetInsertBlock();
        m_CurrentCoroFinalSuspendBB = llvm::BasicBlock::Create(m_Context, "coro.suspend.final", savedBB->getParent());
        m_Builder.SetInsertPoint(m_CurrentCoroFinalSuspendBB);
        llvm::Function *suspendFn = llvm::Intrinsic::getOrInsertDeclaration(m_Module.get(), llvm::Intrinsic::coro_suspend);
        llvm::BasicBlock *cleanupBB = llvm::BasicBlock::Create(m_Context, "coro.cleanup", savedBB->getParent());
        llvm::BasicBlock *trapBB = llvm::BasicBlock::Create(m_Context, "coro.trap", savedBB->getParent());

        llvm::Value *suspendRes = m_Builder.CreateCall(suspendFn, {llvm::ConstantTokenNone::get(m_Context), m_Builder.getInt1(true)});

        llvm::SwitchInst *sw = m_Builder.CreateSwitch(suspendRes, m_CurrentCoroSuspendRetBB, 2);
        sw->addCase(m_Builder.getInt8(0), trapBB);
        sw->addCase(m_Builder.getInt8(1), cleanupBB);

        m_Builder.SetInsertPoint(trapBB);
        m_Builder.CreateUnreachable();

        m_Builder.SetInsertPoint(cleanupBB);
        if (m_CurrentCoroCleanupBB) {
            m_Builder.CreateBr(m_CurrentCoroCleanupBB);
        } else {
            m_Builder.CreateUnreachable();
        }

        m_Builder.SetInsertPoint(savedBB);
    }

    m_Builder.CreateBr(m_CurrentCoroFinalSuspendBB);
    m_Builder.SetInsertPoint(llvm::BasicBlock::Create(m_Context, "coro.dead.cancel", m_Builder.GetInsertBlock()->getParent()));
}

} // namespace toka
