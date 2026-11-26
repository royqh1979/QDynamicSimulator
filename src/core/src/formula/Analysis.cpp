#include <core/formula/Analysis.h>

#include <core/formula/Function.h>
#include <core/Identifier.h>

#include <algorithm>
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace qds::core {
namespace {

void addIssue(ModelAnalysis &result, EntityId id, IssueSeverity severity, QString message, QString code, SourceSpan span)
{
    result.issues.push_back({id, severity, std::move(message), std::move(code), span});
}

void addReference(ModelAnalysis &result, ElementAnalysis &owner, EntityId dependency)
{
    owner.references.insert(dependency);
    result.structurallyUsed.insert(dependency);
    auto &edges = result.dependencies.edges[dependency];
    if (std::find(edges.begin(), edges.end(), owner.id) == edges.end())
        edges.push_back(owner.id);
}

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

void resolveNode(const Document &document, ModelAnalysis &result, ResolutionTarget &target, const FormulaNode *node)
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
            addIssue(result, target.ownerId, IssueSeverity::Error, "Undefined variable: " + reference->nameKey(),
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
                addIssue(result, target.ownerId, IssueSeverity::Error,
                    "Invalid argument count for function: " + call->nameKey(), "invalid-arity", call->span());
            }
        } else if (const MacroDefinition *macro = document.findMacro(call->nameKey(), call->argumentCount(), 1)) {
            target.macroCalls.insert({macro->signature(), static_cast<int>(call->argumentCount())});
        } else if (const ModelElement *model = document.model().findByName(call->nameKey())) {
            target.references.insert(model->id());
        } else {
            addIssue(result, target.ownerId, IssueSeverity::Error, "Unknown function: " + call->nameKey(),
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

ResolutionTarget resolveFormula(const Document &document, ModelAnalysis &result, EntityId ownerId,
    const FormulaAnalysis &formula, const QSet<QString> &locals = {})
{
    for (const FormulaIssue &issue : formula.issues())
        addIssue(result, ownerId, IssueSeverity::Error, issue.message, "parse", issue.span);
    ResolutionTarget target;
    target.ownerId = ownerId;
    target.locals = locals;
    resolveNode(document, result, target, formula.root());
    return target;
}

void commitTarget(ModelAnalysis &result, ElementAnalysis &owner, const ResolutionTarget &target)
{
    for (EntityId reference : target.references)
        addReference(result, owner, reference);
    for (const MacroCall &call : target.macroCalls)
        owner.macroCalls.insert(call);
}

void collectMacroDependencies(const ModelAnalysis &analysis, const MacroCall &call,
    QSet<MacroCall> &visited, QSet<EntityId> &references)
{
    if (visited.contains(call))
        return;
    visited.insert(call);
    const auto found = analysis.macros.find(call.signature);
    if (found == analysis.macros.end())
        return;
    const MacroAnalysis &macro = found->second;
    references.unite(macro.references);
    for (const MacroCall &nested : macro.calls)
        collectMacroDependencies(analysis, nested, visited, references);
    for (int index = call.argumentCount; index < macro.definition->parameters.size(); ++index) {
        references.unite(macro.defaultReferences[index]);
        for (const MacroCall &nested : macro.defaultCalls[index])
            collectMacroDependencies(analysis, nested, visited, references);
    }
}

void resolveMacroDependencies(ModelAnalysis &result)
{
    for (ElementAnalysis &owner : result.elements) {
        for (const MacroCall &call : owner.macroCalls) {
            QSet<MacroCall> visited;
            QSet<EntityId> references;
            collectMacroDependencies(result, call, visited, references);
            for (EntityId reference : references)
                addReference(result, owner, reference);
        }
    }
}

void reportCycles(const Document &document, ModelAnalysis &result)
{
    std::unordered_map<EntityId, int> state;
    std::unordered_set<EntityId> reported;
    std::function<void(EntityId)> visit = [&](EntityId id) {
        const auto *model = dynamic_cast<const ModelElement *>(document.find(id));
        if (model && !model->dimensions().isEmpty()) {
            state[id] = 2;
            return;
        }
        state[id] = 1;
        const auto found = result.dependencies.edges.find(id);
        if (found != result.dependencies.edges.end()) {
            for (EntityId next : found->second) {
                if (state[next] == 1) {
                    if (reported.insert(next).second)
                        addIssue(result, next, IssueSeverity::Error, "Algebraic dependency cycle", "algebraic-cycle", {});
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

ModelAnalysis analyzeDocument(const Document &document)
{
    ModelAnalysis result;
    for (const MacroDefinition &definition : document.macros()) {
        MacroAnalysis macro;
        macro.definition = &definition;
        macro.formula = analyzeFormula(definition.equation);
        QSet<QString> locals;
        for (const MacroParameter &parameter : definition.parameters)
            locals.insert(parameter.nameKey);
        const ResolutionTarget body = resolveFormula(document, result, 0, macro.formula, locals);
        macro.references = body.references;
        macro.calls = body.macroCalls;
        for (int index = 0; index < definition.parameters.size(); ++index) {
            macro.defaults.push_back(analyzeFormula(definition.parameters[index].defaultEquation));
            QSet<QString> defaultLocals;
            for (int previous = 0; previous < index; ++previous)
                defaultLocals.insert(definition.parameters[previous].nameKey);
            const ResolutionTarget value = resolveFormula(document, result, 0, macro.defaults.back(), defaultLocals);
            macro.defaultReferences.push_back(value.references);
            macro.defaultCalls.push_back(value.macroCalls);
        }
        result.macros.emplace(definition.signature(), std::move(macro));
    }
    for (const ViewElement &element : document.diagram().elements()) {
        if (element.kind() == ElementKind::Connector) {
            const Connector &connector = static_cast<const Connector &>(element);
            result.structurallyUsed.insert(connector.from());
            result.structurallyUsed.insert(connector.to());
        }
    }
    for (const ModelElement &element : document.model().elements()) {
        if (element.kind() == ElementKind::Flow) {
            const Flow &flow = static_cast<const Flow &>(element);
            result.structurallyUsed.insert(flow.endpoints().from);
            result.structurallyUsed.insert(flow.endpoints().to);
            result.structurallyUsed.insert(flow.id());
        }
        ElementAnalysis owner;
        owner.id = element.id();
        const auto analyzeOwned = [&](const FormulaAnalysis &formula) {
            commitTarget(result, owner, resolveFormula(document, result, element.id(), formula));
        };
        owner.formula = analyzeFormula(element.equation());
        analyzeOwned(owner.formula);
        for (const ArrayElementEquation &entry : element.elementEquations()) {
            ArrayElementAnalysis item;
            item.indices = entry.indices;
            item.formula = analyzeFormula(entry.equation);
            analyzeOwned(item.formula);
            owner.elementFormulas.push_back(std::move(item));
        }
        if (!element.elementEquations().isEmpty()) {
            std::size_t expected = 1;
            for (const QString &name : element.dimensions())
                expected *= document.findDimension(name)->elements.size();
            if (element.elementEquations().size() != expected)
                addIssue(result, element.id(), IssueSeverity::Error, "Array element equations are incomplete",
                    "incomplete-array-elements", {});
        }
        result.elements.push_back(std::move(owner));
    }
    resolveMacroDependencies(result);
    for (const ModelElement &element : document.model().elements()) {
        if (!result.structurallyUsed.count(element.id()))
            addIssue(result, element.id(), IssueSeverity::Warning, "Variable is not connected or referenced", "unused-variable", {});
    }
    reportCycles(document, result);
    return result;
}

MacroReader makeMacroReader(const ModelAnalysis &analysis)
{
    return [&analysis](const QString &name, int argumentCount, int outputCount) -> std::optional<MacroEvaluation> {
        const QString key = normalizeIdentifier(name);
        auto found = analysis.macros.lower_bound({key, 0, outputCount});
        while (found != analysis.macros.end() && found->first.nameKey == key) {
            const MacroAnalysis &macro = found->second;
            if (macro.definition && macro.definition->signature().outputCount == outputCount
                && argumentCount >= macro.definition->requiredArguments()
                && argumentCount <= macro.definition->parameters.size()) {
                return MacroEvaluation {macro.definition, macro.formula.root(), &macro.defaults};
            }
            ++found;
        }
        return std::nullopt;
    };
}

} // namespace qds::core
