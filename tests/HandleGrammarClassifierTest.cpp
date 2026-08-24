// Copyright (c) 2026 YiZhonghua<zhyi@dpai.com>. All rights reserved.
// Licensed under the Apache License, Version 2.0.

#include "toka/Type.h"
#include <iostream>
#include <vector>
#include <string>

bool g_JsonDiagnostics = false;

namespace {

using toka::Type;
using toka::HandleGrammarViolation;

struct TestCase {
    std::string typeStr;
    HandleGrammarViolation expectedViolation;
    unsigned expectedManagedDepth;
    unsigned expectedBorrowDepth;
    unsigned expectedRawDepth;
};

bool checkTestCase(const TestCase &tc) {
    auto ty = Type::fromString(tc.typeStr);
    if (!ty) {
        std::cerr << "FAIL: Type::fromString returned null for '" << tc.typeStr << "'\n";
        return false;
    }
    auto profile = Type::classifyHandleGrammar(ty);
    if (profile.violation != tc.expectedViolation) {
        std::cerr << "FAIL: " << tc.typeStr << " violation mismatch. Expected "
                  << static_cast<int>(tc.expectedViolation) << " (" << profile.describeViolation()
                  << "), Got " << static_cast<int>(profile.violation) << "\n";
        return false;
    }
    if (profile.continuousManagedDepth != tc.expectedManagedDepth) {
        std::cerr << "FAIL: " << tc.typeStr << " managed depth mismatch. Expected "
                  << tc.expectedManagedDepth << ", Got " << profile.continuousManagedDepth << "\n";
        return false;
    }
    if (profile.continuousBorrowDepth != tc.expectedBorrowDepth) {
        std::cerr << "FAIL: " << tc.typeStr << " borrow depth mismatch. Expected "
                  << tc.expectedBorrowDepth << ", Got " << profile.continuousBorrowDepth << "\n";
        return false;
    }
    if (profile.continuousRawDepth != tc.expectedRawDepth) {
        std::cerr << "FAIL: " << tc.typeStr << " raw depth mismatch. Expected "
                  << tc.expectedRawDepth << ", Got " << profile.continuousRawDepth << "\n";
        return false;
    }
    return true;
}

} // namespace

