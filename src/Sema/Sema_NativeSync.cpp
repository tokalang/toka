#include "toka/Sema.h"
#include <functional>
#include <set>

namespace toka {
NativeClosedPayloadResult
Sema::checkNativeSyncClosedPayload(const std::shared_ptr<Type> &root) const {
  using State = NativeClosedPayloadState;
  std::set<const ShapeDecl *> active;
  std::function<NativeClosedPayloadResult(const std::shared_ptr<Type> &, std::string)> visit;
  visit = [&](const std::shared_ptr<Type> &type, std::string path) -> NativeClosedPayloadResult {
    auto incomplete = [&](const char *reason) { return NativeClosedPayloadResult{State::Incomplete, path, reason}; };
    if (!type || type->isUnknown() || type->isUninit()) return incomplete("UnresolvedPayload");
    if (type->isAddrType() || type->isOAddrType() || type->isRawPointer() ||
        type->isReference() || type->isSlice() || type->isFunction() || type->isDynFn())
      return {State::ExternalDependency, path, "ReplacementMayCarryExternalDependency"};
    if (type->isInteger() || type->isFloatingPoint() || type->isBoolean() || type->isUnit())
      return {State::Closed, {}, {}};
    if (hasCanonicalOwningStringStorage(type)) return {State::Closed, {}, {}};
    if (type->isUniquePtr() || type->isSharedPtr())
      return visit(type->getPointeeType(), path + ".pointee");
    if (auto array = std::dynamic_pointer_cast<ArrayType>(type)) {
      if (!array->SymbolicSize.empty()) return incomplete("UnresolvedArrayExtent");
      return visit(array->ElementType, path + "[]");
    }
    auto shape = std::dynamic_pointer_cast<ShapeType>(type);
    if (!shape || !shape->Decl) return incomplete("MissingPayloadDeclaration");
    const auto *decl = shape->Decl;
    const auto *identity = decl->InstantiationTemplate ? decl->InstantiationTemplate : decl;
    if (!identity->NominalId) return incomplete("MissingNominalIdentity");
    // Only complete instantiated field types are evidence. Never substitute a
    // template recipe here and accidentally strip an argument's morphology.
    if (!decl->GenericParams.empty()) return incomplete("InstantiationRequired");
    if (decl->Kind != ShapeKind::Struct && decl->Kind != ShapeKind::Tuple &&
        decl->Kind != ShapeKind::Enum) return incomplete("UnsupportedPayloadKind");
    if (!active.insert(decl).second) return incomplete("RecursiveProofUnclosed");
    struct ActiveScope {
      std::set<const ShapeDecl *> &Active;
      const ShapeDecl *Decl;
      ~ActiveScope() { Active.erase(Decl); }
    } scope{active, decl};
    if (decl->InstantiationTemplate) {
      if (identity->GenericParams.size() != decl->InstantiationArgs.size())
        return incomplete("IncompleteInstantiationArguments");
      // First batch does not prove unused/phantom argument irrelevance. An
      // external or unknown type argument cannot borrow a closed field recipe
      // from another instantiation of the same template.
      for (size_t i = 0; i < decl->InstantiationArgs.size(); ++i) {
        if (identity->GenericParams[i].IsConst) continue;
        auto argument = visit(decl->InstantiationArgs[i], path + ".type_argument[" + std::to_string(i) + "]");
        if (!argument.closed()) return argument;
      }
    }
    NativeClosedPayloadResult result{State::Closed, {}, {}};
    for (const auto &member : decl->Members) {
      if (decl->Kind == ShapeKind::Enum) {
        if (member.SubMembers.empty() && !member.Type.empty()) {
          result = {State::Incomplete, path + "." + member.Name, "IncompleteVariantPayload"};
          break;
        }
        for (const auto &payload : member.SubMembers) {
          if (!payload.ResolvedType) {
            result = {State::Incomplete, path + "." + member.Name, "UnresolvedField"};
            break;
          }
          result = visit(getPhysicalType(payload), path + "." + member.Name + "." + payload.Name);
          if (!result.closed()) break;
        }
      } else {
        if (!member.ResolvedType) {
          result = {State::Incomplete, path + "." + member.Name, "UnresolvedField"};
          break;
        }
        result = visit(getPhysicalType(member), path + "." + member.Name);
      }
      if (!result.closed()) break;
    }
    return result;
  };
  return visit(root, "payload");
}
}
