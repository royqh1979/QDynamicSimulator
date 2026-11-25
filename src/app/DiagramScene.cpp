#include "DiagramScene.h"
#include "DiagramPalette.h"

#include "DocumentController.h"

#include <QActionGroup>
#include <QGraphicsLineItem>
#include <QGraphicsProxyWidget>
#include <QGraphicsSceneContextMenuEvent>
#include <QGraphicsSceneMouseEvent>
#include <QIcon>
#include <QImage>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMenu>
#include <QPainter>
#include <QStyleOptionGraphicsItem>
#include <QToolTip>
#include <QtMath>

#include <algorithm>
#include <optional>

using namespace qds::core;

namespace {

bool isRouteTool(DiagramScene::Tool tool)
{
    return tool == DiagramScene::Tool::AddFlow || tool == DiagramScene::Tool::Connect;
}

int creationRank(ElementKind kind)
{
    if (kind == ElementKind::Flow)
        return 1;
    if (kind == ElementKind::Connector)
        return 2;
    return 0;
}

DiagramItem *nodeAt(QGraphicsScene &scene, const QPointF &position)
{
    const QList<QGraphicsItem *> candidates = scene.items(position);
    for (QGraphicsItem *candidate : candidates) {
        auto *item = dynamic_cast<DiagramItem *>(candidate);
        if (item && !dynamic_cast<RoutedItem *>(item))
            return item;
    }
    return nullptr;
}

DiagramItem *connectionEndpointAt(QGraphicsScene &scene, const QPointF &position)
{
    const QList<QGraphicsItem *> candidates = scene.items(position);
    for (QGraphicsItem *candidate : candidates) {
        auto *item = dynamic_cast<DiagramItem *>(candidate);
        if (item && !dynamic_cast<ArrowItem *>(item))
            return item;
    }
    return nullptr;
}

QString documentElementName(const DocumentElement &element)
{
    if (const auto *model = dynamic_cast<const ModelElement *>(&element))
        return QString::fromStdString(model->displayName);
    if (const auto *comment = dynamic_cast<const Comment *>(&element))
        return QString::fromStdString(comment->content);
    return {};
}

void makeControlPointsManual(Route &route)
{
    route.manual = true;
    for (RoutePoint &point : route.controlPoints)
        point.automatic = false;
}

} // namespace

DiagramScene::DiagramScene(DocumentController *controller, QObject *parent)
    : QGraphicsScene(parent)
    , m_controller(controller)
{
    Q_INIT_RESOURCE(icons);
    setSceneRect(-3000.0, -3000.0, 6000.0, 6000.0);
    connect(m_controller, &DocumentController::documentChanged, this, &DiagramScene::applyChangeNotification);
    connect(this, &QGraphicsScene::selectionChanged, this, &DiagramScene::syncControlPoints);
    ChangeNotification reset;
    reset.documentReset = true;
    applyChangeNotification(reset);
}

DiagramScene::~DiagramScene()
{
    finishInlineRename(false);
    disconnect(this, &QGraphicsScene::selectionChanged, this, &DiagramScene::syncControlPoints);
    m_controlHandles.clear();
}

void DiagramScene::setTool(Tool tool)
{
    finishInlineRename(false);
    m_tool = tool;
    m_connectionSource = 0;
    cancelRubberLine();
    cancelEndpointPreview();
}

qds::core::EntityId DiagramScene::createElementAt(ElementKind kind, const QPointF &position)
{
    const QPointF snapped = snapPoint(position);
    EntityId id = 0;
    if (kind == ElementKind::Cloud)
        id = m_controller->createCloud(snapped);
    else if (kind == ElementKind::Comment)
        id = m_controller->createComment(tr("Comment"), snapped);
    else
        id = m_controller->createVariable(kind, snapped);
    clearSelection();
    if (DiagramItem *created = m_items.value(id))
        created->setSelected(true);
    emit editingFinished();
    return id;
}

DiagramScene::Tool DiagramScene::tool() const
{
    return m_tool;
}

void DiagramScene::removeSelected()
{
    m_controller->removeElements(selectedElementIds());
}

bool DiagramScene::renameSelected(const QString &newName)
{
    DiagramItem *item = dynamic_cast<DiagramItem *>(selectedItems().value(0));
    return item ? m_controller->renameElement(item->id(), newName) : false;
}

bool DiagramScene::setSelectedEquation(const QString &equation)
{
    DiagramItem *item = dynamic_cast<DiagramItem *>(selectedItems().value(0));
    if (!item)
        return false;
    m_controller->setEquation(item->id(), equation);
    return true;
}

const ModelElement *DiagramScene::selectedModelElement() const
{
    DiagramItem *item = dynamic_cast<DiagramItem *>(selectedItems().value(0));
    return item ? dynamic_cast<const ModelElement *>(m_controller->document().find(item->id())) : nullptr;
}

const Comment *DiagramScene::selectedComment() const
{
    DiagramItem *item = dynamic_cast<DiagramItem *>(selectedItems().value(0));
    return item ? dynamic_cast<const Comment *>(m_controller->document().find(item->id())) : nullptr;
}

