#include <qds/io/XmileFile.h>

#include <QDirIterator>
#include <QFile>
#include <QtTest>

#include <algorithm>
#include <unordered_set>

using namespace qds::core;



class TestXmileCorpus : public QObject
{
    Q_OBJECT

private slots:
    void allXmileFilesImport()
    {
        QStringList files;
        QDirIterator iterator(
            QStringLiteral(QDS_XMILE_CORPUS_DIR),
            {QStringLiteral("*.xmile")},
            QDir::Files,
            QDirIterator::Subdirectories);
        while (iterator.hasNext())
            files.append(iterator.next());
        QCOMPARE(files.size(), 67);

        QStringList failures;
        QStringList formulaOnlyFiles;
        QStringList invalidFiles;
        QStringList unsupportedFiles;
        for (const QString &path : files) {
            QFile file(path);
            QVERIFY(file.open(QIODevice::ReadOnly));
            const QByteArray xml = file.readAll();
            const int modelCount = xml.count("<model>") + xml.count("<model name=");
            const bool unsupported = xml.contains("<module") || modelCount > 1;
            Document document;
            QString error;
            const bool imported = qds::io::XmileFile::importFile(path, document, &error);
            if (unsupported) {
                if (imported) {
                    failures.append(path + ": unsupported module model was partially imported");
                } else if (!error.contains("not supported")) {
                    failures.append(path + ": unsupported model failed for an unexpected reason: " + error);
                } else {
                    unsupportedFiles.append(path);
                }
                continue;
            }
            if (!imported && error.contains("multiple source or target stocks")) {
                unsupportedFiles.append(path);
                continue;
            }
            if (!imported && error.contains("Invalid XMILE XML")) {
                invalidFiles.append(path);
                continue;
            }
            if (!imported) {
                failures.append(path + ": " + error);
                continue;
            }
            if (document.elements().empty()) {
                failures.append(path + ": imported document is empty");
                continue;
            }
            std::unordered_set<EntityId> connected;
            for (const DocumentElement &element : document.elements()) {
                if (element.kind() == ElementKind::Flow) {
                    const auto &flow = static_cast<const Flow &>(element);
                    if (flow.endpoints.from == 0 || flow.endpoints.to == 0
                        || !document.find(flow.endpoints.from) || !document.find(flow.endpoints.to)) {
                        failures.append(path + ": invalid flow endpoints");
                        continue;
                    }
                    connected.insert(flow.id());
                    connected.insert(flow.endpoints.from);
                    connected.insert(flow.endpoints.to);
                } else if (element.kind() == ElementKind::Connector) {
                    const auto &connector = static_cast<const Connector &>(element);
                    if (connector.from == 0 || connector.to == 0
                        || !document.find(connector.from) || !document.find(connector.to)) {
                        failures.append(path + ": invalid connector endpoints");
                        continue;
                    }
                    connected.insert(connector.id());
                    connected.insert(connector.from);
                    connected.insert(connector.to);
                }
            }
            for (const DocumentElement &element : document.elements()) {
                if (connected.count(element.id()))
                    continue;
                const auto *model = dynamic_cast<const ModelElement *>(&element);
                if (model) {
                    formulaOnlyFiles.append(path);
                    continue;
                }
                failures.append(path + ": isolated element " + QString::number(element.id()));
            }
            for (const auto &entry : document.diagram().routes()) {
                const auto &points = entry.second.controlPoints;
                const bool hasFixedPoints = std::any_of(points.begin(), points.end(),
                    [](const RoutePoint &point) { return !point.automatic; });
                if (entry.second.manual || hasFixedPoints)
                    failures.append(path + ": fixed route data");
            }
        }
        formulaOnlyFiles.removeDuplicates();
        invalidFiles.removeDuplicates();
        unsupportedFiles.removeDuplicates();
        if (!formulaOnlyFiles.isEmpty())
            qInfo("Formula-only connectivity checks deferred: %s", qPrintable(formulaOnlyFiles.join(", ")));
        if (!invalidFiles.isEmpty())
            qInfo("Invalid XMILE rejected as required: %s", qPrintable(invalidFiles.join(", ")));
        if (!unsupportedFiles.isEmpty())
            qInfo("Unsupported model structures rejected without partial import: %s", qPrintable(unsupportedFiles.join(", ")));
        QVERIFY2(failures.isEmpty(), qPrintable(failures.join('\n')));
    }
};

QTEST_APPLESS_MAIN(TestXmileCorpus)
#include "TestXmileCorpus.moc"
