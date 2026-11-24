#include <QDir>
#include <QIcon>
#include <QtTest>

class TestIcons : public QObject
{
    Q_OBJECT

private slots:

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
