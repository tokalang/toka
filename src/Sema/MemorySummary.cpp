// Copyright (c) 2025 YiZhonghua<zhyi@dpai.com>. All rights reserved.
//
// Licensed under the Apache License, Version 2.0.
#include "toka/MemorySummary.h"
#include "toka/AST.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Module.h"
#include <algorithm>
#include <ostream>
#include <set>

namespace toka {
namespace {

using RootSet = std::set<std::string>;

uint32_t bit(MemoryRootEffect effect) {
  return static_cast<uint32_t>(effect);
}

uint32_t bit(FunctionMemoryEffect effect) {
  return static_cast<uint32_t>(effect);
}

bool has(uint32_t effects, MemoryRootEffect effect) {
  return (effects & bit(effect)) != 0;
}

bool has(uint32_t effects, FunctionMemoryEffect effect) {
  return (effects & bit(effect)) != 0;
}

std::string rootName(const FunctionDecl::Arg &arg) {
  return Type::stripMorphology(arg.Name);
}

bool isMemoryBearing(const FunctionDecl::Arg &arg) {
  if (arg.IsRawPointer || arg.IsUnique || arg.IsShared || arg.IsReference)
    return true;
  if (!arg.ResolvedType)
    return false;
  return arg.ResolvedType->isPointer() || arg.ResolvedType->isReference() ||
         arg.ResolvedType->isSmartPointer() || arg.ResolvedType->isShape() ||
         arg.ResolvedType->isArray();
}

void addLocal(FunctionMemorySummary &summary, const RootSet &roots,
              uint32_t effects) {
  for (const auto &root : roots) {
    auto &entry = summary.Roots[root];
    entry.LocalEffects |= effects;
    entry.Effects |= effects;
  }
}

void addLocal(FunctionMemorySummary &summary, FunctionMemoryEffect effect) {
  summary.LocalEffects |= bit(effect);
  summary.Effects |= bit(effect);
}

struct CallEdge {
  FunctionDecl *Caller = nullptr;
  FunctionDecl *Callee = nullptr;
  std::vector<RootSet> ActualRoots;
};

std::vector<FunctionDecl *> collectFunctionsImpl(
    const std::vector<Module *> &modules) {
  std::vector<FunctionDecl *> functions;
  for (Module *module : modules) {
    if (!module)
      continue;
    for (const auto &fn : module->Functions)
      if (fn->GenericParams.empty())
        functions.push_back(fn.get());
    for (const auto &impl : module->Impls) {
      if (!impl->GenericParams.empty())
        continue;
      for (const auto &method : impl->Methods) {
        if (!method->GenericParams.empty())
          continue;
        if (method->CodegenName.empty()) {
          method->CodegenName = impl->TraitName.empty()
                                    ? impl->TypeName + "_" + method->Name
                                    : impl->TraitName + "_" + impl->TypeName +
                                          "_" + method->Name;
        }
        functions.push_back(method.get());
      }
    }
    for (const auto &trait : module->Traits)
      for (const auto &method : trait->Methods) {
        method->MemorySummary.FunctionName =
            "@trait:" + trait->Name + ":" + method->Name;
        functions.push_back(method.get());
      }
  }
  std::sort(functions.begin(), functions.end(), [](const FunctionDecl *lhs,
                                                    const FunctionDecl *rhs) {
    const std::string &ln = lhs->CodegenName.empty() ? lhs->Name : lhs->CodegenName;
    const std::string &rn = rhs->CodegenName.empty() ? rhs->Name : rhs->CodegenName;
    if (ln != rn)
      return ln < rn;
    return lhs->NodeSerial < rhs->NodeSerial;
  });
  functions.erase(std::unique(functions.begin(), functions.end()),
                  functions.end());
  return functions;
}

class LocalAnalyzer {
public:
  LocalAnalyzer(FunctionDecl &function, std::vector<CallEdge> &edges,
                bool borrowCheckEnabled, const std::set<std::string> &globals)
      : Function(function), Summary(function.MemorySummary), Edges(edges),
        BorrowCheckEnabled(borrowCheckEnabled), Globals(globals) {
    std::string retainedName = Summary.FunctionName;
    Summary = {};
    Summary.FunctionName = !function.CodegenName.empty()
                               ? function.CodegenName
                               : (!retainedName.empty() ? retainedName
                                                        : function.Name);
    Summary.Origin = function.Body ? MemorySummaryOrigin::SourceBody
                                   : MemorySummaryOrigin::SignatureOnly;
    for (const auto &arg : function.Args) {
      std::string name = rootName(arg);
      Summary.Roots[name] = {};
      Aliases[name].insert(name);
    }
  }

  void run() {
    applyRequiredSignatureEffects();
    if (Function.Body)
      visitStmt(Function.Body.get());
    else
      applyOpaqueSignatureEffects();

    if (!BorrowCheckEnabled) {
      addLocal(Summary, FunctionMemoryEffect::UnknownBoundary);
      for (const auto &arg : Function.Args) {
        if (isMemoryBearing(arg))
          addLocal(Summary, RootSet{rootName(arg)}, bit(MemoryRootEffect::Unknown));
      }
    }
  }

private:
  FunctionDecl &Function;
  FunctionMemorySummary &Summary;
  std::vector<CallEdge> &Edges;
  bool BorrowCheckEnabled;
  const std::set<std::string> &Globals;
  std::map<std::string, RootSet> Aliases;

