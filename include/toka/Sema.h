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
#include "toka/AccessPath.h"
#include "toka/DiagnosticEngine.h"
#include "toka/Type.h"
#include "toka/PAL_Checker.h"
#include "toka/SemanticEvidence.h"
#include "toka/ComptimeValue.h"
#include <map>
#include <set>
#include <string>
#include <vector>

namespace toka {

struct SymbolInfo {
  // New Type Object (Source of Truth)
  std::shared_ptr<toka::Type> TypeObj;
  std::string CodegenName;
  uint64_t SymbolID = 0;
  SourceLocation DeclLoc;
  BindingPermission Permission;
  // A binding's declaration is its authority.  Shared flow may impose a
  // stricter payload ceiling inherited from its direct source; it must not
  // rewrite the declaration itself or require provenance traversal.
  bool PayloadFlowWritable = true;
  bool HasPayloadFlowCeiling = false;

  bool IsTypeAlias = false; // [NEW] For Generic Params (T -> i32)
  bool Moved = false;
  SourceLocation MoveLoc;
  uint64_t InitMask =
      ~0ULL; // 0=unset, 1=set. For shapes, each bit corresponds to a member.

  // "Hot Potato" Tracking
  // If this symbol is a Reference (&T), this mask tracks the InitMask of the
  // REFERENT. If a bit is 0, it means the referent's corresponding field is
  // Unset, and THIS reference is responsible for initializing it (or passing
  // the responsibility).
  uint64_t DirtyReferentMask = ~0ULL; // Default to Clean

  // Borrow Tracking
  std::string BorrowedFrom =
      ""; // If this is a reference, name of the source variable
  // The structured source path is PAL's authority and alias identity.  Keep
  // BorrowedFrom for legacy lifetime/dependency bookkeeping, which names the
  // root binding only and therefore cannot distinguish indexed projections.
  AccessPath BorrowedPath;
  std::set<std::string> LifeDependencySet; // [NEW] Shadow Dependency Set
  std::map<std::string, std::set<std::string>> FieldDependencySet; // [NEW] Member-specific deps

  // An incomplete typed-todo binding is useful to editor tooling only as a
  // conditional fact.  It is never a completed initialization or ordinary
  // semantic-evidence Allow.  This set records the direct todo requirements
  // on which this binding depends; each direct binding transfer reuses the
  // source set rather than tracing arbitrary provenance.  A narrow,
  // non-transfer expression and whole-binding assignment flow may carry the
  // same set forward; this remains editor-only state.
  std::set<uint64_t> ConditionalTodoIds;

  // A closure value must retain its capture facts after the literal is bound.
  // Unknown function values are intentionally not treated as boundary-safe.
  bool HasClosureBoundarySummary = false;
  std::set<std::string> ClosureImplicitCaptures;
  std::set<std::string> ClosureNonSendCaptures;
  std::set<std::string> ClosureNonSyncCopyCaptures;

  void *ReferencedModule = nullptr; // Pointer to ModuleScope (opaque here)

  // Permission queries deliberately name the layer they authorize.  A hatted
  // binding may be rebindable without granting write access to its payload;
  // callers must never use one query as a substitute for the other.
  bool IsHandleRebindable() const {
    // For a value binding, identity and payload are the same storage layer;
    // self# may therefore authorize rebinding a handle field.  For a handle
    // binding, the outer writable bit is the legacy representation of its
    // identity attribute and must not be interpreted as soul writability.
    return IsRebindable || (TypeObj && TypeObj->IsWritable);
  }

  bool IsReference() const { return TypeObj && TypeObj->isReference(); }

  // IsBorrowed() is now handled externally by PALChecker

  bool IsUnique() const {
    return TypeObj && TypeObj->typeKind == toka::Type::UniquePtr;
  }

  bool IsShared() const {
    return TypeObj && TypeObj->typeKind == toka::Type::SharedPtr;
  }

  bool IsSoulMutable() const {
    if (!TypeObj)
      return false;
    if (TypeObj->isReference()) {
      return IsDeclaredMutable;
    }
    // For pointer handles, writability comes from the accessible pointee view.
    // Reference views are handled above: an immutable & binding must not inherit
    // write permission just because its source path was mutable.
    if (TypeObj->isPointer()) {
      auto pointee = TypeObj->getPointeeType();
      return pointee && pointee->IsWritable;
    }
    // For non-pointers, identity and soul are at the same level of mutability.
    return TypeObj->IsWritable;
  }

