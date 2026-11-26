#include <core/Diagram.h>

#include <core/Document.h>

namespace qds::core {

ViewRange DiagramDefinition::elements() const
{
    return ViewRange(m_document.m_elements);
}

Vec2 DiagramDefinition::position(EntityId id) const
{
    const auto it = m_positions.find(id);
    return it == m_positions.end() ? Vec2 {} : it->second;
}

Route DiagramDefinition::route(EntityId id) const
{
    Route result;
    const DocumentElement *element = m_document.find(id);
    if (element && element->kind() == ElementKind::Flow)
        result.mode = RouteMode::Straight;
    const auto it = m_routes.find(id);
    return it == m_routes.end() ? result : it->second;
}

FontStyle DiagramDefinition::fontStyle(EntityId id) const
{
    const auto it = m_fontStyles.find(id);
    return it == m_fontStyles.end() ? FontStyle {} : it->second;
}

Route defaultConnectorRoute()
{
    Route route;
    route.mode = RouteMode::Bezier;
    route.controlPoints = {{1.0 / 3.0, -50.0, true}, {2.0 / 3.0, -50.0, true}};
    return route;
}

} // namespace qds::core
