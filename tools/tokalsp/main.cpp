// Copyright (c) 2025 YiZhonghua<zhyi@dpai.com>. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "toka/Version.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Program.h"
#include "llvm/Support/raw_ostream.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

namespace {

struct Position {
  int64_t Line = 0;
  int64_t Character = 0;
};

struct Token {
  enum class Kind { Identifier, Punctuation } TokenKind;
  std::string Text;
  Position Start;
  Position End;
  size_t ByteOffset = 0;
  bool IsDeclaration = false;
  std::string DeclarationKind;
};

struct Document {
  std::string Uri;
  std::string Text;
  std::vector<Token> Tokens;
};

static bool positionBeforeOrEqual(Position lhs, Position rhs) {
  return lhs.Line < rhs.Line ||
         (lhs.Line == rhs.Line && lhs.Character <= rhs.Character);
}

static bool contains(const Token &token, Position position) {
  return positionBeforeOrEqual(token.Start, position) &&
         positionBeforeOrEqual(position, token.End);
}

static bool isIdentifierStart(unsigned char c) {
  return std::isalpha(c) || c == '_' || c >= 0x80;
}

static bool isIdentifierContinue(unsigned char c) {
  return std::isalnum(c) || c == '_' || c >= 0x80;
}

static size_t utf8Length(unsigned char c) {
  if ((c & 0x80) == 0)
    return 1;
  if ((c & 0xE0) == 0xC0)
    return 2;
  if ((c & 0xF0) == 0xE0)
    return 3;
  if ((c & 0xF8) == 0xF0)
    return 4;
  return 1;
}

static int64_t utf16Width(std::string_view text, size_t offset) {
  return utf8Length(static_cast<unsigned char>(text[offset])) == 4 ? 2 : 1;
}

static void advance(std::string_view text, size_t &offset, Position &position) {
  if (text[offset] == '\n') {
    ++offset;
    ++position.Line;
    position.Character = 0;
    return;
  }
  size_t length = std::min(utf8Length(static_cast<unsigned char>(text[offset])),
                           text.size() - offset);
  position.Character += utf16Width(text, offset);
  offset += length;
}

static std::vector<Token> tokenize(std::string_view text) {
  std::vector<Token> tokens;
  size_t offset = 0;
  Position position;
  while (offset < text.size()) {
    unsigned char c = static_cast<unsigned char>(text[offset]);
    if (std::isspace(c)) {
      advance(text, offset, position);
      continue;
    }
    if (c == '/' && offset + 1 < text.size() && text[offset + 1] == '/') {
      while (offset < text.size() && text[offset] != '\n')
        advance(text, offset, position);
      continue;
    }
    if (c == '/' && offset + 1 < text.size() && text[offset + 1] == '*') {
      advance(text, offset, position);
      advance(text, offset, position);
      while (offset < text.size()) {
        if (text[offset] == '*' && offset + 1 < text.size() &&
            text[offset + 1] == '/') {
          advance(text, offset, position);
          advance(text, offset, position);
          break;
        }
        advance(text, offset, position);
      }
      continue;
    }
    if (c == '"' || c == '\'') {
      unsigned char quote = c;
      advance(text, offset, position);
      while (offset < text.size()) {
        if (text[offset] == '\\' && offset + 1 < text.size()) {
          advance(text, offset, position);
          advance(text, offset, position);
          continue;
        }
        unsigned char current = static_cast<unsigned char>(text[offset]);
        advance(text, offset, position);
        if (current == quote)
          break;
      }
      continue;
    }
    if (isIdentifierStart(c)) {
      size_t startOffset = offset;
      Position start = position;
      while (offset < text.size() &&
             isIdentifierContinue(static_cast<unsigned char>(text[offset])))
        advance(text, offset, position);
      tokens.push_back(
          {Token::Kind::Identifier,
           std::string(text.substr(startOffset, offset - startOffset)), start,
           position, startOffset});
      continue;
    }
    Position start = position;
    size_t startOffset = offset;
    advance(text, offset, position);
    tokens.push_back(
        {Token::Kind::Punctuation,
         std::string(text.substr(startOffset, offset - startOffset)), start,
         position, startOffset});
  }

  const std::set<std::string> declarationKeywords = {"fn", "shape", "trait",
                                                     "alias", "auto"};
  for (size_t i = 0; i < tokens.size(); ++i) {
    if (tokens[i].TokenKind != Token::Kind::Identifier ||
        !declarationKeywords.count(tokens[i].Text))
      continue;
    for (size_t j = i + 1; j < tokens.size(); ++j) {
      if (tokens[j].TokenKind == Token::Kind::Identifier) {
        tokens[j].IsDeclaration = true;
        tokens[j].DeclarationKind =
            tokens[i].Text == "auto" ? "variable" : tokens[i].Text;
        break;
      }
      if (tokens[j].Text == ";" || tokens[j].Text == "{" ||
          tokens[j].Text == "}")
        break;
    }
  }

  for (size_t i = 0; i < tokens.size(); ++i) {
    if (tokens[i].TokenKind != Token::Kind::Identifier ||
        tokens[i].Text != "fn")
      continue;
    size_t open = i + 1;
    while (open < tokens.size() && tokens[open].Text != "(")
      ++open;
    if (open == tokens.size())
      continue;
    int depth = 1;
    for (size_t j = open + 1; j < tokens.size() && depth > 0; ++j) {
      if (tokens[j].Text == "(") {
        ++depth;
      } else if (tokens[j].Text == ")") {
        --depth;
      } else if (depth == 1 && tokens[j].TokenKind == Token::Kind::Identifier &&
                 j + 1 < tokens.size() && tokens[j + 1].Text == ":") {
        tokens[j].IsDeclaration = true;
        tokens[j].DeclarationKind = "parameter";
      }
    }
  }
  return tokens;
}

static llvm::json::Object jsonPosition(Position position) {
  return llvm::json::Object{{"line", position.Line},
                            {"character", position.Character}};
}

static llvm::json::Object jsonRange(const Token &token) {
  return llvm::json::Object{{"start", jsonPosition(token.Start)},
                            {"end", jsonPosition(token.End)}};
}

static llvm::json::Object jsonLocation(const Document &document,
                                       const Token &token) {
  return llvm::json::Object{{"uri", document.Uri}, {"range", jsonRange(token)}};
}

static std::string serialize(const llvm::json::Value &value) {
  std::string result;
  llvm::raw_string_ostream stream(result);
  stream << value;
  return result;
}

static std::string trim(std::string text) {
  auto isSpace = [](unsigned char c) { return std::isspace(c); };
  text.erase(text.begin(), std::find_if(text.begin(), text.end(),
                                        [&](char c) { return !isSpace(c); }));
  text.erase(std::find_if(text.rbegin(), text.rend(),
                          [&](char c) { return !isSpace(c); })
                 .base(),
             text.end());
  return text;
}

static std::string sourceLine(const Document &document, int64_t line) {
  size_t start = 0;
  for (int64_t current = 0; current < line; ++current) {
    start = document.Text.find('\n', start);
    if (start == std::string::npos)
      return {};
    ++start;
  }
  size_t end = document.Text.find('\n', start);
  return trim(document.Text.substr(start, end - start));
}

static std::optional<std::string> getUri(const llvm::json::Object &params) {
  const auto *textDocument = params.getObject("textDocument");
  if (!textDocument)
    return std::nullopt;
  auto uri = textDocument->getString("uri");
  if (!uri)
    return std::nullopt;
  return uri->str();
}

static std::optional<Position> getPosition(const llvm::json::Object &params) {
  const auto *position = params.getObject("position");
  if (!position)
    return std::nullopt;
  auto line = position->getInteger("line");
  auto character = position->getInteger("character");
  if (!line || !character || *line < 0 || *character < 0)
    return std::nullopt;
  return Position{*line, *character};
}

static std::string percentDecode(std::string_view value) {
  std::string decoded;
  auto hex = [](char c) -> int {
    if (c >= '0' && c <= '9')
      return c - '0';
    if (c >= 'a' && c <= 'f')
      return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
      return c - 'A' + 10;
    return -1;
  };
  for (size_t i = 0; i < value.size(); ++i) {
    if (value[i] == '%' && i + 2 < value.size()) {
      int high = hex(value[i + 1]);
      int low = hex(value[i + 2]);
      if (high >= 0 && low >= 0) {
        decoded.push_back(static_cast<char>((high << 4) | low));
        i += 2;
        continue;
      }
    }
    decoded.push_back(value[i]);
  }
  return decoded;
}

static std::string uriToPath(std::string_view uri) {
  constexpr std::string_view prefix = "file://";
  if (uri.substr(0, prefix.size()) != prefix)
    return {};
  std::string path = percentDecode(uri.substr(prefix.size()));
#ifdef _WIN32
  if (path.size() >= 3 && path[0] == '/' && std::isalpha(path[1]) &&
      path[2] == ':')
    path.erase(path.begin());
#endif
  return path;
}

class LanguageServer {
public:
  explicit LanguageServer(const char *argv0) { Compiler = findCompiler(argv0); }

