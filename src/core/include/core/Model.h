#pragma once

#include <core/Element.h>
#include <core/ElementRange.h>

#include <cstddef>
#include <string>
#include <utility>
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
    const std::string &displayName() const { return m_displayName; }
    const std::string &nameKey() const { return m_nameKey; }
    const std::string &equation() const { return m_equation; }
    const std::vector<std::string> &dimensions() const { return m_dimensions; }
    const std::vector<ArrayElementEquation> &elementEquations() const { return m_elementEquations; }
    const std::string &unit() const { return m_unit; }
    const std::string &comment() const { return m_comment; }

protected:
    ModelElement(EntityId id, ElementKind kind)
        : DocumentElement(id, kind)
    {
    }

private:
    friend class Document;

    void setName(std::string displayName, std::string nameKey)
    {
        m_displayName = std::move(displayName);
        m_nameKey = std::move(nameKey);
    }
    void setFormulaData(std::string equation, std::vector<std::string> dimensions,
        std::vector<ArrayElementEquation> elementEquations)
    {
        m_equation = std::move(equation);
        m_dimensions = std::move(dimensions);
        m_elementEquations = std::move(elementEquations);
    }
    void setEquation(std::string equation)
    {
        m_equation = std::move(equation);
        m_elementEquations.clear();
    }
    void setDimensions(std::vector<std::string> dimensions)
    {
        m_dimensions = std::move(dimensions);
        m_elementEquations.clear();
    }
    void setElementEquations(std::vector<ArrayElementEquation> equations)
    {
        m_equation.clear();
        m_elementEquations = std::move(equations);
    }
    void setUnit(std::string unit) { m_unit = std::move(unit); }
    void setComment(std::string comment) { m_comment = std::move(comment); }

    std::string m_displayName;
    std::string m_nameKey;
    std::string m_equation;
    std::vector<std::string> m_dimensions;
    std::vector<ArrayElementEquation> m_elementEquations;
    std::string m_unit;
    std::string m_comment;
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

    const FlowEndpoints &endpoints() const { return m_endpoints; }

private:
    friend class Document;
    void setEndpoints(FlowEndpoints endpoints) { m_endpoints = endpoints; }

    FlowEndpoints m_endpoints;
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
