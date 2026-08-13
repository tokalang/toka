// Copyright (c) 2026 Toka Project. All rights reserved.

#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

namespace toka {

class NominalShapeId {
public:
  enum class Origin : uint8_t { ResolverCoordinate, SourcePath };

  static NominalShapeId
  fromResolverCoordinate(std::string_view crateId,
                         std::string_view logicalModulePath,
                         std::string_view localName, uint64_t genericArity) {
    return NominalShapeId(
        Origin::ResolverCoordinate,
        encode("resolver", crateId, logicalModulePath, localName,
               genericArity));
  }

  static NominalShapeId fromSourcePath(std::string_view canonicalSourcePath,
                                       std::string_view localName,
                                       uint64_t genericArity) {
    return NominalShapeId(
        Origin::SourcePath,
        encode("source", canonicalSourcePath, "", localName, genericArity));
  }

  Origin origin() const noexcept { return OriginValue; }
  const std::string &canonical() const noexcept { return Canonical; }

  // Exact, symbol-safe encoding used in generic instance and ABI names.  Hex
  // is intentionally used instead of a digest so nominal identity cannot be
  // collapsed by a hash collision.
  std::string mangled() const {
    static constexpr char Hex[] = "0123456789abcdef";
    std::string result;
    result.reserve(1 + Canonical.size() * 2);
    result += 'N';
    for (unsigned char byte : Canonical) {
      result += Hex[byte >> 4];
      result += Hex[byte & 0x0f];
    }
    return result;
  }

  friend bool operator==(const NominalShapeId &lhs,
                         const NominalShapeId &rhs) noexcept {
    return lhs.OriginValue == rhs.OriginValue && lhs.Canonical == rhs.Canonical;
  }

  friend bool operator!=(const NominalShapeId &lhs,
                         const NominalShapeId &rhs) noexcept {
    return !(lhs == rhs);
  }

  friend bool operator<(const NominalShapeId &lhs,
                        const NominalShapeId &rhs) noexcept {
    if (lhs.OriginValue != rhs.OriginValue)
      return lhs.OriginValue < rhs.OriginValue;
    return lhs.Canonical < rhs.Canonical;
  }

private:
  Origin OriginValue;
  std::string Canonical;

  NominalShapeId(Origin origin, std::string canonical)
      : OriginValue(origin), Canonical(std::move(canonical)) {}

  static void append(std::string &result, std::string_view component) {
    result += std::to_string(component.size());
    result += ':';
    result.append(component.data(), component.size());
    result += ';';
  }

  static std::string encode(std::string_view origin, std::string_view first,
                            std::string_view second, std::string_view localName,
                            uint64_t genericArity) {
    std::string result = "toka.nominal-shape.v1;";
    append(result, origin);
    append(result, first);
    append(result, second);
    append(result, localName);
    append(result, std::to_string(genericArity));
    return result;
  }
};

} // namespace toka
