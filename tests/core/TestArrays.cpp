#include <core/formula/Analysis.h>

#include <algorithm>
#include <QString>
#include <unordered_set>
#include <QVector>

#include <QtTest>

using namespace qds::core;

namespace {

bool hasIssue(const AnalysisReport &analysis, const QString &code)
{
    return std::any_of(analysis.issues().begin(), analysis.issues().end(), [&code](const ModelIssue &issue) {
        return issue.code == code;
    });
}

class TestArrays : public QObject
{
    Q_OBJECT

private slots:
    void dimensionDefinitions();
    void arraySyntax();
    void arrayAnalysis();
    void captureAndRestore();
};

void TestArrays::dimensionDefinitions()
{
    Document document;
    QVERIFY(document.defineDimension("Location", {"Boston", "New York"}));
    QVERIFY(document.defineDimension("N", 3));
    QVERIFY(!document.defineDimension("Location", 2));
    QVERIFY(!document.defineDimension("Empty", 0));
    QVERIFY(!document.defineDimension("Duplicate", {"A", "a"}));
    QCOMPARE(document.dimensions().size(), std::size_t(2));
    QCOMPARE(document.findDimension("LOCATION")->elements[1].nameKey, QString("new_york"));
    QCOMPARE(document.findDimension("N")->elements[2].displayName, QString("3"));

    const EntityId array = document.createVariable(ElementKind::Auxiliary);
    document.rename(array, "Sales");
    QVERIFY(!document.rename(array, "Location"));
    QVERIFY(!document.defineDimension("Sales", 2));
    const auto *model = static_cast<const ModelElement *>(document.find(array));
    QVERIFY(document.setDimensions(array, {"Location", "N"}));
    QCOMPARE(model->dimensions(), (QVector<QString> {"location", "n"}));
    QVERIFY(!document.setDimensions(array, {"Missing"}));
    QCOMPARE(model->dimensions().size(), std::size_t(2));

    QVERIFY(document.setElementEquation(array, {0, 0}, "1"));
    QVERIFY(document.setElementEquation(array, {0, 0}, "2"));
    QCOMPARE(model->elementEquations().size(), std::size_t(1));
    QCOMPARE(model->elementEquations()[0].equation(), QString("2"));
    QVERIFY(!document.setElementEquation(array, {0}, "3"));
    QVERIFY(!document.setElementEquation(array, {2, 0}, "3"));
    QVERIFY(!document.setElementEquation(array, {0, 3}, "3"));
    document.setEquation(array, "10");
    QVERIFY(model->elementEquations().empty());
    document.clear();
    QVERIFY(document.dimensions().empty());
}

void TestArrays::arraySyntax()
{
    const Formula analysis = analyzeFormula("A[Dim, *, 1:3, @2]");
    QVERIFY2(analysis.issues().empty(), "Array subscript syntax must parse");
    const auto *subscript = dynamic_cast<const SubscriptNode *>(analysis.root());
    QVERIFY(subscript);
    QCOMPARE(subscript->indexCount(), std::size_t(4));
    QVERIFY(dynamic_cast<const ReferenceNode *>(subscript->index(0)));
    QVERIFY(dynamic_cast<const WildcardNode *>(subscript->index(1)));
    const auto *range = dynamic_cast<const RangeNode *>(subscript->index(2));
    QVERIFY(range);
    QVERIFY(dynamic_cast<const NumberNode *>(range->begin()));
    QVERIFY(dynamic_cast<const NumberNode *>(range->end()));
    const auto *position = dynamic_cast<const DimensionPositionNode *>(subscript->index(3));
    QVERIFY(position);
    QCOMPARE(position->position(), std::size_t(2));
    QCOMPARE(formulaNodeCount(analysis.root()), std::size_t(8));

    const Formula literal = analyzeFormula("1, 2 + 3, 4");
    QVERIFY2(literal.issues().empty(), "Array literal syntax must parse");
    const auto *literalNode = dynamic_cast<const ArrayLiteralNode *>(literal.root());
    QVERIFY(literalNode);
    QCOMPARE(literalNode->valueCount(), std::size_t(3));
    QCOMPARE(literalNode->span(), (SourceSpan {0, 11}));
    QCOMPARE(formulaNodeCount(literal.root()), std::size_t(6));

    const Formula qualified = analyzeFormula("Location.Boston");
    QCOMPARE(dynamic_cast<const ReferenceNode *>(qualified.root())->nameKey(), QString("location.boston"));
    QVERIFY(!analyzeFormula("A[@0]").issues().empty());
    QVERIFY(!analyzeFormula("A[@1.5]").issues().empty());
    QVERIFY(!analyzeFormula("A[1:]").issues().empty());
    QVERIFY(!analyzeFormula("*").issues().empty());
}

void TestArrays::arrayAnalysis()
{
    Document document;
    document.defineDimension("Location", {"Boston", "Chicago"});
    document.defineDimension("N", 3);
    const EntityId sales = document.createVariable(ElementKind::Auxiliary);
    const EntityId revenue = document.createVariable(ElementKind::Auxiliary);
    const EntityId costs = document.createVariable(ElementKind::Auxiliary);
    const EntityId recursive = document.createVariable(ElementKind::Auxiliary);
    document.rename(sales, "Sales");
    document.rename(revenue, "Revenue");
    document.rename(costs, "Costs");
    document.rename(recursive, "Recursive");
    document.setDimensions(sales, {"Location", "N"});
    document.setDimensions(revenue, {"Location", "N"});
    document.setDimensions(costs, {"N"});
    document.setDimensions(recursive, {"Location"});
    document.setEquation(sales, "Revenue[Location, 1] + SUM(Costs[1:3]) + Location.Boston");
    document.setEquation(recursive, "Recursive[Location]");

    AnalysisReport analysis = analyzeDocument(document);
    QCOMPARE(analysis.references(sales), (QSet<EntityId> {revenue, costs}));
    QVERIFY(!hasIssue(analysis, "undefined-variable"));
    QVERIFY(!hasIssue(analysis, "parse"));
    QVERIFY(!hasIssue(analysis, "algebraic-cycle"));
    QCOMPARE(analysis.dependencies().edges().value(revenue), QVector<EntityId> {sales});
    QCOMPARE(analysis.dependencies().edges().value(costs), QVector<EntityId> {sales});

    document.setEquation(recursive, "");
    document.setElementEquation(recursive, {0}, "Revenue[Boston, 2]");
    analysis = analyzeDocument(document);
    const auto *recursiveModel = static_cast<const ModelElement *>(document.find(recursive));
    QCOMPARE(recursiveModel->elementEquations().size(), std::size_t(1));
    QCOMPARE(recursiveModel->elementEquations()[0].indices(), QVector<std::size_t> {0});
    QCOMPARE(analysis.references(recursive), QSet<EntityId> {revenue});
    QVERIFY(hasIssue(analysis, "incomplete-array-elements"));
}

void TestArrays::captureAndRestore()
{
    Document document;
    document.defineDimension("Location", {"Boston", "Chicago"});
    const EntityId input = document.createVariable(ElementKind::Constant);
    const EntityId array = document.createVariable(ElementKind::Auxiliary);
    document.rename(input, "Input");
    document.setDimensions(array, {"Location"});
    document.setElementEquation(array, {0}, "Input");
    document.setElementEquation(array, {1}, "Input * 2");
    const ElementData captured = *document.capture(array);

    Document restored;
    restored.defineDimension("Location", {"Boston", "Chicago"});
    QVERIFY(restored.restore(captured));
    const auto *model = static_cast<const ModelElement *>(restored.find(captured.id));
    QCOMPARE(model->dimensions(), captured.dimensions);
    QCOMPARE(model->elementEquations().size(), captured.elementEquations.size());
    for (int index = 0; index < model->elementEquations().size(); ++index) {
        QCOMPARE(model->elementEquations()[index].indices(), captured.elementEquations[index].indices);
        QCOMPARE(model->elementEquations()[index].equation(), captured.elementEquations[index].equation);
    }

    QVERIFY(document.rename(input, "Rate"));
    model = static_cast<const ModelElement *>(document.find(array));
    QCOMPARE(model->elementEquations()[0].equation(), QString("Rate"));
    QCOMPARE(model->elementEquations()[1].equation(), QString("Rate * 2"));
}

} // namespace

QTEST_APPLESS_MAIN(TestArrays)
#include "TestArrays.moc"
