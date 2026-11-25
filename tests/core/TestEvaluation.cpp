#include <qds/core/Analysis.h>
#include <qds/core/Evaluation.h>

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>

#include <QtTest>

using namespace qds::core;

namespace {

EvaluationResult evaluate(const FormulaAnalysis &analysis, const EvaluationContext &context = {})
{
    return evaluateFormula(analysis.ast.get(), context);
}

void expectValue(const std::string &equation, double expected, const EvaluationContext &context = {})
{
    const FormulaAnalysis analysis = analyzeFormula(equation);
    QVERIFY2(analysis.issues.empty(), equation.c_str());
    const EvaluationResult result = evaluate(analysis, context);
    QVERIFY2(result.value.has_value(), result.error.c_str());
    const auto *scalar = std::get_if<double>(&*result.value);
    QVERIFY(scalar);
    QVERIFY2(qAbs(*scalar - expected) < 1e-12, equation.c_str());
}

void expectError(const std::string &equation, const EvaluationContext &context = {})
{
    const FormulaAnalysis analysis = analyzeFormula(equation);
    QVERIFY2(analysis.issues.empty(), equation.c_str());
    const EvaluationResult result = evaluate(analysis, context);
    QVERIFY(!result.value);
    QVERIFY(!result.error.empty());
}

const ElementAnalysis &element(const ModelAnalysis &analysis, EntityId id)
{
    const auto found = std::find_if(analysis.elements.begin(), analysis.elements.end(), [id](const ElementAnalysis &entry) {
        return entry.id == id;
    });
    return *found;
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
    const EntityId input = document.createVariable(ElementKind::Constant);
    const EntityId offset = document.createVariable(ElementKind::Constant);
    const EntityId result = document.createVariable(ElementKind::Auxiliary);
    document.rename(input, "Input");
    document.rename(offset, "Offset");
    document.setEquation(result, "Input * 2 + Offset");
    const ModelAnalysis analysis = analyzeDocument(document);
    const ElementAnalysis &entry = element(analysis, result);

    EvaluationContext context;
    context.readVariable = [input, offset](EntityId id) -> std::optional<double> {
        if (id == input)
            return 3.5;
        if (id == offset)
            return 1.5;
        return std::nullopt;
    };
    EvaluationResult evaluated = evaluateFormula(entry.formula.ast.get(), context);
    QVERIFY2(evaluated.value.has_value(), evaluated.error.c_str());
    const auto *scalar = std::get_if<double>(&*evaluated.value);
    QVERIFY(scalar);
    QCOMPARE(*scalar, 8.5);

    document.rename(input, "Input.Value");
    document.setEquation(result, "\"Input.Value\" * 2 + Offset");
    const ModelAnalysis qualifiedAnalysis = analyzeDocument(document);
    const ElementAnalysis &qualifiedEntry = element(qualifiedAnalysis, result);
    evaluated = evaluateFormula(qualifiedEntry.formula.ast.get(), context);
    QVERIFY2(evaluated.value.has_value(), evaluated.error.c_str());
    scalar = std::get_if<double>(&*evaluated.value);
    QVERIFY(scalar);
    QCOMPARE(*scalar, 8.5);

    context.readVariable = {};
    QVERIFY(!evaluateFormula(entry.formula.ast.get(), context).value);
    context.readVariable = [](EntityId) -> std::optional<double> { return std::nullopt; };
    QVERIFY(!evaluateFormula(entry.formula.ast.get(), context).value);
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
    expectValue(equation.toStdString(), expected);
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
    QVERIFY(!missingExpression.value && !missingExpression.error.empty());

    const EvaluationResult unresolved = evaluate(analyzeFormula("Missing"));
    QVERIFY(!unresolved.value);
    QCOMPARE(unresolved.errorSpan, SourceSpan({0, 7}));

    expectError("ABS()");
    expectError("ABS(1, 2)");
    expectError("RANDOM(0, 1)");
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
