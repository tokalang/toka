// Copyright (c) 2026 YiZhonghua<zhyi@dpai.com>. All rights reserved.
const fs = require('fs');
const path = require('path');

if (process.argv.length !== 3) {
    console.error('usage: node browser_compiler_self_test.js <tokacheck.js>');
    process.exit(2);
}

const checkerPath = path.resolve(process.argv[2]);
const wasmPath = checkerPath.endsWith('.js')
    ? checkerPath.slice(0, -3) + '.wasm'
    : checkerPath + '.wasm';
if (!fs.existsSync(checkerPath) || !fs.existsSync(wasmPath)) {
    console.error(`browser compiler artifacts are missing: ${checkerPath}, ${wasmPath}`);
    process.exit(2);
}

const Module = require(checkerPath);

const EXAMPLES = {
    "hello": `import std/io::println

fn main() -> i32 {
    println("Welcome to the Toka Playground!")
    
    auto name = "World"
    println("Hello, {}!", name)
    
    return 0
}`,
    "smart_ptr": `import std/io::println

shape Data(id: i32)

fn create_data(id: i32) -> ~Data {
    auto ~p = new Data(id = id)
    println("Created data with id: {}", p.id)
    return ~p 
}

fn consume_data(~p: Data) {
    println("Consuming data with id: {}", p.id)
}

fn main() -> i32 {
    auto ~p1 = create_data(101)
    consume_data(~p1)
    println("After consume_data, p1 is still valid: id={}", p1.id)
    return 0
}`,
    "shape_match": `import std/io::println

shape MyResult(
    Ok(i32) = 0 | 
    Err(i32) = 1
)

fn process(val: i32) -> i32 {
    auto res = MyResult::Ok(val)
    auto outcome = match res {
        auto MyResult::Ok(v) => {
            pass v * 2
        }
        auto MyResult::Err(e) => {
            pass 0
        }
    }
    return outcome
}

fn main() -> i32 {
    auto b = process(10)
    println("Result: {}", b)
    return 0
}`,
    "async_demo": `import std/task::{sleep, block_on}
import std/io::println

fn worker_a() -> async i32 {
    println(" --> [Worker A] Resumed and running background task!")
    return 0
}

fn worker_b() -> async i32 {
    println(" --> [Worker B] Resumed and running background task!")
    return 0
}

fn event_loop_sentinel() -> async i32 {
    println("--- Queue Start Discharging ---")
    sleep(100) 
    println("--- Queue Exhausted ---")
    return 0
}

pub fn main() -> async i32 {
    println("[Async Main] Spawning background Workers...")
    worker_a().start
    worker_b().start
    block_on<i32>(event_loop_sentinel())
    println("[Async Main] All done!")
    return 0
}`
};

// Add special validation for Comptime / Constant folding to ensure refactored ASTEvaluator is running.
EXAMPLES["const_folding_refactor_test"] = `import std/io::println
fn main() -> i32 {
    auto N = 250 + 6
    println("val: {}", N)
    return 0
}`;

let hasRun = false;
function runSelfTest() {
    if (hasRun) return;
    hasRun = true;
    console.log("=== Starting Browser Compiler WASM Self-Test ===");
    let passed = true;

    for (const [name, code] of Object.entries(EXAMPLES)) {
        // Invoke WASM check_toka_code
        const resultJson = Module.ccall('check_toka_code', 'string', ['string'], [code]);
        try {
            const res = JSON.parse(resultJson);
            
            // Filter out std/sys warnings/errors that are expected on WASM's virtual FS
            const validDiagnostics = (res.diagnostics || []).filter(diag => {
                if (diag.code === "E0901" && diag.message.includes("std/sys/")) return false;
                return true;
            });
            
            const hasRealErrors = validDiagnostics.some(diag => diag.level === 0 && !diag.code.startsWith("W"));
            
            if (res.status === "ok" || !hasRealErrors) {
                console.log(`[PASS] Sample '${name}' successfully verified.`);
            } else {
                console.error(`[FAIL] Sample '${name}' failed verification:`, JSON.stringify(validDiagnostics, null, 2));
                passed = false;
            }
        } catch (e) {
            console.error(`[FAIL] Failed to parse output for '${name}':`, e);
            console.error("Raw result:", resultJson);
            passed = false;
        }
    }

    if (passed) {
        console.log("=== Browser Compiler WASM Self-Test: SUCCESS ===");
        process.exit(0);
    } else {
        console.error("=== Browser Compiler WASM Self-Test: FAILED ===");
        process.exit(1);
    }
}

Module.onRuntimeInitialized = runSelfTest;
if (Module.calledRun) runSelfTest();
