#pragma once

#include <QListWidget>

class EntityPalette : public QListWidget
{
public:
    explicit EntityPalette(QWidget *parent = nullptr);
    static QString mimeType();

protected:
    QMimeData *mimeData(const QList<QListWidgetItem *> items) const override;
};
