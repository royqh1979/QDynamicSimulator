#include <core/Document.h>
#include <io/MdlFile.h>

#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>

using namespace qds::core;

namespace {

int elementCount(const Document &document)
{
    int result = 0;
    for (const DocumentElement &element : document.elements()) {
        Q_UNUSED(element);
        ++result;
    }
    return result;
}

QString mediumModel()
{
    QString text;
    for (int index = 0; index < 60; ++index) {
        text += QStringLiteral("rate %1=1~~|\n").arg(index);
        text += QStringLiteral("inflow %1=rate %1~~|\n").arg(index);
        text += QStringLiteral("outflow %1=inflow %1 * 0.1~~|\n").arg(index);
        text += QStringLiteral("stock %1=INTEG(inflow %1-outflow %1,10)~~|\n").arg(index);
        text += QStringLiteral("aux %1=ABS(stock %1)~~|\n").arg(index);
    }
    text += QStringLiteral("FINAL TIME=10~Month~|\nINITIAL TIME=0~Month~|\nSAVEPER=TIME STEP~Month~|\nTIME STEP=0.25~Month~|\n");
    return text;
}

bool profileText(const QString &name, const QString &text, int count)
{
    QString error;
    QVector<ModelIssue> warnings;
    Document initial;
    if (!qds::io::MdlFile::importText(text, initial, &error, &warnings)) {
        QTextStream(stderr) << name << " warmup failed: " << error << "\n";
        return false;
    }
    QElapsedTimer timer;
    timer.start();
    int elements = 0;
    int warningCount = 0;
    for (int index = 0; index < count; ++index) {
        Document document;
        if (!qds::io::MdlFile::importText(text, document, &error, &warnings)) {
            QTextStream(stderr) << name << " import failed: " << error << "\n";
            return false;
        }
        elements = elementCount(document);
        warningCount = warnings.size();
    }
    const qint64 elapsed = timer.nsecsElapsed();
    QTextStream(stdout) << name << ',' << count << ',' << elapsed / 1.0e6 << ','
                        << elapsed / 1000.0 / count << ',' << elements << ',' << warningCount << "\n";
    return true;
}

bool profileFile(const QString &name, const QString &path, int count)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        QTextStream(stderr) << "Cannot open profile input: " << path << "\n";
        return false;
    }
    return profileText(name, QString::fromUtf8(file.readAll()), count);
}

} // namespace

int main(int argc, char **argv)
{
    const int count = argc > 1 ? QString(argv[1]).toInt() : 50;
    if (count <= 0)
        return 2;
    QTextStream(stdout) << "operation,count,total_ms,per_operation_us,model_elements,warnings\n";
    if (argc > 2) {
        const QString path = QString::fromLocal8Bit(argv[2]);
        return profileFile("custom_" + QFileInfo(path).completeBaseName(), path, count) ? 0 : 3;
    }
    if (!profileFile("small_abs", QStringLiteral(QDS_ABS_MDL_PATH), count)
        || !profileFile("small_teacup", QStringLiteral(QDS_TEACUP_MDL_PATH), count)
        || !profileText("medium_300_variables", mediumModel(), count))
        return 4;
    return 0;
}
