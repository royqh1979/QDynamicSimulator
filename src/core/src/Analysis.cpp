#include <qds/core/Analysis.h>

#include <qds/core/Function.h>

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

void resolveNode(const Document &document, ModelAnalysis &result, ElementAnalysis &owner, FormulaNode *node)
{
    if (!node)
        return;
    if (auto *reference = dynamic_cast<ReferenceNode *>(node)) {
        if (const FunctionSignature *signature = findFunctionSignature(reference->nameKey);
            signature && signature->minArguments == 0 && signature->maxArguments == 0)
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
        for (const auto &index : subscript->indices)
            resolveNode(document, result, owner, index.get());
    }
}

void reportCycles(const Document &document, ModelAnalysis &result)
{
    std::unordered_map<EntityId, int> state;
    std::unordered_set<EntityId> reported;
    std::function<void(EntityId)> visit = [&](EntityId id) {
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
    for (const ViewElement &element : document.diagram().elements()) {
        if (element.kind() == ElementKind::Connector) {
            const Connector &connector = static_cast<const Connector &>(element);
            result.structurallyUsed.insert(connector.from);
            result.structurallyUsed.insert(connector.to);
        }
    }
    for (const ModelElement &element : document.model().elements()) {
        if (element.kind() == ElementKind::Flow) {
            const Flow &flow = static_cast<const Flow &>(element);
            result.structurallyUsed.insert(flow.endpoints.from);
            result.structurallyUsed.insert(flow.endpoints.to);
            result.structurallyUsed.insert(flow.id());
        }
        ElementAnalysis owner;
        owner.id = element.id();
        owner.formula = analyzeFormula(element.equation);
        for (const FormulaIssue &issue : owner.formula.issues)
            addIssue(result, element.id(), IssueSeverity::Error, issue.message, "parse", issue.span);
        resolveNode(document, result, owner, owner.formula.ast.get());
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
