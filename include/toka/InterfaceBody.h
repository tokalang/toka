#pragma once
#include "toka/AST.h"
#include <functional>

namespace toka {
// Enumerate cleanup declarations, not an independence/ownership proof. A
// visited type only bounds traversal; it does not discharge recursive facts.
inline void visitInterfaceCleanupTypes(
    const std::shared_ptr<Type> &type, std::set<const Type *> &visited,
    const std::function<void(const ShapeDecl *)> &visit) {
  if (!type || !visited.insert(type.get()).second || type->isReference() ||
      type->isRawPointer()) return;
  if (type->isUniquePtr() || type->isSharedPtr()) {
    visitInterfaceCleanupTypes(type->getPointeeType(), visited, visit);
  } else if (auto element = type->getArrayElementType()) {
    visitInterfaceCleanupTypes(element, visited, visit);
  } else if (auto shape = std::dynamic_pointer_cast<ShapeType>(type); shape && shape->Decl) {
    visit(shape->Decl);
    // Multi-payload variants store their physical slots in SubMembers; the
    // variant's own ResolvedType is not the payload layout. Recurse through
    // those slots without reconstructing types or stripping owning hats.
    std::function<void(const ShapeMember &)> member = [&](const ShapeMember &field) {
      if (field.IsUnitVariant) return;
      if (!field.SubMembers.empty()) {
        for (const auto &payload : field.SubMembers) member(payload);
      } else {
        visitInterfaceCleanupTypes(field.ResolvedType, visited, visit);
      }
    };
    for (const auto &field : shape->Decl->Members)
      member(field);
  }
}
} // namespace toka