  void applyRequiredSignatureEffects() {
    if (Function.Effect == EffectKind::Async) {
      addLocal(Summary, FunctionMemoryEffect::Suspend);
      // Coroutine frame allocation is an observable lowering requirement.
      addLocal(Summary, FunctionMemoryEffect::Allocate);
    }
    for (const auto &arg : Function.Args) {
      RootSet root{rootName(arg)};
      if (arg.IsCeded)
        addLocal(Summary, root, bit(MemoryRootEffect::Transfer) |
                                    bit(MemoryRootEffect::Invalidate) |
                                    bit(MemoryRootEffect::Escape));
      if (arg.IsRawPointer) {
        addLocal(Summary, FunctionMemoryEffect::RawProvenance);
        addLocal(Summary, FunctionMemoryEffect::UnknownBoundary);
        addLocal(Summary, root, bit(MemoryRootEffect::Unknown));
      }
      if (Function.Effect == EffectKind::Async && isMemoryBearing(arg))
        addLocal(Summary, root, bit(MemoryRootEffect::Capture) |
                                    bit(MemoryRootEffect::Escape));
    }
  }

  void applyOpaqueSignatureEffects() {
    addLocal(Summary, FunctionMemoryEffect::UnknownBoundary);
    for (const auto &arg : Function.Args) {
      RootSet root{rootName(arg)};
      addLocal(Summary, root, bit(MemoryRootEffect::Read));
      if (arg.IsValueMutable)
        addLocal(Summary, root, bit(MemoryRootEffect::Write));
      if (arg.IsRebindable)
        addLocal(Summary, root, bit(MemoryRootEffect::Write) |
                                    bit(MemoryRootEffect::Rebind));
      if (isMemoryBearing(arg))
        addLocal(Summary, root, bit(MemoryRootEffect::Unknown));
    }
    for (const auto &dep : Function.LifeDependencies)
      addLocal(Summary, rootsForName(dep), bit(MemoryRootEffect::Escape));
    for (const auto &member : Function.MemberDependencies)
      for (const auto &dep : member.second)
        addLocal(Summary, rootsForName(dep), bit(MemoryRootEffect::Escape));
  }

  RootSet rootsForName(const std::string &name) const {
    std::string stripped = Type::stripMorphology(name);
    auto it = Aliases.find(stripped);
    if (it != Aliases.end())
      return it->second;
    return {};
  }

  void mergeAlias(const std::string &name, const RootSet &roots) {
    auto &target = Aliases[Type::stripMorphology(name)];
    target.insert(roots.begin(), roots.end());
  }

  RootSet rootsFromStmt(const Stmt *stmt) const {
    if (!stmt)
      return {};
    if (auto *expr = dynamic_cast<const Expr *>(stmt))
      return roots(expr);
    if (auto *expr = dynamic_cast<const ExprStmt *>(stmt))
      return roots(expr->Expression.get());
    if (auto *ret = dynamic_cast<const ReturnStmt *>(stmt))
      return roots(ret->ReturnValue.get());
    RootSet result;
    if (auto *block = dynamic_cast<const BlockStmt *>(stmt)) {
      for (const auto &child : block->Statements) {
        RootSet childRoots = rootsFromStmt(child.get());
        result.insert(childRoots.begin(), childRoots.end());
      }
    }
    return result;
  }

