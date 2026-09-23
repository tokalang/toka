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
    for (const auto &field : shape->Decl->Members)
      visitInterfaceCleanupTypes(field.ResolvedType, visited, visit);
  }
}
} // namespace toka
