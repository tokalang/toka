// Copyright (c) 2025 YiZhonghua<zhyi@dpai.com>. All rights reserved.
#include "toka/AST.h"
#include "toka/MemberAccess.h"
#include "toka/Sema.h"
#include "toka/Type.h"
#include <set>

namespace toka {

namespace {

SemanticOperation semanticOperation(PALOperationClass operation) {
  switch (operation) {
  case PALOperationClass::PayloadWrite:
    return SemanticOperation::PayloadWrite;
  case PALOperationClass::SharedPayloadBorrow:
    return SemanticOperation::SharedPayloadBorrow;
  case PALOperationClass::ExclusivePayloadBorrow:
    return SemanticOperation::ExclusivePayloadBorrow;
  case PALOperationClass::HandleViewBorrow:
    return SemanticOperation::HandleViewBorrow;
  case PALOperationClass::ExclusiveMutation:
    return SemanticOperation::ExclusiveMutation;
  case PALOperationClass::Invalidation:
    return SemanticOperation::Invalidation;
  }
  return SemanticOperation::SharedPayloadBorrow;
}

} // namespace

AccessPath Sema::makeAccessPath(Expr *E) {
  if (!E)
    return {};

  if (auto *variable = dynamic_cast<VariableExpr *>(E)) {
    AccessPath path;
    SymbolInfo *info = nullptr;
    Scope *owner = nullptr;
    std::string actualName = variable->Name;
    if (CurrentScope && CurrentScope->findVariableWithDerefScope(
                            variable->Name, info, actualName, owner)) {
      path.RootID = info ? info->SymbolID : 0;
      path.RootLoc = info ? info->DeclLoc : variable->Loc;
      path.RootName = Type::stripMorphology(actualName);
    } else {
      path.RootName = variable->Name;
      path.RootLoc = variable->Loc;
    }
    return path;
  }

  if (auto *cede = dynamic_cast<CedeExpr *>(E))
    return makeAccessPath(cede->Value.get());
  if (auto *postfix = dynamic_cast<PostfixExpr *>(E))
    return makeAccessPath(postfix->LHS.get());
  if (auto *address = dynamic_cast<AddressOfExpr *>(E))
    return makeAccessPath(address->Expression.get());

  if (auto *member = dynamic_cast<MemberExpr *>(E)) {
    AccessPath path = makeAccessPath(member->Object.get());
    if (!path)
      return {};
    MemberAccessIntent intent = parseMemberAccess(member->Member);
    path.Projections.push_back(AccessProjection::field(
        Type::stripMorphology(intent.StrippedName), member->Loc));
    return path;
  }

  if (auto *index = dynamic_cast<ArrayIndexExpr *>(E)) {
    AccessPath path = makeAccessPath(index->Array.get());
    if (!path)
      return {};
    for (const auto &item : index->Indices) {
      if (auto *number = dynamic_cast<NumberExpr *>(item.get())) {
        path.Projections.push_back(
            AccessProjection::constantIndex(number->Value, item->Loc));
      } else {
        path.Projections.push_back(AccessProjection::dynamicIndex(item->Loc));
      }
    }
    return path;
  }

  if (auto *deref = dynamic_cast<DereferenceExpr *>(E)) {
    AccessPath path = makeAccessPath(deref->Expression.get());
    if (path)
      path.Projections.push_back(AccessProjection::dereference(deref->Loc));
    return path;
  }

  if (auto *unary = dynamic_cast<UnaryExpr *>(E)) {
    AccessPath path = makeAccessPath(unary->RHS.get());
    if (path && unary->Op == TokenType::Star)
      path.Projections.push_back(AccessProjection::dereference(unary->Loc));
    return path;
  }

  if (auto *cast = dynamic_cast<CastExpr *>(E)) {
    AccessPath path = makeAccessPath(cast->Expression.get());
    if (path && cast->TargetType.find('*') != std::string::npos)
      path.Projections.push_back(AccessProjection::unknown(cast->Loc));
    return path;
  }

  return {};
}

AccessPath Sema::makeAccessPath(const std::string &Path) {
  if (Path.empty())
    return {};

  const size_t firstDot = Path.find('.');
  std::string root = Path.substr(0, firstDot);
  AccessPath result;
  SymbolInfo *info = nullptr;
  Scope *owner = nullptr;
  std::string actualName = root;
  if (CurrentScope && CurrentScope->findVariableWithDerefScope(
                          root, info, actualName, owner)) {
    result.RootID = info ? info->SymbolID : 0;
    result.RootLoc = info ? info->DeclLoc : SourceLocation{};
    result.RootName = Type::stripMorphology(actualName);
  } else {
    result.RootName = root;
  }

  size_t start = firstDot;
  while (start != std::string::npos) {
    const size_t next = Path.find('.', start + 1);
    std::string field = Path.substr(start + 1, next - start - 1);
    if (!field.empty()) {
      result.Projections.push_back(
          AccessProjection::field(Type::stripMorphology(field)));
    }
    start = next;
  }
  return result;
}

AccessPath Sema::canonicalizeAccessPath(const AccessPath &Path) {
  if (!Path || !CurrentScope)
    return Path;

  AccessPath current = Path;
  std::set<uint64_t> visited;
  while (current.RootID != 0 && visited.insert(current.RootID).second) {
    SymbolInfo *info = nullptr;
    std::string name;
    if (!CurrentScope->findSymbolByID(current.RootID, info, &name) || !info ||
        info->BorrowedFrom.empty()) {
      break;
    }

    AccessPath source = makeAccessPath(info->BorrowedFrom);
    if (!source)
      break;
    source.Projections.insert(source.Projections.end(),
                              current.Projections.begin(),
                              current.Projections.end());
    current = std::move(source);
  }
  return current;
}

void Sema::recordPALDecision(ASTNode *Node, SemanticRuleID Rule,
                             PALOperationClass Operation,
                             const AccessPath &Subject,
                             const std::optional<PALConflict> &Conflict,
                             SemanticDecision Decision, SemanticReason Reason,
                             bool ReportOrigin) {
  if (m_IsPrecomputingCaptures)
    return;
  SourceLocation primaryLoc = Node ? Node->Loc : SourceLocation{};
  SourceLocation originLoc = Conflict ? Conflict->OriginLoc : SourceLocation{};
  SemanticEvidence::record(
      Rule, semanticOperation(Operation), Decision, Reason,
      Subject.toLegacyString(), Conflict ? Conflict->displayPath() : "",
      primaryLoc, originLoc);
  if (ReportOrigin && Conflict && originLoc.isValid()) {
    DiagnosticEngine::report(originLoc, DiagID::NOTE_GENERIC,
                             "conflicting borrow originates here");
  }
}

void Sema::recordPALConflict(ASTNode *Node, PALOperationClass Operation,
                             const AccessPath &Subject,
                             const PALConflict &Conflict,
                             bool ReportOrigin) {
  bool shared = Conflict.State == PathState::BorrowedShared;
  SemanticRuleID rule = shared ? SemanticRuleID::PALBorrow002
                               : SemanticRuleID::PALBorrow001;
  SemanticReason reason = shared ? SemanticReason::ActiveSharedBorrow
                                 : SemanticReason::ActiveExclusiveBorrow;
  recordPALDecision(Node, rule, Operation, Subject, Conflict,
                    SemanticDecision::Reject, reason, ReportOrigin);
}

void Sema::recordDecision(ASTNode *Node, SemanticRuleID Rule,
                          SemanticOperation Operation,
                          SemanticDecision Decision, SemanticReason Reason,
                          const std::string &Subject,
                          const std::string &Origin,
                          SourceLocation OriginLoc) {
  if (m_IsPrecomputingCaptures)
    return;
  SemanticEvidence::record(Rule, Operation, Decision, Reason, Subject, Origin,
                           Node ? Node->Loc : SourceLocation{}, OriginLoc);
}

SourceLocation Sema::findPathDeclaration(const std::string &Path) {
  if (!CurrentScope || Path.empty())
    return {};
  std::string root = Path.substr(0, Path.find('.'));
  SymbolInfo *info = nullptr;
  std::string actualName;
  if (CurrentScope->findVariableWithDeref(root, info, actualName) && info)
    return info->DeclLoc;
  return {};
}

bool Sema::isBorrowAccessAuthorized(const AccessPath &Path,
                                    const std::string &ConflictPath) {
  if (!Path || Path.RootID == 0 || ConflictPath.empty() || !CurrentScope)
    return false;

  SymbolInfo *info = nullptr;
  if (!CurrentScope->findSymbolByID(Path.RootID, info) || !info ||
      info->BorrowedFrom.empty()) {
    return false;
  }

  AccessPath borrowed =
      canonicalizeAccessPath(makeAccessPath(info->BorrowedFrom));
  AccessPath conflict = canonicalizeAccessPath(makeAccessPath(ConflictPath));
  return borrowed && conflict && accessPathsMayOverlap(borrowed, conflict);
}

std::string Sema::getPathString(Expr *E) {
  return makeAccessPath(E).toLegacyString();
}

SymbolInfo *Sema::resolveBorrowSource(SymbolInfo *Info,
                                      std::string &EffectiveName) {
  if (!Info || !CurrentScope)
    return Info;

  std::set<uint64_t> visited;
  SymbolInfo *current = Info;
  while (current && current->SymbolID != 0 &&
         visited.insert(current->SymbolID).second &&
         !current->BorrowedFrom.empty()) {
    AccessPath source = makeAccessPath(current->BorrowedFrom);
    if (!source || source.RootID == 0)
      break;
    SymbolInfo *next = nullptr;
    std::string nextName;
    if (!CurrentScope->findSymbolByID(source.RootID, next, &nextName) || !next)
      break;
    EffectiveName = nextName;
    current = next;
  }
  return current;
}

} // namespace toka