int main() {
    std::vector<TestCase> testCases = {
        // --- 1. Legal Level 1 Handles ---
        {"*i32", HandleGrammarViolation::None, 0, 0, 1},
        {"nul *i32", HandleGrammarViolation::None, 0, 0, 1},
        {"^i32", HandleGrammarViolation::None, 1, 0, 0},
        {"~i32", HandleGrammarViolation::None, 1, 0, 0},
        {"&i32", HandleGrammarViolation::None, 1, 1, 0},
        {"&i32#", HandleGrammarViolation::None, 1, 1, 0},

        // --- 2. Legal Level 2 Handles ---
        {"**i32", HandleGrammarViolation::None, 0, 0, 2},
        {"&^i32", HandleGrammarViolation::None, 2, 1, 0},
        {"&~i32", HandleGrammarViolation::None, 2, 1, 0},
        {"&&i32", HandleGrammarViolation::None, 2, 2, 0},

        // --- 3. Legal Raw Depth N Handles ---
        {"***i32", HandleGrammarViolation::None, 0, 0, 3},
        {"****i32", HandleGrammarViolation::None, 0, 0, 4},
        {"*****i32", HandleGrammarViolation::None, 0, 0, 5},

        // --- 4. Illegal Managed Layer Order ---
        {"^&i32", HandleGrammarViolation::InvalidManagedLayerOrder, 2, 0, 0},
        {"~&i32", HandleGrammarViolation::InvalidManagedLayerOrder, 2, 0, 0},

        // --- 5. Illegal Exceeded Managed / Borrow Depth ---
        {"^^i32", HandleGrammarViolation::ExceededManagedDepth, 2, 0, 0},
        {"~~i32", HandleGrammarViolation::ExceededManagedDepth, 2, 0, 0},
        {"^~i32", HandleGrammarViolation::ExceededManagedDepth, 2, 0, 0},
        {"~^i32", HandleGrammarViolation::ExceededManagedDepth, 2, 0, 0},
        {"&&&i32", HandleGrammarViolation::ExceededBorrowDepth, 3, 3, 0},
        {"&&&&i32", HandleGrammarViolation::ExceededBorrowDepth, 4, 4, 0},
        {"&&^i32", HandleGrammarViolation::ExceededManagedDepth, 3, 2, 0},
        {"&&~i32", HandleGrammarViolation::ExceededManagedDepth, 3, 2, 0},
        {"&^^i32", HandleGrammarViolation::ExceededManagedDepth, 3, 1, 0},

        // --- 6. Illegal Mixed Managed / Raw ---
        {"*^i32", HandleGrammarViolation::MixedManagedRaw, 1, 0, 1},
        {"^*i32", HandleGrammarViolation::MixedManagedRaw, 1, 0, 1},
        {"*~i32", HandleGrammarViolation::MixedManagedRaw, 1, 0, 1},
        {"~*i32", HandleGrammarViolation::MixedManagedRaw, 1, 0, 1},
        {"*&i32", HandleGrammarViolation::MixedManagedRaw, 1, 1, 1},
        {"&*i32", HandleGrammarViolation::MixedManagedRaw, 1, 1, 1},
        {"**^i32", HandleGrammarViolation::MixedManagedRaw, 1, 0, 2},
        {"*&^i32", HandleGrammarViolation::MixedManagedRaw, 2, 1, 1},
        {"&*~i32", HandleGrammarViolation::MixedManagedRaw, 2, 1, 1},
        {"***^i32", HandleGrammarViolation::MixedManagedRaw, 1, 0, 3}
    };

    unsigned passed = 0;
    for (const auto &tc : testCases) {
        if (checkTestCase(tc)) {
            passed++;
        }
    }

    // --- 7. Recursive Structural Boundaries ---
    struct RecursiveTestCase {
        std::string typeStr;
        HandleGrammarViolation expectedViolation;
    };

    std::vector<RecursiveTestCase> recursiveCases = {
        {"*Option<&i32>", HandleGrammarViolation::None},
        {"Option<*&i32>", HandleGrammarViolation::MixedManagedRaw},
        {"[*^i32; 4]", HandleGrammarViolation::MixedManagedRaw},
        {"[*&i32]", HandleGrammarViolation::MixedManagedRaw},
        {"fn(*&i32)->i32", HandleGrammarViolation::MixedManagedRaw},
        {"fn(i32)->*&i32", HandleGrammarViolation::MixedManagedRaw},
        {"Uninit<*&i32>", HandleGrammarViolation::MixedManagedRaw},
        {"*&i32|miss", HandleGrammarViolation::MixedManagedRaw},
        {"Option<&&&i32>", HandleGrammarViolation::ExceededBorrowDepth},
        {"Option<^^i32>", HandleGrammarViolation::ExceededManagedDepth},
        {"Option<^&i32>", HandleGrammarViolation::InvalidManagedLayerOrder},
        {"Option<Option<*&i32>>", HandleGrammarViolation::MixedManagedRaw},
        {"fn(Option<*~i32>)->void", HandleGrammarViolation::MixedManagedRaw},
    };

    for (const auto &rc : recursiveCases) {
        auto ty = Type::fromString(rc.typeStr);
        if (!ty) {
            std::cerr << "FAIL: Type::fromString returned null for recursive case '" << rc.typeStr << "'\n";
            continue;
        }
        auto issue = Type::findHandleGrammarIssueRecursive(ty);
        HandleGrammarViolation actual = issue.has_value() ? issue->Violation : HandleGrammarViolation::None;
        if (actual == rc.expectedViolation) {
            passed++;
        } else {
            std::cerr << "FAIL: recursive " << rc.typeStr << " violation mismatch. Expected "
                      << static_cast<int>(rc.expectedViolation) << ", Got "
                      << static_cast<int>(actual) << "\n";
        }
    }

    size_t totalTests = testCases.size() + recursiveCases.size();
    std::cout << "HandleGrammarClassifierTest: " << passed << "/" << totalTests << " test cases passed.\n";
    return (passed == totalTests) ? 0 : 1;
}