  bool HasConstValue = false;
  uint64_t ConstValue = 0;
  ComptimeValue ConstValObj;
  bool IsDeclaredMutable = false;
  bool HasBeenMutated = false;
  bool HasBeenUsed = false;
  // A payload projection such as `p.field` is a meaningful use even when the
  // handle view of `p` is intentionally unused. Keep it distinct from a
  // whole-binding read so H/P-aware warnings can remain precise.
  bool HasPayloadBeenUsed = false;
  bool HasHandleBeenUsed = false;
  bool IsDeclaredVariable = false;
  const ImportDecl* ImportingDecl = nullptr;
  bool IsTypeName = false;
  bool IsTraitName = false;
  bool IsRebindable = false; // [NEW] prefix '#' or '!' rebind permission
  bool IsMorphicExempt = false; // [NEW] Track morphic exemption
  bool IsCeded = false;
  bool IsFunctionParameter = false;
  CallableReceiverMode CallableReceiver = CallableReceiverMode::Shared;

  // [Phase 2] Comptime Field Unroll Node Information
  bool IsComptimeField = false;
  std::string ComptimeFieldName = "";
  std::string ComptimeFieldTypeStr = "";
  uint64_t ComptimeFieldOffset = 0;
  uint64_t ComptimeFieldSize = 0;

  void *ASTPtr = nullptr; // Pointer to the underlying AST node (FunctionDecl, VariableDecl, ExternDecl etc.)
};

// Capabilities originate only at declarations and signatures.  A member or
// index path may preserve or restrict those capabilities, but never create
// one.  Surface syntax may request a capability, but must never manufacture
// it.
struct AccessCapability {
  bool PayloadWritable = false;
  bool HandleRebindable = false;
  bool PayloadFlowRestricted = false;
};

struct AccessIntent {
  bool PayloadWrite = false;
  bool HandleRebind = false;
};

enum class PermissionFlowKind {
  Fresh,
  Independent,
  Shared,
  UnsafeRaw,
  RequirementOnly,
};

// This is deliberately a one-hop fact.  DirectCapability describes the RHS
// expression currently being transferred; its bindings have already applied
// any earlier shared-flow ceiling.
struct PermissionFlow {
  PermissionFlowKind Kind = PermissionFlowKind::Fresh;
  AccessCapability DirectCapability;
};

class Scope {
public:
  inline static uint64_t NextSymbolID = 1;
  Scope *Parent = nullptr;
  std::map<std::string, SymbolInfo> Symbols;

  int Depth = 0; // [NEW] Scope depth for lifetime comparison
  bool IsLoop = false; // [NEW] Track if scope is a loop body
  Scope(Scope *P = nullptr) : Parent(P) {
    if (P)
      Depth = P->Depth + 1;
  }

  void define(const std::string &Name, const SymbolInfo &Info) {
    SymbolInfo stored = Info;
    if (stored.SymbolID == 0)
      stored.SymbolID = NextSymbolID++;
    Symbols[Name] = std::move(stored);
  }

  bool findSymbolByID(uint64_t ID, SymbolInfo *&OutInfo,
                      std::string *OutName = nullptr) {
    for (auto &entry : Symbols) {
      if (entry.second.SymbolID == ID) {
        OutInfo = &entry.second;
        if (OutName)
          *OutName = entry.first;
        return true;
      }
    }
    if (Parent)
      return Parent->findSymbolByID(ID, OutInfo, OutName);
    return false;
  }

  // Find symbol and its owning scope
  bool findSymbol(const std::string &Name, SymbolInfo *&OutInfo) {
    if (Symbols.count(Name)) {
      OutInfo = &Symbols[Name];
      return true;
    }
    if (Parent)
      return Parent->findSymbol(Name, OutInfo);
    return false;
  }

  bool findVariableWithDeref(const std::string &Name, SymbolInfo *&OutInfo, std::string &ActualName) {
    if (Symbols.count(Name)) { OutInfo = &Symbols[Name]; ActualName = Name; return true; }
    if (Symbols.count("&" + Name)) { OutInfo = &Symbols["&" + Name]; ActualName = "&" + Name; return true; }
    if (Symbols.count("*" + Name)) { OutInfo = &Symbols["*" + Name]; ActualName = "*" + Name; return true; }
    if (Symbols.count("^" + Name)) { OutInfo = &Symbols["^" + Name]; ActualName = "^" + Name; return true; }
    if (Symbols.count("~" + Name)) { OutInfo = &Symbols["~" + Name]; ActualName = "~" + Name; return true; }
    if (Parent) return Parent->findVariableWithDeref(Name, OutInfo, ActualName);
    return false;
  }

