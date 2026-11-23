#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QIcon>
#include <QTextStream>

int main(int argc, char **argv)
{
    QApplication application(argc, argv);
    const QDir directory(QStringLiteral(":/icons"));
    const QStringList files = directory.entryList({QStringLiteral("*.svg")}, QDir::Files);
    if (files.isEmpty())
        return 2;
    QElapsedTimer timer;
    timer.start();
    int rendered = 0;
    for (int repeat = 0; repeat < 100; ++repeat) {
        for (const QString &file : files) {
            const QIcon icon(directory.absoluteFilePath(file));
            rendered += icon.pixmap(24, 24).isNull() ? 0 : 1;
        }
    }
    const qint64 elapsed = timer.nsecsElapsed();
    const qint64 operations = files.size() * 100;
    QTextStream(stdout) << "operation,count,total_ms,per_operation_us\n"
                        << "svg_icon_render," << operations << "," << elapsed / 1e6 << ","
                        << elapsed / 1000.0 / operations << "\n";
    return rendered == operations ? 0 : 1;
}
