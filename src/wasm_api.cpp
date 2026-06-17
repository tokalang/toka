#include <emscripten.h>
#include "toka/Lexer.h"
#include "toka/Parser.h"
#include "toka/Sema.h"
#include "toka/SourceManager.h"
#include "toka/DiagnosticEngine.h"
#include "toka/PathUtils.h"
#include "toka/ModuleResolver.h"
#include <string>
#include <sstream>
#include <iostream>

bool g_JsonDiagnostics = false;
bool verboseMode = false;

// Intercept std::cout to capture JSON output
class StringbufStream : public std::stringbuf {
public:
    std::string getString() {
        std::string s = this->str();
        this->str(""); // clear
        return s;
    }
};


#include <fstream>
#include <vector>
#include <set>
#include <memory>


extern "C" {

EMSCRIPTEN_KEEPALIVE
const char* check_toka_code(const char* code_cstr) {
    static std::string lastResult;
    
    // Enable JSON output
    g_JsonDiagnostics = true;
    
    // Reset errors
    toka::DiagnosticEngine::ErrorCount = 0;
    
    // Capture stdout
    std::streambuf* oldCout = std::cout.rdbuf();
    StringbufStream captureBuf;
    std::cout.rdbuf(&captureBuf);

    toka::SourceManager sm;
    toka::DiagnosticEngine::init(sm);

    std::vector<std::unique_ptr<toka::Module>> astModules;
    std::vector<std::string> searchPaths; // Emscripten will have access to virtual /lib
    bool parseSuccess = resolver.resolveAndParse("playground.tk", astModules, code_cstr);

    if (parseSuccess && !toka::DiagnosticEngine::hasErrors()) {
        toka::Sema sema;
        sema.setBorrowCheckEnabled(true);
        for (auto &mod : astModules) {
            sema.checkModule(*mod);
        }
    }

    // Restore stdout
    std::cout.rdbuf(oldCout);

    std::string errors = captureBuf.getString();
    
    if (errors.empty()) {
        lastResult = "{\"status\": \"ok\"}";
    } else {
        // Output might contain multiple JSON objects separated by newlines
        // We wrap them in a JSON array
        std::stringstream jsonArr;
        jsonArr << "{\"status\": \"error\", \"diagnostics\": [";
        
        bool first = true;
        std::istringstream iss(errors);
        std::string line;
        while (std::getline(iss, line)) {
            if (line.empty()) continue;
            if (!first) jsonArr << ",";
            jsonArr << line;
            first = false;
        }
        jsonArr << "]}";
        
        lastResult = jsonArr.str();
    }

    return lastResult.c_str();
}

} // extern "C"
