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
#pragma once

#include "toka/AST.h"
#include "toka/DiagnosticEngine.h"
#include "toka/Type.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/DIBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include <set>

namespace toka {
struct MemberAccessIntent;
class Type;
class ASTNode;
class Module;
class Stmt;
class Expr;
class FunctionDecl;
class ExternDecl;
class VariableDecl;
class ShapeDecl;
class ImplDecl;
class MethodCallExpr;

enum class AddressingMode {
  Direct,   // Stack allocated value (Box is Soul): i32, [5]i32
  Pointer,  // Pointer orbit (Address in Box): *i32, ^Point, ~Point
  Reference // Implicit alias: &v
};

enum class Morphology {
  None,   // Scalar/Value
  Raw,    // * (Manual)
  Unique, // ^ (Auto-Owned)
  Shared  // ~ (Ref-counted)
};

struct TokaSymbol {
  llvm::Value *allocaPtr = nullptr; // Identity (The stationary "Box" address)
  llvm::Type *soulType = nullptr;   // Soul Type (Explicit layout for LLVM 17 GEP/Load)
  AddressingMode mode = AddressingMode::Direct; // Path type
  Morphology morphology = Morphology::None; // Ownership/Cleanup logic
  int indirectionLevel = 0; // Depth (1 for *p, 2 for **p)
  bool isRebindable = false; // # on identity (Swappable address)
  // A rebindable non-shared parameter is captured as an alloca containing
  // the caller's handle-slot address.  Rebinding must store through that
  // address, never overwrite the callee's capture slot.
  bool isCallerHandleSlot = false;
  bool isMutable = false;    // # on entity (Writable data)
  bool isContinuous = false; // Sequence marker (alloc [N])
  bool isNullable = false;   // ?/! marker
  std::string typeName;   // Original type string (e.g. "dyn @Shape")
  std::shared_ptr<Type> soulTypeObj; // The new Type Object source of truth
  bool hasDrop = false;
  std::string dropFunc = "";
};

struct PhysEntity {
  llvm::Value *value = nullptr;
  std::string typeName;
  llvm::Type *irType = nullptr; // For Load instructions
  bool isAddress = false;

  PhysEntity(llvm::Value *v = nullptr) : value(v) {
    if (v)
      irType = v->getType();
  }
  PhysEntity(llvm::Value *v, std::string t, llvm::Type *it, bool addr)
      : value(v), typeName(t), irType(it), isAddress(addr) {
    if (!irType && value)
      irType = value->getType();
  }

  llvm::Value *load(llvm::IRBuilder<> &b) const {
    if (!value)
      return nullptr;
    if (isAddress && irType) {
      return b.CreateLoad(irType, value);
    }
    return value;
  }
};

class CodeGen {
public:
  CodeGen(llvm::LLVMContext &context, const std::string &moduleName)
      : m_Context(context), m_Builder(context) {
    m_Module = std::make_unique<llvm::Module>(moduleName, context);
  }

  void discover(const Module &ast);
  void resolveSignatures(const Module &ast);
  void generate(const Module &ast);
  void finalizeGlobals();
  void enableDebugInfo(const std::string &sourcePath, bool optimized);
  void finalizeDebugInfo();
  bool hasErrors() const { return m_ErrorCount > 0; }
  void print(llvm::raw_ostream &os);
  llvm::Module *getModule() { return m_Module.get(); }
  void importParenthesizedRecordTypes(const std::map<std::string, std::shared_ptr<toka::Type>>& recordTypes) {
    m_ParenthesizedRecordTypes = recordTypes;
  }
  llvm::IntegerType *getIntPtrTy();

private:
  int m_ErrorCount = 0;
  void markMemoryEvent(llvm::Instruction *instruction, const char *event);
  template <typename... Args>
  void error(const ASTNode *node, DiagID id, Args &&...args) {
    m_ErrorCount++;
    if (node) {
      DiagnosticEngine::report(node->Loc, id, std::forward<Args>(args)...);
    } else {
      DiagnosticEngine::report(SourceLocation{}, id, std::forward<Args>(args)...);
    }
  }
  llvm::LLVMContext &m_Context;
  llvm::IRBuilder<> m_Builder;
  std::unique_ptr<llvm::Module> m_Module;
  const Module *m_AST = nullptr;

