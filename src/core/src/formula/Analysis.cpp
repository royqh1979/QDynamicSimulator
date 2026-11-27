#include <core/formula/Analysis.h>

#include <core/formula/Function.h>
#include <core/Identifier.h>

#include <QMap>

#include <algorithm>
#include <functional>
#include <utility>

namespace qds::core {
namespace {

struct MacroCall {
    MacroSignature signature;
    int argumentCount = 0;

    bool operator==(const MacroCall &other) const
    {
        return signature == other.signature && argumentCount == other.argumentCount;
    }
};

uint qHash(const MacroCall &call, uint seed = 0) noexcept
{
    return ::qHash(call.signature.nameKey, seed) ^ ::qHash(call.signature.parameterCount, seed)
        ^ ::qHash(call.signature.outputCount, seed) ^ ::qHash(call.argumentCount, seed);
}

struct MacroDependencies {
    QSet<EntityId> references;
    QSet<MacroCall> calls;
    QVector<QSet<EntityId>> defaultReferences;
    QVector<QSet<MacroCall>> defaultCalls;
};

struct ResolutionTarget {
    EntityId ownerId = 0;
    QSet<QString> locals;
    QSet<EntityId> references;
    QSet<MacroCall> macroCalls;
};

bool isDimensionElement(const Document &document, const QString &nameKey)
{
    const int separator = nameKey.indexOf('.');
    if (separator < 0)
        return false;
    const Dimension *dimension = document.findDimension(nameKey.left(separator));
    return dimension && dimension->findElement(nameKey.mid(separator + 1));
}

} // namespace

class AnalysisBuilder
{
public:
    void addIssue(EntityId id, IssueSeverity severity, QString message, QString code, SourceSpan span)
    {
        m_issues.push_back({id, severity, std::move(message), std::move(code), span});
    }

    void addStructuralUse(EntityId id)
    {
        m_structurallyUsed.insert(id);
    }

    void addReference(EntityId owner, EntityId dependency)
    {
        m_structurallyUsed.insert(dependency);
        auto &edges = m_dependencies.m_edges[dependency];
        if (std::find(edges.begin(), edges.end(), owner) == edges.end())
            edges.push_back(owner);
    }

    void addMacroCall(EntityId owner, const MacroCall &call)
    {
        m_macroCalls[owner].insert(call);
    }

    void setMacroDependencies(const MacroSignature &signature, MacroDependencies dependencies)
    {
        m_macroDependencies.insert(signature, std::move(dependencies));
    }

    void commit(EntityId owner, const ResolutionTarget &target);
    const DependencyGraph &dependencies() const { return m_dependencies; }
    const QSet<EntityId> &structurallyUsed() const { return m_structurallyUsed; }
    void resolveMacroDependencies();
    AnalysisReport finish();

private:
    void collectMacroDependencies(const MacroCall &call, QSet<MacroCall> &visited, QSet<EntityId> &references) const;

