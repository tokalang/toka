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

enum class TKICacheStatus {
    Ok,
    ReadError,
    MissingCompilerVersion,
    MissingFormatVersion,
    MissingTargetTriple,
    MissingSourceHash,
    CompilerVersionMismatch,
    FormatVersionMismatch,
    TargetTripleMismatch,
    SourceHashMismatch
};

struct TKIMetadata {
    std::string CompilerVersion;
    std::string FormatVersion;
    std::string TargetTriple;
    std::string SourceHash;
};

struct ModuleResolutionInfo {
    std::string CanonicalPath;
    bool IsInterface;
    bool FallbackTriggered;
    TKICacheStatus CacheStatus;
    std::string CacheStatusReason;
};

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

    // Get the recorded module dependencies
    const std::map<std::string, std::vector<std::string>>& getDependencies() const {
        return m_Dependencies;
    }

    const std::map<std::string, ModuleResolutionInfo>& getResolutionRecords() const {
        return m_ResolutionRecords;
    }

private:
    SourceManager &m_SourceManager;
    std::vector<std::string> m_SearchPaths;
    std::map<std::string, std::string> m_PkgMap;

    std::set<std::string> m_Visited;
    std::vector<std::string> m_RecursionStack;
    std::map<std::string, std::vector<std::string>> m_Dependencies;
    std::map<std::string, ModuleResolutionInfo> m_ResolutionRecords;

    // Helper to search and resolve rawFilename to a lexically normalized path.
    // Returns empty string if not found.
    std::string resolveSourcePath(const std::string &rawFilename,
                                   const std::vector<std::string> &activeSearchPaths);

    // Recursively resolve imports and build AST.
    bool parseRecursive(const std::string &filename,
                        std::vector<std::unique_ptr<Module>> &astModules,
                        const std::string &overrideSourceCode,
                        std::string *outActualPath = nullptr);

    // Helper to read and validate metadata from a .tki file
    bool readTKIMetadata(const std::string &path, TKIMetadata &meta);
    TKICacheStatus validateTKIMetadata(const std::string &path, std::string &reason);
};

} // namespace toka

#endif // TOKA_MODULE_RESOLVER_H
