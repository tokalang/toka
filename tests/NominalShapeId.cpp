#include "toka/AST.h"
#include "toka/DiagnosticEngine.h"
#include "toka/NominalShapeId.h"
#include "toka/PathUtils.h"
#include "toka/Sema.h"
#include "toka/SourceManager.h"
#include "toka/TKIExporter.h"

#include "llvm/Support/raw_ostream.h"

#include <memory>
#include <string>
#include <utility>

using toka::NominalShapeId;

bool g_JsonDiagnostics = false;

#define CHECK(condition)                                                       \
  do {                                                                         \
    if (!(condition))                                                          \
      return __LINE__;                                                         \
  } while (false)

namespace {

std::unique_ptr<toka::ShapeDecl> shape(const std::string &name) {
  return std::make_unique<toka::ShapeDecl>(
      true, name, std::vector<toka::GenericParam>{}, toka::ShapeKind::Struct,
      std::vector<toka::ShapeMember>{});
}

void setPath(toka::Module &module, const std::string &path) {
  module.SourcePath = path;
  module.ResolvedPath = path;
}

void setCoordinate(toka::Module &module, const std::string &crate,
                   const std::string &logicalModule) {
  module.ShadowCoordinateKnown = true;
  module.ShadowCrateId = crate;
  module.ShadowLogicalModulePath = logicalModule;
}

} // namespace