std::vector<EntityId> DiagramScene::selectedElementIds() const
{
    std::vector<EntityId> ids;
    for (QGraphicsItem *graphicsItem : selectedItems()) {
        if (auto *item = dynamic_cast<DiagramItem *>(graphicsItem))
            ids.push_back(item->id());
    }
    return ids;
}

std::vector<EntityId> DiagramScene::findElements(const QString &text) const
{
    std::vector<EntityId> matches;
    for (const DocumentElement &element : m_controller->document().elements()) {
        if (documentElementName(element).contains(text, Qt::CaseInsensitive))
            matches.push_back(element.id());
    }
    return matches;
}

void DiagramScene::selectAllItems()
{
    const QList<QGraphicsItem *> allItems = items();
    for (QGraphicsItem *item : allItems) {
        if (item->flags() & QGraphicsItem::ItemIsSelectable)
            item->setSelected(true);
    }
}

void DiagramScene::alignSelected(Qt::Alignment alignment)
{
    QVector<DiagramItem *> selected;
    for (QGraphicsItem *item : selectedItems()) {
        auto *diagramItem = dynamic_cast<DiagramItem *>(item);
        if (diagramItem && (diagramItem->flags() & QGraphicsItem::ItemIsMovable))
            selected.append(diagramItem);
    }
    if (selected.size() < 2)
        return;

    std::vector<std::pair<EntityId, QPointF>> positions;
    for (DiagramItem *item : selected) {
        QPointF position = item->pos();
        if (alignment & Qt::AlignLeft)
            position.setX(std::min(position.x(), selected.front()->pos().x()));
        for (DiagramItem *other : selected) {
            if (alignment & Qt::AlignLeft)
                position.setX(std::min(position.x(), other->pos().x()));
            else if (alignment & Qt::AlignRight)
                position.setX(std::max(position.x(), other->pos().x()));
            else if (alignment & Qt::AlignTop)
                position.setY(std::min(position.y(), other->pos().y()));
            else if (alignment & Qt::AlignBottom)
                position.setY(std::max(position.y(), other->pos().y()));
        }
        if (alignment & Qt::AlignHCenter) {
            double center = 0.0;
            for (DiagramItem *other : selected)
                center += other->pos().x();
            position.setX(center / selected.size());
        } else if (alignment & Qt::AlignVCenter) {
            double center = 0.0;
            for (DiagramItem *other : selected)
                center += other->pos().y();
            position.setY(center / selected.size());
        }
        positions.emplace_back(item->id(), position);
    }
    m_controller->moveElements(positions);
}

void DiagramScene::distributeSelected(bool horizontal)
{
    QVector<DiagramItem *> selected;
    for (QGraphicsItem *item : selectedItems()) {
        auto *diagramItem = dynamic_cast<DiagramItem *>(item);
        if (diagramItem && (diagramItem->flags() & QGraphicsItem::ItemIsMovable))
            selected.append(diagramItem);
    }
    if (selected.size() < 3)
        return;
    std::sort(selected.begin(), selected.end(), [horizontal](DiagramItem *left, DiagramItem *right) {
        return horizontal ? left->pos().x() < right->pos().x() : left->pos().y() < right->pos().y();
    });
    const double start = horizontal ? selected.front()->pos().x() : selected.front()->pos().y();
    const double end = horizontal ? selected.back()->pos().x() : selected.back()->pos().y();
    const double spacing = (end - start) / (selected.size() - 1);
    std::vector<std::pair<EntityId, QPointF>> positions;
    for (int index = 1; index < selected.size() - 1; ++index) {
        QPointF position = selected[index]->pos();
        if (horizontal)
            position.setX(start + spacing * index);
        else
            position.setY(start + spacing * index);
        positions.emplace_back(selected[index]->id(), position);
    }
    m_controller->moveElements(positions);
}

void DiagramScene::setGridVisible(bool visible)
{
    m_gridVisible = visible;
    update();
}

QRectF DiagramScene::contentRect(bool selectionOnly) const
{
    QRectF bounds;
    if (selectionOnly) {
        const QList<QGraphicsItem *> selection = selectedItems();
        for (const QGraphicsItem *item : selection) {
            if (dynamic_cast<const DiagramItem *>(item))
                bounds = bounds.united(item->sceneBoundingRect());
        }
        return bounds.isEmpty() ? QRectF() : bounds.adjusted(-20.0, -20.0, 20.0, 20.0);
    }
    for (const DiagramItem *item : m_items)
        bounds = bounds.united(item->sceneBoundingRect());
    return bounds.isEmpty() ? QRectF(0.0, 0.0, 400.0, 300.0) : bounds.adjusted(-20.0, -20.0, 20.0, 20.0);
}

