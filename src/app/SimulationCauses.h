#pragma once

#include <core/Document.h>
#include <core/formula/Analysis.h>

QVector<qds::core::EntityId> directSimulationCauses(const qds::core::Document &document,
    const qds::core::AnalysisReport &analysis, qds::core::EntityId dependent);