  RootSet roots(const Expr *expr) const {
    if (!expr)
      return {};
    if (auto *value = dynamic_cast<const VariableExpr *>(expr))
      return rootsForName(value->Name);
    if (auto *member = dynamic_cast<const MemberExpr *>(expr))
      return roots(member->Object.get());
    if (auto *index = dynamic_cast<const ArrayIndexExpr *>(expr))
      return roots(index->Array.get());
    if (auto *value = dynamic_cast<const DereferenceExpr *>(expr))
      return roots(value->Expression.get());
    if (auto *value = dynamic_cast<const AddressOfExpr *>(expr))
      return roots(value->Expression.get());
    if (auto *value = dynamic_cast<const CastExpr *>(expr))
      return roots(value->Expression.get());
    if (auto *value = dynamic_cast<const UnaryExpr *>(expr))
      return roots(value->RHS.get());
    if (auto *value = dynamic_cast<const PostfixExpr *>(expr))
      return roots(value->LHS.get());
    if (auto *value = dynamic_cast<const UnwrapPropagationExpr *>(expr))
      return roots(value->Base.get());
    if (auto *value = dynamic_cast<const CedeExpr *>(expr))
      return roots(value->Value.get());
    if (auto *value = dynamic_cast<const PassExpr *>(expr))
      return roots(value->Value.get());
    if (auto *value = dynamic_cast<const SpreadExpr *>(expr))
      return roots(value->Base.get());
    if (auto *value = dynamic_cast<const ElisionExpr *>(expr))
      return roots(value->Target.get());
    if (auto *value = dynamic_cast<const AwaitExpr *>(expr))
      return roots(value->Expression.get());
    if (auto *value = dynamic_cast<const WaitExpr *>(expr))
      return roots(value->Expression.get());
    if (auto *value = dynamic_cast<const StartExpr *>(expr))
      return roots(value->Expression.get());
    RootSet result;
    auto merge = [&](const RootSet &other) {
      result.insert(other.begin(), other.end());
    };
    if (auto *binary = dynamic_cast<const BinaryExpr *>(expr)) {
      merge(roots(binary->LHS.get()));
      merge(roots(binary->RHS.get()));
    } else if (auto *call = dynamic_cast<const CallExpr *>(expr)) {
      if (call->ResolvedShape) {
        for (const auto &argument : call->Args)
          merge(roots(argument.get()));
      } else if (call->ResolvedFn) {
        auto mergeDependency = [&](const std::string &dependency) {
          for (size_t i = 0; i < call->ResolvedFn->Args.size() &&
                             i < call->Args.size();
               ++i)
            if (rootName(call->ResolvedFn->Args[i]) ==
                Type::stripMorphology(dependency))
              merge(roots(call->Args[i].get()));
        };
        for (const auto &dependency : call->ResolvedFn->LifeDependencies)
          mergeDependency(dependency);
        for (const auto &member : call->ResolvedFn->MemberDependencies)
          for (const auto &dependency : member.second)
            mergeDependency(dependency);
      }
    } else if (auto *call = dynamic_cast<const MethodCallExpr *>(expr)) {
      if (call->ResolvedFn) {
        std::vector<const Expr *> arguments{call->Object.get()};
        for (const auto &argument : call->Args)
          arguments.push_back(argument.get());
        auto mergeDependency = [&](const std::string &dependency) {
          for (size_t i = 0; i < call->ResolvedFn->Args.size() &&
                             i < arguments.size();
               ++i)
            if (rootName(call->ResolvedFn->Args[i]) ==
                Type::stripMorphology(dependency))
              merge(roots(arguments[i]));
        };
        for (const auto &dependency : call->ResolvedFn->LifeDependencies)
          mergeDependency(dependency);
        for (const auto &member : call->ResolvedFn->MemberDependencies)
          for (const auto &dependency : member.second)
            mergeDependency(dependency);
      }
    } else if (auto *array = dynamic_cast<const ArrayExpr *>(expr))
      for (const auto &item : array->Elements)
        merge(roots(item.get()));
    else if (auto *record = dynamic_cast<const AnonymousRecordExpr *>(expr))
      for (const auto &field : record->Fields)
        merge(roots(field.second.get()));
    else if (auto *init = dynamic_cast<const InitStructExpr *>(expr))
      for (const auto &field : init->Members)
        merge(roots(field.second.get()));
    else if (auto *value = dynamic_cast<const AllocExpr *>(expr))
      merge(roots(value->Initializer.get()));
    else if (auto *value = dynamic_cast<const NewExpr *>(expr))
      merge(roots(value->Initializer.get()));
    else if (auto *value = dynamic_cast<const ArrayInitExpr *>(expr))
      merge(roots(value->Initializer.get()));
    else if (auto *value = dynamic_cast<const RepeatedArrayExpr *>(expr))
      merge(roots(value->Value.get()));
    else if (auto *value = dynamic_cast<const IfExpr *>(expr)) {
      merge(rootsFromStmt(value->Then.get()));
      merge(rootsFromStmt(value->Else.get()));
    } else if (auto *value = dynamic_cast<const GuardExpr *>(expr)) {
      merge(rootsFromStmt(value->Then.get()));
      merge(rootsFromStmt(value->Else.get()));
    } else if (auto *value = dynamic_cast<const MatchExpr *>(expr)) {
      for (const auto &arm : value->Arms)
        merge(rootsFromStmt(arm->Body.get()));
    } else if (auto *value = dynamic_cast<const LoopExpr *>(expr))
      merge(rootsFromStmt(value->Body.get()));
    else if (auto *value = dynamic_cast<const ForExpr *>(expr))
      merge(rootsFromStmt(value->Body.get()));
    return result;
  }

  RootSet provenanceRoots(const Expr *expr) const {
    if (!expr)
      return {};
    const auto &type = expr->ResolvedType;
    if (!type)
      return roots(expr);
    return mayCarryProvenance(type) ? roots(expr) : RootSet{};
  }

  static bool mayCarryProvenance(const std::shared_ptr<Type> &type) {
    if (!type)
      return true;
    return type->isPointer() || type->isReference() ||
           type->isSmartPointer() || type->isShape() || type->isArray() ||
           type->isSlice() || type->isFunction() || type->isDynFn() ||
           type->isUnknown() || type->isUninit();
  }

  void markUnknownBoundary(const std::vector<const Expr *> &arguments) {
    addLocal(Summary, FunctionMemoryEffect::UnknownCall);
    addLocal(Summary, FunctionMemoryEffect::UnknownBoundary);
    for (const Expr *argument : arguments)
      addLocal(Summary, roots(argument), bit(MemoryRootEffect::Read) |
                                         bit(MemoryRootEffect::Write) |
                                         bit(MemoryRootEffect::Capture) |
                                         bit(MemoryRootEffect::Unknown));
  }

  void recordCall(FunctionDecl *callee,
                  const std::vector<const Expr *> &arguments) {
    if (!callee) {
      markUnknownBoundary(arguments);
      return;
    }
    CallEdge edge;
    edge.Caller = &Function;
    edge.Callee = callee;
    for (const Expr *argument : arguments)
      edge.ActualRoots.push_back(roots(argument));
    Edges.push_back(std::move(edge));
  }

