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
#pragma once

#include <memory>

namespace toka {

class Expr;
class Scope;
class Sema;

class ASTEvaluator {
public:
  static std::unique_ptr<Expr> foldExpression(std::unique_ptr<Expr> E, Scope *CurrentScope, Sema *SemaInstance);
};

} // namespace toka
