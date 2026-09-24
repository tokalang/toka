#include "toka/Sema.h"
#include "toka/Parser.h"
#include "toka/Lexer.h"
#include "toka/SourceManager.h"
#include "toka/DiagnosticEngine.h"
#include <iostream>

bool g_JsonDiagnostics = false;
namespace toka {
struct ResultIndependenceTestAccess {
  static bool rollbackAndJoin(Sema &sema) {
    Scope local;
    auto *saved = sema.CurrentScope;
    auto savedFacts = std::move(sema.m_IndependentValues);
    sema.CurrentScope = &local;
    SymbolInfo info;
    info.TypeObj = Type::fromString("^Box");
    local.define("value", info);
    const auto id = local.Symbols.at("value").SymbolID;
    auto proof = std::make_shared<ResultIndependenceFact>();
    proof->ValueType = info.TypeObj;
    sema.m_IndependentValues[id] = proof;
    auto before = sema.captureAnalysisState();
    VariableExpr value("value");
    sema.invalidateReturnSourceProof(&value);
    const bool erased = !sema.m_IndependentValues.count(id);
    auto changed = sema.captureAnalysisState();
    sema.mergeAnalysisStates({before}, before.PAL);
    const bool restored = sema.m_IndependentValues.count(id);
    sema.mergeAnalysisStates({before, changed}, before.PAL);
    const bool conservativeJoin = !sema.m_IndependentValues.count(id);
    sema.CurrentScope = saved;
    sema.m_IndependentValues = std::move(savedFacts);
    return erased && restored && conservativeJoin;
  }
  static bool has(Sema &sema, FunctionDecl *fn, size_t conditions) {
    auto found = sema.m_IndependentReturns.find(fn);
    return found != sema.m_IndependentReturns.end() && found->second->Scope == fn &&
           found->second->RequiredArguments.size() == conditions;
  }
};
}

int main() {
  using namespace toka;
  const std::string code = R"(
shape Box(n#:i32)
fn make()->^Box {auto ^result=new Box(n=7)
 return ^result}
fn relay(cede ^value:Box)->^Box {return ^value}
fn changed()->^Box {auto ^result=new Box(n=7)
 result.n=9
 return ^result}
fn replaced()->^Box {auto ^#result=new Box(n=7)
 ^result=new Box(n=9)
 return ^result}
fn branches(flag:bool)->^Box {
 if flag {auto ^a=new Box(n=1)
 return ^a}
 auto ^b=new Box(n=2)
 return ^b
}
fn forwarded()->^Box {auto ^value=make()
 return ^value}
fn alias_changed()->^Box {auto ^result=new Box(n=1)
 {auto &view#=&result
 view.n=9}
 return ^result}
fn loop_changed(flag:bool)->^Box {auto ^result=new Box(n=1)
 loop flag {result.n=9
 break}
 return ^result}
fn unknown_branch(flag:bool)->^Box {
 auto ^result=new Box(n=1)
 if flag {result.n=3}
 return ^result
}
fn main()->i32 {return 0}
)";
  SourceManager sources;
  auto start = sources.addFile("/tmp/tests/independent_results.tk", code);
  DiagnosticEngine::reset();
  DiagnosticEngine::init(sources);
  Lexer lexer(code.c_str(), start);
  // Parser retains a reference to these tokens until parseModule returns.
  auto tokens = lexer.tokenize();
  Parser parser(tokens, "/tmp/tests/independent_results.tk");
  auto module = parser.parseModule();
  module->Imports.clear(); // This isolated Sema unit uses only local declarations/primitives.
  Sema sema;
  sema.setStage1ExplicitCallerCedeEnabled(true);
  sema.setSignatureDrivenCallCedeEnabled(true);
  sema.declareGlobals(*module);
  if (!sema.checkModule(*module)) return 1;
  for (const auto &fn : module->Functions) {
    if (fn->Name == "main") continue;
    const bool expected = fn->Name == "make" || fn->Name == "relay" ||
                          fn->Name == "branches" || fn->Name == "forwarded";
    const size_t conditions = fn->Name == "relay" ? 1 : 0;
    if (ResultIndependenceTestAccess::has(sema, fn.get(), conditions) != expected) {
      std::cerr << "incorrect independence summary: " << fn->Name << '\n';
      return 2;
    }
  }
  if (!ResultIndependenceTestAccess::rollbackAndJoin(sema)) return 3;
  std::cout << "checked result facts: constructor, conditional relay, all returns, mutation/rebind denial\n";
  return 0;
}