  std::unique_ptr<llvm::DIBuilder> m_DebugBuilder;
  llvm::DICompileUnit *m_DebugCompileUnit = nullptr;
  llvm::DISubprogram *m_CurrentDebugScope = nullptr;
  std::map<std::string, llvm::DIFile *> m_DebugFiles;
  std::map<llvm::Type *, llvm::DIType *> m_DebugTypes;

  llvm::DIFile *getDebugFile(SourceLocation loc);
  llvm::DIType *getDebugType(llvm::Type *type);
  void beginDebugFunction(const FunctionDecl *func, llvm::Function *function,
                          const std::string &linkageName);
  void setDebugLocation(const ASTNode *node);
  void emitDebugVariable(const std::string &name, llvm::Value *storage,
                         llvm::Type *type, SourceLocation loc,
                         bool isParameter = false, unsigned argNo = 0);

  llvm::Function *m_GlobalInitFunc = nullptr;
  std::unique_ptr<llvm::IRBuilder<>> m_GlobalInitBuilder = nullptr;
  llvm::Function *getOrCreateGlobalInit();

  std::map<std::string, const FunctionDecl *> m_Functions;
  std::map<std::string, const ExternDecl *> m_Externs;
  std::map<std::string, TokaSymbol> m_Symbols;
  // Argument ABI adaptation can revisit a call-shaped receiver. Preserve the
  // single evaluation of every task activation expression in its LLVM function.
  std::map<std::pair<llvm::Function *, const MemberExpr *>, PhysEntity>
      m_TaskStartCache;
  std::string m_CurrentSelfType;
  std::map<std::string, llvm::Value *> m_NamedValues;
  bool m_InLHS = false;
  llvm::Value *m_CurrentCoroHandle = nullptr;
  llvm::Value *m_CurrentCoroPromise = nullptr;
  llvm::Value *m_CurrentCoroTCB = nullptr;
  llvm::Value *m_CurrentCoroId = nullptr;
  llvm::Type *m_CurrentCoroPromiseType = nullptr;
  llvm::Type *m_CurrentCoroRetTy = nullptr;
  llvm::BasicBlock *m_CurrentCoroSuspendRetBB = nullptr;
  llvm::BasicBlock *m_CurrentCoroCleanupBB = nullptr;
  llvm::BasicBlock *m_CurrentCoroFinalSuspendBB = nullptr;
  llvm::Value *m_CurrentSRetPtr = nullptr;
  llvm::Type *m_CurrentSRetTy = nullptr;
  bool shouldReturnSRet(std::shared_ptr<Type> retTypeObj);
  uint64_t estimateTypeSize(std::shared_ptr<Type> type, std::set<std::string> &visited);
  void genCoroutineReturn(llvm::Value *retVal);
  void genCoroutineCancelReturn();
  // Although m_ValueElementTypes is redundant with m_Symbols for Variables, it
  // might be used elsewhere. But m_StructTypes, m_Shapes, etc. are absolutely
  // required.
  std::map<std::string, llvm::StructType *> m_StructTypes;
  std::map<const ShapeDecl *, llvm::StructType *> m_StructTypesByDecl;
  std::map<llvm::StructType *, const ShapeDecl *> m_StructDeclsByType;
  std::map<std::string, std::vector<std::string>> m_StructFieldNames;
  std::map<std::string, std::shared_ptr<Type>> m_TypeAliases;
  std::map<std::string, const ShapeDecl *> m_Shapes;
  std::map<std::string, llvm::Function *> m_DropCascadeHelpers;
  std::set<std::string> m_ActiveDropCascadeTypes;
  std::set<std::string> m_DropCascadeHelperRootTypes;
  std::map<std::string, const TraitDecl *> m_Traits;
  std::map<llvm::Type *, std::string> m_TypeToName;
  std::map<std::string, std::shared_ptr<toka::Type>> m_ParenthesizedRecordTypes;

  struct CFInfo {
    std::string Label;
    llvm::BasicBlock *BreakTarget;
    llvm::BasicBlock *ContinueTarget;
    llvm::Value *ResultAddr; // Alloca for storing results
    size_t ScopeDepth;
  };
  std::vector<CFInfo> m_CFStack;