QImage DiagramScene::renderImage(bool selectionOnly)
{
    const QRectF bounds = contentRect(selectionOnly);
    if (bounds.isEmpty())
        return {};
    const double scale = std::max(0.01, std::min({2.0, 16384.0 / bounds.width(), 16384.0 / bounds.height()}));
    QImage image(qMax(1, qCeil(bounds.width() * scale)), qMax(1, qCeil(bounds.height() * scale)), QImage::Format_RGB32);
    image.fill(diagram_palette::CanvasBackgroundColor);

    const QList<QGraphicsItem *> selection = selectedItems();
    const bool gridVisible = m_gridVisible;
    const bool rubberVisible = m_rubberLine && m_rubberLine->isVisible();
    const bool editorVisible = m_inlineNameEditor && m_inlineNameEditor->isVisible();
    QWidget *editorWidget = m_inlineNameEditor ? m_inlineNameEditor->widget() : nullptr;
    const bool editorSignalsBlocked = editorWidget && editorWidget->blockSignals(true);
    if (m_rubberLine)
        m_rubberLine->hide();
    if (m_inlineNameEditor)
        m_inlineNameEditor->hide();
    clearSelection();
    setGridVisible(false);
    {
        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setRenderHint(QPainter::TextAntialiasing);
        if (selectionOnly) {
            painter.setWorldTransform(QTransform::fromScale(image.width() / bounds.width(), image.height() / bounds.height()), true);
            painter.translate(-bounds.topLeft());
            QList<QGraphicsItem *> ordered = selection;
            std::sort(ordered.begin(), ordered.end(), [](const QGraphicsItem *left, const QGraphicsItem *right) {
                return left->zValue() < right->zValue();
            });
            for (QGraphicsItem *item : ordered) {
                painter.save();
                painter.setWorldTransform(item->sceneTransform(), true);
                QStyleOptionGraphicsItem option;
                option.exposedRect = item->boundingRect();
                option.levelOfDetail = QStyleOptionGraphicsItem::levelOfDetailFromTransform(painter.worldTransform());
                item->paint(&painter, &option, nullptr);
                painter.restore();
            }
        } else {
            render(&painter, QRectF(QPointF(0.0, 0.0), QSizeF(image.size())), bounds, Qt::IgnoreAspectRatio);
        }
    }
    setGridVisible(gridVisible);
    for (QGraphicsItem *item : selection)
        item->setSelected(true);
    if (m_rubberLine)
        m_rubberLine->setVisible(rubberVisible);
    if (m_inlineNameEditor)
        m_inlineNameEditor->setVisible(editorVisible);
    if (editorWidget)
        editorWidget->blockSignals(editorSignalsBlocked);
    return image;
}

void DiagramScene::setSnapEnabled(bool enabled)
{
    m_snapEnabled = enabled;
}

QPointF DiagramScene::snapPoint(const QPointF &position) const
{
    if (!m_snapEnabled)
        return position;
    return QPointF(qRound64(position.x() / m_gridSize) * m_gridSize, qRound64(position.y() / m_gridSize) * m_gridSize);
}

void DiagramScene::setSelectedRouteMode(RouteMode mode)
{
    for (QGraphicsItem *item : selectedItems()) {
        auto *routed = dynamic_cast<RoutedItem *>(item);
        if (!routed || (routed->type() == FlowItem::Type && mode == RouteMode::Orthogonal))
            continue;
        Route route = m_controller->document().diagram().route(routed->id());
        route.mode = mode;
        route.manual = true;
        if (mode == RouteMode::Straight)
            route.controlPoints.clear();
        else if (mode == RouteMode::Bezier) {
            if (route.controlPoints.size() < 2)
                route.controlPoints = defaultConnectorRoute().controlPoints;
            makeControlPointsManual(route);
        }
        m_controller->setRoute(routed->id(), route);
    }
}

void DiagramScene::resetSelectedRoute()
{
    for (QGraphicsItem *item : selectedItems()) {
        if (auto *routed = dynamic_cast<RoutedItem *>(item)) {
            Route route;
            if (routed->type() == FlowItem::Type) {
                route.mode = RouteMode::Straight;
            } else {
                route = defaultConnectorRoute();
            }
            m_controller->setRoute(routed->id(), route);
        }
    }
}

DiagramItem *DiagramScene::item(EntityId id) const
{
    return m_items.value(id);
}

void DiagramScene::mousePressEvent(QGraphicsSceneMouseEvent *event)
{
    if (event->button() == Qt::RightButton && isRouteTool(m_tool)) {
        m_connectionSource = 0;
        cancelRubberLine();
        clearSelection();
        event->accept();
        return;
    }

    if (event->button() == Qt::LeftButton && m_tool != Tool::Select && !isRouteTool(m_tool)) {
        ElementKind kind = ElementKind::Stock;
        if (m_tool == Tool::AddAuxiliary)
            kind = ElementKind::Auxiliary;
        else if (m_tool == Tool::AddConstant)
            kind = ElementKind::Constant;
        else if (m_tool == Tool::AddData)
            kind = ElementKind::Data;
        else if (m_tool == Tool::AddCloud)
            kind = ElementKind::Cloud;
        else if (m_tool == Tool::AddComment)
            kind = ElementKind::Comment;
        createElementAt(kind, event->scenePos());
        event->accept();
        return;
    }

    if (event->button() == Qt::LeftButton && isRouteTool(m_tool)) {
        DiagramItem *item = connectionEndpointAt(*this, event->scenePos());
        if (!item && m_tool != Tool::AddFlow) {
            event->accept();
            return;
        }
        m_connectionSource = item ? item->id() : 0;
        m_connectionStart = item ? item->center() : snapPoint(event->scenePos());
        clearSelection();
        if (item)
            item->setSelected(true);
        m_rubberLine = new QGraphicsLineItem(QLineF(m_connectionStart, event->scenePos()));
        m_rubberLine->setPen(QPen(diagram_palette::SelectionColor, 2.0, Qt::DashLine));
        m_rubberLine->setZValue(20.0);
        addItem(m_rubberLine);
        event->accept();
        return;
    }

    if (event->button() == Qt::LeftButton && m_tool == Tool::Select && nodeAt(*this, event->scenePos())) {
        const Qt::KeyboardModifiers modifiers = event->modifiers();
        if (modifiers & Qt::ControlModifier)
            event->setModifiers(modifiers & ~Qt::ControlModifier);
        QGraphicsScene::mousePressEvent(event);
        event->setModifiers(modifiers);
        m_variableDragging = true;
        m_dragStart = event->scenePos();
        m_dragOrigins.clear();
        for (QGraphicsItem *selected : selectedItems()) {
            auto *item = dynamic_cast<DiagramItem *>(selected);
            if (item && (item->flags() & QGraphicsItem::ItemIsMovable))
                m_dragOrigins.insert(item->id(), item->pos());
        }
        return;
    }
    const QGraphicsItem *pressedItem = event->button() == Qt::LeftButton && m_tool == Tool::Select
        ? itemAt(event->scenePos(), QTransform()) : nullptr;
    if (event->button() == Qt::LeftButton && m_tool == Tool::Select
        && (!pressedItem || dynamic_cast<const RoutedItem *>(pressedItem))) {
        m_areaSelecting = true;
        syncControlPoints();
    }
    QGraphicsScene::mousePressEvent(event);
}

