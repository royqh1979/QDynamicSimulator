#pragma once

#include <core/Model.h>

#include <iterator>
#include <memory>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace qds::core {

struct Vec2 {
    double x = 0.0;
    double y = 0.0;

    bool operator==(const Vec2 &other) const { return x == other.x && y == other.y; }
    bool operator!=(const Vec2 &other) const { return !(*this == other); }
    Vec2 operator+(const Vec2 &other) const { return {x + other.x, y + other.y}; }
};

enum class RouteMode {
    Straight,
    Orthogonal,
    Bezier
};

struct RoutePoint {
    Vec2 relative;
    bool automatic = false;

    bool operator==(const RoutePoint &other) const { return relative == other.relative && automatic == other.automatic; }
    bool operator!=(const RoutePoint &other) const { return !(*this == other); }
};

struct Route {
    RouteMode mode = RouteMode::Straight;
    std::vector<RoutePoint> controlPoints;
    bool manual = false;
    bool locked = false;

    bool operator==(const Route &other) const
    {
        return mode == other.mode && controlPoints == other.controlPoints && manual == other.manual && locked == other.locked;
    }
    bool operator!=(const Route &other) const { return !(*this == other); }
};

Route defaultConnectorRoute();

struct FontStyle {
    std::string family;
    int pointSize = 0;

    bool operator==(const FontStyle &other) const { return family == other.family && pointSize == other.pointSize; }
    bool operator!=(const FontStyle &other) const { return !(*this == other); }
};

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
};

class Document;

template <typename T>
class ElementRange
{
    using Store = std::unordered_map<EntityId, std::unique_ptr<DocumentElement>>;
    using BaseIterator = Store::const_iterator;

    static bool matches(ElementKind kind)
    {
        if constexpr (std::is_same_v<T, DocumentElement>) {
            return true;
        } else if constexpr (std::is_same_v<T, ModelElement>) {
            return kind <= ElementKind::Data;
        } else {
            return kind >= ElementKind::Connector;
        }
    }

public:
    class Iterator
    {
    public:
        using iterator_category = std::forward_iterator_tag;
        using value_type = const T;
        using difference_type = std::ptrdiff_t;
        using pointer = const T *;
        using reference = const T &;

        Iterator(BaseIterator current, BaseIterator end)
            : m_current(current)
            , m_end(end)
        {
            seek();
        }

        reference operator*() const { return static_cast<const T &>(*m_current->second); }
        pointer operator->() const { return &static_cast<const T &>(*m_current->second); }
        Iterator &operator++()
        {
            ++m_current;
            seek();
            return *this;
        }
        bool operator==(const Iterator &other) const { return m_current == other.m_current; }
        bool operator!=(const Iterator &other) const { return !(*this == other); }

    private:
        void seek()
        {
            while (m_current != m_end && !matches(m_current->second->kind()))
                ++m_current;
        }

        BaseIterator m_current;
        BaseIterator m_end;
    };

    explicit ElementRange(const Store &elements)
        : m_elements(elements)
    {
    }

    Iterator begin() const { return Iterator(m_elements.begin(), m_elements.end()); }
    Iterator end() const { return Iterator(m_elements.end(), m_elements.end()); }
    bool empty() const { return begin() == end(); }

private:
    const Store &m_elements;
};

using DocumentRange = ElementRange<DocumentElement>;
using ModelRange = ElementRange<ModelElement>;
using ViewRange = ElementRange<ViewElement>;

class DiagramDefinition
{
public:
    explicit DiagramDefinition(const Document &document)
        : m_document(document)
    {
    }

    ViewRange elements() const;
    Vec2 position(EntityId id) const;
    Route route(EntityId id) const;
    FontStyle fontStyle(EntityId id) const;
    const std::unordered_map<EntityId, Vec2> &positions() const { return m_positions; }
    const std::unordered_map<EntityId, Route> &routes() const { return m_routes; }
    const std::unordered_map<EntityId, FontStyle> &fontStyles() const { return m_fontStyles; }

private:
    friend class Document;
    const Document &m_document;
    std::unordered_map<EntityId, Vec2> m_positions;
    std::unordered_map<EntityId, Route> m_routes;
    std::unordered_map<EntityId, FontStyle> m_fontStyles;
};

class Document
{
public:
    Document();
    Document(Document &&other) noexcept;
    Document &operator=(Document &&other) noexcept;
    Document(const Document &) = delete;
    Document &operator=(const Document &) = delete;

    DocumentElement *find(EntityId id);
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
    void setWorkspace(const Workspace &workspace) { m_workspace = workspace; }

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

    template <typename T>
    EntityId insert(std::unique_ptr<T> element, Vec2 position);
    bool defineDimensionData(Dimension dimension);
    bool validFlowEndpoint(EntityId id) const;
    bool validConnectorEndpoints(EntityId from, EntityId to) const;
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