  int run() {
#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    while (true) {
      std::optional<std::string> body = readMessage();
      if (!body)
        return ShutdownRequested ? 0 : 1;
      if (process(*body))
        return ShutdownRequested ? 0 : 1;
    }
  }

private:
  std::map<std::string, Document> Documents;
  std::string Compiler;
  bool ShutdownRequested = false;

  static std::string findCompiler(const char *argv0) {
    llvm::SmallString<256> sibling(argv0 ? argv0 : "");
    llvm::sys::path::remove_filename(sibling);
#ifdef _WIN32
    llvm::sys::path::append(sibling, "tokac.exe");
#else
    llvm::sys::path::append(sibling, "tokac");
#endif
    if (!sibling.empty() && llvm::sys::fs::exists(sibling))
      return sibling.str().str();
    auto found = llvm::sys::findProgramByName("tokac");
    return found ? *found : std::string();
  }

  static std::optional<std::string> readMessage() {
    std::string line;
    size_t contentLength = 0;
    bool sawHeader = false;
    while (std::getline(std::cin, line)) {
      if (!line.empty() && line.back() == '\r')
        line.pop_back();
      if (line.empty())
        break;
      sawHeader = true;
      std::string lower = line;
      std::transform(lower.begin(), lower.end(), lower.begin(),
                     [](unsigned char c) { return std::tolower(c); });
      constexpr std::string_view header = "content-length:";
      if (lower.substr(0, header.size()) == header) {
        std::string value = trim(line.substr(header.size()));
        try {
          contentLength = static_cast<size_t>(std::stoull(value));
        } catch (...) {
          return std::nullopt;
        }
      }
    }
    if (!sawHeader || contentLength == 0)
      return std::nullopt;
    std::string body(contentLength, '\0');
    std::cin.read(body.data(), static_cast<std::streamsize>(contentLength));
    if (static_cast<size_t>(std::cin.gcount()) != contentLength)
      return std::nullopt;
    return body;
  }

