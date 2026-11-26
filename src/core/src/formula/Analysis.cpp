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

void addIssue(ModelAnalysis &result, EntityId id, IssueSeverity severity, std::string message, std::string code, SourceSpan span)
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

using Substitutions = std::unordered_map<std::string, const FormulaNode *>;

std::unique_ptr<FormulaNode> expandFormula(const Document &document, ModelAnalysis &result, ElementAnalysis &owner,
    const FormulaNode *node, const Substitutions &substitutions, std::vector<std::string> &macroStack)
{
    if (!node)
        return nullptr;
    if (const auto *reference = dynamic_cast<const ReferenceNode *>(node)) {
        const auto found = substitutions.find(reference->nameKey);
        if (found != substitutions.end())
            return expandFormula(document, result, owner, found->second, {}, macroStack);
        return std::make_unique<ReferenceNode>(reference->nameKey, reference->span);
    }
    if (const auto *number = dynamic_cast<const NumberNode *>(node))
        return std::make_unique<NumberNode>(number->value, number->span);
    if (const auto *unary = dynamic_cast<const UnaryNode *>(node)) {
        return std::make_unique<UnaryNode>(unary->op,
            expandFormula(document, result, owner, unary->operand.get(), substitutions, macroStack), unary->span);
    }
    if (const auto *binary = dynamic_cast<const BinaryNode *>(node)) {
        return std::make_unique<BinaryNode>(binary->op,
            expandFormula(document, result, owner, binary->left.get(), substitutions, macroStack),
            expandFormula(document, result, owner, binary->right.get(), substitutions, macroStack), binary->span);
    }
    if (const auto *conditional = dynamic_cast<const ConditionalNode *>(node)) {
        return std::make_unique<ConditionalNode>(
            expandFormula(document, result, owner, conditional->condition.get(), substitutions, macroStack),
            expandFormula(document, result, owner, conditional->thenExpression.get(), substitutions, macroStack),
            expandFormula(document, result, owner, conditional->elseExpression.get(), substitutions, macroStack),
            conditional->span);
    }
    if (const auto *subscript = dynamic_cast<const SubscriptNode *>(node)) {
        std::vector<std::unique_ptr<FormulaNode>> indices;
        for (const auto &index : subscript->indices)
            indices.push_back(expandFormula(document, result, owner, index.get(), substitutions, macroStack));
        return std::make_unique<SubscriptNode>(
            expandFormula(document, result, owner, subscript->target.get(), substitutions, macroStack),
            std::move(indices), subscript->span);
    }
    if (const auto *wildcard = dynamic_cast<const WildcardNode *>(node))
        return std::make_unique<WildcardNode>(wildcard->span);
    if (const auto *range = dynamic_cast<const RangeNode *>(node)) {
        return std::make_unique<RangeNode>(
            expandFormula(document, result, owner, range->begin.get(), substitutions, macroStack),
            expandFormula(document, result, owner, range->end.get(), substitutions, macroStack), range->span);
    }
    if (const auto *position = dynamic_cast<const DimensionPositionNode *>(node))
        return std::make_unique<DimensionPositionNode>(position->position, position->span);
    if (const auto *literal = dynamic_cast<const ArrayLiteralNode *>(node)) {
        std::vector<std::unique_ptr<FormulaNode>> values;
        for (const auto &value : literal->values)
            values.push_back(expandFormula(document, result, owner, value.get(), substitutions, macroStack));
        return std::make_unique<ArrayLiteralNode>(std::move(values), literal->span);
    }
    const auto *call = dynamic_cast<const CallNode *>(node);
    if (!call)
        return nullptr;
    std::vector<std::unique_ptr<FormulaNode>> arguments;
    for (const auto &argument : call->arguments)
        arguments.push_back(expandFormula(document, result, owner, argument.get(), substitutions, macroStack));
    if (findFunctionSignature(call->nameKey))
        return std::make_unique<CallNode>(call->nameKey, std::move(arguments), call->span);
    const MacroDefinition *macro = document.findMacro(call->nameKey, arguments.size());
    if (!macro)
        return std::make_unique<CallNode>(call->nameKey, std::move(arguments), call->span);
    if (std::find(macroStack.begin(), macroStack.end(), macro->nameKey) != macroStack.end()) {
        addIssue(result, owner.id, IssueSeverity::Error, "Recursive expression macro: " + macro->nameKey,
            "recursive-macro", call->span);
        return std::make_unique<CallNode>(call->nameKey, std::move(arguments), call->span);
    }
    const auto analysis = std::find_if(result.macros.begin(), result.macros.end(), [macro](const MacroAnalysis &entry) {
        return entry.definition == macro;
    });
    if (analysis == result.macros.end() || !analysis->formula.ast) {
        addIssue(result, owner.id, IssueSeverity::Error, "Invalid expression macro: " + macro->nameKey,
            "invalid-macro", call->span);
        return std::make_unique<CallNode>(call->nameKey, std::move(arguments), call->span);
    }
    Substitutions macroSubstitutions;
    std::vector<std::unique_ptr<FormulaNode>> expandedDefaults;
    macroStack.push_back(macro->nameKey);
    for (std::size_t index = 0; index < macro->parameters.size(); ++index) {
        const FormulaNode *value = index < arguments.size() ? arguments[index].get() : nullptr;
        if (index >= arguments.size()) {
            expandedDefaults.push_back(expandFormula(document, result, owner,
                analysis->defaults[index].ast.get(), macroSubstitutions, macroStack));
            value = expandedDefaults.back().get();
        }
        if (!value) {
            macroStack.pop_back();
            addIssue(result, owner.id, IssueSeverity::Error, "Invalid expression macro: " + macro->nameKey,
                "invalid-macro", call->span);
            return std::make_unique<CallNode>(call->nameKey, std::move(arguments), call->span);
        }
        macroSubstitutions[macro->parameters[index].nameKey] = value;
    }
    auto expanded = expandFormula(document, result, owner, analysis->formula.ast.get(), macroSubstitutions, macroStack);
    macroStack.pop_back();
    return expanded;
}

