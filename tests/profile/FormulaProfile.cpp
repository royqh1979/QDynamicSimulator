#include <qds/core/Analysis.h>
#include <qds/core/Evaluation.h>
#include <qds/core/Formula.h>

#include <cmath>
#include <unordered_map>

#include <QElapsedTimer>
#include <QString>
#include <QTextStream>

using namespace qds::core;

int main(int argc, char **argv)
{
    const int count = argc > 1 ? QString(argv[1]).toInt() : 1000;
    if (count <= 0)
        return 2;
    QElapsedTimer timer;
    timer.start();
    std::size_t totalNodes = 0;
    for (int index = 0; index < count; ++index) {
        const std::string equation = "a" + std::to_string(index) + "*b" + std::to_string(index)
            + "+ABS(c" + std::to_string(index) + ")-IF d" + std::to_string(index)
            + " THEN e" + std::to_string(index) + " ELSE f" + std::to_string(index);
        FormulaAnalysis analysis = analyzeFormula(equation);
        if (!analysis.ast || !analysis.issues.empty()) {
            QTextStream(stderr) << "Formula analysis failed at index " << index << "\n";
            return 1;
        }
        totalNodes += formulaNodeCount(analysis.ast.get());
    }
    const qint64 elapsedNs = timer.nsecsElapsed();
    if (totalNodes == 0)
        return 3;

    Document document;
    document.createVariable(ElementKind::Stock);
    std::string previousName = "Stock1";
    for (int index = 0; index < count; ++index) {
        const EntityId id = document.createVariable(ElementKind::Auxiliary);
        const std::string currentName = "Equation" + std::to_string(index);
        if (!document.rename(id, currentName))
            return 4;
        for (const char prefix : std::string("bcdef")) {
            const EntityId input = document.createVariable(ElementKind::Auxiliary);
            if (!document.rename(input, prefix + std::to_string(index)))
                return 4;
        }
        const std::string equation = previousName + "*b" + std::to_string(index)
            + "+ABS(c" + std::to_string(index) + ")-IF d" + std::to_string(index)
            + " THEN e" + std::to_string(index) + " ELSE f" + std::to_string(index);
        document.setEquation(id, equation);
        previousName = currentName;
    }
    timer.restart();
    ModelAnalysis modelAnalysis = analyzeDocument(document);
    const qint64 documentNs = timer.nsecsElapsed();
    std::size_t dependencyCount = 0;
    for (const auto &entry : modelAnalysis.dependencies.edges)
        dependencyCount += entry.second.size();
    std::size_t referenceCount = 0;
    for (const ElementAnalysis &entry : modelAnalysis.elements)
        referenceCount += entry.references.size();
    for (const ModelIssue &issue : modelAnalysis.issues) {
        if (issue.severity == IssueSeverity::Error) {
            QTextStream(stderr) << "Model analysis failed: " << QString::fromStdString(issue.message) << "\n";
            return 5;
        }
    }
    if (referenceCount != static_cast<std::size_t>(count) * 6 || dependencyCount != referenceCount)
        return 6;

    std::unordered_map<EntityId, double> values;
    for (const ModelElement &entry : document.model().elements())
        values[entry.id()] = static_cast<double>(entry.id() % 17) - 8.0;
    EvaluationContext context;
    context.time = 4.5;
    context.dt = 0.25;
    context.readVariable = [&values](EntityId id) -> std::optional<double> {
        const auto found = values.find(id);
        return found == values.end() ? std::nullopt : std::optional<double>(found->second);
    };
    timer.restart();
    double evaluationChecksum = 0.0;
    int evaluationCount = 0;
    for (const ElementAnalysis &entry : modelAnalysis.elements) {
        if (!entry.formula.ast)
            continue;
        const EvaluationResult evaluated = evaluateFormula(entry.formula.ast.get(), context);
        if (!evaluated.value) {
            QTextStream(stderr) << "Formula evaluation failed: " << QString::fromStdString(evaluated.error) << "\n";
            return 7;
        }
        evaluationChecksum += *evaluated.value;
        ++evaluationCount;
    }
    const qint64 evaluationNs = timer.nsecsElapsed();
    if (evaluationCount != count || !std::isfinite(evaluationChecksum))
        return 8;
    QTextStream(stdout) << "operation,count,total_ms,per_formula_us\n"
                        << "formula_analyze," << count << "," << elapsedNs / 1e6 << ","
                        << elapsedNs / 1000.0 / count << "\n"
                        << "formula_nodes," << totalNodes << ",\n"
                        << "document_analysis," << count << "," << documentNs / 1e6 << ","
                        << documentNs / 1000.0 / count << "\n"
                        << "formula_references," << referenceCount << ",\n"
                        << "formula_dependencies," << dependencyCount << ",\n"
                        << "formula_evaluate," << evaluationCount << "," << evaluationNs / 1e6 << ","
                        << evaluationNs / 1000.0 / evaluationCount << "\n"
                        << "evaluation_checksum," << evaluationChecksum << ",\n";
    return 0;
}
