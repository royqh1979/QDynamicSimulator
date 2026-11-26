#include <core/formula/Analysis.h>
#include <core/formula/Evaluation.h>
#include <core/Identifier.h>

#include <algorithm>
#include <QString>
#include <unordered_set>

#include <QtTest>

using namespace qds::core;

namespace {

MacroParameter parameter(const QString &name, const QString &defaultEquation = {})
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

bool hasIssue(const ModelAnalysis &analysis, const QString &code)
{
    return std::any_of(analysis.issues.begin(), analysis.issues.end(), [&code](const ModelIssue &issue) {
        return issue.code == code;
    });
}

EvaluationResult evaluate(const ModelAnalysis &analysis, EntityId id, const EvaluationContext &context = {})
{
    EvaluationContext actual = context;
    actual.readMacro = makeMacroReader(analysis);
    return evaluateFormula(element(analysis, id).formula.root(), actual);
}

void expectScalar(const ModelAnalysis &analysis, EntityId id, const EvaluationContext &context, double expected)
{
    const EvaluationResult result = evaluate(analysis, id, context);
    QVERIFY2(result.value.has_value(), qPrintable(result.error));
    const auto *scalar = std::get_if<double>(&*result.value);
    QVERIFY(scalar);
    QCOMPARE(*scalar, expected);
}

void expectScalar(const ModelAnalysis &analysis, EntityId id, double expected)
{
    expectScalar(analysis, id, EvaluationContext {}, expected);
}

class TestMacros : public QObject
{
    Q_OBJECT

private slots:
    void definitionsAndOverloads();
    void expansionAndEvaluation();
    void recursionAndErrors();
    void runtimeScopesAndRecursion();
    void dependencyClosure();
    void depthAndModification();
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
    QCOMPARE(document.findMacro("scale", 1)->parameters.size(), 2);
    QCOMPARE(document.findMacro("scale", 3)->parameters.size(), 3);
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
    QVERIFY(document.macros().isEmpty());
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
    QVERIFY(!hasIssue(analysis, "recursive-macro"));
    QCOMPARE(element(analysis, output).references, std::unordered_set<EntityId> {offset});
    QCOMPARE(analysis.macros.size(), std::size_t(4));
    EvaluationContext context;
    context.readVariable = [](const QString &name) -> std::optional<Value> {
        return name == "offset" ? std::optional<Value>(10.0) : std::nullopt;
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
    QVERIFY(!hasIssue(analysis, "recursive-macro"));
    QVERIFY(!hasIssue(analysis, "unknown-function"));
    expectScalar(analysis, output, 6.0);

    document.setEquation(output, "MissingMacro(1)");
    QVERIFY(hasIssue(analyzeDocument(document), "unknown-function"));
}

void TestMacros::runtimeScopesAndRecursion()
{
    Document document;
    const EntityId input = document.createVariable(ElementKind::Constant);
    document.rename(input, "Input");
    const EntityId globalA = document.createVariable(ElementKind::Constant);
    document.rename(globalA, "A");
    const EntityId globalOffset = document.createVariable(ElementKind::Constant);
    document.rename(globalOffset, "Offset");
    const EntityId values = document.createVariable(ElementKind::Constant);
    document.rename(values, "Values");
    const EntityId output = document.createVariable(ElementKind::Auxiliary);

    MacroDefinition fact {"Fact", {}, {parameter("x")}, "IF x <= 1 THEN 1 ELSE x * Fact(x - 1)"};
    MacroDefinition even {"Even", {}, {parameter("x")}, "IF x <= 0 THEN 1 ELSE Odd(x - 1)"};
    MacroDefinition odd {"Odd", {}, {parameter("x")}, "IF x <= 0 THEN 0 ELSE Even(x - 1)"};
    MacroDefinition twice {"Twice", {}, {parameter("x")}, "x + x"};
    MacroDefinition addScaled {"AddScaled", {}, {parameter("x"), parameter("y", "x * 2")}, "x + y"};
    MacroDefinition shadow {"Shadow", {}, {parameter("Offset")}, "Offset * 2"};
    MacroDefinition callee {"Callee", {}, {parameter("x"), parameter("y", "A")}, "y"};
    MacroDefinition caller {"Caller", {}, {parameter("a")}, "Callee(1)"};
    MacroDefinition total {"Total", {}, {parameter("x")}, "SUM(x)"};
    MacroDefinition overloadedOne {"Overloaded", {}, {parameter("x")}, "Overloaded(x, 2)"};
    MacroDefinition overloadedTwo {"Overloaded", {}, {parameter("x"), parameter("y")},
        "IF y <= 1 THEN x ELSE Overloaded(x, y - 1)"};
    for (const MacroDefinition &macro : {fact, even, odd, twice, addScaled, shadow, callee, caller, total,
             overloadedOne, overloadedTwo})
        QVERIFY(document.defineMacro(macro));

    document.setEquation(output, "Fact(5)");
    expectScalar(analyzeDocument(document), output, 120.0);
    document.setEquation(output, "Even(10) + Odd(9)");
    expectScalar(analyzeDocument(document), output, 2.0);
    document.setEquation(output, "AddScaled(4)");
    expectScalar(analyzeDocument(document), output, 12.0);
    document.setEquation(output, "Overloaded(3)");
    expectScalar(analyzeDocument(document), output, 3.0);

    int inputReads = 0;
    EvaluationContext context;
    context.readVariable = [&](const QString &name) -> std::optional<Value> {
        if (name == "input") {
            ++inputReads;
            return 5.0;
        }
        if (name == "a")
            return 100.0;
        if (name == "offset")
            return 10.0;
        if (name == "values")
            return ArrayValue {{"n"}, {3}, {1.0, 2.0, 3.0}};
        return std::nullopt;
    };
    document.setEquation(output, "Twice(Input)");
    expectScalar(analyzeDocument(document), output, context, 10.0);
    QCOMPARE(inputReads, 1);
    document.setEquation(output, "Shadow(3)");
    expectScalar(analyzeDocument(document), output, context, 6.0);
    document.setEquation(output, "Caller(5)");
    expectScalar(analyzeDocument(document), output, context, 100.0);
    document.setEquation(output, "Total(Values)");
    expectScalar(analyzeDocument(document), output, context, 6.0);
}

void TestMacros::dependencyClosure()
{
    Document document;
    const EntityId a = document.createVariable(ElementKind::Constant);
    const EntityId b = document.createVariable(ElementKind::Constant);
    document.rename(a, "A");
    document.rename(b, "B");
    const EntityId withDefault = document.createVariable(ElementKind::Auxiliary);
    const EntityId explicitArgument = document.createVariable(ElementKind::Auxiliary);

    MacroDefinition inner {"Inner", {}, {parameter("x")}, "x + A"};
    MacroDefinition outer {"Outer", {}, {parameter("x"), parameter("y", "Inner(x)")}, "y + B"};
    QVERIFY(document.defineMacro(inner));
    QVERIFY(document.defineMacro(outer));
    document.setEquation(withDefault, "Outer(2)");
    document.setEquation(explicitArgument, "Outer(2, 0)");
    const ModelAnalysis analysis = analyzeDocument(document);
    QCOMPARE(element(analysis, withDefault).references, std::unordered_set<EntityId>({a, b}));
    QCOMPARE(element(analysis, explicitArgument).references, std::unordered_set<EntityId>({b}));

    EvaluationContext context;
    context.readVariable = [](const QString &name) -> std::optional<Value> {
        if (name == "a")
            return 1.0;
        if (name == "b")
            return 2.0;
        return std::nullopt;
    };
    expectScalar(analysis, withDefault, context, 5.0);
    expectScalar(analysis, explicitArgument, context, 2.0);
}

void TestMacros::depthAndModification()
{
    Document document;
    const EntityId output = document.createVariable(ElementKind::Auxiliary);
    MacroDefinition down {"Down", {}, {parameter("x")}, "IF x <= 0 THEN 0 ELSE Down(x - 1)"};
    MacroDefinition scale {"Scale", {}, {parameter("x"), parameter("factor", "2")}, "x * factor"};
    QVERIFY(document.defineMacro(down));
    QVERIFY(document.defineMacro(scale));

    document.setEquation(output, "Down(63)");
    expectScalar(analyzeDocument(document), output, 0.0);
    document.setEquation(output, "Down(64)");
    const EvaluationResult exceeded = evaluate(analyzeDocument(document), output);
    QVERIFY(!exceeded.value);
    QVERIFY(exceeded.error.contains("depth limit"));

    document.setEquation(output, "Scale(3)");
    expectScalar(analyzeDocument(document), output, 6.0);
    MacroDefinition updated = *document.findMacro("Scale", 1);
    updated.parameters[1].defaultEquation = "5";
    updated.equation = "x + factor";
    QVERIFY(document.setMacro(updated));
    expectScalar(analyzeDocument(document), output, 8.0);
    MacroDefinition missing = updated;
    missing.displayName = "Missing";
    QVERIFY(!document.setMacro(missing));
}

} // namespace

QTEST_APPLESS_MAIN(TestMacros)
#include "TestMacros.moc"
