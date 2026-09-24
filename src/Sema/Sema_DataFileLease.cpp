#include "toka/Sema.h"
#include "toka/SourceManager.h"
#include "llvm/Support/SHA256.h"

namespace toka {
namespace {
// This source-visible private contract is deliberately tied to the reviewed
// RC13 implementation. A changed library body cannot silently inherit native
// lease authority; updating the implementation requires requalification.
constexpr const char *kReadDataFileSourceSHA256 =
    "560d2e089d9a356d6253c05a0134cbe82590c582b08028008cdef8a76e237ee1";
constexpr const char *kNativeDeclarationsSHA256 =
    "9ad7d86356a3e7da822b9ff78ce5eb17c315eba280d0b0e55f9ac319d48c87a0";

std::string sha256LoadedSource(SourceLocation location) {
  if (!DiagnosticEngine::SrcMgr || !location.isValid()) return {};
  const auto content = DiagnosticEngine::SrcMgr->getBufferData(location);
  if (content.empty()) return {};
  llvm::SHA256 hash;
  hash.update(llvm::StringRef(content.data(), content.size()));
  const auto bytes = hash.final();
  static constexpr char hex[] = "0123456789abcdef";
  std::string result;
  result.reserve(bytes.size() * 2);
  for (uint8_t byte : bytes) {
    result.push_back(hex[byte >> 4]);
    result.push_back(hex[byte & 15]);
  }
  return result;
}

bool sameValueType(const std::shared_ptr<Type> &left,
                   const std::shared_ptr<Type> &right) {
  return left && right &&
         left->withAttributes(false, false, left->IsBlocked)->equals(
             *right->withAttributes(false, false, right->IsBlocked));
}
} // namespace

NativeSyncOwnerCandidatePtr Sema::collectDataFileLeaseRecipe(Expr *source) {
  if (!source || !source->ResolvedType) return {};
  auto exactContract = [&](const FunctionDecl *function)
      -> std::shared_ptr<const DataFileLeaseContract> {
    if (!function || function->Name != "open" || !function->Body ||
        function->TemplateOrigin || !function->GenericParams.empty()) return {};
    auto *scope = getLexicalModule(function->Loc);
    if (!scope || !scope->SourceModule || scope->SourceModule->IsInterface ||
        !scope->IsTrustedSystemModule || !scope->ShadowCoordinateKnown ||
        scope->ShadowLogicalModulePath != "std/data_file" ||
        scope->SourceModule->ShadowCoordinateOrigin != "toolchain") return {};
    auto found = scope->Shapes.find("ReadDataFile");
    auto *owner = found == scope->Shapes.end() ? nullptr : found->second;
    if (!owner || !owner->NominalId || owner->Kind != ShapeKind::Struct ||
        !owner->GenericParams.empty() || owner->Members.size() != 1 ||
        owner->Members[0].Name != "handle" || !owner->Members[0].IsValueMutable ||
        !getPhysicalType(owner->Members[0]) ||
        !getPhysicalType(owner->Members[0])->isAddrType() ||
        !owner->HasExplicitDrop || !owner->ResolvedDestructor)
      return {};
    auto cached = m_DataFileLeaseContracts.find(owner);
    if (cached != m_DataFileLeaseContracts.end())
      return cached->second;
    const auto &module = *scope->SourceModule;
    // Hash the very buffer parsed into these declarations. Re-reading a path
    // after parsing would allow a changed file to certify a different AST.
    const auto digest = sha256LoadedSource(function->Loc);
    if (digest != kReadDataFileSourceSHA256) return {};
    auto native = [&](const char *name) -> const ExternDecl * {
      auto found = ExternMap.find(name);
      if (found == ExternMap.end() || !found->second) return nullptr;
      auto *nativeScope = getLexicalModule(found->second->Loc);
      if (!nativeScope || !nativeScope->SourceModule ||
          nativeScope->SourceModule->IsInterface ||
          !nativeScope->IsTrustedSystemModule ||
          !nativeScope->ShadowCoordinateKnown ||
          nativeScope->ShadowLogicalModulePath != "sys/libc" ||
          nativeScope->SourceModule->ShadowCoordinateOrigin != "toolchain")
        return nullptr;
      return found->second;
    };
    const auto *nativeOpen = native("toka_datafile_open_read");
    const auto *nativeRetain = native("toka_datafile_read_retain");
    const auto *nativeRelease = native("toka_datafile_read_release");
    const auto *nativeRead = native("toka_datafile_pread");
    if (!nativeOpen || !nativeRetain || !nativeRelease || !nativeRead ||
        nativeOpen->Args.size() != 2 || nativeRetain->Args.size() != 1 ||
        nativeRelease->Args.size() != 1 || nativeRead->Args.size() != 5)
      return {};
    const auto nativeDigest = sha256LoadedSource(nativeOpen->Loc);
    if (nativeDigest != kNativeDeclarationsSHA256) return {};
    auto method = [&](const char *name) -> const FunctionDecl * {
      for (const auto &implementation : module.Impls) {
        if (implementation->ResolvedOwner != owner ||
            !implementation->TraitName.empty()) continue;
        for (const auto &candidate : implementation->Methods)
          if (candidate->Name == name) return candidate.get();
      }
      return nullptr;
    };
    const auto *open = method("open");
    const auto *clone = method("clone");
    const auto *read = method("read_at");
    const auto *drop = owner->ResolvedDestructor;
    auto ownerType = std::make_shared<ShapeType>(owner->Name);
    ownerType->resolve(owner);
    if (open != function || !open->Body || !clone || !clone->Body ||
        !read || !read->Body || !drop->Body ||
        open->Args.size() != 1 || clone->Args.size() != 1 ||
        drop->Args.size() != 1 || read->Args.size() != 4 ||
        clone->Args[0].IsCeded || !drop->Args[0].IsValueMutable ||
        !sameValueType(clone->ResolvedReturnType, ownerType) ||
        !sameValueType(clone->Args[0].ResolvedType, ownerType) ||
        !sameValueType(read->Args[0].ResolvedType, ownerType)) return {};
    auto contract = std::shared_ptr<DataFileLeaseContract>(new DataFileLeaseContract);
    contract->SourceModule = &module;
    contract->Owner = owner;
    contract->Open = open;
    contract->Clone = clone;
    contract->Drop = drop;
    contract->ReadAt = read;
    contract->NativeOpen = nativeOpen;
    contract->NativeRetain = nativeRetain;
    contract->NativeRelease = nativeRelease;
    contract->NativeReadAt = nativeRead;
    contract->SourceDigest = digest;
    contract->NativeDeclarationsDigest = nativeDigest;
    contract->Complete = true;
    m_DataFileLeaseContracts[owner] = contract;
    return contract;
  };

  if (auto *call = dynamic_cast<CallExpr *>(source)) {
    auto contract = exactContract(call->ResolvedFn);
    if (!contract || !sameValueType(source->ResolvedType,
                                     contract->Open->ResolvedReturnType)) return {};
    auto receipt = std::shared_ptr<NativeSyncOwnerCandidate>(new NativeSyncOwnerCandidate);
    receipt->DataFile = std::move(contract);
    receipt->DataFilePhase = DataFileLeasePhase::PendingOpen;
    receipt->OwnerEdge = source;
    receipt->Provider = call->ResolvedFn;
    receipt->ValueType = source->ResolvedType;
    return receipt;
  }
  auto *method = dynamic_cast<MethodCallExpr *>(source);
  if (!method || !method->ResolvedFn || !method->Object ||
      !method->Object->NativeSyncOwnerRecipe || method->Args.size()) return {};
  const auto parent = method->Object->NativeSyncOwnerRecipe;
  if (!parent->DataFile || !parent->DataFile->Complete ||
      m_InvalidNativeSyncOwnerRecipes.count(parent)) return {};
  const auto &contract = parent->DataFile;
  const auto *unwrapOrigin = method->ResolvedFn->TemplateOrigin
                                 ? method->ResolvedFn->TemplateOrigin
                                 : method->ResolvedFn;
  auto *unwrapScope = getLexicalModule(unwrapOrigin->Loc);
  const bool trustedUnwrap = unwrapScope && unwrapScope->SourceModule &&
      !unwrapScope->SourceModule->IsInterface &&
      unwrapScope->IsTrustedSystemModule && unwrapScope->ShadowCoordinateKnown &&
      unwrapScope->ShadowLogicalModulePath == "core/result" &&
      unwrapScope->SourceModule->ShadowCoordinateOrigin == "toolchain";
  const bool cloned = parent->DataFilePhase == DataFileLeasePhase::Owned &&
                      method->ResolvedFn == contract->Clone &&
                      sameValueType(source->ResolvedType, parent->ValueType);
  const bool unwrapped = parent->DataFilePhase == DataFileLeasePhase::PendingOpen &&
                         method->Method == "unwrap" && trustedUnwrap &&
                         method->ResolvedFn->Body &&
                         method->Object->ResolvedType &&
                         sameValueType(method->Object->ResolvedType,
                                       contract->Open->ResolvedReturnType) &&
                         std::dynamic_pointer_cast<ShapeType>(source->ResolvedType) &&
                         std::static_pointer_cast<ShapeType>(source->ResolvedType)->Decl ==
                             contract->Owner;
  if (!cloned && !unwrapped) return {};
  auto receipt = std::shared_ptr<NativeSyncOwnerCandidate>(new NativeSyncOwnerCandidate);
  receipt->DataFile = contract;
  receipt->DataFilePhase = DataFileLeasePhase::Owned;
  receipt->Parent = parent;
  receipt->OwnerEdge = source;
  receipt->Provider = method->ResolvedFn;
  receipt->ValueType = source->ResolvedType;
  return receipt;
}

bool Sema::dataFileLeaseLive(const NativeSyncOwnerWitnessPtr &witness) const {
  if (!witness || !witness->DataFile || !witness->DataFile->Complete ||
      !witness->Origin || !witness->Origin->DataFile ||
      witness->Origin->DataFile != witness->DataFile ||
      witness->Origin->DataFilePhase != DataFileLeasePhase::Owned ||
      m_InvalidNativeSyncOwnerRecipes.count(witness->Origin)) return false;
  // Parents are immutable historical operations. Retiring a moved-from
  // binding cannot revoke the independent lease now held by its target.
  std::set<const NativeSyncOwnerCandidate *> seen;
  bool sawOpen = false;
  for (auto node = witness->Origin; node; node = node->Parent) {
    if (!seen.insert(node.get()).second || node->DataFile != witness->DataFile ||
        !node->OwnerEdge || !node->ValueType) return false;
    if (node->DataFilePhase == DataFileLeasePhase::PendingOpen) {
      auto *call = dynamic_cast<const CallExpr *>(node->OwnerEdge);
      if (node->Parent || !call || call->ResolvedFn != witness->DataFile->Open)
        return false;
      sawOpen = true;
    } else if (auto *method = dynamic_cast<const MethodCallExpr *>(node->OwnerEdge)) {
      if (!node->Parent || method->Object->NativeSyncOwnerRecipe != node->Parent ||
          (method->ResolvedFn != witness->DataFile->Clone &&
           method->Method != "unwrap")) return false;
    } else if (auto *cede = dynamic_cast<const CedeExpr *>(node->OwnerEdge)) {
      if (!node->Parent || cede->Value->NativeSyncOwnerRecipe != node->Parent)
        return false;
    } else return false;
  }
  return sawOpen;
}

NativeSyncOwnerWitnessPtr Sema::qualifyDataFileLease(
    const NativeSyncOwnerCandidatePtr &recipe,
    const std::shared_ptr<Type> &actualType) {
  if (!recipe || !recipe->DataFile || !recipe->DataFile->Complete ||
      recipe->DataFilePhase != DataFileLeasePhase::Owned ||
      !sameValueType(recipe->ValueType, actualType)) return {};
  auto owner = std::dynamic_pointer_cast<ShapeType>(actualType);
  if (!owner || owner->Decl != recipe->DataFile->Owner) return {};
  auto witness = std::shared_ptr<NativeSyncOwnerWitness>(new NativeSyncOwnerWitness);
  witness->Origin = recipe;
  witness->DataFile = recipe->DataFile;
  witness->ValueType = actualType;
  witness->OwnerType = actualType;
  return dataFileLeaseLive(witness) ? witness : NativeSyncOwnerWitnessPtr{};
}
} // namespace toka
