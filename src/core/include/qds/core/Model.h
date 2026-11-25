#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace qds::core {

using EntityId = std::uint64_t;
using PortId = std::uint32_t;

enum class ElementKind {
    Stock,
    Flow,
    Auxiliary,
    Constant,
    Data,
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

struct ArrayElementEquation {
    std::vector<std::size_t> indices;
    std::string equation;

    bool operator==(const ArrayElementEquation &other) const { return indices == other.indices && equation == other.equation; }
    bool operator!=(const ArrayElementEquation &other) const { return !(*this == other); }
};

class ModelElement : public DocumentElement
{
public:
    std::string displayName;
    std::string nameKey;
    std::string equation;
    std::vector<std::string> dimensions;
    std::vector<ArrayElementEquation> elementEquations;
    std::string unit;
    std::string comment;

protected:
    ModelElement(EntityId id, ElementKind kind)
        : DocumentElement(id, kind)
    {
    }
};

class Variable final : public ModelElement
{
public:
    Variable(EntityId id, ElementKind kind)
        : ModelElement(id, kind)
    {
    }
};

struct FlowEndpoints {
    EntityId from = 0;
    EntityId to = 0;

    bool operator==(const FlowEndpoints &other) const { return from == other.from && to == other.to; }
    bool operator!=(const FlowEndpoints &other) const { return !(*this == other); }
};

class Flow final : public ModelElement
{
public:
    explicit Flow(EntityId id)
        : ModelElement(id, ElementKind::Flow)
    {
    }

    FlowEndpoints endpoints;
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

struct DimensionElement {
    std::string displayName;
    std::string nameKey;

    bool operator==(const DimensionElement &other) const
    {
        return displayName == other.displayName && nameKey == other.nameKey;
    }
};

struct Dimension {
    std::string displayName;
    std::string nameKey;
    std::vector<DimensionElement> elements;

    const DimensionElement *findElement(const std::string &name) const;
    bool operator==(const Dimension &other) const
    {
        return displayName == other.displayName && nameKey == other.nameKey && elements == other.elements;
    }
};

class Document;
template <typename T>
class ElementRange;
using ModelRange = ElementRange<ModelElement>;

class Model
{
public:
    explicit Model(const Document &document)
        : m_document(document)
    {
    }

    ModelRange elements() const;
    const ModelElement *findByName(const std::string &name) const;

private:
    const Document &m_document;
};

std::string normalizeIdentifier(const std::string &name);
std::string trimDisplayName(const std::string &name);

} // namespace qds::core
