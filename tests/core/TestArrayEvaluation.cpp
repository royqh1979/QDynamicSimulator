#include <core/formula/Analysis.h>
#include <core/formula/Evaluation.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <QString>
#include <unordered_map>
#include <QHash>
#include <QVector>

#include <QtTest>

using namespace qds::core;

namespace {

class TestArrayEvaluation : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void subscriptAndSlices();
    void transposeAndBroadcast();
    void applyToAllScope();
    void arrayFunctions();
    void arrayLiterals();
    void conditionsAndErrors();

private:
    EvaluationResult evaluate(const QString &equation);
    void setArray(const QString &name, QVector<QString> dimensions, QVector<std::size_t> shape,
        QVector<double> values);

    Document document;
    EvaluationContext context;
    QHash<QString, ArrayValue> arrays;
    EntityId a = 0;
    EntityId b = 0;
    EntityId c = 0;
    EntityId result = 0;
};

void TestArrayEvaluation::initTestCase()
{
    document.defineDimension("Location", {"Boston", "Chicago", "LA"});
    document.defineDimension("N", 2);
    a = document.createVariable(ElementKind::Variable);
    b = document.createVariable(ElementKind::Variable);
    c = document.createVariable(ElementKind::Variable);
    result = document.createVariable(ElementKind::Variable);
    document.rename(a, "A");
    document.rename(b, "B");
    document.rename(c, "C");
    document.rename(result, "Result");
    document.setDimensions(a, {"Location", "N"});
    document.setDimensions(b, {"N", "Location"});
    document.setDimensions(c, {"N"});
    document.setDimensions(result, {"Location", "N"});
    setArray("a", {"location", "n"}, {3, 2}, {1, 2, 3, 4, 5, 6});
    setArray("b", {"n", "location"}, {2, 3}, {10, 30, 50, 20, 40, 60});
    setArray("c", {"n"}, {2}, {7, 8});
    context.readDimension = [this](const QString &name) { return document.findDimension(name); };
    context.readVariable = [this](const QString &name) -> std::optional<Value> {
        const auto found = arrays.find(name);
        return found == arrays.end() ? std::nullopt : std::optional<Value>(found.value());
    };
    context.scope.dimensions = {"location", "n"};
}

void TestArrayEvaluation::setArray(const QString &name, QVector<QString> dimensions,
    QVector<std::size_t> shape, QVector<double> values)
{
    arrays[name] = {std::move(dimensions), std::move(shape), std::move(values)};
}

EvaluationResult TestArrayEvaluation::evaluate(const QString &equation)
{
    document.setEquation(result, equation);
    const auto *model = static_cast<const ModelElement *>(document.find(result));
    return evaluateFormula(model->formula().root(), context);
}

void expectScalar(const EvaluationResult &result, double expected)
{
    QVERIFY2(result.value.has_value(), qPrintable(result.error));
    const auto *scalar = std::get_if<double>(&*result.value);
    QVERIFY(scalar);
    QVERIFY(qAbs(*scalar - expected) < 1e-12);
}

void expectArray(const EvaluationResult &result, const QVector<QString> &dimensions,
    const QVector<std::size_t> &shape, const QVector<double> &values)
{
    QVERIFY2(result.value.has_value(), qPrintable(result.error));
    const auto *array = std::get_if<ArrayValue>(&*result.value);
    QVERIFY(array);
    QCOMPARE(array->dimensions(), dimensions);
    QCOMPARE(array->shape(), shape);
    QCOMPARE(array->values().size(), values.size());
    for (std::size_t index = 0; index < values.size(); ++index)
        QVERIFY(qAbs(array->values()[index] - values[index]) < 1e-12);
}

void TestArrayEvaluation::subscriptAndSlices()
{
    expectScalar(evaluate("A[1, 1]"), 1.0);
    expectScalar(evaluate("A[Boston, 2]"), 2.0);
    expectScalar(evaluate("A[Location.Chicago, 1]"), 3.0);
    expectScalar(evaluate("A[0, 1]"), 0.0);
    expectScalar(evaluate("A[1.5, 1]"), 0.0);
    expectArray(evaluate("A[1, *]"), {"n"}, {2}, {1, 2});
    expectArray(evaluate("A[2:3, 1]"), {"location"}, {2}, {3, 5});
    expectArray(evaluate("A[1]"), {"n"}, {2}, {1, 2});
}

void TestArrayEvaluation::transposeAndBroadcast()
{
    expectArray(evaluate("A'"), {"n", "location"}, {2, 3}, {1, 3, 5, 2, 4, 6});
    expectArray(evaluate("A + B"), {"location", "n"}, {3, 2}, {11, 22, 33, 44, 55, 66});
    expectArray(evaluate("A * 2"), {"location", "n"}, {3, 2}, {2, 4, 6, 8, 10, 12});
    expectArray(evaluate("A > 3"), {"location", "n"}, {3, 2}, {0, 0, 0, 1, 1, 1});
    QVERIFY(!evaluate("A + C").value);
}

