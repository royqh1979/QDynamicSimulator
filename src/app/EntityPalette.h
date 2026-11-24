#pragma once

#include "DiagramScene.h"

#include <QListWidget>

class EntityPalette : public QListWidget
{
    Q_OBJECT

public:
    explicit EntityPalette(QWidget *parent = nullptr);
    static QString mimeType();
    static constexpr int ToolRole = Qt::UserRole + 1;

signals:
    void toolRequested(DiagramScene::Tool tool);

protected:
    QMimeData *mimeData(const QList<QListWidgetItem *> items) const override;
};
