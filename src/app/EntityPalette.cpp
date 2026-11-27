#include "EntityPalette.h"

#include <QFrame>
#include <QIcon>
#include <QMimeData>

EntityPalette::EntityPalette(QWidget *parent)
    : QListWidget(parent)
{
    Q_INIT_RESOURCE(icons);
    setDragEnabled(true);
    setSelectionMode(SingleSelection);
    setIconSize(QSize(24, 24));
    const struct {
        const char *name;
        qds::core::ElementKind type;
        const char *icon;
    } entities[] = {
        {QT_TR_NOOP("Stock"), qds::core::ElementKind::Stock, "stock"},
        {QT_TR_NOOP("Variable"), qds::core::ElementKind::Variable, "variable"},
        {QT_TR_NOOP("Cloud"), qds::core::ElementKind::Cloud, "cloud"},
        {QT_TR_NOOP("Comment"), qds::core::ElementKind::Comment, "annotation"}};
    for (const auto &entity : entities) {
        const QString name = tr(entity.name);
        QListWidgetItem *item = new QListWidgetItem(
            QIcon(QStringLiteral(":/icons/%1.svg").arg(QLatin1String(entity.icon))), name, this);
        item->setData(Qt::UserRole, static_cast<int>(entity.type));
        item->setToolTip(tr("Drag onto the canvas to create %1").arg(name));
    }

    QListWidgetItem *separator = new QListWidgetItem(this);
    separator->setFlags(Qt::NoItemFlags);
    separator->setSizeHint(QSize(1, 12));
    QFrame *line = new QFrame(this);
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Sunken);
    setIndexWidget(indexFromItem(separator), line);

    const struct {
        const char *name;
        DiagramScene::Tool tool;
        const char *icon;
    } routes[] = {
        {QT_TR_NOOP("Flow"), DiagramScene::Tool::AddFlow, "flow"},
        {QT_TR_NOOP("Arrow"), DiagramScene::Tool::Connect, "connect"}};
    for (const auto &route : routes) {
        const QString name = tr(route.name);
        QListWidgetItem *item = new QListWidgetItem(
            QIcon(QStringLiteral(":/icons/%1.svg").arg(QLatin1String(route.icon))), name, this);
        item->setData(ToolRole, static_cast<int>(route.tool));
        item->setFlags(item->flags() & ~Qt::ItemIsDragEnabled);
        item->setToolTip(tr("Drag on the canvas to create %1").arg(name));
    }
    connect(this, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
        const QVariant tool = item->data(ToolRole);
        if (tool.isValid())
            emit toolRequested(static_cast<DiagramScene::Tool>(tool.toInt()));
    });
}

QString EntityPalette::mimeType()
{
    return QStringLiteral("application/x-qds-entity-type");
}

QMimeData *EntityPalette::mimeData(const QList<QListWidgetItem *> items) const
{
    if (items.isEmpty() || items.front()->data(ToolRole).isValid())
        return nullptr;
    QMimeData *data = new QMimeData();
    data->setData(mimeType(), QByteArray::number(items.front()->data(Qt::UserRole).toInt()));
    data->setText(items.front()->text());
    return data;
}