void TestArrayEvaluation::applyToAllScope()
{
    expectArray(evaluate("A[Location, N]"), {"location", "n"}, {3, 2}, {1, 2, 3, 4, 5, 6});
    context.scope.indices = {2, 1};
    expectScalar(evaluate("A[Location, N]"), 6.0);
    expectScalar(evaluate("A[@1, @2]"), 6.0);
    expectScalar(evaluate("SUM(A[Location, *])"), 11.0);
    context.scope.indices = {1, 0};
    expectScalar(evaluate("SUM(A[Location, *])"), 7.0);
    expectScalar(evaluate("SUM(SQRT(A[Location, *]))"), std::sqrt(3.0) + 2.0);
    context.scope.indices.clear();
    context.scope.self = "result";
    setArray("result", {"location", "n"}, {3, 2}, {1, 2, 3, 4, 5, 6});
    expectScalar(evaluate("SIZE(SELF)"), 6.0);
    context.scope.self.clear();
    arrays.remove("result");
}

void TestArrayEvaluation::arrayFunctions()
{
    expectScalar(evaluate("SUM(A)"), 21.0);
    expectScalar(evaluate("MEAN(A)"), 3.5);
    expectScalar(evaluate("STDDEV(A)"), std::sqrt(35.0 / 12.0));
    expectScalar(evaluate("SIZE(A)"), 6.0);
    expectScalar(evaluate("MAX(A)"), 6.0);
    expectScalar(evaluate("MIN(A)"), 1.0);
    expectArray(evaluate("SQRT(A)"), {"location", "n"}, {3, 2},
        {1, std::sqrt(2.0), std::sqrt(3.0), 2, std::sqrt(5.0), std::sqrt(6.0)});
    expectArray(evaluate("MAX(A, 2)"), {"location", "n"}, {3, 2}, {2, 2, 3, 4, 5, 6});

    setArray("a", {"location", "n"}, {3, 2}, {20, 10, 20, 10, 40, 30});
    setArray("b", {"n", "location"}, {2, 3}, {2, 2, 3, 1, 0, 1});
    expectScalar(evaluate("RANK(A, 1)"), 2.0);
    expectScalar(evaluate("RANK(A, 1, B)"), 4.0);
    expectScalar(evaluate("RANK(A, 0)"), 0.0);
}

void TestArrayEvaluation::arrayLiterals()
{
    expectArray(evaluate("1, 2, 3, 4, 5, 6"), {"location", "n"}, {3, 2}, {1, 2, 3, 4, 5, 6});
    expectArray(evaluate("1, 2 + 3, 3, 4, 5, 6"), {"location", "n"}, {3, 2}, {1, 5, 3, 4, 5, 6});
    context.scope.indices = {2, 1};
    expectScalar(evaluate("1, 2, 3, 4, 5, 6"), 6.0);
    context.scope.indices.clear();
    QVERIFY(!evaluate("1, 2").value);
    context.scope.dimensions.clear();
    QVERIFY(!evaluate("1, 2, 3, 4, 5, 6").value);
    context.scope.dimensions = {"location", "n"};
}

void TestArrayEvaluation::conditionsAndErrors()
{
    setArray("a", {"location", "n"}, {3, 2}, {1, 2, 3, 4, 5, 6});
    expectArray(evaluate("IF A > 3 THEN A ELSE 0"), {"location", "n"}, {3, 2}, {0, 0, 0, 4, 5, 6});
    expectArray(evaluate("(A > 3) AND 1"), {"location", "n"}, {3, 2}, {0, 0, 0, 1, 1, 1});
    setArray("a", {"location", "n"}, {2, 2}, {1, 2, 3, 4});
    QVERIFY(!evaluate("A + 1").value);
    setArray("a", {"location", "n"}, {3, 2}, {1, 2, 3, 4, 5, 6});
    context.scope.indices = {0, 0};
    context.scope.dimensions.clear();
    QVERIFY(!evaluate("Location").value);

    const ArrayValue first {{"n"}, {1}, {std::numeric_limits<double>::quiet_NaN()}};
    const ArrayValue shared = first;
    QVERIFY(first == shared);
    const ArrayValue second {{"n"}, {1}, {std::numeric_limits<double>::quiet_NaN()}};
    QVERIFY(!(first == second));
    const ArrayValue third {{"n"}, {1}, {1.0}};
    const ArrayValue fourth {{"n"}, {1}, {1.0}};
    QVERIFY(third == fourth);
}

} // namespace

QTEST_APPLESS_MAIN(TestArrayEvaluation)
#include "TestArrayEvaluation.moc"
