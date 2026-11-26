#pragma once

#include <core/Element.h>
#include <core/ElementRange.h>

#include <cstddef>
#include <string>
#include <vector>

namespace qds::core {

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

class Document;
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

} // namespace qds::core