  static void sendBody(const std::string &body) {
    std::cout << "Content-Length: " << body.size() << "\r\n\r\n" << body;
    std::cout.flush();
  }

  static void reply(const llvm::json::Value *id, llvm::json::Value result) {
    std::string idJson = id ? serialize(*id) : "null";
    sendBody("{\"jsonrpc\":\"2.0\",\"id\":" + idJson +
             ",\"result\":" + serialize(result) + "}");
  }

  static void replyError(const llvm::json::Value *id, int code,
                         std::string message) {
    std::string idJson = id ? serialize(*id) : "null";
    llvm::json::Object error{{"code", code}, {"message", std::move(message)}};
    sendBody("{\"jsonrpc\":\"2.0\",\"id\":" + idJson + ",\"error\":" +
             serialize(llvm::json::Value(std::move(error))) + "}");
  }

  static void notify(std::string method, llvm::json::Value params) {
    llvm::json::Object message{{"jsonrpc", "2.0"},
                               {"method", std::move(method)},
                               {"params", std::move(params)}};
    sendBody(serialize(llvm::json::Value(std::move(message))));
  }

  bool process(const std::string &body) {
    llvm::Expected<llvm::json::Value> parsed = llvm::json::parse(body);
    if (!parsed) {
      llvm::consumeError(parsed.takeError());
      replyError(nullptr, -32700, "Parse error");
      return false;
    }
    auto *message = parsed->getAsObject();
    if (!message) {
      replyError(nullptr, -32600, "Invalid Request");
      return false;
    }
    auto method = message->getString("method");
    if (!method) {
      replyError(message->get("id"), -32600, "Invalid Request");
      return false;
    }
    const llvm::json::Value *id = message->get("id");
    const llvm::json::Object *params = message->getObject("params");

    if (*method == "exit")
      return true;
    if (ShutdownRequested) {
      if (id)
        replyError(id, -32600, "Server has shut down");
      return false;
    }
    if (*method == "initialize") {
      llvm::json::Object sync{{"openClose", true}, {"change", 1}};
      llvm::json::Array triggers{llvm::json::Value("."), llvm::json::Value(":"),
                                 llvm::json::Value("@")};
      llvm::json::Object completion{{"triggerCharacters", std::move(triggers)},
                                    {"resolveProvider", false}};
      llvm::json::Object rename{{"prepareProvider", false}};
      llvm::json::Object capabilities{
          {"positionEncoding", "utf-16"},
          {"textDocumentSync", std::move(sync)},
          {"hoverProvider", true},
          {"definitionProvider", true},
          {"referencesProvider", true},
          {"completionProvider", std::move(completion)},
          {"renameProvider", std::move(rename)}};
      llvm::json::Object serverInfo{{"name", "tokalsp"},
                                    {"version", TOKA_VERSION_STRING}};
      reply(id, llvm::json::Object{{"capabilities", std::move(capabilities)},
                                   {"serverInfo", std::move(serverInfo)}});
      return false;
    }
    if (*method == "shutdown") {
      ShutdownRequested = true;
      reply(id, nullptr);
      return false;
    }
    if (*method == "initialized" || *method == "$/cancelRequest")
      return false;
    if (!params) {
      if (id)
        replyError(id, -32602, "Invalid params");
      return false;
    }
    if (*method == "textDocument/didOpen") {
      didOpen(*params);
      return false;
    }
    if (*method == "textDocument/didChange") {
      didChange(*params);
      return false;
    }
    if (*method == "textDocument/didClose") {
      didClose(*params);
      return false;
    }
    if (*method == "textDocument/hover") {
      handleHover(id, *params);
      return false;
    }
    if (*method == "textDocument/definition") {
      handleDefinition(id, *params);
      return false;
    }
    if (*method == "textDocument/references") {
      handleReferences(id, *params);
      return false;
    }
    if (*method == "textDocument/completion") {
      handleCompletion(id, *params);
      return false;
    }
    if (*method == "textDocument/rename") {
      handleRename(id, *params);
      return false;
    }
    if (id)
      replyError(id, -32601, "Method not found");
    return false;
  }

