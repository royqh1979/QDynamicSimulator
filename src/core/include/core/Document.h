#pragma once

#include <core/Diagram.h>
#include <core/Dimension.h>
#include <core/Macro.h>
#include <core/Model.h>

#include <memory>
#include <optional>
#include <QHash>
#include <QSet>
#include <QString>
#include <unordered_map>
#include <unordered_set>
#include <QVector>

namespace qds::core {

struct Workspace {
    bool hasViewport = false;
    double zoom = 1.0;
    Vec2 center;
    bool hasScrollValues = false;
    int scrollX = 0;
    int scrollY = 0;

    bool operator==(const Workspace &other) const
    {
        return hasViewport == other.hasViewport && zoom == other.zoom && center == other.center
            && hasScrollValues == other.hasScrollValues && scrollX == other.scrollX && scrollY == other.scrollY;
    }
    bool operator!=(const Workspace &other) const { return !(*this == other); }
};

struct ElementData {
    EntityId id = 0;
    ElementKind kind = ElementKind::Stock;
    QString displayName;
    QString equation;
    QVector<QString> dimensions;
    QVector<ArrayElementEquation> elementEquations;
    QString unit;
    QString comment;
    QString content;
    FlowEndpoints flowEndpoints;
    EntityId from = 0;
    EntityId to = 0;
    Vec2 position;
    Route route;
    FontStyle font;

    bool operator==(const ElementData &other) const
    {
        return id == other.id && kind == other.kind && displayName == other.displayName && equation == other.equation
            && dimensions == other.dimensions && elementEquations == other.elementEquations
            && unit == other.unit && comment == other.comment && content == other.content
            && flowEndpoints == other.flowEndpoints && from == other.from && to == other.to
            && position == other.position && route == other.route && font == other.font;
    }
    bool operator!=(const ElementData &other) const { return !(*this == other); }
};

using DocumentRange = ElementRange<DocumentElement>;

class Document
{
public:
    Document();
    Document(Document &&other) noexcept;
    Document &operator=(Document &&other) noexcept;
    Document(const Document &) = delete;
    Document &operator=(const Document &) = delete;

    const DocumentElement *find(EntityId id) const;
    const ModelElement *findByName(const QString &name) const;
    const Dimension *findDimension(const QString &name) const;
    const QVector<Dimension> &dimensions() const { return m_dimensions; }
    bool defineDimension(const QString &displayName, std::size_t size);
    bool defineDimension(const QString &displayName, const QVector<QString> &elements);
    bool defineMacro(MacroDefinition macro);
    const MacroDefinition *findMacro(const QString &name, std::size_t argumentCount) const;
    const QVector<MacroDefinition> &macros() const { return m_macros; }

    DocumentRange elements() const { return DocumentRange(m_elements); }
    const Model &model() const { return m_model; }
    const DiagramDefinition &diagram() const { return m_diagram; }
    const Workspace &workspace() const { return m_workspace; }
    bool setWorkspace(const Workspace &workspace);

    EntityId createVariable(ElementKind kind, Vec2 position = {});
    EntityId createFlow(EntityId from, EntityId to);
    EntityId createConnector(EntityId from, EntityId to);
    EntityId createCloud(Vec2 position = {});
    EntityId createComment(const QString &text, Vec2 position = {});

    QVector<EntityId> cascade(EntityId id) const;
    QVector<EntityId> remove(EntityId id);
    bool restore(const ElementData &data);
    bool apply(const ElementData &data);
    std::optional<ElementData> capture(EntityId id) const;

    bool rename(EntityId id, const QString &displayName);
    bool setEquation(EntityId id, const QString &equation);
    bool setDimensions(EntityId id, const QVector<QString> &dimensions);
    bool setElementEquation(EntityId id, const QVector<std::size_t> &indices, const QString &equation);
    bool setUnit(EntityId id, const QString &unit);
    bool setComment(EntityId id, const QString &comment);
    bool setCommentContent(EntityId id, const QString &content);
    bool setPosition(EntityId id, Vec2 position);
    bool setFontStyle(EntityId id, const FontStyle &font);
    bool reconnectFlow(EntityId id, EntityId from, EntityId to);
    bool reconnectConnector(EntityId id, EntityId from, EntityId to);
    bool setRoute(EntityId id, const Route &route);
    void clear();

private:
    friend class Model;
    friend class DiagramDefinition;

    DocumentElement *findMutable(EntityId id);

    template <typename T>
    EntityId insert(std::unique_ptr<T> element, Vec2 position);
    bool defineDimensionData(Dimension dimension);
    bool validRename(EntityId id, const QString &displayName) const;
    bool validFlowEndpoint(EntityId id) const;
    bool validFlowConnection(EntityId ignoredId, EntityId from, EntityId to) const;
    bool validConnectorEndpoints(EntityId from, EntityId to) const;
    bool validConnectorConnection(EntityId ignoredId, EntityId from, EntityId to) const;
    static bool validFontData(ElementKind kind, const FontStyle &font);
    static bool validRouteData(ElementKind kind, const Route &route);
    bool validArrayData(const QVector<QString> &dimensions,
        const QVector<ArrayElementEquation> &elementEquations, const QString &equation) const;
    QString uniqueName(ElementKind kind);
    void updateReferences(EntityId id, const QString &oldName, const QString &newName);

    std::unordered_map<EntityId, std::unique_ptr<DocumentElement>> m_elements;
    QHash<QString, EntityId> m_nameIndex;
    QVector<Dimension> m_dimensions;
    QHash<QString, int> m_dimensionIndex;
    QVector<MacroDefinition> m_macros;
    QSet<QString> m_macroNames;
    std::unordered_map<ElementKind, std::uint64_t> m_nextNameSuffix;
    EntityId m_nextId = 1;
    Model m_model;
    DiagramDefinition m_diagram;
    Workspace m_workspace;
};

} // namespace qds::core
