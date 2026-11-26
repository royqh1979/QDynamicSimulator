#pragma once

#include <core/formula/Formula.h>
#include <core/Macro.h>
#include <core/Validation.h>

#include <unordered_map>
#include <unordered_set>

namespace qds::core {

struct ArrayElementAnalysis {
    QVector<std::size_t> indices;
    FormulaAnalysis formula;
};

struct ElementAnalysis {
    EntityId id = 0;
    FormulaAnalysis formula;
    std::vector<ArrayElementAnalysis> elementFormulas;
    std::unordered_set<EntityId> references;
};

struct MacroAnalysis {
    const MacroDefinition *definition = nullptr;
    FormulaAnalysis formula;
    std::vector<FormulaAnalysis> defaults;
};

struct DependencyGraph {
    std::unordered_map<EntityId, QVector<EntityId>> edges;
};

struct ModelAnalysis {
    std::vector<ElementAnalysis> elements;
    std::vector<MacroAnalysis> macros;
    DependencyGraph dependencies;
    std::unordered_set<EntityId> structurallyUsed;
    QVector<ModelIssue> issues;
};

ModelAnalysis analyzeDocument(const Document &document);

} // namespace qds::core
