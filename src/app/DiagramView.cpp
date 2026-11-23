#include "DiagramView.h"

#include <QWheelEvent>
#include <QtMath>

DiagramView::DiagramView(QGraphicsScene *scene, QWidget *parent)
    : QGraphicsView(scene, parent)
{
    setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);
    setDragMode(RubberBandDrag);
    setTransformationAnchor(AnchorUnderMouse);
    setResizeAnchor(AnchorViewCenter);
}

void DiagramView::wheelEvent(QWheelEvent *event)
{
    const double factor = qPow(1.15, event->angleDelta().y() / 120.0);
    const double targetScale = transform().m11() * factor;
    if (targetScale < 0.1 || targetScale > 5.0) {
        event->accept();
        return;
    }
    scale(factor, factor);
    event->accept();
}
