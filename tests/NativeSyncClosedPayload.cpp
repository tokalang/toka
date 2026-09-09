#include "toka/Sema.h"
#include "toka/SourceManager.h"
#include <iostream>

bool g_JsonDiagnostics = false;
using namespace toka;
#define CHECK(expr) do { if (!(expr)) { std::cerr << "check failed: " << __LINE__ << '\n'; return __LINE__; } } while (false)

static ShapeMember field(const char *name, std::shared_ptr<Type> type) {
  ShapeMember member;
  member.Name = name;
  member.Type = type ? type->toString() : "T";
  member.ResolvedType = std::move(type);
  return member;
}
static std::unique_ptr<ShapeDecl> record(const char *name, std::vector<ShapeMember> fields) {
  auto result = std::make_unique<ShapeDecl>(true, name, std::vector<GenericParam>{},
                                          ShapeKind::Struct, std::move(fields));
  result->NominalId = NominalShapeId::fromResolverCoordinate("unit", "native-sync-types", name, 0);
  return result;
}
static std::shared_ptr<Type> typeOf(ShapeDecl *decl) {
  auto type = std::make_shared<ShapeType>(decl->Name);
  type->Decl = decl;
  return type;
}

int main() {
  SourceManager sources;
  DiagnosticEngine::init(sources);
  DiagnosticEngine::setPrintingEnabled(false);
  Sema sema;
  auto check = [&](const std::shared_ptr<Type> &type) { return sema.checkNativeSyncClosedPayload(type); };
  for (const char *name : {"bool", "i32", "u64", "f32", "char", "byte", "()"})
    CHECK(check(Type::fromString(name)).closed());
  for (const char *name : {"Addr", "OAddr", "*i32", "&i32", "fn() -> i32", "dyn fn() -> i32"})
    CHECK(check(Type::fromString(name)).State == NativeClosedPayloadState::ExternalDependency);
  CHECK(check(nullptr).State == NativeClosedPayloadState::Incomplete);
  CHECK(check(Type::fromString("T")).State == NativeClosedPayloadState::Incomplete);

  auto token = record("Token", {field("value", Type::fromString("i32"))});
  token->HasExplicitDrop = true;
  auto tokenType = typeOf(token.get());
  CHECK(check(tokenType).closed());
  CHECK(check(std::make_shared<UniquePointerType>(tokenType)).closed());
  CHECK(check(std::make_shared<SharedPointerType>(tokenType)).closed());
  CHECK(check(std::make_shared<ArrayType>(tokenType, 3)).closed());

  auto raw = record("Mutex", {field("address", Type::fromString("Addr"))});
  raw->HasExplicitDrop = true;
  CHECK(check(typeOf(raw.get())).State == NativeClosedPayloadState::ExternalDependency);
  auto borrowed = record("Borrowed", {field("reference", std::make_shared<ReferenceType>(tokenType))});
  CHECK(check(std::make_shared<SharedPointerType>(typeOf(borrowed.get()))).State == NativeClosedPayloadState::ExternalDependency);
  CHECK(check(std::make_shared<ArrayType>(typeOf(borrowed.get()), 2)).Path == "payload[].reference");
  auto fakeString = record("string", {field("buffer", Type::fromString("*byte"))});
  fakeString->HasExplicitDrop = true;
  CHECK(!check(typeOf(fakeString.get())).closed());

  auto variant = record("Outcome", {});
  variant->Kind = ShapeKind::Enum;
  ShapeMember empty; empty.Name = "None";
  ShapeMember full; full.Name = "Some"; full.SubMembers.push_back(field("item", tokenType));
  variant->Members = {empty, full};
  CHECK(check(typeOf(variant.get())).closed());
  variant->Members[1].SubMembers.push_back(field("hidden", Type::fromString("*byte")));
  CHECK(check(typeOf(variant.get())).Path == "payload.Some.hidden");
  variant->Members[1].SubMembers.back().ResolvedType.reset();
  CHECK(check(typeOf(variant.get())).State == NativeClosedPayloadState::Incomplete);

  auto generic = record("Box", {field("item", Type::fromString("T"))});
  GenericParam parameter; parameter.Name = "T"; generic->GenericParams.push_back(parameter);
  CHECK(check(typeOf(generic.get())).Reason == "InstantiationRequired");
  auto closedInstance = record("Box_i32", {field("item", Type::fromString("i32"))});
  closedInstance->InstantiationTemplate = generic.get();
  closedInstance->InstantiationArgs = {Type::fromString("i32")};
  auto borrowedInstance = record("Box_ref", {field("item", Type::fromString("&i32"))});
  borrowedInstance->InstantiationTemplate = generic.get();
  borrowedInstance->InstantiationArgs = {Type::fromString("&i32")};
  CHECK(check(typeOf(closedInstance.get())).closed());
  CHECK(check(typeOf(borrowedInstance.get())).State == NativeClosedPayloadState::ExternalDependency);
  closedInstance->InstantiationArgs = {Type::fromString("&i32")};
  CHECK(!check(typeOf(closedInstance.get())).closed());
  closedInstance->InstantiationArgs.clear();
  CHECK(check(typeOf(closedInstance.get())).Reason == "IncompleteInstantiationArguments");
  closedInstance->InstantiationArgs = {typeOf(closedInstance.get())};
  CHECK(check(typeOf(closedInstance.get())).Reason == "RecursiveProofUnclosed");
  closedInstance->InstantiationArgs = {Type::fromString("i32")};

  auto recursive = record("Recursive", {});
  auto recursiveType = typeOf(recursive.get());
  recursive->Members.push_back(field("next", std::make_shared<UniquePointerType>(recursiveType)));
  CHECK(check(recursiveType).Reason == "RecursiveProofUnclosed");
  auto unresolved = record("Incomplete", {field("item", nullptr)});
  CHECK(check(typeOf(unresolved.get())).Reason == "UnresolvedField");
  auto noIdentity = record("NoIdentity", {}); noIdentity->NominalId.reset();
  CHECK(check(typeOf(noIdentity.get())).Reason == "MissingNominalIdentity");
  // Repeated queries preserve all supplied type/field identities and results.
  auto before = tokenType->canonicalIdentity();
  CHECK(check(tokenType).closed() && tokenType->canonicalIdentity() == before);
  CHECK(token->Members[0].ResolvedType->toString() == "i32");
  std::cout << "native sync ClosedPayload prerequisite: pass (no witness authority)\n";
}
