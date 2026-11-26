#pragma once

#include <core/Element.h>
#include <core/ElementRange.h>

#include <cstddef>
#include <QString>
#include <utility>
#include <QVector>

namespace qds::core {

struct ArrayElementEquation {
    QVector<std::size_t> indices;
    QString equation;

    bool operator==(const ArrayElementEquation &other) const { return indices == other.indices && equation == other.equation; }
    bool operator!=(const ArrayElementEquation &other) const { return !(*this == other); }
};

class ModelElement : public DocumentElement
{
public:
    const QString &displayName() const { return m_displayName; }
    const QString &nameKey() const { return m_nameKey; }
    const QString &equation() const { return m_equation; }
    const QVector<QString> &dimensions() const { return m_dimensions; }
    const QVector<ArrayElementEquation> &elementEquations() const { return m_elementEquations; }
    const QString &unit() const { return m_unit; }
    const QString &comment() const { return m_comment; }

protected:
    ModelElement(EntityId id, ElementKind kind)
        : DocumentElement(id, kind)
    {
    }

private:
    friend class Document;

    void setName(QString displayName, QString nameKey)
    {
        m_displayName = std::move(displayName);
        m_nameKey = std::move(nameKey);
    }
    void setFormulaData(QString equation, QVector<QString> dimensions,
        QVector<ArrayElementEquation> elementEquations)
    {
        m_equation = std::move(equation);
        m_dimensions = std::move(dimensions);
        m_elementEquations = std::move(elementEquations);
    }
    void setEquation(QString equation)
    {
        m_equation = std::move(equation);
        m_elementEquations.clear();
    }
    void setDimensions(QVector<QString> dimensions)
    {
        m_dimensions = std::move(dimensions);
        m_elementEquations.clear();
    }
    void setElementEquations(QVector<ArrayElementEquation> equations)
    {
        m_equation.clear();
        m_elementEquations = std::move(equations);
    }
    void setUnit(QString unit) { m_unit = std::move(unit); }
    void setComment(QString comment) { m_comment = std::move(comment); }

    QString m_displayName;
    QString m_nameKey;
    QString m_equation;
    QVector<QString> m_dimensions;
    QVector<ArrayElementEquation> m_elementEquations;
    QString m_unit;
    QString m_comment;
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
    const ModelElement *findByName(const QString &name) const;

private:
    const Document &m_document;
};

} // namespace qds::core
