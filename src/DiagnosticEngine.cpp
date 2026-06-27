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
#include "toka/SourceManager.h"
#include "toka/AST.h"
#include <iostream>

extern bool g_JsonDiagnostics;

namespace toka {

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

  if (::g_JsonDiagnostics) {
    std::string escapedMsg = message;
    size_t pos = 0;
    while ((pos = escapedMsg.find('"', pos)) != std::string::npos) {
      escapedMsg.replace(pos, 1, "\\\"");
      pos += 2;
    }
    std::cout << "{\"file\": \"" << loc.File << "\", \"line\": " << loc.Line 
              << ", \"col\": " << loc.Col << ", \"message\": \"" << escapedMsg 
              << "\", \"code\": \"" << getCode(id) << "\", \"level\": " << (int)level
              << ", \"ast_anchor\": 0, \"semantic_id\": {\"file_id\": 0, \"node_serial\": 0, \"expansion_context\": 0}"
              << ", \"compiler_version\": \"" << TOKA_COMPILER_INTERFACE_VERSION
              << "\"}\n";
#ifndef __EMSCRIPTEN__
    if (level == DiagLevel::Error) exit(0); // Exit 0 for LSP so it doesn't think it crashed
#endif
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
  if (::g_JsonDiagnostics && level == DiagLevel::Error) {
    ErrorCount++;
  }

  if (::g_JsonDiagnostics) {
    std::string escapedMsg = message;
    size_t pos = 0;
    while ((pos = escapedMsg.find('"', pos)) != std::string::npos) {
      escapedMsg.replace(pos, 1, "\\\"");
      pos += 2;
    }
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
    std::cout << "{\"file\": \"" << fileName << "\", \"line\": " << line 
              << ", \"col\": " << col << ", \"message\": \"" << escapedMsg 
              << "\", \"code\": \"" << getCode(id) << "\", \"level\": " << (int)level
              << ", \"ast_anchor\": " << loc.getRawEncoding()
              << ", \"semantic_id\": {\"file_id\": " << file_id
              << ", \"node_serial\": " << node_serial
              << ", \"expansion_context\": " << expansion_context << "}"
              << ", \"compiler_version\": \"" << TOKA_COMPILER_INTERFACE_VERSION
              << "\"}\n";
#ifndef __EMSCRIPTEN__
    if (level == DiagLevel::Error) exit(0); // Exit 0 for LSP so it doesn't think it crashed
#endif
    return;
  }

  if (SrcMgr) {
    FullSourceLoc Full = SrcMgr->getFullSourceLoc(loc);
    DiagLoc DL{Full.FileName, (int)Full.Line, (int)Full.Column, length};
    
    // Print the basic header first
    reportImpl(DL, id, message);

    // Print rich snippet if available
    std::string lineData = SrcMgr->getLineData(Full);
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
