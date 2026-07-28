// Copyright (c) 2025 YiZhonghua<zhyi@dpai.com>. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "toka/AnalysisSession.h"
#include "toka/Parser.h"
#include "toka/PathUtils.h"
#include "toka/Version.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Program.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/Host.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

bool g_JsonDiagnostics = false;

namespace {

struct Position {
  unsigned Line = 0;
  unsigned Character = 0;
};

struct Document {
  std::string Uri;
  std::string Path;
  std::string Text;
  int64_t Version = 0;
};

static std::string serialize(const llvm::json::Value &value) {
  std::string result;
  llvm::raw_string_ostream stream(result);
  stream << value;
  return result;
}

static std::string trim(std::string text) {
  auto isSpace = [](unsigned char ch) { return std::isspace(ch); };
  text.erase(text.begin(), std::find_if(text.begin(), text.end(),
                                        [&](char ch) { return !isSpace(ch); }));
  text.erase(std::find_if(text.rbegin(), text.rend(),
                          [&](char ch) { return !isSpace(ch); })
                 .base(),
             text.end());
  return text;
}

static std::string percentDecode(std::string_view value) {
  std::string decoded;
  auto hex = [](char ch) -> int {
    if (ch >= '0' && ch <= '9')
      return ch - '0';
    if (ch >= 'a' && ch <= 'f')
      return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F')
      return ch - 'A' + 10;
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
  return toka::PathUtils::canonicalize(path);
}

static std::string pathToUri(const std::string &path) {
  std::string canonical = toka::PathUtils::canonicalize(path);
  std::string encoded;
  static constexpr char hex[] = "0123456789ABCDEF";
  for (unsigned char ch : canonical) {
    if (std::isalnum(ch) || ch == '/' || ch == '-' || ch == '_' || ch == '.' ||
        ch == '~' || ch == ':') {
      encoded.push_back(static_cast<char>(ch));
    } else {
      encoded.push_back('%');
      encoded.push_back(hex[ch >> 4]);
      encoded.push_back(hex[ch & 15]);
    }
  }
#ifdef _WIN32
  return "file:///" + encoded;
#else
  return "file://" + encoded;
#endif
}

static std::optional<std::string> getUri(const llvm::json::Object &params) {
  const auto *document = params.getObject("textDocument");
  if (!document)
    return std::nullopt;
  auto uri = document->getString("uri");
  return uri ? std::optional<std::string>(uri->str()) : std::nullopt;
}

static std::optional<Position> getPosition(const llvm::json::Object &params) {
  const auto *position = params.getObject("position");
  if (!position)
    return std::nullopt;
  auto line = position->getInteger("line");
  auto character = position->getInteger("character");
  if (!line || !character || *line < 0 || *character < 0)
    return std::nullopt;
  return Position{static_cast<unsigned>(*line),
                  static_cast<unsigned>(*character)};
}

static size_t utf8Length(unsigned char ch) {
  if ((ch & 0x80) == 0)
    return 1;
  if ((ch & 0xE0) == 0xC0)
    return 2;
  if ((ch & 0xF0) == 0xE0)
    return 3;
  if ((ch & 0xF8) == 0xF0)
    return 4;
  return 1;
}

static std::string readFile(const std::string &path) {
  auto buffer = llvm::MemoryBuffer::getFile(path);
  return buffer ? buffer.get()->getBuffer().str() : std::string();
}

static std::string_view lineView(std::string_view text, unsigned line) {
  size_t start = 0;
  for (unsigned current = 0; current < line; ++current) {
    start = text.find('\n', start);
    if (start == std::string_view::npos)
      return {};
    ++start;
  }
  size_t end = text.find('\n', start);
  return text.substr(start, end == std::string_view::npos ? text.size() - start
                                                          : end - start);
}

static unsigned utf16ToByte(std::string_view text, unsigned character) {
  size_t offset = 0;
  unsigned units = 0;
  while (offset < text.size() && units < character) {
    size_t length =
        std::min(utf8Length(static_cast<unsigned char>(text[offset])),
                 text.size() - offset);
    unsigned width = length == 4 ? 2 : 1;
    if (units + width > character)
      break;
    units += width;
    offset += length;
  }
  return static_cast<unsigned>(offset);
}

static unsigned byteToUtf16(std::string_view text, unsigned character) {
  size_t offset = 0;
  unsigned units = 0;
  size_t limit = std::min<size_t>(character, text.size());
  while (offset < limit) {
    size_t length = std::min(
        utf8Length(static_cast<unsigned char>(text[offset])), limit - offset);
    units += length == 4 ? 2 : 1;
    offset += length;
  }
  return units;
}

static int lspSymbolKind(toka::SemanticSymbolKind kind) {
  using Kind = toka::SemanticSymbolKind;
  switch (kind) {
  case Kind::Module:
    return 2;
  case Kind::Function:
    return 12;
  case Kind::Method:
    return 6;
  case Kind::ExternFunction:
    return 12;
  case Kind::Shape:
    return 23;
  case Kind::Trait:
    return 11;
  case Kind::TypeAlias:
    return 26;
  case Kind::Field:
    return 8;
  case Kind::EnumVariant:
    return 22;
  case Kind::Global:
    return 14;
  case Kind::Parameter:
    return 13;
  case Kind::Variable:
    return 13;
  case Kind::Pattern:
    return 13;
  }
  return 13;
}

static int completionKind(toka::SemanticSymbolKind kind) {
  using Kind = toka::SemanticSymbolKind;
  switch (kind) {
  case Kind::Function:
  case Kind::ExternFunction:
    return 3;
  case Kind::Method:
    return 2;
  case Kind::Shape:
    return 7;
  case Kind::Trait:
    return 8;
  case Kind::TypeAlias:
    return 25;
  case Kind::Field:
    return 5;
  case Kind::EnumVariant:
    return 20;
  case Kind::Module:
    return 9;
  default:
    return 6;
  }
}

class LanguageServer {
public:
  explicit LanguageServer(const char *argv0)
      : Formatter(findSibling(argv0, "tokafmt")) {
    toka::Parser::TargetTriple = llvm::sys::getDefaultTargetTriple();
    SearchPaths = environmentPaths("TOKA_LIB");
    std::vector<std::string> additional = environmentPaths("TOKA_PATH");
    SearchPaths.insert(SearchPaths.end(), additional.begin(), additional.end());
    llvm::SmallString<256> executable(argv0 ? argv0 : "");
    llvm::sys::path::remove_filename(executable);
    llvm::SmallString<256> bundled(executable);
    llvm::sys::path::append(bundled, "..", "share", "toka", "lib");
    if (llvm::sys::fs::exists(bundled))
      SearchPaths.push_back(bundled.str().str());
    Session = std::make_unique<toka::AnalysisSession>(SearchPaths);
  }

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
  std::unique_ptr<toka::AnalysisSession> Session;
  toka::AnalysisResult LastAnalysis;
  std::vector<std::string> SearchPaths;
  std::string RootUri;
  std::string Formatter;
  bool ShutdownRequested = false;

  static std::vector<std::string> environmentPaths(const char *name) {
    std::vector<std::string> paths;
    const char *value = std::getenv(name);
    if (!value)
      return paths;
    std::string text(value);
    size_t start = 0;
    while (start <= text.size()) {
      size_t end = text.find(llvm::sys::EnvPathSeparator, start);
      std::string path = text.substr(start, end - start);
      if (!path.empty())
        paths.push_back(path);
      if (end == std::string::npos)
        break;
      start = end + 1;
    }
    return paths;
  }

  static std::string findSibling(const char *argv0, llvm::StringRef name) {
    llvm::SmallString<256> sibling(argv0 ? argv0 : "");
    llvm::sys::path::remove_filename(sibling);
#ifdef _WIN32
    llvm::sys::path::append(sibling, name + ".exe");
#else
    llvm::sys::path::append(sibling, name);
#endif
    if (!sibling.empty() && llvm::sys::fs::exists(sibling))
      return sibling.str().str();
    auto found = llvm::sys::findProgramByName(name);
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
                     [](unsigned char ch) { return std::tolower(ch); });
      constexpr std::string_view header = "content-length:";
      if (lower.substr(0, header.size()) == header) {
        try {
          contentLength = static_cast<size_t>(
              std::stoull(trim(line.substr(header.size()))));
        } catch (...) {
          return std::nullopt;
        }
      }
    }
    if (!sawHeader || contentLength == 0)
      return std::nullopt;
    std::string body(contentLength, '\0');
    std::cin.read(body.data(), static_cast<std::streamsize>(contentLength));
    return static_cast<size_t>(std::cin.gcount()) == contentLength
               ? std::optional<std::string>(std::move(body))
               : std::nullopt;
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
    sendBody(serialize(llvm::json::Object{{"jsonrpc", "2.0"},
                                          {"method", std::move(method)},
                                          {"params", std::move(params)}}));
  }

