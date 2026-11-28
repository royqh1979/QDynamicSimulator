#pragma once

#include <core/Element.h>
#include <core/ElementRange.h>
#include <core/formula/Formula.h>
#include <core/Lookup.h>

#include <cstddef>
#include <optional>
#include <QString>
#include <utility>
#include <QVector>
#include <vector>

namespace qds::core {

struct ArrayElementEquation {
    QVector<std::size_t> indices;
    QString equation;

    bool operator==(const ArrayElementEquation &other) const { return indices == other.indices && equation == other.equation; }
    bool operator!=(const ArrayElementEquation &other) const { return !(*this == other); }
};

class ArrayElementFormula
{
public:
    ArrayElementFormula(QVector<std::size_t> indices, Formula formula)
        : m_indices(std::move(indices))
        , m_formula(std::move(formula))
    {
    }

    const QVector<std::size_t> &indices() const { return m_indices; }
    const Formula &formula() const { return m_formula; }
    const QString &equation() const { return m_formula.source(); }

private:
    QVector<std::size_t> m_indices;
    Formula m_formula;
};

class ModelElement : public DocumentElement
{
public:
    const QString &displayName() const { return m_displayName; }
    const QString &nameKey() const { return m_nameKey; }
    const QString &equation() const { return m_formula.source(); }
    const Formula &formula() const { return m_formula; }
    const QVector<QString> &dimensions() const { return m_dimensions; }
    const std::vector<ArrayElementFormula> &elementEquations() const { return m_elementFormulas; }
    const QString &unit() const { return m_unit; }
    const QString &comment() const { return m_comment; }
    const std::optional<LookupDefinition> &lookup() const { return m_lookup; }

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
        m_formula = analyzeFormula(std::move(equation));
        m_dimensions = std::move(dimensions);
        m_elementFormulas.clear();
        for (const ArrayElementEquation &entry : elementEquations)
            m_elementFormulas.push_back(ArrayElementFormula(entry.indices, analyzeFormula(entry.equation)));
    }
    void setEquation(QString equation)
    {
        m_formula = analyzeFormula(std::move(equation));
        m_elementFormulas.clear();
    }
    void setDimensions(QVector<QString> dimensions)
    {
        m_dimensions = std::move(dimensions);
        m_elementFormulas.clear();
    }
    void setElementEquations(QVector<ArrayElementEquation> equations)
    {
        m_formula = Formula {};
        m_elementFormulas.clear();
        for (const ArrayElementEquation &entry : equations)
            m_elementFormulas.push_back(ArrayElementFormula(entry.indices, analyzeFormula(entry.equation)));
    }
    void setUnit(QString unit) { m_unit = std::move(unit); }
    void setComment(QString comment) { m_comment = std::move(comment); }
    void setLookup(std::optional<LookupDefinition> lookup) { m_lookup = std::move(lookup); }

    QVector<ArrayElementEquation> elementEquationData() const
    {
        QVector<ArrayElementEquation> result;
        for (const ArrayElementFormula &entry : m_elementFormulas)
            result.push_back({entry.indices(), entry.equation()});
        return result;
    }

    QString m_displayName;
    QString m_nameKey;
    Formula m_formula;
    QVector<QString> m_dimensions;
    std::vector<ArrayElementFormula> m_elementFormulas;
    QString m_unit;
    QString m_comment;
    std::optional<LookupDefinition> m_lookup;
};

class Stock final : public ModelElement
{
public:
    explicit Stock(EntityId id)
        : ModelElement(id, ElementKind::Stock)
    {
    }

    const QString &initialEquation() const { return m_initialFormula.source(); }
    const Formula &initialFormula() const { return m_initialFormula; }

private:
    friend class Document;
    void setInitialEquation(QString equation) { m_initialFormula = analyzeFormula(std::move(equation)); }

    Formula m_initialFormula;
};

class Variable final : public ModelElement
{
public:
    Variable(EntityId id, ElementKind kind)
        : ModelElement(id, kind)
    {
    }
};

class Lookup final : public ModelElement
{
public:
    explicit Lookup(EntityId id)
        : ModelElement(id, ElementKind::Lookup)
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
