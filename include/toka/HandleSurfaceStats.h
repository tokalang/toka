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

#include "toka/AST.h"
#include "toka/Token.h"
#include "toka/Type.h"
#include "llvm/Support/raw_ostream.h"
#include <cstdint>
#include <set>
#include <string>

namespace toka {

struct HandleSurfaceStats {
  bool Enabled = false;

  uint64_t VariableDeclarations = 0;
  uint64_t HattedVariableDeclarations = 0;
  uint64_t RawVariableDeclarations = 0;
  uint64_t UniqueVariableDeclarations = 0;
  uint64_t SharedVariableDeclarations = 0;
  uint64_t ReferenceVariableDeclarations = 0;
  uint64_t RebindableHandleDeclarations = 0;
  uint64_t NullableHandleDeclarations = 0;

  uint64_t ShapeMembers = 0;
  uint64_t HattedShapeMembers = 0;
  uint64_t RawShapeMembers = 0;
  uint64_t UniqueShapeMembers = 0;
  uint64_t SharedShapeMembers = 0;
  uint64_t ReferenceShapeMembers = 0;
  uint64_t RebindableHandleMembers = 0;
  uint64_t NullableHandleMembers = 0;
  uint64_t MorphologyShapeMembers = 0;
  uint64_t QuotedMorphicShapeMembers = 0;

  uint64_t FunctionSignatures = 0;
  uint64_t ExternSignatures = 0;
  uint64_t TraitMethodSignatures = 0;
  uint64_t ImplMethodSignatures = 0;
  uint64_t SignatureParameters = 0;
  uint64_t HattedSignatureParameters = 0;
  uint64_t RawSignatureParameters = 0;
  uint64_t UniqueSignatureParameters = 0;
  uint64_t SharedSignatureParameters = 0;
  uint64_t ReferenceSignatureParameters = 0;
  uint64_t RebindableHandleParameters = 0;
  uint64_t NullableHandleParameters = 0;
  uint64_t CedeHattedParameters = 0;
  uint64_t HattedReturnTypes = 0;

  uint64_t UnaryHandleExpressions = 0;
  uint64_t RawHandleExpressions = 0;
  uint64_t UniqueHandleExpressions = 0;
  uint64_t SharedHandleExpressions = 0;
  uint64_t ReferenceHandleExpressions = 0;
  uint64_t RebindableUnaryHandleExpressions = 0;
  uint64_t NullableUnaryHandleExpressions = 0;
  uint64_t AddressOfExpressions = 0;
  uint64_t DereferenceExpressions = 0;

  uint64_t MemberAccesses = 0;
  uint64_t MemberHandleAccesses = 0;
  uint64_t RawMemberHandleAccesses = 0;
  uint64_t UniqueMemberHandleAccesses = 0;
  uint64_t SharedMemberHandleAccesses = 0;
  uint64_t ReferenceMemberHandleAccesses = 0;
  uint64_t RebindableMemberHandleAccesses = 0;
  uint64_t NullableMemberHandleAccesses = 0;
  uint64_t MemberMorphologyAccesses = 0;
  uint64_t QuotedMorphicMemberAccesses = 0;

  std::set<const void *> CountedModules;
  std::set<const void *> CountedVariables;
  std::set<const void *> CountedFunctions;
  std::set<const void *> CountedExterns;
  std::set<const void *> CountedTraits;
  std::set<const void *> CountedImpls;
  std::set<const void *> CountedExprs;

