#pragma once

#include <QGraphicsView>

class MiniMapView : public QGraphicsView
{
public:
    explicit MiniMapView(QGraphicsScene *scene, QWidget *parent = nullptr);

protected:
    void resizeEvent(QResizeEvent *event) override;
};