  bool findVariableWithDerefScope(const std::string &Name, SymbolInfo *&OutInfo,
                                  std::string &ActualName, Scope *&OutScope) {
    if (Symbols.count(Name)) { OutInfo = &Symbols[Name]; ActualName = Name; OutScope = this; return true; }
    if (Symbols.count("&" + Name)) { OutInfo = &Symbols["&" + Name]; ActualName = "&" + Name; OutScope = this; return true; }
    if (Symbols.count("*" + Name)) { OutInfo = &Symbols["*" + Name]; ActualName = "*" + Name; OutScope = this; return true; }
    if (Symbols.count("^" + Name)) { OutInfo = &Symbols["^" + Name]; ActualName = "^" + Name; OutScope = this; return true; }
    if (Symbols.count("~" + Name)) { OutInfo = &Symbols["~" + Name]; ActualName = "~" + Name; OutScope = this; return true; }
    if (Parent) return Parent->findVariableWithDerefScope(Name, OutInfo, ActualName, OutScope);
    return false;
  }

  bool lookup(const std::string &Name, SymbolInfo &OutInfo) {
    SymbolInfo *ptr = nullptr;
    if (findSymbol(Name, ptr)) {
      OutInfo = *ptr;
      return true;
    }
    return false;
  }

  // Mark a symbol as moved. Returns true if found and updated.
  bool markMoved(const std::string &Name, SourceLocation Loc = {}) {
    SymbolInfo *ptr = nullptr;
    if (findSymbol(Name, ptr)) {
      ptr->Moved = true;
      if (Loc.isValid())
        ptr->MoveLoc = Loc;
      return true;
    }
    return false;
  }

  // Clear moved flag. Returns true if found and updated.
  bool resetMoved(const std::string &Name) {
    SymbolInfo *ptr = nullptr;
    if (findSymbol(Name, ptr)) {
      ptr->Moved = false;
      ptr->MoveLoc = {};
      return true;
    }
    return false;
  }
};

class Sema {
public:
  friend bool areStructsStructurallyCompatible(Sema *sema, const std::string &targetName, const std::string &sourceName);

  Sema() {
    GenericInstancesModule = std::make_unique<toka::Module>();
  }

  /// \brief Run semantic analysis on the module.
  /// \return true if success, false if errors found.
  bool checkModule(Module &M);
  
  /// \brief Declare all globals in a module for multi-pass resolution.
  void declareGlobals(Module &M);

  /// \brief Run global shape safety checks.
  void checkShapeSovereignty();
  
  std::unique_ptr<toka::Module> extractGenericRegistry() {
    return std::move(GenericInstancesModule);
  }

  void setBorrowCheckEnabled(bool enabled) {
    PALCheckerState.IsEnabled = enabled;
  }

  bool hasErrors() const { return HasError; }

  const std::map<std::string, std::shared_ptr<toka::Type>>& getParenthesizedRecordTypes() const {
    return ParenthesizedRecordTypes;
  }

  // [NEW] Trait  // Concurrency type bounds
  bool hasDrop(const std::string &shapeName);
  bool canImplicitlyPassToCede(std::shared_ptr<toka::Type> Ty);
  bool isStartBoundaryScalar(std::shared_ptr<toka::Type> Ty) const;
  void checkStartBoundaryArgument(ASTNode *Node,
                                  std::shared_ptr<toka::Type> Ty,
                                  bool ParamIsCeded, bool ArgIsCeded,
                                  const std::string &Name,
                                  SourceLocation ParamLoc = {});
  bool isShapeSend(const std::string &shapeName);
  bool isShapeSync(const std::string &shapeName);
  std::string resolveType(const std::string &Type, bool force = false);
  std::shared_ptr<toka::Type> resolveType(std::shared_ptr<toka::Type> Type,
                                          bool force = false);
  const std::map<std::string, ShapeDecl *> &getShapeMap() const {
    return ShapeMap;
  }

private:
  // Shape Analysis Caches
  enum class ShapeAnalysisStatus {
    Unvisited,
    Visiting, // Cycle detection
    Analyzed
  };