void DiagramScene::mouseMoveEvent(QGraphicsSceneMouseEvent *event)
{
    if (m_rubberLine) {
        DiagramItem *source = m_items.value(m_connectionSource);
        m_rubberLine->setLine(QLineF(source ? source->center() : m_connectionStart, event->scenePos()));
        DiagramItem *target = connectionEndpointAt(*this, event->scenePos());
        setEndpointTarget(target && target->id() != m_connectionSource ? target : nullptr);
        event->accept();
        return;
    }
    QGraphicsScene::mouseMoveEvent(event);
    if (m_variableDragging && (event->modifiers() & Qt::ShiftModifier)) {
        QPointF delta = event->scenePos() - m_dragStart;
        if (std::abs(delta.x()) >= std::abs(delta.y()))
            delta.setY(0.0);
        else
            delta.setX(0.0);
        for (auto it = m_dragOrigins.constBegin(); it != m_dragOrigins.constEnd(); ++it) {
            if (DiagramItem *item = m_items.value(it.key()))
                item->setPos(it.value() + delta);
        }
    }
}

void DiagramScene::mouseReleaseEvent(QGraphicsSceneMouseEvent *event)
{
    if (m_rubberLine && event->button() == Qt::LeftButton) {
        DiagramItem *target = connectionEndpointAt(*this, event->scenePos());
        const bool differentTarget = target && target->id() != m_connectionSource;
        EntityId created = 0;
        if (m_tool == Tool::AddFlow && (!target || differentTarget)) {
            const std::optional<QPointF> fromPosition = m_connectionSource == 0
                ? std::optional<QPointF>(m_connectionStart)
                : std::nullopt;
            const std::optional<QPointF> toPosition = !target
                ? std::optional<QPointF>(snapPoint(event->scenePos()))
                : std::nullopt;
            created = m_controller->createFlow(
                m_connectionSource, target ? target->id() : 0, fromPosition, toPosition);
        } else if (m_tool == Tool::Connect && differentTarget) {
            created = m_controller->createConnector(m_connectionSource, target->id());
        }
        if (created != 0) {
            clearSelection();
            if (DiagramItem *item = m_items.value(created))
                item->setSelected(true);
        }
        cancelRubberLine();
        m_connectionSource = 0;
        emit editingFinished();
        event->accept();
        return;
    }
    QGraphicsScene::mouseReleaseEvent(event);
    m_variableDragging = false;
    m_dragOrigins.clear();
    if (m_areaSelecting) {
        m_areaSelecting = false;
        syncControlPoints();
    }
}

void DiagramScene::contextMenuEvent(QGraphicsSceneContextMenuEvent *event)
{
    auto *routed = dynamic_cast<RoutedItem *>(itemAt(event->scenePos(), QTransform()));
    if (!routed || isRouteTool(m_tool)) {
        QGraphicsScene::contextMenuEvent(event);
        return;
    }

    clearSelection();
    routed->setSelected(true);
    QMenu menu;
    QMenu *routeMenu = menu.addMenu(QIcon(QStringLiteral(":/icons/route-orthogonal.svg")), tr("Route type"));
    auto *modes = new QActionGroup(&menu);
    modes->setExclusive(true);
    const Route current = m_controller->document().diagram().route(routed->id());
    const struct {
        QString text;
        RouteMode mode;
        const char *iconName;
    } definitions[] = {
        {tr("Straight"), RouteMode::Straight, "straight"},
        {tr("Orthogonal"), RouteMode::Orthogonal, "orthogonal"},
        {tr("Bezier"), RouteMode::Bezier, "bezier"}};
    for (const auto &definition : definitions) {
        if (routed->type() == FlowItem::Type && definition.mode == RouteMode::Orthogonal)
            continue;
        QAction *action = routeMenu->addAction(
            QIcon(QStringLiteral(":/icons/route-%1.svg").arg(QLatin1String(definition.iconName))), definition.text);
        action->setCheckable(true);
        action->setChecked(current.mode == definition.mode);
        modes->addAction(action);
        connect(action, &QAction::triggered, this, [this, mode = definition.mode]() {
            setSelectedRouteMode(mode);
        });
    }

    QAction *reset = menu.addAction(QIcon(QStringLiteral(":/icons/route-reset.svg")), tr("Reset automatic route"));
    connect(reset, &QAction::triggered, this, &DiagramScene::resetSelectedRoute);
    QAction *locked = menu.addAction(QIcon(QStringLiteral(":/icons/route-lock.svg")), tr("Lock route"));
    locked->setCheckable(true);
    locked->setChecked(current.locked);
    connect(locked, &QAction::toggled, this, [this, id = routed->id()](bool checked) {
        Route route = m_controller->document().diagram().route(id);
        route.locked = checked;
        m_controller->setRoute(id, route);
    });
    menu.addSeparator();
    QAction *remove = menu.addAction(QIcon(QStringLiteral(":/icons/delete.svg")), tr("Delete"));
    connect(remove, &QAction::triggered, this, &DiagramScene::removeSelected);
    menu.exec(event->screenPos());
    event->accept();
}

