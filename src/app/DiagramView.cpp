#include "DiagramView.h"

#include "DiagramScene.h"
#include "EntityPalette.h"

#include <QDragEnterEvent>
#include <QFileInfo>
#include <QGraphicsItem>
#include <QMimeData>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QScrollBar>
#include <QWheelEvent>
#include <QUrl>
#include <QtMath>

namespace {

bool isXmileFileDrop(const QMimeData *mime)
{
    if (!mime->hasUrls() || mime->urls().size() != 1)
        return false;
    const QUrl url = mime->urls().front();
    return url.isLocalFile() && QFileInfo(url.toLocalFile()).suffix().compare(QStringLiteral("xmile"), Qt::CaseInsensitive) == 0;
}

}

DiagramView::DiagramView(QGraphicsScene *scene, QWidget *parent)
    : QGraphicsView(scene, parent)
{
    setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);
    setAcceptDrops(true);
    viewport()->setAcceptDrops(true);
    setDragMode(RubberBandDrag);
    setTransformationAnchor(AnchorUnderMouse);
    setResizeAnchor(AnchorViewCenter);
}

void DiagramView::zoomIn()
{
    zoomBy(1.15);
}

void DiagramView::zoomOut()
{
    zoomBy(1.0 / 1.15);
}

void DiagramView::resetZoom()
{
    resetTransform();
}

void DiagramView::fitAll()
{
    const QRectF bounds = scene()->itemsBoundingRect();
    if (!bounds.isEmpty())
        fitInView(bounds.adjusted(-50.0, -50.0, 50.0, 50.0), Qt::KeepAspectRatio);
}

void DiagramView::fitSelection()
{
    QRectF bounds;
    const QList<QGraphicsItem *> selection = scene()->selectedItems();
    for (QGraphicsItem *item : selection)
        bounds = bounds.united(item->sceneBoundingRect());
    if (!bounds.isEmpty())
        fitInView(bounds.adjusted(-50.0, -50.0, 50.0, 50.0), Qt::KeepAspectRatio);
    else
        fitAll();
}

void DiagramView::wheelEvent(QWheelEvent *event)
{
    zoomBy(qPow(1.15, event->angleDelta().y() / 120.0));
    event->accept();
}

void DiagramView::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::MiddleButton || (event->button() == Qt::LeftButton && m_spacePressed)) {
        m_panning = true;
        m_lastPanPosition = event->pos();
        viewport()->setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }
    QGraphicsView::mousePressEvent(event);
}

void DiagramView::mouseMoveEvent(QMouseEvent *event)
{
    if (m_panning) {
        const QPoint delta = event->pos() - m_lastPanPosition;
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - delta.x());
        verticalScrollBar()->setValue(verticalScrollBar()->value() - delta.y());
        m_lastPanPosition = event->pos();
        event->accept();
        return;
    }
    QGraphicsView::mouseMoveEvent(event);
}

void DiagramView::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_panning && (event->button() == Qt::MiddleButton || event->button() == Qt::LeftButton)) {
        m_panning = false;
        viewport()->unsetCursor();
        event->accept();
        return;
    }
    QGraphicsView::mouseReleaseEvent(event);
}

void DiagramView::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Space) {
        m_spacePressed = true;
        event->accept();
        return;
    }
    QGraphicsView::keyPressEvent(event);
}

void DiagramView::keyReleaseEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Space) {
        m_spacePressed = false;
        event->accept();
        return;
    }
    QGraphicsView::keyReleaseEvent(event);
}

void DiagramView::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasFormat(EntityPalette::mimeType()) || isXmileFileDrop(event->mimeData()))
        event->acceptProposedAction();
}

void DiagramView::dragMoveEvent(QDragMoveEvent *event)
{
    if (event->mimeData()->hasFormat(EntityPalette::mimeType()) || isXmileFileDrop(event->mimeData()))
        event->acceptProposedAction();
}

void DiagramView::dropEvent(QDropEvent *event)
{
    if (isXmileFileDrop(event->mimeData())) {
        emit fileOpenRequested(event->mimeData()->urls().front().toLocalFile());
        event->acceptProposedAction();
        return;
    }
    if (!event->mimeData()->hasFormat(EntityPalette::mimeType()))
        return;
    bool ok = false;
    const int value = event->mimeData()->data(EntityPalette::mimeType()).toInt(&ok);
    const auto kind = static_cast<qds::core::ElementKind>(value);
    if (!ok || kind == qds::core::ElementKind::Flow || value < static_cast<int>(qds::core::ElementKind::Stock)
        || value > static_cast<int>(qds::core::ElementKind::Comment))
        return;
    DiagramScene *diagram = qobject_cast<DiagramScene *>(scene());
    if (diagram)
        diagram->createElementAt(kind, mapToScene(event->pos()));
    event->acceptProposedAction();
}

void DiagramView::zoomBy(double factor)
{
    const double targetScale = transform().m11() * factor;
    if (targetScale < 0.1 || targetScale > 5.0)
        return;
    scale(factor, factor);
}