  struct ShapeProperties {
    bool HasRawPtr = false;
    bool HasDrop = false;
    bool HasManualDrop = false; // [NEW] Derived from explicit 'drop' impl
    bool IsSend = true;         // [NEW] True by default unless proven otherwise
    bool IsSync = true;         // [NEW] True by default unless proven otherwise
    ShapeAnalysisStatus Status = ShapeAnalysisStatus::Unvisited;
  };

  void instantiateGenericImpl(
      ImplDecl *Template, const std::string &ConcreteTypeName,
      const std::vector<std::shared_ptr<toka::Type>> &GenericArgs);
  std::string genericImplKey(const std::string &typeName,
                             SourceLocation loc = {});
  static std::string genericImplKey(const ShapeDecl *shape);

  std::map<std::string, ShapeProperties> m_ShapeProps;

  // Generic Impl Templates (Lazy Instantiation)
  // Key: resolved generic template identity. This is the source name for an
  // unambiguous template and the module-scoped codegen name on collisions.
  // Value: Vector of Pointers to the Template ImplDecls (owned by Module)
  std::map<std::string, std::vector<ImplDecl *>> GenericImplMap;

  std::map<std::string, FunctionDecl *> InstantiationCache;
  std::map<std::string, std::shared_ptr<toka::Type>> GenericShapeCache;
  int RecursionDepth = 0;

  void analyzeShapes(Module &M);
  void computeShapeProperties(const std::string &shapeName, Module &M);

  bool HasError = false;
  uint64_t m_LastInitMask =
      1; // Default to fully initialized (1 for simple var)
  bool m_AllowUnsetUsage = false;
  Scope *CurrentScope = nullptr;
  std::vector<FunctionDecl *>
      GlobalFunctions; // All functions across all modules
  std::map<std::string, ExternDecl *> ExternMap;
  std::map<std::string, ShapeDecl *> ShapeMap;
  std::map<std::string, const ImportDecl*> ShapeImportMap; // [NEW] Track which ImportDecl brought in a shape/type
  struct AliasInfo {
    std::string Target;
    bool IsStrong;
    std::vector<GenericParam> GenericParams; // [NEW]
  };
  struct AssociatedTypeBinding {
    std::string Type;
    bool IsPer = false;
    SourceLocation Loc;
  };
  std::map<std::string, AliasInfo> TypeAliasMap;
  // TypeName -> {MethodName -> ReturnType}
  std::map<std::string, std::map<std::string, std::string>> MethodMap;
  // TypeName -> {MethodName -> FunctionDecl*}
  std::map<std::string, std::map<std::string, FunctionDecl *>> MethodDecls;
  std::map<std::string, TraitDecl *> TraitMap;
  // Key: "StructName@TraitName" -> {MethodName -> FunctionDecl*}
  std::map<std::string, std::map<std::string, FunctionDecl *>> ImplMap;
  std::map<std::string, AssociatedTypeBinding> AssociatedTypeMap;
  std::map<const ImplDecl *, std::map<std::string, std::string>>
      AssociatedTypeSubstitutionCache;
  std::set<const TraitDecl *> CheckedAssociatedTypeTraits;
  std::map<std::string, std::vector<EncapEntry>> EncapMap;
  std::string CurrentFunctionReturnType;
  FunctionDecl *CurrentFunction =
      nullptr; // [NEW] Track current function for dependencies
  std::string m_LastBorrowSource;
  std::set<std::string>
      m_LastLifeDependencies; // [NEW] Track shape dependencies
  std::map<std::string, std::set<std::string>> m_LastFieldDependencies; // [NEW] Track field specific dependencies
  std::shared_ptr<toka::Type> m_ExpectedType;
  bool m_ExpectedCedeTransfer = false;
  bool m_CheckingNegativeIntegerLiteral = false;
  std::set<std::string> m_AccessedVariables; // [CLOSURE] Track accessed variables
  PALChecker PALCheckerState; // [NEW] Path-Anchored Borrow Checker
  struct ModuleScope {
    std::string Name;
    bool IsTrustedSystemModule = false;
    std::map<std::string, SymbolInfo> LexicalSymbols;
    std::map<std::string, SymbolInfo> LexicalTypes;
    std::map<std::string, FunctionDecl *> Functions;
    std::map<std::string, std::vector<FunctionDecl *>> FunctionOverloads;
    std::map<std::string, ExternDecl *> Externs;
    std::map<std::string, ShapeDecl *> Shapes;
    std::map<std::string, AliasInfo> TypeAliases;
    std::map<std::string, TraitDecl *> Traits;
    std::map<std::string, VariableDecl *> Globals;
  };
  std::map<std::string, ModuleScope> ModuleMap; // FullPath -> Scope
  std::map<std::string, ModuleScope *> ModulePathAliases;
  std::map<const ASTNode *, ModuleScope *> DeclarationLexicalScopes;
  std::map<const FunctionDecl *, ModuleScope *> InstantiationLexicalScopes;
  std::map<const FunctionDecl *, std::set<std::string>>
      InstantiationTypeNames;