  void reset() { *this = HandleSurfaceStats{}; }
};

inline HandleSurfaceStats &handleSurfaceStats() {
  static HandleSurfaceStats stats;
  return stats;
}

inline void enableHandleSurfaceStats(bool enabled) {
  handleSurfaceStats().reset();
  handleSurfaceStats().Enabled = enabled;
}

inline bool handleSurfaceStatsEnabled() {
  return handleSurfaceStats().Enabled;
}

inline bool handleSurfaceMark(std::set<const void *> &seen,
                              const void *node) {
  return node && seen.insert(node).second;
}

inline bool hasHandleMorphology(bool raw, bool unique, bool shared,
                                bool reference) {
  return raw || unique || shared || reference;
}

inline bool isMorphicName(const std::string &name) {
  return Type::stripMorphology(name) != name;
}

inline bool typeStringHasHandleSurface(const std::string &typeName) {
  if (typeName.empty() || typeName == "void")
    return false;
  auto type = Type::fromString(typeName);
  return type && (type->isPointer() || type->isSmartPointer() ||
                  type->isReference());
}

inline void recordHandleSurfaceKind(bool raw, bool unique, bool shared,
                                    bool reference, uint64_t &rawCount,
                                    uint64_t &uniqueCount,
                                    uint64_t &sharedCount,
                                    uint64_t &referenceCount) {
  if (raw)
    rawCount++;
  if (unique)
    uniqueCount++;
  if (shared)
    sharedCount++;
  if (reference)
    referenceCount++;
}

inline void recordHandleSurfaceVariableDecl(const VariableDecl &decl) {
  auto &stats = handleSurfaceStats();
  if (!stats.Enabled ||
      !handleSurfaceMark(stats.CountedVariables, &decl))
    return;

  stats.VariableDeclarations++;
  bool hatted = hasHandleMorphology(decl.IsRawPointer, decl.IsUnique,
                                    decl.IsShared, decl.IsReference);
  if (hatted)
    stats.HattedVariableDeclarations++;
  recordHandleSurfaceKind(decl.IsRawPointer, decl.IsUnique, decl.IsShared,
                          decl.IsReference, stats.RawVariableDeclarations,
                          stats.UniqueVariableDeclarations,
                          stats.SharedVariableDeclarations,
                          stats.ReferenceVariableDeclarations);
  if (hatted && decl.IsRebindable)
    stats.RebindableHandleDeclarations++;
  if (hatted && decl.IsPointerNullable)
    stats.NullableHandleDeclarations++;
}

inline void recordHandleSurfaceShapeMember(const ShapeMember &member) {
  auto &stats = handleSurfaceStats();
  if (!stats.Enabled)
    return;

  stats.ShapeMembers++;
  bool hatted = hasHandleMorphology(member.IsRawPointer, member.IsUnique,
                                    member.IsShared, member.IsReference);
  if (hatted)
    stats.HattedShapeMembers++;
  recordHandleSurfaceKind(member.IsRawPointer, member.IsUnique,
                          member.IsShared, member.IsReference,
                          stats.RawShapeMembers, stats.UniqueShapeMembers,
                          stats.SharedShapeMembers,
                          stats.ReferenceShapeMembers);
  if (hatted && member.IsRebindable)
    stats.RebindableHandleMembers++;
  if (hatted && member.IsPointerNullable)
    stats.NullableHandleMembers++;
  if (isMorphicName(member.Name))
    stats.MorphologyShapeMembers++;
  if (member.Name.find('\'') != std::string::npos)
    stats.QuotedMorphicShapeMembers++;

  for (const auto &sub : member.SubMembers)
    recordHandleSurfaceShapeMember(sub);
}

template <typename ArgT>
inline void recordHandleSurfaceParam(const ArgT &arg) {
  auto &stats = handleSurfaceStats();
  if (!stats.Enabled)
    return;

  stats.SignatureParameters++;
  bool hatted = hasHandleMorphology(arg.IsRawPointer, arg.IsUnique,
                                    arg.IsShared, arg.IsReference);
  if (hatted)
    stats.HattedSignatureParameters++;
  recordHandleSurfaceKind(arg.IsRawPointer, arg.IsUnique, arg.IsShared,
                          arg.IsReference, stats.RawSignatureParameters,
                          stats.UniqueSignatureParameters,
                          stats.SharedSignatureParameters,
                          stats.ReferenceSignatureParameters);
  if (hatted && arg.IsRebindable)
    stats.RebindableHandleParameters++;
  if (hatted && arg.IsPointerNullable)
    stats.NullableHandleParameters++;
  if (hatted && arg.IsCeded)
    stats.CedeHattedParameters++;
}

inline void recordHandleSurfaceFunction(const FunctionDecl &fn,
                                        const char *kind) {
  auto &stats = handleSurfaceStats();
  if (!stats.Enabled ||
      !handleSurfaceMark(stats.CountedFunctions, &fn))
    return;

  std::string k = kind ? kind : "";
  if (k == "trait")
    stats.TraitMethodSignatures++;
  else if (k == "impl")
    stats.ImplMethodSignatures++;
  else
    stats.FunctionSignatures++;

  for (const auto &arg : fn.Args)
    recordHandleSurfaceParam(arg);
  if (typeStringHasHandleSurface(fn.ReturnType))
    stats.HattedReturnTypes++;
}

inline void recordHandleSurfaceExtern(const ExternDecl &ext) {
  auto &stats = handleSurfaceStats();
  if (!stats.Enabled ||
      !handleSurfaceMark(stats.CountedExterns, &ext))
    return;

  stats.ExternSignatures++;
  for (const auto &arg : ext.Args)
    recordHandleSurfaceParam(arg);
  if (typeStringHasHandleSurface(ext.ReturnType))
    stats.HattedReturnTypes++;
}

inline void recordHandleSurfaceUnaryExpr(const UnaryExpr &expr,
                                         bool isHandleUse) {
  auto &stats = handleSurfaceStats();
  if (!stats.Enabled || !isHandleUse ||
      !handleSurfaceMark(stats.CountedExprs, &expr))
    return;

  stats.UnaryHandleExpressions++;
  if (expr.Op == TokenType::Star)
    stats.RawHandleExpressions++;
  else if (expr.Op == TokenType::Caret)
    stats.UniqueHandleExpressions++;
  else if (expr.Op == TokenType::Tilde)
    stats.SharedHandleExpressions++;
  else if (expr.Op == TokenType::Ampersand)
    stats.ReferenceHandleExpressions++;
  if (expr.IsRebindable)
    stats.RebindableUnaryHandleExpressions++;
  if (expr.HasNull)
    stats.NullableUnaryHandleExpressions++;
}

inline void recordHandleSurfaceAddressOfExpr(const AddressOfExpr &expr) {
  auto &stats = handleSurfaceStats();
  if (!stats.Enabled ||
      !handleSurfaceMark(stats.CountedExprs, &expr))
    return;
  stats.AddressOfExpressions++;
  stats.ReferenceHandleExpressions++;
}

inline void recordHandleSurfaceDereferenceExpr(const DereferenceExpr &expr) {
  auto &stats = handleSurfaceStats();
  if (!stats.Enabled ||
      !handleSurfaceMark(stats.CountedExprs, &expr))
    return;
  stats.DereferenceExpressions++;
  stats.RawHandleExpressions++;
}

inline void recordHandleSurfaceMemberExpr(const MemberExpr &expr) {
  auto &stats = handleSurfaceStats();
  if (!stats.Enabled ||
      !handleSurfaceMark(stats.CountedExprs, &expr))
    return;

  stats.MemberAccesses++;
  const std::string &name = expr.Member;
  if (isMorphicName(name))
    stats.MemberMorphologyAccesses++;
  if (name.find('\'') != std::string::npos)
    stats.QuotedMorphicMemberAccesses++;

  bool raw = name.find('*') != std::string::npos;
  bool unique = name.find('^') != std::string::npos;
  bool shared = name.find('~') != std::string::npos;
  bool reference = name.find('&') != std::string::npos;
  bool hatted = hasHandleMorphology(raw, unique, shared, reference);
  if (!hatted)
    return;

  stats.MemberHandleAccesses++;
  recordHandleSurfaceKind(raw, unique, shared, reference,
                          stats.RawMemberHandleAccesses,
                          stats.UniqueMemberHandleAccesses,
                          stats.SharedMemberHandleAccesses,
                          stats.ReferenceMemberHandleAccesses);
  if (name.find('#') != std::string::npos)
    stats.RebindableMemberHandleAccesses++;
  if (name.find('?') != std::string::npos)
    stats.NullableMemberHandleAccesses++;
}

inline void recordHandleSurfaceModule(const Module &module) {
  auto &stats = handleSurfaceStats();
  if (!stats.Enabled ||
      !handleSurfaceMark(stats.CountedModules, &module))
    return;

  for (const auto &fn : module.Functions)
    recordHandleSurfaceFunction(*fn, "function");
  for (const auto &ext : module.Externs)
    recordHandleSurfaceExtern(*ext);
  for (const auto &shape : module.Shapes) {
    for (const auto &member : shape->Members)
      recordHandleSurfaceShapeMember(member);
  }
  for (const auto &global : module.Globals) {
    if (auto *var = dynamic_cast<VariableDecl *>(global.get()))
      recordHandleSurfaceVariableDecl(*var);
  }
  for (const auto &trait : module.Traits) {
    if (!handleSurfaceMark(stats.CountedTraits, trait.get()))
      continue;
    for (const auto &method : trait->Methods)
      recordHandleSurfaceFunction(*method, "trait");
  }
  for (const auto &impl : module.Impls) {
    if (!handleSurfaceMark(stats.CountedImpls, impl.get()))
      continue;
    for (const auto &method : impl->Methods)
      recordHandleSurfaceFunction(*method, "impl");
  }
}

inline void dumpHandleSurfaceStatsJson(llvm::raw_ostream &os,
                                       uint64_t files) {
  const auto &stats = handleSurfaceStats();
  os << "{\n";
  os << "  \"tool\": \"toka\",\n";
  os << "  \"mode\": \"handle-surface-audit\",\n";
  os << "  \"files\": " << files << ",\n";
  os << "  \"declarations\": {\n";
  os << "    \"variable_declarations\": " << stats.VariableDeclarations
     << ",\n";
  os << "    \"hatted_variable_declarations\": "
     << stats.HattedVariableDeclarations << ",\n";
  os << "    \"raw_variable_declarations\": "
     << stats.RawVariableDeclarations << ",\n";
  os << "    \"unique_variable_declarations\": "
     << stats.UniqueVariableDeclarations << ",\n";
  os << "    \"shared_variable_declarations\": "
     << stats.SharedVariableDeclarations << ",\n";
  os << "    \"reference_variable_declarations\": "
     << stats.ReferenceVariableDeclarations << ",\n";
  os << "    \"rebindable_handle_declarations\": "
     << stats.RebindableHandleDeclarations << ",\n";
  os << "    \"nullable_handle_declarations\": "
     << stats.NullableHandleDeclarations << "\n";
  os << "  },\n";
  os << "  \"fields\": {\n";
  os << "    \"shape_members\": " << stats.ShapeMembers << ",\n";
  os << "    \"hatted_shape_members\": " << stats.HattedShapeMembers
     << ",\n";
  os << "    \"raw_shape_members\": " << stats.RawShapeMembers << ",\n";
  os << "    \"unique_shape_members\": " << stats.UniqueShapeMembers
     << ",\n";
  os << "    \"shared_shape_members\": " << stats.SharedShapeMembers
     << ",\n";
  os << "    \"reference_shape_members\": " << stats.ReferenceShapeMembers
     << ",\n";
  os << "    \"rebindable_handle_members\": "
     << stats.RebindableHandleMembers << ",\n";
  os << "    \"nullable_handle_members\": " << stats.NullableHandleMembers
     << ",\n";
  os << "    \"morphology_shape_members\": "
     << stats.MorphologyShapeMembers << ",\n";
  os << "    \"quoted_morphic_shape_members\": "
     << stats.QuotedMorphicShapeMembers << "\n";
  os << "  },\n";
  os << "  \"signatures\": {\n";
  os << "    \"function_signatures\": " << stats.FunctionSignatures
     << ",\n";
  os << "    \"extern_signatures\": " << stats.ExternSignatures << ",\n";
  os << "    \"trait_method_signatures\": "
     << stats.TraitMethodSignatures << ",\n";
  os << "    \"impl_method_signatures\": " << stats.ImplMethodSignatures
     << ",\n";
  os << "    \"signature_parameters\": " << stats.SignatureParameters
     << ",\n";
  os << "    \"hatted_signature_parameters\": "
     << stats.HattedSignatureParameters << ",\n";
  os << "    \"raw_signature_parameters\": "
     << stats.RawSignatureParameters << ",\n";
  os << "    \"unique_signature_parameters\": "
     << stats.UniqueSignatureParameters << ",\n";
  os << "    \"shared_signature_parameters\": "
     << stats.SharedSignatureParameters << ",\n";
  os << "    \"reference_signature_parameters\": "
     << stats.ReferenceSignatureParameters << ",\n";
  os << "    \"rebindable_handle_parameters\": "
     << stats.RebindableHandleParameters << ",\n";
  os << "    \"nullable_handle_parameters\": "
     << stats.NullableHandleParameters << ",\n";
  os << "    \"cede_hatted_parameters\": " << stats.CedeHattedParameters
     << ",\n";
  os << "    \"hatted_return_types\": " << stats.HattedReturnTypes
     << "\n";
  os << "  },\n";
  os << "  \"expressions\": {\n";
  os << "    \"unary_handle_expressions\": "
     << stats.UnaryHandleExpressions << ",\n";
  os << "    \"raw_handle_expressions\": " << stats.RawHandleExpressions
     << ",\n";
  os << "    \"unique_handle_expressions\": "
     << stats.UniqueHandleExpressions << ",\n";
  os << "    \"shared_handle_expressions\": "
     << stats.SharedHandleExpressions << ",\n";
  os << "    \"reference_handle_expressions\": "
     << stats.ReferenceHandleExpressions << ",\n";
  os << "    \"rebindable_unary_handle_expressions\": "
     << stats.RebindableUnaryHandleExpressions << ",\n";
  os << "    \"nullable_unary_handle_expressions\": "
     << stats.NullableUnaryHandleExpressions << ",\n";
  os << "    \"address_of_expressions\": " << stats.AddressOfExpressions
     << ",\n";
  os << "    \"dereference_expressions\": "
     << stats.DereferenceExpressions << ",\n";
  os << "    \"member_accesses\": " << stats.MemberAccesses << ",\n";
  os << "    \"member_handle_accesses\": " << stats.MemberHandleAccesses
     << ",\n";
  os << "    \"raw_member_handle_accesses\": "
     << stats.RawMemberHandleAccesses << ",\n";
  os << "    \"unique_member_handle_accesses\": "
     << stats.UniqueMemberHandleAccesses << ",\n";
  os << "    \"shared_member_handle_accesses\": "
     << stats.SharedMemberHandleAccesses << ",\n";
  os << "    \"reference_member_handle_accesses\": "
     << stats.ReferenceMemberHandleAccesses << ",\n";
  os << "    \"rebindable_member_handle_accesses\": "
     << stats.RebindableMemberHandleAccesses << ",\n";
  os << "    \"nullable_member_handle_accesses\": "
     << stats.NullableMemberHandleAccesses << ",\n";
  os << "    \"member_morphology_accesses\": "
     << stats.MemberMorphologyAccesses << ",\n";
  os << "    \"quoted_morphic_member_accesses\": "
     << stats.QuotedMorphicMemberAccesses << "\n";
  os << "  }\n";
  os << "}\n";
}

} // namespace toka
