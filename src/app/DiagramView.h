#pragma once

#include <QGraphicsView>

class QFrame;

class DiagramView : public QGraphicsView
{
    Q_OBJECT

public:
    explicit DiagramView(QGraphicsScene *scene, QWidget *parent = nullptr);

    void zoomIn();
    void zoomOut();
    void resetZoom();
    void fitAll();
    void fitSelection();

signals:
    void fileOpenRequested(const QString &fileName);

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
    void updateSelection(const QPoint &position);
    void finishSelection();

    bool m_panning = false;
    bool m_spacePressed = false;
    QPoint m_lastPanPosition;
    QFrame *m_selectionFrame = nullptr;
    QPoint m_selectionStart;
    QRect m_rubberBandRect;
    Qt::ItemSelectionOperation m_selectionOperation = Qt::ReplaceSelection;
    bool m_areaSelecting = false;
};
