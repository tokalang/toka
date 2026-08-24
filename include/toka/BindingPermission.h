#pragma once

#include <vector>
#include <string>

namespace toka {

enum class BindingMorphology {
  None,
  Raw,
  Unique,
  Shared,
  Reference,
};

struct HandleLayer {
  BindingMorphology Morphology = BindingMorphology::None;
  bool Rebindable = false; // #
  bool Nullable = false;   // nul
  bool Blocked = false;    // $
};

// Structured view of Toka's binding/path permission surface.
//
// This intentionally models the source-level contract:
//   - morphology and identity attributes live on the binding/handle
//   - soul attributes live on the entity behind that binding
//
// Parser AST bools remain as recovery inputs for precise removed-syntax
// diagnostics. Semantic permission carries only admitted language facts.
struct BindingPermission {
  // Ordered sequence of handle layers from outermost to innermost:
  // e.g. &^x => [ {Reference}, {Unique} ]
  // e.g. &~x => [ {Reference}, {Shared} ]
  // e.g. &&x => [ {Reference}, {Reference} ]
  // e.g. ^x  => [ {Unique} ]
  // e.g. *x  => [ {Raw} ]
  std::vector<HandleLayer> HandleLayers;

  BindingMorphology Morphology = BindingMorphology::None;

  bool IdentityRebindable = false; // ^#p, *#p, &#p
  bool IdentityMayBeZero = false;  // nul *p
  bool IdentityBlocked = false;

  bool SoulWritable = false;       // p#
  bool SoulBlocked = false;

  bool MorphicExempt = false;      // 'T preserves morphology

  bool isLevel2Borrow() const {
    return HandleLayers.size() == 2 &&
           HandleLayers[0].Morphology == BindingMorphology::Reference &&
           (HandleLayers[1].Morphology == BindingMorphology::Unique ||
            HandleLayers[1].Morphology == BindingMorphology::Shared ||
            HandleLayers[1].Morphology == BindingMorphology::Reference);
  }

  bool isBorrowOfUnique() const {
    return HandleLayers.size() == 2 &&
           HandleLayers[0].Morphology == BindingMorphology::Reference &&
           HandleLayers[1].Morphology == BindingMorphology::Unique;
  }

  bool isBorrowOfShared() const {
    return HandleLayers.size() == 2 &&
           HandleLayers[0].Morphology == BindingMorphology::Reference &&
           HandleLayers[1].Morphology == BindingMorphology::Shared;
  }

  bool isDoubleBorrow() const {
    return HandleLayers.size() == 2 &&
           HandleLayers[0].Morphology == BindingMorphology::Reference &&
           HandleLayers[1].Morphology == BindingMorphology::Reference;
  }

  BindingMorphology outerMorphology() const {
    return HandleLayers.empty() ? Morphology : HandleLayers[0].Morphology;
  }

  BindingMorphology innerMorphology() const {
    return HandleLayers.size() >= 2 ? HandleLayers[1].Morphology : BindingMorphology::None;
  }

  void syncProjections() {
    if (!HandleLayers.empty()) {
      Morphology = HandleLayers[0].Morphology;
      IdentityRebindable = HandleLayers[0].Rebindable;
      IdentityMayBeZero = HandleLayers[0].Nullable;
      IdentityBlocked = HandleLayers[0].Blocked;
    } else {
      Morphology = BindingMorphology::None;
    }
  }

  static BindingMorphology legacyMorphology(bool isRawPointer,
                                            bool isUnique,
                                            bool isShared,
                                            bool isReference) {
    if (isReference)
      return BindingMorphology::Reference;
    if (isUnique)
      return BindingMorphology::Unique;
    if (isShared)
      return BindingMorphology::Shared;
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
    if (permission.Morphology != BindingMorphology::None) {
      HandleLayer layer;
      layer.Morphology = permission.Morphology;
      layer.Rebindable = isRebindable;
      layer.Nullable = isPointerNullable;
      layer.Blocked = isRebindBlocked;
      permission.HandleLayers.push_back(layer);
    }
    permission.IdentityRebindable = isRebindable;
    permission.IdentityMayBeZero = isPointerNullable;
    permission.IdentityBlocked = isRebindBlocked;
    permission.SoulWritable = isValueMutable;
    (void)isValueNullable;
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
           IdentityMayBeZero == isPointerNullable &&
           IdentityBlocked == isRebindBlocked &&
           SoulWritable == isValueMutable &&
           !isValueNullable &&
           SoulBlocked == isValueBlocked &&
           MorphicExempt == isMorphicExempt;
  }
};

} // namespace toka
