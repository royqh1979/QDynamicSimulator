#include <core/formula/Analysis.h>
#include <core/formula/Evaluation.h>

#include <algorithm>
#include <cmath>
#include <optional>
#include <QString>

#include <QtTest>

using namespace qds::core;

namespace {

EvaluationResult evaluate(const Formula &analysis, const EvaluationContext &context = {})
{
    return evaluateFormula(analysis.root(), context);
}

void expectValue(const QString &equation, double expected, const EvaluationContext &context = {})
{
    const Formula analysis = analyzeFormula(equation);
    QVERIFY2(analysis.issues().empty(), qPrintable(equation));
    const EvaluationResult result = evaluate(analysis, context);
    QVERIFY2(result.value.has_value(), qPrintable(result.error));
    const auto *scalar = std::get_if<double>(&*result.value);
    QVERIFY(scalar);
    QVERIFY2(qAbs(*scalar - expected) < 1e-12, qPrintable(equation));
}

void expectError(const QString &equation, const EvaluationContext &context = {})
{
    const Formula analysis = analyzeFormula(equation);
    QVERIFY2(analysis.issues().empty(), qPrintable(equation));
    const EvaluationResult result = evaluate(analysis, context);
    QVERIFY(!result.value);
    QVERIFY(!result.error.isEmpty());
}

class TestEvaluation : public QObject
{
    Q_OBJECT

private slots:
    void resolvedReferences();
    void arithmeticAndLogic_data();
    void arithmeticAndLogic();
    void constantsAndMath();
    void shortCircuit();
    void errorsAndUnsupported();
};

void TestEvaluation::resolvedReferences()
{
    Document document;
    const EntityId input = document.createVariable(ElementKind::Variable);
    const EntityId offset = document.createVariable(ElementKind::Variable);
    const EntityId result = document.createVariable(ElementKind::Variable);
    document.rename(input, "Input");
    document.rename(offset, "Offset");
    document.setEquation(result, "Input * 2 + Offset");
    const Formula &entry = static_cast<const ModelElement *>(document.find(result))->formula();

    EvaluationContext context;
    context.readVariable = [](const QString &name) -> std::optional<Value> {
        if (name == "input" || name == "input.value")
            return 3.5;
        if (name == "offset")
            return 1.5;
        return std::nullopt;
    };
    EvaluationResult evaluated = evaluateFormula(entry.root(), context);
    QVERIFY2(evaluated.value.has_value(), qPrintable(evaluated.error));
    const auto *scalar = std::get_if<double>(&*evaluated.value);
    QVERIFY(scalar);
    QCOMPARE(*scalar, 8.5);

    const std::size_t originalNodes = formulaNodeCount(entry.root());
    const SourceSpan originalSpan = entry.root()->span();
    EvaluationContext otherContext;
    otherContext.readVariable = [](const QString &name) -> std::optional<Value> {
        if (name == "input")
            return 5.0;
        if (name == "offset")
            return 2.0;
        return std::nullopt;
    };
    evaluated = evaluateFormula(entry.root(), otherContext);
    QVERIFY2(evaluated.value.has_value(), qPrintable(evaluated.error));
    QCOMPARE(*std::get_if<double>(&*evaluated.value), 12.0);
    QCOMPARE(formulaNodeCount(entry.root()), originalNodes);
    QCOMPARE(entry.root()->span(), originalSpan);

    document.rename(input, "Input.Value");
    document.setEquation(result, "\"Input.Value\" * 2 + Offset");
    const Formula &qualifiedEntry = static_cast<const ModelElement *>(document.find(result))->formula();
    evaluated = evaluateFormula(qualifiedEntry.root(), context);
    QVERIFY2(evaluated.value.has_value(), qPrintable(evaluated.error));
    scalar = std::get_if<double>(&*evaluated.value);
    QVERIFY(scalar);
    QCOMPARE(*scalar, 8.5);

    const Formula &updatedEntry = static_cast<const ModelElement *>(document.find(result))->formula();
    context.readVariable = {};
    QVERIFY(!evaluateFormula(updatedEntry.root(), context).value);
    context.readVariable = [](const QString &) -> std::optional<Value> { return std::nullopt; };
    QVERIFY(!evaluateFormula(updatedEntry.root(), context).value);
}

void TestEvaluation::arithmeticAndLogic_data()
{
    QTest::addColumn<QString>("equation");
    QTest::addColumn<double>("expected");
    QTest::newRow("precedence") << "1 + 2 * 3" << 7.0;
    QTest::newRow("parentheses") << "(1 + 2) * 3" << 9.0;
    QTest::newRow("right associative power") << "2 ^ 3 ^ 2" << 512.0;
    QTest::newRow("unary power precedence") << "-2 ^ 2" << -4.0;
    QTest::newRow("positive") << "+3" << 3.0;
    QTest::newRow("division") << "7 / 2" << 3.5;
    QTest::newRow("positive modulo") << "5 MOD 3" << 2.0;
    QTest::newRow("negative dividend") << "-5 MOD 3" << 1.0;
    QTest::newRow("negative divisor") << "5 MOD -3" << -1.0;
    QTest::newRow("both negative") << "-5 MOD -3" << -2.0;
    QTest::newRow("less") << "1 < 2" << 1.0;
    QTest::newRow("less equal") << "2 <= 2" << 1.0;
    QTest::newRow("greater") << "2 > 3" << 0.0;
    QTest::newRow("greater equal") << "3 >= 3" << 1.0;
    QTest::newRow("equal") << "2 = 2" << 1.0;
    QTest::newRow("not equal") << "2 <> 2" << 0.0;
    QTest::newRow("zero literal equality") << "0 = 0.0" << 1.0;
    QTest::newRow("one literal equality") << "1 = 1.0" << 1.0;
    QTest::newRow("not zero") << "NOT 0" << 1.0;
    QTest::newRow("not nonzero") << "NOT 3" << 0.0;
    QTest::newRow("and") << "3 AND 4" << 1.0;
    QTest::newRow("and false") << "3 AND 0" << 0.0;
    QTest::newRow("or") << "0 OR 4" << 1.0;
}

void TestEvaluation::arithmeticAndLogic()
{
    QFETCH(QString, equation);
    QFETCH(double, expected);
    expectValue(equation, expected);
}

void TestEvaluation::constantsAndMath()
{
    EvaluationContext context;
    context.time = 4.5;
    context.dt = 0.25;
    context.startTime = 1.0;
    context.stopTime = 20.0;
    expectValue("TIME + DT + STARTTIME + STOPTIME", 25.75, context);
    expectValue("TIME() + DT() + STARTTIME() + STOPTIME()", 25.75, context);
    expectValue("PI", 3.14159265358979323846);
    expectValue("ABS(-2) + INT(-1.2) + MAX(3, 4) + MIN(3, 4) + MAX(5) + MIN(5)", 17.0);
    expectValue("LN(EXP(1)) + LOG(EXP(1)) + LOG10(100) + SQRT(9)", 7.0);
    expectValue("ARCCOS(1) + ARCSIN(0) + ARCTAN(0) + COS(0) + SIN(0) + TAN(0)", 1.0);
    expectValue("SUM(1) + SIZE(5) + STDDEV(2)", 2.0);

    const EvaluationResult infinity = evaluate(analyzeFormula("INF + 1 / 0"));
    QVERIFY(infinity.value && std::isinf(*std::get_if<double>(&*infinity.value)));
    const EvaluationResult nan = evaluate(analyzeFormula("SQRT(-1)"));
    QVERIFY(nan.value && std::isnan(*std::get_if<double>(&*nan.value)));
}

void TestEvaluation::shortCircuit()
{
    expectValue("IF 2 THEN 10 ELSE Missing", 10.0);
    expectValue("IF 0 THEN Missing ELSE 20", 20.0);
    expectValue("if_then_else(0, Missing, 30)", 30.0);
    expectValue("0 AND Missing", 0.0);
    expectValue("1 OR Missing", 1.0);
    expectError("1 AND Missing");
    expectError("0 OR Missing");
}

void TestEvaluation::errorsAndUnsupported()
{
    const EvaluationResult missingExpression = evaluateFormula(nullptr, {});
    QVERIFY(!missingExpression.value && !missingExpression.error.isEmpty());

    const EvaluationResult unresolved = evaluate(analyzeFormula("Missing"));
    QVERIFY(!unresolved.value);
    QCOMPARE(unresolved.errorSpan, SourceSpan({0, 7}));

    expectError("ABS()");
    expectError("ABS(1, 2)");
    expectError("DELAY(1, 2)");
    expectError("INIT(1)");
    expectError("SELF");
    expectError("A[1]");
    expectError("A[*, 1:2, @1]");
    expectError("A'");
    expectError("Unknown(1)");
}

} // namespace

QTEST_APPLESS_MAIN(TestEvaluation)
#include "TestEvaluation.moc"
