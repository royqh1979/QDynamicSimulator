#include "DocumentController.h"

#include <QFont>
#include <QUndoCommand>

#include <algorithm>
#include <functional>
#include <unordered_map>
#include <unordered_set>

using namespace qds::core;

namespace {

class ChangeCommand : public QUndoCommand
{
public:
    using Apply = std::function<void(const ChangeSet &, bool)>;

    ChangeCommand(const Apply &apply, const ChangeSet &changes, bool alreadyApplied)
        : m_apply(apply)
        , m_changes(changes)
        , m_alreadyApplied(alreadyApplied)
    {
    }

    void undo() override;
    void redo() override;

private:
    Apply m_apply;
    ChangeSet m_changes;
    bool m_alreadyApplied;
    bool m_firstRedo = true;
};

class SimulationSettingsCommand : public QUndoCommand
{
public:
    using Apply = std::function<void(const SimulationSettings &)>;

    SimulationSettingsCommand(const Apply &apply, const SimulationSettings &before, const SimulationSettings &after)
        : m_apply(apply)
        , m_before(before)
        , m_after(after)
    {
    }

    void undo() override { m_apply(m_before); }
    void redo() override { m_apply(m_after); }

private:
    Apply m_apply;
    SimulationSettings m_before;
    SimulationSettings m_after;
};

ChangeRecord addedRecord(const Document &document, EntityId id)
{
    ChangeRecord record;
    record.id = id;
    record.kind = document.find(id)->kind();
    record.after = document.capture(id);
    return record;
}

ChangeRecord removedRecord(const Document &document, EntityId id)
{
    ChangeRecord record;
    record.id = id;
    record.kind = document.find(id)->kind();
    record.before = document.capture(id);
    return record;
}

int removalRank(ElementKind kind)
{
    if (kind == ElementKind::Connector)
        return 0;
    if (kind == ElementKind::Flow)
        return 1;
    return 2;
}

QPointF point(Vec2 value)
{
    return QPointF(value.x, value.y);
}

Vec2 vec(const QPointF &value)
{
    return {value.x(), value.y()};
}

QString uniqueCopyName(const Document &document, const QString &base)
{
    if (!document.findByName(base))
        return base;
    for (int suffix = 2;; ++suffix) {
        const QString candidate = base + " " + QString::number(suffix);
        if (!document.findByName(candidate))
            return candidate;
    }
}

bool affectsSimulation(const ChangeRecord &record)
{
    if (record.kind >= ElementKind::Connector)
        return false;
    if (record.before.has_value() != record.after.has_value())
        return true;
    if (!record.before)
        return false;
    const ElementData &before = *record.before;
    const ElementData &after = *record.after;
    return before.kind != after.kind || before.displayName != after.displayName
        || before.equation != after.equation || before.dimensions != after.dimensions
        || before.elementEquations != after.elementEquations || before.unit != after.unit
        || before.initialEquation != after.initialEquation || before.flowEndpoints != after.flowEndpoints;
}

} // namespace

void ChangeCommand::undo()
{
    m_apply(m_changes, false);
}

void ChangeCommand::redo()
{
    if (m_alreadyApplied && m_firstRedo) {
        m_firstRedo = false;
        return;
    }
    m_firstRedo = false;
    m_apply(m_changes, true);
}

DocumentController::DocumentController(QObject *parent)
    : QObject(parent)
{
}

const Document &DocumentController::document() const
{
    return m_document;
}

QUndoStack *DocumentController::undoStack()
{
    return &m_undoStack;
}

EntityId DocumentController::createVariable(ElementKind kind, const QPointF &position)
{
    const EntityId id = m_document.createVariable(kind, vec(position));
    if (id != 0)
        pushChanges(ChangeSet {{addedRecord(m_document, id)}}, true);
    return id;
}

EntityId DocumentController::createFlow(EntityId from, EntityId to)
{
    return createFlow(from, to, std::nullopt, std::nullopt);
}

