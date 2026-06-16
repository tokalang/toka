#ifndef TOKA_MODULE_RESOLVER_H
#define TOKA_MODULE_RESOLVER_H

#include "toka/Parser.h"
#include "toka/SourceManager.h"
#include <string>
#include <vector>
#include <set>
#include <map>
#include <memory>

namespace toka {

class ModuleResolver {
public:
    ModuleResolver(SourceManager &sm,
                   std::vector<std::string> searchPaths,
                   std::map<std::string, std::string> pkgMap = {});

    // Parse the entry file and all imports recursively.
    // If overrideSourceCode is non-empty, the entry file content is taken from it (used in playground).
    bool resolveAndParse(const std::string &rawFilename,
                         std::vector<std::unique_ptr<Module>> &astModules,
                         const std::string &overrideSourceCode = "");

private:
    SourceManager &m_SourceManager;
    std::vector<std::string> m_SearchPaths;
    std::map<std::string, std::string> m_PkgMap;

    std::set<std::string> m_Visited;
    std::vector<std::string> m_RecursionStack;

    // Helper to search and resolve rawFilename to a lexically normalized path.
    // Returns empty string if not found.
    std::string resolveSourcePath(const std::string &rawFilename,
                                   const std::vector<std::string> &activeSearchPaths);

    // Recursively resolve imports and build AST.
    bool parseRecursive(const std::string &filename,
                        std::vector<std::unique_ptr<Module>> &astModules,
                        const std::string &overrideSourceCode);
};

} // namespace toka

#endif // TOKA_MODULE_RESOLVER_H