  void didOpen(const llvm::json::Object &params) {
    const auto *textDocument = params.getObject("textDocument");
    if (!textDocument)
      return;
    auto uri = textDocument->getString("uri");
    auto text = textDocument->getString("text");
    if (!uri || !text)
      return;
    updateDocument(uri->str(), text->str());
  }

  void didChange(const llvm::json::Object &params) {
    auto uri = getUri(params);
    const auto *changes = params.getArray("contentChanges");
    if (!uri || !changes || changes->empty())
      return;
    const auto *last = changes->back().getAsObject();
    if (!last)
      return;
    auto text = last->getString("text");
    if (text)
      updateDocument(*uri, text->str());
  }

  void didClose(const llvm::json::Object &params) {
    auto uri = getUri(params);
    if (!uri)
      return;
    Documents.erase(*uri);
    notify("textDocument/publishDiagnostics",
           llvm::json::Object{{"uri", *uri},
                              {"diagnostics", llvm::json::Array()}});
  }

  void updateDocument(std::string uri, std::string text) {
    Document document{uri, std::move(text), {}};
    document.Tokens = tokenize(document.Text);
    Documents[uri] = std::move(document);
    publishDiagnostics(Documents.at(uri));
  }

  const Token *tokenAt(const Document &document, Position position) const {
    for (const Token &token : document.Tokens) {
      if (token.TokenKind == Token::Kind::Identifier &&
          contains(token, position))
        return &token;
    }
    return nullptr;
  }

  std::pair<const Document *, const Token *>
  definitionFor(const Document &source, const Token &symbol) const {
    const Token *best = nullptr;
    for (const Token &token : source.Tokens) {
      if (!token.IsDeclaration || token.Text != symbol.Text)
        continue;
      if (!best || (token.ByteOffset <= symbol.ByteOffset &&
                    token.ByteOffset > best->ByteOffset))
        best = &token;
    }
    if (best)
      return {&source, best};
    for (const auto &[uri, document] : Documents) {
      for (const Token &token : document.Tokens) {
        if (token.IsDeclaration && token.Text == symbol.Text)
          return {&document, &token};
      }
    }
    return {nullptr, nullptr};
  }

  std::pair<const Document *, const Token *>
  requestToken(const llvm::json::Object &params) const {
    auto uri = getUri(params);
    auto position = getPosition(params);
    if (!uri || !position)
      return {nullptr, nullptr};
    auto document = Documents.find(*uri);
    if (document == Documents.end())
      return {nullptr, nullptr};
    return {&document->second, tokenAt(document->second, *position)};
  }

