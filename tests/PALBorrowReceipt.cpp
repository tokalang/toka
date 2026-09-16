#include "toka/PAL_Checker.h"

using namespace toka;
bool g_JsonDiagnostics = false;
#define CHECK(value) do { if (!(value)) return __LINE__; } while (false)

int main() {
  const AccessPath root{1, "owner", SourceLocation(1), {}};
  auto child = root;
  child.Projections.push_back(AccessProjection::field("field"));
  const auto write = PALOperationClass::ExclusivePayloadBorrow;
  const auto read = PALOperationClass::SharedPayloadBorrow;
  const auto loc = SourceLocation(7);
  PALChecker checker;
  const auto empty = checker.snapshot();
  PALBorrowReceiptPtr own;
  CHECK(checker.recordBorrow(root, true, loc, &own));
  CHECK(own);
  CHECK(checker.verifyOperation(root, write));
  CHECK(!checker.verifyArgumentBorrow(root, write, own));
  CHECK(!checker.verifyArgumentBorrow(root, read, own));
  CHECK(checker.getState(root) == PathState::BorrowedMut);
  CHECK(checker.verifyArgumentBorrow(root, PALOperationClass::Invalidation, own));
  CHECK(checker.verifyArgumentBorrow(root, PALOperationClass::HandleRebind, own));
  CHECK(checker.verifyArgumentBorrow(child, write, own));

  // Neither source-coordinate reuse nor restoring a snapshot reuses a grant.
  auto stale = own;
  checker.restore(empty);
  CHECK(checker.recordBorrow(root, true, loc, &own));
  CHECK(own != stale);
  CHECK(checker.verifyArgumentBorrow(root, write, stale));
  auto failed = own;
  CHECK(!checker.recordBorrow(root, true, loc, &failed));
  CHECK(!failed);
  CHECK(checker.verifyArgumentBorrow(root, write, failed));
  CHECK(!checker.verifyArgumentBorrow(root, write, own));
  checker.commitTransient(root);
  CHECK(checker.verifyArgumentBorrow(root, write, own));

  // A receipt cannot exclude a separate checker's identical path/coordinate.
  PALChecker unrelated;
  CHECK(unrelated.recordBorrow(root, true, loc));
  CHECK(unrelated.verifyArgumentBorrow(root, write, own));

  // Merge may retain several overlapping entries. Excluding one must not
  // skip the parent/child or outer-scope loan contributed by another branch.
  for (bool ownIsParent : {false, true}) {
    PALChecker base, left, right, merged;
    left.pushScope();
    PALBorrowReceiptPtr receipt;
    auto ownPath = ownIsParent ? root : child;
    auto otherPath = ownIsParent ? child : root;
    CHECK(left.recordBorrow(ownPath, true, loc, &receipt));
    CHECK(right.recordBorrow(otherPath, false, loc));
    right.commitTransient(otherPath);
    merged.mergeBranches(base, left, true, right, true);
    CHECK(merged.verifyArgumentBorrow(ownPath, write, receipt));
    CHECK(merged.getState(otherPath) == PathState::BorrowedShared);
  }
  {
    PALChecker base, left, right, merged;
    PALBorrowReceiptPtr a, b;
    CHECK(left.recordBorrow(root, true, loc, &a));
    CHECK(right.recordBorrow(root, true, loc, &b));
    CHECK(a != b);
    merged.mergeBranches(base, left, true, right, true);
    CHECK(merged.verifyArgumentBorrow(root, write, a));
    CHECK(merged.verifyArgumentBorrow(root, write, b));
    merged.mergeBranches(base, left, true, base, true);
    CHECK(merged.verifyArgumentBorrow(root, write, a));
    merged.mergeBranches(base, base, true, left, true);
    CHECK(merged.verifyArgumentBorrow(root, write, a));
    auto same = left.snapshot();
    merged.mergeBranches(left, left, true, same, true);
    CHECK(!merged.verifyArgumentBorrow(root, write, a));
  }
  {
    PALChecker shared;
    PALBorrowReceiptPtr a, b;
    CHECK(shared.recordBorrow(root, false, loc, &a));
    CHECK(shared.verifyArgumentBorrow(root, write, a));
    CHECK(shared.recordBorrow(root, false, loc, &b));
    CHECK(shared.verifyArgumentBorrow(root, write, a));
    CHECK(shared.verifyArgumentBorrow(root, write, b));
    CHECK(shared.upgradeBorrow(root));
    CHECK(shared.verifyArgumentBorrow(root, read, b));
  }
  {
    // An independently represented overlapping entry must still be visited
    // when the caller presents a valid receipt for its own read loan.
    PALChecker overlap;
    PALBorrowReceiptPtr receipt;
    CHECK(overlap.recordBorrow(root, false, loc, &receipt));
    CHECK(overlap.recordBorrow(child, false, SourceLocation(8)));
    CHECK(overlap.upgradeBorrow(child));
    auto conflict = overlap.verifyArgumentBorrow(root, read, receipt);
    CHECK(conflict && conflict->Path == child);
  }
  UnaryExpr expression(TokenType::Ampersand, std::make_unique<VariableExpr>("owner"));
  {
    PALChecker nested;
    nested.pushScope();
    CHECK(nested.recordBorrow(child, false, loc));
    nested.commitTransient(child, 1);
    nested.popScope();
    CHECK(nested.getState(child) == PathState::BorrowedShared);
    CHECK(nested.verifyOperation(root, write));
  }
  {
    PALChecker shared;
    CHECK(shared.recordBorrow(root, false, loc));
    CHECK(shared.recordBorrow(root, false, loc));
    shared.commitTransient(root);
    shared.clearTransient();
    CHECK(shared.getState(root) == PathState::BorrowedShared);
  }
  expression.AcquiredBorrow = own;
  auto clone = expression.clone();
  CHECK(!static_cast<UnaryExpr *>(clone.get())->AcquiredBorrow);
  MemberExpr member(std::make_unique<VariableExpr>("owner"), "&field");
  member.AcquiredBorrow = own;
  auto memberClone = member.clone();
  CHECK(!static_cast<MemberExpr *>(memberClone.get())->AcquiredBorrow);
  return 0;
}