  ModuleScope *getModule(const std::string &Path);
  std::string getModuleName(Module *M);

  Module *CurrentModule = nullptr;
  std::unique_ptr<toka::Module> GenericInstancesModule; // [NEW] Central Registry for Generic AST Nodes
  bool m_InUnsafeContext = false;
  bool m_InLHS = false;
  bool m_IsUnsetInitCall = false;     // [NEW] Track .unset() intrinsic
  bool m_DisableSoulCollapse = false; // [NEW] Track context for soul collapse
  bool m_InIntermediatePath =
      false; // [Ch 5] Track if we are in a chain (not leaf)
  bool m_IsAssignmentTarget =
      false; // [Ch 6] Track if we are at the LHS terminal
  bool m_DisableVisibilityCheck =
      false; // [Auto-Clone] Bypass visibility for injected calls
  bool m_IsPrecomputingCaptures = false; // [NEW] Disable errors in closures
  Scope *m_ClosureCaptureRootScope = nullptr;
  bool m_IsMemberBase =
      false; // [NEW] Track if we are checking the base of a member access
  bool m_IsConsumingEffect = false; // [NEW] Track if current eval context consumes async/wait effects
  bool m_IsStartingTask = false; // Enforce the strict detached-task boundary.

  bool m_AllowPermissionSuffix = false; // [NEW] Track explicit method call context
  bool m_ExpectedWritability = false;   // [NEW] Contextual expectation for borrow exclusivity

  struct AnalysisState {
    std::map<std::string, uint64_t> InitMasks;
    std::map<std::string, bool> Moved;
    // Editor-only incompleteness state.  It follows local value flow but
    // never grants an operation or substitutes for PAL.
    std::map<std::string, std::set<uint64_t>> ConditionalTodoIds;
    // Path-local shared-flow ceilings survive control-flow joins.  Presence
    // means that at least one reachable path has installed a direct source
    // whose payload is not writable; the conservative join keeps that
    // restriction until a later unconditional rebind replaces it.
    std::set<AccessPath> PayloadFlowRestrictedPaths;
    PALChecker PAL;
  };

  struct ControlFlowInfo {
    std::string Label;
    std::string ExpectedType;
    std::shared_ptr<toka::Type> ExpectedTypeObj;
    bool IsLoop;
    bool IsReceiver =
        false; // Whether this context expects a 'pass' or 'break' value
    std::vector<AnalysisState> BreakStates;
    std::vector<AnalysisState> ContinueStates;
  };
  std::vector<ControlFlowInfo> m_ControlFlowStack;

  struct FlowSummary {
    bool CanFallThrough = true;
    bool HasReturnLikeExit = false;
    std::set<std::string> BreakLabels;
    std::set<std::string> ContinueLabels;
  };

  // Anonymous Records
  int AnonRecordCounter = 0;
  std::vector<std::unique_ptr<ShapeDecl>> SyntheticShapes;
  std::map<std::string, std::shared_ptr<toka::Type>> ParenthesizedRecordTypes;

  // Path Narrowing
  std::set<std::string> m_NarrowedPaths;

  // Unlike a binding declaration, a projected handle can be rebound during
  // a statement sequence.  Keep its one-hop shared-flow ceiling by exact
  // access path so a field or constant-index target never falls back to its
  // original declared P after receiving a readonly shared source.
  std::set<AccessPath> m_PayloadFlowRestrictedPaths;



  template <typename... Args>
  void error(ASTNode *Node, DiagID ID, Args &&...args) {
    if (m_IsPrecomputingCaptures) return;
    HasError = true;
    DiagnosticEngine::report(Node->Loc, ID, std::forward<Args>(args)...);
  }