  struct VariableScopeInfo {
    std::string Name;
    llvm::Value *Alloca = nullptr;
    llvm::Type *AllocType = nullptr; // The type stored at Alloca (Soul or Envelope)
    bool IsUniquePointer = false;    // ^Type
    bool IsShared = false;           // ~Type
    bool HasDrop = false;
    std::string DropFunc;
    std::string SoulName; // [New] Correct type name for destructor lookup
    // Fixed arrays need the semantic element type for recursive cleanup; a
    // string soul name alone cannot describe their element layout.
    std::shared_ptr<Type> DropType;
    // Explicit `uninit` storage has identity but no constructed value.  This
    // flag is distinct from a resource's drop flag because scalar locals may
    // also need a runtime `place is uninit` observation.
    llvm::Value *InitFlag = nullptr;
    llvm::Value *DropFlag = nullptr;
    // Compiler-managed record values use this field-level liveness mask after
    // a direct member `cede`. Custom destructors retain whole-object control.
    PartialMovePlan PartialMove;
    llvm::Value *DropMask = nullptr;
  };
  std::vector<std::vector<VariableScopeInfo>> m_ScopeStack;

  void suppressDropForMove(const std::string &name);
  void markInitLive(const BinaryExpr *assignment);
  void markInitLive(const VariableExpr *place);
  void markInitState(const VariableExpr *place, llvm::Value *isLive);
  llvm::Value *getInitLiveFlag(const VariableExpr *place);
  void suppressDropForPartialMove(const MemberExpr *member);
  void suppressDropForPartialMove(const ArrayIndexExpr *index);
  void restoreDropForMemberAssignment(const MemberExpr *member);
  void restoreDropForIndexAssignment(const ArrayIndexExpr *index);
  int getDirectMemberDropIndex(const VariableScopeInfo &entry,
                               const MemberExpr *member) const;
  int getDirectArrayDropIndex(const VariableScopeInfo &entry,
                              const ArrayIndexExpr *index) const;

  // Assignment Strategy Dispatcher (Step 2)
  PhysEntity emitAssignment(const Expr *lhs, const Expr *rhs,
                            const BinaryExpr *assignmentSite);
  llvm::StoreInst *emitSoulAssignment(llvm::Value *soulAddr,
                                      llvm::Value *rhsVal,
                                      llvm::Type *type);
  void emitEnvelopeRebind(llvm::Value *handleAddr, llvm::Value *rhsVal,
                          const TokaSymbol &sym, const Expr *lhsExpr);
  llvm::Value *emitPromotion(llvm::Value *rawPtr, llvm::Type *targetHandleType,
                             const TokaSymbol &sym);
  void emitAcquire(llvm::Value *sharedHandle, std::shared_ptr<Type> pointeeType);
  void emitRelease(llvm::Value *sharedHandle, const TokaSymbol &sym, std::shared_ptr<Type> pointeeType);

  llvm::Type *resolveType(const std::string &baseType, bool hasPointer);
  std::shared_ptr<Type> lowerTypeSyntax(const TypeSyntaxPtr &syntax,
                                        const std::string &legacy) const;
  llvm::Type *getLLVMType(std::shared_ptr<Type> type);

  // --- Member Access CodeGen Refactoring Helpers ---
private:
  struct MemberObjectInfo;
  struct MemberFieldInfo;
  struct MemberFieldStorage;
  struct MemberHatPlan;
  struct MemberMaterialization;

  llvm::Value *peelNestedMemberBaseAddress(const Expr *object, llvm::Value *addr);
  MemberObjectInfo resolveMemberObject(const Expr *object, llvm::Value *addr);
  llvm::Value *peelDerefObjectToSoulAddress(const Expr *object, llvm::Value *addr);
  MemberFieldInfo resolveMemberField(llvm::StructType *structTy, const std::string &shapeName, int initialIndex, const std::string &name);
  MemberFieldStorage emitMemberFieldStorage(const ShapeDecl *memberShape, const ShapeDecl *namedShape, llvm::StructType *structTy, llvm::Value *objAddr, int idx, const std::string &memberName);
  llvm::Value *emitMemberHatOffLoads(llvm::Value *addr, const MemberHatPlan &plan);
  llvm::Type *resolveMemberResultType(llvm::Type *baseIrTy, const MemberHatPlan &plan, const std::shared_ptr<Type> &resolvedType);
  MemberMaterialization emitMaterializedMemberValue(llvm::Value *addr, llvm::Type *baseIrTy, const MemberHatPlan &plan, const std::shared_ptr<Type> &resolvedType);
  MemberHatPlan resolveMemberHatPlan(const ShapeDecl *namedShape, int idx, int accessHats, bool isIdentityAssertion, bool isIdentityOperator, const std::shared_ptr<Type> &resolvedType);