void DiagramScene::mouseDoubleClickEvent(QGraphicsSceneMouseEvent *event)
{
    finishInlineRename(true);
    auto *item = dynamic_cast<DiagramItem *>(itemAt(event->scenePos(), QTransform()));
    if (event->button() == Qt::LeftButton && item
        && dynamic_cast<const ModelElement *>(m_controller->document().find(item->id()))) {
        beginInlineRename(item);
        event->accept();
        return;
    }
    auto *routed = dynamic_cast<RoutedItem *>(item);
    if (routed && (routed->type() != FlowItem::Type || routed->route().mode == RouteMode::Bezier)) {
        Route route = m_controller->document().diagram().route(routed->id());
        const QPointF point = snapPoint(event->scenePos());
        route.controlPoints.push_back(routed->routePointFromScene(point));
        makeControlPointsManual(route);
        m_controller->setRoute(routed->id(), route);
        event->accept();
        return;
    }
    QGraphicsScene::mouseDoubleClickEvent(event);
}

void DiagramScene::applyChangeNotification(const ChangeNotification &notification)
{
    if (notification.documentReset) {
        finishInlineRename(false);
        m_controlHandles.clear();
        m_obstacles.clear();
        m_rubberLine = nullptr;
        m_endpointPreviewItem.clear();
        m_endpointTarget.clear();
        clear();
        m_items.clear();
        m_connectionSource = 0;
        m_areaSelecting = false;
        std::vector<EntityId> ids;
        for (const DocumentElement &element : m_controller->document().elements())
            ids.push_back(element.id());
        createCanvasItems(std::move(ids));
        updateSceneRect();
        return;
    }

    for (EntityId id : notification.removed) {
        if (id == m_inlineNameId)
            finishInlineRename(false);
        m_obstacles.remove(id);
        DiagramItem *item = m_items.take(id);
        delete item;
    }
    createCanvasItems(notification.added);
    for (EntityId id : notification.updated) {
        DiagramItem *item = m_items.value(id);
        const DocumentElement *element = m_controller->document().find(id);
        if (!item || !element)
            continue;
        const QPointF position = QPointF(m_controller->document().diagram().position(id).x,
                                         m_controller->document().diagram().position(id).y);
        if (auto *variable = dynamic_cast<VariableItem *>(item)) {
            variable->refresh(*static_cast<const ModelElement *>(element), position);
            m_obstacles.upsert(id, variable->sceneBoundingRect().adjusted(-12.0, -12.0, 12.0, 12.0));
        } else if (auto *view = dynamic_cast<ViewObjectItem *>(item)) {
            view->refresh(*static_cast<const ViewElement *>(element), position);
            m_obstacles.upsert(id, view->sceneBoundingRect().adjusted(-12.0, -12.0, 12.0, 12.0));
        } else if (auto *flow = dynamic_cast<FlowItem *>(item)) {
            const auto &value = static_cast<const Flow &>(*element);
            flow->refresh(value, m_items.value(value.endpoints.from), m_items.value(value.endpoints.to), position);
            flow->setRoute(m_controller->document().diagram().route(id));
        } else if (auto *arrow = dynamic_cast<ArrowItem *>(item)) {
            const auto &value = static_cast<const Connector &>(*element);
            arrow->rebind(m_items.value(value.from), m_items.value(value.to));
            arrow->setRoute(m_controller->document().diagram().route(id));
            updateAutomaticRoute(arrow);
        }
        item->setFontStyle(m_controller->document().diagram().fontStyle(id));
        updateDependentRoutes(id);
    }
    syncControlPoints();
    updateSceneRect();
}