  // Scope management
  void enterScope();
  void exitScope();
  AccessPath makeAccessPath(Expr *E);
  AccessPath makeAccessPath(const std::string &Path);
  AccessPath canonicalizeAccessPath(const AccessPath &Path);
  bool returnTypeHasMember(FunctionDecl *Function,
                           const std::string &Member);
  std::string getDependencyPathString(Expr *E);
  void recordPALDecision(ASTNode *Node, SemanticRuleID Rule,
                         PALOperationClass Operation, const AccessPath &Subject,
                         const std::optional<PALConflict> &Conflict,
                         SemanticDecision Decision, SemanticReason Reason,
                         bool ReportOrigin = false);
  void recordPALConflict(ASTNode *Node, PALOperationClass Operation,
                         const AccessPath &Subject,
                         const PALConflict &Conflict,
                         bool ReportOrigin = true);
  void recordDecision(ASTNode *Node, SemanticRuleID Rule,
                      SemanticOperation Operation, SemanticDecision Decision,
                      SemanticReason Reason, const std::string &Subject = {},
                      const std::string &Origin = {},
                      SourceLocation OriginLoc = {});
  SourceLocation findPathDeclaration(const std::string &Path);
  bool isBorrowAccessAuthorized(const AccessPath &Path,
                                const AccessPath &ConflictPath);
  std::string getPathString(Expr *E);
  SymbolInfo *resolveBorrowSource(SymbolInfo *Info,
                                  std::string &EffectiveName);
  int getScopeDepth(const std::string &
                        Name); // [NEW] Get depth of scope where name is defined

  // Passes
  void registerGlobals(Module &M);
  void checkUnsafePublicFunctionBoundary(FunctionDecl *Fn);
  void checkUnsafePublicShapeBoundary(ShapeDecl *Shape);
  void checkFunction(FunctionDecl *Fn);
  std::string getTraitFamilyName(const std::string &traitName) const;
  std::string canonicalTraitName(const std::string &traitName,
                                 const TraitDecl *trait) const;
  TraitDecl *findTraitDecl(const std::string &traitName) const;
  TraitDecl *findVisibleTraitDecl(const std::string &traitName,
                                  SourceLocation loc);
  ShapeDecl *findVisibleShapeDecl(const std::string &shapeName,
                                  SourceLocation loc = {});
  std::string getDynTraitName(const std::string &typeName) const;
  std::string getDynTraitName(std::shared_ptr<toka::Type> type) const;
  bool validateDynTraitObjectSafety(const std::string &traitName,
                                    SourceLocation loc);
  bool validateDynTraitObjectSafetyInType(const std::string &typeName,
                                          SourceLocation loc);
  bool validateDynTraitObjectSafetyInType(std::shared_ptr<toka::Type> type,
                                          SourceLocation loc);
  ModuleScope *getLexicalModule(SourceLocation loc);
  void recordInstantiationType(FunctionDecl *function,
                               std::shared_ptr<toka::Type> type);
  bool isTypeNameVisible(const std::string &typeName, SourceLocation loc);
  bool validateTypeVisibilityInType(const std::string &typeName,
                                    SourceLocation loc);
  bool validateTypeVisibilityInType(std::shared_ptr<toka::Type> type,
                                    SourceLocation loc);
  bool isBorrowLikeType(std::shared_ptr<toka::Type> type) const;
  std::string resolveAssociatedTypeProjection(const std::string &typeName,
                                              bool force);
  std::map<std::string, std::string>
  registerAssociatedTypes(ImplDecl *Impl, TraitDecl *Trait,
                          const std::string &resolvedTypeName);
  void applyAssociatedTypeSubstitutions(
      ImplDecl *Impl, const std::map<std::string, std::string> &substitutions);
  void validateTraitAssociatedTypes(TraitDecl *Trait);
  void registerImpl(ImplDecl *Impl);
  void declareImpl(ImplDecl *Impl);
  void checkImpl(ImplDecl *Impl);
  void checkStmt(Stmt *S);

