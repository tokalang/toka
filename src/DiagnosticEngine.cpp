// Copyright (c) 2025 YiZhonghua<zhyi@dpai.com>. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
#include "toka/DiagnosticEngine.h"
#include "toka/InterfaceVersion.h"
#include "toka/PathUtils.h"
#include "toka/SourceManager.h"
#include "toka/AST.h"
#include <algorithm>
#include <iostream>

extern bool g_JsonDiagnostics;

namespace toka {

static std::string escapeJson(const std::string &value) {
  std::string escaped;
  escaped.reserve(value.size());
  for (char ch : value) {
    switch (ch) {
    case '\\': escaped += "\\\\"; break;
    case '"': escaped += "\\\""; break;
    case '\n': escaped += "\\n"; break;
    case '\r': escaped += "\\r"; break;
    case '\t': escaped += "\\t"; break;
    default: escaped += ch; break;
    }
  }
  return escaped;
}

uint32_t ASTNode::NextNodeSerial = 1;
uint32_t ASTNode::CurrentExpansionContext = 0;
const ASTNode *DiagnosticEngine::ActiveNode = nullptr;

ActiveNodeRAII::ActiveNodeRAII(const ASTNode *Node) {
  Prev = DiagnosticEngine::ActiveNode;
  DiagnosticEngine::ActiveNode = Node;
}

ActiveNodeRAII::~ActiveNodeRAII() {
  DiagnosticEngine::ActiveNode = Prev;
}

SourceManager *DiagnosticEngine::SrcMgr = nullptr;
int DiagnosticEngine::ErrorCount = 0;
bool DiagnosticEngine::PrintingEnabled = true;
std::vector<DiagnosticRecord> DiagnosticEngine::Records;
std::optional<size_t> DiagnosticEngine::LastPrimaryRecord;

void DiagnosticEngine::reset() {
  ErrorCount = 0;
  ActiveNode = nullptr;
  Records.clear();
  LastPrimaryRecord.reset();
}

const char *DiagnosticEngine::levelName(DiagLevel level) {
  switch (level) {
  case DiagLevel::Warning: return "warning";
  case DiagLevel::Error: return "error";
  case DiagLevel::Note: return "note";
  case DiagLevel::Structural: return "structural";
  }
  return "error";
}

void DiagnosticEngine::capture(DiagLoc loc, DiagID id, DiagLevel level,
                               const std::string &message) {
  if (!loc.File.empty())
    loc.File = PathUtils::canonicalize(loc.File);
  DiagnosticSpan span{loc.File, loc.Line, loc.Col, loc.Length, ""};
  if (level == DiagLevel::Note && LastPrimaryRecord &&
      *LastPrimaryRecord < Records.size()) {
    span.Label = message;
    Records[*LastPrimaryRecord].Related.push_back(std::move(span));
    return;
  }

  DiagnosticRecord record;
  record.File = loc.File;
  record.Line = loc.Line;
  record.Column = loc.Col;
  record.Length = loc.Length;
  record.Level = level;
  record.Code = getCode(id);
  record.Message = message;

  auto addFix = [&](std::string title, int length, std::string newText) {
    DiagnosticSpan editSpan{loc.File, loc.Line, loc.Col, length, ""};
    record.Fixes.push_back(
        {std::move(title), {{std::move(editSpan), std::move(newText)}}});
  };
  if (id == DiagID::ERR_PARSER_DEPRECATED_KEYWORD_VAR_USE_AUTO_FOR_VAR) {
    addFix("Replace 'var' with 'auto'", 3, "auto");
  } else if (id ==
             DiagID::ERR_PARSER_MINUS_OPERATOR_MUST_BE_SURROUNDED) {
    addFix("Add spaces around '-'", 1, " - ");
  } else if (id == DiagID::ERR_PARSER_TRAIT_REQUIRES_AT_PREFIX) {
    addFix("Add the trait '@' prefix", 0, "@");
  } else if (id == DiagID::ERR_POINTER_SIGIL_MISSING) {
    size_t marker = message.find("Use 'auto ");
    if (marker != std::string::npos && marker + 10 < message.size())
      addFix("Add the inferred pointer sigil", 0,
             std::string(1, message[marker + 10]));
  }

  Records.push_back(std::move(record));
  LastPrimaryRecord = Records.size() - 1;
}

std::optional<DiagnosticExplanation>
DiagnosticEngine::explain(const std::string &code) {
  DiagnosticExplanation result;
#define DIAG(DiagName, DiagSeverity, DiagCode, DiagMessage)                    \
  if (code == DiagCode) {                                                     \
    result.ID = #DiagName;                                                    \
    result.Code = DiagCode;                                                   \
    result.Level = DiagLevel::DiagSeverity;                                   \
    result.MessageTemplate = DiagMessage;                                     \
  }
#include "toka/DiagnosticDefs.def"
#undef DIAG
  if (result.Code.empty())
    return std::nullopt;
  if (code == "E0402")
    result.Guidance = "Declare the identifier in the visible scope or import "
                      "the public symbol that defines it.";
  else if (code == "E0408" || code == "E0409")
    result.Guidance = "Compare the resolved source and target types, then "
                      "change the annotation, conversion, or value.";
  else if (code == "E0438")
    result.Guidance = "Use the value before its ownership transfer, borrow it, "
                      "or clone it when duplication is valid.";
  else if (code == "E01244")
    result.Guidance = "Replace the removed 'var' keyword with 'auto'.";
  else if (code == "E01246")
    result.Guidance = "Write whitespace on both sides of binary '-'.";
  else if (code == "E0469")
    result.Guidance = "Return the reference handle with '&name' instead of "
                      "returning only the referenced value.";
  else if (code.front() == 'W')
    result.Guidance = "The program is accepted; remove the warning cause or "
                      "make the intent explicit.";
  else
    result.Guidance = "Use the primary span and related spans to repair the "
                      "reported language rule, then run 'toka check' again.";
  return result;
}

llvm::json::Value DiagnosticEngine::structuredJSON() {
  auto position = [](int line, int column) {
    return llvm::json::Object{{"line", std::max(0, line - 1)},
                              {"character", std::max(0, column - 1)}};
  };
  auto span = [&](const DiagnosticSpan &value) {
    llvm::json::Object range{
        {"start", position(value.Line, value.Column)},
        {"end", position(value.Line,
                         value.Column + std::max(0, value.Length))}};
    return llvm::json::Object{{"file", value.File},
                              {"range", std::move(range)},
                              {"label", value.Label}};
  };
  auto editRange = [&](const DiagnosticSpan &value) {
    return llvm::json::Object{
        {"start", position(value.Line, value.Column)},
        {"end", position(value.Line,
                         value.Column + std::max(0, value.Length))}};
  };
  llvm::json::Array diagnostics;
  for (const DiagnosticRecord &record : Records) {
    llvm::json::Array related;
    for (const DiagnosticSpan &item : record.Related)
      related.emplace_back(span(item));
    llvm::json::Array fixes;
    for (const DiagnosticFix &fix : record.Fixes) {
      llvm::json::Array edits;
      for (const DiagnosticEdit &edit : fix.Edits)
        edits.emplace_back(llvm::json::Object{{"file", edit.Range.File},
                                              {"range", editRange(edit.Range)},
                                              {"newText", edit.NewText}});
      fixes.emplace_back(llvm::json::Object{
          {"title", fix.Title},
          {"applicability", "machine-applicable"},
          {"edits", std::move(edits)}});
    }
    DiagnosticSpan primary{record.File, record.Line, record.Column,
                           record.Length, ""};
    diagnostics.emplace_back(llvm::json::Object{
        {"code", record.Code},
        {"severity", levelName(record.Level)},
        {"message", record.Message},
        {"primary", span(primary)},
        {"related", std::move(related)},
        {"fixes", std::move(fixes)}});
  }
  return llvm::json::Object{
      {"schema", "toka.diagnostics"},
      {"version", 2},
      {"compiler_version", TOKA_COMPILER_INTERFACE_VERSION},
      {"success", ErrorCount == 0},
      {"diagnostics", std::move(diagnostics)}};
}

const char *DiagnosticEngine::getFormatString(DiagID id) {
  switch (id) {
#define DIAG(ID, Level, Code, Msg)                                             \
  case DiagID::ID:                                                             \
    return Msg;
#include "toka/DiagnosticDefs.def"
#undef DIAG
  case DiagID::NUM_DIAGNOSTICS:
    return "Unknown Error";
  default:
    return "Unknown Error";
  }
}

DiagLevel DiagnosticEngine::getLevel(DiagID id) {
  switch (id) {
#define DIAG(ID, Level, Code, Msg)                                             \
  case DiagID::ID:                                                             \
    return DiagLevel::Level;
#include "toka/DiagnosticDefs.def"
#undef DIAG
  case DiagID::NUM_DIAGNOSTICS:
    return DiagLevel::Error;
  default:
    return DiagLevel::Error;
  }
}

const char *DiagnosticEngine::getCode(DiagID id) {
  switch (id) {
#define DIAG(ID, Level, Code, Msg)                                             \
  case DiagID::ID:                                                             \
    return Code;
#include "toka/DiagnosticDefs.def"
#undef DIAG
  case DiagID::NUM_DIAGNOSTICS:
    return "E0000";
  default:
    return "E0000";
  }
}

void DiagnosticEngine::reportImpl(DiagLoc loc, DiagID id,
                                  const std::string &message) {
  DiagLevel level = getLevel(id);

  if (level == DiagLevel::Error) {
    ErrorCount++;
  }

  capture(loc, id, level, message);

  if (!PrintingEnabled)
    return;

  if (::g_JsonDiagnostics) {
    std::string escapedMsg = escapeJson(message);
    std::cout << "{\"file\": \"" << escapeJson(loc.File) << "\", \"line\": " << loc.Line
              << ", \"col\": " << loc.Col << ", \"message\": \"" << escapedMsg 
              << "\", \"code\": \"" << getCode(id) << "\", \"level\": " << (int)level
              << ", \"ast_anchor\": 0, \"semantic_id\": {\"file_id\": 0, \"node_serial\": 0, \"expansion_context\": 0}"
              << ", \"compiler_version\": \"" << TOKA_COMPILER_INTERFACE_VERSION
              << "\"}\n";
    return;
  }

  const char *color = "";
  const char *reset = "\033[0m";

  if (level == DiagLevel::Error) {
    color = "\033[1;31m"; // Red Bold
    std::cerr << color << "error[" << getCode(id) << "]" << reset << ": "
              << message << "\n";
  } else if (level == DiagLevel::Warning) {
    color = "\033[1;33m"; // Yellow Bold
    std::cerr << color << "warning[" << getCode(id) << "]" << reset << ": "
              << message << "\n";
  } else if (level == DiagLevel::Structural) {
    color = "\033[1;35m"; // Magenta Bold
    std::cerr << color << "structural[" << getCode(id) << "]" << reset << ": "
              << message << "\n";
  } else {
    color = "\033[1;36m"; // Cyan Bold
    std::cerr << color << "note" << reset << ": " << message << "\n";
  }

  std::cerr << " --> " << loc.File << ":" << loc.Line << ":" << loc.Col << "\n";

  if (ErrorCount > 20) {
    std::cerr
        << "\033[1;31mfatal:\033[0m too many errors emitted, stopping now.\n";
#ifndef __EMSCRIPTEN__
    exit(1);
#endif
  }
}

void DiagnosticEngine::reportImpl(SourceLocation loc, DiagID id,
                                  const std::string &message) {
  reportImpl(loc, 1, id, message);
}

void DiagnosticEngine::reportImpl(SourceLocation loc, int length, DiagID id,
                                  const std::string &message) {
  DiagLevel level = getLevel(id);
  if (!PrintingEnabled) {
    FullSourceLoc Full = SrcMgr ? SrcMgr->getFullSourceLoc(loc) : FullSourceLoc();
    reportImpl(DiagLoc{Full.FileName, static_cast<int>(Full.Line),
                       static_cast<int>(Full.Column), length},
               id, message);
    return;
  }
  if (::g_JsonDiagnostics && level == DiagLevel::Error) {
    ErrorCount++;
  }

  if (::g_JsonDiagnostics) {
    std::string escapedMsg = escapeJson(message);
    std::string fileName = "";
    int line = 0;
    int col = 0;
    uint32_t file_id = 0;
    if (SrcMgr) {
      FullSourceLoc Full = SrcMgr->getFullSourceLoc(loc);
      fileName = Full.FileName;
      line = (int)Full.Line;
      col = (int)Full.Column;
      file_id = SrcMgr->getFileID(loc);
    }
    uint32_t node_serial = 0;
    uint32_t expansion_context = 0;
    if (ActiveNode) {
      node_serial = ActiveNode->NodeSerial;
      expansion_context = ActiveNode->ExpansionContext;
    }
    capture(DiagLoc{fileName, line, col, length}, id, level, message);
    std::cout << "{\"file\": \"" << escapeJson(fileName) << "\", \"line\": " << line
              << ", \"col\": " << col << ", \"message\": \"" << escapedMsg 
              << "\", \"code\": \"" << getCode(id) << "\", \"level\": " << (int)level
              << ", \"ast_anchor\": " << loc.getRawEncoding()
              << ", \"semantic_id\": {\"file_id\": " << file_id
              << ", \"node_serial\": " << node_serial
              << ", \"expansion_context\": " << expansion_context << "}"
              << ", \"compiler_version\": \"" << TOKA_COMPILER_INTERFACE_VERSION
              << "\"}\n";
    return;
  }

  if (SrcMgr) {
    FullSourceLoc Full = SrcMgr->getFullSourceLoc(loc);
    DiagLoc DL{Full.FileName, (int)Full.Line, (int)Full.Column, length};
    
    // Print the basic header first
    reportImpl(DL, id, message);

    // Print rich snippet if available
    std::string lineData = SrcMgr->getLineData(loc);
    if (!lineData.empty()) {
      std::string lineNumStr = std::to_string(Full.Line);
      std::string padding(lineNumStr.length() + 2, ' ');
      
      const char *blue = "\033[1;34m";
      const char *reset = "\033[0m";
      const char *caretColor = "\033[1;31m"; // Default Red
      if (getLevel(id) == DiagLevel::Warning) caretColor = "\033[1;33m"; // Yellow
      else if (getLevel(id) == DiagLevel::Structural) caretColor = "\033[1;35m"; // Magenta
      else if (getLevel(id) == DiagLevel::Note) caretColor = "\033[1;36m"; // Cyan
      
      std::cerr << padding << blue << "|" << reset << "\n";
      std::cerr << " " << lineNumStr << " " << blue << "|" << reset << " " << lineData << "\n";
      std::cerr << padding << blue << "|" << reset << " ";
      if (Full.Column > 0) {
        std::cerr << std::string(Full.Column - 1, ' ');
      }
      std::cerr << caretColor << "^";
      if (length > 1) {
        std::cerr << std::string(length - 1, '~');
      }
      std::cerr << reset << "\n";

      // (Suggestions)
      if (id == DiagID::ERR_POINTER_SIGIL_MISSING) {
        std::cerr << padding << blue << "|" << reset << "   \033[1;32m👉 help:\033[0m Variable implies pointer type but lacks explicit sigil. Did you mean '^var'?\n";
      } else if (id == DiagID::ERR_MEMBER_SIGIL_MISMATCH) {
        std::cerr << padding << blue << "|" << reset << "   \033[1;32m👉 help:\033[0m Dot operator '.' cannot be used with pointer. Did you mean '^ptr->member' or 'ptr.member'?\n";
      } else if (id == DiagID::ERR_ARROW_SIGIL_REQUIRED) {
        std::cerr << padding << blue << "|" << reset << "   \033[1;32m👉 help:\033[0m Arrow operator '->' requires explicit pointer sigil on variable. Did you mean '^ptr->member'?\n";
      } else if (id == DiagID::ERR_MISSING_AMPERSAND_RETURN) {
        std::cerr << padding << blue << "|" << reset << "   \033[1;32m👉 help:\033[0m Return requires Handle but Soul is returned. Did you mean '&var'?\n";
      }

      std::cerr << "\n";
    }
    
  } else {
    DiagLoc DL{"<unknown>", 0, 0, length};
    reportImpl(DL, id,
               message + " [RawLoc: " + std::to_string(loc.getRawEncoding()) +
                   "]");
  }
}

} // namespace toka