DiagramItem *DiagramScene::createCanvasItem(EntityId id)
{
    const DocumentElement *element = m_controller->document().find(id);
    if (!element || m_items.contains(id))
        return m_items.value(id);
    const Vec2 positionValue = m_controller->document().diagram().position(id);
    const QPointF position(positionValue.x, positionValue.y);
    DiagramItem *item = nullptr;
    if (element->kind() == ElementKind::Flow) {
        const auto &flow = static_cast<const Flow &>(*element);
        item = new FlowItem(flow, m_items.value(flow.endpoints.from), m_items.value(flow.endpoints.to),
                            m_controller->document().diagram().route(id), position);
    } else if (element->kind() == ElementKind::Connector) {
        const auto &connector = static_cast<const Connector &>(*element);
        item = new ArrowItem(id, m_items.value(connector.from), m_items.value(connector.to),
                             m_controller->document().diagram().route(id));
    } else if (const auto *model = dynamic_cast<const ModelElement *>(element)) {
        item = new VariableItem(*model, position);
    } else {
        item = new ViewObjectItem(*static_cast<const ViewElement *>(element), position);
    }
    item->setFontStyle(m_controller->document().diagram().fontStyle(id));
    addItem(item);
    m_items.insert(id, item);
    if (!dynamic_cast<RoutedItem *>(item)) {
        m_obstacles.upsert(id, item->sceneBoundingRect().adjusted(-12.0, -12.0, 12.0, 12.0));
        connect(item, &DiagramItem::positionChanged, this, [this](EntityId id, const QPointF &) {
            updateDependentRoutes(id);
        });
        connect(item, &DiagramItem::moveFinished, this, &DiagramScene::finishMove);
    } else {
        updateAutomaticRoute(static_cast<RoutedItem *>(item));
    }
    return item;
}

void DiagramScene::createCanvasItems(std::vector<EntityId> ids)
{
    std::sort(ids.begin(), ids.end(), [this](EntityId left, EntityId right) {
        const ElementKind leftKind = m_controller->document().find(left)->kind();
        const ElementKind rightKind = m_controller->document().find(right)->kind();
        const int leftRank = creationRank(leftKind);
        const int rightRank = creationRank(rightKind);
        return leftRank != rightRank ? leftRank < rightRank : left < right;
    });
    for (EntityId id : ids)
        createCanvasItem(id);
}

void DiagramScene::updateDependentRoutes(EntityId id)
{
    std::vector<EntityId> updatedFlows;
    for (DiagramItem *item : m_items) {
        if (auto *flow = dynamic_cast<FlowItem *>(item); flow && flow->connects(id)) {
            flow->updatePosition();
            updatedFlows.push_back(flow->id());
        }
    }
    for (DiagramItem *item : m_items) {
        auto *arrow = dynamic_cast<ArrowItem *>(item);
        if (!arrow)
            continue;
        const bool dependsOnMovedFlow = std::any_of(updatedFlows.begin(), updatedFlows.end(),
            [arrow](EntityId flowId) { return arrow->connects(flowId); });
        if (arrow->connects(id) || dependsOnMovedFlow) {
            arrow->updatePosition();
            updateAutomaticRoute(arrow);
        }
    }
    syncControlPoints();
}

RoutedItem *DiagramScene::selectedRoutedItem() const
{
    const QList<QGraphicsItem *> selection = selectedItems();
    return !m_areaSelecting && selection.size() == 1 ? dynamic_cast<RoutedItem *>(selection.front()) : nullptr;
}

void DiagramScene::syncControlPoints()
{
    RoutedItem *selected = selectedRoutedItem();
    for (DiagramItem *item : m_items) {
        if (auto *routed = dynamic_cast<RoutedItem *>(item))
            routed->setControlGuidesVisible(routed == selected && !routed->previewing());
    }
    const Route route = selected ? m_controller->document().diagram().route(selected->id()) : Route();
    int required = 0;

    auto updateHandle = [this](int handleIndex, EntityId id, int pointIndex,
                              ControlPointHandle::Role role, const QPointF &position) {
        if (handleIndex == m_controlHandles.size()) {
            auto *handle = new ControlPointHandle(id, pointIndex, role, position);
            addItem(handle);
            m_controlHandles.append(handle);
            connect(handle, &ControlPointHandle::moved, this, &DiagramScene::previewControlPoint);
            connect(handle, &ControlPointHandle::finished, this, &DiagramScene::finishControlPoint);
            connect(handle, &ControlPointHandle::removeRequested, this, &DiagramScene::removeControlPoint);
        }
        m_controlHandles[handleIndex]->assign(id, pointIndex, role, position);
    };

    if (selected) {
        const QPainterPath path = selected->geometryPath();
        if (selected->from() && !path.isEmpty())
            updateHandle(required++, selected->id(), -1, ControlPointHandle::Role::Source, path.pointAtPercent(0.0));
        if (selected->to() && !path.isEmpty())
            updateHandle(required++, selected->id(), -2, ControlPointHandle::Role::Target, path.pointAtPercent(1.0));
        if (route.mode != RouteMode::Straight && !selected->previewing()) {
            for (std::size_t index = 0; index < route.controlPoints.size(); ++index) {
                const RoutePoint &point = route.controlPoints[index];
                if (point.automatic && route.mode != RouteMode::Bezier)
                    continue;
                updateHandle(required++, selected->id(), static_cast<int>(index),
                             ControlPointHandle::Role::ControlPoint, selected->scenePoint(point));
            }
        }
    }
    for (int index = required; index < m_controlHandles.size(); ++index)
        m_controlHandles[index]->hide();
}

