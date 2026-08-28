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
#include "toka/DirectCallObservationAudit.h"
#include "toka/PureNominalOverloadProbeAudit.h"
#include "toka/PlaceState.h"
#include "toka/Type.h"
#include "toka/PAL_Checker.h"
#include "toka/SemanticEvidence.h"
#include "toka/ComptimeValue.h"
#include <cstdint>
#include <map>
#include <optional>
#include <ostream>
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
      ~0ULL; // 0=uninitialized, 1=initialized. For shapes, each bit corresponds to a member.
  // The one semantic fact for the whole local and, when admitted, its direct
  // projections. `InitMask` remains a compatibility liveness view until its
  // legacy consumers are retired; it is not a second exact-place authority.
  ExactPlaceFacts ExactPlace;

  PlaceStateFact &placeFact() { return ExactPlace.whole(); }
  const PlaceStateFact &placeFact() const { return ExactPlace.whole(); }
  PartialMovePlan &partialMovePlan() { return ExactPlace.plan(); }
  const PartialMovePlan &partialMovePlan() const { return ExactPlace.plan(); }
  ProjectionPlaceFacts &projectionFacts() { return ExactPlace.projections(); }
  const ProjectionPlaceFacts &projectionFacts() const {
    return ExactPlace.projections();
  }
  void installPartialMovePlan(PartialMovePlan plan) {
    ExactPlace.setPlan(plan, InitMask);
  }

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
  std::set<std::string> ClosureExplicitCaptures;
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
  // A place alias names storage owned by another binding.  It may carry
  // qualified H/P access, but never owns the place and therefore cannot be
  // ceded or acquire an independent drop obligation.
  bool IsPlaceAlias = false;
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
    const std::string soulName = Type::stripMorphology(Name);
    for (auto &entry : Symbols) {
      if (Type::stripMorphology(entry.first) == soulName) {
        OutInfo = &entry.second;
        ActualName = entry.first;
        return true;
      }
    }
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
    const std::string soulName = Type::stripMorphology(Name);
    for (auto &entry : Symbols) {
      if (Type::stripMorphology(entry.first) == soulName) {
        OutInfo = &entry.second;
        ActualName = entry.first;
        OutScope = this;
        return true;
      }
    }
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
      if (!ptr->ExactPlace.transitionWhole(PlaceState::Live,
                                           PlaceState::Moved))
        return false;
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
      if (!hasExactlyPlaceState(ptr->placeFact(), PlaceState::Moved))
        return true;
      if (!ptr->ExactPlace.transitionWhole(PlaceState::Moved,
                                           PlaceState::Live))
        return false;
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

  void setDirectCallObservationSession(D3ObservationSession *session) {
    m_D3ObservationSession = session;
  }

  void setPureNominalProbeAuditSession(D4ProbeAuditSession *session) {
    m_D4ProbeAuditSession = session;
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
  void dumpEncapSlice1FactsJSON(std::ostream &out) const;

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
      const std::vector<std::shared_ptr<toka::Type>> &GenericArgs,
      ShapeDecl *ConcreteOwner = nullptr);
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
  // A generic substitution may itself contain an outer type parameter with
  // the same spelling (for example, instantiating Inner<T> with &T).  While
  // expanding one lexical type alias, do not let its target recursively bind
  // that free name back to the alias currently being expanded.
  std::set<uint64_t> m_ResolvingTypeAliasSymbols;
  std::vector<FunctionDecl *>
      GlobalFunctions; // All functions across all modules
  std::map<std::string, ExternDecl *> ExternMap;
  std::map<std::string, ShapeDecl *> ShapeMap;
  std::map<std::string, const ImportDecl*> ShapeImportMap; // [NEW] Track which ImportDecl brought in a shape/type
  struct AliasInfo {
    std::string Target;
    TypeSyntaxPtr TargetSyntax;
    bool IsStrong;
    std::vector<GenericParam> GenericParams; // [NEW]
  };
  struct AssociatedTypeBinding {
    std::string Type;
    std::shared_ptr<toka::Type> ResolvedType;
    bool IsPer = false;
    SourceLocation Loc;
  };
  std::shared_ptr<toka::Type> lowerAliasTarget(const AliasInfo &alias) const;
  std::shared_ptr<toka::Type> instantiateAliasTarget(
      const AliasInfo &alias,
      const std::vector<std::shared_ptr<toka::Type>> &arguments) const;
  std::map<std::string, AliasInfo> TypeAliasMap;
  // TypeName -> {MethodName -> ReturnType}
  std::map<std::string, std::map<std::string, std::string>> MethodMap;
  // TypeName -> {MethodName -> FunctionDecl*}
  std::map<std::string, std::map<std::string, FunctionDecl *>> MethodDecls;
  std::map<std::string, TraitDecl *> TraitMap;
  // Canonical P1 lang-item identity and the only providers currently
  // qualified to construct/transport __PlaceOutcome.
  TraitDecl *m_CorePlaceIteratorTrait = nullptr;
  std::set<const FunctionDecl *> m_QualifiedPlaceIteratorProviders;
  // Key: "StructName@TraitName" -> {MethodName -> FunctionDecl*}
  std::map<std::string, std::map<std::string, FunctionDecl *>> ImplMap;
  std::map<std::string, AssociatedTypeBinding> AssociatedTypeMap;
  std::map<const ImplDecl *,
           std::map<std::string, std::shared_ptr<toka::Type>>>
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
    bool ShadowCoordinateKnown = false;
    std::string ShadowCrateId;
    std::string ShadowLogicalModulePath;
    std::map<std::string, SymbolInfo> LexicalSymbols;
    std::map<std::string, SymbolInfo> LexicalTypes;
    std::map<std::string, FunctionDecl *> Functions;
    std::map<std::string, std::vector<FunctionDecl *>> FunctionOverloads;
    std::map<std::string, ExternDecl *> Externs;
    std::map<std::string, ShapeDecl *> Shapes;
    std::map<std::string, AliasInfo> TypeAliases;
    std::map<std::string, TraitDecl *> Traits;
    std::map<std::string, VariableDecl *> Globals;
    std::set<std::string> GenericTypeParameterNames;
  };

  // RFC @Encap epoch Slice 1 facts. These are observational data only until a
  // later slice explicitly switches a semantic decision to them.
  enum class Slice1ResourceContract { None, Borrowed, Owned };
  enum class Slice1DropPlan { Unknown, LegacyStructural, LegacyCustom };
  enum class Slice1CopyProof { Unknown, ProvenCopy, ProvenNonCopy };
  enum class Slice1CopyWitness { None, ExplicitRequest };
  enum class Slice1DupProvider { None, UserCandidate, InvalidCandidate };
  struct Slice1PolicyFact {
    bool IsExplicit = false;
    bool IsStructuralLegacy = false;
  };
  struct Slice1PartialMovePlan {
    bool IsKnown = false;
  };

  // The keys are canonical nominal-definition identities plus concrete type
  // spelling, never the legacy unqualified base type name.
  std::map<std::string, Slice1PolicyFact> PolicyMap;
  std::map<std::string, Slice1ResourceContract> ResourceContractMap;
  std::map<std::string, Slice1DropPlan> DropPlanMap;
  std::map<std::string, Slice1PartialMovePlan> PartialMovePlanMap;
  std::map<std::string, Slice1CopyProof> CopyProofMap;
  std::map<std::string, Slice1CopyWitness> CopyWitnessMap;
  std::map<std::string, Slice1DupProvider> DupProviderMap;

  std::string canonicalTypeFactKey(const std::string &typeName,
                                   SourceLocation loc);
  std::string canonicalImplDefinitionId(const ImplDecl *impl) const;
  std::string canonicalOutcomeModuleIdentity(const ModuleScope *module) const;
  bool canonicalOutcomeTypeIdentity(const std::shared_ptr<toka::Type> &type,
                                    std::string &result) const;
  std::string canonicalOutcomeFunctionIdentity(FunctionDecl *fn,
                                               bool &hasCanonicalTypes);
  std::string canonicalOutcomeShapeIdentity(const ShapeDecl *shape) const;
  std::optional<std::string>
  canonicalOutcomeDeclarationWitness(FunctionDecl *fn) const;
  void populateOutcomeTransitionIdentities(FunctionDecl *fn);
  void recordSlice1ImplFact(ImplDecl *impl,
                            const std::string &resolvedTypeName,
                            const std::string &canonicalTrait);
  std::set<std::string> GenericImplInstanceMap;
  std::map<const ImplDecl *, std::string> Slice1ImplDefinitionIds;

  // Slice 2 turns @Encap entries into an authority policy tied to the
  // resolver's nominal/module identities, rather than a physical file path.
  struct Slice2Policy {
    ImplDecl *Impl = nullptr;
    ModuleScope *Owner = nullptr;
    std::vector<EncapEntry> Entries;
  };
  std::map<const ShapeDecl *, Slice2Policy> Slice2PolicyMap;
  void registerSlice2Policy(ImplDecl *Impl);
  bool canNameEncapField(const ShapeDecl *Shape, const std::string &Field,
                         SourceLocation UseLoc);

  std::map<const ShapeDecl *, const ImplDecl *> Slice4CopyRequests;
  std::map<const ShapeDecl *, const ImplDecl *> Slice4DupProviders;
  std::map<const ShapeDecl *, Slice1CopyProof> Slice4CopyProofs;
  std::set<const ShapeDecl *> Slice4CopyProofInProgress;
  enum class Slice4CopyRecipeKind { Always, Never, All, Dependent };
  struct Slice4CopyRecipe {
    Slice4CopyRecipeKind Kind = Slice4CopyRecipeKind::Dependent;
    std::vector<std::string> Requirements;
    std::string Detail;
  };
  std::map<const ShapeDecl *, Slice4CopyRecipe> Slice4CopyRecipes;
  std::set<const ShapeDecl *> Slice4CopyRecipeInProgress;
  void registerSlice4Impl(ImplDecl *Impl);
  void validateSlice4CopyAndDup(Module &M);
  void recordSlice5InterfaceFacts(Module &M);
  bool proveSlice4Copy(const ShapeDecl *Shape);
  bool proveSlice4CopyType(std::shared_ptr<toka::Type> Type);
  D3CopyProof lookupD3CopyProof(const std::shared_ptr<toka::Type> &Type) const;
  D3TypeCategory
  classifyD3TypeCategory(const std::shared_ptr<toka::Type> &Type) const;
  D3OwnershipProof
  lookupD3OwnershipProof(const std::shared_ptr<toka::Type> &Type) const;
  D3ObservationSentinel captureD3ObservationSentinel(
      CallExpr *Call, Expr *Actual) const;
  D3SourceLocation makeD3SourceLocation(SourceLocation Location) const;
  Slice4CopyRecipe deriveSlice4CopyRecipe(const ShapeDecl *Shape);
  Slice4CopyRecipe deriveSlice4CopyRecipeType(
      std::shared_ptr<toka::Type> Type, const ShapeDecl *Context);
  bool slice4CopyRequirementIsProven(const ShapeDecl *Shape,
                                     const ImplDecl *Impl,
                                     const std::string &Requirement);
  bool genericImplAppliesToWholeShape(const ShapeDecl *Shape,
                                      const ImplDecl *Impl) const;
  std::string describeSlice4CopyRecipe(const Slice4CopyRecipe &Recipe) const;

  std::map<std::string, ModuleScope> ModuleMap; // FullPath -> Scope
  std::map<std::string, ModuleScope *> ModulePathAliases;
  std::map<const ASTNode *, ModuleScope *> DeclarationLexicalScopes;
  std::set<const FunctionDecl *> TrustedAtomicWrapperDeclarations;
  struct DeclaredShapeIdentityRecord {
    const Module *Owner = nullptr;
    const ShapeDecl *Decl = nullptr;
  };
  std::vector<const Module *> DeclaredModules;
  std::vector<DeclaredShapeIdentityRecord> DeclaredShapeIdentityRecords;
  std::optional<NominalShapeId>
  makeDeclaredShapeId(const Module &module, const ShapeDecl &shape) const;
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
  bool m_BorrowingSelectedHandle = false; // `&^x`, `&~x`, `&&x`
  bool m_InIntermediatePath =
      false; // [Ch 5] Track if we are in a chain (not leaf)
  bool m_IsAssignmentTarget =
      false; // [Ch 6] Track if we are at the LHS terminal
  bool m_DisableVisibilityCheck =
      false; // Temporarily bypass visibility for compiler-synthesized calls.
  bool m_IsPrecomputingCaptures = false; // [NEW] Disable errors in closures
  Scope *m_ClosureCaptureRootScope = nullptr;
  bool m_IsMemberBase =
      false; // [NEW] Track if we are checking the base of a member access
  bool m_IsConsumingEffect = false; // [NEW] Track if current eval context consumes async/wait effects
  // A precise E04646 has already rejected this ownership transfer.  Keep
  // unary handle selection type-checking for recovery without also reporting
  // the less specific PAL invalidation conflict for the same alias.
  bool m_SuppressRejectedAliasInvalidation = false;
  bool m_IsStartingTask = false; // Enforce the strict detached-task boundary.
  const Expr *m_StartBoundaryRoot = nullptr;
  D3ObservationSession *m_D3ObservationSession = nullptr;
  D4ProbeAuditSession *m_D4ProbeAuditSession = nullptr;
  unsigned m_D3SpeculativeCallDepth = 0;

  bool m_AllowPermissionSuffix = false; // [NEW] Track explicit method call context
  bool m_ExpectedWritability = false;   // [NEW] Contextual expectation for borrow exclusivity

  struct AnalysisState {
    std::map<std::string, uint64_t> InitMasks;
    std::map<std::string, bool> Moved;
    std::map<std::string, ExactPlaceFacts> ExactPlaces;
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
  // This is analysis state, not a source type.  In particular it must never
  // reuse ABI `void`, which remains a real FFI type.
  inline static constexpr const char *NoProducedValue =
      "<no-produced-value>";
  std::vector<ControlFlowInfo> m_ControlFlowStack;
  struct InitBlockContext {
    std::string PlaceName;
    size_t ControlFlowDepth;
  };
  // An active lexical obligation owns its target's private Maybe fact. A
  // break or continue may only target a loop introduced after this depth, and
  // `place is uninit` is valid only in the owning direct `if` condition.
  std::vector<InitBlockContext> m_InitBlockContexts;
  const BinaryExpr *m_ExpectedInitStatePredicate = nullptr;
  std::vector<CallExpr *> m_OutcomePendingCalls;

  struct FlowSummary {
    bool CanFallThrough = true;
    bool HasReturnLikeExit = false;
    std::set<std::string> BreakLabels;
    std::set<std::string> ContinueLabels;
  };

  // Anonymous Records
  int AnonRecordCounter = 0;
  std::vector<std::unique_ptr<ShapeDecl>> SyntheticShapes;
  std::map<std::string, std::shared_ptr<toka::Type>>
      ParenthesizedRecordTypeCache;
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
  bool diagnosePlaceAliasOwnershipTransfer(ASTNode *Site, Expr *Source);
  AggregateTransferKind qualifyAggregateTransfer(
      Expr *Source, const std::shared_ptr<Type> &DestinationType);
  CallValueCategory classifyShadowCallValueCategory(
      Expr *Argument, AccessPath &SourcePlace);
  std::shared_ptr<Type> queryShadowCallArgumentType(
      Expr *Argument, const std::shared_ptr<Type> &DestinationType);
  CallExecutionBoundary
  classifyShadowExecutionBoundary(FunctionDecl *Function) const;
  CallTransferPlan buildShadowCallTransferPlan(
      ASTNode *CallSite, Expr *Argument,
      const std::shared_ptr<Type> &ArgumentType,
      const std::shared_ptr<Type> &DestinationType, bool FormalIsCeded,
      bool FormalIsInit, bool ActualIsInit, bool LegacyCallerRuleApplied,
      bool LegacyCedeExempt, CallTransferRoute Route, bool IsAsync,
      CallExecutionBoundary ExecutionBoundary, unsigned ArgumentIndex,
      unsigned FormalIndex);
  void recordShadowCallTransfer(
      ASTNode *CallSite, std::vector<CallTransferPlan> &Plans,
      unsigned ArgumentIndex, unsigned FormalIndex, Expr *Argument,
      const std::shared_ptr<Type> &ArgumentType,
      const std::shared_ptr<Type> &DestinationType, bool FormalIsCeded,
      bool FormalIsInit, bool ActualIsInit, bool LegacyCallerRuleApplied,
      bool LegacyCedeExempt, CallTransferRoute Route, const std::string &Callee,
      const std::string &Parameter,
      SourceLocation ParameterLoc, bool IsAsync,
      CallExecutionBoundary ExecutionBoundary = CallExecutionBoundary::None);
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
  std::string getDisplayArgumentString(Expr *E);
  std::string ownershipSourceLabel(const Expr *expression);
  SymbolInfo *resolveBorrowSource(SymbolInfo *Info,
                                  std::string &EffectiveName);
  static constexpr int FAIL_CLOSED_SCOPE_DEPTH = 999999;
  static std::string extractPathRoot(const std::string &Path);
  int getScopeDepth(const std::string &
                        Name); // [NEW] Get depth of scope where name is defined

  // Passes
  void registerGlobals(Module &M);
  void checkUnsafePublicFunctionBoundary(FunctionDecl *Fn);
  void checkUnsafePublicShapeBoundary(ShapeDecl *Shape);
  void checkFunction(FunctionDecl *Fn);
  void validateGenericSignatureTypeNames(
      FunctionDecl *Fn, const std::vector<GenericParam> &enclosingParams = {},
      const std::set<std::string> &enclosingTypeNames = {});
  std::string getTraitFamilyName(const std::string &traitName) const;
  std::string canonicalTraitName(const std::string &traitName,
                                 const TraitDecl *trait) const;
  TraitDecl *findTraitDecl(const std::string &traitName) const;
  TraitDecl *findVisibleTraitDecl(const std::string &traitName,
                                  SourceLocation loc);
  ShapeDecl *findVisibleShapeDecl(const std::string &shapeName,
                                  SourceLocation loc = {});
  ShapeDecl *resolveImplOwner(ImplDecl *impl);
  PartialMovePlan admittedPartialMovePlan(const SymbolInfo &info);
  void initializeProjectionFacts(SymbolInfo &info);
  void syncLegacyProjectionLiveness(SymbolInfo &info);
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
  bool validateHandleGrammar(SourceLocation loc,
                             const std::shared_ptr<toka::Type> &type);
  bool containsInternalPlaceOutcome(
      const std::shared_ptr<toka::Type> &type) const;
  void reportHandleGrammarViolation(SourceLocation loc,
                                    const std::shared_ptr<toka::Type> &type,
                                    HandleGrammarViolation violation);
  bool validateTypeVisibilityInType(std::shared_ptr<toka::Type> type,
                                    SourceLocation loc);
  bool isBorrowLikeType(std::shared_ptr<toka::Type> type) const;
  std::string resolveAssociatedTypeProjection(const std::string &typeName,
                                              bool force);
  std::shared_ptr<toka::Type>
  resolveAssociatedTypeProjection(const TypeSyntaxPtr &syntax, bool force);
  std::map<std::string, std::shared_ptr<toka::Type>>
  registerAssociatedTypes(ImplDecl *Impl, TraitDecl *Trait,
                          const std::string &resolvedTypeName);
  void applyAssociatedTypeSubstitutions(
      ImplDecl *Impl,
      const std::map<std::string, std::shared_ptr<toka::Type>> &substitutions);
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
  void validateAtomicOrderingArguments(
      const FunctionDecl *Fn,
      const std::vector<std::unique_ptr<Expr>> &Arguments,
      size_t omittedLeadingArguments = 0);
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
  static bool hasExplicitCallArgumentWriteSigil(const Expr *E);
  void validateCallArgumentMutSigil(Expr *arg, bool paramIsValueMutable,
                                    const std::string &paramName,
                                    SourceLocation paramLoc,
                                    SourceLocation callLoc,
                                    size_t argIndex);
  PermissionFlow getPermissionFlow(Expr *E);

  bool checkTraitBounds(SourceLocation Loc, const std::string &ParamName, 
                        const std::vector<std::string> &TraitBounds, 
                        const std::shared_ptr<toka::Type> &ConcreteType,
                        bool isSilent = false,
                        SourceLocation BoundLoc = SourceLocation());
  bool checkMorphologyBounds(
      SourceLocation Loc, const GenericParam &Param,
      const std::shared_ptr<toka::Type> &ConcreteType,
      bool isSilent = false);
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
    if (Permission.IdentityMayBeZero)
      Signature = "nul " + Signature;

    // 3. Soul Type (Base Name)
    Signature += stripSoulPrefixes ? toka::Type::stripPrefixes(SoulType)
                                   : SoulType;

    // 4. Soul/Object attributes (suffix zone).
    if (Permission.SoulBlocked)
      Signature += "$";
    if (Permission.SoulWritable)
      Signature += "#";

    return Signature;
  }

  // Source declarations retain TypeSyntax, while binding permissions live on
  // the declaration name.  Merge those two independent layers directly into
  // semantic Type rather than rebuilding a source spelling and parsing it
  // again.  `LegacySoulType` is used only for older synthesized AST nodes
  // that have no TypeSyntax.
  static std::shared_ptr<toka::Type>
  synthesizePhysicalTypeObject(const BindingPermission &Permission,
                               const TypeSyntaxPtr &Syntax,
                               const std::string &LegacySoulType,
                               bool stripSoulPrefixes = true) {
    TypeSyntaxPtr soulSyntax = Syntax;
    // Function-value compatibility has historically consumed the soul part
    // of a type-side morphology spelling (for example `val: ^T`).  Keep that
    // convention structurally: declaration-side morphology is represented by
    // BindingPermission, while this legacy source surface remains a soul
    // annotation at the call boundary.
    if (stripSoulPrefixes) {
      while (soulSyntax && soulSyntax->NodeKind == TypeSyntax::Kind::Morphology &&
             !soulSyntax->IsPostfix &&
             (soulSyntax->Text == "nul" || soulSyntax->Text == "*" ||
              soulSyntax->Text == "^" || soulSyntax->Text == "~" ||
              soulSyntax->Text == "&" || soulSyntax->Text == "#" ||
              soulSyntax->Text == "?" || soulSyntax->Text == "$")) {
        soulSyntax = soulSyntax->Subject;
      }
    }
    std::shared_ptr<toka::Type> soul =
        soulSyntax ? toka::Type::fromSyntax(soulSyntax)
               : toka::Type::fromString(LegacySoulType);
    if (!soul)
      soul = std::make_shared<toka::UnresolvedType>(LegacySoulType);

    // Source suffixes and declaration-side identity attributes compose.  The
    // latter only add authority; they must not erase a suffix already parsed
    // into TypeSyntax.
    soul = soul->withAttributes(
        soul->IsWritable || Permission.SoulWritable,
        false,
        soul->IsBlocked || Permission.SoulBlocked);

    if (auto ptr = std::dynamic_pointer_cast<toka::PointerType>(soul)) {
      if (Permission.SoulWritable && ptr->PointeeType) {
        ptr->PointeeType = ptr->PointeeType->withAttributes(
            true, ptr->PointeeType->IsNullable, ptr->PointeeType->IsBlocked);
      }
      if (Permission.SoulBlocked && ptr->PointeeType) {
        ptr->PointeeType = ptr->PointeeType->withAttributes(
            false, ptr->PointeeType->IsNullable, true);
      }
    }

    if (!Permission.HandleLayers.empty()) {
      std::shared_ptr<toka::Type> result = soul;
      for (auto it = Permission.HandleLayers.rbegin(); it != Permission.HandleLayers.rend(); ++it) {
        const auto &layer = *it;
        std::shared_ptr<toka::PointerType> physical;
        switch (layer.Morphology) {
        case BindingMorphology::Raw:
          physical = std::make_shared<toka::RawPointerType>(result);
          break;
        case BindingMorphology::Unique:
          physical = std::make_shared<toka::UniquePointerType>(result);
          break;
        case BindingMorphology::Shared:
          physical = std::make_shared<toka::SharedPointerType>(result);
          break;
        case BindingMorphology::Reference:
          physical = std::make_shared<toka::ReferenceType>(result);
          break;
        case BindingMorphology::None:
          break;
        }
        if (physical) {
          physical->IsWritable = layer.Rebindable;
          physical->IsNullable = layer.Nullable;
          physical->IsBlocked = layer.Blocked;
          result = physical;
        }
      }
      return result;
    }

    if (Permission.Morphology == BindingMorphology::None)
      return soul;

    std::shared_ptr<toka::PointerType> physical;
    bool isInferred = (Syntax == nullptr);
    if (isInferred && Permission.Morphology == BindingMorphology::Raw && soul->isRawPointer()) {
      physical = std::dynamic_pointer_cast<toka::PointerType>(soul);
    } else if (isInferred && Permission.Morphology == BindingMorphology::Unique && soul->isUniquePtr()) {
      physical = std::dynamic_pointer_cast<toka::PointerType>(soul);
    } else if (isInferred && Permission.Morphology == BindingMorphology::Shared && soul->isSharedPtr()) {
      physical = std::dynamic_pointer_cast<toka::PointerType>(soul);
    } else if (isInferred && Permission.Morphology == BindingMorphology::Reference && soul->isReference()) {
      physical = std::dynamic_pointer_cast<toka::PointerType>(soul);
    } else {
      switch (Permission.Morphology) {
      case BindingMorphology::Raw:
        physical = std::make_shared<toka::RawPointerType>(soul);
        break;
      case BindingMorphology::Unique:
        physical = std::make_shared<toka::UniquePointerType>(soul);
        break;
      case BindingMorphology::Shared:
        physical = std::make_shared<toka::SharedPointerType>(soul);
        break;
      case BindingMorphology::Reference:
        physical = std::make_shared<toka::ReferenceType>(soul);
        break;
      case BindingMorphology::None:
        break;
      }
    }
    if (!physical)
      return soul;

    physical->IsWritable = Permission.IdentityRebindable;
    physical->IsNullable = Permission.IdentityMayBeZero;
    physical->IsBlocked = Permission.IdentityBlocked;
    return physical;
  }

  bool validateAliasTarget(SourceLocation loc, const std::string &aliasName,
                           const TypeSyntaxPtr &targetSyntax,
                           const std::shared_ptr<toka::Type> &targetType);

  bool validateParameterHandleChain(SourceLocation loc,
                                    const std::string &paramName,
                                    const BindingPermission &permission,
                                    const std::shared_ptr<toka::Type> &physicalType,
                                    const TypeSyntaxPtr &typeSyntax,
                                    bool hadRejectedTypeSideMorphology);

  // Helper for type synthesis from AST nodes with binding/path permissions.
  template <typename T>
  static std::string synthesizePhysicalType(const T &Arg,
                                            bool stripSoulPrefixes = true) {
    return synthesizePhysicalType(Arg.Permission, getTypeName(Arg),
                                  stripSoulPrefixes);
  }

  template <typename T>
  static std::shared_ptr<toka::Type>
  synthesizePhysicalTypeObject(const T &Arg,
                               bool stripSoulPrefixes = true) {
    return synthesizePhysicalTypeObject(Arg.Permission, getTypeSyntax(Arg),
                                        getTypeName(Arg),
                                        stripSoulPrefixes);
  }

  static std::string getPhysicalTypeName(const ShapeMember &Member) {
    return Member.ResolvedType ? Member.ResolvedType->toString()
                               : synthesizePhysicalType(Member);
  }

  static std::shared_ptr<toka::Type> getPhysicalType(const ShapeMember &Member) {
    if (Member.ResolvedType)
      return Member.ResolvedType;
    return synthesizePhysicalTypeObject(Member);
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

private:
  static std::string getTypeName(const FunctionDecl::Arg &A) { return A.Type; }
  static std::string getTypeName(const ExternDecl::Arg &A) { return A.Type; }
  static std::string getTypeName(const VariableDecl &V) { return V.TypeName; }
  static std::string getTypeName(const ShapeMember &M) { return M.Type; }
  static TypeSyntaxPtr getTypeSyntax(const FunctionDecl::Arg &A) {
    return A.TypeSyntax;
  }
  static TypeSyntaxPtr getTypeSyntax(const ExternDecl::Arg &A) {
    return A.TypeSyntax;
  }
  static TypeSyntaxPtr getTypeSyntax(const VariableDecl &V) {
    return V.DeclaredTypeSyntax;
  }
  static TypeSyntaxPtr getTypeSyntax(const ShapeMember &M) {
    return M.TypeSyntax;
  }
};

} // namespace toka
