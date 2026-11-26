#include <core/Document.h>

#include <core/Identifier.h>

#include <QSet>

#include <algorithm>
#include <unordered_set>

namespace qds::core {
namespace {

QString defaultName(ElementKind kind)
{
    switch (kind) {
    case ElementKind::Stock:
        return "Stock";
    case ElementKind::Flow:
        return "Flow";
    case ElementKind::Auxiliary:
        return "Auxiliary";
    case ElementKind::Constant:
        return "Constant";
    case ElementKind::Data:
        return "Data";
    default:
        return {};
    }
}

bool identifierStart(QChar c)
{
    return (c >= QChar('A') && c <= QChar('Z')) || (c >= QChar('a') && c <= QChar('z'))
        || c == QChar('_') || c.unicode() >= 128;
}

bool identifierPart(QChar c)
{
    return identifierStart(c) || (c >= QChar('0') && c <= QChar('9')) || c == QChar('$');
}

QString quotedIdentifier(const QString &name)
{
    bool plain = !name.isEmpty() && identifierStart(name.front());
    for (QChar c : name)
        plain = plain && identifierPart(c);
    if (plain)
        return name;
    QString quoted = "\"";
    for (QChar c : name) {
        if (c == QChar('\\') || c == QChar('"'))
            quoted.push_back(QChar('\\'));
        if (c == QChar('\n'))
            quoted += "\\n";
        else
            quoted.push_back(c);
    }
    quoted.push_back(QChar('"'));
    return quoted;
}

QString unescapeIdentifier(const QString &name)
{
    QString result;
    for (int index = 0; index < name.size(); ++index) {
        if (name[index] == QChar('\\') && index + 1 < name.size()) {
            const QChar escaped = name[++index];
            result.push_back(escaped == QChar('n') ? QChar('\n') : escaped);
        } else {
            result.push_back(name[index]);
        }
    }
    return result;
}

QString replaceReference(const QString &equation, const QString &oldName, const QString &newName)
{
    QString result;
    for (int index = 0; index < equation.size();) {
        if (equation[index] == QChar('"')) {
            const int start = index++;
            QString value;
            bool closed = false;
            while (index < equation.size()) {
                if (equation[index] == QChar('\\') && index + 1 < equation.size()) {
                    value.push_back(equation[index++]);
                    value.push_back(equation[index++]);
                } else if (equation[index] == QChar('"')) {
                    ++index;
                    closed = true;
                    break;
                } else {
                    value.push_back(equation[index++]);
                }
            }
            if (closed && normalizeIdentifier(unescapeIdentifier(value)) == normalizeIdentifier(oldName))
                result += quotedIdentifier(newName);
            else
                result += equation.mid(start, index - start);
            continue;
        }
        if (!identifierStart(equation[index])) {
            result.push_back(equation[index++]);
            continue;
        }
        const int start = index++;
        while (index < equation.size() && identifierPart(equation[index]))
            ++index;
        const QString token = equation.mid(start, index - start);
        result += normalizeIdentifier(token) == normalizeIdentifier(oldName) ? quotedIdentifier(newName) : token;
    }
    return result;
}

int removalRank(ElementKind kind)
{
    if (kind == ElementKind::Connector)
        return 0;
    if (kind == ElementKind::Flow)
        return 1;
    return 2;
}

} // namespace

Document::Document()
    : m_model(*this)
    , m_diagram(*this)
{
}

Document::Document(Document &&other) noexcept
    : m_elements(std::move(other.m_elements))
    , m_nameIndex(std::move(other.m_nameIndex))
    , m_dimensions(std::move(other.m_dimensions))
    , m_dimensionIndex(std::move(other.m_dimensionIndex))
    , m_macros(std::move(other.m_macros))
    , m_nextNameSuffix(std::move(other.m_nextNameSuffix))
    , m_nextId(other.m_nextId)
    , m_model(*this)
    , m_diagram(*this)
{
    m_diagram.m_positions = std::move(other.m_diagram.m_positions);
    m_diagram.m_routes = std::move(other.m_diagram.m_routes);
    m_diagram.m_fontStyles = std::move(other.m_diagram.m_fontStyles);
}

Document &Document::operator=(Document &&other) noexcept
{
    if (this == &other)
        return *this;
    m_elements = std::move(other.m_elements);
    m_nameIndex = std::move(other.m_nameIndex);
    m_dimensions = std::move(other.m_dimensions);
    m_dimensionIndex = std::move(other.m_dimensionIndex);
    m_macros = std::move(other.m_macros);
    m_nextNameSuffix = std::move(other.m_nextNameSuffix);
    m_nextId = other.m_nextId;
    m_diagram.m_positions = std::move(other.m_diagram.m_positions);
    m_diagram.m_routes = std::move(other.m_diagram.m_routes);
    m_diagram.m_fontStyles = std::move(other.m_diagram.m_fontStyles);
    return *this;
}

DocumentElement *Document::findMutable(EntityId id)
{
    const auto it = m_elements.find(id);
    return it == m_elements.end() ? nullptr : it->second.get();
}

const DocumentElement *Document::find(EntityId id) const
{
    const auto it = m_elements.find(id);
    return it == m_elements.end() ? nullptr : it->second.get();
}

const ModelElement *Document::findByName(const QString &name) const
{
    const auto it = m_nameIndex.find(normalizeIdentifier(name));
    if (it == m_nameIndex.end())
        return nullptr;
    return dynamic_cast<const ModelElement *>(find(it.value()));
}

const Dimension *Document::findDimension(const QString &name) const
{
    const auto found = m_dimensionIndex.find(normalizeIdentifier(name));
    return found == m_dimensionIndex.end() ? nullptr : &m_dimensions[found.value()];
}

bool Document::defineDimension(const QString &displayName, std::size_t size)
{
    if (size == 0)
        return false;
    Dimension dimension;
    dimension.displayName = trimDisplayName(displayName);
    dimension.nameKey = normalizeIdentifier(dimension.displayName);
    dimension.elements.resize(size);
    for (std::size_t index = 0; index < size; ++index)
        dimension.elements[index].displayName = QString::number(index + 1);
    return defineDimensionData(std::move(dimension));
}

bool Document::defineDimension(const QString &displayName, const QVector<QString> &elements)
{
    Dimension dimension;
    dimension.displayName = trimDisplayName(displayName);
    dimension.nameKey = normalizeIdentifier(dimension.displayName);
    for (const QString &name : elements) {
        DimensionElement element;
        element.displayName = trimDisplayName(name);
        element.nameKey = normalizeIdentifier(element.displayName);
        dimension.elements.push_back(std::move(element));
    }
    return defineDimensionData(std::move(dimension));
}

bool Document::defineDimensionData(Dimension dimension)
{
    if (dimension.nameKey.isEmpty() || dimension.elements.isEmpty() || m_nameIndex.count(dimension.nameKey)
        || m_dimensionIndex.count(dimension.nameKey) || hasMacroName(dimension.nameKey))
        return false;
    const bool numbered = dimension.elements.front().nameKey.isEmpty();
    QSet<QString> elementNames;
    for (const DimensionElement &element : dimension.elements) {
        if (numbered != element.nameKey.isEmpty())
            return false;
        if (!numbered && elementNames.contains(element.nameKey))
            return false;
        if (!numbered)
            elementNames.insert(element.nameKey);
    }
    m_dimensionIndex[dimension.nameKey] = m_dimensions.size();
    m_dimensions.push_back(std::move(dimension));
    return true;
}

bool Document::prepareMacro(MacroDefinitionData &macro) const
{
    macro.displayName = trimDisplayName(macro.displayName);
    const QString nameKey = normalizeIdentifier(macro.displayName);
    if (nameKey.isEmpty() || macro.equation.isEmpty() || m_nameIndex.count(nameKey)
        || m_dimensionIndex.count(nameKey))
        return false;
    QSet<QString> parameters;
    bool hasDefault = false;
    for (MacroParameterData &parameter : macro.parameters) {
        parameter.displayName = trimDisplayName(parameter.displayName);
        const QString parameterKey = normalizeIdentifier(parameter.displayName);
        if (parameterKey.isEmpty() || parameters.contains(parameterKey))
            return false;
        parameters.insert(parameterKey);
        if (parameter.defaultEquation.isEmpty()) {
            if (hasDefault)
                return false;
        } else {
            hasDefault = true;
        }
    }
    return true;
}

std::unique_ptr<MacroDefinition> Document::makeMacro(const MacroDefinitionData &macro)
{
    std::vector<MacroParameter> parameters;
    for (const MacroParameterData &entry : macro.parameters) {
        parameters.emplace_back(entry.displayName, normalizeIdentifier(entry.displayName),
            analyzeFormula(entry.defaultEquation));
    }
    return std::make_unique<MacroDefinition>(macro.displayName, normalizeIdentifier(macro.displayName),
        std::move(parameters), analyzeFormula(macro.equation));
}

bool Document::hasMacroName(const QString &nameKey) const
{
    const QString key = normalizeIdentifier(nameKey);
    const auto found = m_macros.lower_bound({key, 0, 1});
    return found != m_macros.end() && found->first.nameKey == key;
}

bool Document::defineMacro(MacroDefinitionData macro)
{
    if (!prepareMacro(macro))
        return false;
    int newRequired = macro.parameters.size();
    while (newRequired > 0 && !macro.parameters[newRequired - 1].defaultEquation.isEmpty())
        --newRequired;
    const QString nameKey = normalizeIdentifier(macro.displayName);
    auto existing = m_macros.lower_bound({nameKey, 0, 1});
    while (existing != m_macros.end() && existing->first.nameKey == nameKey) {
        const MacroDefinition &candidate = *existing->second;
        if (newRequired <= candidate.parameters().size() && candidate.requiredArguments() <= macro.parameters.size())
            return false;
        ++existing;
    }
    m_macros.emplace(MacroSignature {nameKey, int(macro.parameters.size()), 1},
        makeMacro(macro));
    return true;
}

bool Document::setMacro(MacroDefinitionData macro)
{
    if (!prepareMacro(macro))
        return false;
    const MacroSignature signature {normalizeIdentifier(macro.displayName), int(macro.parameters.size()), 1};
    const auto found = m_macros.find(signature);
    if (found == m_macros.end())
        return false;
    found->second = makeMacro(macro);
    return true;
}

const MacroDefinition *Document::findMacro(const QString &name, int argumentCount, int outputCount) const
{
    const QString key = normalizeIdentifier(name);
    auto found = m_macros.lower_bound({key, 0, outputCount});
    while (found != m_macros.end() && found->first.nameKey == key) {
        const MacroDefinition &macro = *found->second;
        if (macro.signature().outputCount == outputCount && argumentCount >= macro.requiredArguments()
            && argumentCount <= macro.parameters().size())
            return &macro;
        ++found;
    }
    return nullptr;
}

template <typename T>
EntityId Document::insert(std::unique_ptr<T> element, Vec2 position)
{
    const EntityId id = element->id();
    const ElementKind kind = element->kind();
    if (id == 0 || m_elements.count(id))
        return 0;
    if (auto *model = dynamic_cast<ModelElement *>(element.get())) {
        if (model->nameKey().isEmpty() || m_nameIndex.count(model->nameKey()) || m_dimensionIndex.count(model->nameKey())
            || hasMacroName(model->nameKey()))
            return 0;
        m_nameIndex[model->nameKey()] = id;
    }
    m_elements.emplace(id, std::move(element));
    m_diagram.m_positions[id] = position;
    if (kind == ElementKind::Flow || kind == ElementKind::Connector) {
        Route route;
        if (kind == ElementKind::Flow)
            route.mode = RouteMode::Straight;
        m_diagram.m_routes[id] = route;
    }
    return id;
}

EntityId Document::createVariable(ElementKind kind, Vec2 position)
{
    if (kind != ElementKind::Stock && kind != ElementKind::Auxiliary && kind != ElementKind::Constant
        && kind != ElementKind::Data)
        return 0;
    auto variable = std::make_unique<Variable>(m_nextId++, kind);
    const QString name = uniqueName(kind);
    variable->setName(name, normalizeIdentifier(name));
    return insert(std::move(variable), position);
}

EntityId Document::createFlow(EntityId from, EntityId to)
{
    if (!validFlowConnection(0, from, to))
        return 0;
    auto flow = std::make_unique<Flow>(m_nextId++);
    const QString name = uniqueName(ElementKind::Flow);
    flow->setName(name, normalizeIdentifier(name));
    flow->setEndpoints({from, to});
    const Vec2 source = diagram().position(from);
    const Vec2 target = diagram().position(to);
    return insert(std::move(flow), {(source.x + target.x) / 2.0, (source.y + target.y) / 2.0});
}

EntityId Document::createConnector(EntityId from, EntityId to)
{
    if (!validConnectorConnection(0, from, to))
        return 0;
    auto connector = std::make_unique<Connector>(m_nextId++);
    connector->setEndpoints(from, to);
    const EntityId id = insert(std::move(connector), {});
    if (id != 0)
        m_diagram.m_routes[id] = defaultConnectorRoute();
    return id;
}

EntityId Document::createCloud(Vec2 position)
{
    return insert(std::make_unique<Cloud>(m_nextId++), position);
}

EntityId Document::createComment(const QString &text, Vec2 position)
{
    auto comment = std::make_unique<Comment>(m_nextId++);
    comment->setContent(text);
    return insert(std::move(comment), position);
}

QVector<EntityId> Document::cascade(EntityId id) const
{
    if (!find(id))
        return {};
    std::unordered_set<EntityId> included;
    QVector<EntityId> pending {id};
    for (std::size_t index = 0; index < pending.size(); ++index) {
        const EntityId current = pending[index];
        if (!included.insert(current).second)
            continue;
        for (const DocumentElement &element : elements()) {
            if (element.kind() == ElementKind::Flow) {
                const Flow &flow = static_cast<const Flow &>(element);
                if (flow.endpoints().from == current || flow.endpoints().to == current)
                    pending.push_back(element.id());
            } else if (element.kind() == ElementKind::Connector) {
                const Connector &connector = static_cast<const Connector &>(element);
                if (connector.from() == current || connector.to() == current)
                    pending.push_back(element.id());
            }
        }
    }
    QVector<EntityId> removed(included.begin(), included.end());
    std::sort(removed.begin(), removed.end(), [this](EntityId left, EntityId right) {
        const int leftRank = removalRank(find(left)->kind());
        const int rightRank = removalRank(find(right)->kind());
        return leftRank != rightRank ? leftRank < rightRank : left < right;
    });
    return removed;
}

QVector<EntityId> Document::remove(EntityId id)
{
    const QVector<EntityId> removed = cascade(id);
    for (EntityId removedId : removed) {
        if (auto *model = dynamic_cast<ModelElement *>(findMutable(removedId)))
            m_nameIndex.remove(model->nameKey());
        m_elements.erase(removedId);
        m_diagram.m_positions.erase(removedId);
        m_diagram.m_routes.erase(removedId);
        m_diagram.m_fontStyles.erase(removedId);
    }
    return removed;
}

std::optional<ElementData> Document::capture(EntityId id) const
{
    const DocumentElement *element = find(id);
    if (!element)
        return std::nullopt;
    ElementData data;
    data.id = id;
    data.kind = element->kind();
    data.position = diagram().position(id);
    data.route = diagram().route(id);
    data.font = diagram().fontStyle(id);
    if (auto *model = dynamic_cast<const ModelElement *>(element)) {
        data.displayName = model->displayName();
        data.equation = model->equation();
        data.dimensions = model->dimensions();
        data.elementEquations = model->elementEquationData();
        data.unit = model->unit();
        data.comment = model->comment();
    }
    if (auto *comment = dynamic_cast<const Comment *>(element))
        data.content = comment->content();
    if (auto *flow = dynamic_cast<const Flow *>(element))
        data.flowEndpoints = flow->endpoints();
    if (auto *connector = dynamic_cast<const Connector *>(element)) {
        data.from = connector->from();
        data.to = connector->to();
    }
    return data;
}

bool Document::restore(const ElementData &data)
{
    const EntityId id = data.id == 0 ? m_nextId++ : data.id;
    if (find(id))
        return false;
    const QString normalized = normalizeIdentifier(data.displayName);
    if (data.kind <= ElementKind::Data && (normalized.isEmpty() || m_nameIndex.count(normalized)
            || m_dimensionIndex.count(normalized) || hasMacroName(normalized)
            || !validArrayData(data.dimensions, data.elementEquations, data.equation)))
        return false;
    if (!validFontData(data.kind, data.font))
        return false;
    if ((data.kind == ElementKind::Flow || data.kind == ElementKind::Connector)
        && !validRouteData(data.kind, data.route))
        return false;
    std::unique_ptr<DocumentElement> element;
    if (data.kind == ElementKind::Flow) {
        if (!validFlowConnection(id, data.flowEndpoints.from, data.flowEndpoints.to))
            return false;
        auto flow = std::make_unique<Flow>(id);
        flow->setEndpoints(data.flowEndpoints);
        element = std::move(flow);
    } else if (data.kind == ElementKind::Connector) {
        if (!validConnectorConnection(id, data.from, data.to))
            return false;
        auto connector = std::make_unique<Connector>(id);
        connector->setEndpoints(data.from, data.to);
        element = std::move(connector);
    } else if (data.kind == ElementKind::Cloud) {
        element = std::make_unique<Cloud>(id);
    } else if (data.kind == ElementKind::Comment) {
        element = std::make_unique<Comment>(id);
    } else if (data.kind <= ElementKind::Data) {
        element = std::make_unique<Variable>(id, data.kind);
    } else {
        return false;
    }
    if (auto *model = dynamic_cast<ModelElement *>(element.get())) {
        const QString displayName = trimDisplayName(data.displayName);
        model->setName(displayName, normalizeIdentifier(displayName));
        QVector<QString> dimensions = data.dimensions;
        for (QString &name : dimensions)
            name = normalizeIdentifier(name);
        model->setFormulaData(data.equation, std::move(dimensions), data.elementEquations);
        model->setUnit(data.unit);
        model->setComment(data.comment);
    }
    if (auto *comment = dynamic_cast<Comment *>(element.get()))
        comment->setContent(data.content);
    Route route = data.route;
    if (data.kind == ElementKind::Connector && !route.manual && route.mode == RouteMode::Bezier) {
        const bool locked = route.locked;
        route = defaultConnectorRoute();
        route.locked = locked;
    }
    if (insert(std::move(element), data.position) == 0)
        return false;
    setFontStyle(id, data.font);
    if (data.kind == ElementKind::Flow || data.kind == ElementKind::Connector)
        m_diagram.m_routes[id] = route;
    return true;
}

bool Document::apply(const ElementData &data)
{
    DocumentElement *element = findMutable(data.id);
    if (!element)
        return restore(data);
    if (element->kind() != data.kind || !validFontData(data.kind, data.font))
        return false;
    if ((data.kind == ElementKind::Flow || data.kind == ElementKind::Connector)
        && !validRouteData(data.kind, data.route))
        return false;
    if (dynamic_cast<ModelElement *>(element)
        && (!validArrayData(data.dimensions, data.elementEquations, data.equation) || !validRename(data.id, data.displayName)))
        return false;
    if (dynamic_cast<Flow *>(element)
        && !validFlowConnection(data.id, data.flowEndpoints.from, data.flowEndpoints.to))
        return false;
    if (dynamic_cast<Connector *>(element) && !validConnectorConnection(data.id, data.from, data.to))
        return false;

    if (auto *model = dynamic_cast<ModelElement *>(element)) {
        rename(data.id, data.displayName);
        QVector<QString> dimensions = data.dimensions;
        for (QString &name : dimensions)
            name = normalizeIdentifier(name);
        model->setFormulaData(data.equation, std::move(dimensions), data.elementEquations);
        model->setUnit(data.unit);
        model->setComment(data.comment);
    }
    if (auto *comment = dynamic_cast<Comment *>(element))
        comment->setContent(data.content);
    if (auto *flow = dynamic_cast<Flow *>(element)) {
        if (flow->endpoints() != data.flowEndpoints)
            reconnectFlow(data.id, data.flowEndpoints.from, data.flowEndpoints.to);
    }
    if (auto *connector = dynamic_cast<Connector *>(element)) {
        if (connector->from() != data.from || connector->to() != data.to)
            reconnectConnector(data.id, data.from, data.to);
    }
    setPosition(data.id, data.position);
    setFontStyle(data.id, data.font);
    if (data.kind == ElementKind::Flow || data.kind == ElementKind::Connector)
        setRoute(data.id, data.route);
    return true;
}

bool Document::validRename(EntityId id, const QString &displayName) const
{
    if (!dynamic_cast<const ModelElement *>(find(id)))
        return false;
    const QString normalized = normalizeIdentifier(trimDisplayName(displayName));
    if (normalized.isEmpty())
        return false;
    const auto existing = m_nameIndex.find(normalized);
    return (existing == m_nameIndex.end() || existing.value() == id) && !m_dimensionIndex.count(normalized)
        && !hasMacroName(normalized);
}

bool Document::rename(EntityId id, const QString &displayName)
{
    if (!validRename(id, displayName))
        return false;
    auto *model = dynamic_cast<ModelElement *>(findMutable(id));
    const QString trimmed = trimDisplayName(displayName);
    const QString normalized = normalizeIdentifier(trimmed);
    if (model->displayName() == trimmed)
        return true;
    const QString oldName = model->displayName();
    updateReferences(id, oldName, trimmed);
    m_nameIndex.remove(model->nameKey());
    model->setName(trimmed, normalized);
    m_nameIndex[normalized] = id;
    return true;
}

bool Document::setEquation(EntityId id, const QString &equation)
{
    auto *model = dynamic_cast<ModelElement *>(findMutable(id));
    if (!model)
        return false;
    model->setEquation(equation);
    return true;
}

bool Document::setDimensions(EntityId id, const QVector<QString> &dimensions)
{
    auto *model = dynamic_cast<ModelElement *>(findMutable(id));
    if (!model)
        return false;
    QVector<QString> keys;
    keys.reserve(dimensions.size());
    for (const QString &name : dimensions)
        keys.push_back(normalizeIdentifier(name));
    if (!validArrayData(keys, {}, model->equation()))
        return false;
    model->setDimensions(std::move(keys));
    return true;
}

bool Document::setElementEquation(EntityId id, const QVector<std::size_t> &indices, const QString &equation)
{
    auto *model = dynamic_cast<ModelElement *>(findMutable(id));
    if (!model)
        return false;
    QVector<ArrayElementEquation> updated = model->elementEquationData();
    const auto found = std::find_if(updated.begin(), updated.end(), [&indices](const ArrayElementEquation &entry) {
        return entry.indices == indices;
    });
    if (found == updated.end())
        updated.push_back({indices, equation});
    else
        found->equation = equation;
    if (!validArrayData(model->dimensions(), updated, {}))
        return false;
    model->setElementEquations(std::move(updated));
    return true;
}

bool Document::setUnit(EntityId id, const QString &unit)
{
    auto *model = dynamic_cast<ModelElement *>(findMutable(id));
    if (!model)
        return false;
    model->setUnit(unit);
    return true;
}

bool Document::setComment(EntityId id, const QString &comment)
{
    auto *model = dynamic_cast<ModelElement *>(findMutable(id));
    if (!model)
        return false;
    model->setComment(comment);
    return true;
}

bool Document::setCommentContent(EntityId id, const QString &content)
{
    auto *comment = dynamic_cast<Comment *>(findMutable(id));
    if (!comment)
        return false;
    comment->setContent(content);
    return true;
}

bool Document::setPosition(EntityId id, Vec2 position)
{
    if (!find(id))
        return false;
    m_diagram.m_positions[id] = position;
    for (const DocumentElement &element : elements()) {
        if (element.kind() != ElementKind::Flow)
            continue;
        const Flow &flow = static_cast<const Flow &>(element);
        if (flow.endpoints().from != id && flow.endpoints().to != id)
            continue;
        const Vec2 source = diagram().position(flow.endpoints().from);
        const Vec2 target = diagram().position(flow.endpoints().to);
        m_diagram.m_positions[element.id()] = {(source.x + target.x) / 2.0, (source.y + target.y) / 2.0};
    }
    return true;
}

bool Document::setFontStyle(EntityId id, const FontStyle &font)
{
    const DocumentElement *element = find(id);
    if (!element || !validFontData(element->kind(), font))
        return false;
    if (font == FontStyle {})
        m_diagram.m_fontStyles.erase(id);
    else
        m_diagram.m_fontStyles[id] = font;
    return true;
}

bool Document::reconnectFlow(EntityId id, EntityId from, EntityId to)
{
    auto *flow = dynamic_cast<Flow *>(findMutable(id));
    if (!flow || !validFlowConnection(id, from, to))
        return false;
    flow->setEndpoints({from, to});
    const Vec2 source = diagram().position(from);
    const Vec2 target = diagram().position(to);
    m_diagram.m_positions[id] = {(source.x + target.x) / 2.0, (source.y + target.y) / 2.0};
    return true;
}

bool Document::reconnectConnector(EntityId id, EntityId from, EntityId to)
{
    auto *connector = dynamic_cast<Connector *>(findMutable(id));
    if (!connector || !validConnectorConnection(id, from, to))
        return false;
    connector->setEndpoints(from, to);
    return true;
}

bool Document::setRoute(EntityId id, const Route &route)
{
    const DocumentElement *element = find(id);
    if (!element || !validRouteData(element->kind(), route))
        return false;
    m_diagram.m_routes[id] = route;
    return true;
}

void Document::clear()
{
    m_elements.clear();
    m_nameIndex.clear();
    m_dimensions.clear();
    m_dimensionIndex.clear();
    m_macros.clear();
    m_nextNameSuffix.clear();
    m_diagram.m_positions.clear();
    m_diagram.m_routes.clear();
    m_diagram.m_fontStyles.clear();
    m_nextId = 1;
}

bool Document::validFontData(ElementKind kind, const FontStyle &font)
{
    if (font.pointSize < 0)
        return false;
    return kind <= ElementKind::Data || kind == ElementKind::Comment || font == FontStyle {};
}

bool Document::validRouteData(ElementKind kind, const Route &route)
{
    return (kind == ElementKind::Flow || kind == ElementKind::Connector)
        && !(kind == ElementKind::Flow && route.mode == RouteMode::Orthogonal);
}

bool Document::validArrayData(const QVector<QString> &dimensions,
    const QVector<ArrayElementEquation> &elementEquations, const QString &equation) const
{
    if (dimensions.isEmpty())
        return elementEquations.isEmpty();
    if (!equation.isEmpty() && !elementEquations.isEmpty())
        return false;
    for (const QString &name : dimensions) {
        if (!findDimension(name))
            return false;
    }
    QSet<QString> uniqueIndices;
    for (const ArrayElementEquation &entry : elementEquations) {
        if (entry.indices.size() != dimensions.size())
            return false;
        QString key;
        for (std::size_t index = 0; index < entry.indices.size(); ++index) {
            const Dimension *dimension = findDimension(dimensions[index]);
            if (entry.indices[index] >= dimension->elements.size())
                return false;
            key += QString::number(entry.indices[index]) + ",";
        }
        if (uniqueIndices.contains(key))
            return false;
        uniqueIndices.insert(key);
    }
    return true;
}

bool Document::validFlowEndpoint(EntityId id) const
{
    const DocumentElement *element = find(id);
    return element && (element->kind() == ElementKind::Stock || element->kind() == ElementKind::Cloud);
}

bool Document::validFlowConnection(EntityId ignoredId, EntityId from, EntityId to) const
{
    if (from == to || !validFlowEndpoint(from) || !validFlowEndpoint(to))
        return false;
    for (const DocumentElement &element : elements()) {
        if (element.id() == ignoredId || element.kind() != ElementKind::Flow)
            continue;
        const Flow &flow = static_cast<const Flow &>(element);
        if (flow.endpoints().from == from && flow.endpoints().to == to)
            return false;
    }
    return true;
}

bool Document::validConnectorEndpoints(EntityId from, EntityId to) const
{
    const DocumentElement *source = find(from);
    const DocumentElement *target = find(to);
    if (!source || !target || from == to || source->kind() == ElementKind::Cloud || target->kind() == ElementKind::Cloud
        || source->kind() == ElementKind::Connector || target->kind() == ElementKind::Connector)
        return false;
    return source->kind() != ElementKind::Comment || target->kind() != ElementKind::Comment;
}

bool Document::validConnectorConnection(EntityId ignoredId, EntityId from, EntityId to) const
{
    if (!validConnectorEndpoints(from, to))
        return false;
    for (const DocumentElement &element : elements()) {
        if (element.id() == ignoredId || element.kind() != ElementKind::Connector)
            continue;
        const Connector &connector = static_cast<const Connector &>(element);
        if (connector.from() == from && connector.to() == to)
            return false;
    }
    return true;
}

QString Document::uniqueName(ElementKind kind)
{
    const QString base = defaultName(kind);
    std::uint64_t &suffix = m_nextNameSuffix[kind];
    if (suffix == 0)
        suffix = 1;
    for (;;) {
        const QString candidate = base + QString::number(suffix++);
        if (!m_nameIndex.count(normalizeIdentifier(candidate)) && !m_dimensionIndex.count(normalizeIdentifier(candidate))
            && !hasMacroName(normalizeIdentifier(candidate)))
            return candidate;
    }
}

void Document::updateReferences(EntityId, const QString &oldName, const QString &newName)
{
    for (auto &entry : m_elements) {
        if (auto *model = dynamic_cast<ModelElement *>(entry.second.get())) {
            QString equation = replaceReference(model->equation(), oldName, newName);
            QVector<ArrayElementEquation> elementEquations = model->elementEquationData();
            for (ArrayElementEquation &element : elementEquations)
                element.equation = replaceReference(element.equation, oldName, newName);
            model->setFormulaData(std::move(equation), model->dimensions(), std::move(elementEquations));
        }
    }
}

} // namespace qds::core