EntityId DocumentController::createFlow(
    EntityId from,
    EntityId to,
    std::optional<QPointF> fromPosition,
    std::optional<QPointF> toPosition)
{
    if ((from == 0) != fromPosition.has_value() || (to == 0) != toPosition.has_value())
        return 0;
    ChangeSet changes;
    EntityId createdFrom = 0;
    EntityId createdTo = 0;
    if (from == 0) {
        from = m_document.createCloud(vec(*fromPosition));
        if (from == 0)
            return 0;
        createdFrom = from;
        changes.records.push_back(addedRecord(m_document, from));
    }
    if (to == 0) {
        to = m_document.createCloud(vec(*toPosition));
        if (to == 0) {
            if (createdFrom != 0)
                m_document.remove(createdFrom);
            return 0;
        }
        createdTo = to;
        changes.records.push_back(addedRecord(m_document, to));
    }
    const EntityId id = m_document.createFlow(from, to);
    if (id == 0) {
        if (createdFrom != 0)
            m_document.remove(createdFrom);
        if (createdTo != 0)
            m_document.remove(createdTo);
        return 0;
    }
    changes.records.push_back(addedRecord(m_document, id));
    pushChanges(changes, true);
    return id;
}

EntityId DocumentController::createConnector(EntityId from, EntityId to)
{
    const EntityId id = m_document.createConnector(from, to);
    if (id != 0)
        pushChanges(ChangeSet {{addedRecord(m_document, id)}}, true);
    return id;
}

EntityId DocumentController::createCloud(const QPointF &position)
{
    const EntityId id = m_document.createCloud(vec(position));
    if (id != 0)
        pushChanges(ChangeSet {{addedRecord(m_document, id)}}, true);
    return id;
}

EntityId DocumentController::createComment(const QString &text, const QPointF &position)
{
    const EntityId id = m_document.createComment(text, vec(position));
    if (id != 0)
        pushChanges(ChangeSet {{addedRecord(m_document, id)}}, true);
    return id;
}

void DocumentController::removeElements(const QVector<EntityId> &ids)
{
    ChangeSet changes;
    std::unordered_set<EntityId> included;
    for (EntityId id : ids) {
        for (EntityId affected : m_document.cascade(id)) {
            if (included.insert(affected).second)
                changes.records.push_back(removedRecord(m_document, affected));
        }
    }
    std::stable_sort(changes.records.begin(), changes.records.end(), [](const ChangeRecord &left, const ChangeRecord &right) {
        return removalRank(left.kind) < removalRank(right.kind);
    });
    if (!changes.records.empty())
        pushChanges(changes);
}

bool DocumentController::renameElement(EntityId id, const QString &name)
{
    if (!m_document.find(id))
        return false;
    ChangeRecord record;
    record.id = id;
    record.kind = m_document.find(id)->kind();
    record.before = m_document.capture(id);
    if (!m_document.rename(id, name))
        return false;
    record.after = m_document.capture(id);
    if (*record.before == *record.after)
        return true;
    pushChanges(ChangeSet {{std::move(record)}}, true);
    return true;
}

void DocumentController::setEquation(EntityId id, const QString &equation)
{
    if (!m_document.find(id))
        return;
    ChangeRecord record;
    record.id = id;
    record.kind = m_document.find(id)->kind();
    record.before = m_document.capture(id);
    if (!m_document.setEquation(id, equation))
        return;
    record.after = m_document.capture(id);
    pushChanges(ChangeSet {{std::move(record)}}, true);
}

void DocumentController::setInitialEquation(EntityId id, const QString &equation)
{
    if (!m_document.find(id))
        return;
    ChangeRecord record;
    record.id = id;
    record.kind = m_document.find(id)->kind();
    record.before = m_document.capture(id);
    if (!m_document.setInitialEquation(id, equation))
        return;
    record.after = m_document.capture(id);
    pushChanges(ChangeSet {{std::move(record)}}, true);
}

void DocumentController::setUnit(EntityId id, const QString &unit)
{
    if (!m_document.find(id))
        return;
    ChangeRecord record;
    record.id = id;
    record.kind = m_document.find(id)->kind();
    record.before = m_document.capture(id);
    if (!m_document.setUnit(id, unit))
        return;
    record.after = m_document.capture(id);
    pushChanges(ChangeSet {{std::move(record)}}, true);
}

void DocumentController::setComment(EntityId id, const QString &comment)
{
    if (!m_document.find(id))
        return;
    ChangeRecord record;
    record.id = id;
    record.kind = m_document.find(id)->kind();
    record.before = m_document.capture(id);
    if (!m_document.setComment(id, comment))
        return;
    record.after = m_document.capture(id);
    pushChanges(ChangeSet {{std::move(record)}}, true);
}

void DocumentController::setCommentContent(EntityId id, const QString &content)
{
    const auto *comment = dynamic_cast<const Comment *>(m_document.find(id));
    if (!comment || comment->content() == content)
        return;
    ChangeRecord record;
    record.id = id;
    record.kind = ElementKind::Comment;
    record.before = m_document.capture(id);
    if (!m_document.setCommentContent(id, content))
        return;
    record.after = m_document.capture(id);
    pushChanges(ChangeSet {{std::move(record)}}, true);
}

