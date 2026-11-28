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

bool hasWarning(const AnalysisReport &analysis, EntityId id)
{
    return std::any_of(analysis.issues().begin(), analysis.issues().end(), [id](const ModelIssue &issue) {
        return issue.entityId == id && issue.severity == IssueSeverity::Warning;
    });
}

class TestAnalysis : public QObject
{
    Q_OBJECT

private slots:
    void symbolResolution();
    void functionSignatures();
    void dependencyCycles();
    void structuralUse();
};

void TestAnalysis::symbolResolution()
{
    Document document;
    const EntityId stock = document.createVariable(ElementKind::Stock);
    const EntityId first = document.createVariable(ElementKind::Variable);
    const EntityId second = document.createVariable(ElementKind::Variable);
    const EntityId third = document.createVariable(ElementKind::Variable);
    document.setEquation(first, "Stock1 + MAX(1, 2) + TIME + PI");
    document.setEquation(second, "Variable1 + ABS(Stock1) + Stock1");
    document.setEquation(third, "Variable1(3) + UnknownFn(Stock1) + Missing + ABS(1, 2)");
    const AnalysisReport analysis = analyzeDocument(document);
    QCOMPARE(analysis.references(first), QSet<EntityId> {stock});
    QCOMPARE(analysis.references(second), (QSet<EntityId> {first, stock}));
    QCOMPARE(analysis.references(third), (QSet<EntityId> {first, stock}));
    QVERIFY(hasIssue(analysis, "unknown-function"));
    QVERIFY(hasIssue(analysis, "undefined-variable"));
    QVERIFY(hasIssue(analysis, "invalid-arity"));
    QVERIFY(!hasIssue(analysis, "parse"));
}

void TestAnalysis::functionSignatures()
{
    Document document;
    const EntityId auxiliary = document.createVariable(ElementKind::Variable);
    document.setEquation(auxiliary,
        "DELAYN(Variable1, 1, 2) + NORMAL(0, 1) + SMTHN(Variable1, 1, 2) + RANK(Variable1, 1) + SIZE(Variable1) + SELF"
        " + LOG(Variable1) + INTEGR(Variable1, 1) + SMOOTH(Variable1, 1) + LOOKUP(Variable1)"
        " + RAMP(1, 2, 3) + STEP(1, 2) + PULSE(1, 2) + pulse_train(1, 2, 3, 4)"
        " + FORCST(Variable1, 1, 2) + TREND(Variable1, 1)"
        " + RANDOM(0, 1) + POISSON(1) + EXPRND(1) + LOGNORMAL(1, 2) + SUM(Variable1) + MEAN(Variable1)"
        " + STDDEV(Variable1) + PREVIOUS(Variable1, 0) + INIT(Variable1) + if_then_else(1, 2, 3)"
        " + ARCCOS(1) + ARCSIN(1) + ARCTAN(1) + COS(1) + EXP(1) + INT(1) + LN(1) + LOG10(1)"
        " + MIN(1, 2) + SIN(1) + SQRT(1) + TAN(1) + DELAY(Variable1, 1) + DELAY1(Variable1, 1)"
        " + DELAY3(Variable1, 1) + SMTH1(Variable1, 1) + SMTH3(Variable1, 1) + DT + STARTTIME + STOPTIME");
    const AnalysisReport analysis = analyzeDocument(document);
    QVERIFY(!hasIssue(analysis, "unknown-function"));
    QVERIFY(!hasIssue(analysis, "invalid-arity"));
    QVERIFY(!hasIssue(analysis, "undefined-variable"));
    QVERIFY(!hasIssue(analysis, "parse"));

    document.setEquation(auxiliary, "POISSON(1, 2) + RANK(Variable1, 1, 2)");
    QVERIFY(!hasIssue(analyzeDocument(document), "invalid-arity"));
    const QVector<QString> invalidCalls {
        "POISSON()", "POISSON(1, 2, 3)", "RANK(Variable1)", "RANK(Variable1, 1, 2, 3)",
        "ABS()", "ABS(1, 2)", "MAX()", "MAX(1, 2, 3)", "PREVIOUS(Variable1)",
        "PULSE(1, 2, 3)", "RAMP(1, 2)", "pulse_train(1, 2, 3)", "pulse_train(1, 2, 3, 4, 5)"
    };
    for (const QString &call : invalidCalls) {
        document.setEquation(auxiliary, call);
        QVERIFY2(hasIssue(analyzeDocument(document), "invalid-arity"), qPrintable(call));
    }
}

void TestAnalysis::dependencyCycles()
{
    Document document;
    const EntityId first = document.createVariable(ElementKind::Variable);
    const EntityId second = document.createVariable(ElementKind::Variable);
    document.setEquation(first, "Variable2");
    document.setEquation(second, "Variable1");
    const AnalysisReport analysis = analyzeDocument(document);
    QVERIFY(hasIssue(analysis, "algebraic-cycle"));
    QCOMPARE(analysis.dependencies().edges().value(first), QVector<EntityId> {second});
    QCOMPARE(analysis.dependencies().edges().value(second), QVector<EntityId> {first});

    Document connectors;
    const EntityId source = connectors.createVariable(ElementKind::Variable);
    const EntityId target = connectors.createVariable(ElementKind::Variable);
    connectors.createConnector(source, target);
    connectors.createConnector(target, source);
    QVERIFY(!hasIssue(analyzeDocument(connectors), "algebraic-cycle"));
}

void TestAnalysis::structuralUse()
{
    Document document;
    const EntityId stock = document.createVariable(ElementKind::Stock);
    const EntityId first = document.createVariable(ElementKind::Variable);
    const EntityId second = document.createVariable(ElementKind::Variable);
    document.setEquation(first, "Stock1");
    document.setEquation(second, "Variable1");
    const EntityId flowedStock = document.createVariable(ElementKind::Stock);
    const EntityId cloud = document.createCloud();
    const EntityId flow = document.createFlow(flowedStock, cloud);
    const AnalysisReport analysis = analyzeDocument(document);
    QVERIFY(!hasWarning(analysis, stock));
    QVERIFY(!hasWarning(analysis, first));
    QVERIFY(hasWarning(analysis, second));
    QVERIFY(!hasWarning(analysis, flowedStock));
    QVERIFY(!hasWarning(analysis, flow));
}

} // namespace

QTEST_APPLESS_MAIN(TestAnalysis)
#include "TestAnalysis.moc"