  bool process(const std::string &body) {
    llvm::Expected<llvm::json::Value> parsed = llvm::json::parse(body);
    if (!parsed) {
      llvm::consumeError(parsed.takeError());
      replyError(nullptr, -32700, "Parse error");
      return false;
    }
    auto *message = parsed->getAsObject();
    auto method = message ? message->getString("method") : std::nullopt;
    if (!message || !method) {
      replyError(message ? message->get("id") : nullptr, -32600,
                 "Invalid Request");
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
      initialize(id, params);
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
    if (*method == "textDocument/didOpen")
      didOpen(*params);
    else if (*method == "textDocument/didChange")
      didChange(*params);
    else if (*method == "textDocument/didClose")
      didClose(*params);
    else if (*method == "textDocument/hover")
      hover(id, *params);
    else if (*method == "textDocument/definition")
      definition(id, *params);
    else if (*method == "textDocument/references")
      references(id, *params);
    else if (*method == "textDocument/completion")
      completion(id, *params);
    else if (*method == "textDocument/rename")
      rename(id, *params);
    else if (*method == "textDocument/signatureHelp")
      signatureHelp(id, *params);
    else if (*method == "textDocument/documentSymbol")
      documentSymbols(id, *params);
    else if (*method == "workspace/symbol")
      workspaceSymbols(id, *params);
    else if (*method == "textDocument/formatting")
      formatting(id, *params);
    else if (*method == "toka/semanticBundle")
      semanticBundle(id, *params);
    else if (*method == "toka/analysisStats")
      analysisStats(id);
    else if (id)
      replyError(id, -32601, "Method not found");
    return false;
  }

  void initialize(const llvm::json::Value *id,
                  const llvm::json::Object *params) {
    if (params) {
      std::string root;
      if (auto uri = params->getString("rootUri"))
        root = uriToPath(*uri);
      else if (auto path = params->getString("rootPath"))
        root = path->str();
      if (!root.empty()) {
        SearchPaths.push_back(root);
        Session = std::make_unique<toka::AnalysisSession>(SearchPaths);
      }
    }
    llvm::json::Object sync{{"openClose", true}, {"change", 1}};
    llvm::json::Object completionOptions{
        {"triggerCharacters", llvm::json::Array{".", ":", "@"}},
        {"resolveProvider", false}};
    llvm::json::Object capabilities{
        {"positionEncoding", "utf-16"},
        {"textDocumentSync", std::move(sync)},
        {"hoverProvider", true},
        {"definitionProvider", true},
        {"referencesProvider", true},
        {"completionProvider", std::move(completionOptions)},
        {"renameProvider", llvm::json::Object{{"prepareProvider", false}}},
        {"signatureHelpProvider",
         llvm::json::Object{
             {"triggerCharacters", llvm::json::Array{"(", ","}}}},
        {"documentSymbolProvider", true},
        {"workspaceSymbolProvider", true},
        {"documentFormattingProvider", !Formatter.empty()}};
    reply(id, llvm::json::Object{
                  {"capabilities", std::move(capabilities)},
                  {"serverInfo",
                   llvm::json::Object{{"name", "tokalsp"},
                                      {"version", TOKA_VERSION_STRING}}}});
  }

  void didOpen(const llvm::json::Object &params) {
    const auto *document = params.getObject("textDocument");
    if (!document)
      return;
    auto uri = document->getString("uri");
    auto text = document->getString("text");
    if (!uri || !text)
      return;
    int64_t version = document->getInteger("version").value_or(0);
    updateDocument(uri->str(), text->str(), version, true);
  }

  void didChange(const llvm::json::Object &params) {
    auto uri = getUri(params);
    const auto *changes = params.getArray("contentChanges");
    if (!uri || !changes || changes->empty())
      return;
    const auto *last = changes->back().getAsObject();
    auto text = last ? last->getString("text") : std::nullopt;
    const auto *document = params.getObject("textDocument");
    if (text)
      updateDocument(*uri, text->str(),
                     document ? document->getInteger("version").value_or(0) : 0,
                     false);
  }

  void didClose(const llvm::json::Object &params) {
    auto uri = getUri(params);
    if (!uri)
      return;
    auto document = Documents.find(*uri);
    if (document != Documents.end())
      Session->closeDocument(document->second.Path);
    Documents.erase(*uri);
    notify("textDocument/publishDiagnostics",
           llvm::json::Object{{"uri", *uri},
                              {"diagnostics", llvm::json::Array()}});
    if (RootUri == *uri)
      RootUri = Documents.empty() ? "" : Documents.begin()->first;
    analyzeWorkspace();
  }

  void updateDocument(std::string uri, std::string text, int64_t version,
                      bool opening) {
    std::string path = uriToPath(uri);
    if (path.empty())
      return;
    Documents[uri] = Document{uri, path, std::move(text), version};
    if (RootUri.empty())
      RootUri = uri;
    if (opening)
      Session->openDocument(path, Documents[uri].Text);
    else
      Session->updateDocument(path, Documents[uri].Text);
    analyzeWorkspace();
  }

  void analyzeWorkspace() {
    if (RootUri.empty() || !Documents.count(RootUri))
      return;
    LastAnalysis = Session->analyze(Documents.at(RootUri).Path);
    publishDiagnostics();
  }

  std::string textForPath(const std::string &path) const {
    std::string canonical = toka::PathUtils::canonicalize(path);
    for (const auto &[uri, document] : Documents)
      if (document.Path == canonical)
        return document.Text;
    return readFile(canonical);
  }

  std::string uriForPath(const std::string &path) const {
    std::string canonical = toka::PathUtils::canonicalize(path);
    for (const auto &[uri, document] : Documents)
      if (document.Path == canonical)
        return uri;
    return pathToUri(canonical);
  }

  unsigned queryCharacter(const Document &document, Position position) const {
    return utf16ToByte(lineView(document.Text, position.Line),
                       position.Character);
  }

  llvm::json::Object positionJSON(const std::string &file,
                                  toka::SemanticPosition position) const {
    std::string text = textForPath(file);
    unsigned character =
        byteToUtf16(lineView(text, position.Line), position.Character);
    return llvm::json::Object{{"line", position.Line},
                              {"character", character}};
  }

  llvm::json::Object rangeJSON(const toka::SemanticRange &range) const {
    return llvm::json::Object{{"start", positionJSON(range.File, range.Start)},
                              {"end", positionJSON(range.File, range.End)}};
  }

  llvm::json::Object locationJSON(const toka::SemanticRange &range) const {
    return llvm::json::Object{{"uri", uriForPath(range.File)},
                              {"range", rangeJSON(range)}};
  }

  std::pair<const Document *, const toka::SemanticOccurrence *>
  occurrence(const llvm::json::Object &params) const {
    auto uri = getUri(params);
    auto position = getPosition(params);
    if (!uri || !position)
      return {nullptr, nullptr};
    auto document = Documents.find(*uri);
    if (document == Documents.end())
      return {nullptr, nullptr};
    const auto *found = LastAnalysis.Index.occurrenceAt(
        document->second.Path, position->Line,
        queryCharacter(document->second, *position));
    return {&document->second, found};
  }

  void hover(const llvm::json::Value *id,
             const llvm::json::Object &params) const {
    auto [document, found] = occurrence(params);
    const auto *symbol =
        found ? LastAnalysis.Index.symbol(found->SymbolID) : nullptr;
    if (!symbol) {
      reply(id, nullptr);
      return;
    }
    std::string value = "```toka\n" + symbol->Detail + "\n```";
    if (!symbol->Documentation.empty())
      value += "\n\n" + symbol->Documentation;
    reply(id, llvm::json::Object{
                  {"contents",
                   llvm::json::Object{{"kind", "markdown"}, {"value", value}}},
                  {"range", rangeJSON(found->Range)}});
  }

  void definition(const llvm::json::Value *id,
                  const llvm::json::Object &params) const {
    auto [document, found] = occurrence(params);
    const auto *symbol =
        found ? LastAnalysis.Index.symbol(found->SymbolID) : nullptr;
    reply(id, symbol ? llvm::json::Value(locationJSON(symbol->Declaration))
                     : llvm::json::Value(nullptr));
  }

  void references(const llvm::json::Value *id,
                  const llvm::json::Object &params) const {
    auto [document, found] = occurrence(params);
    bool includeDeclaration = true;
    if (const auto *context = params.getObject("context"))
      includeDeclaration =
          context->getBoolean("includeDeclaration").value_or(true);
    llvm::json::Array locations;
    if (found)
      for (const auto *reference :
           LastAnalysis.Index.references(found->SymbolID, includeDeclaration))
        locations.emplace_back(locationJSON(reference->Range));
    reply(id, std::move(locations));
  }

  void completion(const llvm::json::Value *id,
                  const llvm::json::Object &params) const {
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
    auto uri = getUri(params);
    auto position = getPosition(params);
    auto document = uri ? Documents.find(*uri) : Documents.end();
    if (position && document != Documents.end()) {
      for (const auto *symbol : LastAnalysis.Index.completions(
               document->second.Path, position->Line,
               queryCharacter(document->second, *position))) {
        llvm::json::Object item{{"label", symbol->Name},
                                {"kind", completionKind(symbol->Kind)},
                                {"detail", symbol->Detail}};
        if (!symbol->Documentation.empty())
          item["documentation"] = llvm::json::Object{
              {"kind", "markdown"}, {"value", symbol->Documentation}};
        items.emplace_back(std::move(item));
      }
    }
    reply(id, llvm::json::Object{{"isIncomplete", false},
                                 {"items", std::move(items)}});
  }

  static bool validIdentifier(llvm::StringRef name) {
    if (name.empty() ||
        !(std::isalpha(static_cast<unsigned char>(name[0])) || name[0] == '_'))
      return false;
    return std::all_of(name.begin() + 1, name.end(), [](unsigned char ch) {
      return std::isalnum(ch) || ch == '_';
    });
  }

  void rename(const llvm::json::Value *id,
              const llvm::json::Object &params) const {
    auto newName = params.getString("newName");
    auto [document, found] = occurrence(params);
    const auto *symbol =
        found ? LastAnalysis.Index.symbol(found->SymbolID) : nullptr;
    if (!newName || !validIdentifier(*newName)) {
      replyError(id, -32602, "newName must be a valid Toka identifier");
      return;
    }
    if (!symbol) {
      reply(id, nullptr);
      return;
    }
    for (const auto &[candidateID, candidate] : LastAnalysis.Index.symbols()) {
      if (candidateID != symbol->ID && candidate.Name == *newName &&
          candidate.ScopeID == symbol->ScopeID) {
        replyError(id, -32602, "newName conflicts in the same scope");
        return;
      }
    }
    std::map<std::string, llvm::json::Array> editsByUri;
    for (const auto *reference : LastAnalysis.Index.references(symbol->ID))
      editsByUri[uriForPath(reference->Range.File)].emplace_back(
          llvm::json::Object{{"range", rangeJSON(reference->Range)},
                             {"newText", newName->str()}});
    llvm::json::Object changes;
    for (auto &[uri, edits] : editsByUri)
      changes[uri] = std::move(edits);
    reply(id, llvm::json::Object{{"changes", std::move(changes)}});
  }

  void signatureHelp(const llvm::json::Value *id,
                     const llvm::json::Object &params) const {
    auto uri = getUri(params);
    auto position = getPosition(params);
    auto document = uri ? Documents.find(*uri) : Documents.end();
    if (!position || document == Documents.end()) {
      reply(id, nullptr);
      return;
    }
    const std::string &text = document->second.Text;
    size_t offset = 0;
    for (unsigned line = 0; line < position->Line; ++line) {
      offset = text.find('\n', offset);
      if (offset == std::string::npos) {
        reply(id, nullptr);
        return;
      }
      ++offset;
    }
    offset += utf16ToByte(lineView(text, position->Line), position->Character);
    size_t open = std::string::npos;
    unsigned depth = 0;
    for (size_t cursor = offset; cursor > 0; --cursor) {
      char ch = text[cursor - 1];
      if (ch == ')')
        ++depth;
      else if (ch == '(') {
        if (depth == 0) {
          open = cursor - 1;
          break;
        }
        --depth;
      }
    }
    if (open == std::string::npos) {
      reply(id, nullptr);
      return;
    }
    size_t end = open;
    while (end > 0 && std::isspace(static_cast<unsigned char>(text[end - 1])))
      --end;
    size_t start = end;
    while (start > 0 &&
           (std::isalnum(static_cast<unsigned char>(text[start - 1])) ||
            text[start - 1] == '_'))
      --start;
    unsigned calleeLine = static_cast<unsigned>(
        std::count(text.begin(), text.begin() + start, '\n'));
    size_t lineStart = start == 0 ? 0 : text.rfind('\n', start - 1) + 1;
    const auto *found = LastAnalysis.Index.occurrenceAt(
        document->second.Path, calleeLine,
        static_cast<unsigned>(start - lineStart));
    const auto *symbol =
        found ? LastAnalysis.Index.symbol(found->SymbolID) : nullptr;
    if (!symbol) {
      reply(id, nullptr);
      return;
    }
    unsigned active = 0;
    depth = 0;
    for (size_t cursor = open + 1; cursor < offset; ++cursor) {
      if (text[cursor] == '(' || text[cursor] == '[' || text[cursor] == '{')
        ++depth;
      else if (text[cursor] == ')' || text[cursor] == ']' ||
               text[cursor] == '}') {
        if (depth > 0)
          --depth;
      } else if (text[cursor] == ',' && depth == 0)
        ++active;
    }
    reply(id,
          llvm::json::Object{
              {"signatures", llvm::json::Array{llvm::json::Object{
                                 {"label", symbol->Detail},
                                 {"documentation", symbol->Documentation}}}},
              {"activeSignature", 0},
              {"activeParameter", active}});
  }

  void documentSymbols(const llvm::json::Value *id,
                       const llvm::json::Object &params) const {
    auto uri = getUri(params);
    auto document = uri ? Documents.find(*uri) : Documents.end();
    llvm::json::Array symbols;
    if (document != Documents.end())
      for (const auto *symbol :
           LastAnalysis.Index.documentSymbols(document->second.Path))
        symbols.emplace_back(llvm::json::Object{
            {"name", symbol->Name},
            {"detail", symbol->Detail},
            {"kind", lspSymbolKind(symbol->Kind)},
            {"range", rangeJSON(symbol->Declaration)},
            {"selectionRange", rangeJSON(symbol->Declaration)}});
    reply(id, std::move(symbols));
  }

  void workspaceSymbols(const llvm::json::Value *id,
                        const llvm::json::Object &params) const {
    std::string query = params.getString("query").value_or("").str();
    llvm::json::Array symbols;
    for (const auto *symbol : LastAnalysis.Index.workspaceSymbols()) {
      if (!query.empty() && symbol->Name.find(query) == std::string::npos)
        continue;
      symbols.emplace_back(
          llvm::json::Object{{"name", symbol->Name},
                             {"kind", lspSymbolKind(symbol->Kind)},
                             {"location", locationJSON(symbol->Declaration)},
                             {"containerName", symbol->ContainerID}});
    }
    reply(id, std::move(symbols));
  }

  void formatting(const llvm::json::Value *id,
                  const llvm::json::Object &params) const {
    auto uri = getUri(params);
    auto document = uri ? Documents.find(*uri) : Documents.end();
    if (document == Documents.end() || Formatter.empty()) {
      reply(id, llvm::json::Array());
      return;
    }
    llvm::SmallString<128> temporary;
    if (llvm::sys::fs::createTemporaryFile("tokalsp-format", "tk", temporary)) {
      replyError(id, -32603, "could not create formatter input");
      return;
    }
    std::error_code error;
    llvm::raw_fd_ostream output(temporary, error);
    output << document->second.Text;
    output.close();
    std::vector<llvm::StringRef> arguments = {Formatter, "--write", temporary};
    int status = llvm::sys::ExecuteAndWait(Formatter, arguments);
    std::string formatted = status == 0 ? readFile(temporary.str().str()) : "";
    llvm::sys::fs::remove(temporary);
    if (status != 0 || formatted == document->second.Text) {
      reply(id, llvm::json::Array());
      return;
    }
    unsigned lastLine = static_cast<unsigned>(std::count(
        document->second.Text.begin(), document->second.Text.end(), '\n'));
    std::string_view finalLine = lineView(document->second.Text, lastLine);
    unsigned lastCharacter = byteToUtf16(finalLine, finalLine.size());
    reply(id,
          llvm::json::Array{llvm::json::Object{
              {"range",
               llvm::json::Object{
                   {"start", llvm::json::Object{{"line", 0}, {"character", 0}}},
                   {"end", llvm::json::Object{{"line", lastLine},
                                              {"character", lastCharacter}}}}},
              {"newText", formatted}}});
  }

  llvm::json::Object analysisStatsJSON() const {
    llvm::json::Array invalidated;
    for (const std::string &module : LastAnalysis.Stats.InvalidatedModules)
      invalidated.emplace_back(module);
    return llvm::json::Object{
        {"revision", LastAnalysis.Stats.Revision},
        {"totalModules", LastAnalysis.Stats.TotalModules},
        {"reusedModules", LastAnalysis.Stats.ReusedModules},
        {"recheckedModules", LastAnalysis.Stats.RecheckedModules},
        {"elapsedMs", LastAnalysis.Stats.ElapsedMilliseconds},
        {"invalidatedModules", std::move(invalidated)},
        {"fresh", LastAnalysis.HasFreshIndex}};
  }

  llvm::json::Object diagnosticJSON(const Document &document,
                                    const toka::DiagnosticRecord &record)
      const {
    unsigned line = record.Line > 0 ? record.Line - 1 : 0;
    unsigned byteColumn = record.Column > 0 ? record.Column - 1 : 0;
    unsigned character =
        byteToUtf16(lineView(document.Text, line), byteColumn);
    int severity = record.Level == toka::DiagLevel::Warning      ? 2
                   : record.Level == toka::DiagLevel::Note       ? 3
                   : record.Level == toka::DiagLevel::Structural ? 4
                                                                 : 1;
    return llvm::json::Object{
        {"range",
         llvm::json::Object{
             {"start", llvm::json::Object{{"line", line},
                                           {"character", character}}},
             {"end", llvm::json::Object{{"line", line},
                                         {"character", character + std::max(
                                                                   1, record.Length)}}}}},
        {"severity", severity},
        {"code", record.Code},
        {"source", "toka"},
        {"message", record.Message}};
  }

  void analysisStats(const llvm::json::Value *id) const {
    reply(id, analysisStatsJSON());
  }

  void semanticBundle(const llvm::json::Value *id,
                      const llvm::json::Object &params) const {
    auto uri = getUri(params);
    auto document = uri ? Documents.find(*uri) : Documents.end();
    if (document == Documents.end()) {
      replyError(id, -32602,
                 "toka/semanticBundle requires an open textDocument URI");
      return;
    }

    llvm::json::Array diagnostics;
    for (const toka::DiagnosticRecord &record : LastAnalysis.Diagnostics) {
      if (toka::PathUtils::canonicalize(record.File) == document->second.Path)
        diagnostics.emplace_back(diagnosticJSON(document->second, record));
    }

    llvm::json::Value index = llvm::json::Value(nullptr);
    if (LastAnalysis.HasFreshIndex) {
      index = LastAnalysis.Index.queryJSON("documentSymbols",
                                           document->second.Path, 0, 0);
    }

    reply(id, llvm::json::Object{
                  {"schema", "toka.overlay-semantic-bundle"},
                  {"version", 1},
                  {"document", llvm::json::Object{
                                   {"uri", document->second.Uri},
                                   {"version", document->second.Version},
                                   {"overlay",
                                    Session->hasDocument(document->second.Path)}}},
                  {"analysis", analysisStatsJSON()},
                  {"diagnostics", std::move(diagnostics)},
                  {"semantic_index", std::move(index)},
                  {"read_only", true}});
  }

  void publishDiagnostics() const {
    for (const auto &[uri, document] : Documents) {
      llvm::json::Array diagnostics;
      for (const toka::DiagnosticRecord &record : LastAnalysis.Diagnostics) {
        if (toka::PathUtils::canonicalize(record.File) != document.Path)
          continue;
        diagnostics.emplace_back(diagnosticJSON(document, record));
      }
      notify("textDocument/publishDiagnostics",
             llvm::json::Object{{"uri", uri},
                                {"diagnostics", std::move(diagnostics)}});
    }
  }
};

} // namespace

int main(int argc, char **argv) {
  if (argc > 1) {
    std::string_view argument(argv[1]);
    if (argument == "--help" || argument == "-h") {
      std::cout << "Usage: tokalsp [--help|--version]\n"
                   "Run the Toka semantic language server over standard "
                   "input/output.\n";
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