bool DocumentController::setSimulationSettings(const SimulationSettings &settings)
{
    if (!settings.isValid())
        return false;
    const SimulationSettings before = m_document.simulationSettings();
    if (before == settings)
        return true;
    m_undoStack.push(new SimulationSettingsCommand(
        [this](const SimulationSettings &value) {
            if (!m_document.setSimulationSettings(value))
                return;
            ++m_simulationRevision;
            notify(ChangeNotification {});
        },
        before, settings));
    return true;
}

void DocumentController::setFontStyle(EntityId id, const QFont &font)
{
    const QFont defaultFont;
    const FontStyle style = font.family() == defaultFont.family() && font.pointSize() == defaultFont.pointSize()
        ? FontStyle {} : FontStyle {font.family(), font.pointSize()};
    if (!m_document.find(id) || m_document.diagram().fontStyle(id) == style)
        return;
    ChangeRecord record;
    record.id = id;
    record.kind = m_document.find(id)->kind();
    record.before = m_document.capture(id);
    if (!m_document.setFontStyle(id, style))
        return;
    record.after = m_document.capture(id);
    pushChanges(ChangeSet {{std::move(record)}}, true);
}

void DocumentController::moveElements(const QVector<std::pair<EntityId, QPointF>> &positions)
{
    ChangeSet changes;
    for (const auto &entry : positions) {
        if (!m_document.find(entry.first))
            continue;
        const QPointF current = point(m_document.diagram().position(entry.first));
        if (current == entry.second)
            continue;
        ChangeRecord record;
        record.id = entry.first;
        record.kind = m_document.find(entry.first)->kind();
        record.before = m_document.capture(entry.first);
        m_document.setPosition(entry.first, vec(entry.second));
        record.after = m_document.capture(entry.first);
        changes.records.push_back(std::move(record));
    }
    if (!changes.records.empty())
        pushChanges(changes, true);
}

bool DocumentController::reconnectFlow(EntityId id, EntityId from, EntityId to)
{
    const Flow *flow = dynamic_cast<const Flow *>(m_document.find(id));
    if (!flow)
        return false;
    if (flow->endpoints().from == from && flow->endpoints().to == to)
        return true;
    ChangeRecord record;
    record.id = id;
    record.kind = ElementKind::Flow;
    record.before = m_document.capture(id);
    if (!m_document.reconnectFlow(id, from, to))
        return false;
    record.after = m_document.capture(id);
    pushChanges(ChangeSet {{std::move(record)}}, true);
    return true;
}

bool DocumentController::reconnectConnector(EntityId id, EntityId from, EntityId to)
{
    const Connector *connector = dynamic_cast<const Connector *>(m_document.find(id));
    if (!connector)
        return false;
    if (connector->from() == from && connector->to() == to)
        return true;
    ChangeRecord record;
    record.id = id;
    record.kind = ElementKind::Connector;
    record.before = m_document.capture(id);
    if (!m_document.reconnectConnector(id, from, to))
        return false;
    record.after = m_document.capture(id);
    pushChanges(ChangeSet {{std::move(record)}}, true);
    return true;
}

void DocumentController::setRoute(EntityId id, const Route &route)
{
    if (!m_document.find(id) || m_document.diagram().route(id) == route)
        return;
    ChangeRecord record;
    record.id = id;
    record.kind = m_document.find(id)->kind();
    record.before = m_document.capture(id);
    if (!m_document.setRoute(id, route))
        return;
    record.after = m_document.capture(id);
    pushChanges(ChangeSet {{std::move(record)}}, true);
}

void DocumentController::replaceDocument(Document document)
{
    m_document = std::move(document);
    ++m_simulationRevision;
    m_undoStack.clear();
    ChangeNotification notification;
    notification.documentReset = true;
    notify(notification);
}

ClipboardData DocumentController::copyElements(const QVector<EntityId> &ids) const
{
    ClipboardData clipboard;
    const std::unordered_set<EntityId> selected(ids.begin(), ids.end());
    for (EntityId id : ids) {
        const DocumentElement *element = m_document.find(id);
        if (!element)
            continue;
        if (element->kind() == ElementKind::Flow) {
            const Flow &flow = static_cast<const Flow &>(*element);
            if (!selected.count(flow.endpoints().from) || !selected.count(flow.endpoints().to))
                continue;
        }
        if (element->kind() == ElementKind::Connector) {
            const Connector &connector = static_cast<const Connector &>(*element);
            if (!selected.count(connector.from()) || !selected.count(connector.to()))
                continue;
        }
        clipboard.elements.push_back(*m_document.capture(id));
    }
    return clipboard;
}

