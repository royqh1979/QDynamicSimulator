#include <core/formula/Formula.h>

#include <QtTest>

#include <type_traits>
#include <utility>

using namespace qds::core;

static_assert(std::is_same_v<decltype(std::declval<const Formula &>().root()), const FormulaNode *>);
static_assert(std::is_same_v<decltype(std::declval<const CallNode &>().argument(0)), const FormulaNode *>);
static_assert(std::is_same_v<decltype(std::declval<const SubscriptNode &>().index(0)), const FormulaNode *>);

namespace {

const BinaryNode *binary(const Formula &analysis, BinaryOperator op)
{
    const auto *node = dynamic_cast<const BinaryNode *>(analysis.root());
    return node && node->op() == op ? node : nullptr;
}

const UnaryNode *unary(const Formula &analysis, UnaryOperator op)
{
    const auto *node = dynamic_cast<const UnaryNode *>(analysis.root());
    return node && node->op() == op ? node : nullptr;
}

const ReferenceNode *reference(const FormulaNode *node)
{
    return dynamic_cast<const ReferenceNode *>(node);
}

const CallNode *call(const FormulaNode *node)
{
    return dynamic_cast<const CallNode *>(node);
}

class TestFormula : public QObject
{
    Q_OBJECT

private slots:
    void emptyAndNumbers();
    void constantClassification();
    void precedence();
    void callsAndConditionals();
    void identifiersCommentsAndSubscripts();
    void diagnostics();
    void spans();
};

void TestFormula::emptyAndNumbers()
{
    QVERIFY(!analyzeFormula("").root());
    QVERIFY(analyzeFormula("").issues().empty());
    QCOMPARE(analyzeFormula("   ").issues().empty(), true);
    const auto decimal = analyzeFormula(".375");
    QCOMPARE(dynamic_cast<const NumberNode *>(decimal.root())->value(), 0.375);
    const auto integer = analyzeFormula("14.");
    QCOMPARE(dynamic_cast<const NumberNode *>(integer.root())->value(), 14.0);
    const auto exponent = analyzeFormula("6E5");
    QCOMPARE(dynamic_cast<const NumberNode *>(exponent.root())->value(), 600000.0);
    const auto positiveAnalysis = analyzeFormula("+8.123e-10");
    const auto positive = unary(positiveAnalysis, UnaryOperator::Positive);
    QVERIFY(positive);
    QCOMPARE(dynamic_cast<const NumberNode *>(positive->operand())->value(), 8.123e-10);
    QVERIFY(!analyzeFormula("1.2.3").issues().empty());
    QVERIFY(!analyzeFormula("1e").issues().empty());
}

void TestFormula::constantClassification()
{
    for (const QString &source : {QStringLiteral("0"), QStringLiteral(".375"), QStringLiteral("14."),
                                  QStringLiteral("6E5"), QStringLiteral("+8.123e-10"), QStringLiteral("-1")})
        QVERIFY(isConstantFormula(source));
    for (const QString &source : {QString(), QStringLiteral("A"), QStringLiteral("1 + 2"),
                                  QStringLiteral("1.2.3"), QStringLiteral("NOT 1")})
        QVERIFY(!isConstantFormula(source));
}

void TestFormula::precedence()
{
    const auto sumAnalysis = analyzeFormula("a+b*c");
    const auto *sum = binary(sumAnalysis, BinaryOperator::Add);
    QVERIFY(sum);
    QVERIFY(reference(sum->left()));
    const auto *product = dynamic_cast<const BinaryNode *>(sum->right());
    QVERIFY(product && product->op() == BinaryOperator::Multiply);

    const auto powerAnalysis = analyzeFormula("a^b^c");
    const auto *power = binary(powerAnalysis, BinaryOperator::Power);
    QVERIFY(power);
    QVERIFY(reference(power->left()));
    const auto *nested = dynamic_cast<const BinaryNode *>(power->right());
    QVERIFY(nested && nested->op() == BinaryOperator::Power);

    const auto negativeAnalysis = analyzeFormula("-x^2");
    const auto *negative = unary(negativeAnalysis, UnaryOperator::Negative);
    QVERIFY(negative);
    const auto *squared = dynamic_cast<const BinaryNode *>(negative->operand());
    QVERIFY(squared && squared->op() == BinaryOperator::Power);

    const auto powerNegativeAnalysis = analyzeFormula("a^-b");
    const auto *powerNegative = binary(powerNegativeAnalysis, BinaryOperator::Power);
    QVERIFY(powerNegative);
    QVERIFY(dynamic_cast<const UnaryNode *>(powerNegative->right()));

    const auto logicalAnalysis = analyzeFormula("a = b AND c OR d");
    const auto *logical = binary(logicalAnalysis, BinaryOperator::Or);
    QVERIFY(logical);
    const auto *andNode = dynamic_cast<const BinaryNode *>(logical->left());
    QVERIFY(andNode && andNode->op() == BinaryOperator::And);
    const auto *equality = dynamic_cast<const BinaryNode *>(andNode->left());
    QVERIFY(equality && equality->op() == BinaryOperator::Equal);
}

void TestFormula::callsAndConditionals()
{
    const auto sumAnalysis = analyzeFormula("MAX(a,b)+ABS(-c)");
    const auto *sum = binary(sumAnalysis, BinaryOperator::Add);
    QVERIFY(sum);
    const auto *max = call(sum->left());
    QVERIFY(max && max->argumentCount() == 2);
    QCOMPARE(max->nameKey(), QString("max"));
    const auto *abs = call(sum->right());
    QVERIFY(abs && abs->argumentCount() == 1);
    QVERIFY(dynamic_cast<const UnaryNode *>(abs->argument(0)));

    const auto conditionalAnalysis = analyzeFormula("IF a > 0 THEN b ELSE c");
    const auto *conditional = dynamic_cast<const ConditionalNode *>(conditionalAnalysis.root());
    QVERIFY(conditional);
    QVERIFY(dynamic_cast<const BinaryNode *>(conditional->condition()));
    QVERIFY(reference(conditional->thenExpression()));
    QVERIFY(reference(conditional->elseExpression()));

    const auto alternativeAnalysis = analyzeFormula("if_then_else(a,b,c)");
    const auto *alternative = call(alternativeAnalysis.root());
    QVERIFY(alternative && alternative->argumentCount() == 3);
    QCOMPARE(alternative->nameKey(), QString("if_then_else"));
    const auto emptyCallAnalysis = analyzeFormula("F()");
    const auto *emptyCall = call(emptyCallAnalysis.root());
    QVERIFY(emptyCall && emptyCall->argumentCount() == 0);
    const auto namespacedAnalysis = analyzeFormula("isee.HISTORY(x)");
    const auto *namespaced = call(namespacedAnalysis.root());
    QVERIFY(namespaced);
    QCOMPARE(namespaced->nameKey(), QString("isee.history"));
    const auto timeAnalysis = analyzeFormula("TIME");
    QCOMPARE(reference(timeAnalysis.root())->nameKey(), QString("time"));
}

void TestFormula::identifiersCommentsAndSubscripts()
{
    const auto productAnalysis = analyzeFormula("\"流入 速率\" { comment } * \"库存\"");
    const auto *product = binary(productAnalysis, BinaryOperator::Multiply);
    QVERIFY(product);
    QCOMPARE(reference(product->left())->nameKey(), QString("流入_速率"));
    QCOMPARE(reference(product->right())->nameKey(), QString("库存"));
    const auto escapedAnalysis = analyzeFormula("\"a\\\"b\"");
    QCOMPARE(reference(escapedAnalysis.root())->nameKey(), QString("a\"b"));

    const auto transposeAnalysis = analyzeFormula("A[1,x+2]'");
    const auto *transpose = unary(transposeAnalysis, UnaryOperator::Transpose);
    QVERIFY(transpose);
    const auto *subscript = dynamic_cast<const SubscriptNode *>(transpose->operand());
    QVERIFY(subscript && subscript->indexCount() == 2);
    const auto *indexSum = dynamic_cast<const BinaryNode *>(subscript->index(1));
    QVERIFY(indexSum && indexSum->op() == BinaryOperator::Add);
    QVERIFY(!analyzeFormula("A[]").issues().empty());
}

void TestFormula::diagnostics()
{
    const Formula incomplete = analyzeFormula("(a+b");
    QCOMPARE(incomplete.source(), QString("(a+b"));
    QVERIFY(!incomplete.issues().empty());
    QVERIFY(incomplete.root());
    QVERIFY(!analyzeFormula("a + * b").issues().empty());
    QVERIFY(!analyzeFormula("a{comment").issues().empty());
    QVERIFY(!analyzeFormula("\"abc").issues().empty());
    QVERIFY(!analyzeFormula("a b").issues().empty());
    QVERIFY(!analyzeFormula("IF a THEN b").issues().empty());
    QVERIFY(!analyzeFormula("a==b").issues().empty());
    QVERIFY(!analyzeFormula("a && b").issues().empty());
    QVERIFY(!analyzeFormula("a ? b : c").issues().empty());
}

void TestFormula::spans()
{
    const auto analysis = analyzeFormula("a + \"库存\"");
    const auto *root = binary(analysis, BinaryOperator::Add);
    QVERIFY(root);
    QCOMPARE(root->span(), (SourceSpan {0, 8}));
    const auto *name = reference(root->right());
    QVERIFY(name);
    QCOMPARE(name->span(), (SourceSpan {4, 8}));

    const auto conditionalSpanAnalysis = analyzeFormula("IF a THEN b ELSE c");
    const auto *conditional = dynamic_cast<const ConditionalNode *>(conditionalSpanAnalysis.root());
    QVERIFY(conditional);
    QCOMPARE(conditional->span(), (SourceSpan {0, 18}));
}

} // namespace

QTEST_MAIN(TestFormula)
#include "TestFormula.moc"