  void handleHover(const llvm::json::Value *id,
                   const llvm::json::Object &params) const {
    auto [document, symbol] = requestToken(params);
    if (!document || !symbol) {
      reply(id, nullptr);
      return;
    }
    auto [definitionDocument, definition] = definitionFor(*document, *symbol);
    std::string detail;
    if (definitionDocument && definition) {
      detail = sourceLine(*definitionDocument, definition->Start.Line);
    } else {
      detail = symbol->Text;
    }
    llvm::json::Object contents{{"kind", "markdown"},
                                {"value", "```toka\n" + detail + "\n```"}};
    reply(id, llvm::json::Object{{"contents", std::move(contents)},
                                 {"range", jsonRange(*symbol)}});
  }

  void handleDefinition(const llvm::json::Value *id,
                        const llvm::json::Object &params) const {
    auto [document, symbol] = requestToken(params);
    if (!document || !symbol) {
      reply(id, nullptr);
      return;
    }
    auto [definitionDocument, definition] = definitionFor(*document, *symbol);
    if (!definitionDocument || !definition) {
      reply(id, nullptr);
      return;
    }
    reply(id, jsonLocation(*definitionDocument, *definition));
  }

  void handleReferences(const llvm::json::Value *id,
                        const llvm::json::Object &params) const {
    auto [document, symbol] = requestToken(params);
    if (!document || !symbol) {
      reply(id, llvm::json::Array());
      return;
    }
    bool includeDeclaration = true;
    if (const auto *context = params.getObject("context"))
      includeDeclaration =
          context->getBoolean("includeDeclaration").value_or(true);
    llvm::json::Array locations;
    for (const auto &[uri, candidate] : Documents) {
      for (const Token &token : candidate.Tokens) {
        if (token.TokenKind == Token::Kind::Identifier &&
            token.Text == symbol->Text &&
            (includeDeclaration || !token.IsDeclaration))
          locations.emplace_back(jsonLocation(candidate, token));
      }
    }
    reply(id, std::move(locations));
  }

  void handleCompletion(const llvm::json::Value *id,
                        const llvm::json::Object &) const {
    static const std::vector<std::string> keywords = {
        "fn",     "shape",    "trait",  "impl",  "auto",  "return",
        "if",     "else",     "match",  "for",   "in",    "loop",
        "break",  "continue", "import", "pub",   "alias", "extern",
        "unsafe", "async",    "await",  "start", "wait",  "cede",
        "pass",   "true",     "false",  "none",  "null",  "comptime"};
    llvm::json::Array items;
    for (const std::string &keyword : keywords)
      items.emplace_back(llvm::json::Object{
          {"label", keyword}, {"kind", 14}, {"detail", "Toka keyword"}});
    std::set<std::string> seen(keywords.begin(), keywords.end());
    for (const auto &[uri, document] : Documents) {
      for (const Token &token : document.Tokens) {
        if (token.TokenKind != Token::Kind::Identifier ||
            !seen.insert(token.Text).second)
          continue;
        int kind = token.DeclarationKind == "fn"      ? 3
                   : token.DeclarationKind == "shape" ? 7
                                                      : 6;
        items.emplace_back(llvm::json::Object{
            {"label", token.Text}, {"kind", kind}, {"detail", "Toka symbol"}});
      }
    }
    reply(id, llvm::json::Object{{"isIncomplete", false},
                                 {"items", std::move(items)}});
  }

  static bool validIdentifier(llvm::StringRef name) {
    if (name.empty() || !isIdentifierStart(static_cast<unsigned char>(name[0])))
      return false;
    return std::all_of(name.begin() + 1, name.end(),
                       [](unsigned char c) { return isIdentifierContinue(c); });
  }

  void handleRename(const llvm::json::Value *id,
                    const llvm::json::Object &params) const {
    auto newName = params.getString("newName");
    auto [document, symbol] = requestToken(params);
    if (!newName || !validIdentifier(*newName)) {
      replyError(id, -32602, "newName must be a valid Toka identifier");
      return;
    }
    if (!document || !symbol) {
      reply(id, nullptr);
      return;
    }
    llvm::json::Object changes;
    for (const auto &[uri, candidate] : Documents) {
      llvm::json::Array edits;
      for (const Token &token : candidate.Tokens) {
        if (token.TokenKind == Token::Kind::Identifier &&
            token.Text == symbol->Text)
          edits.emplace_back(llvm::json::Object{{"range", jsonRange(token)},
                                                {"newText", *newName}});
      }
      if (!edits.empty())
        changes[uri] = std::move(edits);
    }
    reply(id, llvm::json::Object{{"changes", std::move(changes)}});
  }

