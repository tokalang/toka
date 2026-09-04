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
#include "toka/DiagnosticEngine.h"
#include "toka/HandleGrammarAudit.h"
#include "toka/MemberAccess.h"
#include "toka/Type.h"
#include "toka/Parser.h"
#include "toka/PathUtils.h"
#include "llvm/Transforms/Utils/ModuleUtils.h"
#include "llvm/IR/Comdat.h"
#include "llvm/TargetParser/Triple.h"
#include <cctype>
#include <iostream>
#include <set>
#include <string>
#include <typeinfo>

extern bool verboseMode;

namespace toka {

static std::string getTraitFamilyNameForCodeGen(std::string traitName) {
  if (!traitName.empty() && traitName[0] == '@') {
    traitName = traitName.substr(1);
  }

  int balance = 0;
  for (size_t i = 0; i < traitName.size(); ++i) {
    char c = traitName[i];
    if (c == '<' && balance == 0) {
      return traitName.substr(0, i);
    }
    if (c == '<' || c == '(' || c == '[') {
      balance++;
    } else if (c == '>' || c == ')' || c == ']') {
      if (balance > 0)
        balance--;
    }
  }
  return traitName;
}

static bool isCoreRuntimePanicFallback(const Module *module,
                                       const FunctionDecl *func) {
  if (!func || func->Name != "__toka_panic_handler") {
    return false;
  }
  std::string file =
      module && !module->ResolvedPath.empty() ? module->ResolvedPath
                                              : (module ? module->SourcePath : "");
  file = toka::PathUtils::canonicalize(file);
  return file.find("/lib/core/internal/runtime.tk") != std::string::npos;
}

static bool hasCallBackedReceiver(const Expr *expr) {
  while (expr) {
    if (dynamic_cast<const CallExpr *>(expr) ||
        dynamic_cast<const MethodCallExpr *>(expr))
      return true;
    if (auto *member = dynamic_cast<const MemberExpr *>(expr)) {
      expr = member->Object.get();
      continue;
    }
    if (auto *postfix = dynamic_cast<const PostfixExpr *>(expr)) {
      expr = postfix->LHS.get();
      continue;
    }
    if (auto *cast = dynamic_cast<const CastExpr *>(expr)) {
      expr = cast->Expression.get();
      continue;
    }
    if (auto *unsafeExpr = dynamic_cast<const UnsafeExpr *>(expr)) {
      expr = unsafeExpr->Expression.get();
      continue;
    }
    return false;
  }
  return false;
}

static bool hasIndexBackedReceiver(const Expr *expr) {
  while (expr) {
    if (dynamic_cast<const ArrayIndexExpr *>(expr))
      return true;
    if (auto *member = dynamic_cast<const MemberExpr *>(expr)) {
      expr = member->Object.get();
      continue;
    }
    if (auto *postfix = dynamic_cast<const PostfixExpr *>(expr)) {
      expr = postfix->LHS.get();
      continue;
    }
    if (auto *cast = dynamic_cast<const CastExpr *>(expr)) {
      expr = cast->Expression.get();
      continue;
    }
    if (auto *unsafeExpr = dynamic_cast<const UnsafeExpr *>(expr)) {
      expr = unsafeExpr->Expression.get();
      continue;
    }
    return false;
  }
  return false;
}

static bool isDirectCallReceiver(const Expr *expr) {
  while (expr) {
    if (dynamic_cast<const CallExpr *>(expr) ||
        dynamic_cast<const MethodCallExpr *>(expr))
      return true;
    if (auto *postfix = dynamic_cast<const PostfixExpr *>(expr)) {
      expr = postfix->LHS.get();
      continue;
    }
    if (auto *cast = dynamic_cast<const CastExpr *>(expr)) {
      expr = cast->Expression.get();
      continue;
    }
    if (auto *unsafeExpr = dynamic_cast<const UnsafeExpr *>(expr)) {
      expr = unsafeExpr->Expression.get();
      continue;
    }
    return false;
  }
  return false;
}

static bool isOwnedUniqueReceiverRvalue(const Expr *expr) {
  while (expr) {
    if (auto *postfix = dynamic_cast<const PostfixExpr *>(expr)) {
      expr = postfix->LHS.get();
      continue;
    }
    if (auto *cast = dynamic_cast<const CastExpr *>(expr)) {
      expr = cast->Expression.get();
      continue;
    }
    if (auto *unsafeExpr = dynamic_cast<const UnsafeExpr *>(expr)) {
      expr = unsafeExpr->Expression.get();
      continue;
    }
    break;
  }
  return dynamic_cast<const CedeExpr *>(expr) ||
         dynamic_cast<const CallExpr *>(expr) ||
         dynamic_cast<const MethodCallExpr *>(expr) ||
         dynamic_cast<const NewExpr *>(expr) ||
         dynamic_cast<const AllocExpr *>(expr);
}

llvm::Function *CodeGen::genFunction(const FunctionDecl *func,
                                     const std::string &overrideName,
                                     bool declOnly) {
  if (!func->GenericParams.empty())
    return nullptr;

  struct FnGuard {
    const FunctionDecl *&Target;
    const FunctionDecl *Old;
    FnGuard(const FunctionDecl *&t, const FunctionDecl *fn) : Target(t), Old(t) { Target = fn; }
    ~FnGuard() { Target = Old; }
  } fnGuard(m_CurrentFunction, func);

  if (!declOnly && func->Body && !validateStage0SpecializationAuthority(func))
    return nullptr;

  if (handleGrammarAuditEnabled() && func) {
    markHandleGrammarFunctionCodeGenLowered(func->Name);
    if (!func->CodegenName.empty() && func->CodegenName != func->Name) {
      markHandleGrammarFunctionCodeGenLowered(func->CodegenName);
    }
  }

  const bool isAsyncEntrypoint =
      overrideName.empty() && func->Name == "main" &&
      func->Effect == EffectKind::Async;
  std::string funcName = overrideName.empty()
                             ? (func->CodegenName.empty() ? func->Name
                                                           : func->CodegenName)
                             : overrideName;
  if (isAsyncEntrypoint) {
    // A coroutine factory returns its TCB pointer, which is not a native C
    // entry-point ABI. Keep the source coroutine internal and synthesize the
    // ordinary process entry point after its body has been generated.
    funcName = "__toka_async_main";
  } else if (funcName == "main") {
    llvm::Triple triple(toka::Parser::TargetTriple);
    if (triple.isOSWASI() || triple.getArch() == llvm::Triple::wasm32 || triple.getArch() == llvm::Triple::wasm64) {
      funcName = "__main_void";
    }
  }
  if (verboseMode) {
    std::cerr << "[DEBUG] genFunction funcName=" << funcName << " declOnly=" << declOnly << std::endl;
  }

  // [Fix] Context Guard: Save/Restore symbol table to prevent corruption during
  // recursive generation
  struct GenContextGuard {
    CodeGen &CG;
    GenContext Ctx;
    std::string Name;
    GenContextGuard(CodeGen &cg, std::string n)
        : CG(cg), Ctx(cg.saveContext()), Name(n) {}
    ~GenContextGuard() { CG.restoreContext(Ctx); }
  } guard(*this, funcName);

  m_Functions[funcName] = func;
  m_Functions[func->Name] = func;
  m_Symbols.clear();
  m_ExpressionDepth = 0;
  m_FullExpressionTemporaries.clear();

  llvm::Function *f = m_Module->getFunction(funcName);
  if (f) {
    if (verboseMode) {
      std::cerr << "[DEBUG EXISTS] funcName=" << funcName << " existing_f_arg_size=" << f->arg_size() << " declOnly=" << declOnly << std::endl;
    }
  }

  std::shared_ptr<Type> retTypeObj;
  if (func->ResolvedReturnType) {
    retTypeObj = func->ResolvedReturnType;
  } else {
    retTypeObj = lowerTypeSyntax(func->ReturnTypeSyntax, func->ReturnType);
  }
  const ReturnResultKind resultKind = func->ReturnContract.ResultKind;
  const bool usesVoidResultABI =
      resultKind == ReturnResultKind::Unit ||
      resultKind == ReturnResultKind::AbiVoid ||
      resultKind == ReturnResultKind::Never;
  bool isSRet = !usesVoidResultABI && shouldReturnSRet(retTypeObj) &&
                func->Effect != EffectKind::Async;

  if (!f) {
    std::vector<llvm::Type *> argTypes;
    for (const auto &arg : func->Args) {
      std::shared_ptr<Type> typeObj;
      if (arg.ResolvedType) {
        typeObj = arg.ResolvedType;
      } else {
        // CodeGen normally receives Sema-populated types.  Retain a direct
        // TypeSyntax fallback for synthetic declarations that bypass Sema;
        // do not reparse source spelling here.
        typeObj = lowerTypeSyntax(arg.TypeSyntax, arg.Type);
      }

      // Determine LLVM Type
      llvm::Type *t = getLLVMType(typeObj);
      if (!t) {
        error(func, DiagID::ERR_CODEGEN_UNRESOLVED_ARGUMENT_TYPE_FOR_IN_FUNCTI, arg.Name, funcName);
        return nullptr;
      }

      // [Restored Logic] Implicit Capture (ABI)
      // Structs, Arrays, and Mutable bindings are passed by pointer (Implicit
      // Reference) unless they are already explicit pointers/references.
      // SharedPtr and UniquePtr are already pointers (or struct wrappers acting
      // as pointers), so checks below mostly separate them.

      bool isAggregate = t->isStructTy() || t->isArrayTy();
      // Note: SharedPtr is a struct {T*, i32*}, but we handle Shared explicitly
      // in AST logic usually. However, old logic checked `!arg.IsReference`.
      // New logic: `typeObj` already wraps Reference/Pointer if AST had them.
      // So if `typeObj` is ALREADY a Pointer/Reference/Unique/Shared, `t` is a
      // pointer (or {ptr,ptr}). We only want to capture if it is a DIRECT value
      // (Primitive, Shape, Array, enum payload) that needs to be passed by ptr.

      bool isDirectValue = !typeObj->isPointer() && !typeObj->isReference();
      // isPointer() covers Raw, Unique, Shared, Reference in Type.h?
      // Checking Type.h: isPointer() covers Raw, Unique, Shared, Reference.

      // [Fix] Enable Capture for Unique Pointers
      bool needsCapture =
          (isDirectValue && (isAggregate || arg.IsValueMutable)) ||
          arg.IsRebindable || (arg.IsUnique && !arg.IsCeded) || arg.IsShared;

      // An init formal is an explicit out-place ABI: it aliases caller-owned
      // storage even for scalars, and never transfers cleanup ownership.
      if (arg.IsInit)
        needsCapture = true;

      // [NEW] Lifetime Union: Force capture if param is a dependency
      for (const auto &dep : func->LifeDependencies) {
        if (dep == arg.Name && isDirectValue) {
          needsCapture = true;
          break;
        }
      }

      // [ABI Fix] Shared Pointers must be passed by Single Pointer (Reference
      // to Handle) to avoid ABI dissecting the struct {ptr, ptr} across
      // registers.
      if (typeObj->isSharedPtr() || arg.IsShared) {
        needsCapture = true;
      }

      if (needsCapture) {
        t = llvm::PointerType::getUnqual(m_Context);
      }

      if (t)
        argTypes.push_back(t);
    }

    llvm::Type *retType = usesVoidResultABI
                               ? llvm::Type::getVoidTy(m_Context)
                               : getLLVMType(retTypeObj);
    if (!retType) {
      error(func, DiagID::ERR_CODEGEN_UNRESOLVED_RETURN_TYPE_FOR_FUNCTION, funcName);
      return nullptr;
    }

    if (func->Effect == EffectKind::Async) {
      retType = llvm::PointerType::getUnqual(m_Context);
    }

    llvm::Type *actualRetTy = isSRet ? llvm::Type::getVoidTy(m_Context) : retType;
    if (isSRet) {
      argTypes.insert(argTypes.begin(), llvm::PointerType::getUnqual(m_Context));
    }

    llvm::FunctionType *ft = llvm::FunctionType::get(actualRetTy, argTypes, false);
    f = llvm::Function::Create(ft, llvm::Function::ExternalLinkage, funcName,
                               m_Module.get());
    if (isSRet) {
      f->addParamAttr(0, llvm::Attribute::get(m_Context, llvm::Attribute::StructRet, getLLVMType(retTypeObj)));
    }
    if (func->Effect == EffectKind::Async) {
      f->setPresplitCoroutine();
    }
  }

  bool isGenericSpec = (funcName.find("_M") != std::string::npos && func->Body != nullptr && !declOnly);
  bool isImportedSource = (!m_AST->IsRootModule && !declOnly && func->Body != nullptr);
  if (isGenericSpec || isImportedSource) {
    f->setLinkage(llvm::Function::LinkOnceODRLinkage);
    llvm::Triple triple(m_Module->getTargetTriple());
    if (triple.supportsCOMDAT()) {
      f->setComdat(m_Module->getOrInsertComdat(f->getName()));
    }
  }
  if (!declOnly && isCoreRuntimePanicFallback(m_AST, func)) {
    f->setLinkage(llvm::Function::WeakAnyLinkage);
  }

  // [Fix] Prevent double generation of function bodies (e.g. from multiple
  // imports)
  if (!f->empty()) {
    return f;
  }

  if (declOnly) {
    return f;
  }

  if (!func->Body) {
    return f;
  }


  llvm::BasicBlock *bb = llvm::BasicBlock::Create(m_Context, "entry", f);
  m_Builder.SetInsertPoint(bb);
  beginDebugFunction(func, f, funcName);
  m_ScopeStack.push_back({});

  if (func->Effect == EffectKind::Async) {
      std::shared_ptr<Type> retTypeObj;
      if (func->ResolvedReturnType) {
        retTypeObj = func->ResolvedReturnType;
      } else {
        retTypeObj = lowerTypeSyntax(func->ReturnTypeSyntax, func->ReturnType);
      }
      llvm::Type *actualRetTy = getLLVMType(retTypeObj);
      m_CurrentCoroRetTy = actualRetTy;
      
      llvm::Type *promiseType;
      if (actualRetTy->isVoidTy()) {
          promiseType = llvm::StructType::get(m_Context, {m_Builder.getInt8Ty(), m_Builder.getPtrTy(), m_Builder.getPtrTy()});
      } else {
          promiseType = llvm::StructType::get(m_Context, {m_Builder.getInt8Ty(), m_Builder.getPtrTy(), m_Builder.getPtrTy(), actualRetTy});
      }
      m_CurrentCoroPromiseType = promiseType;
      m_CurrentCoroPromise = createEntryBlockAlloca(promiseType, nullptr, "coro.promise");

      // A task result can be consumed by await/wait or, after ownership moves
      // into a TaskScope, disposed by the runtime.  Generate the latter as a
      // private typed thunk instead of asking the runtime to guess a payload
      // layout from the promise bytes.
      llvm::Function *resultDropFn = nullptr;
      if (!actualRetTy->isVoidTy()) {
          const std::string resultDropName =
              "__toka_task_result_drop_" + f->getName().str();
          resultDropFn = m_Module->getFunction(resultDropName);
          if (!resultDropFn) {
              auto *dropTy = llvm::FunctionType::get(
                  m_Builder.getVoidTy(), {m_Builder.getPtrTy()}, false);
              resultDropFn = llvm::Function::Create(
                  dropTy, llvm::Function::PrivateLinkage, resultDropName,
                  m_Module.get());
              auto savedIP = m_Builder.saveIP();
              auto *dropEntry = llvm::BasicBlock::Create(
                  m_Context, "entry", resultDropFn);
              m_Builder.SetInsertPoint(dropEntry);
              emitDropForType(resultDropFn->getArg(0), retTypeObj);
              if (!m_Builder.GetInsertBlock()->getTerminator())
                  m_Builder.CreateRetVoid();
              m_Builder.restoreIP(savedIP);
          }
      }
      
      llvm::Function *idFn = llvm::Intrinsic::getOrInsertDeclaration(m_Module.get(), llvm::Intrinsic::coro_id);
      llvm::Value *zero = llvm::ConstantInt::get(llvm::Type::getInt32Ty(m_Context), 0);
      llvm::Value *nullPtr = llvm::ConstantPointerNull::get(llvm::PointerType::getUnqual(m_Context));
      m_CurrentCoroId = m_Builder.CreateCall(idFn, {zero, m_CurrentCoroPromise, nullPtr, nullPtr});
      
      llvm::Function *sizeFn = llvm::Intrinsic::getOrInsertDeclaration(m_Module.get(), llvm::Intrinsic::coro_size, {getIntPtrTy()});
      llvm::Value *size = m_Builder.CreateCall(sizeFn);
      
      llvm::Function *mallocFn = m_Module->getFunction("malloc");
      if (!mallocFn) {
          std::vector<llvm::Type*> args = {getIntPtrTy()};
          llvm::FunctionType *ft = llvm::FunctionType::get(llvm::PointerType::getUnqual(m_Context), args, false);
          mallocFn = llvm::Function::Create(ft, llvm::Function::ExternalLinkage, "malloc", m_Module.get());
      }
      llvm::Value *alloc = m_Builder.CreateCall(mallocFn, {size});
      
      llvm::Function *beginFn = llvm::Intrinsic::getOrInsertDeclaration(m_Module.get(), llvm::Intrinsic::coro_begin);
      m_CurrentCoroHandle = m_Builder.CreateCall(beginFn, {m_CurrentCoroId, alloc});
      
      // Compiler-generated tasks pass the private typed result drop hook and
      // opt into the cold-finalizer handshake. Older objects retain the
      // three-argument ABI for compatibility.
      llvm::Function *taskCreateFn =
          m_Module->getFunction(
              "toka_task_create_with_result_drop_and_cold_cleanup");
      if (!taskCreateFn) {
          llvm::FunctionType *ft = llvm::FunctionType::get(
              m_Builder.getPtrTy(),
              {m_Builder.getPtrTy(), m_Builder.getPtrTy(), m_Builder.getPtrTy()},
              false);
          taskCreateFn = llvm::Function::Create(
              ft, llvm::Function::ExternalLinkage,
              "toka_task_create_with_result_drop_and_cold_cleanup",
              m_Module.get());
      }
      llvm::Value *dropHook = resultDropFn
          ? static_cast<llvm::Value*>(resultDropFn)
          : llvm::ConstantPointerNull::get(m_Builder.getPtrTy());
      m_CurrentCoroTCB = m_Builder.CreateCall(
          taskCreateFn, {m_CurrentCoroHandle, m_CurrentCoroPromise, dropHook});

      m_CurrentCoroSuspendRetBB = llvm::BasicBlock::Create(m_Context, "coro.suspend.ret");
      m_CurrentCoroCleanupBB = llvm::BasicBlock::Create(m_Context, "coro.cleanup.ret");
      
      // Promise header initialization is owned by toka_task_create. Keep the
      // private header layout out of CodeGen so cross-module ABI changes only
      // require updating the runtime boundary.
  } else {
      m_CurrentCoroHandle = nullptr;
      m_CurrentCoroPromise = nullptr;
      m_CurrentCoroTCB = nullptr;
      m_CurrentCoroId = nullptr;
      m_CurrentCoroPromiseType = nullptr;
      m_CurrentCoroSuspendRetBB = nullptr;
      m_CurrentCoroCleanupBB = nullptr;
      m_CurrentCoroFinalSuspendBB = nullptr;
  }

  auto argIt = f->arg_begin();
  m_CurrentSRetPtr = nullptr;
  m_CurrentSRetTy = nullptr;
  if (isSRet) {
    llvm::Value *sretPtr = &(*argIt);
    sretPtr->setName("sret.slot");
    m_CurrentSRetPtr = sretPtr;
    m_CurrentSRetTy = getLLVMType(retTypeObj);
    argIt++;
  }

  size_t idx = 0;
  for (; argIt != f->arg_end(); ++argIt) {
    if (idx >= func->Args.size()) {
      break;
    }
    llvm::Argument &arg = *argIt;
    const auto &argDecl = func->Args[idx];
    std::string argName = argDecl.Name;
    
    // std::cout << "DEBUG: Registering Arg " << idx << ": " << argName << " in function " << funcName << std::endl;

    // 1. Strip morphology to get the base symbol name
    argName = Type::stripMorphology(argName);

    arg.setName(argName);

    // 2. Resolve Type Object
    std::shared_ptr<Type> typeObj;
    const bool isMutable = argDecl.IsValueMutable;

    if (argDecl.ResolvedType) {
      typeObj = argDecl.ResolvedType;
    } else {
      typeObj = lowerTypeSyntax(argDecl.TypeSyntax, argDecl.Type);
    }

    // 3. Get LLVM Type from Object
    llvm::Type *allocaType = getLLVMType(typeObj);
    if (!allocaType) {
      std::cerr
          << "CodeGen Error: Failed to resolve LLVM type for argument body '"
          << argDecl.Name << "' in function '" << funcName
          << "'. typeObj: " << (typeObj ? typeObj->toString() : "null") << "\n";
      return nullptr;
    }
    llvm::Type *pTy = allocaType; // Soul type approx (refines later)

    // [Restored Logic] Implicit Capture (ABI) - Body
    bool isAggregate = allocaType->isStructTy() || allocaType->isArrayTy();
    bool isDirectValue = !typeObj->isPointer() && !typeObj->isReference();
    bool needsCapture =
        (isDirectValue && (isAggregate || argDecl.IsValueMutable)) ||
        argDecl.IsRebindable ||
        (argDecl.IsUnique && !argDecl.IsCeded) || argDecl.IsShared;

    const bool isMorphicParameter =
        argDecl.IsMorphicExempt ||
        (!argDecl.Name.empty() && argDecl.Name[0] == '\'');
    bool returnsCapturedHandleIdentity = false;
    if (isMorphicParameter && func->ResolvedReturnType &&
        func->ResolvedReturnType->isReference()) {
      auto returnedPointee = func->ResolvedReturnType->getPointeeType();
      const bool isLevel2Return =
          returnedPointee &&
          (returnedPointee->isPointer() || returnedPointee->isReference() ||
           returnedPointee->isSmartPointer());
      if (isLevel2Return) {
        const std::string cleanArg = Type::stripMorphology(argDecl.Name);
        for (const auto &dep : func->LifeDependencies) {
          if (Type::stripMorphology(dep) == cleanArg) {
            returnsCapturedHandleIdentity = true;
            break;
          }
        }
      }
    }
    if (returnsCapturedHandleIdentity) {
      needsCapture = true;
    }

    if (argDecl.IsInit)
      needsCapture = true;

    // [NEW] Lifetime Union: Force capture if param is a dependency
    for (const auto &dep : func->LifeDependencies) {
      if (dep == argDecl.Name && isDirectValue) {
        needsCapture = true;
        break;
      }
    }

    if (needsCapture) {
      // Argument passed by pointer
      allocaType = llvm::PointerType::getUnqual(m_Context);
    }

    llvm::Value *finalStorage = nullptr;
    // Async ceded aggregates use a pointer slot for expression lowering, but
    // the owned value lives in a separate frame alloca.  Scope cleanup must
    // target that value, not the pointer slot.
    llvm::Value *ownedValueStorage = nullptr;
    bool isOwnedParam = false;
    // A ceded unique argument arrives as the moved heap handle value.  It has
    // one fewer physical indirection than an ordinary in-place capture.
    bool storesMovedUniqueHandleDirectly = false;

    bool isDirectType = typeObj && !typeObj->isPointer() && !typeObj->isReference() && !argDecl.IsShared && !argDecl.IsUnique && !typeObj->isUniquePtr() && !typeObj->isSharedPtr();
    bool isScalarValue = isDirectType && !needsCapture && !pTy->isStructTy() && !pTy->isArrayTy();

    if (argDecl.IsInit) {
      // The incoming pointer is the caller's place itself.  Do not create an
      // intermediate slot: `init param = value` must write that place.
      finalStorage = &arg;
      isOwnedParam = false;
    } else if (func->Effect == EffectKind::Async) {
      if (argDecl.IsShared || (typeObj && typeObj->isSharedPtr())) {
        llvm::Type *sharedStructTy = getLLVMType(typeObj);
        llvm::AllocaInst *alloca = createEntryBlockAlloca(sharedStructTy, nullptr, argName + ".addr");
        if (arg.getType()->isPointerTy()) {
          llvm::Value *loadedStruct = m_Builder.CreateLoad(sharedStructTy, &arg);
          m_Builder.CreateStore(loadedStruct, alloca);
        } else {
          m_Builder.CreateStore(&arg, alloca);
        }
        finalStorage = alloca;
        isOwnedParam = argDecl.IsCeded;
      } else if ((argDecl.IsUnique || (typeObj && typeObj->isUniquePtr())) && argDecl.IsCeded) {
        // Single Handle Move for cede ^T (No double indirection!)
        llvm::AllocaInst *alloca = createEntryBlockAlloca(pTy, nullptr, argName + ".addr");
        // The coroutine entry ABI matches synchronous cede ^T: the LLVM
        // argument already is the owned heap handle, never a caller slot.
        m_Builder.CreateStore(&arg, alloca);
        finalStorage = alloca;
        isOwnedParam = true;
        storesMovedUniqueHandleDirectly = true;
      } else if (isScalarValue) {
        // Direct Uncaptured Scalar Value Parameter (i32, bool, etc.) -> Single valAlloca!
        llvm::AllocaInst *valAlloca = createEntryBlockAlloca(pTy, nullptr, argName + ".coro_val");
        if (arg.getType()->isPointerTy()) {
          llvm::Value *loadedVal = m_Builder.CreateLoad(pTy, &arg);
          m_Builder.CreateStore(loadedVal, valAlloca);
        } else {
          m_Builder.CreateStore(&arg, valAlloca);
        }
        finalStorage = valAlloca;
        isOwnedParam = argDecl.IsCeded;
      } else if (argDecl.IsCeded) {
        // Frame Inline Aggregate Value Move (ONLY for cede struct / cede aggregate)
        llvm::Type *valTy = pTy;
        llvm::AllocaInst *valAlloca = createEntryBlockAlloca(valTy, nullptr, argName + ".coro_val");
        if (arg.getType()->isPointerTy()) {
          llvm::Value *loadedVal = m_Builder.CreateLoad(valTy, &arg);
          m_Builder.CreateStore(loadedVal, valAlloca);
        } else {
          m_Builder.CreateStore(&arg, valAlloca);
        }
        llvm::AllocaInst *ptrAlloca = createEntryBlockAlloca(llvm::PointerType::getUnqual(m_Context), nullptr, argName + ".addr");
        m_Builder.CreateStore(valAlloca, ptrAlloca);
        finalStorage = ptrAlloca;
        ownedValueStorage = valAlloca;
        isOwnedParam = true;
      } else {
        // Borrowed Pointer Slot (self#, &ref, value#: i32, non-ceded aggregate borrow)
        llvm::AllocaInst *frameSlot = createEntryBlockAlloca(llvm::PointerType::getUnqual(m_Context), nullptr, argName + ".addr");
        m_Builder.CreateStore(&arg, frameSlot);
        finalStorage = frameSlot;
        isOwnedParam = false;
      }
    } else if ((argDecl.IsUnique ||
                (typeObj && typeObj->isUniquePtr())) &&
               argDecl.IsCeded) {
      // Keep synchronous `cede ^T` ABI identical to the coroutine path: the
      // callee owns the heap handle, not the caller's temporary handle slot.
      llvm::AllocaInst *alloca =
          createEntryBlockAlloca(pTy, nullptr, argName + ".addr");
      // The LLVM parameter already is the moved heap handle value.  `&arg`
      // is an llvm::Argument object, not an address passed by the caller;
      // loading through it interprets the pointee payload bytes as a second
      // pointer and corrupts every generic `cede ^T` parameter.
      m_Builder.CreateStore(&arg, alloca);
      finalStorage = alloca;
      isOwnedParam = true;
      storesMovedUniqueHandleDirectly = true;
    } else if (argDecl.IsShared) {
      finalStorage = &arg;
      isOwnedParam = false;
    } else {
      llvm::AllocaInst *alloca =
          createEntryBlockAlloca(allocaType, nullptr, argName + ".addr");

      // [Legacy] Bare union alignment
      if (argDecl.ResolvedType) {
        auto soul = argDecl.ResolvedType;
        while (soul && (soul->isPointer() || soul->isReference() ||
                        soul->isSmartPointer())) {
          soul = soul->getPointeeType();
        }
        if (soul && soul->isShape()) {
          auto st = std::dynamic_pointer_cast<ShapeType>(soul);
          if (st->Decl && st->Decl->Kind == ShapeKind::Union) {
            alloca->setAlignment(llvm::Align(st->Decl->MaxAlign));
          }
        }
      }

      m_Builder.CreateStore(&arg, alloca);
      finalStorage = alloca;
    }

    llvm::Type *debugStorageType = allocaType;
    if (auto *alloca = llvm::dyn_cast<llvm::AllocaInst>(finalStorage))
      debugStorageType = alloca->getAllocatedType();
    emitDebugVariable(argName, finalStorage, debugStorageType, argDecl.Loc,
                      true, static_cast<unsigned>(idx + 1));

    // 4. Register in Symbol Table using Type Object
    TokaSymbol sym;
    sym.allocaPtr = finalStorage;

    // Refactored Metadata Filler
    fillSymbolMetadata(
        sym, typeObj,
        pTy); // Pass pTy (base element type) not the captured pointer type
    sym.typeName =
        argDecl.Type; // [Fix] Set legacy type string for Dynamic Dispatch

    // [HOTFIX] Exempt variables (like 'val) preserve their raw morphology!
    if (!argName.empty() && argName[0] == '\'' && !argDecl.IsReference) {
        sym.mode = AddressingMode::Direct;
        sym.indirectionLevel = 0;
        sym.morphology = Morphology::None;
        sym.soulType = getLLVMType(typeObj);
    }

    if (needsCapture && !storesMovedUniqueHandleDirectly &&
        !argDecl.IsInit) {
      sym.mode = AddressingMode::Pointer;
      // If captured, we add a level of indirection (ptr -> ptr*)
      sym.indirectionLevel++;
    }

    // Explicit permission/flag overrides from AST if not in Type String
    sym.isRebindable = argDecl.IsRebindable;
    sym.isCallerHandleSlot =
        needsCapture && !storesMovedUniqueHandleDirectly &&
        argDecl.IsRebindable && !argDecl.IsShared;
    sym.capturedHandleSlotNeedsLoad =
        needsCapture && !storesMovedUniqueHandleDirectly &&
        llvm::isa<llvm::AllocaInst>(finalStorage) &&
        (argDecl.IsUnique ||
         (typeObj && typeObj->isUniquePtr()) ||
         returnsCapturedHandleIdentity);
    sym.isMutable = isMutable;

    m_Symbols[argName] = sym;

    m_NamedValues[argName] = reinterpret_cast<llvm::AllocaInst *>(
        finalStorage); // Warning: cast mostly for legacy support

    if (!m_ScopeStack.empty()) {
      // [Fix] Argument Lifecycle
      // Arguments passed by In-Place Capture (Unique Pointers) are effectively
      // borrowed. The Callee must NOT free them. Ownership remains with Caller.
      // So we register them as IsUnique=false for cleanup purposes.
      // Shared Pointers passed by pointer are also technically borrowed (no new
      // ref). But we might want normal semantics? If we don't inc-ref, we
      // shouldn't dec-ref. For now, follow Unique pattern to be safe.
      llvm::Type *argAllocTy = finalStorage->getType();
      if (auto *AI = llvm::dyn_cast<llvm::AllocaInst>(finalStorage))
        argAllocTy = AI->getAllocatedType();

      bool argHasDrop = false;
      std::string argDropFunc = "";
      std::string soulName = "";
      if (argDecl.ResolvedType) {
        auto soul = argDecl.ResolvedType->getSoulType();
        soulName = soul->getSoulName();
        // Check authoritative metadata from Sema
        const ShapeDecl *SD = nullptr;
        if (auto exactShape = std::dynamic_pointer_cast<ShapeType>(soul))
          SD = exactShape->Decl;
        if (!SD && m_Shapes.count(soulName))
          SD = m_Shapes[soulName];
        if (SD) {
          if (!SD->MangledDestructorName.empty()) {
            argHasDrop = true;
            argDropFunc = SD->MangledDestructorName;
          }
        }
        if (auto outcome =
                std::dynamic_pointer_cast<MissOutcomeType>(
                    argDecl.ResolvedType)) {
          auto payload = outcome->PayloadType;
          auto payloadSoul = payload ? payload->getSoulType() : nullptr;
          auto payloadShape =
              std::dynamic_pointer_cast<ShapeType>(payloadSoul);
          if ((payload &&
               (payload->isUniquePtr() || payload->isSharedPtr())) ||
              (payloadShape &&
               (payloadShape->Decl ||
                m_Shapes.count(payloadShape->getSoulName())))) {
            argHasDrop = true;
            argDropFunc.clear();
          }
        }
      }

      VariableScopeInfo info;
      info.Name = argName;
      info.Alloca = ownedValueStorage ? ownedValueStorage : finalStorage;
      info.AllocType = ownedValueStorage ? pTy : argAllocTy;
      info.IsUniquePointer = isOwnedParam && (argDecl.IsUnique || (typeObj && typeObj->isUniquePtr()));
      info.IsShared = isOwnedParam && (argDecl.IsShared || (typeObj && typeObj->isSharedPtr()));
      info.HasDrop = isOwnedParam && argHasDrop;
      info.DropFunc = isOwnedParam ? argDropFunc : "";
      info.SoulName = soulName;
      if (isOwnedParam)
        info.DropType = typeObj;
      if (finalStorage && isOwnedParam && (argHasDrop || argDecl.IsUnique || argDecl.IsShared || (typeObj && (typeObj->isUniquePtr() || typeObj->isSharedPtr())))) {
        info.DropFlag = createEntryBlockAlloca(
            llvm::Type::getInt1Ty(m_Context), nullptr, argName + ".drop.live");
        m_Builder.CreateStore(llvm::ConstantInt::getTrue(m_Context),
                              info.DropFlag);
      }
      m_ScopeStack.back().push_back(info);
    }

    idx++;
  }

  if (func->IsClosureInvoke &&
      func->ClosureReceiver == CallableReceiverMode::Consuming &&
      !func->Args.empty() && func->Args[0].ResolvedType &&
      func->Args[0].ResolvedType->isShape() && !m_ScopeStack.empty()) {
    auto closureType =
        std::static_pointer_cast<ShapeType>(func->Args[0].ResolvedType);
    if (closureType->Decl) {
      for (const auto &member : closureType->Decl->Members) {
        if (!member.ResolvedType || member.ResolvedType->isReference())
          continue;
        llvm::Value *fieldAddr = getEntityAddr(member.Name);
        if (!fieldAddr)
          continue;
        VariableScopeInfo capture;
        capture.Name = member.Name;
        capture.Alloca = fieldAddr;
        capture.AllocType = getLLVMType(member.ResolvedType);
        capture.HasDrop = true;
        capture.SoulName = member.ResolvedType->getSoulType()->getSoulName();
        capture.DropFlag = createEntryBlockAlloca(
            llvm::Type::getInt1Ty(m_Context), nullptr,
            member.Name + ".callable.drop.live");
        m_Builder.CreateStore(llvm::ConstantInt::getTrue(m_Context),
                              capture.DropFlag);
        m_ScopeStack.back().push_back(std::move(capture));
      }
    }
  }
  if (m_CurrentCoroHandle) {
      // Initial Suspend Point (Created -> Queued -> Running)
      // Placed AFTER argument binding so all incoming parameters & cede values
      // are safely moved/captured inside coroutine-owned frame allocas.
      llvm::Function *saveFn = llvm::Intrinsic::getOrInsertDeclaration(m_Module.get(), llvm::Intrinsic::coro_save);
      llvm::Function *suspFn = llvm::Intrinsic::getOrInsertDeclaration(m_Module.get(), llvm::Intrinsic::coro_suspend);
      llvm::Value *initSaveToken = m_Builder.CreateCall(saveFn, {m_CurrentCoroHandle});
      llvm::Value *initSuspendRes = m_Builder.CreateCall(suspFn, {initSaveToken, m_Builder.getInt1(false)});

      llvm::BasicBlock *initBodyBB = llvm::BasicBlock::Create(m_Context, "coro.init.body", f);
      llvm::BasicBlock *initDestroyBB = llvm::BasicBlock::Create(m_Context, "coro.init.destroy", f);

      llvm::SwitchInst *initSw = m_Builder.CreateSwitch(initSuspendRes, m_CurrentCoroSuspendRetBB, 2);
      initSw->addCase(m_Builder.getInt8(0), initBodyBB);
      initSw->addCase(m_Builder.getInt8(1), initDestroyBB);

      m_Builder.SetInsertPoint(initDestroyBB);
      executeScopeUnwinding(0);
      m_Builder.CreateBr(m_CurrentCoroCleanupBB);

      m_Builder.SetInsertPoint(initBodyBB);
  }

  genStmt(func->Body.get());

  // Ensure Implicit Cleanup
  if (!m_Builder.GetInsertBlock()->getTerminator()) {
    executeScopeUnwinding(0);

    if (func->Effect == EffectKind::Async) {
      genCoroutineReturn(nullptr);
    } else if (resultKind == ReturnResultKind::Unit ||
               resultKind == ReturnResultKind::AbiVoid ||
               func->Name == "main") {
      if (func->Name == "main" && !f->getReturnType()->isVoidTy()) {
        m_Builder.CreateRet(
            llvm::ConstantInt::get(llvm::Type::getInt32Ty(m_Context), 0));
      } else {
        m_Builder.CreateRetVoid();
      }
    } else {
      m_Builder.CreateUnreachable();
    }
  }
  llvm::BasicBlock *coroEndSharedBB = nullptr;
  if (func->Effect == EffectKind::Async) {
      coroEndSharedBB = llvm::BasicBlock::Create(m_Context, "coro.end.shared", f);
      llvm::IRBuilder<> tmpB(coroEndSharedBB);
      llvm::Function *endFn = llvm::Intrinsic::getOrInsertDeclaration(m_Module.get(), llvm::Intrinsic::coro_end);
      tmpB.CreateCall(endFn, {m_CurrentCoroHandle, tmpB.getInt1(false), llvm::ConstantTokenNone::get(m_Context)});
      tmpB.CreateRet(m_CurrentCoroTCB);
  }

  if (func->Effect == EffectKind::Async && m_CurrentCoroSuspendRetBB) {
      f->insert(f->end(), m_CurrentCoroSuspendRetBB);
      llvm::IRBuilder<> tmpB(m_CurrentCoroSuspendRetBB);
      tmpB.CreateBr(coroEndSharedBB);
  }
  if (func->Effect == EffectKind::Async && m_CurrentCoroCleanupBB) {
      f->insert(f->end(), m_CurrentCoroCleanupBB);
      llvm::IRBuilder<> tmpB(m_CurrentCoroCleanupBB);

      llvm::Function *deferColdFreeFn =
          m_Module->getFunction("toka_task_should_defer_cold_frame_free");
      if (!deferColdFreeFn) {
        llvm::FunctionType *ft = llvm::FunctionType::get(
            tmpB.getInt32Ty(), {tmpB.getPtrTy()}, false);
        deferColdFreeFn = llvm::Function::Create(
            ft, llvm::Function::ExternalLinkage,
            "toka_task_should_defer_cold_frame_free", m_Module.get());
      }
      llvm::Value *deferColdFree = tmpB.CreateCall(
          deferColdFreeFn, {m_CurrentCoroPromise}, "coro.cold.defer_free");
      llvm::Value *shouldDeferColdFree = tmpB.CreateICmpNE(
          deferColdFree, tmpB.getInt32(0));
      llvm::BasicBlock *freeCleanupBB = llvm::BasicBlock::Create(
          m_Context, "coro.cleanup.free", f);
      llvm::BasicBlock *finishCleanupBB = llvm::BasicBlock::Create(
          m_Context, "coro.cleanup.finish", f);
      tmpB.CreateCondBr(shouldDeferColdFree, finishCleanupBB, freeCleanupBB);

      tmpB.SetInsertPoint(freeCleanupBB);
      llvm::Function *freeIdFn = llvm::Intrinsic::getOrInsertDeclaration(m_Module.get(), llvm::Intrinsic::coro_free);
      llvm::Value *memToFree = tmpB.CreateCall(freeIdFn, {m_CurrentCoroId, m_CurrentCoroHandle});
      llvm::Function *freeFn = m_Module->getFunction("free");
      if (!freeFn) {
        std::vector<llvm::Type*> freeArgs = {tmpB.getPtrTy()};
        llvm::FunctionType *freeFt = llvm::FunctionType::get(tmpB.getVoidTy(), freeArgs, false);
        freeFn = llvm::Function::Create(freeFt, llvm::Function::ExternalLinkage, "free", m_Module.get());
      }
      tmpB.CreateCall(freeFn, memToFree);

      tmpB.CreateBr(finishCleanupBB);

      tmpB.SetInsertPoint(finishCleanupBB);
      tmpB.CreateBr(coroEndSharedBB);
  }

  // Ensure all basic blocks have a terminator to satisfy LLVM verifier
  for (llvm::BasicBlock &bb : *f) {
      if (!bb.getTerminator()) {
          llvm::IRBuilder<> tmpB(&bb);
          tmpB.CreateUnreachable();
      }
  }

  if (isAsyncEntrypoint) {
    genAsyncMainEntrypoint(f, func);
  }

  return f;
}

void CodeGen::genAsyncMainEntrypoint(llvm::Function *asyncMain,
                                     const FunctionDecl *func) {
  llvm::Triple triple(toka::Parser::TargetTriple);
  const bool isWasm = triple.isOSWASI() ||
                      triple.getArch() == llvm::Triple::wasm32 ||
                      triple.getArch() == llvm::Triple::wasm64;
  const std::string entryName = isWasm ? "__main_void" : "main";
  if (m_Module->getFunction(entryName))
    return;

  llvm::Type *entryRetTy = isWasm ? m_Builder.getVoidTy()
                                  : m_Builder.getInt32Ty();
  llvm::FunctionType *entryTy =
      llvm::FunctionType::get(entryRetTy, {}, false);
  llvm::Function *entry = llvm::Function::Create(
      entryTy, llvm::Function::ExternalLinkage, entryName, m_Module.get());
  llvm::BasicBlock *entryBB =
      llvm::BasicBlock::Create(m_Context, "entry", entry);
  m_Builder.SetInsertPoint(entryBB);

  llvm::Value *tcb = m_Builder.CreateCall(asyncMain, {}, "async_main.tcb");

  llvm::Function *detachFn = m_Module->getFunction("toka_task_detach");
  if (!detachFn) {
    llvm::FunctionType *ft = llvm::FunctionType::get(
        m_Builder.getVoidTy(), {m_Builder.getPtrTy()}, false);
    detachFn = llvm::Function::Create(ft, llvm::Function::ExternalLinkage,
                                      "toka_task_detach", m_Module.get());
  }

  std::shared_ptr<Type> sourceRet = func->ResolvedReturnType
                                        ? func->ResolvedReturnType
                                        : lowerTypeSyntax(func->ReturnTypeSyntax,
                                                          func->ReturnType);
  const bool returnsVoid = sourceRet && sourceRet->getSoulName() == "void";

  llvm::BasicBlock *runDoneBB =
      llvm::BasicBlock::Create(m_Context, "async_main.run_done", entry);
  llvm::Function *spawnFn = m_Module->getFunction("__toka_spawn_blocking");
  if (spawnFn && !spawnFn->isDeclaration()) {
    m_Builder.CreateCall(spawnFn, {tcb});
    m_Builder.CreateBr(runDoneBB);
  } else {
    // A program with an async `main` but no std/task import still needs to
    // execute a ready root task. Standard suspension points import std/task
    // and therefore take the full executor path above; this fallback is only
    // for immediately-ready roots.
    llvm::Function *startFn = m_Module->getFunction("toka_task_start");
    if (!startFn) {
      llvm::FunctionType *ft = llvm::FunctionType::get(
          m_Builder.getInt32Ty(), {m_Builder.getPtrTy()}, false);
      startFn = llvm::Function::Create(ft, llvm::Function::ExternalLinkage,
                                       "toka_task_start", m_Module.get());
    }
    llvm::Function *isDoneFn = m_Module->getFunction("toka_tcb_is_done");
    if (!isDoneFn) {
      llvm::FunctionType *ft = llvm::FunctionType::get(
          m_Builder.getInt1Ty(), {m_Builder.getPtrTy()}, false);
      isDoneFn = llvm::Function::Create(ft, llvm::Function::ExternalLinkage,
                                         "toka_tcb_is_done", m_Module.get());
    }
    llvm::Function *popFn = m_Module->getFunction("toka_task_pop_ready");
    if (!popFn) {
      llvm::FunctionType *ft = llvm::FunctionType::get(
          m_Builder.getInt32Ty(),
          {m_Builder.getPtrTy(), m_Builder.getPtrTy(), m_Builder.getPtrTy()},
          false);
      popFn = llvm::Function::Create(ft, llvm::Function::ExternalLinkage,
                                      "toka_task_pop_ready", m_Module.get());
    }
    llvm::Function *releaseFn = m_Module->getFunction("toka_task_release");
    if (!releaseFn) {
      llvm::FunctionType *ft = llvm::FunctionType::get(
          m_Builder.getVoidTy(), {m_Builder.getPtrTy()}, false);
      releaseFn = llvm::Function::Create(ft, llvm::Function::ExternalLinkage,
                                          "toka_task_release", m_Module.get());
    }
    llvm::Function *clearCurrentFn =
        m_Module->getFunction("toka_task_clear_current");
    if (!clearCurrentFn) {
      llvm::FunctionType *ft = llvm::FunctionType::get(
          m_Builder.getVoidTy(), {m_Builder.getPtrTy()}, false);
      clearCurrentFn = llvm::Function::Create(
          ft, llvm::Function::ExternalLinkage, "toka_task_clear_current",
          m_Module.get());
    }
    llvm::Function *getFrameForRunFn =
        m_Module->getFunction("toka_task_get_current_coro_frame");
    if (!getFrameForRunFn) {
      llvm::FunctionType *ft = llvm::FunctionType::get(
          m_Builder.getPtrTy(), {}, false);
      getFrameForRunFn = llvm::Function::Create(
          ft, llvm::Function::ExternalLinkage, "toka_task_get_current_coro_frame",
          m_Module.get());
    }

    llvm::AllocaInst *taskId =
        m_Builder.CreateAlloca(m_Builder.getInt64Ty(), nullptr, "async_main.task_id");
    llvm::AllocaInst *taskGen =
        m_Builder.CreateAlloca(m_Builder.getInt64Ty(), nullptr, "async_main.task_gen");
    llvm::AllocaInst *queuedTcb =
        m_Builder.CreateAlloca(m_Builder.getPtrTy(), nullptr, "async_main.queued_tcb");
    m_Builder.CreateCall(startFn, {tcb});

    llvm::BasicBlock *pollBB =
        llvm::BasicBlock::Create(m_Context, "async_main.poll", entry);
    llvm::BasicBlock *popBB =
        llvm::BasicBlock::Create(m_Context, "async_main.pop", entry);
    llvm::BasicBlock *resumeBB =
        llvm::BasicBlock::Create(m_Context, "async_main.resume", entry);
    llvm::BasicBlock *executorFailedBB =
        llvm::BasicBlock::Create(m_Context, "async_main.executor_failed", entry);
    m_Builder.CreateBr(pollBB);

    m_Builder.SetInsertPoint(pollBB);
    llvm::Value *done = m_Builder.CreateCall(isDoneFn, {tcb});
    m_Builder.CreateCondBr(done, runDoneBB, popBB);

    m_Builder.SetInsertPoint(popBB);
    llvm::Value *popped = m_Builder.CreateCall(popFn, {taskId, taskGen, queuedTcb});
    llvm::Value *hasReady =
        m_Builder.CreateICmpNE(popped, m_Builder.getInt32(0));
    m_Builder.CreateCondBr(hasReady, resumeBB, executorFailedBB);

    m_Builder.SetInsertPoint(resumeBB);
    llvm::Value *queued =
        m_Builder.CreateLoad(m_Builder.getPtrTy(), queuedTcb, "async_main.queued");
    llvm::Value *frameForRun = m_Builder.CreateCall(getFrameForRunFn);
    llvm::Function *resumeFn = llvm::Intrinsic::getOrInsertDeclaration(
        m_Module.get(), llvm::Intrinsic::coro_resume);
    m_Builder.CreateCall(resumeFn, {frameForRun});
    m_Builder.CreateCall(clearCurrentFn, {queued});
    m_Builder.CreateCall(releaseFn, {queued});
    m_Builder.CreateBr(pollBB);

    m_Builder.SetInsertPoint(executorFailedBB);
    m_Builder.CreateCall(detachFn, {tcb});
    if (isWasm)
      m_Builder.CreateRetVoid();
    else
      m_Builder.CreateRet(m_Builder.getInt32(1));
  }

  m_Builder.SetInsertPoint(runDoneBB);

  llvm::Function *getPromiseFn = m_Module->getFunction("toka_tcb_get_promise");
  if (!getPromiseFn) {
    llvm::FunctionType *ft = llvm::FunctionType::get(
        m_Builder.getPtrTy(), {m_Builder.getPtrTy()}, false);
    getPromiseFn = llvm::Function::Create(ft, llvm::Function::ExternalLinkage,
                                           "toka_tcb_get_promise", m_Module.get());
  }
  llvm::Value *promise = m_Builder.CreateCall(getPromiseFn, {tcb});

  llvm::Function *getStateFn = m_Module->getFunction("toka_task_get_result_state");
  if (!getStateFn) {
    llvm::FunctionType *ft = llvm::FunctionType::get(
        m_Builder.getInt8Ty(), {m_Builder.getPtrTy()}, false);
    getStateFn = llvm::Function::Create(ft, llvm::Function::ExternalLinkage,
                                         "toka_task_get_result_state", m_Module.get());
  }
  llvm::Value *state = m_Builder.CreateCall(getStateFn, {promise});
  llvm::Value *isReady =
      m_Builder.CreateICmpEQ(state, m_Builder.getInt8(1), "async_main.ready");
  llvm::BasicBlock *readyBB =
      llvm::BasicBlock::Create(m_Context, "async_main.ready", entry);
  llvm::BasicBlock *failedBB =
      llvm::BasicBlock::Create(m_Context, "async_main.failed", entry);
  m_Builder.CreateCondBr(isReady, readyBB, failedBB);

  m_Builder.SetInsertPoint(failedBB);
  m_Builder.CreateCall(detachFn, {tcb});
  if (isWasm)
    m_Builder.CreateRetVoid();
  else
    m_Builder.CreateRet(m_Builder.getInt32(1));

  m_Builder.SetInsertPoint(readyBB);
  llvm::Function *takeResultAccessFn =
      m_Module->getFunction("__toka_task_take_result_access");
  if (!takeResultAccessFn) {
    llvm::FunctionType *ft = llvm::FunctionType::get(
        m_Builder.getInt32Ty(),
        {m_Builder.getPtrTy(), m_Builder.getPtrTy(), m_Builder.getPtrTy()},
        false);
    takeResultAccessFn = llvm::Function::Create(
        ft, llvm::Function::ExternalLinkage, "__toka_task_take_result_access",
        m_Module.get());
  }
  llvm::Function *releaseResultAccessFn =
      m_Module->getFunction("__toka_task_release_result_access");
  if (!releaseResultAccessFn) {
    llvm::FunctionType *ft = llvm::FunctionType::get(
        m_Builder.getVoidTy(), {m_Builder.getPtrTy()}, false);
    releaseResultAccessFn = llvm::Function::Create(
        ft, llvm::Function::ExternalLinkage, "__toka_task_release_result_access",
        m_Module.get());
  }
  llvm::Value *resultValueSlot =
      createEntryBlockAlloca(m_Builder.getPtrTy(), nullptr,
                             "async_main.result.value.slot");
  llvm::Value *resultAccessSlot =
      createEntryBlockAlloca(m_Builder.getPtrTy(), nullptr,
                             "async_main.result.access.slot");
  llvm::Value *resultAccess = m_Builder.CreateCall(
      takeResultAccessFn, {promise, resultValueSlot, resultAccessSlot});
  llvm::Value *resultAccessOK = m_Builder.CreateICmpEQ(
      resultAccess, m_Builder.getInt32(1), "async_main.result.access.ok");
  llvm::BasicBlock *resultAccessBB = llvm::BasicBlock::Create(
      m_Context, "async_main.result.access", entry);
  m_Builder.CreateCondBr(resultAccessOK, resultAccessBB, failedBB);
  m_Builder.SetInsertPoint(resultAccessBB);
  llvm::Value *resultAccessGuard = m_Builder.CreateLoad(
      m_Builder.getPtrTy(), resultAccessSlot, "async_main.result.access.guard");
  if (returnsVoid) {
    m_Builder.CreateCall(releaseResultAccessFn, {resultAccessGuard});
    m_Builder.CreateCall(detachFn, {tcb});
    if (isWasm)
      m_Builder.CreateRetVoid();
    else
      m_Builder.CreateRet(m_Builder.getInt32(0));
    return;
  }

  llvm::Value *valuePtr = m_Builder.CreateLoad(
      m_Builder.getPtrTy(), resultValueSlot, "async_main.result.value.ptr");
  llvm::Value *result = m_Builder.CreateLoad(m_Builder.getInt32Ty(), valuePtr,
                                               "async_main.result");
  m_Builder.CreateCall(releaseResultAccessFn, {resultAccessGuard});
  m_Builder.CreateCall(detachFn, {tcb});
  if (isWasm)
    m_Builder.CreateRetVoid();
  else
    m_Builder.CreateRet(result);
}

llvm::Value *CodeGen::genVariableDecl(const VariableDecl *var) {
  std::string varName = Type::stripMorphology(var->Name);
  if (var->Init) {
    const auto *unique = dynamic_cast<const UnaryExpr *>(var->Init.get());
    const bool syntacticTransfer =
        dynamic_cast<const CedeExpr *>(var->Init.get()) != nullptr ||
        (unique && unique->Op == TokenType::Caret);
    if (!validateStage0CodeGenAuthority(var,
                                        Stage0CodeGenAuthorityKind::NonCallItem,
                                        TransferDestination::Initialization,
                                        "initialization", syntacticTransfer))
      return nullptr;
  }
  size_t scopeBeforeInit = 0;

  llvm::Value *initVal = nullptr;
  llvm::Type *decayArrayType = nullptr;
  std::string inferredTypeName = "";
  llvm::Value *closureEnvAddr = nullptr;
  llvm::Type *closureEnvType = nullptr;
  std::string closureEnvTypeName;
  bool startsUninitialized = !var->Init;
  const Expr *sourceInitExpr = var->Init.get();
  if (var->Init) {
    // [Fix] Handle UnsetExpr: Skip generation for explicit 'uninit'.
    Expr *initExpr = var->Init.get();
    if (auto *cast = dynamic_cast<const CastExpr *>(initExpr);
        cast && cast->Kind == CastKind::Ascription)
      initExpr = cast->Expression.get();
    sourceInitExpr = initExpr;
    if (dynamic_cast<const UnsetExpr *>(initExpr)) {
      startsUninitialized = true;
      // Do nothing -> initVal remains nullptr.
      // This prevents 'Store' from being generated later, leaving memory
      // uninitialized (or garbage).
      // Note: type inference logic below handles missing type + initVal=null
      // if TypeName is present.
    } else {
      m_CFStack.push_back({varName, nullptr, nullptr, nullptr});
      scopeBeforeInit = m_ScopeStack.empty() ? 0 : m_ScopeStack.back().size();
      PhysEntity initEnt = genExpr(initExpr);

      // [Fix] Array-to-Pointer Decay Interception
      // Check if RHS is physically an array type that should decay to a
      // pointer
      if (var->IsRawPointer || var->IsReference) {
        if (var->Init) {
          if (auto *ue = dynamic_cast<const UnaryExpr *>(var->Init.get())) {
            if (ue->Op == TokenType::Star) {
              if (auto *ve =
                      dynamic_cast<const VariableExpr *>(ue->RHS.get())) {
                std::string veName = Type::stripMorphology(ve->Name);
                if (m_Symbols.count(veName)) {
                  llvm::Type *t = m_Symbols[veName].soulType;
                  if (t && t->isArrayTy()) {
                    decayArrayType = t;
                  }
                }
              }
            }
          }
        }
      }

      if (decayArrayType) {

        llvm::Value *arrPtr = initEnt.value; // PhysEntity.value gives address
                                             // due to genUnaryExpr
        llvm::Value *zero =
            llvm::ConstantInt::get(llvm::Type::getInt32Ty(m_Context), 0);
        initVal = m_Builder.CreateInBoundsGEP(decayArrayType, arrPtr,
                                              {zero, zero}, "array.decay");
      } else {
        initVal = initEnt.load(m_Builder);
      }

      inferredTypeName = initEnt.typeName;
      m_CFStack.pop_back();
      if (!initVal) {
        return nullptr;
      }
    }
  }

  llvm::Type *type = nullptr;
  llvm::Type *elemTy = nullptr;

  // [New] Annotated AST: Use ResolvedType if available
  // Enabled for all types including Shared Pointers.
  if (var->ResolvedType) {
    type = getLLVMType(var->ResolvedType);

    // Derive elemTy (Soul Type) for metadata and allocation
    std::shared_ptr<Type> inner = var->ResolvedType;
    while (inner->isPointer() || inner->isReference()) {
      if (auto ptr = std::dynamic_pointer_cast<PointerType>(inner)) {
        inner = ptr->PointeeType;
      } else {
        break;
      }
    }
    if (inner->isArray()) {
      elemTy = getLLVMType(inner->getArrayElementType());
    } else {
      elemTy = getLLVMType(inner);
    }
  }

  if (!type) {
    if (!var->TypeName.empty()) {
      type = resolveType(var->TypeName, var->IsRawPointer || var->IsReference ||
                                            var->IsUnique || var->IsShared);
    } else if (initVal) {
      type = initVal->getType();
    }
  }

  std::string soulTypeName = var->TypeName;
  if (!elemTy) {
    if (!soulTypeName.empty()) {
      // Strip ALL morphology to find the core Soul dimension
      while (!soulTypeName.empty() &&
             (soulTypeName[0] == '^' || soulTypeName[0] == '*' ||
              soulTypeName[0] == '&' || soulTypeName[0] == '~')) {
        soulTypeName = soulTypeName.substr(1);
      }
      while (!soulTypeName.empty() &&
             (soulTypeName.back() == '#' || soulTypeName.back() == '?' ||
              soulTypeName.back() == '!')) {
        soulTypeName.pop_back();
      }
      elemTy = resolveType(soulTypeName, false);
    } else if (initVal) {
      // 1. Prefer Inferred Type from PhysEntity (The "Soul" Type)
      if (!inferredTypeName.empty()) {
        std::string tn = inferredTypeName;
        // Strip morphology to find base element type
        while (!tn.empty() &&
               (tn[0] == '*' || tn[0] == '^' || tn[0] == '&' || tn[0] == '#'))
          tn = tn.substr(1);
        elemTy = resolveType(tn, false);
      }

      // 2. Fallbacks using AST inspection (Legacy/Redundant if 1 works, but
      // kept for safety)
      if (!elemTy) {
        if (auto *ve = dynamic_cast<const VariableExpr *>(var->Init.get())) {
          if (m_Symbols.count(ve->Name))
            elemTy = m_Symbols[ve->Name].soulType;
        } else if (auto *ae =
                       dynamic_cast<const AddressOfExpr *>(var->Init.get())) {
          if (auto *vae =
                  dynamic_cast<const VariableExpr *>(ae->Expression.get())) {
            if (m_Symbols.count(vae->Name))
              elemTy = m_Symbols[vae->Name].soulType;
          }
        } else if (auto *allocExpr =
                       dynamic_cast<const AllocExpr *>(var->Init.get())) {
          // auto *p = alloc Point(...) -> elemTy should be Point
          elemTy = resolveType(allocExpr->TypeName, false);
        } else if (auto *newExpr =
                       dynamic_cast<const NewExpr *>(var->Init.get())) {
          elemTy = resolveType(newExpr->Type, false);
        } else if (auto *cast =
                       dynamic_cast<const CastExpr *>(var->Init.get())) {
          std::string tn = cast->TargetType;
          while (!tn.empty() &&
                 (tn[0] == '*' || tn[0] == '^' || tn[0] == '&' || tn[0] == '#'))
            tn = tn.substr(1);
          elemTy = resolveType(tn, false);
        } else if (auto *call =
                       dynamic_cast<const CallExpr *>(var->Init.get())) {
          std::string retTypeName;
          if (m_Functions.count(call->Callee)) {
            retTypeName = m_Functions[call->Callee]->ReturnType;
          } else if (m_Externs.count(call->Callee)) {
            retTypeName = m_Externs[call->Callee]->ReturnType;
          }

          if (!retTypeName.empty()) {
            std::string tn = retTypeName;
            while (!tn.empty() && (tn[0] == '*' || tn[0] == '^' ||
                                   tn[0] == '&' || tn[0] == '#'))
              tn = tn.substr(1);
            elemTy = resolveType(tn, false);
          }
        } else if (auto *ue =
                       dynamic_cast<const UnaryExpr *>(var->Init.get())) {
          // [Fix] Handle *var for type deduction
          if (ue->Op == TokenType::Star) {
            if (auto *ve = dynamic_cast<const VariableExpr *>(ue->RHS.get())) {
              if (m_Symbols.count(ve->Name))
                elemTy = m_Symbols[ve->Name].soulType;
            }
          }
        } else if (initVal->getType()->isPointerTy()) {
          // Fallback: use the value type itself as elem
          elemTy = initVal->getType();
        }
      }
    }

    if (!elemTy) {
      if (initVal)
        elemTy = initVal->getType();
      else
        elemTy = llvm::Type::getInt32Ty(m_Context);
    }
  }

  // Ensure m_ValueElementTypes is set early

  // The Form (Identity) is always what resolveType returns for the full name
  if (!type) { // Only try to resolve if type hasn't been determined yet
    if (!var->TypeName.empty()) {
      type = resolveType(var->TypeName, var->IsRawPointer || var->IsUnique ||
                                            var->IsShared || var->IsReference);
    } else if (elemTy && (var->IsRawPointer || var->IsReference)) {
      // [Fix] Auto-deduction with pointer modifiers/decorators
      // If we have 'auto *p = ...', we deduced elemTy from initVal, but we
      // need to ensure 'type' is a pointer to elemTy.
      // Additionally, if elemTy is an Array, we must decay it to Pointer to
      // Element.
      llvm::Type *innerTy = elemTy;
      if (innerTy->isArrayTy()) {
        innerTy = innerTy->getArrayElementType();
      }
      type = llvm::PointerType::getUnqual(m_Context);
    }
  }
  if (!type && initVal)
    type = initVal->getType();

  // [Fix] Update the element type map with the FINAL resolved type
  // This ensures that pointers decayed from arrays are registered as pointers
  // to the ELEMENT type (e.g. i32), not the ARRAY type ([N]i32).
  if (decayArrayType) {
    elemTy = decayArrayType->getArrayElementType();
  }

  // CRITICAL: For Shared variables, ALWAYS use the handle struct { ptr, ptr
  // }, regardless of what resolveType returned. This ensures all Shared
  // variables have consistent memory layout with ref counting support.
  if (var->IsShared) {
    llvm::Type *ptrTy = llvm::PointerType::getUnqual(m_Context);
    llvm::Type *refTy =
        llvm::PointerType::getUnqual(m_Context);
    type = llvm::StructType::get(m_Context, {ptrTy, refTy});
  } else if (var->IsUnique && (!type || (!type->isPointerTy() && !type->isStructTy()))) {
    // Unique variables must be pointers or fat pointer structs, never raw Soul types
    type = llvm::PointerType::getUnqual(m_Context);
  } else if (!type) {
    // Regular variables use the Soul type directly
    type = elemTy;
  }

  if (!type) {
    error(var, DiagID::ERR_CODEGEN_CANNOT_INFER_TYPE_FOR_VARIABLE, varName);
    return nullptr;
  }

  if (var->Init && initVal) {
    // Move Semantics for Unique
    if (var->IsUnique) {
      const VariableExpr *ve =
          dynamic_cast<const VariableExpr *>(var->Init.get());
      if (!ve) {
        if (auto *ue = dynamic_cast<const UnaryExpr *>(var->Init.get())) {
          if (ue->Op == TokenType::Caret)
            ve = dynamic_cast<const VariableExpr *>(ue->RHS.get());
        }
      }
      if (ve) {
        // Stripping logic for unique variable lookup could be added here if
        // needed, but m_NamedValues should have stripped keys now.
        std::string veName = Type::stripMorphology(ve->Name);
        if (m_Symbols.count(veName) &&
            m_Symbols[veName].morphology == Morphology::Unique) {
          TokaSymbol &sSym = m_Symbols[veName];
          llvm::Value *s = sSym.allocaPtr;
          if (s && llvm::isa<llvm::AllocaInst>(s))
            m_Builder.CreateStore(
                llvm::Constant::getNullValue(
                    llvm::cast<llvm::AllocaInst>(s)->getAllocatedType()),
                s);
        }
      }
    } else if (var->IsShared) {
      // Shared Semantics: Incref or Promote
      if (initVal->getType()->isStructTy() &&
          initVal->getType()->getStructNumElements() == 2 &&
          initVal->getType()->getStructElementType(0)->isPointerTy() &&
          initVal->getType()->getStructElementType(1)->isPointerTy()) {
        // [Refactor] IncRef logic is handled later with proper isCopy check
        // (LValue vs RValue)
      } else {
        // Promote to Shared Handle (Ptr or Value)
        if (initVal->getType()->isPointerTy() &&
            llvm::isa<llvm::ConstantPointerNull>(initVal)) {
          // Null shared pointer initialization
          llvm::Type *ptrTy = llvm::PointerType::getUnqual(m_Context);
          llvm::Type *refTy =
              llvm::PointerType::getUnqual(m_Context);
          llvm::StructType *st =
              llvm::StructType::get(m_Context, {ptrTy, refTy});
          llvm::Value *u = llvm::UndefValue::get(st);
          initVal = m_Builder.CreateInsertValue(
              m_Builder.CreateInsertValue(
                  u,
                  llvm::ConstantPointerNull::get(
                      llvm::cast<llvm::PointerType>(ptrTy)),
                  0),
              llvm::ConstantPointerNull::get(
                  llvm::cast<llvm::PointerType>(refTy)),
              1);
          type = st;
        } else {
          llvm::Function *mallocFn = m_Module->getFunction("malloc");
          if (!mallocFn) {
            std::vector<llvm::Type *> args;
            args.push_back(getIntPtrTy());
            llvm::FunctionType *ft =
                llvm::FunctionType::get(m_Builder.getPtrTy(), args, false);
            mallocFn = llvm::Function::Create(
                ft, llvm::Function::ExternalLinkage, "malloc", m_Module.get());
          }
          if (mallocFn) {
            // 1. Allocate RefCount
            llvm::Value *rcSize =
                llvm::ConstantInt::get(getIntPtrTy(), 4);
            llvm::Value *refPtr = m_Builder.CreateCall(mallocFn, rcSize);
            refPtr = m_Builder.CreateBitCast(
                refPtr, llvm::PointerType::getUnqual(m_Context));
            m_Builder.CreateStore(
                llvm::ConstantInt::get(llvm::Type::getInt32Ty(m_Context), 1),
                refPtr);

            // 2. Prepare Data Pointer
            llvm::Value *dataPtr = nullptr;
            if (initVal->getType()->isPointerTy()) {
              // Already a pointer, use it (assume ownership transfer or raw
              // -> shared promotion)
              dataPtr = m_Builder.CreateBitCast(
                  initVal, llvm::PointerType::getUnqual(m_Context));
            } else {
              // Value Type -> Allocate and Copy
              const llvm::DataLayout &dl = m_Module->getDataLayout();
              uint64_t dataSz = dl.getTypeAllocSize(elemTy);
              llvm::Value *valSize = llvm::ConstantInt::get(
                  getIntPtrTy(), dataSz);
              dataPtr = m_Builder.CreateCall(mallocFn, valSize);
              dataPtr = m_Builder.CreateBitCast(
                  dataPtr, llvm::PointerType::getUnqual(m_Context));
              m_Builder.CreateStore(initVal, dataPtr);
            }

            // 3. Create Handle
            llvm::Type *ptrTy = llvm::PointerType::getUnqual(m_Context);
            llvm::Type *refTy =
                llvm::PointerType::getUnqual(m_Context);
            llvm::StructType *st =
                llvm::StructType::get(m_Context, {ptrTy, refTy});
            llvm::Value *u = llvm::UndefValue::get(st);
            initVal = m_Builder.CreateInsertValue(
                m_Builder.CreateInsertValue(u, dataPtr, 0), refPtr, 1);
            type = st;
          }
        }
      }
    }
  }

  if (type->isVoidTy()) {
    initVal = nullptr;
  }

  llvm::AllocaInst *alloca = nullptr;
  if (!type->isVoidTy()) {
    alloca = createEntryBlockAlloca(type, nullptr, varName);
  }

  if (alloca)
    emitDebugVariable(varName, alloca, type, var->Loc);

  // [Legacy] Bare union alignment
  if (alloca && var->ResolvedType) {
    auto soul = var->ResolvedType;
    while (soul && (soul->isPointer() || soul->isReference() ||
                    soul->isSmartPointer())) {
      soul = soul->getPointeeType();
    }
    if (soul && soul->isShape()) {
      auto st = std::dynamic_pointer_cast<ShapeType>(soul);
      if (st->Decl && st->Decl->Kind == ShapeKind::Union) {
        alloca->setAlignment(llvm::Align(st->Decl->MaxAlign));
      }
    }
  }

  TokaSymbol sym;
  sym.allocaPtr = alloca;
  fillSymbolMetadata(sym, var->TypeName, var->IsRawPointer, var->IsUnique,
                     var->IsShared, var->IsReference, var->IsValueMutable,
                     elemTy);
                     
  if (var->ResolvedType) {
      sym.soulTypeObj = var->ResolvedType;
      sym.soulType = getLLVMType(var->ResolvedType->getSoulType());
      int semanticDepth = 0;
      auto layer = var->ResolvedType;
      while (layer && (layer->isPointer() || layer->isReference() ||
                       layer->isSmartPointer())) {
        ++semanticDepth;
        layer = layer->getPointeeType();
      }
      if (semanticDepth > 0)
        sym.indirectionLevel = semanticDepth;
  }
  
  sym.isRebindable = var->IsRebindable;
  sym.hasDrop = false;
  sym.dropFunc = "";
  sym.isContinuous =
      (elemTy && elemTy->isArrayTy()) ||
      (dynamic_cast<const AllocExpr *>(var->Init.get()) &&
       dynamic_cast<const AllocExpr *>(var->Init.get())->IsArray);

  m_Symbols[varName] = sym;

  m_NamedValues[varName] = alloca;

  // [Refactor] Shared Pointer Init RC Logic
  // Redundant IncRef removed because genExpr (via genVariableExpr) now
  // handle ownership transfer (Acquire) for RValues.

  // Refined implicit casts
  if (initVal && initVal->getType() != type) {
    if (initVal->getType()->isPointerTy() && type->isPointerTy()) {
      initVal = m_Builder.CreateBitCast(initVal, type);
    } else if (initVal->getType()->isPointerTy() && !type->isPointerTy()) {
      initVal = m_Builder.CreateLoad(type, initVal);
    } else if (initVal->getType()->isIntegerTy() && type->isIntegerTy()) {
      initVal = m_Builder.CreateIntCast(initVal, type, true);
    } else if (initVal->getType()->isFloatingPointTy() && type->isFloatingPointTy()) {
      initVal = m_Builder.CreateFPCast(initVal, type);
    }
  }

  // [NEW] Fat pointer synthesis for Closures in VariableDecl
  if (initVal && initVal->getType() != type && type && type->isStructTy() && (type->getStructNumElements() == 2 || type->getStructNumElements() == 3) && type->getStructElementType(0)->isPointerTy() && type->getStructElementType(1)->isPointerTy()) {
      if (sourceInitExpr && sourceInitExpr->ResolvedType && sourceInitExpr->ResolvedType->isShape()) {
         auto shp = std::static_pointer_cast<toka::ShapeType>(sourceInitExpr->ResolvedType);
         if (shp->Name.find("__Closure_") == 0) {
             bool isDynFn = type->getStructNumElements() == 3;
             llvm::Type *envTy = initVal->getType();
             closureEnvType = getLLVMType(sourceInitExpr->ResolvedType);
             closureEnvTypeName = shp->Name;
             llvm::Value *envPtrAddr;
             
             if (isDynFn) {
                 // Heap Allocation for `dyn fn`
                 llvm::Type *objTy = getLLVMType(sourceInitExpr->ResolvedType);
                 
                  llvm::Function *mallocFn = m_Module->getFunction("malloc");
                  if (!mallocFn) {
                      mallocFn = llvm::Function::Create(llvm::FunctionType::get(m_Builder.getPtrTy(), {getIntPtrTy()}, false), llvm::Function::ExternalLinkage, "malloc", m_Module.get());
                  }
                  uint64_t size = m_Module->getDataLayout().getTypeAllocSize(objTy);
                  llvm::Value *heapMem = m_Builder.CreateCall(mallocFn, {llvm::ConstantInt::get(getIntPtrTy(), size)});
                  envPtrAddr = m_Builder.CreatePointerCast(heapMem, llvm::PointerType::getUnqual(m_Context));
                 
                 if (envTy->isPointerTy()) {
                     llvm::Value *loadedEnv = m_Builder.CreateLoad(objTy, initVal);
                     m_Builder.CreateStore(loadedEnv, envPtrAddr);
                 } else {
                     m_Builder.CreateStore(initVal, envPtrAddr);
                 }
             } else {
                 // Stack Allocation for `fn`
                 if (envTy->isPointerTy()) {
                     envPtrAddr = initVal;
                 } else {
                     envPtrAddr = createEntryBlockAlloca(envTy, nullptr, "closure_env_alloc");
                     m_Builder.CreateStore(initVal, envPtrAddr);
                 }
                 closureEnvAddr = envPtrAddr;
             }
             
             llvm::Value *opaqueEnv = m_Builder.CreatePointerCast(envPtrAddr, llvm::PointerType::getUnqual(m_Context));
             
             std::string invokeName = shp->Name + "___invoke";
             llvm::Function *invokeFn = m_Module->getFunction(invokeName);
             if (invokeFn) {
                 llvm::Value *opaqueFunc = m_Builder.CreatePointerCast(invokeFn, llvm::PointerType::getUnqual(m_Context));
                 
                 llvm::Value *fatPtr = llvm::UndefValue::get(type);
                 fatPtr = m_Builder.CreateInsertValue(fatPtr, opaqueEnv, 0);
                 fatPtr = m_Builder.CreateInsertValue(fatPtr, opaqueFunc, 1);
                 
                 if (isDynFn) {
                     std::string dropName = "Encap_" + shp->Name + "_drop";
                     llvm::Function *dropFn = m_Module->getFunction(dropName);
                     if (!dropFn)
                         dropFn = getOrCreateDropCascadeHelper(shp->Name);
                     llvm::Value *opaqueDrop = llvm::ConstantPointerNull::get(llvm::PointerType::getUnqual(m_Context));
                     if (dropFn) {
                         opaqueDrop = m_Builder.CreatePointerCast(dropFn, llvm::PointerType::getUnqual(m_Context));
                     }
                     fatPtr = m_Builder.CreateInsertValue(fatPtr, opaqueDrop, 2);
                 }
                 
                 initVal = fatPtr; // Value of type { ptr, ptr } or { ptr, ptr, ptr }
             }
         }
      }
  }

  if (initVal && initVal->getType() != type) {
    std::string s1 = "Unknown", s2 = "Unknown";
    if (type) {
      llvm::raw_string_ostream os1(s1);
      type->print(os1);
    }
    if (initVal) {
      llvm::raw_string_ostream os2(s2);
      initVal->getType()->print(os2);
    }

    error(var, DiagID::ERR_CODEGEN_INTERNAL_ERROR_TYPE_MISMATCH_IN_VARIAB, s1, s2);
    return nullptr;
  }

  // A legal destructive read of an erased `fn` binding transfers ownership
  // of the same concrete environment. Carry its lowering identity to the new
  // binding so a later consuming fn-to-dyn coercion can still heap-promote it.
  // Bare copies deliberately do not inherit owning environment metadata.
  if (!closureEnvAddr && sourceInitExpr) {
    const Expr *transferSource = sourceInitExpr;
    while (auto *cast = dynamic_cast<const CastExpr *>(transferSource))
      transferSource = cast->Expression.get();
    if (auto *cede = dynamic_cast<const CedeExpr *>(transferSource)) {
      transferSource = cede->Value.get();
      while (auto *cast = dynamic_cast<const CastExpr *>(transferSource))
        transferSource = cast->Expression.get();
      if (auto *sourceVariable =
              dynamic_cast<const VariableExpr *>(transferSource)) {
        const std::string sourceName =
            Type::stripMorphology(sourceVariable->Name);
        auto sourceSymbol = m_Symbols.find(sourceName);
        if (sourceSymbol != m_Symbols.end() &&
            sourceSymbol->second.ClosureEnvAddr &&
            sourceSymbol->second.ClosureEnvType &&
            !sourceSymbol->second.ClosureEnvTypeName.empty()) {
          closureEnvAddr = sourceSymbol->second.ClosureEnvAddr;
          closureEnvType = sourceSymbol->second.ClosureEnvType;
          closureEnvTypeName = sourceSymbol->second.ClosureEnvTypeName;
        }
      }
    }
  }

  if (closureEnvAddr && closureEnvType && !closureEnvTypeName.empty() &&
      m_Symbols.count(varName)) {
    auto &closureSymbol = m_Symbols[varName];
    closureSymbol.ClosureEnvAddr = closureEnvAddr;
    closureSymbol.ClosureEnvType = closureEnvType;
    closureSymbol.ClosureEnvTypeName = closureEnvTypeName;
  }

  if (initVal && alloca) {
    m_Builder.CreateStore(initVal, alloca);
    if (!m_ScopeStack.empty() && scopeBeforeInit < m_ScopeStack.back().size()) {
      for (size_t i = scopeBeforeInit; i < m_ScopeStack.back().size(); ++i) {
        auto &vsi = m_ScopeStack.back()[i];
        if (vsi.Name.empty() || vsi.Name[0] == '.') {
          vsi.HasDrop = false;
          vsi.IsShared = false;
          vsi.IsUniquePointer = false;
          if (vsi.DropFlag) {
            m_Builder.CreateStore(llvm::ConstantInt::getFalse(m_Context), vsi.DropFlag);
          }
        }
      }
    }
  }

  // Automatic Drop Registration
  if (!m_ScopeStack.empty()) {
    std::string typeName = var->TypeName;
    if (var->ResolvedType) {
      auto soul = var->ResolvedType;
      while (soul && (soul->isPointer() || soul->isReference() ||
                      soul->isSmartPointer())) {
        soul = soul->getPointeeType();
      }
      if (soul) {
        typeName = soul->getSoulName();
      }
    }

    // Fallback if empty (shouldn't happen with Annotated AST)
    if (typeName.empty() || typeName == "auto") {
      if (m_Symbols.count(varName))
        typeName = m_Symbols[varName].typeName;
    }

    std::string dropFunc = "";
    bool hasDrop = false;
    std::shared_ptr<Type> dropValueType = var->ResolvedType;
    while (dropValueType &&
           (dropValueType->isPointer() || dropValueType->isReference() ||
            dropValueType->isSmartPointer())) {
      dropValueType = dropValueType->getPointeeType();
    }

    std::function<bool(const std::shared_ptr<Type> &)> typeNeedsDrop =
        [&](const std::shared_ptr<Type> &candidate) {
          if (!candidate)
            return false;
          if (candidate->isArray())
            return typeNeedsDrop(candidate->getArrayElementType());
          if (auto outcome =
                  std::dynamic_pointer_cast<MissOutcomeType>(candidate))
            return typeNeedsDrop(outcome->PayloadType);
          if (candidate->isSharedPtr() || candidate->isUniquePtr())
            return true;
          if (candidate->isDynFn())
            return true;
          if (candidate->isRawPointer() || candidate->isReference())
            return false;
          const auto soul = candidate->getSoulType();
          if (!soul)
            return false;
          if (auto exactShape = std::dynamic_pointer_cast<ShapeType>(soul))
            return exactShape->Decl != nullptr ||
                   m_Shapes.count(soul->getSoulName()) != 0;
          return m_Shapes.count(soul->getSoulName()) != 0;
        };

    const ShapeDecl *exactDropShape = nullptr;
    if (dropValueType) {
      auto soul = dropValueType->getSoulType();
      if (auto shape = std::dynamic_pointer_cast<ShapeType>(soul))
        exactDropShape = shape->Decl;
    }

    if (exactDropShape) {
      hasDrop = true;
      dropFunc = exactDropShape->MangledDestructorName;
    } else if (!typeName.empty()) {
      if (m_Shapes.count(typeName)) {
        dropFunc = m_Shapes[typeName]->MangledDestructorName;
      }

      if (!dropFunc.empty()) {
        hasDrop = true;
      } else {
        // Simple mangling check: Type_Encap_drop
        // Need to strip morphology
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
        } else if (!base.empty() && base.front() == '[' && base.back() == ']') {
          std::string elemBase = base.substr(1, base.length() - 2);
          if (m_Shapes.count(elemBase)) {
             hasDrop = true;
          }
        }
      }
    }

    if (dropValueType && dropValueType->isArray()) {
      hasDrop = typeNeedsDrop(dropValueType);
      dropFunc.clear();
    }
    if (dropValueType && dropValueType->isMissOutcome()) {
      hasDrop = typeNeedsDrop(dropValueType);
      dropFunc.clear();
    }

    // [Fix] Scope Registration Logic
    // We must register ALL variables (including references and raw pointers)
    // so they can be looked up for Identity Rebinds. However, we must Ensure
    // they are NOT auto-dropped unless they own their data.

    bool canDrop = !var->IsReference &&
                   (!var->IsRawPointer || var->IsUnique || var->IsShared);
    if (!canDrop) {
      hasDrop = false;
      dropFunc = "";
    }
    if (var->ResolvedType &&
        var->ResolvedType->typeKind == toka::Type::DynFn) {
      hasDrop = true;
      dropFunc = "";
    }

    VariableScopeInfo info;
    info.Name = varName;
    info.Alloca = alloca;
    info.AllocType = alloca ? llvm::cast<llvm::AllocaInst>(alloca)->getAllocatedType() : type;
    info.IsUniquePointer = var->IsUnique;
    info.IsShared = var->IsShared;
    info.HasDrop = hasDrop;
    info.DropFunc = dropFunc;
    info.PartialMove = var->PartialMove;
    if (dropValueType)
      info.DropType = dropValueType;
    if (m_Symbols.count(varName)) {
        auto soul = m_Symbols[varName].soulTypeObj;
        info.SoulName = soul ? soul->getSoulType()->getSoulName() : "";
    }
    if (alloca && (hasDrop || var->IsUnique || var->IsShared)) {
      info.DropFlag = createEntryBlockAlloca(
          llvm::Type::getInt1Ty(m_Context), nullptr, varName + ".drop.live");
      m_Builder.CreateStore(
          llvm::ConstantInt::get(llvm::Type::getInt1Ty(m_Context),
                                 !startsUninitialized),
          info.DropFlag);
    }
    if (alloca && startsUninitialized) {
      info.InitFlag = createEntryBlockAlloca(
          llvm::Type::getInt1Ty(m_Context), nullptr, varName + ".init.live");
      m_Builder.CreateStore(llvm::ConstantInt::getFalse(m_Context),
                            info.InitFlag);
    }

    // Sema admits exactly the bounded projections whose cleanup representation
    // CodeGen may install.  Do not recreate that eligibility predicate here:
    // this plan is the shared Sema-to-cleanup boundary for P0.3.
    if (alloca && hasDrop && info.PartialMove.isAdmitted()) {
      info.DropMask = createEntryBlockAlloca(
          llvm::Type::getInt64Ty(m_Context), nullptr, varName + ".drop.mask");
      m_Builder.CreateStore(m_Builder.getInt64(info.PartialMove.eligibleMask()),
                            info.DropMask);
    }

    m_ScopeStack.back().push_back(info);

    if (closureEnvAddr && closureEnvType && !closureEnvTypeName.empty()) {
      VariableScopeInfo envInfo;
      envInfo.Name = varName;
      envInfo.Alloca = closureEnvAddr;
      envInfo.AllocType = closureEnvType;
      envInfo.IsUniquePointer = false;
      envInfo.IsShared = false;
      envInfo.HasDrop = true;
      envInfo.SoulName = closureEnvTypeName;
      envInfo.DropFlag = createEntryBlockAlloca(
          llvm::Type::getInt1Ty(m_Context), nullptr,
          varName + ".env.drop.live");
      m_Builder.CreateStore(llvm::ConstantInt::getTrue(m_Context),
                            envInfo.DropFlag);
      m_ScopeStack.back().push_back(envInfo);
    }

    // [New] Update m_Symbols with drop metadata for dispatcher
    if (m_Symbols.count(varName)) {
      m_Symbols[varName].hasDrop = hasDrop;
      m_Symbols[varName].dropFunc = dropFunc;
    }
  } else {
    // Top-level or outside a scope (e.g. globals)
    return nullptr;
  }
  return nullptr;
}

