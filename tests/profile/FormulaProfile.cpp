#include <core/formula/Analysis.h>
#include <core/formula/Evaluation.h>
#include <core/formula/Formula.h>

#include <cmath>
#include <numeric>
#include <vector>
#include <unordered_map>

#include <QElapsedTimer>
#include <QHash>
#include <QVector>
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
        const QString equation = "a" + QString::number(index) + "*b" + QString::number(index)
            + "+ABS(c" + QString::number(index) + ")-IF d" + QString::number(index)
            + " THEN e" + QString::number(index) + " ELSE f" + QString::number(index);
        Formula analysis = analyzeFormula(equation);
        if (!analysis.root() || !analysis.issues().empty()) {
            QTextStream(stderr) << "Formula analysis failed at index " << index << "\n";
            return 1;
        }
        totalNodes += formulaNodeCount(analysis.root());
    }
    const qint64 elapsedNs = timer.nsecsElapsed();
    if (totalNodes == 0)
        return 3;

    timer.restart();
    int constantClassificationCount = 0;
    for (int index = 0; index < count; ++index) {
        if (!isConstantFormula(QStringLiteral("+8.123e-10"))
            || isConstantFormula(QStringLiteral("a + 1")))
            return 24;
        ++constantClassificationCount;
    }
    const qint64 constantClassificationNs = timer.nsecsElapsed();

    Document document;
    document.createVariable(ElementKind::Stock);
    QString previousName = "Stock1";
    for (int index = 0; index < count; ++index) {
        const EntityId id = document.createVariable(ElementKind::Variable);
        const QString currentName = "Equation" + QString::number(index);
        if (!document.rename(id, currentName))
            return 4;
        for (const QChar prefix : QString("bcdef")) {
            const EntityId input = document.createVariable(ElementKind::Variable);
            if (!document.rename(input, prefix + QString::number(index)))
                return 4;
        }
        const QString equation = previousName + "*b" + QString::number(index)
            + "+ABS(c" + QString::number(index) + ")-IF d" + QString::number(index)
            + " THEN e" + QString::number(index) + " ELSE f" + QString::number(index);
        document.setEquation(id, equation);
        previousName = currentName;
    }
    timer.restart();
    AnalysisReport analysis = analyzeDocument(document);
    const qint64 documentNs = timer.nsecsElapsed();
    std::size_t dependencyCount = 0;
    for (const auto &entry : analysis.dependencies().edges())
        dependencyCount += entry.size();
    std::size_t referenceCount = 0;
    for (const ModelElement &entry : document.model().elements())
        referenceCount += analysis.references(entry.id()).size();
    for (const ModelIssue &issue : analysis.issues()) {
        if (issue.severity == IssueSeverity::Error) {
            QTextStream(stderr) << "Model analysis failed: " << (issue.message) << "\n";
            return 5;
        }
    }
    if (referenceCount != static_cast<std::size_t>(count) * 6 || dependencyCount != referenceCount)
        return 6;

    QHash<QString, double> values;
    for (const ModelElement &entry : document.model().elements())
        values[entry.nameKey()] = static_cast<double>(entry.id() % 17) - 8.0;
    EvaluationContext context;
    context.time = 4.5;
    context.dt = 0.25;
    context.readVariable = [&values](const QString &name) -> std::optional<Value> {
        const auto found = values.find(name);
        return found == values.end() ? std::nullopt : std::optional<Value>(found.value());
    };
    timer.restart();
    double evaluationChecksum = 0.0;
    int evaluationCount = 0;
    for (const ModelElement &entry : document.model().elements()) {
        if (!entry.formula().root())
            continue;
        const EvaluationResult evaluated = evaluateFormula(entry.formula().root(), context);
        if (!evaluated.value) {
            QTextStream(stderr) << "Formula evaluation failed: " << (evaluated.error) << "\n";
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
        const EntityId input = arrays.createVariable(ElementKind::Variable);
        const EntityId array = arrays.createVariable(ElementKind::Variable);
        const QString inputName = "Input" + QString::number(index);
        if (!arrays.rename(input, inputName) || !arrays.rename(array, "Array" + QString::number(index))
            || !arrays.setDimensions(array, {"Location", "N"}))
            return 9;
        for (std::size_t location = 0; location < 2; ++location) {
            for (std::size_t n = 0; n < 3; ++n)
                arrays.setElementEquation(array, {location, n}, inputName + " * 2");
        }
    }
    timer.restart();
    const AnalysisReport arrayAnalysis = analyzeDocument(arrays);
    const qint64 arrayNs = timer.nsecsElapsed();
    std::size_t arrayFormulaCount = 0;
    std::size_t arrayReferenceCount = 0;
    for (const ModelElement &entry : arrays.model().elements()) {
        arrayFormulaCount += entry.elementEquations().size();
        arrayReferenceCount += arrayAnalysis.references(entry.id()).size();
    }
    for (const ModelIssue &issue : arrayAnalysis.issues()) {
        if (issue.severity == IssueSeverity::Error) {
            QTextStream(stderr) << "Array analysis failed: " << (issue.message) << "\n";
            return 10;
        }
    }
    if (arrayFormulaCount != static_cast<std::size_t>(count) * 6
        || arrayReferenceCount != static_cast<std::size_t>(count))
        return 11;

    Document evaluatedArrays;
    evaluatedArrays.defineDimension("Location", {"Boston", "Chicago"});
    evaluatedArrays.defineDimension("N", 3);
    QHash<QString, Value> arrayValues;
    QVector<EntityId> evaluatedArrayIds;
    for (int index = 0; index < count; ++index) {
        const EntityId input = evaluatedArrays.createVariable(ElementKind::Variable);
        const EntityId array = evaluatedArrays.createVariable(ElementKind::Variable);
        const QString inputName = "Input" + QString::number(index);
        const QString arrayName = "Array" + QString::number(index);
        if (!evaluatedArrays.rename(input, inputName) || !evaluatedArrays.rename(array, arrayName)
            || !evaluatedArrays.setDimensions(array, {"Location", "N"}))
            return 12;
        evaluatedArrays.setEquation(array, inputName + " + " + arrayName + "[Location, N]");
        arrayValues["input" + QString::number(index)] = 1.0;
        arrayValues["array" + QString::number(index)] = ArrayValue {{"location", "n"}, {2, 3}, {1, 2, 3, 4, 5, 6}};
        evaluatedArrayIds.push_back(array);
    }
    std::unordered_map<EntityId, const FormulaNode *> arrayFormulas;
    for (const ModelElement &entry : evaluatedArrays.model().elements())
        arrayFormulas[entry.id()] = entry.formula().root();
    EvaluationContext arrayContext;
    arrayContext.readDimension = [&evaluatedArrays](const QString &name) {
        return evaluatedArrays.findDimension(name);
    };
    arrayContext.readVariable = [&arrayValues](const QString &name) -> std::optional<Value> {
        const auto found = arrayValues.find(name);
        return found == arrayValues.end() ? std::nullopt : std::optional<Value>(found.value());
    };
    arrayContext.scope.dimensions = {"location", "n"};
    timer.restart();
    double arrayChecksum = 0.0;
    std::size_t evaluatedValueCount = 0;
    for (const EntityId id : evaluatedArrayIds) {
        const EvaluationResult evaluated = evaluateFormula(arrayFormulas.at(id), arrayContext);
        const ArrayValue *value = evaluated.value ? std::get_if<ArrayValue>(&*evaluated.value) : nullptr;
        if (!value) {
            QTextStream(stderr) << "Array evaluation failed: " << (evaluated.error) << "\n";
            return 13;
        }
        evaluatedValueCount += value->values.size();
        arrayChecksum = std::accumulate(value->values.begin(), value->values.end(), arrayChecksum);
    }
    const qint64 arrayEvaluationNs = timer.nsecsElapsed();
    if (evaluatedValueCount != static_cast<std::size_t>(count) * 6 || !std::isfinite(arrayChecksum))
        return 14;

    std::vector<Formula> literals;
    literals.reserve(count);
    for (int index = 0; index < count; ++index)
        literals.push_back(analyzeFormula("1, 2, 3, 4, 5, 6"));
    timer.restart();
    double literalChecksum = 0.0;
    std::size_t literalValueCount = 0;
    for (const Formula &literal : literals) {
        const EvaluationResult evaluated = evaluateFormula(literal.root(), arrayContext);
        const ArrayValue *value = evaluated.value ? std::get_if<ArrayValue>(&*evaluated.value) : nullptr;
        if (!value) {
            QTextStream(stderr) << "Array literal evaluation failed: " << (evaluated.error) << "\n";
            return 15;
        }
        literalValueCount += value->values.size();
        literalChecksum = std::accumulate(value->values.begin(), value->values.end(), literalChecksum);
    }
    const qint64 literalNs = timer.nsecsElapsed();
    if (literalValueCount != evaluatedValueCount || !std::isfinite(literalChecksum))
        return 16;

    Document macros;
    const EntityId macroGlobal = macros.createVariable(ElementKind::Variable);
    macros.rename(macroGlobal, "Global");
    MacroDefinitionData macro;
    macro.displayName = "Scale";
    macro.parameters = {{"x", {}}, {"factor", "2"}};
    macro.equation = "x * factor + Global";
    if (!macros.defineMacro(std::move(macro)))
        return 17;
    QHash<QString, Value> macroValues {{"global", 10.0}};
    QVector<EntityId> macroOutputs;
    for (int index = 0; index < count; ++index) {
        const EntityId input = macros.createVariable(ElementKind::Variable);
        const EntityId output = macros.createVariable(ElementKind::Variable);
        const QString inputName = "Input" + QString::number(index);
        macros.rename(input, inputName);
        macros.rename(output, "Output" + QString::number(index));
        macros.setEquation(output, "Scale(" + inputName + ")");
        macroValues["input" + QString::number(index)] = 1.0;
        macroOutputs.push_back(output);
    }
    timer.restart();
    const AnalysisReport macroAnalysis = analyzeDocument(macros);
    const qint64 macroNs = timer.nsecsElapsed();
    for (const ModelIssue &issue : macroAnalysis.issues()) {
        if (issue.severity == IssueSeverity::Error) {
            QTextStream(stderr) << "Macro analysis failed: " << (issue.message) << "\n";
            return 18;
        }
    }
    std::unordered_map<EntityId, const FormulaNode *> macroFormulas;
    for (const ModelElement &entry : macros.model().elements())
        macroFormulas[entry.id()] = entry.formula().root();
    EvaluationContext macroContext;
    macroContext.readMacro = makeMacroReader(macros);
    macroContext.readVariable = [&macroValues](const QString &name) -> std::optional<Value> {
        const auto found = macroValues.find(name);
        return found == macroValues.end() ? std::nullopt : std::optional<Value>(found.value());
    };
    timer.restart();
    double macroChecksum = 0.0;
    for (const EntityId id : macroOutputs) {
        const EvaluationResult evaluated = evaluateFormula(macroFormulas.at(id), macroContext);
        const auto *value = evaluated.value ? std::get_if<double>(&*evaluated.value) : nullptr;
        if (!value) {
            QTextStream(stderr) << "Macro evaluation failed: " << (evaluated.error) << "\n";
            return 19;
        }
        macroChecksum += *value;
    }
    const qint64 macroEvaluationNs = timer.nsecsElapsed();
    if (!std::isfinite(macroChecksum))
        return 20;

    Document recursiveMacros;
    MacroDefinitionData recursive;
    recursive.displayName = "Down";
    recursive.parameters = {{"x", {}}};
    recursive.equation = "IF x <= 0 THEN 0 ELSE Down(x - 1)";
    if (!recursiveMacros.defineMacro(recursive))
        return 21;
    QVector<EntityId> recursiveOutputs;
    for (int index = 0; index < count; ++index) {
        const EntityId output = recursiveMacros.createVariable(ElementKind::Variable);
        recursiveMacros.setEquation(output, "Down(4)");
        recursiveOutputs.push_back(output);
    }
    timer.restart();
    const AnalysisReport recursiveAnalysis = analyzeDocument(recursiveMacros);
    const qint64 recursiveAnalysisNs = timer.nsecsElapsed();
    EvaluationContext recursiveContext;
    recursiveContext.readMacro = makeMacroReader(recursiveMacros);
    std::size_t recursiveNodes = 0;
    std::unordered_map<EntityId, const FormulaNode *> recursiveFormulas;
    double recursiveChecksum = 0.0;
    timer.restart();
    for (const ModelElement &entry : recursiveMacros.model().elements()) {
        recursiveNodes += formulaNodeCount(entry.formula().root());
        recursiveFormulas[entry.id()] = entry.formula().root();
    }
    for (const EntityId output : recursiveOutputs) {
        const EvaluationResult evaluated = evaluateFormula(recursiveFormulas.at(output), recursiveContext);
        if (!evaluated.value)
            return 22;
        recursiveChecksum += *std::get_if<double>(&*evaluated.value);
    }
    const qint64 recursiveEvaluationNs = timer.nsecsElapsed();
    if (recursiveNodes != static_cast<std::size_t>(count) * 2 || !std::isfinite(recursiveChecksum)) {
        QTextStream(stderr) << "Recursive profile mismatch: nodes " << recursiveNodes << " expected " << count
                            << " checksum " << recursiveChecksum << "\n";
        return 23;
    }
    QTextStream(stdout) << "operation,count,total_ms,per_formula_us\n"
                        << "formula_analyze," << count << "," << elapsedNs / 1e6 << ","
                        << elapsedNs / 1000.0 / count << "\n"
                        << "formula_nodes," << totalNodes << ",\n"
                        << "constant_formula_classification," << constantClassificationCount << ","
                        << constantClassificationNs / 1e6 << "," << constantClassificationNs / 1000.0 / count << "\n"
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
                        << "macro_checksum," << macroChecksum << ",\n"
                        << "macro_recursive_analysis," << count << "," << recursiveAnalysisNs / 1e6 << ","
                        << recursiveAnalysisNs / 1000.0 / count << "\n"
                        << "macro_recursive_evaluate," << count << "," << recursiveEvaluationNs / 1e6 << ","
                        << recursiveEvaluationNs / 1000.0 / count << "\n"
                        << "macro_recursive_nodes," << recursiveNodes << ",\n"
                        << "macro_recursive_checksum," << recursiveChecksum << ",\n";
    return 0;
}