  static bool isAssignment(const std::string &op) {
    return op == "=" || op == "+=" || op == "-=" || op == "*=" ||
           op == "/=" || op == "%=" || op == "&=" || op == "|=" ||
           op == "^=" || op == "<<=" || op == ">>=";
  }

  void visitExpr(const Expr *expr) {
    if (!expr)
      return;

    if (auto *variable = dynamic_cast<const VariableExpr *>(expr)) {
      std::string name = Type::stripMorphology(variable->Name);
      if (Globals.count(name) && !Aliases.count(name))
        addLocal(Summary, FunctionMemoryEffect::TouchGlobal);
    }

    if (expr->ResolvedType && expr->ResolvedType->isRawPointer()) {
      addLocal(Summary, FunctionMemoryEffect::RawProvenance);
      addLocal(Summary, FunctionMemoryEffect::UnknownBoundary);
      addLocal(Summary, roots(expr), bit(MemoryRootEffect::Unknown));
    }

    if (auto *binary = dynamic_cast<const BinaryExpr *>(expr)) {
      if (isAssignment(binary->Op)) {
        visitExpr(binary->RHS.get());
        visitExpr(binary->LHS.get());
        if (auto *variable = dynamic_cast<const VariableExpr *>(binary->LHS.get())) {
          std::string name = Type::stripMorphology(variable->Name);
          if (Globals.count(name) && !Aliases.count(name)) {
            addLocal(Summary, FunctionMemoryEffect::TouchGlobal);
            addLocal(Summary, provenanceRoots(binary->RHS.get()),
                     bit(MemoryRootEffect::Capture) |
                         bit(MemoryRootEffect::Escape));
          }
        }
        RootSet lhsRoots = roots(binary->LHS.get());
        uint32_t effects = bit(MemoryRootEffect::Write);
        if (binary->AssignmentKind == AssignmentSemanticKind::Handle)
          effects |= bit(MemoryRootEffect::Rebind);
        addLocal(Summary, lhsRoots, effects);
        std::string lhsVarName = "";
        if (auto *variable =
                dynamic_cast<const VariableExpr *>(binary->LHS.get())) {
          lhsVarName = variable->Name;
        } else if (auto *unary =
                       dynamic_cast<const UnaryExpr *>(binary->LHS.get())) {
          if (auto *varInUnary =
                  dynamic_cast<const VariableExpr *>(unary->RHS.get())) {
            lhsVarName = varInUnary->Name;
          }
        }
        if (!lhsVarName.empty()) {
          mergeAlias(lhsVarName, provenanceRoots(binary->RHS.get()));
        } else {
          addLocal(Summary, provenanceRoots(binary->RHS.get()),
                   bit(MemoryRootEffect::Capture) |
                       bit(MemoryRootEffect::Escape));
        }
        return;
      }
      if (!mayCarryProvenance(binary->ResolvedType)) {
        if (binary->LHS && mayCarryProvenance(binary->LHS->ResolvedType))
          addLocal(Summary, roots(binary->LHS.get()),
                   bit(MemoryRootEffect::Capture));
        if (binary->RHS && mayCarryProvenance(binary->RHS->ResolvedType))
          addLocal(Summary, roots(binary->RHS.get()),
                   bit(MemoryRootEffect::Capture));
      }
      visitExpr(binary->LHS.get());
      visitExpr(binary->RHS.get());
      return;
    }
    if (auto *call = dynamic_cast<const CallExpr *>(expr)) {
      std::vector<const Expr *> args;
      for (const auto &arg : call->Args) {
        visitExpr(arg.get());
        args.push_back(arg.get());
      }
      if (call->ResolvedShape)
        return;
      recordCall(call->ResolvedFn, args);
      return;
    }
    if (auto *call = dynamic_cast<const MethodCallExpr *>(expr)) {
      std::vector<const Expr *> args{call->Object.get()};
      visitExpr(call->Object.get());
      for (const auto &arg : call->Args) {
        visitExpr(arg.get());
        args.push_back(arg.get());
      }
      recordCall(call->ResolvedFn, args);
      return;
    }
    if (auto *value = dynamic_cast<const CedeExpr *>(expr)) {
      visitExpr(value->Value.get());
      addLocal(Summary, roots(value->Value.get()),
               bit(MemoryRootEffect::Transfer) |
                   bit(MemoryRootEffect::Invalidate) |
                   bit(MemoryRootEffect::Escape));
      return;
    }
    if (auto *value = dynamic_cast<const CastExpr *>(expr)) {
      visitExpr(value->Expression.get());
      if (!mayCarryProvenance(value->ResolvedType))
        addLocal(Summary, roots(value->Expression.get()),
                 bit(MemoryRootEffect::Capture));
      return;
    }
    if (auto *value = dynamic_cast<const UnsafeExpr *>(expr)) {
      addLocal(Summary, FunctionMemoryEffect::UnsafeBoundary);
      addLocal(Summary, FunctionMemoryEffect::UnknownBoundary);
      visitExpr(value->Expression.get());
      addLocal(Summary, roots(value->Expression.get()),
               bit(MemoryRootEffect::Unknown));
      return;
    }
    if (auto *value = dynamic_cast<const AllocExpr *>(expr)) {
      addLocal(Summary, FunctionMemoryEffect::Allocate);
      visitExpr(value->Initializer.get());
      visitExpr(value->ArraySize.get());
      return;
    }
    if (auto *value = dynamic_cast<const NewExpr *>(expr)) {
      addLocal(Summary, FunctionMemoryEffect::Allocate);
      visitExpr(value->Initializer.get());
      visitExpr(value->ArraySize.get());
      return;
    }
    if (auto *value = dynamic_cast<const ArrayInitExpr *>(expr)) {
      visitExpr(value->Initializer.get());
      visitExpr(value->ArraySize.get());
      return;
    }
    if (auto *value = dynamic_cast<const StartExpr *>(expr)) {
      addLocal(Summary, FunctionMemoryEffect::Suspend);
      visitExpr(value->Expression.get());
      addLocal(Summary, roots(value->Expression.get()),
               bit(MemoryRootEffect::Capture) |
                   bit(MemoryRootEffect::Escape));
      return;
    }
    if (auto *value = dynamic_cast<const AwaitExpr *>(expr)) {
      addLocal(Summary, FunctionMemoryEffect::Suspend);
      visitExpr(value->Expression.get());
      return;
    }
    if (auto *value = dynamic_cast<const WaitExpr *>(expr)) {
      addLocal(Summary, FunctionMemoryEffect::Suspend);
      visitExpr(value->Expression.get());
      return;
    }
    if (auto *closure = dynamic_cast<const ClosureExpr *>(expr)) {
      for (const auto &capture : closure->ExplicitCaptures)
        addLocal(Summary, rootsForName(capture.Name),
                 bit(MemoryRootEffect::Capture) |
                     bit(MemoryRootEffect::Escape));
      for (const auto &capture : closure->ImplicitCaptures)
        addLocal(Summary, rootsForName(capture),
                 bit(MemoryRootEffect::Capture) |
                     bit(MemoryRootEffect::Escape));
      visitStmt(closure->Body.get());
      return;
    }
    if (auto *value = dynamic_cast<const IfExpr *>(expr)) {
      visitExpr(value->Condition.get());
      visitStmt(value->Then.get());
      visitStmt(value->Else.get());
      return;
    }
    if (auto *value = dynamic_cast<const GuardExpr *>(expr)) {
      visitExpr(value->Condition.get());
      visitStmt(value->Then.get());
      visitStmt(value->Else.get());
      return;
    }
    if (auto *value = dynamic_cast<const LoopExpr *>(expr)) {
      visitExpr(value->Condition.get());
      visitStmt(value->Body.get());
      return;
    }
    if (auto *value = dynamic_cast<const ForExpr *>(expr)) {
      visitExpr(value->Collection.get());
      mergeAlias(value->VarName, roots(value->Collection.get()));
      visitStmt(value->Body.get());
      visitStmt(value->ElseBody.get());
      for (const auto &body : value->UnrolledBodies)
        visitStmt(body.get());
      return;
    }
    if (auto *value = dynamic_cast<const MatchExpr *>(expr)) {
      visitExpr(value->Target.get());
      for (const auto &arm : value->Arms) {
        visitExpr(arm->Guard.get());
        visitStmt(arm->Body.get());
      }
      return;
    }
    if (auto *value = dynamic_cast<const MemberExpr *>(expr))
      visitExpr(value->Object.get());
    else if (auto *value = dynamic_cast<const ArrayIndexExpr *>(expr)) {
      visitExpr(value->Array.get());
      for (const auto &index : value->Indices)
        visitExpr(index.get());
    } else if (auto *value = dynamic_cast<const DereferenceExpr *>(expr))
      visitExpr(value->Expression.get());
    else if (auto *value = dynamic_cast<const AddressOfExpr *>(expr))
      visitExpr(value->Expression.get());
    else if (auto *value = dynamic_cast<const UnaryExpr *>(expr))
      visitExpr(value->RHS.get());
    else if (auto *value = dynamic_cast<const PostfixExpr *>(expr))
      visitExpr(value->LHS.get());
    else if (auto *value = dynamic_cast<const UnwrapPropagationExpr *>(expr))
      visitExpr(value->Base.get());
    else if (auto *value = dynamic_cast<const PassExpr *>(expr))
      visitExpr(value->Value.get());
    else if (auto *value = dynamic_cast<const SpreadExpr *>(expr))
      visitExpr(value->Base.get());
    else if (auto *value = dynamic_cast<const ElisionExpr *>(expr))
      visitExpr(value->Target.get());
    else if (auto *value = dynamic_cast<const BreakExpr *>(expr))
      visitExpr(value->Value.get());
    else if (auto *value = dynamic_cast<const ArrayExpr *>(expr))
      for (const auto &item : value->Elements)
        visitExpr(item.get());
    else if (auto *value = dynamic_cast<const RepeatedArrayExpr *>(expr)) {
      visitExpr(value->Value.get());
      visitExpr(value->Count.get());
    } else if (auto *value = dynamic_cast<const InitStructExpr *>(expr))
      for (const auto &field : value->Members)
        visitExpr(field.second.get());
    else if (auto *value = dynamic_cast<const AnonymousRecordExpr *>(expr))
      for (const auto &field : value->Fields)
        visitExpr(field.second.get());

    addLocal(Summary, roots(expr), bit(MemoryRootEffect::Read));
  }