  std::string checkUnaryExprStr(UnaryExpr *Unary); // Legacy
  std::shared_ptr<toka::Type>
  checkExprImpl(Expr *E); // New Object API Implementation
  std::shared_ptr<toka::Type> checkClosureExpr(ClosureExpr *Clo);
  std::shared_ptr<toka::Type>
  checkExpr(Expr *E); // New Object API Wrapper (Annotates AST)
  std::shared_ptr<toka::Type> checkExpr(
      Expr *E,
      std::shared_ptr<toka::Type> expected); // [NEW] Overload for inference
  std::shared_ptr<toka::Type>
  checkUnaryExpr(UnaryExpr *Unary); // New Object API
  std::shared_ptr<toka::Type>
  checkBinaryExpr(BinaryExpr *Bin); // New Object API
  std::set<uint64_t> collectConditionalTodoDependencies(const Expr *expr);
  bool validateIntegerLiteralRange(
      ASTNode *site, NumberExpr *literal,
      const std::shared_ptr<toka::Type> &targetType, bool isNegative);
  bool projectOwnedStringView(
      std::unique_ptr<Expr> &argument,
      std::shared_ptr<toka::Type> &argumentType,
      const std::shared_ptr<toka::Type> &expectedType);
  std::shared_ptr<toka::Type> checkMemberExpr(MemberExpr *Memb);
  std::shared_ptr<toka::Type>
  checkIndexExpr(ArrayIndexExpr *Idx);                       // New Object API
  std::shared_ptr<toka::Type> checkCallExpr(CallExpr *Call); // New Object API
  void checkPattern(MatchArm::Pattern *Pat, const std::string &TargetType,
                    AccessCapability SourceCapability,
                    const std::string &TargetPath = "",
                    const AccessPath &TargetAccessPath = {});

  // Decoupled Initialization Helpers
  std::shared_ptr<toka::Type> checkShapeInit(InitStructExpr *Init);
  std::shared_ptr<toka::Type>
  checkStructInit(InitStructExpr *Init, ShapeDecl *SD,
                  const std::string &resolvedName,
                  std::map<std::string, uint64_t> &memberMasks);
  std::shared_ptr<toka::Type>
  checkVariantInit(InitStructExpr *Init, ShapeDecl *SD,
                 const std::string &resolvedName,
                 std::map<std::string, uint64_t> &memberMasks);

  // Control flow helpers
  bool allPathsReturn(Stmt *S);
  bool allPathsJump(Stmt *S);
  FlowSummary summarizeFlow(Stmt *S);
  FlowSummary summarizeFlowExpr(Expr *E);
  void mergeFlowExits(FlowSummary &dst, const FlowSummary &src);
  AnalysisState captureAnalysisState();
  void mergeAnalysisStates(const std::vector<AnalysisState> &states,
                           const PALChecker &palBase);

  // Type system helpers
  bool isLValue(const Expr *expr);
  std::string getCommonType(const std::string &T1, const std::string &T2);

  // Helpers
  uint64_t getTypeSize(std::shared_ptr<toka::Type> Type);
  bool checkVisibility(ASTNode *Node, ShapeDecl *SD);
  bool isTypeCompatible(std::shared_ptr<toka::Type> Target,
                        std::shared_ptr<toka::Type> Source);
  AccessCapability getAccessCapability(Expr *E, bool declarationOnly = false);
  AccessIntent getAccessIntent(Expr *E);
  PermissionFlow getPermissionFlow(Expr *E);

  bool checkTraitBounds(SourceLocation Loc, const std::string &ParamName, 
                        const std::vector<std::string> &TraitBounds, 
                        const std::string &ConcreteType, bool isSilent = false,
                        SourceLocation BoundLoc = SourceLocation());
  std::vector<std::string> substituteTraitBounds(
      const std::vector<std::string> &Bounds,
      const std::vector<GenericParam> &Params,
      const std::vector<std::shared_ptr<toka::Type>> &Args);

  // [NEW] Deep Inspection for Union Safety
  std::shared_ptr<toka::Type>
  getDeepestUnderlyingType(std::shared_ptr<toka::Type> Type);

  std::shared_ptr<toka::Type>
  instantiateGenericShape(std::shared_ptr<ShapeType> GenericShape);

  FunctionDecl *instantiateGenericFunction(
      FunctionDecl *Template,
      const std::vector<std::shared_ptr<toka::Type>> &Args, CallExpr *CallSite);

  // [NEW] Helper to substitute GenericConst variables with NumberExpr
  std::unique_ptr<Expr> foldGenericConstant(std::unique_ptr<Expr> E);

