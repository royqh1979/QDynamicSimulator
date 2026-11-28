#pragma once

#include <core/Element.h>
#include <core/formula/Formula.h>
#include <core/formula/Function.h>

#include <cstddef>
#include <memory>
#include <QString>
#include <utility>
#include <vector>

namespace qds::core {

class CompiledMacroDefinition;
class LookupDefinition;

enum class BoundReferenceKind {
    Variable,
    Dimension,
    DimensionElement,
    Time,
    Dt,
    StartTime,
    StopTime,
    Pi,
    Infinity
};

class BoundReferenceNode final : public FormulaNode
{
public:
    BoundReferenceNode(BoundReferenceKind referenceKind, QString nameKey, EntityId variable, std::size_t element,
        SourceSpan span)
        : FormulaNode(FormulaNodeKind::BoundReference, span)
        , m_referenceKind(referenceKind)
        , m_nameKey(std::move(nameKey))
        , m_variable(variable)
        , m_element(element)
    {
    }

    BoundReferenceKind referenceKind() const { return m_referenceKind; }
    const QString &nameKey() const { return m_nameKey; }
    EntityId variable() const { return m_variable; }
    std::size_t element() const { return m_element; }

private:
    BoundReferenceKind m_referenceKind;
    QString m_nameKey;
    EntityId m_variable;
    std::size_t m_element;
};

class LocalReferenceNode final : public FormulaNode
{
public:
    LocalReferenceNode(QString nameKey, SourceSpan span)
        : FormulaNode(FormulaNodeKind::LocalReference, span)
        , m_nameKey(std::move(nameKey))
    {
    }

    const QString &nameKey() const { return m_nameKey; }

private:
    QString m_nameKey;
};

enum class BoundCallKind {
    Builtin,
    Macro,
    Lookup
};

class BoundCallNode final : public FormulaNode
{
public:
    BoundCallNode(QString nameKey, FunctionSignature signature,
        std::vector<std::unique_ptr<FormulaNode>> arguments, SourceSpan span)
        : BoundCallNode(BoundCallKind::Builtin, std::move(nameKey), signature, nullptr, nullptr,
            std::move(arguments), span)
    {
    }

    BoundCallNode(QString nameKey, const CompiledMacroDefinition *macro,
        std::vector<std::unique_ptr<FormulaNode>> arguments, SourceSpan span)
        : BoundCallNode(BoundCallKind::Macro, std::move(nameKey), {}, macro, nullptr, std::move(arguments), span)
    {
    }

    BoundCallNode(QString nameKey, const LookupDefinition *lookup,
        std::vector<std::unique_ptr<FormulaNode>> arguments, SourceSpan span)
        : BoundCallNode(BoundCallKind::Lookup, std::move(nameKey), {}, nullptr, lookup, std::move(arguments), span)
    {
    }

    BoundCallKind callKind() const { return m_callKind; }
    const QString &nameKey() const { return m_nameKey; }
    const FunctionSignature &signature() const { return m_signature; }
    const CompiledMacroDefinition *macro() const { return m_macro; }
    const LookupDefinition *lookup() const { return m_lookup; }
    std::size_t argumentCount() const { return m_arguments.size(); }
    const FormulaNode *argument(std::size_t position) const { return m_arguments[position].get(); }

private:
    BoundCallNode(BoundCallKind callKind, QString nameKey, FunctionSignature signature,
        const CompiledMacroDefinition *macro, const LookupDefinition *lookup,
        std::vector<std::unique_ptr<FormulaNode>> arguments, SourceSpan span)
        : FormulaNode(FormulaNodeKind::BoundCall, span)
        , m_callKind(callKind)
        , m_nameKey(std::move(nameKey))
        , m_signature(signature)
        , m_macro(macro)
        , m_lookup(lookup)
        , m_arguments(std::move(arguments))
    {
    }

    BoundCallKind m_callKind;
    QString m_nameKey;
    FunctionSignature m_signature;
    const CompiledMacroDefinition *m_macro;
    const LookupDefinition *m_lookup;
    std::vector<std::unique_ptr<FormulaNode>> m_arguments;
};

class CompiledFormula
{
public:
    CompiledFormula() = default;
    explicit CompiledFormula(std::unique_ptr<FormulaNode> root)
        : m_root(std::move(root))
    {
    }

    CompiledFormula(CompiledFormula &&) noexcept = default;
    CompiledFormula &operator=(CompiledFormula &&) noexcept = default;
    CompiledFormula(const CompiledFormula &) = delete;
    CompiledFormula &operator=(const CompiledFormula &) = delete;

    const FormulaNode *root() const { return m_root.get(); }

private:
    std::unique_ptr<FormulaNode> m_root;
};

class CompiledMacroParameter
{
public:
    const QString &nameKey() const { return m_nameKey; }
    const CompiledFormula &defaultFormula() const { return m_defaultFormula; }

private:
    friend class SimulationCompiler;

    QString m_nameKey;
    CompiledFormula m_defaultFormula;
};

class CompiledMacroDefinition
{
public:
    const std::vector<CompiledMacroParameter> &parameters() const { return m_parameters; }
    const CompiledFormula &formula() const { return m_formula; }

private:
    friend class SimulationCompiler;

    std::vector<CompiledMacroParameter> m_parameters;
    CompiledFormula m_formula;
};

struct CompiledArrayElementFormula {
    QVector<std::size_t> indices;
    CompiledFormula formula;
};

struct CompiledElementFormula {
    CompiledFormula formula;
    std::vector<CompiledArrayElementFormula> elements;
};

} // namespace qds::core