int main() {
  toka::SourceManager sources;
  toka::DiagnosticEngine::init(sources);
  toka::DiagnosticEngine::setPrintingEnabled(false);
  toka::DiagnosticEngine::reset();

  const auto left = NominalShapeId::fromSourcePath(
      toka::PathUtils::canonicalize("/workspace/left/model.tk"), "Model", 0);
  const auto right = NominalShapeId::fromSourcePath(
      toka::PathUtils::canonicalize("/workspace/right/model.tk"), "Model", 0);
  CHECK(left != right);

  const auto normalized = NominalShapeId::fromSourcePath(
      toka::PathUtils::canonicalize("/workspace/left/../left/model.tk"),
      "Model", 0);
  CHECK(left == normalized);

  const auto resolver = NominalShapeId::fromResolverCoordinate(
      "workspace-node", "left/model", "Model", 0);
  const auto relocated = NominalShapeId::fromResolverCoordinate(
      "workspace-node", "left/model", "Model", 0);
  CHECK(resolver == relocated);
  CHECK(resolver != NominalShapeId::fromResolverCoordinate(
                        "workspace-node", "right/model", "Model", 0));
  CHECK(resolver != NominalShapeId::fromResolverCoordinate(
                        "workspace-node", "left/model", "Item", 0));

  const auto delimitedA = NominalShapeId::fromResolverCoordinate(
      "crate:a", "module;b", "Shape", 1);
  const auto delimitedB = NominalShapeId::fromResolverCoordinate(
      "crate", "a:module;b", "Shape", 1);
  CHECK(delimitedA != delimitedB);

  toka::Module leftModule;
  setPath(leftModule, "/workspace/left/model.tk");
  leftModule.Shapes.push_back(shape("Model"));
  toka::Module rightModule;
  setPath(rightModule, "/workspace/right/model.tk");
  rightModule.Shapes.push_back(shape("Model"));

  toka::Sema sourceSema;
  sourceSema.declareGlobals(leftModule);
  sourceSema.declareGlobals(rightModule);
  CHECK(leftModule.Shapes[0]->NominalId.has_value());
  CHECK(rightModule.Shapes[0]->NominalId.has_value());
  CHECK(*leftModule.Shapes[0]->NominalId !=
        *rightModule.Shapes[0]->NominalId);
  sourceSema.checkShapeSovereignty();
  CHECK(!sourceSema.hasErrors());

  toka::Module sourceTransport;
  setPath(sourceTransport, "/old/location/model.tk");
  setCoordinate(sourceTransport, "workspace-node", "models/model");
  sourceTransport.Shapes.push_back(shape("Model"));
  toka::Sema sourceTransportSema;
  sourceTransportSema.declareGlobals(sourceTransport);

  toka::Module interfaceTransport;
  interfaceTransport.SourcePath = "/old/location/model.tk";
  interfaceTransport.ResolvedPath = "/cache/model.tki";
  interfaceTransport.IsInterface = true;
  setCoordinate(interfaceTransport, "workspace-node", "models/model");
  interfaceTransport.Shapes.push_back(shape("Model"));
  toka::Sema interfaceTransportSema;
  interfaceTransportSema.declareGlobals(interfaceTransport);
  CHECK(sourceTransport.Shapes[0]->NominalId.has_value());
  CHECK(interfaceTransport.Shapes[0]->NominalId.has_value());
  CHECK(*sourceTransport.Shapes[0]->NominalId ==
        *interfaceTransport.Shapes[0]->NominalId);

  toka::Module incremental;
  setPath(incremental, "/workspace/incremental.tk");
  incremental.Shapes.push_back(shape("Declared"));
  auto synthesized = shape("__Closure_test");
  synthesized->IsCompilerSynthesized = true;
  incremental.Shapes.push_back(std::move(synthesized));

  toka::Sema firstRevision;
  firstRevision.declareGlobals(incremental);
  CHECK(incremental.Shapes[0]->NominalId.has_value());
  CHECK(!incremental.Shapes[1]->NominalId.has_value());
  firstRevision.checkShapeSovereignty();
  CHECK(!firstRevision.hasErrors());

  toka::Sema secondRevision;
  secondRevision.declareGlobals(incremental);
  CHECK(incremental.Shapes[0]->NominalId.has_value());
  CHECK(!incremental.Shapes[1]->NominalId.has_value());
  secondRevision.checkShapeSovereignty();
  CHECK(!secondRevision.hasErrors());

  std::string exported;
  llvm::raw_string_ostream exportStream(exported);
  toka::TKIExporter(exportStream).exportModule(incremental);
  exportStream.flush();
  CHECK(exported.find("shape Declared") != std::string::npos);
  CHECK(exported.find("__Closure_test") == std::string::npos);

  toka::DiagnosticEngine::reset();
  toka::Module incompleteCoordinate;
  setPath(incompleteCoordinate, "/workspace/incomplete.tk");
  incompleteCoordinate.ShadowCoordinateKnown = true;
  incompleteCoordinate.ShadowLogicalModulePath = "incomplete";
  incompleteCoordinate.Shapes.push_back(shape("Model"));
  toka::Sema incompleteCoordinateSema;
  incompleteCoordinateSema.declareGlobals(incompleteCoordinate);
  CHECK(incompleteCoordinateSema.hasErrors());
  CHECK(!incompleteCoordinate.Shapes[0]->NominalId.has_value());

  toka::DiagnosticEngine::reset();
  toka::Module tampered;
  setPath(tampered, "/workspace/tampered.tk");
  tampered.Shapes.push_back(shape("Model"));
  toka::Sema tamperedSema;
  tamperedSema.declareGlobals(tampered);
  tampered.Shapes[0]->NominalId = NominalShapeId::fromSourcePath(
      "/workspace/other.tk", "Model", 0);
  tamperedSema.checkShapeSovereignty();
  CHECK(tamperedSema.hasErrors());

  toka::DiagnosticEngine::reset();
  toka::Module duplicateA;
  setPath(duplicateA, "/workspace/a/model.tk");
  setCoordinate(duplicateA, "workspace-node", "duplicate/model");
  duplicateA.Shapes.push_back(shape("Model"));
  toka::Module duplicateB;
  setPath(duplicateB, "/workspace/b/model.tk");
  setCoordinate(duplicateB, "workspace-node", "duplicate/model");
  duplicateB.Shapes.push_back(shape("Model"));
  toka::Sema duplicateSema;
  duplicateSema.declareGlobals(duplicateA);
  duplicateSema.declareGlobals(duplicateB);
  duplicateSema.checkShapeSovereignty();
  CHECK(duplicateSema.hasErrors());

  toka::DiagnosticEngine::reset();
  toka::Module detachedOwner;
  setPath(detachedOwner, "/workspace/detached.tk");
  detachedOwner.Shapes.push_back(shape("Detached"));
  toka::Sema detachedSema;
  detachedSema.declareGlobals(detachedOwner);
  auto detached = std::move(detachedOwner.Shapes[0]);
  detachedOwner.Shapes.clear();
  toka::Module newOwner;
  newOwner.Shapes.push_back(std::move(detached));
  detachedSema.checkShapeSovereignty();
  CHECK(detachedSema.hasErrors());

  toka::DiagnosticEngine::reset();
  toka::Module wrongBinding;
  setPath(wrongBinding, "/workspace/wrong-binding.tk");
  auto target = shape("Target");
  auto wrong = shape("Wrong");
  toka::ShapeDecl *wrongDecl = wrong.get();
  toka::ShapeMember field;
  field.Name = "value";
  field.Type = "Target";
  field.TypeSyntax = toka::TypeSyntax::named(
      "Target", toka::SourceLocation{}, toka::SourceLocation{});
  auto resolvedWrong = std::make_shared<toka::ShapeType>("Target");
  resolvedWrong->resolve(wrongDecl);
  field.ResolvedType = resolvedWrong;
  std::vector<toka::ShapeMember> members;
  members.push_back(std::move(field));
  auto container = std::make_unique<toka::ShapeDecl>(
      true, "Container", std::vector<toka::GenericParam>{},
      toka::ShapeKind::Struct, std::move(members));
  wrongBinding.Shapes.push_back(std::move(target));
  wrongBinding.Shapes.push_back(std::move(wrong));
  wrongBinding.Shapes.push_back(std::move(container));
  toka::Sema wrongBindingSema;
  wrongBindingSema.declareGlobals(wrongBinding);
  wrongBindingSema.checkShapeSovereignty();
  CHECK(wrongBindingSema.hasErrors());

  toka::DiagnosticEngine::reset();
  toka::Module sharedBinding;
  setPath(sharedBinding, "/workspace/shared-binding.tk");
  auto sharedTarget = shape("Target");
  auto sharedWrong = shape("Wrong");
  auto sharedResolvedWrong = std::make_shared<toka::ShapeType>("Wrong");
  sharedResolvedWrong->resolve(sharedWrong.get());
  toka::ShapeMember firstSharedField;
  firstSharedField.Name = "first";
  firstSharedField.TypeSyntax = toka::TypeSyntax::named(
      "Wrong", toka::SourceLocation{}, toka::SourceLocation{});
  firstSharedField.ResolvedType = sharedResolvedWrong;
  toka::ShapeMember secondSharedField;
  secondSharedField.Name = "second";
  secondSharedField.TypeSyntax = toka::TypeSyntax::named(
      "Target", toka::SourceLocation{}, toka::SourceLocation{});
  secondSharedField.ResolvedType = sharedResolvedWrong;
  std::vector<toka::ShapeMember> sharedMembers;
  sharedMembers.push_back(std::move(firstSharedField));
  sharedMembers.push_back(std::move(secondSharedField));
  auto sharedContainer = std::make_unique<toka::ShapeDecl>(
      true, "Container", std::vector<toka::GenericParam>{},
      toka::ShapeKind::Struct, std::move(sharedMembers));
  sharedBinding.Shapes.push_back(std::move(sharedTarget));
  sharedBinding.Shapes.push_back(std::move(sharedWrong));
  sharedBinding.Shapes.push_back(std::move(sharedContainer));
  toka::Sema sharedBindingSema;
  sharedBindingSema.declareGlobals(sharedBinding);
  sharedBindingSema.checkShapeSovereignty();
  CHECK(sharedBindingSema.hasErrors());

  toka::DiagnosticEngine::reset();
  toka::Module nestedPayload;
  setPath(nestedPayload, "/workspace/nested-payload.tk");
  auto nestedTarget = shape("Target");
  auto nestedWrong = shape("Wrong");
  auto nestedResolvedWrong = std::make_shared<toka::ShapeType>("Target");
  nestedResolvedWrong->resolve(nestedWrong.get());
  toka::ShapeMember deepPayload;
  deepPayload.Name = "deep";
  deepPayload.TypeSyntax = toka::TypeSyntax::named(
      "Target", toka::SourceLocation{}, toka::SourceLocation{});
  deepPayload.ResolvedType = nestedResolvedWrong;
  toka::ShapeMember middlePayload;
  middlePayload.Name = "middle";
  middlePayload.SubMembers.push_back(std::move(deepPayload));
  toka::ShapeMember outerPayload;
  outerPayload.Name = "Variant";
  outerPayload.SubMembers.push_back(std::move(middlePayload));
  std::vector<toka::ShapeMember> nestedMembers;
  nestedMembers.push_back(std::move(outerPayload));
  auto nestedContainer = std::make_unique<toka::ShapeDecl>(
      true, "Container", std::vector<toka::GenericParam>{},
      toka::ShapeKind::Enum, std::move(nestedMembers));
  nestedPayload.Shapes.push_back(std::move(nestedTarget));
  nestedPayload.Shapes.push_back(std::move(nestedWrong));
  nestedPayload.Shapes.push_back(std::move(nestedContainer));
  toka::Sema nestedPayloadSema;
  nestedPayloadSema.declareGlobals(nestedPayload);
  nestedPayloadSema.checkShapeSovereignty();
  CHECK(nestedPayloadSema.hasErrors());

  toka::DiagnosticEngine::reset();
  toka::Module syntheticWrapper;
  setPath(syntheticWrapper, "/workspace/synthetic-wrapper.tk");
  auto wrapperTarget = shape("Target");
  auto wrapperWrong = shape("Wrong");
  auto wrapperResolvedWrong = std::make_shared<toka::ShapeType>("Target");
  wrapperResolvedWrong->resolve(wrapperWrong.get());
  toka::ShapeMember wrappedField;
  wrappedField.Name = "item";
  wrappedField.TypeSyntax = toka::TypeSyntax::named(
      "Target", toka::SourceLocation{}, toka::SourceLocation{});
  wrappedField.ResolvedType = wrapperResolvedWrong;
  std::vector<toka::ShapeMember> wrappedMembers;
  wrappedMembers.push_back(std::move(wrappedField));
  auto wrapper = std::make_unique<toka::ShapeDecl>(
      false, "Wrapper_M_Target", std::vector<toka::GenericParam>{},
      toka::ShapeKind::Struct, std::move(wrappedMembers));
  wrapper->IsCompilerSynthesized = true;
  auto wrapperType =
      std::make_shared<toka::ShapeType>("Wrapper_M_Target");
  wrapperType->resolve(wrapper.get());
  toka::ShapeMember wrapperOwnerField;
  wrapperOwnerField.Name = "values";
  wrapperOwnerField.ResolvedType = wrapperType;
  std::vector<toka::ShapeMember> wrapperOwnerMembers;
  wrapperOwnerMembers.push_back(std::move(wrapperOwnerField));
  auto wrapperOwner = std::make_unique<toka::ShapeDecl>(
      true, "Container", std::vector<toka::GenericParam>{},
      toka::ShapeKind::Struct, std::move(wrapperOwnerMembers));
  syntheticWrapper.Shapes.push_back(std::move(wrapperTarget));
  syntheticWrapper.Shapes.push_back(std::move(wrapperWrong));
  syntheticWrapper.Shapes.push_back(std::move(wrapper));
  syntheticWrapper.Shapes.push_back(std::move(wrapperOwner));
  toka::Sema syntheticWrapperSema;
  syntheticWrapperSema.declareGlobals(syntheticWrapper);
  syntheticWrapperSema.checkShapeSovereignty();
  CHECK(syntheticWrapperSema.hasErrors());

  toka::DiagnosticEngine::reset();
  toka::Module staleBinding;
  setPath(staleBinding, "/workspace/stale-binding.tk");
  auto staleDecl = shape("Stale");
  staleDecl->NominalId = NominalShapeId::fromSourcePath(
      "/workspace/old-stale.tk", "Stale", 0);
  auto staleType = std::make_shared<toka::ShapeType>("Stale");
  staleType->resolve(staleDecl.get());
  toka::ShapeMember staleField;
  staleField.Name = "value";
  staleField.ResolvedType = staleType;
  std::vector<toka::ShapeMember> staleMembers;
  staleMembers.push_back(std::move(staleField));
  staleBinding.Shapes.push_back(std::make_unique<toka::ShapeDecl>(
      true, "Container", std::vector<toka::GenericParam>{},
      toka::ShapeKind::Struct, std::move(staleMembers)));
  toka::Sema staleBindingSema;
  staleBindingSema.declareGlobals(staleBinding);
  staleBindingSema.checkShapeSovereignty();
  CHECK(staleBindingSema.hasErrors());

  toka::DiagnosticEngine::reset();
  toka::Module missingBinding;
  setPath(missingBinding, "/workspace/missing-binding.tk");
  missingBinding.Shapes.push_back(shape("Target"));
  toka::ShapeMember missingField;
  missingField.Name = "value";
  missingField.TypeSyntax = toka::TypeSyntax::named(
      "Target", toka::SourceLocation{}, toka::SourceLocation{});
  missingField.ResolvedType = std::make_shared<toka::ShapeType>("Target");
  std::vector<toka::ShapeMember> missingMembers;
  missingMembers.push_back(std::move(missingField));
  missingBinding.Shapes.push_back(std::make_unique<toka::ShapeDecl>(
      true, "Container", std::vector<toka::GenericParam>{},
      toka::ShapeKind::Struct, std::move(missingMembers)));
  toka::Sema missingBindingSema;
  missingBindingSema.declareGlobals(missingBinding);
  missingBindingSema.checkShapeSovereignty();
  CHECK(missingBindingSema.hasErrors());

  toka::DiagnosticEngine::reset();
  toka::Module remoteAlias;
  setPath(remoteAlias, "/workspace/remote-alias.tk");
  remoteAlias.Shapes.push_back(shape("Thing"));
  toka::ShapeDecl *remoteThing = remoteAlias.Shapes[0].get();
  toka::Module localAlias;
  setPath(localAlias, "/workspace/local-alias.tk");
  auto aliasImport = std::make_unique<toka::ImportDecl>(
      false, "./remote-alias", "",
      std::vector<toka::ImportItem>{{"Thing", "Alias"}});
  aliasImport->ResolvedPath = remoteAlias.ResolvedPath;
  localAlias.Imports.push_back(std::move(aliasImport));
  localAlias.Shapes.push_back(shape("Thing"));
  auto aliasedType = std::make_shared<toka::ShapeType>("Alias");
  aliasedType->resolve(remoteThing);
  toka::ShapeMember aliasedField;
  aliasedField.Name = "value";
  aliasedField.TypeSyntax = toka::TypeSyntax::named(
      "Alias", toka::SourceLocation{}, toka::SourceLocation{});
  aliasedField.ResolvedType = aliasedType;
  std::vector<toka::ShapeMember> aliasedMembers;
  aliasedMembers.push_back(std::move(aliasedField));
  localAlias.Shapes.push_back(std::make_unique<toka::ShapeDecl>(
      true, "Holder", std::vector<toka::GenericParam>{},
      toka::ShapeKind::Struct, std::move(aliasedMembers)));
  toka::Sema aliasSema;
  aliasSema.declareGlobals(remoteAlias);
  aliasSema.declareGlobals(localAlias);
  aliasSema.checkShapeSovereignty();
  CHECK(!aliasSema.hasErrors());

  toka::DiagnosticEngine::reset();
  toka::Module extractedRegistryModule;
  setPath(extractedRegistryModule, "/workspace/extracted-registry.tk");
  extractedRegistryModule.Shapes.push_back(shape("Declared"));
  toka::Sema extractedRegistrySema;
  extractedRegistrySema.declareGlobals(extractedRegistryModule);
  auto extractedRegistry = extractedRegistrySema.extractGenericRegistry();
  CHECK(extractedRegistry != nullptr);
  extractedRegistrySema.checkShapeSovereignty();
  CHECK(!extractedRegistrySema.hasErrors());

  auto rejectsWrappedBinding = [&](const std::string &path, auto wrapType,
                                   auto wrapSyntax) {
    toka::DiagnosticEngine::reset();
    toka::Module module;
    setPath(module, path);
    auto wrappedTarget = shape("Target");
    auto wrappedWrong = shape("Wrong");
    auto wrongType = std::make_shared<toka::ShapeType>("Target");
    wrongType->resolve(wrappedWrong.get());
    auto targetSyntax = toka::TypeSyntax::named(
        "Target", toka::SourceLocation{}, toka::SourceLocation{});
    toka::ShapeMember member;
    member.Name = "value";
    member.ResolvedType = wrapType(wrongType);
    member.TypeSyntax = wrapSyntax(targetSyntax);
    std::vector<toka::ShapeMember> wrapped;
    wrapped.push_back(std::move(member));
    module.Shapes.push_back(std::move(wrappedTarget));
    module.Shapes.push_back(std::move(wrappedWrong));
    module.Shapes.push_back(std::make_unique<toka::ShapeDecl>(
        true, "Container", std::vector<toka::GenericParam>{},
        toka::ShapeKind::Struct, std::move(wrapped)));
    toka::Sema sema;
    sema.declareGlobals(module);
    sema.checkShapeSovereignty();
    return sema.hasErrors();
  };

  CHECK(rejectsWrappedBinding(
      "/workspace/reference-binding.tk",
      [](const std::shared_ptr<toka::Type> &inner) {
        return std::make_shared<toka::ReferenceType>(inner);
      },
      [](const toka::TypeSyntaxPtr &inner) {
        return toka::TypeSyntax::morphology(
            "&", inner, toka::SourceLocation{}, toka::SourceLocation{});
      }));
  CHECK(rejectsWrappedBinding(
      "/workspace/array-binding.tk",
      [](const std::shared_ptr<toka::Type> &inner) {
        return std::make_shared<toka::ArrayType>(inner, 1);
      },
      [](const toka::TypeSyntaxPtr &inner) {
        return toka::TypeSyntax::array(
            inner,
            toka::TypeArgumentSyntax::constant(
                "1", toka::SourceLocation{}, toka::SourceLocation{}),
            toka::SourceLocation{}, toka::SourceLocation{});
      }));
  CHECK(rejectsWrappedBinding(
      "/workspace/slice-binding.tk",
      [](const std::shared_ptr<toka::Type> &inner) {
        return std::make_shared<toka::SliceType>(inner);
      },
      [](const toka::TypeSyntaxPtr &inner) {
        return toka::TypeSyntax::slice(
            inner, toka::SourceLocation{}, toka::SourceLocation{});
      }));
  CHECK(rejectsWrappedBinding(
      "/workspace/uninit-binding.tk",
      [](const std::shared_ptr<toka::Type> &inner) {
        return std::make_shared<toka::UninitType>(inner);
      },
      [](const toka::TypeSyntaxPtr &inner) {
        std::vector<toka::TypeArgumentSyntax> arguments;
        arguments.push_back(toka::TypeArgumentSyntax::type(inner));
        return toka::TypeSyntax::generic(
            toka::TypeSyntax::named("Uninit", toka::SourceLocation{},
                                    toka::SourceLocation{}),
            std::move(arguments), toka::SourceLocation{},
            toka::SourceLocation{});
      }));
  CHECK(rejectsWrappedBinding(
      "/workspace/function-binding.tk",
      [](const std::shared_ptr<toka::Type> &inner) {
        return std::make_shared<toka::FunctionType>(
            std::vector<std::shared_ptr<toka::Type>>{}, inner);
      },
      [](const toka::TypeSyntaxPtr &inner) {
        return toka::TypeSyntax::function(
            "fn", std::vector<toka::TypeSyntaxPtr>{}, inner, true, false,
            toka::SourceLocation{}, toka::SourceLocation{});
      }));
  CHECK(rejectsWrappedBinding(
      "/workspace/dyn-function-binding.tk",
      [](const std::shared_ptr<toka::Type> &inner) {
        return std::make_shared<toka::DynFnType>(
            std::vector<std::shared_ptr<toka::Type>>{}, inner);
      },
      [](const toka::TypeSyntaxPtr &inner) {
        return toka::TypeSyntax::function(
            "dyn fn", std::vector<toka::TypeSyntaxPtr>{}, inner, true,
            false, toka::SourceLocation{}, toka::SourceLocation{});
      }));
  CHECK(rejectsWrappedBinding(
      "/workspace/generic-argument-binding.tk",
      [](const std::shared_ptr<toka::Type> &inner) {
        return std::make_shared<toka::ShapeType>(
            "Box", std::vector<std::shared_ptr<toka::Type>>{inner});
      },
      [](const toka::TypeSyntaxPtr &inner) {
        std::vector<toka::TypeArgumentSyntax> arguments;
        arguments.push_back(toka::TypeArgumentSyntax::type(inner));
        return toka::TypeSyntax::generic(
            toka::TypeSyntax::named("Box", toka::SourceLocation{},
                                    toka::SourceLocation{}),
            std::move(arguments), toka::SourceLocation{},
            toka::SourceLocation{});
      }));

  toka::DiagnosticEngine::reset();
  toka::DiagnosticEngine::setPrintingEnabled(true);
  return 0;
}