  // Deprecated/Legacy version
  void fillSymbolMetadata(TokaSymbol &sym, const std::string &typeStr,
                          bool hasPointer, bool isUnique, bool isShared,
                          bool isReference, bool isMutable, bool isNullable,
                          llvm::Type *allocaElemTy);

  // New version
  void fillSymbolMetadata(TokaSymbol &sym, std::shared_ptr<Type> typeObj,
                          llvm::Type *allocaElemTy);

  void executeScopeUnwinding(size_t targetDepth);
  PhysEntity genExpr(const Expr *expr);
  llvm::Constant *genConstant(const Expr *expr,
                              llvm::Type *targetType = nullptr);
  llvm::Value *genAddr(const Expr *expr);
  // llvm::Value *getVarAddr(const std::string &name);

  // Address Layering Protocol
  llvm::Value *getEntityAddr(const std::string &name);
  llvm::Value *getIdentityAddr(const std::string &name);
  llvm::Value *projectSoul(llvm::Value *handle, const TokaSymbol &sym);
  llvm::Value *emitEntityAddr(const Expr *expr); // "Soul" - actual data address
  llvm::Value *
  emitHandleAddr(const Expr *expr); // "Handle" - identity/sleeve (alloca)
  llvm::Value *wrapFreshAllocationAsNullableSoul(llvm::Value *payloadPtr,
                                                  llvm::StructType *soulType);

