#include "toka/TKIExporter.h"
#include "toka/Type.h"
#include "toka/InterfaceVersion.h"
#include "toka/Parser.h"
#include "toka/PathUtils.h"
#include <cstdio>
#include <sstream>
#include <fstream>

namespace toka {

static std::string calculateFNV1a(const std::string &str) {
    uint64_t hash = 14695981039346656037ULL;
    for (char c : str) {
        hash ^= static_cast<uint8_t>(c);
        hash *= 1099511628211ULL;
    }
    char buf[17];
    snprintf(buf, sizeof(buf), "%016llx", (unsigned long long)hash);
    return std::string(buf);
}

static std::string escapeLiteralContent(const std::string &value) {
    std::string out;
    for (unsigned char c : value) {
        switch (c) {
        case '\n': out += "\\n"; break;
        case '\t': out += "\\t"; break;
        case '\r': out += "\\r"; break;
        case '\\': out += "\\\\"; break;
        case '"': out += "\\\""; break;
        case '\0': out += "\\0"; break;
        default:
            if (c < 0x20 || c >= 0x7f) {
                char buf[5];
                std::snprintf(buf, sizeof(buf), "\\x%02x", c);
                out += buf;
            } else {
                out += static_cast<char>(c);
            }
            break;
        }
    }
    return out;
}

static std::string escapeCharLiteral(char value) {
    switch (value) {
    case '\n': return "\\n";
    case '\t': return "\\t";
    case '\r': return "\\r";
    case '\\': return "\\\\";
    case '\'': return "\\'";
    case '\0': return "\\0";
    default:
        unsigned char c = static_cast<unsigned char>(value);
        if (c < 0x20 || c >= 0x7f) {
            char buf[5];
            std::snprintf(buf, sizeof(buf), "\\x%02x", c);
            return std::string(buf);
        }
        return std::string(1, value);
    }
}

// Static helper to reconstruct variable names physical morphology
static std::string reconstructVar(
    const std::string &rawName,
    const std::string &typeStr,
    bool hasPointer, bool isUnique, bool isShared, bool isReference,
    bool isPointerNullable, bool isRebindable, bool isRebindBlocked,
    bool isExplicitBound, bool isMorphicExempt, bool isCeded,
    bool isValueMutable, bool isValueNullable, bool isValueBlocked
) {
    std::string result = "";
    if (isCeded) {
        result += "cede ";
    }
    if (isExplicitBound) {
        result += "`";
    }
    if (isPointerNullable) {
        result += "nul ";
    }

    if (isUnique) {
        result += "^";
    } else if (isShared) {
        result += "~";
    } else if (isReference) {
        result += "&";
    } else if (hasPointer) {
        result += "*";
    }

    if (isRebindable) {
        result += "#";
    } else if (isRebindBlocked) {
        result += "$";
    }

    std::string name = toka::Type::stripMorphology(rawName);
    bool nameHasMorphicPrefix = !name.empty() && name[0] == '\'';
    std::string strippedType = toka::Type::stripPrefixes(typeStr);
    bool typeHasMorphicPrefix = !strippedType.empty() && strippedType[0] == '\'';
    if ((isMorphicExempt || typeHasMorphicPrefix) && !nameHasMorphicPrefix) {
        result += "'";
    }
    result += name;

    if (isValueMutable) {
        result += "#";
    } else if (isValueNullable) {
        result += "?";
    } else if (isValueBlocked) {
        result += "$";
    }

    return result;
}

static std::string exportBindingType(const std::string &typeStr) {
    std::string stripped = toka::Type::stripPrefixes(typeStr);
    if (!stripped.empty() && stripped[0] == '\'') {
        stripped.erase(0, 1);
    }
    return stripped;
}

static std::string exportTypeSyntax(const TypeSyntaxPtr &syntax,
                                    const std::string &fallback) {
    return syntax ? syntax->toCanonicalString() : fallback;
}

static bool hasTypeSideMorphicPrefix(const std::string &typeStr) {
    std::string stripped = toka::Type::stripPrefixes(typeStr);
    return !stripped.empty() && stripped[0] == '\'';
}

static std::string exportReturnType(const ReturnContractSyntax &contract,
                                    bool isExtern) {
    const std::string canonicalReturn =
        exportTypeSyntax(contract.TypeSyntax, contract.Type);
    if (contract.ResultKind == ReturnResultKind::Unit &&
        contract.Effect == EffectKind::None && !isExtern)
        return "";
    std::string result = " -> ";
    if (contract.Effect == EffectKind::Async) {
        result += "async ";
    } else if (contract.Effect == EffectKind::Wait) {
        result += "wait ";
    }
    // Invalid source externs are retained as explicit ABI void in exported
    // interfaces so a source-less replay does not reintroduce omission.
    if (contract.ResultKind == ReturnResultKind::Unit) {
        if (isExtern)
            result += "void";
        else if (contract.HasExplicitResultType)
            result += "()";
    }
    else
        result += canonicalReturn;
    return result;
}

static std::string takePatternMorphologyPrefix(std::string &name) {
    if (name.rfind("&&", 0) == 0) {
        name.erase(0, 2);
        return "&&";
    }
    if (!name.empty()) {
        char c = name[0];
        if (c == '&' || c == '^' || c == '~' || c == '*') {
            name.erase(0, 1);
            return std::string(1, c);
        }
    }
    return "";
}

static std::string patternMorphologyPrefix(const MatchArm::Pattern *pat) {
    if (!pat) return "";
    switch (pat->Permission.Morphology) {
    case BindingMorphology::Raw:
        return "*";
    case BindingMorphology::Unique:
        return "^";
    case BindingMorphology::Shared:
        return "~";
    case BindingMorphology::Reference:
        return "&";
    case BindingMorphology::None:
        break;
    }
    return pat->IsReference ? "&" : "";
}

static std::string reconstructPatternHead(const MatchArm::Pattern *pat,
                                          bool includeSoulSuffix) {
    std::string name = pat ? pat->Name : "";
    std::string prefix = takePatternMorphologyPrefix(name);
    if (prefix.empty()) {
        prefix = patternMorphologyPrefix(pat);
    }

    if (!prefix.empty() && pat) {
        if (pat->Permission.IdentityRebindable) {
            prefix += "#";
        } else if (pat->Permission.IdentityBlocked) {
            prefix += "$";
        }
    }

    std::string result = prefix + name;
    if (includeSoulSuffix && pat) {
        if (pat->IsValueMutable || pat->Permission.SoulWritable) {
            result += "#";
        } else if (pat->IsValueBlocked || pat->Permission.SoulBlocked) {
            result += "$";
        }
    }
    return result;
}

static bool evaluateConstExpr(const Expr *expr, uint64_t &outValue) {
    if (!expr) return false;
    if (auto num = dynamic_cast<const NumberExpr *>(expr)) {
        outValue = num->Value;
        return true;
    }
    if (auto var = dynamic_cast<const VariableExpr *>(expr)) {
        if (var->HasConstantValue) {
            outValue = var->ConstantValue;
            return true;
        }
    }
    if (auto bin = dynamic_cast<const BinaryExpr *>(expr)) {
        uint64_t lhs = 0, rhs = 0;
        if (evaluateConstExpr(bin->LHS.get(), lhs) && evaluateConstExpr(bin->RHS.get(), rhs)) {
            if (bin->Op == "+") { outValue = lhs + rhs; return true; }
            if (bin->Op == "-") { outValue = lhs - rhs; return true; }
            if (bin->Op == "*") { outValue = lhs * rhs; return true; }
            if (bin->Op == "/") { if (rhs != 0) { outValue = lhs / rhs; return true; } }
            if (bin->Op == "%") { if (rhs != 0) { outValue = lhs % rhs; return true; } }
            if (bin->Op == "&&") { outValue = lhs && rhs; return true; }
            if (bin->Op == "||") { outValue = lhs || rhs; return true; }
            if (bin->Op == "==") { outValue = (lhs == rhs); return true; }
            if (bin->Op == "!=") { outValue = (lhs != rhs); return true; }
            if (bin->Op == "<") { outValue = (lhs < rhs); return true; }
            if (bin->Op == ">") { outValue = (lhs > rhs); return true; }
            if (bin->Op == "<=") { outValue = (lhs <= rhs); return true; }
            if (bin->Op == ">=") { outValue = (lhs >= rhs); return true; }
        }
    }
    if (auto un = dynamic_cast<const UnaryExpr *>(expr)) {
        uint64_t rhs = 0;
        if (evaluateConstExpr(un->RHS.get(), rhs)) {
            if (un->Op == TokenType::Minus) { outValue = -rhs; return true; }
            if (un->Op == TokenType::Bang) { outValue = !rhs; return true; }
            if (un->Op == TokenType::Tilde) { outValue = ~rhs; return true; }
        }
    }
    return false;
}

void TKIExporter::indent() {
    for (int i = 0; i < m_Indent; ++i) {
        m_OS << "  ";
    }
}

void TKIExporter::write(const std::string &str) {
    m_OS << str;
}

void TKIExporter::writeln(const std::string &str) {
    if (!str.empty()) {
        indent();
        m_OS << str;
    }
    m_OS << "\n";
}

void TKIExporter::exportModule(const Module &module) {
    // 0. Export Metadata Headers
    std::string sourceHash = "";
    if (!module.SourcePath.empty()) {
        std::ifstream ifs(module.SourcePath, std::ios::binary);
        if (ifs) {
            std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
            sourceHash = calculateFNV1a(content);
        }
    }
    writeln("// @meta compiler_version: " + std::string(TOKA_COMPILER_INTERFACE_VERSION));
    writeln("// @meta format_version: " + std::string(TOKA_INTERFACE_FORMAT_VERSION));
    writeln("// @meta target_triple: " + Parser::TargetTriple);
    writeln("// @meta source_hash: " + sourceHash);
    writeln("// @meta source_path: " + PathUtils::canonicalize(module.SourcePath));
    const std::string logicalPath = module.ShadowCoordinateKnown
        ? module.ShadowLogicalModulePath
        : "unbound";
    const std::string resolverDigest = module.ShadowCoordinateKnown
        ? calculateFNV1a("toka-tki-v2|" + module.ShadowCrateId + "|" +
                         module.ShadowLogicalModulePath)
        : "unbound";
    writeln("// @meta identity_schema_version: 2");
    writeln("// @meta logical_module_path: " + logicalPath);
    writeln("// @meta resolver_binding_digest: " + resolverDigest);
    for (const auto &fact : module.InterfaceV2Facts)
        writeln("// @tki v2 " + fact);
    writeln();

    exportDeclarations(module);
}

void TKIExporter::exportSemanticReplaySurface(const Module &module) {
    exportDeclarations(module);
}

void TKIExporter::exportDeclarations(const Module &module) {
    // 1. Export Imports
    for (const auto &decl : module.Imports) {
        if (!decl->IsImplicit) {
            exportImport(*decl);
        }
    }
    if (!module.Imports.empty()) writeln();

    // 2. Export Type Aliases
    for (const auto &decl : module.TypeAliases) {
        exportTypeAlias(*decl);
    }
    if (!module.TypeAliases.empty()) writeln();

    // 3. Export Shapes
    for (const auto &decl : module.Shapes) {
        exportShape(*decl);
    }
    if (!module.Shapes.empty()) writeln();

    // 4. Export Traits
    for (const auto &decl : module.Traits) {
        exportTrait(*decl);
    }
    if (!module.Traits.empty()) writeln();

    // 5. Export Impls
    for (const auto &decl : module.Impls) {
        exportImpl(*decl);
    }
    if (!module.Impls.empty()) writeln();

    // 6. Export Functions
    for (const auto &decl : module.Functions) {
        exportFunction(*decl);
    }
    if (!module.Functions.empty()) writeln();

    // 7. Export Externs
    for (const auto &decl : module.Externs) {
        exportExtern(*decl);
    }
    if (!module.Externs.empty()) writeln();

    // 8. Export Globals
    for (const auto &stmt : module.Globals) {
        exportGlobal(*stmt);
    }
}

void TKIExporter::exportImport(const ImportDecl &decl) {
    indent();
    if (decl.IsPub) m_OS << "pub ";
    m_OS << "import " << decl.PhysicalPath;
    if (!decl.Alias.empty()) {
        m_OS << " as " << decl.Alias;
    }
    if (!decl.Items.empty()) {
        if (decl.Items.size() == 1 && decl.Items[0].Symbol == "*") {
            m_OS << " :: *";
        } else {
            m_OS << " :: {";
            for (size_t i = 0; i < decl.Items.size(); ++i) {
                if (i > 0) m_OS << ", ";
                m_OS << decl.Items[i].Symbol;
                if (!decl.Items[i].Alias.empty()) {
                    m_OS << " as " << decl.Items[i].Alias;
                }
            }
            m_OS << "}";
        }
    }
    m_OS << "\n";
}

void TKIExporter::exportTypeAlias(const TypeAliasDecl &decl) {
    indent();
    if (decl.IsPub) m_OS << "pub ";
    m_OS << (decl.IsStrong ? "type " : "alias ") << decl.Name;
    printGenericParams(decl.GenericParams);
    m_OS << " = " << exportTypeSyntax(decl.TargetTypeSyntax, decl.TargetType);
    m_OS << "\n";
}

void TKIExporter::exportShape(const ShapeDecl &decl) {
    if (decl.IsCompilerSynthesized) return;
    indent();
    if (decl.IsPub) m_OS << "pub ";
    m_OS << "shape " << decl.Name;
    printGenericParams(decl.GenericParams);

    if (decl.Kind == ShapeKind::Array) {
        const auto &member = decl.Members[0];
        m_OS << " = [" << exportTypeSyntax(member.TypeSyntax, member.Type)
             << "; " << decl.ArraySize << "]\n";
        return;
    }

    m_OS << "(";
    if (decl.Kind == ShapeKind::Struct) {
        for (size_t i = 0; i < decl.Members.size(); ++i) {
            if (i > 0) m_OS << ", ";
            const auto &m = decl.Members[i];
            std::string varStr = reconstructVar(
                m.Name, exportTypeSyntax(m.TypeSyntax, m.Type),
                m.IsRawPointer, m.IsUnique, m.IsShared, m.IsReference,
                m.IsPointerNullable, m.IsRebindable, m.IsRebindBlocked,
                m.IsExplicitBound, m.IsMorphicExempt, false,
                m.IsValueMutable, m.IsValueNullable, m.IsValueBlocked
            );
            m_OS << varStr << ": " << exportBindingType(
                exportTypeSyntax(m.TypeSyntax, m.Type));
            if (m.DefaultValue) {
                m_OS << " = ";
                exportExpr(m.DefaultValue.get());
            }
        }
    } else if (decl.Kind == ShapeKind::Tuple) {
        for (size_t i = 0; i < decl.Members.size(); ++i) {
            if (i > 0) m_OS << ", ";
            const auto &member = decl.Members[i];
            m_OS << exportTypeSyntax(member.TypeSyntax, member.Type);
        }
    } else if (decl.Kind == ShapeKind::Enum) {
        for (size_t i = 0; i < decl.Members.size(); ++i) {
            if (i > 0) m_OS << " | ";
            const auto &m = decl.Members[i];
            m_OS << m.Name;
            if (m.SubKind == ShapeKind::Tuple) {
                m_OS << "(";
                for (size_t j = 0; j < m.SubMembers.size(); ++j) {
                    if (j > 0) m_OS << ", ";
                    const auto &member = m.SubMembers[j];
                    m_OS << exportTypeSyntax(member.TypeSyntax, member.Type);
                }
                m_OS << ")";
            }
            if (m.TagValue != -1) {
                m_OS << " = " << m.TagValue;
            }
        }
    } else if (decl.Kind == ShapeKind::Union) {
        for (size_t i = 0; i < decl.Members.size(); ++i) {
            if (i > 0) m_OS << " | ";
            const auto &m = decl.Members[i];
            m_OS << "as ";
            if (!m.Name.empty() && m.Name != std::to_string(i)) {
                m_OS << m.Name << ": ";
            }
            m_OS << exportTypeSyntax(m.TypeSyntax, m.Type);
        }
    }
    m_OS << ")\n";
}

void TKIExporter::exportTrait(const TraitDecl &decl) {
    indent();
    if (decl.IsPub) m_OS << "pub ";
    m_OS << "trait @" << decl.Name;
    printGenericParams(decl.GenericParams);
    bool multilineHeader = false;
    if (!decl.SelfTraitBounds.empty()) {
        multilineHeader = true;
        m_OS << "\n";
        m_Indent++;
        indent();
        m_OS << "where:\n";
        m_Indent++;
        indent();
        m_OS << "Self: ";
        if (decl.SelfTraitBounds.size() == 1) {
            m_OS << "@" << decl.SelfTraitBounds[0] << "\n";
        } else {
            m_OS << "@{";
            for (size_t i = 0; i < decl.SelfTraitBounds.size(); ++i) {
                if (i > 0) m_OS << ", ";
                m_OS << decl.SelfTraitBounds[i];
            }
            m_OS << "}\n";
        }
        m_Indent -= 2;
        indent();
    }
    m_OS << (multilineHeader ? "{\n" : " {\n");
    m_Indent++;
    for (const auto &assoc : decl.AssociatedTypes) {
        indent();
        if (assoc.IsPer) m_OS << "per ";
        m_OS << "type " << assoc.Name << "\n";
    }
    for (const auto &method : decl.Methods) {
        exportFunction(*method, /*forceKeepBody=*/false);
    }
    m_Indent--;
    writeln("}");
}

void TKIExporter::exportImpl(const ImplDecl &decl) {
    indent();
    m_OS << "impl";
    printGenericParams(decl.GenericParams);
    m_OS << " ";
    if (!decl.TraitName.empty()) {
        std::string cleanTrait = decl.TraitName;
        if (cleanTrait[0] == '@') cleanTrait = cleanTrait.substr(1);
        m_OS << exportTypeSyntax(decl.HeaderSyntax.Type, decl.TypeName)
             << "@" << cleanTrait << " {\n";
    } else {
        m_OS << exportTypeSyntax(decl.HeaderSyntax.Type, decl.TypeName)
             << " {\n";
    }
    m_Indent++;

    // Associated types
    for (const auto &assoc : decl.AssociatedTypes) {
        indent();
        if (assoc.IsPer) m_OS << "per ";
        m_OS << "type " << assoc.Name << " = "
             << exportTypeSyntax(assoc.TypeSyntax, assoc.Type) << "\n";
    }

    // Encap entries
    for (const auto &entry : decl.EncapEntries) {
        indent();
        m_OS << "pub ";
        for (size_t i = 0; i < entry.Fields.size(); ++i) {
            if (i > 0) m_OS << ", ";
            m_OS << entry.Fields[i];
        }
        m_OS << "\n";
    }

    // Methods
    for (const auto &method : decl.Methods) {
        exportFunction(*method, /*forceKeepBody=*/!decl.GenericParams.empty());
    }
    m_Indent--;
    writeln("}");
}

void TKIExporter::exportFunction(const FunctionDecl &decl, bool forceKeepBody) {
    indent();
    if (decl.IsPub) m_OS << "pub ";
    m_OS << "fn " << decl.Name;
    printGenericParams(decl.GenericParams);
    m_OS << "(";
    for (size_t i = 0; i < decl.Args.size(); ++i) {
        if (i > 0) m_OS << ", ";
        printArg(decl.Args[i]);
    }
    if (decl.IsVariadic) {
        if (!decl.Args.empty()) m_OS << ", ";
        m_OS << "...";
    }
    m_OS << ")";

    m_OS << exportReturnType(decl.ReturnContract, /*isExtern=*/false);

    std::vector<std::string> lifeDependencies;
    std::map<std::string, std::vector<std::string>> memberDependencies;
    decl.ReturnContract.deriveLegacyDependencies(lifeDependencies,
                                                 memberDependencies);
    if (decl.ReturnContract.Routes.empty() &&
        (!decl.LifeDependencies.empty() || !decl.MemberDependencies.empty())) {
        lifeDependencies = decl.LifeDependencies;
        memberDependencies = decl.MemberDependencies;
    }
    bool hasDottedLifeDependency = false;
    for (const auto &dep : lifeDependencies) {
        if (dep.find('.') != std::string::npos) {
            hasDottedLifeDependency = true;
            break;
        }
    }
    bool useEffectsBlock =
        !memberDependencies.empty() || hasDottedLifeDependency;

    if (!lifeDependencies.empty() && !useEffectsBlock) {
        m_OS << " <- ";
        for (size_t i = 0; i < lifeDependencies.size(); ++i) {
            if (i > 0) m_OS << " | ";
            m_OS << lifeDependencies[i];
        }
    }

    if (useEffectsBlock) {
        m_OS << "\n";
        m_Indent++;
        writeln("effects:");
        m_Indent++;
        if (!lifeDependencies.empty()) {
            indent();
            m_OS << "return <- ";
            for (size_t i = 0; i < lifeDependencies.size(); ++i) {
                if (i > 0) m_OS << " | ";
                m_OS << lifeDependencies[i];
            }
            m_OS << "\n";
        }
        for (const auto &pair : memberDependencies) {
            indent();
            if (pair.first.empty()) {
                m_OS << "return <- ";
            } else {
                m_OS << "return." << pair.first << " <- ";
            }
            for (size_t i = 0; i < pair.second.size(); ++i) {
                if (i > 0) m_OS << " | ";
                m_OS << pair.second[i];
            }
            m_OS << "\n";
        }
        m_Indent -= 2;
        indent();
    }

    if (decl.ResolvedOutcomeTransition) {
        m_OS << "\n";
        m_Indent++;
        writeln("outcomes:");
        m_Indent++;
        const auto &outcome = *decl.ResolvedOutcomeTransition;
        for (const auto &transition : outcome.Cases) {
            indent();
            m_OS << (transition.Variant ? transition.Variant->Name : "<invalid>")
                 << " => "
                 << (outcome.Subject ? outcome.Subject->Name : "<invalid>")
                 << ": "
                 << (transition.Post == OutcomePostState::Init ? "init"
                                                                : "uninit")
                 << "\n";
        }
        m_Indent -= 2;
        indent();
    }

    bool hasGenerics = !decl.GenericParams.empty();
    if (decl.Body &&
        (hasGenerics || forceKeepBody ||
         (m_RetainOutcomeBodies && decl.ResolvedOutcomeTransition))) {
        m_OS << " ";
        exportBlock(*decl.Body);
    } else {
        m_OS << "\n"; // Semicolon-free "toka "
    }
}

void TKIExporter::exportExtern(const ExternDecl &decl) {
    indent();
    m_OS << "extern fn " << decl.Name << "(";
    for (size_t i = 0; i < decl.Args.size(); ++i) {
        if (i > 0) m_OS << ", ";
        const auto &arg = decl.Args[i];
        std::string varStr = reconstructVar(
            arg.Name, exportTypeSyntax(arg.TypeSyntax, arg.Type),
            arg.IsRawPointer, arg.IsUnique, arg.IsShared, arg.IsReference,
            arg.IsPointerNullable, arg.IsRebindable, arg.IsRebindBlocked,
            false, arg.IsMorphicExempt, arg.IsCeded,
            arg.IsValueMutable, arg.IsValueNullable, arg.IsValueBlocked
        );
        m_OS << varStr << ": " << exportBindingType(
            exportTypeSyntax(arg.TypeSyntax, arg.Type));
        if (arg.DefaultValue) {
            m_OS << " = ";
            exportExpr(arg.DefaultValue.get());
        }
    }
    if (decl.IsVariadic) {
        if (!decl.Args.empty()) m_OS << ", ";
        m_OS << "...";
    }
    m_OS << ")";
    m_OS << exportReturnType(decl.ReturnContract, /*isExtern=*/true);
    m_OS << "\n";
}

void TKIExporter::exportGlobal(const Stmt &stmt) {
    if (auto decl = dynamic_cast<const VariableDecl *>(&stmt)) {
        if (!decl->IsPub || !decl->IsConst) return;
        indent();
        m_OS << "pub const ";
        std::string varStr = reconstructVar(
            decl->Name, exportTypeSyntax(decl->DeclaredTypeSyntax, decl->TypeName),
            decl->IsRawPointer, decl->IsUnique, decl->IsShared, decl->IsReference,
            decl->IsPointerNullable, decl->IsRebindable, decl->IsRebindBlocked,
            false, decl->IsMorphicExempt, false,
            decl->IsValueMutable, decl->IsValueNullable, decl->IsValueBlocked
        );
        m_OS << varStr;
        if (!decl->TypeName.empty() &&
            exportTypeSyntax(decl->DeclaredTypeSyntax, decl->TypeName)
                .rfind("__Toka_Anon_Rec_", 0) != 0) {
            m_OS << ": " << exportBindingType(
                exportTypeSyntax(decl->DeclaredTypeSyntax, decl->TypeName));
        }
        m_OS << " = ";
        uint64_t val = 0;
        if (evaluateConstExpr(decl->Init.get(), val)) {
            m_OS << val;
        } else {
            exportExpr(decl->Init.get());
        }
        m_OS << "\n";
    }
}

void TKIExporter::printGenericParams(const std::vector<GenericParam> &params) {
    if (params.empty()) return;
    m_OS << "<";
    for (size_t i = 0; i < params.size(); ++i) {
        if (i > 0) m_OS << ", ";
        const auto &gp = params[i];
        if (gp.IsConst) {
            if (gp.Type.empty()) {
                m_OS << "const " << gp.Name;
            } else {
                m_OS << gp.Name << ": "
                     << exportTypeSyntax(gp.TypeSyntax, gp.Type);
            }
        } else {
            m_OS << gp.Name;
            if (!gp.TraitBounds.empty()) {
                if (gp.TraitBounds.size() == 1) {
                    m_OS << ": @" << gp.TraitBounds[0];
                } else {
                    m_OS << ": @{";
                    for (size_t j = 0; j < gp.TraitBounds.size(); ++j) {
                        if (j > 0) m_OS << ", ";
                        m_OS << gp.TraitBounds[j];
                    }
                    m_OS << "}";
                }
            }
        }
    }
    m_OS << ">";
}

void TKIExporter::printArg(const FunctionDecl::Arg &arg) {
    if (arg.IsInit)
        m_OS << "init ";
    const std::string type = exportTypeSyntax(arg.TypeSyntax, arg.Type);
    bool nameHasMorphicPrefix = !arg.Name.empty() && arg.Name[0] == '\'';
    bool keepTypeSideCede =
        arg.IsCeded && hasTypeSideMorphicPrefix(type) &&
        !nameHasMorphicPrefix && !arg.IsMorphicExempt;
    std::string varStr = reconstructVar(
        arg.Name, keepTypeSideCede ? exportBindingType(type) : type,
        arg.IsRawPointer, arg.IsUnique, arg.IsShared, arg.IsReference,
        arg.IsPointerNullable, arg.IsRebindable, arg.IsRebindBlocked,
        false, arg.IsMorphicExempt, keepTypeSideCede ? false : arg.IsCeded,
        arg.IsValueMutable, arg.IsValueNullable, arg.IsValueBlocked
    );
    if (keepTypeSideCede) {
        m_OS << varStr << ": cede " << toka::Type::stripPrefixes(type);
    } else {
        m_OS << varStr << ": " << exportBindingType(type);
    }
    if (arg.DefaultValue) {
        m_OS << " = ";
        exportExpr(arg.DefaultValue.get());
    }
}

void TKIExporter::exportExpr(const Expr *expr, bool stripHats) {
    if (!expr) return;

    if (expr->HasParens) m_OS << "(";

    if (auto num = dynamic_cast<const NumberExpr *>(expr)) {
        m_OS << num->Value;
    } else if (auto fl = dynamic_cast<const FloatExpr *>(expr)) {
        m_OS << fl->Value;
    } else if (auto bl = dynamic_cast<const BoolExpr *>(expr)) {
        m_OS << (bl->Value ? "true" : "false");
    } else if (dynamic_cast<const NullExpr *>(expr)) {
        m_OS << "null";
    } else if (dynamic_cast<const NoneExpr *>(expr)) {
        m_OS << "none";
    } else if (dynamic_cast<const UnsetExpr *>(expr)) {
        m_OS << "uninit";
    } else if (auto sz = dynamic_cast<const SizeOfExpr *>(expr)) {
        m_OS << "sizeof(" << exportTypeSyntax(sz->TypeSyntax, sz->TypeStr)
             << ")";
    } else if (auto var = dynamic_cast<const VariableExpr *>(expr)) {
        if (!stripHats) {
            if (var->IsUnique) m_OS << "^";
            else if (var->IsShared) m_OS << "~";
            else if (var->IsRawPointer) m_OS << "*";
        }
        m_OS << var->Name;
        if (var->IsValueMutable) m_OS << "#";
        else if (var->IsValueNullable) m_OS << "?";
        else if (var->IsValueBlocked) m_OS << "$";
    } else if (auto str = dynamic_cast<const StringExpr *>(expr)) {
        m_OS << "c\"" << escapeLiteralContent(str->Value) << "\"";
    } else if (auto vstr = dynamic_cast<const ViewStringExpr *>(expr)) {
        m_OS << "\"" << escapeLiteralContent(vstr->Value) << "\"";
    } else if (auto ch = dynamic_cast<const CharLiteralExpr *>(expr)) {
        m_OS << "'" << escapeCharLiteral(ch->Value) << "'";
    } else if (auto deref = dynamic_cast<const DereferenceExpr *>(expr)) {
        exportExpr(deref->Expression.get(), /*stripHats=*/true);
    } else if (auto bin = dynamic_cast<const BinaryExpr *>(expr)) {
        if (bin->IsInitialization)
            m_OS << "init ";
        exportExpr(bin->LHS.get());
        m_OS << " " << bin->Op << " ";
        exportExpr(bin->RHS.get());
    } else if (auto un = dynamic_cast<const UnaryExpr *>(expr)) {
        std::string opStr = "";
        switch (un->Op) {
            case TokenType::Caret: opStr = "^"; break;
            case TokenType::Tilde: opStr = "~"; break;
            case TokenType::Star: opStr = "*"; break;
            case TokenType::Ampersand: opStr = "&"; break;
            case TokenType::Plus: opStr = "+"; break;
            case TokenType::Minus: opStr = "-"; break;
            case TokenType::Bang: opStr = "!"; break;
            default: break;
        }
        m_OS << opStr;
        if (un->HasNull) m_OS << "?";
        if (un->IsRebindable) m_OS << "#";
        if (un->IsRebindBlocked) m_OS << "$";
        exportExpr(un->RHS.get());
    } else if (auto post = dynamic_cast<const PostfixExpr *>(expr)) {
        exportExpr(post->LHS.get());
        if (post->Op == TokenType::PlusPlus) m_OS << "++";
        else if (post->Op == TokenType::MinusMinus) m_OS << "--";
        else if (post->Op == TokenType::TokenWrite) m_OS << "#";
        else if (post->Op == TokenType::TokenNull) m_OS << "?";
        else if (post->Op == TokenType::TokenNone) m_OS << "$";
        else if (post->Op == TokenType::DoubleQuestion) m_OS << "??";
        else if (post->Op == TokenType::Bang) m_OS << "!";
    } else if (auto prop = dynamic_cast<const UnwrapPropagationExpr *>(expr)) {
        exportExpr(prop->Base.get());
        m_OS << "!";
    } else if (auto aw = dynamic_cast<const AwaitExpr *>(expr)) {
        exportExpr(aw->Expression.get());
        m_OS << ".await" << (aw->CatchesCancellation ? "?" : "");
    } else if (auto wa = dynamic_cast<const WaitExpr *>(expr)) {
        exportExpr(wa->Expression.get());
        m_OS << ".wait";
    } else if (auto st = dynamic_cast<const StartExpr *>(expr)) {
        exportExpr(st->Expression.get());
        m_OS << ".start";
    } else if (auto cast = dynamic_cast<const CastExpr *>(expr)) {
        exportExpr(cast->Expression.get());
        if (cast->Kind == CastKind::Ascription) {
            m_OS << ":"
                 << exportTypeSyntax(cast->TargetTypeSyntax, cast->TargetType);
        } else if (cast->Kind == CastKind::Conversion) {
            m_OS << " as "
                 << exportTypeSyntax(cast->TargetTypeSyntax, cast->TargetType);
        }
    } else if (auto addr = dynamic_cast<const AddressOfExpr *>(expr)) {
        m_OS << "&";
        exportExpr(addr->Expression.get());
    } else if (auto mem = dynamic_cast<const MemberExpr *>(expr)) {
        exportExpr(mem->Object.get());
        m_OS << "." << mem->Member;
    } else if (auto idx = dynamic_cast<const ArrayIndexExpr *>(expr)) {
        exportExpr(idx->Array.get());
        m_OS << "[";
        for (size_t i = 0; i < idx->Indices.size(); ++i) {
            if (i > 0) m_OS << ", ";
            exportExpr(idx->Indices[i].get());
        }
        m_OS << "]";
    } else if (auto arr = dynamic_cast<const ArrayExpr *>(expr)) {
        m_OS << "[";
        for (size_t i = 0; i < arr->Elements.size(); ++i) {
            if (i > 0) m_OS << ", ";
            exportExpr(arr->Elements[i].get());
        }
        m_OS << "]";
    } else if (auto rep = dynamic_cast<const RepeatedArrayExpr *>(expr)) {
        m_OS << "[";
        exportExpr(rep->Value.get());
        m_OS << "; ";
        exportExpr(rep->Count.get());
        m_OS << "]";
    } else if (auto uns = dynamic_cast<const UnsafeExpr *>(expr)) {
        m_OS << "unsafe ";
        exportExpr(uns->Expression.get());
    } else if (auto alloc = dynamic_cast<const AllocExpr *>(expr)) {
        m_OS << "alloc ";
        if (alloc->IsArray) {
            m_OS << "[";
            if (alloc->ArraySize) exportExpr(alloc->ArraySize.get());
            m_OS << "]";
        }
        m_OS << exportTypeSyntax(alloc->TypeSyntax, alloc->TypeName);
        if (alloc->Initializer) {
            m_OS << "(";
            if (alloc->InitializerIsArgumentList) {
              if (auto call = dynamic_cast<const CallExpr *>(alloc->Initializer.get())) {
                for (size_t i = 0; i < call->Args.size(); ++i) {
                    if (i > 0) m_OS << ", ";
                    if (call->isInitArgument(i)) m_OS << "init ";
                    exportExpr(call->Args[i].get());
                }
              } else if (auto init = dynamic_cast<const InitStructExpr *>(
                             alloc->Initializer.get())) {
                for (size_t i = 0; i < init->Members.size(); ++i) {
                  if (i > 0) m_OS << ", ";
                  if (init->Members[i].first != "..")
                    m_OS << init->Members[i].first << " = ";
                  exportExpr(init->Members[i].second.get());
                }
              } else {
                exportExpr(alloc->Initializer.get());
              }
            } else {
                exportExpr(alloc->Initializer.get());
            }
            m_OS << ")";
        }
    } else if (auto init = dynamic_cast<const InitStructExpr *>(expr)) {
        m_OS << init->ShapeName << "(";
        for (size_t i = 0; i < init->Members.size(); ++i) {
            if (i > 0) m_OS << ", ";
            m_OS << init->Members[i].first << " = ";
            exportExpr(init->Members[i].second.get());
        }
        m_OS << ")";
    } else if (auto anon = dynamic_cast<const AnonymousRecordExpr *>(expr)) {
        m_OS << "(";
        for (size_t i = 0; i < anon->Fields.size(); ++i) {
            if (i > 0) m_OS << ", ";
            m_OS << anon->Fields[i].first << " = ";
            exportExpr(anon->Fields[i].second.get());
        }
        m_OS << ")";
    } else if (auto call = dynamic_cast<const CallExpr *>(expr)) {
        m_OS << call->Callee;
        if (call->CallableReceiver == CallableReceiverMode::Mutable)
            m_OS << "#";
        if (!call->GenericArgs.empty()) {
            m_OS << "<";
            for (size_t i = 0; i < call->GenericArgs.size(); ++i) {
                if (i > 0) m_OS << ", ";
                m_OS << (i < call->GenericArgSyntax.size()
                             ? call->GenericArgSyntax[i].toCanonicalString()
                             : call->GenericArgs[i]);
            }
            m_OS << ">";
        }
        m_OS << "(";
        for (size_t i = 0; i < call->Args.size(); ++i) {
            if (i > 0) m_OS << ", ";
            if (call->isInitArgument(i)) m_OS << "init ";
            exportExpr(call->Args[i].get());
        }
        m_OS << ")";
    } else if (auto mcall = dynamic_cast<const MethodCallExpr *>(expr)) {
        exportExpr(mcall->Object.get());
        m_OS << "." << mcall->Method << "(";
        for (size_t i = 0; i < mcall->Args.size(); ++i) {
            if (i > 0) m_OS << ", ";
            exportExpr(mcall->Args[i].get());
        }
        m_OS << ")";
    } else if (auto magic = dynamic_cast<const MagicExpr *>(expr)) {
        if (magic->Kind == TokenType::KwFile) m_OS << "__FILE__";
        else if (magic->Kind == TokenType::KwLine) m_OS << "__LINE__";
        else if (magic->Kind == TokenType::KwLoc) m_OS << "__LOC__";
    } else if (auto ne = dynamic_cast<const NewExpr *>(expr)) {
        m_OS << "new ";
        if (ne->ArraySize) {
            m_OS << "[";
            exportExpr(ne->ArraySize.get());
            m_OS << "]";
        }
        m_OS << exportTypeSyntax(ne->TypeSyntax, ne->Type);
        if (ne->Initializer) {
            m_OS << "(";
            exportExpr(ne->Initializer.get());
            m_OS << ")";
        }
    } else if (auto ae = dynamic_cast<const ArrayInitExpr *>(expr)) {
        m_OS << "new [";
        exportExpr(ae->ArraySize.get());
        m_OS << "]" << exportTypeSyntax(ae->TypeSyntax, ae->Type);
        if (ae->Initializer) {
            m_OS << "(";
            exportExpr(ae->Initializer.get());
            m_OS << ")";
        }
    } else if (auto pass = dynamic_cast<const PassExpr *>(expr)) {
        m_OS << "pass ";
        exportExpr(pass->Value.get());
    } else if (auto ce = dynamic_cast<const CedeExpr *>(expr)) {
        m_OS << "cede ";
        exportExpr(ce->Value.get());
    } else if (dynamic_cast<const ElisionExpr *>(expr)) {
        m_OS << "..";
    } else if (auto sp = dynamic_cast<const SpreadExpr *>(expr)) {
        exportExpr(sp->Base.get());
        m_OS << ".*";
    } else if (auto cmpF = dynamic_cast<const ComptimeFieldExpr *>(expr)) {
        m_OS << cmpF->FieldName;
    } else if (auto cmpR = dynamic_cast<const ComptimeReflectExpr *>(expr)) {
        m_OS << exportTypeSyntax(cmpR->TypeSyntax, cmpR->ReflectedTypeStr);
    } else if (auto matchEx = dynamic_cast<const MatchExpr *>(expr)) {
        m_OS << "match ";
        exportExpr(matchEx->Target.get());
        m_OS << " {\n";
        m_Indent++;
        for (const auto &arm : matchEx->Arms) {
            indent();
            exportPattern(arm->Pat.get());
            if (arm->Guard) {
                m_OS << " if ";
                exportExpr(arm->Guard.get());
            }
            m_OS << " => ";
            exportStmt(arm->Body.get());
            m_OS << "\n";
        }
        m_Indent--;
        indent();
        m_OS << "}";
    } else if (auto br = dynamic_cast<const BreakExpr *>(expr)) {
        m_OS << "break";
        if (!br->TargetLabel.empty()) m_OS << " " << br->TargetLabel;
        if (br->Value) {
            m_OS << " ";
            exportExpr(br->Value.get());
        }
    } else if (auto co = dynamic_cast<const ContinueExpr *>(expr)) {
        m_OS << "continue";
        if (!co->TargetLabel.empty()) m_OS << " " << co->TargetLabel;
    } else if (auto ifEx = dynamic_cast<const IfExpr *>(expr)) {
        if (ifEx->IsComptime) m_OS << "comptime ";
        m_OS << "if ";
        exportExpr(ifEx->Condition.get());
        m_OS << " ";
        exportStmt(ifEx->Then.get());
        if (ifEx->Else) {
            m_OS << " else ";
            exportStmt(ifEx->Else.get());
        }
    } else if (auto gEx = dynamic_cast<const GuardExpr *>(expr)) {
        m_OS << "guard ";
        exportExpr(gEx->Condition.get());
        m_OS << " else ";
        exportStmt(gEx->Else.get());
    } else if (auto loopEx = dynamic_cast<const LoopExpr *>(expr)) {
        m_OS << "loop ";
        if (loopEx->Condition) {
            exportExpr(loopEx->Condition.get());
            m_OS << " ";
        }
        exportStmt(loopEx->Body.get());
    } else if (auto forEx = dynamic_cast<const ForExpr *>(expr)) {
        m_OS << "for ";
        if (forEx->IsMutable) m_OS << "mut ";
        else m_OS << "auto ";
        m_OS << forEx->MorphologyPrefix << forEx->VarName << " in ";
        exportExpr(forEx->Collection.get());
        m_OS << " ";
        exportStmt(forEx->Body.get());
        if (forEx->ElseBody) {
            m_OS << " else ";
            exportStmt(forEx->ElseBody.get());
        }
    } else if (auto clo = dynamic_cast<const ClosureExpr *>(expr)) {
        m_OS << "{\n";
        m_Indent++;
        bool hasHeader = !clo->ExplicitCaptures.empty() || clo->HasExplicitArgs;
        if (hasHeader) {
            indent();
        }
        if (!clo->ExplicitCaptures.empty()) {
            m_OS << "[";
            for (size_t i = 0; i < clo->ExplicitCaptures.size(); ++i) {
                if (i > 0) m_OS << ", ";
                const auto &cap = clo->ExplicitCaptures[i];
                if (cap.Mode == CaptureMode::ExplicitCede) m_OS << "cede ";
                else if (cap.Mode == CaptureMode::ExplicitCopy) m_OS << "copy ";
                m_OS << cap.Name;
            }
            m_OS << "] ";
        }
        if (clo->HasExplicitArgs) {
            for (size_t i = 0; i < clo->ArgNames.size(); ++i) {
                if (i > 0) m_OS << ", ";
                m_OS << clo->ArgNames[i];
            }
        }
        if (hasHeader) {
            m_OS << "=>\n";
        } else {
            m_OS << "\n";
        }
        if (clo->Body) {
            for (const auto &stmt : clo->Body->Statements) {
                exportStmt(stmt.get());
            }
        }
        m_Indent--;
        indent();
        m_OS << "}";
    }

    if (expr->HasParens) m_OS << ")";
}

void TKIExporter::exportStmt(const Stmt *stmt, bool indentStmt) {
    if (!stmt) return;

    if (auto decl = dynamic_cast<const VariableDecl *>(stmt)) {
        indent();
        if (decl->IsConst) {
            m_OS << "const ";
        } else {
            m_OS << "auto ";
        }
        std::string varStr = reconstructVar(
            decl->Name, decl->TypeName,
            decl->IsRawPointer, decl->IsUnique, decl->IsShared, decl->IsReference,
            decl->IsPointerNullable, decl->IsRebindable, decl->IsRebindBlocked,
            false, decl->IsMorphicExempt, false,
            decl->IsValueMutable, decl->IsValueNullable, decl->IsValueBlocked
        );
        m_OS << varStr;
        if (!decl->TypeName.empty()) {
            m_OS << ": " << exportBindingType(decl->TypeName);
        }
        if (decl->Init) {
            m_OS << " = ";
            exportExpr(decl->Init.get());
        }
        m_OS << "\n";
    } else if (auto dest = dynamic_cast<const DestructuringDecl *>(stmt)) {
        indent();
        m_OS << "auto ";
        if (!dest->TypeName.empty()) {
            m_OS << dest->TypeName;
        }
        m_OS << "(";
        for (size_t i = 0; i < dest->Variables.size(); ++i) {
            if (i > 0) m_OS << ", ";
            const auto &v = dest->Variables[i];
            if (v.IsWildcard) {
                m_OS << "_";
            } else {
                if (!v.FieldName.empty()) {
                    m_OS << v.FieldName << " = ";
                }
                m_OS << v.Name;
                if (v.IsValueMutable) m_OS << "#";
                else if (v.IsValueNullable) m_OS << "?";
                else if (v.IsValueBlocked) m_OS << "$";
            }
        }
        m_OS << ")";
        if (dest->Init) {
            m_OS << " = ";
            exportExpr(dest->Init.get());
        }
        m_OS << "\n";
    } else if (auto block = dynamic_cast<const BlockStmt *>(stmt)) {
        exportBlock(*block);
    } else if (auto initBlock = dynamic_cast<const InitBlockStmt *>(stmt)) {
        indent();
        m_OS << "init " << initBlock->PlaceName << " ";
        exportBlock(*initBlock->Body);
    } else if (auto ret = dynamic_cast<const ReturnStmt *>(stmt)) {
        indent();
        m_OS << "return";
        if (ret->ReturnValue) {
            m_OS << " ";
            exportExpr(ret->ReturnValue.get());
        }
        m_OS << "\n";
    } else if (auto exprSt = dynamic_cast<const ExprStmt *>(stmt)) {
        indent();
        exportExpr(exprSt->Expression.get());
        m_OS << "\n";
    } else if (auto del = dynamic_cast<const DeleteStmt *>(stmt)) {
        indent();
        m_OS << "delete ";
        exportExpr(del->Expression.get());
        m_OS << "\n";
    } else if (auto uns = dynamic_cast<const UnsafeStmt *>(stmt)) {
        indent();
        m_OS << "unsafe ";
        exportStmt(uns->Statement.get(), false);
    } else if (auto freeSt = dynamic_cast<const FreeStmt *>(stmt)) {
        indent();
        m_OS << "free ";
        exportExpr(freeSt->Expression.get());
        m_OS << "\n";
    } else if (dynamic_cast<const UnreachableStmt *>(stmt)) {
        writeln("unreachable");
    } else if (auto guardBind = dynamic_cast<const GuardBindStmt *>(stmt)) {
        indent();
        m_OS << "guard auto ";
        exportPattern(guardBind->Pat.get());
        m_OS << " = ";
        exportExpr(guardBind->Target.get());
        m_OS << " else ";
        exportStmt(guardBind->ElseBody.get(), false);
    }
}

void TKIExporter::exportBlock(const BlockStmt &block) {
    m_OS << "{\n";
    m_Indent++;
    for (const auto &stmt : block.Statements) {
        exportStmt(stmt.get());
    }
    m_Indent--;
    indent();
    m_OS << "}\n";
}

void TKIExporter::exportPattern(const MatchArm::Pattern *pat) {
    if (!pat) return;
    if (pat->HasAutoBinding)
        m_OS << "auto ";
    switch (pat->PatternKind) {
        case MatchArm::Pattern::Literal:
            m_OS << pat->LiteralVal;
            break;
        case MatchArm::Pattern::Variable:
            m_OS << reconstructPatternHead(pat, true);
            break;
        case MatchArm::Pattern::Wildcard:
            m_OS << "_";
            break;
        case MatchArm::Pattern::Elision:
            m_OS << "..";
            break;
        case MatchArm::Pattern::Decons: {
            m_OS << reconstructPatternHead(pat, false) << "(";
            for (size_t i = 0; i < pat->SubPatterns.size(); ++i) {
                if (i > 0) m_OS << ", ";
                if (pat->SubPatternNames.size() > i && !pat->SubPatternNames[i].empty()) {
                    m_OS << pat->SubPatternNames[i] << " = ";
                }
                exportPattern(pat->SubPatterns[i].get());
            }
            m_OS << ")";
            break;
        }
        case MatchArm::Pattern::Or: {
            for (size_t i = 0; i < pat->SubPatterns.size(); ++i) {
                if (i > 0) m_OS << " | ";
                exportPattern(pat->SubPatterns[i].get());
            }
            break;
        }
        case MatchArm::Pattern::Range: {
            if (pat->SubPatterns.size() == 2) {
                exportPattern(pat->SubPatterns[0].get());
                m_OS << (pat->IsInclusive ? " ..= " : " ..< ");
                exportPattern(pat->SubPatterns[1].get());
            }
            break;
        }
    }
}

} // namespace toka