  void visitStmt(const Stmt *stmt) {
    if (!stmt)
      return;
    if (auto *block = dynamic_cast<const BlockStmt *>(stmt)) {
      for (const auto &child : block->Statements)
        visitStmt(child.get());
    } else if (auto *ret = dynamic_cast<const ReturnStmt *>(stmt)) {
      visitExpr(ret->ReturnValue.get());
      addLocal(Summary, provenanceRoots(ret->ReturnValue.get()),
               bit(MemoryRootEffect::Escape));
    } else if (auto *expr = dynamic_cast<const ExprStmt *>(stmt)) {
      visitExpr(expr->Expression.get());
    } else if (auto *decl = dynamic_cast<const VariableDecl *>(stmt)) {
      visitExpr(decl->Init.get());
      std::string stripped = Type::stripMorphology(decl->Name);
      Aliases[stripped].insert(stripped);
      mergeAlias(decl->Name, provenanceRoots(decl->Init.get()));
    } else if (auto *decl = dynamic_cast<const DestructuringDecl *>(stmt)) {
      visitExpr(decl->Init.get());
      RootSet initRoots = roots(decl->Init.get());
      for (const auto &variable : decl->Variables) {
        if (!variable.IsWildcard) {
          std::string stripped = Type::stripMorphology(variable.Name);
          Aliases[stripped].insert(stripped);
          mergeAlias(variable.Name, initRoots);
        }
      }
    } else if (auto *value = dynamic_cast<const DeleteStmt *>(stmt)) {
      visitExpr(value->Expression.get());
      addLocal(Summary, FunctionMemoryEffect::Free);
      addLocal(Summary, roots(value->Expression.get()),
               bit(MemoryRootEffect::Invalidate));
    } else if (auto *value = dynamic_cast<const FreeStmt *>(stmt)) {
      visitExpr(value->Expression.get());
      visitExpr(value->Count.get());
      addLocal(Summary, FunctionMemoryEffect::Free);
      addLocal(Summary, roots(value->Expression.get()),
               bit(MemoryRootEffect::Invalidate));
    } else if (auto *value = dynamic_cast<const UnsafeStmt *>(stmt)) {
      addLocal(Summary, FunctionMemoryEffect::UnsafeBoundary);
      addLocal(Summary, FunctionMemoryEffect::UnknownBoundary);
      visitStmt(value->Statement.get());
      for (const auto &arg : Function.Args)
        if (isMemoryBearing(arg))
          addLocal(Summary, RootSet{rootName(arg)},
                   bit(MemoryRootEffect::Unknown));
    } else if (auto *value = dynamic_cast<const GuardBindStmt *>(stmt)) {
      visitExpr(value->Target.get());
      visitStmt(value->ElseBody.get());
    } else if (auto *expr = dynamic_cast<const Expr *>(stmt)) {
      visitExpr(expr);
    }
  }
};

void propagate(const std::vector<CallEdge> &edges) {
  bool changed;
  do {
    changed = false;
    for (const auto &edge : edges) {
      if (!edge.Callee)
        continue;
      auto &caller = edge.Caller->MemorySummary;
      const auto &callee = edge.Callee->MemorySummary;
      uint32_t oldEffects = caller.Effects;
      caller.Effects |= callee.Effects;
      changed |= oldEffects != caller.Effects;
      for (size_t i = 0; i < edge.ActualRoots.size() &&
                         i < edge.Callee->Args.size();
           ++i) {
        std::string calleeRoot = rootName(edge.Callee->Args[i]);
        auto found = callee.Roots.find(calleeRoot);
        if (found == callee.Roots.end())
          continue;
        for (const auto &callerRoot : edge.ActualRoots[i]) {
          auto &target = caller.Roots[callerRoot];
          uint32_t old = target.Effects;
          target.Effects |= found->second.Effects;
          changed |= old != target.Effects;
        }
      }
    }
  } while (changed);
}

std::string escapeJSON(const std::string &value) {
  std::string result;
  for (unsigned char ch : value) {
    switch (ch) {
    case '\\': result += "\\\\"; break;
    case '"': result += "\\\""; break;
    case '\n': result += "\\n"; break;
    case '\r': result += "\\r"; break;
    case '\t': result += "\\t"; break;
    default:
      if (ch >= 0x20)
        result += static_cast<char>(ch);
      break;
    }
  }
  return result;
}

template <typename Enum>
std::vector<const char *> effectNames(uint32_t effects);

template <>
std::vector<const char *> effectNames<MemoryRootEffect>(uint32_t effects) {
  std::vector<const char *> result;
  const std::pair<MemoryRootEffect, const char *> names[] = {
      {MemoryRootEffect::Read, "read"},
      {MemoryRootEffect::Write, "write"},
      {MemoryRootEffect::Rebind, "rebind"},
      {MemoryRootEffect::Invalidate, "invalidate"},
      {MemoryRootEffect::Capture, "capture"},
      {MemoryRootEffect::Escape, "escape"},
      {MemoryRootEffect::Transfer, "transfer"},
      {MemoryRootEffect::Unknown, "unknown"},
  };
  for (const auto &entry : names)
    if (has(effects, entry.first))
      result.push_back(entry.second);
  return result;
}

template <>
std::vector<const char *> effectNames<FunctionMemoryEffect>(uint32_t effects) {
  std::vector<const char *> result;
  const std::pair<FunctionMemoryEffect, const char *> names[] = {
      {FunctionMemoryEffect::Allocate, "allocate"},
      {FunctionMemoryEffect::Free, "free"},
      {FunctionMemoryEffect::TouchGlobal, "touch_global"},
      {FunctionMemoryEffect::UnknownCall, "unknown_call"},
      {FunctionMemoryEffect::RawProvenance, "raw_provenance"},
      {FunctionMemoryEffect::UnsafeBoundary, "unsafe_boundary"},
      {FunctionMemoryEffect::Suspend, "suspend"},
      {FunctionMemoryEffect::UnknownBoundary, "unknown_boundary"},
  };
  for (const auto &entry : names)
    if (has(effects, entry.first))
      result.push_back(entry.second);
  return result;
}

void dumpNames(std::ostream &out, const std::vector<const char *> &names) {
  out << '[';
  for (size_t i = 0; i < names.size(); ++i) {
    if (i)
      out << ',';
    out << '"' << names[i] << '"';
  }
  out << ']';
}

} // namespace

std::vector<FunctionDecl *> MemorySummaryAnalysis::collectFunctions(
    const std::vector<Module *> &modules) {
  return collectFunctionsImpl(modules);
}

void MemorySummaryAnalysis::run(const std::vector<Module *> &modules,
                                bool borrowCheckEnabled,
                                bool activateTrustedEvidence) {
  std::vector<CallEdge> edges;
  std::set<std::string> globals;
  for (Module *module : modules) {
    if (!module)
      continue;
    for (const auto &global : module->Globals)
      if (auto *decl = dynamic_cast<VariableDecl *>(global.get()))
        globals.insert(Type::stripMorphology(decl->Name));
  }
  std::vector<FunctionDecl *> functions = collectFunctionsImpl(modules);
  std::set<FunctionDecl *> analyzed(functions.begin(), functions.end());
  for (FunctionDecl *function : functions)
    LocalAnalyzer(*function, edges, borrowCheckEnabled, globals).run();
  if (borrowCheckEnabled && activateTrustedEvidence) {
    for (Module *module : modules) {
      if (!module || module->TrustedMemorySummaries.empty())
        continue;
      std::vector<Module *> oneModule = {module};
      std::map<std::string, FunctionDecl *> declarations;
      for (FunctionDecl *function : collectFunctionsImpl(oneModule)) {
        const std::string &name = function->MemorySummary.FunctionName;
        if (name.empty() || !declarations.emplace(name, function).second) {
          declarations.clear();
          break;
        }
      }
      bool valid = !declarations.empty();
      for (const auto &entry : module->TrustedMemorySummaries) {
        auto declaration = declarations.find(entry.first);
        if (declaration == declarations.end()) {
          valid = false;
          break;
        }
        std::set<std::string> expectedRoots;
        for (const auto &argument : declaration->second->Args)
          expectedRoots.insert(rootName(argument));
        std::set<std::string> cachedRoots;
        for (const auto &root : entry.second.Roots)
          cachedRoots.insert(root.first);
        if (expectedRoots != cachedRoots) {
          valid = false;
          break;
        }
      }
      if (!valid)
        continue;
      for (const auto &entry : module->TrustedMemorySummaries) {
        FunctionDecl *function = declarations.at(entry.first);
        if (!function->Body)
          function->MemorySummary = entry.second;
      }
    }
  }
  for (auto &edge : edges) {
    if (analyzed.count(edge.Callee))
      continue;
    addLocal(edge.Caller->MemorySummary, FunctionMemoryEffect::UnknownCall);
    addLocal(edge.Caller->MemorySummary,
             FunctionMemoryEffect::UnknownBoundary);
    for (const auto &roots : edge.ActualRoots)
      addLocal(edge.Caller->MemorySummary, roots,
               bit(MemoryRootEffect::Read) |
                   bit(MemoryRootEffect::Write) |
                   bit(MemoryRootEffect::Capture) |
                   bit(MemoryRootEffect::Unknown));
    edge.Callee = nullptr;
  }
  propagate(edges);
}

bool MemorySummaryAnalysis::verify(const std::vector<Module *> &modules,
                                   bool borrowCheckEnabled,
                                   std::vector<std::string> &errors) {
  for (FunctionDecl *function : collectFunctionsImpl(modules)) {
    const auto &summary = function->MemorySummary;
    for (const auto &arg : function->Args) {
      std::string name = rootName(arg);
      auto root = summary.Roots.find(name);
      if (root == summary.Roots.end()) {
        errors.push_back(summary.FunctionName + ": missing root " + name);
        continue;
      }
      if (has(root->second.Effects, MemoryRootEffect::Transfer) &&
          !has(root->second.Effects, MemoryRootEffect::Invalidate))
        errors.push_back(summary.FunctionName + ": transfer does not invalidate " + name);
      if (arg.IsRawPointer &&
          !has(root->second.Effects, MemoryRootEffect::Unknown))
        errors.push_back(summary.FunctionName + ": raw root is not unknown " + name);
      if (!borrowCheckEnabled && isMemoryBearing(arg) &&
          !has(root->second.Effects, MemoryRootEffect::Unknown))
        errors.push_back(summary.FunctionName + ": disabled PAL did not degrade " + name);
    }
    if (function->Effect == EffectKind::Async &&
        !has(summary.LocalEffects, FunctionMemoryEffect::Suspend))
      errors.push_back(summary.FunctionName + ": async function does not suspend");
    if (has(summary.LocalEffects, FunctionMemoryEffect::UnknownCall) &&
        !has(summary.LocalEffects, FunctionMemoryEffect::UnknownBoundary))
      errors.push_back(summary.FunctionName + ": unknown call is not an unknown boundary");
    if (has(summary.LocalEffects, FunctionMemoryEffect::UnsafeBoundary) &&
        !has(summary.LocalEffects, FunctionMemoryEffect::UnknownBoundary))
      errors.push_back(summary.FunctionName + ": unsafe use is not an unknown boundary");
  }
  return errors.empty();
}

bool MemorySummaryAnalysis::verifyIR(const std::vector<Module *> &modules,
                                     const llvm::Module &irModule,
                                     std::vector<std::string> &errors) {
  for (FunctionDecl *function : collectFunctionsImpl(modules)) {
    const auto &summary = function->MemorySummary;
    if (summary.FunctionName.rfind("@trait:", 0) == 0)
      continue;
    std::string irName = summary.FunctionName;
    // An async source `main` lowers to an internal coroutine factory plus a
    // native process-entry wrapper. The memory summary describes the factory.
    if (function->Name == "main" && function->Effect == EffectKind::Async)
      irName = "__toka_async_main";
    const llvm::Function *ir = irModule.getFunction(irName);
    if (!ir || ir->isDeclaration())
      continue;
    bool allocates = false;
    bool suspends = false;
    bool taggedAllocation = false;
    bool taggedFree = false;
    bool taggedRebind = false;
    for (const llvm::BasicBlock &block : *ir) {
      for (const llvm::Instruction &instruction : block) {
        if (const llvm::MDNode *metadata =
                instruction.getMetadata("toka.memory.local")) {
          if (metadata->getNumOperands() == 1) {
            if (auto *event = llvm::dyn_cast<llvm::MDString>(
                    metadata->getOperand(0).get())) {
              taggedAllocation |= event->getString() == "allocate";
              taggedFree |= event->getString() == "free";
              taggedRebind |= event->getString() == "rebind";
            }
          }
        }
        auto *call = llvm::dyn_cast<llvm::CallBase>(&instruction);
        if (!call)
          continue;
        const llvm::Function *callee = call->getCalledFunction();
        if (!callee)
          continue;
        llvm::StringRef name = callee->getName();
        allocates |= name == "malloc" || name == "calloc" ||
                     name == "realloc";
        suspends |= name.starts_with("llvm.coro.id");
      }
    }
    if (taggedAllocation &&
        !has(summary.LocalEffects, FunctionMemoryEffect::Allocate))
      errors.push_back(summary.FunctionName + ": tagged IR allocation lacks local summary evidence");
    if (taggedFree && !has(summary.LocalEffects, FunctionMemoryEffect::Free))
      errors.push_back(summary.FunctionName + ": tagged IR free lacks local summary evidence");
    bool hasLocalRebind = false;
    for (const auto &root : summary.Roots)
      hasLocalRebind |=
          has(root.second.LocalEffects, MemoryRootEffect::Rebind);
    if (taggedRebind && !hasLocalRebind)
      errors.push_back(summary.FunctionName +
                       ": tagged IR rebind lacks root summary evidence");
    if (suspends && !has(summary.LocalEffects, FunctionMemoryEffect::Suspend))
      errors.push_back(summary.FunctionName + ": coroutine IR lacks suspend summary evidence");
    if (function->Effect == EffectKind::Async && !suspends)
      errors.push_back(summary.FunctionName + ": async summary lacks coroutine IR evidence");
    if (function->Effect == EffectKind::Async && !allocates)
      errors.push_back(summary.FunctionName + ": async summary lacks frame allocation IR evidence");
  }
  return errors.empty();
}

void MemorySummaryAnalysis::dumpJSON(const std::vector<Module *> &modules,
                                     std::ostream &out) {
  auto functions = collectFunctionsImpl(modules);
  out << "{\"schema\":\"toka.memory-summary\",\"version\":"
      << FunctionMemorySummary::SchemaVersion << ",\"functions\":[";
  for (size_t i = 0; i < functions.size(); ++i) {
    if (i)
      out << ',';
    const auto &summary = functions[i]->MemorySummary;
    out << "{\"name\":\"" << escapeJSON(summary.FunctionName)
        << "\",\"origin\":\"";
    if (summary.Origin == MemorySummaryOrigin::SourceBody)
      out << "source_body";
    else if (summary.Origin == MemorySummaryOrigin::TrustedCache)
      out << "trusted_cache";
    else
      out << "signature_only";
    out
        << "\",\"local_effects\":";
    dumpNames(out, effectNames<FunctionMemoryEffect>(summary.LocalEffects));
    out << ",\"effects\":";
    dumpNames(out, effectNames<FunctionMemoryEffect>(summary.Effects));
    out << ",\"roots\":[";
    size_t rootIndex = 0;
    for (const auto &root : summary.Roots) {
      if (rootIndex++)
        out << ',';
      out << "{\"name\":\"" << escapeJSON(root.first)
          << "\",\"local_effects\":";
      dumpNames(out, effectNames<MemoryRootEffect>(root.second.LocalEffects));
      out << ",\"effects\":";
      dumpNames(out, effectNames<MemoryRootEffect>(root.second.Effects));
      out << '}';
    }
    out << "]}";
  }
  out << "]}\n";
}

} // namespace toka
