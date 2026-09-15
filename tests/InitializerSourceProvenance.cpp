#include "toka/AST.h"
#include <iostream>

bool g_JsonDiagnostics = false;

int main() {
  // Model the post-expansion lists that must not erase their earlier origin.
  for (const auto &original : {std::vector<std::string>{"file"},
                               std::vector<std::string>{"tag", "*"},
                               std::vector<std::string>{"file", ".."},
                               std::vector<std::string>{"file", "tag"}}) {
    toka::InitStructExpr init("Box", {});
    init.PreExpansionMemberNames = original;
    init.Members.emplace_back("file", nullptr);
    init.Members.emplace_back("tag", nullptr);
    auto clonedNode = init.clone();
    auto *clone = dynamic_cast<toka::InitStructExpr *>(clonedNode.get());
    if (!clone || !clone->PreExpansionMemberNames ||
        *clone->PreExpansionMemberNames != original || clone->Members.size() != 2)
      return 1;
  }
  toka::InitStructExpr unchecked("Box", {});
  auto uncheckedClone = unchecked.clone();
  if (static_cast<toka::InitStructExpr *>(uncheckedClone.get())->PreExpansionMemberNames) return 2;
  std::cout << "initializer expansion provenance survives clone; absence stays absent\n";
  return 0;
}
