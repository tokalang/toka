#include "toka/CodeGen.h"

namespace toka {

PhysEntity CodeGen::genRawTakeExpr(const RawTakeExpr *take) {
  auto plan = take->Plan;
#ifdef TOKA_BUILD_TESTING
  if (!m_RawTakeFaultConsumed && !m_RawTakeFault.empty()) {
    m_RawTakeFaultConsumed = true;
    if (m_RawTakeFault == "missing") plan.reset();
    else if (plan) {
      if (m_RawTakeFault == "rejected") plan->SemaValidated = false;
      else if (m_RawTakeFault == "mismatch") plan->SlotEdge = nullptr;
      else if (m_RawTakeFault == "incomplete") plan->DependencyFree = false;
      else if (m_RawTakeFault == "production-none") plan->Production = TransferValueProduction::None;
      else if (m_RawTakeFault == "production-identity") plan->Production = TransferValueProduction::CopyIdentity;
      else if (m_RawTakeFault == "production-borrow") plan->Production = TransferValueProduction::BorrowCapture;
      else if (m_RawTakeFault == "production-temporary") plan->Production = TransferValueProduction::ConsumeTemporary;
      else if (m_RawTakeFault == "production-unknown") plan->Production = static_cast<TransferValueProduction>(255);
      else if (m_RawTakeFault == "copy-proof") plan->CopyProof = TransferCopyProof::Indeterminate;
      else if (m_RawTakeFault == "drop") plan->CarriesDropLiability = !plan->CarriesDropLiability;
      else if (m_RawTakeFault == "storage-type") plan->StorageType.reset();
      else if (m_RawTakeFault == "index-type") plan->IndexType.reset();
    }
  }
#endif
  auto fail = [&]() -> PhysEntity {
    error(take, DiagID::ERR_CODEGEN, "raw_take requires a complete matching Sema plan");
    return {};
  };
  const auto *slot = dynamic_cast<const ArrayIndexExpr *>(take->Slot.get());
  // The qualified first batch has only variable raw bases. No general genAddr,
  // cast-to-pointer, guessed stride, or managed-storage fallback is permitted.
  const auto *base = slot ? dynamic_cast<const VariableExpr *>(slot->Array.get()) : nullptr;
  if (!plan || !plan->SemaValidated || !slot || !base ||
      slot != plan->SlotEdge || base != plan->BaseEdge || slot->Indices.size() != 1 ||
      slot->Indices[0].get() != plan->IndexEdge || !plan->SourceSlot.RootID ||
      plan->EdgeIdentity.empty() || !plan->ElementType || !take->ResolvedType ||
      !plan->ElementType->equals(*take->ResolvedType) || !slot->ResolvedType ||
      !plan->ElementType->equals(*slot->ResolvedType) || !plan->StorageType ||
      !base->ResolvedType || !plan->StorageType->equals(*base->ResolvedType) ||
      !plan->StorageType->isRawPointer() || !plan->IndexType ||
      !plan->IndexEdge->ResolvedType || !plan->IndexType->isInteger() ||
      !plan->IndexType->equals(*plan->IndexEdge->ResolvedType) ||
      !plan->BaseKnownNonNull || !plan->DependencyFree ||
      !plan->UnsafeCallerPreconditions || !plan->CallerMaintainsRemainder)
    return fail();
  // Positive, closed-world production table. Do not accept arbitrary non-None
  // dispositions (in particular CopyIdentity / ConsumeTemporary / future enums).
  switch (plan->Production) {
  case TransferValueProduction::CopyValue:
    if (plan->CopyProof != TransferCopyProof::ProvenCopy || plan->CarriesDropLiability ||
        plan->ElementType->isPointer()) return fail();
    break;
  case TransferValueProduction::MoveOwned:
    if (plan->CopyProof != TransferCopyProof::ProvenNonCopy ||
        plan->ElementType->isSharedPtr()) return fail();
    break;
  case TransferValueProduction::TransferShared:
    if (plan->CopyProof != TransferCopyProof::ProvenNonCopy ||
        !plan->ElementType->isSharedPtr() || !plan->CarriesDropLiability) return fail();
    break;
  default:
    return fail();
  }
  switch (plan->ResultCleanup) {
  case TransferDropDisposition::NoLiability:
    if (plan->CarriesDropLiability) return fail();
    break;
  case TransferDropDisposition::DestinationAssumesLiability:
    if (!plan->CarriesDropLiability || plan->Production == TransferValueProduction::CopyValue)
      return fail();
    break;
  default:
    return fail();
  }
  llvm::Type *elementType = getLLVMType(plan->ElementType);
  if (!elementType || !elementType->isSized() || elementType->isVoidTy()) return fail();

  // Fixed order: obtain the base identity once, then evaluate the index once.
  // The existing helper selects this variable's handle slot, never its payload.
  llvm::Value *handleSlot = emitHandleAddr(base);
  if (!handleSlot || !handleSlot->getType()->isPointerTy()) return fail();
  llvm::Value *pointer = m_Builder.CreateLoad(m_Builder.getPtrTy(), handleSlot, "raw.take.base");
  llvm::Value *offset = genExpr(plan->IndexEdge).load(m_Builder);
  if (!offset || !offset->getType()->isIntegerTy()) return fail();
  offset = m_Builder.CreateIntCast(offset, getIntPtrTy(), plan->IndexType->isSignedInteger());
  // Exact Sema element type is the only stride. No i8 fallback and no repeated
  // genAddr evaluation. Bounds/initialization are explicit unsafe preconditions.
  llvm::Value *address = m_Builder.CreateGEP(elementType, pointer, offset, "raw.take.slot");
  // The accepted unsafe contract leaves live-set/remainder maintenance with
  // the caller. This operation neither frees allocation nor retains/clones the
  // element. The complete result goes to the normal destination cleanup path.
  return PhysEntity(m_Builder.CreateLoad(elementType, address, "raw.take.value"),
                    plan->ElementType->toString(), elementType, false);
}

} // namespace toka
