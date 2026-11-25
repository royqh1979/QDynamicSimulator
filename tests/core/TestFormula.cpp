#include <qds/core/Formula.h>

#include <QtTest>

using namespace qds::core;

namespace {

const BinaryNode *binary(const FormulaAnalysis &analysis, BinaryOperator op)
{
    const auto *node = dynamic_cast<const BinaryNode *>(analysis.ast.get());
    return node && node->op == op ? node : nullptr;
}

const UnaryNode *unary(const FormulaAnalysis &analysis, UnaryOperator op)
{
    const auto *node = dynamic_cast<const UnaryNode *>(analysis.ast.get());
    return node && node->op == op ? node : nullptr;
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
    void precedence();
    void callsAndConditionals();
    void identifiersCommentsAndSubscripts();
    void diagnostics();
    void spans();
};

void TestFormula::emptyAndNumbers()
{
    QVERIFY(!analyzeFormula("").ast);
    QVERIFY(analyzeFormula("").issues.empty());
    QCOMPARE(analyzeFormula("   ").issues.empty(), true);
    const auto decimal = analyzeFormula(".375");
    QCOMPARE(dynamic_cast<const NumberNode *>(decimal.ast.get())->value, 0.375);
    const auto integer = analyzeFormula("14.");
    QCOMPARE(dynamic_cast<const NumberNode *>(integer.ast.get())->value, 14.0);
    const auto exponent = analyzeFormula("6E5");
    QCOMPARE(dynamic_cast<const NumberNode *>(exponent.ast.get())->value, 600000.0);
    const auto positiveAnalysis = analyzeFormula("+8.123e-10");
    const auto positive = unary(positiveAnalysis, UnaryOperator::Positive);
    QVERIFY(positive);
    QCOMPARE(dynamic_cast<const NumberNode *>(positive->operand.get())->value, 8.123e-10);
    QVERIFY(!analyzeFormula("1.2.3").issues.empty());
    QVERIFY(!analyzeFormula("1e").issues.empty());
}

void TestFormula::precedence()
{
    const auto sumAnalysis = analyzeFormula("a+b*c");
    const auto *sum = binary(sumAnalysis, BinaryOperator::Add);
    QVERIFY(sum);
    QVERIFY(reference(sum->left.get()));
    const auto *product = dynamic_cast<const BinaryNode *>(sum->right.get());
    QVERIFY(product && product->op == BinaryOperator::Multiply);

    const auto powerAnalysis = analyzeFormula("a^b^c");
    const auto *power = binary(powerAnalysis, BinaryOperator::Power);
    QVERIFY(power);
    QVERIFY(reference(power->left.get()));
    const auto *nested = dynamic_cast<const BinaryNode *>(power->right.get());
    QVERIFY(nested && nested->op == BinaryOperator::Power);

    const auto negativeAnalysis = analyzeFormula("-x^2");
    const auto *negative = unary(negativeAnalysis, UnaryOperator::Negative);
    QVERIFY(negative);
    const auto *squared = dynamic_cast<const BinaryNode *>(negative->operand.get());
    QVERIFY(squared && squared->op == BinaryOperator::Power);

    const auto powerNegativeAnalysis = analyzeFormula("a^-b");
    const auto *powerNegative = binary(powerNegativeAnalysis, BinaryOperator::Power);
    QVERIFY(powerNegative);
    QVERIFY(dynamic_cast<const UnaryNode *>(powerNegative->right.get()));

    const auto logicalAnalysis = analyzeFormula("a = b AND c OR d");
    const auto *logical = binary(logicalAnalysis, BinaryOperator::Or);
    QVERIFY(logical);
    const auto *andNode = dynamic_cast<const BinaryNode *>(logical->left.get());
    QVERIFY(andNode && andNode->op == BinaryOperator::And);
    const auto *equality = dynamic_cast<const BinaryNode *>(andNode->left.get());
    QVERIFY(equality && equality->op == BinaryOperator::Equal);
}

void TestFormula::callsAndConditionals()
{
    const auto sumAnalysis = analyzeFormula("MAX(a,b)+ABS(-c)");
    const auto *sum = binary(sumAnalysis, BinaryOperator::Add);
    QVERIFY(sum);
    const auto *max = call(sum->left.get());
    QVERIFY(max && max->arguments.size() == 2);
    QCOMPARE(max->nameKey, std::string("max"));
    const auto *abs = call(sum->right.get());
    QVERIFY(abs && abs->arguments.size() == 1);
    QVERIFY(dynamic_cast<const UnaryNode *>(abs->arguments.front().get()));

    const auto conditionalAnalysis = analyzeFormula("IF a > 0 THEN b ELSE c");
    const auto *conditional = dynamic_cast<const ConditionalNode *>(conditionalAnalysis.ast.get());
    QVERIFY(conditional);
    QVERIFY(dynamic_cast<const BinaryNode *>(conditional->condition.get()));
    QVERIFY(reference(conditional->thenExpression.get()));
    QVERIFY(reference(conditional->elseExpression.get()));

    const auto alternativeAnalysis = analyzeFormula("if_then_else(a,b,c)");
    const auto *alternative = call(alternativeAnalysis.ast.get());
    QVERIFY(alternative && alternative->arguments.size() == 3);
    QCOMPARE(alternative->nameKey, std::string("if_then_else"));
    const auto emptyCallAnalysis = analyzeFormula("F()");
    const auto *emptyCall = call(emptyCallAnalysis.ast.get());
    QVERIFY(emptyCall && emptyCall->arguments.empty());
    const auto namespacedAnalysis = analyzeFormula("isee.HISTORY(x)");
    const auto *namespaced = call(namespacedAnalysis.ast.get());
    QVERIFY(namespaced);
    QCOMPARE(namespaced->nameKey, std::string("isee.history"));
    const auto timeAnalysis = analyzeFormula("TIME");
    QCOMPARE(reference(timeAnalysis.ast.get())->nameKey, std::string("time"));
}

void TestFormula::identifiersCommentsAndSubscripts()
{
    const auto productAnalysis = analyzeFormula("\"流入 速率\" { comment } * \"库存\"");
    const auto *product = binary(productAnalysis, BinaryOperator::Multiply);
    QVERIFY(product);
    QCOMPARE(reference(product->left.get())->nameKey, std::string("流入_速率"));
    QCOMPARE(reference(product->right.get())->nameKey, std::string("库存"));
    const auto escapedAnalysis = analyzeFormula("\"a\\\"b\"");
    QCOMPARE(reference(escapedAnalysis.ast.get())->nameKey, std::string("a\"b"));

    const auto transposeAnalysis = analyzeFormula("A[1,x+2]'");
    const auto *transpose = unary(transposeAnalysis, UnaryOperator::Transpose);
    QVERIFY(transpose);
    const auto *subscript = dynamic_cast<const SubscriptNode *>(transpose->operand.get());
    QVERIFY(subscript && subscript->indices.size() == 2);
    const auto *indexSum = dynamic_cast<const BinaryNode *>(subscript->indices[1].get());
    QVERIFY(indexSum && indexSum->op == BinaryOperator::Add);
    QVERIFY(!analyzeFormula("A[]").issues.empty());
}

void TestFormula::diagnostics()
{
    QVERIFY(!analyzeFormula("(a+b").issues.empty());
    QVERIFY(!analyzeFormula("a + * b").issues.empty());
    QVERIFY(!analyzeFormula("a{comment").issues.empty());
    QVERIFY(!analyzeFormula("\"abc").issues.empty());
    QVERIFY(!analyzeFormula("a b").issues.empty());
    QVERIFY(!analyzeFormula("IF a THEN b").issues.empty());
    QVERIFY(!analyzeFormula("a==b").issues.empty());
    QVERIFY(!analyzeFormula("a && b").issues.empty());
    QVERIFY(!analyzeFormula("a ? b : c").issues.empty());
}

void TestFormula::spans()
{
    const auto analysis = analyzeFormula("a + \"库存\"");
    const auto *root = binary(analysis, BinaryOperator::Add);
    QVERIFY(root);
    QCOMPARE(root->span, (SourceSpan {0, 12}));
    const auto *name = reference(root->right.get());
    QVERIFY(name);
    QCOMPARE(name->span, (SourceSpan {4, 12}));

    const auto conditionalSpanAnalysis = analyzeFormula("IF a THEN b ELSE c");
    const auto *conditional = dynamic_cast<const ConditionalNode *>(conditionalSpanAnalysis.ast.get());
    QVERIFY(conditional);
    QCOMPARE(conditional->span, (SourceSpan {0, 18}));
}

} // namespace

QTEST_MAIN(TestFormula)
#include "TestFormula.moc"
