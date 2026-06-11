// Copyright (c) 2026 YiZhonghua<zhyi@dpai.com>. All rights reserved.
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
#include "toka/ASTEvaluator.h"
#include "toka/AST.h"
#include "toka/Sema.h"
#include "toka/Type.h"
#include "toka/ComptimeValue.h"
#include <memory>
#include <string>
#include <vector>

namespace toka {

std::unique_ptr<Expr> ASTEvaluator::foldExpression(std::unique_ptr<Expr> E, Scope *CurrentScope, Sema *SemaInstance) {
  if (!E)
    return nullptr;

  if (auto *Var = dynamic_cast<VariableExpr *>(E.get())) {
    SymbolInfo Info;
    // Look up symbol. If explicit Const Generic, substitute.
    if (CurrentScope && CurrentScope->lookup(Var->Name, Info) && Info.HasConstValue && !Info.IsDeclaredVariable) {
      // Create replacement NumberExpr using ComptimeValue
      auto Num = std::make_unique<NumberExpr>(Info.ConstValObj.getInt());
      Num->Loc = Var->Loc;

      // Enforce Declared Type via Cast
      if (Info.TypeObj) {
        std::string typeStr = Info.TypeObj->toString();
        if (typeStr != "unknown" && typeStr != "auto") {
          auto Cast = std::make_unique<CastExpr>(std::move(Num), typeStr);
          Cast->Loc = Var->Loc;
          return Cast;
        }
      }
      return Num;
    } else if (CurrentScope && CurrentScope->lookup(Var->Name, Info) && Info.IsComptimeField) {
      // Setup replacement for compiled static unrolled Macro fields
      auto Field = std::make_unique<ComptimeFieldExpr>(
          Info.ComptimeFieldName, Info.ComptimeFieldTypeStr,
          Info.ComptimeFieldOffset, Info.ComptimeFieldSize);
      Field->Loc = Var->Loc;
      return Field;
    }
  } else if (auto *Call = dynamic_cast<CallExpr *>(E.get())) {
    if (Call->Callee == "core/comptime::is_pointer" || Call->Callee == "is_pointer") {
      if (!Call->GenericArgs.empty()) {
        std::string targetTyStr = Call->GenericArgs[0];
        auto targetObj = toka::Type::fromString(targetTyStr);
        auto resolvedObj = SemaInstance ? SemaInstance->resolveType(targetObj, true) : targetObj;
        bool isPtr = resolvedObj && (resolvedObj->isPointer() || resolvedObj->isRawPointer() || resolvedObj->isReference() || resolvedObj->isSmartPointer());
        
        auto boolExpr = std::make_unique<BoolExpr>(isPtr);
        boolExpr->Loc = Call->Loc;
        return boolExpr;
      }
    } else if (Call->Callee == "core/comptime::reflect" || Call->Callee == "reflect") {
      if (!Call->GenericArgs.empty()) {
          auto reflectExpr = std::make_unique<ComptimeReflectExpr>(Call->GenericArgs[0]);
          reflectExpr->Loc = Call->Loc;
          return reflectExpr;
      }
    }
  } else if (auto *Memb = dynamic_cast<MemberExpr *>(E.get())) {
    Memb->Object = foldExpression(std::move(Memb->Object), CurrentScope, SemaInstance);
    if (auto *CFE = dynamic_cast<ComptimeFieldExpr *>(Memb->Object.get())) {
      if (Memb->Member == "name") {
        auto str = std::make_unique<ViewStringExpr>(CFE->FieldName);
        str->Loc = Memb->Loc;
        return str;
      } else if (Memb->Member == "type_name") {
        auto str = std::make_unique<ViewStringExpr>(CFE->FieldTypeName);
        str->Loc = Memb->Loc;
        return str;
      } else if (Memb->Member == "offset") {
        auto num = std::make_unique<NumberExpr>(CFE->FieldOffset);
        num->Loc = Memb->Loc;
        return num;
      } else if (Memb->Member == "size") {
        auto num = std::make_unique<NumberExpr>(CFE->FieldSize);
        num->Loc = Memb->Loc;
        return num;
      }
    }
  } else if (auto *Met = dynamic_cast<MethodCallExpr *>(E.get())) {
    Met->Object = foldExpression(std::move(Met->Object), CurrentScope, SemaInstance);
    if (auto *CFE = dynamic_cast<ComptimeFieldExpr *>(Met->Object.get())) {
      if (Met->Method == "get" && Met->Args.size() == 1) {
        // Fold format: field.get(obj) -> obj.FieldName
        Met->Args[0] = foldExpression(std::move(Met->Args[0]), CurrentScope, SemaInstance);
        auto replacement = std::make_unique<MemberExpr>(std::move(Met->Args[0]), CFE->FieldName);
        replacement->Loc = Met->Loc;
        return replacement;
      } else if (Met->Method == "set" && Met->Args.size() == 2) {
        // Fold format: field.set(obj, val) -> obj.FieldName = val
        Met->Args[0] = foldExpression(std::move(Met->Args[0]), CurrentScope, SemaInstance);
        Met->Args[1] = foldExpression(std::move(Met->Args[1]), CurrentScope, SemaInstance);
        auto dest = std::make_unique<MemberExpr>(std::move(Met->Args[0]), CFE->FieldName);
        dest->Loc = Met->Loc;
        auto assign = std::make_unique<BinaryExpr>("=", std::move(dest), std::move(Met->Args[1]));
        assign->Loc = Met->Loc;
        return assign;
      }
    }
  } else if (auto *Bin = dynamic_cast<BinaryExpr *>(E.get())) {
    Bin->LHS = foldExpression(std::move(Bin->LHS), CurrentScope, SemaInstance);
    Bin->RHS = foldExpression(std::move(Bin->RHS), CurrentScope, SemaInstance);
    if (Bin->Op == "==" || Bin->Op == "!=") {
        std::string lhsVal;
        bool lhsIsStr = false;
        if (auto *s = dynamic_cast<StringExpr *>(Bin->LHS.get())) {
            lhsVal = s->Value;
            lhsIsStr = true;
        } else if (auto *vs = dynamic_cast<ViewStringExpr *>(Bin->LHS.get())) {
            lhsVal = vs->Value;
            lhsIsStr = true;
        }

        std::string rhsVal;
        bool rhsIsStr = false;
        if (auto *s = dynamic_cast<StringExpr *>(Bin->RHS.get())) {
            rhsVal = s->Value;
            rhsIsStr = true;
        } else if (auto *vs = dynamic_cast<ViewStringExpr *>(Bin->RHS.get())) {
            rhsVal = vs->Value;
            rhsIsStr = true;
        }

        if (lhsIsStr && rhsIsStr) {
            bool matches = (lhsVal == rhsVal);
            bool result = (Bin->Op == "==") ? matches : !matches;
            auto boolExpr = std::make_unique<BoolExpr>(result);
            boolExpr->Loc = Bin->Loc;
            return boolExpr;
        }
    }
  }
  return E;
}

} // namespace toka