  llvm::Value *genStmt(const Stmt *stmt);
  llvm::Function *genFunction(const FunctionDecl *func,
                              const std::string &overrideName = "",
                              bool declOnly = false);
  void genAsyncMainEntrypoint(llvm::Function *asyncMain,
                              const FunctionDecl *func);
  void genGlobal(const Stmt *stmt);
  void genExtern(const ExternDecl *ext);
  void genShape(const ShapeDecl *sh);
  void genImpl(const ImplDecl *impl, bool declOnly = false);
  PhysEntity genMatchExpr(const MatchExpr *expr);
  PhysEntity genAwaitExpr(const AwaitExpr *E);
  PhysEntity genWaitExpr(const WaitExpr *waitExpr);
  PhysEntity genStartExpr(const StartExpr *E);
  PhysEntity genTaskStart(const Expr *E);
  PhysEntity genBinaryExpr(const BinaryExpr *expr);
  PhysEntity genAllocExpr(const AllocExpr *expr);
  PhysEntity genStaticMemberExpr(const MemberExpr *expr);
  PhysEntity genDynamicMemberExpr(const MemberExpr *expr);
  PhysEntity genMemberExpr(const MemberExpr *expr);
  PhysEntity genIndexExpr(const ArrayIndexExpr *expr);
  PhysEntity genVariableExpr(const VariableExpr *expr);
  PhysEntity genLiteralExpr(const Expr *expr);
  PhysEntity genArrayExpr(const ArrayExpr *expr);
  PhysEntity genRepeatedArrayExpr(const RepeatedArrayExpr *expr);
  PhysEntity genCastExpr(const CastExpr *expr);
  PhysEntity genUnaryExpr(const UnaryExpr *expr);
  PhysEntity genIfExpr(const IfExpr *expr,
                       llvm::AllocaInst *inheritedResultAddr = nullptr);
  PhysEntity genGuardExpr(const GuardExpr *expr);
  PhysEntity genLoopExpr(const LoopExpr *expr);
  PhysEntity genForExpr(const ForExpr *expr);
  void genPatternBinding(const MatchArm::Pattern *pat, llvm::Value *targetAddr,
                         llvm::Type *targetType, std::shared_ptr<Type> targetTypeObj = nullptr);
  PhysEntity genInitStructExpr(const InitStructExpr *expr);
  PhysEntity genAnonymousRecordExpr(const AnonymousRecordExpr *expr);
  PhysEntity genMethodCall(const MethodCallExpr *expr);
  PhysEntity genCallExpr(const CallExpr *expr);
  PhysEntity genPostfixExpr(const PostfixExpr *expr);
  PhysEntity genUnwrapPropagationExpr(const UnwrapPropagationExpr *expr);
  PhysEntity genPassExpr(const PassExpr *expr);
  PhysEntity genCedeExpr(const CedeExpr *expr);
  PhysEntity genBreakExpr(const BreakExpr *expr);
  PhysEntity genContinueExpr(const ContinueExpr *expr);
  PhysEntity genClosureExpr(const ClosureExpr *expr);
  PhysEntity genUnsafeExpr(const UnsafeExpr *expr);
  PhysEntity genArrayInitExpr(const ArrayInitExpr *expr);
  PhysEntity genNewExpr(const NewExpr *expr);
  PhysEntity genComptimeReflectExpr(const ComptimeReflectExpr *expr);
  llvm::Value *genReturnStmt(const ReturnStmt *stmt);
  llvm::Value *genBlockStmt(const BlockStmt *stmt);
  llvm::Value *genVariableDecl(const VariableDecl *stmt);
  llvm::Value *genDestructuringDecl(const DestructuringDecl *stmt);
  llvm::Value *genDeleteStmt(const DeleteStmt *stmt);
  llvm::Value *genFreeStmt(const FreeStmt *stmt);
  void emitDropCascade(llvm::Value *ptrAddr, const std::string &typeName);
  llvm::Function *getOrCreateDropCascadeHelper(const std::string &typeName);
  void emitDropForType(llvm::Value *ptrAddr,
                       const std::shared_ptr<Type> &type);
  void emitDropForTypeWithMask(llvm::Value *ptrAddr,
                               const std::shared_ptr<Type> &type,
                               llvm::Value *dropMaskAddr);
  void emitDropCascadeWithMask(llvm::Value *ptrAddr,
                               const std::string &typeName,
                               llvm::Value *dropMaskAddr);

  // Helpers
  llvm::AllocaInst *createEntryBlockAlloca(llvm::Type *type, llvm::Value *ArraySize = nullptr, const std::string &varName = "");
  std::string stripMorphology(const std::string &name);
  llvm::Value *genUnsafeStmt(const UnsafeStmt *stmt);
  llvm::Value *genExprStmt(const ExprStmt *stmt);
  llvm::Value *genUnreachableStmt(const UnreachableStmt *stmt);
  llvm::Value *genGuardBindStmt(const GuardBindStmt *gbs);
  llvm::Value *genNullCheck(llvm::Value *val, const ASTNode *node,
                            const std::string &msg = "panic: null access");

  struct GenContext {
    std::map<std::string, TokaSymbol> Symbols;
    std::map<std::string, llvm::Value *> NamedValues;
    std::string CurrentSelfType;
    std::vector<CFInfo> CFStack;
    std::vector<std::vector<VariableScopeInfo>> ScopeStack;
    llvm::BasicBlock *InsertBlock;
    llvm::BasicBlock::iterator InsertPoint;
    llvm::Value *CurrentCoroHandle;
    llvm::Value *CurrentCoroPromise;
    llvm::Value *CurrentCoroTCB;
    llvm::Value *CurrentCoroId;
    llvm::Type *CurrentCoroPromiseType;
    llvm::Type *CurrentCoroRetTy;
    llvm::BasicBlock *CurrentCoroSuspendRetBB;
    llvm::BasicBlock *CurrentCoroCleanupBB;
    llvm::BasicBlock *CurrentCoroFinalSuspendBB;
    llvm::Value *CurrentSRetPtr;
    llvm::Type *CurrentSRetTy;
    llvm::DISubprogram *CurrentDebugScope;
    llvm::DebugLoc DebugLocation;
  };

  GenContext saveContext();
  void restoreContext(const GenContext &ctx);
};

} // namespace toka
