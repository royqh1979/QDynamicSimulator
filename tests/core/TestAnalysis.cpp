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
    const EntityId first = document.createVariable(ElementKind::Auxiliary);
    const EntityId second = document.createVariable(ElementKind::Auxiliary);
    const EntityId third = document.createVariable(ElementKind::Auxiliary);
    document.setEquation(first, "Stock1 + MAX(1, 2) + TIME + PI");
    document.setEquation(second, "Auxiliary1 + ABS(Stock1) + Stock1");
    document.setEquation(third, "Auxiliary1(3) + UnknownFn(Stock1) + Missing + ABS(1, 2)");
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
    const EntityId auxiliary = document.createVariable(ElementKind::Auxiliary);
    document.setEquation(auxiliary,
        "DELAYN(Auxiliary1, 1, 2) + NORMAL(0, 1) + SMTHN(Auxiliary1, 1, 2) + RANK(Auxiliary1, 1) + SIZE(Auxiliary1) + SELF"
        " + LOG(Auxiliary1) + INTEGR(Auxiliary1, 1) + SMOOTH(Auxiliary1, 1) + LOOKUP(Auxiliary1)"
        " + RAMP(1, 2) + STEP(1, 2) + PULSE(1, 2) + FORCST(Auxiliary1, 1, 2) + TREND(Auxiliary1, 1)"
        " + RANDOM(0, 1) + POISSON(1) + EXPRND(1) + LOGNORMAL(1, 2) + SUM(Auxiliary1) + MEAN(Auxiliary1)"
        " + STDDEV(Auxiliary1) + PREVIOUS(Auxiliary1, 0) + INIT(Auxiliary1) + if_then_else(1, 2, 3)"
        " + ARCCOS(1) + ARCSIN(1) + ARCTAN(1) + COS(1) + EXP(1) + INT(1) + LN(1) + LOG10(1)"
        " + MIN(1, 2) + SIN(1) + SQRT(1) + TAN(1) + DELAY(Auxiliary1, 1) + DELAY1(Auxiliary1, 1)"
        " + DELAY3(Auxiliary1, 1) + SMTH1(Auxiliary1, 1) + SMTH3(Auxiliary1, 1) + DT + STARTTIME + STOPTIME");
    const AnalysisReport analysis = analyzeDocument(document);
    QVERIFY(!hasIssue(analysis, "unknown-function"));
    QVERIFY(!hasIssue(analysis, "invalid-arity"));
    QVERIFY(!hasIssue(analysis, "undefined-variable"));
    QVERIFY(!hasIssue(analysis, "parse"));

    document.setEquation(auxiliary, "POISSON(1, 2) + RANK(Auxiliary1, 1, 2)");
    QVERIFY(!hasIssue(analyzeDocument(document), "invalid-arity"));
    const QVector<QString> invalidCalls {
        "POISSON()", "POISSON(1, 2, 3)", "RANK(Auxiliary1)", "RANK(Auxiliary1, 1, 2, 3)",
        "ABS()", "ABS(1, 2)", "MAX()", "MAX(1, 2, 3)", "PREVIOUS(Auxiliary1)"
    };
    for (const QString &call : invalidCalls) {
        document.setEquation(auxiliary, call);
        QVERIFY2(hasIssue(analyzeDocument(document), "invalid-arity"), qPrintable(call));
    }
}

void TestAnalysis::dependencyCycles()
{
    Document document;
    const EntityId first = document.createVariable(ElementKind::Auxiliary);
    const EntityId second = document.createVariable(ElementKind::Auxiliary);
    document.setEquation(first, "Auxiliary2");
    document.setEquation(second, "Auxiliary1");
    const AnalysisReport analysis = analyzeDocument(document);
    QVERIFY(hasIssue(analysis, "algebraic-cycle"));
    QCOMPARE(analysis.dependencies().edges().value(first), QVector<EntityId> {second});
    QCOMPARE(analysis.dependencies().edges().value(second), QVector<EntityId> {first});

    Document connectors;
    const EntityId source = connectors.createVariable(ElementKind::Auxiliary);
    const EntityId target = connectors.createVariable(ElementKind::Auxiliary);
    connectors.createConnector(source, target);
    connectors.createConnector(target, source);
    QVERIFY(!hasIssue(analyzeDocument(connectors), "algebraic-cycle"));
}

void TestAnalysis::structuralUse()
{
    Document document;
    const EntityId stock = document.createVariable(ElementKind::Stock);
    const EntityId first = document.createVariable(ElementKind::Auxiliary);
    const EntityId second = document.createVariable(ElementKind::Auxiliary);
    document.setEquation(first, "Stock1");
    document.setEquation(second, "Auxiliary1");
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
