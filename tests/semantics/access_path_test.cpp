#include "toka/AccessPath.h"
#include <cassert>

using namespace toka;

static AccessPath root(uint64_t id, const char *name) {
  AccessPath path;
  path.RootID = id;
  path.RootName = name;
  return path;
}

int main() {
  AccessPath object = root(1, "object");
  AccessPath left = object;
  left.Projections.push_back(AccessProjection::field("left"));
  AccessPath right = object;
  right.Projections.push_back(AccessProjection::field("right"));

  assert(classifyAccessPathOverlap(object, left) ==
         AccessPathOverlap::MustOverlap);
  assert(classifyAccessPathOverlap(left, right) ==
         AccessPathOverlap::NoOverlap);

  AccessPath shadowed = root(2, "object");
  assert(classifyAccessPathOverlap(object, shadowed) ==
         AccessPathOverlap::NoOverlap);

  AccessPath sameSymbolDifferentDisplay = root(1, "^object");
  assert(object == sameSymbolDifferentDisplay);
  assert(!(object < sameSymbolDifferentDisplay));
  assert(!(sameSymbolDifferentDisplay < object));

  AccessPath first = object;
  first.Projections.push_back(AccessProjection::constantIndex(0));
  AccessPath firstAgain = first;
  AccessPath second = object;
  second.Projections.push_back(AccessProjection::constantIndex(1));
  assert(classifyAccessPathOverlap(first, firstAgain) ==
         AccessPathOverlap::MustOverlap);
  assert(classifyAccessPathOverlap(first, second) ==
         AccessPathOverlap::MayOverlap);

  AccessPath dynamic = object;
  dynamic.Projections.push_back(AccessProjection::dynamicIndex());
  assert(classifyAccessPathOverlap(first, dynamic) ==
         AccessPathOverlap::MayOverlap);

  AccessPath unknown = object;
  unknown.Projections.push_back(AccessProjection::unknown());
  assert(classifyAccessPathOverlap(unknown, object) ==
         AccessPathOverlap::MayOverlap);

  assert(accessPathIsLegacyPrefix(object, first));
  assert(first.toLegacyString() == "object");
  assert(first.toDebugString() == "object[0]");
  return 0;
}
