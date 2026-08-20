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
#include "toka/Parser.h"
#include "llvm/TargetParser/Triple.h"
#include "toka/SourceManager.h"
#include "toka/PathUtils.h"
#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/IR/DebugInfo.h"
#include <cctype>
#include <filesystem>
#include <iostream>
#include <iterator>
#include <typeinfo>

extern bool verboseMode;

namespace toka {

void CodeGen::enableDebugInfo(const std::string &sourcePath, bool optimized) {
  if (m_DebugBuilder)
    return;

  m_Module->addModuleFlag(llvm::Module::Warning, "Debug Info Version",
                          llvm::DEBUG_METADATA_VERSION);
  if (llvm::Triple(m_Module->getTargetTriple()).isOSDarwin())
    m_Module->addModuleFlag(llvm::Module::Warning, "Dwarf Version", 4);

  m_DebugBuilder = std::make_unique<llvm::DIBuilder>(*m_Module);
  std::filesystem::path path(sourcePath);
  std::string fileName = path.filename().string();
  std::string directory = path.parent_path().string();
  if (directory.empty())
    directory = ".";
  llvm::DIFile *file = m_DebugBuilder->createFile(fileName, directory);
  m_DebugFiles[sourcePath] = file;
  m_DebugCompileUnit = m_DebugBuilder->createCompileUnit(
      llvm::dwarf::DW_LANG_C_plus_plus_17, file, "Toka", optimized, "", 0);
}

void CodeGen::finalizeDebugInfo() {
  if (m_DebugBuilder)
    m_DebugBuilder->finalize();
}

llvm::DIFile *CodeGen::getDebugFile(SourceLocation loc) {
  if (!m_DebugBuilder || !DiagnosticEngine::SrcMgr || loc.isInvalid())
    return nullptr;
  FullSourceLoc full = DiagnosticEngine::SrcMgr->getFullSourceLoc(loc);
  if (!full.isValid())
    return nullptr;
  std::string sourcePath = full.FileName;
  auto existing = m_DebugFiles.find(sourcePath);
  if (existing != m_DebugFiles.end())
    return existing->second;

  std::filesystem::path path(sourcePath);
  std::string directory = path.parent_path().string();
  if (directory.empty())
    directory = ".";
  llvm::DIFile *file =
      m_DebugBuilder->createFile(path.filename().string(), directory);
  m_DebugFiles[sourcePath] = file;
  return file;
}

llvm::DIType *CodeGen::getDebugType(llvm::Type *type) {
  if (!m_DebugBuilder || !type)
    return nullptr;
  auto existing = m_DebugTypes.find(type);
  if (existing != m_DebugTypes.end())
    return existing->second;

  llvm::DIType *debugType = nullptr;
  if (type->isIntegerTy()) {
    unsigned bits = type->getIntegerBitWidth();
    debugType = m_DebugBuilder->createBasicType(
        bits == 1 ? "bool" : "i" + std::to_string(bits), bits,
        bits == 1 ? llvm::dwarf::DW_ATE_boolean : llvm::dwarf::DW_ATE_signed);
  } else if (type->isFloatingPointTy()) {
    uint64_t bits = m_Module->getDataLayout().getTypeSizeInBits(type);
    debugType = m_DebugBuilder->createBasicType(
        type->isFloatTy() ? "f32" : "f64", bits, llvm::dwarf::DW_ATE_float);
  } else if (type->isPointerTy()) {
    uint64_t bits = m_Module->getDataLayout().getPointerSizeInBits();
    debugType = m_DebugBuilder->createPointerType(
        m_DebugBuilder->createUnspecifiedType("opaque"), bits);
  } else if (type->isVoidTy()) {
    debugType = nullptr;
  } else {
    std::string name;
    llvm::raw_string_ostream stream(name);
    type->print(stream);
    debugType = m_DebugBuilder->createUnspecifiedType(stream.str());
  }
  m_DebugTypes[type] = debugType;
  return debugType;
}

void CodeGen::beginDebugFunction(const FunctionDecl *func,
                                 llvm::Function *function,
                                 const std::string &linkageName) {
  m_CurrentDebugScope = nullptr;
  m_Builder.SetCurrentDebugLocation(llvm::DebugLoc());
  if (!m_DebugBuilder || !func || !function || !m_DebugCompileUnit ||
      !DiagnosticEngine::SrcMgr)
    return;

  FullSourceLoc full = DiagnosticEngine::SrcMgr->getFullSourceLoc(func->Loc);
  llvm::DIFile *file = getDebugFile(func->Loc);
  if (!file || !full.isValid())
    return;

  llvm::DISubroutineType *type = m_DebugBuilder->createSubroutineType(
      m_DebugBuilder->getOrCreateTypeArray({}));
  llvm::DISubprogram::DISPFlags flags =
      llvm::DISubprogram::SPFlagDefinition;
  m_CurrentDebugScope = m_DebugBuilder->createFunction(
      file, func->Name, linkageName, file, full.Line, type, full.Line,
      llvm::DINode::FlagPrototyped, flags);
  function->setSubprogram(m_CurrentDebugScope);
  m_Builder.SetCurrentDebugLocation(llvm::DILocation::get(
      m_Context, full.Line, full.Column, m_CurrentDebugScope));
}

void CodeGen::setDebugLocation(const ASTNode *node) {
  if (!m_DebugBuilder || !m_CurrentDebugScope || !DiagnosticEngine::SrcMgr ||
      !node ||
      node->Loc.isInvalid())
    return;
  FullSourceLoc full = DiagnosticEngine::SrcMgr->getFullSourceLoc(node->Loc);
  if (!full.isValid())
    return;
  m_Builder.SetCurrentDebugLocation(llvm::DILocation::get(
      m_Context, full.Line, full.Column, m_CurrentDebugScope));
}

void CodeGen::emitDebugVariable(const std::string &name, llvm::Value *storage,
                                llvm::Type *type, SourceLocation loc,
                                bool isParameter, unsigned argNo) {
  if (!m_DebugBuilder || !m_CurrentDebugScope || !DiagnosticEngine::SrcMgr ||
      !storage || !type)
    return;
  FullSourceLoc full = DiagnosticEngine::SrcMgr->getFullSourceLoc(loc);
  llvm::DIFile *file = getDebugFile(loc);
  if (!file || !full.isValid())
    return;

  llvm::DIType *debugType = getDebugType(type);
  llvm::DILocalVariable *variable =
      isParameter
          ? m_DebugBuilder->createParameterVariable(
                m_CurrentDebugScope, name, argNo, file, full.Line, debugType,
                true)
          : m_DebugBuilder->createAutoVariable(m_CurrentDebugScope, name, file,
                                               full.Line, debugType, true);
  const llvm::DILocation *debugLoc = llvm::DILocation::get(
      m_Context, full.Line, full.Column, m_CurrentDebugScope);
  m_DebugBuilder->insertDeclare(storage, variable,
                                m_DebugBuilder->createExpression(), debugLoc,
                                m_Builder.GetInsertBlock());
}

void CodeGen::markMemoryEvent(llvm::Instruction *instruction,
                              const char *event) {
  if (!instruction)
    return;
  instruction->setMetadata(
      "toka.memory.local",
      llvm::MDNode::get(m_Context, llvm::MDString::get(m_Context, event)));
}

llvm::IntegerType* CodeGen::getIntPtrTy() {
  llvm::Triple triple(m_Module->getTargetTriple());
  if (triple.isArch32Bit()) {
    return llvm::Type::getInt32Ty(m_Context);
  }
  return llvm::Type::getInt64Ty(m_Context);
}

PhysEntity CodeGen::genExpr(const Expr *expr) {
  const bool isOutermostExpression = m_ExpressionDepth == 0;
  if (isOutermostExpression)
    m_FullExpressionTemporaries.clear();
  ++m_ExpressionDepth;
  PhysEntity result = genExprImpl(expr);
  --m_ExpressionDepth;
  if (isOutermostExpression)
    emitFullExpressionTemporaryDrops();
  return result;
}

PhysEntity CodeGen::genExprImpl(const Expr *expr) {
  if (!expr)
    return {};

  if (m_Builder.GetInsertBlock() && m_Builder.GetInsertBlock()->getTerminator())
    return {};

  setDebugLocation(expr);

  // 1. Basic Expressions
  if (auto e = dynamic_cast<const BinaryExpr *>(expr))
    return genBinaryExpr(e);
  if (auto e = dynamic_cast<const UnaryExpr *>(expr))
    return genUnaryExpr(e);
  if (auto e = dynamic_cast<const VariableExpr *>(expr))
    return genVariableExpr(e);
  if (auto e = dynamic_cast<const TodoExpr *>(expr)) {
    error(e, DiagID::ERR_TYPED_TODO_INCOMPLETE,
          e->ResolvedType ? e->ResolvedType->toString() : "unknown");
    return {};
  }

  // 2. Literal Expressions (Crucial fix: manually list actual class names in AST)
  if (dynamic_cast<const NumberExpr *>(expr) ||
      dynamic_cast<const FloatExpr *>(expr) ||
      dynamic_cast<const BoolExpr *>(expr) ||
      dynamic_cast<const NullExpr *>(expr) ||
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
    auto operandType = e->OperandType;
    if (!operandType)
      operandType = lowerTypeSyntax(e->TypeSyntax, e->TypeStr);
    llvm::Type *targetTy = getLLVMType(operandType);
    if (!targetTy) {
      error(e, DiagID::ERR_CODEGEN_CANNOT_DETERMINE_SIZE_OF_INCOMPLETE_TY, e->TypeStr);
      return PhysEntity(llvm::ConstantInt::get(getIntPtrTy(), 0), "usize", getIntPtrTy(), false);
    }
    llvm::Value *nullPtr = llvm::ConstantPointerNull::get(llvm::PointerType::getUnqual(m_Context));
    llvm::Value *gep = m_Builder.CreateGEP(targetTy, nullPtr, m_Builder.getInt32(1));
    llvm::Value *size = m_Builder.CreatePtrToInt(gep, getIntPtrTy());
    return PhysEntity(size, "usize", getIntPtrTy(), false);
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

void CodeGen::registerFullExpressionTemporary(llvm::Value *address,
                                              std::shared_ptr<Type> type) {
  if (!address || !type || !m_Builder.GetInsertBlock())
    return;

  llvm::AllocaInst *liveFlag = createEntryBlockAlloca(
      llvm::Type::getInt1Ty(m_Context), nullptr, "expr.tmp.live");
  llvm::BasicBlock *entry = &liveFlag->getFunction()->getEntryBlock();
  llvm::IRBuilder<> entryBuilder(entry, std::next(liveFlag->getIterator()));
  entryBuilder.CreateStore(llvm::ConstantInt::getFalse(m_Context), liveFlag);
  m_Builder.CreateStore(llvm::ConstantInt::getTrue(m_Context), liveFlag);
  m_FullExpressionTemporaries.push_back(
      {address, std::move(type), liveFlag});
}

void CodeGen::emitFullExpressionTemporaryDrops(bool clear) {
  if (!m_Builder.GetInsertBlock() ||
      m_Builder.GetInsertBlock()->getTerminator()) {
    if (clear)
      m_FullExpressionTemporaries.clear();
    return;
  }
  for (auto temporary = m_FullExpressionTemporaries.rbegin();
       temporary != m_FullExpressionTemporaries.rend(); ++temporary) {
    llvm::Function *function = m_Builder.GetInsertBlock()->getParent();
    llvm::BasicBlock *dropBlock =
        llvm::BasicBlock::Create(m_Context, "expr.tmp.drop", function);
    llvm::BasicBlock *doneBlock =
        llvm::BasicBlock::Create(m_Context, "expr.tmp.done", function);
    llvm::Value *isLive = m_Builder.CreateLoad(
        llvm::Type::getInt1Ty(m_Context), temporary->LiveFlag,
        "expr.tmp.is_live");
    m_Builder.CreateCondBr(isLive, dropBlock, doneBlock);

    m_Builder.SetInsertPoint(dropBlock);
    emitDropForType(temporary->Address, temporary->TypeObj);
    if (!m_Builder.GetInsertBlock()->getTerminator())
      m_Builder.CreateBr(doneBlock);

    m_Builder.SetInsertPoint(doneBlock);
    m_Builder.CreateStore(llvm::ConstantInt::getFalse(m_Context),
                          temporary->LiveFlag);
  }
  if (clear)
    m_FullExpressionTemporaries.clear();
}

llvm::Value *CodeGen::genStmt(const Stmt *stmt) {
  const unsigned outerDepth = m_ExpressionDepth;
  auto outerTemporaries = std::move(m_FullExpressionTemporaries);
  m_ExpressionDepth = 0;
  m_FullExpressionTemporaries.clear();

  llvm::Value *result = genStmtImpl(stmt);
  emitFullExpressionTemporaryDrops();

  m_ExpressionDepth = outerDepth;
  m_FullExpressionTemporaries = std::move(outerTemporaries);
  return result;
}

llvm::Value *CodeGen::genStmtImpl(const Stmt *stmt) {
  if (!stmt)
    return nullptr;

  setDebugLocation(stmt);

  if (auto s = dynamic_cast<const BlockStmt *>(stmt))
    return genBlockStmt(s);
  if (auto s = dynamic_cast<const InitBlockStmt *>(stmt))
    return genBlockStmt(s->Body.get());
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
    if (!sh->CodegenName.empty())
      m_Shapes[sh->CodegenName] = sh.get();
    m_Shapes[sh->Name] = sh.get();

  }
  for (const auto &alias : ast.TypeAliases) {
    std::shared_ptr<Type> target =
        lowerTypeSyntax(alias->TargetTypeSyntax, alias->TargetType);
    llvm::Triple triple(m_Module->getTargetTriple());
    if (triple.isArch32Bit()) {
      if (alias->Name == "usize" || alias->Name == "Addr" || alias->Name == "OAddr") {
        target = std::make_shared<PrimitiveType>("u32");
      } else if (alias->Name == "isize") {
        target = std::make_shared<PrimitiveType>("i32");
      }
    }
    m_TypeAliases[alias->Name] = target;
  }
  for (const auto &func : ast.Functions)
    m_Functions[func->Name] = func.get();
  for (const auto &ext : ast.Externs)
    m_Externs[ext->Name] = ext.get();
  for (const auto &trait : ast.Traits)
    m_Traits[trait->Name] = trait.get();
}

std::shared_ptr<Type>
CodeGen::lowerTypeSyntax(const TypeSyntaxPtr &syntax,
                         const std::string &legacy) const {
  if (syntax)
    return Type::fromSyntax(syntax);

  // Source-less interfaces and a small set of compiler-synthesized AST nodes
  // predate TypeSyntax.  Keep their compatibility parse in this one named
  // boundary; parser-derived declarations reach CodeGen through `syntax` or
  // Sema's ResolvedType instead.
  return Type::fromString(legacy);
}

std::string CodeGen::ownerLinkName(const std::shared_ptr<Type> &type) const {
  if (!type)
    return {};

  auto soul = type->getSoulType();
  if (!soul)
    return {};

  if (auto shape = std::dynamic_pointer_cast<ShapeType>(soul);
      shape && shape->Decl) {
    if (shape->Decl->OwnerLinkName.rfind("__toka_owner_", 0) == 0)
      return shape->Decl->OwnerLinkName;
    if (!shape->Decl->CodegenName.empty())
      return shape->Decl->CodegenName;
    return shape->Decl->Name;
  }

  return Type::stripMorphology(soul->getSoulName());
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
  bool declOnly = !ast.IsRootModule && ast.IsInterface;

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
    const bool recheckOutcomeBody = declOnly && func->Body &&
        func->ResolvedOutcomeTransition.has_value();
    genFunction(func.get(), "", declOnly && !recheckOutcomeBody);
    if (hasErrors())
      return;
  }
}

void CodeGen::print(llvm::raw_ostream &os) { m_Module->print(os, nullptr); }

bool CodeGen::shouldReturnSRet(std::shared_ptr<Type> retTypeObj) {
  if (!retTypeObj || retTypeObj->isPointer())
    return false;

  auto soul = retTypeObj->getSoulType();
  if (!soul || (!soul->isShape() && !soul->isArray()))
    return false;

  llvm::Type *llvmType = getLLVMType(retTypeObj);
  if (!llvmType || !llvmType->isSized())
    return false;

  const llvm::DataLayout &dataLayout = m_Module->getDataLayout();
  return dataLayout.getTypeAllocSize(llvmType).getFixedValue() > 16;
}

CodeGen::GenContext CodeGen::saveContext() {
  GenContext ctx;
  ctx.Symbols = m_Symbols;
  ctx.NamedValues = m_NamedValues;
  ctx.CurrentSelfType = m_CurrentSelfType;
  ctx.CFStack = m_CFStack;
  ctx.ScopeStack = m_ScopeStack;
  ctx.ExpressionDepth = m_ExpressionDepth;
  ctx.FullExpressionTemporaries = m_FullExpressionTemporaries;
  ctx.InsertBlock = m_Builder.GetInsertBlock();
  if (ctx.InsertBlock)
    ctx.InsertPoint = m_Builder.GetInsertPoint();
  ctx.CurrentCoroHandle = m_CurrentCoroHandle;
  ctx.CurrentCoroPromise = m_CurrentCoroPromise;
  ctx.CurrentCoroTCB = m_CurrentCoroTCB;
  ctx.CurrentCoroId = m_CurrentCoroId;
  ctx.CurrentCoroPromiseType = m_CurrentCoroPromiseType;
  ctx.CurrentCoroRetTy = m_CurrentCoroRetTy;
  ctx.CurrentCoroSuspendRetBB = m_CurrentCoroSuspendRetBB;
  ctx.CurrentCoroCleanupBB = m_CurrentCoroCleanupBB;
  ctx.CurrentCoroFinalSuspendBB = m_CurrentCoroFinalSuspendBB;
  ctx.CurrentSRetPtr = m_CurrentSRetPtr;
  ctx.CurrentSRetTy = m_CurrentSRetTy;
  ctx.CurrentDebugScope = m_CurrentDebugScope;
  ctx.DebugLocation = m_Builder.getCurrentDebugLocation();
  return ctx;
}

void CodeGen::restoreContext(const GenContext &ctx) {
  m_Symbols = ctx.Symbols;
  m_NamedValues = ctx.NamedValues;
  m_CurrentSelfType = ctx.CurrentSelfType;
  m_CFStack = ctx.CFStack;
  m_ScopeStack = ctx.ScopeStack;
  m_ExpressionDepth = ctx.ExpressionDepth;
  m_FullExpressionTemporaries = ctx.FullExpressionTemporaries;
  m_CurrentCoroHandle = ctx.CurrentCoroHandle;
  m_CurrentCoroPromise = ctx.CurrentCoroPromise;
  m_CurrentCoroTCB = ctx.CurrentCoroTCB;
  m_CurrentCoroId = ctx.CurrentCoroId;
  m_CurrentCoroPromiseType = ctx.CurrentCoroPromiseType;
  m_CurrentCoroRetTy = ctx.CurrentCoroRetTy;
  m_CurrentCoroSuspendRetBB = ctx.CurrentCoroSuspendRetBB;
  m_CurrentCoroCleanupBB = ctx.CurrentCoroCleanupBB;
  m_CurrentCoroFinalSuspendBB = ctx.CurrentCoroFinalSuspendBB;
  m_CurrentSRetPtr = ctx.CurrentSRetPtr;
  m_CurrentSRetTy = ctx.CurrentSRetTy;
  m_CurrentDebugScope = ctx.CurrentDebugScope;
  m_Builder.SetCurrentDebugLocation(ctx.DebugLocation);
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
