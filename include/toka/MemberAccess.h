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

#include <string>

namespace toka {

struct MemberAccessIntent {
  std::string Original;
  std::string Prefix;
  std::string MemberName;
  std::string StrippedName;
  int AccessHats = 0;
  bool IsMorphicIdentity = false;
  bool IsIdentityAssertion = false;

  bool hasExplicitPrefix() const { return !Prefix.empty(); }
};

inline bool isMemberAccessPrefixChar(char c) {
  return c == '*' || c == '^' || c == '~' || c == '&' || c == '?' ||
         c == '#' || c == '!';
}

inline int countLeadingMemberHats(const std::string &s) {
  int count = 0;
  for (char c : s) {
    if (c == '^' || c == '*' || c == '~' || c == '&')
      count++;
    else
      break;
  }
  return count;
}

inline MemberAccessIntent parseMemberAccess(const std::string &rawName) {
  MemberAccessIntent intent;
  intent.Original = rawName;

  size_t prefixEnd = 0;
  if (rawName.size() >= 2 && rawName.substr(0, 2) == "??") {
    prefixEnd = 2;
    intent.IsIdentityAssertion = true;
  } else {
    while (prefixEnd < rawName.size() &&
           isMemberAccessPrefixChar(rawName[prefixEnd])) {
      prefixEnd++;
    }
  }

  intent.Prefix = rawName.substr(0, prefixEnd);
  intent.AccessHats = countLeadingMemberHats(rawName);

  intent.MemberName = rawName.substr(prefixEnd);
  if (!intent.MemberName.empty() && intent.MemberName[0] == '\'') {
    intent.IsMorphicIdentity = true;
    intent.MemberName = intent.MemberName.substr(1);
  }

  std::string name = intent.MemberName;
  while (!name.empty() &&
         (name.back() == '#' || name.back() == '?' || name.back() == '!')) {
    name.pop_back();
  }
  intent.StrippedName = name;

  return intent;
}

inline std::string stripMemberAccessMarkers(const std::string &rawName) {
  return parseMemberAccess(rawName).StrippedName;
}

} // namespace toka