bool isDimensionElement(const Document &document, const std::string &nameKey)
{
    const std::size_t separator = nameKey.find('.');
    if (separator == std::string::npos)
        return false;
    const Dimension *dimension = document.findDimension(nameKey.substr(0, separator));
    return dimension && dimension->findElement(nameKey.substr(separator + 1));
}

void resolveNode(const Document &document, ModelAnalysis &result, ElementAnalysis &owner, FormulaNode *node)
{
    if (!node)
        return;
    if (auto *reference = dynamic_cast<ReferenceNode *>(node)) {
        if (const FunctionSignature *signature = findFunctionSignature(reference->nameKey);
            signature && signature->minArguments == 0 && signature->maxArguments == 0)
            return;
        if (document.findDimension(reference->nameKey) || isDimensionElement(document, reference->nameKey))
            return;
        if (const ModelElement *model = document.model().findByName(reference->nameKey)) {
            reference->entityId = model->id();
            addReference(result, owner, model->id());
        } else {
            addIssue(result, owner.id, IssueSeverity::Error, "Undefined variable: " + reference->nameKey,
                "undefined-variable", reference->span);
        }
        return;
    }
    if (auto *unary = dynamic_cast<UnaryNode *>(node)) {
        resolveNode(document, result, owner, unary->operand.get());
        return;
    }
    if (auto *binary = dynamic_cast<BinaryNode *>(node)) {
        resolveNode(document, result, owner, binary->left.get());
        resolveNode(document, result, owner, binary->right.get());
        return;
    }
    if (auto *call = dynamic_cast<CallNode *>(node)) {
        const FunctionSignature *signature = findFunctionSignature(call->nameKey);
        if (signature) {
            if (call->arguments.size() < signature->minArguments || call->arguments.size() > signature->maxArguments) {
                addIssue(result, owner.id, IssueSeverity::Error,
                    "Invalid argument count for function: " + call->nameKey, "invalid-arity", call->span);
            }
        } else if (document.findMacro(call->nameKey, call->arguments.size())) {
        } else if (const ModelElement *model = document.model().findByName(call->nameKey)) {
            addReference(result, owner, model->id());
        } else {
            addIssue(result, owner.id, IssueSeverity::Error, "Unknown function: " + call->nameKey,
                "unknown-function", call->span);
        }
        for (const auto &argument : call->arguments)
            resolveNode(document, result, owner, argument.get());
        return;
    }
    if (auto *conditional = dynamic_cast<ConditionalNode *>(node)) {
        resolveNode(document, result, owner, conditional->condition.get());
        resolveNode(document, result, owner, conditional->thenExpression.get());
        resolveNode(document, result, owner, conditional->elseExpression.get());
        return;
    }
    if (auto *subscript = dynamic_cast<SubscriptNode *>(node)) {
        resolveNode(document, result, owner, subscript->target.get());
        const auto *reference = dynamic_cast<const ReferenceNode *>(subscript->target.get());
        const ModelElement *target = reference ? document.model().findByName(reference->nameKey) : nullptr;
        for (std::size_t position = 0; position < subscript->indices.size(); ++position) {
            const auto &index = subscript->indices[position];
            const auto *name = dynamic_cast<const ReferenceNode *>(index.get());
            if (name && target && position < target->dimensions().size()) {
                const Dimension *dimension = document.findDimension(target->dimensions()[position]);
                if (dimension && (name->nameKey == dimension->nameKey || dimension->findElement(name->nameKey)))
                    continue;
            }
            resolveNode(document, result, owner, index.get());
        }
        return;
    }
    if (auto *range = dynamic_cast<RangeNode *>(node)) {
        resolveNode(document, result, owner, range->begin.get());
        resolveNode(document, result, owner, range->end.get());
        return;
    }
    if (auto *literal = dynamic_cast<ArrayLiteralNode *>(node)) {
        for (const auto &value : literal->values)
            resolveNode(document, result, owner, value.get());
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
        for (const FormulaIssue &issue : macro.formula.issues)
            addIssue(result, 0, IssueSeverity::Error, issue.message, "parse", issue.span);
        for (const MacroParameter &parameter : definition.parameters) {
            macro.defaults.push_back(analyzeFormula(parameter.defaultEquation));
            for (const FormulaIssue &issue : macro.defaults.back().issues)
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
        const auto analyzeOwned = [&](FormulaAnalysis &formula) {
            for (const FormulaIssue &issue : formula.issues)
                addIssue(result, element.id(), IssueSeverity::Error, issue.message, "parse", issue.span);
            std::vector<std::string> macroStack;
            formula.ast = expandFormula(document, result, owner, formula.ast.get(), {}, macroStack);
            resolveNode(document, result, owner, formula.ast.get());
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
        if (!element.elementEquations().empty()) {
            std::size_t expected = 1;
            for (const std::string &name : element.dimensions())
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