void DiagramScene::previewControlPoint(EntityId id, int index, const QPointF &position)
{
    if (index < 0) {
        auto *item = dynamic_cast<RoutedItem *>(m_items.value(id));
        if (!item || (index != -1 && index != -2))
            return;
        m_endpointPreviewItem = item;
        m_endpointPreviewItem->previewEndpoint(index, position);
        setEndpointTarget(connectionEndpointAt(*this, position));
        updateDependentRoutes(id);
        return;
    }
    auto *item = dynamic_cast<RoutedItem *>(m_items.value(id));
    if (!item)
        return;
    Route route = m_controller->document().diagram().route(id);
    if (index >= static_cast<int>(route.controlPoints.size()))
        return;
    route.controlPoints[static_cast<std::size_t>(index)] = item->routePointFromScene(position);
    makeControlPointsManual(route);
    item->setRoute(route);
}

void DiagramScene::finishControlPoint(EntityId id, int index, const QPointF &position)
{
    auto *item = dynamic_cast<RoutedItem *>(m_items.value(id));
    const DocumentElement *element = m_controller->document().find(id);
    if (!item || !element)
        return;
    if (index < 0)
        cancelEndpointPreview();
    if (index >= 0) {
        Route route = m_controller->document().diagram().route(id);
        if (index >= static_cast<int>(route.controlPoints.size()))
            return;
        const QPointF snapped = snapPoint(position);
        route.controlPoints[static_cast<std::size_t>(index)] = item->routePointFromScene(snapped);
        makeControlPointsManual(route);
        m_controller->setRoute(id, route);
        return;
    }

    DiagramItem *target = connectionEndpointAt(*this, position);
    if (!target) {
        syncControlPoints();
        return;
    }
    bool restored = false;
    if (element->kind() == ElementKind::Flow) {
        const auto &flow = static_cast<const Flow &>(*element);
        const EntityId from = index == -1 ? target->id() : flow.endpoints.from;
        const EntityId to = index == -2 ? target->id() : flow.endpoints.to;
        restored = m_controller->reconnectFlow(id, from, to);
    } else {
        const auto &connector = static_cast<const Connector &>(*element);
        const EntityId from = index == -1 ? target->id() : connector.from;
        const EntityId to = index == -2 ? target->id() : connector.to;
        restored = m_controller->reconnectConnector(id, from, to);
    }
    if (!restored)
        syncControlPoints();
}

void DiagramScene::removeControlPoint(EntityId id, int index)
{
    Route route = m_controller->document().diagram().route(id);
    if (index < 0 || index >= static_cast<int>(route.controlPoints.size()))
        return;
    route.controlPoints.erase(route.controlPoints.begin() + index);
    makeControlPointsManual(route);
    m_controller->setRoute(id, route);
}

void DiagramScene::setEndpointTarget(DiagramItem *target)
{
    if (m_endpointTarget == target)
        return;
    if (m_endpointTarget)
        m_endpointTarget->setEndpointTargetHighlighted(false);
    m_endpointTarget = target;
    if (m_endpointTarget)
        m_endpointTarget->setEndpointTargetHighlighted(true);
}

void DiagramScene::cancelRubberLine()
{
    setEndpointTarget(nullptr);
    if (!m_rubberLine)
        return;
    removeItem(m_rubberLine);
    delete m_rubberLine;
    m_rubberLine = nullptr;
}

void DiagramScene::cancelEndpointPreview()
{
    setEndpointTarget(nullptr);
    if (m_endpointPreviewItem) {
        RoutedItem *item = m_endpointPreviewItem;
        const EntityId id = item->id();
        m_endpointPreviewItem.clear();
        item->clearEndpointPreview();
        updateDependentRoutes(id);
        return;
    }
    syncControlPoints();
}

bool DiagramScene::eventFilter(QObject *watched, QEvent *event)
{
    if (m_inlineNameEditor && watched == m_inlineNameEditor->widget()
        && event->type() == QEvent::KeyPress && static_cast<QKeyEvent *>(event)->key() == Qt::Key_Escape) {
        finishInlineRename(false);
        return true;
    }
    return QGraphicsScene::eventFilter(watched, event);
}

void DiagramScene::beginInlineRename(DiagramItem *item)
{
    const auto *model = item ? dynamic_cast<const ModelElement *>(m_controller->document().find(item->id())) : nullptr;
    if (!model || item->labelRect().isEmpty())
        return;
    finishInlineRename(true);
    const QRectF editorRect = item->mapToScene(item->labelRect()).boundingRect().adjusted(-2.0, -2.0, 2.0, 2.0);
    auto *proxy = new QGraphicsProxyWidget;
    auto *editor = new QLineEdit;
    editor->setObjectName(QStringLiteral("inlineNameEditor"));
    editor->setFont(item->labelFont());
    editor->setStyleSheet(QStringLiteral("QLineEdit { border: 2px solid %1; border-radius: 4px; padding: 2px 4px; background: %2; color: %3; }")
        .arg(diagram_palette::SelectionColor.name(), diagram_palette::EntityFillColor.name(), diagram_palette::EntityTextColor.name()));
    editor->setText(QString::fromStdString(model->displayName));
    editor->resize(editorRect.size().toSize());
    proxy->setWidget(editor);
    proxy->setPos(editorRect.topLeft());
    proxy->setZValue(30.0);
    addItem(proxy);
    m_inlineNameEditor = proxy;
    m_inlineNameId = item->id();
    clearSelection();
    item->setSelected(true);
    connect(editor, &QLineEdit::editingFinished, this, [this]() {
        finishInlineRename(true);
    });
    connect(editor, &QLineEdit::textChanged, this, [this, editor]() {
        QToolTip::hideText();
        editor->setToolTip(QString());
    });
    editor->installEventFilter(this);
    editor->setFocus(Qt::MouseFocusReason);
    editor->selectAll();
}

