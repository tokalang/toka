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
#include "toka/CodeGen.h"
#include "toka/MemberAccess.h"
#include <cctype>
#include <iostream>
#include <set>
#include <typeinfo>
#include <vector>

namespace toka {

struct CodeGen::MemberObjectInfo {
  llvm::Type *ObjectType = nullptr;
  llvm::StructType *StructTy = nullptr;
  std::string ShapeName;
};

struct CodeGen::MemberFieldInfo {
  llvm::StructType *StructTy = nullptr;
  int Index = -1;
  std::string StructName;
};

struct CodeGen::MemberFieldStorage {
  llvm::Value *Addr = nullptr;
  std::string TypeName;
  llvm::Type *IrTy = nullptr;
};

struct CodeGen::MemberHatPlan {
  int DefHats = 0;
  int AccessHats = 0;
  int DerefCount = 0;
  bool IsHatOn = false;
  bool IsIdentityAssertion = false;
};

struct CodeGen::MemberMaterialization {
  llvm::Value *Addr = nullptr;
  llvm::Type *IrTy = nullptr;
};

static int getTypeHatCount(std::shared_ptr<toka::Type> type) {
  if (!type)
    return 0;
  int count = 0;
  auto cur = type;
  while (cur &&
         (cur->isPointer() || cur->isReference() || cur->isSmartPointer())) {
    count++;
    cur = cur->getPointeeType();
  }
  return count;
}


static std::shared_ptr<toka::Type>
peelMemberObjectType(std::shared_ptr<toka::Type> type) {
  while (type &&
         (type->isPointer() || type->isReference() || type->isSmartPointer())) {
    auto next = type->getPointeeType();
    if (!next)
      break;
    type = next;
  }
  return type;
}

static bool isDerefObjectForMemberAccess(const UnaryExpr *ue) {
  return ue && (ue->Op == TokenType::Star || ue->Op == TokenType::Caret ||
                ue->Op == TokenType::Tilde ||
                ue->Op == TokenType::TokenNull);
}


static const ShapeDecl *shapeDeclFromType(std::shared_ptr<toka::Type> type) {
  auto soul = peelMemberObjectType(type);
  if (!soul || !soul->isShape())
    return nullptr;
  auto shapeType = std::dynamic_pointer_cast<ShapeType>(soul);
  return shapeType ? shapeType->Decl : nullptr;
}

static std::string resolveStructName(llvm::StructType *structTy,
                                     const std::string &shapeName,
                                     const std::map<llvm::Type *, std::string> &typeToName,
                                     const std::map<std::string, llvm::StructType *> &structTypes) {
  if (!shapeName.empty() && structTypes.count(shapeName) &&
      structTypes.at(shapeName) == structTy) {
    return shapeName;
  }
  auto typeIt = typeToName.find(structTy);
  if (typeIt != typeToName.end()) {
    return typeIt->second;
  }
  for (const auto &pair : structTypes) {
    if (pair.second == structTy) {
      return pair.first;
    }
  }
  return std::string();
}

static int findMemberIndexInFields(const std::vector<std::string> &fields,
                                   const std::string &name,
                                   bool allowPartialMatch) {
  for (size_t i = 0; i < fields.size(); i++) {
    std::string fieldName = stripMemberAccessMarkers(fields[i]);
    if (allowPartialMatch ? fieldName.find(name) != std::string::npos
                          : fieldName == name) {
      return i;
    }
  }
  return -1;
}




PhysEntity CodeGen::genAllocExpr(const AllocExpr *ae) {
  llvm::Function *allocHook = m_Module->getFunction("__toka_alloc");
  if (!allocHook) {
    allocHook = m_Module->getFunction("malloc");
  }
  if (!allocHook) {
    // Declare malloc if neither is present
    llvm::Type *sizeTy = getIntPtrTy();
    llvm::Type *retTy = m_Builder.getPtrTy();
    llvm::FunctionType *ft = llvm::FunctionType::get(retTy, {sizeTy}, false);
    allocHook = llvm::Function::Create(ft, llvm::Function::ExternalLinkage,
                                       "malloc", m_Module.get());
  }

  llvm::Type *elemTy = nullptr;
  if (ae->ResolvedType) {
    auto resTy = ae->ResolvedType;
    if (resTy->isPointer() || resTy->isSmartPointer()) {
      elemTy = getLLVMType(resTy->getPointeeType());
    } else {
      elemTy = getLLVMType(resTy);
    }
  } else {
    elemTy = resolveType(ae->TypeName, false);
  }
  const llvm::DataLayout &dl = m_Module->getDataLayout();
  uint64_t size = dl.getTypeAllocSize(elemTy);
  llvm::Value *sizeVal =
      llvm::ConstantInt::get(getIntPtrTy(), size);

  llvm::Value *arrayCount = nullptr;

  if (ae->IsArray && ae->ArraySize) {
    llvm::Value *count = genExpr(ae->ArraySize.get()).load(m_Builder);
    count = m_Builder.CreateIntCast(count, getIntPtrTy(),
                                    false);
    arrayCount = count;
    sizeVal = m_Builder.CreateMul(sizeVal, count);
  }

  llvm::CallInst *rawPtr = m_Builder.CreateCall(allocHook, sizeVal);
  markMemoryEvent(rawPtr, "allocate");
  llvm::Type *ptrTy = llvm::PointerType::getUnqual(m_Context);
  llvm::Value *castedPtr = m_Builder.CreateBitCast(rawPtr, ptrTy);

  if (ae->Initializer) {
    // Evaluate initializer once
    llvm::Value *initVal = genExpr(ae->Initializer.get()).load(m_Builder);

    if (arrayCount) {
      // Loop to initialize all elements
      llvm::BasicBlock *preHeaderBB = m_Builder.GetInsertBlock();
      llvm::Function *F = preHeaderBB->getParent();
      llvm::BasicBlock *loopBB =
          llvm::BasicBlock::Create(m_Context, "alloc_init_loop", F);
      llvm::BasicBlock *afterBB =
          llvm::BasicBlock::Create(m_Context, "alloc_init_after", F);

      m_Builder.CreateBr(loopBB);
      m_Builder.SetInsertPoint(loopBB);

      llvm::PHINode *iVar =
          m_Builder.CreatePHI(getIntPtrTy(), 2, "i");
      iVar->addIncoming(
          llvm::ConstantInt::get(getIntPtrTy(), 0),
          preHeaderBB);

      // GEP to element
      llvm::Value *elemPtr =
          m_Builder.CreateInBoundsGEP(elemTy, castedPtr, iVar);
      m_Builder.CreateStore(initVal, elemPtr);

      llvm::Value *nextI = m_Builder.CreateAdd(
          iVar, llvm::ConstantInt::get(getIntPtrTy(), 1));
      llvm::Value *cond = m_Builder.CreateICmpULT(nextI, arrayCount);
      iVar->addIncoming(nextI, loopBB);

      m_Builder.CreateCondBr(cond, loopBB, afterBB);
      m_Builder.SetInsertPoint(afterBB);
    } else {
      m_Builder.CreateStore(initVal, castedPtr);
    }
  }
  return castedPtr;
}

void CodeGen::emitDropForType(llvm::Value *ptrAddr,
                              const std::shared_ptr<Type> &type) {
  if (!ptrAddr || !type)
    return;

  if (type->isArray()) {
    auto elementType = type->getArrayElementType();
    auto *arrayType = llvm::dyn_cast<llvm::ArrayType>(getLLVMType(type));
    if (!elementType || !arrayType)
      return;
    for (uint64_t i = 0; i < arrayType->getNumElements(); ++i) {
      llvm::Value *elementAddr = m_Builder.CreateInBoundsGEP(
          arrayType, ptrAddr,
          {m_Builder.getInt32(0), m_Builder.getInt32(static_cast<unsigned>(i))},
          "drop.array.element");
      emitDropForType(elementAddr, elementType);
    }
    return;
  }

  if (type->IsNullable) {
    auto *nullableType = llvm::dyn_cast<llvm::StructType>(getLLVMType(type));
    if (nullableType && nullableType->getNumElements() == 2 &&
        nullableType->getStructElementType(1)->isIntegerTy(1)) {
      llvm::Function *function = m_Builder.GetInsertBlock()->getParent();
      llvm::BasicBlock *dropPayload =
          llvm::BasicBlock::Create(m_Context, "drop.nullable.live", function);
      llvm::BasicBlock *dropDone =
          llvm::BasicBlock::Create(m_Context, "drop.nullable.done", function);
      llvm::Value *presentAddr = m_Builder.CreateStructGEP(
          nullableType, ptrAddr, 1, "drop.nullable.present.addr");
      llvm::Value *present = m_Builder.CreateLoad(
          llvm::Type::getInt1Ty(m_Context), presentAddr,
          "drop.nullable.present");
      m_Builder.CreateCondBr(present, dropPayload, dropDone);
      m_Builder.SetInsertPoint(dropPayload);
      llvm::Value *payloadAddr = m_Builder.CreateStructGEP(
          nullableType, ptrAddr, 0, "drop.nullable.payload.addr");
      emitDropForType(payloadAddr,
                      type->withAttributes(type->IsWritable, false,
                                           type->IsBlocked));
      if (!m_Builder.GetInsertBlock()->getTerminator())
        m_Builder.CreateBr(dropDone);
      m_Builder.SetInsertPoint(dropDone);
      return;
    }
  }

  emitDropCascade(ptrAddr, type->getSoulName());
}

void CodeGen::emitDropForTypeWithMask(
    llvm::Value *ptrAddr, const std::shared_ptr<Type> &type,
    llvm::Value *dropMaskAddr) {
  if (!ptrAddr || !type || !dropMaskAddr || !type->isArray()) {
    emitDropForType(ptrAddr, type);
    return;
  }
  auto elementType = type->getArrayElementType();
  auto *arrayType = llvm::dyn_cast<llvm::ArrayType>(getLLVMType(type));
  if (!elementType || !arrayType || arrayType->getNumElements() > 64) {
    emitDropForType(ptrAddr, type);
    return;
  }
  for (uint64_t i = 0; i < arrayType->getNumElements(); ++i) {
    llvm::Function *function = m_Builder.GetInsertBlock()->getParent();
    llvm::BasicBlock *dropElement = llvm::BasicBlock::Create(
        m_Context, "drop.array.element.live", function);
    llvm::BasicBlock *nextElement = llvm::BasicBlock::Create(
        m_Context, "drop.array.element.done", function);
    llvm::Value *mask = m_Builder.CreateLoad(
        llvm::Type::getInt64Ty(m_Context), dropMaskAddr, "drop.array.mask");
    llvm::Value *isLive = m_Builder.CreateICmpNE(
        m_Builder.CreateAnd(mask, m_Builder.getInt64(1ULL << i)),
        m_Builder.getInt64(0), "drop.array.element.is_live");
    m_Builder.CreateCondBr(isLive, dropElement, nextElement);
    m_Builder.SetInsertPoint(dropElement);
    llvm::Value *elementAddr = m_Builder.CreateInBoundsGEP(
        arrayType, ptrAddr,
        {m_Builder.getInt32(0), m_Builder.getInt32(static_cast<unsigned>(i))},
        "drop.array.element");
    emitDropForType(elementAddr, elementType);
    if (!m_Builder.GetInsertBlock()->getTerminator())
      m_Builder.CreateBr(nextElement);
    m_Builder.SetInsertPoint(nextElement);
  }
}

void CodeGen::emitDropCascade(llvm::Value *ptrAddr, const std::string &typeName) {
  if (typeName.empty() || !ptrAddr) return;
  
  // [NEW] Dynamic Closure (dyn fn) Drop Logic
  if (typeName.find("dyn fn(") == 0) {
      llvm::Type* envTy = llvm::PointerType::getUnqual(m_Context);
      llvm::StructType* fatTy = llvm::StructType::get(envTy, envTy, envTy);
      llvm::Value* fatPtr = m_Builder.CreateLoad(fatTy, ptrAddr);
      
      llvm::Value* envVal = m_Builder.CreateExtractValue(fatPtr, 0);
      llvm::Value* dropVal = m_Builder.CreateExtractValue(fatPtr, 2);
      
      llvm::Value* isNotNull = m_Builder.CreateIsNotNull(dropVal);
      llvm::Function* f = m_Builder.GetInsertBlock()->getParent();
      llvm::BasicBlock* dropBB = llvm::BasicBlock::Create(m_Context, "dynfn.drop", f);
      llvm::BasicBlock* endBB = llvm::BasicBlock::Create(m_Context, "dynfn.dropend", f);
      m_Builder.CreateCondBr(isNotNull, dropBB, endBB);
      
      m_Builder.SetInsertPoint(dropBB);
      llvm::FunctionType* dropFTy = llvm::FunctionType::get(m_Builder.getVoidTy(), {envTy}, false);
      m_Builder.CreateCall(dropFTy, dropVal, {envVal});
      
      llvm::Function *freeFn = m_Module->getFunction("free");
      if (!freeFn) {
          freeFn = llvm::Function::Create(llvm::FunctionType::get(m_Builder.getVoidTy(), {envTy}, false), llvm::Function::ExternalLinkage, "free", m_Module.get());
      }
      m_Builder.CreateCall(freeFn, {envVal});
      
      m_Builder.CreateBr(endBB);
      m_Builder.SetInsertPoint(endBB);
      return;
  }
  
  std::string dropFunc = "";
  if (m_Shapes.count(typeName)) {
    dropFunc = m_Shapes[typeName]->MangledDestructorName;
  }
  if (dropFunc.empty()) {
    std::string try1 = "encap_" + typeName + "_drop";
    std::string try2 = typeName + "_drop"; // Legacy
    if (m_Module->getFunction(try1)) {
        dropFunc = try1;
    } else if (m_Module->getFunction(try2)) {
        dropFunc = try2;
    }
  }

  bool calledDestructor = false;
  // 1. Core Action: Call custom destructor if present
  if (!dropFunc.empty()) {
    llvm::Function *dFn = m_Module->getFunction(dropFunc);
    if (dFn) {
      llvm::Type *elemTy = resolveType(typeName, false);
      if (elemTy) {
        llvm::Value *typedPtr = m_Builder.CreateBitCast(ptrAddr, llvm::PointerType::getUnqual(m_Context));
        m_Builder.CreateCall(dFn, {typedPtr});
        calledDestructor = true;
      }
    } else {
    }
  }

  // 2. Cascade Drop: Recursively drop fields that are shapes natively
  // [Fix] Bypass manual cascade if we already called a destructor (which handles its own fields)
  if (!calledDestructor && m_Shapes.count(typeName)) {
    const ShapeDecl *sh = m_Shapes[typeName];
    llvm::StructType *st = m_StructTypes[typeName];
    if (!st) {
        return;
    }

    if (sh->Kind == ShapeKind::Enum) {
      llvm::Value *tagAddr = m_Builder.CreateStructGEP(st, ptrAddr, 0, "drop_tag.gep");
      llvm::Value *tagVal = m_Builder.CreateLoad(llvm::Type::getInt8Ty(m_Context), tagAddr, "drop_tag.val");
      
      llvm::BasicBlock *currentBB = m_Builder.GetInsertBlock();
      llvm::Function *F = currentBB->getParent();
      llvm::BasicBlock *mergeBB = llvm::BasicBlock::Create(m_Context, "drop_enum_merge", F);

      int numPayloads = 0;
      for (const auto &member : sh->Members) {
        if (!member.Type.empty() || !member.SubMembers.empty()) numPayloads++;
      }

      if (numPayloads > 0) {
        llvm::SwitchInst *switchInst = m_Builder.CreateSwitch(tagVal, mergeBB, numPayloads);
        for (size_t i = 0; i < sh->Members.size(); ++i) {
          const auto &variant = sh->Members[i];
          if (variant.Type.empty() && variant.SubMembers.empty()) continue;
          
          int tag = (variant.TagValue != -1) ? (int)variant.TagValue : (int)i;
          llvm::BasicBlock *caseBB = llvm::BasicBlock::Create(m_Context, "drop_enum_case_" + std::to_string(tag), F);
          switchInst->addCase(m_Builder.getInt8(tag), caseBB);
          
          m_Builder.SetInsertPoint(caseBB);
          llvm::Value *payloadArrayPtr = m_Builder.CreateStructGEP(st, ptrAddr, 1, "drop_payload.gep");

          std::vector<std::string> payloadTypes;
          std::vector<std::shared_ptr<Type>> payloadTypeObjs;
          if (!variant.SubMembers.empty()) {
              for (const auto& f : variant.SubMembers) {
                  std::string pt = f.Type;
                  if (f.ResolvedType) pt = f.ResolvedType->toString();
                  payloadTypes.push_back(pt);
                  payloadTypeObjs.push_back(f.ResolvedType);
              }
          } else {
              std::string pt = variant.Type;
              if (variant.ResolvedType) pt = variant.ResolvedType->toString();
              if (pt != "void" && !pt.empty()) {
                  payloadTypes.push_back(pt);
                  payloadTypeObjs.push_back(variant.ResolvedType);
              }
          }

          if (!payloadTypes.empty()) {
              llvm::Type *payloadLayoutType = nullptr;
              std::vector<llvm::Type*> fieldTypes;
              if (!variant.SubMembers.empty()) {
                  for (size_t k = 0; k < payloadTypes.size(); ++k) {
                      if (payloadTypeObjs[k]) {
                          fieldTypes.push_back(getLLVMType(payloadTypeObjs[k]));
                      } else {
                          fieldTypes.push_back(resolveType(payloadTypes[k], false));
                      }
                  }
                  payloadLayoutType =
                      llvm::StructType::get(m_Context, fieldTypes, false);
              } else {
                  if (payloadTypeObjs[0]) {
                      payloadLayoutType = getLLVMType(payloadTypeObjs[0]);
                  } else {
                      payloadLayoutType = resolveType(payloadTypes[0], false);
                  }
              }

              if (payloadLayoutType) {
                  llvm::Value *variantAddr = m_Builder.CreateBitCast(payloadArrayPtr, llvm::PointerType::getUnqual(m_Context), "drop_cast");
                  for (size_t k = 0; k < payloadTypes.size(); ++k) {
                      std::string memType = payloadTypes[k];
                      bool isPointer = false;
                      std::string rawType = memType;
                      while (!rawType.empty() && rawType[0] == '(' && rawType.back() == ')') {
                        rawType = rawType.substr(1, rawType.size() - 2);
                      }
                      if (!rawType.empty() && (rawType[0] == '*' || rawType[0] == '^' || rawType[0] == '~' || rawType[0] == '&' || rawType[0] == '#')) {
                        isPointer = true;
                      }
                      if (!isPointer) {
                        std::string cleanType = Type::stripMorphology(rawType);
                        if (m_Shapes.count(cleanType)) {
                            llvm::Value *fieldAddr = variantAddr;
                            if (payloadTypes.size() > 1 || !variant.SubMembers.empty()) {
                                fieldAddr = m_Builder.CreateStructGEP(payloadLayoutType, variantAddr, k, "drop_field_gep");
                            }
                            emitDropCascade(fieldAddr, cleanType);
                        }
                      }
                  }
              }
          }
          m_Builder.CreateBr(mergeBB);
        }
      } else {
        m_Builder.CreateBr(mergeBB);
      }
      m_Builder.SetInsertPoint(mergeBB);
    } else {
      for (size_t i = 0; i < sh->Members.size(); ++i) {
        std::string rawType = sh->Members[i].Type;
        bool isPointer = false;
        // Strip outer parens, though rare
        while (!rawType.empty() && rawType[0] == '(' && rawType.back() == ')') {
          rawType = rawType.substr(1, rawType.size() - 2);
        }
        // If it's a pointer type (*T, ^T, ~T), drop cascade is BYPASSED
        if (!rawType.empty() && (rawType[0] == '*' || rawType[0] == '^' || rawType[0] == '~' || rawType[0] == '&' || rawType[0] == '#')) {
          isPointer = true;
        }
        
        std::string memberType = Type::stripMorphology(rawType);
        
        // If the bare member type is a shape and not behind a pointer, cascade into it!
        if (!isPointer && m_Shapes.count(memberType)) {
           llvm::Value *typedBase = m_Builder.CreateBitCast(ptrAddr, llvm::PointerType::getUnqual(m_Context));
           llvm::Value *fieldPtr = m_Builder.CreateStructGEP(st, typedBase, i, "drop_cascade.gep");
           emitDropCascade(fieldPtr, memberType);
        }
      }
    }
  }
}

void CodeGen::emitDropCascadeWithMask(llvm::Value *ptrAddr,
                                      const std::string &typeName,
                                      llvm::Value *dropMaskAddr) {
  if (!ptrAddr || !dropMaskAddr || !m_Shapes.count(typeName)) {
    emitDropCascade(ptrAddr, typeName);
    return;
  }

  const ShapeDecl *shape = m_Shapes[typeName];
  if ((shape->Kind != ShapeKind::Struct && shape->Kind != ShapeKind::Tuple) ||
      shape->HasExplicitDrop || shape->Members.size() > 64) {
    emitDropCascade(ptrAddr, typeName);
    return;
  }
  llvm::StructType *structTy = m_StructTypes[typeName];
  if (!structTy)
    return;

  for (size_t i = 0; i < shape->Members.size(); ++i) {
    std::string rawType = shape->Members[i].Type;
    while (!rawType.empty() && rawType.front() == '(' && rawType.back() == ')')
      rawType = rawType.substr(1, rawType.size() - 2);
    if (rawType.empty() || rawType.front() == '*' || rawType.front() == '^' ||
        rawType.front() == '~' || rawType.front() == '&' ||
        rawType.front() == '#')
      continue;
    const std::string memberType = Type::stripMorphology(rawType);
    if (!m_Shapes.count(memberType))
      continue;

    llvm::Function *function = m_Builder.GetInsertBlock()->getParent();
    llvm::BasicBlock *dropField =
        llvm::BasicBlock::Create(m_Context, "drop.field.live", function);
    llvm::BasicBlock *nextField =
        llvm::BasicBlock::Create(m_Context, "drop.field.done", function);
    llvm::Value *mask = m_Builder.CreateLoad(
        llvm::Type::getInt64Ty(m_Context), dropMaskAddr, "drop.mask");
    llvm::Value *isLive = m_Builder.CreateICmpNE(
        m_Builder.CreateAnd(mask, m_Builder.getInt64(1ULL << i)),
        m_Builder.getInt64(0), "drop.field.is_live");
    m_Builder.CreateCondBr(isLive, dropField, nextField);

    m_Builder.SetInsertPoint(dropField);
    llvm::Value *fieldAddr = m_Builder.CreateStructGEP(
        structTy, ptrAddr, static_cast<unsigned>(i), "drop.field.gep");
    emitDropCascade(fieldAddr, memberType);
    if (!m_Builder.GetInsertBlock()->getTerminator())
      m_Builder.CreateBr(nextField);
    m_Builder.SetInsertPoint(nextField);
  }
}

llvm::Value *CodeGen::genFreeStmt(const FreeStmt *fs) {
  llvm::Function *freeHook = m_Module->getFunction("free");

  llvm::Value *ptrAddr = nullptr;
  if (auto *unary = dynamic_cast<const UnaryExpr *>(fs->Expression.get())) {
    if (unary->Op == TokenType::Star || unary->Op == TokenType::Caret ||
        unary->Op == TokenType::Tilde) {
      // [Fix] Freeing a pointer (*p) means freeing the Value (the Soul),
      // not the Stack Address (identity). genAddr(*p) returns Identity.
      // genExpr(*p) returns Soul.
      ptrAddr = genExpr(fs->Expression.get()).load(m_Builder);
    }
  }

  if (!ptrAddr) {
    // [Fix] Always load the pointer value. free() expects the heap address
    // (RValue), not the variable address (LValue).
    ptrAddr = genExpr(fs->Expression.get()).load(m_Builder);
  }

  if (freeHook && ptrAddr) {
    // [Feature] Drop before Free for Raw Pointers
    // Check if the type being freed has a drop method
    std::string typeName = "";
    bool isArray = false;
    uint64_t arraySize = 0;
    llvm::Value *dynamicCount = nullptr;

    std::shared_ptr<Type> exprTy = fs->Expression->ResolvedType;
    if (exprTy) {
      if (exprTy->isPointer() || exprTy->isReference()) {
        exprTy = exprTy->getPointeeType();
      }
      if (exprTy) {
        if (exprTy->isArray() || exprTy->isSlice()) {
          auto elemTyObj = exprTy->getArrayElementType();
          if (elemTyObj) {
            typeName = elemTyObj->getSoulName();
            isArray = true;
          }
        } else {
          typeName = exprTy->getSoulName();
        }
      }
    }

    if (typeName.empty()) {
      // Try to deduce type from expression (Fallback)
      const Expr *rawExpr = fs->Expression.get();
      // Peel layers (*, ?, etc.)
      while (true) {
        if (auto *ue = dynamic_cast<const UnaryExpr *>(rawExpr)) {
          rawExpr = ue->RHS.get();
        } else if (auto *pe = dynamic_cast<const PostfixExpr *>(rawExpr)) {
          rawExpr = pe->LHS.get();
        } else {
          break;
        }
      }

      if (auto *ve = dynamic_cast<const VariableExpr *>(rawExpr)) {
        std::string varName = Type::stripMorphology(ve->Name);

        if (m_Symbols.count(varName)) {
          std::string vType = m_Symbols[varName].typeName;
          // vType is e.g. *Data or *[10]Data
          if (!vType.empty()) {
            if (vType[0] == '*') {
              typeName = vType.substr(1); // Peel pointer
            } else {
              typeName = vType;
            }
          }
        }
      }

      // Handle Array Type parsing (e.g. [10]Data)
      if (!typeName.empty() && typeName[0] == '[') {
        size_t close = typeName.find(']');
        if (close != std::string::npos) {
          std::string sizeStr = typeName.substr(1, close - 1);
          try {
            arraySize = std::stoull(sizeStr);
            typeName = typeName.substr(close + 1);
            isArray = true;
          } catch (...) {
          }
        }
      }
    }

    if (fs->Count) {
      dynamicCount = genExpr(fs->Count.get()).load(m_Builder);
      isArray = true;
    }

    if (!typeName.empty()) {
      if (isArray) {
        // We need the element size
        llvm::Type *elemTy = resolveType(typeName, false);

        if (elemTy) {
          llvm::Value *countVal = dynamicCount;
          if (!countVal) {
            countVal = llvm::ConstantInt::get(getIntPtrTy(),
                                              arraySize);
          }

          if (countVal->getType() != getIntPtrTy()) {
            countVal = m_Builder.CreateIntCast(
                countVal, getIntPtrTy(), false,
                "count_cast");
          }

          llvm::BasicBlock *preHeaderBB = m_Builder.GetInsertBlock();
          llvm::Function *F = preHeaderBB->getParent();
          llvm::BasicBlock *loopBB =
              llvm::BasicBlock::Create(m_Context, "drop_loop", F);
          llvm::BasicBlock *afterBB =
              llvm::BasicBlock::Create(m_Context, "drop_after", F);

          llvm::Value *isZero = m_Builder.CreateICmpEQ(
              countVal, llvm::ConstantInt::get(getIntPtrTy(), 0), "is_zero");
          m_Builder.CreateCondBr(isZero, afterBB, loopBB);
          m_Builder.SetInsertPoint(loopBB);

          llvm::PHINode *iVar =
              m_Builder.CreatePHI(getIntPtrTy(), 2, "i");
          iVar->addIncoming(
              llvm::ConstantInt::get(getIntPtrTy(), 0),
              preHeaderBB);

          // GEP to element
          // ptrAddr is the base pointer (void* or T*). Cast to T*
          llvm::Value *typedBase = m_Builder.CreateBitCast(
              ptrAddr, llvm::PointerType::getUnqual(m_Context));
          llvm::Value *elemPtr =
              m_Builder.CreateInBoundsGEP(elemTy, typedBase, iVar);

          emitDropCascade(elemPtr, typeName);

          llvm::Value *nextI = m_Builder.CreateAdd(
              iVar,
              llvm::ConstantInt::get(getIntPtrTy(), 1));
          llvm::Value *cond = m_Builder.CreateICmpULT(nextI, countVal);
          llvm::BasicBlock *loopEndBB = m_Builder.GetInsertBlock();
          iVar->addIncoming(nextI, loopEndBB);

          m_Builder.CreateCondBr(cond, loopBB, afterBB);
          m_Builder.SetInsertPoint(afterBB);
        }

      } else {
        // Single drop
        emitDropCascade(ptrAddr, typeName);
      }
    }

    llvm::Value *casted =
        m_Builder.CreateBitCast(ptrAddr, m_Builder.getPtrTy());
    llvm::CallInst *freeCall = m_Builder.CreateCall(freeHook, casted);
    markMemoryEvent(freeCall, "free");
  }
  return nullptr;
}

PhysEntity CodeGen::genStaticMemberExpr(const MemberExpr *mem) {
  const ShapeDecl *sh = nullptr;
  std::string typeName = "";
  llvm::Type *enumType = nullptr;

  // 1. Resolve Shape from Type Expression (Robust for Generics)
  if (mem->Object->ResolvedType) {
    auto rt = mem->Object->ResolvedType;
    // Peel pointers/refs if present (unlikely for static type access but
    // safe)
    while (rt &&
           (rt->isPointer() || rt->isReference() || rt->isSmartPointer()))
      rt = rt->getPointeeType();

    if (rt && rt->isShape()) {
      auto st = std::dynamic_pointer_cast<ShapeType>(rt);
      if (st && st->Decl) {
        sh = st->Decl;
        typeName = st->toString(); // Fallback name
        enumType = getLLVMType(rt);
      }
    }
  }

  // 2. Fallback: Lookup by Variable Name (Legacy/Non-Resolved)
  if (!sh) {
    if (auto *ve = dynamic_cast<const VariableExpr *>(mem->Object.get())) {
      typeName = ve->Name;
      // Strip morphology
      typeName = Type::stripMorphology(typeName);
      if (m_Shapes.count(typeName)) {
        sh = m_Shapes[typeName];
        if (m_StructTypes.count(typeName))
          enumType = m_StructTypes[typeName];
      } else if (ve->ResolvedType && ve->ResolvedType->isShape()) {
        // ResolvedType found on variable but not handled above?
        typeName = ve->ResolvedType->getSoulName();
        if (m_Shapes.count(typeName))
          sh = m_Shapes[typeName];
        if (m_StructTypes.count(typeName))
          enumType = m_StructTypes[typeName];
      }
    }
  }

  // 3. Check if Enum Variant
  if (sh && sh->Kind == ShapeKind::Enum) {
    int tag = -1;
    for (size_t i = 0; i < sh->Members.size(); ++i) {
      if (sh->Members[i].Name == mem->Member) {
        tag = (sh->Members[i].TagValue != -1) ? (int)sh->Members[i].TagValue
                                              : (int)i;
        break;
      }
    }
    if (tag != -1) {
      // Enums are Shapes (Structs), so return { tag }
      if (enumType && enumType->isStructTy()) {
        llvm::Value *res = llvm::UndefValue::get(enumType);
        // Dynamically determine tag type (usually i32, but check struct)
        if (enumType->getStructNumElements() > 0) {
          llvm::Type *tagTy = enumType->getStructElementType(0);
          llvm::Value *typedTagVal = llvm::ConstantInt::get(tagTy, tag);
          res = m_Builder.CreateInsertValue(res, typedTagVal, 0);
          return PhysEntity(res, typeName, enumType, false); // RValue
        }
      }
      // Fallback (should ideally not happen for well-formed Enums)
      llvm::Value *tagVal =
          llvm::ConstantInt::get(llvm::Type::getInt32Ty(m_Context), tag);
      return PhysEntity(tagVal, typeName, llvm::Type::getInt32Ty(m_Context),
                        false);
    }
  }
  return nullptr;
}

llvm::Value *CodeGen::peelNestedMemberBaseAddress(const Expr *object, llvm::Value *addr) {
  if (auto *baseMem = dynamic_cast<const MemberExpr *>(object)) {
    if (baseMem->ResolvedType &&
        (baseMem->ResolvedType->isPointer() ||
         baseMem->ResolvedType->isReference())) {
      return m_Builder.CreateLoad(m_Builder.getPtrTy(), addr,
                                  "member.peel_base");
    }
  }
  return addr;
}

CodeGen::MemberObjectInfo CodeGen::resolveMemberObject(const Expr *object,
                                                       llvm::Value *addr) {
  MemberObjectInfo info;

  if (object->ResolvedType) {
    auto base = peelMemberObjectType(object->ResolvedType);
    if (base) {
      info.ObjectType = getLLVMType(base);
      if (base->isShape()) {
        info.ShapeName = base->getSoulName();
      }
    }
  }

  // Fallback for cases without ResolvedType (unlikely in modern Toka Sema).
  if (!info.ObjectType) {
    if (auto *alloca = llvm::dyn_cast<llvm::AllocaInst>(addr)) {
      info.ObjectType = alloca->getAllocatedType();
    } else if (auto *gep = llvm::dyn_cast<llvm::GetElementPtrInst>(addr)) {
      info.ObjectType = gep->getResultElementType();
    } else if (auto *load = llvm::dyn_cast<llvm::LoadInst>(addr)) {
      info.ObjectType = load->getType();
    }
  }

  // Final fallback: contextual symbol table for self/globals.
  if (!info.ObjectType || !info.ObjectType->isStructTy()) {
    if (auto *ve = dynamic_cast<const VariableExpr *>(object)) {
      std::string baseName = ve->Name;
      while (!baseName.empty() &&
             (baseName[0] == '*' || baseName[0] == '&' || baseName[0] == '#'))
        baseName = baseName.substr(1);
      if (m_Symbols.count(baseName)) {
        const TokaSymbol &sym = m_Symbols[baseName];
        info.ObjectType = sym.soulType;
        if (sym.soulTypeObj) {
          info.ShapeName = sym.soulTypeObj->getSoulName();
        }
      }
    }
  }

  if (info.ObjectType && info.ObjectType->isStructTy()) {
    info.StructTy = llvm::cast<llvm::StructType>(info.ObjectType);
  }
  if (!info.StructTy && !info.ShapeName.empty() &&
      m_StructTypes.count(info.ShapeName)) {
    info.StructTy = m_StructTypes[info.ShapeName];
  }
  return info;
}

llvm::Value *CodeGen::peelDerefObjectToSoulAddress(const Expr *object,
                                                  llvm::Value *addr) {
  // (*p.x) If the object expression is a Dereference (*p), genAddr returned
  // the Identity (Handle Address). We need the Soul (Data Address) to access
  // members.
  auto *ue = dynamic_cast<const UnaryExpr *>(object);
  if (!isDerefObjectForMemberAccess(ue))
    return addr;

  bool isShared = false;
  if (ue->RHS->ResolvedType && ue->RHS->ResolvedType->isSharedPtr()) {
    isShared = true;
  }

  if (isShared) {
    // Shared Pointer Identity is { T*, Ref* }*. We want T*.
    llvm::Value *dataAddr = m_Builder.CreateStructGEP(
        llvm::StructType::get(m_Context,
                              {llvm::PointerType::getUnqual(m_Context),
                               llvm::PointerType::getUnqual(m_Context)}),
        addr, 0, "member.sh_data_gep");
    return m_Builder.CreateLoad(m_Builder.getPtrTy(), dataAddr,
                                "member.sh_soul");
  }

  // Raw/Unique Pointer Identity is T**. We want T*.
  return m_Builder.CreateLoad(m_Builder.getPtrTy(), addr,
                              "member.peel_soul");
}

CodeGen::MemberFieldInfo CodeGen::resolveMemberField(llvm::StructType *structTy,
                                                    const std::string &shapeName,
                                                    int initialIndex,
                                                    const std::string &name) {
  MemberFieldInfo info;
  info.StructTy = structTy;
  info.Index = initialIndex;

  if (!info.StructTy)
    return info;

  info.StructName = resolveStructName(info.StructTy, shapeName, m_TypeToName, m_StructTypes);
  if (info.Index != -1 || info.StructName.empty())
    return info;

  auto fieldsIt = m_StructFieldNames.find(info.StructName);
  if (fieldsIt == m_StructFieldNames.end())
    return info;

  const auto &fields = fieldsIt->second;
  info.Index = findMemberIndexInFields(fields, name, false);
  return info;
}

struct MemberShapeContext {
  const ShapeDecl *NamedShape = nullptr;
  const ShapeDecl *ObjectShape = nullptr;
  const ShapeDecl *MemberShape = nullptr;
};

static MemberShapeContext resolveMemberShapeContext(
    const std::string &stName,
    const Expr *objectExpr,
    const std::map<std::string, const ShapeDecl *> &shapes) {
  MemberShapeContext context;
  if (!stName.empty()) {
    auto shapeIt = shapes.find(stName);
    context.NamedShape = shapeIt == shapes.end() ? nullptr : shapeIt->second;
  }
  context.ObjectShape = shapeDeclFromType(objectExpr->ResolvedType);
  context.MemberShape =
      context.NamedShape ? context.NamedShape : context.ObjectShape;
  return context;
}

CodeGen::MemberFieldStorage CodeGen::emitMemberFieldStorage(const ShapeDecl *memberShape,
                                                           const ShapeDecl *namedShape,
                                                           llvm::StructType *structTy,
                                                           llvm::Value *objAddr,
                                                           int idx,
                                                           const std::string &memberName) {
  MemberFieldStorage storage;
  storage.Addr = m_Builder.CreateStructGEP(structTy, objAddr, idx, memberName);

  if (memberShape && idx >= 0 &&
      idx < (int)memberShape->Members.size()) {
    const ShapeMember &member = memberShape->Members[idx];
    storage.TypeName = member.Type;
    if (member.ResolvedType) {
      storage.IrTy = getLLVMType(member.ResolvedType);
    } else if (namedShape) {
      storage.IrTy = resolveType(storage.TypeName, false);
    }
  }

  if (!storage.IrTy) {
    storage.IrTy = structTy->getElementType(idx);
  }
  return storage;
}

llvm::Value *CodeGen::emitMemberHatOffLoads(llvm::Value *addr,
                                           const MemberHatPlan &plan) {
  for (int i = 0; i < plan.DerefCount; ++i) {
    // Current address is the address of the pointer. Load it to get the
    // target address.
    addr = m_Builder.CreateLoad(m_Builder.getPtrTy(), addr, "hat_off_deref");
  }
  return addr;
}

llvm::Type *CodeGen::resolveMemberResultType(llvm::Type *baseIrTy,
                                            const MemberHatPlan &plan,
                                            const std::shared_ptr<Type> &resolvedType) {
  llvm::Type *resultTy = baseIrTy;
  if (resolvedType) {
    auto soul = resolvedType;
    if (plan.DerefCount > 0) {
      soul = soul->getSoulType();
    }
    resultTy = getLLVMType(soul);
  }
  return resultTy;
}

CodeGen::MemberMaterialization CodeGen::emitMaterializedMemberValue(llvm::Value *addr,
                                                                   llvm::Type *baseIrTy,
                                                                   const MemberHatPlan &plan,
                                                                   const std::shared_ptr<Type> &resolvedType) {
  return {emitMemberHatOffLoads(addr, plan),
          resolveMemberResultType(baseIrTy, plan, resolvedType)};
}

CodeGen::MemberHatPlan CodeGen::resolveMemberHatPlan(const ShapeDecl *namedShape,
                                                     int idx,
                                                     int accessHats,
                                                     bool isIdentityAssertion,
                                                     bool isIdentityOperator,
                                                     const std::shared_ptr<Type> &resolvedType) {
  MemberHatPlan plan;
  plan.IsIdentityAssertion = isIdentityAssertion;

  if (namedShape && idx >= 0 && idx < (int)namedShape->Members.size()) {
    const ShapeMember &member = namedShape->Members[idx];
    if (member.ResolvedType) {
      plan.DefHats = getTypeHatCount(member.ResolvedType);
    } else {
      plan.DefHats = countLeadingMemberHats(member.Name);
      if (plan.DefHats == 0) {
        plan.DefHats = countLeadingMemberHats(member.Type);
      }
    }
  }

  plan.AccessHats = accessHats;
  plan.IsHatOn = (plan.AccessHats > plan.DefHats);

  if (!isIdentityOperator) {
    if (resolvedType) {
      plan.DerefCount = plan.DefHats - getTypeHatCount(resolvedType);
    } else {
      plan.DerefCount = plan.DefHats - plan.AccessHats;
    }
    if (plan.DerefCount < 0)
      plan.DerefCount = 0;
  }
  return plan;
}

PhysEntity CodeGen::genDynamicMemberExpr(const MemberExpr *mem) {
  // --- Dynamic Member Access (Sovereign Logic) ---
  const Expr *objectExpr = mem->Object.get();
  MemberAccessIntent memberAccess = parseMemberAccess(mem->Member);
  std::string memberName = memberAccess.StrippedName;

  llvm::Value *objAddr = nullptr;
  bool isCallResult = dynamic_cast<const CallExpr *>(objectExpr) ||
                      dynamic_cast<const MethodCallExpr *>(objectExpr);
  if (isCallResult && objectExpr->ResolvedType &&
      (objectExpr->ResolvedType->isPointer() ||
       objectExpr->ResolvedType->isReference())) {
    PhysEntity object = genExpr(objectExpr);
    if (!object.isAddress && object.value &&
        object.value->getType()->isPointerTy()) {
      objAddr = object.value;
    }
  }
  if (!objAddr)
    objAddr = emitEntityAddr(objectExpr);
  if (!objAddr)
    return nullptr;

  objAddr = peelNestedMemberBaseAddress(objectExpr, objAddr);
  MemberObjectInfo objInfo = resolveMemberObject(objectExpr, objAddr);
  objAddr = peelDerefObjectToSoulAddress(objectExpr, objAddr);

  MemberFieldInfo fieldInfo =
      resolveMemberField(objInfo.StructTy, objInfo.ShapeName, mem->Index,
                         memberName);
  llvm::StructType *st = fieldInfo.StructTy;
  int idx = fieldInfo.Index;
  std::string stName = fieldInfo.StructName;

  if (!st)
    return nullptr;

  if (idx == -1) {
    std::string typeDesc = stName.empty() ? "anonymous record"
                                          : ("struct '" + stName + "'");
    error(mem, DiagID::ERR_CODEGEN_FAILED_TO_RESOLVE_MEMBER_IN, mem->Member,
          typeDesc);
    return nullptr;
  }

  MemberShapeContext shapeContext = resolveMemberShapeContext(stName, objectExpr, m_Shapes);
  const ShapeDecl *namedShape = shapeContext.NamedShape;
  MemberFieldStorage fieldStorage = emitMemberFieldStorage(shapeContext.MemberShape, shapeContext.NamedShape, st, objAddr, idx, memberName);
  llvm::Value *finalAddr = fieldStorage.Addr;
  std::string memberTypeName = fieldStorage.TypeName;
  llvm::Type *irTy = fieldStorage.IrTy;

  MemberHatPlan hatPlan = resolveMemberHatPlan(
      namedShape, idx, memberAccess.AccessHats, memberAccess.IsIdentityAssertion,
      memberAccess.IsIdentityAssertion || memberAccess.IsIdentityOperator,
      mem->ResolvedType);

  if (hatPlan.IsHatOn) {
    // Hat-On: We are taking the address of the member.
    llvm::Type *finalIrTy = m_Builder.getPtrTy();
    if (mem->ResolvedType) {
      finalIrTy = getLLVMType(mem->ResolvedType);
    }
    return PhysEntity(finalAddr, memberTypeName, finalIrTy, false);
  }

  MemberMaterialization materialized =
      emitMaterializedMemberValue(finalAddr, irTy, hatPlan, mem->ResolvedType);

  if (hatPlan.IsIdentityAssertion) {
    // Identity Assertion (Ch 6.1)
    llvm::Value *ptrVal =
        m_Builder.CreateLoad(materialized.IrTy, materialized.Addr, "nn.load");
    genNullCheck(ptrVal, mem);
    return PhysEntity(ptrVal, memberTypeName, materialized.IrTy,
                      false); // R-Value
  }

  return PhysEntity(materialized.Addr, memberTypeName, materialized.IrTy, true);
}

PhysEntity CodeGen::genMemberExpr(const MemberExpr *mem) {
  if (mem->IsStatic)
    return genStaticMemberExpr(mem);
  return genDynamicMemberExpr(mem);
}

PhysEntity CodeGen::genIndexExpr(const ArrayIndexExpr *idxExpr) {
  // Check for Array Shape Initialization
  if (auto *var = dynamic_cast<const VariableExpr *>(idxExpr->Array.get())) {
    if (m_Shapes.count(var->Name)) {
      const ShapeDecl *sh = m_Shapes[var->Name];
      if (sh->Kind == ShapeKind::Array) {
        llvm::StructType *st = m_StructTypes[var->Name];
        llvm::Value *alloca =
            createEntryBlockAlloca(st, nullptr, var->Name + "_init");

        for (size_t i = 0; i < idxExpr->Indices.size(); ++i) {
          llvm::Value *val = genExpr(idxExpr->Indices[i].get()).load(m_Builder);
          if (!val)
            return nullptr;
          // GEP: struct 0, array i
          llvm::Value *ptr = m_Builder.CreateInBoundsGEP(
              st, alloca,
              {m_Builder.getInt32(0), m_Builder.getInt32(0),
               m_Builder.getInt32((uint32_t)i)});
          m_Builder.CreateStore(val, ptr);
        }
        return m_Builder.CreateLoad(st, alloca);
      }
    }
  }

  // Normal Indexing
  llvm::Value *addr = genAddr(idxExpr);
  if (!addr)
    return nullptr;
  if (auto *gep = llvm::dyn_cast<llvm::GetElementPtrInst>(addr)) {
    return m_Builder.CreateLoad(gep->getResultElementType(), addr);
  }
  return nullptr;
}

llvm::Value *CodeGen::genAddr(const Expr *expr) {
  if (auto *var = dynamic_cast<const VariableExpr *>(expr)) {
    return getEntityAddr(var->codegenName());
  }

  if (auto *unary = dynamic_cast<const UnaryExpr *>(expr)) {
    if (unary->Op == TokenType::Ampersand) {
      if (auto *v = dynamic_cast<const VariableExpr *>(unary->RHS.get())) {
        std::string baseName = v->Name;
        while (!baseName.empty() &&
               (baseName[0] == '*' || baseName[0] == '#' || baseName[0] == '&' ||
                baseName[0] == '^' || baseName[0] == '~' || baseName[0] == '!' ||
                baseName[0] == '?'))
          baseName = baseName.substr(1);
        while (!baseName.empty() &&
               (baseName.back() == '#' || baseName.back() == '?' ||
                baseName.back() == '!'))
          baseName.pop_back();

        auto it = m_Symbols.find(baseName);
        if (it != m_Symbols.end()) {
          TokaSymbol &sym = it->second;
          if (sym.indirectionLevel > 0) {
            return getEntityAddr(v->codegenName());
          }
          return getIdentityAddr(v->codegenName());
        }

        bool isCapturedRef = false;
        if (m_Symbols.count("self")) {
          auto selfTy = m_Symbols["self"].soulTypeObj;
          if (selfTy && selfTy->isReference()) {
            auto ptrTy = std::static_pointer_cast<toka::PointerType>(selfTy);
            selfTy = ptrTy->PointeeType;
          }
          if (selfTy && selfTy->isShape() && selfTy->getSoulName().find("__Closure_") == 0) {
            auto shapeTy = std::static_pointer_cast<ShapeType>(selfTy);
            if (shapeTy->Decl) {
              for (const auto& member : shapeTy->Decl->Members) {
                if (member.Name == baseName) {
                  if (member.ResolvedType && member.ResolvedType->isReference()) {
                    isCapturedRef = true;
                  }
                  break;
                }
              }
            }
          }
        }

        if (isCapturedRef) {
          return getEntityAddr(v->codegenName());
        } else {
          return getIdentityAddr(v->codegenName());
        }
      }
      return genAddr(unary->RHS.get());
    }
    if (unary->Op == TokenType::Star || unary->Op == TokenType::Caret ||
        unary->Op == TokenType::Tilde) {
      // [Constitution] *p, ^p, ~p refer to the Identity (the pointer handle).
      // Their "address" is the address of the handle box (the alloca).
      if (auto *v = dynamic_cast<const VariableExpr *>(unary->RHS.get())) {
        return getIdentityAddr(v->codegenName());
      }
      // For recursive unary, we'd need to go deeper, but Toka usually has 1
      // level.
      return genAddr(unary->RHS.get());
    }
    if (unary->Op == TokenType::TokenNull) {
      // Morphology is transparent to address
      return genAddr(unary->RHS.get());
    }
  }

  if (auto *idxExpr = dynamic_cast<const ArrayIndexExpr *>(expr)) {
    if (idxExpr->Indices.empty())
      return nullptr;

    // 1. Identify the Array base variable
    std::string baseName = "";
    if (auto *v = dynamic_cast<const VariableExpr *>(idxExpr->Array.get())) {
      baseName = v->Name;
    }

    // Scrub decorators
    while (!baseName.empty() &&
           (baseName[0] == '*' || baseName[0] == '#' || baseName[0] == '&' ||
            baseName[0] == '^' || baseName[0] == '~' || baseName[0] == '!'))
      baseName = baseName.substr(1);
    while (!baseName.empty() &&
           (baseName.back() == '#' || baseName.back() == '?' ||
            baseName.back() == '!'))
      baseName.pop_back();

    llvm::Value *indexValue =
        genExpr(idxExpr->Indices[0].get()).load(m_Builder);
    if (!indexValue)
      return nullptr;

    // [Safety Pillar 4] Fat Slices & Arrays Automatic Bounds Checking
    auto arrayTypeObj = idxExpr->Array->ResolvedType;
    if (arrayTypeObj) {
        llvm::Value *lenValue = nullptr;
        // 1. Fat Pointer (UniquePtr / Reference to Slice)
        if (arrayTypeObj->isFatPointer()) {
            PhysEntity arrEnt = genExpr(idxExpr->Array.get());
            llvm::Value *fatStruct = arrEnt.load(m_Builder);
            if (fatStruct) {
                lenValue = m_Builder.CreateExtractValue(fatStruct, {1}, "slice.len");
            }
        } 
        // 2. Shared Slice
        else if (arrayTypeObj->isSharedPtr() && arrayTypeObj->getPointeeType() && arrayTypeObj->getPointeeType()->isSlice()) {
            PhysEntity arrEnt = genExpr(idxExpr->Array.get());
            llvm::Value *shStruct = arrEnt.load(m_Builder);
            if (shStruct) {
                llvm::Value *cbPtr = m_Builder.CreateExtractValue(shStruct, {1}, "slice.cb");
                llvm::Type *cbTy = llvm::StructType::get(m_Context, {llvm::Type::getInt32Ty(m_Context), getIntPtrTy()});
                llvm::Value *lenAddr = m_Builder.CreateStructGEP(cbTy, cbPtr, 1, "slice.len.addr");
                lenValue = m_Builder.CreateLoad(getIntPtrTy(), lenAddr, "slice.len");
            }
        } 
        // 3. Static Array (Local/Global)
        else if (arrayTypeObj->isArray()) {
            auto arr = std::static_pointer_cast<ArrayType>(arrayTypeObj);
            lenValue = llvm::ConstantInt::get(getIntPtrTy(), arr->Size);
        }

        // Generate the runtime assertion block if lenValue exists
        if (lenValue) {
            llvm::Value *castedIndex = indexValue;
            if (castedIndex->getType() != lenValue->getType()) {
                castedIndex = m_Builder.CreateIntCast(castedIndex, lenValue->getType(), false);
            }
            llvm::Value *isOutOfBounds = m_Builder.CreateICmpUGE(castedIndex, lenValue, "bounds.cmp");
            
            llvm::BasicBlock *currentBB = m_Builder.GetInsertBlock();
            llvm::Function *F = currentBB->getParent();
            llvm::BasicBlock *panicBB = llvm::BasicBlock::Create(m_Context, "bounds.panic", F);
            llvm::BasicBlock *contBB = llvm::BasicBlock::Create(m_Context, "bounds.cont", F);
            
            m_Builder.CreateCondBr(isOutOfBounds, panicBB, contBB);
            
            // Generate Trap
            m_Builder.SetInsertPoint(panicBB);
            llvm::Function *trapFn = llvm::Intrinsic::getOrInsertDeclaration(m_Module.get(), llvm::Intrinsic::trap);
            m_Builder.CreateCall(trapFn);
            m_Builder.CreateUnreachable();
            
            // Resume regular generation
            m_Builder.SetInsertPoint(contBB);
        }
    }

    auto it = m_Symbols.find(baseName);
    if (it == m_Symbols.end()) {
      // Fallback for non-variable bases using PhysEntity to recover type info
      PhysEntity arrEnt = genExpr(idxExpr->Array.get());

      // Soul-Identity Protocol for Array Indexing:
      // We need the IDENTITY (the data address) to index off.
      // - If arrEnt is Reference (Soul Address):
      //   - If underlying type is Pointer (e.g. *char): The Soul stores the
      //   Identity. We MUST LOAD the Soul to get Identity.
      //   - If underlying type is Array (e.g. [10]char): The Soul IS the
      //   Identity. We use Soul address directly.

      llvm::Value *basePtr = nullptr;
      llvm::Type *elemTy = nullptr;
      auto indexedElementType = [&](std::shared_ptr<toka::Type> type) {
        if (!type)
          return static_cast<llvm::Type *>(nullptr);
        if (type->isPointer() || type->isReference())
          type = type->getPointeeType();
        if (type && (type->isSlice() || type->isArray()))
          type = type->getArrayElementType();
        return type ? getLLVMType(type) : nullptr;
      };

      if (arrEnt.isAddress) {
        llvm::Type *memTy = arrEnt.irType;
        if (!memTy)
          memTy = arrEnt.value->getType();

        if (memTy->isPointerTy()) {
          // It's a pointer variable/member (like *char source).
          // The entity value is the address OF the pointer (Soul).
          // We must LOAD to get the actual pointer (Identity).
          basePtr = m_Builder.CreateLoad(memTy, arrEnt.value, "arr_load_ptr");

          elemTy = indexedElementType(idxExpr->Array->ResolvedType);
          if (!elemTy)
            elemTy = llvm::Type::getInt8Ty(m_Context);
        } else if (memTy->isArrayTy()) {
          // It's an array variable/member (like char buf[10]).
          // The entity value is the address of the array start.
          basePtr = arrEnt.value;
          elemTy = memTy->getArrayElementType();

          // Array GEP needs [0, index] because base is pointer to array
          return m_Builder.CreateInBoundsGEP(
              memTy, basePtr, {m_Builder.getInt32(0), indexValue},
              "arr_idx_gep");
        }
      } else {
        // R-Value (e.g. function return). It's already the Identity (pointer
        // value).
        basePtr = arrEnt.value;
      }

      if (!basePtr) {
        if (arrEnt.isAddress)
          basePtr = arrEnt.load(m_Builder);
        else
          basePtr = arrEnt.value;
      }
      if (!basePtr)
        return nullptr;

      if (!elemTy) {
        elemTy = indexedElementType(idxExpr->Array->ResolvedType);
      }
      if (!elemTy) {
        // Default stride for unknown pointer types in fallback
        elemTy = llvm::Type::getInt8Ty(m_Context);
      }

      return m_Builder.CreateInBoundsGEP(elemTy, basePtr, indexValue);
    }

    TokaSymbol &sym = it->second;
    llvm::Value *currentBase = sym.allocaPtr;

    // 2. The Radar Logic (Addressing Constitution)
    if (sym.mode == AddressingMode::Direct) {
      // Stack-allocated array [N]: Base is the Identity Slot itself
      // Requires double-GEP: [0, index]
      if (sym.soulType->isArrayTy()) {
        return m_Builder.CreateInBoundsGEP(sym.soulType, currentBase,
                                           {m_Builder.getInt32(0), indexValue});
      }
      return m_Builder.CreateInBoundsGEP(sym.soulType, currentBase, indexValue);
    } else {
      // Pointer or Reference: Peel the onion
      // For Pointer, we need to load 'indirectionLevel' times to get to the
      // Soul base.
      // For Reference, it's basically load 1 time.
      int loads = sym.indirectionLevel;
      if (sym.mode == AddressingMode::Reference && loads == 0)
        loads = 1;

      for (int i = 0; i < loads; ++i) {
        currentBase = m_Builder.CreateLoad(m_Builder.getPtrTy(), currentBase,
                                           baseName + ".deref_step");
      }

      // 3. Final GEP Calculation
      if (sym.soulType->isArrayTy()) {
        return m_Builder.CreateInBoundsGEP(sym.soulType, currentBase,
                                           {m_Builder.getInt32(0), indexValue});
      }
      return m_Builder.CreateInBoundsGEP(sym.soulType, currentBase, indexValue);
    }
  }

  if (auto *mem = dynamic_cast<const MemberExpr *>(expr)) {
    // Delegate to Sovereign genMemberExpr
    PhysEntity pe = genMemberExpr(mem);
    if (pe.isAddress) return pe.value;
    return nullptr;
  }

  if (auto *post = dynamic_cast<const PostfixExpr *>(expr)) {
    if (post->Op == TokenType::TokenWrite) {
      // ptr# address is same as ptr address (Entity)
      return genAddr(post->LHS.get());
    }
  }

  return nullptr;
}

llvm::Value *CodeGen::projectSoul(llvm::Value *handle, const TokaSymbol &sym) {
  if (!handle)
    return nullptr;

  llvm::Value *current = handle;

  // 1. Direct Mode: Box is the Soul
  if (sym.mode == AddressingMode::Direct) {
    return current;
  }

  // 2. Reference Mode: Reference is a pointer alias
  if (sym.mode == AddressingMode::Reference) {
    for (int i = 0; i < sym.indirectionLevel; ++i) {
        current = m_Builder.CreateLoad(m_Builder.getPtrTy(), current, "ref.alias_soul");
    }
    return current;
  }

  // 3. Pointer Modes (Raw, Unique, Shared)
  if (sym.morphology == Morphology::Shared) {
    // allocaPtr is {T*, Ref*}*. We want T*.
    llvm::Type *ptrTy = llvm::PointerType::getUnqual(m_Context);
    llvm::Type *refTy =
        llvm::PointerType::getUnqual(llvm::Type::getInt32Ty(m_Context));
    llvm::Type *structTy = llvm::StructType::get(m_Context, {ptrTy, refTy});

    llvm::Value *dataPtrAddr =
        m_Builder.CreateStructGEP(structTy, current, 0, "shared.data_gep");
    return m_Builder.CreateLoad(m_Builder.getPtrTy(), dataPtrAddr,
                                "shared.data_ptr");
  }

  // Raw & Unique: Peeling recursive loads based on indirection level
  for (int i = 0; i < sym.indirectionLevel; ++i) {
    current =
        m_Builder.CreateLoad(m_Builder.getPtrTy(), current, "ptr.peel_soul");
  }

  return current;
}

llvm::Value *CodeGen::getEntityAddr(const std::string &name) {
  std::string baseName = name;
  while (!baseName.empty() &&
         (baseName[0] == '*' || baseName[0] == '#' || baseName[0] == '&' ||
          baseName[0] == '^' || baseName[0] == '~' || baseName[0] == '!' ||
          baseName[0] == '?'))
    baseName = baseName.substr(1);
  while (!baseName.empty() &&
         (baseName.back() == '#' || baseName.back() == '?' ||
          baseName.back() == '!'))
    baseName.pop_back();

  auto it = m_Symbols.find(baseName);
  if (it == m_Symbols.end()) {
    // [Fix] Closure Environment Fallback
    if (m_Symbols.count("self")) {
      auto selfTy = m_Symbols["self"].soulTypeObj;
      if (selfTy && selfTy->isReference()) {
        auto ptrTy = std::static_pointer_cast<toka::PointerType>(selfTy);
        selfTy = ptrTy->PointeeType;
      }
      if (selfTy && selfTy->isShape() && selfTy->getSoulName().find("__Closure_") == 0) {
        auto shapeTy = std::static_pointer_cast<ShapeType>(selfTy);
        if (shapeTy->Decl) {
          int count = 0;
          for (const auto& member : shapeTy->Decl->Members) {
            if (member.Name == baseName) {
              llvm::Value *selfAddr = getEntityAddr("self");
              if (!selfAddr) return nullptr;
              llvm::Type *structTy = getLLVMType(shapeTy);
              llvm::Value *fieldAddr = m_Builder.CreateStructGEP(structTy, selfAddr, count, "CLOSURE_CAPT_" + baseName);
              
              if (member.ResolvedType && member.ResolvedType->isReference()) {
                  llvm::Type *fieldLLVMTy = getLLVMType(member.ResolvedType);
                  return m_Builder.CreateLoad(fieldLLVMTy, fieldAddr, "CLOSURE_REF_" + baseName);
              }
              return fieldAddr;
            }
            count++;
          }
        }
      }
    }

    // Try global
    if (auto *glob = m_Module->getNamedGlobal(baseName)) {
      return glob;
    }
    std::cerr << "CodeGen Internal Error: Symbol '" << baseName
              << "' not found in getEntityAddr (and not global)\n";
    return nullptr;
  }

  TokaSymbol &sym = it->second;
  if (!sym.allocaPtr) {
    std::cerr << "CodeGen Internal Error: Symbol '" << baseName
              << "' has null allocaPtr\n";
    return nullptr;
  }

  // Unified Address Layering: Project Soul from Identity Handle
  return projectSoul(sym.allocaPtr, sym);
}

llvm::Value *CodeGen::getIdentityAddr(const std::string &name) {
  std::string baseName = name;
  while (!baseName.empty() &&
         (baseName[0] == '*' || baseName[0] == '#' || baseName[0] == '&' ||
          baseName[0] == '^' || baseName[0] == '~' || baseName[0] == '!' ||
          baseName[0] == '?'))
    baseName = baseName.substr(1);
  while (!baseName.empty() &&
         (baseName.back() == '#' || baseName.back() == '?' ||
          baseName.back() == '!'))
    baseName.pop_back();

  auto it = m_Symbols.find(baseName);
  if (it != m_Symbols.end()) {
    // The Identity is ALWAYS the allocaPtr (the box)
    return it->second.allocaPtr;
  }

  // [Fix] Closure Environment Fallback for Identity (Handle)
  if (m_Symbols.count("self")) {
    auto selfTy = m_Symbols["self"].soulTypeObj;
    if (selfTy && selfTy->isReference()) {
      auto ptrTy = std::static_pointer_cast<toka::PointerType>(selfTy);
      selfTy = ptrTy->PointeeType;
    }
    if (selfTy && selfTy->isShape() && selfTy->getSoulName().find("__Closure_") == 0) {
      auto shapeTy = std::static_pointer_cast<ShapeType>(selfTy);
      if (shapeTy->Decl) {
        int count = 0;
        for (const auto& member : shapeTy->Decl->Members) {
          if (member.Name == baseName) {
            llvm::Value *selfAddr = getEntityAddr("self");
            if (!selfAddr) return nullptr;
            llvm::Type *structTy = getLLVMType(shapeTy);
            llvm::Value *fieldAddr = m_Builder.CreateStructGEP(structTy, selfAddr, count, "CLOSURE_CAPT_ID_" + baseName);
            // The identity of a captured value inside the closure struct is the address of its field.
            return fieldAddr;
          }
          count++;
        }
      }
    }
  }

  // Try global
  if (auto *glob = m_Module->getNamedGlobal(baseName)) {
    return glob;
  }

  return nullptr;
}

llvm::Value *CodeGen::emitEntityAddr(const Expr *expr) {
  if (auto *var = dynamic_cast<const VariableExpr *>(expr)) {
    return getEntityAddr(var->codegenName());
  }

  // Try to get address directly (LValue)
  llvm::Value *addr = genAddr(expr);
  if (addr)
    return addr;

  // RValue Spill Fallback (Ch 6.2 Extension)
  // If we can't get an address (e.g. n.??point or pp??), we must evaluate
  // and spill to a temporary alloca if it's a value.
  PhysEntity pe = genExpr(expr);
  if (pe.isAddress)
    return pe.value;

  // Spill to temporary alloca
  if (!pe.value)
    return nullptr;
  llvm::Type *ty = pe.irType;
  if (!ty)
    ty = pe.value->getType();

  llvm::Value *spill = createEntryBlockAlloca(ty, nullptr, "rval.spill");
  m_Builder.CreateStore(pe.value, spill);
  return spill;
}

llvm::Value *CodeGen::emitHandleAddr(const Expr *expr) {
  if (auto *var = dynamic_cast<const VariableExpr *>(expr)) {
    return getIdentityAddr(var->codegenName());
  }
  return genAddr(expr);
}

} // namespace toka
