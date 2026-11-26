#include <core/formula/Analysis.h>

#include <algorithm>
#include <string>
#include <unordered_set>
#include <vector>

#include <QtTest>

using namespace qds::core;

namespace {

bool hasIssue(const ModelAnalysis &analysis, const std::string &code)
{
    return std::any_of(analysis.issues.begin(), analysis.issues.end(), [&code](const ModelIssue &issue) {
        return issue.code == code;
    });
}

const ElementAnalysis &element(const ModelAnalysis &analysis, EntityId id)
{
    const auto found = std::find_if(analysis.elements.begin(), analysis.elements.end(), [id](const ElementAnalysis &entry) {
        return entry.id == id;
    });
    return *found;
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
    QCOMPARE(document.findDimension("LOCATION")->elements[1].nameKey, std::string("new_york"));
    QCOMPARE(document.findDimension("N")->elements[2].displayName, std::string("3"));

    const EntityId array = document.createVariable(ElementKind::Auxiliary);
    document.rename(array, "Sales");
    QVERIFY(!document.rename(array, "Location"));
    QVERIFY(!document.defineDimension("Sales", 2));
    const auto *model = static_cast<const ModelElement *>(document.find(array));
    QVERIFY(document.setDimensions(array, {"Location", "N"}));
    QCOMPARE(model->dimensions, (std::vector<std::string> {"location", "n"}));
    QVERIFY(!document.setDimensions(array, {"Missing"}));
    QCOMPARE(model->dimensions.size(), std::size_t(2));

    QVERIFY(document.setElementEquation(array, {0, 0}, "1"));
    QVERIFY(document.setElementEquation(array, {0, 0}, "2"));
    QCOMPARE(model->elementEquations.size(), std::size_t(1));
    QCOMPARE(model->elementEquations[0].equation, std::string("2"));
    QVERIFY(!document.setElementEquation(array, {0}, "3"));
    QVERIFY(!document.setElementEquation(array, {2, 0}, "3"));
    QVERIFY(!document.setElementEquation(array, {0, 3}, "3"));
    document.setEquation(array, "10");
    QVERIFY(model->elementEquations.empty());
    document.clear();
    QVERIFY(document.dimensions().empty());
}

void TestArrays::arraySyntax()
{
    const FormulaAnalysis analysis = analyzeFormula("A[Dim, *, 1:3, @2]");
    QVERIFY2(analysis.issues.empty(), "Array subscript syntax must parse");
    const auto *subscript = dynamic_cast<const SubscriptNode *>(analysis.ast.get());
    QVERIFY(subscript);
    QCOMPARE(subscript->indices.size(), std::size_t(4));
    QVERIFY(dynamic_cast<const ReferenceNode *>(subscript->indices[0].get()));
    QVERIFY(dynamic_cast<const WildcardNode *>(subscript->indices[1].get()));
    const auto *range = dynamic_cast<const RangeNode *>(subscript->indices[2].get());
    QVERIFY(range);
    QVERIFY(dynamic_cast<const NumberNode *>(range->begin.get()));
    QVERIFY(dynamic_cast<const NumberNode *>(range->end.get()));
    const auto *position = dynamic_cast<const DimensionPositionNode *>(subscript->indices[3].get());
    QVERIFY(position);
    QCOMPARE(position->position, std::size_t(2));
    QCOMPARE(formulaNodeCount(analysis.ast.get()), std::size_t(8));

    const FormulaAnalysis literal = analyzeFormula("1, 2 + 3, 4");
    QVERIFY2(literal.issues.empty(), "Array literal syntax must parse");
    const auto *literalNode = dynamic_cast<const ArrayLiteralNode *>(literal.ast.get());
    QVERIFY(literalNode);
    QCOMPARE(literalNode->values.size(), std::size_t(3));
    QCOMPARE(literalNode->span, (SourceSpan {0, 11}));
    QCOMPARE(formulaNodeCount(literal.ast.get()), std::size_t(6));

    const FormulaAnalysis qualified = analyzeFormula("Location.Boston");
    QCOMPARE(dynamic_cast<const ReferenceNode *>(qualified.ast.get())->nameKey, std::string("location.boston"));
    QVERIFY(!analyzeFormula("A[@0]").issues.empty());
    QVERIFY(!analyzeFormula("A[@1.5]").issues.empty());
    QVERIFY(!analyzeFormula("A[1:]").issues.empty());
    QVERIFY(!analyzeFormula("*").issues.empty());
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

    ModelAnalysis analysis = analyzeDocument(document);
    const ElementAnalysis &salesAnalysis = element(analysis, sales);
    QCOMPARE(salesAnalysis.references, (std::unordered_set<EntityId> {revenue, costs}));
    QVERIFY(!hasIssue(analysis, "undefined-variable"));
    QVERIFY(!hasIssue(analysis, "parse"));
    QVERIFY(!hasIssue(analysis, "algebraic-cycle"));
    QCOMPARE(analysis.dependencies.edges.at(revenue), std::vector<EntityId> {sales});
    QCOMPARE(analysis.dependencies.edges.at(costs), std::vector<EntityId> {sales});

    document.setEquation(recursive, "");
    document.setElementEquation(recursive, {0}, "Revenue[Boston, 2]");
    analysis = analyzeDocument(document);
    const ElementAnalysis &recursiveAnalysis = element(analysis, recursive);
    QCOMPARE(recursiveAnalysis.elementFormulas.size(), std::size_t(1));
    QCOMPARE(recursiveAnalysis.elementFormulas[0].indices, std::vector<std::size_t> {0});
    QCOMPARE(recursiveAnalysis.references, std::unordered_set<EntityId> {revenue});
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
    QCOMPARE(model->dimensions, captured.dimensions);
    QCOMPARE(model->elementEquations, captured.elementEquations);

    QVERIFY(document.rename(input, "Rate"));
    model = static_cast<const ModelElement *>(document.find(array));
    QCOMPARE(model->elementEquations[0].equation, std::string("Rate"));
    QCOMPARE(model->elementEquations[1].equation, std::string("Rate * 2"));
}

} // namespace

QTEST_APPLESS_MAIN(TestArrays)
#include "TestArrays.moc"
