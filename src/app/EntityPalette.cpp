#include "EntityPalette.h"

#include <QMimeData>

EntityPalette::EntityPalette(QWidget *parent)
    : QListWidget(parent)
{
    setDragEnabled(true);
    setSelectionMode(SingleSelection);
    const QStringList names {tr("存量"), tr("流量"), tr("辅助变量"), tr("常量"), tr("外生变量"), tr("云团"), tr("注释")};
    for (int index = 0; index < names.size(); ++index) {
        QListWidgetItem *item = new QListWidgetItem(names[index], this);
        item->setData(Qt::UserRole, index);
        item->setToolTip(tr("拖拽到画布以创建%1").arg(names[index]));
    }
}

QString EntityPalette::mimeType()
{
    return QStringLiteral("application/x-qds-entity-type");
}

QMimeData *EntityPalette::mimeData(const QList<QListWidgetItem *> items) const
{
    if (items.isEmpty())
        return nullptr;
    QMimeData *data = new QMimeData();
    data->setData(mimeType(), QByteArray::number(items.front()->data(Qt::UserRole).toInt()));
    data->setText(items.front()->text());
    return data;
}