QVector<EntityId> DocumentController::pasteClipboard(const ClipboardData &clipboard, const QPointF &offset)
{
    ChangeSet changes;
    QVector<EntityId> created;
    std::unordered_map<EntityId, EntityId> idMap;
    const auto rollback = [&created, this]() {
        for (auto it = created.rbegin(); it != created.rend(); ++it)
            m_document.remove(*it);
    };
    for (int pass = 0; pass < 3; ++pass) {
        for (const ElementData &source : clipboard.elements) {
            const bool routed = source.kind == ElementKind::Flow || source.kind == ElementKind::Connector;
            if ((pass == 0 && routed) || (pass == 1 && source.kind != ElementKind::Flow)
                || (pass == 2 && source.kind != ElementKind::Connector))
                continue;
            EntityId id = 0;
            if (source.kind == ElementKind::Flow) {
                if (!idMap.count(source.flowEndpoints.from) || !idMap.count(source.flowEndpoints.to))
                    continue;
                id = m_document.createFlow(idMap[source.flowEndpoints.from], idMap[source.flowEndpoints.to]);
            } else if (source.kind == ElementKind::Connector) {
                if (!idMap.count(source.from) || !idMap.count(source.to))
                    continue;
                id = m_document.createConnector(idMap[source.from], idMap[source.to]);
            } else if (source.kind == ElementKind::Cloud) {
                id = m_document.createCloud(source.position + Vec2({offset.x(), offset.y()}));
            } else if (source.kind == ElementKind::Comment) {
                id = m_document.createComment(source.content, source.position + Vec2({offset.x(), offset.y()}));
            } else {
                id = m_document.createVariable(source.kind, source.position + Vec2({offset.x(), offset.y()}));
            }
            if (id == 0) {
                rollback();
                return {};
            }
            ElementData target = source;
            target.id = id;
            target.position = source.position + Vec2({offset.x(), offset.y()});
            if (source.kind <= ElementKind::Variable)
                target.displayName = uniqueCopyName(m_document, source.displayName);
            if (source.kind == ElementKind::Flow)
                target.flowEndpoints = {idMap[source.flowEndpoints.from], idMap[source.flowEndpoints.to]};
            if (source.kind == ElementKind::Connector) {
                target.from = idMap[source.from];
                target.to = idMap[source.to];
            }
            if (!m_document.apply(target)) {
                rollback();
                return {};
            }
            idMap[source.id] = id;
            created.push_back(id);
            changes.records.push_back(addedRecord(m_document, id));
        }
    }
    if (!changes.records.empty())
        pushChanges(changes, true);
    return created;
}

void DocumentController::pushChanges(const ChangeSet &changes, bool alreadyApplied)
{
    if (std::any_of(changes.records.begin(), changes.records.end(), affectsSimulation))
        ++m_simulationRevision;
    m_undoStack.push(new ChangeCommand(
        [this](const ChangeSet &value, bool after) {
            applyChanges(value, after);
        },
        changes,
        alreadyApplied));
    if (alreadyApplied) {
        ChangeNotification notification;
        for (const ChangeRecord &record : changes.records) {
            if (!record.after)
                notification.removed.push_back(record.id);
            else if (record.before)
                notification.updated.push_back(record.id);
            else
                notification.added.push_back(record.id);
        }
        notify(notification);
    }
}

void DocumentController::applyChanges(const ChangeSet &changes, bool after)
{
    if (std::any_of(changes.records.begin(), changes.records.end(), affectsSimulation))
        ++m_simulationRevision;
    ChangeNotification notification;
    const auto applyRecord = [this, after, &notification](const ChangeRecord &record) {
        const auto &value = after ? record.after : record.before;
        if (value) {
            const bool existed = m_document.find(record.id) != nullptr;
            if (m_document.apply(*value)) {
                if (existed)
                    notification.updated.push_back(record.id);
                else
                    notification.added.push_back(record.id);
            }
        } else {
            m_document.remove(record.id);
            notification.removed.push_back(record.id);
        }
    };
    if (after) {
        for (const ChangeRecord &record : changes.records)
            applyRecord(record);
    } else {
        for (auto it = changes.records.rbegin(); it != changes.records.rend(); ++it)
            applyRecord(*it);
    }
    notify(notification);
}

void DocumentController::notify(const ChangeNotification &notification)
{
    emit documentChanged(notification);
}
