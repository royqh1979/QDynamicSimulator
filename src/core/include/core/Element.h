#pragma once

#include <cstdint>

namespace qds::core {

using EntityId = std::uint64_t;
using PortId = std::uint32_t;

enum class ElementKind {
    Stock,
    Flow,
    Variable,
    Connector,
    Cloud,
    Comment
};

class DocumentElement
{
public:
    virtual ~DocumentElement() = default;
    EntityId id() const { return m_id; }
    ElementKind kind() const { return m_kind; }

protected:
    DocumentElement(EntityId id, ElementKind kind)
        : m_id(id)
        , m_kind(kind)
    {
    }

private:
    EntityId m_id;
    ElementKind m_kind;
};

} // namespace qds::core