  void publishDiagnostics(const Document &document) const {
    llvm::json::Array diagnostics;
    if (!Compiler.empty()) {
      llvm::SmallString<128> sourcePath;
      llvm::SmallString<128> stdoutPath;
      llvm::SmallString<128> stderrPath;
      llvm::SmallString<128> irPath;
      if (!llvm::sys::fs::createTemporaryFile("tokalsp", "tk", sourcePath) &&
          !llvm::sys::fs::createTemporaryFile("tokalsp-out", "jsonl",
                                              stdoutPath) &&
          !llvm::sys::fs::createTemporaryFile("tokalsp-err", "log",
                                              stderrPath) &&
          !llvm::sys::fs::createTemporaryFile("tokalsp-ir", "ll", irPath)) {
        std::error_code error;
        llvm::raw_fd_ostream source(sourcePath, error);
        if (!error) {
          source << document.Text;
          source.close();
          std::string documentPath = uriToPath(document.Uri);
          llvm::SmallString<128> importPath(documentPath);
          llvm::sys::path::remove_filename(importPath);
          std::vector<std::string> ownedArgs = {Compiler, "--check-json",
                                                "--emit-llvm", "-o",
                                                irPath.str().str()};
          if (!importPath.empty()) {
            ownedArgs.push_back("-I");
            ownedArgs.push_back(importPath.str().str());
          }
          ownedArgs.push_back(sourcePath.str().str());
          std::vector<llvm::StringRef> args;
          for (const std::string &arg : ownedArgs)
            args.push_back(arg);
          std::array<std::optional<llvm::StringRef>, 3> redirects = {
              llvm::StringRef(""), llvm::StringRef(stdoutPath),
              llvm::StringRef(stderrPath)};
          llvm::sys::ExecuteAndWait(Compiler, args, std::nullopt, redirects, 5);

          auto output = llvm::MemoryBuffer::getFile(stdoutPath);
          if (output) {
            llvm::StringRef remaining = output.get()->getBuffer();
            while (!remaining.empty()) {
              auto split = remaining.split('\n');
              llvm::StringRef line = split.first.trim();
              remaining = split.second;
              if (line.empty())
                continue;
              auto parsed = llvm::json::parse(line);
              if (!parsed) {
                llvm::consumeError(parsed.takeError());
                continue;
              }
              const auto *record = parsed->getAsObject();
              if (!record)
                continue;
              int64_t lineNumber = std::max<int64_t>(
                  0, record->getInteger("line").value_or(1) - 1);
              int64_t column = std::max<int64_t>(
                  0, record->getInteger("col").value_or(1) - 1);
              std::string level =
                  record->getString("level").value_or("error").str();
              int severity = level == "warning" ? 2 : level == "note" ? 3 : 1;
              llvm::json::Object start{{"line", lineNumber},
                                       {"character", column}};
              llvm::json::Object end{{"line", lineNumber},
                                     {"character", column + 1}};
              llvm::json::Object range{{"start", std::move(start)},
                                       {"end", std::move(end)}};
              llvm::json::Object diagnostic{
                  {"range", std::move(range)},
                  {"severity", severity},
                  {"source", "toka"},
                  {"message", record->getString("message")
                                  .value_or("compiler diagnostic")
                                  .str()}};
              if (auto code = record->getString("code"))
                diagnostic["code"] = code->str();
              diagnostics.emplace_back(std::move(diagnostic));
            }
          }
        }
      }
      llvm::sys::fs::remove(sourcePath);
      llvm::sys::fs::remove(stdoutPath);
      llvm::sys::fs::remove(stderrPath);
      llvm::sys::fs::remove(irPath);
    }
    notify("textDocument/publishDiagnostics",
           llvm::json::Object{{"uri", document.Uri},
                              {"diagnostics", std::move(diagnostics)}});
  }
};

} // namespace

int main(int argc, char **argv) {
  if (argc > 1) {
    std::string_view argument(argv[1]);
    if (argument == "--help" || argument == "-h") {
      std::cout << "Usage: tokalsp [--help|--version]\n"
                   "Run the Toka language server over standard input/output.\n";
      return 0;
    }
    if (argument == "--version" || argument == "-V") {
      std::cout << "tokalsp version " << TOKA_VERSION_STRING << '\n';
      return 0;
    }
    std::cerr << "error: unknown option '" << argument << "'\n";
    return 1;
  }
  LanguageServer server(argc > 0 ? argv[0] : "tokalsp");
  return server.run();
}
