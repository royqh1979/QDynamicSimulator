#include <io/XmileFile.h>

#include <core/Identifier.h>

#include <QCoreApplication>
#include <QFile>
#include <QSaveFile>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>
#include <QtGlobal>

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace qds::io {
namespace {

using UidMap = std::unordered_map<quint64, core::EntityId>;

QString translate(const char *text)
{
    return QCoreApplication::translate("XmileFile", text);
}

void setError(QString *errorMessage, const QString &message)
{
    if (errorMessage)
        *errorMessage = message;
}

QString referenceName(const QString &text)
{
    const QString trimmed = text.trimmed();
    if (trimmed.size() < 2 || !trimmed.startsWith('"') || !trimmed.endsWith('"'))
        return trimmed;
    QString result;
    const QString value = trimmed.mid(1, trimmed.size() - 2);
    for (int index = 0; index < value.size(); ++index) {
        if (value[index] == '\\' && index + 1 < value.size()) {
            const QChar escaped = value[++index];
            result.append(escaped == 'n' ? QChar('\n') : escaped);
        } else {
            result.append(value[index]);
        }
    }
    return result;
}

QStringList splitSubscripts(const QString &text)
{
    QStringList result;
    QString current;
    bool quoted = false;
    bool escaped = false;
    for (const QChar character : text) {
        if (escaped) {
            current.append(character);
            escaped = false;
            continue;
        }
        if (character == '\\' && quoted) {
            current.append(character);
            escaped = true;
            continue;
        }
        if (character == '"')
            quoted = !quoted;
        if (character == ',' && !quoted) {
            result.append(current.trimmed());
            current.clear();
        } else {
            current.append(character);
        }
    }
    result.append(current.trimmed());
    return result;
}

QString quotedIdentifier(const std::string &name)
{
    QString value = QString::fromStdString(name);
    value.replace('\\', "\\\\");
    value.replace('"', "\\\"");
    value.replace('\n', "\\n");
    return '"' + value + '"';
}

QString variableTag(core::ElementKind kind)
{
    switch (kind) {
    case core::ElementKind::Stock:
        return QStringLiteral("stock");
    case core::ElementKind::Flow:
        return QStringLiteral("flow");
    case core::ElementKind::Auxiliary:
    case core::ElementKind::Constant:
    case core::ElementKind::Data:
        return QStringLiteral("aux");
    case core::ElementKind::Cloud:
        return QStringLiteral("cloud");
    case core::ElementKind::Comment:
        return QStringLiteral("text_box");
    case core::ElementKind::Connector:
        return QStringLiteral("connector");
    }
    return {};
}

QString elementName(const core::DocumentElement &element)
{
    if (const auto *model = dynamic_cast<const core::ModelElement *>(&element))
        return QString::fromStdString(model->displayName());
    if (const auto *comment = dynamic_cast<const core::Comment *>(&element))
        return QString::fromStdString(comment->content());
    return {};
}

const core::ModelElement *findElementByName(const core::Document &document, const std::string &name)
{
    return document.model().findByName(name);
}

void writeXmileDimensions(QXmlStreamWriter &writer, const core::Document &document)
{
    if (document.dimensions().empty())
        return;
    writer.writeStartElement(QStringLiteral("dimensions"));
    for (const core::Dimension &dimension : document.dimensions()) {
        writer.writeStartElement(QStringLiteral("dim"));
        writer.writeAttribute(QStringLiteral("name"), QString::fromStdString(dimension.displayName));
        if (dimension.elements.front().nameKey.empty()) {
            writer.writeAttribute(QStringLiteral("size"), QString::number(dimension.elements.size()));
        } else {
            for (const core::DimensionElement &element : dimension.elements) {
                writer.writeStartElement(QStringLiteral("elem"));
                writer.writeAttribute(QStringLiteral("name"), QString::fromStdString(element.displayName));
                writer.writeEndElement();
            }
        }
        writer.writeEndElement();
    }
    writer.writeEndElement();
}

void writeXmileMacros(QXmlStreamWriter &writer, const core::Document &document)
{
    for (const core::MacroDefinition &macro : document.macros()) {
        writer.writeStartElement(QStringLiteral("macro"));
        writer.writeAttribute(QStringLiteral("name"), QString::fromStdString(macro.displayName));
        for (const core::MacroParameter &parameter : macro.parameters) {
            writer.writeStartElement(QStringLiteral("parm"));
            if (!parameter.defaultEquation.empty())
                writer.writeAttribute(QStringLiteral("default"), QString::fromStdString(parameter.defaultEquation));
            writer.writeCharacters(QString::fromStdString(parameter.displayName));
            writer.writeEndElement();
        }
        writer.writeTextElement(QStringLiteral("eqn"), QString::fromStdString(macro.equation));
        writer.writeEndElement();
    }
}

void writeXmileArrayDefinition(QXmlStreamWriter &writer, const core::Document &document, const core::ModelElement &element)
{
    if (element.dimensions().empty())
        return;
    writer.writeStartElement(QStringLiteral("dimensions"));
    for (const std::string &name : element.dimensions()) {
        writer.writeStartElement(QStringLiteral("dim"));
        writer.writeAttribute(QStringLiteral("name"), QString::fromStdString(document.findDimension(name)->displayName));
        writer.writeEndElement();
    }
    writer.writeEndElement();
    for (const core::ArrayElementEquation &entry : element.elementEquations()) {
        writer.writeStartElement(QStringLiteral("element"));
        QStringList indices;
        for (std::size_t position = 0; position < entry.indices.size(); ++position) {
            const core::Dimension *dimension = document.findDimension(element.dimensions()[position]);
            const core::DimensionElement &index = dimension->elements[entry.indices[position]];
            indices.append(index.nameKey.empty() ? QString::number(entry.indices[position] + 1) : quotedIdentifier(index.displayName));
        }
        writer.writeAttribute(QStringLiteral("subscript"), indices.join(','));
        writer.writeTextElement(QStringLiteral("eqn"), QString::fromStdString(entry.equation));
        writer.writeEndElement();
    }
}

QByteArray writeXml(const core::Document &document)
{
    const core::Model &model = document.model();
    QByteArray output;
    QXmlStreamWriter writer(&output);
    writer.setAutoFormatting(true);
    writer.writeStartDocument();
    writer.writeStartElement(QStringLiteral("xmile"));
    writer.writeDefaultNamespace(QStringLiteral("http://docs.oasis-open.org/xmile/ns/XMILE/v1.0"));
    writer.writeAttribute(QStringLiteral("version"), QStringLiteral("1.1"));

    writer.writeStartElement(QStringLiteral("header"));
    writer.writeStartElement(QStringLiteral("smile"));
    writer.writeAttribute(QStringLiteral("version"), QStringLiteral("1.0"));
    writer.writeAttribute(QStringLiteral("namespace"), QStringLiteral("std"));
    writer.writeEndElement();
    writer.writeTextElement(QStringLiteral("vendor"), QStringLiteral("QDynamicSimulator"));
    writer.writeTextElement(QStringLiteral("product"), QStringLiteral("QDynamicSimulator"));
    writer.writeEndElement();

    if (!document.dimensions().empty() || !document.macros().empty()) {
        writer.writeStartElement(QStringLiteral("options"));
        if (!document.dimensions().empty()) {
            std::size_t maximumDimensions = 0;
            for (const core::ModelElement &element : model.elements())
                maximumDimensions = std::max(maximumDimensions, element.dimensions().size());
            writer.writeStartElement(QStringLiteral("uses_arrays"));
            writer.writeAttribute(QStringLiteral("maximum_dimensions"), QString::number(maximumDimensions));
            writer.writeEndElement();
        }
        if (!document.macros().empty())
            writer.writeEmptyElement(QStringLiteral("uses_macros"));
        writer.writeEndElement();
    }

    writer.writeStartElement(QStringLiteral("sim_specs"));
    writer.writeTextElement(QStringLiteral("start"), QStringLiteral("0"));
    writer.writeTextElement(QStringLiteral("stop"), QStringLiteral("100"));
    writer.writeTextElement(QStringLiteral("dt"), QStringLiteral("0.25"));
    writer.writeEndElement();

    writeXmileDimensions(writer, document);
    writeXmileMacros(writer, document);

    writer.writeStartElement(QStringLiteral("model"));
    writer.writeStartElement(QStringLiteral("variables"));
    for (const core::ModelElement &variable : model.elements()) {
        const QString name = QString::fromStdString(variable.displayName());
        writer.writeStartElement(variableTag(variable.kind()));
        writer.writeAttribute(QStringLiteral("name"), name);
        writeXmileArrayDefinition(writer, document, variable);
        if (variable.elementEquations().empty())
            writer.writeTextElement(
                QStringLiteral("eqn"),
                variable.equation().empty() ? QStringLiteral("0") : QString::fromStdString(variable.equation()));
        if (!variable.unit().empty())
            writer.writeTextElement(QStringLiteral("units"), QString::fromStdString(variable.unit()));
        if (!variable.comment().empty())
            writer.writeTextElement(QStringLiteral("doc"), QString::fromStdString(variable.comment()));
        if (variable.kind() == core::ElementKind::Stock) {
            for (const core::ModelElement &candidate : model.elements()) {
                if (candidate.kind() != core::ElementKind::Flow)
                    continue;
                const auto &flow = static_cast<const core::Flow &>(candidate);
                if (flow.endpoints().to == variable.id())
                    writer.writeTextElement(QStringLiteral("inflow"), QString::fromStdString(flow.displayName()));
                if (flow.endpoints().from == variable.id())
                    writer.writeTextElement(QStringLiteral("outflow"), QString::fromStdString(flow.displayName()));
            }
        }
        writer.writeEndElement();
    }
    writer.writeEndElement();

    writer.writeStartElement(QStringLiteral("views"));
    writer.writeStartElement(QStringLiteral("view"));
    writer.writeAttribute(QStringLiteral("type"), QStringLiteral("stock_flow"));
    for (const core::DocumentElement &element : document.elements()) {
        if (element.kind() == core::ElementKind::Connector || element.kind() == core::ElementKind::Cloud)
            continue;
        writer.writeStartElement(variableTag(element.kind()));
        const core::Vec2 position = document.diagram().position(element.id());
        if (element.kind() != core::ElementKind::Comment)
            writer.writeAttribute(QStringLiteral("name"), elementName(element));
        else
            writer.writeAttribute(QStringLiteral("uid"), QString::number(element.id()));
        writer.writeAttribute(QStringLiteral("x"), QString::number(position.x, 'g', 15));
        writer.writeAttribute(QStringLiteral("y"), QString::number(position.y, 'g', 15));
        if (element.kind() == core::ElementKind::Flow) {
            const auto &flow = static_cast<const core::Flow &>(element);
            writer.writeStartElement(QStringLiteral("pts"));
            const auto writePoint = [&writer](const core::Vec2 &point) {
                writer.writeStartElement(QStringLiteral("pt"));
                writer.writeAttribute(QStringLiteral("x"), QString::number(point.x, 'g', 15));
                writer.writeAttribute(QStringLiteral("y"), QString::number(point.y, 'g', 15));
                writer.writeEndElement();
            };
            writePoint(document.diagram().position(flow.endpoints().from));
            writePoint(document.diagram().position(flow.endpoints().to));
            writer.writeEndElement();
        }
        if (element.kind() == core::ElementKind::Comment)
            writer.writeCharacters(elementName(element));
        writer.writeEndElement();
    }
    for (const core::ViewElement &element : document.diagram().elements()) {
        if (element.kind() != core::ElementKind::Connector)
            continue;
        const auto &connection = static_cast<const core::Connector &>(element);
        const core::DocumentElement *from = document.find(connection.from());
        const core::DocumentElement *to = document.find(connection.to());
        if (!from || !to || !dynamic_cast<const core::ModelElement *>(from)
            || !dynamic_cast<const core::ModelElement *>(to))
            continue;
        const core::Vec2 fromPosition = document.diagram().position(connection.from());
        const core::Vec2 toPosition = document.diagram().position(connection.to());
        double angle = std::atan2(-(toPosition.y - fromPosition.y), toPosition.x - fromPosition.x) * 180.0 / 3.14159265358979323846;
        if (angle < 0.0)
            angle += 360.0;
        writer.writeStartElement(QStringLiteral("connector"));
        writer.writeAttribute(QStringLiteral("uid"), QString::number(connection.id()));
        writer.writeAttribute(QStringLiteral("angle"), QString::number(angle, 'g', 15));
        writer.writeTextElement(QStringLiteral("from"), elementName(*from));
        writer.writeTextElement(QStringLiteral("to"), elementName(*to));
        writer.writeStartElement(QStringLiteral("pts"));
        const auto writePoint = [&writer](const core::Vec2 &point) {
            writer.writeStartElement(QStringLiteral("pt"));
            writer.writeAttribute(QStringLiteral("x"), QString::number(point.x, 'g', 15));
            writer.writeAttribute(QStringLiteral("y"), QString::number(point.y, 'g', 15));
            writer.writeEndElement();
        };
        writePoint(fromPosition);
        writePoint(toPosition);
        writer.writeEndElement();
        writer.writeEndElement();
    }
    writer.writeEndElement();
    writer.writeEndElement();
    writer.writeEndElement();
    writer.writeEndElement();
    writer.writeEndDocument();
    return output;
}

struct PendingConnection {
    std::string from;
    std::string to;
    quint64 fromUid = 0;
    quint64 toUid = 0;
};

struct PendingFlowEndpoint {
    std::string flow;
    std::string stock;
    bool inflow = false;
};

struct PendingFlowLayout {
    std::string flow;
    std::vector<core::Vec2> anchorPoints;
    core::Vec2 valveCenter;
    bool hasValveCenter = false;
};

bool parseXmileMacro(QXmlStreamReader &reader, core::Document &document, QString *errorMessage)
{
    core::MacroDefinition macro;
    macro.displayName = reader.attributes().value(QStringLiteral("name")).toString().toStdString();
    while (reader.readNext() != QXmlStreamReader::Invalid) {
        if (reader.isEndElement() && reader.name() == QStringLiteral("macro")) {
            if (document.defineMacro(std::move(macro)))
                return true;
            setError(errorMessage, translate("The XMILE macro is invalid or has a conflicting signature."));
            return false;
        }
        if (!reader.isStartElement())
            continue;
        if (reader.name() == QStringLiteral("parm")) {
            core::MacroParameter parameter;
            parameter.displayName = reader.attributes().value(QStringLiteral("name")).toString().toStdString();
            parameter.defaultEquation = reader.attributes().value(QStringLiteral("default")).toString().toStdString();
            const QString text = reader.readElementText().trimmed();
            if (!text.isEmpty())
                parameter.displayName = text.toStdString();
            macro.parameters.push_back(std::move(parameter));
        } else if (reader.name() == QStringLiteral("eqn")) {
            macro.equation = reader.readElementText().trimmed().toStdString();
        } else {
            reader.skipCurrentElement();
        }
    }
    return false;
}

bool parseXmileDimensions(QXmlStreamReader &reader, core::Document &document, QString *errorMessage)
{
    while (reader.readNext() != QXmlStreamReader::Invalid) {
        if (reader.isEndElement() && reader.name() == QStringLiteral("dimensions"))
            return true;
        if (!reader.isStartElement() || reader.name() != QStringLiteral("dim"))
            continue;
        const QString name = reader.attributes().value(QStringLiteral("name")).toString();
        bool sizeOk = false;
        const int size = reader.attributes().value(QStringLiteral("size")).toInt(&sizeOk);
        QStringList elements;
        while (reader.readNext() != QXmlStreamReader::Invalid) {
            if (reader.isEndElement() && reader.name() == QStringLiteral("dim"))
                break;
            if (reader.isStartElement() && reader.name() == QStringLiteral("elem")) {
                elements.append(reader.attributes().value(QStringLiteral("name")).toString());
                reader.skipCurrentElement();
            }
        }
        bool valid = false;
        if (elements.isEmpty() && sizeOk && size > 0) {
            valid = document.defineDimension(name.toStdString(), static_cast<std::size_t>(size));
        } else if (!elements.isEmpty() && !sizeOk) {
            std::vector<std::string> names;
            for (const QString &element : elements)
                names.push_back(element.toStdString());
            valid = document.defineDimension(name.toStdString(), names);
        }
        if (!valid) {
            setError(errorMessage, translate("The XMILE dimensions are invalid or conflict with a variable."));
            return false;
        }
    }
    return false;
}

bool parseXmileVariableDimensions(QXmlStreamReader &reader, std::vector<std::string> &dimensions)
{
    while (reader.readNext() != QXmlStreamReader::Invalid) {
        if (reader.isEndElement() && reader.name() == QStringLiteral("dimensions"))
            return !dimensions.empty();
        if (reader.isStartElement() && reader.name() == QStringLiteral("dim")) {
            dimensions.push_back(reader.attributes().value(QStringLiteral("name")).toString().toStdString());
            reader.skipCurrentElement();
        }
    }
    return false;
}

bool parseXmileArrayElement(QXmlStreamReader &reader, const core::Document &document,
    const std::vector<std::string> &dimensions, core::ArrayElementEquation &entry)
{
    const QStringList indices = splitSubscripts(reader.attributes().value(QStringLiteral("subscript")).toString());
    if (indices.size() != static_cast<int>(dimensions.size()))
        return false;
    for (int position = 0; position < indices.size(); ++position) {
        const core::Dimension *dimension = document.findDimension(dimensions[position]);
        if (!dimension)
            return false;
        bool numeric = false;
        const int number = indices[position].toInt(&numeric);
        if (numeric) {
            if (number <= 0 || static_cast<std::size_t>(number) > dimension->elements.size())
                return false;
            entry.indices.push_back(static_cast<std::size_t>(number - 1));
            continue;
        }
        const std::string name = referenceName(indices[position]).toStdString();
        const core::DimensionElement *element = dimension->findElement(name);
        if (!element)
            return false;
        entry.indices.push_back(static_cast<std::size_t>(element - dimension->elements.data()));
    }
    bool foundEquation = false;
    while (reader.readNext() != QXmlStreamReader::Invalid) {
        if (reader.isEndElement() && reader.name() == QStringLiteral("element"))
            return foundEquation;
        if (reader.isStartElement() && reader.name() == QStringLiteral("eqn")) {
            if (foundEquation)
                return false;
            entry.equation = reader.readElementText().trimmed().toStdString();
            foundEquation = true;
        } else if (reader.isStartElement()) {
            reader.skipCurrentElement();
        }
    }
    return false;
}

bool parseVariable(
    QXmlStreamReader &reader,
    core::Document &model,
    std::vector<core::ElementData> &pendingFlows,
    std::vector<core::ElementData> &pendingAuxiliaries,
    std::vector<PendingFlowEndpoint> &pendingFlowEndpoints,
    QString *errorMessage)
{
    const QString tag = reader.name().toString();
    const QString name = reader.attributes().value(QStringLiteral("name")).toString().trimmed();
    if (name.isEmpty()) {
        setError(errorMessage, translate("A model variable has no name."));
        return false;
    }

    core::ElementData variable;
    variable.displayName = name.toStdString();
    if (tag == QStringLiteral("stock"))
        variable.kind = core::ElementKind::Stock;
    else if (tag == QStringLiteral("flow")) {
        variable.kind = core::ElementKind::Flow;
        variable.route.mode = core::RouteMode::Straight;
    } else {
        variable.kind = core::ElementKind::Auxiliary;
    }

    QStringList inflows;
    QStringList outflows;
    while (reader.readNext() != QXmlStreamReader::Invalid) {
        if (reader.isEndElement() && reader.name().toString() == tag)
            break;
        if (!reader.isStartElement())
            continue;
        const QString child = reader.name().toString();
        if (child == QStringLiteral("eqn"))
            variable.equation = reader.readElementText().trimmed().toStdString();
        else if (child == QStringLiteral("dimensions")) {
            if (!parseXmileVariableDimensions(reader, variable.dimensions)) {
                setError(errorMessage, translate("An array variable has invalid dimensions: %1").arg(name));
                return false;
            }
        } else if (child == QStringLiteral("element")) {
            core::ArrayElementEquation entry;
            if (!parseXmileArrayElement(reader, model, variable.dimensions, entry)) {
                setError(errorMessage, translate("An array element is invalid: %1").arg(name));
                return false;
            }
            variable.elementEquations.push_back(std::move(entry));
        } else if (child == QStringLiteral("units"))
            variable.unit = reader.readElementText().trimmed().toStdString();
        else if (child == QStringLiteral("doc"))
            variable.comment = reader.readElementText().toStdString();
        else if (child == QStringLiteral("inflow"))
            inflows.append(referenceName(reader.readElementText()));
        else if (child == QStringLiteral("outflow"))
            outflows.append(referenceName(reader.readElementText()));
        else
            reader.skipCurrentElement();
    }

    if (!variable.elementEquations.empty()) {
        std::size_t expected = 1;
        for (const std::string &dimension : variable.dimensions) {
            const core::Dimension *definition = model.findDimension(dimension);
            if (!definition) {
                setError(errorMessage, translate("An array references an undefined dimension: %1").arg(name));
                return false;
            }
            expected *= definition->elements.size();
        }
        if (variable.elementEquations.size() != expected || !variable.equation.empty()) {
            setError(errorMessage, translate("A non-apply-to-all array is incomplete: %1").arg(name));
            return false;
        }
    }

    if (variable.kind == core::ElementKind::Flow) {
        pendingFlows.push_back(std::move(variable));
    } else if (variable.kind == core::ElementKind::Auxiliary) {
        pendingAuxiliaries.push_back(std::move(variable));
    } else if (!model.restore(variable)) {
        setError(errorMessage, translate("The model contains a duplicate variable name: %1").arg(name));
        return false;
    }
    for (const QString &flow : inflows)
        pendingFlowEndpoints.push_back({flow.toStdString(), variable.displayName, true});
    for (const QString &flow : outflows)
        pendingFlowEndpoints.push_back({flow.toStdString(), variable.displayName, false});
    return true;
}

bool parseVariables(
    QXmlStreamReader &reader,
    core::Document &model,
    std::vector<core::ElementData> &pendingFlows,
    std::vector<core::ElementData> &pendingAuxiliaries,
    std::vector<PendingFlowEndpoint> &pendingFlowEndpoints,
    QString *errorMessage)
{
    while (reader.readNext() != QXmlStreamReader::Invalid) {
        if (reader.isEndElement() && reader.name() == QStringLiteral("variables")) {
            for (core::ElementData &auxiliary : pendingAuxiliaries) {
                const bool usedAsFlow = std::any_of(
                    pendingFlowEndpoints.begin(), pendingFlowEndpoints.end(), [&auxiliary](const PendingFlowEndpoint &endpoint) {
                        return core::normalizeIdentifier(endpoint.flow) == core::normalizeIdentifier(auxiliary.displayName);
                    });
                if (usedAsFlow) {
                    auxiliary.kind = core::ElementKind::Flow;
                    auxiliary.route.mode = core::RouteMode::Straight;
                    pendingFlows.push_back(std::move(auxiliary));
                } else if (!model.restore(auxiliary)) {
                    setError(errorMessage, translate("The model contains a duplicate variable name: %1")
                        .arg(QString::fromStdString(auxiliary.displayName)));
                    return false;
                }
            }
            return true;
        }
        if (!reader.isStartElement())
            continue;
        const QString tag = reader.name().toString();
        if (tag == QStringLiteral("stock") || tag == QStringLiteral("flow") || tag == QStringLiteral("aux")) {
            if (!parseVariable(reader, model, pendingFlows, pendingAuxiliaries, pendingFlowEndpoints, errorMessage))
                return false;
        } else if (tag == QStringLiteral("module")) {
            setError(errorMessage, translate("XMILE modules are not supported."));
            return false;
        } else {
            reader.skipCurrentElement();
        }
    }
    return true;
}

void parseConnectorEndpoint(QXmlStreamReader &reader, PendingConnection &pending, bool from)
{
    QString name;
    quint64 uid = 0;
    const QString tag = reader.name().toString();
    while (reader.readNext() != QXmlStreamReader::Invalid) {
        if (reader.isEndElement() && reader.name().toString() == tag)
            break;
        if (reader.isCharacters())
            name += reader.text().toString();
        if (!reader.isStartElement())
            continue;
        const QXmlStreamAttributes attributes = reader.attributes();
        if (attributes.hasAttribute(QStringLiteral("uid")))
            uid = attributes.value(QStringLiteral("uid")).toULongLong();
        if (attributes.hasAttribute(QStringLiteral("name")))
            name = attributes.value(QStringLiteral("name")).toString();
        name += reader.readElementText(QXmlStreamReader::IncludeChildElements);
    }
    if (from) {
        pending.from = referenceName(name).toStdString();
        pending.fromUid = uid;
    } else {
        pending.to = referenceName(name).toStdString();
        pending.toUid = uid;
    }
}

void parseConnector(QXmlStreamReader &reader, std::vector<PendingConnection> &pendingConnections)
{
    PendingConnection pending;
    pending.from = reader.attributes().value(QStringLiteral("from")).toString().toStdString();
    pending.to = reader.attributes().value(QStringLiteral("to")).toString().toStdString();
    while (reader.readNext() != QXmlStreamReader::Invalid) {
        if (reader.isEndElement() && reader.name() == QStringLiteral("connector"))
            break;
        if (!reader.isStartElement())
            continue;
        if (reader.name() == QStringLiteral("from"))
            parseConnectorEndpoint(reader, pending, true);
        else if (reader.name() == QStringLiteral("to"))
            parseConnectorEndpoint(reader, pending, false);
        else if (reader.name() == QStringLiteral("pts")) {
            reader.skipCurrentElement();
        } else {
            reader.skipCurrentElement();
        }
    }
    if ((!pending.from.empty() || pending.fromUid != 0) && (!pending.to.empty() || pending.toUid != 0))
        pendingConnections.push_back(std::move(pending));
}

void parseView(
    QXmlStreamReader &reader,
    core::Document &model,
    std::vector<PendingConnection> &pendingConnections,
    std::vector<PendingFlowLayout> &pendingFlowLayouts,
    std::unordered_set<core::EntityId> &positioned,
    UidMap &uids)
{
    while (reader.readNext() != QXmlStreamReader::Invalid) {
        if (reader.isEndElement() && reader.name() == QStringLiteral("view"))
            return;
        if (!reader.isStartElement())
            continue;

        const QString tag = reader.name().toString();
        if (tag == QStringLiteral("stock") || tag == QStringLiteral("flow") || tag == QStringLiteral("aux")
            || tag == QStringLiteral("cloud") || tag == QStringLiteral("text_box")) {
            const QXmlStreamAttributes attributes = reader.attributes();
            const std::string name = attributes.value(QStringLiteral("name")).toString().toStdString();
            const core::DocumentElement *variable = findElementByName(model, name);
            if (!variable && (tag == QStringLiteral("cloud") || tag == QStringLiteral("text_box"))) {
                core::EntityId created = 0;
                if (tag == QStringLiteral("text_box")) {
                    QString content = reader.readElementText(QXmlStreamReader::IncludeChildElements);
                    if (content.isEmpty())
                        content = attributes.value(QStringLiteral("name")).toString();
                    created = model.createComment(content.toStdString());
                } else {
                    created = model.createCloud();
                }
                variable = model.find(created);
                const quint64 uid = attributes.value(QStringLiteral("uid")).toULongLong();
                if (created != 0 && uid != 0)
                    uids[uid] = created;
            }
            bool xOk = false;
            bool yOk = false;
            const double x = attributes.value(QStringLiteral("x")).toDouble(&xOk);
            const double y = attributes.value(QStringLiteral("y")).toDouble(&yOk);
            if (variable && xOk && yOk) {
                model.setPosition(variable->id(), {x, y});
                if (tag == QStringLiteral("stock") || tag == QStringLiteral("aux"))
                    positioned.insert(variable->id());
            }
            if (tag == QStringLiteral("flow")) {
                PendingFlowLayout pending;
                pending.flow = name;
                pending.valveCenter = {x, y};
                pending.hasValveCenter = xOk && yOk;
                while (reader.readNext() != QXmlStreamReader::Invalid) {
                    if (reader.isEndElement() && reader.name() == QStringLiteral("flow"))
                        break;
                    if (!reader.isStartElement() || reader.name() != QStringLiteral("pts"))
                        continue;
                    while (reader.readNext() != QXmlStreamReader::Invalid) {
                        if (reader.isEndElement() && reader.name() == QStringLiteral("pts"))
                            break;
                        if (!reader.isStartElement() || reader.name() != QStringLiteral("pt"))
                            continue;
                        bool pointXOk = false;
                        bool pointYOk = false;
                        const double pointX = reader.attributes().value(QStringLiteral("x")).toDouble(&pointXOk);
                        const double pointY = reader.attributes().value(QStringLiteral("y")).toDouble(&pointYOk);
                        if (pointXOk && pointYOk)
                            pending.anchorPoints.push_back({pointX, pointY});
                    }
                }
                pendingFlowLayouts.push_back(std::move(pending));
            }
        } else if (tag == QStringLiteral("connector")) {
            parseConnector(reader, pendingConnections);
        }
    }
}

void layoutMissingVariables(core::Document &document, const std::unordered_set<core::EntityId> &positioned)
{
    std::vector<const core::ModelElement *> missing;
    for (const core::ModelElement &element : document.model().elements()) {
        if (element.kind() != core::ElementKind::Flow && !positioned.count(element.id()))
            missing.push_back(&element);
    }
    std::sort(missing.begin(), missing.end(), [](const core::ModelElement *left, const core::ModelElement *right) {
        return left->id() < right->id();
    });
    for (std::size_t index = 0; index < missing.size(); ++index) {
        document.setPosition(
            missing[index]->id(),
            {100.0 + static_cast<int>(index % 5) * 180.0,
             100.0 + static_cast<int>(index / 5) * 140.0});
    }
}

const PendingFlowLayout *findFlowLayout(const std::vector<PendingFlowLayout> &routes, const std::string &name)
{
    const auto found = std::find_if(routes.begin(), routes.end(), [&name](const PendingFlowLayout &route) {
        return core::normalizeIdentifier(route.flow) == core::normalizeIdentifier(name);
    });
    return found == routes.end() ? nullptr : &*found;
}

bool createImportedFlows(
    core::Document &document,
    const std::vector<core::ElementData> &flows,
    const std::vector<PendingFlowEndpoint> &endpoints,
    const std::vector<PendingFlowLayout> &routes,
    QString *errorMessage)
{
    for (const PendingFlowEndpoint &endpoint : endpoints) {
        const auto found = std::find_if(flows.begin(), flows.end(), [&endpoint](const core::ElementData &flow) {
            return core::normalizeIdentifier(flow.displayName) == core::normalizeIdentifier(endpoint.flow);
        });
        if (found == flows.end() || !document.model().findByName(endpoint.stock)) {
            setError(errorMessage, translate("A stock references an undefined flow or stock."));
            return false;
        }
    }

    std::size_t flowIndex = 0;
    for (const core::ElementData &source : flows) {
        core::EntityId fromStock = 0;
        core::EntityId toStock = 0;
        for (const PendingFlowEndpoint &endpoint : endpoints) {
            if (core::normalizeIdentifier(endpoint.flow) != core::normalizeIdentifier(source.displayName))
                continue;
            const core::ModelElement *stock = document.model().findByName(endpoint.stock);
            if (!stock || stock->kind() != core::ElementKind::Stock) {
                setError(errorMessage, translate("A flow endpoint is not a stock: %1")
                    .arg(QString::fromStdString(endpoint.stock)));
                return false;
            }
            core::EntityId &slot = endpoint.inflow ? toStock : fromStock;
            if (slot != 0 && slot != stock->id()) {
                setError(errorMessage, translate("A flow has multiple source or target stocks: %1")
                    .arg(QString::fromStdString(source.displayName)));
                return false;
            }
            slot = stock->id();
        }

        const PendingFlowLayout *view = findFlowLayout(routes, source.displayName);
        const std::vector<core::Vec2> points = view ? view->anchorPoints : std::vector<core::Vec2> {};
        const bool hasPath = points.size() >= 2;
        core::Vec2 fromPosition;
        core::Vec2 toPosition;
        if (fromStock != 0) {
            fromPosition = document.diagram().position(fromStock);
        } else if (hasPath) {
            fromPosition = points.front();
        } else if (toStock != 0) {
            fromPosition = document.diagram().position(toStock) + core::Vec2 {-160.0, 0.0};
        } else if (view && view->hasValveCenter) {
            fromPosition = view->valveCenter + core::Vec2 {-80.0, 0.0};
        } else {
            fromPosition = {100.0 + static_cast<int>(flowIndex % 5) * 220.0,
                            100.0 + static_cast<int>(flowIndex / 5) * 180.0};
        }
        if (toStock != 0) {
            toPosition = document.diagram().position(toStock);
        } else if (hasPath) {
            toPosition = points.back();
        } else if (fromStock != 0) {
            toPosition = fromPosition + core::Vec2 {160.0, 0.0};
        } else if (view && view->hasValveCenter) {
            toPosition = view->valveCenter + core::Vec2 {80.0, 0.0};
        } else {
            toPosition = fromPosition + core::Vec2 {160.0, 0.0};
        }

        const core::EntityId from = fromStock != 0 ? fromStock : document.createCloud(fromPosition);
        const core::EntityId to = toStock != 0 ? toStock : document.createCloud(toPosition);
        const core::EntityId id = document.createFlow(from, to);
        if (id == 0) {
            setError(errorMessage, translate("Could not create flow endpoints for: %1")
                .arg(QString::fromStdString(source.displayName)));
            return false;
        }
        core::ElementData target = source;
        target.id = id;
        target.flowEndpoints = {from, to};
        target.position = {(fromPosition.x + toPosition.x) / 2.0, (fromPosition.y + toPosition.y) / 2.0};
        target.route = core::Route();
        target.route.mode = core::RouteMode::Straight;
        if (!document.apply(target)) {
            setError(errorMessage, translate("The model contains a duplicate or invalid flow: %1")
                .arg(QString::fromStdString(source.displayName)));
            return false;
        }
        ++flowIndex;
    }
    return true;
}

bool parseXml(
    const QByteArray &xml,
    core::Document &model,
    UidMap &uids,
    QString *errorMessage)
{
    QXmlStreamReader reader(xml);
    reader.setNamespaceProcessing(false);
    core::Document parsed;
    std::vector<PendingConnection> pendingConnections;
    std::vector<core::ElementData> pendingFlows;
    std::vector<core::ElementData> pendingAuxiliaries;
    std::vector<PendingFlowEndpoint> pendingFlowEndpoints;
    std::vector<PendingFlowLayout> pendingFlowLayouts;
    std::unordered_set<core::EntityId> positioned;
    bool foundRoot = false;
    bool foundModel = false;
    bool foundView = false;

    while (!reader.atEnd()) {
        reader.readNext();
        if (!reader.isStartElement())
            continue;
        const QString tag = reader.name().toString();
        if (tag == QStringLiteral("xmile")) {
            foundRoot = true;
        } else if (tag == QStringLiteral("macro")) {
            if (!parseXmileMacro(reader, parsed, errorMessage))
                return false;
        } else if (tag == QStringLiteral("dimensions")) {
            if (!parseXmileDimensions(reader, parsed, errorMessage))
                return false;
        } else if (tag == QStringLiteral("model")) {
            if (foundModel) {
                setError(errorMessage, translate("Multiple XMILE models are not supported."));
                return false;
            }
            foundModel = true;
        } else if (tag == QStringLiteral("variables")) {
            if (!parseVariables(reader, parsed, pendingFlows, pendingAuxiliaries, pendingFlowEndpoints, errorMessage))
                return false;
        } else if (tag == QStringLiteral("view") && !foundView) {
            foundView = true;
            parseView(reader, parsed, pendingConnections, pendingFlowLayouts, positioned, uids);
        }
    }

    if (reader.hasError()) {
        setError(
            errorMessage,
            translate("Invalid XMILE XML at line %1: %2").arg(reader.lineNumber()).arg(reader.errorString()));
        return false;
    }
    if (!foundRoot) {
        setError(errorMessage, translate("The document is not an XMILE model."));
        return false;
    }

    layoutMissingVariables(parsed, positioned);
    if (!createImportedFlows(parsed, pendingFlows, pendingFlowEndpoints, pendingFlowLayouts, errorMessage))
        return false;

    for (const PendingConnection &connection : pendingConnections) {
        const core::DocumentElement *from = connection.fromUid != 0
            ? parsed.find(uids[connection.fromUid])
            : findElementByName(parsed, connection.from);
        const core::DocumentElement *to = connection.toUid != 0
            ? parsed.find(uids[connection.toUid])
            : findElementByName(parsed, connection.to);
        if (!from || !to)
            continue;
        core::EntityId id = parsed.createConnector(from->id(), to->id());
        if (id == 0) {
            for (const core::ViewElement &element : parsed.diagram().elements()) {
                if (element.kind() != core::ElementKind::Connector)
                    continue;
                const auto &existing = static_cast<const core::Connector &>(element);
                if (existing.from() == from->id() && existing.to() == to->id()) {
                    id = existing.id();
                    break;
                }
            }
        }
    }

    model = std::move(parsed);
    return true;
}


bool readFileBytes(const QString &fileName, QByteArray &contents, QString *errorMessage)
{
    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly)) {
        setError(errorMessage, translate("Could not open the file for reading: %1").arg(file.errorString()));
        return false;
    }
    contents = file.readAll();
    if (file.error() != QFileDevice::NoError) {
        setError(errorMessage, translate("Could not read the file: %1").arg(file.errorString()));
        return false;
    }
    return true;
}

bool writeFileBytes(const QString &fileName, const QByteArray &contents, QString *errorMessage)
{
    QSaveFile file(fileName);
    if (!file.open(QIODevice::WriteOnly)) {
        setError(errorMessage, translate("Could not open the file for writing: %1").arg(file.errorString()));
        return false;
    }
    if (file.write(contents) != contents.size() || !file.commit()) {
        setError(errorMessage, translate("Could not write the file: %1").arg(file.errorString()));
        return false;
    }
    return true;
}

} // namespace

bool XmileFile::importFile(const QString &fileName, core::Document &document, QString *errorMessage)
{
    QByteArray xml;
    if (!readFileBytes(fileName, xml, errorMessage))
        return false;
    if (xml.startsWith("PK")) {
        setError(errorMessage, translate("XMILE files must be uncompressed XML, not ZIP containers."));
        return false;
    }
    UidMap uids;
    return parseXml(xml, document, uids, errorMessage);
}

bool XmileFile::exportFile(const QString &fileName, const core::Document &document, QString *errorMessage)
{
    return writeFileBytes(fileName, writeXml(document), errorMessage);
}

} // namespace qds::io
