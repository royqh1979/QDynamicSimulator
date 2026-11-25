#include "Translation.h"

#include <QCoreApplication>
#include <QLocale>
#include <QTranslator>

void installApplicationTranslation(QCoreApplication &application)
{
    if (QLocale::system().language() != QLocale::Chinese)
        return;
    auto *translator = new QTranslator(&application);
    if (translator->load(QStringLiteral(":/i18n/qds_zh_CN.qm")))
        application.installTranslator(translator);
    else
        delete translator;
}
