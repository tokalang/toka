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
#include "toka/DiagnosticEngine.h"
#include "toka/SourceManager.h"
#include <cctype>
#include <iostream>
#include <set>
#include <typeinfo>

extern bool verboseMode;

namespace toka {

// Inside the genExpr function of src/CodeGen/CodeGen.cpp
PhysEntity CodeGen::genExpr(const Expr *expr) {
  if (!expr)
    return {};

  if (m_Builder.GetInsertBlock() && m_Builder.GetInsertBlock()->getTerminator())
    return {};

  // 1. Basic Expressions
  if (auto e = dynamic_cast<const BinaryExpr *>(expr))
    return genBinaryExpr(e);
  if (auto e = dynamic_cast<const UnaryExpr *>(expr))
    return genUnaryExpr(e);
  if (auto e = dynamic_cast<const VariableExpr *>(expr))
    return genVariableExpr(e);

  // 2. Literal Expressions (Crucial fix: manually list actual class names in AST)
  if (dynamic_cast<const NumberExpr *>(expr) ||
      dynamic_cast<const FloatExpr *>(expr) ||
      dynamic_cast<const BoolExpr *>(expr) ||
      dynamic_cast<const NullExpr *>(expr) ||
      dynamic_cast<const NoneExpr *>(expr) ||
      dynamic_cast<const StringExpr *>(expr) ||
      dynamic_cast<const ViewStringExpr *>(expr) ||
      dynamic_cast<const CharLiteralExpr *>(expr)) {
    return genLiteralExpr(expr);
  }
  if (auto e = dynamic_cast<const AnonymousRecordExpr *>(expr))
    return genAnonymousRecordExpr(e);
  if (auto e = dynamic_cast<const ArrayExpr *>(expr))
    return genArrayExpr(e);
  if (auto e = dynamic_cast<const RepeatedArrayExpr *>(expr))
    return genRepeatedArrayExpr(e);

  // 3. Memory & Member Access
  if (auto e = dynamic_cast<const MemberExpr *>(expr))
    return genMemberExpr(e);
  if (auto e = dynamic_cast<const ArrayIndexExpr *>(expr))
    return genIndexExpr(e);
  if (auto e = dynamic_cast<const AllocExpr *>(expr))
    return genAllocExpr(e);

  // 4. Control Flow & Advanced Expressions
  if (auto e = dynamic_cast<const CastExpr *>(expr))
    return genCastExpr(e);
  if (auto e = dynamic_cast<const MatchExpr *>(expr))
    return genMatchExpr(e);
  if (auto e = dynamic_cast<const IfExpr *>(expr))
    return genIfExpr(e);
  if (auto e = dynamic_cast<const SizeOfExpr *>(expr)) {
    llvm::Type *targetTy = getLLVMType(toka::Type::fromString(e->TypeStr));
    if (!targetTy) {
      error(e, DiagID::ERR_CODEGEN_CANNOT_DETERMINE_SIZE_OF_INCOMPLETE_TY, e->TypeStr);
      return PhysEntity(llvm::ConstantInt::get(llvm::Type::getInt64Ty(m_Context), 0), "usize", llvm::Type::getInt64Ty(m_Context), false);
    }
    llvm::Value *nullPtr = llvm::ConstantPointerNull::get(llvm::PointerType::getUnqual(m_Context));
    llvm::Value *gep = m_Builder.CreateGEP(targetTy, nullPtr, m_Builder.getInt32(1));
    llvm::Value *size = m_Builder.CreatePtrToInt(gep, m_Builder.getInt64Ty());
    return PhysEntity(size, "usize", m_Builder.getInt64Ty(), false);
  }
  if (auto e = dynamic_cast<const GuardExpr *>(expr))
    return genGuardExpr(e);
  if (auto e = dynamic_cast<const LoopExpr *>(expr))
    return genLoopExpr(e);
  if (auto e = dynamic_cast<const AwaitExpr *>(expr))
    return genAwaitExpr(e);
  if (auto e = dynamic_cast<const WaitExpr *>(expr))
    return genWaitExpr(e);
  if (auto e = dynamic_cast<const ForExpr *>(expr))
    return genForExpr(e);
  if (auto e = dynamic_cast<const MethodCallExpr *>(expr))
    return genMethodCall(e);
  if (auto e = dynamic_cast<const CallExpr *>(expr))
    return genCallExpr(e);
  if (auto e = dynamic_cast<const PostfixExpr *>(expr))
    return genPostfixExpr(e);
  if (auto e = dynamic_cast<const UnwrapPropagationExpr *>(expr))
    return genUnwrapPropagationExpr(e);
  if (auto e = dynamic_cast<const AwaitExpr *>(expr)) {
      if (!m_CurrentCoroHandle) {
          error(e, DiagID::ERR_CODEGEN_AWAIT_CAN_ONLY_BE_USED_INSIDE_AN_ASYNC);
          return {};
      }
      llvm::Function *suspendFn = llvm::Intrinsic::getOrInsertDeclaration(m_Module.get(), llvm::Intrinsic::coro_suspend);
      llvm::Value *suspendRes = m_Builder.CreateCall(suspendFn, {llvm::ConstantTokenNone::get(m_Context), m_Builder.getInt1(false)});
      
      llvm::BasicBlock *suspendBB = llvm::BasicBlock::Create(m_Context, "await.suspend", m_Builder.GetInsertBlock()->getParent());
      llvm::BasicBlock *resumeBB = llvm::BasicBlock::Create(m_Context, "await.resume", m_Builder.GetInsertBlock()->getParent());
      llvm::BasicBlock *cleanupBB = llvm::BasicBlock::Create(m_Context, "await.cleanup", m_Builder.GetInsertBlock()->getParent());
      
      llvm::SwitchInst *sw = m_Builder.CreateSwitch(suspendRes, suspendBB, 2);
      sw->addCase(m_Builder.getInt8(0), resumeBB);
      sw->addCase(m_Builder.getInt8(1), cleanupBB);
      
      m_Builder.SetInsertPoint(suspendBB);
      m_Builder.CreateRet(m_CurrentCoroHandle);
      
      m_Builder.SetInsertPoint(cleanupBB);
      llvm::Function *freeIdFn = llvm::Intrinsic::getOrInsertDeclaration(m_Module.get(), llvm::Intrinsic::coro_free);
      llvm::Value *memToFree = m_Builder.CreateCall(freeIdFn, {m_CurrentCoroId, m_CurrentCoroHandle});
      llvm::Function *freeFn = m_Module->getFunction("free");
      m_Builder.CreateCall(freeFn, memToFree);
      m_Builder.CreateUnreachable();
      
      m_Builder.SetInsertPoint(resumeBB);
      
      return PhysEntity(llvm::ConstantInt::get(m_Builder.getInt32Ty(), 0), "i32", m_Builder.getInt32Ty(), false);
  }
  if (auto e = dynamic_cast<const WaitExpr *>(expr))
    return genExpr(e->Expression.get());
  if (auto e = dynamic_cast<const StartExpr *>(expr)) {
    return genStartExpr(e);
  }
  if (auto e = dynamic_cast<const ClosureExpr *>(expr))
    return genClosureExpr(e);
  if (auto e = dynamic_cast<const InitStructExpr *>(expr))
    return genInitStructExpr(e);
  if (auto e = dynamic_cast<const PassExpr *>(expr))
    return genPassExpr(e);
  if (auto e = dynamic_cast<const CedeExpr *>(expr))
    return genCedeExpr(e);
  if (auto e = dynamic_cast<const BreakExpr *>(expr))
    return genBreakExpr(e);
  if (auto e = dynamic_cast<const ContinueExpr *>(expr))
    return genContinueExpr(e);
  if (auto e = dynamic_cast<const UnsafeExpr *>(expr))
    return genUnsafeExpr(e);
  if (auto e = dynamic_cast<const ArrayInitExpr *>(expr))
    return genArrayInitExpr(e);

  if (auto e = dynamic_cast<const NewExpr *>(expr))
    return genNewExpr(e);

  // [Phase 2] Comptime Intrinsic Fallbacks
  if (auto e = dynamic_cast<const ComptimeReflectExpr *>(expr))
    return genComptimeReflectExpr(e);
  if (auto e = dynamic_cast<const ComptimeFieldExpr *>(expr)) {
    error(e, DiagID::ERR_CODEGEN_COMPILE_TIME_FIELD_ITERATION_VARIABLES);
    return {};
  }

  return {};
}

llvm::Value *CodeGen::genStmt(const Stmt *stmt) {
  if (!stmt)
    return nullptr;

  if (auto s = dynamic_cast<const BlockStmt *>(stmt))
    return genBlockStmt(s);
  if (auto s = dynamic_cast<const ReturnStmt *>(stmt))
    return genReturnStmt(s);
  if (auto s = dynamic_cast<const VariableDecl *>(stmt))
    return genVariableDecl(s);
  if (auto s = dynamic_cast<const DestructuringDecl *>(stmt))
    return genDestructuringDecl(s);
  if (auto s = dynamic_cast<const DeleteStmt *>(stmt))
    return genDeleteStmt(s);
  if (auto s = dynamic_cast<const FreeStmt *>(stmt))
    return genFreeStmt(s);
  if (auto s = dynamic_cast<const UnsafeStmt *>(stmt))
    return genUnsafeStmt(s);
  if (auto s = dynamic_cast<const UnreachableStmt *>(stmt))
    return genUnreachableStmt(s);
  if (auto s = dynamic_cast<const GuardBindStmt *>(stmt))
    return genGuardBindStmt(s);
  if (auto s = dynamic_cast<const ExprStmt *>(stmt))
    // genExprStmt returns Value* (wrapper) or PhysEntity?
    // CodeGen.h: genExprStmt returns Value*.
    // Wait. In CodeGen.h I didn't verify genExprStmt signature update.
    // I assumed genExprStmt returns Value* because genStmt signature didn't
    // change. Let's assume genExprStmt returns Value* for now.
    return genExprStmt(s);

  // If Stmt is a wrapper around Expr
  if (auto e = dynamic_cast<const Expr *>(stmt))
    return genExpr(e).load(m_Builder);

  return nullptr;
}

void CodeGen::discover(const Module &ast) {
  m_AST = &ast;
  if (!m_Module) {
    m_Module = std::make_unique<llvm::Module>("toka_module", m_Context);
  }

  // Phase 1: Registration (Names only)
  for (const auto &sh : ast.Shapes) {
    m_Shapes[sh->Name] = sh.get();

  }
  for (const auto &alias : ast.TypeAliases)
    m_TypeAliases[alias->Name] = alias->TargetType;
  for (const auto &func : ast.Functions)
    m_Functions[func->Name] = func.get();
  for (const auto &ext : ast.Externs)
    m_Externs[ext->Name] = ext.get();
  for (const auto &trait : ast.Traits)
    m_Traits[trait->Name] = trait.get();
}

void CodeGen::resolveSignatures(const Module &ast) {
  m_AST = &ast;

  // Phase 2: Declaration (Signatures and Types)
  // Shapes first (for struct layouts)
  for (const auto &sh : ast.Shapes) {
    genShape(sh.get());
    if (hasErrors())
      return;
  }

  for (const auto &ext : ast.Externs) {
    genExtern(ext.get());
    if (hasErrors())
      return;
  }

  // [Fix] Generate Impl declarations BEFORE functions
  for (const auto &impl : ast.Impls) {
    genImpl(impl.get(), true);
    if (hasErrors())
      return;
  }

  for (const auto &func : ast.Functions) {
    genFunction(func.get(), "", true);
    if (hasErrors())
      return;
  }
}

void CodeGen::generate(const Module &ast) {
  m_AST = &ast;
  bool declOnly = !ast.IsRootModule && (ast.SourcePath.length() > 4 && ast.SourcePath.substr(ast.SourcePath.length() - 4) == ".tki");

  // Generate Globals (Emission)
  for (const auto &glob : ast.Globals) {
    genGlobal(glob.get());
    if (hasErrors()) return;
  }

  // [Fix] Generate Impl bodies BEFORE function bodies so drop() exists
  for (const auto &impl : ast.Impls) {
    genImpl(impl.get(), declOnly);
    if (hasErrors()) return;
  }

  // Generate Functions (Body Phase)
  for (const auto &func : ast.Functions) {
    genFunction(func.get(), "", declOnly);
    if (hasErrors())
      return;
  }
}

void CodeGen::print(llvm::raw_ostream &os) { m_Module->print(os, nullptr); }



uint64_t CodeGen::estimateTypeSize(std::shared_ptr<Type> type, std::set<std::string> &visited) {
  if (!type) return 0;

  // Handle nullable wrapper: { T, bool } aligned
  if (type->IsNullable && !type->isPointer() && !type->isSmartPointer() &&
      !type->isReference() && !type->isVoid()) {
    auto baseTyObj = type->withAttributes(type->IsWritable, false, type->IsBlocked);
    return estimateTypeSize(baseTyObj, visited) + 8;
  }

  if (type->typeKind == Type::Primitive) {
    auto prim = std::static_pointer_cast<PrimitiveType>(type);
    if (prim->Name == "i8" || prim->Name == "u8" || prim->Name == "byte" || prim->Name == "char" || prim->Name == "bool" || prim->Name == "i1")
      return 1;
    if (prim->Name == "i16" || prim->Name == "u16")
      return 2;
    if (prim->Name == "i32" || prim->Name == "u32" || prim->Name == "int" || prim->Name == "f32" || prim->Name == "float")
      return 4;
    return 8; // i64, u64, usize, isize, double, cstring, Addr, OAddr, null, etc.
  }

  if (type->typeKind == Type::Void) return 0;

  if (type->typeKind == Type::RawPtr || type->typeKind == Type::UniquePtr || type->typeKind == Type::Reference) {
    if (type->isFatPointer()) return 16;
    return 8;
  }

  if (type->typeKind == Type::SharedPtr) return 16;

  if (type->typeKind == Type::UninitWrapper) {
    auto uninit = std::static_pointer_cast<UninitType>(type);
    return estimateTypeSize(uninit->InnerType, visited);
  }

  if (type->typeKind == Type::Slice) return 16; // passed by fat pointer reference or similar

  if (type->typeKind == Type::Array) {
    auto arrType = std::static_pointer_cast<ArrayType>(type);
    return estimateTypeSize(arrType->ElementType, visited) * arrType->Size;
  }

  if (type->typeKind == Type::Shape) {
    auto shapeType = std::static_pointer_cast<ShapeType>(type);
    std::string shapeName = shapeType->Name;

    // Break cycle if visited
    if (visited.count(shapeName)) return 8; // fallback to pointer size to prevent cycles
    visited.insert(shapeName);

    const ShapeDecl *sh = nullptr;
    if (shapeType->Decl) sh = shapeType->Decl;
    else if (m_Shapes.count(shapeName)) sh = m_Shapes[shapeName];

    if (sh) {
      if (sh->Kind == ShapeKind::Struct || sh->Kind == ShapeKind::Tuple) {
        uint64_t totalSize = 0;
        for (const auto &member : sh->Members) {
          std::shared_ptr<Type> membType = member.ResolvedType;
          totalSize += membType ? estimateTypeSize(membType, visited) : 8;
        }
        visited.erase(shapeName);
        return totalSize;
      } else if (sh->Kind == ShapeKind::Array) {
        std::shared_ptr<Type> elemTy = sh->Members[0].ResolvedType;
        uint64_t elemSize = elemTy ? estimateTypeSize(elemTy, visited) : 8;
        visited.erase(shapeName);
        return elemSize * sh->ArraySize;
      } else if (sh->Kind == ShapeKind::Union) {
        uint64_t maxSize = 0;
        for (const auto &member : sh->Members) {
          std::shared_ptr<Type> membType = member.ResolvedType;
          uint64_t s = membType ? estimateTypeSize(membType, visited) : 8;
          if (s > maxSize) maxSize = s;
        }
        visited.erase(shapeName);
        return maxSize;
      }
    }
    visited.erase(shapeName);
  }

  if (type->typeKind == Type::Function) return 16;
  if (type->typeKind == Type::DynFn) return 24;

  return 8;
}

bool CodeGen::shouldReturnSRet(std::shared_ptr<Type> retTypeObj) {
  if (!retTypeObj)
    return false;
  if (retTypeObj->isPointer())
    return false;
  auto soul = retTypeObj->getSoulType();
  if (soul->isShape() || soul->isArray()) {
    std::set<std::string> visited;
    uint64_t size = estimateTypeSize(soul, visited);
    return size > 16;
  }
  return false;
}

CodeGen::GenContext CodeGen::saveContext() {
  GenContext ctx;
  ctx.Symbols = m_Symbols;
  ctx.NamedValues = m_NamedValues;
  ctx.CurrentSelfType = m_CurrentSelfType;
  ctx.CFStack = m_CFStack;
  ctx.ScopeStack = m_ScopeStack;
  ctx.InsertBlock = m_Builder.GetInsertBlock();
  if (ctx.InsertBlock)
    ctx.InsertPoint = m_Builder.GetInsertPoint();
  ctx.CurrentCoroHandle = m_CurrentCoroHandle;
  ctx.CurrentCoroPromise = m_CurrentCoroPromise;
  ctx.CurrentCoroId = m_CurrentCoroId;
  ctx.CurrentCoroPromiseType = m_CurrentCoroPromiseType;
  ctx.CurrentCoroRetTy = m_CurrentCoroRetTy;
  ctx.CurrentCoroSuspendRetBB = m_CurrentCoroSuspendRetBB;
  ctx.CurrentCoroCleanupBB = m_CurrentCoroCleanupBB;
  ctx.CurrentCoroFinalSuspendBB = m_CurrentCoroFinalSuspendBB;
  ctx.CurrentSRetPtr = m_CurrentSRetPtr;
  ctx.CurrentSRetTy = m_CurrentSRetTy;
  return ctx;
}

void CodeGen::restoreContext(const GenContext &ctx) {
  m_Symbols = ctx.Symbols;
  m_NamedValues = ctx.NamedValues;
  m_CurrentSelfType = ctx.CurrentSelfType;
  m_CFStack = ctx.CFStack;
  m_ScopeStack = ctx.ScopeStack;
  m_CurrentCoroHandle = ctx.CurrentCoroHandle;
  m_CurrentCoroPromise = ctx.CurrentCoroPromise;
  m_CurrentCoroId = ctx.CurrentCoroId;
  m_CurrentCoroPromiseType = ctx.CurrentCoroPromiseType;
  m_CurrentCoroRetTy = ctx.CurrentCoroRetTy;
  m_CurrentCoroSuspendRetBB = ctx.CurrentCoroSuspendRetBB;
  m_CurrentCoroCleanupBB = ctx.CurrentCoroCleanupBB;
  m_CurrentCoroFinalSuspendBB = ctx.CurrentCoroFinalSuspendBB;
  m_CurrentSRetPtr = ctx.CurrentSRetPtr;
  m_CurrentSRetTy = ctx.CurrentSRetTy;
  if (ctx.InsertBlock) {
    if (ctx.InsertPoint != ctx.InsertBlock->end())
      m_Builder.SetInsertPoint(ctx.InsertBlock, ctx.InsertPoint);
    else
      m_Builder.SetInsertPoint(ctx.InsertBlock);
  } else {
    m_Builder.ClearInsertionPoint();
  }
}


llvm::AllocaInst *CodeGen::createEntryBlockAlloca(llvm::Type *type, llvm::Value *ArraySize, const std::string &varName) {
  if (verboseMode) {
    std::cerr << "[DEBUG createEntryBlockAlloca] varName=" << varName << " type=";
    if (type) {
      std::string typeStr;
      llvm::raw_string_ostream os(typeStr);
      type->print(os);
      std::cerr << os.str();
    } else {
      std::cerr << "NULL";
    }
    std::cerr << std::endl;
  }
  llvm::Function *TheFunction = m_Builder.GetInsertBlock()->getParent();
  if (TheFunction->empty()) {
    llvm::BasicBlock *Entry = llvm::BasicBlock::Create(m_Context, "entry", TheFunction);
  }
  llvm::IRBuilder<> TmpB(&TheFunction->getEntryBlock(), TheFunction->getEntryBlock().begin());
  return TmpB.CreateAlloca(type, ArraySize, varName);
}

} // namespace toka