void DiagramScene::finishInlineRename(bool commit)
{
    if (!m_inlineNameEditor) {
        m_inlineNameId = 0;
        return;
    }
    QGraphicsProxyWidget *proxy = m_inlineNameEditor;
    auto *editor = qobject_cast<QLineEdit *>(proxy->widget());
    if (commit && editor && !m_controller->renameElement(m_inlineNameId, editor->text())) {
        editor->setToolTip(tr("The name cannot be empty or duplicate another variable."));
        QToolTip::showText(editor->mapToGlobal(QPoint(0, editor->height())), editor->toolTip(), editor);
        editor->setFocus(Qt::OtherFocusReason);
        editor->selectAll();
        return;
    }
    m_inlineNameEditor.clear();
    m_inlineNameId = 0;
    if (editor)
        editor->blockSignals(true);
    removeItem(proxy);
    proxy->deleteLater();
}

void DiagramScene::updateAutomaticRoute(RoutedItem *item)
{
    if (!item || item->type() != ArrowItem::Type || !item->from() || !item->to())
        return;
    const Route route = m_controller->document().diagram().route(item->id());
    if (route.manual || route.locked || route.mode != RouteMode::Orthogonal)
        return;
    item->setAutomaticRoute(m_router.route(item->from()->center(), item->to()->center(), item->from()->id(), item->to()->id(), m_obstacles));
}

void DiagramScene::rerouteAll()
{
    for (DiagramItem *item : m_items) {
        if (auto *routed = dynamic_cast<RoutedItem *>(item))
            updateAutomaticRoute(routed);
    }
}

void DiagramScene::toggleSelectedRouteLock()
{
    for (QGraphicsItem *item : selectedItems()) {
        if (auto *routed = dynamic_cast<RoutedItem *>(item)) {
            Route route = m_controller->document().diagram().route(routed->id());
            route.locked = !route.locked;
            m_controller->setRoute(routed->id(), route);
        }
    }
}

void DiagramScene::updateSceneRect()
{
    QRectF bounds = itemsBoundingRect().adjusted(-1000.0, -1000.0, 1000.0, 1000.0);
    if (bounds.isEmpty())
        bounds = QRectF(-1000.0, -1000.0, 2000.0, 2000.0);
    setSceneRect(bounds);
}

void DiagramScene::finishMove(EntityId id, const QPointF &position, Qt::KeyboardModifiers modifiers)
{
    const QList<QGraphicsItem *> selection = selectedItems();
    if (modifiers & Qt::ControlModifier) {
        const Vec2 origin = m_controller->document().diagram().position(id);
        const QPointF offset = snapPoint(position) - QPointF(origin.x, origin.y);
        if (offset.isNull())
            return;
        std::vector<EntityId> ids;
        for (QGraphicsItem *selected : selection) {
            auto *item = dynamic_cast<DiagramItem *>(selected);
            if (item && (item->flags() & QGraphicsItem::ItemIsMovable))
                ids.push_back(item->id());
        }
        const ClipboardData clipboard = m_controller->copyElements(ids);
        const std::vector<EntityId> copies = m_controller->pasteClipboard(clipboard, offset);
        for (EntityId original : ids) {
            DiagramItem *item = m_items.value(original);
            const Vec2 originalPosition = m_controller->document().diagram().position(original);
            if (item)
                item->setPos(QPointF(originalPosition.x, originalPosition.y));
        }
        clearSelection();
        for (EntityId copy : copies) {
            if (DiagramItem *item = m_items.value(copy))
                item->setSelected(true);
        }
        return;
    }

    std::vector<std::pair<EntityId, QPointF>> positions;
    for (QGraphicsItem *item : selection) {
        auto *diagramItem = dynamic_cast<DiagramItem *>(item);
        if (diagramItem && (diagramItem->flags() & QGraphicsItem::ItemIsMovable))
            positions.emplace_back(diagramItem->id(), snapPoint(diagramItem->pos()));
    }
    m_controller->moveElements(positions);
}

void DiagramScene::drawBackground(QPainter *painter, const QRectF &rect)
{
    painter->fillRect(rect, diagram_palette::CanvasBackgroundColor);
    if (!m_gridVisible)
        return;
    painter->setPen(QPen(diagram_palette::GridColor, 0.0));
    const double left = qFloor(rect.left() / m_gridSize) * m_gridSize;
    const double top = qFloor(rect.top() / m_gridSize) * m_gridSize;
    for (double x = left; x < rect.right(); x += m_gridSize)
        painter->drawLine(QPointF(x, rect.top()), QPointF(x, rect.bottom()));
    for (double y = top; y < rect.bottom(); y += m_gridSize)
        painter->drawLine(QPointF(rect.left(), y), QPointF(rect.right(), y));
}
