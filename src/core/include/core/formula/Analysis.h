#pragma once

#include <core/formula/Evaluation.h>
#include <core/formula/Formula.h>
#include <core/Macro.h>
#include <core/Validation.h>

#include <QHash>
#include <QSet>
#include <QVector>

#include <utility>

namespace qds::core {

class AnalysisBuilder;

class DependencyGraph
{
public:
    const QHash<EntityId, QVector<EntityId>> &edges() const { return m_edges; }

private:
    friend class AnalysisBuilder;
    QHash<EntityId, QVector<EntityId>> m_edges;
};

class AnalysisReport
{
public:
    const DependencyGraph &dependencies() const { return m_dependencies; }
    const QSet<EntityId> &structurallyUsed() const { return m_structurallyUsed; }
    const QVector<ModelIssue> &issues() const { return m_issues; }
    QSet<EntityId> references(EntityId dependent) const;

private:
    friend class AnalysisBuilder;
    AnalysisReport(DependencyGraph dependencies, QSet<EntityId> structurallyUsed, QVector<ModelIssue> issues)
        : m_dependencies(std::move(dependencies))
        , m_structurallyUsed(std::move(structurallyUsed))
        , m_issues(std::move(issues))
    {
    }

    DependencyGraph m_dependencies;
    QSet<EntityId> m_structurallyUsed;
    QVector<ModelIssue> m_issues;
};

AnalysisReport analyzeDocument(const Document &document);
MacroReader makeMacroReader(const Document &document);

} // namespace qds::core
