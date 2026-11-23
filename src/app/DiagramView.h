#pragma once

#include <QGraphicsView>

class DiagramView : public QGraphicsView
{
public:
    explicit DiagramView(QGraphicsScene *scene, QWidget *parent = nullptr);

    void zoomIn();
    void zoomOut();
    void resetZoom();
    void fitAll();
    void fitSelection();

protected:
    void wheelEvent(QWheelEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    void zoomBy(double factor);

    bool m_panning = false;
    bool m_spacePressed = false;
    QPoint m_lastPanPosition;
};