  static std::string synthesizePhysicalType(const BindingPermission &Permission,
                                            const std::string &SoulType,
                                            bool stripSoulPrefixes = true) {
    std::string Signature = "";

    // 1. Morphologies (Prefix Zone)
    switch (Permission.Morphology) {
    case BindingMorphology::Unique:
      Signature += "^";
      break;
    case BindingMorphology::Shared:
      Signature += "~";
      break;
    case BindingMorphology::Reference:
      Signature += "&";
      break;
    case BindingMorphology::Raw:
      Signature += "*";
      break;
    case BindingMorphology::None:
      break;
    }

    // 2. Identity Attributes (Prefix Zone)
    if (Permission.IdentityRebindable)
      Signature += "#";
    if (Permission.IdentityBlocked)
      Signature += "$";
    if (Permission.IdentityNullable)
      Signature = "nul " + Signature;

    // 3. Soul Type (Base Name)
    Signature += stripSoulPrefixes ? toka::Type::stripPrefixes(SoulType)
                                   : SoulType;

    // 4. Soul/Object Attributes (Suffix Zone). Match Type::toString() and
    // docs/syntax_zh.md canonical spelling: T?# when both flags are present.
    if (Permission.SoulBlocked)
      Signature += "$";
    if (Permission.SoulNullable)
      Signature += "?";
    if (Permission.SoulWritable)
      Signature += "#";

    return Signature;
  }

  // Helper for type synthesis from AST nodes with binding/path permissions.
  template <typename T>
  static std::string synthesizePhysicalType(const T &Arg,
                                            bool stripSoulPrefixes = true) {
    return synthesizePhysicalType(Arg.Permission, getTypeName(Arg),
                                  stripSoulPrefixes);
  }

  static std::string getPhysicalTypeName(const ShapeMember &Member) {
    return Member.ResolvedType ? Member.ResolvedType->toString()
                               : synthesizePhysicalType(Member);
  }

  static std::shared_ptr<toka::Type> getPhysicalType(const ShapeMember &Member) {
    if (Member.ResolvedType)
      return Member.ResolvedType;
    return toka::Type::fromString(synthesizePhysicalType(Member));
  }

  // Pointer Morphology Strictness
  enum class MorphKind {
    None,    // No pointer (value type)
    Valid,   // Matches generic valid state (e.g. constructor result)
    Raw,     // *
    Unique,  // ^
    Shared,  // ~
    Ref,     // &
    Address, // & (Synonym for Reference in some contexts, but let's stick to
             // Ref)
    Any      // Wildcard
  };

  static MorphKind morphKindFromBindingMorphology(
      BindingMorphology Morphology) {
    switch (Morphology) {
    case BindingMorphology::Raw:
      return MorphKind::Raw;
    case BindingMorphology::Unique:
      return MorphKind::Unique;
    case BindingMorphology::Shared:
      return MorphKind::Shared;
    case BindingMorphology::Reference:
      return MorphKind::Ref;
    case BindingMorphology::None:
      return MorphKind::None;
    }
    return MorphKind::None;
  }

  static MorphKind morphKindFromPermission(
      const BindingPermission &Permission) {
    return morphKindFromBindingMorphology(Permission.Morphology);
  }

  static MorphKind morphKindFromType(
      const std::shared_ptr<toka::Type> &Type) {
    if (!Type)
      return MorphKind::None;
    if (Type->isRawPointer())
      return MorphKind::Raw;
    if (Type->isUniquePtr())
      return MorphKind::Unique;
    if (Type->isSharedPtr())
      return MorphKind::Shared;
    if (Type->isReference())
      return MorphKind::Ref;
    return MorphKind::None;
  }

  static MorphKind morphKindFromTypeString(const std::string &TypeName) {
    if (TypeName.empty())
      return MorphKind::None;
    return morphKindFromType(toka::Type::fromString(TypeName));
  }

  MorphKind getSyntacticMorphology(Expr *E);
  bool checkStrictMorphology(ASTNode *Node, MorphKind Target, MorphKind Source,
                             const std::string &TargetName);

  // [Auto-Clone]
  void tryInjectAutoClone(std::unique_ptr<Expr> &expr);

private:
  static std::string getTypeName(const FunctionDecl::Arg &A) { return A.Type; }
  static std::string getTypeName(const ExternDecl::Arg &A) { return A.Type; }
  static std::string getTypeName(const VariableDecl &V) { return V.TypeName; }
  static std::string getTypeName(const ShapeMember &M) { return M.Type; }
};

} // namespace toka
