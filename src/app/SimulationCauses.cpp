#include "SimulationCauses.h"

#include <algorithm>

using namespace qds::core;

QVector<EntityId> directSimulationCauses(const Document &document, const AnalysisReport &analysis, EntityId dependent)
{
    QSet<EntityId> unique;
    const DocumentElement *element = document.find(dependent);
    if (!element)
        return {};
    if (element->kind() == ElementKind::Stock) {
        for (const ModelElement &candidate : document.model().elements()) {
            if (candidate.kind() != ElementKind::Flow)
                continue;
            const FlowEndpoints endpoints = static_cast<const Flow &>(candidate).endpoints();
            if (endpoints.from == dependent || endpoints.to == dependent)
                unique.insert(candidate.id());
        }
    } else {
        unique = analysis.references(dependent);
    }
    unique.remove(dependent);
    QVector<EntityId> result(unique.begin(), unique.end());
    std::sort(result.begin(), result.end());
    return result;
}
