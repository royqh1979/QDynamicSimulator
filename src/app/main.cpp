#include "MainWindow.h"

#include <QApplication>
#include <QCoreApplication>

int main(int argc, char *argv[])
{
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QCoreApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
    QApplication application(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("QDynamicSimulator"));
    QCoreApplication::setApplicationName(QStringLiteral("QDynamicSimulator"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));

    MainWindow window;
    if (argc > 1 && !window.openFile(QString::fromLocal8Bit(argv[1])))
        return 1;
    if (argc == 1)
        window.offerRecovery();
    window.show();

    return QApplication::exec();
}
