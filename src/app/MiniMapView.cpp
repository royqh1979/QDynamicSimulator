#include "MiniMapView.h"

MiniMapView::MiniMapView(QGraphicsScene *scene, QWidget *parent)
    : QGraphicsView(scene, parent)
{
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setInteractive(false);
    setRenderHint(QPainter::Antialiasing);
    setMinimumHeight(160);
    connect(scene, &QGraphicsScene::changed, this, [this]() {
        if (!this->scene()->itemsBoundingRect().isEmpty())
            fitInView(this->scene()->itemsBoundingRect(), Qt::KeepAspectRatio);
    });
}

void MiniMapView::resizeEvent(QResizeEvent *event)
{
    QGraphicsView::resizeEvent(event);
    if (!scene()->itemsBoundingRect().isEmpty())
        fitInView(scene()->itemsBoundingRect(), Qt::KeepAspectRatio);
}
