#include <core/formula/Analysis.h>
#include <core/formula/Evaluation.h>
#include <core/Identifier.h>

#include <algorithm>
#include <string>
#include <unordered_set>
#include <vector>

#include <QtTest>

using namespace qds::core;

namespace {

MacroParameter parameter(const std::string &name, const std::string &defaultEquation = {})
{
    return {name, normalizeIdentifier(name), defaultEquation};
}

const ElementAnalysis &element(const ModelAnalysis &analysis, EntityId id)
{
    const auto found = std::find_if(analysis.elements.begin(), analysis.elements.end(), [id](const ElementAnalysis &entry) {
        return entry.id == id;
    });
    return *found;
}

bool hasIssue(const ModelAnalysis &analysis, const std::string &code)
{
    return std::any_of(analysis.issues.begin(), analysis.issues.end(), [&code](const ModelIssue &issue) {
        return issue.code == code;
    });
}

void expectScalar(const ModelAnalysis &analysis, EntityId id, const EvaluationContext &context, double expected)
{
    const EvaluationResult result = evaluateFormula(element(analysis, id).formula.ast.get(), context);
    QVERIFY2(result.value.has_value(), result.error.c_str());
    const auto *scalar = std::get_if<double>(&*result.value);
    QVERIFY(scalar);
    QCOMPARE(*scalar, expected);
}

class TestMacros : public QObject
{
    Q_OBJECT

private slots:
    void definitionsAndOverloads();
    void expansionAndEvaluation();
    void recursionAndErrors();
};

void TestMacros::definitionsAndOverloads()
{
    Document document;
    MacroDefinition scale;
    scale.displayName = "Scale";
    scale.parameters = {parameter("x"), parameter("factor", "2")};
    scale.equation = "x * factor";
    QVERIFY(document.defineMacro(scale));
    QVERIFY(!document.defineMacro(scale));

    MacroDefinition three = scale;
    three.parameters.push_back(parameter("offset", "1"));
    QVERIFY(!document.defineMacro(three));
    three.parameters = {parameter("x"), parameter("factor"), parameter("offset")};
    QVERIFY(document.defineMacro(three));
    QVERIFY(document.findMacro("SCALE", 1));
    QVERIFY(document.findMacro("scale", 2));
    QCOMPARE(document.findMacro("scale", 1)->parameters.size(), std::size_t(2));
    QCOMPARE(document.findMacro("scale", 3)->parameters.size(), std::size_t(3));
    QVERIFY(!document.findMacro("scale", 0));

    MacroDefinition invalid = scale;
    invalid.displayName = "Invalid";
    invalid.parameters = {parameter("x", "1"), parameter("y")};
    QVERIFY(!document.defineMacro(invalid));
    invalid.parameters = {parameter("x"), parameter("X")};
    QVERIFY(!document.defineMacro(invalid));

    const EntityId variable = document.createVariable(ElementKind::Constant);
    document.rename(variable, "Variable");
    MacroDefinition conflict = scale;
    conflict.displayName = "Variable";
    QVERIFY(!document.defineMacro(conflict));
    QVERIFY(!document.rename(variable, "Scale"));
    document.clear();
    QVERIFY(document.macros().empty());
}

void TestMacros::expansionAndEvaluation()
{
    Document document;
    const EntityId offset = document.createVariable(ElementKind::Constant);
    const EntityId output = document.createVariable(ElementKind::Auxiliary);
    document.rename(offset, "Offset");
    document.rename(output, "Output");

    MacroDefinition scale;
    scale.displayName = "Scale";
    scale.parameters = {parameter("x"), parameter("factor", "2")};
    scale.equation = "x * factor + Offset";
    QVERIFY(document.defineMacro(scale));
    MacroDefinition three = scale;
    three.parameters = {parameter("x"), parameter("factor"), parameter("offset")};
    three.equation = "x * factor + offset";
    QVERIFY(document.defineMacro(three));
    MacroDefinition twice;
    twice.displayName = "Twice";
    twice.parameters = {parameter("x")};
    twice.equation = "Scale(x, 2)";
    QVERIFY(document.defineMacro(twice));
    MacroDefinition addScaled;
    addScaled.displayName = "AddScaled";
    addScaled.parameters = {parameter("x"), parameter("y", "x * 2")};
    addScaled.equation = "x + y";
    QVERIFY(document.defineMacro(addScaled));

    document.setEquation(output, "Scale(5) + Scale(2, 3) + Scale(1, 2, 3) + Twice(1) + AddScaled(4)");
    const ModelAnalysis analysis = analyzeDocument(document);
    QVERIFY(!hasIssue(analysis, "unknown-function"));
    QVERIFY(!hasIssue(analysis, "parse"));
    QCOMPARE(element(analysis, output).references, std::unordered_set<EntityId> {offset});
    QCOMPARE(analysis.macros.size(), std::size_t(4));
    EvaluationContext context;
    context.readVariable = [offset](EntityId id) -> std::optional<Value> {
        return id == offset ? std::optional<Value>(10.0) : std::nullopt;
    };
    expectScalar(analysis, output, context, 65.0);
}

void TestMacros::recursionAndErrors()
{
    Document document;
    const EntityId output = document.createVariable(ElementKind::Auxiliary);
    MacroDefinition factorial;
    factorial.displayName = "Fact";
    factorial.parameters = {parameter("x")};
    factorial.equation = "IF x <= 1 THEN 1 ELSE x * Fact(x - 1)";
    document.defineMacro(factorial);
    document.setEquation(output, "Fact(3)");
    const ModelAnalysis analysis = analyzeDocument(document);
    QVERIFY(hasIssue(analysis, "recursive-macro"));
    QVERIFY(!hasIssue(analysis, "unknown-function"));
    EvaluationContext context;
    QVERIFY(!evaluateFormula(element(analysis, output).formula.ast.get(), context).value);

    document.setEquation(output, "MissingMacro(1)");
    QVERIFY(hasIssue(analyzeDocument(document), "unknown-function"));
}

} // namespace

QTEST_APPLESS_MAIN(TestMacros)
#include "TestMacros.moc"
