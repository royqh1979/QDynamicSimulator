#include <core/formula/Analysis.h>

#include <core/formula/Function.h>
#include <core/Identifier.h>

#include <QHash>
#include <QVector>

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

using Substitutions = QHash<QString, const FormulaNode *>;

std::unique_ptr<FormulaNode> expandFormula(const Document &document, ModelAnalysis &result, ElementAnalysis &owner,
    const FormulaNode *node, const Substitutions &substitutions, QVector<QString> &macroStack)
{
    if (!node)
        return nullptr;
    if (const auto *reference = dynamic_cast<const ReferenceNode *>(node)) {
        const auto found = substitutions.find(reference->nameKey());
        if (found != substitutions.end())
            return expandFormula(document, result, owner, found.value(), {}, macroStack);
        return std::make_unique<ReferenceNode>(reference->nameKey(), reference->span());
    }
    if (const auto *number = dynamic_cast<const NumberNode *>(node))
        return std::make_unique<NumberNode>(number->value(), number->span());
    if (const auto *unary = dynamic_cast<const UnaryNode *>(node)) {
        return std::make_unique<UnaryNode>(unary->op(),
            expandFormula(document, result, owner, unary->operand(), substitutions, macroStack), unary->span());
    }
    if (const auto *binary = dynamic_cast<const BinaryNode *>(node)) {
        return std::make_unique<BinaryNode>(binary->op(),
            expandFormula(document, result, owner, binary->left(), substitutions, macroStack),
            expandFormula(document, result, owner, binary->right(), substitutions, macroStack), binary->span());
    }
    if (const auto *conditional = dynamic_cast<const ConditionalNode *>(node)) {
        return std::make_unique<ConditionalNode>(
            expandFormula(document, result, owner, conditional->condition(), substitutions, macroStack),
            expandFormula(document, result, owner, conditional->thenExpression(), substitutions, macroStack),
            expandFormula(document, result, owner, conditional->elseExpression(), substitutions, macroStack),
            conditional->span());
    }
    if (const auto *subscript = dynamic_cast<const SubscriptNode *>(node)) {
        std::vector<std::unique_ptr<FormulaNode>> indices;
        for (std::size_t position = 0; position < subscript->indexCount(); ++position)
            indices.push_back(expandFormula(document, result, owner, subscript->index(position), substitutions, macroStack));
        return std::make_unique<SubscriptNode>(
            expandFormula(document, result, owner, subscript->target(), substitutions, macroStack),
            std::move(indices), subscript->span());
    }
    if (const auto *wildcard = dynamic_cast<const WildcardNode *>(node))
        return std::make_unique<WildcardNode>(wildcard->span());
    if (const auto *range = dynamic_cast<const RangeNode *>(node)) {
        return std::make_unique<RangeNode>(
            expandFormula(document, result, owner, range->begin(), substitutions, macroStack),
            expandFormula(document, result, owner, range->end(), substitutions, macroStack), range->span());
    }
    if (const auto *position = dynamic_cast<const DimensionPositionNode *>(node))
        return std::make_unique<DimensionPositionNode>(position->position(), position->span());
    if (const auto *literal = dynamic_cast<const ArrayLiteralNode *>(node)) {
        std::vector<std::unique_ptr<FormulaNode>> values;
        for (std::size_t position = 0; position < literal->valueCount(); ++position)
            values.push_back(expandFormula(document, result, owner, literal->value(position), substitutions, macroStack));
        return std::make_unique<ArrayLiteralNode>(std::move(values), literal->span());
    }
    const auto *call = dynamic_cast<const CallNode *>(node);
    if (!call)
        return nullptr;
    std::vector<std::unique_ptr<FormulaNode>> arguments;
    for (std::size_t position = 0; position < call->argumentCount(); ++position)
        arguments.push_back(expandFormula(document, result, owner, call->argument(position), substitutions, macroStack));
    if (findFunctionSignature(call->nameKey()))
        return std::make_unique<CallNode>(call->nameKey(), std::move(arguments), call->span());
    const MacroDefinition *macro = document.findMacro(call->nameKey(), arguments.size());
    if (!macro)
        return std::make_unique<CallNode>(call->nameKey(), std::move(arguments), call->span());
    if (std::find(macroStack.begin(), macroStack.end(), macro->nameKey) != macroStack.end()) {
        addIssue(result, owner.id, IssueSeverity::Error, "Recursive expression macro: " + macro->nameKey,
            "recursive-macro", call->span());
        return std::make_unique<CallNode>(call->nameKey(), std::move(arguments), call->span());
    }
    const auto analysis = std::find_if(result.macros.begin(), result.macros.end(), [macro](const MacroAnalysis &entry) {
        return entry.definition == macro;
    });
    if (analysis == result.macros.end() || !analysis->formula.root()) {
        addIssue(result, owner.id, IssueSeverity::Error, "Invalid expression macro: " + macro->nameKey,
            "invalid-macro", call->span());
        return std::make_unique<CallNode>(call->nameKey(), std::move(arguments), call->span());
    }
    Substitutions macroSubstitutions;
    std::vector<std::unique_ptr<FormulaNode>> expandedDefaults;
    macroStack.push_back(macro->nameKey);
    for (std::size_t index = 0; index < macro->parameters.size(); ++index) {
        const FormulaNode *value = index < arguments.size() ? arguments[index].get() : nullptr;
        if (index >= arguments.size()) {
            expandedDefaults.push_back(expandFormula(document, result, owner,
                analysis->defaults[index].root(), macroSubstitutions, macroStack));
            value = expandedDefaults.back().get();
        }
        if (!value) {
            macroStack.pop_back();
            addIssue(result, owner.id, IssueSeverity::Error, "Invalid expression macro: " + macro->nameKey,
                "invalid-macro", call->span());
            return std::make_unique<CallNode>(call->nameKey(), std::move(arguments), call->span());
        }
        macroSubstitutions[macro->parameters[index].nameKey] = value;
    }
    auto expanded = expandFormula(document, result, owner, analysis->formula.root(), macroSubstitutions, macroStack);
    macroStack.pop_back();
    return expanded;
}

