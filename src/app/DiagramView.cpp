#include "DiagramView.h"

#include "DiagramScene.h"
#include "DiagramPalette.h"
#include "EntityPalette.h"

#include <QDragEnterEvent>
#include <QFrame>
#include <QFileInfo>
#include <QGraphicsItem>
#include <QMimeData>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainterPath>
#include <QScrollBar>
#include <QWheelEvent>
#include <QUrl>
#include <QtMath>

namespace {

bool isDocumentFileDrop(const QMimeData *mime)
{
    if (!mime->hasUrls() || mime->urls().size() != 1)
        return false;
    const QUrl url = mime->urls().front();
    if (!url.isLocalFile())
        return false;
    const QString suffix = QFileInfo(url.toLocalFile()).suffix();
    return suffix.compare(QStringLiteral("qds"), Qt::CaseInsensitive) == 0
        || suffix.compare(QStringLiteral("xmile"), Qt::CaseInsensitive) == 0
        || suffix.compare(QStringLiteral("xml"), Qt::CaseInsensitive) == 0;
}

}

DiagramView::DiagramView(QGraphicsScene *scene, QWidget *parent)
    : QGraphicsView(scene, parent)
{
    setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);
    setAcceptDrops(true);
    viewport()->setAcceptDrops(true);
    setDragMode(NoDrag);
    setViewportUpdateMode(FullViewportUpdate);
    setTransformationAnchor(AnchorUnderMouse);
    setResizeAnchor(AnchorViewCenter);
    m_selectionFrame = new QFrame(viewport());
    m_selectionFrame->setObjectName(QStringLiteral("selectionFrame"));
    m_selectionFrame->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_selectionFrame->setAttribute(Qt::WA_StyledBackground);
    const QColor color = diagram_palette::SelectionColor;
    m_selectionFrame->setStyleSheet(QStringLiteral("QFrame { border: 2px dashed %1; background-color: rgba(%2, %3, %4, 40); }")
        .arg(color.name()).arg(color.red()).arg(color.green()).arg(color.blue()));
    m_selectionFrame->hide();
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
    emit viewportChanged();
}

void DiagramView::fitAll()
{
    const QRectF bounds = scene()->itemsBoundingRect();
    if (!bounds.isEmpty())
        fitInView(bounds.adjusted(-50.0, -50.0, 50.0, 50.0), Qt::KeepAspectRatio);
    emit viewportChanged();
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
    emit viewportChanged();
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
    if (event->button() == Qt::LeftButton) {
        auto *diagram = qobject_cast<DiagramScene *>(scene());
        const QGraphicsItem *pressedItem = diagram ? diagram->itemAt(mapToScene(event->pos()), QTransform()) : nullptr;
        if (diagram && diagram->tool() == DiagramScene::Tool::Select
            && (!pressedItem || dynamic_cast<const RoutedItem *>(pressedItem))) {
            m_areaSelecting = true;
            m_selectionStart = event->pos();
            m_rubberBandRect = QRect();
            m_selectionOperation = event->modifiers() & Qt::ControlModifier ? Qt::AddToSelection : Qt::ReplaceSelection;
            if (m_selectionOperation == Qt::ReplaceSelection)
                scene()->clearSelection();
            m_selectionFrame->hide();
        }
    }
    QGraphicsView::mousePressEvent(event);
}

void DiagramView::mouseMoveEvent(QMouseEvent *event)
{
    emit cursorScenePositionChanged(mapToScene(event->pos()));
    if (m_panning) {
        const QPoint delta = event->pos() - m_lastPanPosition;
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - delta.x());
        verticalScrollBar()->setValue(verticalScrollBar()->value() - delta.y());
        m_lastPanPosition = event->pos();
        event->accept();
        return;
    }
    if (m_areaSelecting) {
        updateSelection(event->pos());
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
    if (m_areaSelecting && event->button() == Qt::LeftButton) {
        updateSelection(event->pos());
        finishSelection();
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
    if (event->mimeData()->hasFormat(EntityPalette::mimeType()) || isDocumentFileDrop(event->mimeData()))
        event->acceptProposedAction();
}

void DiagramView::dragMoveEvent(QDragMoveEvent *event)
{
    if (event->mimeData()->hasFormat(EntityPalette::mimeType()) || isDocumentFileDrop(event->mimeData()))
        event->acceptProposedAction();
}

void DiagramView::dropEvent(QDropEvent *event)
{
    if (isDocumentFileDrop(event->mimeData())) {
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

void DiagramView::updateSelection(const QPoint &position)
{
    const QRect rect = QRect(m_selectionStart, position).normalized();
    const QRect dirty = m_rubberBandRect.united(rect).adjusted(-2, -2, 2, 2);
    m_rubberBandRect = rect;
    m_selectionFrame->setGeometry(rect);
    if (!m_selectionFrame->isVisible())
        m_selectionFrame->show();
    m_selectionFrame->raise();
    viewport()->update(dirty);
    QPainterPath selectionArea;
    selectionArea.addPolygon(mapToScene(rect));
    selectionArea.closeSubpath();
    scene()->setSelectionArea(selectionArea, m_selectionOperation, Qt::IntersectsItemShape, viewportTransform());
}

void DiagramView::finishSelection()
{
    m_areaSelecting = false;
    m_selectionFrame->hide();
    viewport()->update(m_rubberBandRect.adjusted(-2, -2, 2, 2));
    m_rubberBandRect = QRect();
}

void DiagramView::zoomBy(double factor)
{
    const double targetScale = transform().m11() * factor;
    if (targetScale < 0.1 || targetScale > 5.0)
        return;
    scale(factor, factor);
    emit viewportChanged();
}
