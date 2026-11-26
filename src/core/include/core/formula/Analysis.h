#pragma once

#include <core/formula/Evaluation.h>
#include <core/formula/Formula.h>
#include <core/Macro.h>
#include <core/Validation.h>

#include <QSet>

#include <map>
#include <unordered_map>
#include <unordered_set>

namespace qds::core {

struct MacroCall {
    MacroSignature signature;
    int argumentCount = 0;

    bool operator==(const MacroCall &other) const
    {
        return signature == other.signature && argumentCount == other.argumentCount;
    }
};

inline uint qHash(const MacroCall &call, uint seed = 0) noexcept
{
    return ::qHash(call.signature.nameKey, seed) ^ ::qHash(call.signature.parameterCount, seed)
        ^ ::qHash(call.signature.outputCount, seed) ^ ::qHash(call.argumentCount, seed);
}

struct ArrayElementAnalysis {
    QVector<std::size_t> indices;
    FormulaAnalysis formula;
};

struct ElementAnalysis {
    EntityId id = 0;
    FormulaAnalysis formula;
    std::vector<ArrayElementAnalysis> elementFormulas;
    std::unordered_set<EntityId> references;
    QSet<MacroCall> macroCalls;
};

struct MacroAnalysis {
    const MacroDefinition *definition = nullptr;
    FormulaAnalysis formula;
    std::vector<FormulaAnalysis> defaults;
    QSet<EntityId> references;
    QSet<MacroCall> calls;
    QVector<QSet<EntityId>> defaultReferences;
    QVector<QSet<MacroCall>> defaultCalls;
};

struct DependencyGraph {
    std::unordered_map<EntityId, QVector<EntityId>> edges;
};

struct ModelAnalysis {
    std::vector<ElementAnalysis> elements;
    std::map<MacroSignature, MacroAnalysis> macros;
    DependencyGraph dependencies;
    std::unordered_set<EntityId> structurallyUsed;
    QVector<ModelIssue> issues;
};

ModelAnalysis analyzeDocument(const Document &document);
MacroReader makeMacroReader(const ModelAnalysis &analysis);

} // namespace qds::core