bool isDimensionElement(const Document &document, const QString &nameKey)
{
    const int separator = nameKey.indexOf('.');
    if (separator < 0)
        return false;
    const Dimension *dimension = document.findDimension(nameKey.left(separator));
    return dimension && dimension->findElement(nameKey.mid(separator + 1));
}

void resolveNode(const Document &document, ModelAnalysis &result, ElementAnalysis &owner, const FormulaNode *node)
{
    if (!node)
        return;
    if (const auto *reference = dynamic_cast<const ReferenceNode *>(node)) {
        if (const FunctionSignature *signature = findFunctionSignature(reference->nameKey());
            signature && signature->minArguments == 0 && signature->maxArguments == 0)
            return;
        if (document.findDimension(reference->nameKey()) || isDimensionElement(document, reference->nameKey()))
            return;
        if (const ModelElement *model = document.model().findByName(reference->nameKey())) {
            addReference(result, owner, model->id());
        } else {
            addIssue(result, owner.id, IssueSeverity::Error, "Undefined variable: " + reference->nameKey(),
                "undefined-variable", reference->span());
        }
        return;
    }
    if (const auto *unary = dynamic_cast<const UnaryNode *>(node)) {
        resolveNode(document, result, owner, unary->operand());
        return;
    }
    if (const auto *binary = dynamic_cast<const BinaryNode *>(node)) {
        resolveNode(document, result, owner, binary->left());
        resolveNode(document, result, owner, binary->right());
        return;
    }
    if (const auto *call = dynamic_cast<const CallNode *>(node)) {
        const FunctionSignature *signature = findFunctionSignature(call->nameKey());
        if (signature) {
            if (call->argumentCount() < signature->minArguments || call->argumentCount() > signature->maxArguments) {
                addIssue(result, owner.id, IssueSeverity::Error,
                    "Invalid argument count for function: " + call->nameKey(), "invalid-arity", call->span());
            }
        } else if (document.findMacro(call->nameKey(), call->argumentCount())) {
        } else if (const ModelElement *model = document.model().findByName(call->nameKey())) {
            addReference(result, owner, model->id());
        } else {
            addIssue(result, owner.id, IssueSeverity::Error, "Unknown function: " + call->nameKey(),
                "unknown-function", call->span());
        }
        for (std::size_t position = 0; position < call->argumentCount(); ++position)
            resolveNode(document, result, owner, call->argument(position));
        return;
    }
    if (const auto *conditional = dynamic_cast<const ConditionalNode *>(node)) {
        resolveNode(document, result, owner, conditional->condition());
        resolveNode(document, result, owner, conditional->thenExpression());
        resolveNode(document, result, owner, conditional->elseExpression());
        return;
    }
    if (const auto *subscript = dynamic_cast<const SubscriptNode *>(node)) {
        resolveNode(document, result, owner, subscript->target());
        const auto *reference = dynamic_cast<const ReferenceNode *>(subscript->target());
        const ModelElement *target = reference ? document.model().findByName(reference->nameKey()) : nullptr;
        for (std::size_t position = 0; position < subscript->indexCount(); ++position) {
            const FormulaNode *index = subscript->index(position);
            const auto *name = dynamic_cast<const ReferenceNode *>(index);
            if (name && target && position < target->dimensions().size()) {
                const Dimension *dimension = document.findDimension(target->dimensions()[position]);
                if (dimension && (name->nameKey() == dimension->nameKey || dimension->findElement(name->nameKey())))
                    continue;
            }
            resolveNode(document, result, owner, index);
        }
        return;
    }
    if (const auto *range = dynamic_cast<const RangeNode *>(node)) {
        resolveNode(document, result, owner, range->begin());
        resolveNode(document, result, owner, range->end());
        return;
    }
    if (const auto *literal = dynamic_cast<const ArrayLiteralNode *>(node)) {
        for (std::size_t position = 0; position < literal->valueCount(); ++position)
            resolveNode(document, result, owner, literal->value(position));
    }
}

