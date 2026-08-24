// Copyright (c) 2026 YiZhonghua<zhyi@dpai.com>. All rights reserved.
// Licensed under the Apache License, Version 2.0.

#include "toka/Type.h"
#include "toka/AST.h"
#include "toka/DiagnosticEngine.h"
#include "toka/Lexer.h"
#include "toka/Parser.h"
#include "toka/Sema.h"
#include "toka/SourceManager.h"
#include <iostream>
#include <map>
#include <vector>
#include <string>

bool g_JsonDiagnostics = false;

namespace {

using toka::Type;
using toka::HandleGrammarViolation;

bool testSemaLocalDeductions() {
    std::string source = R"(
shape Item ( id: i32 )

fn test_func() {
    auto ^u_payload = new Item(id = 100)
    auto &payload = &u_payload
    auto ^u_owner = new Item(id = 101)
    auto &^owner = &^u_owner
    auto ~s_payload = new Item(id = 200)
    auto &shared_payload = &s_payload
    auto ~s_owner = new Item(id = 201)
    auto &~shared_owner = &~s_owner
    auto val = 42
    auto &r = &val
    auto &payload_from_ref = &r
    auto &&ref_handle = &&r
}
)";
    toka::SourceManager srcMgr;
    toka::DiagnosticEngine::init(srcMgr);
    auto startLoc = srcMgr.addFile("core/test.tk", source);
    toka::Lexer lexer(source.c_str(), startLoc);
    auto tokens = lexer.tokenize();
    toka::Parser parser(tokens, "core/test.tk");
    auto module = parser.parseModule();
    if (!module || toka::DiagnosticEngine::hasErrors()) {
        std::cerr << "FAIL: parser failed on testSemaLocalDeductions\n";
        return false;
    }
    toka::Sema sema;
    sema.declareGlobals(*module);
    if (!sema.checkModule(*module) || toka::DiagnosticEngine::hasErrors()) {
        std::cerr << "FAIL: sema failed on testSemaLocalDeductions\n";
        return false;
    }

    toka::FunctionDecl *fn = nullptr;
    for (const auto &f : module->Functions) {
        if (f->Name == "test_func") {
            fn = f.get();
            break;
        }
    }
    if (!fn || !fn->Body) {
        std::cerr << "FAIL: test_func not found\n";
        return false;
    }

    std::map<std::string, std::string> varTypes;
    for (const auto &stmt : fn->Body->Statements) {
        if (auto var = dynamic_cast<toka::VariableDecl *>(stmt.get())) {
            if (var->ResolvedType) {
                varTypes[toka::Type::stripMorphology(var->Name)] = var->ResolvedType->toString();
            }
        }
    }

