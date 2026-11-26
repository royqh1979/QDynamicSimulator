#pragma once

#include <core/Diagram.h>
#include <core/Dimension.h>
#include <core/Macro.h>
#include <core/Model.h>

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

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
    std::string displayName;
    std::string equation;
    std::vector<std::string> dimensions;
    std::vector<ArrayElementEquation> elementEquations;
    std::string unit;
    std::string comment;
    std::string content;
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
    const ModelElement *findByName(const std::string &name) const;
    const Dimension *findDimension(const std::string &name) const;
    const std::vector<Dimension> &dimensions() const { return m_dimensions; }
    bool defineDimension(const std::string &displayName, std::size_t size);
    bool defineDimension(const std::string &displayName, const std::vector<std::string> &elements);
    bool defineMacro(MacroDefinition macro);
    const MacroDefinition *findMacro(const std::string &name, std::size_t argumentCount) const;
    const std::vector<MacroDefinition> &macros() const { return m_macros; }

    DocumentRange elements() const { return DocumentRange(m_elements); }
    const Model &model() const { return m_model; }
    const DiagramDefinition &diagram() const { return m_diagram; }
    const Workspace &workspace() const { return m_workspace; }
    bool setWorkspace(const Workspace &workspace);

    EntityId createVariable(ElementKind kind, Vec2 position = {});
    EntityId createFlow(EntityId from, EntityId to);
    EntityId createConnector(EntityId from, EntityId to);
    EntityId createCloud(Vec2 position = {});
    EntityId createComment(const std::string &text, Vec2 position = {});

    std::vector<EntityId> cascade(EntityId id) const;
    std::vector<EntityId> remove(EntityId id);
    bool restore(const ElementData &data);
    bool apply(const ElementData &data);
    std::optional<ElementData> capture(EntityId id) const;

    bool rename(EntityId id, const std::string &displayName);
    bool setEquation(EntityId id, const std::string &equation);
    bool setDimensions(EntityId id, const std::vector<std::string> &dimensions);
    bool setElementEquation(EntityId id, const std::vector<std::size_t> &indices, const std::string &equation);
    bool setUnit(EntityId id, const std::string &unit);
    bool setComment(EntityId id, const std::string &comment);
    bool setCommentContent(EntityId id, const std::string &content);
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
    bool validRename(EntityId id, const std::string &displayName) const;
    bool validFlowEndpoint(EntityId id) const;
    bool validFlowConnection(EntityId ignoredId, EntityId from, EntityId to) const;
    bool validConnectorEndpoints(EntityId from, EntityId to) const;
    bool validConnectorConnection(EntityId ignoredId, EntityId from, EntityId to) const;
    static bool validFontData(ElementKind kind, const FontStyle &font);
    static bool validRouteData(ElementKind kind, const Route &route);
    bool validArrayData(const std::vector<std::string> &dimensions,
        const std::vector<ArrayElementEquation> &elementEquations, const std::string &equation) const;
    std::string uniqueName(ElementKind kind);
    void updateReferences(EntityId id, const std::string &oldName, const std::string &newName);

    std::unordered_map<EntityId, std::unique_ptr<DocumentElement>> m_elements;
    std::unordered_map<std::string, EntityId> m_nameIndex;
    std::vector<Dimension> m_dimensions;
    std::unordered_map<std::string, std::size_t> m_dimensionIndex;
    std::vector<MacroDefinition> m_macros;
    std::unordered_set<std::string> m_macroNames;
    std::unordered_map<ElementKind, std::uint64_t> m_nextNameSuffix;
    EntityId m_nextId = 1;
    Model m_model;
    DiagramDefinition m_diagram;
    Workspace m_workspace;
};

} // namespace qds::core
