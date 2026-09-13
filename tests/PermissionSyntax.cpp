#include "toka/Parser.h"
#include "toka/Lexer.h"
#include "toka/DiagnosticEngine.h"
#include <iostream>
#include <vector>
bool g_JsonDiagnostics = true;
int main() {
  const std::vector<std::pair<std::string, bool>> cases = {
    {"fn f(value:i32#) {}", false},
    {"fn f(*#out:i32#) {}", false},
    {"shape S(value:i32#)", false},
    {"alias Bad = i32#", false},
    {"alias Bad = Box<i32#>", false},
    {"fn f() { auto x:i32# = 1 }", false},
    {"fn f() { auto x = 1 as i32# }", false},
    {"alias Bad = fn(i32#) -> i32", false},
    {"alias Bad = dyn @Reader<i32#>", false},
    {"fn f(*#out#:i32) {}", true},
    {"alias View = &i32#", true},
    {"alias View = *#i32", true},
    {"alias View = [ &i32#; 2 ]", true},
    {"alias View = Slot<&i32#>", true},
    {"alias F = fn(&i32#) -> i32", true},
    {"alias View = dyn @Reader<&i32#>", true},
    {"alias F = fn#(i32) -> i32", true},
    {"alias F = dyn fn#(i32) -> i32", true},
    {"extern fn f() -> *i32#", true},
  };
  for (const auto &[text, admitted] : cases) {
    toka::DiagnosticEngine::reset();
    toka::Lexer lexer(text.c_str());
    auto tokens = lexer.tokenize();
    toka::Parser parser(tokens, "permission-syntax.tk");
    auto module = parser.parseModule();
    bool error = false, positionError = false;
    for (const auto &record : toka::DiagnosticEngine::records()) {
      error |= record.Level == toka::DiagLevel::Error;
      positionError |= record.Code == "E0496";
    }
    if (error == admitted || (!admitted && !positionError)) {
      std::cerr << "unexpected syntax result: " << text << '\n';
      return 1;
    }
  }
  // Only representation/substitution is tested here, not container eligibility.
  auto parameter = toka::Type::fromString("'T");
  auto view = toka::Type::fromString("&i32#");
  auto instance = parameter->substitute({{"T", view}, {"'T", view}});
  if (!instance || !instance->isReference() || instance->IsWritable ||
      !instance->getPointeeType()->IsWritable ||
      !view->getPointeeType()->IsWritable) return 2;
  auto slot = toka::Type::fromString("Slot<'T>")->substitute({{"T", view}, {"'T", view}});
  auto slotType = std::dynamic_pointer_cast<toka::ShapeType>(slot);
  if (!slotType || slotType->IsWritable || slotType->GenericArgs.size() != 1 ||
      slotType->GenericArgs[0]->toString() != view->toString()) return 3;
  auto readonlySlot = toka::Type::fromString("Slot<&i32>");
  if (slotType->equals(*readonlySlot)) return 5;
  auto readonly = toka::Type::fromString("&i32");
  auto writableSlot = toka::Type::fromString("Slot<'T>")->substitute({{"T", readonly}, {"'T", readonly}})->withAttributes(true, false);
  auto writableSlotType = std::dynamic_pointer_cast<toka::ShapeType>(writableSlot);
  if (!writableSlotType || !writableSlotType->IsWritable ||
      writableSlotType->GenericArgs[0]->getPointeeType()->IsWritable ||
      slotType->equals(*writableSlotType)) return 4;
  std::cout << "19 syntax cases; full reference-view substitution\n";
  return 0;
}
