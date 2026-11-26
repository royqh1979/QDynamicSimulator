#include <core/Document.h>

#include <core/Identifier.h>

#include <algorithm>
#include <unordered_set>

namespace qds::core {
namespace {

std::string defaultName(ElementKind kind)
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

bool identifierStart(unsigned char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_' || c >= 128;
}

bool identifierPart(unsigned char c)
{
    return identifierStart(c) || (c >= '0' && c <= '9') || c == '$';
}

std::string quotedIdentifier(const std::string &name)
{
    bool plain = !name.empty() && identifierStart(static_cast<unsigned char>(name.front()));
    for (unsigned char c : name)
        plain = plain && identifierPart(c);
    if (plain)
        return name;
    std::string quoted = "\"";
    for (char c : name) {
        if (c == '\\' || c == '"')
            quoted.push_back('\\');
        if (c == '\n')
            quoted += "\\n";
        else
            quoted.push_back(c);
    }
    quoted.push_back('"');
    return quoted;
}

std::string unescapeIdentifier(const std::string &name)
{
    std::string result;
    for (std::size_t index = 0; index < name.size(); ++index) {
        if (name[index] == '\\' && index + 1 < name.size()) {
            const char escaped = name[++index];
            result.push_back(escaped == 'n' ? '\n' : escaped);
        } else {
            result.push_back(name[index]);
        }
    }
    return result;
}

std::string replaceReference(const std::string &equation, const std::string &oldName, const std::string &newName)
{
    std::string result;
    for (std::size_t index = 0; index < equation.size();) {
        if (equation[index] == '"') {
            const std::size_t start = index++;
            std::string value;
            bool closed = false;
            while (index < equation.size()) {
                if (equation[index] == '\\' && index + 1 < equation.size()) {
                    value.push_back(equation[index++]);
                    value.push_back(equation[index++]);
                } else if (equation[index] == '"') {
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
                result += equation.substr(start, index - start);
            continue;
        }
        const unsigned char c = static_cast<unsigned char>(equation[index]);
        if (!identifierStart(c)) {
            result.push_back(equation[index++]);
            continue;
        }
        const std::size_t start = index++;
        while (index < equation.size() && identifierPart(static_cast<unsigned char>(equation[index])))
            ++index;
        const std::string token = equation.substr(start, index - start);
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
    , m_macroNames(std::move(other.m_macroNames))
    , m_nextNameSuffix(std::move(other.m_nextNameSuffix))
    , m_nextId(other.m_nextId)
    , m_model(*this)
    , m_diagram(*this)
    , m_workspace(std::move(other.m_workspace))
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
    m_macroNames = std::move(other.m_macroNames);
    m_nextNameSuffix = std::move(other.m_nextNameSuffix);
    m_nextId = other.m_nextId;
    m_diagram.m_positions = std::move(other.m_diagram.m_positions);
    m_diagram.m_routes = std::move(other.m_diagram.m_routes);
    m_diagram.m_fontStyles = std::move(other.m_diagram.m_fontStyles);
    m_workspace = std::move(other.m_workspace);
    return *this;
}

DocumentElement *Document::find(EntityId id)
{
    const auto it = m_elements.find(id);
    return it == m_elements.end() ? nullptr : it->second.get();
}

const DocumentElement *Document::find(EntityId id) const
{
    const auto it = m_elements.find(id);
    return it == m_elements.end() ? nullptr : it->second.get();
}

const ModelElement *Document::findByName(const std::string &name) const
{
    const auto it = m_nameIndex.find(normalizeIdentifier(name));
    if (it == m_nameIndex.end())
        return nullptr;
    return dynamic_cast<const ModelElement *>(find(it->second));
}

const Dimension *Document::findDimension(const std::string &name) const
{
    const auto found = m_dimensionIndex.find(normalizeIdentifier(name));
    return found == m_dimensionIndex.end() ? nullptr : &m_dimensions[found->second];
}

bool Document::defineDimension(const std::string &displayName, std::size_t size)
{
    if (size == 0)
        return false;
    Dimension dimension;
    dimension.displayName = trimDisplayName(displayName);
    dimension.nameKey = normalizeIdentifier(dimension.displayName);
    dimension.elements.resize(size);
    for (std::size_t index = 0; index < size; ++index)
        dimension.elements[index].displayName = std::to_string(index + 1);
    return defineDimensionData(std::move(dimension));
}

bool Document::defineDimension(const std::string &displayName, const std::vector<std::string> &elements)
{
    Dimension dimension;
    dimension.displayName = trimDisplayName(displayName);
    dimension.nameKey = normalizeIdentifier(dimension.displayName);
    for (const std::string &name : elements) {
        DimensionElement element;
        element.displayName = trimDisplayName(name);
        element.nameKey = normalizeIdentifier(element.displayName);
        dimension.elements.push_back(std::move(element));
    }
    return defineDimensionData(std::move(dimension));
}

bool Document::defineDimensionData(Dimension dimension)
{
    if (dimension.nameKey.empty() || dimension.elements.empty() || m_nameIndex.count(dimension.nameKey)
        || m_dimensionIndex.count(dimension.nameKey) || m_macroNames.count(dimension.nameKey))
        return false;
    const bool numbered = dimension.elements.front().nameKey.empty();
    std::unordered_set<std::string> elementNames;
    for (const DimensionElement &element : dimension.elements) {
        if (numbered != element.nameKey.empty())
            return false;
        if (!numbered && !elementNames.insert(element.nameKey).second)
            return false;
    }
    m_dimensionIndex[dimension.nameKey] = m_dimensions.size();
    m_dimensions.push_back(std::move(dimension));
    return true;
}

bool Document::defineMacro(MacroDefinition macro)
{
    macro.displayName = trimDisplayName(macro.displayName);
    macro.nameKey = normalizeIdentifier(macro.displayName);
    if (macro.nameKey.empty() || macro.equation.empty() || m_nameIndex.count(macro.nameKey)
        || m_dimensionIndex.count(macro.nameKey))
        return false;
    std::unordered_set<std::string> parameters;
    bool hasDefault = false;
    for (MacroParameter &parameter : macro.parameters) {
        parameter.displayName = trimDisplayName(parameter.displayName);
        parameter.nameKey = normalizeIdentifier(parameter.displayName);
        if (parameter.nameKey.empty() || !parameters.insert(parameter.nameKey).second)
            return false;
        if (parameter.defaultEquation.empty()) {
            if (hasDefault)
                return false;
        } else {
            hasDefault = true;
        }
    }
    const std::size_t required = macro.requiredArguments();
    for (const MacroDefinition &existing : m_macros) {
        if (existing.nameKey == macro.nameKey && required <= existing.parameters.size()
            && existing.requiredArguments() <= macro.parameters.size())
            return false;
    }
    m_macroNames.insert(macro.nameKey);
    m_macros.push_back(std::move(macro));
    return true;
}

const MacroDefinition *Document::findMacro(const std::string &name, std::size_t argumentCount) const
{
    const std::string key = normalizeIdentifier(name);
    for (const MacroDefinition &macro : m_macros) {
        if (macro.nameKey == key && argumentCount >= macro.requiredArguments()
            && argumentCount <= macro.parameters.size())
            return &macro;
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
        if (model->nameKey.empty() || m_nameIndex.count(model->nameKey) || m_dimensionIndex.count(model->nameKey) || m_macroNames.count(model->nameKey))
            return 0;
        m_nameIndex[model->nameKey] = id;
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
    variable->displayName = uniqueName(kind);
    variable->nameKey = normalizeIdentifier(variable->displayName);
    return insert(std::move(variable), position);
}

EntityId Document::createFlow(EntityId from, EntityId to)
{
    if (from == to || !validFlowEndpoint(from) || !validFlowEndpoint(to))
        return 0;
    for (const DocumentElement &element : elements()) {
        if (element.kind() == ElementKind::Flow) {
            const Flow &flow = static_cast<const Flow &>(element);
            if (flow.endpoints.from == from && flow.endpoints.to == to)
                return 0;
        }
    }
    auto flow = std::make_unique<Flow>(m_nextId++);
    flow->displayName = uniqueName(ElementKind::Flow);
    flow->nameKey = normalizeIdentifier(flow->displayName);
    flow->endpoints = {from, to};
    const Vec2 source = diagram().position(from);
    const Vec2 target = diagram().position(to);
    return insert(std::move(flow), {(source.x + target.x) / 2.0, (source.y + target.y) / 2.0});
}

EntityId Document::createConnector(EntityId from, EntityId to)
{
    if (!validConnectorEndpoints(from, to))
        return 0;
    for (const DocumentElement &element : elements()) {
        if (element.kind() == ElementKind::Connector) {
            const Connector &connector = static_cast<const Connector &>(element);
            if (connector.from == from && connector.to == to)
                return 0;
        }
    }
    auto connector = std::make_unique<Connector>(m_nextId++);
    connector->from = from;
    connector->to = to;
    const EntityId id = insert(std::move(connector), {});
    if (id != 0)
        m_diagram.m_routes[id] = defaultConnectorRoute();
    return id;
}

EntityId Document::createCloud(Vec2 position)
{
    return insert(std::make_unique<Cloud>(m_nextId++), position);
}

EntityId Document::createComment(const std::string &text, Vec2 position)
{
    auto comment = std::make_unique<Comment>(m_nextId++);
    comment->content = text;
    return insert(std::move(comment), position);
}

std::vector<EntityId> Document::cascade(EntityId id) const
{
    if (!find(id))
        return {};
    std::unordered_set<EntityId> included;
    std::vector<EntityId> pending {id};
    for (std::size_t index = 0; index < pending.size(); ++index) {
        const EntityId current = pending[index];
        if (!included.insert(current).second)
            continue;
        for (const DocumentElement &element : elements()) {
            if (element.kind() == ElementKind::Flow) {
                const Flow &flow = static_cast<const Flow &>(element);
                if (flow.endpoints.from == current || flow.endpoints.to == current)
                    pending.push_back(element.id());
            } else if (element.kind() == ElementKind::Connector) {
                const Connector &connector = static_cast<const Connector &>(element);
                if (connector.from == current || connector.to == current)
                    pending.push_back(element.id());
            }
        }
    }
    std::vector<EntityId> removed(included.begin(), included.end());
    std::sort(removed.begin(), removed.end(), [this](EntityId left, EntityId right) {
        const int leftRank = removalRank(find(left)->kind());
        const int rightRank = removalRank(find(right)->kind());
        return leftRank != rightRank ? leftRank < rightRank : left < right;
    });
    return removed;
}

std::vector<EntityId> Document::remove(EntityId id)
{
    const std::vector<EntityId> removed = cascade(id);
    for (EntityId removedId : removed) {
        if (auto *model = dynamic_cast<ModelElement *>(find(removedId)))
            m_nameIndex.erase(model->nameKey);
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
        data.displayName = model->displayName;
        data.equation = model->equation;
        data.dimensions = model->dimensions;
        data.elementEquations = model->elementEquations;
        data.unit = model->unit;
        data.comment = model->comment;
    }
    if (auto *comment = dynamic_cast<const Comment *>(element))
        data.content = comment->content;
    if (auto *flow = dynamic_cast<const Flow *>(element))
        data.flowEndpoints = flow->endpoints;
    if (auto *connector = dynamic_cast<const Connector *>(element)) {
        data.from = connector->from;
        data.to = connector->to;
    }
    return data;
}

bool Document::restore(const ElementData &data)
{
    const EntityId id = data.id == 0 ? m_nextId++ : data.id;
    if (find(id))
        return false;
    const std::string normalized = normalizeIdentifier(data.displayName);
    if (data.kind <= ElementKind::Data && (normalized.empty() || m_nameIndex.count(normalized)
            || m_dimensionIndex.count(normalized) || m_macroNames.count(normalized)
            || !validArrayData(data.dimensions, data.elementEquations, data.equation)))
        return false;
    const bool fontSupported = data.kind <= ElementKind::Data || data.kind == ElementKind::Comment;
    if (data.font.pointSize < 0 || (!fontSupported && data.font != FontStyle {}))
        return false;
    std::unique_ptr<DocumentElement> element;
    if (data.kind == ElementKind::Flow) {
        if (!validFlowEndpoint(data.flowEndpoints.from) || !validFlowEndpoint(data.flowEndpoints.to)
            || data.flowEndpoints.from == data.flowEndpoints.to)
            return false;
        auto flow = std::make_unique<Flow>(id);
        flow->endpoints = data.flowEndpoints;
        element = std::move(flow);
    } else if (data.kind == ElementKind::Connector) {
        if (!validConnectorEndpoints(data.from, data.to))
            return false;
        auto connector = std::make_unique<Connector>(id);
        connector->from = data.from;
        connector->to = data.to;
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
        model->displayName = trimDisplayName(data.displayName);
        model->nameKey = normalizeIdentifier(model->displayName);
        model->equation = data.equation;
        model->dimensions = data.dimensions;
        for (std::string &name : model->dimensions)
            name = normalizeIdentifier(name);
        model->elementEquations = data.elementEquations;
        model->unit = data.unit;
        model->comment = data.comment;
    }
    if (auto *comment = dynamic_cast<Comment *>(element.get()))
        comment->content = data.content;
    Route route = data.route;
    if (data.kind == ElementKind::Flow && route.mode == RouteMode::Orthogonal)
        return false;
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
    DocumentElement *element = find(data.id);
    if (!element)
        return restore(data);
    if (element->kind() != data.kind)
        return false;
    if (auto *model = dynamic_cast<ModelElement *>(element)) {
        if (!validArrayData(data.dimensions, data.elementEquations, data.equation) || !rename(data.id, data.displayName))
            return false;
        model->equation = data.equation;
        model->dimensions = data.dimensions;
        for (std::string &name : model->dimensions)
            name = normalizeIdentifier(name);
        model->elementEquations = data.elementEquations;
        model->unit = data.unit;
        model->comment = data.comment;
    }
    if (auto *comment = dynamic_cast<Comment *>(element))
        comment->content = data.content;
    if (auto *flow = dynamic_cast<Flow *>(element)) {
        if (flow->endpoints != data.flowEndpoints
            && !reconnectFlow(data.id, data.flowEndpoints.from, data.flowEndpoints.to))
            return false;
    }
    if (auto *connector = dynamic_cast<Connector *>(element)) {
        if ((connector->from != data.from || connector->to != data.to)
            && !reconnectConnector(data.id, data.from, data.to))
            return false;
    }
    setPosition(data.id, data.position);
    if (!setFontStyle(data.id, data.font))
        return false;
    if (data.kind == ElementKind::Flow || data.kind == ElementKind::Connector)
        return setRoute(data.id, data.route);
    return true;
}

bool Document::rename(EntityId id, const std::string &displayName)
{
    auto *model = dynamic_cast<ModelElement *>(find(id));
    if (!model)
        return false;
    const std::string trimmed = trimDisplayName(displayName);
    const std::string normalized = normalizeIdentifier(trimmed);
    if (normalized.empty())
        return false;
    const auto existing = m_nameIndex.find(normalized);
    if ((existing != m_nameIndex.end() && existing->second != id) || m_dimensionIndex.count(normalized)
        || m_macroNames.count(normalized))
        return false;
    if (model->displayName == trimmed)
        return true;
    const std::string oldName = model->displayName;
    updateReferences(id, oldName, trimmed);
    m_nameIndex.erase(model->nameKey);
    model->displayName = trimmed;
    model->nameKey = normalized;
    m_nameIndex[normalized] = id;
    return true;
}

bool Document::setEquation(EntityId id, const std::string &equation)
{
    auto *model = dynamic_cast<ModelElement *>(find(id));
    if (!model)
        return false;
    model->equation = equation;
    model->elementEquations.clear();
    return true;
}

bool Document::setDimensions(EntityId id, const std::vector<std::string> &dimensions)
{
    auto *model = dynamic_cast<ModelElement *>(find(id));
    if (!model)
        return false;
    std::vector<std::string> keys;
    keys.reserve(dimensions.size());
    for (const std::string &name : dimensions)
        keys.push_back(normalizeIdentifier(name));
    if (!validArrayData(keys, {}, model->equation))
        return false;
    model->dimensions = std::move(keys);
    model->elementEquations.clear();
    return true;
}

bool Document::setElementEquation(EntityId id, const std::vector<std::size_t> &indices, const std::string &equation)
{
    auto *model = dynamic_cast<ModelElement *>(find(id));
    if (!model)
        return false;
    std::vector<ArrayElementEquation> updated = model->elementEquations;
    const auto found = std::find_if(updated.begin(), updated.end(), [&indices](const ArrayElementEquation &entry) {
        return entry.indices == indices;
    });
    if (found == updated.end())
        updated.push_back({indices, equation});
    else
        found->equation = equation;
    if (!validArrayData(model->dimensions, updated, {}))
        return false;
    model->equation.clear();
    model->elementEquations = std::move(updated);
    return true;
}

bool Document::setUnit(EntityId id, const std::string &unit)
{
    auto *model = dynamic_cast<ModelElement *>(find(id));
    if (!model)
        return false;
    model->unit = unit;
    return true;
}

bool Document::setComment(EntityId id, const std::string &comment)
{
    auto *model = dynamic_cast<ModelElement *>(find(id));
    if (!model)
        return false;
    model->comment = comment;
    return true;
}

bool Document::setCommentContent(EntityId id, const std::string &content)
{
    auto *comment = dynamic_cast<Comment *>(find(id));
    if (!comment)
        return false;
    comment->content = content;
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
        if (flow.endpoints.from != id && flow.endpoints.to != id)
            continue;
        const Vec2 source = diagram().position(flow.endpoints.from);
        const Vec2 target = diagram().position(flow.endpoints.to);
        m_diagram.m_positions[element.id()] = {(source.x + target.x) / 2.0, (source.y + target.y) / 2.0};
    }
    return true;
}

bool Document::setFontStyle(EntityId id, const FontStyle &font)
{
    const DocumentElement *element = find(id);
    if (!element || font.pointSize < 0)
        return false;
    const bool supported = dynamic_cast<const ModelElement *>(element) || element->kind() == ElementKind::Comment;
    if (!supported)
        return font == FontStyle {};
    if (font == FontStyle {})
        m_diagram.m_fontStyles.erase(id);
    else
        m_diagram.m_fontStyles[id] = font;
    return true;
}

bool Document::reconnectFlow(EntityId id, EntityId from, EntityId to)
{
    auto *flow = dynamic_cast<Flow *>(find(id));
    if (!flow || from == to || !validFlowEndpoint(from) || !validFlowEndpoint(to))
        return false;
    for (const DocumentElement &element : elements()) {
        if (element.id() == id || element.kind() != ElementKind::Flow)
            continue;
        const Flow &other = static_cast<const Flow &>(element);
        if (other.endpoints.from == from && other.endpoints.to == to)
            return false;
    }
    flow->endpoints = {from, to};
    const Vec2 source = diagram().position(from);
    const Vec2 target = diagram().position(to);
    m_diagram.m_positions[id] = {(source.x + target.x) / 2.0, (source.y + target.y) / 2.0};
    return true;
}

bool Document::reconnectConnector(EntityId id, EntityId from, EntityId to)
{
    auto *connector = dynamic_cast<Connector *>(find(id));
    if (!connector || !validConnectorEndpoints(from, to))
        return false;
    for (const DocumentElement &element : elements()) {
        if (element.id() == id || element.kind() != ElementKind::Connector)
            continue;
        const Connector &other = static_cast<const Connector &>(element);
        if (other.from == from && other.to == to)
            return false;
    }
    connector->from = from;
    connector->to = to;
    return true;
}

bool Document::setRoute(EntityId id, const Route &route)
{
    const DocumentElement *element = find(id);
    if (!element || (element->kind() != ElementKind::Flow && element->kind() != ElementKind::Connector)
        || (element->kind() == ElementKind::Flow && route.mode == RouteMode::Orthogonal))
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
    m_macroNames.clear();
    m_nextNameSuffix.clear();
    m_diagram.m_positions.clear();
    m_diagram.m_routes.clear();
    m_diagram.m_fontStyles.clear();
    m_workspace = {};
    m_nextId = 1;
}

bool Document::validArrayData(const std::vector<std::string> &dimensions,
    const std::vector<ArrayElementEquation> &elementEquations, const std::string &equation) const
{
    if (dimensions.empty())
        return elementEquations.empty();
    if (!equation.empty() && !elementEquations.empty())
        return false;
    for (const std::string &name : dimensions) {
        if (!findDimension(name))
            return false;
    }
    std::unordered_set<std::string> uniqueIndices;
    for (const ArrayElementEquation &entry : elementEquations) {
        if (entry.indices.size() != dimensions.size())
            return false;
        std::string key;
        for (std::size_t index = 0; index < entry.indices.size(); ++index) {
            const Dimension *dimension = findDimension(dimensions[index]);
            if (entry.indices[index] >= dimension->elements.size())
                return false;
            key += std::to_string(entry.indices[index]) + ",";
        }
        if (!uniqueIndices.insert(key).second)
            return false;
    }
    return true;
}

bool Document::validFlowEndpoint(EntityId id) const
{
    const DocumentElement *element = find(id);
    return element && (element->kind() == ElementKind::Stock || element->kind() == ElementKind::Cloud);
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

std::string Document::uniqueName(ElementKind kind)
{
    const std::string base = defaultName(kind);
    std::uint64_t &suffix = m_nextNameSuffix[kind];
    if (suffix == 0)
        suffix = 1;
    for (;;) {
        const std::string candidate = base + std::to_string(suffix++);
        if (!m_nameIndex.count(normalizeIdentifier(candidate)) && !m_dimensionIndex.count(normalizeIdentifier(candidate))
            && !m_macroNames.count(normalizeIdentifier(candidate)))
            return candidate;
    }
}

void Document::updateReferences(EntityId, const std::string &oldName, const std::string &newName)
{
    for (auto &entry : m_elements) {
        if (auto *model = dynamic_cast<ModelElement *>(entry.second.get())) {
            model->equation = replaceReference(model->equation, oldName, newName);
            for (ArrayElementEquation &element : model->elementEquations)
                element.equation = replaceReference(element.equation, oldName, newName);
        }
    }
}

} // namespace qds::core
