#include <core/formula/Analysis.h>
#include <core/formula/Evaluation.h>
#include <core/formula/Formula.h>

#include <cmath>
#include <numeric>
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
    context.readVariable = [&values](EntityId id) -> std::optional<Value> {
        const auto found = values.find(id);
        return found == values.end() ? std::nullopt : std::optional<Value>(found->second);
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
        evaluationChecksum += *std::get_if<double>(&*evaluated.value);
        ++evaluationCount;
    }
    const qint64 evaluationNs = timer.nsecsElapsed();
    if (evaluationCount != count || !std::isfinite(evaluationChecksum))
        return 8;

    Document arrays;
    arrays.defineDimension("Location", {"Boston", "Chicago"});
    arrays.defineDimension("N", 3);
    for (int index = 0; index < count; ++index) {
        const EntityId input = arrays.createVariable(ElementKind::Auxiliary);
        const EntityId array = arrays.createVariable(ElementKind::Auxiliary);
        const std::string inputName = "Input" + std::to_string(index);
        if (!arrays.rename(input, inputName) || !arrays.rename(array, "Array" + std::to_string(index))
            || !arrays.setDimensions(array, {"Location", "N"}))
            return 9;
        for (std::size_t location = 0; location < 2; ++location) {
            for (std::size_t n = 0; n < 3; ++n)
                arrays.setElementEquation(array, {location, n}, inputName + " * 2");
        }
    }
    timer.restart();
    const ModelAnalysis arrayAnalysis = analyzeDocument(arrays);
    const qint64 arrayNs = timer.nsecsElapsed();
    std::size_t arrayFormulaCount = 0;
    std::size_t arrayReferenceCount = 0;
    for (const ElementAnalysis &entry : arrayAnalysis.elements) {
        arrayFormulaCount += entry.elementFormulas.size();
        arrayReferenceCount += entry.references.size();
    }
    for (const ModelIssue &issue : arrayAnalysis.issues) {
        if (issue.severity == IssueSeverity::Error) {
            QTextStream(stderr) << "Array analysis failed: " << QString::fromStdString(issue.message) << "\n";
            return 10;
        }
    }
    if (arrayFormulaCount != static_cast<std::size_t>(count) * 6
        || arrayReferenceCount != static_cast<std::size_t>(count))
        return 11;

    Document evaluatedArrays;
    evaluatedArrays.defineDimension("Location", {"Boston", "Chicago"});
    evaluatedArrays.defineDimension("N", 3);
    std::unordered_map<EntityId, Value> arrayValues;
    std::vector<EntityId> evaluatedArrayIds;
    for (int index = 0; index < count; ++index) {
        const EntityId input = evaluatedArrays.createVariable(ElementKind::Auxiliary);
        const EntityId array = evaluatedArrays.createVariable(ElementKind::Auxiliary);
        const std::string inputName = "Input" + std::to_string(index);
        const std::string arrayName = "Array" + std::to_string(index);
        if (!evaluatedArrays.rename(input, inputName) || !evaluatedArrays.rename(array, arrayName)
            || !evaluatedArrays.setDimensions(array, {"Location", "N"}))
            return 12;
        evaluatedArrays.setEquation(array, inputName + " + " + arrayName + "[Location, N]");
        arrayValues[input] = 1.0;
        arrayValues[array] = ArrayValue {{"location", "n"}, {2, 3}, {1, 2, 3, 4, 5, 6}};
        evaluatedArrayIds.push_back(array);
    }
    const ModelAnalysis evaluatedArrayAnalysis = analyzeDocument(evaluatedArrays);
    std::unordered_map<EntityId, const FormulaNode *> arrayFormulas;
    for (const ElementAnalysis &entry : evaluatedArrayAnalysis.elements)
        arrayFormulas[entry.id] = entry.formula.ast.get();
    EvaluationContext arrayContext;
    arrayContext.readDimension = [&evaluatedArrays](const std::string &name) {
        return evaluatedArrays.findDimension(name);
    };
    arrayContext.readVariable = [&arrayValues](EntityId id) -> std::optional<Value> {
        const auto found = arrayValues.find(id);
        return found == arrayValues.end() ? std::nullopt : std::optional<Value>(found->second);
    };
    arrayContext.scope.dimensions = {"location", "n"};
    timer.restart();
    double arrayChecksum = 0.0;
    std::size_t evaluatedValueCount = 0;
    for (const EntityId id : evaluatedArrayIds) {
        const EvaluationResult evaluated = evaluateFormula(arrayFormulas.at(id), arrayContext);
        const ArrayValue *value = evaluated.value ? std::get_if<ArrayValue>(&*evaluated.value) : nullptr;
        if (!value) {
            QTextStream(stderr) << "Array evaluation failed: " << QString::fromStdString(evaluated.error) << "\n";
            return 13;
        }
        evaluatedValueCount += value->values.size();
        arrayChecksum = std::accumulate(value->values.begin(), value->values.end(), arrayChecksum);
    }
    const qint64 arrayEvaluationNs = timer.nsecsElapsed();
    if (evaluatedValueCount != static_cast<std::size_t>(count) * 6 || !std::isfinite(arrayChecksum))
        return 14;

    std::vector<FormulaAnalysis> literals;
    literals.reserve(count);
    for (int index = 0; index < count; ++index)
        literals.push_back(analyzeFormula("1, 2, 3, 4, 5, 6"));
    timer.restart();
    double literalChecksum = 0.0;
    std::size_t literalValueCount = 0;
    for (const FormulaAnalysis &literal : literals) {
        const EvaluationResult evaluated = evaluateFormula(literal.ast.get(), arrayContext);
        const ArrayValue *value = evaluated.value ? std::get_if<ArrayValue>(&*evaluated.value) : nullptr;
        if (!value) {
            QTextStream(stderr) << "Array literal evaluation failed: " << QString::fromStdString(evaluated.error) << "\n";
            return 15;
        }
        literalValueCount += value->values.size();
        literalChecksum = std::accumulate(value->values.begin(), value->values.end(), literalChecksum);
    }
    const qint64 literalNs = timer.nsecsElapsed();
    if (literalValueCount != evaluatedValueCount || !std::isfinite(literalChecksum))
        return 16;

    Document macros;
    const EntityId macroGlobal = macros.createVariable(ElementKind::Constant);
    macros.rename(macroGlobal, "Global");
    MacroDefinition macro;
    macro.displayName = "Scale";
    macro.parameters = {{"x", "x", {}}, {"factor", "factor", "2"}};
    macro.equation = "x * factor + Global";
    if (!macros.defineMacro(std::move(macro)))
        return 17;
    std::unordered_map<EntityId, Value> macroValues {{macroGlobal, 10.0}};
    std::vector<EntityId> macroOutputs;
    for (int index = 0; index < count; ++index) {
        const EntityId input = macros.createVariable(ElementKind::Auxiliary);
        const EntityId output = macros.createVariable(ElementKind::Auxiliary);
        const std::string inputName = "Input" + std::to_string(index);
        macros.rename(input, inputName);
        macros.rename(output, "Output" + std::to_string(index));
        macros.setEquation(output, "Scale(" + inputName + ")");
        macroValues[input] = 1.0;
        macroOutputs.push_back(output);
    }
    timer.restart();
    const ModelAnalysis macroAnalysis = analyzeDocument(macros);
    const qint64 macroNs = timer.nsecsElapsed();
    for (const ModelIssue &issue : macroAnalysis.issues) {
        if (issue.severity == IssueSeverity::Error) {
            QTextStream(stderr) << "Macro analysis failed: " << QString::fromStdString(issue.message) << "\n";
            return 18;
        }
    }
    std::unordered_map<EntityId, const FormulaNode *> macroFormulas;
    for (const ElementAnalysis &entry : macroAnalysis.elements)
        macroFormulas[entry.id] = entry.formula.ast.get();
    EvaluationContext macroContext;
    macroContext.readVariable = [&macroValues](EntityId id) -> std::optional<Value> {
        const auto found = macroValues.find(id);
        return found == macroValues.end() ? std::nullopt : std::optional<Value>(found->second);
    };
    timer.restart();
    double macroChecksum = 0.0;
    for (const EntityId id : macroOutputs) {
        const EvaluationResult evaluated = evaluateFormula(macroFormulas.at(id), macroContext);
        const auto *value = evaluated.value ? std::get_if<double>(&*evaluated.value) : nullptr;
        if (!value) {
            QTextStream(stderr) << "Macro evaluation failed: " << QString::fromStdString(evaluated.error) << "\n";
            return 19;
        }
        macroChecksum += *value;
    }
    const qint64 macroEvaluationNs = timer.nsecsElapsed();
    if (!std::isfinite(macroChecksum))
        return 20;
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
                        << "evaluation_checksum," << evaluationChecksum << ",\n"
                        << "array_analysis," << arrayFormulaCount << "," << arrayNs / 1e6 << ","
                        << arrayNs / 1000.0 / arrayFormulaCount << "\n"
                        << "array_references," << arrayReferenceCount << ",\n"
                        << "array_evaluate," << count << "," << arrayEvaluationNs / 1e6 << ","
                        << arrayEvaluationNs / 1000.0 / count << "\n"
                        << "array_values," << evaluatedValueCount << ",\n"
                        << "array_evaluation_checksum," << arrayChecksum << ",\n"
                        << "array_literal_evaluate," << count << "," << literalNs / 1e6 << ","
                        << literalNs / 1000.0 / count << "\n"
                        << "array_literal_values," << literalValueCount << ",\n"
                        << "array_literal_checksum," << literalChecksum << ",\n"
                        << "macro_analysis," << count << "," << macroNs / 1e6 << ","
                        << macroNs / 1000.0 / count << "\n"
                        << "macro_evaluate," << count << "," << macroEvaluationNs / 1e6 << ","
                        << macroEvaluationNs / 1000.0 / count << "\n"
                        << "macro_checksum," << macroChecksum << ",\n";
    return 0;
}
