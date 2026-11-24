#include <QDir>
#include <QFile>
#include <QIcon>
#include <QtTest>

class TestIcons : public QObject
{
    Q_OBJECT

private slots:
    void flowAndConnectorSvgStructure()
    {
        const auto contents = [](const QString &path) {
            QFile file(path);
            if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
                return QByteArray();
            return file.readAll();
        };
        const QByteArray flow = contents(QStringLiteral(":/icons/flow.svg"));
        QVERIFY(flow.contains("M2 8h6m8 0h6M2 16h6m8 0h6"));
        QVERIFY(flow.contains("M8 5h8l-4 7 4 7H8l4-7z"));
        const QByteArray connector = contents(QStringLiteral(":/icons/connect.svg"));
        QVERIFY(!connector.contains("stroke-dasharray"));
        QVERIFY(connector.contains("M13 5h7v7"));
    }

    void allSvgIconsRender()
    {
        QDir directory(QStringLiteral(":/icons"));
        const QStringList files = directory.entryList({QStringLiteral("*.svg")}, QDir::Files);
        QCOMPARE(files.size(), 48);
        for (const QString &file : files) {
            const QString path = directory.absoluteFilePath(file);
            const QIcon icon(path);
            QVERIFY2(!icon.isNull(), qPrintable(path));
            QVERIFY2(!icon.pixmap(24, 24).isNull(), qPrintable(path));
            QVERIFY2(!icon.pixmap(48, 48).isNull(), qPrintable(path));
        }
    }
};

QTEST_MAIN(TestIcons)
#include "TestIcons.moc"
