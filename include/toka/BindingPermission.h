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

namespace toka {

enum class BindingMorphology {
  None,
  Raw,
  Unique,
  Shared,
  Reference,
};

// Structured view of Toka's binding/path permission surface.
//
// This intentionally models the source-level contract:
//   - morphology and identity attributes live on the binding/handle
//   - soul attributes live on the entity behind that binding
//
// The legacy bool fields remain the compatibility source during the first
// migration phase. This struct lets new code stop re-inventing the mapping.
struct BindingPermission {
  BindingMorphology Morphology = BindingMorphology::None;

  bool IdentityRebindable = false; // ^#p, *#p, &#p
  bool IdentityNullable = false;   // nul *p, nul ^p
  bool IdentityBlocked = false;

  bool SoulWritable = false;       // p#
  bool SoulNullable = false;       // T? / value noneability
  bool SoulBlocked = false;

  bool MorphicExempt = false;      // 'T preserves morphology

  static BindingMorphology legacyMorphology(bool isRawPointer,
                                            bool isUnique,
                                            bool isShared,
                                            bool isReference) {
    if (isUnique)
      return BindingMorphology::Unique;
    if (isShared)
      return BindingMorphology::Shared;
    if (isReference)
      return BindingMorphology::Reference;
    if (isRawPointer)
      return BindingMorphology::Raw;
    return BindingMorphology::None;
  }

  static BindingPermission fromLegacy(bool isRawPointer, bool isUnique,
                                      bool isShared, bool isReference,
                                      bool isRebindable,
                                      bool isPointerNullable,
                                      bool isRebindBlocked,
                                      bool isValueMutable,
                                      bool isValueNullable,
                                      bool isValueBlocked,
                                      bool isMorphicExempt = false) {
    BindingPermission permission;
    permission.Morphology =
        legacyMorphology(isRawPointer, isUnique, isShared, isReference);
    permission.IdentityRebindable = isRebindable;
    permission.IdentityNullable = isPointerNullable;
    permission.IdentityBlocked = isRebindBlocked;
    permission.SoulWritable = isValueMutable;
    permission.SoulNullable = isValueNullable;
    permission.SoulBlocked = isValueBlocked;
    permission.MorphicExempt = isMorphicExempt;
    return permission;
  }

  bool matchesLegacy(bool isRawPointer, bool isUnique, bool isShared,
                     bool isReference, bool isRebindable,
                     bool isPointerNullable, bool isRebindBlocked,
                     bool isValueMutable, bool isValueNullable,
                     bool isValueBlocked,
                     bool isMorphicExempt = false) const {
    return Morphology ==
               legacyMorphology(isRawPointer, isUnique, isShared,
                                isReference) &&
           IdentityRebindable == isRebindable &&
           IdentityNullable == isPointerNullable &&
           IdentityBlocked == isRebindBlocked &&
           SoulWritable == isValueMutable &&
           SoulNullable == isValueNullable &&
           SoulBlocked == isValueBlocked &&
           MorphicExempt == isMorphicExempt;
  }
};

} // namespace toka
