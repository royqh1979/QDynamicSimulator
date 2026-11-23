#pragma once

#include <QGraphicsView>

class DiagramView : public QGraphicsView
{
public:
    explicit DiagramView(QGraphicsScene *scene, QWidget *parent = nullptr);

protected:
    void wheelEvent(QWheelEvent *event) override;
};
