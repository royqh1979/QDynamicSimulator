#pragma once

#include <core/Element.h>
#include <core/ElementRange.h>

#include <string>
#include <unordered_map>
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

class ViewElement : public DocumentElement
{
protected:
    ViewElement(EntityId id, ElementKind kind)
        : DocumentElement(id, kind)
    {
    }
};

class Connector final : public ViewElement
{
public:
    explicit Connector(EntityId id)
        : ViewElement(id, ElementKind::Connector)
    {
    }

    EntityId from = 0;
    EntityId to = 0;
};

class Cloud final : public ViewElement
{
public:
    explicit Cloud(EntityId id)
        : ViewElement(id, ElementKind::Cloud)
    {
    }
};

class Comment final : public ViewElement
{
public:
    explicit Comment(EntityId id)
        : ViewElement(id, ElementKind::Comment)
    {
    }

    std::string content;
};

class Document;
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

} // namespace qds::core