    DependencyGraph m_dependencies;
    QSet<EntityId> m_structurallyUsed;
    QVector<ModelIssue> m_issues;
    QHash<EntityId, QSet<MacroCall>> m_macroCalls;
    QMap<MacroSignature, MacroDependencies> m_macroDependencies;
};

namespace {

void resolveNode(const Document &document, AnalysisBuilder &result, ResolutionTarget &target, const FormulaNode *node)
{
    if (!node)
        return;
    if (const auto *reference = dynamic_cast<const ReferenceNode *>(node)) {
        if (target.locals.contains(reference->nameKey()))
            return;
        if (const FunctionSignature *signature = findFunctionSignature(reference->nameKey());
            signature && signature->minArguments == 0 && signature->maxArguments == 0)
            return;
        if (document.findDimension(reference->nameKey()) || isDimensionElement(document, reference->nameKey()))
            return;
        if (const ModelElement *model = document.model().findByName(reference->nameKey())) {
            target.references.insert(model->id());
        } else {
            result.addIssue(target.ownerId, IssueSeverity::Error, "Undefined variable: " + reference->nameKey(),
                "undefined-variable", reference->span());
        }
        return;
    }
    if (const auto *unary = dynamic_cast<const UnaryNode *>(node)) {
        resolveNode(document, result, target, unary->operand());
        return;
    }
    if (const auto *binary = dynamic_cast<const BinaryNode *>(node)) {
        resolveNode(document, result, target, binary->left());
        resolveNode(document, result, target, binary->right());
        return;
    }
    if (const auto *call = dynamic_cast<const CallNode *>(node)) {
        const FunctionSignature *signature = findFunctionSignature(call->nameKey());
        if (signature) {
            if (call->argumentCount() < signature->minArguments || call->argumentCount() > signature->maxArguments) {
                result.addIssue(target.ownerId, IssueSeverity::Error,
                    "Invalid argument count for function: " + call->nameKey(), "invalid-arity", call->span());
            }
        } else if (const MacroDefinition *macro = document.findMacro(call->nameKey(), call->argumentCount(), 1)) {
            target.macroCalls.insert({macro->signature(), int(call->argumentCount())});
        } else if (const ModelElement *model = document.model().findByName(call->nameKey())) {
            target.references.insert(model->id());
        } else {
            result.addIssue(target.ownerId, IssueSeverity::Error, "Unknown function: " + call->nameKey(),
                "unknown-function", call->span());
        }
        for (int position = 0; position < call->argumentCount(); ++position)
            resolveNode(document, result, target, call->argument(position));
        return;
    }
    if (const auto *conditional = dynamic_cast<const ConditionalNode *>(node)) {
        resolveNode(document, result, target, conditional->condition());
        resolveNode(document, result, target, conditional->thenExpression());
        resolveNode(document, result, target, conditional->elseExpression());
        return;
    }
    if (const auto *subscript = dynamic_cast<const SubscriptNode *>(node)) {
        resolveNode(document, result, target, subscript->target());
        const auto *reference = dynamic_cast<const ReferenceNode *>(subscript->target());
        const ModelElement *targetElement = reference ? document.model().findByName(reference->nameKey()) : nullptr;
        for (int position = 0; position < subscript->indexCount(); ++position) {
            const FormulaNode *index = subscript->index(position);
            const auto *name = dynamic_cast<const ReferenceNode *>(index);
            if (name && targetElement && position < targetElement->dimensions().size()
                && !target.locals.contains(name->nameKey())) {
                const Dimension *dimension = document.findDimension(targetElement->dimensions()[position]);
                if (dimension && (name->nameKey() == dimension->nameKey || dimension->findElement(name->nameKey())))
                    continue;
            }
            resolveNode(document, result, target, index);
        }
        return;
    }
    if (const auto *range = dynamic_cast<const RangeNode *>(node)) {
        resolveNode(document, result, target, range->begin());
        resolveNode(document, result, target, range->end());
        return;
    }
    if (const auto *literal = dynamic_cast<const ArrayLiteralNode *>(node)) {
        for (int position = 0; position < literal->valueCount(); ++position)
            resolveNode(document, result, target, literal->value(position));
    }
}

ResolutionTarget resolveFormula(const Document &document, AnalysisBuilder &result, EntityId ownerId,
    const Formula &formula, const QSet<QString> &locals = {})
{
    for (const FormulaIssue &issue : formula.issues())
        result.addIssue(ownerId, IssueSeverity::Error, issue.message, "parse", issue.span);
    ResolutionTarget target;
    target.ownerId = ownerId;
    target.locals = locals;
    resolveNode(document, result, target, formula.root());
    return target;
}

void commitTarget(AnalysisBuilder &result, EntityId owner, const ResolutionTarget &target)
{
    for (EntityId reference : target.references)
        result.addReference(owner, reference);
    for (const MacroCall &call : target.macroCalls)
        result.addMacroCall(owner, call);
}

void reportCycles(const Document &document, AnalysisBuilder &result)
{
    QHash<EntityId, int> state;
    QSet<EntityId> reported;
    std::function<void(EntityId)> visit = [&](EntityId id) {
        const auto *model = dynamic_cast<const ModelElement *>(document.find(id));
        if (model && !model->dimensions().isEmpty()) {
            state[id] = 2;
            return;
        }
        state[id] = 1;
        const auto &edges = result.dependencies().edges();
        const auto found = edges.constFind(id);
        if (found != edges.constEnd()) {
            for (EntityId next : found.value()) {
                if (state[next] == 1) {
                    if (!reported.contains(next)) {
                        reported.insert(next);
                        result.addIssue(next, IssueSeverity::Error, "Algebraic dependency cycle", "algebraic-cycle", {});
                    }
                } else if (state[next] == 0) {
                    visit(next);
                }
            }
        }
        state[id] = 2;
    };
    for (const ModelElement &element : document.model().elements()) {
        if (state[element.id()] == 0)
            visit(element.id());
    }
}

} // namespace

void AnalysisBuilder::commit(EntityId owner, const ResolutionTarget &target)
{
    commitTarget(*this, owner, target);
}

void AnalysisBuilder::collectMacroDependencies(const MacroCall &call, QSet<MacroCall> &visited,
    QSet<EntityId> &references) const
{
    if (visited.contains(call))
        return;
    visited.insert(call);
    const auto found = m_macroDependencies.constFind(call.signature);
    if (found == m_macroDependencies.constEnd())
        return;
    const MacroDependencies &macro = found.value();
    references.unite(macro.references);
    for (const MacroCall &nested : macro.calls)
        collectMacroDependencies(nested, visited, references);
    for (int index = call.argumentCount; index < macro.defaultReferences.size(); ++index) {
        references.unite(macro.defaultReferences[index]);
        for (const MacroCall &nested : macro.defaultCalls[index])
            collectMacroDependencies(nested, visited, references);
    }
}

void AnalysisBuilder::resolveMacroDependencies()
{
    for (auto owner = m_macroCalls.constBegin(); owner != m_macroCalls.constEnd(); ++owner) {
        for (const MacroCall &call : owner.value()) {
            QSet<MacroCall> visited;
            QSet<EntityId> references;
            collectMacroDependencies(call, visited, references);
            for (EntityId reference : references)
                addReference(owner.key(), reference);
        }
    }
}

AnalysisReport AnalysisBuilder::finish()
{
    resolveMacroDependencies();
    std::sort(m_issues.begin(), m_issues.end(), [](const ModelIssue &left, const ModelIssue &right) {
        if (left.entityId != right.entityId)
            return left.entityId < right.entityId;
        if (left.code != right.code)
            return left.code < right.code;
        if (left.span.begin != right.span.begin)
            return left.span.begin < right.span.begin;
        if (left.span.end != right.span.end)
            return left.span.end < right.span.end;
        return left.message < right.message;
    });
    return AnalysisReport(std::move(m_dependencies), std::move(m_structurallyUsed), std::move(m_issues));
}

QSet<EntityId> AnalysisReport::references(EntityId dependent) const
{
    QSet<EntityId> result;
    const auto &edges = dependencies().edges();
    for (auto edge = edges.constBegin(); edge != edges.constEnd(); ++edge) {
        if (edge.value().contains(dependent))
            result.insert(edge.key());
    }
    return result;
}

AnalysisReport analyzeDocument(const Document &document)
{
    AnalysisBuilder result;
    for (const MacroDefinition &definition : document.macros()) {
        QSet<QString> locals;
        for (const MacroParameter &parameter : definition.parameters())
            locals.insert(parameter.nameKey());
        const ResolutionTarget body = resolveFormula(document, result, 0, definition.formula(), locals);
        MacroDependencies dependencies;
        dependencies.references = body.references;
        dependencies.calls = body.macroCalls;
        for (int index = 0; index < definition.parameters().size(); ++index) {
            QSet<QString> defaultLocals;
            for (int previous = 0; previous < index; ++previous)
                defaultLocals.insert(definition.parameters()[previous].nameKey());
            const ResolutionTarget value = resolveFormula(document, result, 0,
                definition.parameters()[index].defaultFormula(), defaultLocals);
            dependencies.defaultReferences.push_back(value.references);
            dependencies.defaultCalls.push_back(value.macroCalls);
        }
        result.setMacroDependencies(definition.signature(), std::move(dependencies));
    }
    for (const ViewElement &element : document.diagram().elements()) {
        if (element.kind() == ElementKind::Connector) {
            const Connector &connector = static_cast<const Connector &>(element);
            result.addStructuralUse(connector.from());
            result.addStructuralUse(connector.to());
        }
    }
    for (const ModelElement &element : document.model().elements()) {
        if (element.kind() == ElementKind::Flow) {
            const Flow &flow = static_cast<const Flow &>(element);
            result.addStructuralUse(flow.endpoints().from);
            result.addStructuralUse(flow.endpoints().to);
            result.addStructuralUse(flow.id());
        }
        const Formula &formula = element.kind() == ElementKind::Stock
            ? static_cast<const Stock &>(element).initialFormula() : element.formula();
        result.commit(element.id(), resolveFormula(document, result, element.id(), formula));
        for (const ArrayElementFormula &entry : element.elementEquations())
            result.commit(element.id(), resolveFormula(document, result, element.id(), entry.formula()));
        if (!element.elementEquations().empty()) {
            std::size_t expected = 1;
            for (const QString &name : element.dimensions())
                expected *= document.findDimension(name)->elements.size();
            if (element.elementEquations().size() != expected) {
                result.addIssue(element.id(), IssueSeverity::Error, "Array element equations are incomplete",
                    "incomplete-array-elements", {});
            }
        }
    }
    for (const ModelElement &element : document.model().elements()) {
        if (!result.structurallyUsed().contains(element.id()))
            result.addIssue(element.id(), IssueSeverity::Warning, "Variable is not connected or referenced", "unused-variable", {});
    }
    reportCycles(document, result);
    return result.finish();
}

MacroReader makeMacroReader(const Document &document)
{
    return [&document](const QString &name, int argumentCount, int outputCount) -> std::optional<MacroEvaluation> {
        const MacroDefinition *macro = document.findMacro(name, argumentCount, outputCount);
        if (!macro)
            return std::nullopt;
        return MacroEvaluation {macro};
    };
}

} // namespace qds::core
