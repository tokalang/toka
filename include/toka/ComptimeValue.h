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

#include <variant>
#include <string>
#include <vector>
#include <map>
#include <cstdint>

namespace toka {

class ComptimeValue {
public:
  enum Kind {
    None,
    Int,
    Float,
    String,
    Array,
    Struct
  };

  Kind ValKind = None;

  using IntType = uint64_t;
  using FloatType = double;
  using StringType = std::string;
  using ArrayTypeVal = std::vector<ComptimeValue>;
  using StructTypeVal = std::map<std::string, ComptimeValue>;

  std::variant<std::monostate, IntType, FloatType, StringType, ArrayTypeVal, StructTypeVal> Data;

  ComptimeValue() : ValKind(None), Data(std::monostate{}) {}
  ComptimeValue(IntType val) : ValKind(Int), Data(val) {}
  ComptimeValue(FloatType val) : ValKind(Float), Data(val) {}
  ComptimeValue(const StringType &val) : ValKind(String), Data(val) {}
  ComptimeValue(const ArrayTypeVal &val) : ValKind(Array), Data(val) {}
  ComptimeValue(const StructTypeVal &val) : ValKind(Struct), Data(val) {}

  bool isNone() const { return ValKind == None; }
  bool isInt() const { return ValKind == Int; }
  bool isFloat() const { return ValKind == Float; }
  bool isString() const { return ValKind == String; }
  bool isArray() const { return ValKind == Array; }
  bool isStruct() const { return ValKind == Struct; }

  IntType getInt() const {
    return std::holds_alternative<IntType>(Data) ? std::get<IntType>(Data) : 0;
  }
  FloatType getFloat() const {
    return std::holds_alternative<FloatType>(Data) ? std::get<FloatType>(Data) : 0.0;
  }
  const StringType &getString() const {
    static const StringType empty = "";
    return std::holds_alternative<StringType>(Data) ? std::get<StringType>(Data) : empty;
  }
  const ArrayTypeVal &getArray() const {
    static const ArrayTypeVal empty = {};
    return std::holds_alternative<ArrayTypeVal>(Data) ? std::get<ArrayTypeVal>(Data) : empty;
  }
  const StructTypeVal &getStruct() const {
    static const StructTypeVal empty = {};
    return std::holds_alternative<StructTypeVal>(Data) ? std::get<StructTypeVal>(Data) : empty;
  }
};

} // namespace toka