    if (varTypes["payload"] != "&Item") {
        std::cerr << "FAIL: expected payload borrow to be '&Item', got '" << varTypes["payload"] << "'\n";
        return false;
    }
    if (varTypes["owner"] != "&^Item") {
        std::cerr << "FAIL: expected unique-handle borrow to be '&^Item', got '" << varTypes["owner"] << "'\n";
        return false;
    }
    if (varTypes["shared_payload"] != "&Item") {
        std::cerr << "FAIL: expected shared payload borrow to be '&Item', got '" << varTypes["shared_payload"] << "'\n";
        return false;
    }
    if (varTypes["shared_owner"] != "&~Item") {
        std::cerr << "FAIL: expected shared-handle borrow to be '&~Item', got '" << varTypes["shared_owner"] << "'\n";
        return false;
    }
    if (varTypes["payload_from_ref"] != "&i32") {
        std::cerr << "FAIL: expected reference payload borrow to remain '&i32', got '" << varTypes["payload_from_ref"] << "'\n";
        return false;
    }
    if (varTypes["ref_handle"] != "&&i32") {
        std::cerr << "FAIL: expected explicit reference-handle borrow to be '&&i32', got '" << varTypes["ref_handle"] << "'\n";
        return false;
    }
    return true;
}

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

    // --- 7. Recursive Structural Boundaries with Typed Paths and Layer Indices ---
    struct RecursiveTestCase {
        std::string typeStr;
        HandleGrammarViolation expectedViolation;
        std::string expectedPath;
        unsigned expectedOuterLayer;
        unsigned expectedInnerLayer;
    };

    std::vector<RecursiveTestCase> recursiveCases = {
        {"*Option<&i32>", HandleGrammarViolation::None, "", 0, 0},
        {"Option<&^i32>", HandleGrammarViolation::None, "", 0, 0},
        {"Option<&~i32>", HandleGrammarViolation::None, "", 0, 0},
        {"Option<&&i32>", HandleGrammarViolation::None, "", 0, 0},
        {"fn(i32)->&^i32", HandleGrammarViolation::None, "", 0, 0},
        {"fn(&^i32)->void", HandleGrammarViolation::ParamHandleDepthForbidden, "FunctionParam(0)", 0, 1},
        {"fn(&~i32)->void", HandleGrammarViolation::ParamHandleDepthForbidden, "FunctionParam(0)", 0, 1},
        {"fn(&&i32)->void", HandleGrammarViolation::ParamHandleDepthForbidden, "FunctionParam(0)", 0, 1},
        {"fn(**i32)->void", HandleGrammarViolation::ParamHandleDepthForbidden, "FunctionParam(0)", 0, 1},
        {"Option<*&i32>", HandleGrammarViolation::MixedManagedRaw, "GenericArg(0)", 0, 1},
        {"[*^i32; 4]", HandleGrammarViolation::MixedManagedRaw, "ArrayElement", 0, 1},
        {"[*&i32]", HandleGrammarViolation::MixedManagedRaw, "SliceElement", 0, 1},
        {"fn(*&i32)->i32", HandleGrammarViolation::MixedManagedRaw, "FunctionParam(0)", 0, 1},
        {"fn(i32)->*&i32", HandleGrammarViolation::MixedManagedRaw, "FunctionReturn", 0, 1},
        {"Uninit<*&i32>", HandleGrammarViolation::MixedManagedRaw, "UninitInner", 0, 1},
        {"*&i32|miss", HandleGrammarViolation::MixedManagedRaw, "OutcomePayload", 0, 1},
        {"Option<&&&i32>", HandleGrammarViolation::ExceededBorrowDepth, "GenericArg(0)", 0, 2},
        {"Option<^^i32>", HandleGrammarViolation::ExceededManagedDepth, "GenericArg(0)", 0, 1},
        {"Option<^&i32>", HandleGrammarViolation::InvalidManagedLayerOrder, "GenericArg(0)", 0, 1},
        {"Option<Option<*&i32>>", HandleGrammarViolation::MixedManagedRaw, "GenericArg(0) -> GenericArg(0)", 0, 1},
        {"fn(Option<*~i32>)->void", HandleGrammarViolation::MixedManagedRaw, "FunctionParam(0) -> GenericArg(0)", 0, 1},
    };

    for (const auto &rc : recursiveCases) {
        auto ty = Type::fromString(rc.typeStr);
        if (!ty) {
            std::cerr << "FAIL: Type::fromString returned null for recursive case '" << rc.typeStr << "'\n";
            continue;
        }
        auto issue = Type::findHandleGrammarIssueRecursive(ty);
        HandleGrammarViolation actual = issue.has_value() ? issue->Violation : HandleGrammarViolation::None;
        if (actual != rc.expectedViolation) {
            std::cerr << "FAIL: recursive " << rc.typeStr << " violation mismatch. Expected "
                      << static_cast<int>(rc.expectedViolation) << ", Got "
                      << static_cast<int>(actual) << "\n";
            continue;
        }
        if (actual != HandleGrammarViolation::None) {
            std::string pathStr = issue->formatTypePath();
            if (pathStr != rc.expectedPath) {
                std::cerr << "FAIL: recursive " << rc.typeStr << " path mismatch. Expected '"
                          << rc.expectedPath << "', Got '" << pathStr << "'\n";
                continue;
            }
            if (issue->OuterLayer != rc.expectedOuterLayer || issue->InnerLayer != rc.expectedInnerLayer) {
                std::cerr << "FAIL: recursive " << rc.typeStr << " layer index mismatch. Expected ("
                          << rc.expectedOuterLayer << ", " << rc.expectedInnerLayer << "), Got ("
                          << issue->OuterLayer << ", " << issue->InnerLayer << ")\n";
                continue;
            }
        }
        passed++;
    }

    if (testSemaLocalDeductions()) {
        passed++;
    }

    size_t totalTests = testCases.size() + recursiveCases.size() + 1;
    std::cout << "HandleGrammarClassifierTest: " << passed << "/" << totalTests << " test cases passed.\n";
    return (passed == totalTests) ? 0 : 1;
}