void reportCycles(const Document &document, ModelAnalysis &result)
{
    std::unordered_map<EntityId, int> state;
    std::unordered_set<EntityId> reported;
    std::function<void(EntityId)> visit = [&](EntityId id) {
        const auto *model = dynamic_cast<const ModelElement *>(document.find(id));
        if (model && !model->dimensions().empty()) {
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
        for (const FormulaIssue &issue : macro.formula.issues())
            addIssue(result, 0, IssueSeverity::Error, issue.message, "parse", issue.span);
        for (const MacroParameter &parameter : definition.parameters) {
            macro.defaults.push_back(analyzeFormula(parameter.defaultEquation));
            for (const FormulaIssue &issue : macro.defaults.back().issues())
                addIssue(result, 0, IssueSeverity::Error, issue.message, "parse", issue.span);
        }
        result.macros.push_back(std::move(macro));
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
        const auto analyzeOwned = [&](FormulaAnalysis formula) {
            for (const FormulaIssue &issue : formula.issues())
                addIssue(result, element.id(), IssueSeverity::Error, issue.message, "parse", issue.span);
            QVector<QString> macroStack;
            auto ast = expandFormula(document, result, owner, formula.root(), {}, macroStack);
            resolveNode(document, result, owner, ast.get());
            return FormulaAnalysis(std::move(ast), formula.issues());
        };
        owner.formula = analyzeOwned(analyzeFormula(element.equation()));
        for (const ArrayElementEquation &entry : element.elementEquations()) {
            ArrayElementAnalysis item;
            item.indices = entry.indices;
            item.formula = analyzeOwned(analyzeFormula(entry.equation));
            owner.elementFormulas.push_back(std::move(item));
        }
        if (!element.elementEquations().empty()) {
            std::size_t expected = 1;
            for (const QString &name : element.dimensions())
                expected *= document.findDimension(name)->elements.size();
            if (element.elementEquations().size() != expected)
                addIssue(result, element.id(), IssueSeverity::Error, "Array element equations are incomplete",
                    "incomplete-array-elements", {});
        }
        result.elements.push_back(std::move(owner));
    }
    for (const ModelElement &element : document.model().elements()) {
        if (!result.structurallyUsed.count(element.id()))
            addIssue(result, element.id(), IssueSeverity::Warning, "Variable is not connected or referenced", "unused-variable", {});
    }
    reportCycles(document, result);
    return result;
}

} // namespace qds::core