llvm::Value *CodeGen::genDestructuringDecl(const DestructuringDecl *dest) {
  PhysEntity initEnt = genExpr(dest->Init.get());
  llvm::Value *initVal = nullptr;

  llvm::Type *srcTy = nullptr;
  if (initEnt.isAddress) {
    srcTy = initEnt.irType;
  } else {
    initVal = initEnt.load(m_Builder); // Actually returns value
    if (initVal)
      srcTy = initVal->getType();
  }

  if (!srcTy || !srcTy->isStructTy()) {
    error(dest, DiagID::ERR_CODEGEN_POSITIONAL_DESTRUCTURING_REQUIRES_A_ST);
    return nullptr;
  }

  auto *st = llvm::cast<llvm::StructType>(srcTy);
  size_t elisionIndex = -1;
  size_t elisionCount = 0;
  for (size_t i = 0; i < dest->Variables.size(); ++i) {
    if (dest->Variables[i].Name == "..") {
      elisionIndex = i;
      elisionCount++;
    }
  }

  size_t expectedSize = st->getNumElements();
  size_t elidedCount = 0;
  if (elisionCount == 1) {
    elidedCount = expectedSize - (dest->Variables.size() - 1);
  }
  std::string shapeName = Type::stripMorphology(dest->TypeName);
  if (shapeName.empty()) {
    if (dest->Init && dest->Init->ResolvedType) {
      auto resTy = dest->Init->ResolvedType;
      while (resTy && (resTy->isPointer() || resTy->isReference() || resTy->isSmartPointer())) {
        if (auto inner = resTy->getPointeeType())
          resTy = inner;
        else
          break;
      }
      if (resTy && resTy->isShape()) {
        shapeName = std::static_pointer_cast<ShapeType>(resTy)->Name;
      }
    }
    if (shapeName.empty()) {
      if (st && m_TypeToName.count(st)) {
        shapeName = m_TypeToName[st];
      } else if (st) {
        shapeName = st->getName().str();
      }
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

  for (size_t i = 0; i < dest->Variables.size(); ++i) {
    const auto &v = dest->Variables[i];
    if (v.Name == ".." || v.IsWildcard) {
      continue;
    }

    size_t memberIndex = -1;
    if (m_Shapes.count(shapeName)) {
      const auto *sh = m_Shapes[shapeName];
      for (size_t m = 0; m < sh->Members.size(); ++m) {
        std::string cleanDef = sh->Members[m].Name;
        while (!cleanDef.empty() && (cleanDef.back() == '#' || cleanDef.back() == '!' || cleanDef.back() == '?')) {
          cleanDef.pop_back();
        }
        std::string cleanProv = v.FieldName;
        while (!cleanProv.empty() && (cleanProv.back() == '#' || cleanProv.back() == '!' || cleanProv.back() == '?')) {
          cleanProv.pop_back();
        }
        if (cleanDef == cleanProv ||
            toka::Type::stripMorphology(cleanDef) == toka::Type::stripMorphology(cleanProv)) {
          memberIndex = m;
          break;
        }
      }
    }

    if (memberIndex >= expectedSize || memberIndex == (size_t)-1) {
      error(dest, DiagID::ERR_CODEGEN_INVALID_MEMBER_IN_DESTRUCTURING_OF, v.FieldName, shapeName);
      break;
    }

    std::string vName = Type::stripMorphology(v.Name);

    // [Legacy] Bare union safety for destructuring
    llvm::Type *memberTy = nullptr;
    if (st->isOpaque() || st->getNumElements() <= memberIndex) {
      memberTy = llvm::Type::getInt8Ty(m_Context);
    } else {
      memberTy = st->getElementType(memberIndex);
    }

    llvm::Value *finalVal = nullptr;
    if (v.IsReference) {
      if (!initEnt.isAddress) {
        error(dest, DiagID::ERR_CODEGEN_CANNOT_TAKE_REFERENCE_OF_A_TEMPORARY_V);
        return nullptr;
      }
      llvm::Value *memberAddr =
          m_Builder.CreateStructGEP(st, initEnt.value, memberIndex,
                                    vName + ".addr");
      bool sourceFieldIsReference = false;
      if (m_Shapes.count(shapeName) &&
          memberIndex < m_Shapes[shapeName]->Members.size()) {
        sourceFieldIsReference =
            m_Shapes[shapeName]->Members[memberIndex].IsReference;
      }
      if (sourceFieldIsReference) {
        // A reference field already stores its referent address.  Forward
        // that address so `&view = .&field` remains a view of the referent,
        // rather than becoming a reference to the field's pointer slot.
        finalVal = m_Builder.CreateLoad(memberTy, memberAddr, vName);
      } else {
        // A value field has no stored referent address; its member slot is
        // the reference target.
        finalVal = memberAddr;
      }
    } else {
      if (initEnt.isAddress) {
        // [L-Value to R-Value] GEP + Load
        llvm::Value *addr =
            m_Builder.CreateStructGEP(st, initEnt.value, memberIndex, vName + ".addr");
        finalVal = m_Builder.CreateLoad(memberTy, addr, vName);
      } else {
        // [R-Value Destructuring] ExtractValue
        finalVal = m_Builder.CreateExtractValue(initVal, memberIndex, vName);
      }
    }

    llvm::AllocaInst *alloca =
        createEntryBlockAlloca(finalVal->getType(), nullptr, vName);
    m_Builder.CreateStore(finalVal, alloca);

    m_NamedValues[vName] = alloca;

    // [Fix] Register Type Name for Lookup (Auto Deduction)
    // We attempt to extract the user-written type from AST
    // (AllocExpr/NewExpr).
    std::string deducedType = "";
    Expr *rawInit = dest->Init.get();

    // Peel UnsafeExpr wrapper
    if (auto *ue = dynamic_cast<UnsafeExpr *>(rawInit)) {
      rawInit = ue->Expression.get();
    }

    if (auto *ae = dynamic_cast<AllocExpr *>(rawInit)) {
      deducedType = ae->TypeName; // "Data"
    } else if (auto *ne = dynamic_cast<NewExpr *>(rawInit)) {
      deducedType = ne->Type; // "Data"
    }

    // Strip decorators if present
    deducedType = Type::stripMorphology(deducedType);

    TokaSymbol sym;
    sym.allocaPtr = alloca;
    // A reference field stores its referent address.  Keep the field's
    // semantic reference type here, rather than treating the LLVM pointer
    // storage itself as the referent.  The latter makes a later use of the
    // destructured binding peel one pointer too many.
    std::shared_ptr<toka::Type> bindingType;
    if (m_Shapes.count(shapeName) &&
        memberIndex < m_Shapes[shapeName]->Members.size()) {
      bindingType = m_Shapes[shapeName]->Members[memberIndex].ResolvedType;
    }
    if (v.IsReference && bindingType && bindingType->isReference()) {
      auto pointeeType = bindingType->getPointeeType();
      fillSymbolMetadata(sym, bindingType,
                         pointeeType ? getLLVMType(pointeeType) : memberTy);
    } else {
      // Use memberTy (the 'Meat') as the soul type.
      fillSymbolMetadata(sym, "", false, false, false, v.IsReference,
                         v.IsValueMutable, memberTy);
    }
    sym.typeName = deducedType; // Set typeName in symbol
    sym.isRebindable = false;
    sym.isContinuous = memberTy->isArrayTy();
    m_Symbols[vName] = sym;

    if (!m_ScopeStack.empty()) {
      llvm::Type *vAllocTy = alloca->getType();
      if (auto *AI = llvm::dyn_cast<llvm::AllocaInst>(alloca))
        vAllocTy = AI->getAllocatedType();
      m_ScopeStack.back().push_back({v.Name, alloca, vAllocTy, false, false, false, "", deducedType});
    }
  }
  return nullptr;
}

void CodeGen::genGlobal(const Stmt *stmt) {
  if (auto *var = dynamic_cast<const VariableDecl *>(stmt)) {
    llvm::Value *initVal = nullptr;
    llvm::Constant *constInit = nullptr;
    bool needsDynamicInit = false;

    if (var->Init) {
      // Try resolving type hint first
      llvm::Type *hintType = nullptr;
      if (!var->TypeName.empty()) {
        hintType = resolveType(var->TypeName, var->IsRawPointer);
      }

      // Try compile-time constant generation first
      if (auto *c = genConstant(var->Init.get(), hintType)) {
        constInit = c;
        initVal = constInit;
      } else {
        // Fallback to Dynamic Initialization
        needsDynamicInit = true;
      }
    }

    llvm::Type *type = nullptr;
    if (!var->TypeName.empty()) {
      type = resolveType(var->TypeName, var->IsRawPointer);
    } else if (initVal) {
      type = initVal->getType();
    }

    if (!type && needsDynamicInit) {
      // Need type inference for globals with dynamic init but no hint
      getOrCreateGlobalInit();
      CodeGen::GenContext ctx = saveContext();
      m_Builder.SetInsertPoint(m_GlobalInitBuilder->GetInsertBlock(), m_GlobalInitBuilder->GetInsertPoint());
      PhysEntity dynValEnt = genExpr(var->Init.get());
      if (dynValEnt.value) type = dynValEnt.value->getType();
      m_GlobalInitBuilder->SetInsertPoint(m_Builder.GetInsertBlock(), m_Builder.GetInsertPoint());
      m_Builder.ClearInsertionPoint();
      restoreContext(ctx);
    }

    if (!type) {
      type = llvm::Type::getInt32Ty(m_Context);
    }

    bool declOnly = !m_AST->IsRootModule && m_AST->IsInterface;
    bool backedInterface = declOnly && m_AST->HasBackingObject;
    llvm::Constant *finalConstInit = constInit;
    if (declOnly && (!var->IsConst || backedInterface)) {
      finalConstInit = nullptr;
    } else if (!constInit) {
      finalConstInit = llvm::Constant::getNullValue(type);
    }

    bool importedSource = !m_AST->IsRootModule && !declOnly;
    llvm::GlobalValue::LinkageTypes linkage = llvm::GlobalValue::ExternalLinkage;
    if ((var->IsConst && !backedInterface) || importedSource) {
      linkage = llvm::GlobalValue::LinkOnceODRLinkage;
    }

    auto *globalVar = new llvm::GlobalVariable(
        *m_Module, type, false, linkage, finalConstInit,
        var->Name);

    if ((var->IsConst && !backedInterface) || importedSource) {
      llvm::Triple triple(m_Module->getTargetTriple());
      if (triple.supportsCOMDAT()) {
        globalVar->setComdat(m_Module->getOrInsertComdat(globalVar->getName()));
      }
    }

    m_NamedValues[var->Name] = globalVar;

    TokaSymbol sym;
    sym.allocaPtr = globalVar;
    fillSymbolMetadata(sym, var->TypeName, var->IsRawPointer, var->IsUnique,
                       var->IsShared, var->IsReference, var->IsValueMutable,
                       type);
    sym.isRebindable = var->IsRebindable;
    sym.isContinuous = type->isArrayTy();
    m_Symbols[var->Name] = sym;

    // Do actual assignment in .init_array constructor
    if (needsDynamicInit && !declOnly) {
       getOrCreateGlobalInit();
       CodeGen::GenContext ctx = saveContext();
       m_Builder.SetInsertPoint(m_GlobalInitBuilder->GetInsertBlock(), m_GlobalInitBuilder->GetInsertPoint());
       
       PhysEntity dynValEnt = genExpr(var->Init.get());
       llvm::Value *dynVal = dynValEnt.load(m_Builder);
       
       if (dynVal) {
          if (dynVal->getType() != type) {
               if (dynVal->getType()->isIntegerTy() && type->isIntegerTy()) {
                   dynVal = m_Builder.CreateIntCast(dynVal, type, false);
               } else if (dynVal->getType()->isPointerTy() && type->isPointerTy()) {
                   dynVal = m_Builder.CreatePointerCast(dynVal, type);
               }
          }
          m_Builder.CreateStore(dynVal, globalVar);
       }
       m_GlobalInitBuilder->SetInsertPoint(m_Builder.GetInsertBlock(), m_Builder.GetInsertPoint());
       m_Builder.ClearInsertionPoint();
       restoreContext(ctx);
    }
  } else {
    // We could support global destructuring here, but for now just skip or
    // error
    error(dynamic_cast<const ASTNode *>(stmt), DiagID::ERR_CODEGEN_GLOBAL_DESTRUCTURING_NOT_YET_SUPPORTED);
  }
}

void CodeGen::genExtern(const ExternDecl *ext) {
  std::vector<llvm::Type *> argTypes;
  for (const auto &arg : ext->Args) {
    auto type = arg.ResolvedType
                    ? arg.ResolvedType
                    : lowerTypeSyntax(arg.TypeSyntax, arg.Type);
    llvm::Type *t = getLLVMType(type);
    if (!t) {
      error(ext, DiagID::ERR_CODEGEN_UNRESOLVED_ARGUMENT_TYPE_IN_EXTERN_FUN, arg.Type, ext->Name);
      return;
    }
    argTypes.push_back(t);
  }
  auto returnType = lowerTypeSyntax(ext->ReturnTypeSyntax, ext->ReturnType);
  llvm::Type *retType = getLLVMType(returnType);
  if (!retType) {
    error(ext, DiagID::ERR_CODEGEN_UNRESOLVED_RETURN_TYPE_IN_EXTERN_FUNCT, ext->ReturnType, ext->Name);
    return;
  }
  llvm::FunctionType *ft =
      llvm::FunctionType::get(retType, argTypes, ext->IsVariadic);

  std::string llvmName = ext->Name;
  if (llvmName.size() > 5 && llvmName.substr(0, 5) == "libc_") {
    llvmName = llvmName.substr(5);
  }

  if (llvm::Function *existing = m_Module->getFunction(llvmName)) {
    if (existing->getFunctionType() != ft) {
      std::string existingType;
      std::string requestedType;
      llvm::raw_string_ostream(existingType) << *existing->getFunctionType();
      llvm::raw_string_ostream(requestedType) << *ft;
      error(ext, DiagID::ERR_CODEGEN_EXTERN_SIGNATURE_CONFLICT, llvmName,
            existingType, requestedType);
    }
    return;
  }

  llvm::Function::Create(ft, llvm::Function::ExternalLinkage, llvmName,
                         m_Module.get());
}

void CodeGen::genShape(const ShapeDecl *sh) {
  if (!sh->GenericParams.empty())
    return;

  const std::string shapeName =
      sh->CodegenName.empty() ? sh->Name : sh->CodegenName;
  if (m_StructTypesByDecl.count(sh))
    return;

  llvm::StructType *st = llvm::StructType::create(m_Context, shapeName);
  m_StructTypesByDecl[sh] = st;
  m_StructDeclsByType[st] = sh;
  m_Shapes[sh->Name] = sh;
  m_Shapes[shapeName] = sh;
  m_StructTypes[shapeName] = st;
  if (!sh->OwnerLinkName.empty()) {
    m_Shapes[sh->OwnerLinkName] = sh;
    m_StructTypes[sh->OwnerLinkName] = st;
  }
  m_TypeToName[st] = shapeName;

  std::vector<llvm::Type *> body;
  const llvm::DataLayout &DL = m_Module->getDataLayout();

  if (sh->Kind == ShapeKind::Struct || sh->Kind == ShapeKind::Tuple) {
    std::vector<std::string> fieldNames;
    for (const auto &member : sh->Members) {
      if (verboseMode) {
        std::cerr << "[DEBUG genShape] shape=" << sh->Name << " member=" << member.Name << " Type=" << member.Type << " ResolvedType=" << (member.ResolvedType ? member.ResolvedType->toString() : "NULL") << " IsRawPointer=" << member.IsRawPointer << " IsUnique=" << member.IsUnique << " IsShared=" << member.IsShared << " IsReference=" << member.IsReference << std::endl;
      }
      llvm::Type *t = nullptr;
      if (member.ResolvedType) {
        t = getLLVMType(member.ResolvedType);
      } else {
        // Fallback (Should be unreachable if Sema Pass 2 worked)
        if (member.Type == "unknown") { // Assuming "unknown" is the string
                                         // representation for unresolved types
          DiagnosticEngine::report({"<codegen>", 0, 0},
                                   DiagID::WARN_CODEGEN_UNRESOLVED,
                                   member.Name);
        }
        t = getLLVMType(lowerTypeSyntax(member.TypeSyntax, member.Type));
      }
      if (!t) {
        error(sh, DiagID::ERR_CODEGEN_UNRESOLVED_MEMBER_TYPE_IN_SHAPE, member.Type, sh->Name);
        return;
      }
      body.push_back(t);
      fieldNames.push_back(member.Name);
    }
    st->setBody(body, false);
    m_StructFieldNames[sh->Name] = fieldNames;
    m_StructFieldNames[shapeName] = fieldNames;
  } else if (sh->Kind == ShapeKind::Array) {
    llvm::Type *elemTy = nullptr;
    if (sh->Members[0].ResolvedType) {
      elemTy = getLLVMType(sh->Members[0].ResolvedType);
    } else {
      elemTy = getLLVMType(
          lowerTypeSyntax(sh->Members[0].TypeSyntax, sh->Members[0].Type));
    }
    if (!elemTy) {
      error(sh, DiagID::ERR_CODEGEN_UNRESOLVED_ARRAY_ELEMENT_TYPE_IN_SHAPE, sh->Members[0].Type, sh->Name);
      return;
    }
    llvm::Type *arrTy = llvm::ArrayType::get(elemTy, sh->ArraySize);
    body.push_back(arrTy);
    st->setBody(body, false);
  } else if (sh->Kind == ShapeKind::Union) {
    // Legacy bare union: find max size and alignment
    uint64_t maxSize = 0;
    uint64_t maxAlign = 1;
    for (const auto &member : sh->Members) {
      llvm::Type *t = nullptr;
      if (member.ResolvedType) {
        t = getLLVMType(member.ResolvedType);
      } else {
        t = getLLVMType(lowerTypeSyntax(member.TypeSyntax, member.Type));
      }
      if (!t) {
        error(sh, DiagID::ERR_CODEGEN_UNRESOLVED_UNION_MEMBER_TYPE_IN_SHAPE, member.Type, sh->Name);
        return;
      }
      if (!t->isVoidTy()) {
        maxSize =
            std::max(maxSize, (uint64_t)DL.getTypeAllocSize(t).getFixedValue());
      }
      maxAlign = std::max(maxAlign, (uint64_t)DL.getABITypeAlign(t).value());
    }
    // Model as [maxSize x i8]
    if (maxSize % maxAlign != 0) {
      maxSize = ((maxSize / maxAlign) + 1) * maxAlign;
    }
    const_cast<ShapeDecl *>(sh)->MaxAlign = maxAlign;

    body.push_back(
        llvm::ArrayType::get(llvm::Type::getInt8Ty(m_Context), maxSize));
    st->setBody(body, false);

    std::vector<std::string> fieldNames;
    for (const auto &member : sh->Members) {
      fieldNames.push_back(member.Name);
    }
    m_StructFieldNames[sh->Name] = fieldNames;
    m_StructFieldNames[shapeName] = fieldNames;
  } else if (sh->Kind == ShapeKind::Enum) {
    // Tagged enum: { i8 tag, [aligned payload storage] }.  Payloads are
    // viewed through their concrete field types, so the backing storage must
    // retain the maximum ABI alignment of every variant.
    uint64_t maxPayloadSize = 0;
    uint64_t maxPayloadAlign = 1;
    for (const auto &variant : sh->Members) {
      uint64_t variantSize = 0;
      uint64_t variantAlign = 1;
      if (!variant.SubMembers.empty()) {
        std::vector<llvm::Type *> fieldTypes;
        for (const auto &field : variant.SubMembers) {
          llvm::Type *t = nullptr;
          if (field.ResolvedType) {
            t = getLLVMType(field.ResolvedType);
          } else {
            t = getLLVMType(lowerTypeSyntax(field.TypeSyntax, field.Type));
          }
          if (!t) {
            error(sh, DiagID::ERR_CODEGEN_UNRESOLVED_VARIANT_FIELD_TYPE_IN_ENUM, field.Type, sh->Name);
            return;
          }
          fieldTypes.push_back(t);
        }
        // Enum payloads are accessed through their concrete field types.  Do
        // not pack them: placing an i32 payload immediately after the i8 tag
        // would otherwise create an unaligned address on strict-alignment
        // targets such as arm64.
        llvm::StructType *st =
            llvm::StructType::get(m_Context, fieldTypes, false);
        variantSize = DL.getTypeAllocSize(st).getFixedValue();
        variantAlign = DL.getABITypeAlign(st).value();
      } else if (!variant.IsUnitVariant && !variant.Type.empty() &&
                 variant.Type != "void") {
        llvm::Type *t = nullptr;
        if (variant.ResolvedType) {
          t = getLLVMType(variant.ResolvedType);
        } else {
          t = getLLVMType(
              lowerTypeSyntax(variant.TypeSyntax, variant.Type));
        }
        if (!t) {
          error(sh, DiagID::ERR_CODEGEN_UNRESOLVED_VARIANT_PAYLOAD_TYPE_IN_ENU, variant.Type, sh->Name);
          return;
        }
        if (!t->isVoidTy()) {
          variantSize = DL.getTypeAllocSize(t).getFixedValue();
          variantAlign = DL.getABITypeAlign(t).value();
        }
      }
      maxPayloadSize = std::max(maxPayloadSize, variantSize);
      maxPayloadAlign = std::max(maxPayloadAlign, variantAlign);
    }
    body.push_back(llvm::Type::getInt8Ty(m_Context)); // Tag
    if (maxPayloadSize > 0) {
      llvm::Type *storageUnit = llvm::IntegerType::get(
          m_Context, static_cast<unsigned>(maxPayloadAlign * 8));
      uint64_t storageUnits =
          (maxPayloadSize + maxPayloadAlign - 1) / maxPayloadAlign;
      body.push_back(llvm::ArrayType::get(storageUnit, storageUnits));
    }
    // The tag and payload storage must receive ordinary ABI padding.  The
    // payload is later viewed as its concrete type, so a packed enum can
    // produce unaligned loads/stores for aggregate variants.
    st->setBody(body, false);

    std::vector<std::string> fieldNames;
    for (const auto &member : sh->Members) {
      fieldNames.push_back(member.Name);
    }
    m_StructFieldNames[sh->Name] = fieldNames;
    m_StructFieldNames[shapeName] = fieldNames;
  }
}

void toka::CodeGen::genImpl(const toka::ImplDecl *decl, bool declOnly) {
  if (!decl->GenericParams.empty()) {
    return;
  }

  // [NEW] Skip Impls for template shapes (they won't have LLVM types)
  if (!resolveType(decl->TypeName, false)) {
    return;
  }

  const std::string ownerTypeName =
      decl->ResolvedOwner
          ? (decl->ResolvedOwner->CodegenName.empty()
                 ? decl->ResolvedOwner->Name
                 : decl->ResolvedOwner->CodegenName)
          : decl->TypeName;
  const std::string ownerLinkName =
      decl->ResolvedOwner &&
              decl->ResolvedOwner->OwnerLinkName.rfind("__toka_owner_", 0) == 0
          ? decl->ResolvedOwner->OwnerLinkName
          : ownerTypeName;
  m_CurrentSelfType = ownerTypeName;
  std::set<std::string> implementedMethods;

  // Methods defined in Impl block
  for (const auto &method : decl->Methods) {
    std::string mangledName;
    if (!method->CodegenName.empty()) {
      mangledName = method->CodegenName;
    } else if (!decl->TraitName.empty()) {
      mangledName = decl->TraitName + "_" + ownerLinkName + "_" + method->Name;
    } else {
      mangledName = ownerLinkName + "_" + method->Name;
    }
    genFunction(method.get(), mangledName, declOnly);
    implementedMethods.insert(method->Name);
  }

  // @Encap policies and @Copy markers never participate in a trait vtable.
  if (!decl->TraitName.empty() &&
      getTraitFamilyNameForCodeGen(decl->TraitName) != "Encap" &&
      getTraitFamilyNameForCodeGen(decl->TraitName) != "Copy") {
    const TraitDecl *trait = nullptr;
    std::string traitLookupName = getTraitFamilyNameForCodeGen(decl->TraitName);
    if (m_Traits.count(traitLookupName)) {
      trait = m_Traits[traitLookupName];
    }

    if (trait) {
      for (const auto &method : trait->Methods) {
        if (implementedMethods.count(method->Name))
          continue;

        if (method->Body) {
          // Generate default implementation
          std::string mangledName =
              decl->TraitName + "_" + ownerLinkName + "_" + method->Name;
          genFunction(method.get(), mangledName, declOnly);
        } else {

          error(decl, DiagID::ERR_CODEGEN_MISSING_IMPLEMENTATION_FOR_METHOD_OF_T, method->Name, decl->TraitName);
        }
      }
    } else {
      error(decl, DiagID::ERR_CODEGEN_TRAIT_NOT_FOUND, decl->TraitName);
    }

    // Generate VTable
    if (trait && !declOnly) {
      std::vector<llvm::Constant *> vtableMethods;
      llvm::Type *voidPtrTy =
      llvm::PointerType::getUnqual(m_Context);
      for (const auto &method : trait->Methods) {
        std::string implFuncName;
        for (const auto &implemented : decl->Methods) {
          if (implemented->Name == method->Name &&
              !implemented->CodegenName.empty()) {
            implFuncName = implemented->CodegenName;
            break;
          }
        }
        if (implFuncName.empty())
          implFuncName = decl->TraitName + "_" + ownerLinkName + "_" +
                         method->Name;
        llvm::Function *f = m_Module->getFunction(implFuncName);
        if (f) {
          vtableMethods.push_back(llvm::ConstantExpr::getBitCast(f, voidPtrTy));
        } else {
          vtableMethods.push_back(llvm::Constant::getNullValue(voidPtrTy));
        }
      }

      llvm::ArrayType *arrTy =
          llvm::ArrayType::get(voidPtrTy, vtableMethods.size());
      llvm::Constant *init = llvm::ConstantArray::get(arrTy, vtableMethods);
      std::string vtableName =
          "_VTable_" + ownerLinkName + "_" + decl->TraitName;
      llvm::GlobalVariable *vtableGV =
          m_Module->getGlobalVariable(vtableName);
      if (vtableGV && vtableGV->isDeclaration()) {
        if (vtableGV->getValueType() == arrTy) {
          vtableGV->setInitializer(init);
          vtableGV->setConstant(true);
        } else {
          auto *replacement = new llvm::GlobalVariable(
              *m_Module, arrTy, true, llvm::GlobalValue::ExternalLinkage, init,
              vtableName + ".definition");
          vtableGV->replaceAllUsesWith(replacement);
          vtableGV->eraseFromParent();
          replacement->setName(vtableName);
          vtableGV = replacement;
        }
      } else if (!vtableGV) {
        vtableGV = new llvm::GlobalVariable(
            *m_Module, arrTy, true, llvm::GlobalValue::ExternalLinkage, init,
            vtableName);
      }
      vtableGV->setLinkage(llvm::GlobalValue::LinkOnceODRLinkage);
      llvm::Triple triple(m_Module->getTargetTriple());
      if (triple.supportsCOMDAT()) {
        vtableGV->setComdat(m_Module->getOrInsertComdat(vtableName));
      }
    }
  }

  m_CurrentSelfType = "";
}

PhysEntity toka::CodeGen::genMethodCall(const toka::MethodCallExpr *expr) {
  const std::string authorityRoute =
      expr->Stage0Authority && !expr->Stage0Authority->Route.empty()
          ? expr->Stage0Authority->Route
          : "method";
  if (!validateStage0CodeGenAuthority(
          expr, Stage0CodeGenAuthorityKind::CallTransaction,
          TransferDestination::Indeterminate, "call:" + authorityRoute, true))
    return {};
  if (expr->IsIntrinsicCopyDup) {
    PhysEntity source = genExpr(expr->Object.get());
    llvm::Value *value = source.load(m_Builder);
    auto resultType = expr->ResolvedType ? expr->ResolvedType
                                         : expr->Object->ResolvedType;
    llvm::Type *llvmType = resultType ? getLLVMType(resultType)
                                      : (value ? value->getType() : nullptr);
    return PhysEntity(value, resultType ? resultType->toString() : source.typeName,
                      llvmType, false);
  }

  // [Intrinsic] unset & unwrap
  if (expr->Method == "unset") {
    PhysEntity obj = genExpr(expr->Object.get());
    return PhysEntity(obj.value, "", obj.irType,
                      true); // Return address as LValue
  }
  if (expr->Method == "unwrap") {
    // Intrinsic unwrap is reserved for may-zero raw pointers.
    // Do NOT hijack 'unwrap' method on Structs (like Option<T>).
    bool isPointer = false;
    if (expr->Object->ResolvedType) {
      isPointer = expr->Object->ResolvedType->isPointer();
    }

    if (isPointer) {
      llvm::Value *objVal = nullptr;
      const Expr *object = expr->Object.get();
      while (auto *postfix = dynamic_cast<const PostfixExpr *>(object))
        object = postfix->LHS.get();

      if (auto *var = dynamic_cast<const VariableExpr *>(object)) {
        llvm::Value *identity = getIdentityAddr(var->codegenName());
        llvm::Type *handleTy = getLLVMType(expr->Object->ResolvedType);
        if (identity && handleTy)
          objVal = m_Builder.CreateLoad(handleTy, identity, "unwrap.handle");
      }
      if (!objVal)
        objVal = genExpr(expr->Object.get()).load(m_Builder);
      if (!objVal)
        return nullptr;

      genNullCheck(objVal, expr, "null pointer unwrap");
      return PhysEntity(objVal, expr->ResolvedType->toString(),
                        objVal->getType(), false);
    }
  }

  const Expr *receiverExpr = expr->Object.get();
  while (auto *postfix = dynamic_cast<const PostfixExpr *>(receiverExpr))
    receiverExpr = postfix->LHS.get();
  const auto *receiverVar = dynamic_cast<const VariableExpr *>(receiverExpr);
  const auto receiverSymbol =
      receiverVar
          ? m_Symbols.find(Type::stripMorphology(receiverVar->codegenName()))
          : m_Symbols.end();
  const bool receiverIsDirectUnique =
      receiverVar &&
      ((receiverVar->ResolvedType &&
        receiverVar->ResolvedType->isUniquePtr()) ||
       receiverVar->IsUnique ||
       (receiverSymbol != m_Symbols.end() &&
        receiverSymbol->second.morphology == Morphology::Unique));
  const bool receiverIsMorphicProjection =
      receiverSymbol != m_Symbols.end() &&
      receiverSymbol->second.isMorphicValueTransport &&
      receiverSymbol->second.mode != AddressingMode::Direct;

  // `genVariableExpr` carries a unique receiver as its payload address but
  // also records a pointer-shaped IR type. Loading that entity once more
  // would read the payload bytes as a pointer.  A direct unique receiver's
  // payload address is already its method receiver value.
  PhysEntity receiverEntity;
  llvm::Value *objVal = nullptr;
  if (receiverIsDirectUnique || receiverIsMorphicProjection) {
    objVal = getEntityAddr(receiverVar->codegenName());
  } else {
    receiverEntity = genExpr(expr->Object.get());
    objVal = receiverEntity.load(m_Builder);
  }
  if (!objVal)
    return nullptr;

  // --- Dynamic Dispatch (dyn @Trait) ---
  std::string dynamicTypeName = "";
  if (auto *ve = dynamic_cast<const VariableExpr *>(expr->Object.get())) {
    std::string varName = Type::stripMorphology(ve->Name);
    // [Fix] Use Symbol Table typeName instead of legacy m_ValueTypeNames
    if (m_Symbols.count(varName)) {
      std::string vType = m_Symbols[varName].typeName;
      // vType is e.g. *Data or Data
      if (!vType.empty()) {
        if (vType[0] == '*') {
          dynamicTypeName = vType.substr(1); // Peel pointer
        } else {
          dynamicTypeName = vType; // Already base type (e.g. "Data")
        }
      }
    }
  }

  if (!dynamicTypeName.empty()) {
    std::string traitName = "";
    if (dynamicTypeName.find("dyn @") == 0)
      traitName = dynamicTypeName.substr(5);
    else if (dynamicTypeName.find("dyn@") == 0)
      traitName = dynamicTypeName.substr(4);

    if (!traitName.empty() && m_Traits.count(traitName)) {
      const TraitDecl *trait = m_Traits[traitName];
      int methodIdx = -1;
      const FunctionDecl *methodDecl = nullptr;

      for (size_t i = 0; i < trait->Methods.size(); ++i) {
        if (trait->Methods[i]->Name == expr->Method) {
          methodIdx = i;
          methodDecl = trait->Methods[i].get();
          break;
        }
      }

      if (methodIdx != -1) {
        // 1. Extract Data and VTable
        llvm::Value *dataPtr =
            m_Builder.CreateExtractValue(objVal, 0, "dyn_data");
        llvm::Value *vtablePtr =
            m_Builder.CreateExtractValue(objVal, 1, "dyn_vtable");

        // 2. Load Function Pointer from VTable
        llvm::Type *voidPtrTy =
            llvm::PointerType::getUnqual(m_Context);
        llvm::Type *vtableArrayTy =
            llvm::PointerType::getUnqual(m_Context); // i8**

        llvm::Value *vtableArray =
            m_Builder.CreateBitCast(vtablePtr, vtableArrayTy);
        llvm::Value *funcPtrAddr =
            m_Builder.CreateConstGEP1_32(voidPtrTy, vtableArray, methodIdx);
        llvm::Value *voidFuncPtr = m_Builder.CreateLoad(voidPtrTy, funcPtrAddr);

        // 3. Prepare Arguments
        std::vector<llvm::Value *> args;
        std::vector<llvm::Type *> argTypes;

        // Self (dataPtr)
        args.push_back(dataPtr); // i8* passed to opaque ptr
        argTypes.push_back(llvm::PointerType::getUnqual(m_Context));

        for (size_t index = 0; index < expr->Args.size(); ++index) {
          const auto &arg = expr->Args[index];
          const FunctionDecl::Arg *formal =
              methodDecl && index + 1 < methodDecl->Args.size()
                  ? &methodDecl->Args[index + 1]
                  : nullptr;
          if (formal && formal->IsCeded &&
              !hasValidatedCallTransferElaboration(arg.get()) &&
              isCallTransferSourcePlace(arg.get()) &&
              typeCarriesCleanupLiability(arg->ResolvedType)) {
            error(arg.get(),
                  DiagID::ERR_CODEGEN_MISSING_CALL_TRANSFER_ELABORATION,
                  std::to_string(index + 1), expr->Method);
            return nullptr;
          }
          llvm::Value *av = genExpr(arg.get()).load(m_Builder);
          if (!av)
            return nullptr;
          auto formalType =
              formal
                  ? (formal->ResolvedType
                         ? formal->ResolvedType
                         : lowerTypeSyntax(formal->TypeSyntax, formal->Type))
                  : nullptr;
          llvm::Type *logicalType = formalType ? getLLVMType(formalType) : nullptr;
          const bool aggregateCapture =
              formal && logicalType &&
              (logicalType->isStructTy() || logicalType->isArrayTy()) &&
              !formal->IsRawPointer && !formal->IsReference &&
              !formal->IsUnique && !formal->IsShared;
          if (aggregateCapture) {
            llvm::Value *address = nullptr;
            if (!formal->IsCeded)
              address = genAddr(arg.get());
            if (!address) {
              auto *temporary = createEntryBlockAlloca(
                  av->getType(), nullptr, "dyn.arg.tmp");
              m_Builder.CreateStore(av, temporary);
              address = temporary;
              if (!formal->IsCeded && arg->ResolvedType)
                registerFullExpressionTemporary(temporary, arg->ResolvedType);
            }
            av = address;
          }
          args.push_back(av);
          argTypes.push_back(av->getType());
        }

        bool isSRet = expr->ResolvedType && shouldReturnSRet(expr->ResolvedType);
        llvm::Value *sretAlloc = nullptr;
        if (isSRet) {
            llvm::Type *retLLVMTy = getLLVMType(expr->ResolvedType);
            sretAlloc = createEntryBlockAlloca(retLLVMTy, nullptr, "sret.tmp");
            args.insert(args.begin(), sretAlloc);
            argTypes.insert(argTypes.begin(), llvm::PointerType::getUnqual(m_Context));
        }

        // 4. Determine Return Type
        llvm::Type *retTy = isSRet ? llvm::Type::getVoidTy(m_Context) : resolveType(methodDecl->ReturnType, false);
        llvm::FunctionType *ft =
            llvm::FunctionType::get(retTy, argTypes, false);

        // 5. Call
        llvm::CallInst *ci = m_Builder.CreateCall(ft, voidFuncPtr, args);
        if (isSRet) {
            ci->addParamAttr(0, llvm::Attribute::get(m_Context, llvm::Attribute::StructRet, getLLVMType(expr->ResolvedType)));
            return PhysEntity(sretAlloc, expr->ResolvedType->getSoulName(), getLLVMType(expr->ResolvedType), true);
        }
        if (expr->ResolvedType && expr->ResolvedType->isUnit() &&
            ci->getType()->isVoidTy()) {
          llvm::Type *unitTy = getLLVMType(expr->ResolvedType);
          return PhysEntity(llvm::Constant::getNullValue(unitTy),
                            expr->ResolvedType->toString(), unitTy, false);
        }
        return ci;
      }
    }
  }
  // --- End Dynamic Dispatch ---

  llvm::Type *ty = objVal->getType();
  llvm::Type *structTy = nullptr;

  if (ty->isStructTy()) {
    structTy = ty;
  } else {
    if (auto *ai = llvm::dyn_cast<llvm::AllocaInst>(objVal)) {
      if (ai->getAllocatedType()->isStructTy())
        structTy = ai->getAllocatedType();
    } else if (auto *gep = llvm::dyn_cast<llvm::GetElementPtrInst>(objVal)) {
      if (gep->getResultElementType()->isStructTy())
        structTy = gep->getResultElementType();
    }

    if (!structTy) {
      if (auto *ve = dynamic_cast<const VariableExpr *>(expr->Object.get())) {
        if (m_Symbols.count(ve->Name)) {
          structTy = m_Symbols[ve->Name].soulType;
        }
      }
    }
  }

  if (!structTy) {
    if (auto *ne = dynamic_cast<const NewExpr *>(expr->Object.get())) {
      structTy = resolveType(ne->Type, false);
    }
  }

  std::string typeName;
  if (expr->Object->ResolvedType) {
    typeName =
        toka::Type::stripMorphology(expr->Object->ResolvedType->getSoulName());
  }

  if (typeName.empty() && structTy && m_TypeToName.count(structTy)) {
    typeName = m_TypeToName[structTy];
  }

  if (typeName.empty()) {
    error(expr, DiagID::ERR_CODEGEN_CANNOT_DETERMINE_TYPE_FOR_METHOD_CALL, expr->Method);
    return nullptr;
  }

  std::string methodOwnerName = typeName;
  if (expr->Object->ResolvedType) {
    auto soul = expr->Object->ResolvedType->getSoulType();
    if (auto shape = std::dynamic_pointer_cast<ShapeType>(soul)) {
      if (shape->Decl &&
          shape->Decl->OwnerLinkName.rfind("__toka_owner_", 0) == 0)
        methodOwnerName = shape->Decl->OwnerLinkName;
    }
  }
  std::string funcName =
      expr->ResolvedFn && !expr->ResolvedFn->CodegenName.empty()
          ? expr->ResolvedFn->CodegenName
          : methodOwnerName + "_" + expr->Method;
  llvm::Function *callee = m_Module->getFunction(funcName);

  // Check Traits
  if (!callee) {
    for (auto const &[traitName, traitDecl] : m_Traits) {
      std::string traitFunc =
          traitName + "_" + methodOwnerName + "_" + expr->Method;
      callee = m_Module->getFunction(traitFunc);
      if (callee) {
        funcName = traitFunc;
        break;
      }
      if (callee)
        break;
    }
  }

  if (!callee) {
    std::string traitImplSuffix =
        "_" + methodOwnerName + "_" + expr->Method;
    for (const auto &[candidateName, candidateDecl] : m_Functions) {
      if (candidateName.size() <= traitImplSuffix.size())
        continue;
      if (candidateName.compare(candidateName.size() - traitImplSuffix.size(),
                                traitImplSuffix.size(),
                                traitImplSuffix) != 0)
        continue;
      callee = m_Module->getFunction(candidateName);
      if (callee) {
        funcName = candidateName;
        break;
      }
    }
  }

  // Explicit check for @Encap (hybrid trait)
  if (!callee) {
    std::string encapFunc = "Encap_" + typeName + "_" + expr->Method;
    callee = m_Module->getFunction(encapFunc);
  }

  if (!callee) {
    error(expr, DiagID::ERR_CODEGEN_METHOD_NOT_FOUND_FOR_TYPE_MANGLED, expr->Method, typeName, funcName);
    return nullptr;
  }

  // Retrieve FunctionDecl to check for Mutability (Pass-By-Reference)
  const FunctionDecl *fd = expr->ResolvedFn;
  if (!fd && m_Functions.count(funcName)) {
    fd = m_Functions[funcName];
  }

  bool isMethodAsync = (fd && fd->Effect == EffectKind::Async);
  bool isSRet = expr->ResolvedType && shouldReturnSRet(expr->ResolvedType) && !isMethodAsync;
  size_t selfLlvmIdx = isSRet ? 1 : 0;

  // A `cede self` method receives the payload value, even if the LLVM ABI
  // represents that value by a pointer. Recover a direct unique receiver's
  // heap payload address from its symbol; its expression type may have been
  // lowered as a pointer-shaped value and must not be loaded through again.
  const bool selfIsCeded =
      fd && !fd->Args.empty() && fd->Args[0].IsCeded;
  const bool selfReceivesPayloadByValue =
      selfIsCeded &&
      !fd->Args[0].IsUnique &&
      !(fd->Args[0].ResolvedType && fd->Args[0].ResolvedType->isUniquePtr());
  const bool receiverProvidesUniquePayload =
      selfReceivesPayloadByValue &&
      ((expr->Object->ResolvedType &&
        expr->Object->ResolvedType->isUniquePtr()) ||
       receiverIsDirectUnique);

  std::vector<llvm::Value *> args;

  // 1. Handle Self (Argument 0)
  // Check if self is mutable (requires pointer)
  bool selfIsMutable = false;
  if (fd && !fd->Args.empty()) {
    // Arg 0 is self
    if (fd->Args[0].IsValueMutable)
      selfIsMutable = true;
  }
  // Fallback: Check LLVM Arg Type
  if (!fd && callee->arg_size() > selfLlvmIdx &&
      callee->getArg(selfLlvmIdx)->getType()->isPointerTy()) {
    selfIsMutable = true;
  }

  llvm::Value *finalObjVal = objVal;
  if (receiverProvidesUniquePayload && receiverVar) {
    if (llvm::Value *payloadAddr = getEntityAddr(receiverVar->codegenName()))
      finalObjVal = payloadAddr;
  }
  bool targetExpectsPtr =
      (callee->arg_size() > selfLlvmIdx && callee->getArg(selfLlvmIdx)->getType()->isPointerTy());

  if ((selfIsMutable || targetExpectsPtr) && !receiverProvidesUniquePayload) {
    // Must pass address
    // Reuse the address produced by the single receiver evaluation. Calling
    // genAddr again for a member chain whose base is a consuming call would
    // evaluate that call twice and duplicate the same ownership payload.
    const bool reusesEvaluatedAddress =
        receiverEntity.isAddress &&
        (hasCallBackedReceiver(expr->Object.get()) ||
         hasIndexBackedReceiver(expr->Object.get()));
    llvm::Value *addr = reusesEvaluatedAddress ? receiverEntity.value : nullptr;
    if (reusesEvaluatedAddress && hasCallBackedReceiver(expr->Object.get()) &&
        !selfIsCeded &&
        isDirectCallReceiver(expr->Object.get()) &&
        expr->Object->ResolvedType) {
      registerFullExpressionTemporary(addr, expr->Object->ResolvedType);
    }
    if (!addr)
      addr = genAddr(expr->Object.get());
    if (addr) {
      finalObjVal = addr;
    } else {
      // Fallback for R-Values: Create temporary alloca
      // Only if objVal is not already a pointer
      if (!objVal->getType()->isPointerTy()) {
        llvm::AllocaInst *tmp = createEntryBlockAlloca(objVal->getType());
        m_Builder.CreateStore(objVal, tmp);
        finalObjVal = tmp;
        if (!selfIsCeded && expr->Object->ResolvedType) {
          registerFullExpressionTemporary(tmp,
                                          expr->Object->ResolvedType);
        }
      } else if (!selfIsCeded && expr->Object->ResolvedType &&
                 expr->Object->ResolvedType->isUniquePtr() &&
                 isOwnedUniqueReceiverRvalue(expr->Object.get())) {
        llvm::AllocaInst *tmp =
            createEntryBlockAlloca(objVal->getType(), nullptr,
                                   "unique.receiver.tmp");
        m_Builder.CreateStore(objVal, tmp);
        registerFullExpressionTemporary(tmp, expr->Object->ResolvedType);
      }
    }
  }

  bool isStatic =
      (fd && (fd->Args.empty() ||
              Type::stripMorphology(fd->Args[0].Name) != "self"));
  // Type Check Self
  if (!isStatic) {
    if (callee->arg_size() > selfLlvmIdx) {
      llvm::Type *targetTy = callee->getArg(selfLlvmIdx)->getType();
      if (finalObjVal->getType() != targetTy) {
        if (finalObjVal->getType()->isPointerTy() && !targetTy->isPointerTy()) {
          // Implicit Dereference (Pass Reference as Value - Rare for self but
          // possible)
          finalObjVal = m_Builder.CreateLoad(targetTy, finalObjVal);
        }
      }
    }
    args.push_back(finalObjVal);
  }

  // 2. Handle Arguments
  for (size_t i = 0; i < expr->Args.size(); ++i) {
    const auto *cededArg =
        dynamic_cast<const CedeExpr *>(expr->Args[i].get());
    const auto *unaryArg =
        dynamic_cast<const UnaryExpr *>(expr->Args[i].get());
    const Expr *source = cededArg ? cededArg->Value.get()
                                  : ((unaryArg && unaryArg->Op == TokenType::Ampersand) ? unaryArg->RHS.get() : nullptr);
    while (auto *cast = dynamic_cast<const CastExpr *>(source))
      source = cast->Expression.get();
    bool isCaptured = false;
    size_t targetArgIdx = isStatic ? i : (i + 1);
    size_t llvmArgIdx = targetArgIdx + (isSRet ? 1 : 0);
    if (fd && targetArgIdx < fd->Args.size() &&
        fd->Args[targetArgIdx].IsCeded &&
        !hasValidatedCallTransferElaboration(expr->Args[i].get()) &&
        isCallTransferSourcePlace(expr->Args[i].get()) &&
        typeCarriesCleanupLiability(expr->Args[i]->ResolvedType)) {
      error(expr->Args[i].get(),
            DiagID::ERR_CODEGEN_MISSING_CALL_TRANSFER_ELABORATION,
            std::to_string(i + 1), expr->Method);
      return nullptr;
    }
    // Arg i maps to fd->Args[targetArgIdx]
    if (fd && targetArgIdx < fd->Args.size()) {
      const auto &arg = fd->Args[targetArgIdx];
      bool argIsPointerLike = arg.IsRawPointer || arg.IsReference ||
                              arg.IsUnique || arg.IsShared;
      if (arg.ResolvedType) {
        argIsPointerLike = argIsPointerLike || arg.ResolvedType->isPointer() ||
                           arg.ResolvedType->isReference();
      }
      const bool consumesUnique =
          arg.IsCeded &&
          (arg.IsUnique ||
           (arg.ResolvedType && arg.ResolvedType->isUniquePtr()));

      // [NEW] Lifetime dependencies check
      for (const auto &dep : fd->LifeDependencies) {
        if (dep == arg.Name && !argIsPointerLike) {
          isCaptured = true;
          break;
        }
      }

      // Capture by Struct/Array value types, or explicit Mutable Value types
      if (!isCaptured && !arg.IsRawPointer && !arg.IsReference &&
          !consumesUnique) {
        if (arg.IsValueMutable) {
          isCaptured = true;
        } else {
          llvm::Type *logicalTy = resolveType(arg.Type, false);
          if (logicalTy && (logicalTy->isStructTy() || logicalTy->isArrayTy()))
            isCaptured = true;
        }
      }

      // [Fix] Unique/Shared/Rebindable/Reference Pointers MUST be passed by Reference (Capture)
      if (arg.IsReference || (arg.IsUnique && !consumesUnique) ||
          arg.IsShared || arg.IsRebindable) {
        isCaptured = true;
      }
    }

    llvm::Value *argVal = nullptr;
    if (isCaptured) {
      // Captured values are passed through their storage slot. In particular,
      // a ceded local must not be materialized into an aggregate temporary:
      // the callee receives the source storage and the caller's drop
      // obligation is discharged below.
      if (auto *var = dynamic_cast<const VariableExpr *>(source)) {
        const std::string baseName =
            Type::stripMorphology(var->codegenName());
        const auto symbol = m_Symbols.find(baseName);
        if (symbol != m_Symbols.end() &&
            (symbol->second.mode == AddressingMode::Reference ||
             (symbol->second.mode == AddressingMode::Pointer &&
              symbol->second.morphology == Morphology::None))) {
          argVal = getEntityAddr(var->codegenName());
        } else {
          argVal = getIdentityAddr(var->codegenName());
        }
      } else if (dynamic_cast<const MemberExpr *>(source) ||
                 dynamic_cast<const ArrayIndexExpr *>(source)) {
        argVal = genAddr(source);
      }
      if (!argVal)
        argVal = genAddr(expr->Args[i].get());
      if (!argVal) {
        // R-Value fallback
        llvm::Value *rval = genExpr(expr->Args[i].get()).load(m_Builder);
        if (!rval)
          return nullptr;

        // Don't double-alloc if already pointer?
        // If expects mutable (Pointer) and we have Pointer R-value allow it?
        // Usually IsMutable expects L-Value.
        // For safety, store R-value in temp.
        llvm::AllocaInst *tmp = createEntryBlockAlloca(rval->getType());
        m_Builder.CreateStore(rval, tmp);
        argVal = tmp;
        const bool transfersOwnership =
            fd && targetArgIdx < fd->Args.size() &&
            fd->Args[targetArgIdx].IsCeded;
        if (!transfersOwnership && expr->Args[i]->ResolvedType) {
          registerFullExpressionTemporary(tmp,
                                          expr->Args[i]->ResolvedType);
        }
      }
    } else {
      argVal = genExpr(expr->Args[i].get()).load(m_Builder);

      // Implicit By-Ref Fix for Method Arguments
      if (argVal && callee->arg_size() > llvmArgIdx) {
        llvm::Type *paramTy = callee->getFunctionType()->getParamType(llvmArgIdx);
        if (paramTy->isPointerTy() && argVal->getType()->isStructTy()) {
          llvm::AllocaInst *tmp = createEntryBlockAlloca(
              argVal->getType(), nullptr, "arg_byref_tmp");
          m_Builder.CreateStore(argVal, tmp);
          argVal = tmp;
          const bool transfersOwnership =
              fd && targetArgIdx < fd->Args.size() &&
              fd->Args[targetArgIdx].IsCeded;
          if (!transfersOwnership && expr->Args[i]->ResolvedType) {
            registerFullExpressionTemporary(tmp,
                                            expr->Args[i]->ResolvedType);
          }
        }
      }
    }

    if (!argVal)
      return nullptr;

    if (cededArg && isCaptured) {
      if (auto *var = dynamic_cast<const VariableExpr *>(source))
        suppressDropForMove(var->Name);
      else if (auto *member = dynamic_cast<const MemberExpr *>(source))
        suppressDropForPartialMove(member);
      else if (auto *index = dynamic_cast<const ArrayIndexExpr *>(source))
        suppressDropForPartialMove(index);
    }

    // [NEW] Fat Pointer Synthesis for Strings in Method Calls
    // Method argument types might not be found in local m_Functions if defined in another module.
    // However, Sema updates the argument's ResolvedType to the expected shape (e.g. str) if it allowed an implicit cast!
    if (expr->Args[i]->ResolvedType && expr->Args[i]->ResolvedType->isShape()) {
        auto shpArg = std::static_pointer_cast<toka::ShapeType>(expr->Args[i]->ResolvedType);
        if (shpArg->Name == "str") {
            auto *strExpr = dynamic_cast<const StringExpr *>(expr->Args[i].get());
            if (strExpr) {
                // We must synthesize { i8*, i64 } for literal
                size_t literalLen = strExpr->Value.size();
                llvm::StructType *viewStrTy = m_StructTypes["str"];
                if (viewStrTy) {
                    llvm::Value *alloca = createEntryBlockAlloca(viewStrTy, nullptr, "str_synth");
                    llvm::Value *bufPtr = m_Builder.CreateStructGEP(viewStrTy, alloca, 0, "buf_ptr");
                    llvm::Value *lenPtr = m_Builder.CreateStructGEP(viewStrTy, alloca, 1, "len_ptr");
                    // We must generate the raw i8* pointer directly
                    llvm::Value *rawStr = genExpr(expr->Args[i].get()).load(m_Builder);
                    m_Builder.CreateStore(rawStr, bufPtr);
                    m_Builder.CreateStore(llvm::ConstantInt::get(getIntPtrTy(), literalLen), lenPtr);
                    if (isCaptured) {
                        argVal = alloca;
                    } else {
                        argVal = m_Builder.CreateLoad(viewStrTy, alloca);
                    }
                }
            } else if (argVal->getType()->isPointerTy() && !argVal->getType()->isStructTy()) {
                 // Wait, opaque pointers are just PTR. If we reached here but it wasn't a StringExpr.
                 // We don't abort unless we are SURE it was a naked CString.
            }
        }
    }
    // Auto-cast for primitives
    // ... (Existing cast logic could be added here if needed) ...

    args.push_back(argVal);
  }

  llvm::Value *sretAlloc = nullptr;
  if (isSRet) {
      llvm::Type *retLLVMTy = getLLVMType(expr->ResolvedType);
      sretAlloc = createEntryBlockAlloca(retLLVMTy, nullptr, "sret.tmp");
      args.insert(args.begin(), sretAlloc);
  }

  llvm::CallInst *ci = m_Builder.CreateCall(callee, args);

  if (!isStatic && fd && !fd->Args.empty() && fd->Args[0].IsCeded) {
    auto releaseTransferredUniqueHeapSlot = [&]() {
      llvm::Function *freeFn = m_Module->getFunction("free");
      if (!freeFn) {
        freeFn = llvm::Function::Create(
            llvm::FunctionType::get(m_Builder.getVoidTy(),
                                    {m_Builder.getPtrTy()}, false),
            llvm::Function::ExternalLinkage, "free", m_Module.get());
      }
      m_Builder.CreateCall(
          freeFn,
          m_Builder.CreateBitCast(finalObjVal, m_Builder.getPtrTy()));
    };

    const Expr *movedObject = expr->Object.get();
    while (auto *postfix = dynamic_cast<const PostfixExpr *>(movedObject))
      movedObject = postfix->LHS.get();

    if (auto *var = dynamic_cast<const VariableExpr *>(movedObject)) {
      suppressDropForMove(var->Name);

      // A consuming method declared as `cede self` receives a direct value
      // payload.  For a unique receiver, the call/factory has copied that
      // payload into callee-owned storage, so the source handle's heap slot
      // must be released here after the call.  The callee owns the payload
      // destructor; freeing here releases only the now-empty allocation.
      const std::string receiverName =
          Type::stripMorphology(var->codegenName());
      const auto receiver = m_Symbols.find(receiverName);
      const bool receiverIsUnique =
          (var->ResolvedType && var->ResolvedType->isUniquePtr()) ||
          (receiver != m_Symbols.end() &&
           receiver->second.morphology == Morphology::Unique);
      if (receiverIsUnique && selfReceivesPayloadByValue &&
          finalObjVal->getType()->isPointerTy()) {
        releaseTransferredUniqueHeapSlot();
      }
    } else if (auto *member = dynamic_cast<const MemberExpr *>(movedObject)) {
      bool memberIsUnique = member->ResolvedType &&
                            member->ResolvedType->isUniquePtr();
      if (!memberIsUnique && member->Object->ResolvedType) {
        const std::string ownerSoul =
            member->Object->ResolvedType->getSoulName();
        auto shape = m_Shapes.find(ownerSoul);
        if (shape != m_Shapes.end()) {
          for (const auto &field : shape->second->Members) {
            if (stripMemberAccessMarkers(field.Name) ==
                stripMemberAccessMarkers(member->Member)) {
              memberIsUnique = field.IsUnique;
              break;
            }
          }
        }
      }

      bool hasTrackedDirectField = false;
      for (int i = static_cast<int>(m_ScopeStack.size()) - 1; i >= 0; --i) {
        for (const auto &entry : m_ScopeStack[i]) {
          if (entry.DropMask && getDirectMemberDropIndex(entry, member) >= 0) {
            hasTrackedDirectField = true;
            break;
          }
        }
        if (hasTrackedDirectField)
          break;
      }

      if (hasTrackedDirectField) {
        suppressDropForPartialMove(member);
        if (memberIsUnique && selfReceivesPayloadByValue &&
            finalObjVal->getType()->isPointerTy()) {
          releaseTransferredUniqueHeapSlot();
        }
      }
    } else if (dynamic_cast<const NewExpr *>(movedObject) ||
               dynamic_cast<const AllocExpr *>(movedObject) ||
               dynamic_cast<const CallExpr *>(movedObject) ||
               dynamic_cast<const MethodCallExpr *>(movedObject)) {
      // A fresh `new T(...)#.consume()` has no named source binding whose
      // scope cleanup can release the allocation. A call returning ^T has the
      // same caller-owned shell. The by-value receiver ABI has already copied
      // its payload into callee-owned storage, so release only that shell.
      if (movedObject->ResolvedType && movedObject->ResolvedType->isUniquePtr() &&
          selfReceivesPayloadByValue && finalObjVal->getType()->isPointerTy()) {
        releaseTransferredUniqueHeapSlot();
      }
    }
  }

  if (isSRet) {
      ci->addParamAttr(0, llvm::Attribute::get(m_Context, llvm::Attribute::StructRet, getLLVMType(expr->ResolvedType)));
      return PhysEntity(sretAlloc, expr->ResolvedType->getSoulName(), getLLVMType(expr->ResolvedType), true);
  }

  // Async methods, like ordinary async calls, return a raw TCB pointer from
  // their coroutine factory.  The language expression is a TaskHandle<T>;
  // preserving that wrapper is required before `.start`/`.await` can extract
  // the TCB.  Returning the raw pointer here made method-produced tasks be
  // interpreted as a pointer to TaskHandle storage.
  if (isMethodAsync) {
    if (!expr->ResolvedType) {
      error(expr, DiagID::ERR_CODEGEN_INTERNAL_CODEGEN_ERROR_ASYNC_CALL_MISS);
      return nullptr;
    }
    const std::string taskName = expr->ResolvedType->toString();
    llvm::Type *handleTy = m_StructTypes[taskName];
    if (!handleTy) {
      error(expr, DiagID::ERR_CODEGEN_INTERNAL_CODEGEN_ERROR_ASYNC_CALL_MISS);
      return nullptr;
    }
    llvm::Value *handle = llvm::UndefValue::get(handleTy);
    handle = m_Builder.CreateInsertValue(handle, ci, 0, "task.wrap");
    return PhysEntity(handle, taskName, handleTy, false);
  }

  // Ordinary methods use the same void ABI for Unit as ordinary functions,
  // but a Unit method call can still be bound or stored in source code.
  if (expr->ResolvedType && expr->ResolvedType->isUnit() &&
      ci->getType()->isVoidTy()) {
    llvm::Type *unitTy = getLLVMType(expr->ResolvedType);
    return PhysEntity(llvm::Constant::getNullValue(unitTy),
                      expr->ResolvedType->toString(), unitTy, false);
  }

  std::string retTypeName = "";
  if (fd)
    retTypeName = fd->ReturnType;

  return PhysEntity(ci, retTypeName, ci->getType(), false);
}

void CodeGen::fillSymbolMetadata(TokaSymbol &sym, const std::string &typeStr,
                                 bool hasPointer, bool isUnique, bool isShared,
                                 bool isReference, bool isMutable,
                                 llvm::Type *allocaElemTy) {
  sym.indirectionLevel = 0;
  sym.typeName =
      typeStr; // [Fix] Store original type string for legacy/dynamic logic
  std::string ts = typeStr;

  // 1. Peel recursive indirection prefixes
  while (!ts.empty() && (ts[0] == '*' || ts[0] == '^' || ts[0] == '~' || ts[0] == '&')) {
    sym.indirectionLevel++;
    ts = ts.substr(1);
  }

  // 2. Determine Addressing Mode
  if (isReference) {
    sym.mode = AddressingMode::Reference;
    if (sym.indirectionLevel == 0)
      sym.indirectionLevel = 1;
  } else if (hasPointer || isUnique || isShared || sym.indirectionLevel > 0) {
    sym.mode = AddressingMode::Pointer;
    if (sym.indirectionLevel == 0)
      sym.indirectionLevel = 1;
  } else {
    sym.mode = AddressingMode::Direct;
  }

  // 3. Extract Elemental Soul Type (the 'Meat')
  if (sym.soulTypeObj) {
    auto soulObj = sym.soulTypeObj->getSoulType();
    sym.soulType = getLLVMType(soulObj);
  } else {
    sym.soulType = resolveType(ts, false);
    if (!sym.soulType) {
      sym.soulType = allocaElemTy;
    } else if (sym.soulType->isPointerTy() &&
               (isReference || hasPointer || isUnique || isShared)) {
      auto resolvedTypeObj = toka::Type::fromString(ts);
      if (resolvedTypeObj) {
        sym.soulType = getLLVMType(resolvedTypeObj->getSoulType());
      }
    }
  }

  // 4. Morphology (Ownership/Cleanup)
  if (isUnique)
    sym.morphology = Morphology::Unique;
  else if (isShared)
    sym.morphology = Morphology::Shared;
  else if (hasPointer)
    sym.morphology = Morphology::Raw;
  else
    sym.morphology = Morphology::None;

  // 5. Semantic flags
  sym.isMutable = isMutable;
  // Note: isRebindable is usually set separately based on '#' token presence
  // but it's often linked to morphology in declarations.
}

llvm::Type *CodeGen::resolveType(const std::string &baseType, bool hasPointer) {
  llvm::Type *type = nullptr;
  if (baseType.empty())
    return nullptr;

  if (baseType.size() > 4 && baseType.substr(0, 4) == "nul ") {
    return resolveType(baseType.substr(4), hasPointer);
  }

  if (baseType == "Self") {
    if (m_CurrentSelfType.empty()) {
      // Should not happen if Parser checks context, but for safety in CodeGen
      return nullptr;
    }
    return resolveType(m_CurrentSelfType, hasPointer);
  }

  // Intercept primitive pointer types before aliases
  if (baseType == "null") {
    type = llvm::PointerType::getUnqual(m_Context);
    if (hasPointer) type = llvm::PointerType::getUnqual(m_Context);
    return type;
  }

  // Check aliases first
  if (m_TypeAliases.count(baseType)) {
    llvm::Type *aliasType = getLLVMType(m_TypeAliases[baseType]);
    return hasPointer && aliasType
               ? llvm::PointerType::getUnqual(m_Context)
               : aliasType;
  }

  // Handle 'shape' keyword (e.g. shape(u8, u8))
  if (baseType.size() > 5 && baseType.substr(0, 5) == "shape") {
    return resolveType(baseType.substr(5), hasPointer);
  }

  // Handle Dynamic Traits (dyn @Trait) vs Dynamic Functions (dyn fn)
  if (baseType.size() >= 4 && baseType.substr(0, 3) == "dyn") {
    if (baseType.size() >= 6 && baseType.substr(0, 6) == "dyn fn") {
        llvm::Type *voidPtr = llvm::PointerType::getUnqual(m_Context);
        return llvm::StructType::get(m_Context, {voidPtr, voidPtr, voidPtr});
    }
    // Fat Pointer: { void* data, void* vtable }
    llvm::Type *voidPtr =
        llvm::PointerType::getUnqual(m_Context);
    return llvm::StructType::get(m_Context, {voidPtr, voidPtr});
  }

  // Handle Shared Pointers (~Type): { T*, i32* }
  if (baseType.size() > 1 && baseType[0] == '~') {
    llvm::Type *elemTy = resolveType(baseType.substr(1), false);
    llvm::Type *ptrTy = llvm::PointerType::getUnqual(m_Context);
    llvm::Type *refCountTy =
        llvm::PointerType::getUnqual(m_Context);
    return llvm::StructType::get(m_Context, {ptrTy, refCountTy});
  }

  // Handle raw pointer types (e.g. *i32, *void) AND managed
  // pointers (^Type)
  if (baseType.size() > 1 && (baseType[0] == '*' || baseType[0] == '^' ||
                              baseType[0] == '~' || baseType[0] == '&')) {
    size_t offset = 1;
    if (offset < baseType.size()) {
      char next = baseType[offset];
      if (next == '#' || next == '?' || next == '!') {
        offset++;
      }
    }
    llvm::Type *elemTy = resolveType(baseType.substr(offset), false);
    if (!elemTy)
      return nullptr;
    return llvm::PointerType::getUnqual(m_Context);
  }

  if (baseType[0] == '[') {
    // Array: [T; N]
    size_t lastSemi = baseType.find_last_of(';');
    if (lastSemi != std::string::npos) {
      std::string elemTyStr = baseType.substr(1, lastSemi - 1);
      std::string countStr =
          baseType.substr(lastSemi + 1, baseType.size() - lastSemi - 2);
      llvm::Type *elemTy = resolveType(elemTyStr, false);
      if (!elemTy)
        return nullptr;
      uint64_t count = 0;
      try {
        count = std::stoull(countStr);
      } catch (...) {
        std::string funcCtxt = "Unknown";
        if (auto *BB = m_Builder.GetInsertBlock()) {
          if (auto *F = BB->getParent()) {
            funcCtxt = F->getName().str();
          }
        }
        std::cerr << "CodeGen Error: Invalid array size '" << countStr
                  << "' in type '" << baseType << "' (Function: " << funcCtxt
                  << ")\n";
        // Attempt fallback? No, crash is better than silent fail for now, but
        // explicit msg helps.
        exit(1);
      }
      type = llvm::ArrayType::get(elemTy, count);
    }
  } else if (baseType == "bool" || baseType == "i1")
    type = llvm::Type::getInt1Ty(m_Context);
  else if (baseType == "i8" || baseType == "u8" || baseType == "char" ||
           baseType == "byte")
    type = llvm::Type::getInt8Ty(m_Context);
  else if (baseType == "i16" || baseType == "u16")
    type = llvm::Type::getInt16Ty(m_Context);
  else if (baseType == "i32" || baseType == "u32" || baseType == "int")
    type = llvm::Type::getInt32Ty(m_Context);
  else if (baseType == "i64" || baseType == "u64" || baseType == "long")
    type = llvm::Type::getInt64Ty(m_Context);
  else if (baseType == "usize" || baseType == "isize")
    type = getIntPtrTy();
  else if (baseType == "f32" || baseType == "float")
    type = llvm::Type::getFloatTy(m_Context);
  else if (baseType == "f64" || baseType == "double")
    type = llvm::Type::getDoubleTy(m_Context);
  else if (baseType == "cstring")
    type = llvm::PointerType::getUnqual(m_Context);
  else if (baseType == "void")
    type = llvm::Type::getVoidTy(m_Context);
  else if (baseType == "ptr")
    type = llvm::PointerType::getUnqual(m_Context);
  else {
    std::string actualType = baseType;
    while (!actualType.empty() && (actualType.back() == '#' || actualType.back() == '?' || actualType.back() == '!')) {
      actualType.pop_back();
    }
    if (!actualType.empty() && actualType.front() == '(' && actualType.back() == ')') {
      if (m_ParenthesizedRecordTypes.count(actualType)) {
        auto resolved = m_ParenthesizedRecordTypes[actualType];
        if (resolved && resolved->typeKind == Type::Shape) {
          actualType = std::static_pointer_cast<ShapeType>(resolved)->Name;
        }
      }
    }
    if (m_StructTypes.count(actualType)) {
      type = m_StructTypes[actualType];
    } else if (m_Shapes.count(actualType)) {
      const ShapeDecl *shape = m_Shapes[actualType];
      genShape(shape);
      const std::string codegenName =
          shape->CodegenName.empty() ? shape->Name : shape->CodegenName;
      if (m_StructTypes.count(codegenName))
        type = m_StructTypes[codegenName];
    } else if (baseType == "unknown") {
      return nullptr;
    } else {
      return nullptr;
    }
  }

  if (hasPointer && type)
    return llvm::PointerType::getUnqual(m_Context);
  return type;
}

llvm::Type *CodeGen::getLLVMType(std::shared_ptr<Type> type) {
  if (!type) {
    return llvm::Type::getVoidTy(m_Context);
  }

  auto issue = Type::findHandleGrammarIssueRecursive(type);
  if (issue.has_value() && !issue->isValid()) {
    DiagnosticEngine::report(m_CurrentFunction ? m_CurrentFunction->Loc : SourceLocation(),
                             DiagID::ERR_CODEGEN_ILLEGAL_HANDLE_GRAMMAR,
                             type->toString());
    return nullptr;
  }

  if (handleGrammarAuditEnabled()) {
    std::string fnId = m_CurrentFunction ? (!m_CurrentFunction->CodegenName.empty() ? m_CurrentFunction->CodegenName : m_CurrentFunction->Name) : "";
    markHandleGrammarTypeLowered(type, fnId);
  }

  // A miss outcome is a real returned value with two states.  Keep its
  // representation independent from raw may-zero pointers and Option: field 0 is
  // the hit discriminator and field 1 is payload storage.  The payload is
  // read or dropped only when the discriminator is true.
  if (auto outcome = std::dynamic_pointer_cast<MissOutcomeType>(type)) {
    llvm::Type *payload = getLLVMType(outcome->PayloadType);
    if (!payload || payload->isVoidTy())
      return nullptr;
    return llvm::StructType::get(
        m_Context, {llvm::Type::getInt1Ty(m_Context), payload});
  }

  // PlaceOutcome is a compiler-only logical {Hit, element-place} carrier.
  // Its Item morphology is semantic metadata; the physical payload is one
  // pointer-width address and never ReferenceType<Item> storage.
  if (type->isPlaceOutcome()) {
    return llvm::StructType::get(
        m_Context, {llvm::Type::getInt8Ty(m_Context), getIntPtrTy()});
  }

  // Handle Primitives
  if (type->typeKind == Type::Primitive) {
    auto prim = std::static_pointer_cast<PrimitiveType>(type);
    if (prim->Name == "i32" || prim->Name == "u32" || prim->Name == "int")
      return llvm::Type::getInt32Ty(m_Context);
    if (prim->Name == "i64" || prim->Name == "u64" || prim->Name == "long")
      return llvm::Type::getInt64Ty(m_Context);
    if (prim->Name == "usize" || prim->Name == "isize" ||
        prim->Name == "Addr" || prim->Name == "OAddr")
      return getIntPtrTy();
    if (prim->Name == "i8" || prim->Name == "u8" || prim->Name == "byte" ||
        prim->Name == "char")
      return llvm::Type::getInt8Ty(m_Context);
    if (prim->Name == "i16" || prim->Name == "u16")
      return llvm::Type::getInt16Ty(m_Context);
    if (prim->Name == "bool" || prim->Name == "i1")
      return llvm::Type::getInt1Ty(m_Context); // i1
    if (prim->Name == "f32" || prim->Name == "float")
      return llvm::Type::getFloatTy(m_Context);
    if (prim->Name == "f64" || prim->Name == "double")
      return llvm::Type::getDoubleTy(m_Context);
    if (prim->Name == "void")
      return llvm::Type::getVoidTy(m_Context);
    if (prim->Name == "cstring")
      return llvm::PointerType::getUnqual(m_Context);
    if (prim->Name == "null")
      return llvm::PointerType::getUnqual(m_Context);
  }

  // Unit has one ordinary storage representation.  Functions with a Unit
  // result select LLVM void separately in genFunction above.
  if (type->typeKind == Type::Unit) {
    return llvm::Type::getInt8Ty(m_Context);
  }

  // ABI void and bottom have no LLVM value representation.
  if (type->typeKind == Type::Void) {
    return llvm::Type::getVoidTy(m_Context);
  }
  if (type->typeKind == Type::Never) {
    return llvm::Type::getVoidTy(m_Context);
  }

  // Handle Pointers (Raw, Unique, Reference) -> Map to LLVM Pointer (or Fat Pointer Struct)
  // Note: Reference in Toka is a pointer in LLVM.
  // Note: Unique is a pointer in LLVM.
  if (type->typeKind == Type::RawPtr || type->typeKind == Type::UniquePtr ||
      type->typeKind == Type::Reference) {

    auto ptrType = std::static_pointer_cast<PointerType>(type);

    if (type->isFatPointer()) {
      // [Fix: Mutually Recursive Generics]
      // DO NOT eagerly evaluate PointeeType layout since LLVM 15+ opaque pointers do not need it!
      // This trivially breaks the cyclic layout sizing dependency.
      llvm::Type *ptrTy = llvm::PointerType::getUnqual(m_Context);
      return llvm::StructType::get(m_Context, {ptrTy, getIntPtrTy()});
    }

    return llvm::PointerType::getUnqual(m_Context);
  }

  // Handle Shared Pointer (~T) -> { T*, cb* }
  if (type->typeKind == Type::SharedPtr) {
    llvm::Type *ptrTy = llvm::PointerType::getUnqual(m_Context);
    llvm::Type *refCountTy = llvm::PointerType::getUnqual(m_Context);
    return llvm::StructType::get(m_Context, {ptrTy, refCountTy});
  }

  // Handle Uninit Wrapper (Delegates layout to inner type)
  if (type->typeKind == Type::UninitWrapper) {
    auto uninit = std::static_pointer_cast<UninitType>(type);
    return getLLVMType(uninit->InnerType);
  }

  // Handle Slices ([T])
  // A Slice is a dynamically sized memory region. For LLVM GEP stepping logic,
  // its apparent layout type must be the Element Type.
  if (type->typeKind == Type::Slice) {
    auto sliceType = std::static_pointer_cast<SliceType>(type);
    llvm::Type *elemTy = getLLVMType(sliceType->ElementType);
    if (!elemTy)
      elemTy = llvm::Type::getInt8Ty(m_Context);
    return elemTy;
  }

  // Handle Arrays ([T; N])
  if (type->typeKind == Type::Array) {
    auto arrType = std::static_pointer_cast<ArrayType>(type);
    llvm::Type *elemTy = getLLVMType(arrType->ElementType);
    if (!elemTy)
      elemTy = llvm::Type::getInt8Ty(m_Context);
    return llvm::ArrayType::get(elemTy, arrType->Size);
  }

  // Handle Shapes (Structs) via Name Lookup
  if (type->typeKind == Type::Shape) {
    auto shapeType = std::static_pointer_cast<ShapeType>(type);
    if (shapeType->Decl) {
      auto declIt = m_StructTypesByDecl.find(shapeType->Decl);
      if (declIt != m_StructTypesByDecl.end())
        return declIt->second;

      genShape(shapeType->Decl);
      declIt = m_StructTypesByDecl.find(shapeType->Decl);
      if (declIt != m_StructTypesByDecl.end())
        return declIt->second;
    }

    std::string shapeName = shapeType->Name;
    if (shapeType->Decl && !shapeType->Decl->CodegenName.empty())
      shapeName = shapeType->Decl->CodegenName;
    if (!shapeName.empty() && shapeName.front() == '(' && shapeName.back() == ')') {
      if (m_ParenthesizedRecordTypes.count(shapeName)) {
        auto resolved = m_ParenthesizedRecordTypes[shapeName];
        if (resolved && resolved->typeKind == Type::Shape) {
          shapeName = std::static_pointer_cast<ShapeType>(resolved)->Name;
        }
      }
    }
    if (m_StructTypes.count(shapeName)) {
      return m_StructTypes[shapeName];
    }
    // [Fix] On-Demand Generation for Synthetic Shapes (Dependencies)
    if (m_Shapes.count(shapeName)) {
      genShape(m_Shapes[shapeName]);
      if (m_StructTypes.count(shapeName))
        return m_StructTypes[shapeName];
    }
    // If generic lookup fails, try resolving by name (backup)
    return resolveType(shapeName, false);
  }

  // Handle Function Types (closures)
  if (type->typeKind == Type::Function) {
    llvm::Type *voidPtr = llvm::PointerType::getUnqual(m_Context);
    return llvm::StructType::get(m_Context, {voidPtr, voidPtr}); // { env, fptr }
  } else if (type->typeKind == Type::DynFn) {
    llvm::Type *voidPtr = llvm::PointerType::getUnqual(m_Context);
    return llvm::StructType::get(m_Context, {voidPtr, voidPtr, voidPtr}); // { env, fptr, dropPtr }
  }

  // Fallback to string based resolution if we have an Unresolved type
  // wrapping a string
  if (type->typeKind == Type::Unresolved) {
    auto unresolved = std::static_pointer_cast<UnresolvedType>(type);
    return resolveType(unresolved->Name, false);
  }

  // Default Void
  return llvm::Type::getVoidTy(m_Context);
}

void CodeGen::fillSymbolMetadata(TokaSymbol &sym, std::shared_ptr<Type> typeObj,
                                 llvm::Type *allocaElemTy) {
  if (!typeObj)
    return;

  // 1. Indirection and Core Type Logic is now driven by TypeObj structure
  sym.soulTypeObj = typeObj;

  // Determine Morphology and Indirection based on Type Kind
  sym.indirectionLevel = 0;
  sym.morphology = Morphology::None;
  sym.mode = AddressingMode::Direct;

  std::shared_ptr<Type> current = typeObj;

  // Unseal wrappers to find "Soul"
  // We loop to peel of layers if needed, or just switch on the top layer
  // logic. But TokaSymbol logic expects 'soulType' to be the underlying data
  // type.

  // Handle recursive peeling for indirection levels
  bool firstLayer = true;
  while (current && (current->isReference() || current->isPointer() || current->isSmartPointer())) {
    if (current->isReference()) {
      if (firstLayer) {
        sym.mode = AddressingMode::Reference;
        sym.morphology = Morphology::None;
      }
      sym.indirectionLevel++;
      current = std::static_pointer_cast<PointerType>(current)->PointeeType;
    } else if (current->typeKind == Type::UniquePtr) {
      if (firstLayer) {
        sym.mode = AddressingMode::Pointer;
        sym.morphology = Morphology::Unique;
      }
      sym.indirectionLevel++;
      current = std::static_pointer_cast<PointerType>(current)->PointeeType;
    } else if (current->typeKind == Type::SharedPtr) {
      if (firstLayer) {
        sym.mode = AddressingMode::Pointer;
        sym.morphology = Morphology::Shared;
      }
      sym.indirectionLevel++;
      current = std::static_pointer_cast<SharedPointerType>(current)->PointeeType;
    } else if (current->typeKind == Type::RawPtr) {
      if (firstLayer) {
        sym.mode = AddressingMode::Pointer;
        sym.morphology = Morphology::Raw;
      }
      sym.indirectionLevel++;
      current = std::static_pointer_cast<PointerType>(current)->PointeeType;
    } else {
      break;
    }
    firstLayer = false;
  }

  if (firstLayer) {
    // Direct Value
    sym.mode = AddressingMode::Direct;
    sym.morphology = Morphology::None;
  }

  sym.soulType = getLLVMType(current);

  if (!sym.soulType)
    sym.soulType = allocaElemTy;

  if (typeObj) {
      // std::cout << " soulTypePtr=" << sym.soulType << "\\n";
  }

  // Attributes
  sym.isMutable = typeObj->IsWritable;

  // Arrays are continuous
  if (current && current->isArray()) {
    sym.isContinuous = true;
  } else {
    sym.isContinuous = false;
  }

  // Rebindable? usually strict to the variable decl itself, not the type
  // always, but we can check if the top level pointer was rebindable if we
  // stored it in Type? Currently Type has IsWritable. IsRebindable is
  // typically a property of the binding, not the type (like `mut` binding in
  // Rust). So we'll leave sym.isRebindable to be set by the caller
  // (Declaration).

  // Drop logic placeholder (caller should refine if needed)
  sym.hasDrop = false;
  sym.dropFunc = "";
}

llvm::Function *CodeGen::getOrCreateGlobalInit() {
  if (m_GlobalInitFunc)
    return m_GlobalInitFunc;
  llvm::FunctionType *ft =
      llvm::FunctionType::get(llvm::Type::getVoidTy(m_Context), false);
  m_GlobalInitFunc = llvm::Function::Create(
      ft, llvm::Function::InternalLinkage, "__toka_global_init", m_Module.get());
  llvm::BasicBlock *bb =
      llvm::BasicBlock::Create(m_Context, "entry", m_GlobalInitFunc);
  m_GlobalInitBuilder = std::make_unique<llvm::IRBuilder<>>(bb);
  return m_GlobalInitFunc;
}

void CodeGen::finalizeGlobals() {
  if (m_GlobalInitFunc) {
    m_GlobalInitBuilder->CreateRetVoid();
    llvm::appendToGlobalCtors(*m_Module, m_GlobalInitFunc, 65535);
  }
}

} // namespace toka
