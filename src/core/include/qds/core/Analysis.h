#pragma once

#include <qds/core/Formula.h>
#include <qds/core/Validation.h>

#include <unordered_map>
#include <unordered_set>

namespace qds::core {

struct ArrayElementAnalysis {
    std::vector<std::size_t> indices;
    FormulaAnalysis formula;
};

struct ElementAnalysis {
    EntityId id = 0;
    FormulaAnalysis formula;
    std::vector<ArrayElementAnalysis> elementFormulas;
    std::unordered_set<EntityId> references;
};

struct DependencyGraph {
    std::unordered_map<EntityId, std::vector<EntityId>> edges;
};

struct ModelAnalysis {
    std::vector<ElementAnalysis> elements;
    DependencyGraph dependencies;
    std::unordered_set<EntityId> structurallyUsed;
    std::vector<ModelIssue> issues;
};

ModelAnalysis analyzeDocument(const Document &document);

} // namespace qds::core
